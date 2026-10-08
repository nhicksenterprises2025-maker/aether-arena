#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Simulation/RiftSimulation.h"
#include <memory>
#include "RiftMatchSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FRiftSimulationEvent,const rift::Event&);
DECLARE_MULTICAST_DELEGATE(FRiftMatchChanged);

UCLASS()
class RIFTCROWNARENA_API URiftMatchSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    void Initialize(FSubsystemCollectionBase& Collection) override;
    void Deinitialize() override;
    void Tick(float DeltaTime) override;
    TStatId GetStatId()const override;
    bool IsTickable()const override;
    UFUNCTION(BlueprintCallable) void StartMatch(bool Training=false,bool BothAI=false);
    UFUNCTION(BlueprintCallable) void LeaveMatch();
    UFUNCTION(BlueprintCallable) bool PlayCard(int32 HandIndex,FVector2D Tile);
    UFUNCTION(BlueprintCallable) void SetSpeed(float Value);
    UFUNCTION(BlueprintCallable) float GetSpeed()const{return Speed;}
    UFUNCTION(BlueprintCallable) bool IsActive()const{return Match!=nullptr;}
    UFUNCTION(BlueprintCallable) bool IsTraining()const{return bTraining;}
    UFUNCTION(BlueprintCallable) float GetAether(int32 Team=0)const;
    UFUNCTION(BlueprintCallable) float GetTimeRemaining()const;
    UFUNCTION(BlueprintCallable) FString GetPhase()const;
    UFUNCTION(BlueprintCallable) bool CanPlace(int32 HandIndex,FVector2D Tile)const;
    rift::Match* Simulation(){return Match.get();}
    const rift::Snapshot* ViewState()const;
    void SetReplayView(const rift::Snapshot* State);
    void FlushEvents();
    void SampleBeforeMutation();
    FRiftSimulationEvent OnEvent;
    FRiftMatchChanged OnChanged;
    static FVector WorldPoint(rift::Vec2 Point,float Height=0){return FVector(Point.x*100.0,Point.z*100.0,Height);}
    static rift::Vec2 TilePoint(FVector Point){return {Point.X/100.0,Point.Y/100.0};}
private:
    std::unique_ptr<rift::Match> Match;
    const rift::Snapshot* ReplayView=nullptr;
    float Speed=1;
    bool bTraining=false;
    bool bResultSaved=false;
    double ReplaySampleClock=0;
    void FinalizeResult();
};

UCLASS()
class RIFTCROWNARENA_API URiftCombatSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    bool CanTarget(rift::EntityId Source,rift::EntityId Target)const;
};
UCLASS()
class RIFTCROWNARENA_API URiftPathfindingSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    TArray<FVector> Route(rift::EntityId Source,rift::EntityId Target)const;
};
UCLASS()
class RIFTCROWNARENA_API URiftAISubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable) void SetEnabled(int32 Team,bool Enabled);
    UFUNCTION(BlueprintCallable) bool SetStyle(int32 Team,const FString& Style);
    UFUNCTION(BlueprintCallable) FString Readout(int32 Team=1)const;
};
UCLASS()
class RIFTCROWNARENA_API URiftDeveloperSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite) bool ShowPaths=false;
    UPROPERTY(BlueprintReadWrite) bool ShowSight=false;
    UPROPERTY(BlueprintReadWrite) bool ShowRanges=false;
    UPROPERTY(BlueprintReadWrite) bool ShowTargets=false;
    UPROPERTY(BlueprintReadWrite) bool ShowTiles=false;
    UPROPERTY(BlueprintReadWrite) bool ShowHardLocks=false;
    UFUNCTION(BlueprintCallable) void ChangeAether(int32 Team,float Delta,bool Maximum=false);
    UFUNCTION(BlueprintCallable) bool SpawnCard(int32 Team,const FString& Card,FVector2D Tile);
    UFUNCTION(BlueprintCallable) bool SetTowerHP(int64 Tower,float HP);
    UFUNCTION(BlueprintCallable) void ClearBattlefield();
};
