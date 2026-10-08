#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dom/JsonObject.h"
#include "RiftProfileSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRift, Log, All);

USTRUCT(BlueprintType)
struct FRiftDeckPreset
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FString> Cards;
};

USTRUCT(BlueprintType)
struct FRiftSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MasterVolume=0.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MusicVolume=0.45f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float SFXVolume=0.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float UIVolume=0.7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float UIScale=1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float CameraSpeed=1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool DragDeploy=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool ConfirmDeploy=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool VSync=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Width=1920;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Height=1080;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 WindowMode=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 FrameCap=60;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Quality=2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 AA=2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Shadows=2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Effects=2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Textures=2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Post=2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 ViewDistance=2;
};

UCLASS()
class RIFTCROWNARENA_API URiftProfileSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    void Initialize(FSubsystemCollectionBase& Collection) override;
    void Deinitialize() override;
    UPROPERTY(BlueprintReadOnly) FString Username=TEXT("RIFTBOUND");
    UPROPERTY(BlueprintReadOnly) FString PlayerId;
    UPROPERTY(BlueprintReadOnly) int32 Wins=0;
    UPROPERTY(BlueprintReadOnly) int32 Losses=0;
    UPROPERTY(BlueprintReadOnly) int32 Draws=0;
    UPROPERTY(BlueprintReadOnly) int32 Matches=0;
    UPROPERTY(BlueprintReadOnly) int32 Crowns=0;
    UPROPERTY(BlueprintReadOnly) int32 Gems=1250;
    UPROPERTY(BlueprintReadOnly) int32 Gold=8420;
    UPROPERTY(BlueprintReadOnly) TArray<FRiftDeckPreset> Presets;
    UPROPERTY(BlueprintReadOnly) FString ActivePreset=TEXT("deck-1");
    UPROPERTY(BlueprintReadWrite) FRiftSettings Settings;
    UPROPERTY(BlueprintReadOnly) TArray<FString> ReplayFiles;
    UPROPERTY(BlueprintReadOnly) TArray<FString> MetaFiles;
    UPROPERTY(BlueprintReadOnly) FString LastError;
    UFUNCTION(BlueprintCallable) bool Save();
    UFUNCTION(BlueprintCallable) bool SetName(const FString& NewName);
    UFUNCTION(BlueprintCallable) bool SaveDeck(int32 Index, const FString& Name, const TArray<FString>& Cards);
    UFUNCTION(BlueprintCallable) bool SelectPreset(int32 Index);
    UFUNCTION(BlueprintCallable) void ApplySettings();
    void RecordResult(int32 Winner, int32 PlayerCrowns);
    TArray<FString> ActiveDeck() const;
    static FString SaveRoot();
    static bool AtomicWrite(const FString& Filename, const FString& Contents, FString& Error);
    static bool ValidDeck(const TArray<FString>& Cards);
    static TArray<FString> DefaultDeck();
private:
    friend class FRiftProfilePersistenceTest;
    bool LoadFile(const FString& Filename, bool Browser);
    void Defaults();
    void LoadStoredProfile();
    TSharedPtr<FJsonObject> PreservedRoot;
    FString ImportedChecksum;
    bool bReadOnlyFutureSchema=false;
};
