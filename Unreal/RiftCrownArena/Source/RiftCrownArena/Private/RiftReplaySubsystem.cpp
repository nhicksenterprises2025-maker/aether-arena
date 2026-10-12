#include "RiftReplaySubsystem.h"
#include "Async/Async.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/Compression.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "RiftDiagnostics.h"
#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "Serialization/JsonSerializer.h"
#include <limits>
#include <type_traits>

struct FRiftReplayWriteJob {
    FString Filename, Path, Error;
    TFuture<void> Work;
    bool Saved = false;
    double ValidateMS = 0, SerializeMS = 0, CompressMS = 0, WriteMS = 0;
    int32 Events = 0, Samples = 0, Characters = 0, StoredBytes = 0;
};

struct FRiftReplayBalanceContext {
    bool HasCapturedDefinitions = false;
    int Schema = 0;
    FString ManifestHash, ManifestVersion;
    std::map<std::string, rift::Card> Definitions;
    std::map<std::string, double> BuildingDecayRates;
    TSet<FString> PlayableIds, Limitations;
    TMap<uint64, rift::Entity> Entities;
    TSet<uint64> ProjectileIds,WaveIds;
    TArray<rift::Projectile> Projectiles;
    TArray<rift::Shockwave> Waves;
    TMap<uint64, rift::Hazard> Hazards;
    TMap<uint64, double> HazardIntervals;
};

namespace {
constexpr int64 ReplayLimit = 64 * 1024 * 1024;
constexpr int32 DecodedLimit = 256 * 1024 * 1024, ArchiveHeaderSize = 24;
constexpr uint8 ArchiveMagic[] = {'R', 'I', 'F', 'T', 'R', 'E', 'P', '1'};
constexpr double MaxTime = 3600;
const TCHAR* ReplaySizeError = TEXT("Replay exceeds the 256 MB decoded JSON limit.");

// Count a lower bound without formatting numbers or allocating a second JSON document.
// Every field is included, including persistent per-body telemetry. UTF-8 strings,
// escaping and multi-digit numbers can only make the final document larger.
bool MinimumJSONValueBytes(const TSharedPtr<FJsonValue>& Value, int64& Bytes, int64 Limit, int Depth);
bool AddMinimumBytes(int64 Amount, int64& Bytes, int64 Limit) {
    if (Amount < 0 || Amount > Limit - Bytes) return false;
    Bytes += Amount;
    return true;
}
bool MinimumJSONObjectBytes(const TSharedPtr<FJsonObject>& Object, int64& Bytes, int64 Limit, int Depth=0) {
    if (!Object || Depth > 64 || !AddMinimumBytes(2, Bytes, Limit)) return false;
    bool First=true;
    for (const auto& Pair : Object->Values) {
        if (!AddMinimumBytes(int64(Pair.Key.Len())+3+(First?0:1),Bytes,Limit) ||
            !MinimumJSONValueBytes(Pair.Value,Bytes,Limit,Depth+1)) return false;
        First=false;
    }
    return true;
}
bool MinimumJSONValueBytes(const TSharedPtr<FJsonValue>& Value, int64& Bytes, int64 Limit, int Depth) {
    if (!Value || Depth > 64) return false;
    switch (Value->Type) {
    case EJson::Object: return MinimumJSONObjectBytes(Value->AsObject(),Bytes,Limit,Depth);
    case EJson::Array: {
        if (!AddMinimumBytes(2,Bytes,Limit)) return false;
        bool First=true;
        for (const auto& Element : Value->AsArray()) {
            if ((!First&&!AddMinimumBytes(1,Bytes,Limit)) ||
                !MinimumJSONValueBytes(Element,Bytes,Limit,Depth+1)) return false;
            First=false;
        }
        return true;
    }
    case EJson::String: return AddMinimumBytes(int64(Value->AsString().Len())+2,Bytes,Limit);
    case EJson::Number: return AddMinimumBytes(1,Bytes,Limit);
    case EJson::Boolean: return AddMinimumBytes(Value->AsBool()?4:5,Bytes,Limit);
    case EJson::Null: return AddMinimumBytes(4,Bytes,Limit);
    default: return false;
    }
}

// The normal FString writer internally grows a 32-bit FMemoryWriter before the
// compression limit is checked. Write UTF-8 directly, with a hard allocation cap.
class FRiftBoundedReplayWriter final : public FArchive {
public:
    FRiftBoundedReplayWriter(TArray<uint8>& InBytes,int64 InLimit):Bytes(InBytes),Limit(InLimit) {
        SetIsSaving(true);
    }
    void Serialize(void* Data,int64 Length) override {
        if (IsError()) return;
        if (Length<0 || Length>Limit-Bytes.Num()) {SetError();return;}
        if (Length>0) {
            const int64 Required=Bytes.Num()+Length;
            if (Required>Bytes.Max()) Bytes.Reserve(int32(FMath::Min(Limit,FMath::Max(Required,FMath::Max<int64>(65536,int64(Bytes.Max())*2)))));
            Bytes.Append(static_cast<const uint8*>(Data),int32(Length));
        }
    }
private:
    TArray<uint8>& Bytes;
    int64 Limit;
};
void ArchiveUInt32(uint8 *At, uint32 Value) {
    for (int I = 0; I < 4; ++I)
        At[I] = uint8(Value >> (8 * I));
}
uint32 ReadArchiveUInt32(const uint8 *At) {
    uint32 Value = 0;
    for (int I = 0; I < 4; ++I)
        Value |= uint32(At[I]) << (8 * I);
    return Value;
}
bool EncodeRecording(TConstArrayView<uint8> Raw, TArray<uint8> &Archive, FString &Error) {
    const int32 RawBytes = Raw.Num();
    if (RawBytes <= 0 || RawBytes > DecodedLimit) {
        Error = TEXT("Replay exceeds the 256 MB decoded JSON limit.");
        return false;
    }
    int32 CompressedBytes = FCompression::CompressMemoryBound(NAME_Zlib, RawBytes);
    if (CompressedBytes <= 0) {
        Error = TEXT("Replay compression is unavailable.");
        return false;
    }
    Archive.SetNumUninitialized(ArchiveHeaderSize + CompressedBytes);
    if (!FCompression::CompressMemory(NAME_Zlib, Archive.GetData() + ArchiveHeaderSize, CompressedBytes,
                                      Raw.GetData(), RawBytes) ||
        int64(CompressedBytes) + ArchiveHeaderSize > ReplayLimit) {
        Archive.Reset();
        Error = TEXT("Replay compression failed or exceeds the 64 MB native archive limit.");
        return false;
    }
    Archive.SetNum(ArchiveHeaderSize + CompressedBytes, EAllowShrinking::No);
    FMemory::Memcpy(Archive.GetData(), ArchiveMagic, UE_ARRAY_COUNT(ArchiveMagic));
    ArchiveUInt32(Archive.GetData() + 8, uint32(RawBytes));
    ArchiveUInt32(Archive.GetData() + 12, FCrc::MemCrc32(Raw.GetData(), RawBytes));
    ArchiveUInt32(Archive.GetData() + 16, uint32(CompressedBytes));
    ArchiveUInt32(Archive.GetData() + 20,
                  FCrc::MemCrc32(Archive.GetData() + ArchiveHeaderSize, CompressedBytes));
    Error.Empty();
    return true;
}
bool EncodeRecording(const FString &Text, TArray<uint8> &Archive, FString &Error) {
    FTCHARToUTF8 UTF8(*Text);
    return EncodeRecording(TConstArrayView<uint8>(reinterpret_cast<const uint8*>(UTF8.Get()),UTF8.Length()),Archive,Error);
}
FString FS(const std::string &S) {
    return UTF8_TO_TCHAR(S.c_str());
}
TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonValue> &V) {
    const TSharedPtr<FJsonObject> *O = nullptr;
    return V && V->TryGetObject(O) ? *O : nullptr;
}
double N(const TSharedPtr<FJsonObject> &O, const TCHAR *Key, double Default = 0) {
    double V = Default;
    return O && O->TryGetNumberField(Key, V) && FMath::IsFinite(V) ? V : Default;
}
FString S(const TSharedPtr<FJsonObject> &O, const TCHAR *Key) {
    FString V;
    if (O)
        O->TryGetStringField(Key, V);
    return V;
}
void Point(const TSharedRef<FJsonObject> &O, const TCHAR *Key, rift::Vec2 P) {
    auto V = MakeShared<FJsonObject>();
    V->SetNumberField(TEXT("x"), P.x);
    V->SetNumberField(TEXT("z"), P.z);
    O->SetObjectField(Key, V);
}
TArray<TSharedPtr<FJsonValue>> Strings(const std::vector<std::string> &Values) {
    TArray<TSharedPtr<FJsonValue>> A;
    for (const auto &V : Values)
        A.Add(MakeShared<FJsonValueString>(FS(V)));
    return A;
}
const rift::Card* RecordedCard(const FRiftReplayBalanceContext* Context, const std::string& Id) {
    if (!Context || !Context->HasCapturedDefinitions) return nullptr;
    const auto Found=Context->Definitions.find(Id);
    return Found==Context->Definitions.end()?nullptr:&Found->second;
}
bool KnownCard(const FRiftReplayBalanceContext* Context,const std::string& Id) {
    return Context && Context->HasCapturedDefinitions ? RecordedCard(Context,Id)!=nullptr : rift::FindEntityDefinition(Id)!=nullptr;
}
bool PlayableCard(const FRiftReplayBalanceContext* Context,const std::string& Id) {
    return Context && Context->HasCapturedDefinitions ? Context->PlayableIds.Contains(UTF8_TO_TCHAR(Id.c_str())) : rift::FindCard(Id)!=nullptr;
}
bool SpellCard(const FRiftReplayBalanceContext* Context,const std::string& Id) {
    const auto* Card=Context && Context->HasCapturedDefinitions ? RecordedCard(Context,Id) : rift::FindEntityDefinition(Id);
    return Card&&Card->spell;
}
bool RecordedDeck(const FRiftReplayBalanceContext* Context,const std::vector<std::string>& Deck) {
    if (Deck.size()!=8) return false;
    TSet<FString> Unique;
    for (const auto& Id:Deck) {if (!PlayableCard(Context,Id)||Unique.Contains(UTF8_TO_TCHAR(Id.c_str()))) return false;Unique.Add(UTF8_TO_TCHAR(Id.c_str()));}
    return true;
}
void MissingMechanic(FRiftReplayBalanceContext* Context,const TCHAR* Detail) {
    if (Context && !Context->HasCapturedDefinitions) Context->Limitations.Add(Detail);
}
struct Reader {
    TSharedPtr<FJsonObject> O;
    const FRiftReplayBalanceContext* Context=nullptr;
    bool Valid = true;
    explicit Reader(TSharedPtr<FJsonObject> Object,const FRiftReplayBalanceContext* Balance=nullptr)
        : O(Object), Context(Balance), Valid(Object.IsValid()) {}
    template <class T>
    void Number(const TCHAR *Key, T &Out, double Min = -1e12, double Max = 1e12, bool Required = false) {
        if (!O) {
            Valid = false;
            return;
        }
        if (!O->HasField(Key)) {
            if (Required)
                Valid = false;
            return;
        }
        double V = 0;
        if (!O->TryGetNumberField(Key, V) || !FMath::IsFinite(V) || V < Min || V > Max) {
            Valid = false;
            return;
        }
        if constexpr (std::is_integral_v<T>) {
            if (V != FMath::FloorToDouble(V) || V < double(std::numeric_limits<T>::lowest()) ||
                V > double(std::numeric_limits<T>::max())) {
                Valid = false;
                return;
            }
        }
        if constexpr (std::is_enum_v<T>) {
            if (V != FMath::FloorToDouble(V)) {
                Valid = false;
                return;
            }
        }
        Out = static_cast<T>(V);
    }
    void Text(const TCHAR *Key, std::string &Out, bool Required = false) {
        if (!O) {
            Valid = false;
            return;
        }
        if (!O->HasField(Key)) {
            if (Required)
                Valid = false;
            return;
        }
        FString V;
        if (!O->TryGetStringField(Key, V) || V.Len() > 1024) {
            Valid = false;
            return;
        }
        Out = TCHAR_TO_UTF8(*V);
    }
    void Flag(const TCHAR *Key, bool &Out) {
        if (O && O->HasField(Key) && !O->TryGetBoolField(Key, Out))
            Valid = false;
    }
    void Vec(const TCHAR *Key, rift::Vec2 &Out, bool Required = false) {
        if (!O) {
            Valid = false;
            return;
        }
        if (!O->HasField(Key)) {
            if (Required)
                Valid = false;
            return;
        }
        const TSharedPtr<FJsonObject> *P = nullptr;
        if (!O->TryGetObjectField(Key, P)) {
            Valid = false;
            return;
        }
        Reader R(*P);
        R.Number(TEXT("x"), Out.x, -1e6, 1e6, true);
        R.Number(TEXT("z"), Out.z, -1e6, 1e6, true);
        Valid &= R.Valid;
    }
    bool List(const TCHAR *Key, const TArray<TSharedPtr<FJsonValue>> *&A, int32 Limit,
              bool Required = false) {
        A = nullptr;
        if (!O) {
            Valid = false;
            return false;
        }
        if (!O->HasField(Key)) {
            if (Required)
                Valid = false;
            return false;
        }
        if (!O->TryGetArrayField(Key, A) || A->Num() > Limit) {
            Valid = false;
            return false;
        }
        return true;
    }
    void CardList(const TCHAR *Key, std::vector<std::string> &Out, int32 RequiredCount = -1,
                  int32 Limit = 32) {
        const TArray<TSharedPtr<FJsonValue>> *A = nullptr;
        if (!List(Key, A, Limit, RequiredCount >= 0))
            return;
        if (RequiredCount >= 0 && A->Num() != RequiredCount) {
            Valid = false;
            return;
        }
        Out.clear();
        for (const auto &V : *A) {
            FString Id;
            if (!V || !V->TryGetString(Id) || !PlayableCard(Context,TCHAR_TO_UTF8(*Id))) {
                Valid = false;
                return;
            }
            Out.emplace_back(TCHAR_TO_UTF8(*Id));
        }
    }
};
#define CARD_NUMBERS(X) \
    X(cost) X(count) X(hp) X(damage) X(attackInterval) X(moveSpeed) X(range) X(scale) \
    X(projectileSpeed) X(splash) X(lifetime) X(footprint) X(shockwaveSpeed) X(collisionRadius) X(targetMask) \
    X(towerDamage) X(spellRadius) X(chargeDamage) X(castDelay) X(slowPct) X(slowDuration) \
    X(auraDamage) X(auraInterval) X(auraRadius) X(stunDuration) X(dotDamage) X(dotDuration) X(dotInterval) X(rounds)
#define CARD_FLAGS(X) X(flying) X(canHitAir) X(structuresOnly) X(spell) X(building)
#define EXPANSION_CARD_NUMBERS(X) X(chargeMoveSeconds) X(chargedMoveSpeed) X(initialAttackCooldown) X(shieldHP) X(deathSpawnCount) X(invisibilityDuration) X(dotStructureDamage)
#define EXPANSION_CARD_FLAGS(X) X(hardExpiry) X(shieldOverflowProtected) X(globalFriendlyInvisibility) X(breakExistingLocks)
TSharedRef<FJsonObject> BalanceJSON() {
    auto Balance=MakeShared<FJsonObject>();
    Balance->SetNumberField(TEXT("schema"),2);
    Balance->SetStringField(TEXT("manifestHash"),FS(rift::CardManifestHash()));
    Balance->SetStringField(TEXT("manifestVersion"),FS(rift::CardManifestVersion()));
    TArray<TSharedPtr<FJsonValue>> Cards;
    auto Add=[&](const rift::Card& Card,bool Playable) {
        auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("id"),FS(Card.id));C->SetStringField(TEXT("name"),FS(Card.name));C->SetBoolField(TEXT("playable"),Playable);
        C->SetNumberField(TEXT("hpDecayPerSecond"),Card.hpDecayPerSecond);
        C->SetStringField(TEXT("deathSpawnId"),FS(Card.deathSpawnId));
#define FIELD(K) C->SetNumberField(TEXT(#K),double(Card.K));
        CARD_NUMBERS(FIELD)
        EXPANSION_CARD_NUMBERS(FIELD)
#undef FIELD
#define FIELD(K) C->SetBoolField(TEXT(#K),Card.K);
        CARD_FLAGS(FIELD)
        EXPANSION_CARD_FLAGS(FIELD)
#undef FIELD
        Cards.Add(MakeShared<FJsonValueObject>(C));
    };
    for (const auto& Card:rift::Cards()) Add(Card,true);
    for (const auto& Card:rift::EntityDefinitions()) Add(Card,false);
    Balance->SetArrayField(TEXT("cards"),Cards);
    return Balance;
}
bool ReadBalance(const TSharedPtr<FJsonObject>& Recording,FRiftReplayBalanceContext& Context) {
    if (!Recording) return false;
    if (!Recording->HasField(TEXT("balance"))) return true;
    const TSharedPtr<FJsonObject>* B=nullptr;
    if (!Recording->TryGetObjectField(TEXT("balance"),B)) return false;
    Reader R(*B);int Schema=0;std::string Hash,Version;
    R.Number(TEXT("schema"),Schema,1,2,true);R.Text(TEXT("manifestHash"),Hash,true);R.Text(TEXT("manifestVersion"),Version,true);
    if (Hash.size()!=64||Version.empty()||Version.size()>128) return false;
    for (char C:Hash) if (!(C>='0'&&C<='9')&&!(C>='a'&&C<='f')) return false;
    const TArray<TSharedPtr<FJsonValue>>* Cards=nullptr;
    if (!R.Valid||!R.List(TEXT("cards"),Cards,512,true)||Cards->Num()<8) return false;
    for (const auto& Value:*Cards) {
        rift::Card C;Reader CR(Obj(Value));bool Playable=false;double Decay=0;
        CR.Text(TEXT("id"),C.id,true);CR.Text(TEXT("name"),C.name,true);
#define FIELD(K) CR.Number(TEXT(#K),C.K,0,1e9,true);
        CARD_NUMBERS(FIELD)
#undef FIELD
        CR.Number(TEXT("hpDecayPerSecond"),Decay,0,1e9,true);
        C.hpDecayPerSecond=Decay;
#define FIELD(K) CR.Number(TEXT(#K),C.K,0,1e9,Schema>=2);
        EXPANSION_CARD_NUMBERS(FIELD)
#undef FIELD
        CR.Text(TEXT("deathSpawnId"),C.deathSpawnId,Schema>=2);
#define FIELD(K) if (!CR.O||!CR.O->HasField(TEXT(#K))) CR.Valid=false;else CR.Flag(TEXT(#K),C.K);
        CARD_FLAGS(FIELD)
#undef FIELD
#define FIELD(K) if (Schema>=2&&(!CR.O||!CR.O->HasField(TEXT(#K)))) CR.Valid=false;else CR.Flag(TEXT(#K),C.K);
        EXPANSION_CARD_FLAGS(FIELD)
#undef FIELD
        if (!CR.O||!CR.O->HasField(TEXT("playable"))) CR.Valid=false;else CR.Flag(TEXT("playable"),Playable);
        bool SafeId=!C.id.empty()&&C.id.size()<=128;
        for (char Ch:C.id) SafeId &= (Ch>='a'&&Ch<='z')||(Ch>='0'&&Ch<='9')||Ch=='_';
        if (!CR.Valid||!SafeId||C.name.empty()||Context.Definitions.count(C.id)||
            C.cost>10||C.count<1||C.count>100||C.rounds>1000||C.targetMask<1||C.targetMask>7||
            C.scale<=0||C.scale>100||C.collisionRadius>100||C.range>100||C.splash>100||C.spellRadius>100||C.auraRadius>100||
            C.moveSpeed>100||C.projectileSpeed>1000||C.shockwaveSpeed>100||
            C.attackInterval>MaxTime||C.lifetime>MaxTime||C.castDelay>10||C.slowDuration>10||C.slowPct>1||
            C.auraInterval>MaxTime||C.stunDuration>10||C.dotDuration>MaxTime||C.dotInterval>MaxTime||
            (C.spell&&C.building)||(!C.building&&Decay>0)||(!C.spell&&(C.hp<=0||C.collisionRadius<=0||C.attackInterval<=0))||
            (C.building&&C.lifetime<=0)||((C.dotDamage>0||C.dotStructureDamage>0)&&(C.dotInterval<=0||C.dotDuration<=0))||
            (C.auraDamage>0&&(C.auraInterval<=0||C.auraRadius<=0))||
            (C.shockwaveSpeed>0&&C.splash<=0)||(C.slowPct>0&&C.slowDuration<=0)||
            C.chargeMoveSeconds>MaxTime||C.chargedMoveSpeed>100||C.initialAttackCooldown>MaxTime||C.invisibilityDuration>10||
            C.deathSpawnCount>100||((C.deathSpawnCount>0)!=!C.deathSpawnId.empty())||
            (C.hardExpiry&&!C.building)||(C.shieldHP>0&&C.spell)||(C.invisibilityDuration>0&&!C.spell)||(!Playable&&C.cost!=0)) return false;
        if (Playable) Context.PlayableIds.Add(FS(C.id));
        Context.BuildingDecayRates.emplace(C.id,Decay);
        Context.Definitions.emplace(C.id,MoveTemp(C));
    }
    for (const auto& Entry:Context.Definitions) if (Entry.second.deathSpawnCount>0) {
        const auto Child=Context.Definitions.find(Entry.second.deathSpawnId);
        if (Child==Context.Definitions.end()||Context.PlayableIds.Contains(FS(Child->first))||Child->second.spell||Child->second.building) return false;
    }
    if (Context.PlayableIds.Num()<8) return false;
    Context.Schema=Schema;Context.HasCapturedDefinitions=true;Context.ManifestHash=FS(Hash);Context.ManifestVersion=FS(Version);
    if (Schema==1) Context.Limitations.Add(TEXT("Captured balance schema1 predates full expansion metadata. Missing shield, cloak, death-spawn, charge-speed and hard-expiry fields are unavailable; current definitions are not substituted."));
    return true;
}
#define TELEMETRY_DOUBLES(X)                                                                                 \
    X(spent)                                                                                                 \
    X(troopDamage)                                                                                           \
    X(towerDamage)                                                                                           \
    X(buildingDamage)                                                                                        \
    X(damageTaken)                                                                                           \
    X(kills)                                                                                                 \
    X(deaths)                                                                                                \
    X(killValue) X(lifetime) X(slowTime) X(stunTime) X(initialDamage) X(dotDamage) X(auraDamage) X(overkill) \
        X(prevented) X(spellValue) X(zoneOccupancy) X(zoneSeconds) X(crownContribution) X(buildingLifetime)  \
            X(buildingCapacity) X(slowTrackedSeconds) X(stunTrackedSeconds) X(shieldDamage) X(shieldDamageTaken) X(shieldAbsorbed) X(overflowPrevented)
#define TELEMETRY_COUNTS(X)                                                                                  \
    X(plays)                                                                                                 \
    X(spawns)                                                                                                \
    X(surviving)                                                                                             \
    X(pulls)                                                                                                 \
    X(connected)                                                                                             \
    X(targets) X(openingPlays) X(firstPlays) X(overtimePlays) X(stunned) X(dotTicks) X(openingEligible) X(childSpawns) X(childDeaths) X(childSurviving)
TSharedRef<FJsonObject> Telemetry(const rift::CardTelemetry &T) {
    auto O = MakeShared<FJsonObject>();
#define FIELD(K) O->SetNumberField(TEXT(#K), double(T.K));
    TELEMETRY_DOUBLES(FIELD)
    TELEMETRY_COUNTS(FIELD)
    FIELD(placementX)
    FIELD(placementZ)
#undef FIELD
    return O;
}
bool ReadTelemetry(const TSharedPtr<FJsonObject> &O, rift::CardTelemetry &T) {
    Reader R(O);
#define FIELD(K) R.Number(TEXT(#K), T.K, 0, 1e12);
    TELEMETRY_DOUBLES(FIELD)
    TELEMETRY_COUNTS(FIELD)
#undef FIELD
    R.Number(TEXT("placementX"), T.placementX);
    R.Number(TEXT("placementZ"), T.placementZ);
    return R.Valid;
}
#define ENTITY_NUMBERS(X)                                                                                    \
    X(id)                                                                                                    \
    X(target)                                                                                                \
    X(hardLock)                                                                                              \
    X(forcedTarget)                                                                                          \
    X(playId)                                                                                                \
    X(slowSource)                                                                                            \
    X(stunSource)                                                                                            \
    X(hp) X(maxHp) X(radius) X(cooldown) X(born) X(died) X(slowPct) X(slowUntil) X(stunUntil) X(forcedUntil) \
        X(chargeTime) X(auraClock) X(repathClock) X(scanClock) X(stuckClock) X(stuckTime) X(stuckDistance)   \
            X(slowTrackedFrom) X(stunTrackedFrom) X(lane) X(bridge) X(memberCount) X(shieldHP) X(maxShieldHP) X(shieldBrokenAt) X(invisibleUntil) X(invisibilitySource) X(invisibilityPlayId) X(parentEntityId)
#define EVENT_NUMBERS(X)                                                                                     \
    X(sequence)                                                                                              \
    X(time)                                                                                                  \
    X(source)                                                                                                \
    X(target)                                                                                                \
    X(playId)                                                                                                \
    X(amount)                                                                                                \
    X(requested)                                                                                             \
    X(overkill) X(hp) X(maxHp) X(aetherBefore) X(aetherAfter) X(until) X(targetCost) X(handIndex) X(count)   \
        X(crowns) X(parentEntityId) X(shieldHP) X(maxShieldHP) X(shieldOverflowBlocked)
TSharedRef<FJsonObject> EventJSON(const rift::Event &E) {
    auto O = MakeShared<FJsonObject>();
    O->SetStringField(TEXT("type"), FS(E.type));
    O->SetStringField(TEXT("reason"), FS(E.reason));
    O->SetStringField(TEXT("cardId"), FS(E.cardId));
    O->SetStringField(TEXT("damageKind"), FS(E.damageKind));
    O->SetStringField(TEXT("attributionCardId"), FS(E.attributionCardId));
#define FIELD(K) O->SetNumberField(TEXT(#K), double(E.K));
    EVENT_NUMBERS(FIELD)
    FIELD(team)
    FIELD(targetTeam)
    FIELD(targetKind)
#undef FIELD
    O->SetBoolField(TEXT("sandbox"), E.sandbox);
    O->SetBoolField(TEXT("openingHand"), E.openingHand);
    O->SetBoolField(TEXT("firstPlay"), E.firstPlay);
    O->SetBoolField(TEXT("overtime"), E.overtime);
    Point(O, TEXT("position"), E.position);
    TArray<TSharedPtr<FJsonValue>> Hand;
    for (const auto &Id : E.handAfter)
        Hand.Add(MakeShared<FJsonValueString>(FS(Id)));
    O->SetArrayField(TEXT("handAfter"), Hand);
    return O;
}
bool ReadEvent(const TSharedPtr<FJsonObject> &O, rift::Event &E,const FRiftReplayBalanceContext* Context=nullptr) {
    Reader R(O,Context);
    R.Text(TEXT("type"), E.type, true);
    R.Text(TEXT("reason"), E.reason);
    R.Text(TEXT("cardId"), E.cardId);
    R.Text(TEXT("damageKind"), E.damageKind);
    R.Text(TEXT("attributionCardId"), E.attributionCardId);
#define FIELD(K) R.Number(TEXT(#K), E.K);
    EVENT_NUMBERS(FIELD)
#undef FIELD
    R.Number(TEXT("team"), E.team, 0, 1, true);
    R.Number(TEXT("targetTeam"), E.targetTeam, 0, 1);
    R.Number(TEXT("targetKind"), E.targetKind, 0, 3);
    R.Flag(TEXT("sandbox"), E.sandbox);
    R.Flag(TEXT("openingHand"), E.openingHand);
    R.Flag(TEXT("firstPlay"), E.firstPlay);
    R.Flag(TEXT("overtime"), E.overtime);
    R.Vec(TEXT("position"), E.position);
    const TArray<TSharedPtr<FJsonValue>> *Hand = nullptr;
    if (R.List(TEXT("handAfter"), Hand, 4)) {
        if (Hand->Num() != 4)
            R.Valid = false;
        else
            for (int32 I = 0; I < 4; ++I) {
                FString Id;
                if (!(*Hand)[I] || !(*Hand)[I]->TryGetString(Id) || (!Id.IsEmpty()&&!PlayableCard(Context,TCHAR_TO_UTF8(*Id))))
                    R.Valid = false;
                else
                    E.handAfter[I] = TCHAR_TO_UTF8(*Id);
            }
    }
    const auto *Card = RecordedCard(Context,E.cardId);
    if (E.type == "spell_cast" || E.type == "spell_impact") {
        if (!SpellCard(Context,E.cardId) || (Card&&Card->castDelay<=0) || E.playId == 0)
            R.Valid = false;
        if (E.type == "spell_cast" &&
            (E.until <= E.time || E.until > MaxTime + 10 ||
             (Card&&FMath::Abs(E.until - E.time - Card->castDelay) > 1e-6)))
            R.Valid = false;
    }
    if (E.type == "shockwave_launch") {
        // New wave events must describe the same traveling attack as the core.
        // Older event types retain their existing optional-field contracts.
        R.Number(TEXT("time"),E.time,0,MaxTime,true);
        R.Number(TEXT("source"),E.source,1,1e12,true);
        R.Number(TEXT("target"),E.target,1,1e12,true);
        R.Number(TEXT("playId"),E.playId,1,1e12,true);
        R.Number(TEXT("amount"),E.amount,0,1e12,true);
        R.Number(TEXT("until"),E.until,0,MaxTime+10,true);
        R.Vec(TEXT("position"),E.position,true);
        if (!KnownCard(Context,E.cardId) || E.until<=E.time ||
            (Card&&(Card->shockwaveSpeed<=0 || Card->splash<=0 ||
                FMath::Abs(E.amount-Card->damage)>1e-6 ||
                FMath::Abs(E.until-E.time-Card->splash/Card->shockwaveSpeed)>1e-6)))
            R.Valid=false;
    }
    if (E.type=="slow" && (E.amount<=0||E.amount>1||E.until<=E.time||E.until>E.time+10||
        (Card&&(FMath::Abs(E.amount-Card->slowPct)>1e-6||FMath::Abs(E.until-E.time-Card->slowDuration)>1e-6)))) R.Valid=false;
    if (E.type=="card_play" && (!PlayableCard(Context,E.cardId)||E.amount<0||E.amount>10||E.aetherBefore<0||E.aetherBefore>10||E.aetherAfter<0||E.aetherAfter>10||
        (!E.sandbox&&(FMath::Abs(E.aetherBefore-E.aetherAfter-E.amount)>1e-6||(Card&&FMath::Abs(E.amount-Card->cost)>1e-6))))) R.Valid=false;
    if (E.type=="projectile_launch" && (E.until<=E.time||E.until>E.time+MaxTime||E.amount<0||E.source==0||E.target==0)) R.Valid=false;
    if (!E.attributionCardId.empty()&&!PlayableCard(Context,E.attributionCardId)) R.Valid=false;
    if (E.type=="shield_damage" && (E.target==0||E.amount<=0||E.requested<E.amount||E.shieldHP<0||E.maxShieldHP<E.shieldHP||E.shieldOverflowBlocked<0||E.amount+E.shieldOverflowBlocked>E.requested+1e-6)) R.Valid=false;
    if (E.type=="invisibility_apply" && (E.target==0||E.playId==0||E.until<=E.time||E.until>E.time+10||
        (Card&&(Card->invisibilityDuration<=0||FMath::Abs(E.until-E.time-Card->invisibilityDuration)>1e-6)))) R.Valid=false;
    if(E.type=="charge_ready"||E.type=="charge_spent") {
        if(E.source==0||E.playId==0||!KnownCard(Context,E.cardId))R.Valid=false;
        if(Context&&Context->Schema>=2&&Card) {
            const auto* Body=Context->Entities.Find(E.source);
            if(!Body||Body->cardId!=E.cardId||Body->playId!=E.playId||Body->team!=E.team||
               Body->parentEntityId!=E.parentEntityId||Body->attributionCardId!=E.attributionCardId)R.Valid=false;
            if(Card->chargeDamage<=0)R.Valid=false;
            if(E.type=="charge_ready"&&(E.requested<=Card->chargeMoveSeconds||E.requested>MaxTime||
                FMath::Abs(E.amount-(Card->chargedMoveSpeed>0?Card->chargedMoveSpeed:Card->moveSpeed))>1e-6))R.Valid=false;
            if(E.type=="charge_spent"&&(E.target==0||FMath::Abs(E.amount-Card->chargeDamage)>1e-6))R.Valid=false;
        }
    }
    return R.Valid && !E.type.empty() && E.sequence > 0 && E.time >= 0 && E.time <= MaxTime &&
           (E.cardId.empty() || E.type == "ai_decision" || KnownCard(Context,E.cardId));
}
FString SafeReplayPath(const FString &Name) {
    return FPaths::Combine(URiftProfileSubsystem::SaveRoot(), TEXT("Replays"),
                           FPaths::GetCleanFilename(Name));
}
bool ReadRecordingFile(const FString &Filename, TSharedPtr<FJsonObject> &Out, FString &Text, FString &Error) {
    TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Filename));
    const int64 Size = File ? File->TotalSize() : -1;
    if (Size <= 0 || Size > DecodedLimit) {
        Error = TEXT("Replay is missing or exceeds the 256 MB JSON input limit.");
        return false;
    }
    uint8 Header[ArchiveHeaderSize] = {};
    if (Size >= ArchiveHeaderSize)
        File->Serialize(Header, ArchiveHeaderSize);
    const bool Native = FPaths::GetExtension(Filename).Equals(TEXT("riftreplay"), ESearchCase::IgnoreCase) ||
                        FMemory::Memcmp(Header, ArchiveMagic, 7) == 0;
    if (Native) {
        if (Size <= ArchiveHeaderSize || Size > ReplayLimit ||
            FMemory::Memcmp(Header, ArchiveMagic, UE_ARRAY_COUNT(ArchiveMagic)) != 0) {
            Error = TEXT("Invalid, unsupported or oversized native replay archive header.");
            return false;
        }
        const uint32 RawBytes = ReadArchiveUInt32(Header + 8), CRC = ReadArchiveUInt32(Header + 12),
                     CompressedBytes = ReadArchiveUInt32(Header + 16),
                     CompressedCRC = ReadArchiveUInt32(Header + 20);
        if (RawBytes == 0 || RawBytes > uint32(DecodedLimit)) {
            Error = TEXT("Native replay declares an unsupported decoded JSON size.");
            return false;
        }
        if (int64(CompressedBytes) != Size - ArchiveHeaderSize) {
            Error = TEXT("Native replay payload size does not match its header.");
            return false;
        }
        TArray<uint8> Compressed, Raw;
        Compressed.SetNumUninitialized(int32(Size - ArchiveHeaderSize));
        File->Serialize(Compressed.GetData(), Compressed.Num());
        if (File->IsError() || FCrc::MemCrc32(Compressed.GetData(), Compressed.Num()) != CompressedCRC) {
            Error = TEXT("Native replay compressed payload checksum is invalid.");
            return false;
        }
        Raw.SetNumUninitialized(int32(RawBytes));
        if (!FCompression::UncompressMemory(NAME_Zlib, Raw.GetData(), RawBytes, Compressed.GetData(),
                                            Compressed.Num()) ||
            FCrc::MemCrc32(Raw.GetData(), Raw.Num()) != CRC) {
            Error = TEXT("Native replay payload is truncated or corrupt.");
            return false;
        }
        FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR *>(Raw.GetData()), Raw.Num());
        Text = FString(Decoded.Length(), Decoded.Get());
    } else {
        File.Reset();
        if (!FFileHelper::LoadFileToString(Text, *Filename)) {
            Error = TEXT("Unreadable replay JSON.");
            return false;
        }
    }
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out)) {
        Error = TEXT("Unreadable replay JSON.");
        return false;
    }
    return URiftReplaySubsystem::ValidateRecording(Out, Error);
}
} // namespace

TSharedRef<FJsonObject> URiftReplaySubsystem::SnapshotJSON(const rift::Snapshot &State) {
    auto O = MakeShared<FJsonObject>();
    O->SetNumberField(TEXT("time"), State.elapsed);
    O->SetNumberField(TEXT("phaseElapsed"), State.phaseElapsed);
    O->SetNumberField(TEXT("timeRemaining"), State.timeRemaining);
    O->SetNumberField(TEXT("phase"), double(State.phase));
    O->SetNumberField(TEXT("winner"), State.winner);
    O->SetStringField(TEXT("resultReason"), FS(State.resultReason));
    O->SetNumberField(TEXT("seed"), State.seed);
    O->SetNumberField(TEXT("randomState"), State.randomState);
    TArray<TSharedPtr<FJsonValue>> Teams;
    for (int32 I = 0; I < 2; ++I) {
        auto T = MakeShared<FJsonObject>();
        T->SetNumberField(TEXT("aether"), State.aether[I]);
        T->SetNumberField(TEXT("spent"), State.spent[I]);
        T->SetNumberField(TEXT("leaked"), State.leaked[I]);
        T->SetNumberField(TEXT("crowns"), State.crowns[I]);
        T->SetArrayField(TEXT("deck"), Strings(State.decks[I]));
        T->SetArrayField(TEXT("queue"), Strings(State.queues[I]));
        TArray<TSharedPtr<FJsonValue>> Hand;
        for (const auto &Id : State.hands[I])
            Hand.Add(MakeShared<FJsonValueString>(FS(Id)));
        T->SetArrayField(TEXT("hand"), Hand);
        const auto &AI = State.ai[I];
        T->SetStringField(TEXT("style"), FS(AI.style));
        T->SetStringField(TEXT("aiPhase"), FS(AI.phase));
        T->SetStringField(TEXT("decision"), FS(AI.decision));
        T->SetStringField(TEXT("reason"), FS(AI.reason));
        T->SetBoolField(TEXT("aiEnabled"), AI.enabled);
        T->SetNumberField(TEXT("estimatedOpponentAether"), AI.estimatedOpponentAether);
        T->SetNumberField(TEXT("think"), AI.think);
        T->SetNumberField(TEXT("lastPunish"), AI.lastPunish);
        T->SetNumberField(TEXT("lastSpell"), AI.lastSpell);
        T->SetNumberField(TEXT("anchor"), double(AI.anchor));
        T->SetNumberField(TEXT("supports"), AI.supports);
        T->SetNumberField(TEXT("pushLane"), AI.pushLane);
        T->SetArrayField(TEXT("observedCycle"), Strings(AI.observedCycle));
        auto Metrics = MakeShared<FJsonObject>();
        for (const auto &Pair : State.telemetry[I])
            Metrics->SetObjectField(FS(Pair.first), Telemetry(Pair.second));
        T->SetObjectField(TEXT("telemetry"), Metrics);
        TArray<TSharedPtr<FJsonValue>> BodyMetrics;
        for (const auto& Pair:State.entityTelemetry[I]) {
            const auto& Body=Pair.second;auto B=MakeShared<FJsonObject>();
            B->SetNumberField(TEXT("id"),double(Body.id));B->SetNumberField(TEXT("parentEntityId"),double(Body.parentEntityId));
            B->SetNumberField(TEXT("playId"),double(Body.playId));B->SetStringField(TEXT("cardId"),FS(Body.cardId));
            B->SetStringField(TEXT("attributionCardId"),FS(Body.attributionCardId));B->SetObjectField(TEXT("observed"),Telemetry(Body.observed));
            BodyMetrics.Add(MakeShared<FJsonValueObject>(B));
        }
        T->SetArrayField(TEXT("entityTelemetry"),BodyMetrics);
        Teams.Add(MakeShared<FJsonValueObject>(T));
    }
    O->SetArrayField(TEXT("teams"), Teams);
    TArray<TSharedPtr<FJsonValue>> Entities;
    for (const auto &E : State.entities) {
        auto R = MakeShared<FJsonObject>();
        R->SetStringField(TEXT("cardId"), FS(E.cardId));
        R->SetStringField(TEXT("attributionCardId"),FS(E.attributionCardId));
        R->SetStringField(TEXT("invisibilityCardId"),FS(E.invisibilityCardId));
        Point(R, TEXT("position"), E.position);
        Point(R, TEXT("facing"), E.facing);
        Point(R, TEXT("pathTarget"), E.pathTarget);
        Point(R, TEXT("stuckPosition"), E.stuckPosition);
#define FIELD(K) R->SetNumberField(TEXT(#K), double(E.K));
        ENTITY_NUMBERS(FIELD)
        FIELD(team)
        FIELD(kind)
#undef FIELD
        R->SetBoolField(TEXT("active"), E.active);
        R->SetBoolField(TEXT("dead"), E.dead);
        R->SetBoolField(TEXT("flying"), E.flying);
        R->SetBoolField(TEXT("charged"), E.charged);
        TArray<TSharedPtr<FJsonValue>> Slows;
        for (const auto& Slow:E.slows)
        {
            auto V=MakeShared<FJsonObject>();
            V->SetNumberField(TEXT("source"),double(Slow.source));V->SetNumberField(TEXT("playId"),double(Slow.playId));
            V->SetNumberField(TEXT("team"),double(Slow.team));V->SetStringField(TEXT("cardId"),FS(Slow.cardId));
            V->SetNumberField(TEXT("pct"),Slow.pct);V->SetNumberField(TEXT("born"),Slow.born);V->SetNumberField(TEXT("until"),Slow.until);
            Slows.Add(MakeShared<FJsonValueObject>(V));
        }
        R->SetArrayField(TEXT("slows"),Slows);
        TArray<TSharedPtr<FJsonValue>> Path;
        for (auto P : E.path) {
            auto V = MakeShared<FJsonObject>();
            V->SetNumberField(TEXT("x"), P.x);
            V->SetNumberField(TEXT("z"), P.z);
            Path.Add(MakeShared<FJsonValueObject>(V));
        }
        R->SetArrayField(TEXT("path"), Path);
        Entities.Add(MakeShared<FJsonValueObject>(R));
    }
    O->SetArrayField(TEXT("entities"), Entities);
    TArray<TSharedPtr<FJsonValue>> Projectiles;
    for (const auto &P : State.projectiles) {
        auto R = MakeShared<FJsonObject>();
#define FIELD(K) R->SetNumberField(TEXT(#K), double(P.K));
        FIELD(id)
        FIELD(source)
        FIELD(target)
        FIELD(playId)
        FIELD(team)
        FIELD(damage)
        FIELD(splash)
        FIELD(remaining) FIELD(duration)
#undef FIELD
            R->SetStringField(TEXT("cardId"), FS(P.cardId));
        Point(R, TEXT("origin"), P.origin);
        Projectiles.Add(MakeShared<FJsonValueObject>(R));
    }
    O->SetArrayField(TEXT("projectiles"), Projectiles);
    TArray<TSharedPtr<FJsonValue>> Shockwaves;
    for (const auto& Wave:State.shockwaves)
    {
        auto V=MakeShared<FJsonObject>();
#define WAVE_FIELD(K) V->SetNumberField(TEXT(#K),double(Wave.K));
        WAVE_FIELD(id) WAVE_FIELD(source) WAVE_FIELD(playId) WAVE_FIELD(team)
        WAVE_FIELD(radius) WAVE_FIELD(speed) WAVE_FIELD(progress) WAVE_FIELD(born) WAVE_FIELD(expires)
#undef WAVE_FIELD
        V->SetStringField(TEXT("cardId"),FS(Wave.cardId));Point(V,TEXT("origin"),Wave.origin);
        TArray<TSharedPtr<FJsonValue>> Hits;
        for (auto Id:Wave.hitTargets) Hits.Add(MakeShared<FJsonValueNumber>(double(Id)));
        V->SetArrayField(TEXT("hitTargets"),Hits);Shockwaves.Add(MakeShared<FJsonValueObject>(V));
    }
    O->SetArrayField(TEXT("shockwaves"),Shockwaves);
    TArray<TSharedPtr<FJsonValue>> SpellCasts;
    for (const auto &C : State.spellCasts) {
        auto R = MakeShared<FJsonObject>();
        R->SetNumberField(TEXT("playId"), double(C.playId));
        R->SetNumberField(TEXT("team"), double(C.team));
        R->SetStringField(TEXT("cardId"), FS(C.cardId));
        Point(R, TEXT("position"), C.position);
        R->SetNumberField(TEXT("born"), C.born);
        R->SetNumberField(TEXT("impactAt"), C.impactAt);
        SpellCasts.Add(MakeShared<FJsonValueObject>(R));
    }
    O->SetArrayField(TEXT("spellCasts"), SpellCasts);
    TArray<TSharedPtr<FJsonValue>> Hazards;
    for (const auto &H : State.hazards) {
        auto R = MakeShared<FJsonObject>();
#define FIELD(K) R->SetNumberField(TEXT(#K), double(H.K));
        FIELD(playId)
        FIELD(team)
        FIELD(radius)
        FIELD(born)
        FIELD(nextTick)
        FIELD(expires)
        FIELD(ticks)
#undef FIELD
        R->SetStringField(TEXT("cardId"), FS(H.cardId));
        Point(R, TEXT("position"), H.position);
        Hazards.Add(MakeShared<FJsonValueObject>(R));
    }
    O->SetArrayField(TEXT("hazards"), Hazards);
    return O;
}
static bool ReadSnapshot(const TSharedPtr<FJsonObject> &O, rift::Snapshot &State,const FRiftReplayBalanceContext* Context=nullptr) {
    rift::Snapshot Parsed;
    Reader R(O,Context);
    uint64 EventSequence = 0;
    R.Number(TEXT("eventSequence"), EventSequence, 0, 1e12);
    R.Number(TEXT("time"), Parsed.elapsed, 0, MaxTime, true);
    R.Number(TEXT("phaseElapsed"), Parsed.phaseElapsed, 0, MaxTime);
    R.Number(TEXT("timeRemaining"), Parsed.timeRemaining, 0, MaxTime);
    R.Number(TEXT("phase"), Parsed.phase, 0, 3, true);
    R.Number(TEXT("winner"), Parsed.winner, -1, 1);
    R.Text(TEXT("resultReason"), Parsed.resultReason);
    R.Number(TEXT("seed"), Parsed.seed, 0, MAX_uint32);
    R.Number(TEXT("randomState"), Parsed.randomState, 0, MAX_uint32);
    const TArray<TSharedPtr<FJsonValue>> *Teams = nullptr;
    if (!R.List(TEXT("teams"), Teams, 2, true) || Teams->Num() != 2)
        return false;
    for (int32 I = 0; I < 2; ++I) {
        Reader T(Obj((*Teams)[I]),Context);
        T.Number(TEXT("aether"), Parsed.aether[I], 0, 10, true);
        T.Number(TEXT("spent"), Parsed.spent[I], 0, 1e9);
        T.Number(TEXT("leaked"), Parsed.leaked[I], 0, 1e9);
        T.Number(TEXT("crowns"), Parsed.crowns[I], 0, 3);
        std::vector<std::string> Hand;
        T.CardList(TEXT("hand"), Hand, 4);
        T.CardList(TEXT("deck"), Parsed.decks[I], 8);
        T.CardList(TEXT("queue"), Parsed.queues[I], 4);
        if (Hand.size() == 4)
            std::copy(Hand.begin(), Hand.end(), Parsed.hands[I].begin());
        std::vector<std::string> Cycle = Hand;
        Cycle.insert(Cycle.end(), Parsed.queues[I].begin(), Parsed.queues[I].end());
        if (!RecordedDeck(Context,Parsed.decks[I]) || !RecordedDeck(Context,Cycle))
            T.Valid = false;
        auto DeckIds = Parsed.decks[I];
        std::sort(DeckIds.begin(), DeckIds.end());
        std::sort(Cycle.begin(), Cycle.end());
        if (DeckIds != Cycle)
            T.Valid = false;
        auto &AI = Parsed.ai[I];
        T.Text(TEXT("style"), AI.style);
        T.Text(TEXT("aiPhase"), AI.phase);
        T.Text(TEXT("decision"), AI.decision);
        T.Text(TEXT("reason"), AI.reason);
        T.Flag(TEXT("aiEnabled"), AI.enabled);
        T.Number(TEXT("estimatedOpponentAether"), AI.estimatedOpponentAether, 0, 10);
        T.Number(TEXT("think"), AI.think);
        T.Number(TEXT("lastPunish"), AI.lastPunish);
        T.Number(TEXT("lastSpell"), AI.lastSpell);
        T.Number(TEXT("anchor"), AI.anchor, 0, 1e12);
        T.Number(TEXT("supports"), AI.supports, 0, 1000);
        T.Number(TEXT("pushLane"), AI.pushLane, -1, 1);
        T.CardList(TEXT("observedCycle"), AI.observedCycle, -1, 12);
        if (T.O && T.O->HasField(TEXT("telemetry"))) {
            const TSharedPtr<FJsonObject> *Metrics = nullptr;
            if (!T.O->TryGetObjectField(TEXT("telemetry"), Metrics) || (*Metrics)->Values.Num() > 512)
                T.Valid = false;
            else
                for (const auto &Pair : (*Metrics)->Values) {
                    rift::CardTelemetry Tele;
                    if (!KnownCard(Context,TCHAR_TO_UTF8(*Pair.Key)) || !ReadTelemetry(Obj(Pair.Value), Tele))
                        T.Valid = false;
                    else
                        Parsed.telemetry[I].emplace(TCHAR_TO_UTF8(*Pair.Key), Tele);
                }
        }
        const TArray<TSharedPtr<FJsonValue>>* Bodies=nullptr;
        if(T.List(TEXT("entityTelemetry"),Bodies,50000,Context&&Context->Schema>=2)) for(const auto& Value:*Bodies) {
            rift::EntityTelemetry Body;Reader B(Obj(Value),Context);
            B.Number(TEXT("id"),Body.id,1,1e12,true);B.Number(TEXT("parentEntityId"),Body.parentEntityId,0,1e12,true);
            B.Number(TEXT("playId"),Body.playId,1,1e12,true);B.Text(TEXT("cardId"),Body.cardId,true);
            B.Text(TEXT("attributionCardId"),Body.attributionCardId,true);
            const TSharedPtr<FJsonObject>* Observed=nullptr;
            if(!B.O||!B.O->TryGetObjectField(TEXT("observed"),Observed)||!ReadTelemetry(*Observed,Body.observed))B.Valid=false;
            if(!KnownCard(Context,Body.cardId)||!PlayableCard(Context,Body.attributionCardId)||
               Body.id==Body.parentEntityId||Parsed.entityTelemetry[I].count(Body.id)||
               Body.observed.plays||Body.observed.spent||Body.observed.spawns!=1||
               (!Body.parentEntityId&&Body.attributionCardId!=Body.cardId))B.Valid=false;
            T.Valid &= B.Valid;Parsed.entityTelemetry[I].emplace(Body.id,std::move(Body));
        }
        R.Valid &= T.Valid;
    }
    const TArray<TSharedPtr<FJsonValue>> *A = nullptr;
    TSet<uint64> EntityIds,EntityPlayIdsCaptured;
    if (!R.List(TEXT("entities"), A, 5000, true))
        return false;
    for (const auto &V : *A) {
        rift::Entity E;
        Reader T(Obj(V));
        T.Text(TEXT("cardId"), E.cardId, true);
        T.Text(TEXT("attributionCardId"),E.attributionCardId);
        T.Text(TEXT("invisibilityCardId"),E.invisibilityCardId);
        T.Vec(TEXT("position"), E.position, true);
        T.Vec(TEXT("facing"), E.facing);
        T.Vec(TEXT("pathTarget"), E.pathTarget);
        T.Vec(TEXT("stuckPosition"), E.stuckPosition);
#define FIELD(K) T.Number(TEXT(#K), E.K);
        ENTITY_NUMBERS(FIELD)
#undef FIELD
        if(T.O&&T.O->HasField(TEXT("playId")))EntityPlayIdsCaptured.Add(E.id);
        T.Number(TEXT("team"), E.team, 0, 1, true);
        T.Number(TEXT("kind"), E.kind, 0, 3, true);
        T.Flag(TEXT("active"), E.active);
        T.Flag(TEXT("dead"), E.dead);
        T.Flag(TEXT("flying"), E.flying);
        T.Flag(TEXT("charged"), E.charged);
        const TArray<TSharedPtr<FJsonValue>>* Slows=nullptr;
        TSet<FString> SlowCards;
        if (T.List(TEXT("slows"),Slows,100)) for (const auto& SV:*Slows)
        {
            rift::SlowEffect Slow;Reader SR(Obj(SV));
            SR.Number(TEXT("source"),Slow.source,0,1e12,true);SR.Number(TEXT("playId"),Slow.playId,1,1e12,true);
            SR.Number(TEXT("team"),Slow.team,0,1,true);SR.Text(TEXT("cardId"),Slow.cardId,true);
            SR.Number(TEXT("pct"),Slow.pct,0,1,true);SR.Number(TEXT("born"),Slow.born,0,MaxTime,true);
            SR.Number(TEXT("until"),Slow.until,0,MaxTime+10,true);
            const FString CardId=FS(Slow.cardId);
            const auto* SlowCard=RecordedCard(Context,Slow.cardId);
            if (!KnownCard(Context,Slow.cardId)||SlowCards.Contains(CardId)||Slow.pct<=0||Slow.until<=Slow.born||
                (SlowCard&&(SlowCard->slowPct<=0||FMath::Abs(Slow.pct-SlowCard->slowPct)>1e-6||
                    FMath::Abs(Slow.until-Slow.born-SlowCard->slowDuration)>1e-6))) SR.Valid=false;
            SlowCards.Add(CardId);T.Valid &= SR.Valid;E.slows.push_back(Slow);
        }
        const TArray<TSharedPtr<FJsonValue>> *Path = nullptr;
        if (T.List(TEXT("path"), Path, 2000))
            for (const auto &PV : *Path) {
                auto Q = Obj(PV);
                Reader P(Q);
                rift::Vec2 Position;
                P.Number(TEXT("x"), Position.x, -1e6, 1e6, true);
                P.Number(TEXT("z"), Position.z, -1e6, 1e6, true);
                T.Valid &= P.Valid;
                E.path.push_back(Position);
            }
        if (E.id == 0 || EntityIds.Contains(E.id) || E.hp < 0 || E.maxHp <= 0 || E.hp > E.maxHp + .001 ||
            E.radius < 0 || E.slowPct < 0 || E.slowPct > 1 || E.memberCount < 1 || E.memberCount > 100 ||
            E.shieldHP<0||E.maxShieldHP<0||E.shieldHP>E.maxShieldHP+.001||E.shieldBrokenAt < -1||
            E.shieldBrokenAt>Parsed.elapsed+1e-6||E.invisibleUntil<0||E.invisibleUntil>MaxTime+10||
            (!E.attributionCardId.empty()&&!PlayableCard(Context,E.attributionCardId))||
            (!E.invisibilityCardId.empty()&&!SpellCard(Context,E.invisibilityCardId))||
            (E.parentEntityId&&(E.parentEntityId==E.id||E.attributionCardId.empty()))||
            (!E.cardId.empty() && !KnownCard(Context,E.cardId)) ||
            (E.cardId.empty() && E.kind < rift::EntityKind::Guard))
            T.Valid = false;
        EntityIds.Add(E.id);
        R.Valid &= T.Valid;
        Parsed.entities.push_back(E);
    }
    for(int Team=0;Team<2;++Team) for(const auto& Entry:Parsed.entityTelemetry[Team]) {
        const auto& Body=Entry.second;
        if(Parsed.entityTelemetry[1-Team].count(Body.id))R.Valid=false;
        if(Body.parentEntityId) {
            const auto Parent=Parsed.entityTelemetry[Team].find(Body.parentEntityId);
            if(Parent==Parsed.entityTelemetry[Team].end()||Parent->second.parentEntityId||Parent->second.playId!=Body.playId||
               Parent->second.attributionCardId!=Body.attributionCardId)R.Valid=false;
            const auto* Definition=RecordedCard(Context,Body.attributionCardId);
            if(Context&&Context->Schema>=2&&(!Definition||Definition->deathSpawnId!=Body.cardId))R.Valid=false;
        }
    }
    for(auto& Entity:Parsed.entities) {
        const auto Found=Parsed.entityTelemetry[int(Entity.team)].find(Entity.id);
        // Older entity records may omit playId. A persistent body observation is
        // still an authoritative captured identity; use it rather than comparing
        // its recorded cast against an invented zero or today's definitions.
        if(Found!=Parsed.entityTelemetry[int(Entity.team)].end()&&!EntityPlayIdsCaptured.Contains(Entity.id)) {
            Entity.playId=Found->second.playId;
            EntityPlayIdsCaptured.Add(Entity.id);
        }
        if(Found!=Parsed.entityTelemetry[int(Entity.team)].end()&&
           (Found->second.cardId!=Entity.cardId||Found->second.parentEntityId!=Entity.parentEntityId||
            Found->second.playId!=Entity.playId||Found->second.attributionCardId!=Entity.attributionCardId))R.Valid=false;
    }
    if (R.List(TEXT("projectiles"), A, 10000))
        for (const auto &V : *A) {
            rift::Projectile P;
            Reader T(Obj(V));
            T.Text(TEXT("cardId"), P.cardId);
            T.Vec(TEXT("origin"), P.origin, true);
#define FIELD(K) T.Number(TEXT(#K), P.K, 0, 1e12);
            FIELD(id)
            FIELD(source)
            FIELD(target)
            FIELD(playId)
            FIELD(damage)
            FIELD(splash)
            FIELD(remaining)
            FIELD(duration)
#undef FIELD
                T.Number(TEXT("team"), P.team, 0, 1, true);
            if ((!P.cardId.empty() && !KnownCard(Context,P.cardId)) || P.id==0||P.source==0||P.target==0||P.duration<=0||P.remaining>P.duration+1e-6)
                T.Valid = false;
            R.Valid &= T.Valid;
            Parsed.projectiles.push_back(P);
        }
    // Optional for recordings made before Pulverizer was introduced.
    TSet<uint64> WaveIds;
    if (R.List(TEXT("shockwaves"),A,10000)) for (const auto& WV:*A)
    {
        rift::Shockwave Wave;Reader W(Obj(WV));
        W.Number(TEXT("id"),Wave.id,1,1e12,true);W.Number(TEXT("source"),Wave.source,1,1e12,true);
        W.Number(TEXT("playId"),Wave.playId,1,1e12,true);W.Number(TEXT("team"),Wave.team,0,1,true);
        W.Text(TEXT("cardId"),Wave.cardId,true);W.Vec(TEXT("origin"),Wave.origin,true);
        W.Number(TEXT("radius"),Wave.radius,0,100,true);W.Number(TEXT("speed"),Wave.speed,0,100,true);
        W.Number(TEXT("progress"),Wave.progress,0,100,true);W.Number(TEXT("born"),Wave.born,0,MaxTime,true);
        W.Number(TEXT("expires"),Wave.expires,0,MaxTime+10,true);
        const auto* Card=RecordedCard(Context,Wave.cardId);
        // The source may have died and been removed while its wave travels.
        // A source that is still recorded must describe this same deployment.
        const auto Source=std::find_if(Parsed.entities.begin(),Parsed.entities.end(),[&](const auto& Entity){return Entity.id==Wave.source;});
        if(Source!=Parsed.entities.end()&&(Source->cardId!=Wave.cardId||Source->team!=Wave.team||
            (EntityPlayIdsCaptured.Contains(Source->id)&&Source->playId!=Wave.playId)))W.Valid=false;
        if (!KnownCard(Context,Wave.cardId)||(Card&&Card->shockwaveSpeed<=0)||WaveIds.Contains(Wave.id)||Wave.speed<=0||Wave.radius<=0||
            Wave.progress>Wave.radius+1e-7||Wave.born>Parsed.elapsed+1e-7||Wave.expires<Parsed.elapsed-1e-7||
            FMath::Abs(Wave.expires-Wave.born-Wave.radius/FMath::Max(.001,Wave.speed))>1e-6||
            FMath::Abs(Wave.progress-FMath::Clamp((Parsed.elapsed-Wave.born)*Wave.speed,0.,Wave.radius))>1e-6||
            (Card&&(FMath::Abs(Wave.speed-Card->shockwaveSpeed)>1e-6||FMath::Abs(Wave.radius-Card->splash)>1e-6))||
            Parsed.phase==rift::Phase::Tiebreaker||Parsed.phase==rift::Phase::Finished) W.Valid=false;
        const TArray<TSharedPtr<FJsonValue>>* Hits=nullptr;TSet<uint64> HitIds;
        if (W.List(TEXT("hitTargets"),Hits,5000,true)) for (const auto& Hit:*Hits)
        {
            double Id=0;
            if (!Hit||!Hit->TryGetNumber(Id)||!FMath::IsFinite(Id)||Id<1||Id>1e12||FMath::FloorToDouble(Id)!=Id||HitIds.Contains(uint64(Id))) W.Valid=false;
            else {HitIds.Add(uint64(Id));Wave.hitTargets.push_back(uint64(Id));}
        }
        WaveIds.Add(Wave.id);R.Valid &= W.Valid;Parsed.shockwaves.push_back(Wave);
    }
    // Optional for recordings made before delayed spell impacts were introduced.
    TSet<uint64> CastIds;
    if (R.List(TEXT("spellCasts"), A, 5000))
        for (const auto &V : *A) {
            rift::SpellCast C;
            Reader T(Obj(V));
            T.Number(TEXT("playId"), C.playId, 1, 1e12, true);
            T.Number(TEXT("team"), C.team, 0, 1, true);
            T.Text(TEXT("cardId"), C.cardId, true);
            T.Vec(TEXT("position"), C.position, true);
            T.Number(TEXT("born"), C.born, 0, MaxTime, true);
            T.Number(TEXT("impactAt"), C.impactAt, 0, MaxTime + 10, true);
            const auto *Card = RecordedCard(Context,C.cardId);
            if (!SpellCard(Context,C.cardId) || (Card&&Card->castDelay<=0) || CastIds.Contains(C.playId) ||
                C.born > Parsed.elapsed + 1e-7 || C.impactAt < Parsed.elapsed - 1e-7 ||
                C.impactAt <= C.born ||
                (Card && FMath::Abs(C.impactAt - C.born - Card->castDelay) > 1e-6) ||
                Parsed.phase == rift::Phase::Tiebreaker || Parsed.phase == rift::Phase::Finished)
                T.Valid = false;
            CastIds.Add(C.playId);
            R.Valid &= T.Valid;
            Parsed.spellCasts.push_back(C);
        }
    if (R.List(TEXT("hazards"), A, 5000))
        for (const auto &V : *A) {
            rift::Hazard H;
            Reader T(Obj(V));
            T.Text(TEXT("cardId"), H.cardId, true);
            T.Vec(TEXT("position"), H.position, true);
#define FIELD(K) T.Number(TEXT(#K), H.K, 0, 1e12);
            FIELD(playId)
            FIELD(radius)
            FIELD(born)
            FIELD(nextTick)
            FIELD(expires)
            FIELD(ticks)
#undef FIELD
            T.Number(TEXT("team"), H.team, 0, 1, true);
            const auto* Card=RecordedCard(Context,H.cardId);
            if (!KnownCard(Context,H.cardId)||H.playId==0||H.radius<=0||H.radius>100||H.born>Parsed.elapsed+1e-7||H.expires<H.born||H.expires>MaxTime+MaxTime||H.nextTick<H.born||H.nextTick>H.expires+MaxTime||
                (Card&&(Card->dotDamage<=0||FMath::Abs(H.radius-Card->spellRadius)>1e-6||FMath::Abs(H.expires-H.born-Card->dotDuration)>1e-6||FMath::Abs(H.nextTick-H.born-(H.ticks+1)*Card->dotInterval)>1e-6)))
                T.Valid = false;
            R.Valid &= T.Valid;
            Parsed.hazards.push_back(H);
        }
    if (!R.Valid)
        return false;
    State = MoveTemp(Parsed);
    return true;
}
bool URiftReplaySubsystem::SnapshotFromJSON(const TSharedPtr<FJsonObject>& O,rift::Snapshot& State) {
    return ReadSnapshot(O,State);
}
bool URiftReplaySubsystem::ValidateRecording(const TSharedPtr<FJsonObject> &O, FString &Error) {
    int64 MinimumBytes=0;
    if (!MinimumJSONObjectBytes(O,MinimumBytes,DecodedLimit)) {
        Error=ReplaySizeError;
        return false;
    }
    FRiftReplayBalanceContext Context;
    if (!ReadBalance(O,Context)) {Error=TEXT("Replay captured balance definitions are invalid.");return false;}
    Reader R(O,&Context);
    int32 Version = 0;
    double Duration = 0;
    std::string Model;
    R.Number(TEXT("formatVersion"), Version, 1, 1, true);
    R.Text(TEXT("model"), Model, true);
    R.Number(TEXT("duration"), Duration, 0, MaxTime, true);
    const TArray<TSharedPtr<FJsonValue>> *States = nullptr;
    const TArray<TSharedPtr<FJsonValue>> *Events = nullptr;
    if (!R.Valid || Model != "rift-native-1" || !R.List(TEXT("states"), States, 20000, true) ||
        States->IsEmpty() || !R.List(TEXT("events"), Events, 200000, true)) {
        Error = TEXT("Replay format is incompatible or incomplete.");
        return false;
    }
    double Previous = -1;
    for (const auto &V : *States) {
        rift::Snapshot State;
        if (!ReadSnapshot(Obj(V), State,&Context) || State.elapsed < Previous || State.elapsed > Duration + .001) {
            Error = TEXT("Replay contains invalid or out-of-order snapshots.");
            return false;
        }
        Previous = State.elapsed;
        for(const auto& Entity:State.entities)Context.Entities.Add(Entity.id,Entity);
        for(int Team=0;Team<2;++Team)for(const auto& Entry:State.entityTelemetry[Team])if(!Context.Entities.Contains(Entry.first)) {
            const auto& Body=Entry.second;rift::Entity Identity;Identity.id=Body.id;Identity.team=static_cast<rift::Team>(Team);
            Identity.cardId=Body.cardId;Identity.playId=Body.playId;Identity.parentEntityId=Body.parentEntityId;
            Identity.attributionCardId=Body.attributionCardId;Context.Entities.Add(Identity.id,std::move(Identity));
        }
    }
    if (FMath::Abs(N(Obj((*States)[0]), TEXT("time"))) > 1e-7) {
        Error = TEXT("Replay is missing its initial snapshot.");
        return false;
    }
    if (FMath::Abs(N(Obj(States->Last()), TEXT("time")) - Duration) > .001) {
        Error = TEXT("Replay is missing its final snapshot.");
        return false;
    }
    uint64 Sequence = 0;
    Previous = -1;
    for (const auto &V : *Events) {
        rift::Event E;
        if (!ReadEvent(Obj(V), E,&Context) || E.time < Previous || E.time > Duration + .001 ||
            E.sequence <= Sequence) {
            Error = TEXT("Replay contains invalid or out-of-order events.");
            return false;
        }
        Previous = E.time;
        Sequence = E.sequence;
    }
    // A sample cannot claim events that occur after its time, or move its bookmark backward.
    int32 EventIndex = 0;
    uint64 AvailableSequence = 0, PreviousBookmark = 0;
    for (const auto &V : *States) {
        auto Sample = Obj(V);
        while (EventIndex < Events->Num() &&
               N(Obj((*Events)[EventIndex]), TEXT("time")) <= N(Sample, TEXT("time")) + 1e-7)
            AvailableSequence = uint64(N(Obj((*Events)[EventIndex++]), TEXT("sequence")));
        if (Sample->HasField(TEXT("eventSequence"))) {
            const uint64 Bookmark = uint64(N(Sample, TEXT("eventSequence")));
            if (Bookmark > AvailableSequence || Bookmark < PreviousBookmark) {
                Error = TEXT("Replay snapshot event bookmarks are inconsistent.");
                return false;
            }
            PreviousBookmark = Bookmark;
        }
    }
    const TSharedPtr<FJsonObject> *Result = nullptr;
    rift::Snapshot Final;
    if (O->HasField(TEXT("result")) &&
        (!O->TryGetObjectField(TEXT("result"), Result) || !ReadSnapshot(*Result, Final,&Context))) {
        Error = TEXT("Replay analysis is invalid.");
        return false;
    }
    if (Result && FMath::Abs(Final.elapsed - Duration) > .001) {
        Error = TEXT("Replay analysis does not match its duration.");
        return false;
    }
    Error.Empty();
    return true;
}
namespace {
void CaptureRecordedState(FRiftReplayBalanceContext& Context,const rift::Snapshot& State) {
    for (const auto& Entity:State.entities)
        if (!Context.Entities.Contains(Entity.id)) Context.Entities.Add(Entity.id,Entity);
    if(Context.Schema>=2)for(int Team=0;Team<2;++Team)for(const auto& Entry:State.entityTelemetry[Team])if(!Context.Entities.Contains(Entry.first)) {
        const auto& Body=Entry.second;rift::Entity Identity;Identity.id=Body.id;Identity.team=static_cast<rift::Team>(Team);
        Identity.cardId=Body.cardId;Identity.playId=Body.playId;Identity.parentEntityId=Body.parentEntityId;
        Identity.attributionCardId=Body.attributionCardId;Context.Entities.Add(Identity.id,std::move(Identity));
    }
    for (const auto& P:State.projectiles) if (!Context.ProjectileIds.Contains(P.id)) {Context.ProjectileIds.Add(P.id);Context.Projectiles.Add(P);}
    for (const auto& Wave:State.shockwaves) if (!Context.WaveIds.Contains(Wave.id)) {Context.WaveIds.Add(Wave.id);Context.Waves.Add(Wave);}
    for (const auto& H:State.hazards) if (!Context.Hazards.Contains(H.playId)) {
        Context.Hazards.Add(H.playId,H);
        const double Interval=(H.nextTick-H.born)/(H.ticks+1);
        if (Interval>0&&FMath::IsFinite(Interval)) Context.HazardIntervals.Add(H.playId,Interval);
    }
}
rift::Entity *EntityById(rift::Snapshot &State, rift::EntityId Id) {
    for (auto &E : State.entities)
        if (E.id == Id)
            return &E;
    return nullptr;
}
void RefreshReplaySlow(rift::Entity& Entity,double Time)
{
    Entity.slows.erase(std::remove_if(Entity.slows.begin(),Entity.slows.end(),
        [&](const auto& Slow){return Slow.until<=Time;}),Entity.slows.end());
    Entity.slowPct=0;
    const rift::SlowEffect* Dominant=nullptr;
    for (const auto& Slow:Entity.slows)
        if (!Dominant||Slow.pct>Dominant->pct || (Slow.pct==Dominant->pct&&Slow.born>Dominant->born)) Dominant=&Slow;
    if (Dominant)
    {
        if (Entity.slowSource!=Dominant->playId||Entity.slowTrackedFrom<0) Entity.slowTrackedFrom=Time;
        Entity.slowPct=Dominant->pct;Entity.slowUntil=Dominant->until;Entity.slowSource=Dominant->playId;
    }
}
void AdvanceSnapshot(rift::Snapshot &State, double To,FRiftReplayBalanceContext* Context) {
    const double Dt = FMath::Max(0.0, To - State.elapsed);
    if (State.phase == rift::Phase::Regulation || State.phase == rift::Phase::Overtime) {
        const double Generated = rift::Match::AetherGenerated(State.elapsed, FMath::Min(To, 300.0));
        for (int I = 0; I < 2; ++I) {
            const double Added = FMath::Min(Generated, 10 - State.aether[I]);
            State.aether[I] += Added;
            State.leaked[I] += Generated - Added;
        }
        for (auto &E : State.entities)
            if (!E.dead && E.kind == rift::EntityKind::Building) {
                if (Context&&Context->HasCapturedDefinitions) {
                    const auto Decay=Context->BuildingDecayRates.find(E.cardId);
                    if (Decay!=Context->BuildingDecayRates.end()) E.hp=FMath::Max(0.,E.hp-Decay->second*Dt);
                    const auto* Definition=RecordedCard(Context,E.cardId);
                    if(Definition&&Context->Schema>=2&&Definition->hardExpiry&&Definition->lifetime>0&&To+1e-9>=E.born+Definition->lifetime)E.hp=0;
                } else if (Dt>0) MissingMechanic(Context,TEXT("Legacy building HP decay between snapshots is unavailable; recorded HP and damage events remain authoritative."));
                if(!E.cardId.empty())State.telemetry[int(E.team)][E.attributionCardId.empty()?E.cardId:E.attributionCardId].buildingLifetime+=Dt;
                auto Body=State.entityTelemetry[int(E.team)].find(E.id);
                if(Body!=State.entityTelemetry[int(E.team)].end())Body->second.observed.buildingLifetime+=Dt;
            }
        State.timeRemaining = FMath::Max(0.0, State.timeRemaining - Dt);
    } else if (State.phase == rift::Phase::Tiebreaker) {
        const double Drain = 180 * (FMath::Max(0.0, State.phaseElapsed + Dt - .85) -
                                    FMath::Max(0.0, State.phaseElapsed - .85));
        for (auto &E : State.entities)
            if (!E.dead && E.kind >= rift::EntityKind::Guard)
                E.hp = FMath::Max(0.0, E.hp - Drain);
    }
    if (State.phase != rift::Phase::Finished)
        State.phaseElapsed += Dt;
    for (auto &P : State.projectiles)
        P.remaining = FMath::Max(0.0, P.remaining - Dt);
    State.projectiles.erase(std::remove_if(State.projectiles.begin(), State.projectiles.end(),
                                           [](const auto &P) { return P.remaining <= 0; }),
                            State.projectiles.end());
    State.hazards.erase(std::remove_if(State.hazards.begin(), State.hazards.end(),
                                       [&](const auto &H) { return H.expires < To; }),
                        State.hazards.end());
    for (auto& Wave:State.shockwaves) Wave.progress=FMath::Clamp((To-Wave.born)*Wave.speed,0.,Wave.radius);
    State.shockwaves.erase(std::remove_if(State.shockwaves.begin(),State.shockwaves.end(),
        [&](const auto& Wave){return To>=Wave.expires;}),State.shockwaves.end());
    State.elapsed = To;
    for (auto& Entity:State.entities) {
        if (!Entity.slows.empty()) RefreshReplaySlow(Entity,To);
        if(Entity.invisibleUntil>0&&To+1e-9>=Entity.invisibleUntil)Entity.invisibleUntil=0;
    }
}
void ApplyReplayEvent(rift::Snapshot &State, const rift::Event &E,FRiftReplayBalanceContext* Context) {
    const int Team = int(E.team);
    auto *Target = EntityById(State, E.target);
    auto *Source = EntityById(State, E.source);
    const auto *Card = RecordedCard(Context,E.cardId);
    auto SourceRecord=State.entityTelemetry[Team].find(E.source);
    const std::string Credit=!E.attributionCardId.empty()?E.attributionCardId:
        SourceRecord!=State.entityTelemetry[Team].end()?SourceRecord->second.attributionCardId:E.cardId;
    auto VictimCredit=[](const rift::Entity& Body)->const std::string& {return Body.attributionCardId.empty()?Body.cardId:Body.attributionCardId;};
    auto AddSpellHazard = [&]() {
        if (!SpellCard(Context,E.cardId)) return;
        if (std::any_of(State.hazards.begin(), State.hazards.end(),
                        [&](const auto &H) { return H.playId == E.playId; }))
            return;
        rift::Hazard H;
        if (Card) {
            if (Card->dotDamage<=0) return;
            H.radius=Card->spellRadius;H.nextTick=E.time+Card->dotInterval;H.expires=E.time+Card->dotDuration;
        } else if (Context) {
            const auto* Captured=Context->Hazards.Find(E.playId);
            const auto* Interval=Context->HazardIntervals.Find(E.playId);
            if (!Captured||!Interval) {
                MissingMechanic(Context,TEXT("Legacy spell-zone geometry or tick interval absent from snapshots; unavailable zones are omitted between snapshots."));return;
            }
            H.radius=Captured->radius;H.nextTick=E.time+*Interval;H.expires=Captured->expires;
        } else return;
        H.playId = E.playId;
        H.team = E.team;
        H.cardId = E.cardId;
        H.position = E.position;
        H.born = E.time;
        State.hazards.push_back(H);
    };
    if (E.type == "card_play") {
        if (!E.sandbox) {
            State.aether[Team] = E.aetherAfter;
            State.spent[Team] += E.amount;
            State.hands[Team] = E.handAfter;
            if (!State.queues[Team].empty()) {
                State.queues[Team].erase(State.queues[Team].begin());
                State.queues[Team].push_back(E.cardId);
            }
            auto &T = State.telemetry[Team][E.cardId];
            ++T.plays;
            T.spent += E.amount;
            T.placementX += E.position.x;
            T.placementZ += E.position.z;
            if (E.openingHand)
                ++T.openingPlays;
            if (E.firstPlay)
                ++T.firstPlays;
            if (E.overtime)
                ++T.overtimePlays;
        }
        // Legacy Meteor recordings created their zone at the instant card play.
        if (E.until <= E.time)
            AddSpellHazard();
    } else if (E.type == "spell_cast" &&
               std::none_of(State.spellCasts.begin(), State.spellCasts.end(),
                            [&](const auto &C) { return C.playId == E.playId; })) {
        rift::SpellCast C;
        C.playId = E.playId;
        C.team = E.team;
        C.cardId = E.cardId;
        C.position = E.position;
        C.born = E.time;
        C.impactAt = E.until;
        State.spellCasts.push_back(C);
    } else if (E.type == "spell_impact") {
        State.spellCasts.erase(std::remove_if(State.spellCasts.begin(), State.spellCasts.end(),
                                            [&](const auto &C) { return C.playId == E.playId; }),
                              State.spellCasts.end());
        AddSpellHazard();
    } else if (E.type == "entity_spawn" && !Source) {
        rift::Entity R;
        if (Card) {
            R.kind=Card->building?rift::EntityKind::Building:rift::EntityKind::Troop;
            R.flying=Card->flying;R.memberCount=E.parentEntityId&&E.count>0?E.count:Card->count;R.radius=Card->collisionRadius;
            if(Context&&Context->Schema>=2)R.cooldown=Card->initialAttackCooldown;
        } else if (Context) {
            const auto* Captured=Context->Entities.Find(E.source);
            if (!Captured||Captured->cardId!=E.cardId||Captured->team!=E.team||Captured->playId!=E.playId) {
                MissingMechanic(Context,TEXT("Legacy spawned entity collision or mobility absent from snapshots; unavailable bodies are omitted between snapshots."));return;
            }
            R.kind=Captured->kind;R.flying=Captured->flying;R.memberCount=Captured->memberCount;R.radius=Captured->radius;
        } else return;
        R.id = E.source;
        R.team = E.team;
        R.cardId = E.cardId;
        R.playId = E.playId;
        R.parentEntityId=E.parentEntityId;R.attributionCardId=E.attributionCardId;
        if(Context&&Context->Schema>=2&&R.attributionCardId.empty())R.attributionCardId=R.cardId;
        R.position = E.position;
        R.facing = {0, E.team == rift::Team::Player ? -1. : 1.};
        R.hp = E.hp;
        R.maxHp = E.maxHp;
        R.shieldHP=E.shieldHP;R.maxShieldHP=E.maxShieldHP;
        R.born = E.time;
        R.lane = E.position.x < 0 ? -1 : 1;
        State.entities.push_back(R);
        auto& Aggregate=State.telemetry[Team][VictimCredit(R)];
        if(R.parentEntityId)++Aggregate.childSpawns;else ++Aggregate.spawns;
        if(Card&&Card->building)Aggregate.buildingCapacity+=Card->lifetime;
        if(Context&&Context->Schema>=2) {
            rift::EntityTelemetry Record;Record.id=R.id;Record.parentEntityId=R.parentEntityId;Record.playId=R.playId;
            Record.cardId=R.cardId;Record.attributionCardId=R.attributionCardId;Record.observed.spawns=1;
            State.entityTelemetry[Team].emplace(R.id,std::move(Record));
        }
    } else if (E.type == "damage" && Target) {
        Target->hp = E.hp;
        if (E.damageKind=="shockwave") for (auto& Wave:State.shockwaves)
            if (Wave.source==E.source&&Wave.playId==E.playId&&Wave.cardId==E.cardId&&
                std::find(Wave.hitTargets.begin(),Wave.hitTargets.end(),E.target)==Wave.hitTargets.end())
                Wave.hitTargets.push_back(E.target);
        auto CreditDamage=[&](rift::CardTelemetry& T) {
            if (E.targetKind >= rift::EntityKind::Guard)
                T.towerDamage += E.amount;
            else if (E.targetKind == rift::EntityKind::Building)
                T.buildingDamage += E.amount;
            else
                T.troopDamage += E.amount;
            T.overkill += E.overkill;
            if (E.damageKind == "initial")
                T.initialDamage += E.amount;
            if (E.damageKind == "dot")
                T.dotDamage += E.amount;
            if (E.damageKind == "aura")
                T.auraDamage += E.amount;
        };
        if(!Credit.empty())CreditDamage(State.telemetry[Team][Credit]);
        if(SourceRecord!=State.entityTelemetry[Team].end())CreditDamage(SourceRecord->second.observed);
        if (!Target->cardId.empty()) {
            auto Taken=[&](rift::CardTelemetry& T){T.damageTaken+=E.amount;if(Target->kind==rift::EntityKind::Building)T.prevented+=E.amount;};
            Taken(State.telemetry[int(Target->team)][VictimCredit(*Target)]);
            auto Record=State.entityTelemetry[int(Target->team)].find(Target->id);
            if(Record!=State.entityTelemetry[int(Target->team)].end())Taken(Record->second.observed);
        }
    } else if(E.type=="shield_damage"&&Target) {
        Target->shieldHP=E.shieldHP;Target->maxShieldHP=E.maxShieldHP;
        if(E.shieldHP<=1e-9)Target->shieldBrokenAt=E.time;
        auto Taken=[&](rift::CardTelemetry& T){T.shieldDamageTaken+=E.amount;T.shieldAbsorbed+=E.amount+E.shieldOverflowBlocked;T.overflowPrevented+=E.shieldOverflowBlocked;};
        if(!Target->cardId.empty())Taken(State.telemetry[int(Target->team)][VictimCredit(*Target)]);
        auto Record=State.entityTelemetry[int(Target->team)].find(Target->id);
        if(Record!=State.entityTelemetry[int(Target->team)].end())Taken(Record->second.observed);
        if(!Credit.empty())State.telemetry[Team][Credit].shieldDamage+=E.amount;
        if(SourceRecord!=State.entityTelemetry[Team].end())SourceRecord->second.observed.shieldDamage+=E.amount;
    } else if ((E.type == "death" || E.type == "tower_destroy") && Target) {
        if(E.type=="death"&&!Target->dead&&!Target->cardId.empty()) {
            auto& Victim=State.telemetry[int(Target->team)][VictimCredit(*Target)];
            if(Target->parentEntityId)++Victim.childDeaths;else ++Victim.deaths;
            Victim.lifetime+=FMath::Max(0.,E.time-Target->born);
            auto Record=State.entityTelemetry[int(Target->team)].find(Target->id);
            if(Record!=State.entityTelemetry[int(Target->team)].end()) {++Record->second.observed.deaths;Record->second.observed.lifetime+=FMath::Max(0.,E.time-Target->born);}
            if(!Credit.empty()) {++State.telemetry[Team][Credit].kills;State.telemetry[Team][Credit].killValue+=E.targetCost;}
            if(SourceRecord!=State.entityTelemetry[Team].end()) {++SourceRecord->second.observed.kills;SourceRecord->second.observed.killValue+=E.targetCost;}
        }
        Target->dead = true;
        Target->hp = 0;
        Target->died = E.time;
        if (E.type == "death")
            Target->position = E.position;
        if (E.type == "tower_destroy")
            State.crowns[Team] =
                E.targetKind == rift::EntityKind::Core ? 3 : FMath::Min(3, State.crowns[Team] + E.crowns);
    } else if (E.type == "core_activate" && Target)
        Target->active = true;
    else if (E.type == "tower_edit" && Target)
        Target->hp = E.hp;
    else if (E.type == "aether_grant")
        State.aether[Team] = E.aetherAfter;
    else if (E.type == "clear_field") {
        State.entities.erase(std::remove_if(State.entities.begin(), State.entities.end(),
                                            [](const auto &R) { return R.kind < rift::EntityKind::Guard; }),
                             State.entities.end());
        State.projectiles.clear();
        State.shockwaves.clear();
        State.spellCasts.clear();
        State.hazards.clear();
    } else if (E.type == "slow" && Target) {
        auto Existing=std::find_if(Target->slows.begin(),Target->slows.end(),[&](const auto& Slow){return Slow.cardId==E.cardId;});
        rift::SlowEffect Slow;Slow.source=E.source;Slow.playId=E.playId;Slow.team=E.team;
        Slow.cardId=E.cardId;Slow.pct=E.amount;Slow.born=E.time;Slow.until=E.until;
        if (Existing==Target->slows.end()) Target->slows.push_back(Slow); else *Existing=Slow;
        RefreshReplaySlow(*Target,E.time);
    } else if (E.type == "stun" && Target) {
        Target->stunUntil = E.until;
        Target->stunSource = E.playId;
    } else if(E.type=="invisibility_apply"&&Target) {
        Target->invisibleUntil=E.until;Target->invisibilitySource=E.source;
        Target->invisibilityPlayId=E.playId;Target->invisibilityCardId=E.cardId;
    } else if(E.type=="invisibility_expire"&&Target) {
        if(Target->invisibilityPlayId==E.playId)Target->invisibleUntil=0;
    } else if(E.type=="target_drop"&&Source) {
        Source->target=Source->hardLock=Source->forcedTarget=0;Source->forcedUntil=0;
        Source->path.clear();Source->repathClock=Source->scanClock=0;
    } else if(E.type=="charge_ready"&&Source) {
        Source->charged=true;Source->chargeTime=E.requested;
    } else if(E.type=="charge_spent"&&Source) {
        Source->charged=false;Source->chargeTime=0;
    } else if ((E.type == "target_pull" || E.type == "building_pull") && Target) {
        Target->target = E.source;
        Target->hardLock = 0;
        if (E.type == "target_pull") {
            Target->forcedTarget = E.source;
            Target->forcedUntil = E.until;
        }
    } else if (E.type == "attack" && Source) {
        Source->target=E.target;
        const auto* SourceCard=RecordedCard(Context,Source->cardId);
        const bool CrownTarget=Target&&(Target->kind==rift::EntityKind::Guard||Target->kind==rift::EntityKind::Core);
        const bool StructureOnly=SourceCard&&rift::TargetMaskFor(*SourceCard)==rift::targets::Structure;
        Source->hardLock=Source->kind==rift::EntityKind::Troop&&Target&&(CrownTarget||(StructureOnly&&Target->kind==rift::EntityKind::Building))?E.target:0;
        if (Source->hardLock) Source->forcedTarget=0;
        if (!SourceCard&&Source->kind==rift::EntityKind::Troop&&Target&&Target->kind==rift::EntityKind::Building)
            MissingMechanic(Context,TEXT("Legacy structures-only target policy absent from captured definitions; between-snapshot building attacks retain a soft target."));
        if (Target) {
            const double X = Target->position.x - Source->position.x,
                         Z = Target->position.z - Source->position.z, L = FMath::Sqrt(X * X + Z * Z);
            if (L > 0)
                Source->facing = {X / L, Z / L};
        }
    } else if (E.type == "projectile_launch") {
        rift::Projectile P;
        P.id = E.sequence;
        P.source = E.source;
        P.target = E.target;
        P.playId = E.playId;
        P.team = E.team;
        P.cardId = E.cardId;
        P.origin = E.position;
        P.damage = E.amount;
        if (Card) P.splash=Card->splash;
        else if (Context) {
            const auto Found=std::find_if(Context->Projectiles.begin(),Context->Projectiles.end(),[&](const auto& Recorded) {
                return Recorded.source==E.source&&Recorded.target==E.target&&Recorded.playId==E.playId&&Recorded.cardId==E.cardId&&
                    FMath::Abs(Recorded.duration-(E.until-E.time))<1e-6;
            });
            if (Found!=Context->Projectiles.end()) P.splash=Found->splash;
            else MissingMechanic(Context,TEXT("Legacy projectile splash absent from snapshots; projectile flight and recorded damage remain authoritative."));
        }
        P.duration = P.remaining = FMath::Max(0.0, E.until - E.time);
        State.projectiles.push_back(P);
    } else if (E.type == "shockwave_launch") {
        rift::Shockwave Wave;Wave.id=E.sequence;Wave.source=E.source;Wave.playId=E.playId;
        Wave.team=E.team;Wave.cardId=E.cardId;Wave.origin=E.position;
        if (Card) {Wave.radius=Card->splash;Wave.speed=Card->shockwaveSpeed;}
        else if (Context) {
            const auto Found=std::find_if(Context->Waves.begin(),Context->Waves.end(),[&](const auto& Recorded) {
                return Recorded.source==E.source&&Recorded.playId==E.playId&&Recorded.cardId==E.cardId&&FMath::Abs(Recorded.born-E.time)<1e-6;
            });
            if (Found==Context->Waves.end()) {MissingMechanic(Context,TEXT("Legacy shockwave radius and speed absent from snapshots; unavailable wave geometry is omitted between snapshots."));return;}
            Wave.radius=Found->radius;Wave.speed=Found->speed;
        } else return;
        Wave.born=E.time;Wave.expires=E.until;
        State.shockwaves.push_back(Wave);
    } else if (E.type == "hazard_tick") {
        for (auto &H : State.hazards)
            if (H.playId == E.playId) {
                ++H.ticks;
                const auto* HazardCard=RecordedCard(Context,H.cardId);
                const auto* Interval=Context?Context->HazardIntervals.Find(H.playId):nullptr;
                if (HazardCard) H.nextTick+=HazardCard->dotInterval;
                else if (Interval) H.nextTick+=*Interval;
                else MissingMechanic(Context,TEXT("Legacy hazard tick interval absent from captured fields; recorded ticks and damage remain authoritative."));
                break;
            }
    } else if (E.type == "phase") {
        State.phase = E.reason == "overtime" ? rift::Phase::Overtime : rift::Phase::Tiebreaker;
        State.phaseElapsed = 0;
        State.timeRemaining = State.phase == rift::Phase::Overtime ? 120 : 0;
        if (State.phase == rift::Phase::Tiebreaker) {
            State.projectiles.clear();
            State.shockwaves.clear();
            State.spellCasts.clear();
            State.hazards.clear();
        }
    } else if (E.type == "match_end") {
        for(const auto& Body:State.entities)if(!Body.dead&&!Body.cardId.empty()) {
            auto& T=State.telemetry[int(Body.team)][VictimCredit(Body)];
            if(Body.parentEntityId)++T.childSurviving;else ++T.surviving;
            T.lifetime+=FMath::Max(0.,E.time-Body.born);
            auto Record=State.entityTelemetry[int(Body.team)].find(Body.id);
            if(Record!=State.entityTelemetry[int(Body.team)].end()) {++Record->second.observed.surviving;Record->second.observed.lifetime+=FMath::Max(0.,E.time-Body.born);}
        }
        State.shockwaves.clear();
        State.spellCasts.clear();
        State.phase = rift::Phase::Finished;
        State.winner = int(E.amount);
        State.resultReason = E.reason;
    } else if (E.type == "ai_decision") {
        auto &AI = State.ai[Team];
        AI.style = E.cardId;
        const auto P = E.reason.find(": ");
        AI.decision = E.reason.substr(0, P);
        AI.reason = P == std::string::npos ? E.reason : E.reason.substr(P + 2);
    }
}
} // namespace
void URiftReplaySubsystem::Initialize(FSubsystemCollectionBase &Collection) {
    Collection.InitializeDependency<URiftProfileSubsystem>();
    Super::Initialize(Collection);
}
void URiftReplaySubsystem::Deinitialize() {
    // World EndPlay owns the final recording save. GameInstance services may
    // already be removed here, so cleanup must not call another subsystem.
    // World EndPlay flushes and publishes while Profile is alive. Late teardown
    // waits for filesystem jobs only and never looks up another GI subsystem.
    CompleteWrites(true, false);
    Recording.Reset();
    Loaded.Reset();
    PlaybackBalance.Reset();
    RecordedEvents.Reset();
    RecordedSamples.Reset();
    PlaybackEvents.Reset();
    FirstDamageTimes.Reset();
    PlaybackTime = PlaybackDuration = 0;
    EventCursor = 0;
    Super::Deinitialize();
}
bool URiftReplaySubsystem::IsTickable() const {
    return !IsTemplate() && IsSaving();
}
TStatId URiftReplaySubsystem::GetStatId() const {
    RETURN_QUICK_DECLARE_CYCLE_STAT(URiftReplaySubsystem, STATGROUP_Tickables);
}
void URiftReplaySubsystem::Tick(float DeltaTime) {
    CompleteWrites(false);
}
void URiftReplaySubsystem::CompleteWrites(bool Wait, bool Publish) {
    // Publish in recording order even when a later small file finishes first.
    while (!PendingWrites.IsEmpty()) {
        auto Job = PendingWrites[0];
        if (!Wait && !Job->Work.IsReady())
            break;
        Job->Work.Get();
        PendingWrites.RemoveAt(0);
        if (Job->Saved) {
            if (Job->Filename == LatestQueuedFilename) {
                LatestFilename = Job->Filename;
                LastError.Empty();
            }
            auto *GI = Publish ? GetGameInstance() : nullptr;
            auto *Profile = GI ? GI->GetSubsystem<URiftProfileSubsystem>() : nullptr;
            if (Profile) {
                Profile->ReplayFiles.AddUnique(Job->Filename);
                if (!Profile->Save())
                    RIFT_LOG(LogRift, Warning, TEXT("Replay saved but profile index could not be saved: %s"),
                             *Profile->LastError);
            }
        } else {
            if (Job->Filename == LatestQueuedFilename)
                LastError = Job->Error;
            RIFT_LOG(LogRift, Warning, TEXT("Replay could not be saved: %s"), *Job->Error);
        }
        RIFT_LOG(LogRift, Log,
                 TEXT("Replay write %s: saved=%d events=%d samples=%d chars=%d stored=%d validate=%.2fms "
                      "serialize=%.2fms compress=%.2fms write=%.2fms"),
                 *Job->Filename, Job->Saved, Job->Events, Job->Samples, Job->Characters, Job->StoredBytes,
                 Job->ValidateMS, Job->SerializeMS, Job->CompressMS, Job->WriteMS);
    }
}
bool URiftReplaySubsystem::FlushPendingWrites() {
    CompleteWrites(true);
    return LastError.IsEmpty();
}
void URiftReplaySubsystem::BeginRecording(const rift::MatchOptions &Options, bool Training) {
    Recording = MakeShared<FJsonObject>();
    RecordedEvents.Reset();
    RecordedSamples.Reset();
    LastSample = -1;
    RecordedSequence = 0;
    RecordedMinimumBytes = 0;
    RecordingBudgetError.Empty();
    Recording->SetNumberField(TEXT("formatVersion"), 1);
    Recording->SetStringField(TEXT("model"), TEXT("rift-native-1"));
    Recording->SetObjectField(TEXT("balance"),BalanceJSON());
    Recording->SetStringField(TEXT("id"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
    Recording->SetStringField(TEXT("createdAt"), FDateTime::UtcNow().ToIso8601());
    Recording->SetBoolField(TEXT("training"), Training);
    Recording->SetNumberField(TEXT("seed"), Options.seed);
    auto *GI = GetGameInstance();
    auto *Profile = GI ? GI->GetSubsystem<URiftProfileSubsystem>() : nullptr;
    Recording->SetStringField(TEXT("player"), Profile ? Profile->Username : TEXT("RIFTBOUND"));
    TArray<TSharedPtr<FJsonValue>> Decks;
    for (int I = 0; I < 2; ++I) {
        auto D = MakeShared<FJsonObject>();
        D->SetArrayField(TEXT("cards"), Strings(Options.decks[I]));
        D->SetStringField(TEXT("style"), FS(Options.aiStyles[I]));
        Decks.Add(MakeShared<FJsonValueObject>(D));
    }
    Recording->SetArrayField(TEXT("decks"), Decks);
}
void URiftReplaySubsystem::RejectRecording(const FString& Error) {
    RecordingBudgetError=Error;
    // Reject the whole oversized archive. Never publish a truncated replay; the
    // live match and its analytical counters remain owned by the simulation.
    RecordedEvents.Reset();
    RecordedSamples.Reset();
}
bool URiftReplaySubsystem::AccountRecordingObject(const TSharedPtr<FJsonObject>& Object) {
    int64 Bytes=RecordedMinimumBytes;
    if (!MinimumJSONObjectBytes(Object,Bytes,DecodedLimit) || !AddMinimumBytes(1,Bytes,DecodedLimit)) {
        RejectRecording(ReplaySizeError);
        return false;
    }
    // The omitted outer field names/brackets exceed these per-element separators,
    // so this remains a lower bound and cannot reject a document that fits.
    RecordedMinimumBytes=Bytes;
    return true;
}
void URiftReplaySubsystem::RecordEvent(const rift::Event &E) {
    if (Recording && RecordingBudgetError.IsEmpty()) {
        if (RecordedEvents.Num()>=200000) {RejectRecording(TEXT("Replay exceeds its 200,000 event limit."));return;}
        auto Object=EventJSON(E);
        if (!AccountRecordingObject(Object)) return;
        RecordedEvents.Add(MakeShared<FJsonValueObject>(Object));
        RecordedSequence = E.sequence;
    }
}
void URiftReplaySubsystem::Sample(const rift::Snapshot &State) {
    if (Recording && RecordingBudgetError.IsEmpty() && State.elapsed >= LastSample) {
        if (RecordedSamples.Num()>=20000) {RejectRecording(TEXT("Replay exceeds its 20,000 snapshot limit."));return;}
        auto O = SnapshotJSON(State);
        O->SetNumberField(TEXT("eventSequence"), double(RecordedSequence));
        if (!AccountRecordingObject(O)) return;
        RecordedSamples.Add(MakeShared<FJsonValueObject>(O));
        LastSample = State.elapsed;
    }
}
bool URiftReplaySubsystem::SerializeRecording(const TSharedPtr<FJsonObject>& Object,TArray<uint8>& UTF8Bytes,
                                              FString& Error,int64 ByteLimit) {
    UTF8Bytes.Reset();
    int64 MinimumBytes=0;
    if (ByteLimit<=0 || ByteLimit>DecodedLimit || !MinimumJSONObjectBytes(Object,MinimumBytes,ByteLimit)) {
        Error=ReplaySizeError;
        return false;
    }
    FRiftBoundedReplayWriter Stream(UTF8Bytes,ByteLimit);
    const bool Serialized=FJsonSerializer::Serialize(Object.ToSharedRef(),
        TJsonWriterFactory<UTF8CHAR,TCondensedJsonPrintPolicy<UTF8CHAR>>::Create(&Stream));
    if (Stream.IsError() || !Serialized) {
        UTF8Bytes.Reset();
        Error=Stream.IsError()?ReplaySizeError:TEXT("Replay JSON could not be serialized.");
        return false;
    }
    Error.Empty();
    return true;
}
void URiftReplaySubsystem::EndRecording(const rift::Snapshot &State, bool Abandoned) {
    if (!Recording)
        return;
    Sample(State);
    if (RecordingBudgetError.IsEmpty()) {
        auto Result=SnapshotJSON(State);
        if (AccountRecordingObject(Result)) {
            Recording->SetNumberField(TEXT("duration"), State.elapsed);
            Recording->SetBoolField(TEXT("abandoned"), Abandoned);
            Recording->SetObjectField(TEXT("result"),Result);
            Recording->SetArrayField(TEXT("events"), RecordedEvents);
            Recording->SetArrayField(TEXT("states"), RecordedSamples);
        }
    }
    auto Job = MakeShared<FRiftReplayWriteJob, ESPMode::ThreadSafe>();
    Job->Filename = S(Recording, TEXT("id")) + TEXT(".riftreplay");
    Job->Path = SafeReplayPath(Job->Filename);
    LatestQueuedFilename = Job->Filename;
    Job->Events = RecordedEvents.Num();
    Job->Samples = RecordedSamples.Num();
    // Transfer the completed JSON tree. No UObject, live recording or profile is read by the worker.
    auto Document = MoveTemp(Recording);
    auto BudgetError=MoveTemp(RecordingBudgetError);
    RecordedEvents.Reset();
    RecordedSamples.Reset();
    LatestFilename.Empty();
    LastError.Empty();
    Job->Work = Async(EAsyncExecution::ThreadPool, [Job, Document = MoveTemp(Document),BudgetError=MoveTemp(BudgetError)]() {
        if (!BudgetError.IsEmpty()) {Job->Error=BudgetError;return;}
        TArray<uint8> UTF8Bytes;
        double Started = FPlatformTime::Seconds();
        const bool Valid = URiftReplaySubsystem::ValidateRecording(Document, Job->Error);
        Job->ValidateMS = (FPlatformTime::Seconds() - Started) * 1000;
        if (!Valid)
            return;
        Started = FPlatformTime::Seconds();
        const bool Serialized = URiftReplaySubsystem::SerializeRecording(Document,UTF8Bytes,Job->Error);
        Job->SerializeMS = (FPlatformTime::Seconds() - Started) * 1000;
        if (!Serialized) {
            return;
        }
        Job->Characters = FPlatformString::ConvertedLength<TCHAR>(reinterpret_cast<const UTF8CHAR*>(UTF8Bytes.GetData()),UTF8Bytes.Num());
        TArray<uint8> Archive;
        Started = FPlatformTime::Seconds();
        const bool Encoded = EncodeRecording(UTF8Bytes, Archive, Job->Error);
        Job->CompressMS = (FPlatformTime::Seconds() - Started) * 1000;
        if (!Encoded)
            return;
        Job->StoredBytes = Archive.Num();
        Started = FPlatformTime::Seconds();
        Job->Saved = URiftProfileSubsystem::AtomicWriteBytes(Job->Path, Archive, Job->Error);
        Job->WriteMS = (FPlatformTime::Seconds() - Started) * 1000;
    });
    PendingWrites.Add(MoveTemp(Job));
}
bool URiftReplaySubsystem::OpenReplay(const FString &Filename) {
    FString Text;
    TSharedPtr<FJsonObject> O;
    if (!ReadRecordingFile(SafeReplayPath(Filename), O, Text, LastError))
        return false;
    TSharedPtr<FRiftReplayBalanceContext> Balance=MakeShared<FRiftReplayBalanceContext>();
    if (!ReadBalance(O,*Balance)) {LastError=TEXT("Replay captured balance definitions are invalid.");return false;}
    const TArray<TSharedPtr<FJsonValue>>* RecordedStates=nullptr;
    if (!O->TryGetArrayField(TEXT("states"),RecordedStates)) return false;
    // Legacy archives need sparse captured fields; schema2 also retains stable
    // source identities after corpse removal for charge/child event validation.
    if (!Balance->HasCapturedDefinitions||Balance->Schema>=2) for (const auto& Value:*RecordedStates) {
        rift::Snapshot State;
        if (!ReadSnapshot(Obj(Value),State,Balance.Get())) return false;
        CaptureRecordedState(*Balance,State);
    }
    if (!Balance->HasCapturedDefinitions) Balance->Limitations.Add(TEXT("Legacy recording has no captured mechanical definitions. Full snapshots and recorded event amounts are authoritative; absent between-snapshot mechanics cannot be recovered from current balance."));
    const TArray<TSharedPtr<FJsonValue>> *Events = nullptr;
    O->TryGetArrayField(TEXT("events"), Events);
    TArray<rift::Event> ParsedEvents;
    for (const auto &V : *Events) {
        rift::Event E;
        if (!ReadEvent(Obj(V), E,Balance.Get()))
            return false;
        ParsedEvents.Add(MoveTemp(E));
    }
    auto *W = GetWorld();
    auto *M = W ? W->GetSubsystem<URiftMatchSubsystem>() : nullptr;
    if (M)
        M->LeaveMatch();
    Loaded = O;
    PlaybackBalance=MoveTemp(Balance);
    PlaybackEvents = MoveTemp(ParsedEvents);
    FirstDamageTimes.Reset();
    // Crown structures exist in the initial snapshot, including their native
    // maximum HP. DEV HP edits must reveal the same bar in live play and replay.
    TMap<uint64,double> CrownMaxHP;
    const TArray<TSharedPtr<FJsonValue>>* States=nullptr;
    rift::Snapshot Initial;
    if(O->TryGetArrayField(TEXT("states"),States) && !States->IsEmpty() && ReadSnapshot(Obj((*States)[0]),Initial,PlaybackBalance.Get()))
        for(const auto& Entity:Initial.entities)
            if(Entity.kind==rift::EntityKind::Guard || Entity.kind==rift::EntityKind::Core)
                CrownMaxHP.Add(Entity.id,Entity.maxHp);
    for(const auto& Event:PlaybackEvents)
    {
        const auto* Maximum=CrownMaxHP.Find(Event.target);
        const bool PositiveHit=Event.type=="damage" && FMath::IsFinite(Event.amount) && Event.amount>0.;
        const bool LoweredTower=Event.type=="tower_edit" && Maximum && FMath::IsFinite(Event.hp) && Event.hp<*Maximum;
        if(Event.target && (PositiveHit || LoweredTower))
        {
            if(auto* First=FirstDamageTimes.Find(Event.target))*First=FMath::Min(*First,Event.time);
            else FirstDamageTimes.Add(Event.target,Event.time);
        }
    }
    PlaybackTime = 0;
    PlaybackDuration = N(O, TEXT("duration"));
    PlaybackSpeed = 1;
    UpdateView();
    ResetEventCursor();
    if (M)
        M->SetReplayView(&View);
    LastError.Empty();
    return true;
}
void URiftReplaySubsystem::CloseReplay() {
    auto *W = GetWorld();
    auto *M = W ? W->GetSubsystem<URiftMatchSubsystem>() : nullptr;
    if (M)
        M->SetReplayView(nullptr);
    Loaded.Reset();
    PlaybackBalance.Reset();
    PlaybackEvents.Reset();
    FirstDamageTimes.Reset();
    EventCursor = 0;
    PlaybackTime = PlaybackDuration = 0;
    PlaybackSpeed = 0;
}
void URiftReplaySubsystem::ResetEventCursor() {
    EventCursor = 0;
    while (EventCursor < PlaybackEvents.Num() && PlaybackEvents[EventCursor].time <= PlaybackTime + 1e-7)
        ++EventCursor;
}
bool URiftReplaySubsystem::HasTakenDamageBy(uint64 EntityId,double Time) const {
    const auto* First=FirstDamageTimes.Find(EntityId);
    return First && FMath::IsFinite(Time) && *First<=Time;
}
void URiftReplaySubsystem::Seek(float Seconds) {
    if (!FMath::IsFinite(Seconds) || !Loaded)
        return;
    // The slider's maximum can round below the recording's last timestamp.
    // An explicit seek to that maximum still means the exact recorded end.
    PlaybackTime = Seconds >= Duration() ? PlaybackDuration : FMath::Clamp(double(Seconds), 0.0, PlaybackDuration);
    UpdateView();
    ResetEventCursor();
}
void URiftReplaySubsystem::SetSpeed(float Value) {
    static const float Allowed[] = {0, .25f, .5f, 1, 2, 4};
    for (float V : Allowed)
        if (FMath::IsNearlyEqual(V, Value))
            PlaybackSpeed = V;
}
void URiftReplaySubsystem::Advance(float DeltaTime) {
    if (!Loaded || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0)
        return;
    const double Before = PlaybackTime;
    PlaybackTime = FMath::Min(PlaybackDuration, PlaybackTime + double(DeltaTime) * PlaybackSpeed);
    UpdateView();
    auto *W = GetWorld();
    auto *M = W ? W->GetSubsystem<URiftMatchSubsystem>() : nullptr;
    while (EventCursor < PlaybackEvents.Num() && PlaybackEvents[EventCursor].time <= PlaybackTime + 1e-7) {
        const auto &E = PlaybackEvents[EventCursor++];
        if (M && E.time > Before + 1e-7)
            M->OnEvent.Broadcast(E);
    }
    if (PlaybackTime >= PlaybackDuration)
        PlaybackSpeed = 0;
}
void URiftReplaySubsystem::UpdateView() {
    if (!Loaded)
        return;
    const TArray<TSharedPtr<FJsonValue>> *States = nullptr;
    if (!Loaded->TryGetArrayField(TEXT("states"), States) || States->IsEmpty())
        return;
    int32 Low = 0, High = States->Num() - 1;
    while (Low < High) {
        const int32 Mid = (Low + High + 1) / 2;
        if (N(Obj((*States)[Mid]), TEXT("time")) <= PlaybackTime + 1e-7)
            Low = Mid;
        else
            High = Mid - 1;
    }
    auto Base = Obj((*States)[Low]);
    rift::Snapshot Current;
    if (!ReadSnapshot(Base, Current,PlaybackBalance.Get()))
        return;
    const double BaseTime = Current.elapsed;
    const bool HasSequence = Base->HasField(TEXT("eventSequence"));
    const uint64 Sequence = uint64(N(Base, TEXT("eventSequence")));
    int32 FirstEvent = 0, EventEnd = PlaybackEvents.Num();
    while (FirstEvent < EventEnd) {
        const int32 Mid = (FirstEvent + EventEnd) / 2;
        const auto &Event = PlaybackEvents[Mid];
        if (HasSequence ? Event.sequence <= Sequence : Event.time <= BaseTime + 1e-7)
            FirstEvent = Mid + 1;
        else
            EventEnd = Mid;
    }
    for (int32 I = FirstEvent; I < PlaybackEvents.Num(); ++I) {
        const auto &E = PlaybackEvents[I];
        if (E.time > PlaybackTime + 1e-7)
            break;
        AdvanceSnapshot(Current, E.time,PlaybackBalance.Get());
        ApplyReplayEvent(Current, E,PlaybackBalance.Get());
    }
    AdvanceSnapshot(Current, PlaybackTime,PlaybackBalance.Get());
    if (Low + 1 < States->Num()) {
        rift::Snapshot Next;
        if (ReadSnapshot(Obj((*States)[Low + 1]), Next,PlaybackBalance.Get())) {
            TMap<uint64, const rift::Entity *> NextEntities;
            for (const auto &E : Next.entities)
                NextEntities.Add(E.id, &E);
            for (auto &E : Current.entities) {
                const auto *Found = NextEntities.Find(E.id);
                if (!Found || E.dead)
                    continue;
                const auto &NE = **Found;
                const double Start = FMath::Max(BaseTime, E.born);
                const double End =
                    NE.dead && NE.died > Start ? FMath::Min(Next.elapsed, NE.died) : Next.elapsed;
                const double Alpha =
                    FMath::Clamp((PlaybackTime - Start) / FMath::Max(.0001, End - Start), 0.0, 1.0);
                E.position.x = FMath::Lerp(E.position.x, NE.position.x, Alpha);
                E.position.z = FMath::Lerp(E.position.z, NE.position.z, Alpha);
            }
        }
    }
    View = MoveTemp(Current);
}
TSharedPtr<FJsonObject> URiftReplaySubsystem::CurrentAnalysis() const {
    const TSharedPtr<FJsonObject> *R = nullptr;
    return Loaded && Loaded->TryGetObjectField(TEXT("result"), R) ? *R : nullptr;
}
TSharedRef<FJsonObject> URiftReplaySubsystem::ReconstructionDiagnostics() const {
    auto Result=MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("hasCapturedDefinitions"),PlaybackBalance&&PlaybackBalance->HasCapturedDefinitions);
    Result->SetNumberField(TEXT("balanceSchema"),PlaybackBalance?PlaybackBalance->Schema:0);
    Result->SetStringField(TEXT("manifestHash"),PlaybackBalance?PlaybackBalance->ManifestHash:FString());
    Result->SetStringField(TEXT("manifestVersion"),PlaybackBalance?PlaybackBalance->ManifestVersion:FString());
    Result->SetStringField(TEXT("hashMeaning"),TEXT("Recorded canonical-manifest provenance; not archive authentication or a digest of the mechanical subset."));
    TArray<FString> Limits;
    if (PlaybackBalance) for (const auto& Limit:PlaybackBalance->Limitations) Limits.Add(Limit);
    Limits.Sort();TArray<TSharedPtr<FJsonValue>> Values;
    for (const auto& Limit:Limits) Values.Add(MakeShared<FJsonValueString>(Limit));
    Result->SetArrayField(TEXT("limitations"),Values);
    Result->SetNumberField(TEXT("capturedEntityTemplates"),PlaybackBalance?PlaybackBalance->Entities.Num():0);
    return Result;
}
TArray<TSharedPtr<FJsonObject>> URiftReplaySubsystem::EventsNear(double Time, double Window) const {
    TArray<TSharedPtr<FJsonObject>> Out;
    const TArray<TSharedPtr<FJsonValue>> *A = nullptr;
    if (Loaded && Loaded->TryGetArrayField(TEXT("events"), A))
        for (const auto &V : *A) {
            auto E = Obj(V);
            if (E && N(E, TEXT("time")) >= Time - Window && N(E, TEXT("time")) <= Time)
                Out.Add(E);
        }
    return Out;
}
TArray<TSharedPtr<FJsonObject>> URiftReplaySubsystem::RecordedStates() const {
    TArray<TSharedPtr<FJsonObject>> Out;
    const TArray<TSharedPtr<FJsonValue>> *A = nullptr;
    if (Loaded && Loaded->TryGetArrayField(TEXT("states"), A))
        for (const auto &V : *A) {
            auto O = Obj(V);
            if (O)
                Out.Add(O);
        }
    return Out;
}
bool URiftReplaySubsystem::ImportReplay(const FString &Filename) {
    FString Text;
    TSharedPtr<FJsonObject> O;
    if (!ReadRecordingFile(Filename, O, Text, LastError))
        return false;
    const FString Name = FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".riftreplay");
    TArray<uint8> Archive;
    if (!EncodeRecording(Text, Archive, LastError) ||
        !URiftProfileSubsystem::AtomicWriteBytes(SafeReplayPath(Name), Archive, LastError))
        return false;
    auto *GI = GetGameInstance();
    auto *P = GI ? GI->GetSubsystem<URiftProfileSubsystem>() : nullptr;
    if (P) {
        P->ReplayFiles.AddUnique(Name);
        return P->Save();
    }
    return true;
}
bool URiftReplaySubsystem::ExportReplay(const FString &Filename, const FString &Destination) {
    FString Text;
    TSharedPtr<FJsonObject> O;
    if (!ReadRecordingFile(SafeReplayPath(Filename), O, Text, LastError))
        return false;
    return URiftProfileSubsystem::AtomicWrite(Destination, Text, LastError);
}
bool URiftReplaySubsystem::DeleteReplay(const FString &Filename) {
    if (!IFileManager::Get().Delete(*SafeReplayPath(Filename), false, true))
        return false;
    auto *GI = GetGameInstance();
    auto *P = GI ? GI->GetSubsystem<URiftProfileSubsystem>() : nullptr;
    if (P) {
        P->ReplayFiles.Remove(FPaths::GetCleanFilename(Filename));
        return P->Save();
    }
    return true;
}
