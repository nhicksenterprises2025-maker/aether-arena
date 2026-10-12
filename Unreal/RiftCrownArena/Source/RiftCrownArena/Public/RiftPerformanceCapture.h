#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Dom/JsonObject.h"
#include "Simulation/RiftSimulation.h"
#include "RiftPerformanceCapture.generated.h"

// Explicit, isolated command-line release QA; inactive in ordinary games.
UCLASS()
class RIFTCROWNARENA_API URiftPerformanceCapture : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    void Initialize(FSubsystemCollectionBase& Collection) override;
    void Tick(float DeltaTime) override;
    bool IsTickable() const override;
    TStatId GetStatId() const override;
private:
    friend class FRiftExpansionPerformanceFixtureTest;
    FString Filename;
    bool bEnabled=false,bStarted=false,bStress=false,bWithMeta=false,bExpansionStress=false;
    double StartTime=0,PreviousTime=0,LastWave=0,Warmup=8,Duration=90;
    double InitialMetaGames=0;
    int32 PeakEntities=0,PeakProjectiles=0,PeakHazards=0;
    TArray<double> Frames,GameTimes,RenderTimes,GPUTimes;
    TArray<TSharedPtr<FJsonValue>> Hitches;
    double FixtureStartSeconds=0,LastVisualSample=0,FixtureGrantAether=0,FixturePaidAether=0;
    double LastExpansionWaveSimulationSeconds=0;
    float FixtureSpeed=1;
    uint32 FixtureSeed=74021;
    int32 FixtureMatches=0,FixtureWaves=0,MatchWaveIndex=0,PaidPlayEvents=0,SpawnEvents=0,DeathEvents=0,WaveEvents=0,WaveHitEvents=0;
    int32 DeathBurst=0,MaximumDeathBurst=0,PeakShockwaves=0,PeakObservedShockwaveRings=0,PeakObservedVisuals=0,PeakObservedDeadVisuals=0,PeakObservedParticles=0;
    double LastDeathTime=-1,MaximumObservedDeadAge=0,MaximumCleanupSeconds=0;
    int32 ObservedDeathVisuals=0,RetiredDeathVisuals=0,RestartDeathVisuals=0,LateDeathVisuals=0;
    int32 PendingNotDueDeathVisuals=0,PendingGraceDeathVisuals=0,PendingOverdueDeathVisuals=0;
    double PreviousCleanupTime=-1,PreviousCleanupFrame=1./60.,CleanupToleranceSeconds=1./30.;
    struct FDeathVisual
    {
        TWeakObjectPtr<class ARiftUnitVisual> Actor;
        double Died=0,CleanupSeconds=1.05;
        bool Late=false;
        TSharedPtr<FJsonObject> Record;
    };
    TArray<FDeathVisual> DeathVisuals;
    TArray<TSharedPtr<FJsonValue>> DeathCleanupRecords;
    TSet<FString> ObservedModelIds;
    TArray<double> DiagnosticTimes;
    TArray<TSharedPtr<FJsonValue>> PaidDeployments,VisualSamples;
    FString ScreenshotRoot;
    int32 NextScreenshot=0;
    TArray<TSharedPtr<FJsonValue>> ScreenshotRequests;
    int32 PeakHoofDust=0,PeakFormationDust=0,PeakDeathFlecks=0,PeakSlowRipples=0,PeakSelectedSurfaceInstances=0;
    int32 LastHoofDust=0,LastFormationDust=0,LastDeathFlecks=0,LastSlowRipples=0,ExpiredBodiesWithDeathFlecks=0;
    TSharedPtr<FJsonObject> FinalVisualState;
    TArray<TSharedPtr<FJsonValue>> FixtureErrors;
    void StartExpansionMatch();
    void ResetExpansionWaveClock(double SimulationSeconds);
    bool AdvanceExpansionWaveClock(double SimulationSeconds);
    void ObserveFixtureEvent(const rift::Event& Event);
    void ObserveDeathCleanup();
    void ObserveFixtureVisuals(double Now);
    void SpawnWave();
    void Finish();
};
