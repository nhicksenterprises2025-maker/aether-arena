#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RiftCardData.h"
#include "RiftAssetLibrary.generated.h"

UCLASS()
class RIFTCROWNARENA_API URiftAssetLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable) static FString CardDefinitionsJSON();
};

UCLASS()
class RIFTCROWNARENA_API URiftAssetCatalogSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    void Initialize(FSubsystemCollectionBase& Collection)override;
    UPROPERTY() TMap<FString,TObjectPtr<URiftCardData>> Cards;
    URiftCardData* Card(const FString& Id)const;
    bool Validate(FString& Error)const;
};
