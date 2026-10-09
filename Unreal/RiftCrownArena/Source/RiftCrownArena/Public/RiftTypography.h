#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

class UFont;

/** Shared, cooked UI typeface. Font sizes are the existing UI sizes. */
namespace RiftTypography
{
    RIFTCROWNARENA_API UFont* Typeface();
    RIFTCROWNARENA_API FSlateFontInfo Font(FName Weight, float Size);
    RIFTCROWNARENA_API FSlateFontInfo Font(const ANSICHAR* Weight, float Size);
}
