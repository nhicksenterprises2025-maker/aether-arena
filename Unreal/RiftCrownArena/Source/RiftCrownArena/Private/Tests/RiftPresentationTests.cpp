#if WITH_DEV_AUTOMATION_TESTS
#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "RiftReplaySubsystem.h"
#include "RiftUIPrimitives.h"
#include "RiftUIWidget.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "Widgets/SWidget.h"

namespace
{
    template<class T> TArray<T*> PresentationWidgets(UWidget* Root)
    {
        TArray<T*> Found;if(auto* Value=Cast<T>(Root))Found.Add(Value);
        if(auto* Panel=Cast<UPanelWidget>(Root))for(int32 I=0;I<Panel->GetChildrenCount();++I)Found.Append(PresentationWidgets<T>(Panel->GetChildAt(I)));
        return Found;
    }
    template<class T> T* NamedPresentationWidget(URiftUIWidget* UI,const TCHAR* Prefix)
    {
        for(auto* Widget:PresentationWidgets<T>(UI->GetRootWidget()))if(Widget->GetName().StartsWith(Prefix))return Widget;
        return nullptr;
    }
    URiftActionButton* PresentationButton(URiftUIWidget* UI,const FString& Label)
    {
        for(auto* Button:PresentationWidgets<URiftActionButton>(UI->GetRootWidget()))for(auto* Text:PresentationWidgets<UTextBlock>(Button))if(Text->GetText().ToString()==Label)return Button;
        return nullptr;
    }
    bool HasPresentationText(URiftUIWidget* UI,const FString& Value)
    {
        for(auto* Text:PresentationWidgets<UTextBlock>(UI->GetRootWidget()))if(Text->GetText().ToString().Contains(Value))return true;
        return false;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftPresentationIntegrationTest,"Rift.Integration.Presentation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftPresentationIntegrationTest::RunTest(const FString& Parameters)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox")))
    {AddError(TEXT("Presentation fixture requires isolated RiftSaveRoot/UserDir and RiftAutomationSandbox."));return false;}
    auto* GI=NewObject<UGameInstance>(GEngine);GI->InitializeStandalone(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto* World=GI->GetWorld();auto* Match=World->GetSubsystem<URiftMatchSubsystem>();auto* Replay=GI->GetSubsystem<URiftReplaySubsystem>();auto* Profile=GI->GetSubsystem<URiftProfileSubsystem>();
    const auto OriginalPreset=Profile->Presets[0];const FString OriginalActive=Profile->ActivePreset;
    auto* UI=CreateWidget<URiftUIWidget>(GI,URiftUIWidget::StaticClass());auto Slate=UI->TakeWidget();
    ON_SCOPE_EXIT
    {
        Match->LeaveMatch();Replay->FlushPendingWrites();Profile->Presets[0]=OriginalPreset;Profile->ActivePreset=OriginalActive;Profile->Save();UI->ReleaseSlateResources(true);World->DestroyWorld(false);GI->Shutdown();GEngine->DestroyWorldContext(World);
    };
    if(!TestTrue(TEXT("Isolated presentation fixture selects a canonical known deck"),Profile->SaveDeck(0,TEXT("Presentation QA"),URiftProfileSubsystem::DefaultDeck())))return false;
    auto Click=[&](const TCHAR* Label){auto* Button=PresentationButton(UI,Label);if(!TestNotNull(FString(TEXT("Active production presentation control "))+Label,Button))return false;Button->OnClicked.Broadcast();return true;};
    auto Refresh=[&](){Slate->Tick(FGeometry(),0,.11f);};

    if(!Click(TEXT("Help")))return false;
    TestTrue(TEXT("Help opens the field manual through production navigation"),HasPresentationText(UI,TEXT("THE FIELD MANUAL")));
    TestTrue(TEXT("Manual explains confirm input, legal pockets and exact phase rules"),HasPresentationText(UI,TEXT("same legal tile twice"))&&HasPresentationText(UI,TEXT("pockets never reach the enemy Core"))&&HasPresentationText(UI,TEXT("0–0 score alone unlocks overtime")));
    TestTrue(TEXT("Manual does not claim Training suppresses the preserved profile accounting"),!HasPresentationText(UI,TEXT("does not count toward")));
    if(!Click(TEXT("Home"))||!Click(TEXT("ENTER BATTLE")))return false;
    World->GetSubsystem<URiftAISubsystem>()->SetEnabled(0,false);World->GetSubsystem<URiftAISubsystem>()->SetEnabled(1,false);Refresh();
    auto* Next=NamedPresentationWidget<UImage>(UI,TEXT("NextCardArt"));auto* Meter=NamedPresentationWidget<URiftAetherMeter>(UI,TEXT("AetherSegments"));
    auto* Phase=NamedPresentationWidget<UTextBlock>(UI,TEXT("BattlePhase"));auto* Timer=NamedPresentationWidget<UTextBlock>(UI,TEXT("BattleTimer"));
    if(!TestNotNull(TEXT("Battle has actual next-card artwork"),Next)||!TestNotNull(TEXT("Battle has the segmented resource meter"),Meter)||!TestNotNull(TEXT("Battle has a distinct phase label"),Phase)||!TestNotNull(TEXT("Battle has a distinct timer"),Timer))return false;
    TestEqual(TEXT("New battle timer uses the authoritative three-minute clock"),Timer->GetText().ToString(),FString(TEXT("3:00")));
    TestEqual(TEXT("Resource meter reads the actual opening bank"),Meter->Bank,5.f);
    const FString OpeningNext=UTF8_TO_TCHAR(Match->ViewState()->queues[0].front().c_str());
    if(!TestNotNull(TEXT("Next-card widget binds a real texture asset"),Next->GetBrush().GetResourceObject()))return false;
    TestEqual(TEXT("Next-card image matches the authoritative queue"),Next->GetBrush().GetResourceObject()->GetName(),TEXT("T_Card_")+OpeningNext);
    TArray<URiftActionButton*> Hand;for(auto* Button:PresentationWidgets<URiftActionButton>(UI->GetRootWidget()))if(Button->OnPressed.IsBound()&&!Button->OnClicked.IsBound())Hand.Add(Button);
    if(!TestEqual(TEXT("Presentation retains exactly four deployment cards"),Hand.Num(),4))return false;
    const auto* Played=rift::FindCard(Match->ViewState()->hands[0][0]);Hand[0]->OnPressed.Broadcast();Refresh();
    auto* Selected=NamedPresentationWidget<UTextBlock>(UI,TEXT("SelectedCardStats"));if(!TestNotNull(TEXT("Hand selection has an in-battle stat readout"),Selected))return false;
    TestTrue(TEXT("Selected stats and hover details show the canonical cost and HP"),Selected->GetText().ToString().Contains(FString::Printf(TEXT("%d Aether"),Played->cost))&&Selected->GetText().ToString().Contains(FString::Printf(TEXT("%.0f HP"),Played->hp))&&Hand[0]->GetToolTipText().ToString().Contains(FString::Printf(TEXT("%.2fs"),Played->attackInterval)));
    UI->WorldClicked({0,8.5});Refresh();
    const FString CycledNext=UTF8_TO_TCHAR(Match->ViewState()->queues[0].front().c_str());
    TestEqual(TEXT("A paid deployment updates the preview to the new queue head"),Next->GetBrush().GetResourceObject()->GetName(),TEXT("T_Card_")+CycledNext);
    TestTrue(TEXT("Meter follows the bank after exact-cost deployment"),FMath::IsNearlyEqual(Meter->Bank,float(Match->ViewState()->aether[0])));
    const float Remaining=Match->GetAether(0);World->GetSubsystem<URiftDeveloperSubsystem>()->ChangeAether(0,-Remaining,false);Refresh();
    Hand[0]->OnPressed.Broadcast();const double BeforeInvalid=Match->ViewState()->spent[0];UI->WorldClicked({0,8.5});Refresh();
    TestEqual(TEXT("Unaffordable card remains inspectable without spending Aether"),UI->SelectedHand(),0);
    TestEqual(TEXT("Invalid deployment leaves actual spend unchanged"),Match->ViewState()->spent[0],BeforeInvalid);

    Match->SetSpeed(.5f);if(!Click(TEXT("MENU")))return false;Refresh();
    TestFalse(TEXT("Pause overlay blocks battle deployment input"),UI->CanAcceptBattleInput());
    if(!TestNotNull(TEXT("Pause is a real overlay in the active widget tree"),NamedPresentationWidget<UWidget>(UI,TEXT("BattlePauseMenu"))))return false;
    const double PausedTime=Match->ViewState()->elapsed;Match->Tick(.2f);
    TestEqual(TEXT("Pause menu stops the authoritative clock"),Match->ViewState()->elapsed,PausedTime);
    if(!Click(TEXT("FIELD MANUAL")))return false;
    Match->Tick(.2f);TestEqual(TEXT("Reading Help from the pause menu leaves battle paused"),Match->ViewState()->elapsed,PausedTime);
    if(!Click(TEXT("RETURN TO BATTLE")))return false;
    TestEqual(TEXT("Returning from Help restores the prior simulation speed"),Match->GetSpeed(),.5f);TestTrue(TEXT("Returning from Help restores paid deployment input"),UI->CanAcceptBattleInput());
    Match->Simulation()->ClearField();Match->Simulation()->Step(120.02);Match->FlushEvents();Refresh();
    auto* Announcement=NamedPresentationWidget<UTextBlock>(UI,TEXT("BattleAnnouncement"));if(!TestNotNull(TEXT("Battle has a real phase announcement"),Announcement))return false;
    TestTrue(TEXT("The exact 120-second boundary produces the double-Aether announcement"),Announcement->GetText().ToString().Contains(TEXT("2× AETHER")));
    Match->Simulation()->Step(60);Match->FlushEvents();Refresh();
    TestTrue(TEXT("The authoritative 0–0 overtime boundary updates the announcement"),Announcement->GetText().ToString().Contains(TEXT("OVERTIME")));
    TestTrue(TEXT("Overtime remains visible in its distinct phase field"),NamedPresentationWidget<UTextBlock>(UI,TEXT("BattlePhase"))->GetText().ToString().Contains(TEXT("OVERTIME")));
    Match->Simulation()->Step(60);Match->FlushEvents();Refresh();
    TestTrue(TEXT("The 240-second overtime boundary announces triple Aether without repeating overtime"),Announcement->GetText().ToString().Contains(TEXT("3× AETHER")));
    TestTrue(TEXT("Triple Aether retains the persistent overtime phase field"),NamedPresentationWidget<UTextBlock>(UI,TEXT("BattlePhase"))->GetText().ToString().Contains(TEXT("OVERTIME")));
    if(!Click(TEXT("HOME"))||!Click(TEXT("Loadout")))return false;
    TestFalse(TEXT("Detailed deck analysis is collapsed on first entry"),HasPresentationText(UI,TEXT("PAIR SYNERGY")));
    if(!Click(TEXT("SHOW DECK ANALYSIS")))return false;
    TestTrue(TEXT("Expanding deck analysis preserves exact pair synergy information"),HasPresentationText(UI,TEXT("PAIR SYNERGY")));
    const FString FirstName=UTF8_TO_TCHAR(rift::FindCard(TCHAR_TO_UTF8(*Profile->ActiveDeck()[0]))->name.c_str());
    if(!Click(*FirstName))return false;
    TestTrue(TEXT("Removing a selected card retains a visible empty slot"),HasPresentationText(UI,TEXT("EMPTY SLOT")));
    auto* Save=PresentationButton(UI,TEXT("SAVE & SELECT"));if(!TestNotNull(TEXT("Incomplete deck retains the save action"),Save))return false;
    TestFalse(TEXT("Incomplete deck visibly disables saving"),Save->GetIsEnabled());
    if(!Click(TEXT("RESET DRAFT")))return false;
    TestTrue(TEXT("Reset restores an actionable complete deck"),PresentationButton(UI,TEXT("SAVE & SELECT"))->GetIsEnabled());
    return true;
}
#endif
