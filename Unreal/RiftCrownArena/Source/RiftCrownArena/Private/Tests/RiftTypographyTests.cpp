#if WITH_DEV_AUTOMATION_TESTS
#include "RiftTypography.h"

#include "Blueprint/WidgetTree.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"
#include "Rendering/SlateRenderer.h"
#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "RiftUIWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftTypographyIntegrationTest,
    "Rift.Integration.Typography",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FRiftTypographyIntegrationTest::RunTest(const FString& Parameters)
{
    if (!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox")))
    {
        AddError(TEXT("Typography UI fixture requires isolated -RiftAutomationSandbox."));
        return false;
    }
    UFont* Typeface=RiftTypography::Typeface();
    if (!TestNotNull(TEXT("The authored UI font asset is loaded; no CoreStyle fallback"),Typeface)) return false;
    TestEqual(TEXT("Runtime caching supports text at each existing UI size"),Typeface->FontCacheType,EFontCacheType::Runtime);
    const auto& Entries=Typeface->GetInternalCompositeFont().DefaultTypeface.Fonts;
    TestEqual(TEXT("Four actual named typeface weights"),Entries.Num(),4);
    const FName Weights[]={TEXT("Regular"),TEXT("Medium"),TEXT("SemiBold"),TEXT("Bold")};
    const int32 ByteCounts[]={164164,163992,168500,170604};
    const TCHAR* FaceHashes[]={TEXT("1c799a4e9d9b673cedd03ef869b0cbd70acfa5d2"),
        TEXT("e74c622b21d0e288f34e94e01ada09c11b77c925"),
        TEXT("db456d4c1c404c9c2315c5f8893a03e299dd6066"),
        TEXT("840cb914b4ba05210a30cadea1ab8cbf4de311ab")};
    for (int32 I=0;I<UE_ARRAY_COUNT(Weights) && I<Entries.Num();++I)
    {
        TestEqual(FString::Printf(TEXT("Weight %d has the correct name"),I),Entries[I].Name,Weights[I]);
        const auto* Face=Cast<UFontFace>(Entries[I].Font.GetFontFaceAsset());
        if (!TestNotNull(FString::Printf(TEXT("%s uses a font-face asset"),*Weights[I].ToString()),Face)) continue;
        TestEqual(TEXT("The face is embedded, independent of system font installation"),Face->GetLoadingPolicy(),EFontLoadingPolicy::Inline);
        TestEqual(TEXT("The complete unmodified TTF data is embedded"),Face->GetFontFaceData()->GetData().Num(),ByteCounts[I]);
        const auto& Bytes=Face->GetFontFaceData()->GetData();
        TestEqual(TEXT("Embedded face bytes match the pinned unmodified upstream font"),
            FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num()).ToString().ToLower(),FString(FaceHashes[I]));
        const auto Font=RiftTypography::Font(Weights[I],14);
        TestTrue(TEXT("Slate FontObject selects the cooked UFont"),Font.FontObject.Get()==Typeface);
        TestEqual(TEXT("Slate selects the requested named weight"),Font.TypefaceFontName,Weights[I]);
        TestEqual(TEXT("Font size is retained"),Font.Size,14.f);
    }
    if (FSlateApplication::IsInitialized())
    {
        const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        const auto Size=Measure->Measure(TEXT("AETHER 10 / 10  ·  03:00"),RiftTypography::Font("Bold",14));
        TestTrue(TEXT("Slate rasterizer can measure timer and aether text"),Size.X>50 && Size.Y>0 && Size.Y<40);
    }

    auto* GI=NewObject<UGameInstance>(GEngine);
    GI->InitializeStandalone(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto* UI=CreateWidget<URiftUIWidget>(GI,URiftUIWidget::StaticClass());
    const auto Slate=UI->TakeWidget();
    int32 TextCount=0,EditCount=0,ComboCount=0;
    TFunction<void(UWidget*)> Verify=[&](UWidget* Widget)
    {
        if (const auto* Text=Cast<UTextBlock>(Widget))
        {++TextCount;TestTrue(TEXT("Every visible text block uses the packaged family"),Text->GetFont().FontObject.Get()==Typeface);}
        if (const auto* Edit=Cast<UEditableTextBox>(Widget))
        {++EditCount;TestTrue(TEXT("Editable text uses the packaged family"),Edit->GetWidgetStyle().TextStyle.Font.FontObject.Get()==Typeface);}
        if (const auto* Combo=Cast<UComboBoxString>(Widget))
        {++ComboCount;TestTrue(TEXT("Selected options and generated dropdown options use the packaged family"),Combo->GetFont().FontObject.Get()==Typeface);}
        if (const auto* Panel=Cast<UPanelWidget>(Widget))
            for (int32 I=0;I<Panel->GetChildrenCount();++I) Verify(Panel->GetChildAt(I));
    };
    for (const auto* Page:{TEXT("Home"),TEXT("Cards"),TEXT("Loadout"),TEXT("Profile"),
                         TEXT("Settings"),TEXT("Help"),TEXT("PatchNotes"),TEXT("Replays"),TEXT("Meta")})
    {
        UI->Navigate(Page);
        Verify(UI->GetRootWidget());
    }
    UI->InspectCard(TEXT("ember_archer"));Verify(UI->GetRootWidget());
    GI->GetWorld()->GetSubsystem<URiftMatchSubsystem>()->StartMatch(true);
    UI->SetBattleView();Verify(UI->GetRootWidget());
    TestTrue(TEXT("Menu, collection and battle labels were checked"),TextCount>50);
    TestTrue(TEXT("Editable controls were checked"),EditCount>0);
    TestTrue(TEXT("Dropdown controls were checked"),ComboCount>0);
    GI->GetWorld()->GetSubsystem<URiftMatchSubsystem>()->LeaveMatch();
    UI->RemoveFromParent();
    GI->Shutdown();
    return true;
}
#endif
