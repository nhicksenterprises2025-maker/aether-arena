#include "RiftGameMode.h"
#include "RiftDiagnostics.h"
#include "RiftHandButton.h"
#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "RiftReplaySubsystem.h"
#include "Presentation/RiftArenaPresentation.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Input/Events.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/SViewport.h"
#include "Widgets/SToolTip.h"
#include "Widgets/SWindow.h"

void ARiftPlayerController::RunCardDragSmoke(const FString& ReportPath)
{
    auto Report=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Checks;bool Passed=true;
    auto Check=[&](const TCHAR* Name,bool Result)
    {
        auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("name"),Name);Item->SetBoolField(TEXT("passed"),Result);
        if(auto* Current=GetWorld()->GetSubsystem<URiftMatchSubsystem>())if(const auto* State=Current->ViewState())
        {
            Item->SetNumberField(TEXT("aether"),State->aether[0]);Item->SetNumberField(TEXT("spent"),State->spent[0]);
            Item->SetNumberField(TEXT("entityCount"),State->entities.size());
            TArray<TSharedPtr<FJsonValue>> Cards;for(const auto& Id:State->hands[0])Cards.Add(MakeShared<FJsonValueString>(UTF8_TO_TCHAR(Id.c_str())));
            Item->SetArrayField(TEXT("hand"),Cards);
        }
        Item->SetNumberField(TEXT("selectedHand"),Interface?Interface->SelectedHand():INDEX_NONE);
        Item->SetBoolField(TEXT("pendingDrag"),HasPendingCardDrag());Item->SetBoolField(TEXT("activeDrag"),IsDraggingCard());
        if(FSlateApplication::IsInitialized())if(const auto Focus=FSlateApplication::Get().GetKeyboardFocusedWidget())
            Item->SetStringField(TEXT("focusedWidgetType"),Focus->GetTypeAsString());
        Checks.Add(MakeShared<FJsonValueObject>(Item));Passed&=Result;
        RIFT_LOG(LogRift,Log,TEXT("Card drag route: %s %s"),Result?TEXT("PASS"):TEXT("FAIL"),Name);
    };
    auto Save=[&]()
    {
        Report->SetNumberField(TEXT("schemaVersion"),1);FString Version;
        GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"),TEXT("ProjectVersion"),Version,GGameIni);
        Report->SetStringField(TEXT("version"),Version);Report->SetBoolField(TEXT("passed"),Passed);
        Report->SetStringField(TEXT("route"),TEXT("production UMG hand cards through FSlateApplication ProcessMouseButtonDownEvent / ProcessMouseMoveEvent / ProcessMouseButtonUpEvent"));
        Report->SetArrayField(TEXT("checks"),Checks);Report->SetNumberField(TEXT("checkCount"),Checks.Num());
        FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath),true);
        const bool Saved=FFileHelper::SaveStringToFile(Text,*ReportPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        RIFT_LOG(LogRift,Log,TEXT("Native card drag smoke %s: %s"),Passed&&Saved?TEXT("passed"):TEXT("failed"),*ReportPath);
        FPlatformMisc::RequestExitWithStatus(false,Passed&&Saved?0:2);
    };
    Check(TEXT("live controller and Slate available"),Interface&&FSlateApplication::IsInitialized());
    auto* Match=GetWorld()->GetSubsystem<URiftMatchSubsystem>();auto* Profile=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();
    Check(TEXT("isolated training match accepts input"),Match&&Match->IsTraining()&&Match->Simulation()&&Interface&&Interface->CanAcceptBattleInput());
    if(!Passed){Save();return;}
    auto& Slate=FSlateApplication::Get();const bool OriginallyActive=Slate.IsActive();Slate.OnApplicationActivationChanged(true);Match->SetSpeed(0);auto* Sim=Match->Simulation();
    Sim->SetAIEnabled(rift::Team::Player,false);Sim->SetAIEnabled(rift::Team::Enemy,false);Sim->SetAether(rift::Team::Player,10);
    const bool OriginalDrag=Profile->Settings.DragDeploy,OriginalConfirm=Profile->Settings.ConfirmDeploy;
    Profile->Settings.DragDeploy=true;Profile->Settings.ConfirmDeploy=false;
    Slate.ReleaseAllPointerCapture();Slate.Tick(ESlateTickType::Widgets);
    if(GEngine->GameViewport&&GEngine->GameViewport->GetGameViewportWidget().IsValid())
        Slate.SetKeyboardFocus(GEngine->GameViewport->GetGameViewportWidget(),EFocusCause::SetDirectly);
    TArray<URiftHandButton*> Hand;
    Interface->WidgetTree->ForEachWidget([&](UWidget* Widget){if(auto* Button=Cast<URiftHandButton>(Widget))Hand.Add(Button);});
    Check(TEXT("four production Slate hand buttons"),Hand.Num()==4);
    bool HandLabelsCorrect=Hand.Num()==4;
    for(auto* HandButton:Hand)
    {
        TArray<FString> Labels;
        TFunction<void(UWidget*)> CollectText=[&](UWidget* Widget)
        {
            if(auto* Text=Cast<UTextBlock>(Widget))Labels.Add(Text->GetText().ToString());
            if(auto* Panel=Cast<UPanelWidget>(Widget))for(int32 Index=0;Index<Panel->GetChildrenCount();++Index)CollectText(Panel->GetChildAt(Index));
        };
        CollectText(HandButton);
        const auto* HandCard=HandButton->HandSlot>=0&&HandButton->HandSlot<4?rift::FindCard(Sim->State().hands[0][HandButton->HandSlot]):nullptr;
        HandLabelsCorrect&=HandCard&&Labels.Num()==2&&Labels.Contains(UTF8_TO_TCHAR(HandCard->name.c_str()))&&Labels.Contains(FText::AsNumber(HandCard->cost).ToString());
    }
    Check(TEXT("hand cards show their names and Aether costs without hotkey number badges"),HandLabelsCorrect);
    URiftHandButton* Button=nullptr;
    for(auto* Candidate:Hand)
        if(Candidate->HandSlot>=0&&Candidate->HandSlot<4)
            if(const auto* Card=rift::FindCard(Sim->State().hands[0][Candidate->HandSlot]);Card&&!Card->spell)
            {Button=Candidate;break;}
    Check(TEXT("troop or building card available for legal and illegal drops"),Button!=nullptr);
    if(!Passed){Profile->Settings.DragDeploy=OriginalDrag;Profile->Settings.ConfirmDeploy=OriginalConfirm;Save();return;}
    const int32 Slot=Button->HandSlot;const auto* Card=rift::FindCard(Sim->State().hands[0][Slot]);
    const FGeometry Viewport=UWidgetLayoutLibrary::GetViewportWidgetGeometry(this);
    int32 Width=0,Height=0;GetViewportSize(Width,Height);
    Report->SetNumberField(TEXT("width"),Width);Report->SetNumberField(TEXT("height"),Height);Report->SetNumberField(TEXT("handSlot"),Slot);
    Report->SetStringField(TEXT("cardId"),UTF8_TO_TCHAR(Card->id.c_str()));
    auto PixelToAbsolute=[&](FVector2D Pixel){return Viewport.LocalToAbsolute(FVector2D(Pixel.X/FMath::Max(1,Width)*Viewport.GetLocalSize().X,Pixel.Y/FMath::Max(1,Height)*Viewport.GetLocalSize().Y));};
    auto TileToAbsolute=[&](FVector2D Tile)
    {FVector2D Pixel;ProjectWorldLocationToScreen(URiftMatchSubsystem::WorldPoint({Tile.X,Tile.Y}),Pixel,false);return PixelToAbsolute(Pixel);};
    const FGeometry CardGeometry=Button->GetCachedGeometry();const FVector2D Origin=CardGeometry.LocalToAbsolute(CardGeometry.GetLocalSize()*.5);
    FVector2D LegalTile=FVector2D::ZeroVector;bool FoundLegal=false;
    for(double Z:{8.5,12.5,5.5,16.5})for(double X:{-6.5,6.5,-10.5,10.5,.5})
        if(!FoundLegal&&Match->CanPlace(Slot,FVector2D(X,Z))){LegalTile=FVector2D(X,Z);FoundLegal=true;}
    FVector2D AlternateTile=FVector2D::ZeroVector;bool FoundAlternate=false;
    for(int32 Z=3;Z<rift::arena::HalfHeight;++Z)for(int32 X=-rift::arena::HalfWidth;X<rift::arena::HalfWidth;++X)
    {
        const FVector2D Candidate(X+.5,Z+.5);
        if(!FoundAlternate&&!Candidate.Equals(LegalTile,.001)&&Match->CanPlace(Slot,Candidate)){AlternateTile=Candidate;FoundAlternate=true;}
    }
    const FVector2D Legal=TileToAbsolute(LegalTile),Alternate=TileToAbsolute(AlternateTile),Enemy=TileToAbsolute(FVector2D(-6.5,-10.5));
    FVector2D Resolved;
    Check(TEXT("legal arena target projects to its actual board tile"),FoundLegal&&ScreenPointToTile(Legal,Resolved)&&Resolved.Equals(LegalTile,.001));
    Check(TEXT("hand geometry is arranged and lies outside deployment area"),CardGeometry.GetLocalSize().X>40&&CardGeometry.GetLocalSize().Y>40&&!ScreenPointToTile(Origin,Resolved));
    auto Projection=MakeShared<FJsonObject>();Report->SetObjectField(TEXT("placementAccuracy"),Projection);
    Projection->SetNumberField(TEXT("viewportSlateWidth"),Viewport.GetLocalSize().X);Projection->SetNumberField(TEXT("viewportSlateHeight"),Viewport.GetLocalSize().Y);
    Projection->SetNumberField(TEXT("viewportDpiScale"),UWidgetLayoutLibrary::GetViewportScale(this));
    int32 CentersChecked=0,EdgesChecked=0;bool CentersCorrect=true,EdgesCorrect=true;
    TArray<TSharedPtr<FJsonValue>> ProjectionFailures;
    auto CheckProjection=[&](FVector2D Point,FVector2D Expected)
    {
        FVector2D Actual=FVector2D::ZeroVector;const bool Correct=ScreenPointToTile(TileToAbsolute(Point),Actual)&&Actual.Equals(Expected,.001);
        if(!Correct&&ProjectionFailures.Num()<16)
        {auto Failure=MakeShared<FJsonObject>();Failure->SetStringField(TEXT("point"),Point.ToString());Failure->SetStringField(TEXT("expected"),Expected.ToString());Failure->SetStringField(TEXT("actual"),Actual.ToString());ProjectionFailures.Add(MakeShared<FJsonValueObject>(Failure));}
        return Correct;
    };
    for(int32 Z=-rift::arena::HalfHeight;Z<rift::arena::HalfHeight;++Z)for(int32 X=-rift::arena::HalfWidth;X<rift::arena::HalfWidth;++X)
    {
        const FVector2D Center(X+.5,Z+.5);++CentersChecked;CentersCorrect&=CheckProjection(Center,Center);
        // Close to each real tile edge, with a small margin greater than ray
        // roundoff. This covers the outer board edges as well as lane changes.
        for(const FVector2D Offset:{FVector2D(-.49,0),FVector2D(.49,0),FVector2D(0,-.49),FVector2D(0,.49)})
        {++EdgesChecked;EdgesCorrect&=CheckProjection(Center+Offset,Center);}
    }
    Projection->SetNumberField(TEXT("boardCellCentersChecked"),CentersChecked);Projection->SetNumberField(TEXT("tileEdgeSamplesChecked"),EdgesChecked);
    Projection->SetArrayField(TEXT("failures"),ProjectionFailures);
    Check(TEXT("every board cell center resolves exactly through actual viewport and DPI geometry"),CentersCorrect&&CentersChecked==rift::arena::Width*rift::arena::Height);
    Check(TEXT("samples beside every tile and outer board edge keep the correct deployment cell"),EdgesCorrect&&EdgesChecked==4*rift::arena::Width*rift::arena::Height);
    if(!Passed){Profile->Settings.DragDeploy=OriginalDrag;Profile->Settings.ConfirmDeploy=OriginalConfirm;Save();return;}
    FVector2D Previous=Origin;TSet<FKey> Buttons;
    auto Event=[&](FVector2D Point,FKey Key)
    {return FPointerEvent(0,Point,Previous,Buttons,Key,0,FModifierKeysState());};
    auto Move=[&](FVector2D Point)
    {const auto Pointer=Event(Point,EKeys::Invalid);Slate.ProcessMouseMoveEvent(Pointer,false);Previous=Point;ProcessPlayerInput(0,false);Slate.Tick(ESlateTickType::Widgets);};
    auto Down=[&](FVector2D Point,FKey Key=EKeys::LeftMouseButton)
    {Move(Point);Buttons.Add(Key);const auto Pointer=Event(Point,Key);Slate.ProcessMouseButtonDownEvent(nullptr,Pointer);Previous=Point;ProcessPlayerInput(0,false);Slate.Tick(ESlateTickType::Widgets);};
    auto Up=[&](FVector2D Point,FKey Key=EKeys::LeftMouseButton)
    {Buttons.Remove(Key);const auto Pointer=Event(Point,Key);Slate.ProcessMouseButtonUpEvent(Pointer);Previous=Point;ProcessPlayerInput(0,false);Slate.Tick(ESlateTickType::Widgets);};
    auto Digest=[&]()
    {FString Text;FJsonSerializer::Serialize(URiftReplaySubsystem::SnapshotJSON(Sim->State()),TJsonWriterFactory<>::Create(&Text));return Text;};
    TArray<TSharedPtr<FJsonValue>> KeyRoutes;
    auto Key=[&](FKey Value)
    {
        auto Route=MakeShared<FJsonObject>();Route->SetStringField(TEXT("key"),Value.ToString());
        if(const auto Focus=Slate.GetKeyboardFocusedWidget())Route->SetStringField(TEXT("focusedWidgetType"),Focus->GetTypeAsString());
        Route->SetBoolField(TEXT("keyDownHandled"),Slate.ProcessKeyDownEvent(FKeyEvent(Value,FModifierKeysState(),0,false,0,0)));ProcessPlayerInput(0,false);
        Route->SetNumberField(TEXT("selectedHandAfterKeyDown"),Interface->SelectedHand());Route->SetBoolField(TEXT("pendingDragAfterKeyDown"),HasPendingCardDrag());
        Route->SetNumberField(TEXT("aetherAfterKeyDown"),Sim->State().aether[0]);Route->SetNumberField(TEXT("spentAfterKeyDown"),Sim->State().spent[0]);
        KeyRoutes.Add(MakeShared<FJsonValueObject>(Route));
        Slate.ProcessKeyUpEvent(FKeyEvent(Value,FModifierKeysState(),0,false,0,0));ProcessPlayerInput(0,false);Slate.Tick(ESlateTickType::Widgets);
    };
    const FString Initial=Digest();
    // Use UE's supported application-local faux cursor while sampling hover.
    // The API swaps an ICursor pointer without moving/hiding the OS cursor.
    // Restore the platform cursor before the existing real drag transactions.
    auto TooltipLifecycle=MakeShared<FJsonObject>();Report->SetObjectField(TEXT("tooltipLifecycle"),TooltipLifecycle);
    TooltipLifecycle->SetStringField(TEXT("route"),TEXT("application-local FFauxSlateCursor / ProcessMouseMoveEvent / FSlateApplication UpdateToolTip / production SObjectWidget Tick"));
    TooltipLifecycle->SetBoolField(TEXT("usesHardwareCursor"),false);
    TooltipLifecycle->SetStringField(TEXT("hudSlateWidgetType"),Interface->TakeWidget()->GetTypeAsString());
    auto HoverMove=[&](FVector2D Point)
    {
        // Only called while this application's cursor is FFauxSlateCursor.
        Slate.SetCursorPos(Point);Move(Point);
    };
    auto RefreshHover=[&]()
    {
        // Route through the actual UMG Slate wrapper instead of invoking a
        // controller helper or reproducing UpdateBattleHUD in this fixture.
        Interface->TakeWidget()->Tick(Interface->GetCachedGeometry(),FPlatformTime::Seconds(),.11f);
        Slate.Tick(ESlateTickType::Widgets);Slate.UpdateToolTip(true);
    };
    auto HasNoBattleTooltip=[&]()
    {
        const auto Tip=Button->GetCachedWidget()->GetToolTip();
        return Button->GetToolTipText().IsEmpty() && (!Tip || Tip->IsEmpty());
    };
    Slate.UsePlatformCursorForCursorUser(false);Slate.CloseToolTip();HoverMove(Origin);RefreshHover();
    const bool InitialTooltipAbsent=HasNoBattleTooltip();
    TooltipLifecycle->SetStringField(TEXT("initialCardId"),UTF8_TO_TCHAR(Card->id.c_str()));
    TooltipLifecycle->SetBoolField(TEXT("initialAbsent"),InitialTooltipAbsent);
    Check(TEXT("hovering a battle hand card does not open a stats window"),InitialTooltipAbsent);
    bool HoverAbsentThroughout=true;
    constexpr int32 HoverRefreshes=12;
    for(int32 I=0;I<HoverRefreshes;++I)
    {
        FPlatformProcess::SleepNoStats(.035f);RefreshHover();
        HoverAbsentThroughout&=HasNoBattleTooltip();
    }
    TooltipLifecycle->SetNumberField(TEXT("unchangedRefreshCount"),HoverRefreshes);
    TooltipLifecycle->SetBoolField(TEXT("absentThroughRefreshes"),HoverAbsentThroughout);
    Check(TEXT("battle HUD refreshes never recreate a hover stats window or mutate combat"),
        InitialTooltipAbsent&&HoverAbsentThroughout&&Digest()==Initial);
    Slate.CloseToolTip();HoverMove(PixelToAbsolute(FVector2D(-100,-100)));Slate.UsePlatformCursorForCursorUser(true);
    Down(Origin);Check(TEXT("hand press selects and owns pointer capture"),HasPendingCardDrag()&&Interface->SelectedHand()==Slot&&Button->GetCachedWidget()->HasMouseCapture());
    Move(Origin+FVector2D(3,0));Check(TEXT("small pointer motion remains a selection click"),!IsDraggingCard()&&!Interface->IsCardDragGhostVisible());
    Up(Origin+FVector2D(3,0));Check(TEXT("simple card click selects without spending or cycling"),!HasPendingCardDrag()&&Interface->SelectedHand()==Slot&&Digest()==Initial);
    // Also reproduce a rapid input batch: an older viewport release is queued
    // immediately before the new card press, then normal input is processed.
    // Neither event calls a controller action directly.
    Slate.ProcessMouseButtonUpEvent(Event(Legal,EKeys::LeftMouseButton));Previous=Legal;
    Slate.ProcessMouseMoveEvent(Event(Origin,EKeys::Invalid),false);Previous=Origin;
    Buttons.Add(EKeys::LeftMouseButton);Slate.ProcessMouseButtonDownEvent(nullptr,Event(Origin,EKeys::LeftMouseButton));Previous=Origin;
    ProcessPlayerInput(0,false);Slate.Tick(ESlateTickType::Widgets);
    const bool BatchedReleasePreserved=HasPendingCardDrag()&&Interface->SelectedHand()==Slot&&Digest()==Initial;
    Report->SetBoolField(TEXT("batchedViewportReleasePreserved"),BatchedReleasePreserved);
    Move(Legal);Check(TEXT("drag threshold and ghost survive an earlier queued viewport release"),BatchedReleasePreserved&&IsDraggingCard()&&Interface->IsCardDragGhostVisible());
    const bool FirstPreviewCorrect=Arena&&Arena->HasPlacementPreview()&&Arena->PreviewIsValid()&&Arena->PreviewTile().Equals(LegalTile,.001);
    Move(Alternate);
    const bool AlternatePreviewCorrect=FoundAlternate&&Arena&&Arena->HasPlacementPreview()&&Arena->PreviewIsValid()&&Arena->PreviewTile().Equals(AlternateTile,.001);
    Projection->SetBoolField(TEXT("previewUpdatesBetweenPlayerTicks"),FirstPreviewCorrect&&AlternatePreviewCorrect);
    Check(TEXT("captured pointer moves refresh the exact placement preview before another player tick"),FirstPreviewCorrect&&AlternatePreviewCorrect);
    Move(Origin);Check(TEXT("returning to original hand card remains a drag"),IsDraggingCard());
    Up(Origin);Check(TEXT("return to hand cancels and preserves full match state"),!HasPendingCardDrag()&&Interface->SelectedHand()==INDEX_NONE&&!Interface->IsCardDragGhostVisible()&&Digest()==Initial);
    Down(Origin);Move(Legal);Move(PixelToAbsolute(FVector2D(Width-60,30)));Up(Previous);
    Check(TEXT("release over HUD cancels without spending or cycling"),Interface->SelectedHand()==INDEX_NONE&&!HasPendingCardDrag()&&Digest()==Initial);
    Down(Origin);Move(Enemy);Up(Enemy);
    Check(TEXT("release over illegal enemy tile cancels without spending or cycling"),Interface->SelectedHand()==INDEX_NONE&&!HasPendingCardDrag()&&Digest()==Initial);
    Down(Origin);Move(PixelToAbsolute(FVector2D(-80,Height*.5)));Up(Previous);
    Check(TEXT("release outside viewport cancels without edge deployment"),Interface->SelectedHand()==INDEX_NONE&&!HasPendingCardDrag()&&Digest()==Initial);
    Down(Origin);Move(Legal);Down(Legal,EKeys::RightMouseButton);Up(Legal,EKeys::RightMouseButton);Up(Legal);
    Check(TEXT("right click cancels captured drag and later release cannot rearm"),Interface->SelectedHand()==INDEX_NONE&&!HasPendingCardDrag()&&Digest()==Initial);
    Down(Origin);Move(Legal);Key(EKeys::Escape);Up(Legal);
    Check(TEXT("Escape cancels first without opening battle menu"),Interface->SelectedHand()==INDEX_NONE&&!HasPendingCardDrag()&&Interface->CanAcceptBattleInput()&&Digest()==Initial);
    Down(Origin);Move(Legal);Slate.ReleaseAllPointerCapture();Up(Legal);
    Check(TEXT("lost pointer capture cancels without spending or cycling"),Interface->SelectedHand()==INDEX_NONE&&!HasPendingCardDrag()&&Digest()==Initial);
    Down(Origin);Move(Legal);Slate.OnApplicationActivationChanged(false);PlayerTick(0);Up(Legal);
    Check(TEXT("application focus loss cancels without spending or cycling"),Interface->SelectedHand()==INDEX_NONE&&!HasPendingCardDrag()&&Digest()==Initial);
    Slate.OnApplicationActivationChanged(true);
    Down(Origin);Move(Legal);Interface->ToggleBattleMenu();Up(Legal);
    Check(TEXT("opening pause modal cancels before its later release"),Interface->SelectedHand()==INDEX_NONE&&!HasPendingCardDrag()&&!Interface->CanAcceptBattleInput()&&Digest()==Initial);
    Interface->ToggleBattleMenu();
    // The fixture resumes and presses again within one synchronous engine
    // frame. Let the production UMG wrapper refresh the reopened battle HUD,
    // as it does between real player gestures, before sampling the next press.
    RefreshHover();
    Sim->SetAether(rift::Team::Player,0);const FString EmptyBank=Digest();Down(Origin);
    const bool UnaffordablePressCaptured=HasPendingCardDrag()&&Interface->SelectedHand()==Slot&&Button->GetCachedWidget()->HasMouseCapture();
    Report->SetBoolField(TEXT("unaffordablePressCaptured"),UnaffordablePressCaptured);Move(Legal);Up(Legal);
    Check(TEXT("unaffordable drag returns to hand without mutation"),UnaffordablePressCaptured&&Interface->SelectedHand()==INDEX_NONE&&!HasPendingCardDrag()&&Digest()==EmptyBank);
    Sim->SetAether(rift::Team::Player,10);RefreshHover();
    auto ClickPreference=MakeShared<FJsonObject>();Report->SetObjectField(TEXT("clickPreference"),ClickPreference);
    auto ClickStage=[&](const TCHAR* Name)
    {
        auto Stage=MakeShared<FJsonObject>();Stage->SetNumberField(TEXT("selectedHand"),Interface->SelectedHand());
        Stage->SetBoolField(TEXT("pendingDrag"),HasPendingCardDrag());Stage->SetBoolField(TEXT("activeDrag"),IsDraggingCard());
        Stage->SetBoolField(TEXT("pointerCapture"),Button->GetCachedWidget()->HasMouseCapture());
        Stage->SetBoolField(TEXT("buttonEnabled"),Button->GetIsEnabled());Stage->SetBoolField(TEXT("slateButtonEnabled"),Button->GetCachedWidget()->IsEnabled());
        Stage->SetBoolField(TEXT("battleInputAllowed"),Interface->CanAcceptBattleInput());
        Stage->SetBoolField(TEXT("dragDeploy"),Profile->Settings.DragDeploy);Stage->SetNumberField(TEXT("transactionHand"),CardDragHand);
        Stage->SetBoolField(TEXT("selectionIsCurrent"),CardDragSelectionIsCurrent());Stage->SetBoolField(TEXT("combatUnchanged"),Digest()==Initial);
        ClickPreference->SetObjectField(Name,Stage);
    };
    Profile->Settings.DragDeploy=false;ClickStage(TEXT("beforePress"));Down(Origin);ClickStage(TEXT("afterPress"));
    const bool ClickPreferencePressCaptured=HasPendingCardDrag()&&Interface->SelectedHand()==Slot&&Button->GetCachedWidget()->HasMouseCapture()&&!IsDraggingCard();
    ClickPreference->SetBoolField(TEXT("pressCaptured"),ClickPreferencePressCaptured);Move(Legal);ClickStage(TEXT("afterMove"));Up(Legal);ClickStage(TEXT("afterRelease"));
    Check(TEXT("click deployment preference does not accidentally drag deploy"),ClickPreferencePressCaptured&&Interface->SelectedHand()==Slot&&!HasPendingCardDrag()&&!IsDraggingCard()&&Digest()==Initial);
    Profile->Settings.DragDeploy=true;CancelCardDrag();
    Down(Origin);Move(Legal);Key(EKeys::Two);Up(Legal);
    Check(TEXT("changing keyboard selection cancels the old pointer transaction"),!HasPendingCardDrag()&&Digest()==Initial);
    CancelCardDrag();
    const auto Before=Sim->State();const auto OldId=Before.hands[0][Slot];const auto Next=Before.queues[0].front();const double Cost=Card->cost;
    int32 CardPlayEvents=0;FVector2D PaidTile=FVector2D::ZeroVector;
    const FDelegateHandle DropObserver=Match->OnEvent.AddLambda([&](const rift::Event& Event)
    {if(Event.type=="card_play"&&Event.team==rift::Team::Player&&Event.cardId==OldId){++CardPlayEvents;PaidTile=FVector2D(Event.position.x,Event.position.z);}});
    Down(Origin);Move(Legal);Up(Alternate);
    Match->OnEvent.Remove(DropObserver);
    Projection->SetStringField(TEXT("lastMoveTile"),LegalTile.ToString());Projection->SetStringField(TEXT("releaseTile"),AlternateTile.ToString());Projection->SetStringField(TEXT("paidCardPlayTile"),PaidTile.ToString());
    Check(TEXT("release uses its final pointer sample rather than the earlier move preview"),CardPlayEvents==1&&PaidTile.Equals(AlternateTile,.001));
    const auto After=Sim->State();
    Check(TEXT("legal drop spends exactly one card cost"),FMath::IsNearlyEqual(After.aether[0],Before.aether[0]-Cost,1e-9)&&FMath::IsNearlyEqual(After.spent[0],Before.spent[0]+Cost,1e-9));
    Check(TEXT("legal drop cycles the selected slot exactly once"),After.hands[0][Slot]==Next&&After.queues[0].size()==Before.queues[0].size()&&After.queues[0].back()==OldId);
    Check(TEXT("legal drop creates the selected troop or building"),After.entities.size()==Before.entities.size()+Card->count);
    Check(TEXT("legal drop clears capture selection ghost and preview transaction"),!HasPendingCardDrag()&&Interface->SelectedHand()==INDEX_NONE&&!Interface->IsCardDragGhostVisible());
    const FString Deployed=Digest();Up(Legal);
    Check(TEXT("duplicate mouse release cannot spend or cycle again"),Digest()==Deployed);
    Slate.UsePlatformCursorForCursorUser(false);HoverMove(Origin);bool CycledTooltipAbsent=true;
    for(int32 I=0;I<6;++I)
    {
        FPlatformProcess::SleepNoStats(.035f);RefreshHover();
        CycledTooltipAbsent&=HasNoBattleTooltip();
    }
    TooltipLifecycle->SetStringField(TEXT("cycledCardId"),UTF8_TO_TCHAR(After.hands[0][Slot].c_str()));
    TooltipLifecycle->SetNumberField(TEXT("cycledRefreshCount"),6);
    TooltipLifecycle->SetBoolField(TEXT("cycledAbsent"),CycledTooltipAbsent);
    Check(TEXT("cycling a battle hand card never rearms a hover stats window"),CycledTooltipAbsent&&Digest()==Deployed);
    Slate.CloseToolTip();HoverMove(PixelToAbsolute(FVector2D(-100,-100)));Slate.UsePlatformCursorForCursorUser(true);
    // Phase termination is checked last, so it cannot conceal failed earlier
    // deployment assertions. This is fixture state, never a gameplay override.
    auto* FinishedButton=Hand[0];const FGeometry EndGeometry=FinishedButton->GetCachedGeometry();
    const FVector2D EndOrigin=EndGeometry.LocalToAbsolute(EndGeometry.GetLocalSize()*.5);
    Down(EndOrigin);Move(Legal);auto& State=const_cast<rift::Snapshot&>(Sim->State());State.phase=rift::Phase::Finished;
    const FString Finished=Digest();UpdateCardDragAtScreen(Legal);Up(Legal);
    Check(TEXT("finished phase cancels a pending drag without deployment"),!HasPendingCardDrag()&&Interface->SelectedHand()==INDEX_NONE&&Digest()==Finished);
    Profile->Settings.DragDeploy=OriginalDrag;Profile->Settings.ConfirmDeploy=OriginalConfirm;
    Slate.OnApplicationActivationChanged(OriginallyActive);
    Report->SetArrayField(TEXT("keyRoutes"),KeyRoutes);
    Save();
}
