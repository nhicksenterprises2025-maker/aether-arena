#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RiftBattleOverlay.generated.h"

class ARiftArenaPresentation;

/** Small projected bars and placement geometry; this widget never receives input. */
UCLASS()
class RIFTCROWNARENA_API URiftBattleOverlay : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetPresentation(ARiftArenaPresentation* InPresentation);
protected:
    int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool ParentEnabled) const override;
private:
    UPROPERTY() TObjectPtr<ARiftArenaPresentation> Presentation;
};
