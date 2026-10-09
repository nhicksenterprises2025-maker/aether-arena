#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RiftUIPrimitives.generated.h"

// Original vector interface art stays crisp across window sizes and UI scales.
UCLASS()
class RIFTCROWNARENA_API URiftCrownIcon : public UUserWidget
{
    GENERATED_BODY()
public:
    FLinearColor Tint=FLinearColor(.95f,.64f,.16f,1);
protected:
    TSharedRef<SWidget> RebuildWidget() override;
    int32 NativePaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool)const override;
};
UCLASS()
class RIFTCROWNARENA_API URiftAetherMeter : public UUserWidget
{
    GENERATED_BODY()
public:
    float Bank=5;
protected:
    TSharedRef<SWidget> RebuildWidget() override;
    int32 NativePaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool)const override;
};
UCLASS()
class RIFTCROWNARENA_API URiftMenuBackdrop : public UUserWidget
{
    GENERATED_BODY()
protected:
    TSharedRef<SWidget> RebuildWidget() override;
    int32 NativePaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool)const override;
};
