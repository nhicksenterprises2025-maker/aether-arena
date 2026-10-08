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
#include "HAL/PlatformProcess.h"
#include "Engine/GameInstance.h"
#include "Serialization/JsonSerializer.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
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
        if(!Dataset||Number(Dataset,TEXT("schemaVersion"),-1)!=1||String(Dataset,TEXT("fingerprint"))!=URiftMetaSimulationSubsystem::Fingerprint()||String(Dataset,TEXT("model"))!=TEXT("rift-native-1")||String(Dataset,TEXT("deckPolicy"))!=TEXT("native-observed-2")||Number(Dataset,TEXT("telemetryRevision"),-1)!=3)return false;
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
        O->SetStringField(TEXT("version"),TEXT("1.0.0"));O->SetStringField(TEXT("model"),TEXT("rift-native-1"));O->SetStringField(TEXT("deckPolicy"),TEXT("native-observed-2"));O->SetStringField(TEXT("fingerprint"),URiftMetaSimulationSubsystem::Fingerprint());
        O->SetNumberField(TEXT("games"),0);O->SetNumberField(TEXT("invalid"),0);O->SetNumberField(TEXT("seed"),151515);O->SetNumberField(TEXT("simulationWallSeconds"),0);O->SetNumberField(TEXT("economyChecks"),0);O->SetNumberField(TEXT("economyInvalid"),0);O->SetNumberField(TEXT("maximumEconomyResidual"),0);O->SetNumberField(TEXT("telemetryRevision"),3);
        O->SetObjectField(TEXT("buckets"),MakeShared<FJsonObject>());O->SetArrayField(TEXT("checkpoints"),{});
        TArray<TSharedPtr<FJsonValue>> CardSnapshot;
        for(const auto& Card:rift::Cards())
        {
            auto Definition=MakeShared<FJsonObject>();Definition->SetStringField(TEXT("id"),FS(Card.id));Definition->SetStringField(TEXT("name"),FS(Card.name));Definition->SetStringField(TEXT("category"),Card.spell?TEXT("Spell"):Card.building?TEXT("Building"):TEXT("Troop"));
#define SNAPSHOT_FIELD(Key) Definition->SetNumberField(TEXT(#Key),double(Card.Key))
            SNAPSHOT_FIELD(cost);SNAPSHOT_FIELD(count);SNAPSHOT_FIELD(hp);SNAPSHOT_FIELD(damage);SNAPSHOT_FIELD(attackInterval);SNAPSHOT_FIELD(moveSpeed);SNAPSHOT_FIELD(range);SNAPSHOT_FIELD(scale);SNAPSHOT_FIELD(projectileSpeed);SNAPSHOT_FIELD(splash);SNAPSHOT_FIELD(lifetime);SNAPSHOT_FIELD(footprint);SNAPSHOT_FIELD(towerDamage);SNAPSHOT_FIELD(spellRadius);SNAPSHOT_FIELD(chargeDamage);SNAPSHOT_FIELD(slowPct);SNAPSHOT_FIELD(slowDuration);SNAPSHOT_FIELD(auraDamage);SNAPSHOT_FIELD(auraInterval);SNAPSHOT_FIELD(auraRadius);SNAPSHOT_FIELD(stunDuration);SNAPSHOT_FIELD(dotDamage);SNAPSHOT_FIELD(dotDuration);SNAPSHOT_FIELD(dotInterval);SNAPSHOT_FIELD(rounds);
#undef SNAPSHOT_FIELD
            Definition->SetBoolField(TEXT("flying"),Card.flying);Definition->SetBoolField(TEXT("canHitAir"),Card.canHitAir);Definition->SetBoolField(TEXT("structuresOnly"),Card.structuresOnly);Definition->SetBoolField(TEXT("spell"),Card.spell);Definition->SetBoolField(TEXT("building"),Card.building);CardSnapshot.Add(MakeShared<FJsonValueObject>(Definition));
        }
        O->SetArrayField(TEXT("cardSnapshot"),CardSnapshot);auto Rules=MakeShared<FJsonObject>();
        Rules->SetNumberField(TEXT("arenaWidth"),28);Rules->SetNumberField(TEXT("arenaHeight"),42);Rules->SetNumberField(TEXT("riverHalfWidth"),1.65);Rules->SetNumberField(TEXT("bridgeCenterX"),7.2);Rules->SetNumberField(TEXT("bridgeWidth"),4.2);Rules->SetNumberField(TEXT("frontSight"),8);Rules->SetNumberField(TEXT("rearSight"),5);Rules->SetNumberField(TEXT("regulationSeconds"),180);Rules->SetNumberField(TEXT("overtimeSeconds"),120);Rules->SetNumberField(TEXT("aetherInterval"),2.8);Rules->SetNumberField(TEXT("doubleAetherAt"),120);Rules->SetNumberField(TEXT("tripleAetherAt"),240);Rules->SetNumberField(TEXT("tiebreakerDrainPerSecond"),180);Rules->SetNumberField(TEXT("openingAether"),5);Rules->SetNumberField(TEXT("maximumAether"),10);Rules->SetStringField(TEXT("coreActivation"),TEXT("friendly Guard Tower destroyed"));Rules->SetStringField(TEXT("targetHardLock"),TEXT("at Crown Tower attack range"));Rules->SetStringField(TEXT("navigation"),TEXT("card-aware ground grid A-star with bridges; flying ignores obstacles"));O->SetObjectField(TEXT("rulesSnapshot"),Rules);
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
            R->SetStringField(TEXT("id"),Id);R->SetStringField(TEXT("name"),FS(C.name));R->SetStringField(TEXT("category"),C.spell?TEXT("Spell"):C.building?TEXT("Building"):TEXT("Troop"));R->SetNumberField(TEXT("cost"),C.cost);R->SetBoolField(TEXT("flying"),C.flying);R->SetBoolField(TEXT("winCondition"),C.structuresOnly);R->SetBoolField(TEXT("swarm"),C.count>1);
            R->SetNumberField(TEXT("pickRate"),100*Ratio(Ap,Number(Bucket,TEXT("n"))));R->SetNumberField(TEXT("adjustedWinRate"),100*(Score+12)/(N+24));if(N>0)R->SetNumberField(TEXT("rawWinRate"),100*Score/N);else R->SetField(TEXT("rawWinRate"),MakeShared<FJsonValueNull>());
            R->SetNumberField(TEXT("ciLow"),CI.Key);R->SetNumberField(TEXT("ciHigh"),CI.Value);R->SetNumberField(TEXT("cleanN"),N);R->SetNumberField(TEXT("appearances"),Ap);R->SetNumberField(TEXT("mirrorExclusions"),Number(S,TEXT("mirror")));
            static const TPair<const TCHAR*,const TCHAR*> PerGame[]={
                {TEXT("usesPerGame"),TEXT("plays")},{TEXT("aetherPerGame"),TEXT("spent")},{TEXT("troopDamagePerGame"),TEXT("troopDamage")},{TEXT("towerDamagePerGame"),TEXT("towerDamage")},{TEXT("buildingDamagePerGame"),TEXT("buildingDamage")},{TEXT("damageTakenPerGame"),TEXT("damageTaken")},{TEXT("killsPerGame"),TEXT("kills")},{TEXT("deathsPerGame"),TEXT("deaths")},{TEXT("crownContribution"),TEXT("crownContribution")},{TEXT("damagePreventedPerGame"),TEXT("prevented")},{TEXT("pullsPerGame"),TEXT("pulls")},{TEXT("auraDamagePerGame"),TEXT("auraDamage")},{TEXT("unitsStunnedPerGame"),TEXT("stunned")},{TEXT("initialDamagePerGame"),TEXT("initialDamage")},{TEXT("dotDamagePerGame"),TEXT("dotDamage")}};
            for(const auto& P:PerGame)SetRatio(R,P.Key,Number(S,P.Value),Ap);
            SetRatio(R,TEXT("averageLifetime"),Number(S,TEXT("lifetime")),Spawns);SetRatio(R,TEXT("averagePlacementX"),Number(S,TEXT("placementX")),Plays);SetRatio(R,TEXT("averagePlacementZ"),Number(S,TEXT("placementZ")),Plays);SetRatio(R,TEXT("averageKillValue"),Number(S,TEXT("killValue")),Number(S,TEXT("kills")));SetRatio(R,TEXT("damagePerAether"),Damage,Spent);SetRatio(R,TEXT("towerDamagePerAether"),Number(S,TEXT("towerDamage")),Spent);SetRatio(R,TEXT("survivalRate"),Number(S,TEXT("surviving")),Spawns,100);SetRatio(R,TEXT("openingHandPlayRate"),Number(S,TEXT("openingPlays")),Number(S,TEXT("openingEligible")),100);SetRatio(R,TEXT("firstPlayRate"),Number(S,TEXT("firstPlays")),Ap,100);SetRatio(R,TEXT("otPlayRate"),Number(S,TEXT("overtimePlays")),Plays,100);SetRatio(R,TEXT("connectionRate"),Number(S,TEXT("connected")),Plays,100);SetRatio(R,TEXT("lifetimeUtilization"),Number(S,TEXT("buildingLifetime")),Number(S,TEXT("buildingCapacity")),100);SetRatio(R,TEXT("targetsPerCast"),Number(S,TEXT("targets")),Plays);SetRatio(R,TEXT("spellAetherValuePerCast"),Number(S,TEXT("spellValue")),Plays);SetRatio(R,TEXT("overkillPerCast"),Number(S,TEXT("overkill")),Plays);SetRatio(R,TEXT("damagePerCast"),Damage,Plays);SetRatio(R,TEXT("slowUptime"),Number(S,TEXT("slowTime")),Number(S,TEXT("slowTrackedSeconds")),100);SetRatio(R,TEXT("stunUptime"),Number(S,TEXT("stunTime")),Number(S,TEXT("stunTrackedSeconds")),100);SetRatio(R,TEXT("zoneOccupancy"),Number(S,TEXT("zoneOccupancy")),Number(S,TEXT("zoneSeconds")));SetRatio(R,TEXT("dotTicksPerCast"),Number(S,TEXT("dotTicks")),Plays);Rows.Add(R);
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
                    RF(spent);RF(troopDamage);RF(towerDamage);RF(buildingDamage);RF(damageTaken);RF(kills);RF(deaths);RF(killValue);RF(lifetime);RF(slowTime);RF(stunTime);RF(initialDamage);RF(dotDamage);RF(auraDamage);RF(overkill);RF(prevented);RF(placementX);RF(placementZ);RF(spellValue);RF(zoneOccupancy);RF(zoneSeconds);RF(crownContribution);RF(plays);RF(spawns);RF(surviving);RF(pulls);RF(connected);RF(targets);RF(openingPlays);RF(firstPlays);RF(overtimePlays);RF(stunned);RF(dotTicks);RF(openingEligible);RF(buildingLifetime);RF(buildingCapacity);RF(slowTrackedSeconds);RF(stunTrackedSeconds);
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
        for(auto Row:Rows)if(auto Definition=Captured.FindRef(String(Row,TEXT("id"))))for(const TCHAR* Key:{TEXT("name"),TEXT("cost"),TEXT("category"),TEXT("flying")})if(auto Field=Definition->TryGetField(Key))Row->SetField(Key,Field);
    }
}

FString URiftMetaSimulationSubsystem::Fingerprint()
{
    FString Canonical=TEXT("rift-native-1|native-observed-2|ai-v15-port-2|nav-grid-a-star-1|telemetry-3|arena28x42|river1.65|bridges7.2,4.2|sight8,5|phase180,120|aether2.8,120,240|drain180|coreGuardOnly|hardlockAtRange|pocket2,13.2,2.25,9.25");
    for(const auto& C:rift::Cards())
    {
        Canonical+=FS(C.id);
#define RF(K) Canonical+=FString::Printf(TEXT("|" #K "=%.9g"),double(C.K))
        RF(cost);RF(count);RF(hp);RF(damage);RF(attackInterval);RF(moveSpeed);RF(range);RF(scale);RF(projectileSpeed);RF(splash);RF(lifetime);RF(footprint);RF(towerDamage);RF(spellRadius);RF(chargeDamage);RF(slowPct);RF(slowDuration);RF(auraDamage);RF(auraInterval);RF(auraRadius);RF(stunDuration);RF(dotDamage);RF(dotDuration);RF(dotInterval);RF(rounds);RF(flying);RF(canHitAir);RF(structuresOnly);RF(spell);RF(building);
#undef RF
    }return FMD5::HashAnsiString(*Canonical);
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
{FString State=bBattle?TEXT("Paused during battle"):bPaused||!Worker?TEXT("Paused"):Future.IsReady()?TEXT("Batch complete"):TEXT("Simulating");return FString::Printf(TEXT("%s · %.0f matches · %.0f invalid · %.1fs active simulation time · %s"),*State,Number(Data,TEXT("games")),Number(Data,TEXT("invalid")),Number(Data,TEXT("simulationWallSeconds")),*String(Data,TEXT("fingerprint")).Left(8));}
TArray<FString> URiftMetaSimulationSubsystem::DatasetNames()const{return GetGameInstance()->GetSubsystem<URiftProfileSubsystem>()->MetaFiles;}
bool URiftMetaSimulationSubsystem::SelectDataset(const FString& Name)
{if(!DatasetNames().Contains(Name))return false;Stop();Persist();FString S;if(!FFileHelper::LoadFileToString(S,*FPaths::Combine(URiftProfileSubsystem::SaveRoot(),TEXT("Meta"),FPaths::GetCleanFilename(Name))))return false;auto O=Parse(S);if(!O||Number(O,TEXT("schemaVersion"))!=1)return false;Data=O;CurrentFile=FPaths::GetCleanFilename(Name);bDirty=false;bPaused=true;return true;}
TSharedPtr<FJsonObject> URiftMetaSimulationSubsystem::Bucket(const FString& Style,const FString& Archetype)const{return ReadObject(ReadObject(Data,TEXT("buckets")),Style+TEXT("|")+Archetype);}
TArray<TSharedPtr<FJsonObject>> URiftMetaSimulationSubsystem::CardRows(const FString& Style,const FString& Archetype)const{auto Rows=RowsFor(Bucket(Style,Archetype));ApplyCapturedIdentity(Rows,Data);return Rows;}
TArray<TSharedPtr<FJsonObject>> URiftMetaSimulationSubsystem::SynergyRows(const FString& Style,const FString& Archetype)const
{
    auto B=Bucket(Style,Archetype);auto Pairs=ReadObject(B,TEXT("pairs"));auto Cards=CardRows(Style,Archetype);TMap<FString,double> WR;for(auto C:Cards)WR.Add(String(C,TEXT("id")),Number(C,TEXT("adjustedWinRate")));
    TArray<TSharedPtr<FJsonObject>> Rows;const auto& Roster=rift::Cards();for(size_t A=0;A<Roster.size();++A)for(size_t BIndex=A+1;BIndex<Roster.size();++BIndex)
    {
        const auto& CA=Roster[A];const auto& CB=Roster[BIndex];FString IA=FS(CA.id),IB=FS(CB.id),Key=IA<IB?IA+TEXT("+")+IB:IB+TEXT("+")+IA;auto S=ReadObject(Pairs,Key);double N=Number(S,TEXT("n")),Score=Number(S,TEXT("score")),Base=(WR[IA]+WR[IB])/2,Raw=N?100*Score/N:Base;auto CI=Wilson(Score,N);auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("id"),Key);R->SetStringField(TEXT("name"),FS(CA.name)+TEXT(" + ")+FS(CB.name));R->SetNumberField(TEXT("cleanN"),N);R->SetNumberField(TEXT("baseline"),Base);R->SetNumberField(TEXT("adjustedWinRate"),(100*Score+48*Base)/(N+48));R->SetNumberField(TEXT("delta"),N?(Raw-Base)*N/(N+48):0);if(N)R->SetNumberField(TEXT("rawWinRate"),Raw);else R->SetField(TEXT("rawWinRate"),MakeShared<FJsonValueNull>());R->SetNumberField(TEXT("rawDelta"),N?Raw-Base:0);R->SetNumberField(TEXT("ciLow"),CI.Key);R->SetNumberField(TEXT("ciHigh"),CI.Value);R->SetNumberField(TEXT("mechanicalSynergy"),rift::PairSynergy(CA,CB));Rows.Add(R);
    }return Rows;
}
TArray<TSharedPtr<FJsonObject>> URiftMetaSimulationSubsystem::StyleRows(const FString& Archetype)const
{TArray<TSharedPtr<FJsonObject>> Rows;const double Total=Number(Bucket(TEXT("all"),Archetype),TEXT("n"));for(const TCHAR* Style:{TEXT("beatdown"),TEXT("aggro"),TEXT("control"),TEXT("cycle"),TEXT("split"),TEXT("spell_cycle"),TEXT("counter")})Rows.Add(SummaryFor(Bucket(Style,Archetype),Style,Total,true));return Rows;}
TArray<TSharedPtr<FJsonObject>> URiftMetaSimulationSubsystem::ArchetypeRows(const FString& Style)const
{TArray<TSharedPtr<FJsonObject>> Rows;auto Buckets=ReadObject(Data,TEXT("buckets"));const double Total=Number(Bucket(Style,TEXT("all")),TEXT("n"));const FString Prefix=Style+TEXT("|");for(const auto& Pair:Buckets->Values){FString Key(*Pair.Key);if(Key.StartsWith(Prefix)&&Key!=Prefix+TEXT("all")&&Pair.Value->Type==EJson::Object)Rows.Add(SummaryFor(Pair.Value->AsObject(),Key.Mid(Prefix.Len()),Total,false));}return Rows;}
TArray<TSharedPtr<FJsonObject>> URiftMetaSimulationSubsystem::MatchupRows()const
{TArray<TSharedPtr<FJsonObject>> R;for(const auto& A:rift::Cards())for(const auto& B:rift::Cards()){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("id"),FS(A.id)+TEXT("/")+FS(B.id));O->SetStringField(TEXT("name"),FS(A.name)+TEXT(" vs ")+FS(B.name));O->SetNumberField(TEXT("edge"),rift::CounterScore(A,B)-rift::CounterScore(B,A));O->SetStringField(TEXT("reason"),FS(rift::CounterReason(A,B)));R.Add(O);}return R;}
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
    else{auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("model"),String(Data,TEXT("model")));O->SetStringField(TEXT("fingerprint"),String(Data,TEXT("fingerprint")));O->SetStringField(TEXT("subject"),Subject);O->SetStringField(TEXT("styleFilter"),Style);O->SetStringField(TEXT("archetypeFilter"),Archetype);TArray<TSharedPtr<FJsonValue>> A;for(auto R:Rows)A.Add(MakeShared<FJsonValueObject>(R));O->SetArrayField(TEXT("rows"),A);Output=Text(O);}
    return URiftProfileSubsystem::AtomicWrite(Filename,Output,LastError);
}

#if WITH_DEV_AUTOMATION_TESTS
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
    const auto& Captured=Dataset->GetArrayField(TEXT("cardSnapshot"));TestEqual(TEXT("All fourteen immutable card definitions captured"),Captured.Num(),14);TestTrue(TEXT("Rule snapshot preserved"),Dataset->HasField(TEXT("rulesSnapshot")));TestEqual(TEXT("Counter personality receives its own observation"),Number(ReadObject(ReadObject(Dataset,TEXT("buckets")),TEXT("counter|all")),TEXT("n")),1.0);
    auto Broken=State;Broken.spent[0]+=1;Residual=0;TestFalse(TEXT("Extra unaccounted spend fails validation"),EconomyValid(Broken,Residual));Broken=State;Broken.aether[0]=11;Residual=0;TestFalse(TEXT("Over-cap banks fail validation"),EconomyValid(Broken,Residual));
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
