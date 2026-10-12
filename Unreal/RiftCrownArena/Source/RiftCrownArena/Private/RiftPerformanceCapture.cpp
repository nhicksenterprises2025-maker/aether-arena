#include "RiftPerformanceCapture.h"
#include "RiftDiagnostics.h"
#include "RiftGameMode.h"
#include "RiftUIWidget.h"
#include "RiftMatchSubsystem.h"
#include "RiftMetaSimulationSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "RiftAssetLibrary.h"
#include "Presentation/RiftArenaPresentation.h"
#include "Presentation/RiftUnitVisual.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "UnrealClient.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "DynamicRHI.h"
#include "RenderTimer.h"
#include "RHI.h"
#include "HAL/PlatformMemory.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Serialization/JsonSerializer.h"
#include <limits>
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#endif

namespace
{
constexpr double ExpansionWaveCadenceSeconds=5.;
TSharedPtr<FJsonObject> Summary(TArray<double> Values)
{
    auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("samples"),Values.Num());
    if(Values.IsEmpty())return O;
    Values.Sort();double Sum=0;for(double Value:Values)Sum+=Value;
    O->SetNumberField(TEXT("meanMs"),Sum/Values.Num());
    O->SetNumberField(TEXT("p95Ms"),Values[FMath::Clamp(FMath::CeilToInt(Values.Num()*.95)-1,0,Values.Num()-1)]);
    O->SetNumberField(TEXT("p99Ms"),Values[FMath::Clamp(FMath::CeilToInt(Values.Num()*.99)-1,0,Values.Num()-1)]);
    O->SetNumberField(TEXT("maxMs"),Values.Last());return O;
}
bool DeathCleanupLate(double Age,double ExpectedSeconds,double FrameTolerance)
{return Age>ExpectedSeconds+FrameTolerance+1e-7;}
bool DeathCleanupComplete(int32 Retired,int32 Late,int32 Overdue)
{return Retired>0&&Late==0&&Overdue==0;}
rift::MatchOptions ExpansionOptions(uint32 Seed)
{
    rift::MatchOptions Options;Options.seed=Seed;Options.aiEnabled={false,false};
    for(auto& Deck:Options.decks)Deck={"pulverizer","stampede","mini_stampede","bone_warden","aether_reaper","iron_giant","shadow_surge","nova_flask"};
    return Options;
}
rift::Vec2 ExpansionPosition(const std::string& Id,int32 Lane,double Sign)
{
    const double LaneX=rift::arena::BridgeCenterX;
    if(Id=="pulverizer")return {Lane*LaneX,Sign*3.5};
    if(Id=="stampede")return {Lane*LaneX,Sign*6.5};
    if(Id=="mini_stampede")return {Lane*LaneX,Sign*4.5};
    if(Id=="bone_warden")return {Lane*(rift::arena::HalfWidth-3.5),Sign*5.5};
    if(Id=="aether_reaper")return {Lane*LaneX,Sign*4.5};
    if(Id=="iron_giant")return {Lane*(LaneX-2.8),Sign*8.};
    if(Id=="shadow_surge")return {Lane*LaneX,Sign*4.5};
    if(Id=="boulderback")return {Lane*(rift::arena::HalfWidth-3.5),Sign*5.5};
    if(Id=="vampire_bats")return {Lane*LaneX,Sign*4.5};
    if(Id=="storm_raven")return {Lane*LaneX,Sign*7.5};
    if(Id=="archer_tower")return {Lane*(rift::arena::HalfWidth-2.5),Sign*6.5};
    return {Lane*LaneX,0};
}
TArray<TSharedPtr<FJsonValue>> PaidExpansionWave(rift::Match& Sim,int32 MatchIndex,int32 WaveIndex)
{
    TArray<TSharedPtr<FJsonValue>> Plays;
    const auto Requested=ExpansionOptions(0).decks[0];
    for(auto Team:{rift::Team::Player,rift::Team::Enemy})for(int32 Lane:{-1,1})for(const auto& Id:Requested)
    {
        const auto* Card=rift::FindCard(Id);const int32 Side=int32(Team);const double Sign=Side==0?1.:-1.;
        auto Entry=MakeShared<FJsonObject>();Entry->SetNumberField(TEXT("match"),MatchIndex);Entry->SetNumberField(TEXT("wave"),WaveIndex);
        Entry->SetStringField(TEXT("cardId"),UTF8_TO_TCHAR(Id.c_str()));Entry->SetStringField(TEXT("team"),UTF8_TO_TCHAR(rift::TeamName(Team).c_str()));
        Entry->SetNumberField(TEXT("lane"),Lane);Entry->SetNumberField(TEXT("simulationSeconds"),Sim.State().elapsed);
        int32 HandIndex=-1;for(int32 I=0;I<4;++I)if(Sim.State().hands[Side][I]==Id){HandIndex=I;break;}
        Entry->SetNumberField(TEXT("handIndex"),HandIndex);Entry->SetBoolField(TEXT("accepted"),false);
        if(!Card||HandIndex<0){Entry->SetStringField(TEXT("reason"),TEXT("requested card absent from the real hand"));Plays.Add(MakeShared<FJsonValueObject>(Entry));continue;}
        rift::Vec2 Point=ExpansionPosition(Id,Lane,Sign);
        if(Id=="nova_flask" && WaveIndex>0)
        {
            // The opening wave walks normally. Later real paid spell impacts
            // exercise simultaneous herd defeats using an observed group center.
            std::map<rift::PlayId,std::pair<rift::Vec2,int>> Herds;
            for(const auto& E:Sim.State().entities)if(!E.dead&&E.team!=Team&&E.cardId=="stampede"&&(E.position.x<0?-1:1)==Lane)
            {auto& Herd=Herds[E.playId];Herd.first.x+=E.position.x;Herd.first.z+=E.position.z;++Herd.second;}
            int Largest=0;for(const auto& Pair:Herds)if(Pair.second.second>Largest)
            {Largest=Pair.second.second;Point={Pair.second.first.x/Largest,Pair.second.first.z/Largest};}
            Entry->SetNumberField(TEXT("observedHerdMembersBeforeSpell"),Largest);
        }
        const double BeforeGrant=Sim.State().aether[Side];Sim.SetAether(Team,10);
        Entry->SetNumberField(TEXT("fixtureAetherGrant"),Sim.State().aether[Side]-BeforeGrant);
        const double BeforeSpent=Sim.State().spent[Side];const auto FirstEvent=Sim.Events().empty()?0:Sim.Events().back().sequence;
        bool Accepted=false;
        const rift::Vec2 Offsets[]={{0,0},{3*double(Lane),0},{-3*double(Lane),0},{0,3*Sign},{0,6*Sign},{0,9*Sign},{-3*double(Lane),6*Sign}};
        for(const auto Offset:Offsets)
        {
            const rift::Vec2 Candidate{Point.x+Offset.x,Point.z+Offset.z};
            if(!Card->spell&&Candidate.x*Lane<=0)continue;
            if(Sim.Play(Team,HandIndex,Candidate,"performance_expansion_paid_fixture")){Accepted=true;break;}
            if(Card->spell)break;
        }
        TArray<TSharedPtr<FJsonValue>> Members;uint64 PlayId=0;
        for(const auto& Event:Sim.Events())if(Event.sequence>FirstEvent&&Event.team==Team&&Event.cardId==Id)
        {
            if(Event.type=="card_play")
            {PlayId=Event.playId;Entry->SetNumberField(TEXT("x"),Event.position.x);Entry->SetNumberField(TEXT("z"),Event.position.z);Entry->SetBoolField(TEXT("sandbox"),Event.sandbox);}
            if(Event.type=="entity_spawn")
            {auto Member=MakeShared<FJsonObject>();Member->SetNumberField(TEXT("id"),double(Event.source));Member->SetNumberField(TEXT("x"),Event.position.x);Member->SetNumberField(TEXT("z"),Event.position.z);Members.Add(MakeShared<FJsonValueObject>(Member));}
        }
        Entry->SetNumberField(TEXT("playId"),double(PlayId));Entry->SetBoolField(TEXT("accepted"),Accepted);
        Entry->SetNumberField(TEXT("cost"),Card->cost);Entry->SetNumberField(TEXT("actualSpent"),Sim.State().spent[Side]-BeforeSpent);
        Entry->SetNumberField(TEXT("expectedMembers"),Card->spell?0:Card->count);Entry->SetNumberField(TEXT("actualMembers"),Members.Num());Entry->SetArrayField(TEXT("members"),Members);
        if(!Accepted)Entry->SetStringField(TEXT("reason"),TEXT("all tested placements rejected by real deployment packing"));
        Plays.Add(MakeShared<FJsonValueObject>(Entry));
    }
    return Plays;
}
}
void URiftPerformanceCapture::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if(!FParse::Value(FCommandLine::Get(),TEXT("RiftPerfReport="),Filename))return;
    FString ExplicitSaveRoot;
    if(!FParse::Value(FCommandLine::Get(),TEXT("RiftSaveRoot="),ExplicitSaveRoot))
    {RIFT_LOG(LogRift,Error,TEXT("Performance QA requires an explicit isolated -RiftSaveRoot."));return;}
    FParse::Value(FCommandLine::Get(),TEXT("RiftPerfSeconds="),Duration);
    FParse::Value(FCommandLine::Get(),TEXT("RiftPerfWarmup="),Warmup);
    Duration=FMath::Clamp(Duration,10.,600.);Warmup=FMath::Clamp(Warmup,2.,60.);
    bStress=FParse::Param(FCommandLine::Get(),TEXT("RiftPerfStress"));bWithMeta=FParse::Param(FCommandLine::Get(),TEXT("RiftPerfWithMeta"));
    bExpansionStress=FParse::Param(FCommandLine::Get(),TEXT("RiftPerfExpansionStress"));
    if(bExpansionStress)
    {
        if(FParse::Value(FCommandLine::Get(),TEXT("RiftPerfScreenshotRoot="),ScreenshotRoot))
        {
            FPaths::NormalizeDirectoryName(ScreenshotRoot);
            const FString ReportDirectory=FPaths::GetPath(FPaths::ConvertRelativePathToFull(Filename));
            bool Safe=!ScreenshotRoot.IsEmpty()&&!FPaths::IsRelative(ScreenshotRoot)&&!FPaths::IsRelative(Filename)
                &&FPaths::IsUnderDirectory(ScreenshotRoot,ReportDirectory);
            TArray<FString> LiveRoots;
            const FString Local=FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
            if(!Local.IsEmpty())LiveRoots.Add(FPaths::Combine(Local,TEXT("RiftCrownArena")));
            for(const TCHAR* Key:{TEXT("RIFT_SAVE_ROOT"),TEXT("RIFT_CROWN_SAVE_ROOT")})
            {const FString Root=FPlatformMisc::GetEnvironmentVariable(Key);if(!Root.IsEmpty())LiveRoots.Add(FPaths::ConvertRelativePathToFull(Root));}
            for(const auto& Root:LiveRoots)Safe&=!FPaths::IsSamePath(ScreenshotRoot,Root)&&!FPaths::IsUnderDirectory(ScreenshotRoot,Root)&&!FPaths::IsUnderDirectory(Root,ScreenshotRoot);
            if(!Safe||!IFileManager::Get().MakeDirectory(*ScreenshotRoot,true))
            {RIFT_LOG(LogRift,Error,TEXT("Expansion screenshot root must be an isolated absolute subdirectory of this performance run."));return;}
        }
        Collection.InitializeDependency<URiftMatchSubsystem>();
        bStress=true;FParse::Value(FCommandLine::Get(),TEXT("RiftPerfStartSeconds="),FixtureStartSeconds);
        FParse::Value(FCommandLine::Get(),TEXT("RiftPerfSpeed="),FixtureSpeed);FParse::Value(FCommandLine::Get(),TEXT("RiftPerfSeed="),FixtureSeed);
        FixtureStartSeconds=FMath::Clamp(FixtureStartSeconds,0.,299.);
        if(FixtureSpeed!=.25f&&FixtureSpeed!=.5f&&FixtureSpeed!=1.f&&FixtureSpeed!=2.f&&FixtureSpeed!=4.f)FixtureSpeed=1;
        GetWorld()->GetSubsystem<URiftMatchSubsystem>()->OnEvent.AddUObject(this,&URiftPerformanceCapture::ObserveFixtureEvent);
    }
    bEnabled=true;
}
bool URiftPerformanceCapture::IsTickable()const{return bEnabled&&GetWorld()&&GetWorld()->IsGameWorld();}
TStatId URiftPerformanceCapture::GetStatId()const{RETURN_QUICK_DECLARE_CYCLE_STAT(URiftPerformanceCapture,STATGROUP_Tickables);}
void URiftPerformanceCapture::SpawnWave()
{
    auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();auto* Sim=M->Simulation();if(!Sim)return;
    if(bExpansionStress){M->SampleBeforeMutation();PaidDeployments.Append(PaidExpansionWave(*Sim,FixtureMatches,MatchWaveIndex++));++FixtureWaves;M->FlushEvents();return;}
    for(auto Team:{rift::Team::Player,rift::Team::Enemy})for(int32 Lane:{-1,1})
    {
        const double Z=Team==rift::Team::Player?5.:-5.;
        for(const char* Card:{"boulderback","twin_blades","vampire_bats","frost_fang","storm_raven"})
            Sim->Spawn(Team,Card,{Lane*7.+(std::string(Card)=="storm_raven"?2.:0.),Z});
        Sim->Spawn(Team,"archer_tower",{Lane*10.,Z+(Team==rift::Team::Player?4.:-4.)});
    }
    M->FlushEvents();
}
void URiftPerformanceCapture::StartExpansionMatch()
{
    auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();
    ObserveDeathCleanup();
    for(const auto& Entry:DeathVisuals)Entry.Record->SetStringField(TEXT("status"),Entry.Late?TEXT("late_interrupted_by_match_reset"):TEXT("interrupted_by_match_reset_before_tolerance_deadline"));
    RestartDeathVisuals+=DeathVisuals.Num();DeathVisuals.Reset();LastDeathTime=-1;DeathBurst=0;
    PendingNotDueDeathVisuals=PendingGraceDeathVisuals=PendingOverdueDeathVisuals=0;
    PreviousCleanupTime=-1;PreviousCleanupFrame=1./60.;CleanupToleranceSeconds=1./30.;
    if(!M->StartPerformanceMatch(ExpansionOptions(FixtureSeed)))
    {FixtureErrors.Add(MakeShared<FJsonValueString>(TEXT("isolated paid fixture startup was rejected")));Finish();return;}
    ++FixtureMatches;MatchWaveIndex=0;
    M->Simulation()->Step(FixtureStartSeconds);M->FlushEvents();M->SetSpeed(FixtureSpeed);
    ResetExpansionWaveClock(M->Simulation()->State().elapsed);SpawnWave();
}
void URiftPerformanceCapture::ResetExpansionWaveClock(double SimulationSeconds)
{LastExpansionWaveSimulationSeconds=SimulationSeconds;}
bool URiftPerformanceCapture::AdvanceExpansionWaveClock(double SimulationSeconds)
{
    if(!FMath::IsFinite(SimulationSeconds)||!FMath::IsFinite(LastExpansionWaveSimulationSeconds)||
        SimulationSeconds+1e-9<LastExpansionWaveSimulationSeconds+ExpansionWaveCadenceSeconds)return false;
    // A slow frame never earns additional waves from elapsed wall time or a
    // catch-up burst. The next interval begins at this actual accepted age.
    LastExpansionWaveSimulationSeconds=SimulationSeconds;return true;
}
void URiftPerformanceCapture::ObserveFixtureEvent(const rift::Event& Event)
{
    if(!bEnabled||!bExpansionStress)return;
    if(Event.type=="card_play"&&!Event.sandbox){++PaidPlayEvents;FixturePaidAether+=Event.amount;}
    if(Event.type=="entity_spawn")++SpawnEvents;
    if(Event.type=="aether_grant")FixtureGrantAether+=Event.amount;
    if(Event.type=="shockwave_launch")++WaveEvents;
    if(Event.type=="damage"&&Event.damageKind=="shockwave")++WaveHitEvents;
    if(Event.type!="death")return;
    ++DeathEvents;DeathBurst=LastDeathTime==Event.time?DeathBurst+1:1;LastDeathTime=Event.time;MaximumDeathBurst=FMath::Max(MaximumDeathBurst,DeathBurst);
    if(Event.targetKind==rift::EntityKind::Core||Event.targetKind==rift::EntityKind::Guard)return;
    for(TActorIterator<ARiftArenaPresentation> It(GetWorld());It;++It)if(auto* Actor=It->Visual(Event.target))
    {
        const auto* Card=GetWorld()->GetGameInstance()->GetSubsystem<URiftAssetCatalogSubsystem>()->Card(Actor->PresentationAssetId());
        const bool Authored=Card&&Card->DeathCleanupSeconds>0;
        FDeathVisual Entry;Entry.Actor=Actor;Entry.Died=Event.time;Entry.CleanupSeconds=Authored?FMath::Clamp(double(Card->DeathCleanupSeconds),.1,1.05):1.05;
        Entry.Record=MakeShared<FJsonObject>();Entry.Record->SetNumberField(TEXT("match"),FixtureMatches);Entry.Record->SetNumberField(TEXT("entityId"),double(Event.target));
        Entry.Record->SetStringField(TEXT("cardId"),Actor->PresentationAssetId());Entry.Record->SetNumberField(TEXT("deathSimulationSeconds"),Event.time);
        Entry.Record->SetNumberField(TEXT("expectedCleanupSeconds"),Entry.CleanupSeconds);Entry.Record->SetNumberField(TEXT("expectedDeadlineSimulationSeconds"),Event.time+Entry.CleanupSeconds);
        Entry.Record->SetStringField(TEXT("deadlineSource"),Authored?TEXT("actual imported CardData.DeathCleanupSeconds clamped to visual limits"):TEXT("legacy visual cleanup 1.05 seconds"));
        Entry.Record->SetStringField(TEXT("status"),TEXT("pending_before_deadline"));Entry.Record->SetBoolField(TEXT("late"),false);
        DeathCleanupRecords.Add(MakeShared<FJsonValueObject>(Entry.Record));DeathVisuals.Add(MoveTemp(Entry));++ObservedDeathVisuals;break;
    }
}
void URiftPerformanceCapture::ObserveDeathCleanup()
{
    const auto* State=GetWorld()->GetSubsystem<URiftMatchSubsystem>()->ViewState();if(!State)return;
    if(PreviousCleanupTime<0||State->elapsed>PreviousCleanupTime)
    {
        const double Frame=PreviousCleanupTime>=0?State->elapsed-PreviousCleanupTime:1./60.;
        CleanupToleranceSeconds=FMath::Max(1./60.,Frame)+FMath::Max(1./60.,PreviousCleanupFrame);
        PreviousCleanupTime=State->elapsed;PreviousCleanupFrame=Frame;
    }
    PendingNotDueDeathVisuals=PendingGraceDeathVisuals=PendingOverdueDeathVisuals=0;
    DeathVisuals.RemoveAll([&](FDeathVisual& Entry)
    {
        const double Age=FMath::Max(0.,State->elapsed-Entry.Died);
        Entry.Record->SetNumberField(TEXT("lastObservedSimulationSeconds"),State->elapsed);Entry.Record->SetNumberField(TEXT("lastObservedAgeSeconds"),Age);
        Entry.Record->SetNumberField(TEXT("frameToleranceSeconds"),CleanupToleranceSeconds);
        if(DeathCleanupLate(Age,Entry.CleanupSeconds,CleanupToleranceSeconds)&&!Entry.Late)
        {Entry.Late=true;++LateDeathVisuals;Entry.Record->SetBoolField(TEXT("late"),true);Entry.Record->SetNumberField(TEXT("firstLateObservedSimulationSeconds"),State->elapsed);}
        if(!Entry.Actor.IsValid())
        {
            ++RetiredDeathVisuals;MaximumCleanupSeconds=FMath::Max(MaximumCleanupSeconds,Age);
            Entry.Record->SetNumberField(TEXT("observedRetirementAgeSeconds"),Age);Entry.Record->SetNumberField(TEXT("observedRetirementSimulationSeconds"),State->elapsed);
            Entry.Record->SetStringField(TEXT("status"),Entry.Late?TEXT("retired_late"):TEXT("retired_within_deadline_tolerance"));return true;
        }
        MaximumObservedDeadAge=FMath::Max(MaximumObservedDeadAge,Age);
        if(Entry.Late){++PendingOverdueDeathVisuals;Entry.Record->SetStringField(TEXT("status"),TEXT("live_past_deadline_tolerance"));}
        else if(Age<Entry.CleanupSeconds){++PendingNotDueDeathVisuals;Entry.Record->SetStringField(TEXT("status"),TEXT("pending_before_deadline"));}
        else{++PendingGraceDeathVisuals;Entry.Record->SetStringField(TEXT("status"),TEXT("pending_within_frame_tolerance"));}
        return false;
    });
}
void URiftPerformanceCapture::ObserveFixtureVisuals(double Now)
{
    const auto* State=GetWorld()->GetSubsystem<URiftMatchSubsystem>()->ViewState();if(!State)return;
    const double Began=FPlatformTime::Seconds();
    PeakShockwaves=FMath::Max(PeakShockwaves,int32(State->shockwaves.size()));ObserveDeathCleanup();
    int32 Visuals=0,Dead=0;LastHoofDust=LastFormationDust=LastDeathFlecks=LastSlowRipples=0;
    for(TActorIterator<ARiftUnitVisual> It(GetWorld());It;++It)
    {
        ++Visuals;Dead+=It->IsDead()?1:0;if(!It->PresentationAssetId().IsEmpty())ObservedModelIds.Add(It->PresentationAssetId());
        LastHoofDust+=It->HoofDustCount();LastFormationDust+=It->FormationDustCount();LastDeathFlecks+=It->DeathFleckCount();LastSlowRipples+=It->SlowRippleCount();
        if(It->IsExpired(State->elapsed-CleanupToleranceSeconds)&&It->DeathFleckCount()>0)++ExpiredBodiesWithDeathFlecks;
    }
    PeakObservedVisuals=FMath::Max(PeakObservedVisuals,Visuals);PeakObservedDeadVisuals=FMath::Max(PeakObservedDeadVisuals,Dead);
    PeakHoofDust=FMath::Max(PeakHoofDust,LastHoofDust);PeakFormationDust=FMath::Max(PeakFormationDust,LastFormationDust);
    PeakDeathFlecks=FMath::Max(PeakDeathFlecks,LastDeathFlecks);PeakSlowRipples=FMath::Max(PeakSlowRipples,LastSlowRipples);
    PeakSelectedSurfaceInstances=FMath::Max(PeakSelectedSurfaceInstances,LastHoofDust+LastFormationDust+LastDeathFlecks+LastSlowRipples);
    static constexpr double ScreenshotAges[]={.12,.37,.6,3.,6.,8.,20.,40.};
    if(!ScreenshotRoot.IsEmpty()&&FixtureMatches==1&&NextScreenshot<UE_ARRAY_COUNT(ScreenshotAges)&&
        State->elapsed-FixtureStartSeconds>=ScreenshotAges[NextScreenshot]&&!FScreenshotRequest::IsScreenshotRequested())
    {
        const FString Path=FPaths::Combine(ScreenshotRoot,FString::Printf(TEXT("expansion-%02d.png"),NextScreenshot+1));
        auto Request=MakeShared<FJsonObject>();Request->SetStringField(TEXT("path"),Path);Request->SetNumberField(TEXT("simulationSeconds"),State->elapsed);
        Request->SetStringField(TEXT("timeMeaning"),TEXT("Actual state time at the asynchronous screenshot request; rendered pixels are captured by the subsequent viewport screenshot pass."));
        Request->SetNumberField(TEXT("scheduledAgeSeconds"),ScreenshotAges[NextScreenshot]);
        Request->SetNumberField(TEXT("hoofDustInstances"),LastHoofDust);Request->SetNumberField(TEXT("formationDustInstances"),LastFormationDust);
        Request->SetNumberField(TEXT("deathFleckInstances"),LastDeathFlecks);Request->SetNumberField(TEXT("slowRippleInstances"),LastSlowRipples);
        Request->SetNumberField(TEXT("shockwaves"),State->shockwaves.size());Request->SetNumberField(TEXT("deathEventsSoFar"),DeathEvents);
        TArray<TSharedPtr<FJsonValue>> Hands,Selected;
        for(const auto& Hand:State->hands){TArray<TSharedPtr<FJsonValue>> Cards;for(const auto& Id:Hand)Cards.Add(MakeShared<FJsonValueString>(UTF8_TO_TCHAR(Id.c_str())));Hands.Add(MakeShared<FJsonValueArray>(Cards));}
        Request->SetArrayField(TEXT("hands"),Hands);
        for(TActorIterator<ARiftUnitVisual> It(GetWorld());It;++It)
        {
            const FString Id=It->PresentationAssetId();if(Id.IsEmpty())continue;
            auto Visual=MakeShared<FJsonObject>();Visual->SetNumberField(TEXT("entityId"),double(It->EntityId));Visual->SetStringField(TEXT("modelId"),Id);
            Visual->SetBoolField(TEXT("dead"),It->IsDead());Visual->SetStringField(TEXT("animation"),It->CurrentAnimation().ToString());
            Visual->SetNumberField(TEXT("visualYawDegrees"),It->GetActorRotation().Yaw);
            for(const auto& Body:State->entities)if(Body.id==It->EntityId)
            {Visual->SetNumberField(TEXT("team"),int(Body.team));Visual->SetNumberField(TEXT("facingX"),Body.facing.x);Visual->SetNumberField(TEXT("facingZ"),Body.facing.z);break;}
            Selected.Add(MakeShared<FJsonValueObject>(Visual));
        }
        Request->SetArrayField(TEXT("selectedVisuals"),Selected);
        ScreenshotRequests.Add(MakeShared<FJsonValueObject>(Request));++NextScreenshot;
        FScreenshotRequest::RequestScreenshot(Path,true,false,false,FIntRect(),true);
    }
    if(Now-LastVisualSample<1.){DiagnosticTimes.Add((FPlatformTime::Seconds()-Began)*1000.);return;}LastVisualSample=Now;
    auto Sample=MakeShared<FJsonObject>();Sample->SetNumberField(TEXT("match"),FixtureMatches);Sample->SetNumberField(TEXT("simulationSeconds"),State->elapsed);
    Sample->SetStringField(TEXT("phase"),UTF8_TO_TCHAR(rift::PhaseName(State->phase).c_str()));Sample->SetNumberField(TEXT("unitVisuals"),Visuals);Sample->SetNumberField(TEXT("deadVisuals"),Dead);
    Sample->SetNumberField(TEXT("hoofDustInstances"),LastHoofDust);Sample->SetNumberField(TEXT("formationDustInstances"),LastFormationDust);
    Sample->SetNumberField(TEXT("deathFleckInstances"),LastDeathFlecks);Sample->SetNumberField(TEXT("slowRippleInstances"),LastSlowRipples);
    Sample->SetNumberField(TEXT("selectedSurfaceInstances"),LastHoofDust+LastFormationDust+LastDeathFlecks+LastSlowRipples);
    for(TActorIterator<ARiftArenaPresentation> It(GetWorld());It;++It)
    {
        TSharedPtr<FJsonObject> Diagnostics;
        if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(It->NiagaraDiagnosticsJSON()),Diagnostics)&&Diagnostics)
        {
            FinalVisualState=Diagnostics;double Particles=0;Diagnostics->TryGetNumberField(TEXT("totalParticles"),Particles);
            PeakObservedParticles=FMath::Max(PeakObservedParticles,int32(Particles));Sample->SetNumberField(TEXT("particles"),Particles);
            Sample->SetNumberField(TEXT("shockwaveRings"),It->ShockwaveVisualCount());
            PeakObservedShockwaveRings=FMath::Max(PeakObservedShockwaveRings,It->ShockwaveVisualCount());
        }
        break;
    }
    VisualSamples.Add(MakeShared<FJsonValueObject>(Sample));DiagnosticTimes.Add((FPlatformTime::Seconds()-Began)*1000.);
}
void URiftPerformanceCapture::Tick(float)
{
    const double Now=FPlatformTime::Seconds();auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();
    auto* Meta=GetWorld()->GetGameInstance()->GetSubsystem<URiftMetaSimulationSubsystem>();
    if(!bStarted)
    {
        if(bExpansionStress)StartExpansionMatch();else M->StartMatch(true,!bStress);
        if(!bEnabled)return;
        if(bWithMeta){Meta->SetRate(0);Meta->Start();}
        if(auto Data=Meta->Dataset())Data->TryGetNumberField(TEXT("games"),InitialMetaGames);
        if(bStress&&!bExpansionStress){M->Simulation()->SetAIEnabled(rift::Team::Enemy,false);SpawnWave();}
        if(auto* PC=Cast<ARiftPlayerController>(GetWorld()->GetFirstPlayerController());PC&&PC->Interface)PC->Interface->SetBattleView();
        StartTime=PreviousTime=LastWave=Now;bStarted=true;return;
    }
    if(bStress&&M->ViewState()&&M->ViewState()->phase==rift::Phase::Finished)
    {
        if(bExpansionStress)StartExpansionMatch();else {M->StartMatch(true,false);M->Simulation()->SetAIEnabled(rift::Team::Enemy,false);SpawnWave();}LastWave=Now;
        if(!bEnabled)return;
        if(auto* PC=Cast<ARiftPlayerController>(GetWorld()->GetFirstPlayerController());PC&&PC->Interface)PC->Interface->SetBattleView();
    }
    if(bStress)
    {
        if(bExpansionStress)
        {
            if(const auto* State=M->ViewState();State&&AdvanceExpansionWaveClock(State->elapsed))SpawnWave();
        }
        else if(Now-LastWave>=5.){SpawnWave();LastWave=Now;}
    }
    if(bExpansionStress)ObserveFixtureVisuals(Now);
    if(Now-StartTime>=Warmup)
    {
        const double FrameMs=(Now-PreviousTime)*1000.;Frames.Add(FrameMs);
        if(FrameMs>50. && Hitches.Num()<128)
        {
            auto Hitch=MakeShared<FJsonObject>();Hitch->SetNumberField(TEXT("wallSeconds"),Now-StartTime);
            Hitch->SetNumberField(TEXT("frameMs"),FrameMs);
            if(const auto* State=M->ViewState())
            {Hitch->SetNumberField(TEXT("simulationSeconds"),State->elapsed);Hitch->SetStringField(TEXT("phase"),M->GetPhase());Hitch->SetNumberField(TEXT("entities"),double(State->entities.size()));}
            Hitches.Add(MakeShared<FJsonValueObject>(Hitch));
        }
        if(GGameThreadTime)GameTimes.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
        if(GRenderThreadTime)RenderTimes.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
        const uint32 GPUCycles=RHIGetGPUFrameCycles();if(GPUCycles)GPUTimes.Add(FPlatformTime::ToMilliseconds(GPUCycles));
        if(const auto* S=M->ViewState())
        {PeakEntities=FMath::Max(PeakEntities,int32(S->entities.size()));PeakProjectiles=FMath::Max(PeakProjectiles,int32(S->projectiles.size()));PeakHazards=FMath::Max(PeakHazards,int32(S->hazards.size()));}
    }
    PreviousTime=Now;if(Now-StartTime>=Warmup+Duration)Finish();
}
void URiftPerformanceCapture::Finish()
{
    if(bExpansionStress)ObserveDeathCleanup();
    bEnabled=false;auto Report=MakeShared<FJsonObject>();
    Report->SetNumberField(TEXT("schema"),1);Report->SetStringField(TEXT("build"),LexToString(FApp::GetBuildConfiguration()));
    Report->SetStringField(TEXT("cpu"),FPlatformMisc::GetCPUBrand());Report->SetStringField(TEXT("gpu"),GRHIAdapterName);
    Report->SetBoolField(TEXT("stress"),bStress);Report->SetBoolField(TEXT("metaRequested"),bWithMeta);
    const auto* Settings=GEngine?GEngine->GetGameUserSettings():nullptr;
    Report->SetNumberField(TEXT("observedQualityPreset"),Settings?Settings->GetOverallScalabilityLevel():-1);
    Report->SetNumberField(TEXT("observedFrameRateLimit"),Settings?Settings->GetFrameRateLimit():-1);
    Report->SetBoolField(TEXT("observedVSync"),Settings&&Settings->IsVSyncEnabled());
    Report->SetStringField(TEXT("observedVSyncMeaning"),TEXT("GameUserSettings preference; command-line overrides can change r.VSync."));
    if(const auto* VSync=IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync")))
        Report->SetBoolField(TEXT("renderVSyncCVar"),VSync->GetInt()!=0);
    if(const auto* MaxFPS=IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
        Report->SetNumberField(TEXT("maxFPSCVar"),MaxFPS->GetFloat());
    Report->SetBoolField(TEXT("screenshotCaptureEnabled"),!ScreenshotRoot.IsEmpty());Report->SetArrayField(TEXT("screenshotRequests"),ScreenshotRequests);
    if(!ScreenshotRoot.IsEmpty())Report->SetStringField(TEXT("measurementLimitation"),TEXT("Screenshot capture and readback are enabled; this run supplies visual evidence and its timing must not be compared with screenshot-disabled performance measurements."));
    bool FixturePassed=true;
    if(bExpansionStress)
    {
        auto Fixture=MakeShared<FJsonObject>();Fixture->SetStringField(TEXT("name"),TEXT("expansion-paid-both-bridges-1"));
        Fixture->SetStringField(TEXT("balanceHash"),UTF8_TO_TCHAR(rift::CardManifestHash().c_str()));Fixture->SetStringField(TEXT("manifestVersion"),UTF8_TO_TCHAR(rift::CardManifestVersion().c_str()));
        Fixture->SetNumberField(TEXT("seed"),FixtureSeed);Fixture->SetNumberField(TEXT("startSimulationSeconds"),FixtureStartSeconds);Fixture->SetNumberField(TEXT("simulationSpeed"),FixtureSpeed);
        Fixture->SetNumberField(TEXT("matches"),FixtureMatches);Fixture->SetNumberField(TEXT("waves"),FixtureWaves);
        Fixture->SetNumberField(TEXT("waveCadenceSeconds"),ExpansionWaveCadenceSeconds);
        Fixture->SetStringField(TEXT("waveCadenceTimeDomain"),TEXT("simulation"));
        Fixture->SetStringField(TEXT("waveCadencePolicy"),TEXT("Opening wave at the actual match start age; later waves require five actual simulation seconds since the previous wave. At most one wave per capture tick, with no wall-time catch-up burst. The clock resets on each actual new match."));
        Fixture->SetStringField(TEXT("economy"),TEXT("real legal eight-card decks and paid Match::Play; explicit sandbox aether_grant replenishment is fixture input, not natural generation"));
        Fixture->SetStringField(TEXT("spellStage"),TEXT("opening Nova targets the empty bridge; later paid Novas target an observed enemy herd center to exercise simultaneous defeats"));
        bool Proof=true,OpeningComplete=true;int32 Rejected=0,OpeningRequests=0;TSet<FString> Coverage;
        for(const auto& Value:PaidDeployments)
        {
            const auto Entry=Value->AsObject();const bool Accepted=Entry->GetBoolField(TEXT("accepted"));
            const bool Opening=Entry->GetNumberField(TEXT("match"))==1&&Entry->GetNumberField(TEXT("wave"))==0;
            if(Opening){++OpeningRequests;OpeningComplete&=Accepted;}
            if(!Accepted){++Rejected;continue;}
            const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*Entry->GetStringField(TEXT("cardId"))));
            Proof&=Card&&!Entry->GetBoolField(TEXT("sandbox"))&&FMath::IsNearlyEqual(Entry->GetNumberField(TEXT("actualSpent")),double(Card->cost),1e-7)
                &&Entry->GetNumberField(TEXT("actualMembers"))==double(Card->spell?0:Card->count);
            Coverage.Add(Entry->GetStringField(TEXT("cardId"))+TEXT("|")+Entry->GetStringField(TEXT("team"))+FString::Printf(TEXT("|%.0f"),Entry->GetNumberField(TEXT("lane"))));
        }
        OpeningComplete&=OpeningRequests==32;
        Fixture->SetBoolField(TEXT("paidDeploymentProof"),Proof);Fixture->SetBoolField(TEXT("firstWaveComplete"),OpeningComplete);
        Fixture->SetNumberField(TEXT("rejectedDeploymentRequests"),Rejected);Fixture->SetNumberField(TEXT("observedCardTeamLaneCombinations"),Coverage.Num());
        Fixture->SetArrayField(TEXT("paidDeployments"),PaidDeployments);Fixture->SetNumberField(TEXT("paidCardPlayEvents"),PaidPlayEvents);Fixture->SetNumberField(TEXT("entitySpawnEvents"),SpawnEvents);
        Fixture->SetNumberField(TEXT("paidAether"),FixturePaidAether);Fixture->SetNumberField(TEXT("fixtureGrantedAether"),FixtureGrantAether);
        Fixture->SetNumberField(TEXT("deathEvents"),DeathEvents);Fixture->SetNumberField(TEXT("maximumDeathsAtOneSimulationTimestamp"),MaximumDeathBurst);
        Fixture->SetNumberField(TEXT("shockwaveLaunchEvents"),WaveEvents);Fixture->SetNumberField(TEXT("shockwaveDamageEvents"),WaveHitEvents);Fixture->SetNumberField(TEXT("peakSimulationShockwaves"),PeakShockwaves);
        Fixture->SetNumberField(TEXT("deathVisualsObservedAtEvent"),ObservedDeathVisuals);Fixture->SetNumberField(TEXT("deathVisualsObservedRetired"),RetiredDeathVisuals);
        Fixture->SetNumberField(TEXT("deathVisualsInterruptedByMatchReset"),RestartDeathVisuals);Fixture->SetNumberField(TEXT("pendingObservedDeathVisuals"),DeathVisuals.Num());
        Fixture->SetNumberField(TEXT("lateObservedDeathVisuals"),LateDeathVisuals);Fixture->SetNumberField(TEXT("pendingDeathVisualsBeforeDeadline"),PendingNotDueDeathVisuals);
        Fixture->SetNumberField(TEXT("pendingDeathVisualsWithinFrameTolerance"),PendingGraceDeathVisuals);Fixture->SetNumberField(TEXT("pendingDeathVisualsPastDeadlineTolerance"),PendingOverdueDeathVisuals);
        Fixture->SetBoolField(TEXT("allDueObservedDeathVisualsRetiredOnTime"),DeathCleanupComplete(RetiredDeathVisuals,LateDeathVisuals,PendingOverdueDeathVisuals));
        Fixture->SetStringField(TEXT("cleanupTolerance"),TEXT("two most recent observed simulation-frame advances, each at least one 1/60-second simulation tick; pending deaths before their deadline or within this tolerance are reported separately"));
        Fixture->SetArrayField(TEXT("deathCleanupRecords"),DeathCleanupRecords);
        Fixture->SetNumberField(TEXT("maximumObservedDeadVisualAgeSeconds"),MaximumObservedDeadAge);Fixture->SetNumberField(TEXT("maximumObservedCleanupSeconds"),MaximumCleanupSeconds);
        Fixture->SetStringField(TEXT("visualSampling"),TEXT("unit visuals, selected native surface instances, death retirement and simulation shockwaves are sampled every capture tick including warmup; Niagara particles and shockwave rings at one-second wall intervals; diagnostic overhead is included in frame timing"));
        Fixture->SetNumberField(TEXT("maximumObservedUnitVisuals"),PeakObservedVisuals);Fixture->SetNumberField(TEXT("maximumObservedDeadVisuals"),PeakObservedDeadVisuals);
        Fixture->SetNumberField(TEXT("maximumObservedParticles"),PeakObservedParticles);Fixture->SetNumberField(TEXT("maximumObservedShockwaveRings"),PeakObservedShockwaveRings);
        Fixture->SetNumberField(TEXT("maximumObservedHoofDustInstances"),PeakHoofDust);Fixture->SetNumberField(TEXT("maximumObservedFormationDustInstances"),PeakFormationDust);
        Fixture->SetNumberField(TEXT("maximumObservedDeathFleckInstances"),PeakDeathFlecks);Fixture->SetNumberField(TEXT("maximumObservedSlowRippleInstances"),PeakSlowRipples);
        Fixture->SetNumberField(TEXT("maximumObservedSelectedSurfaceInstances"),PeakSelectedSurfaceInstances);
        auto Native=MakeShared<FJsonObject>();Native->SetNumberField(TEXT("hoofDustInstances"),LastHoofDust);Native->SetNumberField(TEXT("formationDustInstances"),LastFormationDust);
        Native->SetNumberField(TEXT("deathFleckInstances"),LastDeathFlecks);Native->SetNumberField(TEXT("slowRippleInstances"),LastSlowRipples);
        Native->SetNumberField(TEXT("selectedSurfaceInstances"),LastHoofDust+LastFormationDust+LastDeathFlecks+LastSlowRipples);Fixture->SetObjectField(TEXT("lastNativeSurfaceCounts"),Native);
        Fixture->SetNumberField(TEXT("expiredBodyDeathFleckObservations"),ExpiredBodiesWithDeathFlecks);
        Fixture->SetBoolField(TEXT("noDeathFlecksOnExpiredBodies"),ExpiredBodiesWithDeathFlecks==0);
        Fixture->SetStringField(TEXT("deathFleckResidueSampling"),TEXT("every capture tick; a body is overdue after its authored cleanup deadline plus the same two-frame tolerance used for actor retirement, allowing presentation/capture tick ordering"));
        Fixture->SetObjectField(TEXT("diagnosticSamplingTime"),Summary(DiagnosticTimes));Fixture->SetArrayField(TEXT("visualSamples"),VisualSamples);
        TArray<TSharedPtr<FJsonValue>> Models;auto SortedModels=ObservedModelIds.Array();SortedModels.Sort();for(const auto& Id:SortedModels)Models.Add(MakeShared<FJsonValueString>(Id));Fixture->SetArrayField(TEXT("observedCardModelIds"),Models);
        if(FinalVisualState)Fixture->SetObjectField(TEXT("lastVisualDiagnostics"),FinalVisualState);
        auto Require=[&](bool Passed,const TCHAR* Message){if(!Passed)FixtureErrors.Add(MakeShared<FJsonValueString>(Message));};
        Require(Proof,TEXT("an accepted deployment lacked real paid cost or canonical member-count proof"));
        Require(OpeningComplete&&Coverage.Num()==32,TEXT("the opening wave did not complete all eight cards on both teams and lanes"));
        Require(WaveEvents>0&&WaveHitEvents>0,TEXT("actual Pulverizer launch and damage events were not both observed"));
        Require(MaximumDeathBurst>=2,TEXT("no simultaneous defeats were observed"));
        Require(DeathCleanupComplete(RetiredDeathVisuals,LateDeathVisuals,PendingOverdueDeathVisuals),TEXT("observed death cleanup was absent, late, or still live past its deadline and frame tolerance"));
        Require(ExpiredBodiesWithDeathFlecks==0,TEXT("native death flecks remained on an already expired body"));
        Require(ObservedModelIds.Contains(TEXT("mini_stampede"))&&ObservedModelIds.Contains(TEXT("stampede"))&&ObservedModelIds.Contains(TEXT("pulverizer")),TEXT("one or more selected card models were never observed"));
        Require(ObservedModelIds.Contains(TEXT("bone_warden"))&&ObservedModelIds.Contains(TEXT("aether_reaper"))&&ObservedModelIds.Contains(TEXT("iron_giant")),TEXT("one or more approved new card models were never observed"));
        FixturePassed=FixtureErrors.IsEmpty();
        Fixture->SetBoolField(TEXT("passed"),FixturePassed);Fixture->SetArrayField(TEXT("errors"),FixtureErrors);Report->SetObjectField(TEXT("expansionFixture"),Fixture);
    }
    Report->SetNumberField(TEXT("warmupSeconds"),Warmup);Report->SetNumberField(TEXT("measuredSeconds"),Duration);
    Report->SetObjectField(TEXT("frame"),Summary(Frames));Report->SetObjectField(TEXT("gameThread"),Summary(GameTimes));
    Report->SetObjectField(TEXT("renderThread"),Summary(RenderTimes));Report->SetObjectField(TEXT("gpuFrame"),Summary(GPUTimes));
    Report->SetArrayField(TEXT("hitchesOver50Ms"),Hitches);
    Report->SetNumberField(TEXT("peakEntities"),PeakEntities);Report->SetNumberField(TEXT("peakProjectiles"),PeakProjectiles);Report->SetNumberField(TEXT("peakHazards"),PeakHazards);
    auto* Meta=GetWorld()->GetGameInstance()->GetSubsystem<URiftMetaSimulationSubsystem>();double FinalMetaGames=0;
    if(auto Data=Meta->Dataset())Data->TryGetNumberField(TEXT("games"),FinalMetaGames);
    Report->SetNumberField(TEXT("metaGamesDuringBattle"),FinalMetaGames-InitialMetaGames);Report->SetStringField(TEXT("metaStatus"),Meta->Status());
    Report->SetNumberField(TEXT("processPeakPhysicalBytes"),double(FPlatformMemory::GetStats().PeakUsedPhysical));
    int32 Width=0,Height=0;if(auto* PC=GetWorld()->GetFirstPlayerController())PC->GetViewportSize(Width,Height);
    Report->SetNumberField(TEXT("width"),Width);Report->SetNumberField(TEXT("height"),Height);
    FString Text,Error;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));
    const bool Saved=URiftProfileSubsystem::AtomicWrite(Filename,Text,Error);
    RIFT_LOG(LogRift,Log,TEXT("Performance QA %s: %s"),Saved?TEXT("saved"):TEXT("failed"),Saved?*Filename:*Error);
    GetWorld()->GetSubsystem<URiftMatchSubsystem>()->OnEvent.RemoveAll(this);
    FPlatformMisc::RequestExitWithStatus(false,Saved&&FixturePassed?0:1);
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftExpansionPerformanceFixtureTest,"Rift.Performance.ExpansionPaidFixture",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftExpansionPerformanceFixtureTest::RunTest(const FString& Parameters)
{
    auto* Clock=NewObject<URiftPerformanceCapture>();Clock->ResetExpansionWaveClock(0);
    TestFalse(TEXT("Opening age does not immediately earn a second paid wave"),Clock->AdvanceExpansionWaveClock(0));
    TestFalse(TEXT("A simulation age below five seconds cannot earn another wave"),Clock->AdvanceExpansionWaveClock(4.99));
    for(int32 WallInterval=0;WallInterval<20;++WallInterval)
        TestFalse(TEXT("Repeated capture ticks with stalled simulation do not manufacture waves"),Clock->AdvanceExpansionWaveClock(4.99));
    TestTrue(TEXT("Five actual simulation seconds earn exactly one next paid wave"),Clock->AdvanceExpansionWaveClock(5));
    TestFalse(TEXT("The same simulation age cannot earn a duplicate wave"),Clock->AdvanceExpansionWaveClock(5));
    TestTrue(TEXT("A long simulation advance earns one wave at its actual age"),Clock->AdvanceExpansionWaveClock(21));
    TestFalse(TEXT("A long advance cannot trigger an immediate catch-up burst"),Clock->AdvanceExpansionWaveClock(21));
    TestFalse(TEXT("The following interval starts at the actual overshoot age"),Clock->AdvanceExpansionWaveClock(25.99));
    TestTrue(TEXT("The following interval completes after five more simulation seconds"),Clock->AdvanceExpansionWaveClock(26));
    Clock->ResetExpansionWaveClock(120);
    TestFalse(TEXT("Nonzero phase fixtures wait five seconds after their opening wave"),Clock->AdvanceExpansionWaveClock(124.99));
    TestTrue(TEXT("Nonzero phase fixtures earn their next wave at the actual threshold"),Clock->AdvanceExpansionWaveClock(125));
    rift::Match Restarted(ExpansionOptions(74021));Clock->ResetExpansionWaveClock(Restarted.State().elapsed);
    TestFalse(TEXT("An actual new match resets the prior match's cadence"),Clock->AdvanceExpansionWaveClock(Restarted.State().elapsed));
    Restarted.Step(5);
    TestTrue(TEXT("A restarted real simulation earns its own five-second wave"),Clock->AdvanceExpansionWaveClock(Restarted.State().elapsed));
    TestFalse(TEXT("A nonfinite clock never grants a wave"),Clock->AdvanceExpansionWaveClock(std::numeric_limits<double>::infinity()));
    rift::Match Sim(ExpansionOptions(74021));const auto Plays=PaidExpansionWave(Sim,1,0);
    TestEqual(TEXT("Opening stress wave requests all eight real cards on both teams and lanes"),Plays.Num(),32);
    // Retain the complete options object: operator[] on a member of a temporary
    // MatchOptions does not extend that object's lifetime for a range-for.
    const auto FixtureOptions=ExpansionOptions(0);
    int ExpectedMembers=0;for(const auto& Id:FixtureOptions.decks[0]) {
        const auto* Card=rift::FindCard(Id);
        if(!TestNotNull(TEXT("Every retained fixture deck ID has a canonical definition"),Card))continue;
        if(!Card->spell)ExpectedMembers+=4*Card->count;
    }
    TestTrue(TEXT("Expected fixture bodies are computed from a retained legal deck"),ExpectedMembers>0&&rift::ValidateDeck(FixtureOptions.decks[0]));
    int Spawned=0,StampedeMembers=0,MiniMembers=0;double Spent=0;TSet<FString> Coverage;
    for(const auto& Value:Plays)
    {
        const auto Play=Value->AsObject();const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*Play->GetStringField(TEXT("cardId"))));
        if(!TestTrue(TEXT("Every opening wave request succeeds through the real hand and collision packing"),Play->GetBoolField(TEXT("accepted"))))continue;
        TestFalse(TEXT("Performance wave card plays are paid, not developer spawns"),Play->GetBoolField(TEXT("sandbox")));
        TestEqual(TEXT("Actual paid member count matches the canonical card count"),Play->GetNumberField(TEXT("actualMembers")),double(Card->spell?0:Card->count));
        TestEqual(TEXT("Real paid spend matches the canonical cost"),Play->GetNumberField(TEXT("actualSpent")),double(Card->cost));
        Spawned+=int(Play->GetNumberField(TEXT("actualMembers")));Spent+=Play->GetNumberField(TEXT("actualSpent"));
        if(Card->id=="stampede")StampedeMembers+=int(Play->GetNumberField(TEXT("actualMembers")));
        if(Card->id=="mini_stampede")MiniMembers+=int(Play->GetNumberField(TEXT("actualMembers")));
        Coverage.Add(Play->GetStringField(TEXT("cardId"))+TEXT("|")+Play->GetStringField(TEXT("team"))+FString::Printf(TEXT("|%.0f"),Play->GetNumberField(TEXT("lane"))));
    }
    TestEqual(TEXT("Paid stress fixture never silently loses a swarm member"),Spawned,ExpectedMembers);TestEqual(TEXT("All card/team/lane combinations are exercised"),Coverage.Num(),32);
    TestEqual(TEXT("Four real paid Stampede plays create exactly sixty members"),StampedeMembers,60);
    TestEqual(TEXT("Four real paid Mini Stampede plays create exactly twenty members"),MiniMembers,20);
    int ActualOpeningBodies=0;for(const auto& Entity:Sim.State().entities)if(Entity.kind<rift::EntityKind::Guard)++ActualOpeningBodies;
    TestEqual(TEXT("Spawn event telemetry agrees with every actual opening simulation body"),Spawned,ActualOpeningBodies);
    TestEqual(TEXT("Fixture grants are distinct from actual paid spend"),Sim.State().spent[0]+Sim.State().spent[1],Spent);
    Sim.Step(5);PaidExpansionWave(Sim,1,1);Sim.Step(3);
    int Waves=0,Hits=0,Deaths=0,Burst=0,MaximumBurst=0;double PreviousDeath=-1;bool TeamWaves[2]={false,false};
    for(const auto& Event:Sim.Events())
    {
        if(Event.type=="shockwave_launch"){++Waves;TeamWaves[int(Event.team)]=true;}
        if(Event.type=="damage"&&Event.damageKind=="shockwave")++Hits;
        if(Event.type=="death"){++Deaths;Burst=PreviousDeath==Event.time?Burst+1:1;PreviousDeath=Event.time;MaximumBurst=FMath::Max(MaximumBurst,Burst);}
    }
    TestTrue(TEXT("Both sides launch actual traveling Pulverizer waves"),Waves>0&&TeamWaves[0]&&TeamWaves[1]);
    TestTrue(TEXT("Stress fixture executes genuine wave damage and simultaneous defeats"),Hits>0&&Deaths>0&&MaximumBurst>=2);
    TestFalse(TEXT("One successful retirement cannot conceal a late death visual"),DeathCleanupComplete(1,1,0));
    TestFalse(TEXT("One successful retirement cannot conceal another overdue live visual"),DeathCleanupComplete(1,0,1));
    TestTrue(TEXT("Every due visual can pass while newer deaths remain before their deadlines"),DeathCleanupComplete(4,0,0));
    TestFalse(TEXT("Two-frame observation tolerance permits a normal boundary sample"),DeathCleanupLate(.87,.85,2./60.));
    TestTrue(TEXT("An authored building body still alive beyond its tolerance fails cleanup"),DeathCleanupLate(1.,.85,2./60.));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftCorpseVisualCleanupTest,"Rift.Integration.CorpseVisualCleanup",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftCorpseVisualCleanupTest::RunTest(const FString& Parameters)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox")))
    {AddError(TEXT("Corpse cleanup fixture requires isolated RiftSaveRoot/UserDir and RiftAutomationSandbox."));return false;}
    auto* GI=NewObject<UGameInstance>(GEngine);GI->InitializeStandalone(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto* World=GI->GetWorld();if(!TestNotNull(TEXT("Cleanup fixture has a native world"),World)){GI->Shutdown();return false;}
    auto* Match=World->GetSubsystem<URiftMatchSubsystem>();
    ON_SCOPE_EXIT {Match->LeaveMatch();World->DestroyWorld(false);GI->Shutdown();GEngine->DestroyWorldContext(World);};
    auto* Catalog=GI->GetSubsystem<URiftAssetCatalogSubsystem>();auto* Authored=Catalog->Card(TEXT("pulverizer"));auto* Legacy=Catalog->Card(TEXT("archer_tower"));
    if(!TestNotNull(TEXT("Cleanup fixture uses the actual imported Pulverizer metadata"),Authored)||!TestNotNull(TEXT("Cleanup fixture uses the legacy Archer Tower metadata"),Legacy))return false;
    if(!TestTrue(TEXT("Pulverizer has an authored cleanup earlier than authoritative building removal"),Authored->DeathCleanupSeconds>0&&Authored->DeathCleanupSeconds<1.))return false;
    if(!TestTrue(TEXT("Legacy Archer Tower retains its original cleanup boundary"),Legacy->DeathCleanupSeconds<=0))return false;
    rift::MatchOptions Options;Options.seed=74021;Options.aiEnabled={false,false};rift::Match Sim(Options);
    if(!TestTrue(TEXT("Cleanup fixture creates real building bodies"),Sim.Spawn(rift::Team::Player,"pulverizer",{rift::arena::BridgeCenterX,4.5})&&Sim.Spawn(rift::Team::Player,"archer_tower",{rift::arena::HalfWidth-2.5,6.5})))return false;
    Sim.Step(.05);uint64 AuthoredId=0,LegacyId=0,CrownId=0;
    auto FindBody=[&](uint64 Id)->const rift::Entity* {for(const auto& Entity:Sim.State().entities)if(Entity.id==Id)return &Entity;return nullptr;};
    for(const auto& Entity:Sim.State().entities)
    {
        if(Entity.cardId=="pulverizer")AuthoredId=Entity.id;
        if(Entity.cardId=="archer_tower")LegacyId=Entity.id;
        if(!CrownId&&Entity.kind==rift::EntityKind::Guard&&Entity.team==rift::Team::Player)CrownId=Entity.id;
    }
    const auto* Nova=rift::FindCard("nova_flask");
    if(!TestTrue(TEXT("Cleanup fixture has the canonical structure-damage spell"),Nova&&Nova->towerDamage>0))return false;
    for(uint64 Id:{AuthoredId,LegacyId})
    {
        const auto* Body=FindBody(Id);if(!TestNotNull(TEXT("Cleanup fixture preserves the actual spawned body"),Body))return false;
        const auto Position=Body->position;const int Hits=FMath::CeilToInt(Body->hp/Nova->towerDamage);
        for(int I=0;I<Hits;++I)if(!TestTrue(TEXT("Real spell damage defeats the fixture building"),Sim.Spawn(rift::Team::Enemy,"nova_flask",Position)))return false;
    }
    // Queue both victims before advancing their real cast delay so neither
    // corpse ages out while the other spell is still in flight.
    const double ImpactDeadline=Sim.State().elapsed+Nova->castDelay;
    while(Sim.State().elapsed<=ImpactDeadline+1e-9)Sim.Step(1./60.);
    for(uint64 Id:{AuthoredId,LegacyId})
    {
        if(!TestTrue(TEXT("Fixture corpse came from authoritative damage, not a fabricated dead flag"),FindBody(Id)&&FindBody(Id)->dead))return false;
    }
    if(!TestTrue(TEXT("Actual Crown destruction creates permanent rubble"),Sim.SetTowerHP(CrownId,0)))return false;
    auto State=Sim.State();const double Died=FindBody(AuthoredId)->died;
    const double Cleanup=FMath::Clamp(double(Authored->DeathCleanupSeconds),.1,1.05);
    State.elapsed=Died+Cleanup*.5;Match->SetReplayView(&State);
    auto* Arena=World->SpawnActor<ARiftArenaPresentation>();if(!TestNotNull(TEXT("Cleanup fixture uses the production arena owner"),Arena))return false;
    Arena->DispatchBeginPlay();Arena->Tick(0);
    if(!TestNotNull(TEXT("Recent dead building renders its actual death animation"),Arena->Visual(AuthoredId)))return false;
    TestTrue(TEXT("Recent corpse is already dead rather than starting a fresh live pose"),Arena->Visual(AuthoredId)->IsDead());
    TWeakObjectPtr<ARiftUnitVisual> OldBody=Arena->Visual(AuthoredId);
    State.elapsed=Died+Cleanup+.001;Match->SetReplayView(&State);Arena->Tick(0);
    TestNull(TEXT("Authored corpse retires while its authoritative entity is still present"),Arena->Visual(AuthoredId));
    TestFalse(TEXT("Retirement destroys the actual authored visual actor"),OldBody.IsValid());
    TestNotNull(TEXT("Earlier authored cleanup preserves the longer legacy death"),Arena->Visual(LegacyId));
    TestTrue(TEXT("Cleanup never removes or edits authoritative bodies"),State.entities.size()==Sim.State().entities.size()&&FindBody(AuthoredId)&&FindBody(AuthoredId)->dead);
    Arena->Tick(0);TestNull(TEXT("A retained dead entity cannot recreate its expired visual next frame"),Arena->Visual(AuthoredId));
    State.elapsed=Died+1.05;Match->SetReplayView(&State);Arena->Tick(0);
    TestNotNull(TEXT("Legacy corpse remains at its exact strict 1.05-second boundary"),Arena->Visual(LegacyId));
    State.elapsed=Died+1.051;Match->SetReplayView(&State);Arena->Tick(0);
    TestNull(TEXT("Legacy corpse retires after its preserved boundary"),Arena->Visual(LegacyId));
    TestNotNull(TEXT("Dead Crown towers retain their permanent rubble"),Arena->Visual(CrownId));
    State.elapsed=Died+Cleanup*.25;Match->SetReplayView(&State);Arena->Tick(0);
    TestNotNull(TEXT("Backward replay seeking restores a corpse whose death has not finished"),Arena->Visual(AuthoredId));
    State.elapsed=Died+1.1;Match->SetReplayView(&State);Arena->Tick(0);
    TestNull(TEXT("Forward replay seeking suppresses an already completed corpse"),Arena->Visual(AuthoredId));
    auto* LoadedArena=World->SpawnActor<ARiftArenaPresentation>();if(!TestNotNull(TEXT("Fresh replay creates its production arena owner"),LoadedArena))return false;
    LoadedArena->DispatchBeginPlay();LoadedArena->Tick(0);
    TestNull(TEXT("Fresh replay presentation never creates an old completed corpse"),LoadedArena->Visual(AuthoredId));
    TestNotNull(TEXT("Fresh replay presentation still restores Crown rubble"),LoadedArena->Visual(CrownId));
    return !HasAnyErrors();
}
#endif
