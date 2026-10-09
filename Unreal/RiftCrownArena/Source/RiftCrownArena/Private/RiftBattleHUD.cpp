#include "RiftUIWidget.h"
#include "RiftTypography.h"
#include "RiftUIPrimitives.h"
#include "RiftGameMode.h"
#include "RiftHandButton.h"
#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "RiftReplaySubsystem.h"
#include "Presentation/RiftBattleLayout.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Styling/CoreStyle.h"

namespace {
const FLinearColor Gold(.95f,.64f,.19f,1),Blue(.22f,.69f,.97f,1),Red(.98f,.32f,.29f,1),Ink(.018f,.035f,.065f,.99f),White(.95f,.94f,.89f,1),Soft(.55f,.66f,.79f,1),Violet(.55f,.24f,.94f,1);
FString GameString(const std::string& S){return UTF8_TO_TCHAR(S.c_str());}
FString ResultLabel(const std::string& Reason){
    if(Reason=="core_destroyed")return TEXT("Core destroyed");
    if(Reason=="overtime_tower")return TEXT("Decisive tower destroyed in overtime");
    if(Reason=="regulation")return TEXT("Regulation complete");
    if(Reason=="tiebreaker")return TEXT("Tower HP tiebreaker complete");
    FString Label=GameString(Reason).Replace(TEXT("_"),TEXT(" "));return Label.IsEmpty()?Label:Label.Left(1).ToUpper()+Label.Mid(1);
}
FString CardSummary(const rift::Card& C){
    FString Result=C.spell?FString::Printf(TEXT("%d Aether · Spell\n%.0f damage · %.1f tile radius\n%.0f structure damage"),C.cost,C.damage,C.spellRadius,C.towerDamage):FString::Printf(TEXT("%d Aether · %s%s\n%.0f HP · %.0f damage / %.2fs\n%.1f range · %s"),C.cost,C.flying?TEXT("Air"):C.building?TEXT("Building"):TEXT("Ground"),C.count>1?*FString::Printf(TEXT(" ×%d"),C.count):TEXT(""),C.hp,C.damage,C.attackInterval,C.range,C.structuresOnly?TEXT("Structures"):C.canHitAir?TEXT("Ground + Air"):TEXT("Ground targets"));
    if(C.castDelay>0)Result+=FString::Printf(TEXT("\n%.2fs to impact · lead moving targets"),C.castDelay);
    if(C.dotDamage)Result+=FString::Printf(TEXT("\n%.0f damage/s for %.1fs"),C.dotDamage,C.dotDuration);
    if(C.slowPct)Result+=FString::Printf(TEXT("\n%.0f%% slow for %.1fs"),C.slowPct*100,C.slowDuration);
    if(C.auraDamage)Result+=FString::Printf(TEXT("\n%.0f pulse / %.1fs · %.1fs stun"),C.auraDamage,C.auraInterval,C.stunDuration);
    if(C.chargeDamage)Result+=FString::Printf(TEXT("\n%.0f charge damage"),C.chargeDamage);
    if(C.building)Result+=FString::Printf(TEXT("\n%.0fs lifetime · %.1f tile footprint"),C.lifetime,C.footprint);
    return Result;
}
}

bool URiftUIWidget::CanAcceptBattleInput()const
{
    if(!IsLiveBattleView()||bMenuPausedMatch)return false;
    const auto* Match=GetWorld()->GetSubsystem<URiftMatchSubsystem>();
    const auto* State=Match?Match->ViewState():nullptr;
    return State&&(State->phase==rift::Phase::Regulation||State->phase==rift::Phase::Overtime);
}
bool URiftUIWidget::IsCardDragGhostVisible()const
{return DragFrame&&DragFrame->GetVisibility()==ESlateVisibility::HitTestInvisible;}

void URiftUIWidget::Battle()
{
    auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();const auto* S=M->ViewState();if(!S){Navigate(TEXT("Home"));return;}
    auto* Header=WidgetTree->ConstructWidget<UHorizontalBox>();
    auto CrownBlock=[&](FLinearColor Color,const TCHAR* Label,const FName Name,UTextBlock*& Out){auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();auto* Icon=WidgetTree->ConstructWidget<URiftCrownIcon>();Icon->Tint=Color;auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(30);Size->SetHeightOverride(28);Size->SetContent(Icon);Add(Row,Size,false,4);auto* Stack=WidgetTree->ConstructWidget<UVerticalBox>();Add(Stack,Text(Label,9,Soft),0);Out=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),MakeUniqueObjectName(WidgetTree,UTextBlock::StaticClass(),Name));Out->SetFont(RiftTypography::Font("Bold",22));Out->SetColorAndOpacity(Color);Add(Stack,Out,0);Add(Row,Stack,false,5);return Panel(Row,FMargin(12,3));};
    UTextBlock* Friendly=nullptr;Add(Header,CrownBlock(Blue,TEXT("YOU"),TEXT("FriendlyCrowns"),Friendly),true,0);ScoreText=Friendly;
    auto* Clock=WidgetTree->ConstructWidget<UVerticalBox>();PhaseText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),MakeUniqueObjectName(WidgetTree,UTextBlock::StaticClass(),TEXT("BattlePhase")));PhaseText->SetFont(RiftTypography::Font("Bold",9));PhaseText->SetColorAndOpacity(Gold);PhaseText->SetJustification(ETextJustify::Center);Add(Clock,PhaseText,0);
    TimerText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),MakeUniqueObjectName(WidgetTree,UTextBlock::StaticClass(),TEXT("BattleTimer")));TimerText->SetFont(RiftTypography::Font("Bold",24));TimerText->SetColorAndOpacity(White);TimerText->SetJustification(ETextJustify::Center);Add(Clock,TimerText,0);auto* ClockSize=WidgetTree->ConstructWidget<USizeBox>();ClockSize->SetWidthOverride(164);ClockSize->SetContent(Panel(Clock,FMargin(8,2)));Add(Header,ClockSize,false,0);CastChecked<UHorizontalBoxSlot>(ClockSize->Slot)->SetPadding(FMargin(5,0));
    UTextBlock* Enemy=nullptr;Add(Header,CrownBlock(Red,TEXT("OPPONENT"),TEXT("EnemyCrowns"),Enemy),true,0);EnemyScore=Enemy;
    auto* HeaderSlot=Root->AddChildToCanvas(Header);HeaderSlot->SetAnchors(FAnchors(.5,0));HeaderSlot->SetOffsets(FMargin(-240,16,480,RiftBattleLayout::HeaderBottom-16));
    auto* Utility=WidgetTree->ConstructWidget<UHorizontalBox>();Add(Utility,Button(TEXT("HOME"),[this](){Navigate(TEXT("Home"));}),false,3);Add(Utility,Button(TEXT("MENU"),[this](){ToggleBattleMenu();}),false,3);Add(Utility,Button(TEXT("DEV"),[this](){ToggleDeveloper();}),false,3);auto* UtilitySlot=Root->AddChildToCanvas(Utility);UtilitySlot->SetAnchors(FAnchors(1,0));UtilitySlot->SetOffsets(FMargin(-276,16,258,46));
    auto* Identity=WidgetTree->ConstructWidget<UVerticalBox>();Add(Identity,Text(TEXT("RIFT CROWN ARENA"),14,Gold),0);Add(Identity,Text(M->IsTraining()?TEXT("TRAINING BATTLE"):TEXT("CROWN BATTLE"),11,Ink),3);auto* IdentitySlot=Root->AddChildToCanvas(Identity);IdentitySlot->SetOffsets(FMargin(24,20,230,44));

    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();auto* Bottom=Panel(Column,FMargin(8,6),Ink);auto* BottomSlot=Root->AddChildToCanvas(Bottom);BottomSlot->SetAnchors(FAnchors(.5,1));BottomSlot->SetOffsets(FMargin(-RiftBattleLayout::HandWidth*.5f,-RiftBattleLayout::HandTop,RiftBattleLayout::HandWidth,RiftBattleLayout::HandHeight));
    auto* Hand=WidgetTree->ConstructWidget<UHorizontalBox>();Add(Column,Hand,0);
    for(int32 I=0;I<4;++I){
        auto* B=CastChecked<URiftHandButton>(Button(TEXT(""),[this,I](){if(!CanAcceptBattleInput())return;SelectHand(I);Say(TEXT("Drag to deploy. Return to your hand to cancel."));},false,true));B->HandSlot=I;B->OnClicked.Clear();B->OnPressed.AddDynamic(B,&URiftActionButton::Invoke);
        B->PointerPressed=[this](FVector2D Point){if(!CanAcceptBattleInput())return;if(auto* PC=Cast<ARiftPlayerController>(GetOwningPlayer()))PC->BeginCardDragAtScreen(Point);};
        B->PointerMoved=[this](FVector2D Point){if(auto* PC=Cast<ARiftPlayerController>(GetOwningPlayer()))PC->UpdateCardDragAtScreen(Point);};
        B->PointerReleased=[this](FVector2D Point){if(auto* PC=Cast<ARiftPlayerController>(GetOwningPlayer()))PC->FinishCardDragAtScreen(Point);};
        B->PointerCancelled=[this](){if(auto* PC=Cast<ARiftPlayerController>(GetOwningPlayer()))PC->CancelCardDrag();};auto Style=B->GetStyle();Style.SetNormalPadding(FMargin(2));Style.SetPressedPadding(FMargin(2,4,2,0));B->SetStyle(Style);
        FString Id=GameString(S->hands[0][I]);UImage* Art=nullptr;auto* Stack=WidgetTree->ConstructWidget<UVerticalBox>();auto* Overlay=WidgetTree->ConstructWidget<UOverlay>();auto* ArtSlot=Overlay->AddChildToOverlay(Illustration(Id,64,&Art));ArtSlot->SetHorizontalAlignment(HAlign_Center);
        auto* Cost=Text(TEXT(""),18,White);Cost->SetJustification(ETextJustify::Center);auto* Orb=Panel(Cost,FMargin(7,3),Violet);auto* OrbSlot=Overlay->AddChildToOverlay(Orb);OrbSlot->SetHorizontalAlignment(HAlign_Left);OrbSlot->SetVerticalAlignment(VAlign_Top);OrbSlot->SetPadding(FMargin(-3,-4,0,0));
        auto* Key=Badge(FString::FromInt(I+1),Ink,10);auto* KeySlot=Overlay->AddChildToOverlay(Key);KeySlot->SetHorizontalAlignment(HAlign_Right);KeySlot->SetVerticalAlignment(VAlign_Bottom);KeySlot->SetPadding(FMargin(0,0,-2,-2));Add(Stack,Overlay,0);
        auto* Name=Text(TEXT(""),13,White);Name->SetAutoWrapText(false);Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);Name->SetJustification(ETextJustify::Center);Add(Stack,Name,1);B->SetContent(Stack);auto* Width=WidgetTree->ConstructWidget<USizeBox>();Width->SetWidthOverride(116);Width->SetContent(B);Add(Hand,Width,false,2);HandButtons.Add(B);HandText.Add(Name);HandCost.Add(Cost);HandImages.Add(Art);HandArtIds.Add(Id);
    }
    auto* Next=WidgetTree->ConstructWidget<UHorizontalBox>();NextImage=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),MakeUniqueObjectName(WidgetTree,UImage::StaticClass(),TEXT("NextCardArt")));auto* Fit=WidgetTree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);Fit->SetContent(NextImage);auto* NextSize=WidgetTree->ConstructWidget<USizeBox>();NextSize->SetWidthOverride(28);NextSize->SetHeightOverride(35);NextSize->SetContent(Fit);Add(Next,NextSize,false,2);auto* NextLabels=WidgetTree->ConstructWidget<UVerticalBox>();Add(NextLabels,Text(TEXT("NEXT"),9,Soft),0);NextText=Text(TEXT(""),11,Soft);NextText->SetAutoWrapText(false);NextText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);Add(NextLabels,NextText,0);Add(Next,NextLabels,true,4);auto* NextWidth=WidgetTree->ConstructWidget<USizeBox>();NextWidth->SetWidthOverride(106);NextWidth->SetContent(Next);Add(Hand,NextWidth,false,6);CastChecked<UHorizontalBoxSlot>(NextWidth->Slot)->SetVerticalAlignment(VAlign_Center);
    auto* Bank=WidgetTree->ConstructWidget<UHorizontalBox>();Add(Column,Bank,1);AetherText=Text(TEXT(""),15,White);AetherText->SetAutoWrapText(false);Add(Bank,AetherText,false,1);AetherMeter=WidgetTree->ConstructWidget<URiftAetherMeter>(URiftAetherMeter::StaticClass(),MakeUniqueObjectName(WidgetTree,URiftAetherMeter::StaticClass(),TEXT("AetherSegments")));auto* MeterSize=WidgetTree->ConstructWidget<USizeBox>();MeterSize->SetHeightOverride(10);MeterSize->SetContent(AetherMeter);Add(Bank,MeterSize,true,3);SurgeText=Text(TEXT("1×"),10,Violet);SurgeText->SetAutoWrapText(false);Add(Bank,SurgeText,false,1);
    NoticeText=Text(Notice.IsEmpty()?TEXT("1–4: select a card\nDrag or click to deploy\nESC: battle menu"):Notice,12,Soft);NoticeText->SetWrapTextAt(bDev?274:218);auto* Feedback=Panel(NoticeText,FMargin(12));auto* FeedbackSlot=Root->AddChildToCanvas(Feedback);FeedbackSlot->SetAnchors(FAnchors(1,0));FeedbackSlot->SetOffsets(bDev?FMargin(-RiftBattleLayout::DeveloperRightSidebar,94,302,92):FMargin(-RiftBattleLayout::NormalRightSidebar,112,244,92));
    auto* Info=WidgetTree->ConstructWidget<UVerticalBox>();SelectedName=Text(TEXT("YOUR HAND"),18,Gold);Add(Info,SelectedName,2);SelectedStats=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),MakeUniqueObjectName(WidgetTree,UTextBlock::StaticClass(),TEXT("SelectedCardStats")));SelectedStats->SetFont(RiftTypography::Font("Regular",13));SelectedStats->SetColorAndOpacity(White);SelectedStats->SetAutoWrapText(true);Add(Info,SelectedStats,4);Add(Info,Text(TEXT("Right click: cancel"),11,Soft),3);auto* InfoPanel=Panel(Info);auto* InfoSlot=Root->AddChildToCanvas(InfoPanel);InfoSlot->SetOffsets(FMargin(24,112,244,232));
    if(S->phase==rift::Phase::Finished){InfoPanel->SetVisibility(ESlateVisibility::Collapsed);Feedback->SetVisibility(ESlateVisibility::Collapsed);}
    auto* Announcement=WidgetTree->ConstructWidget<UVerticalBox>();AnnouncementText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),MakeUniqueObjectName(WidgetTree,UTextBlock::StaticClass(),TEXT("BattleAnnouncement")));AnnouncementText->SetFont(RiftTypography::Font("Bold",25));AnnouncementText->SetColorAndOpacity(Gold);AnnouncementText->SetJustification(ETextJustify::Center);Add(Announcement,AnnouncementText,8);AnnouncementPanel=Panel(Announcement,FMargin(20,12));AnnouncementPanel->SetVisibility(ESlateVisibility::Collapsed);auto* AnnounceSlot=Root->AddChildToCanvas(AnnouncementPanel);AnnounceSlot->SetAnchors(FAnchors(.5f,.31f));AnnounceSlot->SetOffsets(FMargin(-260,0,520,84));
    if(bDev){auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();auto* Dev=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Dev);auto* Frame=Panel(Scroll,FMargin(12));auto* LayoutSlot=Root->AddChildToCanvas(Frame);LayoutSlot->SetAnchors(FAnchors(1,0,1,1));LayoutSlot->SetOffsets(FMargin(-RiftBattleLayout::DeveloperRightSidebar,202,302,RiftBattleLayout::HandTop+RiftBattleLayout::WorldGutter));Developer(Dev);}
    DragImage=WidgetTree->ConstructWidget<UImage>();auto* GhostFit=WidgetTree->ConstructWidget<UScaleBox>();GhostFit->SetStretch(EStretch::ScaleToFit);GhostFit->SetContent(DragImage);DragFrame=Panel(GhostFit,FMargin(3));DragFrame->SetVisibility(ESlateVisibility::Collapsed);auto* GhostSlot=Root->AddChildToCanvas(DragFrame);GhostSlot->SetOffsets(FMargin(0,0,76,96));GhostSlot->SetZOrder(8);
    if(S->phase==rift::Phase::Finished){
        auto* R=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();bEndReplaySaving=R->IsSaving();const bool Ready=!bEndReplaySaving&&!R->LatestFilename.IsEmpty();auto* Summary=WidgetTree->ConstructWidget<UVerticalBox>();auto* Title=Text(S->winner==0?TEXT("VICTORY"):S->winner==1?TEXT("DEFEAT"):TEXT("DRAW"),34,S->winner==0?Gold:White);Title->SetJustification(ETextJustify::Center);Add(Summary,Title,8);auto* Result=Text(FString::Printf(TEXT("%d  —  %d CROWNS"),S->crowns[0],S->crowns[1]),22,Blue);Result->SetJustification(ETextJustify::Center);Add(Summary,Result,6);auto* Reason=Text(ResultLabel(S->resultReason),14,Soft);Reason->SetJustification(ETextJustify::Center);Add(Summary,Reason,8);
        if(bEndReplaySaving)Add(Summary,Text(TEXT("Saving your replay…"),12,Soft));else if(!Ready)Add(Summary,Text(R->LastError.IsEmpty()?TEXT("Replay unavailable."):R->LastError,12,Gold));
        auto Open=[this](const FString& Destination){auto* Replay=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();if(Replay->IsSaving()||Replay->LatestFilename.IsEmpty()){Say(TEXT("Replay is not ready yet."));return;}if(Replay->OpenReplay(Replay->LatestFilename)){OpenedReplay=Replay->LatestFilename;if(Destination==TEXT("Analysis"))Replay->SetSpeed(0);Navigate(Destination);}else Say(Replay->LastError);};
        Add(Summary,Button(TEXT("PLAY AGAIN"),[this,M](){M->StartMatch(M->IsTraining());HandIndex=-1;LastPhase.Empty();LastSurge=1;Navigate(TEXT("Battle"));},true),8);auto* Review=Row(Summary);auto* A=Button(TEXT("MATCH ANALYSIS"),[Open](){Open(TEXT("Analysis"));});A->SetIsEnabled(Ready);Add(Review,A,true);auto* W=Button(TEXT("WATCH REPLAY"),[Open](){Open(TEXT("ReplayView"));});W->SetIsEnabled(Ready);Add(Review,W,true);Add(Summary,Button(TEXT("HOME"),[this](){Navigate(TEXT("Home"));}),6);auto* End=Panel(Summary,FMargin(24));auto* LayoutSlot=Root->AddChildToCanvas(End);LayoutSlot->SetAnchors(FAnchors(.5,.5));LayoutSlot->SetOffsets(FMargin(-260,-215,520,430));
    }
    UpdateBattleHUD(0);
}

void URiftUIWidget::ToggleBattleMenu(){
    if(!IsLiveBattleView())return;auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();
    if(auto* PC=Cast<ARiftPlayerController>(GetOwningPlayer()))PC->CancelCardDrag();
    if(bMenuPausedMatch){M->SetSpeed(MenuResumeSpeed);bMenuPausedMatch=false;if(BattleMenu)BattleMenu->SetVisibility(ESlateVisibility::Collapsed);return;}
    MenuResumeSpeed=M->GetSpeed();M->SetSpeed(0);bMenuPausedMatch=true;HandIndex=-1;bSpawnArmed=false;
    if(BattleMenu){BattleMenu->SetVisibility(ESlateVisibility::Visible);return;}
    auto* Choices=WidgetTree->ConstructWidget<UVerticalBox>();auto* Title=Text(TEXT("BATTLE PAUSED"),25,Gold);Title->SetJustification(ETextJustify::Center);Add(Choices,Title,10);Add(Choices,Button(TEXT("RESUME BATTLE"),[this](){ToggleBattleMenu();},true));
    Add(Choices,Button(TEXT("SETTINGS"),[this](){Navigate(TEXT("Settings"));}));Add(Choices,Button(TEXT("LOADOUT"),[this](){Navigate(TEXT("Loadout"));}));Add(Choices,Button(TEXT("FIELD MANUAL"),[this](){Navigate(TEXT("Help"));}));Add(Choices,Button(TEXT("RESTART BATTLE"),[this,M](){bMenuPausedMatch=false;M->StartMatch(M->IsTraining());HandIndex=-1;LastPhase.Empty();LastSurge=1;Navigate(TEXT("Battle"));}));Add(Choices,Button(TEXT("EXIT TO HOME"),[this](){Navigate(TEXT("Home"));}));
    BattleMenu=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),MakeUniqueObjectName(WidgetTree,UBorder::StaticClass(),TEXT("BattlePauseMenu")));BattleMenu->SetBrushColor(FLinearColor(.004f,.008f,.02f,.82f));auto* Fit=WidgetTree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);Fit->SetStretchDirection(EStretchDirection::DownOnly);auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(360);Size->SetContent(Panel(Choices,FMargin(24)));Fit->SetContent(Size);BattleMenu->SetContent(Fit);auto* LayoutSlot=Root->AddChildToCanvas(BattleMenu);LayoutSlot->SetAnchors(FAnchors(0,0,1,1));LayoutSlot->SetOffsets(FMargin(0));LayoutSlot->SetZOrder(20);
}

void URiftUIWidget::UpdateBattleHUD(float Delta){
    auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();const auto* S=M->ViewState();if(!S)return;if(S->phase==rift::Phase::Tiebreaker||S->phase==rift::Phase::Finished){HandIndex=-1;bSpawnArmed=false;}auto* R=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();NoticeAge+=Delta;AnnouncementAge+=Delta;
    if(DragFrame){auto* PC=Cast<ARiftPlayerController>(GetOwningPlayer());const bool Visible=PC&&CanAcceptBattleInput()&&PC->IsDraggingCard()&&HandIndex>=0;const FVector2D Point=Visible?Root->GetCachedGeometry().AbsoluteToLocal(PC->CardDragScreenPosition()):FVector2D::ZeroVector;
        DragFrame->SetVisibility(Visible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        if(Visible){const FString Id=SelectedCardId();if(Id!=DragArtId){DragArtId=Id;if(auto* Art=LoadObject<UTexture2D>(nullptr,*FString::Printf(TEXT("/Game/Rift/CardArt/T_Card_%s.T_Card_%s"),*Id,*Id)))DragImage->SetBrushFromTexture(Art,true);}FVector2D Tile;const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*Id));const bool Valid=PC->CursorTile(Tile)&&M->CanPlace(HandIndex,Tile)&&Card&&S->aether[0]+1e-9>=Card->cost;DragFrame->SetBrush(FSlateRoundedBoxBrush(Ink,7.f,Valid?Gold:Red,2.f));if(auto* GhostSlot=Cast<UCanvasPanelSlot>(DragFrame->Slot))GhostSlot->SetPosition(Point-FVector2D(74,96));}}
    for(int32 I=0;I<HandButtons.Num();++I){const float Target=I==HandIndex?-3.f:0.f;HandButtons[I]->SetRenderTranslation(FVector2D(0,FMath::FInterpTo(HandButtons[I]->GetRenderTransform().Translation.Y,Target,Delta,18)));if(HandImages.IsValidIndex(I)){const auto* PC=Cast<ARiftPlayerController>(GetOwningPlayer());HandImages[I]->SetRenderOpacity(PC&&PC->IsDraggingCard()&&I==HandIndex?.35f:1.f);}}
    if(AnnouncementPanel&&AnnouncementPanel->GetVisibility()!=ESlateVisibility::Collapsed){const float Alpha=S->phase==rift::Phase::Tiebreaker?1.f:FMath::Clamp((1.75f-AnnouncementAge)/.35f,0.f,1.f);AnnouncementPanel->SetRenderOpacity(Alpha);if(Alpha<=0)AnnouncementPanel->SetVisibility(ESlateVisibility::Collapsed);}
    if(Delta>0&&RefreshClock+Delta<.10f)return;
    const FString Phase=M->GetPhase();const int32 Time=FMath::Max(0,FMath::CeilToInt(M->GetTimeRemaining()));if(TimerText)TimerText->SetText(FText::FromString(FString::Printf(TEXT("%d:%02d"),Time/60,Time%60)));if(PhaseText)PhaseText->SetText(FText::FromString(M->GetSpeed()==0&&S->phase!=rift::Phase::Finished?TEXT("PAUSED · ")+Phase.ToUpper():Phase.ToUpper()));if(ScoreText)ScoreText->SetText(FText::AsNumber(S->crowns[0]));if(EnemyScore)EnemyScore->SetText(FText::AsNumber(S->crowns[1]));
    if(AetherText)AetherText->SetText(FText::FromString(FString::Printf(TEXT("%.1f"),S->aether[0])));if(AetherMeter)AetherMeter->Bank=S->aether[0];const int32 Surge=S->elapsed>=240?3:S->elapsed>=120?2:1;if(SurgeText)SurgeText->SetText(FText::FromString(FString::Printf(TEXT("%d× AETHER"),Surge)));
    if((!LastPhase.IsEmpty()&&LastPhase!=Phase)||Surge>LastSurge){if(AnnouncementText)AnnouncementText->SetText(FText::FromString(S->phase==rift::Phase::Tiebreaker?TEXT("TIEBREAKER"):S->phase==rift::Phase::Overtime&&LastPhase!=Phase?TEXT("OVERTIME — SUDDEN DEATH"):Surge>LastSurge?FString::Printf(TEXT("%d× AETHER"),Surge):Phase.ToUpper()));AnnouncementAge=0;if(AnnouncementPanel)AnnouncementPanel->SetVisibility(S->phase==rift::Phase::Finished?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);}
    LastPhase=Phase;LastSurge=Surge;if(AnnouncementPanel&&AnnouncementPanel->GetVisibility()!=ESlateVisibility::Collapsed){const bool Persistent=S->phase==rift::Phase::Tiebreaker;const float Opacity=Persistent?1.f:FMath::Clamp((1.75f-AnnouncementAge)/.35f,0.f,1.f);AnnouncementPanel->SetRenderOpacity(Opacity);AnnouncementPanel->SetRenderScale(FVector2D(FMath::Lerp(.94f,1.f,FMath::Clamp(AnnouncementAge/.16f,0.f,1.f))));if(Opacity<=0)AnnouncementPanel->SetVisibility(ESlateVisibility::Collapsed);}
    if(NextImage&&!S->queues[0].empty()){const auto* C=rift::FindCard(S->queues[0].front());if(C){FString Id=GameString(C->id);if(Id!=NextArtId){NextArtId=Id;if(auto* Art=LoadObject<UTexture2D>(nullptr,*FString::Printf(TEXT("/Game/Rift/CardArt/T_Card_%s.T_Card_%s"),*Id,*Id)))NextImage->SetBrushFromTexture(Art,true);}if(NextText)NextText->SetText(FText::FromString(GameString(C->name)));}}
    for(int32 I=0;I<HandButtons.Num();++I){const auto* C=rift::FindCard(S->hands[0][I]);if(!C)continue;const FString Id=GameString(C->id);if(HandArtIds[I]!=Id){HandArtIds[I]=Id;if(auto* Art=LoadObject<UTexture2D>(nullptr,*FString::Printf(TEXT("/Game/Rift/CardArt/T_Card_%s.T_Card_%s"),*Id,*Id)))HandImages[I]->SetBrushFromTexture(Art,true);}HandText[I]->SetText(FText::FromString(GameString(C->name)));HandCost[I]->SetText(FText::AsNumber(C->cost));HandButtons[I]->SetToolTipText(FText::FromString(GameString(C->name)+TEXT("\n")+CardSummary(*C)));const bool Affordable=S->aether[0]+1e-9>=C->cost;HandImages[I]->SetColorAndOpacity(Affordable?FLinearColor::White:FLinearColor(.42f,.46f,.55f,1));HandButtons[I]->SetBackgroundColor(I==HandIndex?FLinearColor(.31f,.20f,.06f,1):Affordable?FLinearColor(.06f,.13f,.23f,1):FLinearColor(.035f,.05f,.075f,1));HandButtons[I]->SetIsEnabled(CanAcceptBattleInput());}
    if(SelectedName&&SelectedStats){if(HandIndex>=0&&HandIndex<4){const auto* C=rift::FindCard(S->hands[0][HandIndex]);if(C){SelectedName->SetText(FText::FromString(GameString(C->name)));SelectedStats->SetText(FText::FromString(CardSummary(*C)));}}else{SelectedName->SetText(FText::FromString(TEXT("YOUR HAND")));SelectedStats->SetText(FText::FromString(TEXT("Select a card to inspect it.\nDeploy troops on your half.\nSpells can target either side.")));}}
    if(NoticeText&&NoticeAge>4)NoticeText->SetText(FText::FromString(TEXT("1–4: select a card\nDrag or click to deploy\nESC: battle menu")));UpdateTrainingReadout();
    if(S->phase==rift::Phase::Finished&&(!bEndPresented||bEndReplaySaving!=R->IsSaving())){bEndPresented=true;Navigate(TEXT("Battle"));return;}if(S->phase!=rift::Phase::Finished){bEndPresented=false;bEndReplaySaving=false;}
}
