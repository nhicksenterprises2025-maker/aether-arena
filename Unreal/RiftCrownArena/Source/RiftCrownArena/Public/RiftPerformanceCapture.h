#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Dom/JsonObject.h"
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
    FString Filename;
    bool bEnabled=false,bStarted=false,bStress=false,bWithMeta=false;
    double StartTime=0,PreviousTime=0,LastWave=0,Warmup=8,Duration=90;
    double InitialMetaGames=0;
    int32 PeakEntities=0,PeakProjectiles=0,PeakHazards=0;
    TArray<double> Frames,GameTimes,RenderTimes,GPUTimes;
    TArray<TSharedPtr<FJsonValue>> Hitches;
    void SpawnWave();
    void Finish();
};
