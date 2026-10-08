#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/WidgetTree.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "HAL/PlatformFileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "RiftCardData.h"
#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "RiftReplaySubsystem.h"
#include "RiftUIWidget.h"
#include "Serialization/JsonSerializer.h"
#include <limits>

namespace {
constexpr EAutomationTestFlags Flags =
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
TSharedPtr<FJsonObject> Json(const FString &Text) {
    TSharedPtr<FJsonObject> O;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), O);
    return O;
}
FString Serialize(const TSharedRef<FJsonObject> &O) {
    FString Text;
    FJsonSerializer::Serialize(O, TJsonWriterFactory<>::Create(&Text));
    return Text;
}
TSharedRef<FJsonObject> Clone(const TSharedRef<FJsonObject> &O) {
    return Json(Serialize(O)).ToSharedRef();
}
FString Fixture(const TCHAR *Name) {
    return FPaths::Combine(URiftProfileSubsystem::SaveRoot(), TEXT("AutomationFixtures"),
                           FGuid::NewGuid().ToString(EGuidFormats::Digits) + Name);
}
bool Sandbox() {
    return FParse::Param(FCommandLine::Get(), TEXT("RiftAutomationSandbox"));
}
template <class T> TArray<T *> ActiveWidgets(UWidget *Root) {
    TArray<T *> Found;
    if (auto *Value = Cast<T>(Root))
        Found.Add(Value);
    if (auto *Panel = Cast<UPanelWidget>(Root))
        for (int32 I = 0; I < Panel->GetChildrenCount(); ++I)
            Found.Append(ActiveWidgets<T>(Panel->GetChildAt(I)));
    return Found;
}
URiftActionButton *ActiveButton(URiftUIWidget *UI, const FString &Label, bool Last = false) {
    URiftActionButton *Found = nullptr;
    for (auto *Button : ActiveWidgets<URiftActionButton>(UI->GetRootWidget()))
        for (auto *Text : ActiveWidgets<UTextBlock>(Button))
            if (Text->GetText().ToString() == Label) {
                Found = Button;
                if (!Last)
                    return Found;
                break;
            }
    return Found;
}
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftConnectedUIIntegrationTest, "Rift.Integration.ConnectedUI", Flags)
bool FRiftConnectedUIIntegrationTest::RunTest(const FString &Parameters) {
    if (!Sandbox()) {
        AddError(TEXT(
            "Connected UI fixture requires isolated -RiftSaveRoot/-UserDir and -RiftAutomationSandbox."));
        return false;
    }
    auto *UISettings = GetMutableDefault<UUserInterfaceSettings>();
    const float OriginalApplicationScale = UISettings->ApplicationScale;
    const FIntPoint DPISizes[] = {{1280, 720}, {1600, 900}, {1920, 1080}, {3440, 1440}};
    TArray<float> BaseDPIScales;
    UISettings->ApplicationScale = 1.f;
    for (const auto Size : DPISizes)
        BaseDPIScales.Add(UISettings->GetDPIScaleBasedOnSize(Size));
    UISettings->ApplicationScale = OriginalApplicationScale;
    auto *GI = NewObject<UGameInstance>(GEngine);
    GI->InitializeStandalone(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto *World = GI->GetWorld();
    auto *Profile = GI->GetSubsystem<URiftProfileSubsystem>();
    auto *Replay = GI->GetSubsystem<URiftReplaySubsystem>();
    auto *Match = World->GetSubsystem<URiftMatchSubsystem>();
    auto *UI = CreateWidget<URiftUIWidget>(GI, URiftUIWidget::StaticClass());
    TSharedPtr<SWidget> Slate = UI->TakeWidget();
    UWidget *Canvas = UI->GetRootWidget();
    const FRiftSettings OriginalSettings = Profile->Settings;
    const int32 OriginalReplays = Profile->ReplayFiles.Num();
    auto Shutdown = [](UGameInstance *Instance, URiftUIWidget *Widget) {
        if (Widget)
            Widget->ReleaseSlateResources(true);
        auto *InstanceWorld = Instance->GetWorld();
        if (auto *InstanceMatch = InstanceWorld->GetSubsystem<URiftMatchSubsystem>())
            InstanceMatch->LeaveMatch();
        Instance->GetSubsystem<URiftReplaySubsystem>()->FlushPendingWrites();
        InstanceWorld->DestroyWorld(false);
        Instance->Shutdown();
        GEngine->DestroyWorldContext(InstanceWorld);
    };
    ON_SCOPE_EXIT {
        if (GI)
            Shutdown(GI, UI);
        UISettings->ApplicationScale = OriginalApplicationScale;
    };
    auto Click = [&](const TCHAR *Label, bool Last = false) {
        auto *Button = ActiveButton(UI, Label, Last);
        if (!TestNotNull(FString(TEXT("Active production button ")) + Label, Button))
            return false;
        Button->OnClicked.Broadcast();
        TestTrue(TEXT("Navigation retains the live UMG canvas and cached Slate root"),
                 UI->GetRootWidget() == Canvas && UI->TakeWidget() == Slate.ToSharedRef());
        return true;
    };
    if (!Click(TEXT("ENTER BATTLE")))
        return false;
    TestTrue(TEXT("Home Enter Battle callback starts the actual paid match"),
             Match->IsActive() && !Match->IsTraining() && UI->IsBattleView());
    const std::string Played = Match->ViewState()->hands[0][0];
    const std::string NextCard = Match->ViewState()->queues[0].front();
    const double InitialBank = Match->ViewState()->aether[0];
    TArray<URiftActionButton *> Hand;
    for (auto *Button : ActiveWidgets<URiftActionButton>(Canvas))
        if (Button->OnPressed.IsBound() && !Button->OnClicked.IsBound())
            Hand.Add(Button);
    if (!TestEqual(TEXT("Exactly four live hand controls"), Hand.Num(), 4))
        return false;
    Hand[0]->OnPressed.Broadcast();
    TestEqual(TEXT("Hand press selects its production deployment slot"), UI->SelectedHand(), 0);
    UI->WorldClicked({0, 8.5});
    TestTrue(TEXT("Tile deployment pays the exact canonical cost and cycles the deck"),
             Match->ViewState()->aether[0] == InitialBank - rift::FindCard(Played)->cost &&
                 Match->ViewState()->hands[0][0] == NextCard && UI->SelectedHand() == -1);
    TestEqual(TEXT("Paid UI deployment records one card play"),
              int32(Match->ViewState()->telemetry[0].at(Played).plays), 1);
    if (!Click(TEXT("DEV")) || !Click(TEXT("PAUSE")))
        return false;
    TestEqual(TEXT("Developer pause reaches the actual match clock"), Match->GetSpeed(), 0.f);
    const double PausedTime = Match->ViewState()->elapsed;
    Match->Tick(.5f);
    TestEqual(TEXT("Paused game tick leaves simulation time unchanged"), Match->ViewState()->elapsed,
              PausedTime);
    if (!Click(TEXT("MAX")))
        return false;
    TestEqual(TEXT("Friendly MAX callback reaches the canonical capped bank"), Match->GetAether(0), 10.f);
    if (!Click(TEXT("−1")))
        return false;
    TestEqual(TEXT("Friendly decrement callback edits only the selected bank"), Match->GetAether(0), 9.f);
    const auto DeveloperCombos = ActiveWidgets<UComboBoxString>(Canvas);
    UComboBoxString *TowerChoice = nullptr;
    for (auto *Combo : DeveloperCombos)
        if (Combo->GetOptionAtIndex(0).Contains(TEXT("Friendly Core")))
            TowerChoice = Combo;
    const auto Edits = ActiveWidgets<UEditableTextBox>(Canvas);
    if (!TestNotNull(TEXT("Actual Developer tower selector"), TowerChoice) ||
        !TestEqual(TEXT("Actual Developer HP field"), Edits.Num(), 1))
        return false;
    const uint64 TowerId = uint64(FCString::Atoi64(*TowerChoice->GetSelectedOption()));
    Edits[0]->SetText(FText::FromString(TEXT("500")));
    if (!Click(TEXT("SET TOWER HP")))
        return false;
    bool TowerEdited = false;
    for (const auto &Entity : Match->ViewState()->entities)
        if (Entity.id == TowerId)
            TowerEdited = Entity.hp == 500;
    TestTrue(TEXT("Tower editor callback changes the selected live Core HP"), TowerEdited);
    if (!Click(TEXT("HOME")))
        return false;
    TestFalse(TEXT("Home leaves the actual live match"), Match->IsActive());
    TestTrue(TEXT("Home asynchronously finalizes its complete replay"), Replay->FlushPendingWrites());
    TestEqual(TEXT("Home indexes its recording exactly once"), Profile->ReplayFiles.Num(),
              OriginalReplays + 1);
    if (!Click(TEXT("Home")))
        return false;
    Replay->FlushPendingWrites();
    TestEqual(TEXT("Repeated Home navigation cannot duplicate a recording"), Profile->ReplayFiles.Num(),
              OriginalReplays + 1);
    if (!Click(TEXT("Loadout")) || !Click(*Profile->Presets[1].Name))
        return false;
    const auto PreviousDeck = Profile->Presets[1].Cards;
    FString Removed = PreviousDeck[0], Added;
    for (const auto &Card : rift::Cards()) {
        const FString Id = UTF8_TO_TCHAR(Card.id.c_str());
        if (!PreviousDeck.Contains(Id)) {
            Added = Id;
            break;
        }
    }
    const FString RemovedName = UTF8_TO_TCHAR(rift::FindCard(TCHAR_TO_UTF8(*Removed))->name.c_str());
    const FString AddedName = UTF8_TO_TCHAR(rift::FindCard(TCHAR_TO_UTF8(*Added))->name.c_str());
    if (!Click(*RemovedName) || !Click(TEXT("SAVE & SELECT")))
        return false;
    TestTrue(TEXT("Seven-card UI draft cannot overwrite the saved eight-card preset"),
             Profile->Presets[1].Cards == PreviousDeck);
    if (!Click(*AddedName, true))
        return false;
    const auto NameFields = ActiveWidgets<UEditableTextBox>(Canvas);
    if (!TestEqual(TEXT("Loadout exposes its actual name field"), NameFields.Num(), 1))
        return false;
    NameFields[0]->SetText(FText::FromString(TEXT("Connected UI Deck")));
    if (!Click(TEXT("SAVE & SELECT")))
        return false;
    TestTrue(TEXT("Save and Select persists eight unique cards with the requested replacement"),
             Profile->ActivePreset == Profile->Presets[1].Id &&
                 Profile->Presets[1].Name == TEXT("Connected UI Deck") &&
                 URiftProfileSubsystem::ValidDeck(Profile->ActiveDeck()) &&
                 Profile->ActiveDeck().Contains(Added) && !Profile->ActiveDeck().Contains(Removed));
    const auto SavedDeck = Profile->ActiveDeck();
    if (!Click(TEXT("Home")) || !Click(TEXT("TRAINING / DEVELOPER LAB")))
        return false;
    TestTrue(TEXT("Training callback starts the selected real deck with developer controls"),
             Match->IsTraining() && Match->IsActive() && ActiveButton(UI, TEXT("PAUSE")) &&
                 Match->ViewState()->decks[0][0] == TCHAR_TO_UTF8(*SavedDeck[0]));
    if (!Click(TEXT("HOME")))
        return false;
    TestTrue(TEXT("Training Home finalization succeeds"), Replay->FlushPendingWrites());
    TestEqual(TEXT("Battle and Training each archive once"), Profile->ReplayFiles.Num(), OriginalReplays + 2);
    if (!Click(TEXT("Settings")))
        return false;
    const auto Combos = ActiveWidgets<UComboBoxString>(Canvas);
    const auto Sliders = ActiveWidgets<URiftValueSlider>(Canvas);
    if (!TestEqual(TEXT("All twelve actual display/quality/deployment selectors are connected"), Combos.Num(),
                   12) ||
        !TestEqual(TEXT("All six actual audio/UI/camera sliders are connected"), Sliders.Num(), 6))
        return false;
    Combos[0]->SetSelectedOption(TEXT("Windowed"));
    Combos[1]->SetSelectedOption(TEXT("1600x900"));
    Combos[2]->SetSelectedOption(TEXT("VSync OFF"));
    Combos[3]->SetSelectedOption(TEXT("90"));
    const int32 Qualities[] = {0, 1, 2, 3, 1, 0};
    for (int32 I = 0; I < 6; ++I)
        Combos[I + 4]->SetSelectedIndex(Qualities[I]);
    Combos[10]->SetSelectedIndex(1);
    Combos[11]->SetSelectedIndex(1);
    const float Values[] = {.61f, .22f, .73f, .34f, 1.13f, 1.47f};
    for (int32 I = 0; I < 6; ++I) {
        Sliders[I]->SetValue(Values[I]);
        Sliders[I]->OnValueChanged.Broadcast(Values[I]);
    }
    if (!Click(TEXT("APPLY & SAVE SETTINGS")))
        return false;
    auto CheckGameDPIScale = [&](float Factor) {
        for (int32 I = 0; I < UE_ARRAY_COUNT(DPISizes); ++I) {
            const float Actual = UISettings->GetDPIScaleBasedOnSize(DPISizes[I]);
            TestTrue(FString::Printf(TEXT("Production UI scale %.2f multiplies the preserved DPI curve at %dx%d"),
                                     Factor, DPISizes[I].X, DPISizes[I].Y),
                     FMath::IsNearlyEqual(Actual, BaseDPIScales[I] * Factor, 1e-5f));
        }
    };
    CheckGameDPIScale(1.13f);
    // Exercise the actual minimum/maximum slider callbacks and return to the
    // persisted value. This would fail when only Slate window scaling is changed.
    for (const float Factor : {.65f, 1.6f, 1.13f}) {
        const auto CurrentSliders = ActiveWidgets<URiftValueSlider>(Canvas);
        if (!TestEqual(TEXT("The rebuilt Settings panel retains all six live sliders"),
                       CurrentSliders.Num(), 6))
            return false;
        CurrentSliders[4]->SetValue(Factor);
        CurrentSliders[4]->OnValueChanged.Broadcast(Factor);
        if (!Click(TEXT("APPLY & SAVE SETTINGS")))
            return false;
        CheckGameDPIScale(Factor);
    }
    const auto SavedSettings = Profile->Settings;
    TestTrue(TEXT("Production Apply saves the selected viewport, VSync, cap and deployment mode"),
             SavedSettings.Width == 1600 && SavedSettings.Height == 900 && SavedSettings.WindowMode == 2 &&
                 !SavedSettings.VSync && SavedSettings.FrameCap == 90 && !SavedSettings.DragDeploy &&
                 SavedSettings.ConfirmDeploy);
    const int32 SavedQualities[] = {SavedSettings.AA,      SavedSettings.Shadows,
                                    SavedSettings.Effects, SavedSettings.Textures,
                                    SavedSettings.Post,    SavedSettings.ViewDistance};
    const float SavedValues[] = {SavedSettings.MasterVolume, SavedSettings.MusicVolume,
                                 SavedSettings.SFXVolume,    SavedSettings.UIVolume,
                                 SavedSettings.UIScale,      SavedSettings.CameraSpeed};
    for (int32 I = 0; I < 6; ++I) {
        TestEqual(FString::Printf(TEXT("Production graphics selector %d persists"), I), SavedQualities[I],
                  Qualities[I]);
        TestTrue(FString::Printf(TEXT("Production slider %d persists"), I),
                 FMath::IsNearlyEqual(SavedValues[I], Values[I]));
    }
    Shutdown(GI, UI);
    GI = nullptr;
    UI = nullptr;
    Slate.Reset();
    GI = NewObject<UGameInstance>(GEngine);
    GI->InitializeStandalone(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    Profile = GI->GetSubsystem<URiftProfileSubsystem>();
    TestTrue(TEXT("A fresh actual GameInstance restores the UI-selected deck and both archived matches"),
             Profile->ActiveDeck() == SavedDeck && Profile->Presets[1].Name == TEXT("Connected UI Deck") &&
                 Profile->ReplayFiles.Num() == OriginalReplays + 2);
    const auto &Reloaded = Profile->Settings;
    TestTrue(
        TEXT("A fresh actual GameInstance restores every saved control value"),
        Reloaded.Width == 1600 && Reloaded.Height == 900 && Reloaded.WindowMode == 2 && !Reloaded.VSync &&
            Reloaded.FrameCap == 90 && !Reloaded.DragDeploy && Reloaded.ConfirmDeploy && Reloaded.AA == 0 &&
            Reloaded.Shadows == 1 && Reloaded.Effects == 2 && Reloaded.Textures == 3 && Reloaded.Post == 1 &&
            Reloaded.ViewDistance == 0 && FMath::IsNearlyEqual(Reloaded.MasterVolume, .61f) &&
            FMath::IsNearlyEqual(Reloaded.MusicVolume, .22f) &&
            FMath::IsNearlyEqual(Reloaded.SFXVolume, .73f) && FMath::IsNearlyEqual(Reloaded.UIVolume, .34f) &&
            FMath::IsNearlyEqual(Reloaded.UIScale, 1.13f) &&
            FMath::IsNearlyEqual(Reloaded.CameraSpeed, 1.47f));
    CheckGameDPIScale(1.13f);
    Profile->Settings = OriginalSettings;
    Profile->ApplySettings();
    TestTrue(TEXT("Fixture restores its isolated engine settings"), Profile->Save());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftProfilePersistenceTest, "Rift.Integration.ProfilePersistence", Flags)
bool FRiftProfilePersistenceTest::RunTest(const FString &Parameters) {
    if (!Sandbox()) {
        AddError(
            TEXT("Use -RiftAutomationSandbox and -RiftSaveRoot=<isolated directory> for persistence tests."));
        return false;
    }
    auto *GI = NewObject<UGameInstance>();
    auto *P = NewObject<URiftProfileSubsystem>(GI);
    P->Defaults();
    FString Error;
    const FString File = Fixture(TEXT("-atomic.json"));
    TestTrue(TEXT("First atomic write"), URiftProfileSubsystem::AtomicWrite(File, TEXT("first"), Error));
    TestTrue(TEXT("Second atomic replacement"),
             URiftProfileSubsystem::AtomicWrite(File, TEXT("second"), Error));
    FString Read;
    FFileHelper::LoadFileToString(Read, *File);
    TestEqual(TEXT("Committed contents"), Read, FString(TEXT("second")));
    FFileHelper::LoadFileToString(Read, *(File + TEXT(".bak")));
    TestEqual(TEXT("Previous version backup"), Read, FString(TEXT("first")));
    TestTrue(TEXT("Abandoned staging files do not block next write"),
             URiftProfileSubsystem::AtomicWrite(File, TEXT("third"), Error));
    const FString Browser = Fixture(TEXT("-browser.json"));
    auto Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("extensionRoot"), TEXT("preserved"));
    auto Profile = MakeShared<FJsonObject>();
    Profile->SetStringField(TEXT("username"), TEXT("  Noah<>   Arena  "));
    Profile->SetStringField(TEXT("playerId"), TEXT("RC-EXISTING"));
    Profile->SetNumberField(TEXT("wins"), 7);
    Profile->SetNumberField(TEXT("losses"), 3);
    Profile->SetNumberField(TEXT("draws"), 2);
    Profile->SetNumberField(TEXT("crowns"), 16);
    Profile->SetStringField(TEXT("extensionProfile"), TEXT("preserved"));
    Root->SetObjectField(TEXT("profile"), Profile);
    auto Decks = MakeShared<FJsonObject>();
    Decks->SetStringField(TEXT("activePresetId"), TEXT("deck-2"));
    Decks->SetStringField(TEXT("extensionDecks"), TEXT("preserved"));
    TArray<TSharedPtr<FJsonValue>> Records;
    for (int I = 0; I < 2; ++I) {
        auto Deck = MakeShared<FJsonObject>();
        Deck->SetStringField(TEXT("id"), TEXT("deck-2"));
        Deck->SetStringField(TEXT("name"),
                             I == 0 ? TEXT(" First <deck> ") : TEXT("Duplicate must not replace"));
        Deck->SetStringField(TEXT("extensionPreset"), TEXT("preserved"));
        TArray<TSharedPtr<FJsonValue>> Cards;
        for (const auto &Id : URiftProfileSubsystem::DefaultDeck())
            Cards.Add(MakeShared<FJsonValueString>(Id));
        Deck->SetArrayField(TEXT("cards"), Cards);
        Records.Add(MakeShared<FJsonValueObject>(Deck));
    }
    Records.Add(MakeShared<FJsonValueNumber>(42));
    Decks->SetArrayField(TEXT("presets"), Records);
    Root->SetObjectField(TEXT("deckPresets"), Decks);
    auto Settings = MakeShared<FJsonObject>();
    Settings->SetStringField(TEXT("extensionSettings"), TEXT("preserved"));
    Root->SetObjectField(TEXT("settings"), Settings);
    TestTrue(TEXT("Write browser fixture"),
             URiftProfileSubsystem::AtomicWrite(Browser, Serialize(Root), Error));
    TestTrue(TEXT("Browser migration"), P->LoadFile(Browser, true));
    TestEqual(TEXT("Sanitized name"), P->Username, FString(TEXT("Noah Arena")));
    TestEqual(TEXT("Existing player ID"), P->PlayerId, FString(TEXT("RC-EXISTING")));
    TestEqual(TEXT("Missing matches derive history"), P->Matches, 12);
    TestEqual(TEXT("First duplicate preset wins"), P->Presets[1].Name, FString(TEXT("First deck")));
    TestEqual(TEXT("Five presets retained"), P->Presets.Num(), 5);
    TestTrue(TEXT("Original checksum captured"), !P->ImportedChecksum.IsEmpty());
    P->Settings.MasterVolume = std::numeric_limits<float>::quiet_NaN();
    P->Settings.Width = -10;
    P->Settings.AA = 99;
    TestTrue(TEXT("Migrated native save"), P->Save());
    const FString Native = FPaths::Combine(URiftProfileSubsystem::SaveRoot(), TEXT("ue_save.json"));
    FFileHelper::LoadFileToString(Read, *Native);
    auto Saved = Json(Read);
    TestNotNull(TEXT("Native JSON"), Saved.Get());
    if (Saved) {
        TestEqual(TEXT("Unknown root field preserved"), Saved->GetStringField(TEXT("extensionRoot")),
                  FString(TEXT("preserved")));
        TestEqual(TEXT("Unknown profile field preserved"),
                  Saved->GetObjectField(TEXT("profile"))->GetStringField(TEXT("extensionProfile")),
                  FString(TEXT("preserved")));
        TestEqual(TEXT("Unknown settings field preserved"),
                  Saved->GetObjectField(TEXT("settings"))->GetStringField(TEXT("extensionSettings")),
                  FString(TEXT("preserved")));
    }
    auto *Reload = NewObject<URiftProfileSubsystem>(GI);
    Reload->Defaults();
    TestTrue(TEXT("Reload native settings"), Reload->LoadFile(Native, false));
    TestEqual(TEXT("Nonfinite volume normalized"), Reload->Settings.MasterVolume, .8f);
    TestEqual(TEXT("Resolution normalized"), Reload->Settings.Width, 800);
    TestEqual(TEXT("Scalability normalized"), Reload->Settings.AA, 3);
    TestEqual(TEXT("History persists"), Reload->Wins, 7);
    const FString Broken = Fixture(TEXT("-broken.json"));
    URiftProfileSubsystem::AtomicWrite(Broken, TEXT("{broken"), Error);
    TestFalse(TEXT("Corrupt JSON rejected"), Reload->LoadFile(Broken, false));
    TestEqual(TEXT("Corrupt load preserves current history"), Reload->Wins, 7);
    const FString Future = Fixture(TEXT("-future.json"));
    URiftProfileSubsystem::AtomicWrite(
        Future, TEXT("{\"schemaVersion\":99,\"profile\":{\"username\":\"future\"}}"), Error);
    TestFalse(TEXT("Future schema rejected"), Reload->LoadFile(Future, false));
    TestFalse(TEXT("Future schema blocks edits"), Reload->SetName(TEXT("Overwrite")));
    TestFalse(TEXT("Future schema blocks saves"), Reload->Save());
    TestEqual(TEXT("Future read cannot mutate current name"), Reload->Username, FString(TEXT("Noah Arena")));
    // Exercise every reflected setting spelling through the actual production converter.
    P->Settings.MusicVolume = .21f;
    P->Settings.SFXVolume = .31f;
    P->Settings.UIVolume = .41f;
    P->Settings.UIScale = 1.25f;
    P->Settings.VSync = false;
    P->ReplayFiles = {TEXT("legacy.json"), TEXT("../Replays/native.riftreplay")};
    TestTrue(TEXT("Settings save"), P->Save());
    Reload->Defaults();
    TestTrue(TEXT("Settings reload"), Reload->LoadFile(Native, false));
    TestEqual(TEXT("SFX acronym spelling roundtrip"), Reload->Settings.SFXVolume, .31f);
    TestEqual(TEXT("UI acronym spelling roundtrip"), Reload->Settings.UIVolume, .41f);
    TestEqual(TEXT("UI scale roundtrip"), Reload->Settings.UIScale, 1.25f);
    TestFalse(TEXT("VSync roundtrip"), Reload->Settings.VSync);
    TestTrue(TEXT("Legacy and native replay filenames survive safe profile normalization"),
             Reload->ReplayFiles == TArray<FString>{TEXT("legacy.json"), TEXT("native.riftreplay")});
    FFileHelper::LoadFileToString(Read, *Native);
    TestTrue(TEXT("Prepare known good recovery backup"),
             FFileHelper::SaveStringToFile(Read, *(Native + TEXT(".bak"))));
    TestTrue(TEXT("Corrupt primary fixture"), FFileHelper::SaveStringToFile(TEXT("{truncated"), *Native));
    Reload->Defaults();
    Reload->LoadStoredProfile();
    TestEqual(TEXT("Corrupt primary recovers backup history"), Reload->Wins, 7);
    FString Backup;
    FFileHelper::LoadFileToString(Backup, *(Native + TEXT(".bak")));
    TestEqual(TEXT("Recovery keeps known-good backup intact"), Backup, Read);
    TArray<FString> Quarantined;
    IFileManager::Get().FindFiles(
        Quarantined,
        *FPaths::Combine(URiftProfileSubsystem::SaveRoot(), TEXT("Backups/ue_save.corrupt-*.json")), true,
        false);
    TestTrue(TEXT("Corrupt original is quarantined"), !Quarantined.IsEmpty());
    FFileHelper::SaveStringToFile(TEXT("{broken-primary"), *Native);
    const FString FutureBackup = TEXT("{\"schemaVersion\":99,\"profile\":{\"username\":\"future\"}}");
    FFileHelper::SaveStringToFile(FutureBackup, *(Native + TEXT(".bak")));
    Reload->Defaults();
    Reload->LoadStoredProfile();
    TestTrue(TEXT("Future backup prevents fallback overwrite"), Reload->bReadOnlyFutureSchema);
    FFileHelper::LoadFileToString(Backup, *(Native + TEXT(".bak")));
    TestEqual(TEXT("Future backup preserved byte-for-byte"), Backup, FutureBackup);
    FFileHelper::LoadFileToString(Backup, *Native);
    TestEqual(TEXT("Future recovery leaves original primary untouched"), Backup,
              FString(TEXT("{broken-primary")));
    // Restore a valid isolated test save for other automation tests and subsequent shutdown.
    TestTrue(TEXT("Restore isolated profile fixture"), P->Save());
    const FString Unicode = Fixture(TEXT("-unicode.json"));
    const FString UnicodeName = TEXT("Noah \u00E9\u540D\u5B57");
    auto UnicodeRoot = MakeShared<FJsonObject>();
    UnicodeRoot->SetNumberField(TEXT("schemaVersion"), 1);
    auto UnicodeProfile = MakeShared<FJsonObject>();
    UnicodeProfile->SetStringField(TEXT("username"), UnicodeName);
    UnicodeRoot->SetObjectField(TEXT("profile"), UnicodeProfile);
    URiftProfileSubsystem::AtomicWrite(Unicode, Serialize(UnicodeRoot), Error);
    Reload->Defaults();
    TestTrue(TEXT("Unicode profile import"), Reload->LoadFile(Unicode, false));
    TestEqual(TEXT("Existing Unicode username preserved"), Reload->Username, UnicodeName);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftSnapshotRoundtripTest, "Rift.Integration.SnapshotRoundtrip", Flags)
bool FRiftSnapshotRoundtripTest::RunTest(const FString &Parameters) {
    rift::MatchOptions Options;
    Options.aiEnabled = {false, false};
    rift::Match Match(Options);
    Match.Spawn(rift::Team::Player, "frost_fang", {1, 5});
    Match.Spawn(rift::Team::Enemy, "ironclad", {1, 4});
    Match.Step(.4);
    auto State = Match.State();
    auto &E = State.entities.back();
    E.forcedTarget = 1;
    E.forcedUntil = 4;
    E.slowSource = 88;
    E.stunSource = 99;
    E.slowTrackedFrom = .1;
    E.stunTrackedFrom = .2;
    E.pathTarget = {4, 7};
    E.stuckPosition = {2, 3};
    E.stuckClock = .3;
    E.stuckDistance = 11;
    E.scanClock = .2;
    E.repathClock = .1;
    E.stuckTime = .4;
    E.path = {{1, 2}, {3, 4}};
    State.ai[0].phase = "support";
    State.ai[0].think = .21;
    State.ai[0].lastPunish = -3;
    State.ai[0].lastSpell = 4;
    State.ai[0].anchor = 7;
    State.ai[0].supports = 2;
    State.ai[0].observedCycle = {"ironclad", "frost_fang"};
    auto &T = State.telemetry[0]["meteor_shards"];
    T.openingEligible = 1;
    T.plays = 2;
    T.spawns = 5;
    T.buildingLifetime = 4;
    T.buildingCapacity = 25;
    T.slowTime = .5;
    T.slowTrackedSeconds = 3;
    T.stunTime = .2;
    T.stunTrackedSeconds = 2;
    T.zoneOccupancy = 6;
    T.zoneSeconds = 5;
    T.placementZ = -8;
    T.auraDamage = 82;
    T.dotTicks = 5;
    rift::Projectile P;
    P.id = 99;
    P.source = 1;
    P.target = 2;
    P.playId = 7;
    P.cardId = "arc_mage";
    P.damage = 88;
    P.splash = 1.2;
    P.duration = .7;
    P.remaining = .3;
    P.origin = {1, 2};
    State.projectiles.push_back(P);
    rift::Hazard H;
    H.playId = 9;
    H.cardId = "meteor_shards";
    H.radius = 2.7;
    H.born = .2;
    H.nextTick = 1.2;
    H.expires = 5.2;
    H.ticks = 2;
    H.position = {-3, 4};
    State.hazards.push_back(H);
    auto Original = URiftReplaySubsystem::SnapshotJSON(State);
    rift::Snapshot Parsed;
    TestTrue(TEXT("Full snapshot parses"), URiftReplaySubsystem::SnapshotFromJSON(Original, Parsed));
    TestEqual(TEXT("All snapshot fields roundtrip"), Serialize(URiftReplaySubsystem::SnapshotJSON(Parsed)),
              Serialize(Original));
    TArray<TSharedRef<FJsonObject>> Invalid;
    auto Bad = Clone(Original);
    Bad->SetArrayField(TEXT("teams"), {MakeShared<FJsonValueNumber>(1), MakeShared<FJsonValueNumber>(2)});
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->SetArrayField(TEXT("entities"), {MakeShared<FJsonValueString>(TEXT("wrong type"))});
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->SetNumberField(TEXT("phase"), 99);
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->SetNumberField(TEXT("time"), std::numeric_limits<double>::infinity());
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->SetArrayField(TEXT("hazards"), {MakeShared<FJsonValueBoolean>(false)});
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->SetArrayField(TEXT("projectiles"), {MakeShared<FJsonValueNumber>(42)});
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->GetArrayField(TEXT("entities"))[0]->AsObject()->SetNumberField(TEXT("hp"), -1);
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->GetArrayField(TEXT("entities"))[0]->AsObject()->SetNumberField(TEXT("id"), 1e30);
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->GetArrayField(TEXT("teams"))[0]->AsObject()->SetArrayField(TEXT("hand"),
                                                                    {MakeShared<FJsonValueBoolean>(false)});
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->GetArrayField(TEXT("teams"))[0]->AsObject()->SetStringField(TEXT("telemetry"), TEXT("malformed"));
    Invalid.Add(Bad);
    for (int32 I = 0; I < Invalid.Num(); ++I) {
        rift::Snapshot Unchanged;
        Unchanged.seed = 777;
        TestFalse(FString::Printf(TEXT("Malformed case %d rejected without assertions"), I),
                  URiftReplaySubsystem::SnapshotFromJSON(Invalid[I], Unchanged));
        TestEqual(TEXT("Failed parse leaves destination unchanged"), Unchanged.seed, uint32(777));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftReplayIntegrationTest, "Rift.Integration.ReplayTimeline", Flags)
bool FRiftReplayIntegrationTest::RunTest(const FString &Parameters) {
    if (!Sandbox()) {
        AddError(TEXT("Use an isolated -RiftSaveRoot and -RiftAutomationSandbox."));
        return false;
    }
    auto *GI = NewObject<UGameInstance>();
    auto *R = NewObject<URiftReplaySubsystem>(GI);
    rift::MatchOptions O;
    O.aiEnabled = {false, false};
    rift::Match M(O);
    R->BeginRecording(O, true);
    R->Sample(M.State());
    for (const auto &E : M.DrainEvents())
        R->RecordEvent(E);
    M.Step(.1);
    M.Spawn(rift::Team::Enemy, "vampire_bats", {1, 3});
    M.Spawn(rift::Team::Player, "meteor_shards", {1, 3});
    M.SetAether(rift::Team::Player, 10);
    const auto Tower = M.State().entities[1].id;
    M.SetTowerHP(Tower, 1234);
    for (const auto &E : M.DrainEvents())
        R->RecordEvent(E);
    M.Step(.15);
    for (const auto &E : M.DrainEvents())
        R->RecordEvent(E);
    R->EndRecording(M.State(), true);
    TestTrue(TEXT("Recorded replay background write succeeds"), R->FlushPendingWrites());
    TestTrue(TEXT("Recorded replay saved"),
             IFileManager::Get().FileExists(
                 *FPaths::Combine(URiftProfileSubsystem::SaveRoot(), TEXT("Replays"), R->LatestFilename)));
    TestTrue(TEXT("Open validated replay"), R->OpenReplay(R->LatestFilename));
    R->Seek(.05f);
    TestEqual(TEXT("No future entities before deploy"), int32(R->View.entities.size()), 6);
    R->Seek(.1f);
    TestEqual(TEXT("DEV bank mutation at timestamp"), R->View.aether[0], 10.0);
    int32 DeadBats = 0;
    double TowerHP = 0;
    for (const auto &E : R->View.entities) {
        if (E.cardId == "vampire_bats" && E.dead)
            ++DeadBats;
        if (E.id == Tower)
            TowerHP = E.hp;
    }
    TestEqual(TEXT("All five immediate spell deaths reconstructed"), DeadBats, 5);
    TestEqual(TEXT("Tower edit reconstructed"), TowerHP, 1234.0);
    TestEqual(TEXT("Meteor zone reconstructed"), int32(R->View.hazards.size()), 1);
    R->Seek(0);
    TestEqual(TEXT("Backward seek restores initial state"), int32(R->View.entities.size()), 6);
    R->Advance(.11f);
    TestTrue(TEXT("Forward playback consumes chronological event cursor"), R->EventCursor > 1);
    const int32 Cursor = R->EventCursor;
    R->SetSpeed(0);
    R->Advance(.1f);
    TestEqual(TEXT("Paused playback does not replay effects"), R->EventCursor, Cursor);
    const FString Valid = Fixture(TEXT("-exported-replay.json"));
    TestTrue(TEXT("Native replay exports ordinary JSON"), R->ExportReplay(R->LatestFilename, Valid));
    FString Text;
    FFileHelper::LoadFileToString(Text, *Valid);
    auto Broken = Json(Text);
    Broken->SetArrayField(TEXT("states"), {MakeShared<FJsonValueBoolean>(false)});
    FString Error;
    TestFalse(TEXT("Invalid recording snapshots rejected"),
              URiftReplaySubsystem::ValidateRecording(Broken, Error));
    const FString Imported = Fixture(TEXT("-invalid-replay.json"));
    URiftProfileSubsystem::AtomicWrite(Imported, Serialize(Broken.ToSharedRef()), Error);
    TestFalse(TEXT("Malformed import rejected before archive write"), R->ImportReplay(Imported));
    TestTrue(TEXT("Invalid import preserves active replay"), R->IsPlaying());
    TArray<uint8> NativeBytes;
    FFileHelper::LoadFileToArray(
        NativeBytes, *FPaths::Combine(URiftProfileSubsystem::SaveRoot(), TEXT("Replays"), R->LatestFilename));
    TestTrue(TEXT("New recording has its native archive extension and full header"),
             R->LatestFilename.EndsWith(TEXT(".riftreplay")) && NativeBytes.Num() > 24);
    if (NativeBytes.Num() > 24) {
        TArray<TArray<uint8>> CorruptArchives;
        auto Corrupt = NativeBytes;
        Corrupt[0] = 'X';
        CorruptArchives.Add(Corrupt);
        Corrupt = NativeBytes;
        Corrupt.Last() ^= 0xff;
        CorruptArchives.Add(Corrupt);
        Corrupt = NativeBytes;
        Corrupt[7] = '2';
        CorruptArchives.Add(Corrupt);
        Corrupt = NativeBytes;
        for (int32 I = 8; I < 12; ++I)
            Corrupt[I] = 0xff;
        CorruptArchives.Add(Corrupt);
        Corrupt = NativeBytes;
        Corrupt[12] ^= 0xff;
        CorruptArchives.Add(Corrupt);
        Corrupt = NativeBytes;
        Corrupt.SetNum(Corrupt.Num() - 8);
        CorruptArchives.Add(Corrupt);
        Corrupt = NativeBytes;
        Corrupt.SetNum(8);
        CorruptArchives.Add(Corrupt);
        for (int32 I = 0; I < CorruptArchives.Num(); ++I) {
            const FString CorruptFile = Fixture(TEXT("-corrupt.riftreplay"));
            TestTrue(TEXT("Corrupt archive fixture writes atomically"),
                     URiftProfileSubsystem::AtomicWriteBytes(CorruptFile, CorruptArchives[I], Error));
            TestFalse(
                FString::Printf(TEXT("Invalid archive header/length/CRC/truncation case %d rejected"), I),
                R->ImportReplay(CorruptFile));
            TestTrue(TEXT("Rejected binary import retains the active replay"), R->IsPlaying());
        }
        const FString OversizedFile = Fixture(TEXT("-oversized.riftreplay"));
        {
            TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*OversizedFile));
            if (TestNotNull(TEXT("Oversized native fixture writer"), Writer.Get())) {
                Writer->Serialize(NativeBytes.GetData(), 24);
                Writer->Seek(64 * 1024 * 1024);
                uint8 LastByte = 0;
                Writer->Serialize(&LastByte, 1);
            }
        }
        TestFalse(TEXT("Native physical size cap rejects oversized input before payload allocation"),
                  R->ImportReplay(OversizedFile));
        TestTrue(TEXT("Oversized native import retains the active replay"), R->IsPlaying());
    }
    Broken = Json(Text);
    Broken->SetNumberField(TEXT("formatVersion"), 2);
    TestFalse(TEXT("Future replay version rejected"), URiftReplaySubsystem::ValidateRecording(Broken, Error));
    Broken = Json(Text);
    Broken->GetArrayField(TEXT("states"))[0]->AsObject()->SetNumberField(TEXT("time"), .01);
    TestFalse(TEXT("Missing initial state rejected"), URiftReplaySubsystem::ValidateRecording(Broken, Error));
    Broken = Json(Text);
    Broken->GetArrayField(TEXT("states"))[0]->AsObject()->SetNumberField(TEXT("eventSequence"), 999999);
    TestFalse(TEXT("Future event bookmark rejected"), URiftReplaySubsystem::ValidateRecording(Broken, Error));
    Broken = Json(Text);
    Broken->GetObjectField(TEXT("result"))->SetNumberField(TEXT("time"), .01);
    TestFalse(TEXT("Mismatched analysis duration rejected"),
              URiftReplaySubsystem::ValidateRecording(Broken, Error));
    R->CloseReplay();
    TestFalse(TEXT("Close clears replay state"), R->IsPlaying());
    // A unit approaching death still interpolates to its actual death position, rather than freezing.
    rift::Match PositionMatch(O);
    auto PositionStart = PositionMatch.State();
    rift::Entity Moving;
    Moving.id = 999;
    Moving.cardId = "ironclad";
    Moving.team = rift::Team::Enemy;
    Moving.hp = Moving.maxHp = rift::FindCard("ironclad")->hp;
    Moving.radius = .43;
    PositionStart.entities.push_back(Moving);
    auto PositionEnd = PositionStart;
    PositionEnd.elapsed = PositionEnd.phaseElapsed = 1;
    PositionEnd.timeRemaining = 179;
    PositionEnd.entities.back().position = {4, 0};
    PositionEnd.entities.back().dead = true;
    PositionEnd.entities.back().hp = 0;
    PositionEnd.entities.back().died = .5;
    auto First = URiftReplaySubsystem::SnapshotJSON(PositionStart),
         Last = URiftReplaySubsystem::SnapshotJSON(PositionEnd);
    First->SetNumberField(TEXT("eventSequence"), 0);
    Last->SetNumberField(TEXT("eventSequence"), 1);
    auto Motion = MakeShared<FJsonObject>();
    Motion->SetNumberField(TEXT("formatVersion"), 1);
    Motion->SetStringField(TEXT("model"), TEXT("rift-native-1"));
    Motion->SetNumberField(TEXT("duration"), 1);
    Motion->SetArrayField(TEXT("states"),
                          {MakeShared<FJsonValueObject>(First), MakeShared<FJsonValueObject>(Last)});
    Motion->SetObjectField(TEXT("result"), Last);
    auto Death = MakeShared<FJsonObject>();
    Death->SetStringField(TEXT("type"), TEXT("death"));
    Death->SetStringField(TEXT("cardId"), TEXT("bullet_burst"));
    Death->SetNumberField(TEXT("sequence"), 1);
    Death->SetNumberField(TEXT("time"), .5);
    Death->SetNumberField(TEXT("team"), 0);
    Death->SetNumberField(TEXT("targetTeam"), 1);
    Death->SetNumberField(TEXT("target"), 999);
    auto Position = MakeShared<FJsonObject>();
    Position->SetNumberField(TEXT("x"), 4);
    Position->SetNumberField(TEXT("z"), 0);
    Death->SetObjectField(TEXT("position"), Position);
    Motion->SetArrayField(TEXT("events"), {MakeShared<FJsonValueObject>(Death)});
    const FString MotionName = FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("-motion.json");
    URiftProfileSubsystem::AtomicWrite(
        FPaths::Combine(URiftProfileSubsystem::SaveRoot(), TEXT("Replays"), MotionName), Serialize(Motion),
        Error);
    TestTrue(TEXT("Motion recording validates"), R->OpenReplay(MotionName));
    R->Seek(.25f);
    TestEqual(TEXT("Approach to death interpolates"), R->View.entities.back().position.x, 2.0);
    R->Seek(.5f);
    TestTrue(TEXT("Death occurs at its event time"), R->View.entities.back().dead);
    TestEqual(TEXT("Corpse preserves recorded death position"), R->View.entities.back().position.x, 4.0);
    R->CloseReplay();
    // These events occur between the two snapshots: sampled terminal state must not hide event bugs.
    auto PhaseEnd = PositionMatch.State();
    PhaseEnd.elapsed = 1;
    PhaseEnd.phase = rift::Phase::Finished;
    PhaseEnd.winner = 0;
    PhaseEnd.resultReason = "fixture terminal";
    auto PhaseFirst = URiftReplaySubsystem::SnapshotJSON(PositionMatch.State()),
         PhaseLast = URiftReplaySubsystem::SnapshotJSON(PhaseEnd);
    PhaseFirst->SetNumberField(TEXT("eventSequence"), 0);
    PhaseLast->SetNumberField(TEXT("eventSequence"), 4);
    auto Timeline = Clone(Motion);
    Timeline->SetArrayField(
        TEXT("states"), {MakeShared<FJsonValueObject>(PhaseFirst), MakeShared<FJsonValueObject>(PhaseLast)});
    Timeline->SetObjectField(TEXT("result"), PhaseLast);
    TArray<TSharedPtr<FJsonValue>> TimelineEvents;
    auto AddTimelineEvent = [&](int32 Sequence, double Time, const TCHAR *Type, const TCHAR *Reason,
                                const TCHAR *CardId = TEXT(""), double Amount = 0) {
        auto Event = MakeShared<FJsonObject>();
        Event->SetNumberField(TEXT("sequence"), Sequence);
        Event->SetNumberField(TEXT("time"), Time);
        Event->SetNumberField(TEXT("team"), 1);
        Event->SetStringField(TEXT("type"), Type);
        Event->SetStringField(TEXT("reason"), Reason);
        Event->SetStringField(TEXT("cardId"), CardId);
        Event->SetNumberField(TEXT("amount"), Amount);
        TimelineEvents.Add(MakeShared<FJsonValueObject>(Event));
    };
    AddTimelineEvent(1, .2, TEXT("phase"), TEXT("overtime"));
    AddTimelineEvent(2, .3, TEXT("ai_decision"), TEXT("counter_push: visible surviving units"),
                     TEXT("counter"));
    AddTimelineEvent(3, .5, TEXT("phase"), TEXT("tiebreaker"));
    AddTimelineEvent(4, .75, TEXT("match_end"), TEXT("fixture terminal"));
    Timeline->SetArrayField(TEXT("events"), TimelineEvents);
    const FString TimelineName = FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("-phases.json");
    TestTrue(TEXT("Between-snapshot event fixture writes"),
             URiftProfileSubsystem::AtomicWrite(
                 FPaths::Combine(URiftProfileSubsystem::SaveRoot(), TEXT("Replays"), TimelineName),
                 Serialize(Timeline), Error));
    TestTrue(TEXT("Between-snapshot phase recording validates"), R->OpenReplay(TimelineName));
    R->Seek(.25f);
    TestTrue(TEXT("Overtime phase reconstructs from event before next snapshot"),
             R->View.phase == rift::Phase::Overtime);
    TestTrue(TEXT("Overtime clock advances from phase event"),
             FMath::IsNearlyEqual(R->View.timeRemaining, 119.95, 1e-6));
    R->Seek(.35f);
    TestTrue(TEXT("AI explanation reconstructs from event before next snapshot"),
             R->View.ai[1].style == "counter" && R->View.ai[1].decision == "counter_push" &&
                 R->View.ai[1].reason == "visible surviving units");
    R->Seek(.6f);
    TestTrue(TEXT("Tiebreaker phase reconstructs from event before next snapshot"),
             R->View.phase == rift::Phase::Tiebreaker);
    TestTrue(TEXT("Tiebreaker clock starts at the phase event"),
             FMath::IsNearlyEqual(R->View.phaseElapsed, .1, 1e-6));
    R->Seek(.8f);
    TestTrue(TEXT("Terminal winner and reason reconstruct from event before terminal snapshot"),
             R->View.phase == rift::Phase::Finished && R->View.winner == 0 &&
                 R->View.resultReason == "fixture terminal");
    R->CloseReplay();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftFullReplayIntegrationTest, "Rift.Integration.FullLengthReplay", Flags)
bool FRiftFullReplayIntegrationTest::RunTest(const FString &Parameters) {
    if (!FParse::Param(FCommandLine::Get(), TEXT("RiftAutomationSandbox"))) {
        AddError(TEXT("Full replay fixture requires -RiftAutomationSandbox."));
        return false;
    }
    auto *GI = NewObject<UGameInstance>();
    auto *Replay = NewObject<URiftReplaySubsystem>(GI);
    rift::MatchOptions Options;
    Options.aiEnabled = {false, false};
    Options.decks = {rift::DefaultDeck(), rift::DefaultDeck()};
    rift::Match Match(Options);
    Replay->BeginRecording(Options, true);
    for (const auto &Event : Match.DrainEvents())
        Replay->RecordEvent(Event);
    Replay->Sample(Match.State());
    int32 EventCount = 1;
    while (Match.State().phase != rift::Phase::Finished && Match.State().elapsed < 360) {
        Match.Step(.25);
        auto Events = Match.DrainEvents();
        EventCount += int32(Events.size());
        for (const auto &Event : Events)
            Replay->RecordEvent(Event);
        Replay->Sample(Match.State());
    }
    TestTrue(TEXT("Quiet regulation/overtime reaches its native terminal tiebreaker"),
             Match.State().phase == rift::Phase::Finished && Match.State().elapsed > 300);
    TestTrue(TEXT("Both capped banks produce a realistic long event history"), EventCount > 30000);
    const double FinalizeStarted = FPlatformTime::Seconds();
    Replay->EndRecording(Match.State(), false);
    const double QueueMS = (FPlatformTime::Seconds() - FinalizeStarted) * 1000;
    TestTrue(TEXT("Long replay finalization queues without a game-thread validation/write stall"),
             QueueMS < 250);
    TestTrue(TEXT("Long replay remains explicitly pending until completion is published"),
             Replay->IsSaving() && Replay->LatestFilename.IsEmpty());
    const FString LongFilename = Replay->LatestQueuedFilename;
    // A player can start and leave the next match while a long prior replay is being written.
    rift::Match ShortMatch(Options);
    Replay->BeginRecording(Options, true);
    for (const auto &Event : ShortMatch.DrainEvents())
        Replay->RecordEvent(Event);
    Replay->Sample(ShortMatch.State());
    ShortMatch.Step(.1);
    for (const auto &Event : ShortMatch.DrainEvents())
        Replay->RecordEvent(Event);
    Replay->EndRecording(ShortMatch.State(), true);
    const FString ShortFilename = Replay->LatestQueuedFilename;
    TestTrue(TEXT("Consecutive recording documents are independently queued with unique filenames"),
             Replay->PendingWrites.Num() == 2 && ShortFilename != LongFilename);
    const double PollStarted = FPlatformTime::Seconds();
    Replay->Tick(0);
    TestTrue(TEXT("Normal replay completion polling never waits for the long file"),
             (FPlatformTime::Seconds() - PollStarted) < .25);
    TestTrue(TEXT("Long background recording finishes successfully"), Replay->FlushPendingWrites());
    TestEqual(TEXT("Older write completion cannot replace the latest match filename"), Replay->LatestFilename,
              ShortFilename);
    TestTrue(TEXT("The next-match recording also survives concurrent finalization"),
             Replay->OpenReplay(ShortFilename));
    TestTrue(TEXT("The next-match replay retains its own duration"),
             FMath::IsNearlyEqual(Replay->Duration(), float(ShortMatch.State().elapsed)));
    Replay->CloseReplay();
    const FString Path = FPaths::Combine(URiftProfileSubsystem::SaveRoot(), TEXT("Replays"), LongFilename);
    const int64 Bytes = IFileManager::Get().FileSize(*Path);
    TestTrue(TEXT("Complete compressed native recording fits its own 64MB archive limit"),
             Bytes > 24 && Bytes <= 64 * 1024 * 1024);
    const FString Exported = Fixture(TEXT("-full-replay.json"));
    TestTrue(TEXT("Complete native archive exports ordinary lossless JSON"),
             Replay->ExportReplay(LongFilename, Exported));
    const int64 DecodedBytes = IFileManager::Get().FileSize(*Exported);
    TestTrue(TEXT("Full decoded JSON history is preserved within its 256MB limit"),
             DecodedBytes > 10 * 1024 * 1024 && DecodedBytes <= 256 * 1024 * 1024);
    if (!TestTrue(TEXT("Complete native recording reopens successfully"), Replay->OpenReplay(LongFilename))) {
        AddError(Replay->LastError);
        return false;
    }
    TestTrue(TEXT("Full recorded sample history is preserved"), Replay->RecordedStates().Num() > 1200);
    Replay->Seek(Replay->Duration());
    const auto Analysis = Replay->CurrentAnalysis();
    rift::Snapshot Result;
    TestTrue(TEXT("Full replay retains a valid terminal analysis"),
             URiftReplaySubsystem::SnapshotFromJSON(Analysis, Result));
    TestTrue(TEXT("Full replay retains the exact winner and frozen card telemetry"),
             Result.phase == rift::Phase::Finished && Result.winner == Match.State().winner &&
                 Result.telemetry[0].size() == rift::Cards().size());
    AddInfo(FString::Printf(TEXT("Full native replay: %d events, %d samples, %lld stored bytes, %lld decoded "
                                 "bytes, %.3f seconds."),
                            EventCount, Replay->RecordedStates().Num(), Bytes, DecodedBytes,
                            Match.State().elapsed));
    AddInfo(FString::Printf(TEXT("Long replay game-thread finalization queue time %.3f ms."), QueueMS));
    Replay->CloseReplay();
    auto RecordAIMatch = [&](bool Congestion) {
        rift::MatchOptions AIOptions;
        AIOptions.seed = 32;
        AIOptions.aiEnabled = {true, true};
        AIOptions.aiStyles = {"control", "counter"};
        const std::vector<std::string> SwarmDeck{"ironclad",    "twin_blades",  "archer_tower",
                                                 "frost_fang",  "vampire_bats", "sky_manta",
                                                 "storm_raven", "meteor_shards"};
        AIOptions.decks = {SwarmDeck, SwarmDeck};
        rift::Match AI(AIOptions);
        Replay->BeginRecording(AIOptions, Congestion);
        int32 RecordedCount = 0, Samples = 1, Peak = 6;
        bool ZeroCrownOvertime = false;
        double NextSpawn = 0;
        for (const auto &Event : AI.DrainEvents()) {
            Replay->RecordEvent(Event);
            ++RecordedCount;
        }
        Replay->Sample(AI.State());
        while (AI.State().phase != rift::Phase::Finished && AI.State().elapsed < 360) {
            if (Congestion &&
                (AI.State().phase == rift::Phase::Regulation || AI.State().phase == rift::Phase::Overtime)) {
                // These are ordinary reachable Developer commands, not edited snapshots or combat overrides.
                for (const auto &Entity : AI.State().entities)
                    if (!Entity.dead && Entity.kind >= rift::EntityKind::Guard)
                        AI.SetTowerHP(Entity.id, Entity.maxHp);
                if (AI.State().elapsed >= NextSpawn) {
                    for (const auto Team : {rift::Team::Player, rift::Team::Enemy})
                        for (const double X : {-3.0, 0.0, 3.0})
                            AI.Spawn(Team, "vampire_bats", {X, Team == rift::Team::Player ? 8.5 : -8.5});
                    NextSpawn += 5;
                }
            }
            AI.Step(.25);
            for (const auto &Event : AI.DrainEvents()) {
                Replay->RecordEvent(Event);
                ++RecordedCount;
            }
            Replay->Sample(AI.State());
            ++Samples;
            Peak = FMath::Max(Peak, int32(AI.State().entities.size()));
            ZeroCrownOvertime |= AI.State().phase == rift::Phase::Overtime && AI.State().crowns[0] == 0 &&
                                 AI.State().crowns[1] == 0;
        }
        TestTrue(TEXT("Actual paid AI match reaches a native terminal result through zero-crown overtime"),
                 AI.State().phase == rift::Phase::Finished && AI.State().elapsed > 180 && ZeroCrownOvertime &&
                     AI.State().spent[0] > 100 && AI.State().spent[1] > 100);
        if (Congestion)
            TestTrue(TEXT("Reachable Developer swarm field exercises substantial congestion"), Peak >= 40);
        else
            TestTrue(TEXT("Complete ordinary AI match includes a populated live field"), Peak >= 20);
        const double Started = FPlatformTime::Seconds();
        Replay->EndRecording(AI.State(), false);
        const double AIQueueMS = (FPlatformTime::Seconds() - Started) * 1000;
        TestTrue(TEXT("Populated match finalization also queues without a game-thread stall"),
                 AIQueueMS < 250);
        TestTrue(TEXT("Populated complete native archive saves successfully"), Replay->FlushPendingWrites());
        const FString SavedName = Replay->LatestFilename;
        const int64 StoredSize = IFileManager::Get().FileSize(
            *FPaths::Combine(URiftProfileSubsystem::SaveRoot(), TEXT("Replays"), SavedName));
        if (!TestTrue(TEXT("Populated native archive opens with its bounded checksums"),
                      Replay->OpenReplay(SavedName)))
            return;
        TestEqual(TEXT("Every populated match event survives compression"),
                  Replay->Loaded->GetArrayField(TEXT("events")).Num(), RecordedCount);
        TestEqual(TEXT("Every populated match sample survives compression"), Replay->RecordedStates().Num(),
                  Samples + 1);
        TestEqual(TEXT("Entire final populated analysis survives lossless compression"),
                  Serialize(Replay->CurrentAnalysis().ToSharedRef()),
                  Serialize(URiftReplaySubsystem::SnapshotJSON(AI.State())));
        Replay->Seek(Replay->Duration());
        TestTrue(TEXT("Populated archive terminal playback matches the actual native winner"),
                 Replay->View.phase == rift::Phase::Finished && Replay->View.winner == AI.State().winner);
        int64 JSONSize = 0;
        if (Congestion) {
            const FString Plain = Fixture(TEXT("-congested-plain.json"));
            TestTrue(TEXT("Congested archive exports a complete ordinary JSON document"),
                     Replay->ExportReplay(SavedName, Plain));
            JSONSize = IFileManager::Get().FileSize(*Plain);
            TestTrue(TEXT("Congested ordinary JSON export stays in its decoded input bound"),
                     JSONSize > 0 && JSONSize <= 256 * 1024 * 1024);
            auto *ImportGI = NewObject<UGameInstance>(GEngine);
            ImportGI->InitializeStandalone(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
            auto *ImportedReplay = ImportGI->GetSubsystem<URiftReplaySubsystem>();
            auto *ImportedProfile = ImportGI->GetSubsystem<URiftProfileSubsystem>();
            const int32 BeforeImport = ImportedProfile->ReplayFiles.Num();
            TestTrue(TEXT("Complete ordinary JSON export reimports into the native archive format"),
                     ImportedReplay->ImportReplay(Plain));
            if (TestEqual(TEXT("JSON reimport adds exactly one native archive"),
                          ImportedProfile->ReplayFiles.Num(), BeforeImport + 1)) {
                const FString ImportedName = ImportedProfile->ReplayFiles.Last();
                TestTrue(TEXT("Reimported archive has the native extension"),
                         ImportedName.EndsWith(TEXT(".riftreplay")));
                TestTrue(TEXT("Reimported complete archive opens"), ImportedReplay->OpenReplay(ImportedName));
                TestEqual(TEXT("Reimport preserves all congestion events"),
                          ImportedReplay->EventsNear(AI.State().elapsed, AI.State().elapsed).Num(),
                          RecordedCount);
                TestEqual(TEXT("Reimport preserves every congestion snapshot"),
                          ImportedReplay->RecordedStates().Num(), Samples + 1);
                TestEqual(TEXT("Native archive/JSON/import roundtrip preserves the complete analysis"),
                          Serialize(ImportedReplay->CurrentAnalysis().ToSharedRef()),
                          Serialize(URiftReplaySubsystem::SnapshotJSON(AI.State())));
            }
            ImportedReplay->CloseReplay();
            auto *ImportWorld = ImportGI->GetWorld();
            ImportWorld->DestroyWorld(false);
            ImportGI->Shutdown();
            GEngine->DestroyWorldContext(ImportWorld);
        }
        AddInfo(FString::Printf(
            TEXT("%s replay: %.3fs, peak %d, %d events, %d samples, %lld stored, %lld exported JSON, %.3fms "
                 "queue."),
            Congestion ? TEXT("Congested Developer/paid AI") : TEXT("Paid control/counter AI"),
            AI.State().elapsed, Peak, RecordedCount, Samples + 1, StoredSize, JSONSize, AIQueueMS));
        Replay->CloseReplay();
    };
    RecordAIMatch(false);
    RecordAIMatch(true);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftPausedResultIntegrationTest,
                                 "Rift.Integration.PausedResultAndReplayEvents", Flags)
bool FRiftPausedResultIntegrationTest::RunTest(const FString &Parameters) {
    if (!Sandbox()) {
        AddError(TEXT("Use an isolated -RiftSaveRoot and -RiftAutomationSandbox."));
        return false;
    }
    auto *GI = NewObject<UGameInstance>(GEngine);
    GI->InitializeStandalone(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto *World = GI->GetWorld();
    auto *Match = World->GetSubsystem<URiftMatchSubsystem>();
    auto *Developer = World->GetSubsystem<URiftDeveloperSubsystem>();
    auto *Profile = GI->GetSubsystem<URiftProfileSubsystem>();
    auto *Replay = GI->GetSubsystem<URiftReplaySubsystem>();
    const int32 InitialMatches = Profile->Matches, InitialWins = Profile->Wins;
    Match->StartMatch(true, false);
    Match->SetSpeed(0);
    Match->Simulation()->Step(.2);
    Match->FlushEvents();
    uint64 Core = 0;
    for (const auto &E : Match->Simulation()->State().entities)
        if (E.kind == rift::EntityKind::Core && E.team == rift::Team::Enemy)
            Core = E.id;
    TestTrue(TEXT("Paused DEV Core edit"), Developer->SetTowerHP(Core, 0));
    Match->Tick(.016f);
    TestTrue(TEXT("Paused terminal background recording completes"), Replay->FlushPendingWrites());
    TestEqual(TEXT("Paused terminal state records one result"), Profile->Matches, InitialMatches + 1);
    TestEqual(TEXT("Paused terminal win saved"), Profile->Wins, InitialWins + 1);
    TestTrue(TEXT("Paused terminal replay saved"), !Replay->LatestFilename.IsEmpty());
    Match->Tick(.016f);
    TestEqual(TEXT("Repeated finished tick cannot duplicate result"), Profile->Matches, InitialMatches + 1);
    const FString Filename = Replay->LatestFilename;
    std::vector<uint64> CrossedEvents;
    const auto Handle =
        Match->OnEvent.AddLambda([&](const rift::Event &E) { CrossedEvents.push_back(E.sequence); });
    TestTrue(TEXT("World replay opens"), Replay->OpenReplay(Filename));
    Replay->Seek(.15f);
    TestTrue(TEXT("Seeking emits no historical presentation events"), CrossedEvents.empty());
    Replay->Advance(.1f);
    TestTrue(TEXT("Forward playback broadcasts crossed terminal events"), CrossedEvents.size() >= 4);
    TestTrue(TEXT("Presentation events remain chronological"),
             std::is_sorted(CrossedEvents.begin(), CrossedEvents.end()));
    const auto Count = CrossedEvents.size();
    Replay->Seek(0);
    TestEqual(TEXT("Backward seek stays silent"), int32(CrossedEvents.size()), int32(Count));
    Replay->CloseReplay();
    Match->OnEvent.Remove(Handle);
    Match->LeaveMatch();
    World->DestroyWorld(false);
    GI->Shutdown();
    GEngine->DestroyWorldContext(World);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftCardDataIntegrationTest, "Rift.Integration.CardData", Flags)
bool FRiftCardDataIntegrationTest::RunTest(const FString &Parameters) {
    for (const auto &C : rift::Cards()) {
        const FString Id = UTF8_TO_TCHAR(C.id.c_str());
        auto *D = LoadObject<URiftCardData>(nullptr,
                                            *FString::Printf(TEXT("/Game/Rift/Cards/DA_%s.DA_%s"), *Id, *Id));
        TestNotNull(Id + TEXT(" DataAsset imported"), D);
        if (!D)
            continue;
        TestEqual(Id + TEXT(" canonical ID"), D->CardId, Id);
        TestEqual(Id + TEXT(" cost"), D->Cost, C.cost);
        TestEqual(Id + TEXT(" deployment count"), D->Count, C.count);
#define METRIC(UE, Core) TestTrue(Id + TEXT(" " #UE), FMath::IsNearlyEqual(double(D->UE), C.Core, 1e-5));
        METRIC(HP, hp)
        METRIC(Damage, damage)
        METRIC(AttackSpeed, attackInterval)
        METRIC(MoveSpeed, moveSpeed)
        METRIC(AttackRange, range)
        METRIC(ProjectileSpeed, projectileSpeed)
        METRIC(SplashRadius, splash)
        METRIC(Lifetime, lifetime)
        METRIC(Footprint, footprint)
        METRIC(TowerDamage, towerDamage) METRIC(SpellRadius, spellRadius) METRIC(ChargeDamage, chargeDamage)
            METRIC(SlowPct, slowPct) METRIC(SlowDuration, slowDuration) METRIC(AuraDamage, auraDamage)
                METRIC(AuraInterval, auraInterval) METRIC(AuraRadius, auraRadius)
                    METRIC(StunDuration, stunDuration) METRIC(DotDamage, dotDamage)
                        METRIC(DotDuration, dotDuration) METRIC(DotInterval, dotInterval)
#undef METRIC
                            TestEqual(Id + TEXT(" rounds"), D->Rounds, C.rounds);
        TestEqual(Id + TEXT(" air"), D->Flying, C.flying);
        TestEqual(Id + TEXT(" air targeting"), D->GroundAndAir, C.canHitAir);
        TestEqual(Id + TEXT(" structures targeting"), D->StructuresOnly, C.structuresOnly);
        TestEqual(Id + TEXT(" spell type"), D->Spell, C.spell);
        TestEqual(Id + TEXT(" building type"), D->Building, C.building);
        TestNotNull(Id + TEXT(" card illustration"), D->Illustration.Get());
        if (!C.spell && !C.building) {
            TestNotNull(Id + TEXT(" skeletal mesh"), D->CharacterMesh.Get());
            if (const auto *Mesh = D->CharacterMesh.Get()) {
                const double Height = Mesh->GetBounds().BoxExtent.Z * 2;
                TestTrue(Id + TEXT(" imported centimeter scale"), Height >= 15 && Height <= 600);
                TestNotNull(Id + TEXT(" generated physics asset"), Mesh->GetPhysicsAsset());
                TestTrue(Id + TEXT(" imported LOD hierarchy"), Mesh->GetLODNum() >= 3);
                TestTrue(Id + TEXT(" skeletal rig"), Mesh->GetRefSkeleton().GetNum() >= 8);
            }
            TestTrue(Id + TEXT(" animation set bound"), D->Animations.Num() >= 6);
            for (const auto &A : D->Animations)
                TestNotNull(Id + TEXT(" animation ") + A.Key.ToString(), A.Value.Get());
        }
        if (C.building)
            TestNotNull(Id + TEXT(" structure mesh"), D->StructureMesh.Get());
        TestTrue(Id + TEXT(" effects bound"), D->Effects.Num() >= 10);
        for (const auto &E : D->Effects)
            TestNotNull(Id + TEXT(" effect ") + E.Key.ToString(), E.Value.Get());
        TestTrue(Id + TEXT(" sounds bound"), D->Sounds.Num() >= 3);
        for (const auto &A : D->Sounds)
            TestNotNull(Id + TEXT(" sound ") + A.Key.ToString(), A.Value.Get());
    }
    return true;
}
#endif
