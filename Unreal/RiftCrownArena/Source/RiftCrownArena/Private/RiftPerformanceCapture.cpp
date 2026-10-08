#include "RiftPerformanceCapture.h"
#include "RiftDiagnostics.h"
#include "RiftGameMode.h"
#include "RiftUIWidget.h"
#include "RiftMatchSubsystem.h"
#include "RiftMetaSimulationSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "DynamicRHI.h"
#include "RenderTimer.h"
#include "RHI.h"
#include "HAL/PlatformMemory.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Serialization/JsonSerializer.h"

namespace
{
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
    bEnabled=true;
}
bool URiftPerformanceCapture::IsTickable()const{return bEnabled&&GetWorld()&&GetWorld()->IsGameWorld();}
TStatId URiftPerformanceCapture::GetStatId()const{RETURN_QUICK_DECLARE_CYCLE_STAT(URiftPerformanceCapture,STATGROUP_Tickables);}
void URiftPerformanceCapture::SpawnWave()
{
    auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();auto* Sim=M->Simulation();if(!Sim)return;
    for(auto Team:{rift::Team::Player,rift::Team::Enemy})for(int32 Lane:{-1,1})
    {
        const double Z=Team==rift::Team::Player?5.:-5.;
        for(const char* Card:{"boulderback","twin_blades","vampire_bats","frost_fang","storm_raven"})
            Sim->Spawn(Team,Card,{Lane*7.+(std::string(Card)=="storm_raven"?2.:0.),Z});
        Sim->Spawn(Team,"archer_tower",{Lane*10.,Z+(Team==rift::Team::Player?4.:-4.)});
    }
    M->FlushEvents();
}
void URiftPerformanceCapture::Tick(float)
{
    const double Now=FPlatformTime::Seconds();auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();
    auto* Meta=GetWorld()->GetGameInstance()->GetSubsystem<URiftMetaSimulationSubsystem>();
    if(!bStarted)
    {
        M->StartMatch(true,!bStress);
        if(bWithMeta){Meta->SetRate(0);Meta->Start();}
        if(auto Data=Meta->Dataset())Data->TryGetNumberField(TEXT("games"),InitialMetaGames);
        if(bStress){M->Simulation()->SetAIEnabled(rift::Team::Enemy,false);SpawnWave();}
        if(auto* PC=Cast<ARiftPlayerController>(GetWorld()->GetFirstPlayerController());PC&&PC->Interface)PC->Interface->SetBattleView();
        StartTime=PreviousTime=LastWave=Now;bStarted=true;return;
    }
    if(bStress&&M->ViewState()&&M->ViewState()->phase==rift::Phase::Finished)
    {
        M->StartMatch(true,false);M->Simulation()->SetAIEnabled(rift::Team::Enemy,false);SpawnWave();LastWave=Now;
        if(auto* PC=Cast<ARiftPlayerController>(GetWorld()->GetFirstPlayerController());PC&&PC->Interface)PC->Interface->SetBattleView();
    }
    if(bStress&&Now-LastWave>=5.){SpawnWave();LastWave=Now;}
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
    bEnabled=false;auto Report=MakeShared<FJsonObject>();
    Report->SetNumberField(TEXT("schema"),1);Report->SetStringField(TEXT("build"),LexToString(FApp::GetBuildConfiguration()));
    Report->SetStringField(TEXT("cpu"),FPlatformMisc::GetCPUBrand());Report->SetStringField(TEXT("gpu"),GRHIAdapterName);
    Report->SetBoolField(TEXT("stress"),bStress);Report->SetBoolField(TEXT("metaRequested"),bWithMeta);
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
    FPlatformMisc::RequestExitWithStatus(false,Saved?0:1);
}
