#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Simulation/RiftSimulation.h"
#include <memory>
#include "RiftExpansionCapture.generated.h"

class FJsonObject;
class FJsonValue;
class URiftMatchSubsystem;

/** Explicit isolated QA only: a real paid simulation viewed through the normal snapshot renderer. */
UCLASS(Transient, NotBlueprintable)
class ARiftExpansionCapture : public AActor
{
    GENERATED_BODY()
public:
    ARiftExpansionCapture();
    bool Build(URiftMatchSubsystem* Match,const TSharedRef<FJsonObject>& Report);
    bool SampleReport(const TSharedRef<FJsonObject>& Report) const;
    void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    bool BuildWardenDeath(URiftMatchSubsystem* Match,const TSharedRef<FJsonObject>& Report);
    bool SampleWardenDeath(const TSharedRef<FJsonObject>& Report) const;
    bool BuildReaperDesign(URiftMatchSubsystem* Match,const TSharedRef<FJsonObject>& Report);
    bool SampleReaperDesign(const TSharedRef<FJsonObject>& Report) const;
    std::unique_ptr<rift::Match> Fixture;
    TWeakObjectPtr<URiftMatchSubsystem> View;
    TSet<uint64> SelectedIds;
    TArray<FString> SelectedCards;
    int32 ExpectedTeamCount=21;
    bool EffectsCapture=false;
    FString EffectStage=TEXT("shield");
    double EffectRequestedAdvance=.95;
    int32 EffectAdvanceSteps=57;
    double ExpectedAge=.35;
    bool SetupPassed=false;
    bool WardenDeathCapture=false;
    FString WardenDeathStage=TEXT("children");
    uint64 WardenId=0,WardenPlayId=0;
    double WardenDied=-1,WardenCleanup=0;
    int32 WardenDeathStep=0,WardenFixedSteps=0,WardenDeaths=0,WardenChildSpawnEvents=0;
    bool WardenDeathAnimationObserved=false;
    TSet<uint64> WardenChildIds;
    TArray<TSharedPtr<FJsonValue>> WardenChildBirthVisuals;
    TArray<TSharedPtr<FJsonValue>> WardenCaptiveMotion;
    bool ReaperDesignCapture=false,ReaperAuraMotionObserved=false,ReaperAuraSampled=false,ReaperEventAttributionPassed=true;
    FString ReaperStage=TEXT("idle");
    uint64 ReaperId=0,ReaperPlayId=0;
    int32 ReaperFixedSteps=0,ReaperCleaveEvents=0,ReaperCleaveVictims=0,ReaperDamagePackets=0,ReaperCloakEvents=0,ReaperExpireEvents=0;
    double ReaperCleaveAt=-1,ReaperCloakAt=-1,ReaperFirstPacketHP=-1;
    rift::Vec2 ReaperInitialPosition;
    FTransform ReaperFirstAuraTransform;
    TArray<TSharedPtr<FJsonValue>> ReaperIncomingPackets;
};
