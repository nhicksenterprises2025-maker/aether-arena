#include "RiftUIWidget.h"
#include "RiftTypography.h"
#include "RiftUIPrimitives.h"
#include "RiftMatchSubsystem.h"
#include "RiftGameMode.h"
#include "RiftHandButton.h"
#include "RiftProfileSubsystem.h"
#include "RiftReplaySubsystem.h"
#include "RiftMetaSimulationSubsystem.h"
#include "Presentation/RiftBattleAudioSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ButtonSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ProgressBar.h"
#include "Components/Image.h"
#include "Components/WrapBox.h"
#include "Engine/Texture2D.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Rendering/DrawElements.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    const FLinearColor Ivory(.95f,.94f,.89f,1),Muted(.49f,.61f,.75f,1),Brass(.95f,.64f,.19f,1),Cyan(.22f,.69f,.97f,1),Stone(.018f,.035f,.065f,.99f);
    FString FS(const std::string& S){return UTF8_TO_TCHAR(S.c_str());}
    FString JS(const TSharedPtr<FJsonObject>& O,const TCHAR* Key){FString V;if(O)O->TryGetStringField(Key,V);return V;}
    double JN(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,double Default=0){double V=Default;if(O)O->TryGetNumberField(Key,V);return FMath::IsFinite(V)?V:Default;}
    FString Pretty(const FString& Key){if(Key==TEXT("counter"))return TEXT("Counter Push");FString Out;for(int32 I=0;I<Key.Len();++I){if(I>0&&FChar::IsUpper(Key[I])&&!FChar::IsUpper(Key[I-1]))Out+=TEXT(" ");Out.AppendChar(I==0?FChar::ToUpper(Key[I]):Key[I]);}return Out;}
    const TArray<FString> Styles={TEXT("all"),TEXT("beatdown"),TEXT("aggro"),TEXT("control"),TEXT("cycle"),TEXT("split"),TEXT("spell_cycle"),TEXT("counter")};
    const TArray<FString> Archetypes={TEXT("all"),TEXT("Beatdown"),TEXT("Control"),TEXT("Cycle"),TEXT("Bridge Pressure"),TEXT("Split Lane"),TEXT("Air Pressure"),TEXT("Siege"),TEXT("Spell Control"),TEXT("Defensive"),TEXT("Hybrid")};
    bool CardMatches(const rift::Card& Card,const FString& Cost,const FString& Type,const FString& Trait,const FString& SearchText=TEXT(""))
    {
        if(!SearchText.IsEmpty()&&!FS(Card.name).Contains(SearchText,ESearchCase::IgnoreCase)&&!FS(Card.id).Contains(SearchText,ESearchCase::IgnoreCase))return false;
        if(Cost!=TEXT("all")&&Card.cost!=FCString::Atoi(*Cost))return false;
        const FString Category=Card.spell?TEXT("Spell"):Card.building?TEXT("Building"):TEXT("Troop");
        if(Type!=TEXT("all")&&Category!=Type)return false;
        if(Trait==TEXT("Air")&&!Card.flying)return false;
        if(Trait==TEXT("Ground")&&(Card.flying||Card.spell))return false;
        if(Trait==TEXT("Anti-air")&&!Card.canHitAir&&!Card.spell&&!Card.auraDamage)return false;
        if(Trait==TEXT("Win condition")&&!Card.structuresOnly)return false;
        if(Trait==TEXT("Swarm")&&Card.count<2)return false;
        if(Trait==TEXT("Splash")&&!Card.splash&&!Card.spell&&!Card.auraDamage)return false;
        return true;
    }
    FString Metric(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key)
    {
        double Number=0;if(!Object||!Object->TryGetNumberField(Key,Number)||!FMath::IsFinite(Number))return TEXT("unavailable");
        const FString Name(Key);
        const bool Count=Name.EndsWith(TEXT("N"))||Name==TEXT("appearances")||Name==TEXT("mirrorExclusions")||Name==TEXT("games")||Name==TEXT("n")||Name==TEXT("plays")||Name==TEXT("spawns")||Name==TEXT("kills")||Name==TEXT("wins")||Name==TEXT("losses")||Name==TEXT("draws")||Name==TEXT("matches")||Name==TEXT("count");
        return Count?FString::Printf(TEXT("%.0f"),Number):FString::Printf(TEXT("%.2f"),Number);
    }
}
URiftComboBox::URiftComboBox(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
    InitFont(RiftTypography::Font("Regular",14));InitForegroundColor(Ivory);
}
void URiftActionButton::Bind(TFunction<void()> Callback,bool OnPress){Action=MoveTemp(Callback);OnReleased.AddDynamic(this,&URiftActionButton::InvokeRelease);if(OnPress)OnPressed.AddDynamic(this,&URiftActionButton::Invoke);else OnClicked.AddDynamic(this,&URiftActionButton::Invoke);}
void URiftActionButton::Invoke(){
    if(GetWorld()&&GetWorld()->GetGameInstance())if(auto* Audio=GetWorld()->GetGameInstance()->GetSubsystem<URiftBattleAudioSubsystem>())Audio->PlayUI(TEXT("ui_click"));if(Action)Action();
}
void URiftActionButton::InvokeRelease(){if(ReleaseAction)ReleaseAction();}
void URiftValueSlider::Bind(TFunction<void(float)> Callback){Action=MoveTemp(Callback);OnValueChanged.AddDynamic(this,&URiftValueSlider::Invoke);}
void URiftValueSlider::Invoke(float SliderValue){if(Action)Action(SliderValue);}
TSharedRef<SWidget> URiftChartWidget::RebuildWidget(){if(!WidgetTree->RootWidget){auto* B=WidgetTree->ConstructWidget<UBorder>();B->SetBrushColor(FLinearColor(.018f,.035f,.065f,1));WidgetTree->RootWidget=B;}return Super::RebuildWidget();}
int32 URiftChartWidget::NativePaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool Enabled)const
{
    Layer=Super::NativePaint(Args,G,Clip,Elements,Layer,Style,Enabled);const FVector2D Size=G.GetLocalSize();const FVector2D Origin(48,12),Area(FMath::Max(1.0,Size.X-62),FMath::Max(1.0,Size.Y-40));
    double MinX=0,MaxX=1,MinY=0,MaxY=1;for(const auto& Series:{SeriesA,SeriesB})for(auto P:Series){MinX=FMath::Min(MinX,P.X);MaxX=FMath::Max(MaxX,P.X);MinY=FMath::Min(MinY,P.Y);MaxY=FMath::Max(MaxY,P.Y);}
    for(int32 I=0;I<5;++I){float Y=Origin.Y+Area.Y*I/4;TArray<FVector2D> Line={FVector2D(Origin.X,Y),FVector2D(Origin.X+Area.X,Y)};FSlateDrawElement::MakeLines(Elements,Layer+1,G.ToPaintGeometry(),Line,ESlateDrawEffect::None,FLinearColor(.18f,.26f,.29f,.7f),true,1);FSlateDrawElement::MakeText(Elements,Layer+2,G.ToPaintGeometry(FVector2D(45,18),FSlateLayoutTransform(FVector2D(0,Y-6))),FString::Printf(TEXT("%.0f"),MaxY-(MaxY-MinY)*I/4),RiftTypography::Font("Regular",10),ESlateDrawEffect::None,Muted);}
    auto Draw=[&](const TArray<FVector2D>& Values,FLinearColor Color){TArray<FVector2D> Points;for(auto V:Values)Points.Add(Origin+FVector2D((V.X-MinX)/FMath::Max(1.0,MaxX-MinX)*Area.X,Area.Y-(V.Y-MinY)/FMath::Max(1.0,MaxY-MinY)*Area.Y));if(Points.Num()>1)FSlateDrawElement::MakeLines(Elements,Layer+3,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,2);};Draw(SeriesA,Cyan);Draw(SeriesB,Brass);
    FSlateDrawElement::MakeText(Elements,Layer+4,G.ToPaintGeometry(FVector2D(Size.X,18),FSlateLayoutTransform(FVector2D(48,Size.Y-20))),Caption,RiftTypography::Font("Regular",11),ESlateDrawEffect::None,Muted);return Layer+4;
}

UTextBlock* URiftUIWidget::Text(const FString& Value,int32 Size,FLinearColor Color){auto* W=WidgetTree->ConstructWidget<UTextBlock>();W->SetText(FText::FromString(Value));W->SetFont(RiftTypography::Font(Size>=17?"Bold":"Regular",FMath::Max(9,Size)));W->SetColorAndOpacity(Color);W->SetShadowOffset(FVector2D(0,1));W->SetShadowColorAndOpacity(FLinearColor(0,0,0,.42f));W->SetAutoWrapText(true);return W;}
UBorder* URiftUIWidget::Panel(UWidget* Content,FMargin Insets,FLinearColor Color){auto* W=WidgetTree->ConstructWidget<UBorder>();W->SetBrush(FSlateRoundedBoxBrush(Color,10.f,FLinearColor(.18f,.28f,.40f,1),1.f));W->SetBrushColor(FLinearColor::White);W->SetPadding(Insets);W->SetContent(Content);return W;}
UWidget* URiftUIWidget::Badge(const FString& Value,FLinearColor Color,int32 Size){auto* W=Panel(Text(Value,Size,Ivory),FMargin(8,4),Color);W->SetHorizontalAlignment(HAlign_Center);W->SetVerticalAlignment(VAlign_Center);return W;}
URiftActionButton* URiftUIWidget::Button(const FString& Label,TFunction<void()> Action,bool Accent,bool HandCard)
{
    auto* W=WidgetTree->ConstructWidget<URiftActionButton>(HandCard?URiftHandButton::StaticClass():URiftActionButton::StaticClass());
    FButtonStyle ControlStyle;ControlStyle.SetNormal(FSlateRoundedBoxBrush(FLinearColor::White,7.f,FLinearColor(.58f,.70f,.84f,.55f),1.f));ControlStyle.SetHovered(FSlateRoundedBoxBrush(FLinearColor(1.12f,1.12f,1.12f,1),7.f,FLinearColor(.88f,.95f,1,1),1.5f));ControlStyle.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.72f,.75f,.80f,1),7.f,FLinearColor(.88f,.95f,1,1),1.f));ControlStyle.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(.42f,.46f,.53f,1),7.f));ControlStyle.SetNormalPadding(FMargin(14,10));ControlStyle.SetPressedPadding(FMargin(14,12,14,8));W->SetStyle(ControlStyle);
    auto* LabelText=Text(Label,14,Accent?FLinearColor(.09f,.05f,.015f,1):Ivory);LabelText->SetFont(RiftTypography::Font("Bold",14));LabelText->SetJustification(ETextJustify::Center);W->SetContent(LabelText);W->SetBackgroundColor(Accent?Brass:FLinearColor(.055f,.115f,.205f,1));W->Bind(MoveTemp(Action));return W;
}
UWidget* URiftUIWidget::Illustration(const FString& CardId,float Width,UImage** ImageOut)
{
    auto* Image=WidgetTree->ConstructWidget<UImage>();
    if(auto* Art=LoadObject<UTexture2D>(nullptr,*FString::Printf(TEXT("/Game/Rift/CardArt/T_Card_%s.T_Card_%s"),*CardId,*CardId)))Image->SetBrushFromTexture(Art,true);
    auto* Fit=WidgetTree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);Fit->SetContent(Image);
    auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(Width);Size->SetHeightOverride(Width*1.25f);Size->SetContent(Fit);if(ImageOut)*ImageOut=Image;return Size;
}
URiftActionButton* URiftUIWidget::CardButton(const FString& CardId,float Width,TFunction<void()> Action,bool Selected)
{
    Width=FMath::Max(128.0f,Width);
    const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*CardId));auto* W=Button(TEXT(""),MoveTemp(Action));W->SetBackgroundColor(Selected?FLinearColor(.29f,.19f,.055f,1):FLinearColor(.038f,.075f,.13f,1));
    auto CardStyle=W->GetStyle();CardStyle.SetNormalPadding(FMargin(4));CardStyle.SetPressedPadding(FMargin(4,5,4,3));W->SetStyle(CardStyle);
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();auto* Art=WidgetTree->ConstructWidget<UOverlay>();auto* ImageSlot=Art->AddChildToOverlay(Illustration(CardId,Width-8));ImageSlot->SetHorizontalAlignment(HAlign_Center);
    auto* CostOrb=Badge(Card?FString::FromInt(Card->cost):TEXT("?"),FLinearColor(.55f,.24f,.94f,1),18);auto* OrbSlot=Art->AddChildToOverlay(CostOrb);OrbSlot->SetHorizontalAlignment(HAlign_Left);OrbSlot->SetVerticalAlignment(VAlign_Top);OrbSlot->SetPadding(FMargin(1,1,0,0));
    if(Selected){auto* SelectedSlot=Art->AddChildToOverlay(Badge(TEXT("IN DECK"),FLinearColor(.22f,.14f,.025f,1),9));SelectedSlot->SetHorizontalAlignment(HAlign_Right);SelectedSlot->SetVerticalAlignment(VAlign_Bottom);}Add(Column,Art,0);
    auto* Name=Text(Card?FS(Card->name):CardId,14);Name->SetJustification(ETextJustify::Center);Name->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);Name->SetWrapTextAt(FMath::Max(24.0f,Width-8));Add(Column,Name,4);
    auto* Cost=Text(Card?Card->spell?TEXT("SPELL"):Card->building?TEXT("BUILDING"):Card->flying?TEXT("AIR TROOP"):TEXT("GROUND TROOP"):TEXT(""),10,Selected?Brass:Muted);Cost->SetJustification(ETextJustify::Center);Add(Column,Cost,3);
    auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(Width);Size->SetContent(Column);W->SetContent(Size);return W;
}
void URiftUIWidget::Add(UVerticalBox* Box,UWidget* W,float Spacing){auto* PanelSlot=Box->AddChildToVerticalBox(W);PanelSlot->SetPadding(FMargin(Spacing));}
void URiftUIWidget::Add(UHorizontalBox* Box,UWidget* W,bool Fill,float Spacing){auto* PanelSlot=Box->AddChildToHorizontalBox(W);PanelSlot->SetPadding(FMargin(Spacing));if(Fill)PanelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));}
UHorizontalBox* URiftUIWidget::Row(UVerticalBox* Target){auto* W=WidgetTree->ConstructWidget<UHorizontalBox>();Add(Target?Target:Body.Get(),W,2);return W;}
UEditableTextBox* URiftUIWidget::Edit(const FString& Value,const FString& Hint)
{
    auto* W=WidgetTree->ConstructWidget<UEditableTextBox>();W->SetText(FText::FromString(Value));W->SetHintText(FText::FromString(Hint));auto EditStyle=W->GetWidgetStyle();
    EditStyle.SetFont(RiftTypography::Font("Regular",14));EditStyle.TextStyle.SetColorAndOpacity(Ivory);EditStyle.SetForegroundColor(Ivory);EditStyle.SetFocusedForegroundColor(Ivory);EditStyle.SetReadOnlyForegroundColor(Muted);EditStyle.SetBackgroundColor(FLinearColor::White);EditStyle.SetPadding(FMargin(10,8));
    EditStyle.SetBackgroundImageNormal(FSlateColorBrush(FLinearColor(.018f,.035f,.065f,1)));EditStyle.SetBackgroundImageHovered(FSlateColorBrush(FLinearColor(.06f,.11f,.125f,1)));EditStyle.SetBackgroundImageFocused(FSlateColorBrush(FLinearColor(.085f,.16f,.18f,1)));EditStyle.SetBackgroundImageReadOnly(FSlateColorBrush(FLinearColor(.045f,.07f,.08f,1)));W->SetWidgetStyle(EditStyle);return W;
}
UComboBoxString* URiftUIWidget::Combo(const TArray<FString>& Values,const FString& Selected)
{
    auto* W=WidgetTree->ConstructWidget<URiftComboBox>();auto Control=W->GetWidgetStyle();auto ComboButton=Control.ComboButtonStyle;auto ButtonStyle=ComboButton.ButtonStyle;
    ButtonStyle.SetNormal(FSlateColorBrush(FLinearColor(.035f,.07f,.13f,1)));ButtonStyle.SetHovered(FSlateColorBrush(FLinearColor(.055f,.13f,.23f,1)));ButtonStyle.SetPressed(FSlateColorBrush(FLinearColor(.08f,.19f,.33f,1)));ButtonStyle.SetDisabled(FSlateColorBrush(FLinearColor(.045f,.065f,.07f,1)));ButtonStyle.SetNormalForeground(Ivory);ButtonStyle.SetHoveredForeground(Ivory);ButtonStyle.SetPressedForeground(Ivory);ButtonStyle.SetDisabledForeground(Muted);ButtonStyle.SetNormalPadding(FMargin(8,6));ButtonStyle.SetPressedPadding(FMargin(8,6));
    ComboButton.SetButtonStyle(ButtonStyle);ComboButton.SetMenuBorderBrush(FSlateColorBrush(FLinearColor(.018f,.035f,.065f,1)));ComboButton.SetMenuBorderPadding(FMargin(1));ComboButton.DownArrowImage.TintColor=Brass;Control.SetComboButtonStyle(ComboButton);Control.SetMenuRowPadding(FMargin(8,6));W->SetWidgetStyle(Control);W->SetContentPadding(FMargin(8,5));
    auto Item=W->GetItemStyle();Item.SetTextColor(Ivory);Item.SetSelectedTextColor(Ivory);Item.SetEvenRowBackgroundBrush(FSlateColorBrush(FLinearColor(.018f,.035f,.065f,1)));Item.SetOddRowBackgroundBrush(FSlateColorBrush(FLinearColor(.026f,.048f,.085f,1)));Item.SetEvenRowBackgroundHoveredBrush(FSlateColorBrush(FLinearColor(.08f,.19f,.33f,1)));Item.SetOddRowBackgroundHoveredBrush(FSlateColorBrush(FLinearColor(.08f,.19f,.33f,1)));Item.SetActiveBrush(FSlateColorBrush(FLinearColor(.1f,.24f,.40f,1)));Item.SetActiveHoveredBrush(FSlateColorBrush(FLinearColor(.12f,.28f,.46f,1)));Item.SetInactiveBrush(FSlateColorBrush(FLinearColor(.045f,.11f,.2f,1)));Item.SetInactiveHoveredBrush(FSlateColorBrush(FLinearColor(.12f,.25f,.27f,1)));W->SetItemStyle(Item);
    for(const auto& V:Values)W->AddOption(V);if(Values.Contains(Selected))W->SetSelectedOption(Selected);else if(Values.Num())W->SetSelectedIndex(0);return W;
}
void URiftUIWidget::Say(const FString& Message){Notice=Message;NoticeAge=0;if(NoticeText){NoticeText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);NoticeText->SetText(FText::FromString(Message));NoticeText->SetToolTipText(FText::FromString(Message));}if(!Message.IsEmpty())if(auto* Audio=GetGameInstance()->GetSubsystem<URiftBattleAudioSubsystem>()){FString Lower=Message.ToLower();if(Lower.Contains(TEXT("cannot"))||Lower.Contains(TEXT("invalid"))||Lower.Contains(TEXT("error"))||Lower.Contains(TEXT("blocked"))||Lower.Contains(TEXT("failed")))Audio->PlayUI(TEXT("ui_error"));else if(Lower.Contains(TEXT("saved"))||Lower.Contains(TEXT("exported"))||Lower.Contains(TEXT("imported"))||Lower.Contains(TEXT("applied"))||Lower.Contains(TEXT("recorded")))Audio->PlayUI(TEXT("ui_save"));}}
TSharedRef<SWidget> URiftUIWidget::RebuildWidget()
{
    // Slate retains this canvas; navigation replaces children rather than its cached root.
    if(!Root){Root=WidgetTree->ConstructWidget<UCanvasPanel>();Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);WidgetTree->RootWidget=Root;}
    return Super::RebuildWidget();
}
void URiftUIWidget::NativeConstruct(){Super::NativeConstruct();Navigate(TEXT("Home"));}
void URiftUIWidget::Navigate(const FString& Destination)
{
    if(auto* PC=Cast<ARiftPlayerController>(GetOwningPlayer()))PC->CancelCardDrag();
    if(Destination==TEXT("Battle")&&bMenuPausedMatch){GetWorld()->GetSubsystem<URiftMatchSubsystem>()->SetSpeed(MenuResumeSpeed);bMenuPausedMatch=false;}
    if(Destination==TEXT("Home")){auto* R=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();if(R->IsPlaying())R->CloseReplay();auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();if(M->IsActive())M->LeaveMatch();HandIndex=-1;bSpawnArmed=false;}
    if(Destination==TEXT("Home"))bMenuPausedMatch=false;
    Page=Destination;if(Page==TEXT("MetaGuide")){Page=TEXT("Meta");Tab=TEXT("Stats Guide");MetaDetail.Reset();}if(!Root){Root=WidgetTree->ConstructWidget<UCanvasPanel>();Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);WidgetTree->RootWidget=Root;}Root->ClearChildren();
    TimerText=nullptr;ScoreText=nullptr;AetherText=nullptr;NextText=nullptr;AetherBar=nullptr;DevReadout=nullptr;ReplayPosition=nullptr;ReplaySeek=nullptr;MetaStatus=nullptr;Rows=nullptr;NoticeText=nullptr;HandText.Reset();HandCost.Reset();HandButtons.Reset();HandImages.Reset();HandArtIds.Reset();
    TrainingStatus=nullptr;SpawnArmButton=nullptr;EnemyAIButton=nullptr;FriendlyAIButton=nullptr;TrainingSpeedButtons.Reset();ReplaySpeedButtons.Reset();LastReplayLedgerPosition=-1;LastTrainingTower.Empty();PhaseText=nullptr;EnemyScore=nullptr;SurgeText=nullptr;SelectedName=nullptr;SelectedStats=nullptr;NextImage=nullptr;AetherMeter=nullptr;BattleMenu=nullptr;AnnouncementPanel=nullptr;AnnouncementText=nullptr;NextArtId.Empty();DragImage=nullptr;DragFrame=nullptr;DragArtId.Empty();
    SetRenderTransform(FWidgetTransform(FVector2D::ZeroVector,FVector2D(1,1), FVector2D::ZeroVector,0));
    if(Page==TEXT("Battle")){Battle();return;}if(Page==TEXT("ReplayView")){ReplayView();return;}
    Shell(Page==TEXT("CardDetail")?TEXT("CARD DETAILS"):Page==TEXT("Help")?TEXT("FIELD MANUAL"):Page==TEXT("Analysis")?TEXT("MATCH ANALYSIS"):Page==TEXT("Meta")?TEXT("META LAB"):Page.ToUpper());
    if(Page==TEXT("Home"))Home();else if(Page==TEXT("Profile"))Profile();else if(Page==TEXT("Loadout"))Loadout();else if(Page==TEXT("Cards"))Cards();else if(Page==TEXT("CardDetail"))CardDetail();else if(Page==TEXT("Settings"))Settings();else if(Page==TEXT("Replays"))Replays();else if(Page==TEXT("Analysis"))MatchAnalysis();else if(Page==TEXT("Meta"))Meta();else if(Page==TEXT("Help"))FieldManual();else PatchNotes();
}
void URiftUIWidget::Shell(const FString& Title)
{
    auto* Back=WidgetTree->ConstructWidget<URiftMenuBackdrop>();Back->SetVisibility(ESlateVisibility::HitTestInvisible);auto* BackSlot=Root->AddChildToCanvas(Back);BackSlot->SetAnchors(FAnchors(0,0,1,1));BackSlot->SetOffsets(FMargin(0));
    auto* Sidebar=WidgetTree->ConstructWidget<UVerticalBox>();auto* Side=Panel(Sidebar,FMargin(16,24),FLinearColor(.012f,.026f,.05f,1));auto* SideSlot=Root->AddChildToCanvas(Side);SideSlot->SetAnchors(FAnchors(0,0,0,1));SideSlot->SetOffsets(FMargin(16,16,204,16));
    auto* Crest=WidgetTree->ConstructWidget<URiftCrownIcon>();auto* CrestSize=WidgetTree->ConstructWidget<USizeBox>();CrestSize->SetWidthOverride(46);CrestSize->SetHeightOverride(38);CrestSize->SetContent(Crest);Add(Sidebar,CrestSize,2);
    Add(Sidebar,Text(TEXT("RIFT CROWN"),20,Brass),2);Add(Sidebar,Text(TEXT("A R E N A"),12,Muted),2);
    auto* Space=WidgetTree->ConstructWidget<USizeBox>();Space->SetHeightOverride(24);Add(Sidebar,Space,0);
    for(FString Name:{TEXT("Home"),TEXT("Loadout"),TEXT("Cards"),TEXT("Profile"),TEXT("Replays"),TEXT("Meta"),TEXT("Help"),TEXT("Settings"),TEXT("Patch Notes")}){const bool Active=Page==Name||(Page==TEXT("CardDetail")&&Name==TEXT("Cards"))||(Page==TEXT("PatchNotes")&&Name==TEXT("Patch Notes"));auto* TabButton=Button(Name,[this,Name](){Navigate(Name);},Active);auto* Label=Text(Name,15,Active?Stone:Ivory);TabButton->SetContent(Label);if(auto* LayoutSlot=Cast<UButtonSlot>(Label->Slot))LayoutSlot->SetHorizontalAlignment(HAlign_Left);Add(Sidebar,TabButton,4);}
    auto* P=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();auto* Header=WidgetTree->ConstructWidget<UHorizontalBox>();Add(Header,Text(Title==TEXT("HOME")?TEXT("BATTLE LOBBY"):Title,24),true);Add(Header,Badge(FString::Printf(TEXT("%d GOLD"),P->Gold),FLinearColor(.16f,.10f,.025f,1),13));Add(Header,Badge(FString::Printf(TEXT("%d GEMS"),P->Gems),FLinearColor(.025f,.14f,.14f,1),13));Add(Header,Button(P->Username,[this](){Navigate(TEXT("Profile"));}));auto* HeaderSlot=Root->AddChildToCanvas(Header);HeaderSlot->SetAnchors(FAnchors(0,0,1,0));HeaderSlot->SetOffsets(FMargin(250,28,30,52));
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Scroll->SetScrollbarThickness(FVector2D(5,5));auto* ContentSlot=Root->AddChildToCanvas(Scroll);ContentSlot->SetAnchors(FAnchors(0,0,1,1));ContentSlot->SetOffsets(FMargin(244,100,24,22));Body=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Body);
    if(GetWorld()->GetSubsystem<URiftMatchSubsystem>()->IsActive())Add(Body,Button(TEXT("RETURN TO BATTLE"),[this](){Navigate(TEXT("Battle"));},true));
    NoticeText=Text(Notice,13,Cyan);NoticeText->SetVisibility(Notice.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);Add(Body,NoticeText,4);
}
void URiftUIWidget::Home()
{
    auto* P=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();auto* Hero=WidgetTree->ConstructWidget<UVerticalBox>();Add(Hero,Text(TEXT("YOUR NEXT CROWN AWAITS"),32),2);Add(Hero,Text(TEXT("Eight cards. Two lanes. One decisive push."),16,Muted),4);
    auto* Record=Row(Hero);Add(Record,Badge(FString::Printf(TEXT("%d WINS"),P->Wins),FLinearColor(.035f,.105f,.18f,1),13));Add(Record,Badge(FString::Printf(TEXT("%d CROWNS"),P->Crowns),FLinearColor(.14f,.09f,.025f,1),13));Add(Record,Badge(FString::Printf(TEXT("%d MATCHES"),P->Matches),FLinearColor(.035f,.105f,.18f,1),13));
    auto* Actions=WidgetTree->ConstructWidget<UWrapBox>();Actions->SetInnerSlotPadding(FVector2D(8,8));Add(Hero,Actions);auto* BattleSize=WidgetTree->ConstructWidget<USizeBox>();BattleSize->SetWidthOverride(292);BattleSize->SetHeightOverride(64);BattleSize->SetContent(Button(TEXT("ENTER BATTLE"),[this](){GetWorld()->GetSubsystem<URiftMatchSubsystem>()->StartMatch();bDev=false;HandIndex=-1;LastPhase.Empty();LastSurge=1;Navigate(TEXT("Battle"));},true));Actions->AddChildToWrapBox(BattleSize);auto* TrainingSize=WidgetTree->ConstructWidget<USizeBox>();TrainingSize->SetWidthOverride(260);TrainingSize->SetHeightOverride(64);TrainingSize->SetContent(Button(TEXT("TRAINING / DEVELOPER LAB"),[this](){GetWorld()->GetSubsystem<URiftMatchSubsystem>()->StartMatch(true);bDev=true;HandIndex=-1;LastPhase.Empty();LastSurge=1;Navigate(TEXT("Battle"));}));Actions->AddChildToWrapBox(TrainingSize);auto* HeroRow=WidgetTree->ConstructWidget<UHorizontalBox>();Add(HeroRow,Hero,true,0);auto* HeroArt=WidgetTree->ConstructWidget<UHorizontalBox>();auto* Lead=Illustration(TEXT("ironclad"),130);Lead->SetRenderTransformAngle(-8);Add(HeroArt,Lead,false,10);auto* Partner=Illustration(TEXT("rambeast"),130);Partner->SetRenderTransformAngle(7);Add(HeroArt,Partner,false,10);auto* ArtSize=WidgetTree->ConstructWidget<USizeBox>();ArtSize->SetWidthOverride(330);ArtSize->SetContent(HeroArt);Add(HeroRow,ArtSize,false,16);Add(Body,Panel(HeroRow,FMargin(24,20),FLinearColor(.027f,.065f,.12f,1)),6);
    auto* Main=WidgetTree->ConstructWidget<UWrapBox>();Main->SetInnerSlotPadding(FVector2D(32,24));Main->SetHorizontalAlignment(HAlign_Center);Add(Body,Main,12);
    auto* DeckPanel=WidgetTree->ConstructWidget<UVerticalBox>();auto* DeckSize=WidgetTree->ConstructWidget<USizeBox>();DeckSize->SetWidthOverride(688);DeckSize->SetContent(Panel(DeckPanel));Main->AddChildToWrapBox(DeckSize);
    FString Preset=TEXT("ACTIVE LOADOUT");for(const auto& Entry:P->Presets)if(Entry.Id==P->ActivePreset)Preset+=TEXT(" · ")+Entry.Name;
    auto* DeckHeading=Row(DeckPanel);Add(DeckHeading,Text(Preset,16,Brass),true);Add(DeckHeading,Button(TEXT("EDIT"),[this](){Navigate(TEXT("Loadout"));}));
    const auto DeckIds=P->ActiveDeck();for(int32 First=0;First<DeckIds.Num();First+=4){auto* DeckRow=Row(DeckPanel);for(int32 I=First;I<FMath::Min(First+4,DeckIds.Num());++I){FString Id=DeckIds[I];Add(DeckRow,CardButton(Id,132,[this,Id](){DetailCard=Id;Navigate(TEXT("CardDetail"));}),false,8);}}
    auto* Readout=WidgetTree->ConstructWidget<UVerticalBox>();auto* ReadoutSize=WidgetTree->ConstructWidget<USizeBox>();ReadoutSize->SetWidthOverride(360);ReadoutSize->SetContent(Panel(Readout,FMargin(22)));Main->AddChildToWrapBox(ReadoutSize);
    std::vector<std::string> Values;for(auto Id:DeckIds)Values.push_back(TCHAR_TO_UTF8(*Id));const auto Analysis=rift::AnalyzeDeck(Values);Add(Readout,Text(TEXT("DECK READOUT"),16,Brass));TArray<FString> LeadingArchetypes;for(int32 I=0;I<FMath::Min(2,int32(Analysis.archetypes.size()));++I)LeadingArchetypes.Add(FS(Analysis.archetypes[I]));Add(Readout,Text(FString::Join(LeadingArchetypes,TEXT(" / ")),21));
    Add(Readout,Text(FString::Printf(TEXT("%.2f average Aether"),Analysis.averageCost),19,Cyan));Add(Readout,Text(FString::Printf(TEXT("%d %s   ·   %d %s\n%d air answers   ·   %d win conditions"),Analysis.spells,Analysis.spells==1?TEXT("spell"):TEXT("spells"),Analysis.buildings,Analysis.buildings==1?TEXT("building"):TEXT("buildings"),Analysis.antiAir,Analysis.winConditions),15,Muted));
    Add(Readout,Text(TEXT("BEST AT"),14,Brass));for(int32 I=0;I<FMath::Min(2,int32(Analysis.strengths.size()));++I)Add(Readout,Text(FS(Analysis.strengths[I]),15),4);if(Analysis.strengths.empty())Add(Readout,Text(TEXT("Inspect card roles to plan your opening."),15,Muted),4);
    Add(Readout,Text(TEXT("WATCH FOR"),14,Brass));for(int32 I=0;I<FMath::Min(2,int32(Analysis.weaknesses.size()));++I)Add(Readout,Text(FS(Analysis.weaknesses[I]),15),4);if(Analysis.weaknesses.empty())Add(Readout,Text(TEXT("No immediate deck coverage gaps."),15,Muted),4);
    Add(Readout,Button(TEXT("FULL LOADOUT ANALYSIS"),[this](){Navigate(TEXT("Loadout"));}));
    Add(Body,Button(TEXT("NEW TO THE ARENA? OPEN THE FIELD MANUAL"),[this](){Navigate(TEXT("Help"));}));
    if(GetWorld()->GetSubsystem<URiftMatchSubsystem>()->IsActive())Add(Body,Button(TEXT("RETURN TO CURRENT BATTLE"),[this](){Navigate(TEXT("Battle"));}));
}
void URiftUIWidget::Profile()
{
    auto* P=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();Add(Body,Text(TEXT("YOUR ARENA RECORD"),27),3);Add(Body,Text(TEXT("Your decks, settings and recorded matches stay with this local profile."),15,Muted),5);
    auto* Stats=WidgetTree->ConstructWidget<UWrapBox>();Stats->SetInnerSlotPadding(FVector2D(12,12));for(const auto& Entry:TArray<TPair<FString,int32>>{{TEXT("WINS"),P->Wins},{TEXT("LOSSES"),P->Losses},{TEXT("DRAWS"),P->Draws},{TEXT("CROWNS"),P->Crowns},{TEXT("MATCHES"),P->Matches}}){auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();Add(Column,Text(FString::FromInt(Entry.Value),28,Entry.Key==TEXT("CROWNS")?Brass:Cyan),2);Add(Column,Text(Entry.Key,11,Muted),2);auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(166);Size->SetContent(Panel(Column));Stats->AddChildToWrapBox(Size);}Add(Body,Stats,8);
    auto* Identity=WidgetTree->ConstructWidget<UVerticalBox>();Add(Identity,Text(TEXT("PLAYER NAME"),16,Brass),3);NameInput=Edit(P->Username,TEXT("Player name"));Add(Identity,NameInput);Add(Identity,Button(TEXT("SAVE NAME"),[this,P](){Say(P->SetName(NameInput->GetText().ToString())?TEXT("Profile saved."):P->LastError);},true));Add(Body,Panel(Identity),8);
    Add(Body,Button(bProfileDetailsExpanded?TEXT("HIDE SAVE DETAILS"):TEXT("SAVE LOCATION & PLAYER ID"),[this](){const FString Pending=NameInput->GetText().ToString();bProfileDetailsExpanded=!bProfileDetailsExpanded;Navigate(TEXT("Profile"));NameInput->SetText(FText::FromString(Pending));}));if(bProfileDetailsExpanded){Add(Body,Text(TEXT("Player ID: ")+P->PlayerId,13,Muted));Add(Body,Text(TEXT("Save location: ")+URiftProfileSubsystem::SaveRoot(),13,Muted));}
}
void URiftUIWidget::SelectPreset(int32 Index){auto* P=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();if(!P->Presets.IsValidIndex(Index))return;PresetIndex=Index;Draft=P->Presets[Index].Cards;DraftName=P->Presets[Index].Name;bDraftLoaded=true;Navigate(TEXT("Loadout"));}
void URiftUIWidget::ToggleDraft(const FString& Id){if(PresetName)DraftName=PresetName->GetText().ToString();if(Draft.Contains(Id))Draft.Remove(Id);else if(Draft.Num()<8)Draft.Add(Id);else{Say(TEXT("Remove a card before adding another. Decks use eight distinct cards."));return;}Navigate(TEXT("Loadout"));}
void URiftUIWidget::Settings()
{
    auto* P=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();Add(Body,Text(TEXT("DISPLAY & PERFORMANCE"),18,Brass));
    auto* Modes=Row();auto* Mode=Combo({TEXT("Fullscreen"),TEXT("Borderless"),TEXT("Windowed")},TEXT(""));Mode->SetSelectedIndex(FMath::Clamp(P->Settings.WindowMode,0,2));Add(Modes,Text(TEXT("Window mode"),15));Add(Modes,Mode,true);
    auto* Resolution=Combo({TEXT("1280x720"),TEXT("1600x900"),TEXT("1920x1080"),TEXT("1920x1200"),TEXT("2560x1440"),TEXT("2560x1600"),TEXT("3440x1440"),TEXT("3840x2160")},FString::Printf(TEXT("%dx%d"),P->Settings.Width,P->Settings.Height));Add(Modes,Resolution,true);
    auto* VSync=Combo({TEXT("VSync ON"),TEXT("VSync OFF")},P->Settings.VSync?TEXT("VSync ON"):TEXT("VSync OFF"));Add(Modes,VSync);auto* Cap=Combo({TEXT("30"),TEXT("60"),TEXT("90"),TEXT("120"),TEXT("144"),TEXT("165"),TEXT("240"),TEXT("0")},FString::FromInt(P->Settings.FrameCap));Add(Modes,Text(TEXT("FPS cap"),14));Add(Modes,Cap);
    auto* Presets=Row();for(int32 I=0;I<4;++I){FString Label=TArray<FString>{TEXT("LOW"),TEXT("MEDIUM"),TEXT("HIGH"),TEXT("ULTRA")}[I];Add(Presets,Button(Label,[this,P,I](){P->Settings.Quality=I;P->Settings.AA=I;P->Settings.Shadows=I;P->Settings.Effects=I;P->Settings.Textures=I;P->Settings.Post=I;P->Settings.ViewDistance=I;P->ApplySettings();P->Save();Navigate(TEXT("Settings"));},P->Settings.Quality==I),true);}
    TArray<TPair<FString,UComboBoxString*>> QualityChoices;for(auto Pair:TArray<TPair<FString,int32>>{{TEXT("AA"),P->Settings.AA},{TEXT("Shadows"),P->Settings.Shadows},{TEXT("Effects"),P->Settings.Effects},{TEXT("Textures"),P->Settings.Textures},{TEXT("Post-processing"),P->Settings.Post},{TEXT("View distance"),P->Settings.ViewDistance}}){auto* R=Row();Add(R,Text(Pair.Key,15),true);auto* C=Combo({TEXT("Low"),TEXT("Medium"),TEXT("High"),TEXT("Ultra")},TEXT(""));C->SetSelectedIndex(FMath::Clamp(Pair.Value,0,3));Add(R,C,true);QualityChoices.Add({Pair.Key,C});}
    Add(Body,Text(TEXT("AUDIO & INTERACTION"),18,Brass));
    auto SliderSetting=[&](const FString& Label,float Value,float Min,float Max,TFunction<void(float)> Set){auto* R=Row();Add(R,Text(Label,15),true);auto* W=WidgetTree->ConstructWidget<URiftValueSlider>();W->SetMinValue(Min);W->SetMaxValue(Max);W->SetStepSize(.01f);W->SetValue(Value);W->SetSliderBarColor(FLinearColor(.09f,.19f,.32f,1));W->SetSliderHandleColor(Brass);const bool Percent=Max==1;auto Format=[Percent](float V){return Percent?FString::Printf(TEXT("%.0f%%"),V*100):FString::Printf(TEXT("%.2f×"),V);};auto* Readout=Text(Format(Value),14,Brass);Readout->SetJustification(ETextJustify::Right);W->Bind([Readout,Set=MoveTemp(Set),Format](float V){Set(V);Readout->SetText(FText::FromString(Format(V)));});Add(R,W,true);auto* NumberSize=WidgetTree->ConstructWidget<USizeBox>();NumberSize->SetWidthOverride(74);NumberSize->SetContent(Readout);Add(R,NumberSize,false,12);};
    SliderSetting(TEXT("Master volume"),P->Settings.MasterVolume,0,1,[P](float V){P->Settings.MasterVolume=V;});SliderSetting(TEXT("Music"),P->Settings.MusicVolume,0,1,[P](float V){P->Settings.MusicVolume=V;});SliderSetting(TEXT("Sound effects"),P->Settings.SFXVolume,0,1,[P](float V){P->Settings.SFXVolume=V;});SliderSetting(TEXT("UI sound"),P->Settings.UIVolume,0,1,[P](float V){P->Settings.UIVolume=V;});SliderSetting(TEXT("UI scale"),P->Settings.UIScale,.7,1.4,[P](float V){P->Settings.UIScale=V;});SliderSetting(TEXT("Camera interaction speed"),P->Settings.CameraSpeed,.5,2,[P](float V){P->Settings.CameraSpeed=V;});
    auto* Controls=Row();auto* Drag=Combo({TEXT("Click + drag deployment"),TEXT("Click deployment")},P->Settings.DragDeploy?TEXT("Click + drag deployment"):TEXT("Click deployment"));Add(Controls,Drag,true);auto* Confirm=Combo({TEXT("Direct deployment"),TEXT("Confirm deployment")},P->Settings.ConfirmDeploy?TEXT("Confirm deployment"):TEXT("Direct deployment"));Add(Controls,Confirm,true);
    Add(Body,Button(TEXT("APPLY & SAVE SETTINGS"),[this,P,Mode,Resolution,VSync,Cap,QualityChoices,Drag,Confirm](){P->Settings.WindowMode=Mode->GetSelectedIndex();FString Width,Height;if(Resolution->GetSelectedOption().Split(TEXT("x"),&Width,&Height)){P->Settings.Width=FCString::Atoi(*Width);P->Settings.Height=FCString::Atoi(*Height);}P->Settings.VSync=VSync->GetSelectedIndex()==0;P->Settings.FrameCap=FCString::Atoi(*Cap->GetSelectedOption());int32* Fields[]={&P->Settings.AA,&P->Settings.Shadows,&P->Settings.Effects,&P->Settings.Textures,&P->Settings.Post,&P->Settings.ViewDistance};for(int32 I=0;I<QualityChoices.Num();++I)*Fields[I]=QualityChoices[I].Value->GetSelectedIndex();P->Settings.DragDeploy=Drag->GetSelectedIndex()==0;P->Settings.ConfirmDeploy=Confirm->GetSelectedIndex()==1;P->ApplySettings();FString Result=P->Save()?TEXT("Settings applied and saved."):P->LastError;Navigate(TEXT("Settings"));Say(Result);},true));
}
FString URiftUIWidget::SelectedCardId()const
{
    if(!CanAcceptBattleInput())return TEXT("");
    if(bSpawnArmed&&DevCard)return DevCard->GetSelectedOption();auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();auto* S=M->ViewState();return S&&HandIndex>=0&&HandIndex<4?FS(S->hands[0][HandIndex]):TEXT("");
}
int32 URiftUIWidget::PlacementTeam()const{return bSpawnArmed&&DevTeam&&DevTeam->GetSelectedIndex()==1?1:0;}
void URiftUIWidget::WorldClicked(FVector2D Tile)
{
    if(!CanAcceptBattleInput())return;if(bSpawnArmed){bool Result=GetWorld()->GetSubsystem<URiftDeveloperSubsystem>()->SpawnCard(DevTeam&&DevTeam->GetSelectedIndex()==1?1:0,SelectedCardId(),Tile);Say(Result?TEXT("Developer deployment recorded."):TEXT("That tile is blocked."));return;}if(HandIndex<0)return;bool Result=GetWorld()->GetSubsystem<URiftMatchSubsystem>()->PlayCard(HandIndex,Tile);if(Result){HandIndex=-1;Say(TEXT(""));}else Say(TEXT("Cannot deploy here. Check Aether, occupied tiles and lane pocket rules."));
}
void URiftUIWidget::ShowJSON(const TSharedPtr<FJsonObject>& Object,UVerticalBox* Target,const FString& Prefix)
{
    if(!Object)return;
    UVerticalBox* Box=Target?Target:Body.Get();
    TArray<FString> Keys;
    for(const auto& Pair:Object->Values)Keys.Add(FString(*Pair.Key));
    Keys.Sort();
    for(const auto& Key:Keys)
    {
        auto Value=Object->TryGetField(Key);if(!Value.IsValid())continue;
        if(Value->Type==EJson::Number)Add(Box,Text(Pretty(Prefix+Key)+FString::Printf(TEXT("   %.3f"),Value->AsNumber()),14),2);
        else if(Value->Type==EJson::String)Add(Box,Text(Pretty(Prefix+Key)+TEXT("   ")+Value->AsString(),14),2);
        else if(Value->Type==EJson::Boolean)Add(Box,Text(Pretty(Prefix+Key)+(Value->AsBool()?TEXT("   YES"):TEXT("   NO")),14),2);
        else if(Value->Type==EJson::Null)Add(Box,Text(Pretty(Prefix+Key)+TEXT("   unavailable"),14,Muted),2);
        else if(Value->Type==EJson::Object&&Prefix.Len()<60)ShowJSON(Value->AsObject(),Box,Prefix+Key+TEXT(" · "));
        else if(Value->Type==EJson::Array&&Prefix.Len()<60)
        {
            int32 Index=0;for(const auto& Entry:Value->AsArray())
            {
                ++Index;const FString ItemPrefix=Prefix+Key+FString::Printf(TEXT(" %d · "),Index);
                if(Entry->Type==EJson::Object)ShowJSON(Entry->AsObject(),Box,ItemPrefix);
                else if(Entry->Type==EJson::String)Add(Box,Text(Pretty(ItemPrefix)+Entry->AsString(),14),2);
            }
        }
    }
}
void URiftUIWidget::CaptureFilters()
{
    if(MetaSearch)Search=MetaSearch->GetText().ToString();if(MetaMin)MinSample=FMath::Max(0,FCString::Atoi(*MetaMin->GetText().ToString()));if(MetaType)TypeFilter=MetaType->GetSelectedOption();if(MetaTrait)TraitFilter=MetaTrait->GetSelectedOption();if(MetaCost)CostFilter=MetaCost->GetSelectedOption();if(MetaStyle)StyleFilter=MetaStyle->GetSelectedOption();if(MetaArchetype)ArchetypeFilter=MetaArchetype->GetSelectedOption();
}
TArray<TSharedPtr<FJsonObject>> URiftUIWidget::FilterRows(TArray<TSharedPtr<FJsonObject>> Values,bool CardFilter)const
{
    Values.RemoveAll([this,CardFilter](const TSharedPtr<FJsonObject>& Value)
    {
        if(!Value)return true;
        if(!Search.IsEmpty()&&!JS(Value,TEXT("name")).Contains(Search,ESearchCase::IgnoreCase)&&!JS(Value,TEXT("id")).Contains(Search,ESearchCase::IgnoreCase))return true;
        if(JN(Value,TEXT("cleanN"),DBL_MAX)<MinSample)return true;
        const FString Id=JS(Value,TEXT("id"));
        if(CardFilter)
        {
            const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*Id));return !Card||!CardMatches(*Card,CostFilter,TypeFilter,TraitFilter);
        }
        FString Left,Right;
        if(Id.Split(TEXT("+"),&Left,&Right)||Id.Split(TEXT("/"),&Left,&Right))
        {
            const auto* A=rift::FindCard(TCHAR_TO_UTF8(*Left));const auto* B=rift::FindCard(TCHAR_TO_UTF8(*Right));
            return (!A||!CardMatches(*A,CostFilter,TypeFilter,TraitFilter))&&(!B||!CardMatches(*B,CostFilter,TypeFilter,TraitFilter));
        }
        return false;
    });
    Values.Sort([this](const auto& A,const auto& B){if(SortKey==TEXT("name")||SortKey==TEXT("availability")){const FString VA=JS(A,*SortKey),VB=JS(B,*SortKey);return bSortDescending?VA>VB:VA<VB;}double VA=JN(A,*SortKey,-DBL_MAX),VB=JN(B,*SortKey,-DBL_MAX);return VA==VB?JS(A,TEXT("name"))<JS(B,TEXT("name")):bSortDescending?VA>VB:VA<VB;});return Values;
}
void URiftUIWidget::Meta()
{
    auto* M=GetGameInstance()->GetSubsystem<URiftMetaSimulationSubsystem>();MetaStatus=Text(M->Status(),16,Cyan);Add(Body,MetaStatus);auto* Control=Row();for(int32 Rate:{100,250,500,0})Add(Control,Button(Rate?FString::Printf(TEXT("%d / MIN"),Rate):TEXT("MAX SAFE"),[this,M,Rate](){M->SetRate(Rate);M->Start();bMetaPaused=false;Say(TEXT("Background simulation pauses automatically during live battles."));}));Add(Control,Button(TEXT("PAUSE / RESUME"),[this,M](){bMetaPaused=!bMetaPaused;M->Pause(bMetaPaused);}));Add(Control,Button(TEXT("ARCHIVE & RESET"),[this,M](){M->Reset();DatasetName.Empty();MetaDetail.Reset();Navigate(TEXT("Meta"));}));
    Add(Control,Button(TEXT("STATS GUIDE"),[this](){Navigate(TEXT("MetaGuide"));},Tab==TEXT("Stats Guide")));
    auto* Tabs=Row();for(FString Name:{TEXT("Cards"),TEXT("Matchups"),TEXT("Synergy"),TEXT("Archetypes"),TEXT("AI Styles"),TEXT("Alerts"),TEXT("Trends"),TEXT("Patches"),TEXT("Validation")})Add(Tabs,Button(Name.ToUpper(),[this,Name](){CaptureFilters();Tab=Name;MetaDetail.Reset();Navigate(TEXT("Meta"));},Tab==Name),true);
    if(Tab==TEXT("Stats Guide")){Rows=WidgetTree->ConstructWidget<UVerticalBox>();Add(Body,Rows,2);MetaRows();return;}
    auto* Filters=Row();MetaSearch=Edit(Search,TEXT("Card search"));Add(Filters,MetaSearch,true);MetaType=Combo({TEXT("all"),TEXT("Troop"),TEXT("Building"),TEXT("Spell")},TypeFilter);Add(Filters,MetaType);MetaCost=Combo({TEXT("all"),TEXT("2"),TEXT("3"),TEXT("4"),TEXT("5"),TEXT("6")},CostFilter);Add(Filters,MetaCost);MetaTrait=Combo({TEXT("all"),TEXT("Air"),TEXT("Ground"),TEXT("Anti-air"),TEXT("Win condition"),TEXT("Swarm"),TEXT("Splash")},TraitFilter);Add(Filters,MetaTrait);MetaMin=Edit(FString::FromInt(MinSample),TEXT("Minimum N"));auto* NS=WidgetTree->ConstructWidget<USizeBox>();NS->SetWidthOverride(90);NS->SetContent(MetaMin);Add(Filters,NS);
    auto* Slice=Row();MetaStyle=Combo(Styles,StyleFilter);Add(Slice,Text(TEXT("AI style"),13));Add(Slice,MetaStyle,true);MetaArchetype=Combo(Archetypes,ArchetypeFilter);Add(Slice,Text(TEXT("Archetype"),13));Add(Slice,MetaArchetype,true);auto Names=M->DatasetNames();if(Names.IsEmpty())Names.Add(TEXT("Current"));if(!Names.Contains(DatasetName))DatasetName=Names.Contains(M->SelectedDatasetName())?M->SelectedDatasetName():Names.Last();MetaVersion=Combo(Names,DatasetName);Add(Slice,MetaVersion,true);Add(Slice,Button(TEXT("APPLY FILTERS / VERSION"),[this,M](){CaptureFilters();FString Chosen=MetaVersion->GetSelectedOption();if(Chosen!=DatasetName){if(M->SelectDataset(Chosen)){DatasetName=Chosen;bMetaPaused=true;}else Say(M->LastError.IsEmpty()?TEXT("Cannot open that dataset."):M->LastError);}MetaDetail.Reset();MetaRows();}));
    auto* Exports=Row();MetaSubject=Combo({TEXT("cards"),TEXT("matchups"),TEXT("synergy"),TEXT("archetypes"),TEXT("styles"),TEXT("patch")},TEXT("cards"));Add(Exports,MetaSubject);MetaFormat=Combo({TEXT("CSV"),TEXT("JSON")},TEXT("CSV"));Add(Exports,MetaFormat);FileInput=Edit(TEXT("Meta/export.csv"),TEXT("Export path"));Add(Exports,FileInput,true);Add(Exports,Button(TEXT("EXPORT FILTERED DATA"),[this](){ExportMeta();}));
    if(Tab==TEXT("Patches")){auto* Compare=Row();Names.Append({TEXT("V12.8"),TEXT("V12.10"),TEXT("V14"),TEXT("V15")});MetaBaseline=Combo(Names,TEXT("V15"));Add(Compare,Text(TEXT("Baseline dataset"),13));Add(Compare,MetaBaseline,true);Add(Compare,Button(TEXT("COMPARE"),[this](){MetaRows();}));}
    if(Tab==TEXT("Trends")){auto* Trend=Row();MetaWindow=Combo({TEXT("100"),TEXT("500"),TEXT("1000"),TEXT("5000"),TEXT("10000"),TEXT("all")},TEXT("all"));Add(Trend,Text(TEXT("Recent cumulative checkpoints"),13));Add(Trend,MetaWindow);MetaMetric=Combo({TEXT("adjustedWinRate"),TEXT("pickRate"),TEXT("damagePerAether")},TEXT("adjustedWinRate"));Add(Trend,MetaMetric);TArray<FString> IDs;for(auto C:rift::Cards())IDs.Add(FS(C.id));auto* Card=Combo(IDs,DetailCard.IsEmpty()?TEXT("ironclad"):DetailCard);Add(Trend,Card);Add(Trend,Button(TEXT("DRAW TREND"),[this,Card](){DetailCard=Card->GetSelectedOption();MetaRows();}));}
    Rows=WidgetTree->ConstructWidget<UVerticalBox>();Add(Body,Rows,2);MetaRows();
}
void URiftUIWidget::MetaRows()
{
    if(!Rows)return;Rows->ClearChildren();auto* M=GetGameInstance()->GetSubsystem<URiftMetaSimulationSubsystem>();CaptureFilters();if(MetaDetail){Add(Rows,Button(TEXT("BACK TO TABLE"),[this](){MetaDetail.Reset();MetaRows();}));ShowJSON(MetaDetail,Rows);return;}
    if(Tab==TEXT("Stats Guide"))
    {
        Add(Rows,Text(TEXT("READING THE META LAB"),21,Brass));
        Add(Rows,Text(TEXT("Results come from completed matches in the same gameplay engine as your battles. They describe the sampled AI decks and styles. A card's deck win rate measures association with winning, rather than its individual contribution."),15,Muted),6);
        auto Explain=[this](const TCHAR* Label,const TCHAR* Meaning){Add(Rows,Text(Label,16,Cyan),6);Add(Rows,Text(Meaning,14,Ivory),2);};
        Explain(TEXT("MATCHES, DECK APPEARANCES & CLEAN N"),TEXT("Each match supplies two deck observations. A card appearance means it was in a deck, even if never played. Card Clean N excludes matches where both decks contain that card. Pair Clean N excludes matches where both decks contain the entire pair. Styles and archetypes count deck observations; a deck can belong to several archetypes."));
        Explain(TEXT("PICK %, RAW % & ADJUSTED %"),TEXT("Pick % is appearances divided by deck observations in the selected slice. Raw % is (wins + half of draws) / Clean N. Adjusted % adds 24 prior observations at 50%: 100 × (win score + 12) / (N + 24). Small samples are therefore pulled toward 50%. An unsampled adjusted value of 50% is a prior, not measured performance."));
        Explain(TEXT("95% CI & UNAVAILABLE VALUES"),TEXT("The Wilson interval describes uncertainty around the raw win score. Wider intervals mean less precision. Deck observations share matches and cards, so this is a descriptive interval, not proof that a card causes wins. Unavailable means the necessary denominator is zero or the historical data is missing. Minimum N hides rows below your sample threshold."));
        Explain(TEXT("DAMAGE / AETHER & TOWER DAMAGE / AETHER"),TEXT("Damage / Aether divides actual troop, building and tower damage by Aether paid for that card. Tower Damage / Aether counts tower damage only. Damage is actual HP lost; overkill is reported separately. Neither ratio divides by the card's printed cost without accounting for casts."));
        Explain(TEXT("CARD DETAIL METRICS"),TEXT("Per-game values divide totals by deck appearances, including appearances with no play. Per-cast values divide by paid plays. Lifetime averages divide by spawned members; survival counts members alive at the end. Opening-hand play rate uses opening-hand opportunities, connection rate uses paid plays, and average placement uses recorded tile coordinates. Slow/stun uptime uses affected exposure; zone occupancy is average affected units per active zone second."));
        Explain(TEXT("MATCHUPS & SYNERGY"),TEXT("Matchup cells show rule-based mechanical coverage edges, not simulated duel win rates. Synergy cells show an observed pair win-rate delta against the mean adjusted rates of its two cards. A 48-observation prior shrinks that delta toward zero. Positive values are associations, not guaranteed combos; select a cell to see its sample size, interval and mechanical synergy."));
        Explain(TEXT("STYLES, ARCHETYPES, TRENDS & PATCHES"),TEXT("AI style and archetype filters select the observed deck slice; card/type/cost filters select visible rows. Archetypes overlap, so their pick percentages can sum above 100%. Trends are cumulative checkpoints every 25 matches, not rolling-window rates. Patch comparisons need a real stored baseline; missing baselines remain unavailable. Alerts require at least 100 clean samples and a confidence interval outside 50%."));
        Explain(TEXT("CURRENT RULES & HISTORY"),TEXT("The dataset fingerprint identifies its arena, routing, card and spell rules. Older datasets remain readable as history and are never appended to a different ruleset. Background simulation pauses during live battles. Validation reports completed matches and real economy checks; exports use the same table calculations and selected filters."));
        return;
    }
    if(Tab==TEXT("Validation"))
    {
        Add(Rows,Text(TEXT("ACTUAL NATIVE SIMULATION VALIDATION"),19,Brass));
        Add(Rows,Button(TEXT("RUN 10,000 MATCHES"),[this,M](){M->Start(10000);bMetaPaused=false;Say(TEXT("10,000 real authoritative matches requested. Status reports measured completion."));},true));
        Add(Rows,Text(TEXT("The authoritative native engine records real match telemetry, Aether and clean sample N. Live battles pause this worker. Reports refresh as matches complete."),14,Muted));
        auto Summary=MakeShared<FJsonObject>();auto Data=M->Dataset();
        if(Data)for(const auto& Pair:Data->Values)
        {
            const FString Key(*Pair.Key);
            if(Key==TEXT("buckets")||Key==TEXT("checkpoints")||Key==TEXT("cardSnapshot"))continue;
            Summary->SetField(Key,Pair.Value);
        }
        const TArray<TSharedPtr<FJsonValue>>* Definitions=nullptr;Summary->SetNumberField(TEXT("immutableCardDefinitions"),Data&&Data->TryGetArrayField(TEXT("cardSnapshot"),Definitions)?Definitions->Num():0);Summary->SetNumberField(TEXT("recordedCheckpoints"),M->Checkpoints().Num());ShowJSON(Summary,Rows);
        Add(Rows,Text(TEXT("CLEAN CARD SAMPLES · MIRRORS EXCLUDED"),17,Brass));
        for(const auto& Card:M->CardRows())Add(Rows,Text(JS(Card,TEXT("name"))+TEXT(" · N ")+Metric(Card,TEXT("cleanN"))+TEXT(" · deck appearances ")+Metric(Card,TEXT("appearances"))+TEXT(" · mirror exclusions ")+Metric(Card,TEXT("mirrorExclusions")),14),2);
        return;
    }
    if(Tab==TEXT("Trends")){auto Points=M->Checkpoints();if(Points.IsEmpty()){Add(Rows,Text(TEXT("No cumulative checkpoints recorded yet. Run simulation to collect a trend."),17));return;}FString Id=DetailCard.IsEmpty()?TEXT("ironclad"):DetailCard;auto* Chart=CreateWidget<URiftChartWidget>(GetOwningPlayer());Chart->Caption=Pretty(MetaMetric?MetaMetric->GetSelectedOption():TEXT("adjustedWinRate"))+TEXT(" · cumulative whole-dataset estimate · simulated matches");double Latest=JN(Points.Last(),TEXT("games"));int32 Window=MetaWindow?FCString::Atoi(*MetaWindow->GetSelectedOption()):0;for(auto Point:Points){double N=JN(Point,TEXT("games"));if(Window&&N<Latest-Window)continue;const TArray<TSharedPtr<FJsonValue>>* Cards;if(Point->TryGetArrayField(TEXT("cards"),Cards))for(auto V:*Cards){auto C=V->AsObject();if(JS(C,TEXT("id"))==Id)Chart->SeriesA.Add(FVector2D(N,JN(C,MetaMetric?*MetaMetric->GetSelectedOption():TEXT("adjustedWinRate"))));}}auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetHeightOverride(260);Size->SetContent(Chart);Add(Rows,Text(Id,18,Cyan));Add(Rows,Size);Add(Rows,Text(TEXT("These are cumulative estimates at recorded checkpoints, not rolling-window win rates. Missing historical samples are not fabricated."),14,Muted));return;}
    if(Tab==TEXT("Matchups")||Tab==TEXT("Synergy"))
    {
        bool Empirical=Tab==TEXT("Synergy");auto Values=Empirical?M->SynergyRows(StyleFilter,ArchetypeFilter):M->MatchupRows();TMap<FString,TSharedPtr<FJsonObject>> Cells;for(auto V:Values)Cells.Add(JS(V,TEXT("id")),V);Add(Rows,Text(Empirical?TEXT("Observed joint-deck win-score delta against paired card baseline. Mirrors excluded, shrinkage applied; click for N and confidence."):TEXT("Mechanical coverage edge. These values are not isolated-duel win rates. Click for targeting and interaction reasons."),14,Muted));auto* Horizontal=WidgetTree->ConstructWidget<UScrollBox>();Horizontal->SetOrientation(Orient_Horizontal);auto* Matrix=WidgetTree->ConstructWidget<UVerticalBox>();Horizontal->AddChild(Matrix);Add(Rows,Horizontal);
        TArray<const rift::Card*> VisibleCards;for(auto CardRow:FilterRows(M->CardRows(StyleFilter,ArchetypeFilter),true)){if(const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*JS(CardRow,TEXT("id")))))VisibleCards.Add(Card);}if(VisibleCards.IsEmpty()){Add(Rows,Text(TEXT("No cards meet the current filters."),17));return;}auto* Header=Row(Matrix);auto* Corner=WidgetTree->ConstructWidget<USizeBox>();Corner->SetWidthOverride(140);Add(Header,Corner);for(const auto* Card:VisibleCards){const auto& C=*Card;auto* Cell=WidgetTree->ConstructWidget<USizeBox>();Cell->SetWidthOverride(53);auto* Label=Text(FS(C.name).Left(3),12,Muted);Label->SetToolTipText(FText::FromString(FS(C.name)));Cell->SetContent(Label);Add(Header,Cell);}
        for(const auto* RowCard:VisibleCards){const auto& A=*RowCard;auto* Line=Row(Matrix);auto* Label=WidgetTree->ConstructWidget<USizeBox>();Label->SetWidthOverride(140);Label->SetContent(Text(FS(A.name),13));Add(Line,Label);for(const auto* ColumnCard:VisibleCards){const auto& B=*ColumnCard;FString IA=FS(A.id),IB=FS(B.id),Key=Empirical?(IA<IB?IA+TEXT("+")+IB:IB+TEXT("+")+IA):IA+TEXT("/")+IB;auto Value=Cells.FindRef(Key);double Score=JN(Value,Empirical?TEXT("delta"):TEXT("edge"));bool Available=IA!=IB&&Value&&(!Empirical||JN(Value,TEXT("cleanN"))>=FMath::Max(1,MinSample));auto* ButtonCell=Button(Available?FString::Printf(TEXT("%+.1f"),Score):TEXT("—"),[this,Value](){if(Value){MetaDetail=Value;MetaRows();}});ButtonCell->SetBackgroundColor(Available?(Score>0?FLinearColor(.10f,.27f,.30f,1):Score<0?FLinearColor(.28f,.22f,.14f,1):Stone):Stone);auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(53);Size->SetHeightOverride(32);Size->SetContent(ButtonCell);Add(Line,Size);}}return;
    }
    if(Tab==TEXT("AI Styles"))
    {
        Add(Rows,Text(TEXT("CARD PERFORMANCE BY AI PERSONALITY"),18,Brass));
        Add(Rows,Text(TEXT("Each personality has its own clean samples and confidence interval. Select a cell for its complete card report. Select a column to sort."),14,Muted));
        TMap<FString,TMap<FString,TSharedPtr<FJsonObject>>> StyleCards;
        for(int32 I=1;I<Styles.Num();++I)for(auto Card:M->CardRows(Styles[I],ArchetypeFilter))StyleCards.FindOrAdd(Styles[I]).Add(JS(Card,TEXT("id")),Card);
        auto CardList=FilterRows(M->CardRows(TEXT("all"),ArchetypeFilter),true);
        if(SortKey.StartsWith(TEXT("style:")))
        {
            const FString SelectedStyle=SortKey.Mid(6);const auto& Slice=StyleCards.FindOrAdd(SelectedStyle);
            CardList.Sort([this,&Slice](const auto& A,const auto& B){double VA=JN(Slice.FindRef(JS(A,TEXT("id"))),TEXT("adjustedWinRate")),VB=JN(Slice.FindRef(JS(B,TEXT("id"))),TEXT("adjustedWinRate"));return VA==VB?JS(A,TEXT("name"))<JS(B,TEXT("name")):bSortDescending?VA>VB:VA<VB;});
        }
        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Scroll->SetOrientation(Orient_Horizontal);auto* Matrix=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Matrix);Add(Rows,Scroll);
        auto* Header=Row(Matrix);auto* NameSpace=WidgetTree->ConstructWidget<USizeBox>();NameSpace->SetWidthOverride(150);NameSpace->SetContent(Text(TEXT("CARD"),14,Muted));Add(Header,NameSpace);
        for(int32 I=1;I<Styles.Num();++I){FString StyleName=Styles[I];auto* Cell=WidgetTree->ConstructWidget<USizeBox>();Cell->SetWidthOverride(120);Cell->SetContent(Button(Pretty(StyleName.Replace(TEXT("_"),TEXT(" "))),[this,StyleName](){FString Key=TEXT("style:")+StyleName;bSortDescending=SortKey==Key?!bSortDescending:true;SortKey=Key;MetaRows();}));Add(Header,Cell);}
        for(auto Card:CardList)
        {
            auto* Line=Row(Matrix);auto* NameCell=WidgetTree->ConstructWidget<USizeBox>();NameCell->SetWidthOverride(150);NameCell->SetContent(Text(JS(Card,TEXT("name")),14));Add(Line,NameCell);
            for(int32 I=1;I<Styles.Num();++I){auto Sample=StyleCards.FindOrAdd(Styles[I]).FindRef(JS(Card,TEXT("id")));FString Value=JN(Sample,TEXT("cleanN"))>=FMath::Max(1,MinSample)?Metric(Sample,TEXT("adjustedWinRate"))+TEXT("% · N ")+Metric(Sample,TEXT("cleanN")):TEXT("No clean samples");auto* Cell=WidgetTree->ConstructWidget<USizeBox>();Cell->SetWidthOverride(120);Cell->SetContent(Button(Value,[this,Sample](){if(Sample){MetaDetail=Sample;MetaRows();}}));Add(Line,Cell);}
        }
        Add(Rows,Text(TEXT("PERSONALITY TOTALS"),17,Brass));for(auto StyleRow:M->StyleRows(ArchetypeFilter))Add(Rows,Button(JS(StyleRow,TEXT("name"))+TEXT(" · Adjusted ")+Metric(StyleRow,TEXT("adjustedWinRate"))+TEXT("% · N ")+Metric(StyleRow,TEXT("cleanN"))+TEXT(" · Duration ")+Metric(StyleRow,TEXT("averageDuration"))+TEXT("s · Leaked Aether/game ")+Metric(StyleRow,TEXT("leakedPerGame")),[this,StyleRow](){MetaDetail=StyleRow;MetaRows();}));return;
    }
    auto Values=Tab==TEXT("Archetypes")?M->ArchetypeRows(StyleFilter):Tab==TEXT("Alerts")?M->AlertRows(StyleFilter,ArchetypeFilter):Tab==TEXT("Patches")?M->PatchRows(MetaBaseline?MetaBaseline->GetSelectedOption():TEXT("V15")):M->CardRows(StyleFilter,ArchetypeFilter);Values=FilterRows(Values,Tab==TEXT("Cards")||Tab==TEXT("Patches")||Tab==TEXT("Alerts"));
    if(Values.IsEmpty())Add(Rows,Text(Tab==TEXT("Alerts")?TEXT("No card currently meets the confidence and sample-size requirements for an alert."):TEXT("No recorded rows meet these filters."),17));
    if(Tab==TEXT("Alerts"))
    {
        for(const auto& Value:Values)Add(Rows,Button(JS(Value,TEXT("severity"))+TEXT(" · ")+JS(Value,TEXT("name"))+TEXT(" · ")+JS(Value,TEXT("direction"))+TEXT(" · ")+JS(Value,TEXT("alert")),[this,Value](){MetaDetail=Value;MetaRows();}));return;
    }
    const TArray<FString> Columns=Tab==TEXT("Patches")?TArray<FString>{TEXT("name"),TEXT("availability"),TEXT("delta"),TEXT("baselineN"),TEXT("cleanN"),TEXT("adjustedWinRate"),TEXT("ciLow")}:Tab==TEXT("Archetypes")?TArray<FString>{TEXT("name"),TEXT("pickRate"),TEXT("adjustedWinRate"),TEXT("ciLow"),TEXT("cleanN"),TEXT("averageCrowns"),TEXT("averageDuration")}:TArray<FString>{TEXT("name"),TEXT("pickRate"),TEXT("adjustedWinRate"),TEXT("rawWinRate"),TEXT("ciLow"),TEXT("cleanN"),TEXT("damagePerAether"),TEXT("towerDamagePerAether")};
    const TMap<FString,FString> Labels={{TEXT("name"),Tab==TEXT("Archetypes")?TEXT("ARCHETYPE"):TEXT("CARD")},{TEXT("availability"),TEXT("BASELINE STATUS")},{TEXT("pickRate"),TEXT("PICK %")},{TEXT("adjustedWinRate"),TEXT("ADJUSTED %")},{TEXT("rawWinRate"),TEXT("RAW %")},{TEXT("ciLow"),TEXT("95% CI")},{TEXT("cleanN"),TEXT("CLEAN N")},{TEXT("baselineN"),TEXT("BASELINE N")},{TEXT("delta"),TEXT("DELTA")},{TEXT("averageCrowns"),TEXT("CROWNS / GAME")},{TEXT("averageDuration"),TEXT("DURATION (s)")},{TEXT("damagePerAether"),TEXT("DAMAGE / AETHER")},{TEXT("towerDamagePerAether"),TEXT("TOWER / AETHER")}};
    auto* Header=WidgetTree->ConstructWidget<UHorizontalBox>();Add(Rows,Header,0);
    for(const auto& Key:Columns)
    {
        const FString Label=Labels.FindChecked(Key)+(SortKey==Key?(bSortDescending?TEXT(" ↓"):TEXT(" ↑")):TEXT(""));auto* SortButton=Button(Label,[this,Key](){bSortDescending=SortKey==Key?!bSortDescending:true;SortKey=Key;MetaRows();},SortKey==Key);SortButton->SetToolTipText(FText::FromString(Key==TEXT("ciLow")?TEXT("Sort by the lower confidence bound. Values show both bounds of the 95% interval."):TEXT("Sort by ")+Pretty(Key)));Add(Header,SortButton,true,2);
    }
    for(const auto& Value:Values)
    {
        auto* RowButton=Button(TEXT(""),[this,Value](){MetaDetail=Value;MetaRows();});auto RowStyle=RowButton->GetStyle();RowStyle.SetNormalPadding(FMargin(0));RowStyle.SetPressedPadding(FMargin(0));RowButton->SetStyle(RowStyle);auto* Cells=WidgetTree->ConstructWidget<UHorizontalBox>();RowButton->SetContent(Cells);if(auto* ContentSlot=Cast<UButtonSlot>(Cells->Slot)){ContentSlot->SetHorizontalAlignment(HAlign_Fill);ContentSlot->SetVerticalAlignment(VAlign_Fill);ContentSlot->SetPadding(FMargin(0));}
        for(const auto& Key:Columns)
        {
            FString Label=Key==TEXT("name")||Key==TEXT("availability")?JS(Value,*Key):Metric(Value,*Key);
            if(Key==TEXT("ciLow")){const FString Low=Metric(Value,TEXT("ciLow")),High=Metric(Value,TEXT("ciHigh"));Label=JN(Value,TEXT("cleanN"))>0&&Low!=TEXT("unavailable")&&High!=TEXT("unavailable")?Low+TEXT("–")+High:TEXT("unavailable");}
            auto* Cell=WidgetTree->ConstructWidget<UBorder>();Cell->SetBrushColor(FLinearColor(0,0,0,0));Cell->SetPadding(FMargin(10,9));Cell->SetClipping(EWidgetClipping::ClipToBounds);auto* ValueText=Text(Label,13,Key==TEXT("name")?Ivory:Key==TEXT("ciLow")?Muted:Cyan);ValueText->SetAutoWrapText(false);ValueText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);ValueText->SetJustification(Key==TEXT("name")||Key==TEXT("availability")?ETextJustify::Left:ETextJustify::Center);ValueText->SetToolTipText(FText::FromString(Label));Cell->SetContent(ValueText);Add(Cells,Cell,true,2);
        }
        Add(Rows,RowButton,0);
    }
}
void URiftUIWidget::ExportMeta()
{
    CaptureFilters();auto* M=GetGameInstance()->GetSubsystem<URiftMetaSimulationSubsystem>();FString Subject=MetaSubject->GetSelectedOption();auto Values=Subject==TEXT("matchups")?M->MatchupRows():Subject==TEXT("synergy")?M->SynergyRows(StyleFilter,ArchetypeFilter):Subject==TEXT("archetypes")?M->ArchetypeRows(StyleFilter):Subject==TEXT("styles")?M->StyleRows(ArchetypeFilter):Subject==TEXT("patch")?M->PatchRows(MetaBaseline?MetaBaseline->GetSelectedOption():TEXT("V15")):M->CardRows(StyleFilter,ArchetypeFilter);Values=FilterRows(Values,Subject==TEXT("cards")||Subject==TEXT("patch"));if(Subject==TEXT("matchups")||Subject==TEXT("synergy")){TSet<FString> VisibleIds;for(auto Card:FilterRows(M->CardRows(StyleFilter,ArchetypeFilter),true))VisibleIds.Add(JS(Card,TEXT("id")));Values.RemoveAll([&VisibleIds](const auto& Pair){FString A,B;const FString Id=JS(Pair,TEXT("id"));return !(Id.Split(TEXT("+"),&A,&B)||Id.Split(TEXT("/"),&A,&B))||!VisibleIds.Contains(A)||!VisibleIds.Contains(B);});}FString Output;bool CSV=MetaFormat->GetSelectedOption()==TEXT("CSV");if(CSV){TArray<FString> Keys;for(auto V:Values)for(auto Pair:V->Values)Keys.AddUnique(FString(*Pair.Key));Keys.Sort();auto Quote=[](FString V){return TEXT("\"")+V.Replace(TEXT("\""),TEXT("\"\""))+TEXT("\"");};for(int32 I=0;I<Keys.Num();++I)Output+=(I?TEXT(","):TEXT(""))+Quote(Keys[I]);Output+=TEXT("\r\n");for(auto V:Values){for(int32 I=0;I<Keys.Num();++I){FString Cell;if(auto Found=V->TryGetField(Keys[I])){if(Found->Type==EJson::String)Cell=Found->AsString();else if(Found->Type==EJson::Number)Cell=FString::SanitizeFloat(Found->AsNumber());else if(Found->Type==EJson::Boolean)Cell=Found->AsBool()?TEXT("true"):TEXT("false");else if(Found->Type==EJson::Object||Found->Type==EJson::Array)FJsonSerializer::Serialize(Found,TEXT(""),TJsonWriterFactory<>::Create(&Cell));}Output+=(I?TEXT(","):TEXT(""))+Quote(Cell);}Output+=TEXT("\r\n");}}
    else{auto Object=MakeShared<FJsonObject>();Object->SetStringField(TEXT("subject"),Subject);Object->SetStringField(TEXT("style"),StyleFilter);Object->SetStringField(TEXT("archetype"),ArchetypeFilter);Object->SetStringField(TEXT("search"),Search);Object->SetStringField(TEXT("type"),TypeFilter);Object->SetStringField(TEXT("trait"),TraitFilter);Object->SetStringField(TEXT("cost"),CostFilter);Object->SetNumberField(TEXT("minSample"),MinSample);Object->SetStringField(TEXT("fingerprint"),JS(M->Dataset(),TEXT("fingerprint")));Object->SetStringField(TEXT("model"),JS(M->Dataset(),TEXT("model")));Object->SetStringField(TEXT("version"),JS(M->Dataset(),TEXT("version")));TArray<TSharedPtr<FJsonValue>> RowsJSON;for(auto V:Values)RowsJSON.Add(MakeShared<FJsonValueObject>(V));Object->SetArrayField(TEXT("rows"),RowsJSON);FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Output));}FString Filename=FileInput->GetText().ToString();if(FPaths::IsRelative(Filename))Filename=FPaths::Combine(URiftProfileSubsystem::SaveRoot(),Filename);if(FPaths::GetExtension(Filename).IsEmpty())Filename+=CSV?TEXT(".csv"):TEXT(".json");FString Error;Say(URiftProfileSubsystem::AtomicWrite(Filename,Output,Error)?FString::Printf(TEXT("Exported %d filtered rows to %s"),Values.Num(),*Filename):Error);
}
void URiftUIWidget::PatchNotes()
{
    Add(Body,Text(TEXT("ARENA & LANE ROUTING · 1.3.4"),25,Brass));
    Add(Body,Text(TEXT("The arena gains one tile on every edge. All six Crown Towers move one tile toward their own rear. Ground troops choose the bridge on their current side and advance toward the Core when that lane's Guard Tower falls, while still responding to nearby troops and buildings."),16));
    Add(Body,Text(TEXT("Meta Lab now includes a Stats Guide explaining samples, win rates, confidence ranges, damage efficiency, synergy and mechanical matchups. New simulations use the expanded arena and routing fingerprint; older datasets remain readable as history."),16));
    Add(Body,Text(TEXT("TOWER PATHING FIX · 1.3.3"),25,Brass));
    Add(Body,Text(TEXT("Ground troops placed near towers now start in clear space and route safely around tower edges. Units keep moving toward battle instead of getting stuck behind a Core or Guard Tower."),16));
    Add(Body,Text(TEXT("AUDIO FIX · 1.3.2"),25,Brass));
    Add(Body,Text(TEXT("The continuous river sound no longer plays in menus or battles. Music, interface sounds and combat effects keep their existing volume controls."),16));
    Add(Body,Text(TEXT("CARD HOVER FIX · 1.3.1"),25,Brass));
    Add(Body,Text(TEXT("Card stats stay visible while hovering over your hand. Tooltips now update when a hand slot changes cards, instead of restarting on every HUD refresh."),16));
    Add(Body,Text(TEXT("MODELS, CARD ART & CONTROLS · 1.3.0"),25,Brass));
    Add(Body,Text(TEXT("Revised characters have fitted equipment, cleaner weapon grips and clearer silhouettes. All fourteen portraits are rendered from the upgraded in-game models, with closer framing for faces and held equipment."),16));
    Add(Body,Text(TEXT("The interface uses the bundled Barlow Semi Condensed font. Drag a card to a legal arena position and release to deploy. Return it to the hand, release on an invalid position or UI panel, or cancel to keep the card and Aether."),16));
    Add(Body,Text(TEXT("SPELL TIMING UPDATE · 1.2.1"),25,Brass));
    Add(Body,Text(TEXT("Meteor Shards falls for 0.75 seconds before the initial hit and damage zone begin. Bullet Burst travels for 0.30 seconds before its hit. Lead moving enemies: both spells strike the chosen area using enemy positions at impact."),16));
    Add(Body,Text(TEXT("Aether is spent and the hand cycles when you cast. Pause, battle speed and replay seeking preserve the impact deadline. Nova Flask stays instant; damage, cost, radius and Meteor's five damage ticks are retained."),16));
    Add(Body,Text(TEXT("MODEL & MOTION UPDATE · 1.2.0"),22,Brass),12);
    Add(Body,Text(TEXT("Every collection and hand card now shows its actual arena model. Units have larger silhouettes, clearer faces and equipment, smoother movement and turns, stronger attack anticipation and recovery, and animated flight and defeat."),16));
    Add(Body,Text(TEXT("Archers, mages, flying attackers and Crown Towers fire visible projectiles with trails and impact effects. Tower weapons aim while their foundations stay planted. The battlefield camera and compact health bars keep the larger models clear of the battle interface."),16));
    Add(Body,Text(TEXT("The four hand cards, next card and Aether meter sit together behind your Core Tower. Health bars remain hidden until a unit or tower first takes damage, then stay visible if it heals. Recorded replays preserve the same behavior."),16));
    Add(Body,Text(TEXT("All fourteen cards, game modes, training tools, replay features and numerical battle rules are preserved."),15,Cyan));
    Add(Body,Text(TEXT("PRESENTATION UPDATE · 1.1.0"),22,Brass),12);
    Add(Body,Text(TEXT("A rebuilt battle interface with framed hand cards, an illustrated next card, a ten-segment Aether meter, separate crown scores and clear phase announcements. Escape opens a pause menu with settings, restart, loadout and the field manual."),16));
    Add(Body,Text(TEXT("The lobby, collection, card details, replay viewer and match reports share a new interface. Training tools now show live speed, AI and tower information, and their overlays work in the Windows release. Combat, spell, UI and music audio has been remade and mixed to keep crowded battles clear."),16));
    Add(Body,Text(TEXT("The arena graphics, fourteen cards and numerical battle rules are preserved."),15,Cyan));
    Add(Body,Text(TEXT("PREVIOUS RELEASE"),16,Muted),12);
    Add(Body,Text(TEXT("NATIVE WINDOWS REBUILD · 1.0.0"),22,Brass));Add(Body,Text(TEXT("The authoritative C++ simulation powers rendered battle, background Meta Lab and native regression tests. Current card identities and all locked numerical balance values are retained. Ground bridge routes, directional acquisition, dormant Core Towers, Crown Tower hard locks, lane pockets, Aether phases, overtime and the live HP tiebreaker remain part of the rules."),17));Add(Body,Text(TEXT("This release adds native profile migration, five persistent loadout presets, event-recorded replay playback and post-match telemetry, seven AI personalities, a fully connected Developer Lab, settings, and a separate verified Windows launcher/updater. Browser saves remain preserved; native datasets have their own model fingerprint. Historical patch comparisons remain unavailable when exact observations were not recorded."),17));Add(Body,Text(TEXT("Controls: select a hand card or press 1–4, then choose a legal tile center. Right click cancels selection. Developer controls record their actions in replays. Debug overlays start off. Card, deck and Meta reports are available from the menu without covering the battlefield."),17,Muted));
}
void URiftUIWidget::NativeTick(const FGeometry& Geometry,float Delta)
{
    Super::NativeTick(Geometry,Delta);if(Page==TEXT("Battle"))UpdateBattleHUD(Delta);RefreshClock+=Delta;MetaClock+=Delta;if(RefreshClock<.10f)return;RefreshClock=0;
    auto* R=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();
    if(Page==TEXT("Replays")&&bReplayListSaving!=R->IsSaving())
    {
        const FString TypedPath=FileInput?FileInput->GetText().ToString():FString();Navigate(TEXT("Replays"));if(FileInput&&!TypedPath.IsEmpty())FileInput->SetText(FText::FromString(TypedPath));
    }
    if(Page==TEXT("ReplayView")&&R->IsPlaying())UpdateReplayHUD();
    if(Page==TEXT("Meta")){auto* MetaEngine=GetGameInstance()->GetSubsystem<URiftMetaSimulationSubsystem>();if(MetaStatus)MetaStatus->SetText(FText::FromString(MetaEngine->Status()));if(MetaClock>=3){MetaClock=0;if(!MetaDetail&&Tab!=TEXT("Stats Guide"))MetaRows();}}
}
