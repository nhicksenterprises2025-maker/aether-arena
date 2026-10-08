#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RiftBattleAudioSubsystem.generated.h"

class UAudioComponent;
class USoundBase;
class URiftUIWidget;

UCLASS()
class RIFTCROWNARENA_API URiftBattleAudioSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    void Initialize(FSubsystemCollectionBase& Collection) override;
    void Deinitialize() override;
    void SetBattleWorld(UWorld* World);
    void RefreshVolumes();
    void PlayEffect(FName Name, FVector Position, float Gain=1.f, float Pitch=1.f);
    UFUNCTION(BlueprintCallable) void PlayUI(FName Name=TEXT("ui_click"));
    FString RunAudioSmokeJSON(URiftUIWidget* Interface);
    void RunAudioSmoke(URiftUIWidget* Interface,TFunction<void(const FString&)> Completed);
private:
    USoundBase* Sound(FName Name);
    UPROPERTY() TMap<FName,TObjectPtr<USoundBase>> Sounds;
    UPROPERTY() TObjectPtr<UAudioComponent> Music;
    UPROPERTY() TObjectPtr<UAudioComponent> Ambience;
    struct FVoice { TWeakObjectPtr<UAudioComponent> Component; float Gain=1.f; bool UI=false; };
    TArray<FVoice> Voices;
    TWeakObjectPtr<UWorld> BattleWorld;
    float Master=0.8f, MusicGain=0.45f, SFX=0.8f, UI=0.7f;
};
