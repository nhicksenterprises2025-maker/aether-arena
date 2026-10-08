#include "RiftReplaySubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "Serialization/JsonSerializer.h"
#include <limits>
#include <type_traits>

namespace {
constexpr int64 ReplayLimit = 64 * 1024 * 1024;
constexpr double MaxTime = 3600;
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
struct Reader {
    TSharedPtr<FJsonObject> O;
    bool Valid = true;
    explicit Reader(TSharedPtr<FJsonObject> Object) : O(Object), Valid(Object.IsValid()) {}
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
            if (!V || !V->TryGetString(Id) || !rift::FindCard(TCHAR_TO_UTF8(*Id))) {
                Valid = false;
                return;
            }
            Out.emplace_back(TCHAR_TO_UTF8(*Id));
        }
    }
};
#define TELEMETRY_DOUBLES(X)                                                                                 \
    X(spent)                                                                                                 \
    X(troopDamage)                                                                                           \
    X(towerDamage)                                                                                           \
    X(buildingDamage) X(damageTaken) X(kills) X(deaths) X(killValue) X(lifetime) X(slowTime) X(stunTime)     \
        X(initialDamage) X(dotDamage) X(auraDamage) X(overkill) X(prevented) X(spellValue) X(zoneOccupancy)  \
            X(zoneSeconds) X(crownContribution) X(buildingLifetime) X(buildingCapacity)                      \
                X(slowTrackedSeconds) X(stunTrackedSeconds)
#define TELEMETRY_COUNTS(X)                                                                                  \
    X(plays)                                                                                                 \
    X(spawns)                                                                                                \
    X(surviving)                                                                                             \
    X(pulls) X(connected) X(targets) X(openingPlays) X(firstPlays) X(overtimePlays) X(stunned) X(dotTicks)   \
        X(openingEligible)
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
    X(forcedTarget) X(playId) X(slowSource) X(stunSource) X(hp) X(maxHp) X(radius) X(cooldown) X(born)       \
        X(died) X(slowPct) X(slowUntil) X(stunUntil) X(forcedUntil) X(chargeTime) X(auraClock)               \
            X(repathClock) X(scanClock) X(stuckClock) X(stuckTime) X(stuckDistance) X(slowTrackedFrom)       \
                X(stunTrackedFrom) X(lane) X(bridge) X(memberCount)
#define EVENT_NUMBERS(X)                                                                                     \
    X(sequence)                                                                                              \
    X(time)                                                                                                  \
    X(source)                                                                                                \
    X(target) X(playId) X(amount) X(requested) X(overkill) X(hp) X(maxHp) X(aetherBefore) X(aetherAfter)     \
        X(until) X(targetCost) X(handIndex) X(count) X(crowns)
TSharedRef<FJsonObject> EventJSON(const rift::Event &E) {
    auto O = MakeShared<FJsonObject>();
    O->SetStringField(TEXT("type"), FS(E.type));
    O->SetStringField(TEXT("reason"), FS(E.reason));
    O->SetStringField(TEXT("cardId"), FS(E.cardId));
    O->SetStringField(TEXT("damageKind"), FS(E.damageKind));
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
bool ReadEvent(const TSharedPtr<FJsonObject> &O, rift::Event &E) {
    Reader R(O);
    R.Text(TEXT("type"), E.type, true);
    R.Text(TEXT("reason"), E.reason);
    R.Text(TEXT("cardId"), E.cardId);
    R.Text(TEXT("damageKind"), E.damageKind);
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
                if (!(*Hand)[I] || !(*Hand)[I]->TryGetString(Id))
                    R.Valid = false;
                else
                    E.handAfter[I] = TCHAR_TO_UTF8(*Id);
            }
    }
    return R.Valid && !E.type.empty() && E.sequence > 0 && E.time >= 0 && E.time <= MaxTime &&
           (E.cardId.empty() || E.type == "ai_decision" || rift::FindCard(E.cardId));
}
FString SafeReplayPath(const FString &Name) {
    return FPaths::Combine(URiftProfileSubsystem::SaveRoot(), TEXT("Replays"),
                           FPaths::GetCleanFilename(Name));
}
bool ReadRecordingFile(const FString &Filename, TSharedPtr<FJsonObject> &Out, FString &Text, FString &Error) {
    const int64 Size = IFileManager::Get().FileSize(*Filename);
    if (Size < 0 || Size > ReplayLimit) {
        Error = TEXT("Replay is missing or exceeds the 64 MB limit.");
        return false;
    }
    if (!FFileHelper::LoadFileToString(Text, *Filename) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out)) {
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
        Teams.Add(MakeShared<FJsonValueObject>(T));
    }
    O->SetArrayField(TEXT("teams"), Teams);
    TArray<TSharedPtr<FJsonValue>> Entities;
    for (const auto &E : State.entities) {
        auto R = MakeShared<FJsonObject>();
        R->SetStringField(TEXT("cardId"), FS(E.cardId));
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
        FIELD(playId) FIELD(team) FIELD(damage) FIELD(splash) FIELD(remaining) FIELD(duration)
#undef FIELD
            R->SetStringField(TEXT("cardId"), FS(P.cardId));
        Point(R, TEXT("origin"), P.origin);
        Projectiles.Add(MakeShared<FJsonValueObject>(R));
    }
    O->SetArrayField(TEXT("projectiles"), Projectiles);
    TArray<TSharedPtr<FJsonValue>> Hazards;
    for (const auto &H : State.hazards) {
        auto R = MakeShared<FJsonObject>();
#define FIELD(K) R->SetNumberField(TEXT(#K), double(H.K));
        FIELD(playId)
        FIELD(team)
        FIELD(radius)
        FIELD(born) FIELD(nextTick) FIELD(expires) FIELD(ticks)
#undef FIELD
            R->SetStringField(TEXT("cardId"), FS(H.cardId));
        Point(R, TEXT("position"), H.position);
        Hazards.Add(MakeShared<FJsonValueObject>(R));
    }
    O->SetArrayField(TEXT("hazards"), Hazards);
    return O;
}
bool URiftReplaySubsystem::SnapshotFromJSON(const TSharedPtr<FJsonObject> &O, rift::Snapshot &State) {
    rift::Snapshot Parsed;
    Reader R(O);
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
        Reader T(Obj((*Teams)[I]));
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
        if (!rift::ValidateDeck(Parsed.decks[I]) || !rift::ValidateDeck(Cycle))
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
            if (!T.O->TryGetObjectField(TEXT("telemetry"), Metrics) || (*Metrics)->Values.Num() > 14)
                T.Valid = false;
            else
                for (const auto &Pair : (*Metrics)->Values) {
                    rift::CardTelemetry Tele;
                    if (!rift::FindCard(TCHAR_TO_UTF8(*Pair.Key)) || !ReadTelemetry(Obj(Pair.Value), Tele))
                        T.Valid = false;
                    else
                        Parsed.telemetry[I].emplace(TCHAR_TO_UTF8(*Pair.Key), Tele);
                }
        }
        R.Valid &= T.Valid;
    }
    const TArray<TSharedPtr<FJsonValue>> *A = nullptr;
    TSet<uint64> EntityIds;
    if (!R.List(TEXT("entities"), A, 5000, true))
        return false;
    for (const auto &V : *A) {
        rift::Entity E;
        Reader T(Obj(V));
        T.Text(TEXT("cardId"), E.cardId, true);
        T.Vec(TEXT("position"), E.position, true);
        T.Vec(TEXT("facing"), E.facing);
        T.Vec(TEXT("pathTarget"), E.pathTarget);
        T.Vec(TEXT("stuckPosition"), E.stuckPosition);
#define FIELD(K) T.Number(TEXT(#K), E.K);
        ENTITY_NUMBERS(FIELD)
#undef FIELD
        T.Number(TEXT("team"), E.team, 0, 1, true);
        T.Number(TEXT("kind"), E.kind, 0, 3, true);
        T.Flag(TEXT("active"), E.active);
        T.Flag(TEXT("dead"), E.dead);
        T.Flag(TEXT("flying"), E.flying);
        T.Flag(TEXT("charged"), E.charged);
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
            (!E.cardId.empty() && !rift::FindCard(E.cardId)) ||
            (E.cardId.empty() && E.kind < rift::EntityKind::Guard))
            T.Valid = false;
        EntityIds.Add(E.id);
        R.Valid &= T.Valid;
        Parsed.entities.push_back(E);
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
            FIELD(playId) FIELD(damage) FIELD(splash) FIELD(remaining) FIELD(duration)
#undef FIELD
                T.Number(TEXT("team"), P.team, 0, 1, true);
            if (!P.cardId.empty() && !rift::FindCard(P.cardId))
                T.Valid = false;
            R.Valid &= T.Valid;
            Parsed.projectiles.push_back(P);
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
            FIELD(nextTick) FIELD(expires) FIELD(ticks)
#undef FIELD
                T.Number(TEXT("team"), H.team, 0, 1, true);
            if (!rift::FindCard(H.cardId))
                T.Valid = false;
            R.Valid &= T.Valid;
            Parsed.hazards.push_back(H);
        }
    if (!R.Valid)
        return false;
    State = MoveTemp(Parsed);
    return true;
}
bool URiftReplaySubsystem::ValidateRecording(const TSharedPtr<FJsonObject> &O, FString &Error) {
    Reader R(O);
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
        if (!SnapshotFromJSON(Obj(V), State) || State.elapsed < Previous || State.elapsed > Duration + .001) {
            Error = TEXT("Replay contains invalid or out-of-order snapshots.");
            return false;
        }
        Previous = State.elapsed;
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
        if (!ReadEvent(Obj(V), E) || E.time < Previous || E.time > Duration + .001 ||
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
        (!O->TryGetObjectField(TEXT("result"), Result) || !SnapshotFromJSON(*Result, Final))) {
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
rift::Entity *EntityById(rift::Snapshot &State, rift::EntityId Id) {
    for (auto &E : State.entities)
        if (E.id == Id)
            return &E;
    return nullptr;
}
void AdvanceSnapshot(rift::Snapshot &State, double To) {
    const double Dt = FMath::Max(0.0, To - State.elapsed);
    if (State.phase == rift::Phase::Regulation || State.phase == rift::Phase::Overtime) {
        const double Generated = rift::Match::AetherGenerated(State.elapsed, FMath::Min(To, 300.0));
        for (int I = 0; I < 2; ++I) {
            const double Added = FMath::Min(Generated, 10 - State.aether[I]);
            State.aether[I] += Added;
            State.leaked[I] += Generated - Added;
        }
        for (auto &E : State.entities)
            if (!E.dead && E.kind == rift::EntityKind::Building)
                E.hp = FMath::Max(0.0, E.hp - 34 * Dt);
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
    State.elapsed = To;
}
void ApplyReplayEvent(rift::Snapshot &State, const rift::Event &E) {
    const int Team = int(E.team);
    auto *Target = EntityById(State, E.target);
    auto *Source = EntityById(State, E.source);
    const auto *Card = rift::FindCard(E.cardId);
    if (E.type == "card_play") {
        if (!E.sandbox && Card) {
            State.aether[Team] = E.aetherAfter;
            State.spent[Team] += Card->cost;
            State.hands[Team] = E.handAfter;
            if (!State.queues[Team].empty()) {
                State.queues[Team].erase(State.queues[Team].begin());
                State.queues[Team].push_back(E.cardId);
            }
            auto &T = State.telemetry[Team][E.cardId];
            ++T.plays;
            T.spent += Card->cost;
            T.placementX += E.position.x;
            T.placementZ += E.position.z;
            if (E.openingHand)
                ++T.openingPlays;
            if (E.firstPlay)
                ++T.firstPlays;
            if (E.overtime)
                ++T.overtimePlays;
        }
        if (Card && Card->dotDamage > 0) {
            rift::Hazard H;
            H.playId = E.playId;
            H.team = E.team;
            H.cardId = E.cardId;
            H.position = E.position;
            H.radius = Card->spellRadius;
            H.born = E.time;
            H.nextTick = E.time + Card->dotInterval;
            H.expires = E.time + Card->dotDuration;
            State.hazards.push_back(H);
        }
    } else if (E.type == "entity_spawn" && Card && !Source) {
        rift::Entity R;
        R.id = E.source;
        R.team = E.team;
        R.kind = Card->building ? rift::EntityKind::Building : rift::EntityKind::Troop;
        R.cardId = E.cardId;
        R.playId = E.playId;
        R.position = E.position;
        R.facing = {0, E.team == rift::Team::Player ? -1. : 1.};
        R.hp = E.hp;
        R.maxHp = E.maxHp;
        R.born = E.time;
        R.flying = Card->flying;
        R.memberCount = Card->count;
        R.radius = Card->building ? .9 : Card->flying ? .43 : Card->count > 1 ? .36 : Card->scale * .43;
        R.lane = E.position.x < 0 ? -1 : 1;
        State.entities.push_back(R);
        ++State.telemetry[Team][E.cardId].spawns;
    } else if (E.type == "damage" && Target) {
        Target->hp = E.hp;
        if (Card) {
            auto &T = State.telemetry[Team][E.cardId];
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
        }
        if (!Target->cardId.empty())
            State.telemetry[int(Target->team)][Target->cardId].damageTaken += E.amount;
    } else if ((E.type == "death" || E.type == "tower_destroy") && Target) {
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
        State.hazards.clear();
    } else if (E.type == "slow" && Target) {
        Target->slowPct = E.amount;
        Target->slowUntil = E.until;
        Target->slowSource = E.playId;
    } else if (E.type == "stun" && Target) {
        Target->stunUntil = E.until;
        Target->stunSource = E.playId;
    } else if ((E.type == "target_pull" || E.type == "building_pull") && Target) {
        Target->target = E.source;
        if (E.type == "target_pull") {
            Target->forcedTarget = E.source;
            Target->forcedUntil = E.until;
        }
    } else if (E.type == "attack" && Source) {
        Source->target = Source->hardLock = E.target;
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
        P.splash = Card ? Card->splash : 0;
        P.duration = P.remaining = FMath::Max(0.0, E.until - E.time);
        State.projectiles.push_back(P);
    } else if (E.type == "hazard_tick")
        for (auto &H : State.hazards)
            if (H.playId == E.playId) {
                ++H.ticks;
                H.nextTick += 1;
                break;
            } else if (E.type == "phase") {
                State.phase = E.reason == "overtime" ? rift::Phase::Overtime : rift::Phase::Tiebreaker;
                State.phaseElapsed = 0;
                State.timeRemaining = State.phase == rift::Phase::Overtime ? 120 : 0;
                if (State.phase == rift::Phase::Tiebreaker) {
                    State.projectiles.clear();
                    State.hazards.clear();
                }
            } else if (E.type == "match_end") {
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
void URiftReplaySubsystem::BeginRecording(const rift::MatchOptions &Options, bool Training) {
    Recording = MakeShared<FJsonObject>();
    RecordedEvents.Reset();
    RecordedSamples.Reset();
    LastSample = -1;
    RecordedSequence = 0;
    Recording->SetNumberField(TEXT("formatVersion"), 1);
    Recording->SetStringField(TEXT("model"), TEXT("rift-native-1"));
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
void URiftReplaySubsystem::RecordEvent(const rift::Event &E) {
    if (Recording) {
        RecordedEvents.Add(MakeShared<FJsonValueObject>(EventJSON(E)));
        RecordedSequence = E.sequence;
    }
}
void URiftReplaySubsystem::Sample(const rift::Snapshot &State) {
    if (Recording && State.elapsed >= LastSample) {
        auto O = SnapshotJSON(State);
        O->SetNumberField(TEXT("eventSequence"), double(RecordedSequence));
        RecordedSamples.Add(MakeShared<FJsonValueObject>(O));
        LastSample = State.elapsed;
    }
}
void URiftReplaySubsystem::EndRecording(const rift::Snapshot &State, bool Abandoned) {
    if (!Recording)
        return;
    Sample(State);
    Recording->SetNumberField(TEXT("duration"), State.elapsed);
    Recording->SetBoolField(TEXT("abandoned"), Abandoned);
    Recording->SetObjectField(TEXT("result"), SnapshotJSON(State));
    Recording->SetArrayField(TEXT("events"), RecordedEvents);
    Recording->SetArrayField(TEXT("states"), RecordedSamples);
    FString Text;
    LatestFilename = S(Recording, TEXT("id")) + TEXT(".json");
    if (ValidateRecording(Recording, LastError) &&
        FJsonSerializer::Serialize(Recording.ToSharedRef(), TJsonWriterFactory<>::Create(&Text)) &&
        URiftProfileSubsystem::AtomicWrite(SafeReplayPath(LatestFilename), Text, LastError)) {
        auto *GI = GetGameInstance();
        auto *P = GI ? GI->GetSubsystem<URiftProfileSubsystem>() : nullptr;
        if (P) {
            P->ReplayFiles.AddUnique(LatestFilename);
            P->Save();
        }
    }
    Recording.Reset();
    RecordedEvents.Reset();
    RecordedSamples.Reset();
}
bool URiftReplaySubsystem::OpenReplay(const FString &Filename) {
    FString Text;
    TSharedPtr<FJsonObject> O;
    if (!ReadRecordingFile(SafeReplayPath(Filename), O, Text, LastError))
        return false;
    const TArray<TSharedPtr<FJsonValue>> *Events = nullptr;
    O->TryGetArrayField(TEXT("events"), Events);
    TArray<rift::Event> ParsedEvents;
    for (const auto &V : *Events) {
        rift::Event E;
        if (!ReadEvent(Obj(V), E))
            return false;
        ParsedEvents.Add(MoveTemp(E));
    }
    auto *W = GetWorld();
    auto *M = W ? W->GetSubsystem<URiftMatchSubsystem>() : nullptr;
    if (M)
        M->LeaveMatch();
    Loaded = O;
    PlaybackEvents = MoveTemp(ParsedEvents);
    PlaybackTime = 0;
    PlaybackDuration = float(N(O, TEXT("duration")));
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
    PlaybackEvents.Reset();
    EventCursor = 0;
    PlaybackTime = PlaybackDuration = 0;
    PlaybackSpeed = 0;
}
void URiftReplaySubsystem::ResetEventCursor() {
    EventCursor = 0;
    while (EventCursor < PlaybackEvents.Num() && PlaybackEvents[EventCursor].time <= PlaybackTime + 1e-7)
        ++EventCursor;
}
void URiftReplaySubsystem::Seek(float Seconds) {
    if (!FMath::IsFinite(Seconds) || !Loaded)
        return;
    PlaybackTime = FMath::Clamp(Seconds, 0.f, PlaybackDuration);
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
    const float Before = PlaybackTime;
    PlaybackTime = FMath::Min(PlaybackDuration, PlaybackTime + DeltaTime * PlaybackSpeed);
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
    if (!SnapshotFromJSON(Base, Current))
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
        AdvanceSnapshot(Current, E.time);
        ApplyReplayEvent(Current, E);
    }
    AdvanceSnapshot(Current, PlaybackTime);
    if (Low + 1 < States->Num()) {
        rift::Snapshot Next;
        if (SnapshotFromJSON(Obj((*States)[Low + 1]), Next)) {
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
    const FString Name = FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json");
    if (!URiftProfileSubsystem::AtomicWrite(SafeReplayPath(Name), Text, LastError))
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
