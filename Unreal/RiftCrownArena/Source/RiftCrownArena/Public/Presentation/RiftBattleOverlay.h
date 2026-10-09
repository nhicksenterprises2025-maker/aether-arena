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
    int32 TrainingLineCount()const{return LastTrainingLineCount;}
    int32 TrainingLabelCount()const{return LastTrainingLabelCount;}
protected:
    int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool ParentEnabled) const override;
private:
    int32 PaintTrainingOverlay(const FGeometry& Geometry,FSlateWindowElementList& Elements,int32 Layer)const;
    mutable int32 LastTrainingLineCount=0,LastTrainingLabelCount=0;
    UPROPERTY() TObjectPtr<ARiftArenaPresentation> Presentation;
};
