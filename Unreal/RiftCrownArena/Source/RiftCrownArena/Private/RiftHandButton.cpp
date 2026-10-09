#include "RiftHandButton.h"
#include "Components/ButtonSlot.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Input/Events.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SViewport.h"

namespace
{
class SRiftHandButton final : public SButton
{
public:
    SLATE_BEGIN_ARGS(SRiftHandButton) {}
        SLATE_ARGUMENT(URiftHandButton*, Owner)
        SLATE_ARGUMENT(SButton::FArguments, ButtonArgs)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Owner = Args._Owner;
        SButton::Construct(Args._ButtonArgs);
    }
    bool IsPointerTracking() const { return bPointerTracking; }
    FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (bPointerTracking && Event.GetEffectingButton() == EKeys::RightMouseButton)
        {
            CancelPointer();
            return FReply::Handled().ReleaseMouseCapture();
        }
        if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
            return SButton::OnMouseButtonDown(Geometry, Event);
        FReply Reply = SButton::OnMouseButtonDown(Geometry, Event);
        if (Reply.IsEventHandled() && Owner.IsValid())
        {
            bPointerTracking = true;
            if (Owner->PointerPressed) Owner->PointerPressed(Event.GetScreenSpacePosition());
            // The hand is painted over, rather than inside, SViewport. Keep its
            // keyboard focus explicitly so Escape and 1–4 reach battle input.
            if (auto* World = Owner->GetWorld())
                if (auto* Viewport = World->GetGameViewport())
                    if (Viewport->GetGameViewportWidget().IsValid())
                        Reply.SetUserFocus(Viewport->GetGameViewportWidget().ToSharedRef(), EFocusCause::Mouse);
        }
        return Reply;
    }
    FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (bPointerTracking && Owner.IsValid())
        {
            if (Owner->PointerMoved) Owner->PointerMoved(Event.GetScreenSpacePosition());
            return FReply::Handled();
        }
        return SButton::OnMouseMove(Geometry, Event);
    }
    FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (!bPointerTracking || Event.GetEffectingButton() != EKeys::LeftMouseButton)
            return SButton::OnMouseButtonUp(Geometry, Event);
        // A normal SButton click must not select the old slot again after a drop
        // has spent Aether and cycled the hand. Keyboard clicks still select it.
        bFinishingPointer = true;
        FReply Reply = SButton::OnMouseButtonUp(Geometry, Event);
        if (Owner.IsValid() && Owner->PointerReleased)
            Owner->PointerReleased(Event.GetScreenSpacePosition());
        bPointerTracking = false;
        bFinishingPointer = false;
        return Reply.ReleaseMouseCapture();
    }
    void OnMouseCaptureLost(const FCaptureLostEvent& Event) override
    {
        SButton::OnMouseCaptureLost(Event);
        if (!bFinishingPointer) CancelPointer();
    }
    FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override
    {
        if (bPointerTracking && Event.GetKey() == EKeys::Escape)
        {
            CancelPointer();
            return FReply::Handled().ReleaseMouseCapture();
        }
        return SButton::OnKeyDown(Geometry, Event);
    }
private:
    void CancelPointer()
    {
        if (!bPointerTracking) return;
        SButton::Release();
        bPointerTracking = false;
        if (Owner.IsValid() && Owner->PointerCancelled) Owner->PointerCancelled();
    }
    TWeakObjectPtr<URiftHandButton> Owner;
    bool bPointerTracking = false;
    bool bFinishingPointer = false;
};
}

TSharedRef<SWidget> URiftHandButton::RebuildWidget()
{
    TSharedRef<SRiftHandButton> Hand = SNew(SRiftHandButton)
        .Owner(this)
        .ButtonArgs(SButton::FArguments()
            .OnClicked_Lambda([this]()
            {
                const auto HandButton = StaticCastSharedPtr<SRiftHandButton>(MyButton);
                return HandButton.IsValid() && HandButton->IsPointerTracking()
                    ? FReply::Handled() : SlateHandleClicked();
            })
            .OnPressed(BIND_UOBJECT_DELEGATE(FSimpleDelegate, SlateHandlePressed))
            .OnReleased(BIND_UOBJECT_DELEGATE(FSimpleDelegate, SlateHandleReleased))
            .OnHovered_UObject(this, &URiftHandButton::SlateHandleHovered)
            .OnUnhovered_UObject(this, &URiftHandButton::SlateHandleUnhovered)
            .ButtonStyle(&GetStyle())
            .ClickMethod(EButtonClickMethod::DownAndUp)
            .TouchMethod(EButtonTouchMethod::DownAndUp)
            .PressMethod(GetPressMethod())
            .IsFocusable(false));
    MyButton = Hand;
    if (GetChildrenCount() > 0)
        CastChecked<UButtonSlot>(GetContentSlot())->BuildSlot(MyButton.ToSharedRef());
    return Hand;
}
