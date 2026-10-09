#if WITH_DEV_AUTOMATION_TESTS
#include "RiftGameMode.h"
#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "RiftReplaySubsystem.h"
#include "RiftUIWidget.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/InputComponent.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Widgets/SWidget.h"

namespace
{
    template<class T> TArray<T*> BattleWidgets(UWidget* Root)
    {
        TArray<T*> Found;if(auto* Value=Cast<T>(Root))Found.Add(Value);
        if(auto* Panel=Cast<UPanelWidget>(Root))for(int32 I=0;I<Panel->GetChildrenCount();++I)Found.Append(BattleWidgets<T>(Panel->GetChildAt(I)));
        return Found;
    }
    URiftActionButton* BattleButton(URiftUIWidget* UI,const FString& Label)
    {
        for(auto* Button:BattleWidgets<URiftActionButton>(UI->GetRootWidget()))for(auto* Text:BattleWidgets<UTextBlock>(Button))if(Text->GetText().ToString()==Label)return Button;
        return nullptr;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftBattleInputRoutingTest,"Rift.Integration.BattleInputRouting",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftBattleInputRoutingTest::RunTest(const FString& Parameters)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox")))
    {AddError(TEXT("Battle input fixture requires isolated RiftSaveRoot/UserDir and RiftAutomationSandbox."));return false;}
    auto* GI=NewObject<UGameInstance>(GEngine);GI->InitializeStandalone(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto* World=GI->GetWorld();auto* Match=World->GetSubsystem<URiftMatchSubsystem>();
    auto* UI=CreateWidget<URiftUIWidget>(GI,URiftUIWidget::StaticClass());auto Slate=UI->TakeWidget();
    ON_SCOPE_EXIT
    {
        Match->LeaveMatch();GI->GetSubsystem<URiftReplaySubsystem>()->FlushPendingWrites();
        UI->ReleaseSlateResources(true);World->DestroyWorld(false);GI->Shutdown();GEngine->DestroyWorldContext(World);
    };
    auto* Controller=World->SpawnActor<ARiftPlayerController>();
    if(!TestNotNull(TEXT("Production controller can spawn in the isolated game world"),Controller))return false;
    Controller->Interface=UI;Controller->SetupInputComponent();
    auto Key=[&](FKey KeyValue)
    {
        for(const auto& Binding:Controller->InputComponent->KeyBindings)if(Binding.Chord.Key==KeyValue&&Binding.KeyEvent==IE_Pressed){Binding.KeyDelegate.Execute(KeyValue);return true;}
        AddError(TEXT("Production key binding is absent: ")+KeyValue.ToString());return false;
    };
    auto Click=[&](const TCHAR* Label)
    {auto* Button=BattleButton(UI,Label);if(!TestNotNull(FString(TEXT("Production battle control "))+Label,Button))return false;Button->OnClicked.Broadcast();return true;};

    FVector2D GroundTile=FVector2D::ZeroVector;
    for(double X:{-double(rift::arena::HalfWidth),double(rift::arena::HalfWidth)})for(double Y:{-double(rift::arena::HalfHeight),double(rift::arena::HalfHeight)})
    {
        TestTrue(TEXT("Exact board boundary remains clickable"),Controller->GroundPointToTile(URiftMatchSubsystem::WorldPoint({X,Y}),GroundTile));
        TestTrue(TEXT("Boundary clicks preserve the updated half-cell centers"),GroundTile.Equals(FVector2D(X<0?-rift::arena::LastTileX:rift::arena::LastTileX,Y<0?-rift::arena::LastTileZ:rift::arena::LastTileZ),.001));
    }
    const FVector2D LastValidTile=GroundTile;
    for(const FVector& Point:{FVector(-rift::arena::HalfWidth*100.-.1,0,0),FVector(rift::arena::HalfWidth*100.+.1,0,0),FVector(0,-rift::arena::HalfHeight*100.-.1,0),FVector(0,rift::arena::HalfHeight*100.+.1,0)})
    {
        TestFalse(TEXT("Decorative ground outside every board edge cannot snap into a deployment"),Controller->GroundPointToTile(Point,GroundTile));
        TestTrue(TEXT("Rejected ground cannot supply a replacement deployment tile"),GroundTile.Equals(LastValidTile,.001));
    }

    Key(EKeys::One);TestEqual(TEXT("Hand hotkey leaves Home selection empty"),UI->SelectedHand(),-1);
    Key(EKeys::RightMouseButton);TestFalse(TEXT("Right click on Home does not open Settings"),UI->IsLiveBattleView());
    Match->StartMatch(true);UI->SetBattleView();
    Key(EKeys::Two);TestEqual(TEXT("Hand hotkey selects during live battle"),UI->SelectedHand(),1);
    Controller->BeginCardDrag();TestFalse(TEXT("A card press without pointer travel does not activate a drag ghost"),Controller->IsDraggingCard());
    Controller->FinishCardDrag(false);TestEqual(TEXT("A simple hand release preserves selection for click deployment"),UI->SelectedHand(),1);
    Controller->FinishCardDrag(true);TestEqual(TEXT("A repeated release cannot cast or clear a merely selected card"),UI->SelectedHand(),1);
    Key(EKeys::RightMouseButton);TestEqual(TEXT("Right click cancels the selected hand card"),UI->SelectedHand(),-1);
    Key(EKeys::RightMouseButton);TestTrue(TEXT("Right click without a card keeps the battle open"),UI->CanAcceptBattleInput());

    Match->SetSpeed(.5f);Key(EKeys::Escape);
    TestFalse(TEXT("Escape pause menu closes deployment input"),UI->CanAcceptBattleInput());
    TestEqual(TEXT("Escape pauses the authoritative simulation"),Match->GetSpeed(),0.f);
    Key(EKeys::Three);TestEqual(TEXT("Hand hotkey is ignored beneath the pause menu"),UI->SelectedHand(),-1);
    Key(EKeys::F9);TestFalse(TEXT("Training shortcut cannot close the pause menu and unpause battle"),UI->CanAcceptBattleInput());
    TestEqual(TEXT("Training shortcut beneath the menu keeps simulation paused"),Match->GetSpeed(),0.f);
    Key(EKeys::Escape);TestTrue(TEXT("Second Escape restores live battle input"),UI->CanAcceptBattleInput());
    TestEqual(TEXT("Closing the pause menu restores its exact prior speed"),Match->GetSpeed(),.5f);
    Key(EKeys::Escape);if(!Click(TEXT("SETTINGS")))return false;
    TestFalse(TEXT("Paused battle settings remains outside live deployment input"),UI->CanAcceptBattleInput());
    Key(EKeys::Escape);TestTrue(TEXT("Escape from battle settings returns to the same live battle"),UI->CanAcceptBattleInput());
    TestEqual(TEXT("Escape from settings restores prior simulation speed"),Match->GetSpeed(),.5f);
    Match->SetSpeed(0.f);Key(EKeys::Escape);Key(EKeys::Escape);
    TestEqual(TEXT("Closing a pause menu keeps a manually paused training fixture paused"),Match->GetSpeed(),0.f);

    Key(EKeys::F9);
    const auto Fields=BattleWidgets<UEditableTextBox>(UI->GetRootWidget());
    if(!TestEqual(TEXT("Training provides one tower health editor"),Fields.Num(),1))return false;
    UComboBoxString* TowerChoice=nullptr;
    for(auto* Combo:BattleWidgets<UComboBoxString>(UI->GetRootWidget()))if(Combo->GetOptionAtIndex(0).Contains(TEXT("Friendly Core")))TowerChoice=Combo;
    if(!TestNotNull(TEXT("Training provides the actual tower selector"),TowerChoice))return false;
    TestEqual(TEXT("Initial friendly Core selection shows its real 3600 HP"),Fields[0]->GetText().ToString(),FString(TEXT("3600")));
    int32 GuardChoice=INDEX_NONE;for(int32 I=0;I<TowerChoice->GetOptionCount();++I)if(TowerChoice->GetOptionAtIndex(I).Contains(TEXT("Friendly Left Guard")))GuardChoice=I;
    if(!TestTrue(TEXT("Training includes the friendly left Guard"),GuardChoice!=INDEX_NONE))return false;
    TowerChoice->SetSelectedIndex(GuardChoice);Slate->Tick(FGeometry(),0,.11f);
    TestEqual(TEXT("Changing the tower selector fills actual Guard HP"),Fields[0]->GetText().ToString(),FString(TEXT("2250")));
    Fields[0]->SetText(FText::FromString(TEXT("700")));Slate->Tick(FGeometry(),.11,.11f);
    TestEqual(TEXT("Live training readout preserves health under edit"),Fields[0]->GetText().ToString(),FString(TEXT("700")));
    if(!Click(TEXT("SET TOWER HP")))return false;
    bool GuardEdited=false;for(const auto& Entity:Match->ViewState()->entities)if(Entity.id==uint64(FCString::Atoi64(*TowerChoice->GetSelectedOption())))GuardEdited=Entity.hp==700;
    TestTrue(TEXT("Tower control edits the chosen Guard rather than defaulting to Core"),GuardEdited);
    if(!Click(TEXT("FRIENDLY AI OFF")))return false;
    TestTrue(TEXT("Friendly AI control reaches the real simulation"),Match->ViewState()->ai[0].enabled);
    if(!Click(TEXT("FRIENDLY AI ON")))return false;
    TestFalse(TEXT("Friendly AI can return to manual control"),Match->ViewState()->ai[0].enabled);
    const FString OpeningReadout=World->GetSubsystem<URiftAISubsystem>()->Readout();
    TestTrue(TEXT("AI readout distinguishes no observed opponent cards"),OpeningReadout.Contains(TEXT("No cards observed")));
    World->GetSubsystem<URiftDeveloperSubsystem>()->ChangeAether(0,0,true);
    const auto* Played=rift::FindCard(Match->ViewState()->hands[0][0]);const FString PlayedName=UTF8_TO_TCHAR(Played->name.c_str());
    TestTrue(TEXT("Observed-cycle fixture makes a legal paid deployment"),Match->PlayCard(0,{0,8.5}));
    const FString Readout=World->GetSubsystem<URiftAISubsystem>()->Readout();
    TestTrue(TEXT("AI readout includes the actually observed paid card"),Readout.Contains(TEXT("Observed cycle: ")+PlayedName));

    auto* Simulation=Match->Simulation();Simulation->SetAIEnabled(rift::Team::Player,false);Simulation->SetAIEnabled(rift::Team::Enemy,false);Simulation->ClearField();
    Simulation->Step(181.-Simulation->State().elapsed);Match->FlushEvents();UI->SetBattleView();
    TestTrue(TEXT("An actual 0–0 match reaches overtime"),Match->ViewState()->phase==rift::Phase::Overtime);
    TestTrue(TEXT("Manually paused overtime still accepts training placement"),UI->CanAcceptBattleInput());
    Key(EKeys::Four);TestEqual(TEXT("Hand hotkeys remain available in overtime"),UI->SelectedHand(),3);
    TestFalse(TEXT("Overtime selection provides a real preview card before phase freeze"),UI->SelectedCardId().IsEmpty());
    Simulation->Step(301.-Simulation->State().elapsed);Match->FlushEvents();Slate->Tick(FGeometry(),.22,.11f);
    TestTrue(TEXT("An actual empty overtime reaches tiebreaker"),Match->ViewState()->phase==rift::Phase::Tiebreaker);
    TestFalse(TEXT("Frozen-combat tiebreaker closes deployment and drag input"),UI->CanAcceptBattleInput());
    TestEqual(TEXT("Tiebreaker clears the previously armed hand slot"),UI->SelectedHand(),-1);
    TestTrue(TEXT("Tiebreaker supplies no selected card for a misleading placement preview"),UI->SelectedCardId().IsEmpty());
    Key(EKeys::One);TestEqual(TEXT("Hand keys cannot rearm deployment during tiebreaker"),UI->SelectedHand(),-1);
    Simulation->Step(20);Match->FlushEvents();Slate->Tick(FGeometry(),.33,.11f);
    TestTrue(TEXT("Actual tiebreaker drain reaches a finished result"),Match->ViewState()->phase==rift::Phase::Finished);
    TestFalse(TEXT("Finished result closes deployment and drag input"),UI->CanAcceptBattleInput());
    Key(EKeys::Two);TestEqual(TEXT("Hand keys cannot arm deployment beneath the result"),UI->SelectedHand(),-1);

    Match->Tick(0); // Run the terminal live-host path before waiting on its archive.
    auto* Replay=GI->GetSubsystem<URiftReplaySubsystem>();auto* Profile=GI->GetSubsystem<URiftProfileSubsystem>();
    if(!TestTrue(TEXT("Actual finished input fixture saves its complete replay"),Replay->FlushPendingWrites()))return false;
    const FString Archived=Replay->LatestFilename;
    if(!TestTrue(TEXT("A real saved replay opens for the session-handoff fixture"),Replay->OpenReplay(Archived)))return false;
    Replay->Seek(Replay->Duration()*.5f);UI->Navigate(TEXT("ReplayView"));
    TestFalse(TEXT("Recorded replay remains outside deployment input"),UI->CanAcceptBattleInput());
    Key(EKeys::One);TestEqual(TEXT("Recorded replay ignores hand keys"),UI->SelectedHand(),-1);
    if(!Click(TEXT("ANALYSIS")))return false;
    TestTrue(TEXT("Analysis preserves its real loaded replay for review"),Replay->IsPlaying()&&Replay->CurrentAnalysis().IsValid());
    if(!Click(TEXT("Loadout"))||!Click(TEXT("TEST DECK")))return false;
    if(!TestTrue(TEXT("Test Deck starts a fresh authoritative training match"),Match->IsActive()&&Match->IsTraining()&&Match->ViewState()->phase==rift::Phase::Regulation))return false;
    TestEqual(TEXT("A fresh training match has its own normal speed"),Match->GetSpeed(),1.f);
    TestFalse(TEXT("Starting live training closes the prior playback session"),Replay->IsPlaying());
    TestFalse(TEXT("Starting live training releases prior replay analysis"),Replay->CurrentAnalysis().IsValid());
    TestTrue(TEXT("Closing playback preserves the saved recording in the profile"),Profile->ReplayFiles.Contains(Archived));
    TestTrue(TEXT("Live ViewState belongs to the new simulation"),Match->ViewState()==&Match->Simulation()->State());
    Match->Tick(.25f);const double LiveElapsed=Match->ViewState()->elapsed;
    TestTrue(TEXT("Fresh training advances through the live simulation"),LiveElapsed>.20&&LiveElapsed<.26);
    Replay->Advance(.25f);Replay->Seek(999);
    TestTrue(TEXT("Stale playback calls cannot take ownership of the fresh live view"),Match->ViewState()==&Match->Simulation()->State()&&FMath::IsNearlyEqual(Match->ViewState()->elapsed,LiveElapsed,.0001));
    Key(EKeys::Escape);if(!Click(TEXT("SETTINGS")))return false;Key(EKeys::Escape);
    TestTrue(TEXT("Escape from new training settings returns to that battle rather than old playback"),UI->CanAcceptBattleInput()&&Match->IsActive()&&!Replay->IsPlaying());
    TestEqual(TEXT("Returning from settings restores fresh training speed"),Match->GetSpeed(),1.f);

    // A terminal result must survive the HUD's archive-ready rebuild. Only
    // the explicit Play Again action is allowed to create the next match.
    for(const auto DestroyedTeam:{rift::Team::Enemy,rift::Team::Player})
    {
        Match->StartMatch(true);UI->SetBattleView();
        const auto* ResultSimulation=Match->Simulation();const auto ResultSeed=ResultSimulation->State().seed;
        rift::EntityId Core=0;for(const auto& Entity:ResultSimulation->State().entities)if(Entity.team==DestroyedTeam&&Entity.kind==rift::EntityKind::Core)Core=Entity.id;
        const int32 MatchesBefore=Profile->Matches;
        Match->Simulation()->SetTowerHP(Core,0);Match->FlushEvents();
        if(!TestTrue(TEXT("A real Core destruction produces its finished result"),Match->ViewState()->phase==rift::Phase::Finished))return false;
        const int32 ExpectedWinner=DestroyedTeam==rift::Team::Enemy?0:1;
        TestEqual(TEXT("Core destruction preserves the actual winner"),Match->ViewState()->winner,ExpectedWinner);
        TestEqual(TEXT("The terminal host records this result once"),Profile->Matches,MatchesBefore+1);
        for(int32 I=0;I<5;++I){Match->Tick(.25f);Slate->Tick(FGeometry(),1.+I*.11,.11f);}
        if(!TestTrue(TEXT("The result HUD cannot silently start another match"),Match->ViewState()->phase==rift::Phase::Finished&&Match->ViewState()->seed==ResultSeed))return false;
        if(!TestTrue(TEXT("The result replay finishes saving"),Replay->FlushPendingWrites()))return false;
        Slate->Tick(FGeometry(),2.,.11f);
        TestTrue(TEXT("Archive-ready result rebuild retains the same finished match"),Match->ViewState()->phase==rift::Phase::Finished&&Match->ViewState()->seed==ResultSeed);
        TestEqual(TEXT("HUD refresh and archive completion cannot count the result twice"),Profile->Matches,MatchesBefore+1);
        const FString ExpectedTitle=ExpectedWinner==0?TEXT("VICTORY"):TEXT("DEFEAT");bool HasResultTitle=false;
        for(auto* Text:BattleWidgets<UTextBlock>(UI->GetRootWidget()))HasResultTitle|=Text->GetText().ToString()==ExpectedTitle;
        TestTrue(TEXT("The live result shows the correct Victory or Defeat title"),HasResultTitle);
        TestNotNull(TEXT("A saved terminal result exposes its actual analysis"),BattleButton(UI,TEXT("MATCH ANALYSIS")));
        {
            // Route actual acceptance events through Slate's preprocessor
            // pipeline. Automated
            // captures must consume controller repeats and keyboard releases,
            // while a deliberate direct fixture action remains usable.
            auto& App=FSlateApplication::Get();auto Filter=ARiftGameMode::MakeCaptureInputFilter();
            App.RegisterInputPreProcessor(Filter,0);
            ON_SCOPE_EXIT{App.UnregisterInputPreProcessor(Filter);};
            for(const FKey Accept:{EKeys::Gamepad_FaceButton_Bottom,EKeys::Enter})
            {
                TestTrue(TEXT("Automated input guard consumes Slate accept presses"),App.ProcessKeyDownEvent(FKeyEvent(Accept,FModifierKeysState(),uint32(0),false,0,0)));
                TestTrue(TEXT("Automated input guard consumes held accept repeats"),App.ProcessKeyDownEvent(FKeyEvent(Accept,FModifierKeysState(),uint32(0),true,0,0)));
                TestTrue(TEXT("Automated input guard consumes accept releases"),App.ProcessKeyUpEvent(FKeyEvent(Accept,FModifierKeysState(),uint32(0),false,0,0)));
            }
            TestTrue(TEXT("Hardware accept input cannot restart an automated terminal capture"),Match->ViewState()->phase==rift::Phase::Finished&&Match->ViewState()->seed==ResultSeed);
            if(!Click(TEXT("PLAY AGAIN")))return false;
        }
        TestTrue(TEXT("Only explicit Play Again starts the next live regulation match"),Match->ViewState()->phase==rift::Phase::Regulation&&UI->CanAcceptBattleInput());
    }
    UI->Navigate(TEXT("Home"));Key(EKeys::Four);TestEqual(TEXT("Leaving battle restores menu key isolation"),UI->SelectedHand(),-1);
    return true;
}
#endif
