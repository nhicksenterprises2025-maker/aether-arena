#include "RiftTypography.h"

#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "RiftDiagnostics.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"

UFont* RiftTypography::Typeface()
{
    // Keep one strong reference for Slate-only annotations as well as UMG.
    // Retry a missing asset so an editor font import can complete in-session.
    static TStrongObjectPtr<UFont> Typeface;
    if (!Typeface.IsValid())
    {
        Typeface.Reset(LoadObject<UFont>(nullptr, TEXT("/Game/Rift/Fonts/F_RiftUI.F_RiftUI")));
        if (Typeface.IsValid())
        {
            const auto& Entries=Typeface->GetInternalCompositeFont().DefaultTypeface.Fonts;
            int32 InlineFaces=0;
            for (const auto& Entry:Entries)
                if (const auto* Face=Cast<UFontFace>(Entry.Font.GetFontFaceAsset());
                    Face && Face->GetLoadingPolicy()==EFontLoadingPolicy::Inline &&
                    Face->GetFontFaceData()->GetData().Num()>0) ++InlineFaces;
            FRiftDiagnostics::Write(TEXT("Log"),FString::Printf(
                TEXT("UI_TYPEFACE family=Barlow Semi Condensed font=%s faces=%d inline=%d"),
                *Typeface->GetPathName(),Entries.Num(),InlineFaces));
        }
    }
    return Typeface.Get();
}

FSlateFontInfo RiftTypography::Font(FName Weight, float Size)
{
    if (UFont* Face = Typeface())
        return FSlateFontInfo(Face, Size, Weight);
    // An incomplete editor import remains usable. Shipping QA requires the
    // cooked Barlow asset and verifies its faces; this fallback is not a pass.
    return FCoreStyle::GetDefaultFontStyle(Weight, Size);
}

FSlateFontInfo RiftTypography::Font(const ANSICHAR* Weight, float Size)
{
    return Font(FName(Weight), Size);
}
