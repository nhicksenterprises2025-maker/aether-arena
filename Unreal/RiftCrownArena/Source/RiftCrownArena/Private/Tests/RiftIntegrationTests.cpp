#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
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
#include "HAL/Event.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
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
#include "Presentation/RiftArenaPresentation.h"
#include "Serialization/JsonSerializer.h"
#include <cmath>
#include <limits>
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

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
    for (const auto &Id : {FString(TEXT("meteor_shards")), FString(TEXT("bullet_burst")),
                           FString(TEXT("nova_flask"))}) {
        UI->InspectCard(Id);
        bool HasDelayLabel = false, HasLeadAdvice = false, HasDelayValue = false;
        const auto *Definition = rift::FindCard(TCHAR_TO_UTF8(*Id));
        for (auto *Text : ActiveWidgets<UTextBlock>(Canvas)) {
            const FString Value = Text->GetText().ToString();
            HasDelayLabel |= Value == TEXT("TIME TO IMPACT");
            HasLeadAdvice |= Value == TEXT("LEAD YOUR CAST");
            HasDelayValue |= Value == FString::Printf(TEXT("%.2f s"), Definition->castDelay);
        }
        TestEqual(Id + TEXT(" impact timing is shown only for delayed spells"), HasDelayLabel,
                  Definition->castDelay > 0);
        TestEqual(Id + TEXT(" lead advice matches the actual mechanic"), HasLeadAdvice,
                  Definition->castDelay > 0);
        if (Definition->castDelay > 0)
            TestTrue(Id + TEXT(" exact authoritative delay shown"), HasDelayValue);
    }
    UI->Navigate(TEXT("Home"));
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
    if (!Click(TEXT("Meta")) || !Click(TEXT("STATS GUIDE")))
        return false;
    bool HasGuide=false,HasCleanExplanation=false,HasMechanicalExplanation=false;
    for(auto* Text:ActiveWidgets<UTextBlock>(Canvas))
    {
        const FString Value=Text->GetText().ToString();
        HasGuide|=Value==TEXT("READING THE META LAB");
        HasCleanExplanation|=Value.Contains(TEXT("Card Clean N excludes"));
        HasMechanicalExplanation|=Value.Contains(TEXT("rule-based mechanical coverage edges"));
    }
    TestTrue(TEXT("Actual Stats Guide callback opens definitions and distinguishes observed results from mechanical matchups"),HasGuide&&HasCleanExplanation&&HasMechanicalExplanation);
    TestEqual(TEXT("Guide leaves table filter/export controls out of the reading view"),ActiveWidgets<UComboBoxString>(Canvas).Num(),0);
    if(!Click(TEXT("CARDS"),true))return false;
    TestTrue(TEXT("Stats Guide returns to live filtered Meta tables"),ActiveWidgets<UComboBoxString>(Canvas).Num()>0&&ActiveButton(UI,TEXT("STATS GUIDE")));
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
#if PLATFORM_WINDOWS
    // Real sharing-denied handles exercise both backup operations. The
    // transient owner releases only after it observes the production stage,
    // holding a real conflict while this save creates its own staging file.
    auto ExerciseSaveLock = [&](bool BackupRename, bool Transient) {
        const FString LockedFile = Fixture(TEXT("-locked-atomic.json"));
        const FString LockedBackup = LockedFile + TEXT(".bak");
        FString LockError;
        if (!TestTrue(TEXT("Lock fixture creates previous bytes"),
                      URiftProfileSubsystem::AtomicWrite(LockedFile, TEXT("previous"), LockError)) ||
            !TestTrue(TEXT("Lock fixture creates current bytes and previous backup"),
                      URiftProfileSubsystem::AtomicWrite(LockedFile, TEXT("current"), LockError)))
            return;
        const TCHAR *Stage = BackupRename ? TEXT("backup-rename") : TEXT("backup-copy");
        const FString LockedPath = BackupRename ? LockedBackup : LockedFile;
        HANDLE Lock = ::CreateFileW(*LockedPath, GENERIC_READ,
                                    BackupRename ? FILE_SHARE_READ : 0, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        if (!TestTrue(FString::Printf(TEXT("Real Windows %s sharing lock opens"), Stage),
                      Lock != INVALID_HANDLE_VALUE))
            return;
        ON_SCOPE_EXIT {
            if (Lock != INVALID_HANDLE_VALUE)
                ::CloseHandle(Lock);
        };
        TFuture<bool> Released;
        FEvent *WatcherReady = nullptr;
        if (Transient) {
            WatcherReady = FPlatformProcess::GetSynchEventFromPool(true);
            const HANDLE ReleaseHandle = Lock;
            const FString StagePattern = (BackupRename ? LockedBackup : LockedFile) + TEXT(".tmp-*");
            Released = Async(EAsyncExecution::ThreadPool, [ReleaseHandle, StagePattern, WatcherReady]() {
                WatcherReady->Trigger();
                const double Started = FPlatformTime::Seconds();
                bool SawStage = false;
                while (FPlatformTime::Seconds() - Started < .25) {
                    TArray<FString> Stages;
                    IFileManager::Get().FindFiles(Stages, *StagePattern, true, false);
                    if (!Stages.IsEmpty()) {
                        SawStage = true;
                        break;
                    }
                    FPlatformProcess::SleepNoStats(.001f);
                }
                FPlatformProcess::SleepNoStats(.012f);
                ::CloseHandle(ReleaseHandle);
                return SawStage;
            });
            // The releaser now owns this actual OS handle.
            Lock = INVALID_HANDLE_VALUE;
            TestTrue(TEXT("Independent transient-lock watcher starts before the write"),
                     WatcherReady->Wait(1000));
        }
        const double Started = FPlatformTime::Seconds();
        const bool Saved = URiftProfileSubsystem::AtomicWrite(LockedFile, TEXT("replacement"), LockError);
        const double Elapsed = FPlatformTime::Seconds() - Started;
        const FString FailureDetail = LockError;
        if (Transient) {
            TestTrue(TEXT("Transient owner observed the actual save staging file before releasing"),
                     Released.Get());
            FPlatformProcess::ReturnSynchEventToPool(WatcherReady);
            TestTrue(FString::Printf(TEXT("Released Windows %s lock permits bounded save recovery"), Stage),
                     Saved && LockError.IsEmpty());
        } else {
            TestFalse(FString::Printf(TEXT("Persistent Windows %s lock rejects the save"), Stage), Saved);
            TestTrue(TEXT("Persistent lock reports exact failing stage, OS code and four attempts"),
                     LockError.Contains(FString(TEXT("stage=")) + Stage) &&
                         (LockError.Contains(TEXT("Win32=32;")) ||
                          LockError.Contains(TEXT("Win32=5; sharingWin32=32;"))) &&
                         LockError.Contains(TEXT("attempts=4")));
            TestTrue(TEXT("Persistent sharing conflict waits only the bounded retry budget"),
                     Elapsed >= .030 && Elapsed < .5);
            ::CloseHandle(Lock);
            Lock = INVALID_HANDLE_VALUE;
        }
        FString PrimaryBytes, BackupBytes;
        TestTrue(TEXT("Locked-save primary remains readable"),
                 FFileHelper::LoadFileToString(PrimaryBytes, *LockedFile));
        TestTrue(TEXT("Locked-save backup remains readable"),
                 FFileHelper::LoadFileToString(BackupBytes, *LockedBackup));
        TestEqual(TEXT("Lock handling preserves the exact expected primary bytes"), PrimaryBytes,
                  FString(Transient ? TEXT("replacement") : TEXT("current")));
        TestEqual(TEXT("Lock handling preserves the exact expected previous backup bytes"), BackupBytes,
                  FString(Transient ? TEXT("current") : TEXT("previous")));
        TArray<FString> PrimaryStages, BackupStages;
        IFileManager::Get().FindFiles(PrimaryStages, *(LockedFile + TEXT(".tmp-*")), true, false);
        IFileManager::Get().FindFiles(BackupStages, *(LockedBackup + TEXT(".tmp-*")), true, false);
        TestTrue(TEXT("Successful and rejected locked writes clean their owned staging files"),
                 PrimaryStages.IsEmpty() && BackupStages.IsEmpty());
        if (!Transient) {
            TestTrue(TEXT("Releasing a persistent lock permits a subsequent ordinary save"),
                     URiftProfileSubsystem::AtomicWrite(LockedFile, TEXT("after-release"), LockError));
            FFileHelper::LoadFileToString(PrimaryBytes, *LockedFile);
            FFileHelper::LoadFileToString(BackupBytes, *LockedBackup);
            TestEqual(TEXT("Subsequent save commits exact requested bytes"), PrimaryBytes,
                      FString(TEXT("after-release")));
            TestEqual(TEXT("Subsequent save backs up the unmodified pre-failure primary"), BackupBytes,
                      FString(TEXT("current")));
        }
        AddInfo(FString::Printf(TEXT("Real Windows %s %s-lock save: saved=%d, %.3fms, %s"), Stage,
                                Transient ? TEXT("released") : TEXT("persistent"), Saved,
                                Elapsed * 1000, Transient ? TEXT("recovered") : *FailureDetail));
    };
    ExerciseSaveLock(true, true);
    ExerciseSaveLock(true, false);
    ExerciseSaveLock(false, true);
    ExerciseSaveLock(false, false);
    {
        const FString ReadOnlyFile = Fixture(TEXT("-readonly-atomic.json"));
        const FString ReadOnlyBackup = ReadOnlyFile + TEXT(".bak");
        FString ReadOnlyError;
        if (TestTrue(TEXT("Read-only fixture creates previous bytes"),
                     URiftProfileSubsystem::AtomicWrite(ReadOnlyFile, TEXT("previous"), ReadOnlyError)) &&
            TestTrue(TEXT("Read-only fixture creates current bytes and previous backup"),
                     URiftProfileSubsystem::AtomicWrite(ReadOnlyFile, TEXT("current"), ReadOnlyError))) {
            const DWORD Attributes = ::GetFileAttributesW(*ReadOnlyBackup);
            TestTrue(TEXT("Permanent read-only backup attributes are available"),
                     Attributes != INVALID_FILE_ATTRIBUTES);
            ON_SCOPE_EXIT {
                if (Attributes != INVALID_FILE_ATTRIBUTES)
                    ::SetFileAttributesW(*ReadOnlyBackup, Attributes);
            };
            if (Attributes != INVALID_FILE_ATTRIBUTES &&
                TestTrue(TEXT("Actual backup becomes read-only"),
                         ::SetFileAttributesW(*ReadOnlyBackup, Attributes | FILE_ATTRIBUTE_READONLY) != 0)) {
                const double Started = FPlatformTime::Seconds();
                TestFalse(TEXT("Permanent read-only denial rejects atomic save"),
                          URiftProfileSubsystem::AtomicWrite(ReadOnlyFile, TEXT("replacement"), ReadOnlyError));
                const double Elapsed = FPlatformTime::Seconds() - Started;
                TestTrue(TEXT("Permanent access denial is distinguished from a positive sharing conflict"),
                         ReadOnlyError.Contains(TEXT("stage=backup-rename")) &&
                             ReadOnlyError.Contains(TEXT("Win32=5; sharingWin32=0; attempts=1")));
                TestTrue(TEXT("Permanent access denial returns within the bounded I/O allowance"), Elapsed < .5);
                FString PrimaryBytes, BackupBytes;
                FFileHelper::LoadFileToString(PrimaryBytes, *ReadOnlyFile);
                FFileHelper::LoadFileToString(BackupBytes, *ReadOnlyBackup);
                TestEqual(TEXT("Permanent read-only denial leaves primary bytes untouched"), PrimaryBytes,
                          FString(TEXT("current")));
                TestEqual(TEXT("Permanent read-only denial leaves backup bytes untouched"), BackupBytes,
                          FString(TEXT("previous")));
                TArray<FString> PrimaryStages, BackupStages;
                IFileManager::Get().FindFiles(PrimaryStages, *(ReadOnlyFile + TEXT(".tmp-*")), true, false);
                IFileManager::Get().FindFiles(BackupStages, *(ReadOnlyBackup + TEXT(".tmp-*")), true, false);
                TestTrue(TEXT("Permanent denial also removes only its owned staging files"),
                         PrimaryStages.IsEmpty() && BackupStages.IsEmpty());
                AddInfo(FString::Printf(TEXT("Real Windows permanent read-only save: %.3fms, %s"),
                                        Elapsed * 1000, *ReadOnlyError));
            }
        }
    }
#endif
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
    TestEqual(TEXT("Arena has exactly 24 columns"), rift::arena::Width, 24);
    TestEqual(TEXT("Arena has exactly 34 rows"), rift::arena::Height, 34);
    TSet<double> Columns, Rows;
    int32 Centers = 0;
    for (int32 Row = 0; Row < 34; ++Row)
        for (int32 Column = 0; Column < 24; ++Column) {
            const rift::Vec2 Center{-11.5 + Column, -16.5 + Row};
            const auto Snapped = rift::SnapToTile(Center);
            TestTrue(TEXT("Every authored tile center snaps to itself"),
                     Snapped.x == Center.x && Snapped.z == Center.z);
            Columns.Add(Snapped.x); Rows.Add(Snapped.z); ++Centers;
        }
    TestEqual(TEXT("All 816 tile centers exist"), Centers, 816);
    TestEqual(TEXT("Tile centers occupy 24 distinct columns"), Columns.Num(), 24);
    TestEqual(TEXT("Tile centers occupy 34 distinct rows"), Rows.Num(), 34);
    for (const auto Team : {rift::Team::Player, rift::Team::Enemy})
        for (const int32 Side : {-1, 1}) {
            rift::Match Edge(Options);
            const double Sign = Team == rift::Team::Player ? 1. : -1.;
            Edge.SetAether(Team, 10);
            TestTrue(TEXT("Actual paid hand accepts each mirrored outer tile"),
                     Edge.Play(Team, 0, {Side * 11.5, Sign * 16.5}));
            const auto Encoded = URiftReplaySubsystem::SnapshotJSON(Edge.State());
            rift::Snapshot EdgeParsed;
            TestTrue(TEXT("New-edge paid snapshots remain valid"),
                     URiftReplaySubsystem::SnapshotFromJSON(Encoded, EdgeParsed));
            TestEqual(TEXT("New-edge snapshots preserve actual deployment coordinates"),
                      Serialize(URiftReplaySubsystem::SnapshotJSON(EdgeParsed)), Serialize(Encoded));
            TestFalse(TEXT("Paid deployment rejects outside the new across boundary"),
                      Edge.Play(Team, 1, {Side * 12.001, Sign * 8.5}));
            TestFalse(TEXT("Paid deployment rejects outside the new Core-to-Core boundary"),
                      Edge.Play(Team, 1, {0, Sign * 17.001}));
        }
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
    rift::SpellCast Cast;
    Cast.playId = 88;
    Cast.team = rift::Team::Enemy;
    Cast.cardId = "meteor_shards";
    Cast.position = {2, -4};
    Cast.born = .2;
    Cast.impactAt = .95;
    State.spellCasts.push_back(Cast);
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
    auto Legacy = Clone(Original);
    Legacy->RemoveField(TEXT("spellCasts"));
    TestTrue(TEXT("Older snapshots without pending spell fields remain readable"),
             URiftReplaySubsystem::SnapshotFromJSON(Legacy, Parsed));
    TestTrue(TEXT("Older snapshots retain an empty pending spell queue"), Parsed.spellCasts.empty());
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
    Bad->SetArrayField(TEXT("spellCasts"), {MakeShared<FJsonValueBoolean>(false)});
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->GetArrayField(TEXT("spellCasts"))[0]->AsObject()->SetStringField(TEXT("cardId"), TEXT("ironclad"));
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->GetArrayField(TEXT("spellCasts"))[0]->AsObject()->SetNumberField(TEXT("impactAt"), .1);
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->GetArrayField(TEXT("spellCasts"))[0]->AsObject()->SetNumberField(TEXT("born"), .8);
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->GetArrayField(TEXT("spellCasts"))[0]->AsObject()->SetNumberField(TEXT("impactAt"),
                                                                     std::numeric_limits<double>::infinity());
    Invalid.Add(Bad);
    Bad = Clone(Original);
    auto DuplicateCasts = Bad->GetArrayField(TEXT("spellCasts"));
    const auto DuplicateCast = DuplicateCasts[0];
    DuplicateCasts.Add(DuplicateCast);
    Bad->SetArrayField(TEXT("spellCasts"), DuplicateCasts);
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
    M.Spawn(rift::Team::Player, "nova_flask", {1, 3});
    M.SetAether(rift::Team::Player, 10);
    const auto Tower = M.State().entities[1].id;
    M.SetTowerHP(Tower, 1234);
    for (const auto &E : M.DrainEvents())
        R->RecordEvent(E);
    M.Step(.15);
    for (const auto &E : M.DrainEvents())
        R->RecordEvent(E);
    const auto* Nova=rift::FindCard("nova_flask");
    if(!TestNotNull(TEXT("Nova has its captured delayed-impact definition"),Nova)) return false;
    double ImpactTime=-1;
    M.Step(.1+Nova->castDelay+.01-M.State().elapsed);
    for(const auto& E:M.DrainEvents()) {
        if(E.type=="spell_impact"&&E.cardId=="nova_flask")ImpactTime=E.time;
        R->RecordEvent(E);
    }
    if(!TestTrue(TEXT("Nova reaches its real delayed impact before the final sample"),ImpactTime>=.1+Nova->castDelay-1e-6))return false;
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
    TestEqual(TEXT("Nova does not damage troops at the cast timestamp"),DeadBats,0);
    TestEqual(TEXT("Tower edit reconstructed"), TowerHP, 1234.0);
    auto CountDeadBats=[&](){int32 Count=0;for(const auto& E:R->View.entities)if(E.cardId=="vampire_bats"&&E.dead)++Count;return Count;};
    R->Seek(float(ImpactTime-.001));
    TestEqual(TEXT("Nova still preserves all five troops immediately before impact"),CountDeadBats(),0);
    R->Seek(float(ImpactTime+.001));
    TestEqual(TEXT("All five Nova deaths reconstruct at the authoritative delayed impact"),CountDeadBats(),5);
    TestEqual(TEXT("Nova has no lingering zone after its delayed impact"), int32(R->View.hazards.size()), 0);
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftSpellCastReplayIntegrationTest, "Rift.Integration.SpellCastReplay", Flags)
bool FRiftSpellCastReplayIntegrationTest::RunTest(const FString &Parameters) {
    if (!Sandbox()) {
        AddError(TEXT("Use an isolated -RiftSaveRoot and -RiftAutomationSandbox."));
        return false;
    }
    auto *GI = NewObject<UGameInstance>(GEngine);
    GI->InitializeStandalone(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto *World = GI->GetWorld();
    auto *Replay = GI->GetSubsystem<URiftReplaySubsystem>();
    auto *Arena = World->SpawnActor<ARiftArenaPresentation>();
    if (!TestNotNull(TEXT("Actual spell presentation actor"), Arena))
        return false;
    Arena->DispatchBeginPlay();
    ON_SCOPE_EXIT {
        Replay->CloseReplay();
        Replay->FlushPendingWrites();
        World->DestroyWorld(false);
        GI->Shutdown();
        GEngine->DestroyWorldContext(World);
    };
    rift::MatchOptions Options;
    Options.aiEnabled = {false, false};
    rift::Match Match(Options);
    Replay->BeginRecording(Options, true);
    auto RecordEvents = [&]() {
        for (const auto &Event : Match.DrainEvents())
            Replay->RecordEvent(Event);
    };
    RecordEvents();
    Replay->Sample(Match.State());
    Match.Step(.1);
    Match.Spawn(rift::Team::Enemy, "ironclad", {8, -8});
    const uint64 Troop = Match.State().entities.back().id;
    uint64 Core = 0;
    rift::Vec2 CorePosition;
    double CoreHP = 0;
    for (const auto &Entity : Match.State().entities)
        if (Entity.kind == rift::EntityKind::Core && Entity.team == rift::Team::Enemy) {
            Core = Entity.id;
            CorePosition = Entity.position;
            CoreHP = Entity.hp;
        }
    Match.Spawn(rift::Team::Player, "meteor_shards", {8, -8});
    Match.Spawn(rift::Team::Player, "bullet_burst", CorePosition);
    RecordEvents();
    Match.Step(.15);
    RecordEvents();
    // This sample contains both pending casts; earlier seeks reconstruct them from events.
    Replay->Sample(Match.State());
    Match.Step(1.35);
    RecordEvents();
    Replay->EndRecording(Match.State(), true);
    if (!TestTrue(TEXT("Delayed spell recording saves"), Replay->FlushPendingWrites()) ||
        !TestTrue(TEXT("Delayed spell recording opens"), Replay->OpenReplay(Replay->LatestFilename)))
        return false;
    auto HP = [&](uint64 Id) {
        for (const auto &Entity : Replay->View.entities)
            if (Entity.id == Id)
                return Entity.hp;
        return -1.;
    };
    Replay->Seek(.15f);
    TestEqual(TEXT("Events reconstruct both spell windups before the first pending snapshot"),
              int32(Replay->View.spellCasts.size()), 2);
    TestEqual(TEXT("Meteor has no zone during the fall"), int32(Replay->View.hazards.size()), 0);
    TestEqual(TEXT("Meteor target remains healthy before impact"), HP(Troop),
              rift::FindCard("ironclad")->hp);
    TestEqual(TEXT("Bullet target remains healthy before impact"), HP(Core), CoreHP);
    if (Replay->View.spellCasts.size() == 2) {
        const auto CastPosition = rift::SnapToTile({8, -8});
        TestTrue(TEXT("Meteor replay preserves its fixed target and deadline"),
                 Replay->View.spellCasts[0].position.x == CastPosition.x &&
                 Replay->View.spellCasts[0].position.z == CastPosition.z &&
                 FMath::IsNearlyEqual(Replay->View.spellCasts[0].impactAt -
                                          Replay->View.spellCasts[0].born,
                                      .75, 1e-7));
        TestTrue(TEXT("Bullet replay preserves its authoritative deadline"),
                 FMath::IsNearlyEqual(Replay->View.spellCasts[1].impactAt -
                                          Replay->View.spellCasts[1].born,
                                      .30, 1e-7));
    }
    Replay->Seek(.35f);
    TestEqual(TEXT("Pending snapshot reconstructs both casts without duplicate events"),
              int32(Replay->View.spellCasts.size()), 2);
    Replay->SetSpeed(0);
    const double PausedTime = Replay->TimelinePosition();
    Replay->Advance(1.f);
    TestEqual(TEXT("Paused replay cannot advance either spell animation"),
              Replay->TimelinePosition(), PausedTime);
    TestEqual(TEXT("Paused replay cannot apply early damage"), HP(Core), CoreHP);
    Replay->Seek(.5f);
    TestTrue(TEXT("Bullet impact removes only its own pending cast"),
             Replay->View.spellCasts.size() == 1 &&
                 Replay->View.spellCasts[0].cardId == "meteor_shards");
    TestEqual(TEXT("Bullet damage occurs once at its delayed impact"), HP(Core),
              CoreHP - rift::FindCard("bullet_burst")->towerDamage);
    Replay->Seek(.9f);
    TestTrue(TEXT("Meteor impact removes its windup"), Replay->View.spellCasts.empty());
    TestEqual(TEXT("Meteor initial damage occurs at its delayed impact"), HP(Troop),
              rift::FindCard("ironclad")->hp - rift::FindCard("meteor_shards")->damage);
    TestEqual(TEXT("Meteor zone starts after the fall finishes"), int32(Replay->View.hazards.size()), 1);
    if (!Replay->View.hazards.empty()) {
        const auto &Hazard = Replay->View.hazards[0];
        TestTrue(TEXT("Meteor's complete five-second zone and first tick are relative to impact"),
                 FMath::IsNearlyEqual(Hazard.born, .85, 1e-6) &&
                     FMath::IsNearlyEqual(Hazard.nextTick, Hazard.born + 1, 1e-6) &&
                     FMath::IsNearlyEqual(Hazard.expires, Hazard.born + 5, 1e-6));
    }
    Replay->Seek(.15f);
    TestEqual(TEXT("Backward seek restores both winding casts"),
              int32(Replay->View.spellCasts.size()), 2);
    TestEqual(TEXT("Backward seek removes future Meteor zone"), int32(Replay->View.hazards.size()), 0);
    TestEqual(TEXT("Backward seek restores health before Bullet impact"), HP(Core), CoreHP);
    auto CheckImpactRewind = [&](float Before, int32 Rings, int32 Meshes, const TCHAR *Card) {
        Replay->Seek(Before);
        Arena->Tick(0);
        Replay->SetSpeed(1);
        Replay->Advance(.01f);
        Arena->Tick(0);
        auto After = Json(Arena->NiagaraDiagnosticsJSON());
        TestTrue(FString(Card) + TEXT(" actual forward playback emits impact debris"),
                 After.IsValid() && After->GetNumberField(TEXT("spellDebrisBodies")) > 0 &&
                     After->GetNumberField(TEXT("spellImpactEvents")) == 1 &&
                     After->GetArrayField(TEXT("recentSpellImpacts")).Num() == 1);
        const double AfterTime = Replay->TimelinePosition();
        // Eight milliseconds backwards crosses impact, below the old ten-millisecond reset threshold.
        Replay->Seek(Before + .002f);
        Arena->Tick(0);
        const double Rewind = AfterTime - Replay->TimelinePosition();
        TestTrue(FString(Card) + TEXT(" regression exercises a backward jump under ten milliseconds"),
                 Rewind > 0 && Rewind < .01);
        TestEqual(FString(Card) + TEXT(" backward impact seek restores actual targeting rings"),
                  Arena->SpellCastVisualCount(), Rings);
        TestEqual(FString(Card) + TEXT(" backward impact seek restores actual airborne meshes"),
                  Arena->SpellCastMeshCount(), Meshes);
        auto Rewound = Json(Arena->NiagaraDiagnosticsJSON());
        TestTrue(FString(Card) + TEXT(" backward impact seek removes all future debris and impact cues"),
                 Rewound.IsValid() && Rewound->GetNumberField(TEXT("spellDebrisBodies")) == 0 &&
                     Rewound->GetNumberField(TEXT("spellImpactEvents")) == 0 &&
                     Rewound->GetArrayField(TEXT("recentSpellImpacts")).IsEmpty());
    };
    CheckImpactRewind(.395f, 2, 12, TEXT("Bullet Burst"));
    CheckImpactRewind(.845f, 1, 5, TEXT("Meteor Shards"));
    Replay->CloseReplay();

    rift::Match Cleared(Options);
    Replay->BeginRecording(Options, true);
    for (const auto &Event : Cleared.DrainEvents())
        Replay->RecordEvent(Event);
    Replay->Sample(Cleared.State());
    Cleared.Step(.1);
    Cleared.Spawn(rift::Team::Player, "meteor_shards", {0, 0});
    Cleared.Step(.15);
    Cleared.ClearField();
    Cleared.Step(.25);
    for (const auto &Event : Cleared.DrainEvents())
        Replay->RecordEvent(Event);
    Replay->EndRecording(Cleared.State(), true);
    if (!TestTrue(TEXT("Cancelled spell recording saves"), Replay->FlushPendingWrites()) ||
        !TestTrue(TEXT("Cancelled spell recording opens"), Replay->OpenReplay(Replay->LatestFilename)))
        return false;
    Replay->Seek(.15f);
    TestEqual(TEXT("Cancelled replay still shows the cast before Clear Field"),
              int32(Replay->View.spellCasts.size()), 1);
    Replay->Seek(.3f);
    TestTrue(TEXT("Clear Field removes the pending animation before its deadline"),
             Replay->View.spellCasts.empty() && Replay->View.hazards.empty());
    Replay->CloseReplay();

    // An older archive has no pending-cast field or deadline. Its Meteor zone remains instant.
    rift::Match LegacyMatch(Options);
    LegacyMatch.Spawn(rift::Team::Enemy, "ironclad", {8, -8});
    const auto Initial = URiftReplaySubsystem::SnapshotJSON(LegacyMatch.State());
    Initial->RemoveField(TEXT("spellCasts"));
    Initial->SetNumberField(TEXT("eventSequence"), 0);
    auto LegacyEnd = Clone(Initial);
    LegacyEnd->SetNumberField(TEXT("time"), 1);
    LegacyEnd->SetNumberField(TEXT("eventSequence"), 1);
    auto Legacy = MakeShared<FJsonObject>();
    Legacy->SetNumberField(TEXT("formatVersion"), 1);
    Legacy->SetStringField(TEXT("model"), TEXT("rift-native-1"));
    Legacy->SetNumberField(TEXT("duration"), 1);
    Legacy->SetArrayField(TEXT("states"),
                         {MakeShared<FJsonValueObject>(Initial), MakeShared<FJsonValueObject>(LegacyEnd)});
    auto OldCast = MakeShared<FJsonObject>();
    OldCast->SetStringField(TEXT("type"), TEXT("card_play"));
    OldCast->SetStringField(TEXT("cardId"), TEXT("meteor_shards"));
    OldCast->SetNumberField(TEXT("sequence"), 1);
    OldCast->SetNumberField(TEXT("time"), .1);
    OldCast->SetNumberField(TEXT("team"), 0);
    OldCast->SetNumberField(TEXT("playId"), 55);
    OldCast->SetBoolField(TEXT("sandbox"), true);
    const auto OldVictim = LegacyMatch.State().entities.back();
    const uint64 OldVictimId = OldVictim.id;
    const double OldVictimHP = OldVictim.hp;
    auto OldDamage = MakeShared<FJsonObject>();
    OldDamage->SetStringField(TEXT("type"), TEXT("damage"));
    OldDamage->SetStringField(TEXT("cardId"), TEXT("meteor_shards"));
    OldDamage->SetStringField(TEXT("damageKind"), TEXT("initial"));
    OldDamage->SetNumberField(TEXT("sequence"), 2);
    OldDamage->SetNumberField(TEXT("time"), .1);
    OldDamage->SetNumberField(TEXT("team"), 0);
    OldDamage->SetNumberField(TEXT("targetTeam"), 1);
    OldDamage->SetNumberField(TEXT("target"), double(OldVictimId));
    OldDamage->SetNumberField(TEXT("targetKind"), double(OldVictim.kind));
    OldDamage->SetNumberField(TEXT("hp"), OldVictimHP - 262);
    OldDamage->SetNumberField(TEXT("amount"), 262);
    LegacyEnd->SetNumberField(TEXT("eventSequence"), 2);
    Legacy->SetArrayField(TEXT("events"),
                         {MakeShared<FJsonValueObject>(OldCast), MakeShared<FJsonValueObject>(OldDamage)});
    FString Error;
    const FString LegacyName = FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("-legacy-spell.json");
    if (!TestTrue(TEXT("Legacy spell fixture saves"), URiftProfileSubsystem::AtomicWrite(
                      FPaths::Combine(URiftProfileSubsystem::SaveRoot(), TEXT("Replays"), LegacyName),
                      Serialize(Legacy), Error)) ||
        !TestTrue(TEXT("Legacy archive still opens without spellCasts"), Replay->OpenReplay(LegacyName)))
        return false;
    Replay->Seek(.2f);
    TestTrue(TEXT("Old Meteor has no retroactively added windup"), Replay->View.spellCasts.empty());
    TestTrue(TEXT("Old Meteor does not invent missing historical zone mechanics"),Replay->View.hazards.empty());
    TestTrue(TEXT("Missing legacy zone mechanics have an explicit reconstruction limitation"),Replay->ReconstructionDiagnostics()->GetArrayField(TEXT("limitations")).Num()>0);
    TestEqual(TEXT("Old spell damage is reconstructed at its recorded time"),
              HP(OldVictimId), OldVictimHP - 262);
    Replay->CloseReplay();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftHistoricalReplayIntegrationTest,"Rift.Integration.HistoricalReplayBalance",Flags)
bool FRiftHistoricalReplayIntegrationTest::RunTest(const FString& Parameters) {
    if(!Sandbox()){AddError(TEXT("Use an isolated -RiftSaveRoot and -RiftAutomationSandbox."));return false;}
    auto* GI=NewObject<UGameInstance>();auto* Replay=NewObject<URiftReplaySubsystem>(GI);
    ON_SCOPE_EXIT {Replay->CloseReplay();Replay->FlushPendingWrites();};
    rift::MatchOptions Options;Options.aiEnabled={false,false};
    struct FAttackProof {rift::Event Event;rift::EntityKind SourceKind,TargetKind;bool StructuresOnly=false;};
    TSet<FString> LockCoverage;int32 RadiusChecks=0;
    // These records come from actual Match attacks and deployments. Initial and
    // terminal samples force the interior seek to reconstruct from events.
    for(int32 Scenario=0;Scenario<4;++Scenario) {
        Options.seed=60100+Scenario;rift::Match Match(Options);Replay->BeginRecording(Options,true);Replay->Sample(Match.State());
        auto Flush=[&](){for(const auto& Event:Match.DrainEvents())Replay->RecordEvent(Event);};Flush();Match.Step(.1);Flush();
        if(Scenario==0){TestTrue(TEXT("Crown fixture troop spawns"),Match.Spawn(rift::Team::Player,"ironclad",{-rift::arena::BridgeCenterX,-(rift::arena::GuardDepth-3.9)}));TestTrue(TEXT("Collision fixture full Stampede spawns"),Match.Spawn(rift::Team::Player,"stampede",{rift::arena::BridgeCenterX,6.5}));}
        else if(Scenario==1){TestTrue(TEXT("Melee fixture player spawns"),Match.Spawn(rift::Team::Player,"ironclad",{-10,4}));TestTrue(TEXT("Melee fixture enemy spawns"),Match.Spawn(rift::Team::Enemy,"ironclad",{-10,2.5}));}
        else {TestTrue(TEXT("Building fixture attacking troop spawns"),Match.Spawn(rift::Team::Player,Scenario==2?"ironclad":"rambeast",{-rift::arena::BridgeCenterX,-4.5}));TestTrue(TEXT("Building fixture enemy tower spawns"),Match.Spawn(rift::Team::Enemy,"archer_tower",{-rift::arena::BridgeCenterX,-7.2}));}
        const auto Spawned=Match.State().entities;Flush();TArray<FAttackProof> Attacks;TSet<uint64> SeenSources;
        for(int32 Step=0;Step<60;++Step) {
            Match.Step(.05);
            for(const auto& Event:Match.DrainEvents()) {
                Replay->RecordEvent(Event);if(Event.type!="attack"||SeenSources.Contains(Event.source))continue;
                const rift::Entity* Source=nullptr;const rift::Entity* Target=nullptr;
                for(const auto& Entity:Match.State().entities){if(Entity.id==Event.source)Source=&Entity;if(Entity.id==Event.target)Target=&Entity;}
                if(!Source||!Target)continue;SeenSources.Add(Event.source);const auto* Card=rift::FindEntityDefinition(Source->cardId);
                Attacks.Add({Event,Source->kind,Target->kind,Card&&Card->structuresOnly});
            }
        }
        Replay->EndRecording(Match.State(),true);
        if(!TestTrue(TEXT("Real attack fixture archive finishes writing"),Replay->FlushPendingWrites())||!TestTrue(TEXT("Real attack fixture archive reopens"),Replay->OpenReplay(Replay->LatestFilename)))return false;
        Replay->Seek(.1001f);
        for(const auto& Entity:Spawned)if(Entity.cardId=="stampede") {
            const auto Found=std::find_if(Replay->View.entities.begin(),Replay->View.entities.end(),[&](const auto& E){return E.id==Entity.id;});
            TestTrue(TEXT("Every recorded Stampede member retains its actual collision body"),Found!=Replay->View.entities.end()&&FMath::IsNearlyEqual(Found->radius,Entity.radius,1e-9));++RadiusChecks;
        }
        for(const auto& Proof:Attacks) {
            const bool CrownTarget=Proof.TargetKind==rift::EntityKind::Guard||Proof.TargetKind==rift::EntityKind::Core;
            const bool Hard=Proof.SourceKind==rift::EntityKind::Troop&&(CrownTarget||(Proof.StructuresOnly&&Proof.TargetKind==rift::EntityKind::Building));
            Replay->Seek(float(Proof.Event.time-.0001));
            auto Before=std::find_if(Replay->View.entities.begin(),Replay->View.entities.end(),[&](const auto& E){return E.id==Proof.Event.source;});
            TestTrue(TEXT("Recorded source is uncommitted before its first actual attack"),Before!=Replay->View.entities.end()&&Before->hardLock==0);
            Replay->Seek(float(Proof.Event.time+.0001));
            auto After=std::find_if(Replay->View.entities.begin(),Replay->View.entities.end(),[&](const auto& E){return E.id==Proof.Event.source;});
            TestTrue(TEXT("Replay reconstructs only authoritative first-attack commitments"),After!=Replay->View.entities.end()&&After->hardLock==(Hard?Proof.Event.target:0));
            if(Proof.SourceKind==rift::EntityKind::Guard||Proof.SourceKind==rift::EntityKind::Core)LockCoverage.Add(TEXT("crown-source"));
            else if(Proof.SourceKind==rift::EntityKind::Building)LockCoverage.Add(TEXT("building-source"));
            else if(CrownTarget)LockCoverage.Add(TEXT("troop-crown"));
            else if(Proof.TargetKind==rift::EntityKind::Troop)LockCoverage.Add(TEXT("troop-troop"));
            else if(Proof.TargetKind==rift::EntityKind::Building)LockCoverage.Add(Proof.StructuresOnly?TEXT("structure-building"):TEXT("troop-building"));
        }
        Replay->CloseReplay();
    }
    TestEqual(TEXT("All fifteen real Stampede spawn events exercise radius reconstruction"),RadiusChecks,rift::FindCard("stampede")->count);
    for(const TCHAR* Coverage:{TEXT("crown-source"),TEXT("building-source"),TEXT("troop-crown"),TEXT("troop-troop"),TEXT("structure-building"),TEXT("troop-building")})TestTrue(FString(TEXT("Actual attack fixture covers "))+Coverage,LockCoverage.Contains(Coverage));

    rift::Match Quiet(Options);Replay->BeginRecording(Options,true);Replay->Sample(Quiet.State());
    for(const auto& Event:Quiet.DrainEvents())Replay->RecordEvent(Event);Quiet.Step(.1);
    TestTrue(TEXT("Quiet actual Archer Tower deploys"),Quiet.Spawn(rift::Team::Player,"archer_tower",{-rift::arena::BridgeCenterX,7}));
    TestTrue(TEXT("Quiet actual Pulverizer deploys"),Quiet.Spawn(rift::Team::Player,"pulverizer",{rift::arena::BridgeCenterX,7}));
    for(const auto& Event:Quiet.DrainEvents())Replay->RecordEvent(Event);
    TMap<uint64,double> LiveBuildingHP;double QuietProbeTime=0;
    for(int32 Step=0;Step<20;++Step) {
        Quiet.Step(.05);for(const auto& Event:Quiet.DrainEvents())Replay->RecordEvent(Event);
        if(Step==9){QuietProbeTime=Quiet.State().elapsed;for(const auto& Entity:Quiet.State().entities)if(Entity.kind==rift::EntityKind::Building)LiveBuildingHP.Add(Entity.id,Entity.hp);}
    }
    Replay->EndRecording(Quiet.State(),true);
    if(!TestTrue(TEXT("Quiet building archive finishes writing"),Replay->FlushPendingWrites())||!TestTrue(TEXT("Quiet building archive opens"),Replay->OpenReplay(Replay->LatestFilename)))return false;
    Replay->Seek(float(QuietProbeTime));int32 DecayChecks=0;
    for(const auto& Entity:Replay->View.entities)if(const auto* Actual=LiveBuildingHP.Find(Entity.id)) {
        TestTrue(TEXT("Quiet replay HP matches actual authoritative building decay"),FMath::IsNearlyEqual(Entity.hp,*Actual,1e-4));
        const auto* Definition=rift::FindCard(Entity.cardId);TestTrue(TEXT("Both actual quiet buildings lose HP through their lifetime rule"),Definition&&*Actual<Definition->hp);++DecayChecks;
    }
    TestEqual(TEXT("Actual quiet HP parity covers both baseline buildings"),DecayChecks,2);Replay->CloseReplay();

    // Deliberately synthetic historical mechanics differ from the live roster.
    // Its hash is provenance metadata, not a claim to authenticate a real patch.
    rift::Match Empty(Options);auto Initial=Empty.State();Initial.aether[1]=10;
    auto PlayedSlot=std::find(Initial.hands[1].begin(),Initial.hands[1].end(),"ironclad");
    if(PlayedSlot==Initial.hands[1].end()){auto Queued=std::find(Initial.queues[1].begin(),Initial.queues[1].end(),"ironclad");if(!TestTrue(TEXT("Historical paid card exists in the actual eight-card cycle"),Queued!=Initial.queues[1].end()))return false;std::swap(Initial.hands[1][0],*Queued);PlayedSlot=Initial.hands[1].begin();}
    const int32 Slot=int32(PlayedSlot-Initial.hands[1].begin());auto Mid=Initial;Mid.elapsed=Mid.phaseElapsed=.6;Mid.timeRemaining=179.4;
    rift::Entity Victim;Victim.id=100;Victim.team=rift::Team::Enemy;Victim.cardId="ironclad";Victim.playId=101;Victim.position={0,6};Victim.hp=Victim.maxHp=901;Victim.radius=.65;Victim.born=.1;
    rift::Entity Building;Building.id=101;Building.team=rift::Team::Player;Building.kind=rift::EntityKind::Building;Building.cardId="pulverizer";Building.playId=103;Building.position={0,4};Building.hp=Building.maxHp=700;Building.radius=.9;Building.born=.15;
    Mid.entities.push_back(Victim);Mid.entities.push_back(Building);Mid.aether[1]=1;Mid.spent[1]=9;Mid.telemetry[1]["ironclad"].plays=Mid.telemetry[1]["ironclad"].spawns=1;Mid.telemetry[1]["ironclad"].spent=9;
    Mid.hands[1][Slot]=Initial.queues[1].front();Mid.queues[1].erase(Mid.queues[1].begin());Mid.queues[1].push_back("ironclad");
    Mid.entities[6].target=Mid.entities[6].hardLock=101;Mid.entities[6].cooldown=.8;Mid.entities[7].hp=607;
    rift::SpellCast Cast;Cast.playId=102;Cast.cardId="meteor_shards";Cast.position={0,4};Cast.born=.2;Cast.impactAt=.7;Mid.spellCasts.push_back(Cast);
    rift::Shockwave Wave;Wave.id=5;Wave.source=101;Wave.playId=103;Wave.cardId="pulverizer";Wave.origin={0,4};Wave.radius=Wave.speed=5;Wave.born=.3;Wave.expires=1.3;Wave.progress=1.5;Mid.shockwaves.push_back(Wave);
    auto Final=Mid;Final.elapsed=Final.phaseElapsed=2;Final.timeRemaining=178;Final.spellCasts.clear();Final.shockwaves.clear();Final.entities[6].cooldown=0;
    rift::SlowEffect Slow;Slow.source=101;Slow.playId=103;Slow.cardId="pulverizer";Slow.pct=.25;Slow.born=.8;Slow.until=2.8;
    Final.entities[6].slows.push_back(Slow);Final.entities[6].slowPct=.25;Final.entities[6].slowUntil=2.8;Final.entities[6].slowSource=103;
    rift::Hazard Hazard;Hazard.playId=102;Hazard.cardId="meteor_shards";Hazard.position={0,4};Hazard.radius=4.5;Hazard.born=.7;Hazard.expires=3.2;Hazard.nextTick=2.2;Hazard.ticks=2;Final.hazards.push_back(Hazard);
    const FString HistoricalHash=FString::ChrN(64,TEXT('a'));
    auto SetHistoricalBalance=[&](const TSharedPtr<FJsonObject>& Balance) {
        Balance->SetStringField(TEXT("manifestHash"),HistoricalHash);Balance->SetStringField(TEXT("manifestVersion"),TEXT("synthetic-historical-fixture"));
        for(auto Value:Balance->GetArrayField(TEXT("cards"))) {
            auto Card=Value->AsObject();const FString Id=Card->GetStringField(TEXT("id"));
            if(Id==TEXT("ironclad")){Card->SetNumberField(TEXT("cost"),9);Card->SetNumberField(TEXT("hp"),901);Card->SetNumberField(TEXT("collisionRadius"),.65);Card->SetNumberField(TEXT("targetMask"),4);Card->SetBoolField(TEXT("structuresOnly"),true);}
            if(Id==TEXT("pulverizer")){Card->SetNumberField(TEXT("collisionRadius"),.9);Card->SetNumberField(TEXT("damage"),77);Card->SetNumberField(TEXT("splash"),5);Card->SetNumberField(TEXT("shockwaveSpeed"),5);Card->SetNumberField(TEXT("slowPct"),.25);Card->SetNumberField(TEXT("slowDuration"),2);Card->SetNumberField(TEXT("hpDecayPerSecond"),0);}
            if(Id==TEXT("meteor_shards")){Card->SetNumberField(TEXT("castDelay"),.5);Card->SetNumberField(TEXT("spellRadius"),4.5);Card->SetNumberField(TEXT("dotDamage"),41);Card->SetNumberField(TEXT("dotDuration"),2.5);Card->SetNumberField(TEXT("dotInterval"),.5);}
        }
    };
    Replay->BeginRecording(Options,true);SetHistoricalBalance(Replay->Recording->GetObjectField(TEXT("balance")));Replay->Sample(Initial);uint64 Sequence=0;
    auto Event=[&](const char* Type,double Time,const char* CardId){rift::Event E;E.sequence=++Sequence;E.type=Type;E.time=Time;E.cardId=CardId;return E;};
    auto E=Event("card_play",.1,"ironclad");E.team=rift::Team::Enemy;E.playId=101;E.amount=9;E.aetherBefore=10;E.aetherAfter=1;E.handIndex=Slot;E.handAfter=Mid.hands[1];Replay->RecordEvent(E);
    E=Event("entity_spawn",.1,"ironclad");E.team=rift::Team::Enemy;E.source=100;E.playId=101;E.position=Victim.position;E.hp=E.maxHp=901;Replay->RecordEvent(E);
    E=Event("entity_spawn",.15,"pulverizer");E.source=101;E.playId=103;E.position=Building.position;E.hp=E.maxHp=700;Replay->RecordEvent(E);
    E=Event("spell_cast",.2,"meteor_shards");E.playId=102;E.position={0,4};E.until=.7;Replay->RecordEvent(E);
    E=Event("shockwave_launch",.3,"pulverizer");E.source=101;E.target=100;E.playId=103;E.position={0,4};E.amount=77;E.until=1.3;Replay->RecordEvent(E);
    E=Event("attack",.4,"ironclad");E.team=rift::Team::Enemy;E.source=100;E.target=101;E.playId=101;E.amount=93;E.position=Victim.position;Replay->RecordEvent(E);
    E=Event("damage",.4,"ironclad");E.team=rift::Team::Enemy;E.source=100;E.target=101;E.playId=101;E.targetKind=rift::EntityKind::Building;E.amount=E.requested=93;E.hp=607;Replay->RecordEvent(E);Replay->Sample(Mid);
    E=Event("spell_impact",.7,"meteor_shards");E.playId=102;E.position={0,4};Replay->RecordEvent(E);
    E=Event("slow",.8,"pulverizer");E.source=101;E.target=100;E.targetTeam=rift::Team::Enemy;E.playId=103;E.amount=.25;E.until=2.8;Replay->RecordEvent(E);
    for(double Tick:{1.2,1.7}){E=Event("hazard_tick",Tick,"meteor_shards");E.playId=102;Replay->RecordEvent(E);}
    Replay->EndRecording(Final,true);if(!TestTrue(TEXT("Synthetic archive template finishes writing"),Replay->FlushPendingWrites()))return false;
    const FString Exported=Fixture(TEXT("-historical-template.json"));if(!TestTrue(TEXT("Synthetic archive template exports"),Replay->ExportReplay(Replay->LatestFilename,Exported)))return false;
    FString Bytes;FFileHelper::LoadFileToString(Bytes,*Exported);auto Historical=Json(Bytes);
    const FString LiveHash=UTF8_TO_TCHAR(rift::CardManifestHash().c_str());FString Error;
    if(!TestTrue(TEXT("Self-consistent captured historical balance validates"),URiftReplaySubsystem::ValidateRecording(Historical,Error))){AddError(Error);return false;}
    const FString Name=FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT("-historical-balance.json");const FString Path=FPaths::Combine(URiftProfileSubsystem::SaveRoot(),TEXT("Replays"),Name);
    if(!TestTrue(TEXT("Historical fixture writes atomically"),URiftProfileSubsystem::AtomicWrite(Path,Serialize(Historical.ToSharedRef()),Error))||!TestTrue(TEXT("Historical balance opens despite changed current mechanics"),Replay->OpenReplay(Name)))return false;
    Replay->Seek(.4f);auto Find=[&](uint64 Id)->const rift::Entity*{for(const auto& Entity:Replay->View.entities)if(Entity.id==Id)return &Entity;return nullptr;};
    TestTrue(TEXT("Historical event spawn uses captured body radius"),Find(100)&&Find(100)->radius==.65);
    TestEqual(TEXT("Historical paid cost uses captured mechanics"),Replay->View.spent[1],9.0);
    TestTrue(TEXT("Historical spell windup uses its recorded impact time"),Replay->View.spellCasts.size()==1&&Replay->View.spellCasts[0].impactAt==.7);
    TestTrue(TEXT("Historical wave reconstructs captured radius and speed"),Replay->View.shockwaves.size()==1&&Replay->View.shockwaves[0].radius==5&&Replay->View.shockwaves[0].speed==5);
    Replay->Seek(.45f);TestTrue(TEXT("Historical structure-only mask drives building commitment despite current general targeting"),Find(100)&&Find(100)->hardLock==101&&rift::TargetMaskFor(*rift::FindCard("ironclad"))==5);
    Replay->Seek(.9f);TestTrue(TEXT("Historical slow uses captured amount and expiry"),Find(100)&&Find(100)->slowPct==.25&&Find(100)->slowUntil==2.8);
    TestTrue(TEXT("Synthetic historical zero-decay policy does not inherit today's building drain"),Find(101)&&Find(101)->hp==607);
    TestTrue(TEXT("Historical DOT zone uses captured geometry and duration"),Replay->View.hazards.size()==1&&Replay->View.hazards[0].radius==4.5&&Replay->View.hazards[0].expires==3.2);
    Replay->Seek(1.25f);TestTrue(TEXT("Historical DOT tick advances by captured interval"),Replay->View.hazards.size()==1&&Replay->View.hazards[0].ticks==1&&FMath::IsNearlyEqual(Replay->View.hazards[0].nextTick,1.7,1e-9));
    auto Diagnostics=Replay->ReconstructionDiagnostics();TestTrue(TEXT("Historical diagnostics identify captured definitions"),Diagnostics->GetBoolField(TEXT("hasCapturedDefinitions")));
    TestEqual(TEXT("Historical diagnostics identify the archive's provenance hash"),Diagnostics->GetStringField(TEXT("manifestHash")),HistoricalHash);
    TestEqual(TEXT("Historical reads leave current canonical definitions untouched"),FString(UTF8_TO_TCHAR(rift::CardManifestHash().c_str())),LiveHash);
    auto Broken=Clone(Historical.ToSharedRef());Broken->GetArrayField(TEXT("events"))[4]->AsObject()->SetNumberField(TEXT("amount"),78);
    TestFalse(TEXT("Captured wave/event damage mismatch still rejects"),URiftReplaySubsystem::ValidateRecording(Broken,Error));
    Broken=Clone(Historical.ToSharedRef());Broken->GetArrayField(TEXT("states"))[1]->AsObject()->GetArrayField(TEXT("shockwaves"))[0]->AsObject()->SetNumberField(TEXT("progress"),2);
    TestFalse(TEXT("Captured wave progress inconsistent with its timestamp rejects"),URiftReplaySubsystem::ValidateRecording(Broken,Error));
    Broken=Clone(Historical.ToSharedRef());Broken->GetArrayField(TEXT("events"))[3]->AsObject()->SetNumberField(TEXT("until"),.8);
    TestFalse(TEXT("Captured spell/event delay mismatch still rejects"),URiftReplaySubsystem::ValidateRecording(Broken,Error));
    Broken=Clone(Historical.ToSharedRef());Broken->GetArrayField(TEXT("events"))[8]->AsObject()->SetNumberField(TEXT("amount"),.3);
    TestFalse(TEXT("Captured slow/event amount mismatch still rejects"),URiftReplaySubsystem::ValidateRecording(Broken,Error));
    Broken=Clone(Historical.ToSharedRef());Broken->GetObjectField(TEXT("balance"))->GetArrayField(TEXT("cards"))[0]->AsObject()->SetNumberField(TEXT("hp"),std::numeric_limits<double>::infinity());
    TestFalse(TEXT("Unsafe captured numerical definition rejects"),URiftReplaySubsystem::ValidateRecording(Broken,Error));
    auto Legacy=Clone(Historical.ToSharedRef());Legacy->RemoveField(TEXT("balance"));const FString LegacyName=FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT("-legacy-numeric-balance.json");
    if(!TestTrue(TEXT("Legacy numerical archive without definitions remains readable"),URiftReplaySubsystem::ValidateRecording(Legacy,Error))||!TestTrue(TEXT("Legacy numerical fixture writes"),URiftProfileSubsystem::AtomicWrite(FPaths::Combine(URiftProfileSubsystem::SaveRoot(),TEXT("Replays"),LegacyName),Serialize(Legacy),Error))||!TestTrue(TEXT("Legacy numerical archive opens"),Replay->OpenReplay(LegacyName)))return false;
    Diagnostics=Replay->ReconstructionDiagnostics();TestFalse(TEXT("Legacy diagnostics do not invent captured definitions"),Diagnostics->GetBoolField(TEXT("hasCapturedDefinitions")));
    TestTrue(TEXT("Legacy missing-mechanics limitation is explicit"),Diagnostics->GetArrayField(TEXT("limitations")).Num()>0);
    return !HasAnyErrors();
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
    FString PaidReplayFilename;
    int64 PaidReplayBytes=0;
    auto RecordAIMatch = [&](bool Congestion) {
        rift::MatchOptions AIOptions;
        // Record a complete populated paid match under the current balance. The
        // Developer fixture separately holds towers alive for overtime/congestion.
        AIOptions.seed = Congestion ? 32 : 11;
        AIOptions.aiEnabled = {true, true};
        AIOptions.aiStyles = {"control", "counter"};
        const std::vector<std::string> SwarmDeck{"ironclad",    "twin_blades",  "archer_tower",
                                                 "frost_fang",  "vampire_bats", "sky_manta",
                                                 "storm_raven", "meteor_shards"};
        AIOptions.decks = {SwarmDeck, SwarmDeck};
        rift::Match AI(AIOptions);
        Replay->BeginRecording(AIOptions, Congestion);
        int32 RecordedCount = 0, Samples = 1, Peak = 6;
        std::array<int32,2> PaidPlays{};
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
                if(Event.type=="card_play"&&!Event.sandbox)++PaidPlays[int(Event.team)];
            }
            Replay->Sample(AI.State());
            ++Samples;
            Peak = FMath::Max(Peak, int32(AI.State().entities.size()));
            ZeroCrownOvertime |= AI.State().phase == rift::Phase::Overtime && AI.State().crowns[0] == 0 &&
                                 AI.State().crowns[1] == 0;
        }
        TestTrue(TEXT("Complete native AI history reaches a terminal result with sustained real paid casts"),
                 AI.State().phase == rift::Phase::Finished && AI.State().elapsed>=180 &&
                     PaidPlays[0]>=20&&PaidPlays[1]>=20&&AI.State().spent[0]>0&&AI.State().spent[1]>0);
        if(Congestion)TestTrue(TEXT("Developer congestion still covers zero-crown overtime and paid economy"),
            AI.State().elapsed>180&&ZeroCrownOvertime&&AI.State().spent[0]>100&&AI.State().spent[1]>100);
        if (Congestion)
            TestTrue(TEXT("Reachable Developer swarm field exercises substantial congestion"), Peak >= 40);
        else
            TestTrue(TEXT("Complete ordinary AI match includes a populated live field"), Peak >= 20);
        const double Started = FPlatformTime::Seconds();
        Replay->EndRecording(AI.State(), false);
        const double AIQueueMS = (FPlatformTime::Seconds() - Started) * 1000;
        TestTrue(TEXT("Populated match finalization also queues without a game-thread stall"),
                 AIQueueMS < 250);
        if (Congestion) {
            // Persistent entity telemetry makes this reachable Developer workload
            // exceed the archive's decoded bound. Reject the complete recording;
            // silently dropping body history would make its analysis misleading.
            const FString RejectedName=Replay->LatestQueuedFilename;
            TestFalse(TEXT("Oversized Developer history fails safely before unbounded serialization"),Replay->FlushPendingWrites());
            TestTrue(TEXT("Oversized history reports its decoded JSON limit"),Replay->LastError.Contains(TEXT("256 MB")));
            TestTrue(TEXT("Oversized history never publishes a partial filename"),Replay->LatestFilename.IsEmpty());
            TestFalse(TEXT("Oversized history never writes a partial native archive"),
                IFileManager::Get().FileExists(*FPaths::Combine(URiftProfileSubsystem::SaveRoot(),TEXT("Replays"),RejectedName)));
            const FString PaidPath=FPaths::Combine(URiftProfileSubsystem::SaveRoot(),TEXT("Replays"),PaidReplayFilename);
            TestEqual(TEXT("Prior complete paid archive is unchanged by size rejection"),IFileManager::Get().FileSize(*PaidPath),PaidReplayBytes);
            TestTrue(TEXT("Prior complete paid archive still opens after size rejection"),Replay->OpenReplay(PaidReplayFilename));
            Replay->CloseReplay();
            AddInfo(FString::Printf(TEXT("Oversized Developer/paid AI history rejected without truncation: %.3fs, peak %d, %d events, %d samples, %.3fms queue."),
                AI.State().elapsed,Peak,RecordedCount,Samples+1,AIQueueMS));
            return;
        }
        TestTrue(TEXT("Populated complete native archive saves successfully"), Replay->FlushPendingWrites());
        const FString SavedName = Replay->LatestFilename;
        const int64 StoredSize = IFileManager::Get().FileSize(
            *FPaths::Combine(URiftProfileSubsystem::SaveRoot(), TEXT("Replays"), SavedName));
        PaidReplayFilename=SavedName;
        PaidReplayBytes=StoredSize;
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
        {
            const FString Plain = Fixture(TEXT("-populated-plain.json"));
            TestTrue(TEXT("Complete paid archive exports a lossless ordinary JSON document"),
                     Replay->ExportReplay(SavedName, Plain));
            JSONSize = IFileManager::Get().FileSize(*Plain);
            TestTrue(TEXT("Complete paid JSON export stays in its decoded input bound"),
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
                TestEqual(TEXT("Reimport preserves all paid match events"),
                          ImportedReplay->EventsNear(AI.State().elapsed, AI.State().elapsed).Num(),
                          RecordedCount);
                TestEqual(TEXT("Reimport preserves every paid match snapshot"),
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftReplaySizeBudgetIntegrationTest,"Rift.Integration.ReplaySizeBudget",Flags)
bool FRiftReplaySizeBudgetIntegrationTest::RunTest(const FString& Parameters) {
    if (!Sandbox()) {AddError(TEXT("Replay size fixture requires an isolated automation sandbox."));return false;}
    auto TextObject=MakeShared<FJsonObject>();
    const FString Text=TEXT("Rift \"\n\t\\ \u5b88\u671b\u00e9 \n\n\n\n\n\n\n\n\n\n");
    TextObject->SetStringField(TEXT("text"),Text);
    TextObject->SetNumberField(TEXT("fraction"),1.0/3.0);
    TArray<uint8> Exact,Bounded;
    FString Error;
    if (!TestTrue(TEXT("Bounded UTF-8 serializer preserves an ordinary document"),
        URiftReplaySubsystem::SerializeRecording(TextObject,Exact,Error))) return false;
    TestTrue(TEXT("Exact decoded byte boundary is accepted"),
        URiftReplaySubsystem::SerializeRecording(TextObject,Bounded,Error,Exact.Num()));
    TestTrue(TEXT("Exact boundary output is lossless"),Exact==Bounded);
    TestFalse(TEXT("Escaping and UTF-8 expansion cannot exceed the actual byte cap"),
        URiftReplaySubsystem::SerializeRecording(TextObject,Bounded,Error,Exact.Num()-1));
    TestTrue(TEXT("Rejected serialization clears the partial output"),Bounded.IsEmpty()&&!Error.IsEmpty());
    FUTF8ToTCHAR Converted(reinterpret_cast<const UTF8CHAR*>(Exact.GetData()),Exact.Num());
    TSharedPtr<FJsonObject> Decoded;
    if (TestTrue(TEXT("Bounded UTF-8 output parses as ordinary JSON"),
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Converted.Length(),Converted.Get())),Decoded))) {
        TestEqual(TEXT("Unicode and control characters survive serialization"),Decoded->GetStringField(TEXT("text")),Text);
        TestEqual(TEXT("Double precision survives serialization"),Decoded->GetNumberField(TEXT("fraction")),1.0/3.0);
    }
    auto* GI=NewObject<UGameInstance>();
    auto* Replay=NewObject<URiftReplaySubsystem>(GI);
    rift::MatchOptions Options;Options.aiEnabled={false,false};Options.decks={rift::DefaultDeck(),rift::DefaultDeck()};
    rift::Match Match(Options);
    for(int I=0;I<128;++I)Match.Spawn(rift::Team::Player,"ironclad",{double(I%12)-5.5,8+double(I/12)*.25});
    auto Snapshot=URiftReplaySubsystem::SnapshotJSON(Match.State());
    Replay->BeginRecording(Options,true);
    Replay->Recording->SetNumberField(TEXT("duration"),0);
    Replay->Recording->SetArrayField(TEXT("events"),{});
    Replay->Recording->SetObjectField(TEXT("result"),Snapshot);
    auto Value=MakeShared<FJsonValueObject>(Snapshot);
    Replay->Recording->SetArrayField(TEXT("states"),{Value});
    TestTrue(TEXT("Single captured snapshot with persistent body telemetry is valid"),
        URiftReplaySubsystem::ValidateRecording(Replay->Recording,Error));
    TArray<TSharedPtr<FJsonValue>> Repeated;
    for(int I=0;I<16000;++I)Repeated.Add(Value);
    Replay->Recording->SetArrayField(TEXT("states"),Repeated);
    TestFalse(TEXT("Aggregate persistent telemetry is bounded before parsing or serialization"),
        URiftReplaySubsystem::ValidateRecording(Replay->Recording,Error));
    TestTrue(TEXT("Aggregate telemetry rejection reports decoded size"),Error.Contains(TEXT("256 MB")));
    Replay->BeginRecording(Options,true);
    const auto BeforeBodies=Match.State().entityTelemetry[0].size();
    bool WithinBudget=true;int32 Accounted=0;
    while(WithinBudget&&Accounted<20000) {WithinBudget=Replay->AccountRecordingObject(Snapshot);++Accounted;}
    TestFalse(TEXT("Incremental capture budget includes every persistent body record"),WithinBudget);
    TestTrue(TEXT("Rejected capture releases its recording buffers"),Replay->RecordedSamples.IsEmpty()&&Replay->RecordedEvents.IsEmpty());
    TestEqual(TEXT("Recording rejection preserves live individual analytics"),Match.State().entityTelemetry[0].size(),BeforeBodies);
    Replay->EndRecording(Match.State(),true);
    const FString RejectedFilename=Replay->LatestQueuedFilename;
    TestFalse(TEXT("Oversized capture queues a recoverable save failure"),Replay->FlushPendingWrites());
    TestTrue(TEXT("Recoverable save failure retains size diagnostics"),Replay->LastError.Contains(TEXT("256 MB")));
    TestFalse(TEXT("Rejected capture does not write a partial archive"),
        IFileManager::Get().FileExists(*FPaths::Combine(URiftProfileSubsystem::SaveRoot(),TEXT("Replays"),RejectedFilename)));
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
    // NativeConstruct opens Home and intentionally closes any loaded replay.
    // Build the real UI before recording/opening the endpoint fixture.
    auto* UI = CreateWidget<URiftUIWidget>(GI, URiftUIWidget::StaticClass());
    TSharedPtr<SWidget> Slate = UI->TakeWidget();
    Match->StartMatch(true, false);
    Match->SetSpeed(0);
    // Training keeps the enemy AI enabled; isolate the intended terminal edit
    // through the real developer AI setter before advancing the fractional clock.
    World->GetSubsystem<URiftAISubsystem>()->SetEnabled(1, false);
    // This actual fractional match end rounds down when reduced to a float.
    Match->Simulation()->Step(299.05);
    Match->FlushEvents();
    TestTrue(TEXT("Fractional fixture remains live until its explicit Core destruction"),
             Match->Simulation()->State().phase != rift::Phase::Finished);
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
    int32 CrossedTowerEnds = 0, CrossedMatchEnds = 0;
    const auto Handle =
        Match->OnEvent.AddLambda([&](const rift::Event &E) {
            CrossedEvents.push_back(E.sequence);
            if (E.type == "tower_destroy")
                ++CrossedTowerEnds;
            if (E.type == "match_end")
                ++CrossedMatchEnds;
        });
    TestTrue(TEXT("World replay opens"), Replay->OpenReplay(Filename));
    const double RecordedEnd = Replay->CurrentAnalysis()->GetNumberField(TEXT("time"));
    TestTrue(TEXT("Fractional replay fixture exercises downward float endpoint rounding"),
             RecordedEnd > double(Replay->Duration()));
    TestEqual(TEXT("Replay preserves the exact recorded double duration"), Replay->TimelineDuration(), RecordedEnd);
    const float BeforeEnd = std::nextafter(Replay->Duration(), 0.f);
    Replay->Seek(BeforeEnd);
    TestTrue(TEXT("The preceding representable slider position cannot expose the final result early"),
             Match->ViewState()->phase != rift::Phase::Finished);
    int32 EarlyTerminalEvents = 0;
    for (const auto& Event : Replay->EventsNear(Replay->TimelinePosition(), 5)) {
        const FString Type = Event->GetStringField(TEXT("type"));
        if (Type == TEXT("tower_destroy") || Type == TEXT("match_end"))
            ++EarlyTerminalEvents;
    }
    TestEqual(TEXT("Exact event queries do not include terminal events before their timestamp"), EarlyTerminalEvents, 0);
    TestTrue(TEXT("Seeking emits no historical presentation events"), CrossedEvents.empty());
    Replay->Advance(.1f);
    TestTrue(TEXT("Forward playback broadcasts crossed terminal events"), CrossedEvents.size() >= 4);
    TestEqual(TEXT("Forward playback includes the final tower destruction once"), CrossedTowerEnds, 1);
    TestEqual(TEXT("Forward playback includes the final match-end event once"), CrossedMatchEnds, 1);
    TestEqual(TEXT("Forward playback reaches the exact fractional endpoint"), Replay->TimelinePosition(), RecordedEnd);
    TestTrue(TEXT("Final recorded snapshot is shown instead of its preceding regulation/overtime state"),
             Match->ViewState()->phase == rift::Phase::Finished && Match->ViewState()->winner == 0 &&
                 Match->ViewState()->crowns[0] == 3 && Match->ViewState()->resultReason == "core_destroyed");
    TestEqual(TEXT("Playback pauses at the recorded end"), Replay->Speed(), 0.f);
    TestTrue(TEXT("Presentation events remain chronological"),
             std::is_sorted(CrossedEvents.begin(), CrossedEvents.end()));
    const auto Count = CrossedEvents.size();
    Replay->Advance(1.f);
    TestEqual(TEXT("Paused final playback cannot repeat its terminal effects"), int32(CrossedEvents.size()), int32(Count));
    Replay->Seek(0);
    TestEqual(TEXT("Backward seek stays silent"), int32(CrossedEvents.size()), int32(Count));
    Replay->Seek(Replay->Duration());
    TestEqual(TEXT("Float slider maximum maps to the exact double recorded end"), Replay->TimelinePosition(), RecordedEnd);
    TestTrue(TEXT("Explicit endpoint seeking restores the finished snapshot"), Match->ViewState()->phase == rift::Phase::Finished);
    TestEqual(TEXT("Endpoint seeking also stays silent"), int32(CrossedEvents.size()), int32(Count));
    UI->Navigate(TEXT("ReplayView"));
    auto* TowerBookmark = ActiveButton(UI, TEXT("TOWER FALL"));
    TestNotNull(TEXT("Actual replay UI retains the terminal fractional tower-fall bookmark"), TowerBookmark);
    Replay->Seek(BeforeEnd);
    if (TowerBookmark)
        TowerBookmark->OnClicked.Broadcast();
    const auto* BookmarkState = Match->ViewState();
    TestTrue(TEXT("Tower-fall bookmark reaches the recorded finished result"),
             BookmarkState && BookmarkState->phase == rift::Phase::Finished);
    TestEqual(TEXT("Bookmark seeking emits no historical presentation events"), int32(CrossedEvents.size()), int32(Count));
    UI->ReleaseSlateResources(true);
    Slate.Reset();
    Replay->CloseReplay();
    Match->OnEvent.Remove(Handle);
    Match->LeaveMatch();
    World->DestroyWorld(false);
    GI->Shutdown();
    GEngine->DestroyWorldContext(World);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftExpansionMechanicsIntegrationTest,"Rift.Integration.ExpansionMechanics",Flags)
bool FRiftExpansionMechanicsIntegrationTest::RunTest(const FString& Parameters) {
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox"))) {
        AddError(TEXT("Expansion mechanics requires isolated saves and RiftAutomationSandbox."));return false;
    }
    auto* GI=NewObject<UGameInstance>();auto* Replay=NewObject<URiftReplaySubsystem>(GI);
    auto Find=[](const rift::Snapshot& State,uint64 Id)->const rift::Entity* {
        for(const auto& Body:State.entities)if(Body.id==Id)return &Body;return nullptr;
    };
    auto Mutable=[](rift::Match& Match,uint64 Id)->rift::Entity* {
        for(auto& Body:const_cast<rift::Snapshot&>(Match.State()).entities)if(Body.id==Id)return &Body;return nullptr;
    };
    auto Id=[](const rift::Match& Match,const char* Card,rift::Team Team)->uint64 {
        for(const auto& Body:Match.State().entities)if(Body.cardId==Card&&Body.team==Team&&!Body.dead)return Body.id;return 0;
    };
    for(int32 Scenario=0;Scenario<5;++Scenario) {
        rift::MatchOptions Options;Options.seed=69000+Scenario;Options.aiEnabled={false,false};
        const char* Card=Scenario==0?"iron_giant":Scenario==1?"bone_warden":Scenario==2?"shadow_surge":Scenario==3?"archer_tower":"rambeast";
        Options.decks[0]={Card,"ironclad","ember_archer","twin_blades","arc_mage","sky_manta","bullet_burst","nova_flask"};
        if(Card==std::string("archer_tower"))Options.decks[0][7]="meteor_shards";
        Options.decks[1]=rift::DefaultDeck();rift::Match Match(Options);
        for(auto& Body:const_cast<rift::Snapshot&>(Match.State()).entities)Body.stunUntil=100;
        Replay->BeginRecording(Options,true);Replay->Sample(Match.State());
        Match.SetAether(rift::Team::Player,10);
        uint64 Subject=0,Enemy=0,Crown=0;double Duration=Scenario==3?25.1:Scenario==4?2.:1.6;
        if(Scenario==2) {
            TestTrue(TEXT("Local cloak paid fixture creates friendly body"),Match.Spawn(rift::Team::Player,"ironclad",{.5,8.5}));
            Subject=Id(Match,"ironclad",rift::Team::Player);Mutable(Match,Subject)->stunUntil=100;
            TestTrue(TEXT("Local cloak fixture launches real ranged enemy attack"),Match.Spawn(rift::Team::Enemy,"ember_archer",{.5,2.5}));
            Enemy=Id(Match,"ember_archer",rift::Team::Enemy);
            for(auto& Body:const_cast<rift::Snapshot&>(Match.State()).entities)
                if(Body.team==rift::Team::Enemy&&Body.kind==rift::EntityKind::Guard&&Body.lane==1) {
                    Crown=Body.id;Body.target=Body.hardLock=Body.forcedTarget=Subject;
                }
            TestTrue(TEXT("Actual paid Shadow Surge cast"),Match.Play(rift::Team::Player,0,{.5,8.5}));
        } else {
            TestTrue(TEXT("Actual paid expansion body cast"),Match.Play(rift::Team::Player,0,{.5,8.5}));
            Subject=Id(Match,Card,rift::Team::Player);
            if(!TestTrue(TEXT("Paid body actually exists"),Subject!=0))continue;
            if(Scenario!=4)Mutable(Match,Subject)->stunUntil=100;
            if(Scenario==0)TestTrue(TEXT("Actual delayed375 incoming shield packet"),Match.Spawn(rift::Team::Enemy,"nova_flask",{.5,8.5}));
            if(Scenario==1) {
                Mutable(Match,Subject)->hp=1;
                TestTrue(TEXT("Actual delayed lethal child-spawn packet"),Match.Spawn(rift::Team::Enemy,"bullet_burst",{.5,8.5}));
            }
        }
        auto Feed=[&]() {for(const auto& Event:Match.DrainEvents())Replay->RecordEvent(Event);Replay->Sample(Match.State());};
        Feed();double Probe=Scenario==0?.9:Scenario==1?.35:Scenario==2?.3:Scenario==3?24.9:1.8;
        rift::Snapshot LiveProbe;bool Probed=false;
        for(int32 Tick=0;Tick<int32(Duration*60+.5);++Tick) {
            Match.Step(1./60.);Feed();
            if(!Probed&&Match.State().elapsed+1e-8>=Probe){LiveProbe=Match.State();Probed=true;}
        }
        Replay->EndRecording(Match.State(),true);
        if(!TestTrue(TEXT("Actual expansion recording writes"),Replay->FlushPendingWrites())||
           !TestTrue(TEXT("Captured schema2 expansion recording opens"),Replay->OpenReplay(Replay->LatestFilename)))continue;
        TestEqual(TEXT("Replay captured expansion mechanics schema"),int32(Replay->ReconstructionDiagnostics()->GetNumberField(TEXT("balanceSchema"))),2);
        Replay->Seek(float(LiveProbe.elapsed));const auto* Live=Find(LiveProbe,Subject);const auto* Restored=Find(Replay->View,Subject);
        if(Scenario==0) {
            if(TestNotNull(TEXT("Shield live body remains"),Live)&&TestNotNull(TEXT("Shield replay body remains"),Restored)) {
                TestEqual(TEXT("Live shield-breaking packet leaves1000bodyHP"),Live->hp,1000.);
                TestEqual(TEXT("Replay preserves protected bodyHP"),Restored->hp,1000.);
                TestEqual(TEXT("Replay preserves broken shield"),Restored->shieldHP,0.);
                TestTrue(TEXT("Replay preserves actual shield-break timestamp"),FMath::IsNearlyEqual(Restored->shieldBrokenAt,Live->shieldBrokenAt,1e-7));
            }
            const auto& Tele=Replay->View.telemetry[0].at("iron_giant");
            TestEqual(TEXT("Replay shield actual loss"),Tele.shieldDamageTaken,310.);
            TestEqual(TEXT("Replay shield full packet absorbed"),Tele.shieldAbsorbed,375.);
            TestEqual(TEXT("Replay shield overflow discarded"),Tele.overflowPrevented,65.);
            TestEqual(TEXT("Replay body loss excludes shield packet"),Tele.damageTaken,0.);
        } else if(Scenario==1) {
            int32 Children=0;
            for(const auto& Body:Replay->View.entities)if(Body.parentEntityId==Subject) {
                ++Children;TestEqual(TEXT("Replay subordinate definition"),FString(UTF8_TO_TCHAR(Body.cardId.c_str())),FString(TEXT("bone_warden_skeleton")));
                TestEqual(TEXT("Replay child original paid attribution"),FString(UTF8_TO_TCHAR(Body.attributionCardId.c_str())),FString(TEXT("bone_warden")));
                TestTrue(TEXT("Replay child original paid play identity"),Live&&Body.playId==Live->playId);
                TestTrue(TEXT("Replay entity observations include actual child identity"),Replay->View.entityTelemetry[0].count(Body.id)==1);
            }
            TestEqual(TEXT("Replay reconstructs all four immediate death children"),Children,4);
            const auto& Tele=Replay->View.telemetry[0].at("bone_warden");
            TestEqual(TEXT("Replay keeps one paid parent play"),Tele.plays,uint64(1));
            TestEqual(TEXT("Replay keeps one paid-body spawn"),Tele.spawns,uint64(1));
            TestEqual(TEXT("Replay keeps four subordinate spawns"),Tele.childSpawns,uint64(4));
            TestEqual(TEXT("Replay keeps one parent-body death"),Tele.deaths,double(1));
        } else if(Scenario==2) {
            if(TestNotNull(TEXT("Cloaked live subject"),Live)&&TestNotNull(TEXT("Cloaked replay subject"),Restored))
                TestTrue(TEXT("Replay retains exact local cloak expiration"),FMath::IsNearlyEqual(Restored->invisibleUntil,Live->invisibleUntil,1e-7)&&Restored->invisibleUntil>Replay->TimelinePosition());
            const auto* Drop=Find(Replay->View,Crown);
            TestTrue(TEXT("Replay clears the previously locked Crown target"),Drop&&Drop->target==0&&Drop->hardLock==0&&Drop->forcedTarget==0);
            Replay->Seek(.5f);Restored=Find(Replay->View,Subject);
            TestTrue(TEXT("Replay committed projectile damages invisible subject"),Restored&&Restored->hp==822-152&&Restored->invisibleUntil>Replay->TimelinePosition());
            Replay->Seek(1.3f);Restored=Find(Replay->View,Subject);
            TestTrue(TEXT("Replay local cloak expires without borrowing current targeting"),Restored&&Restored->invisibleUntil==0);
        } else if(Scenario==3) {
            if(TestNotNull(TEXT("Live Archer before hard expiry"),Live)&&TestNotNull(TEXT("Replay Archer before hard expiry"),Restored))
                TestTrue(TEXT("Replay uses captured25HP per second decay"),FMath::IsNearlyEqual(Restored->hp,Live->hp,.005));
            Replay->Seek(25.05f);Restored=Find(Replay->View,Subject);
            TestTrue(TEXT("Replay hard expiry kills Archer while body HP would remain"),Restored&&Restored->dead&&Restored->hp==0);
        } else if(TestNotNull(TEXT("Live charged Ram"),Live)&&TestNotNull(TEXT("Replay charged Ram"),Restored)) {
            TestTrue(TEXT("Actual Ram movement accumulates approved1.5second threshold"),Live->charged&&Live->chargeTime>1.5);
            TestTrue(TEXT("Replay carries actual charged movement state"),Restored->charged&&Restored->chargeTime>1.5);
        }
        Replay->CloseReplay();
    }
    return !HasAnyErrors();
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
        METRIC(ShockwaveSpeed, shockwaveSpeed)
        METRIC(SplashRadius, splash)
        METRIC(Lifetime, lifetime)
        METRIC(Footprint, footprint)
        METRIC(TowerDamage, towerDamage) METRIC(SpellRadius, spellRadius) METRIC(ChargeDamage, chargeDamage)
            METRIC(CastDelay, castDelay)
        METRIC(CollisionRadius, collisionRadius) METRIC(ChargeMoveSeconds, chargeMoveSeconds)
        METRIC(ChargedMoveSpeed, chargedMoveSpeed) METRIC(HPDecayPerSecond, hpDecayPerSecond)
        METRIC(InitialAttackCooldown, initialAttackCooldown) METRIC(ShieldHP, shieldHP)
        METRIC(InvisibilityDuration, invisibilityDuration) METRIC(DotStructureDamage, dotStructureDamage)
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
        TestEqual(Id + TEXT(" target mask"),D->TargetMask,int32(C.targetMask));
        TestEqual(Id + TEXT(" child count"),D->DeathSpawnCount,C.deathSpawnCount);
        TestEqual(Id + TEXT(" child definition"),D->DeathSpawnId,FString(UTF8_TO_TCHAR(C.deathSpawnId.c_str())));
        TestEqual(Id + TEXT(" hard expiry"),D->HardExpiry,C.hardExpiry);
        TestEqual(Id + TEXT(" shield protection"),D->ShieldOverflowProtected,C.shieldOverflowProtected);
        TestEqual(Id + TEXT(" local/global cloak scope"),D->GlobalFriendlyInvisibility,C.globalFriendlyInvisibility);
        TestEqual(Id + TEXT(" cloak lock drop"),D->BreakExistingLocks,C.breakExistingLocks);
        TestNotNull(Id + TEXT(" card illustration"), D->Illustration.Get());
        if ((!C.spell && !C.building)||C.shockwaveSpeed>0) {
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
        if (C.building&&C.shockwaveSpeed<=0)
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
