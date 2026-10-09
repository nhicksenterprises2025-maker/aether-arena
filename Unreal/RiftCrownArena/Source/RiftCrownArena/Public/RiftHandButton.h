#pragma once
#include "RiftUIWidget.h"
#include "RiftHandButton.generated.h"

// Hand cards retain Slate mouse capture until the release, even over the arena.
// Menu buttons continue to use their normal click routing.
UCLASS()
class RIFTCROWNARENA_API URiftHandButton : public URiftActionButton
{
    GENERATED_BODY()
public:
    TFunction<void(FVector2D)> PointerPressed;
    TFunction<void(FVector2D)> PointerMoved;
    TFunction<void(FVector2D)> PointerReleased;
    TFunction<void()> PointerCancelled;
    int32 HandSlot = INDEX_NONE;
protected:
    TSharedRef<SWidget> RebuildWidget() override;
};
