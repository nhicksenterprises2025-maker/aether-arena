#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "RiftBattleAudioSubsystem.generated.h"

class UAudioComponent;
class USoundBase;
class USoundSubmix;
class USoundSubmixBase;
class USubmixEffectDynamicsProcessorPreset;
class URiftUIWidget;

UCLASS()
class RIFTCROWNARENA_API URiftBattleAudioSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()
public:
    void Initialize(FSubsystemCollectionBase& Collection) override;
    void Deinitialize() override;
    void Tick(float DeltaTime) override;
    TStatId GetStatId() const override;
    bool IsTickable() const override;
    bool IsTickableWhenPaused() const override { return true; }
    void SetBattleWorld(UWorld* World);
    void RefreshVolumes();
    void PlayEffect(FName Name, FVector Position, float Gain=1.f, float Pitch=1.f);
    UFUNCTION(BlueprintCallable) void PlayUI(FName Name=TEXT("ui_click"));
    FString RunAudioSmokeJSON(URiftUIWidget* Interface);
    void RunAudioSmoke(URiftUIWidget* Interface,TFunction<void(const FString&)> Completed);
private:
    USoundBase* Sound(FName Name);
    bool EnsureMixRegistered(UWorld* World);
    void PruneVoices();
    bool AdmitVoice(FName Name, bool IsUI, uint8 Priority, float Cooldown);
    FName Variation(FName Name);
    void StopOneShots();
    void FillSmokeBurst();
    UPROPERTY() TMap<FName,TObjectPtr<USoundBase>> Sounds;
    UPROPERTY() TMap<FName,TObjectPtr<USoundSubmixBase>> OriginalSubmixRoutes;
    UPROPERTY() TObjectPtr<UAudioComponent> Music;
    UPROPERTY() TObjectPtr<UAudioComponent> Ambience;
    UPROPERTY() TObjectPtr<USoundSubmix> MasterMix;
    UPROPERTY() TObjectPtr<USubmixEffectDynamicsProcessorPreset> Limiter;
    struct FVoice { TWeakObjectPtr<UAudioComponent> Component; float Gain=1.f; bool UI=false; uint8 Priority=20; FName Cue; };
    TArray<FVoice> Voices;
    TMap<FName,double> LastPlayTime;
    TMap<FName,uint32> VariantSequence;
    TSet<uint32> RegisteredDeviceIds;
    TWeakObjectPtr<UWorld> BattleWorld;
    float MusicDuck=1.f;
    float Master=0.8f, MusicGain=0.45f, SFX=0.8f, UI=0.7f;
};
