#include "RiftMetaSimulationSubsystem.h"
#include "RiftDiagnostics.h"
#include "RiftProfileSubsystem.h"
#include "RiftReplaySubsystem.h"
#include "Simulation/RiftSimulation.h"
#include "Async/Async.h"
#include "Misc/ScopeLock.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/PlatformProcess.h"
#include "Engine/GameInstance.h"
#include "Serialization/JsonSerializer.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/Csv/CsvParser.h"
#endif

struct FRiftMetaWorker
{
    TAtomic<bool> Stop{false},Paused{false},Battle{false};
    TAtomic<int32> Rate{250},Completed{0},HeadroomDelayMs{10};
    FCriticalSection Mutex;
    FString Pending;
    int32 Limit=0;
};
namespace
{
    double Number(const TSharedPtr<FJsonObject>& O,const FString& K,double Default=0){double N=Default;if(O)O->TryGetNumberField(K,N);return FMath::IsFinite(N)?N:Default;}
    FString String(const TSharedPtr<FJsonObject>& O,const FString& K){FString S;if(O)O->TryGetStringField(K,S);return S;}
    double Ratio(double A,double B){return B>0?A/B:0;}
    void Add(const TSharedPtr<FJsonObject>& O,const FString& K,double V){O->SetNumberField(K,Number(O,K)+V);}
    TSharedPtr<FJsonObject> Object(const TSharedPtr<FJsonObject>& O,const FString& K)
    {const TSharedPtr<FJsonObject>* P;if(O->TryGetObjectField(K,P))return *P;auto R=MakeShared<FJsonObject>();O->SetObjectField(K,R);return R;}
    TSharedPtr<FJsonObject> ReadObject(const TSharedPtr<FJsonObject>& O,const FString& K)
    {const TSharedPtr<FJsonObject>* P=nullptr;if(O&&O->TryGetObjectField(K,P))return *P;return MakeShared<FJsonObject>();}
    void SetRatio(const TSharedPtr<FJsonObject>& Row,const FString& Key,double Numerator,double Denominator,double Scale=1)
    {if(Denominator>0&&FMath::IsFinite(Numerator)&&FMath::IsFinite(Denominator))Row->SetNumberField(Key,Scale*Numerator/Denominator);else Row->SetField(Key,MakeShared<FJsonValueNull>());}
    bool EconomyValid(const rift::Snapshot& State,double& MaximumResidual)
    {
        bool Valid=true;const double Budget=5+rift::Match::AetherGenerated(0,FMath::Min(State.elapsed,300.0));
        for(int32 Team=0;Team<2;++Team)
        {
            double CardSpent=0;for(const auto& Entry:State.telemetry[Team])CardSpent+=Entry.second.spent;
            const double Residual=Budget-State.spent[Team]-State.leaked[Team]-State.aether[Team];
            MaximumResidual=FMath::Max(MaximumResidual,FMath::Abs(Residual));
            Valid&=FMath::IsFinite(State.aether[Team])&&FMath::IsFinite(State.spent[Team])&&FMath::IsFinite(State.leaked[Team]);
            Valid&=State.aether[Team]>=-1e-7&&State.aether[Team]<=10+1e-7&&State.spent[Team]>=0&&State.leaked[Team]>=0;
            // Finishing regulation or entering the HP tiebreaker stops the terminal economy tick.
            Valid&=Residual>=-1e-6&&Residual<.05&&FMath::Abs(CardSpent-State.spent[Team])<1e-6;
        }
        return Valid;
    }
    FString Text(const TSharedPtr<FJsonObject>& O){FString S;FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&S));return S;}
    TSharedPtr<FJsonObject> Parse(const FString& S){TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S),O);return O;}
    FString FS(const std::string& S){return UTF8_TO_TCHAR(S.c_str());}
    bool DatasetCanResume(const TSharedPtr<FJsonObject>& Dataset)
    {
        if(!Dataset||Number(Dataset,TEXT("schemaVersion"),-1)!=1||String(Dataset,TEXT("fingerprint"))!=URiftMetaSimulationSubsystem::Fingerprint()||String(Dataset,TEXT("model"))!=TEXT("rift-native-1")||String(Dataset,TEXT("deckPolicy"))!=TEXT("native-observed-2")||Number(Dataset,TEXT("telemetryRevision"),-1)!=4)return false;
        const TSharedPtr<FJsonObject>* Buckets=nullptr;const TArray<TSharedPtr<FJsonValue>>* Points=nullptr;
        if(!Dataset->TryGetObjectField(TEXT("buckets"),Buckets)||!Dataset->TryGetArrayField(TEXT("checkpoints"),Points))return false;
        double Games=-1,Invalid=-1,Seed=-1;
        if(!Dataset->TryGetNumberField(TEXT("games"),Games)||!Dataset->TryGetNumberField(TEXT("invalid"),Invalid)||!Dataset->TryGetNumberField(TEXT("seed"),Seed))return false;
        if(!FMath::IsFinite(Games)||Games<0||!FMath::IsFinite(Invalid)||Invalid<0||!FMath::IsFinite(Seed)||Seed<0||Seed>4294967295.0||Seed!=FMath::FloorToDouble(Seed))return false;
        for(const auto& Pair:(*Buckets)->Values)if(!Pair.Value.IsValid()||Pair.Value->Type!=EJson::Object)return false;
        for(const auto& Point:*Points){const TArray<TSharedPtr<FJsonValue>>* Cards=nullptr;if(!Point.IsValid()||Point->Type!=EJson::Object||!Point->AsObject()->TryGetArrayField(TEXT("cards"),Cards)||Number(Point->AsObject(),TEXT("games"),-1)<0)return false;}
        return true;
    }
    TSharedPtr<FJsonObject> NewDataset()
    {
        auto O=MakeShared<FJsonObject>();
        O->SetNumberField(TEXT("schemaVersion"),1);O->SetStringField(TEXT("id"),FGuid::NewGuid().ToString(EGuidFormats::Digits));O->SetStringField(TEXT("createdAt"),FDateTime::UtcNow().ToIso8601());
        FString Version=TEXT("unknown");if(GConfig)GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"),TEXT("ProjectVersion"),Version,GGameIni);
        O->SetStringField(TEXT("version"),Version);O->SetStringField(TEXT("model"),TEXT("rift-native-1"));O->SetStringField(TEXT("deckPolicy"),TEXT("native-observed-2"));O->SetStringField(TEXT("fingerprint"),URiftMetaSimulationSubsystem::Fingerprint());O->SetStringField(TEXT("balanceHash"),FS(rift::CardManifestHash()));
        O->SetNumberField(TEXT("fingerprintPolicyRevision"),2);
        O->SetNumberField(TEXT("games"),0);O->SetNumberField(TEXT("invalid"),0);O->SetNumberField(TEXT("seed"),151515);O->SetNumberField(TEXT("simulationWallSeconds"),0);O->SetNumberField(TEXT("economyChecks"),0);O->SetNumberField(TEXT("economyInvalid"),0);O->SetNumberField(TEXT("maximumEconomyResidual"),0);O->SetNumberField(TEXT("telemetryRevision"),4);
        O->SetObjectField(TEXT("buckets"),MakeShared<FJsonObject>());O->SetArrayField(TEXT("checkpoints"),{});
        TArray<TSharedPtr<FJsonValue>> CardSnapshot;
        auto CaptureDefinition=[](const rift::Card& Card)
        {
            auto Definition=MakeShared<FJsonObject>();Definition->SetStringField(TEXT("id"),FS(Card.id));Definition->SetStringField(TEXT("name"),FS(Card.name));Definition->SetStringField(TEXT("category"),Card.spell?TEXT("Spell"):Card.building?TEXT("Building"):TEXT("Troop"));
#define SNAPSHOT_FIELD(Key) Definition->SetNumberField(TEXT(#Key),double(Card.Key))
            SNAPSHOT_FIELD(cost);SNAPSHOT_FIELD(count);SNAPSHOT_FIELD(hp);SNAPSHOT_FIELD(damage);SNAPSHOT_FIELD(attackInterval);SNAPSHOT_FIELD(moveSpeed);SNAPSHOT_FIELD(range);SNAPSHOT_FIELD(scale);SNAPSHOT_FIELD(collisionRadius);SNAPSHOT_FIELD(targetMask);SNAPSHOT_FIELD(projectileSpeed);SNAPSHOT_FIELD(shockwaveSpeed);SNAPSHOT_FIELD(splash);SNAPSHOT_FIELD(lifetime);SNAPSHOT_FIELD(footprint);SNAPSHOT_FIELD(towerDamage);SNAPSHOT_FIELD(spellRadius);SNAPSHOT_FIELD(castDelay);SNAPSHOT_FIELD(chargeDamage);SNAPSHOT_FIELD(slowPct);SNAPSHOT_FIELD(slowDuration);SNAPSHOT_FIELD(auraDamage);SNAPSHOT_FIELD(auraInterval);SNAPSHOT_FIELD(auraRadius);SNAPSHOT_FIELD(stunDuration);SNAPSHOT_FIELD(dotDamage);SNAPSHOT_FIELD(dotDuration);SNAPSHOT_FIELD(dotInterval);SNAPSHOT_FIELD(rounds);SNAPSHOT_FIELD(chargeMoveSeconds);SNAPSHOT_FIELD(chargedMoveSpeed);SNAPSHOT_FIELD(hpDecayPerSecond);SNAPSHOT_FIELD(initialAttackCooldown);SNAPSHOT_FIELD(shieldHP);SNAPSHOT_FIELD(deathSpawnCount);SNAPSHOT_FIELD(invisibilityDuration);SNAPSHOT_FIELD(healthCloakThreshold);SNAPSHOT_FIELD(dotStructureDamage);
#undef SNAPSHOT_FIELD
            Definition->SetBoolField(TEXT("flying"),Card.flying);Definition->SetBoolField(TEXT("canHitAir"),Card.canHitAir);Definition->SetBoolField(TEXT("structuresOnly"),Card.structuresOnly);Definition->SetBoolField(TEXT("spell"),Card.spell);Definition->SetBoolField(TEXT("building"),Card.building);
            Definition->SetBoolField(TEXT("hardExpiry"),Card.hardExpiry);Definition->SetBoolField(TEXT("shieldOverflowProtected"),Card.shieldOverflowProtected);Definition->SetBoolField(TEXT("globalFriendlyInvisibility"),Card.globalFriendlyInvisibility);Definition->SetBoolField(TEXT("breakExistingLocks"),Card.breakExistingLocks);Definition->SetStringField(TEXT("deathSpawnId"),FS(Card.deathSpawnId));return Definition;
        };
        for(const auto& Card:rift::Cards())CardSnapshot.Add(MakeShared<FJsonValueObject>(CaptureDefinition(Card)));
        TArray<TSharedPtr<FJsonValue>> EntitySnapshot;for(const auto& Card:rift::EntityDefinitions())EntitySnapshot.Add(MakeShared<FJsonValueObject>(CaptureDefinition(Card)));
        O->SetArrayField(TEXT("entitySnapshot"),EntitySnapshot);O->SetArrayField(TEXT("cardSnapshot"),CardSnapshot);auto Rules=MakeShared<FJsonObject>();
        Rules->SetNumberField(TEXT("combatRevision"),rift::arena::CombatRevision);
        Rules->SetStringField(TEXT("cloakLockException"),TEXT("Approved Shadow Surge drops enemy locks, including committed Crown Tower locks; committed damage and projectiles still resolve"));
        Rules->SetStringField(TEXT("detection"),TEXT("strict center distance: 8 tiles forward, extended to attack range for longer-range projectile cards; 5 tiles rear; eligible targets only; Crown advance and engaged Crown hard locks preserved"));
        Rules->SetStringField(TEXT("shockwave"),TEXT("expanding ground troop-only front; one hit per wave; source death does not cancel; per-card slow refresh, strongest active effect wins"));
        Rules->SetNumberField(TEXT("arenaWidth"),rift::arena::Width);Rules->SetNumberField(TEXT("arenaHeight"),rift::arena::Height);Rules->SetNumberField(TEXT("coreDepth"),rift::arena::CoreDepth);Rules->SetNumberField(TEXT("guardDepth"),rift::arena::GuardDepth);Rules->SetNumberField(TEXT("guardX"),rift::arena::GuardX);Rules->SetNumberField(TEXT("pocketOuterX"),rift::arena::PocketOuterX);Rules->SetNumberField(TEXT("pocketMaxDepth"),rift::arena::PocketMaxDepth);
        Rules->SetNumberField(TEXT("riverHalfWidth"),rift::arena::RiverHalfWidth);Rules->SetNumberField(TEXT("bridgeCenterX"),rift::arena::BridgeCenterX);Rules->SetNumberField(TEXT("bridgeWidth"),rift::arena::BridgeWidth);
        Rules->SetNumberField(TEXT("frontSight"),8);Rules->SetNumberField(TEXT("rearSight"),5);Rules->SetStringField(TEXT("frontSightPolicy"),TEXT("Ranged troops acquire ahead to max(frontSight, attack range); rearSight remains unchanged"));Rules->SetNumberField(TEXT("regulationSeconds"),180);Rules->SetNumberField(TEXT("overtimeSeconds"),120);Rules->SetNumberField(TEXT("aetherInterval"),2.8);Rules->SetNumberField(TEXT("doubleAetherAt"),120);Rules->SetNumberField(TEXT("tripleAetherAt"),240);Rules->SetNumberField(TEXT("tiebreakerDrainPerSecond"),180);Rules->SetNumberField(TEXT("openingAether"),5);Rules->SetNumberField(TEXT("maximumAether"),10);Rules->SetStringField(TEXT("coreActivation"),TEXT("friendly Guard Tower destroyed"));Rules->SetStringField(TEXT("targetHardLock"),TEXT("first authoritative attack against a Crown Tower"));Rules->SetNumberField(TEXT("navigationRevision"),rift::arena::NavigationRevision);Rules->SetStringField(TEXT("navigation"),TEXT("card-aware ground grid A-star with bridges; flying ignores ground obstacles; clear layered body spawns and swept disc clearance; source-side bridge commitment; same-lane Guard else Core; dynamic crowd costs and local steering; visible continuous pocket connectors; per-member landing lanes and radius-aware swarm formations; fifteen-member Stampede packs five across, three deep; Guard Towers one tile inward from bridge centers"));Rules->SetStringField(TEXT("guardBridgeAlignment"),TEXT("guard centers one tile inward from bridge centers; cores at arena center"));Rules->SetStringField(TEXT("spellImpact"),TEXT("fixed target area; current enemy positions at impact; exact per-card delays captured in cardSnapshot"));Rules->SetNumberField(TEXT("collisionSkin"),.02);Rules->SetNumberField(TEXT("structureClearance"),.22);Rules->SetStringField(TEXT("collision"),TEXT("ground troops and structures share a layer; flying troops share an air layer; both teams collide; spells have no body"));O->SetObjectField(TEXT("rulesSnapshot"),Rules);
        TArray<TSharedPtr<FJsonValue>> Styles;for(const TCHAR* Style:{TEXT("beatdown"),TEXT("aggro"),TEXT("control"),TEXT("cycle"),TEXT("split"),TEXT("spell_cycle"),TEXT("counter")})Styles.Add(MakeShared<FJsonValueString>(Style));O->SetArrayField(TEXT("aiStyles"),Styles);
        return O;
    }
    TPair<double,double> Wilson(double Score,double N)
    {
        if(N<=0)return {0,100};const double P=Score/N,Z=1.96,Z2=Z*Z,Den=1+Z2/N;
        const double Center=(P+Z2/(2*N))/Den,Half=Z*FMath::Sqrt((P*(1-P)+Z2/(4*N))/N)/Den;
        return {100*FMath::Max(0.0,Center-Half),100*FMath::Min(1.0,Center+Half)};
    }
    TArray<TSharedPtr<FJsonObject>> RowsFor(const TSharedPtr<FJsonObject>& Bucket)
    {
        TArray<TSharedPtr<FJsonObject>> Rows;auto Stats=ReadObject(Bucket,TEXT("cards"));
        for(const auto& C:rift::Cards())
        {
            FString Id=FS(C.id);auto S=ReadObject(Stats,Id);auto R=MakeShared<FJsonObject>();const double Ap=Number(S,TEXT("appearances")),N=Number(S,TEXT("cleanN")),Score=Number(S,TEXT("score")),Plays=Number(S,TEXT("plays")),Spent=Number(S,TEXT("spent")),Spawns=Number(S,TEXT("spawns")),Damage=Number(S,TEXT("troopDamage"))+Number(S,TEXT("towerDamage"))+Number(S,TEXT("buildingDamage"));auto CI=Wilson(Score,N);
            R->SetStringField(TEXT("id"),Id);R->SetStringField(TEXT("name"),FS(C.name));R->SetStringField(TEXT("category"),C.spell?TEXT("Spell"):C.building?TEXT("Building"):TEXT("Troop"));R->SetNumberField(TEXT("cost"),C.cost);R->SetBoolField(TEXT("flying"),C.flying);R->SetBoolField(TEXT("canHitAir"),C.canHitAir);R->SetBoolField(TEXT("winCondition"),C.structuresOnly);R->SetBoolField(TEXT("swarm"),C.count>1);R->SetNumberField(TEXT("splash"),C.splash);R->SetNumberField(TEXT("auraDamage"),C.auraDamage);
            R->SetNumberField(TEXT("pickRate"),100*Ratio(Ap,Number(Bucket,TEXT("n"))));R->SetNumberField(TEXT("adjustedWinRate"),100*(Score+12)/(N+24));if(N>0)R->SetNumberField(TEXT("rawWinRate"),100*Score/N);else R->SetField(TEXT("rawWinRate"),MakeShared<FJsonValueNull>());
            R->SetNumberField(TEXT("ciLow"),CI.Key);R->SetNumberField(TEXT("ciHigh"),CI.Value);R->SetNumberField(TEXT("cleanN"),N);R->SetNumberField(TEXT("appearances"),Ap);R->SetNumberField(TEXT("mirrorExclusions"),Number(S,TEXT("mirror")));
            static const TPair<const TCHAR*,const TCHAR*> PerGame[]={
                {TEXT("usesPerGame"),TEXT("plays")},{TEXT("aetherPerGame"),TEXT("spent")},{TEXT("troopDamagePerGame"),TEXT("troopDamage")},{TEXT("towerDamagePerGame"),TEXT("towerDamage")},{TEXT("buildingDamagePerGame"),TEXT("buildingDamage")},{TEXT("damageTakenPerGame"),TEXT("damageTaken")},{TEXT("killsPerGame"),TEXT("kills")},{TEXT("deathsPerGame"),TEXT("deaths")},{TEXT("crownContribution"),TEXT("crownContribution")},{TEXT("damagePreventedPerGame"),TEXT("prevented")},{TEXT("pullsPerGame"),TEXT("pulls")},{TEXT("auraDamagePerGame"),TEXT("auraDamage")},{TEXT("unitsStunnedPerGame"),TEXT("stunned")},{TEXT("initialDamagePerGame"),TEXT("initialDamage")},{TEXT("dotDamagePerGame"),TEXT("dotDamage")},{TEXT("shieldDamagePerGame"),TEXT("shieldDamage")},{TEXT("shieldDamageTakenPerGame"),TEXT("shieldDamageTaken")},{TEXT("shieldAbsorbedPerGame"),TEXT("shieldAbsorbed")},{TEXT("overflowPreventedPerGame"),TEXT("overflowPrevented")},{TEXT("childrenSpawnedPerGame"),TEXT("childSpawns")},{TEXT("childrenDiedPerGame"),TEXT("childDeaths")},{TEXT("childrenSurvivedPerGame"),TEXT("childSurviving")}};
            for(const auto& P:PerGame)SetRatio(R,P.Key,Number(S,P.Value),Ap);
            SetRatio(R,TEXT("averageLifetime"),Number(S,TEXT("lifetime")),Spawns+Number(S,TEXT("childSpawns")));SetRatio(R,TEXT("averagePlacementX"),Number(S,TEXT("placementX")),Plays);SetRatio(R,TEXT("averagePlacementZ"),Number(S,TEXT("placementZ")),Plays);SetRatio(R,TEXT("averageKillValue"),Number(S,TEXT("killValue")),Number(S,TEXT("kills")));SetRatio(R,TEXT("damagePerAether"),Damage,Spent);SetRatio(R,TEXT("towerDamagePerAether"),Number(S,TEXT("towerDamage")),Spent);SetRatio(R,TEXT("survivalRate"),Number(S,TEXT("surviving")),Spawns,100);SetRatio(R,TEXT("openingHandPlayRate"),Number(S,TEXT("openingPlays")),Number(S,TEXT("openingEligible")),100);SetRatio(R,TEXT("firstPlayRate"),Number(S,TEXT("firstPlays")),Ap,100);SetRatio(R,TEXT("otPlayRate"),Number(S,TEXT("overtimePlays")),Plays,100);SetRatio(R,TEXT("connectionRate"),Number(S,TEXT("connected")),Plays,100);SetRatio(R,TEXT("lifetimeUtilization"),Number(S,TEXT("buildingLifetime")),Number(S,TEXT("buildingCapacity")),100);SetRatio(R,TEXT("targetsPerCast"),Number(S,TEXT("targets")),Plays);SetRatio(R,TEXT("spellAetherValuePerCast"),Number(S,TEXT("spellValue")),Plays);SetRatio(R,TEXT("overkillPerCast"),Number(S,TEXT("overkill")),Plays);SetRatio(R,TEXT("damagePerCast"),Damage,Plays);SetRatio(R,TEXT("slowUptime"),Number(S,TEXT("slowTime")),Number(S,TEXT("slowTrackedSeconds")),100);SetRatio(R,TEXT("stunUptime"),Number(S,TEXT("stunTime")),Number(S,TEXT("stunTrackedSeconds")),100);SetRatio(R,TEXT("zoneOccupancy"),Number(S,TEXT("zoneOccupancy")),Number(S,TEXT("zoneSeconds")));SetRatio(R,TEXT("dotTicksPerCast"),Number(S,TEXT("dotTicks")),Plays);Rows.Add(R);
        }return Rows;
    }
    void Aggregate(const TSharedPtr<FJsonObject>& Data,const rift::Snapshot& State)
    {
        Add(Data,TEXT("games"),1);Add(Data,TEXT("duration"),State.elapsed);auto Buckets=Object(Data,TEXT("buckets"));
        for(int32 I=0;I<2;++I)
        {
            double Score=State.winner<0?.5:State.winner==I?1:0;auto Analysis=rift::AnalyzeDeck(State.decks[I]);
            std::vector<std::string> Archetypes=Analysis.archetypes;if(Archetypes.empty())Archetypes.push_back("Hybrid");TArray<FString> Keys{TEXT("all|all"),FS(State.ai[I].style)+TEXT("|all")};for(const auto& A:Archetypes){Keys.Add(TEXT("all|")+FS(A));Keys.Add(FS(State.ai[I].style)+TEXT("|")+FS(A));}
            const auto& Deck=State.decks[I];const auto& Opponent=State.decks[1-I];
            auto OpponentArchetypes=rift::AnalyzeDeck(Opponent).archetypes;if(OpponentArchetypes.empty())OpponentArchetypes.push_back("Hybrid");
            for(const auto& Key:Keys)
            {
                auto Bucket=Object(Buckets,Key);Add(Bucket,TEXT("n"),1);Add(Bucket,TEXT("score"),Score);Add(Bucket,TEXT("draws"),State.winner<0?1:0);Add(Bucket,TEXT("duration"),State.elapsed);Add(Bucket,TEXT("spent"),State.spent[I]);Add(Bucket,TEXT("leaked"),State.leaked[I]);Add(Bucket,TEXT("crowns"),State.crowns[I]);auto Cards=Object(Bucket,TEXT("cards"));auto Pairs=Object(Bucket,TEXT("pairs"));
                for(const auto& OpponentArchetype:OpponentArchetypes){auto Observation=Object(Object(Bucket,TEXT("opponents")),FS(OpponentArchetype));Add(Observation,TEXT("n"),1);Add(Observation,TEXT("score"),Score);}
                auto StyleObservation=Object(Object(Bucket,TEXT("opponentStyles")),FS(State.ai[1-I].style));Add(StyleObservation,TEXT("n"),1);Add(StyleObservation,TEXT("score"),Score);
                for(const auto& Id:Deck)
                {
                    auto S=Object(Cards,FS(Id));Add(S,TEXT("appearances"),1);const bool Mirror=std::find(Opponent.begin(),Opponent.end(),Id)!=Opponent.end();if(Mirror)Add(S,TEXT("mirror"),1);else{Add(S,TEXT("cleanN"),1);Add(S,TEXT("score"),Score);}
                    auto It=State.telemetry[I].find(Id);if(It==State.telemetry[I].end())continue;const auto& T=It->second;
#define RF(K) Add(S,TEXT(#K),double(T.K))
                    RF(spent);RF(troopDamage);RF(towerDamage);RF(buildingDamage);RF(damageTaken);RF(kills);RF(deaths);RF(killValue);RF(lifetime);RF(slowTime);RF(stunTime);RF(initialDamage);RF(dotDamage);RF(auraDamage);RF(overkill);RF(prevented);RF(placementX);RF(placementZ);RF(spellValue);RF(zoneOccupancy);RF(zoneSeconds);RF(crownContribution);RF(plays);RF(spawns);RF(surviving);RF(pulls);RF(connected);RF(targets);RF(openingPlays);RF(firstPlays);RF(overtimePlays);RF(stunned);RF(dotTicks);RF(openingEligible);RF(buildingLifetime);RF(buildingCapacity);RF(slowTrackedSeconds);RF(stunTrackedSeconds);RF(shieldDamage);RF(shieldDamageTaken);RF(shieldAbsorbed);RF(overflowPrevented);RF(childSpawns);RF(childDeaths);RF(childSurviving);
#undef RF
                }
                // Body observations are separate from paid card/deck observations.
                // A death child contributes to its parent's card rollup above, while
                // its own combat, shield and lifetime counters remain inspectable here.
                auto Entities=Object(Bucket,TEXT("entities"));
                for(const auto& Entry:State.entityTelemetry[I]) {
                    const auto& Body=Entry.second;const auto& T=Body.observed;
                    auto S=Object(Entities,FS(Body.cardId));S->SetStringField(TEXT("cardId"),FS(Body.cardId));
                    S->SetStringField(TEXT("parentCardId"),FS(Body.attributionCardId));S->SetBoolField(TEXT("subordinate"),Body.parentEntityId!=0);
                    Add(S,TEXT("observations"),1);
#define RF(K) Add(S,TEXT(#K),double(T.K))
                    RF(spent);RF(plays);RF(spawns);RF(surviving);RF(deaths);RF(troopDamage);RF(towerDamage);RF(buildingDamage);RF(damageTaken);RF(kills);RF(killValue);RF(lifetime);RF(initialDamage);RF(dotDamage);RF(auraDamage);RF(overkill);RF(prevented);RF(buildingLifetime);RF(shieldDamage);RF(shieldDamageTaken);RF(shieldAbsorbed);RF(overflowPrevented);
#undef RF
                }
                for(size_t A=0;A<Deck.size();++A)for(size_t B=A+1;B<Deck.size();++B)
                {
                    const auto& IdA=Deck[A]<Deck[B]?Deck[A]:Deck[B];const auto& IdB=Deck[A]<Deck[B]?Deck[B]:Deck[A];auto P=Object(Pairs,FS(IdA)+TEXT("+")+FS(IdB));Add(P,TEXT("appearances"),1);
                    if(std::find(Opponent.begin(),Opponent.end(),IdA)==Opponent.end()||std::find(Opponent.begin(),Opponent.end(),IdB)==Opponent.end()){Add(P,TEXT("n"),1);Add(P,TEXT("score"),Score);}else Add(P,TEXT("mirrors"),1);
                }
            }
        }
    }
    void Publish(const TSharedPtr<FRiftMetaWorker,ESPMode::ThreadSafe>& Worker,const TSharedPtr<FJsonObject>& Data)
    {FString S=Text(Data);FScopeLock Lock(&Worker->Mutex);Worker->Pending=MoveTemp(S);}
    TSharedPtr<FJsonObject> SummaryFor(const TSharedPtr<FJsonObject>& Bucket,const FString& Name,double Total,bool Style)
    {
        auto Row=MakeShared<FJsonObject>();const double N=Number(Bucket,TEXT("n")),Score=Number(Bucket,TEXT("score"));auto CI=Wilson(Score,N);
        Row->SetStringField(TEXT("name"),Name);Row->SetNumberField(TEXT("cleanN"),N);Row->SetNumberField(TEXT("adjustedWinRate"),100*(Score+12)/(N+24));Row->SetNumberField(TEXT("ciLow"),CI.Key);Row->SetNumberField(TEXT("ciHigh"),CI.Value);
        SetRatio(Row,TEXT("rawWinRate"),Score,N,100);SetRatio(Row,TEXT("pickRate"),N,Total,100);SetRatio(Row,TEXT("averageDuration"),Number(Bucket,TEXT("duration")),N);SetRatio(Row,TEXT("aetherPerGame"),Number(Bucket,TEXT("spent")),N);SetRatio(Row,TEXT("leakedPerGame"),Number(Bucket,TEXT("leaked")),N);
        if(Bucket->HasField(TEXT("crowns")))SetRatio(Row,TEXT("averageCrowns"),Number(Bucket,TEXT("crowns")),N);else Row->SetField(TEXT("averageCrowns"),MakeShared<FJsonValueNull>());
        auto Cards=RowsFor(Bucket);Cards.RemoveAll([](const auto& Card){return Number(Card,TEXT("cleanN"))<=0;});Cards.Sort([](const auto& A,const auto& B){return Number(A,TEXT("adjustedWinRate"))>Number(B,TEXT("adjustedWinRate"));});
        TArray<TSharedPtr<FJsonValue>> Best,Weakest,AllCards;for(auto Card:Cards)AllCards.Add(MakeShared<FJsonValueObject>(Card));
        for(int32 I=0;I<FMath::Min(3,Cards.Num());++I){Best.Add(MakeShared<FJsonValueObject>(Cards[I]));Weakest.Add(MakeShared<FJsonValueObject>(Cards[Cards.Num()-1-I]));}
        Row->SetArrayField(TEXT("bestCards"),Best);Row->SetArrayField(TEXT("weakestCards"),Weakest);Row->SetArrayField(TEXT("cards"),AllCards);
        TArray<TSharedPtr<FJsonObject>> Opponents;
        for(const auto& Pair:ReadObject(Bucket,Style?TEXT("opponentStyles"):TEXT("opponents"))->Values)
        {
            if(Pair.Value->Type!=EJson::Object)continue;auto Observation=Pair.Value->AsObject();const double Samples=Number(Observation,TEXT("n")),Wins=Number(Observation,TEXT("score"));if(Samples<=0)continue;
            auto Opponent=MakeShared<FJsonObject>();Opponent->SetStringField(TEXT("name"),FString(*Pair.Key));Opponent->SetNumberField(TEXT("cleanN"),Samples);Opponent->SetNumberField(TEXT("adjustedWinRate"),100*(Wins+12)/(Samples+24));auto Interval=Wilson(Wins,Samples);Opponent->SetNumberField(TEXT("ciLow"),Interval.Key);Opponent->SetNumberField(TEXT("ciHigh"),Interval.Value);Opponents.Add(Opponent);
        }
        Opponents.Sort([](const auto& A,const auto& B){return Number(A,TEXT("adjustedWinRate"))>Number(B,TEXT("adjustedWinRate"));});
        TArray<TSharedPtr<FJsonValue>> OpponentJSON;for(auto Opponent:Opponents)OpponentJSON.Add(MakeShared<FJsonValueObject>(Opponent));Row->SetArrayField(TEXT("opponents"),OpponentJSON);
        if(!Opponents.IsEmpty()){Row->SetObjectField(TEXT("bestOpponent"),Opponents[0]);Row->SetObjectField(TEXT("worstOpponent"),Opponents.Last());}else{Row->SetField(TEXT("bestOpponent"),MakeShared<FJsonValueNull>());Row->SetField(TEXT("worstOpponent"),MakeShared<FJsonValueNull>());}
        Row->SetStringField(TEXT("interpretation"),TEXT("Observed deck associations; overlapping archetype labels and sparse slices require interpretation. These are not isolated duel results."));return Row;
    }
    void ApplyCapturedIdentity(TArray<TSharedPtr<FJsonObject>>& Rows,const TSharedPtr<FJsonObject>& Data)
    {
        const TArray<TSharedPtr<FJsonValue>>* Definitions=nullptr;if(!Data||!Data->TryGetArrayField(TEXT("cardSnapshot"),Definitions))return;
        TMap<FString,TSharedPtr<FJsonObject>> Captured;for(auto Value:*Definitions)if(Value.IsValid()&&Value->Type==EJson::Object)Captured.Add(String(Value->AsObject(),TEXT("id")),Value->AsObject());
        Rows.RemoveAll([&Captured](const auto& Row){return !Captured.Contains(String(Row,TEXT("id")));});
        for(auto Row:Rows)if(auto Definition=Captured.FindRef(String(Row,TEXT("id"))))
        {
            // Filtering and display share the immutable definition. Never fill a
            // missing historical trait from a later balance's live card.
            for(const TCHAR* Key:{TEXT("name"),TEXT("cost"),TEXT("category"),TEXT("flying"),TEXT("canHitAir"),TEXT("splash"),TEXT("auraDamage")})
            {if(auto Field=Definition->TryGetField(Key))Row->SetField(Key,Field);else Row->RemoveField(Key);}
            bool StructuresOnly=false;if(Definition->TryGetBoolField(TEXT("structuresOnly"),StructuresOnly))Row->SetBoolField(TEXT("winCondition"),StructuresOnly);else Row->RemoveField(TEXT("winCondition"));
            double Count=0;if(Definition->TryGetNumberField(TEXT("count"),Count))Row->SetBoolField(TEXT("swarm"),Count>1);else Row->RemoveField(TEXT("swarm"));
        }
    }
    void MechanicalIdentity(const TSharedPtr<FJsonObject>& Row)
    {
        Row->SetStringField(TEXT("mechanicalSource"),TEXT("current canonical definitions"));
        Row->SetStringField(TEXT("mechanicalVersion"),FS(rift::CardManifestVersion()));
        Row->SetStringField(TEXT("mechanicalBalanceHash"),FS(rift::CardManifestHash()));
    }
    void EmpiricalIdentity(const TSharedPtr<FJsonObject>& Row,const TSharedPtr<FJsonObject>& Dataset)
    {
        Row->SetStringField(TEXT("empiricalDatasetVersion"),String(Dataset,TEXT("version")));
        Row->SetStringField(TEXT("empiricalFingerprint"),String(Dataset,TEXT("fingerprint")));
        const FString Hash=String(Dataset,TEXT("balanceHash"));
        if(!Hash.IsEmpty())Row->SetStringField(TEXT("empiricalBalanceHash"),Hash);
        else Row->SetField(TEXT("empiricalBalanceHash"),MakeShared<FJsonValueNull>());
    }
}

bool URiftMetaSimulationSubsystem::CardRowMatches(const TSharedPtr<FJsonObject>& Row,const FString& Cost,const FString& Type,const FString& Trait,const FString& Search)
{
    if(!Row)return false;
    if(!Search.IsEmpty()&&!String(Row,TEXT("name")).Contains(Search,ESearchCase::IgnoreCase)&&!String(Row,TEXT("id")).Contains(Search,ESearchCase::IgnoreCase))return false;
    if(Cost!=TEXT("all")&&Number(Row,TEXT("cost"),-1)!=FCString::Atoi(*Cost))return false;
    const FString Category=String(Row,TEXT("category"));if(Type!=TEXT("all")&&Category!=Type)return false;
    auto Flag=[&](const TCHAR* Key){bool Value=false;Row->TryGetBoolField(Key,Value);return Value;};
    const bool Spell=Category==TEXT("Spell");
    if(Trait==TEXT("Air")&&!Flag(TEXT("flying")))return false;
    bool Flying=false;const bool KnownFlying=Row->TryGetBoolField(TEXT("flying"),Flying);
    if(Trait==TEXT("Ground")&&(!KnownFlying||Flying||Spell||Category.IsEmpty()))return false;
    if(Trait==TEXT("Anti-air")&&!Flag(TEXT("canHitAir"))&&!Spell&&!Number(Row,TEXT("auraDamage")))return false;
    if(Trait==TEXT("Win condition")&&!Flag(TEXT("winCondition")))return false;
    if(Trait==TEXT("Swarm")&&!Flag(TEXT("swarm")))return false;
    if(Trait==TEXT("Splash")&&!Number(Row,TEXT("splash"))&&!Spell&&!Number(Row,TEXT("auraDamage")))return false;
    return true;
}
TSharedPtr<FJsonObject> URiftMetaSimulationSubsystem::ReportIdentity(const FString& Subject)const
{
    auto Identity=MakeShared<FJsonObject>();Identity->SetStringField(TEXT("model"),String(Data,TEXT("model")));Identity->SetStringField(TEXT("fingerprint"),String(Data,TEXT("fingerprint")));Identity->SetStringField(TEXT("version"),String(Data,TEXT("version")));
    Identity->SetStringField(TEXT("selectedDatasetVersion"),String(Data,TEXT("version")));Identity->SetStringField(TEXT("selectedDatasetFingerprint"),String(Data,TEXT("fingerprint")));
    if(Subject!=TEXT("matchups"))EmpiricalIdentity(Identity,Data);
    if(Subject==TEXT("matchups")||Subject==TEXT("synergy"))MechanicalIdentity(Identity);
    return Identity;
}

FString URiftMetaSimulationSubsystem::Fingerprint()
{
    // The canonical numerical manifest excludes names, art and visual scale.
    // Only gameplay data and explicit simulator-policy revisions reset samples.
    // Policy2 formats authoritative tile geometry, independent of world-unit
    // presentation scale. Policy1 archives retain their recorded identity.
    const FString Canonical=FString::Printf(TEXT("rift-native-1|native-observed-2|ai-v15-port-2|fingerprint-policy-2|telemetry-4|arena%dx%d|river%.9g|bridges%.9g,%.9g|phase180,120|aether2.8,120,240|drain180|coreGuardOnly|pocket2,%.9g,2.25,%.9g|towers%.9g,%.9g,%.9g|layered-swept-disc-1|collisionSkin0.02|structureClearance0.22|"),
        rift::arena::Width,rift::arena::Height,rift::arena::RiverHalfWidth,rift::arena::BridgeCenterX,rift::arena::BridgeWidth,
        rift::arena::PocketOuterX,rift::arena::PocketMaxDepth,rift::arena::GuardX,rift::arena::GuardDepth,rift::arena::CoreDepth)
        +FS(rift::CardManifestHash());
    return FMD5::HashAnsiString(*Canonical);
}
void URiftMetaSimulationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);Collection.InitializeDependency<URiftProfileSubsystem>();auto* Profile=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();
    for(int32 I=Profile->MetaFiles.Num()-1;I>=0;--I){FString S;if(FFileHelper::LoadFileToString(S,*FPaths::Combine(URiftProfileSubsystem::SaveRoot(),TEXT("Meta"),FPaths::GetCleanFilename(Profile->MetaFiles[I])))){auto O=Parse(S);if(DatasetCanResume(O)){Data=O;CurrentFile=FPaths::GetCleanFilename(Profile->MetaFiles[I]);break;}}}
    if(!Data){Data=NewDataset();CurrentFile=String(Data,TEXT("id"))+TEXT(".json");bDirty=true;Persist();}
    RIFT_LOG(LogRift,Log,TEXT("Meta dataset %s initialized with fingerprint %s"),*CurrentFile,*String(Data,TEXT("fingerprint")));
}
void URiftMetaSimulationSubsystem::Deinitialize(){Stop();Persist();Super::Deinitialize();}
TStatId URiftMetaSimulationSubsystem::GetStatId()const{RETURN_QUICK_DECLARE_CYCLE_STAT(URiftMetaSimulationSubsystem,STATGROUP_Tickables);}
bool URiftMetaSimulationSubsystem::IsTickable()const{return !IsTemplate()&&GetGameInstance()!=nullptr;}
void URiftMetaSimulationSubsystem::Tick(float DeltaTime)
{
    if(bBattle)return;
    Harvest();SaveClock+=DeltaTime;if(SaveClock>=5){SaveClock=0;Persist();}
    if(Worker&&Rate==0){int32 Delay=Worker->HeadroomDelayMs.Load();if(DeltaTime>.025f)Delay+=10;else if(DeltaTime<.0185f)Delay-=1;Worker->HeadroomDelayMs=FMath::Clamp(Delay,10,250);}
}
void URiftMetaSimulationSubsystem::Start(int32 Games)
{
    Stop();if(!DatasetCanResume(Data)){Reset();}bPaused=false;
    RIFT_LOG(LogRift,Log,TEXT("Native Meta worker starting: %d requested matches, rate %d/min (zero uses adaptive safe headroom)"),Games,Rate);
    Worker=MakeShared<FRiftMetaWorker,ESPMode::ThreadSafe>();Worker->Rate=Rate;Worker->Battle=bBattle;Worker->Limit=FMath::Max(0,Games);auto W=Worker;auto Copy=Parse(Text(Data));
    Future=Async(EAsyncExecution::Thread,[W,Copy]()
    {
        static const char* Styles[]={"beatdown","aggro","control","cycle","split","spell_cycle","counter"};
        while(!W->Stop.Load()&&(W->Limit==0||W->Completed.Load()<W->Limit))
        {
            while((W->Paused.Load()||W->Battle.Load())&&!W->Stop.Load())FPlatformProcess::SleepNoStats(.02f);if(W->Stop.Load())break;
            double Began=FPlatformTime::Seconds();uint32 Seed=uint32(Number(Copy,TEXT("seed"),151515));rift::MatchOptions Options;Options.seed=Seed;Options.aiEnabled={true,true};Options.aiStyles={Styles[Seed%7],Styles[(Seed/7)%7]};Options.decks[0]=rift::BuildAIDeck(Options.aiStyles[0],Seed^0x72F3U);Options.decks[1]=rift::BuildAIDeck(Options.aiStyles[1],Seed^0xBEEFU);rift::Match Match(Options);double ActiveSeconds=FPlatformTime::Seconds()-Began;
            while(Match.State().phase!=rift::Phase::Finished&&Match.State().elapsed<420&&!W->Stop.Load())
            {
                while((W->Paused.Load()||W->Battle.Load())&&!W->Stop.Load())FPlatformProcess::SleepNoStats(.02f);if(W->Stop.Load())break;double StepBegan=FPlatformTime::Seconds();Match.Step(.2);Match.DrainEvents();ActiveSeconds+=FPlatformTime::Seconds()-StepBegan;
            }
            if(W->Stop.Load())break;
            bool Valid=Match.State().phase==rift::Phase::Finished;for(const auto& E:Match.State().entities)Valid&=FMath::IsFinite(E.hp)&&FMath::IsFinite(E.position.x)&&FMath::IsFinite(E.position.z);
            double Residual=0;const bool Economy=EconomyValid(Match.State(),Residual);Add(Copy,TEXT("economyChecks"),1);if(!Economy)Add(Copy,TEXT("economyInvalid"),1);Copy->SetNumberField(TEXT("maximumEconomyResidual"),FMath::Max(Number(Copy,TEXT("maximumEconomyResidual")),Residual));Valid&=Economy;
            if(Valid)Aggregate(Copy,Match.State());else{Add(Copy,TEXT("invalid"),1);RIFT_LOG(LogRift,Error,TEXT("Native Meta invalid match: seed %u, elapsed %.3f, economy residual %.9f"),Seed,Match.State().elapsed,Residual);}
            Add(Copy,TEXT("simulationWallSeconds"),ActiveSeconds);Copy->SetNumberField(TEXT("seed"),uint32(Seed*1664525U+1013904223U));int32 Complete=++W->Completed;
            if(Complete%25==0)
            {
                auto Checkpoint=MakeShared<FJsonObject>();Checkpoint->SetNumberField(TEXT("games"),Number(Copy,TEXT("games")));Checkpoint->SetStringField(TEXT("at"),FDateTime::UtcNow().ToIso8601());TArray<TSharedPtr<FJsonValue>> Rows;for(auto R:RowsFor(Object(Object(Copy,TEXT("buckets")),TEXT("all|all"))))Rows.Add(MakeShared<FJsonValueObject>(R));Checkpoint->SetArrayField(TEXT("cards"),Rows);auto Points=Copy->GetArrayField(TEXT("checkpoints"));Points.Add(MakeShared<FJsonValueObject>(Checkpoint));if(Points.Num()>600){for(int32 I=199;I>0;I-=2)Points.RemoveAt(I);}Copy->SetArrayField(TEXT("checkpoints"),Points);
            }
            if(Complete%5==0||Complete==1)Publish(W,Copy);
            int32 Rate=W->Rate.Load();double Delay=Rate>0?60.0/Rate-ActiveSeconds:W->HeadroomDelayMs.Load()/1000.0;while(Delay>0&&!W->Stop.Load()){double D=FMath::Min(.05,Delay);FPlatformProcess::SleepNoStats(float(D));Delay-=D;}
        }Publish(W,Copy);RIFT_LOG(LogRift,Log,TEXT("Native Meta worker ended: %d completed, %.0f total valid matches, %.0f invalid"),W->Completed.Load(),Number(Copy,TEXT("games")),Number(Copy,TEXT("invalid")));
    });
}
void URiftMetaSimulationSubsystem::Harvest(){if(!Worker)return;FString S;{FScopeLock Lock(&Worker->Mutex);S=MoveTemp(Worker->Pending);Worker->Pending.Empty();}if(!S.IsEmpty()){if(auto O=Parse(S)){Data=O;bDirty=true;}}}
void URiftMetaSimulationSubsystem::Stop(){if(Worker)Worker->Stop=true;if(Future.IsValid()){Future.Wait();Harvest();Future=TFuture<void>();}Worker.Reset();}
void URiftMetaSimulationSubsystem::Pause(bool Paused){bPaused=Paused;if(!Paused&&(!Worker||Future.IsReady()))Start();else if(Worker)Worker->Paused=Paused;}
void URiftMetaSimulationSubsystem::SetRate(int32 Value){if(Value!=0&&Value!=100&&Value!=250&&Value!=500)return;Rate=Value;if(Worker)Worker->Rate=Value;}
void URiftMetaSimulationSubsystem::SetBattleActive(bool Active){bBattle=Active;if(Worker)Worker->Battle=Active;}
void URiftMetaSimulationSubsystem::Reset(){Stop();Persist();Data=NewDataset();CurrentFile=String(Data,TEXT("id"))+TEXT(".json");bDirty=true;bPaused=true;Persist();}
void URiftMetaSimulationSubsystem::Persist()
{
    if(!bDirty||!Data||bBattle)return;FString Error;
    if(URiftProfileSubsystem::AtomicWrite(FPaths::Combine(URiftProfileSubsystem::SaveRoot(),TEXT("Meta"),CurrentFile),Text(Data),Error)){if(auto* Instance=GetGameInstance())if(auto* P=Instance->GetSubsystem<URiftProfileSubsystem>()){P->MetaFiles.AddUnique(CurrentFile);P->Save();}bDirty=false;RIFT_LOG(LogRift,Log,TEXT("Meta dataset %s saved: %.0f matches"),*CurrentFile,Number(Data,TEXT("games")));}else{LastError=Error;RIFT_LOG(LogRift,Error,TEXT("Meta save failed: %s"),*Error);}
}
FString URiftMetaSimulationSubsystem::Status()const
{FString State=bBattle?TEXT("Paused during battle"):bPaused||!Worker?TEXT("Paused"):Future.IsReady()?TEXT("Batch complete"):TEXT("Simulating");return FString::Printf(TEXT("%s \u00B7 %.0f matches \u00B7 %.0f invalid \u00B7 %.1fs active simulation time \u00B7 %s"),*State,Number(Data,TEXT("games")),Number(Data,TEXT("invalid")),Number(Data,TEXT("simulationWallSeconds")),*String(Data,TEXT("fingerprint")).Left(8));}
TArray<FString> URiftMetaSimulationSubsystem::DatasetNames()const{return GetGameInstance()->GetSubsystem<URiftProfileSubsystem>()->MetaFiles;}
bool URiftMetaSimulationSubsystem::SelectDataset(const FString& Name)
{if(!DatasetNames().Contains(Name))return false;Stop();Persist();FString S;if(!FFileHelper::LoadFileToString(S,*FPaths::Combine(URiftProfileSubsystem::SaveRoot(),TEXT("Meta"),FPaths::GetCleanFilename(Name))))return false;auto O=Parse(S);if(!O||Number(O,TEXT("schemaVersion"))!=1)return false;Data=O;CurrentFile=FPaths::GetCleanFilename(Name);bDirty=false;bPaused=true;return true;}
TSharedPtr<FJsonObject> URiftMetaSimulationSubsystem::Bucket(const FString& Style,const FString& Archetype)const{return ReadObject(ReadObject(Data,TEXT("buckets")),Style+TEXT("|")+Archetype);}
TArray<TSharedPtr<FJsonObject>> URiftMetaSimulationSubsystem::CardRows(const FString& Style,const FString& Archetype)const{auto Rows=RowsFor(Bucket(Style,Archetype));ApplyCapturedIdentity(Rows,Data);return Rows;}
TArray<TSharedPtr<FJsonObject>> URiftMetaSimulationSubsystem::SynergyRows(const FString& Style,const FString& Archetype)const
{
    auto B=Bucket(Style,Archetype);auto Pairs=ReadObject(B,TEXT("pairs"));auto Cards=CardRows(Style,Archetype);TMap<FString,double> WR;TMap<FString,FString> Names;for(auto C:Cards){const FString Id=String(C,TEXT("id"));WR.Add(Id,Number(C,TEXT("adjustedWinRate")));Names.Add(Id,String(C,TEXT("name")));}
    TArray<TSharedPtr<FJsonObject>> Rows;const auto& Roster=rift::Cards();for(size_t A=0;A<Roster.size();++A)for(size_t BIndex=A+1;BIndex<Roster.size();++BIndex)
    {
        if(!WR.Contains(FS(Roster[A].id))||!WR.Contains(FS(Roster[BIndex].id)))continue;
        const auto& CA=Roster[A];const auto& CB=Roster[BIndex];FString IA=FS(CA.id),IB=FS(CB.id),Key=IA<IB?IA+TEXT("+")+IB:IB+TEXT("+")+IA;auto S=ReadObject(Pairs,Key);double N=Number(S,TEXT("n")),Score=Number(S,TEXT("score")),Base=(WR[IA]+WR[IB])/2,Raw=N?100*Score/N:Base;auto CI=Wilson(Score,N);auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("id"),Key);R->SetStringField(TEXT("name"),FS(CA.name)+TEXT(" + ")+FS(CB.name));R->SetNumberField(TEXT("cleanN"),N);R->SetNumberField(TEXT("baseline"),Base);R->SetNumberField(TEXT("adjustedWinRate"),(100*Score+48*Base)/(N+48));R->SetNumberField(TEXT("delta"),N?(Raw-Base)*N/(N+48):0);if(N)R->SetNumberField(TEXT("rawWinRate"),Raw);else R->SetField(TEXT("rawWinRate"),MakeShared<FJsonValueNull>());R->SetNumberField(TEXT("rawDelta"),N?Raw-Base:0);R->SetNumberField(TEXT("ciLow"),CI.Key);R->SetNumberField(TEXT("ciHigh"),CI.Value);R->SetNumberField(TEXT("mechanicalSynergy"),rift::PairSynergy(CA,CB));Rows.Add(R);
        Rows.Last()->SetStringField(TEXT("name"),Names.FindRef(IA)+TEXT(" + ")+Names.FindRef(IB));
        MechanicalIdentity(Rows.Last());EmpiricalIdentity(Rows.Last(),Data);
    }return Rows;
}
TArray<TSharedPtr<FJsonObject>> URiftMetaSimulationSubsystem::StyleRows(const FString& Archetype)const
{TArray<TSharedPtr<FJsonObject>> Rows;const double Total=Number(Bucket(TEXT("all"),Archetype),TEXT("n"));for(const TCHAR* Style:{TEXT("beatdown"),TEXT("aggro"),TEXT("control"),TEXT("cycle"),TEXT("split"),TEXT("spell_cycle"),TEXT("counter")})Rows.Add(SummaryFor(Bucket(Style,Archetype),Style,Total,true));return Rows;}
TArray<TSharedPtr<FJsonObject>> URiftMetaSimulationSubsystem::ArchetypeRows(const FString& Style)const
{TArray<TSharedPtr<FJsonObject>> Rows;auto Buckets=ReadObject(Data,TEXT("buckets"));const double Total=Number(Bucket(Style,TEXT("all")),TEXT("n"));const FString Prefix=Style+TEXT("|");for(const auto& Pair:Buckets->Values){FString Key(*Pair.Key);if(Key.StartsWith(Prefix)&&Key!=Prefix+TEXT("all")&&Pair.Value->Type==EJson::Object)Rows.Add(SummaryFor(Pair.Value->AsObject(),Key.Mid(Prefix.Len()),Total,false));}return Rows;}
TArray<TSharedPtr<FJsonObject>> URiftMetaSimulationSubsystem::MatchupRows()const
{TArray<TSharedPtr<FJsonObject>> R;for(const auto& A:rift::Cards())for(const auto& B:rift::Cards()){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("id"),FS(A.id)+TEXT("/")+FS(B.id));O->SetStringField(TEXT("name"),FS(A.name)+TEXT(" vs ")+FS(B.name));O->SetNumberField(TEXT("edge"),rift::CounterScore(A,B)-rift::CounterScore(B,A));O->SetStringField(TEXT("reason"),FS(rift::CounterReason(A,B)));MechanicalIdentity(O);R.Add(O);}return R;}
TArray<TSharedPtr<FJsonObject>> URiftMetaSimulationSubsystem::Checkpoints()const{TArray<TSharedPtr<FJsonObject>> R;const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(Data&&Data->TryGetArrayField(TEXT("checkpoints"),A))for(auto V:*A)if(V.IsValid()&&V->Type==EJson::Object){auto Point=V->AsObject();if(Number(Point,TEXT("games"),-1)>=0)R.Add(Point);}return R;}
TArray<TSharedPtr<FJsonObject>> URiftMetaSimulationSubsystem::AlertRows(const FString& Style,const FString& Archetype)const
{
    TArray<TSharedPtr<FJsonObject>> R;
    for(auto C:CardRows(Style,Archetype))
    {
        double N=Number(C,TEXT("cleanN")),Low=Number(C,TEXT("ciLow")),High=Number(C,TEXT("ciHigh")),Adjusted=Number(C,TEXT("adjustedWinRate"));
        if(N<100)continue;bool Strong=Low>50&&Adjusted>=53,Weak=High<50&&Adjusted<=47;if(!Strong&&!Weak)continue;
        FString Severity=N>=1000&&(Low>=54||High<=46)?TEXT("STRONG SIGNAL"):N>=400&&(Low>=52||High<=48)?TEXT("CONCERN"):TEXT("WATCH");
        C->SetStringField(TEXT("severity"),Severity);C->SetStringField(TEXT("direction"),Strong?TEXT("POSSIBLY OVERTUNED"):TEXT("POSSIBLY UNDERTUNED"));
        C->SetStringField(TEXT("alert"),TEXT("Confidence-gated association. Inspect deck, style and tactical confounding before changing balance."));R.Add(C);
    }return R;
}
TArray<TSharedPtr<FJsonObject>> URiftMetaSimulationSubsystem::PatchRows(const FString& Baseline)const
{
    TArray<TSharedPtr<FJsonObject>> R;FString S;TSharedPtr<FJsonObject> Old;if(DatasetNames().Contains(Baseline)&&FFileHelper::LoadFileToString(S,*FPaths::Combine(URiftProfileSubsystem::SaveRoot(),TEXT("Meta"),FPaths::GetCleanFilename(Baseline))))Old=Parse(S);
    const bool Compatible=Old&&String(Old,TEXT("model"))==String(Data,TEXT("model"))&&Number(Old,TEXT("telemetryRevision"))==Number(Data,TEXT("telemetryRevision"))&&Old->HasField(TEXT("cardSnapshot"))&&Old->HasField(TEXT("rulesSnapshot"));
    auto OldRows=Compatible?RowsFor(ReadObject(ReadObject(Old,TEXT("buckets")),TEXT("all|all"))):TArray<TSharedPtr<FJsonObject>>{};if(Compatible)ApplyCapturedIdentity(OldRows,Old);
    for(auto C:CardRows())
    {
        C->SetStringField(TEXT("baseline"),Baseline);C->SetField(TEXT("delta"),MakeShared<FJsonValueNull>());bool Found=false;
        for(auto B:OldRows)if(String(B,TEXT("id"))==String(C,TEXT("id")))
        {
            C->SetNumberField(TEXT("baselineN"),Number(B,TEXT("cleanN")));C->SetNumberField(TEXT("baselineWinRate"),Number(B,TEXT("adjustedWinRate")));C->SetStringField(TEXT("baselineFingerprint"),String(Old,TEXT("fingerprint")));C->SetStringField(TEXT("baselineVersion"),String(Old,TEXT("version")));
            if(Number(B,TEXT("cleanN"))>0&&Number(C,TEXT("cleanN"))>0){C->SetNumberField(TEXT("delta"),Number(C,TEXT("adjustedWinRate"))-Number(B,TEXT("adjustedWinRate")));C->SetStringField(TEXT("availability"),TEXT("Recorded native observations"));}else C->SetStringField(TEXT("availability"),TEXT("Insufficient clean baseline or current samples"));Found=true;break;
        }
        if(!Found)C->SetStringField(TEXT("availability"),Old?TEXT("Baseline lacks compatible immutable native telemetry"):TEXT("No recorded baseline dataset"));R.Add(C);
    }return R;
}
bool URiftMetaSimulationSubsystem::Export(const FString& Subject,const FString& Filename,const FString& Style,const FString& Archetype,bool CSV)
{
    TArray<TSharedPtr<FJsonObject>> Rows=Subject==TEXT("synergy")?SynergyRows(Style,Archetype):Subject==TEXT("matchups")?MatchupRows():Subject==TEXT("styles")?StyleRows(Archetype):Subject==TEXT("archetypes")?ArchetypeRows(Style):Subject==TEXT("trends")?Checkpoints():Subject==TEXT("alerts")?AlertRows(Style,Archetype):Subject==TEXT("patch")?PatchRows(Style):CardRows(Style,Archetype);FString Output;
    if(CSV)
    {
        TSet<FString> Keys;for(auto O:Rows)for(const auto& P:O->Values)Keys.Add(FString(*P.Key));auto Sorted=Keys.Array();Sorted.Sort();auto Escape=[](FString S){S.ReplaceInline(TEXT("\""),TEXT("\"\""));return TEXT("\"")+S+TEXT("\"");};for(int32 I=0;I<Sorted.Num();++I)Output+=(I?TEXT(","):TEXT(""))+Escape(Sorted[I]);Output+=TEXT("\r\n");
        for(auto O:Rows){for(int32 I=0;I<Sorted.Num();++I){auto V=O->TryGetField(Sorted[I]);FString Cell;if(V){if(V->Type==EJson::Number)Cell=FString::SanitizeFloat(V->AsNumber());else if(V->Type==EJson::String)Cell=V->AsString();else if(V->Type==EJson::Boolean)Cell=V->AsBool()?TEXT("true"):TEXT("false");else if(V->Type==EJson::Object||V->Type==EJson::Array)FJsonSerializer::Serialize(V,TEXT(""),TJsonWriterFactory<>::Create(&Cell));}Output+=(I?TEXT(","):TEXT(""))+Escape(Cell);}Output+=TEXT("\r\n");}
    }
    else{auto O=ReportIdentity(Subject);O->SetStringField(TEXT("subject"),Subject);O->SetStringField(TEXT("styleFilter"),Style);O->SetStringField(TEXT("archetypeFilter"),Archetype);TArray<TSharedPtr<FJsonValue>> A;for(auto R:Rows)A.Add(MakeShared<FJsonValueObject>(R));O->SetArrayField(TEXT("rows"),A);Output=Text(O);}
    return URiftProfileSubsystem::AtomicWrite(Filename,Output,LastError);
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftMetaExpansionAttributionTest,"Rift.Meta.ExpansionAttribution",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftMetaExpansionAttributionTest::RunTest(const FString&)
{
    // Synthetic end-state isolates the production aggregation contract. Native
    // combat fixtures separately prove that these observations are emitted.
    rift::Snapshot State;State.elapsed=10;State.winner=0;
    for(int Team=0;Team<2;++Team)for(size_t I=0;I<8;++I)State.decks[Team].push_back(rift::Cards()[I].id);
    State.decks[0][0]="bone_warden";State.decks[0][1]="iron_giant";
    const auto* Parent=rift::FindCard("bone_warden");const auto* Giant=rift::FindCard("iron_giant");
    TestNotNull(TEXT("Approved paid parent exists"),Parent);TestNotNull(TEXT("Approved shield bearer exists"),Giant);if(!Parent||!Giant)return false;
    auto& Paid=State.telemetry[0][Parent->id];Paid.plays=1;Paid.spent=Parent->cost;Paid.spawns=1;Paid.deaths=1;
    Paid.childSpawns=4;Paid.childDeaths=1;Paid.childSurviving=3;Paid.troopDamage=40;Paid.lifetime=23;
    rift::EntityTelemetry Body;Body.id=100;Body.playId=10;Body.cardId=Body.attributionCardId=Parent->id;Body.observed.spawns=1;Body.observed.deaths=1;Body.observed.lifetime=3;State.entityTelemetry[0].emplace(Body.id,Body);
    for(int I=0;I<4;++I){rift::EntityTelemetry Child;Child.id=101+I;Child.parentEntityId=100;Child.playId=10;Child.cardId=Parent->deathSpawnId;Child.attributionCardId=Parent->id;Child.observed.spawns=1;Child.observed.deaths=I==0?1:0;Child.observed.surviving=I==0?0:1;Child.observed.troopDamage=10;Child.observed.lifetime=5;State.entityTelemetry[0].emplace(Child.id,Child);}
    auto& Shield=State.telemetry[0][Giant->id];Shield.plays=1;Shield.spent=Giant->cost;Shield.spawns=1;Shield.surviving=1;Shield.shieldDamageTaken=310;Shield.shieldAbsorbed=400;Shield.overflowPrevented=90;
    rift::EntityTelemetry Protected;Protected.id=200;Protected.playId=11;Protected.cardId=Protected.attributionCardId=Giant->id;Protected.observed.spawns=1;Protected.observed.surviving=1;Protected.observed.shieldDamageTaken=310;Protected.observed.shieldAbsorbed=400;Protected.observed.overflowPrevented=90;State.entityTelemetry[0].emplace(Protected.id,Protected);
    State.spent[0]=Parent->cost+Giant->cost;auto Dataset=NewDataset();Aggregate(Dataset,State);auto Bucket=ReadObject(ReadObject(Dataset,TEXT("buckets")),TEXT("all|all"));
    auto ParentRow=ReadObject(ReadObject(Bucket,TEXT("cards")),FS(Parent->id));auto ChildRow=ReadObject(ReadObject(Bucket,TEXT("entities")),FS(Parent->deathSpawnId));
    TestEqual(TEXT("Expansion observations receive a distinct telemetry identity"),Number(Dataset,TEXT("telemetryRevision")),4.0);
    auto Old=Parse(Text(Dataset));Old->SetNumberField(TEXT("telemetryRevision"),3);TestFalse(TEXT("Same-hash prior telemetry cannot resume child/shield denominators"),DatasetCanResume(Old));
    TestEqual(TEXT("Children remain outside the paid card roster"),Number(ParentRow,TEXT("spawns")),1.0);TestFalse(TEXT("No manufactured child card appearances"),ReadObject(Bucket,TEXT("cards"))->HasField(FS(Parent->deathSpawnId)));
    TestEqual(TEXT("Four death summons remain separately observed"),Number(ChildRow,TEXT("observations")),4.0);TestEqual(TEXT("Child attribution retains actual paid parent"),String(ChildRow,TEXT("parentCardId")),FS(Parent->id));
    TestEqual(TEXT("Child bodies add no casts"),Number(ChildRow,TEXT("plays")),0.0);TestEqual(TEXT("Child bodies add no spend"),Number(ChildRow,TEXT("spent")),0.0);
    TestEqual(TEXT("Parent contribution includes child damage"),Number(ParentRow,TEXT("troopDamage")),40.0);TestEqual(TEXT("Child damage remains independently inspectable"),Number(ChildRow,TEXT("troopDamage")),40.0);
    TestEqual(TEXT("Real shield loss excludes discarded overflow"),Number(ReadObject(ReadObject(Bucket,TEXT("cards")),FS(Giant->id)),TEXT("shieldDamageTaken")),310.0);TestEqual(TEXT("Protected packet absorption retains discarded overflow"),Number(ReadObject(ReadObject(Bucket,TEXT("cards")),FS(Giant->id)),TEXT("shieldAbsorbed")),400.0);
    for(const auto& Row:RowsFor(Bucket))if(String(Row,TEXT("id"))==FS(Parent->id)){TestEqual(TEXT("Parent lifetime averages over paid and child bodies"),Number(Row,TEXT("averageLifetime")),23.0/5);TestEqual(TEXT("Parent survival rate still describes its paid body"),Number(Row,TEXT("survivalRate")),0.0);}
    const auto& Entities=Dataset->GetArrayField(TEXT("entitySnapshot"));TestEqual(TEXT("Subordinate mechanics are captured outside playable definitions"),Entities.Num(),int32(rift::EntityDefinitions().size()));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftMetaAggregationTest,"Rift.Meta.AggregationEconomy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftMetaAggregationTest::RunTest(const FString& Parameters)
{
    rift::MatchOptions Options;Options.seed=74021;Options.aiEnabled={true,true};Options.aiStyles={"counter","control"};
    Options.decks[0]=rift::BuildAIDeck("counter",Options.seed^0x72F3U);Options.decks[1]=rift::BuildAIDeck("control",Options.seed^0xBEEFU);
    rift::Match Match(Options);while(Match.State().phase!=rift::Phase::Finished&&Match.State().elapsed<420){Match.Step(.2);Match.DrainEvents();}
    const auto& State=Match.State();TestTrue(TEXT("Actual seeded native match finishes"),State.phase==rift::Phase::Finished);
    double Residual=0;TestTrue(TEXT("Both actual Aether banks conserve paid costs, natural generation and leaks"),EconomyValid(State,Residual));TestTrue(TEXT("Only the terminal fixed-step budget can remain ungenerated"),Residual<.05);
    auto Dataset=NewDataset();Aggregate(Dataset,State);auto Bucket=ReadObject(ReadObject(Dataset,TEXT("buckets")),TEXT("all|all"));
    TestTrue(TEXT("Current well-formed dataset can resume"),DatasetCanResume(Dataset));auto BadDataset=Parse(Text(Dataset));BadDataset->SetNumberField(TEXT("schemaVersion"),2);TestFalse(TEXT("Future-schema dataset cannot be appended by this build"),DatasetCanResume(BadDataset));BadDataset=Parse(Text(Dataset));BadDataset->RemoveField(TEXT("checkpoints"));TestFalse(TEXT("Missing checkpoint array cannot reach the worker's required-array access"),DatasetCanResume(BadDataset));BadDataset=Parse(Text(Dataset));BadDataset->SetNumberField(TEXT("seed"),-1);TestFalse(TEXT("Negative seed cannot convert to an unsigned simulation seed"),DatasetCanResume(BadDataset));
    TestEqual(TEXT("One complete match recorded"),Number(Dataset,TEXT("games")),1.0);TestEqual(TEXT("Two side observations recorded"),Number(Bucket,TEXT("n")),2.0);TestEqual(TEXT("Win scores sum to one including draws"),Number(Bucket,TEXT("score")),1.0);
    TestEqual(TEXT("Actual crowns retained"),Number(Bucket,TEXT("crowns")),double(State.crowns[0]+State.crowns[1]));TestTrue(TEXT("Actual spend retained"),FMath::Abs(Number(Bucket,TEXT("spent"))-State.spent[0]-State.spent[1])<1e-7);
    auto Stats=ReadObject(Bucket,TEXT("cards"));double Appearances=0,Clean=0,Mirrors=0,CardSpent=0;for(const auto& Pair:Stats->Values){auto Card=Pair.Value->AsObject();Appearances+=Number(Card,TEXT("appearances"));Clean+=Number(Card,TEXT("cleanN"));Mirrors+=Number(Card,TEXT("mirror"));CardSpent+=Number(Card,TEXT("spent"));}
    TestEqual(TEXT("Eight cards per side have sixteen total deck appearances"),Appearances,16.0);TestEqual(TEXT("Clean samples plus mirror exclusions account for every appearance"),Clean+Mirrors,Appearances);TestTrue(TEXT("Card spend equals real side spend"),FMath::Abs(CardSpent-State.spent[0]-State.spent[1])<1e-7);
    auto Summary=SummaryFor(Bucket,TEXT("all"),2,false);TestEqual(TEXT("Aggregate duration uses actual complete matches"),Number(Summary,TEXT("averageDuration")),State.elapsed);TestEqual(TEXT("Aggregate crowns use actual side observations"),Number(Summary,TEXT("averageCrowns")),double(State.crowns[0]+State.crowns[1])/2);
    auto Empty=MakeShared<FJsonObject>();auto EmptyRows=RowsFor(Empty);TestEqual(TEXT("Reading absent statistics does not mutate archived data"),Empty->Values.Num(),0);TestTrue(TEXT("Unsampled raw rate stays unavailable"),EmptyRows[0]->TryGetField(TEXT("rawWinRate"))->Type==EJson::Null);TestTrue(TEXT("No invented damage efficiency without paid spend"),EmptyRows[0]->TryGetField(TEXT("damagePerAether"))->Type==EJson::Null);
    const int32 RosterCount=int32(rift::Cards().size());const auto& Captured=Dataset->GetArrayField(TEXT("cardSnapshot"));TestEqual(TEXT("All current immutable card definitions captured"),Captured.Num(),RosterCount);TestTrue(TEXT("Rule snapshot preserved"),Dataset->HasField(TEXT("rulesSnapshot")));TestEqual(TEXT("Counter personality receives its own observation"),Number(ReadObject(ReadObject(Dataset,TEXT("buckets")),TEXT("counter|all")),TEXT("n")),1.0);
    TestEqual(TEXT("Dataset balance hash identifies its compiled canonical definitions"),String(Dataset,TEXT("balanceHash")),FS(rift::CardManifestHash()));
    TestFalse(TEXT("Compiled balance hash cannot be an absent identity"),rift::CardManifestHash().empty());
    for(const auto& Value:Captured)
    {
        const auto Definition=Value->AsObject();const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*String(Definition,TEXT("id"))));
        TestTrue(TEXT("Every immutable card snapshot captures its authoritative impact delay"),Card&&Definition->HasField(TEXT("castDelay"))&&FMath::IsNearlyEqual(Number(Definition,TEXT("castDelay")),Card->castDelay,1e-7));
        TestTrue(TEXT("Snapshots capture collision bodies independently from visual scale"),Card&&Definition->HasField(TEXT("collisionRadius"))&&FMath::IsNearlyEqual(Number(Definition,TEXT("collisionRadius")),Card->collisionRadius,1e-7));
        TestTrue(TEXT("Snapshots capture explicit ground, air and structure eligibility"),Card&&Definition->HasField(TEXT("targetMask"))&&Number(Definition,TEXT("targetMask"))==double(Card->targetMask));
        TestTrue(TEXT("Snapshots capture each card's actual health-cloak trigger"),Card&&Definition->HasField(TEXT("healthCloakThreshold"))&&FMath::IsNearlyEqual(Number(Definition,TEXT("healthCloakThreshold")),Card->healthCloakThreshold,1e-7));
    }
    TestTrue(TEXT("Immutable rule snapshot describes target selection at impact"),ReadObject(Dataset,TEXT("rulesSnapshot"))->HasField(TEXT("spellImpact")));
    TestEqual(TEXT("Immutable rule snapshot captures the compiled navigation revision"),Number(ReadObject(Dataset,TEXT("rulesSnapshot")),TEXT("navigationRevision")),double(rift::arena::NavigationRevision));
    TestEqual(TEXT("New Meta snapshots capture dynamic arena fingerprint policy"),Number(Dataset,TEXT("fingerprintPolicyRevision")),2.0);
    TestEqual(TEXT("Immutable arena width captures the confirmed22 tiles"),Number(ReadObject(Dataset,TEXT("rulesSnapshot")),TEXT("arenaWidth")),22.0);
    TestEqual(TEXT("Immutable arena length captures the confirmed40 tiles"),Number(ReadObject(Dataset,TEXT("rulesSnapshot")),TEXT("arenaHeight")),40.0);
    TestEqual(TEXT("Immutable bridge center follows actual compiled tile geometry"),Number(ReadObject(Dataset,TEXT("rulesSnapshot")),TEXT("bridgeCenterX")),rift::arena::BridgeCenterX);
    TestEqual(TEXT("Immutable guard depth follows actual compiled tile geometry"),Number(ReadObject(Dataset,TEXT("rulesSnapshot")),TEXT("guardDepth")),rift::arena::GuardDepth);
    TestEqual(TEXT("Immutable core depth follows actual compiled tile geometry"),Number(ReadObject(Dataset,TEXT("rulesSnapshot")),TEXT("coreDepth")),rift::arena::CoreDepth);
    BadDataset=Parse(Text(Dataset));BadDataset->SetStringField(TEXT("fingerprint"),TEXT("a78ef16b6df859a74bccf91a98b9b472"));TestFalse(TEXT("Preserved30x44 canonical observations remain historical and cannot be appended to22x40 games"),DatasetCanResume(BadDataset));
    TestTrue(TEXT("Immutable navigation prose captures the actual fifteen-member Stampede layout"),String(ReadObject(Dataset,TEXT("rulesSnapshot")),TEXT("navigation")).Contains(TEXT("five across, three deep")));
    TestEqual(TEXT("First-real-attack Crown commitment has an explicit combat revision"),Number(ReadObject(Dataset,TEXT("rulesSnapshot")),TEXT("combatRevision")),double(rift::arena::CombatRevision));
    TestEqual(TEXT("Immutable Crown lock rule records the actual attack trigger"),String(ReadObject(Dataset,TEXT("rulesSnapshot")),TEXT("targetHardLock")),FString(TEXT("first authoritative attack against a Crown Tower")));
    BadDataset=Parse(Text(Dataset));BadDataset->SetStringField(TEXT("fingerprint"),TEXT("2742ab27e50b08046745f9190dea2807"));TestFalse(TEXT("Historical fourteen-card collision observations cannot be appended to the sixteen-card balance patch"),DatasetCanResume(BadDataset));
    BadDataset=Parse(Text(Dataset));BadDataset->SetStringField(TEXT("fingerprint"),TEXT("b3968027993fd0ece72577b77c48d76e"));TestFalse(TEXT("Historical tower-blocking navigation observations cannot be appended to current pathing rules"),DatasetCanResume(BadDataset));
    BadDataset=Parse(Text(Dataset));BadDataset->SetStringField(TEXT("fingerprint"),TEXT("archived-instant-spell-rules"));TestFalse(TEXT("Historical instant-spell observations cannot be appended to current delayed-spell rules"),DatasetCanResume(BadDataset));
    BadDataset=Parse(Text(Dataset));BadDataset->SetStringField(TEXT("fingerprint"),TEXT("6e74a0ba433dc47d8e8a2de834a3cad1"));TestFalse(TEXT("Historical 28x42 observations cannot be appended to expanded arena rules"),DatasetCanResume(BadDataset));
    TestEqual(TEXT("Body contact skin is captured in immutable rules"),Number(ReadObject(Dataset,TEXT("rulesSnapshot")),TEXT("collisionSkin")),.02);TestEqual(TEXT("Structure clearance is retained"),Number(ReadObject(Dataset,TEXT("rulesSnapshot")),TEXT("structureClearance")),.22);TestTrue(TEXT("Immutable rules identify the separate air layer"),String(ReadObject(Dataset,TEXT("rulesSnapshot")),TEXT("collision")).Contains(TEXT("flying troops share an air layer")));
    BadDataset=Parse(Text(Dataset));BadDataset->SetStringField(TEXT("fingerprint"),TEXT("50e3be867ab070246b34be2cc6914196"));TestFalse(TEXT("Historical overlap movement observations cannot be appended to layered collision rules"),DatasetCanResume(BadDataset));
    BadDataset=Parse(Text(Dataset));BadDataset->SetStringField(TEXT("fingerprint"),TEXT("748cbe3efc851679372078272e7b9f2d"));TestFalse(TEXT("Pre-release crowd observations cannot be appended to visible pocket routing rules"),DatasetCanResume(BadDataset));
    auto TableRows=RowsFor(Bucket);
    for(const auto& Row:TableRows)
    {
        auto S=ReadObject(Stats,String(Row,TEXT("id")));const double N=Number(S,TEXT("cleanN")),Score=Number(S,TEXT("score")),Ap=Number(S,TEXT("appearances")),Spent=Number(S,TEXT("spent"));
        TestTrue(TEXT("Table pick rate uses actual deck-side observations"),FMath::IsNearlyEqual(Number(Row,TEXT("pickRate")),100*Ap/2,1e-7));
        TestTrue(TEXT("Table adjusted rate uses 24 observations of 50-percent prior"),FMath::IsNearlyEqual(Number(Row,TEXT("adjustedWinRate")),100*(Score+12)/(N+24),1e-7));
        if(N>0)TestTrue(TEXT("Table raw rate uses clean match scores including half draws"),FMath::IsNearlyEqual(Number(Row,TEXT("rawWinRate")),100*Score/N,1e-7));
        if(Spent>0)TestTrue(TEXT("Damage efficiency uses actual paid spend and applied HP damage"),FMath::IsNearlyEqual(Number(Row,TEXT("damagePerAether")),(Number(S,TEXT("troopDamage"))+Number(S,TEXT("towerDamage"))+Number(S,TEXT("buildingDamage")))/Spent,1e-7));
    }
    // Unregistered fixture exercises production tables without profile writes or a worker.
    auto* TableEngine=NewObject<URiftMetaSimulationSubsystem>(NewObject<UGameInstance>());TableEngine->Data=Dataset;
    double StyleN=0;for(const auto& Row:TableEngine->StyleRows()){StyleN+=Number(Row,TEXT("cleanN"));if(Number(Row,TEXT("cleanN"))>0)TestTrue(TEXT("Style duration comes from complete observed matches"),FMath::IsNearlyEqual(Number(Row,TEXT("averageDuration")),State.elapsed,1e-7));}
    TestEqual(TEXT("Style tables partition both actual deck observations"),StyleN,2.0);
    for(const auto& Row:TableEngine->ArchetypeRows())
    {
        auto S=ReadObject(ReadObject(Dataset,TEXT("buckets")),TEXT("all|")+String(Row,TEXT("name")));
        TestTrue(TEXT("Overlapping archetype pick rates use observed deck counts"),FMath::IsNearlyEqual(Number(Row,TEXT("pickRate")),50*Number(S,TEXT("n")),1e-7));
    }
    const auto Pairs=ReadObject(Bucket,TEXT("pairs"));const auto PairRows=TableEngine->SynergyRows();TestEqual(TEXT("All unordered pairs appear in synergy table"),PairRows.Num(),RosterCount*(RosterCount-1)/2);
    for(const auto& Row:PairRows)
    {
        auto S=ReadObject(Pairs,String(Row,TEXT("id")));const double N=Number(S,TEXT("n")),Score=Number(S,TEXT("score")),Base=Number(Row,TEXT("baseline"));
        TestEqual(TEXT("Pair table excludes full-pair mirrors"),Number(Row,TEXT("cleanN")),N);
        TestTrue(TEXT("Synergy delta uses real observed score and 48-observation shrinkage"),FMath::IsNearlyEqual(Number(Row,TEXT("delta")),N?(100*Score/N-Base)*N/(N+48):0,1e-7));
    }
    const auto Matchups=TableEngine->MatchupRows();TestEqual(TEXT("Mechanical matchup table has every ordered current-roster cell"),Matchups.Num(),RosterCount*RosterCount);
    for(const auto& Row:Matchups){FString A,B;String(Row,TEXT("id")).Split(TEXT("/"),&A,&B);const auto* CA=rift::FindCard(TCHAR_TO_UTF8(*A));const auto* CB=rift::FindCard(TCHAR_TO_UTF8(*B));TestTrue(TEXT("Matchup edge uses authoritative mechanical rules"),CA&&CB&&FMath::IsNearlyEqual(Number(Row,TEXT("edge")),rift::CounterScore(*CA,*CB)-rift::CounterScore(*CB,*CA),1e-7));}
    auto Broken=State;Broken.spent[0]+=1;Residual=0;TestFalse(TEXT("Extra unaccounted spend fails validation"),EconomyValid(Broken,Residual));Broken=State;Broken.aether[0]=11;Residual=0;TestFalse(TEXT("Over-cap banks fail validation"),EconomyValid(Broken,Residual));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftMetaArchiveIdentityTest,"Rift.Meta.ArchiveIdentityAndFilters",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftMetaArchiveIdentityTest::RunTest(const FString& Parameters)
{
    FString RequestedSaveRoot;
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox"))||
       !FParse::Value(FCommandLine::Get(),TEXT("RiftSaveRoot="),RequestedSaveRoot)||RequestedSaveRoot.IsEmpty())
    {
        AddError(TEXT("Historical export fixtures require -RiftAutomationSandbox and an explicit isolated -RiftSaveRoot."));
        return false;
    }
    const FString SaveRoot=URiftProfileSubsystem::SaveRoot();
    if(!TestTrue(TEXT("Historical exports use the explicit automation save root"),FPaths::IsSamePath(SaveRoot,FPaths::ConvertRelativePathToFull(RequestedSaveRoot))))return false;
    // Synthetic historical data is private to this unregistered subsystem. No
    // profile, worker, existing saved dataset or live canonical definition is changed.
    // Production exports are written only to a new directory in the sandbox.
    auto Archived=NewDataset();Archived->RemoveField(TEXT("balanceHash"));
    Archived->SetStringField(TEXT("version"),TEXT("historical-fixture"));Archived->SetStringField(TEXT("fingerprint"),TEXT("archived-fixture-fingerprint"));
    auto CapturedRoster=Archived->GetArrayField(TEXT("cardSnapshot"));CapturedRoster.RemoveAll([](const auto& Value){return String(Value->AsObject(),TEXT("id"))==TEXT("vampire_bats");});Archived->SetArrayField(TEXT("cardSnapshot"),CapturedRoster);
    for(auto Value:Archived->GetArrayField(TEXT("cardSnapshot")))
    {
        auto Definition=Value->AsObject();
        if(String(Definition,TEXT("id"))==TEXT("archer_tower")){Definition->SetBoolField(TEXT("canHitAir"),false);continue;}
        if(String(Definition,TEXT("id"))!=TEXT("ironclad"))continue;
        Definition->SetStringField(TEXT("name"),TEXT("Archived Ironclad, \"Champion\""));Definition->SetNumberField(TEXT("cost"),19);
        Definition->SetStringField(TEXT("category"),TEXT("Spell"));Definition->SetBoolField(TEXT("flying"),true);
        Definition->SetBoolField(TEXT("canHitAir"),true);Definition->SetBoolField(TEXT("structuresOnly"),true);
        Definition->SetNumberField(TEXT("count"),3);Definition->SetNumberField(TEXT("splash"),2);
    }
    auto HistoricalBucket=Object(Object(Archived,TEXT("buckets")),TEXT("all|all"));HistoricalBucket->SetNumberField(TEXT("n"),12);
    auto Observation=Object(Object(HistoricalBucket,TEXT("cards")),TEXT("ironclad"));Observation->SetNumberField(TEXT("appearances"),12);Observation->SetNumberField(TEXT("cleanN"),12);Observation->SetNumberField(TEXT("score"),9);
    auto PairObservation=Object(Object(HistoricalBucket,TEXT("pairs")),TEXT("ember_archer+ironclad"));PairObservation->SetNumberField(TEXT("n"),4);PairObservation->SetNumberField(TEXT("score"),3);
    const FString Before=Text(Archived),LiveHash=FS(rift::CardManifestHash());
    auto* Meta=NewObject<URiftMetaSimulationSubsystem>(NewObject<UGameInstance>());Meta->Data=Archived;
    auto Rows=Meta->CardRows();auto Row=Rows.FindByPredicate([](const auto& R){return String(R,TEXT("id"))==TEXT("ironclad");});
    TestEqual(TEXT("Archived card table uses only its captured roster"),Rows.Num(),int32(rift::Cards().size())-1);
    TestFalse(TEXT("A later live card is not manufactured in archived empirical rows"),Rows.ContainsByPredicate([](const auto& R){return String(R,TEXT("id"))==TEXT("vampire_bats");}));
    TestNotNull(TEXT("Historical card remains readable without a balanceHash"),Row);if(!Row)return false;
    TestTrue(TEXT("Historical name, cost and category drive the actual filter helper"),URiftMetaSimulationSubsystem::CardRowMatches(*Row,TEXT("19"),TEXT("Spell"),TEXT("Air"),TEXT("Archived Ironclad")));
    TestFalse(TEXT("Current cost cannot replace historical cost in a filter"),URiftMetaSimulationSubsystem::CardRowMatches(*Row,FString::FromInt(rift::FindCard("ironclad")->cost),TEXT("all"),TEXT("all")));
    TestFalse(TEXT("Current troop category cannot replace captured historical category"),URiftMetaSimulationSubsystem::CardRowMatches(*Row,TEXT("all"),TEXT("Troop"),TEXT("all")));
    TestFalse(TEXT("Captured air spell cannot pass the ground filter"),URiftMetaSimulationSubsystem::CardRowMatches(*Row,TEXT("all"),TEXT("all"),TEXT("Ground")));
    for(const TCHAR* Trait:{TEXT("Anti-air"),TEXT("Win condition"),TEXT("Swarm"),TEXT("Splash")})TestTrue(FString(TEXT("Captured trait survives historical filtering: "))+Trait,URiftMetaSimulationSubsystem::CardRowMatches(*Row,TEXT("all"),TEXT("all"),Trait));
    const auto* ArchivedTower=Rows.FindByPredicate([](const auto& R){return String(R,TEXT("id"))==TEXT("archer_tower");});
    TestTrue(TEXT("Historical building category and ground trait remain filterable"),ArchivedTower&&URiftMetaSimulationSubsystem::CardRowMatches(*ArchivedTower,TEXT("all"),TEXT("Building"),TEXT("Ground")));
    TestTrue(TEXT("Removed historical anti-air coverage does not inherit current targeting"),ArchivedTower&&!URiftMetaSimulationSubsystem::CardRowMatches(*ArchivedTower,TEXT("all"),TEXT("all"),TEXT("Anti-air")));
    TestEqual(TEXT("Historical clean sample count remains recorded"),Number(*Row,TEXT("cleanN")),12.0);TestEqual(TEXT("Historical raw win score is not recomputed from current balance"),Number(*Row,TEXT("rawWinRate")),75.0);
    TestTrue(TEXT("Historical adjusted rate retains its 24-observation prior"),FMath::IsNearlyEqual(Number(*Row,TEXT("adjustedWinRate")),100.*21/36,1e-7));
    const auto PairRows=Meta->SynergyRows();const auto* Pair=PairRows.FindByPredicate([](const auto& R){return String(R,TEXT("id"))==TEXT("ember_archer+ironclad");});
    TestEqual(TEXT("Archived pair table uses only captured-roster combinations"),PairRows.Num(),Rows.Num()*(Rows.Num()-1)/2);
    TestFalse(TEXT("A later live card cannot create archived empirical pairs"),PairRows.ContainsByPredicate([](const auto& R){return String(R,TEXT("id")).Contains(TEXT("vampire_bats"));}));
    TestNotNull(TEXT("Historical observed pair remains available"),Pair);if(Pair)
    {
        TestTrue(TEXT("Observed pair name uses captured historical card identity"),String(*Pair,TEXT("name")).Contains(TEXT("Archived Ironclad")));
        TestEqual(TEXT("Pair N retains historical observations"),Number(*Pair,TEXT("cleanN")),4.0);
        TestEqual(TEXT("Pair mechanics identify the current canonical hash"),String(*Pair,TEXT("mechanicalBalanceHash")),LiveHash);
        TestEqual(TEXT("Pair observations identify their archived dataset"),String(*Pair,TEXT("empiricalFingerprint")),FString(TEXT("archived-fixture-fingerprint")));
        TestTrue(TEXT("Absent historical balance hash stays unavailable"),(*Pair)->TryGetField(TEXT("empiricalBalanceHash"))->Type==EJson::Null);
    }
    auto Identity=Meta->ReportIdentity(TEXT("synergy"));TestEqual(TEXT("JSON export metadata preserves archived empirical version"),String(Identity,TEXT("empiricalDatasetVersion")),FString(TEXT("historical-fixture")));
    TestEqual(TEXT("JSON export metadata separately identifies current mechanical version"),String(Identity,TEXT("mechanicalVersion")),FS(rift::CardManifestVersion()));
    const auto Matchups=Meta->MatchupRows();TestTrue(TEXT("Every mechanical-only matchup has explicit current attribution"),Matchups.Num()>0&&String(Matchups[0],TEXT("mechanicalBalanceHash"))==LiveHash&&!Matchups[0]->HasField(TEXT("empiricalFingerprint")));
    TestFalse(TEXT("Mechanical-only export does not claim historical empirical outcomes"),Meta->ReportIdentity(TEXT("matchups"))->HasField(TEXT("empiricalFingerprint")));

    const FString FixtureRoot=FPaths::Combine(SaveRoot,TEXT("AutomationFixtures"),FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT("-meta-archive-exports"));
    if(!TestTrue(TEXT("Every serialization artifact stays below the isolated save root"),FPaths::IsUnderDirectory(FixtureRoot,SaveRoot)))return false;
    const FString ArchivedInput=FPaths::Combine(FixtureRoot,TEXT("archived-input.synthetic.json"));
    const FString SynergyJSON=FPaths::Combine(FixtureRoot,TEXT("synergy.json")),SynergyCSV=FPaths::Combine(FixtureRoot,TEXT("synergy.csv"));
    const FString MatchupsJSON=FPaths::Combine(FixtureRoot,TEXT("matchups.json")),MatchupsCSV=FPaths::Combine(FixtureRoot,TEXT("matchups.csv"));
    FString ExportError;
    if(!TestTrue(TEXT("Synthetic archived input writes atomically in its unique sandbox directory"),URiftProfileSubsystem::AtomicWrite(ArchivedInput,Before,ExportError)))return false;
    if(!TestTrue(TEXT("Production synergy JSON export succeeds"),Meta->Export(TEXT("synergy"),SynergyJSON))||
       !TestTrue(TEXT("Production synergy CSV export succeeds"),Meta->Export(TEXT("synergy"),SynergyCSV,TEXT("all"),TEXT("all"),true))||
       !TestTrue(TEXT("Production matchup JSON export succeeds"),Meta->Export(TEXT("matchups"),MatchupsJSON))||
       !TestTrue(TEXT("Production matchup CSV export succeeds"),Meta->Export(TEXT("matchups"),MatchupsCSV,TEXT("all"),TEXT("all"),true)))return false;
    auto ReadExport=[&](const FString& Path)
    {
        FString Saved;
        if(!TestTrue(TEXT("Production export can be read from disk: ")+FPaths::GetCleanFilename(Path),FFileHelper::LoadFileToString(Saved,*Path)))return TSharedPtr<FJsonObject>();
        auto Parsed=Parse(Saved);TestTrue(TEXT("Saved JSON parses: ")+FPaths::GetCleanFilename(Path),Parsed.IsValid());return Parsed;
    };
    auto SavedSynergy=ReadExport(SynergyJSON),SavedMatchups=ReadExport(MatchupsJSON);
    if(!SavedSynergy||!SavedMatchups)return false;
    auto CheckMechanicalIdentity=[&](const TSharedPtr<FJsonObject>& Value,const FString& Label)
    {
        TestEqual(Label+TEXT(" mechanical source"),String(Value,TEXT("mechanicalSource")),FString(TEXT("current canonical definitions")));
        TestEqual(Label+TEXT(" mechanical version"),String(Value,TEXT("mechanicalVersion")),FS(rift::CardManifestVersion()));
        TestEqual(Label+TEXT(" mechanical hash"),String(Value,TEXT("mechanicalBalanceHash")),LiveHash);
    };
    auto CheckEmpiricalIdentity=[&](const TSharedPtr<FJsonObject>& Value,const FString& Label)
    {
        TestEqual(Label+TEXT(" empirical version"),String(Value,TEXT("empiricalDatasetVersion")),FString(TEXT("historical-fixture")));
        TestEqual(Label+TEXT(" empirical fingerprint"),String(Value,TEXT("empiricalFingerprint")),FString(TEXT("archived-fixture-fingerprint")));
        auto Hash=Value->TryGetField(TEXT("empiricalBalanceHash"));
        TestTrue(Label+TEXT(" absent historical hash is explicitly null"),Hash.IsValid()&&Hash->Type==EJson::Null);
    };
    CheckMechanicalIdentity(SavedSynergy,TEXT("Saved synergy JSON metadata"));CheckEmpiricalIdentity(SavedSynergy,TEXT("Saved synergy JSON metadata"));
    TestEqual(TEXT("Saved synergy JSON identifies its actual selected archive version"),String(SavedSynergy,TEXT("selectedDatasetVersion")),FString(TEXT("historical-fixture")));
    TestEqual(TEXT("Saved synergy JSON identifies its actual selected archive fingerprint"),String(SavedSynergy,TEXT("selectedDatasetFingerprint")),FString(TEXT("archived-fixture-fingerprint")));
    const TArray<TSharedPtr<FJsonValue>>* SavedPairRows=nullptr;
    if(!TestTrue(TEXT("Saved synergy JSON contains pair rows"),SavedSynergy->TryGetArrayField(TEXT("rows"),SavedPairRows)))return false;
    TestEqual(TEXT("Saved synergy JSON retains only captured-roster pairs"),SavedPairRows->Num(),PairRows.Num());
    bool FoundSavedPair=false;
    for(auto Value:*SavedPairRows)
    {
        auto Saved=Value->AsObject();CheckMechanicalIdentity(Saved,TEXT("Saved synergy JSON row"));CheckEmpiricalIdentity(Saved,TEXT("Saved synergy JSON row"));
        TestFalse(TEXT("Saved empirical JSON cannot manufacture a later roster member"),String(Saved,TEXT("id")).Contains(TEXT("vampire_bats")));
        if(String(Saved,TEXT("id"))==TEXT("ember_archer+ironclad"))
        {
            FoundSavedPair=true;
            TestEqual(TEXT("Saved JSON preserves the captured pair name including quotes and comma"),String(Saved,TEXT("name")),Pair?String(*Pair,TEXT("name")):FString());
            TestEqual(TEXT("Saved JSON preserves recorded pair sample count"),Number(Saved,TEXT("cleanN")),4.0);
            TestEqual(TEXT("Saved JSON preserves recorded pair outcome rate"),Number(Saved,TEXT("rawWinRate")),75.0);
        }
    }
    TestTrue(TEXT("Saved JSON includes the actually observed archived pair"),FoundSavedPair);
    CheckMechanicalIdentity(SavedMatchups,TEXT("Saved matchup JSON metadata"));
    auto NoEmpiricalClaims=[&](const TSharedPtr<FJsonObject>& Value)
    {for(const auto& Field:Value->Values)if(FString(*Field.Key).StartsWith(TEXT("empirical")))return false;return true;};
    TestTrue(TEXT("Saved mechanical-only JSON metadata makes no empirical claims"),NoEmpiricalClaims(SavedMatchups));
    const TArray<TSharedPtr<FJsonValue>>* SavedMatchupRows=nullptr;
    if(!TestTrue(TEXT("Saved matchup JSON contains mechanical rows"),SavedMatchups->TryGetArrayField(TEXT("rows"),SavedMatchupRows)))return false;
    TestEqual(TEXT("Saved mechanical JSON retains the current ordered roster"),SavedMatchupRows->Num(),Matchups.Num());
    for(auto Value:*SavedMatchupRows){auto Saved=Value->AsObject();CheckMechanicalIdentity(Saved,TEXT("Saved matchup JSON row"));TestTrue(TEXT("Saved mechanical-only JSON row makes no empirical claims"),NoEmpiricalClaims(Saved));}
    auto CheckCSV=[&](const FString& Path,bool Empirical,int32 ExpectedRows)
    {
        FString Saved;
        if(!TestTrue(TEXT("Production CSV reads from disk: ")+FPaths::GetCleanFilename(Path),FFileHelper::LoadFileToString(Saved,*Path)))return;
        // Decode actual quoted CSV through Unreal's independent CSV parser,
        // including a captured name containing both a comma and double quotes.
        FCsvParser Parser(Saved);const auto& CSVRows=Parser.GetRows();
        if(!TestTrue(TEXT("Saved CSV includes a header and data"),CSVRows.Num()>1))return;
        TestEqual(TEXT("Saved CSV row count"),CSVRows.Num()-1,ExpectedRows);
        TMap<FString,int32> Header;for(int32 I=0;I<CSVRows[0].Num();++I)Header.Add(FString(CSVRows[0][I]),I);
        for(const TCHAR* Key:{TEXT("id"),TEXT("name"),TEXT("mechanicalSource"),TEXT("mechanicalVersion"),TEXT("mechanicalBalanceHash")})TestTrue(FString(TEXT("Saved CSV required column: "))+Key,Header.Contains(Key));
        auto Cell=[&](const TArray<const TCHAR*>& Record,const TCHAR* Key)
        {const int32* Index=Header.Find(Key);return Index&&Record.IsValidIndex(*Index)?FString(Record[*Index]):FString();};
        if(Empirical)
        {
            for(const TCHAR* Key:{TEXT("cleanN"),TEXT("rawWinRate"),TEXT("empiricalDatasetVersion"),TEXT("empiricalFingerprint"),TEXT("empiricalBalanceHash")})TestTrue(FString(TEXT("Saved empirical CSV required column: "))+Key,Header.Contains(Key));
        }
        else for(const auto& Field:Header)TestFalse(TEXT("Mechanical-only CSV header makes no empirical claims"),Field.Key.StartsWith(TEXT("empirical")));
        bool FoundCSVPair=false;
        for(int32 I=1;I<CSVRows.Num();++I)
        {
            const auto& Record=CSVRows[I];TestEqual(TEXT("Every saved CSV record has the complete column count"),Record.Num(),CSVRows[0].Num());
            TestEqual(TEXT("CSV mechanics source is current canonical definitions"),Cell(Record,TEXT("mechanicalSource")),FString(TEXT("current canonical definitions")));
            TestEqual(TEXT("CSV mechanics version is current canonical version"),Cell(Record,TEXT("mechanicalVersion")),FS(rift::CardManifestVersion()));
            TestEqual(TEXT("CSV mechanics hash is current canonical hash"),Cell(Record,TEXT("mechanicalBalanceHash")),LiveHash);
            if(!Empirical)continue;
            TestEqual(TEXT("CSV empirical version remains the selected archive"),Cell(Record,TEXT("empiricalDatasetVersion")),FString(TEXT("historical-fixture")));
            TestEqual(TEXT("CSV empirical fingerprint remains the selected archive"),Cell(Record,TEXT("empiricalFingerprint")),FString(TEXT("archived-fixture-fingerprint")));
            TestTrue(TEXT("CSV unavailable historical hash remains empty"),Cell(Record,TEXT("empiricalBalanceHash")).IsEmpty());
            TestFalse(TEXT("Saved empirical CSV cannot manufacture a later roster member"),Cell(Record,TEXT("id")).Contains(TEXT("vampire_bats")));
            if(Cell(Record,TEXT("id"))==TEXT("ember_archer+ironclad"))
            {
                FoundCSVPair=true;
                TestEqual(TEXT("CSV quoting preserves the captured pair name"),Cell(Record,TEXT("name")),Pair?String(*Pair,TEXT("name")):FString());
                TestEqual(TEXT("CSV preserves recorded pair sample count"),FCString::Atod(*Cell(Record,TEXT("cleanN"))),4.0);
                TestEqual(TEXT("CSV preserves recorded pair outcome rate"),FCString::Atod(*Cell(Record,TEXT("rawWinRate"))),75.0);
            }
        }
        if(Empirical)TestTrue(TEXT("Saved CSV contains the observed archived pair"),FoundCSVPair);
    };
    CheckCSV(SynergyCSV,true,PairRows.Num());CheckCSV(MatchupsCSV,false,Matchups.Num());
    FString SavedInput;
    TestTrue(TEXT("Original synthetic archived input still reads"),FFileHelper::LoadFileToString(SavedInput,*ArchivedInput));
    TestEqual(TEXT("Production exports leave the saved archived input unchanged"),SavedInput,Before);
    TestEqual(TEXT("Reading and filtering historical data preserves its complete JSON tree"),Text(Archived),Before);
    TestEqual(TEXT("Historical tables do not alter canonical mechanics"),FS(rift::CardManifestHash()),LiveHash);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftMetaWorkerPauseTest,"Rift.Meta.WorkerPauseAndRecovery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftMetaWorkerPauseTest::RunTest(const FString& Parameters)
{
    // This unregistered subsystem only operates on private in-memory fixtures. It never initializes a profile or persists data.
    auto* Instance=NewObject<UGameInstance>();auto* Meta=NewObject<URiftMetaSimulationSubsystem>(Instance);Meta->Data=NewDataset();Meta->Rate=0;Meta->SetBattleActive(true);
    const double Began=FPlatformTime::Seconds();Meta->Start(4);FPlatformProcess::SleepNoStats(.08f);TestEqual(TEXT("Live battle prevents worker progress"),Meta->Worker->Completed.Load(),0);
    Meta->Pause(true);Meta->SetBattleActive(false);FPlatformProcess::SleepNoStats(.08f);TestEqual(TEXT("Explicit pause also prevents worker progress"),Meta->Worker->Completed.Load(),0);Meta->Pause(false);
    const double Deadline=FPlatformTime::Seconds()+30;while(!Meta->Future.IsReady()&&FPlatformTime::Seconds()<Deadline)FPlatformProcess::SleepNoStats(.01f);
    TestTrue(TEXT("Finite batch terminates after resuming"),Meta->Future.IsReady());Meta->Stop();const double Wall=FPlatformTime::Seconds()-Began;
    TestEqual(TEXT("Four real matches harvested after pause"),Number(Meta->Data,TEXT("games")),4.0);TestEqual(TEXT("All four matches receive economy validation"),Number(Meta->Data,TEXT("economyChecks")),4.0);TestEqual(TEXT("No invalid native economies observed"),Number(Meta->Data,TEXT("economyInvalid")),0.0);TestTrue(TEXT("Reported active simulation time excludes both pause waits"),Number(Meta->Data,TEXT("simulationWallSeconds"))<Wall-.12);
    TestTrue(TEXT("Completed batch can start another finite batch"),Meta->Future.IsValid()==false);Meta->Start(1);const double SecondDeadline=FPlatformTime::Seconds()+15;while(!Meta->Future.IsReady()&&FPlatformTime::Seconds()<SecondDeadline)FPlatformProcess::SleepNoStats(.01f);Meta->Stop();TestEqual(TEXT("Additional finite batch preserves collected observations"),Number(Meta->Data,TEXT("games")),5.0);
    Meta->bDirty=false;return !HasAnyErrors();
}
#endif
