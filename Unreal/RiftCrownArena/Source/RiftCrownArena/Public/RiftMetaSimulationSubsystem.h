#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Async/Future.h"
#include "Dom/JsonObject.h"
#include "RiftMetaSimulationSubsystem.generated.h"

struct FRiftMetaWorker;
UCLASS()
class RIFTCROWNARENA_API URiftMetaSimulationSubsystem : public UGameInstanceSubsystem,public FTickableGameObject
{
    GENERATED_BODY()
public:
    void Initialize(FSubsystemCollectionBase& Collection) override;
    void Deinitialize() override;
    void Tick(float DeltaTime) override;
    TStatId GetStatId()const override;
    bool IsTickable()const override;
    UFUNCTION(BlueprintCallable) void Start(int32 Games=0);
    UFUNCTION(BlueprintCallable) void Pause(bool Paused=true);
    UFUNCTION(BlueprintCallable) void SetRate(int32 GamesPerMinute);
    UFUNCTION(BlueprintCallable) void Reset();
    UFUNCTION(BlueprintCallable) FString Status()const;
    UFUNCTION(BlueprintCallable) TArray<FString> DatasetNames()const;
    UFUNCTION(BlueprintCallable) bool SelectDataset(const FString& Name);
    UFUNCTION(BlueprintCallable) bool Export(const FString& Subject,const FString& Filename,const FString& Style=TEXT("all"),const FString& Archetype=TEXT("all"),bool CSV=false);
    void SetBattleActive(bool Active);
    TSharedPtr<FJsonObject> Dataset()const{return Data;}
    FString SelectedDatasetName()const{return CurrentFile;}
    TArray<TSharedPtr<FJsonObject>> CardRows(const FString& Style=TEXT("all"),const FString& Archetype=TEXT("all"))const;
    TArray<TSharedPtr<FJsonObject>> SynergyRows(const FString& Style=TEXT("all"),const FString& Archetype=TEXT("all"))const;
    TArray<TSharedPtr<FJsonObject>> StyleRows(const FString& Archetype=TEXT("all"))const;
    TArray<TSharedPtr<FJsonObject>> ArchetypeRows(const FString& Style=TEXT("all"))const;
    TArray<TSharedPtr<FJsonObject>> MatchupRows()const;
    TArray<TSharedPtr<FJsonObject>> Checkpoints()const;
    TArray<TSharedPtr<FJsonObject>> AlertRows(const FString& Style=TEXT("all"),const FString& Archetype=TEXT("all"))const;
    TArray<TSharedPtr<FJsonObject>> PatchRows(const FString& Baseline)const;
    FString LastError;
    static FString Fingerprint();
private:
    friend class FRiftMetaAggregationTest;
    friend class FRiftMetaWorkerPauseTest;
    TSharedPtr<FJsonObject> Data;
    TSharedPtr<FRiftMetaWorker,ESPMode::ThreadSafe> Worker;
    TFuture<void> Future;
    FString CurrentFile;
    float SaveClock=0;
    bool bBattle=false,bDirty=false,bPaused=true;
    int32 Rate=250;
    void Stop();
    void Harvest();
    void Persist();
    TSharedPtr<FJsonObject> Bucket(const FString& Style,const FString& Archetype)const;
};
