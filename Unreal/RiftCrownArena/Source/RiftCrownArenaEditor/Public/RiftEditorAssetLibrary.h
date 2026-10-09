#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RiftEditorAssetLibrary.generated.h"

UCLASS()
class RIFTCROWNARENAEDITOR_API URiftEditorAssetLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable) static FString BuildPresentationAssetsJSON();
    UFUNCTION(BlueprintCallable) static FString InspectImportedAssetsJSON();
    UFUNCTION(BlueprintCallable) static FString FinalizeImportedPhysicsAssetsJSON();
    /** FontFace cache refresh requires Slate font services even in a commandlet. */
    UFUNCTION(BlueprintCallable) static bool EnsureFontImportSlate();
    /** Build the composite from imported faces; FFontData is not exposed to Python. */
    UFUNCTION(BlueprintCallable) static FString BuildUIFontJSON();
};
