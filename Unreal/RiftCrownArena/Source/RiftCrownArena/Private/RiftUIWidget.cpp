#include "RiftUIWidget.h"
#include "RiftMatchSubsystem.h"
#include "RiftGameMode.h"
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
#include "Components/ScrollBox.h"
#include "Components/ProgressBar.h"
#include "Components/Image.h"
#include "Components/WrapBox.h"
#include "Engine/Texture2D.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Rendering/DrawElements.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    const FLinearColor Ivory(.9f,.88f,.81f,1),Muted(.55f,.66f,.68f,1),Brass(.75f,.60f,.34f,1),Cyan(.24f,.73f,.86f,1),Stone(.06f,.105f,.12f,.98f);
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
        return FString::SanitizeFloat(Number,2);
    }
}
void URiftActionButton::Bind(TFunction<void()> Callback,bool OnPress){Action=MoveTemp(Callback);OnReleased.AddDynamic(this,&URiftActionButton::InvokeRelease);if(OnPress)OnPressed.AddDynamic(this,&URiftActionButton::Invoke);else OnClicked.AddDynamic(this,&URiftActionButton::Invoke);}
void URiftActionButton::Invoke(){if(GetWorld()&&GetWorld()->GetGameInstance())if(auto* Audio=GetWorld()->GetGameInstance()->GetSubsystem<URiftBattleAudioSubsystem>())Audio->PlayUI(TEXT("ui_click"));if(Action)Action();}
void URiftActionButton::InvokeRelease(){if(ReleaseAction)ReleaseAction();}
void URiftValueSlider::Bind(TFunction<void(float)> Callback){Action=MoveTemp(Callback);OnValueChanged.AddDynamic(this,&URiftValueSlider::Invoke);}
void URiftValueSlider::Invoke(float SliderValue){if(Action)Action(SliderValue);}
void URiftChartWidget::NativeConstruct(){Super::NativeConstruct();if(!WidgetTree->RootWidget){auto* B=WidgetTree->ConstructWidget<UBorder>();B->SetBrushColor(FLinearColor(.035,.065,.075,1));WidgetTree->RootWidget=B;}}
int32 URiftChartWidget::NativePaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool Enabled)const
{
    Layer=Super::NativePaint(Args,G,Clip,Elements,Layer,Style,Enabled);const FVector2D Size=G.GetLocalSize();const FVector2D Origin(48,12),Area(FMath::Max(1.0,Size.X-62),FMath::Max(1.0,Size.Y-40));
    double MinX=0,MaxX=1,MinY=0,MaxY=1;for(const auto& Series:{SeriesA,SeriesB})for(auto P:Series){MinX=FMath::Min(MinX,P.X);MaxX=FMath::Max(MaxX,P.X);MinY=FMath::Min(MinY,P.Y);MaxY=FMath::Max(MaxY,P.Y);}
    for(int32 I=0;I<5;++I){float Y=Origin.Y+Area.Y*I/4;TArray<FVector2D> Line={FVector2D(Origin.X,Y),FVector2D(Origin.X+Area.X,Y)};FSlateDrawElement::MakeLines(Elements,Layer+1,G.ToPaintGeometry(),Line,ESlateDrawEffect::None,FLinearColor(.18,.26,.29,.7),true,1);FSlateDrawElement::MakeText(Elements,Layer+2,G.ToPaintGeometry(FVector2D(45,18),FSlateLayoutTransform(FVector2D(0,Y-6))),FString::Printf(TEXT("%.0f"),MaxY-(MaxY-MinY)*I/4),FCoreStyle::GetDefaultFontStyle("Regular",10),ESlateDrawEffect::None,Muted);}
    auto Draw=[&](const TArray<FVector2D>& Values,FLinearColor Color){TArray<FVector2D> Points;for(auto V:Values)Points.Add(Origin+FVector2D((V.X-MinX)/FMath::Max(1.0,MaxX-MinX)*Area.X,Area.Y-(V.Y-MinY)/FMath::Max(1.0,MaxY-MinY)*Area.Y));if(Points.Num()>1)FSlateDrawElement::MakeLines(Elements,Layer+3,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,2);};Draw(SeriesA,Cyan);Draw(SeriesB,Brass);
    FSlateDrawElement::MakeText(Elements,Layer+4,G.ToPaintGeometry(FVector2D(Size.X,18),FSlateLayoutTransform(FVector2D(48,Size.Y-20))),Caption,FCoreStyle::GetDefaultFontStyle("Regular",11),ESlateDrawEffect::None,Muted);return Layer+4;
}

UTextBlock* URiftUIWidget::Text(const FString& Value,int32 Size,FLinearColor Color){auto* W=WidgetTree->ConstructWidget<UTextBlock>();W->SetText(FText::FromString(Value));float Scale=1;if(GetGameInstance())if(auto* ProfileData=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>())Scale=ProfileData->Settings.UIScale;W->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",FMath::Max(9,FMath::RoundToInt(Size*Scale))));W->SetColorAndOpacity(Color);W->SetAutoWrapText(true);return W;}
URiftActionButton* URiftUIWidget::Button(const FString& Label,TFunction<void()> Action,bool Accent)
{
    auto* W=WidgetTree->ConstructWidget<URiftActionButton>();
    FButtonStyle ControlStyle;ControlStyle.SetNormal(FSlateColorBrush(FLinearColor::White));ControlStyle.SetHovered(FSlateColorBrush(FLinearColor(.88,.94,.96,1)));ControlStyle.SetPressed(FSlateColorBrush(FLinearColor(.66,.75,.78,1)));ControlStyle.SetDisabled(FSlateColorBrush(FLinearColor(.34,.39,.40,1)));ControlStyle.SetNormalPadding(FMargin(12,8));ControlStyle.SetPressedPadding(FMargin(12,9,12,7));W->SetStyle(ControlStyle);
    auto* LabelText=Text(Label,14,Accent?Stone:Ivory);LabelText->SetJustification(ETextJustify::Center);W->SetContent(LabelText);W->SetBackgroundColor(Accent?Brass:FLinearColor(.115,.175,.19,1));W->Bind(MoveTemp(Action));return W;
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
    const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*CardId));auto* W=Button(TEXT(""),MoveTemp(Action));W->SetBackgroundColor(Selected?FLinearColor(.17,.30,.31,1):FLinearColor(.055,.085,.09,1));
    auto CardStyle=W->GetStyle();CardStyle.SetNormalPadding(FMargin(4));CardStyle.SetPressedPadding(FMargin(4,5,4,3));W->SetStyle(CardStyle);
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();Add(Column,Illustration(CardId,Width),0);
    auto* Name=Text(Card?FS(Card->name):CardId,14);Name->SetJustification(ETextJustify::Center);Add(Column,Name,4);
    auto* Cost=Text(Card?FString::Printf(TEXT("%d AETHER%s"),Card->cost,Selected?TEXT(" · IN DECK"):TEXT("")):TEXT(""),11,Selected?Brass:Cyan);Cost->SetJustification(ETextJustify::Center);Add(Column,Cost,3);
    auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(Width);Size->SetContent(Column);W->SetContent(Size);return W;
}
void URiftUIWidget::Add(UVerticalBox* Box,UWidget* W,float Spacing){auto* PanelSlot=Box->AddChildToVerticalBox(W);PanelSlot->SetPadding(FMargin(Spacing));}
void URiftUIWidget::Add(UHorizontalBox* Box,UWidget* W,bool Fill,float Spacing){auto* PanelSlot=Box->AddChildToHorizontalBox(W);PanelSlot->SetPadding(FMargin(Spacing));if(Fill)PanelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));}
UHorizontalBox* URiftUIWidget::Row(UVerticalBox* Target){auto* W=WidgetTree->ConstructWidget<UHorizontalBox>();Add(Target?Target:Body.Get(),W,2);return W;}
UEditableTextBox* URiftUIWidget::Edit(const FString& Value,const FString& Hint){auto* W=WidgetTree->ConstructWidget<UEditableTextBox>();W->SetText(FText::FromString(Value));W->SetHintText(FText::FromString(Hint));auto EditStyle=W->GetWidgetStyle();EditStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Regular",14));W->SetWidgetStyle(EditStyle);return W;}
UComboBoxString* URiftUIWidget::Combo(const TArray<FString>& Values,const FString& Selected){auto* W=WidgetTree->ConstructWidget<UComboBoxString>();for(auto V:Values)W->AddOption(V);if(Values.Contains(Selected))W->SetSelectedOption(Selected);else if(Values.Num())W->SetSelectedIndex(0);return W;}
void URiftUIWidget::Say(const FString& Message){Notice=Message;if(NoticeText)NoticeText->SetText(FText::FromString(Message));if(!Message.IsEmpty())if(auto* Audio=GetGameInstance()->GetSubsystem<URiftBattleAudioSubsystem>()){FString Lower=Message.ToLower();if(Lower.Contains(TEXT("cannot"))||Lower.Contains(TEXT("invalid"))||Lower.Contains(TEXT("error"))||Lower.Contains(TEXT("blocked"))||Lower.Contains(TEXT("failed")))Audio->PlayUI(TEXT("ui_error"));else if(Lower.Contains(TEXT("saved"))||Lower.Contains(TEXT("exported"))||Lower.Contains(TEXT("imported"))||Lower.Contains(TEXT("applied"))||Lower.Contains(TEXT("recorded")))Audio->PlayUI(TEXT("ui_save"));}}
void URiftUIWidget::NativeConstruct(){Super::NativeConstruct();Navigate(TEXT("Home"));}
void URiftUIWidget::Navigate(const FString& Destination)
{
    if(Destination==TEXT("Home")){auto* R=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();if(R->IsPlaying())R->CloseReplay();auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();if(M->IsActive())M->LeaveMatch();HandIndex=-1;bSpawnArmed=false;}
    Page=Destination;Root=WidgetTree->ConstructWidget<UCanvasPanel>();Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);WidgetTree->RootWidget=Root;
    TimerText=nullptr;ScoreText=nullptr;AetherText=nullptr;NextText=nullptr;AetherBar=nullptr;DevReadout=nullptr;ReplayPosition=nullptr;ReplaySeek=nullptr;MetaStatus=nullptr;Rows=nullptr;NoticeText=nullptr;HandText.Reset();HandButtons.Reset();HandImages.Reset();HandArtIds.Reset();
    SetRenderTransform(FWidgetTransform(FVector2D::ZeroVector,FVector2D(1,1), FVector2D::ZeroVector,0));
    if(Page==TEXT("Battle")){Battle();return;}if(Page==TEXT("ReplayView")){ReplayView();return;}
    Shell(Page.ToUpper());
    if(Page==TEXT("Home"))Home();else if(Page==TEXT("Profile"))Profile();else if(Page==TEXT("Loadout"))Loadout();else if(Page==TEXT("Cards"))Cards();else if(Page==TEXT("CardDetail"))CardDetail();else if(Page==TEXT("Settings"))Settings();else if(Page==TEXT("Replays"))Replays();else if(Page==TEXT("Analysis"))MatchAnalysis();else if(Page==TEXT("Meta"))Meta();else PatchNotes();
}
void URiftUIWidget::Shell(const FString& Title)
{
    auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetBrushColor(Stone);Frame->SetPadding(FMargin(22));auto* PanelSlot=Root->AddChildToCanvas(Frame);PanelSlot->SetAnchors(FAnchors(0,0,1,1));PanelSlot->SetOffsets(FMargin(30,24,30,24));
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Frame->SetContent(Scroll);Body=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Body);
    auto* Heading=Row();Add(Heading,Text(TEXT("RIFT CROWN ARENA"),25,Brass),true);Add(Heading,Text(Title,19,Cyan));
    auto* Nav=Row();for(FString Name:{TEXT("Home"),TEXT("Profile"),TEXT("Loadout"),TEXT("Cards"),TEXT("Meta"),TEXT("Replays"),TEXT("Patch Notes"),TEXT("Settings")}){bool Active=Page==Name||(Page==TEXT("CardDetail")&&Name==TEXT("Cards"));auto* TabButton=Button(Name,[this,Name](){Navigate(Name);});TabButton->SetBackgroundColor(Active?FLinearColor(.15,.24,.25,1):FLinearColor(.06,.105,.12,0));TabButton->SetContent(Text(Name,14,Active?Brass:Muted));Add(Nav,TabButton,false,2);}
    auto* Divider=WidgetTree->ConstructWidget<UBorder>();Divider->SetBrushColor(FLinearColor(.36,.32,.22,.8));auto* DividerSize=WidgetTree->ConstructWidget<USizeBox>();DividerSize->SetHeightOverride(1);DividerSize->SetContent(Divider);Add(Body,DividerSize,4);
    if(GetWorld()->GetSubsystem<URiftMatchSubsystem>()->IsActive())Add(Body,Button(TEXT("RETURN TO BATTLE"),[this](){Navigate(TEXT("Battle"));}));
    NoticeText=Text(Notice,13,Cyan);Add(Body,NoticeText,4);
}
void URiftUIWidget::Home()
{
    auto* P=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();Add(Body,Text(FString::Printf(TEXT("%s   ·   %d wins   ·   %d crowns   ·   %d matches"),*P->Username,P->Wins,P->Crowns,P->Matches),19));
    auto* Actions=Row();auto* BattleSize=WidgetTree->ConstructWidget<USizeBox>();BattleSize->SetWidthOverride(260);BattleSize->SetHeightOverride(62);BattleSize->SetContent(Button(TEXT("ENTER BATTLE"),[this](){GetWorld()->GetSubsystem<URiftMatchSubsystem>()->StartMatch();HandIndex=-1;Navigate(TEXT("Battle"));},true));Add(Actions,BattleSize);Add(Actions,Button(TEXT("Training / Developer Lab"),[this](){GetWorld()->GetSubsystem<URiftMatchSubsystem>()->StartMatch(true);bDev=true;HandIndex=-1;Navigate(TEXT("Battle"));}));
    auto* DeckHeading=Row();Add(DeckHeading,Text(TEXT("ACTIVE LOADOUT"),16,Brass),true);Add(DeckHeading,Button(TEXT("EDIT LOADOUT"),[this](){Navigate(TEXT("Loadout"));}));auto* Deck=WidgetTree->ConstructWidget<UWrapBox>();Deck->SetInnerSlotPadding(FVector2D(10));Add(Body,Deck);for(const auto& Id:P->ActiveDeck())Deck->AddChildToWrapBox(CardButton(Id,100,[this,Id](){DetailCard=Id;Navigate(TEXT("CardDetail"));}));
    std::vector<std::string> DeckIds;for(auto Id:P->ActiveDeck())DeckIds.push_back(TCHAR_TO_UTF8(*Id));auto A=rift::AnalyzeDeck(DeckIds);FString Report=FString::Printf(TEXT("Average cost %.2f   ·   %d spells   ·   %d buildings   ·   %d air answers   ·   %d win conditions"),A.averageCost,A.spells,A.buildings,A.antiAir,A.winConditions);Add(Body,Text(Report,17));for(auto V:A.archetypes)Add(Body,Text(FS(V),15,Cyan));for(auto V:A.strengths)Add(Body,Text(TEXT("Strength: ")+FS(V),15));for(auto V:A.weaknesses)Add(Body,Text(TEXT("Watch: ")+FS(V),15,Brass));
    Add(Body,Text(FString::Printf(TEXT("Gold %d   ·   Gems %d   ·   Local player %s"),P->Gold,P->Gems,*P->PlayerId),14,Muted));
    if(GetWorld()->GetSubsystem<URiftMatchSubsystem>()->IsActive())Add(Body,Button(TEXT("RETURN TO CURRENT BATTLE"),[this](){Navigate(TEXT("Battle"));}));
}
void URiftUIWidget::Profile()
{
    auto* P=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();Add(Body,Text(TEXT("Your local profile"),22));NameInput=Edit(P->Username,TEXT("Player name"));Add(Body,NameInput);Add(Body,Button(TEXT("SAVE NAME"),[this,P](){Say(P->SetName(NameInput->GetText().ToString())?TEXT("Profile saved."):P->LastError);},true));
    Add(Body,Text(FString::Printf(TEXT("Player ID  %s\nWins  %d    Losses  %d    Draws  %d\nMatches  %d    Crowns  %d\nGold  %d    Gems  %d"),*P->PlayerId,P->Wins,P->Losses,P->Draws,P->Matches,P->Crowns,P->Gold,P->Gems),18));Add(Body,Text(TEXT("Saved under ")+URiftProfileSubsystem::SaveRoot(),13,Muted));
}
void URiftUIWidget::SelectPreset(int32 Index){auto* P=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();if(!P->Presets.IsValidIndex(Index))return;PresetIndex=Index;Draft=P->Presets[Index].Cards;DraftName=P->Presets[Index].Name;bDraftLoaded=true;Navigate(TEXT("Loadout"));}
void URiftUIWidget::ToggleDraft(const FString& Id){if(PresetName)DraftName=PresetName->GetText().ToString();if(Draft.Contains(Id))Draft.Remove(Id);else if(Draft.Num()<8)Draft.Add(Id);else{Say(TEXT("Remove a card before adding another. Decks use eight distinct cards."));return;}Navigate(TEXT("Loadout"));}
void URiftUIWidget::Loadout()
{
    auto* P=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();if(!bDraftLoaded){for(int32 I=0;I<P->Presets.Num();++I)if(P->Presets[I].Id==P->ActivePreset)PresetIndex=I;if(P->Presets.IsValidIndex(PresetIndex)){Draft=P->Presets[PresetIndex].Cards;DraftName=P->Presets[PresetIndex].Name;}bDraftLoaded=true;}
    auto* Presets=Row();for(int32 I=0;I<P->Presets.Num();++I)Add(Presets,Button(P->Presets[I].Name,[this,I](){SelectPreset(I);},I==PresetIndex),true);
    PresetName=Edit(DraftName);Add(Body,PresetName);Add(Body,Text(FString::Printf(TEXT("Selected cards %d / 8 — click to remove"),Draft.Num()),16,Cyan));
    auto* Slots=WidgetTree->ConstructWidget<UWrapBox>();Slots->SetInnerSlotPadding(FVector2D(8));Add(Body,Slots);for(FString Id:Draft)Slots->AddChildToWrapBox(CardButton(Id,76,[this,Id](){ToggleDraft(Id);},true));
    auto* Actions=Row();Add(Actions,Button(TEXT("SAVE & SELECT"),[this,P](){if(P->SaveDeck(PresetIndex,PresetName->GetText().ToString(),Draft)&&P->SelectPreset(PresetIndex))Say(TEXT("Deck saved and selected for the next battle."));else Say(P->LastError);},true));Add(Actions,Button(TEXT("TEST DECK"),[this,P](){if(!P->SaveDeck(PresetIndex,PresetName->GetText().ToString(),Draft)||!P->SelectPreset(PresetIndex)){Say(P->LastError);return;}GetWorld()->GetSubsystem<URiftMatchSubsystem>()->StartMatch(true);bDev=true;Navigate(TEXT("Battle"));}));Add(Actions,Button(TEXT("RESET DRAFT"),[this](){Draft=URiftProfileSubsystem::DefaultDeck();Navigate(TEXT("Loadout"));}));
    std::vector<std::string> Values;for(auto Id:Draft)Values.push_back(TCHAR_TO_UTF8(*Id));auto A=rift::AnalyzeDeck(Values);Add(Body,Text(FString::Printf(TEXT("Cost %.2f   ·   HP %.0f   ·   Deployment DPS %.1f   ·   Range %.2f\nAir answers %d / sustained %d   ·   Spells %d   ·   Buildings %d   ·   Win conditions %d   ·   Synergy %.1f"),A.averageCost,A.averageHP,A.deploymentDPS,A.averageRange,A.antiAir,A.sustainedAntiAir,A.spells,A.buildings,A.winConditions,A.synergy),16));for(auto V:A.archetypes)Add(Body,Text(TEXT("Archetype: ")+FS(V),15,Cyan));for(auto V:A.strengths)Add(Body,Text(FS(V),14));for(auto V:A.weaknesses)Add(Body,Text(FS(V),14,Brass));
    Add(Body,Text(TEXT("CARD COLLECTION"),17,Brass));auto* Pool=WidgetTree->ConstructWidget<UWrapBox>();Pool->SetInnerSlotPadding(FVector2D(10));Add(Body,Pool);for(const auto& C:rift::Cards()){FString Id=FS(C.id);Pool->AddChildToWrapBox(CardButton(Id,96,[this,Id](){ToggleDraft(Id);},Draft.Contains(Id)));}
    Add(Body,Text(TEXT("PAIR SYNERGY — mechanical compatibility, not measured win rate"),15,Muted));for(int32 I=0;I<Draft.Num();++I)for(int32 J=I+1;J<Draft.Num();++J){const auto* ACard=rift::FindCard(TCHAR_TO_UTF8(*Draft[I]));const auto* BCard=rift::FindCard(TCHAR_TO_UTF8(*Draft[J]));if(ACard&&BCard)Add(Body,Text(FString::Printf(TEXT("%s + %s   %.2f"),*FS(ACard->name),*FS(BCard->name),rift::PairSynergy(*ACard,*BCard)),13),2);}
}
void URiftUIWidget::Cards()
{
    auto* Pool=WidgetTree->ConstructWidget<UWrapBox>();Pool->SetInnerSlotPadding(FVector2D(12));Add(Body,Pool);
    for(const auto& C:rift::Cards())
    {
        FString Id=FS(C.id);Pool->AddChildToWrapBox(CardButton(Id,168,[this,Id](){DetailCard=Id;Navigate(TEXT("CardDetail"));}));
    }
}
void URiftUIWidget::CardDetail()
{
    const auto* C=rift::FindCard(TCHAR_TO_UTF8(*DetailCard));if(!C){Say(TEXT("Card not found."));return;}Add(Body,Text(FS(C->name),26,Brass));
    Add(Body,Text(FString::Printf(TEXT("Cost %d   ·   Units %d   ·   %s\nHP %.0f each   ·   Damage %.0f   ·   Hit speed %.2fs\nMove %.2f tiles/s   ·   Attack range %.2f   ·   Sight 8 front / 5 rear\nTargets %s   ·   Projectile %.1f tiles/s   ·   Splash radius %.2f"),C->cost,C->count,C->spell?TEXT("Spell"):C->building?TEXT("Defensive building"):C->flying?TEXT("Flying troop"):TEXT("Ground troop"),C->hp,C->damage,C->attackInterval,C->moveSpeed,C->range,C->structuresOnly?TEXT("Towers / Buildings"):C->canHitAir?TEXT("Ground + Air"):TEXT("Ground"),C->projectileSpeed,C->splash),18));
    if(C->spell)Add(Body,Text(FString::Printf(TEXT("Spell radius %.2f   ·   Structure damage %.0f   ·   Rounds %d\nInitial troop damage %.0f   ·   DoT %.0f / second for %.1fs"),C->spellRadius,C->towerDamage,C->rounds,C->damage,C->dotDamage,C->dotDuration),17));if(C->building)Add(Body,Text(FString::Printf(TEXT("Lifetime %.1fs   ·   Occupied footprint %.2f tiles"),C->lifetime,C->footprint),17));if(C->chargeDamage)Add(Body,Text(FString::Printf(TEXT("Charge damage %.0f"),C->chargeDamage),17));if(C->slowPct)Add(Body,Text(FString::Printf(TEXT("Every melee hit slows movement %.0f%% for %.1fs. Repeat hits refresh; slows do not stack."),C->slowPct*100,C->slowDuration),17));if(C->auraDamage)Add(Body,Text(FString::Printf(TEXT("Electric ring: %.0f damage every %.1fs, radius %.1f, stun %.1fs. Hits ground and air troops."),C->auraDamage,C->auraInterval,C->auraRadius,C->stunDuration),17));
    const auto Intelligence=rift::IntelligenceFor(C->id);Add(Body,Text(FS(Intelligence.role),19,Cyan));
    Add(Body,Text(TEXT("HOW TO USE THIS CARD"),17,Brass));for(const auto& Use:Intelligence.suggestedUses)Add(Body,Text(FS(Use),15));
    auto Relations=[this](const FString& Label,const std::vector<rift::CardRelation>& Entries){Add(Body,Text(Label,17,Brass));for(const auto& Entry:Entries){const auto* Other=rift::FindCard(Entry.id);FString Id=FS(Entry.id);Add(Body,Button((Other?FS(Other->name):Id)+TEXT(" · ")+FS(Entry.reason),[this,Id](){DetailCard=Id;Navigate(TEXT("CardDetail"));}));}};
    Relations(TEXT("STRONG AGAINST"),Intelligence.bestAgainst);Relations(TEXT("WEAK AGAINST"),Intelligence.weakAgainst);Relations(TEXT("BEST PARTNERS"),Intelligence.partners);
    const auto* Answer=rift::FindCard(Intelligence.bestDefensiveAnswer.id);const auto* Partner=rift::FindCard(Intelligence.bestOffensivePartner.id);
    Add(Body,Text(TEXT("Best defensive answer: ")+(Answer?FS(Answer->name):FS(Intelligence.bestDefensiveAnswer.id))+TEXT(" · ")+FS(Intelligence.bestDefensiveAnswer.reason),15));
    Add(Body,Text(TEXT("Best offensive partner: ")+(Partner?FS(Partner->name):FS(Intelligence.bestOffensivePartner.id))+TEXT(" · ")+FS(Intelligence.bestOffensivePartner.reason),15));
    Add(Body,Text(TEXT("FULL MECHANICAL MATCHUPS"),17,Brass));for(const auto& Other:rift::Cards())if(Other.id!=C->id)Add(Body,Text(FS(Other.name)+TEXT(" · ")+FS(rift::CounterReason(*C,Other)),14,Muted));
}
void URiftUIWidget::Settings()
{
    auto* P=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();Add(Body,Text(TEXT("DISPLAY & PERFORMANCE"),18,Brass));
    auto* Modes=Row();auto* Mode=Combo({TEXT("Fullscreen"),TEXT("Borderless"),TEXT("Windowed")},TEXT(""));Mode->SetSelectedIndex(FMath::Clamp(P->Settings.WindowMode,0,2));Add(Modes,Text(TEXT("Window mode"),15));Add(Modes,Mode,true);
    auto* Resolution=Combo({TEXT("1280x720"),TEXT("1600x900"),TEXT("1920x1080"),TEXT("1920x1200"),TEXT("2560x1440"),TEXT("2560x1600"),TEXT("3440x1440"),TEXT("3840x2160")},FString::Printf(TEXT("%dx%d"),P->Settings.Width,P->Settings.Height));Add(Modes,Resolution,true);
    auto* VSync=Combo({TEXT("VSync ON"),TEXT("VSync OFF")},P->Settings.VSync?TEXT("VSync ON"):TEXT("VSync OFF"));Add(Modes,VSync);auto* Cap=Combo({TEXT("30"),TEXT("60"),TEXT("90"),TEXT("120"),TEXT("144"),TEXT("165"),TEXT("240"),TEXT("0")},FString::FromInt(P->Settings.FrameCap));Add(Modes,Text(TEXT("FPS cap"),14));Add(Modes,Cap);
    auto* Presets=Row();for(int32 I=0;I<4;++I){FString Label=TArray<FString>{TEXT("LOW"),TEXT("MEDIUM"),TEXT("HIGH"),TEXT("ULTRA")}[I];Add(Presets,Button(Label,[this,P,I](){P->Settings.Quality=I;P->Settings.AA=I;P->Settings.Shadows=I;P->Settings.Effects=I;P->Settings.Textures=I;P->Settings.Post=I;P->Settings.ViewDistance=I;P->ApplySettings();P->Save();Navigate(TEXT("Settings"));},P->Settings.Quality==I),true);}
    TArray<TPair<FString,UComboBoxString*>> QualityChoices;for(auto Pair:TArray<TPair<FString,int32>>{{TEXT("AA"),P->Settings.AA},{TEXT("Shadows"),P->Settings.Shadows},{TEXT("Effects"),P->Settings.Effects},{TEXT("Textures"),P->Settings.Textures},{TEXT("Post-processing"),P->Settings.Post},{TEXT("View distance"),P->Settings.ViewDistance}}){auto* R=Row();Add(R,Text(Pair.Key,15),true);auto* C=Combo({TEXT("Low"),TEXT("Medium"),TEXT("High"),TEXT("Ultra")},TEXT(""));C->SetSelectedIndex(FMath::Clamp(Pair.Value,0,3));Add(R,C,true);QualityChoices.Add({Pair.Key,C});}
    Add(Body,Text(TEXT("AUDIO & INTERACTION"),18,Brass));
    auto SliderSetting=[&](const FString& Label,float Value,float Min,float Max,TFunction<void(float)> Set){auto* R=Row();Add(R,Text(Label,15),true);auto* W=WidgetTree->ConstructWidget<URiftValueSlider>();W->SetMinValue(Min);W->SetMaxValue(Max);W->SetValue(Value);W->Bind(MoveTemp(Set));Add(R,W,true);};
    SliderSetting(TEXT("Master volume"),P->Settings.MasterVolume,0,1,[P](float V){P->Settings.MasterVolume=V;});SliderSetting(TEXT("Music"),P->Settings.MusicVolume,0,1,[P](float V){P->Settings.MusicVolume=V;});SliderSetting(TEXT("Sound effects"),P->Settings.SFXVolume,0,1,[P](float V){P->Settings.SFXVolume=V;});SliderSetting(TEXT("UI sound"),P->Settings.UIVolume,0,1,[P](float V){P->Settings.UIVolume=V;});SliderSetting(TEXT("UI scale"),P->Settings.UIScale,.7,1.4,[P](float V){P->Settings.UIScale=V;});SliderSetting(TEXT("Camera interaction speed"),P->Settings.CameraSpeed,.5,2,[P](float V){P->Settings.CameraSpeed=V;});
    auto* Controls=Row();auto* Drag=Combo({TEXT("Click + drag deployment"),TEXT("Click deployment")},P->Settings.DragDeploy?TEXT("Click + drag deployment"):TEXT("Click deployment"));Add(Controls,Drag,true);auto* Confirm=Combo({TEXT("Direct deployment"),TEXT("Confirm deployment")},P->Settings.ConfirmDeploy?TEXT("Confirm deployment"):TEXT("Direct deployment"));Add(Controls,Confirm,true);
    Add(Body,Button(TEXT("APPLY & SAVE SETTINGS"),[this,P,Mode,Resolution,VSync,Cap,QualityChoices,Drag,Confirm](){P->Settings.WindowMode=Mode->GetSelectedIndex();FString Width,Height;if(Resolution->GetSelectedOption().Split(TEXT("x"),&Width,&Height)){P->Settings.Width=FCString::Atoi(*Width);P->Settings.Height=FCString::Atoi(*Height);}P->Settings.VSync=VSync->GetSelectedIndex()==0;P->Settings.FrameCap=FCString::Atoi(*Cap->GetSelectedOption());int32* Fields[]={&P->Settings.AA,&P->Settings.Shadows,&P->Settings.Effects,&P->Settings.Textures,&P->Settings.Post,&P->Settings.ViewDistance};for(int32 I=0;I<QualityChoices.Num();++I)*Fields[I]=QualityChoices[I].Value->GetSelectedIndex();P->Settings.DragDeploy=Drag->GetSelectedIndex()==0;P->Settings.ConfirmDeploy=Confirm->GetSelectedIndex()==1;P->ApplySettings();FString Result=P->Save()?TEXT("Settings applied and saved."):P->LastError;Navigate(TEXT("Settings"));Say(Result);},true));
}
FString URiftUIWidget::SelectedCardId()const
{
    if(bSpawnArmed&&DevCard)return DevCard->GetSelectedOption();auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();auto* S=M->ViewState();return S&&HandIndex>=0&&HandIndex<4?FS(S->hands[0][HandIndex]):TEXT("");
}
int32 URiftUIWidget::PlacementTeam()const{return bSpawnArmed&&DevTeam&&DevTeam->GetSelectedIndex()==1?1:0;}
void URiftUIWidget::WorldClicked(FVector2D Tile)
{
    if(Page!=TEXT("Battle"))return;if(bSpawnArmed){bool Result=GetWorld()->GetSubsystem<URiftDeveloperSubsystem>()->SpawnCard(DevTeam&&DevTeam->GetSelectedIndex()==1?1:0,SelectedCardId(),Tile);Say(Result?TEXT("Developer deployment recorded."):TEXT("That tile is blocked."));return;}if(HandIndex<0)return;bool Result=GetWorld()->GetSubsystem<URiftMatchSubsystem>()->PlayCard(HandIndex,Tile);if(Result){HandIndex=-1;Say(TEXT(""));}else Say(TEXT("Cannot deploy here. Check Aether, occupied tiles and lane pocket rules."));
}
void URiftUIWidget::Battle()
{
    auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();const auto* S=M->ViewState();if(!S){Navigate(TEXT("Home"));return;}
    auto* Top=WidgetTree->ConstructWidget<UHorizontalBox>();auto* TopSlot=Root->AddChildToCanvas(Top);TopSlot->SetAnchors(FAnchors(.20,0,.80,0));TopSlot->SetOffsets(FMargin(0,14,0,60));
    TimerText=Text(TEXT("3:00"),22,Brass);ScoreText=Text(TEXT("0 — 0"),23);Add(Top,TimerText,true);Add(Top,ScoreText,true);Add(Top,Button(TEXT("HOME"),[this](){Navigate(TEXT("Home"));}));Add(Top,Button(TEXT("LOADOUT"),[this](){Navigate(TEXT("Loadout"));}));Add(Top,Button(TEXT("DEV"),[this](){ToggleDeveloper();}));
    auto* Bottom=WidgetTree->ConstructWidget<UBorder>();Bottom->SetBrushColor(Stone);auto* BottomSlot=Root->AddChildToCanvas(Bottom);BottomSlot->SetAnchors(FAnchors(.20,1,.80,1));BottomSlot->SetOffsets(FMargin(0,-190,0,176));auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();Bottom->SetContent(Column);
    auto* Hand=WidgetTree->ConstructWidget<UHorizontalBox>();Add(Column,Hand,2);for(int32 I=0;I<4;++I){auto* B=Button(TEXT(""),[this,I](){HandIndex=I;bSpawnArmed=false;Say(TEXT("Select a tile center to deploy. Right click cancels."));});B->OnClicked.Clear();B->OnPressed.AddDynamic(B,&URiftActionButton::Invoke);B->ReleaseAction=[this,B](){auto* ProfileData=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();if(ProfileData->Settings.DragDeploy&&!B->IsHovered())if(auto* Controller=Cast<ARiftPlayerController>(GetOwningPlayer()))Controller->ReleaseCardAtCursor();};auto* Stack=WidgetTree->ConstructWidget<UVerticalBox>();auto* Image=WidgetTree->ConstructWidget<UImage>();FString Id=FS(S->hands[0][I]);if(auto* Art=LoadObject<UTexture2D>(nullptr,*FString::Printf(TEXT("/Game/Rift/CardArt/T_Card_%s.T_Card_%s"),*Id,*Id)))Image->SetBrushFromTexture(Art);auto* ArtBox=WidgetTree->ConstructWidget<USizeBox>();ArtBox->SetHeightOverride(80);ArtBox->SetContent(Image);Add(Stack,ArtBox,0);auto* Label=Text(TEXT(""),14);Add(Stack,Label,2);B->SetContent(Stack);Add(Hand,B,true);HandText.Add(Label);HandButtons.Add(B);HandImages.Add(Image);HandArtIds.Add(Id);}
    auto* Bank=WidgetTree->ConstructWidget<UHorizontalBox>();Add(Column,Bank,2);AetherText=Text(TEXT("AETHER 5 / 10"),17,Cyan);Add(Bank,AetherText,true);NextText=Text(TEXT("NEXT"),14,Muted);Add(Bank,NextText,true);AetherBar=WidgetTree->ConstructWidget<UProgressBar>();AetherBar->SetFillColorAndOpacity(Cyan);auto* BarSize=WidgetTree->ConstructWidget<USizeBox>();BarSize->SetHeightOverride(5);BarSize->SetContent(AetherBar);Add(Column,BarSize,2);NoticeText=Text(Notice,12,Cyan);Add(Column,NoticeText,2);
    if(bDev){auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetBrushColor(Stone);auto* PanelSlot=Root->AddChildToCanvas(Frame);PanelSlot->SetAnchors(FAnchors(1,0,1,1));PanelSlot->SetOffsets(FMargin(-310,88,298,205));auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Frame->SetContent(Scroll);auto* Dev=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Dev);Developer(Dev);}
    if(S->phase==rift::Phase::Finished){auto* End=WidgetTree->ConstructWidget<UBorder>();End->SetBrushColor(Stone);auto* PanelSlot=Root->AddChildToCanvas(End);PanelSlot->SetAnchors(FAnchors(.28,.25,.72,.65));PanelSlot->SetOffsets(FMargin(0));auto* Summary=WidgetTree->ConstructWidget<UVerticalBox>();End->SetContent(Summary);Add(Summary,Text(S->winner==0?TEXT("VICTORY"):S->winner==1?TEXT("DEFEAT"):TEXT("DRAW"),30,Brass));Add(Summary,Text(FS(S->resultReason),16));Add(Summary,Button(TEXT("MATCH ANALYSIS"),[this](){auto* R=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();if(R->OpenReplay(R->LatestFilename)){OpenedReplay=R->LatestFilename;Navigate(TEXT("Analysis"));}else Say(R->LastError);}));Add(Summary,Button(TEXT("WATCH REPLAY"),[this](){auto* R=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();if(R->OpenReplay(R->LatestFilename)){OpenedReplay=R->LatestFilename;Navigate(TEXT("ReplayView"));}else Say(R->LastError);}));Add(Summary,Button(TEXT("PLAY AGAIN"),[this,M](){M->StartMatch(M->IsTraining());Navigate(TEXT("Battle"));},true));Add(Summary,Button(TEXT("HOME"),[this](){Navigate(TEXT("Home"));}));}
}
void URiftUIWidget::Developer(UVerticalBox* Target)
{
    auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();auto* D=GetWorld()->GetSubsystem<URiftDeveloperSubsystem>();auto* AI=GetWorld()->GetSubsystem<URiftAISubsystem>();Add(Target,Text(TEXT("DEVELOPER LAB"),16,Brass));auto* Time=Row(Target);for(float Speed:{0.f,.25f,.5f,1.f,2.f,4.f})Add(Time,Button(Speed==0?TEXT("PAUSE"):FString::Printf(TEXT("%.2gx"),Speed),[M,Speed](){M->SetSpeed(Speed);}));
    for(int32 Team=0;Team<2;++Team){Add(Target,Text(Team==0?TEXT("FRIENDLY AETHER"):TEXT("ENEMY AETHER"),12,Muted));auto* Bank=Row(Target);Add(Bank,Button(TEXT("−1"),[D,Team](){D->ChangeAether(Team,-1);}));Add(Bank,Button(TEXT("+1"),[D,Team](){D->ChangeAether(Team,1);}));Add(Bank,Button(TEXT("MAX"),[D,Team](){D->ChangeAether(Team,0,true);}));}
    TArray<FString> IDs;for(const auto& C:rift::Cards())IDs.Add(FS(C.id));DevCard=Combo(IDs,DevCard?DevCard->GetSelectedOption():TEXT("ironclad"));Add(Target,DevCard);DevTeam=Combo({TEXT("Friendly"),TEXT("Enemy")},TEXT("Friendly"));Add(Target,DevTeam);Add(Target,Button(bSpawnArmed?TEXT("DISARM SPAWN"):TEXT("ARM SPAWN"),[this](){bSpawnArmed=!bSpawnArmed;Say(bSpawnArmed?TEXT("Click an arena tile to spawn the selected card."):TEXT("Developer spawn disarmed."));}));
    const auto* S=M->ViewState();DevStyle=Combo({TEXT("beatdown"),TEXT("aggro"),TEXT("control"),TEXT("cycle"),TEXT("split"),TEXT("spell_cycle"),TEXT("counter")},S?FS(S->ai[1].style):TEXT("control"));Add(Target,DevStyle);Add(Target,Button(TEXT("APPLY AI STYLE"),[this,AI](){AI->SetStyle(1,DevStyle->GetSelectedOption());}));Add(Target,Button(TEXT("TOGGLE ENEMY AI"),[M,AI](){auto* V=M->ViewState();if(V)AI->SetEnabled(1,!V->ai[1].enabled);}));DevReadout=Text(AI->Readout(),12,Muted);Add(Target,DevReadout);
    TArray<FString> Towers;if(S)for(auto E:S->entities)if(E.kind==rift::EntityKind::Guard||E.kind==rift::EntityKind::Core)Towers.Add(FString::Printf(TEXT("%llu · %s %s %d"),E.id,E.team==rift::Team::Player?TEXT("Friendly"):TEXT("Enemy"),E.kind==rift::EntityKind::Core?TEXT("Core"):TEXT("Guard"),E.lane));DevTower=Combo(Towers,TEXT(""));Add(Target,DevTower);TowerHP=Edit(TEXT("2250"),TEXT("Tower HP"));Add(Target,TowerHP);Add(Target,Button(TEXT("SET TOWER HP"),[this,D](){int64 Id=FCString::Atoi64(*DevTower->GetSelectedOption());Say(D->SetTowerHP(Id,FCString::Atof(*TowerHP->GetText().ToString()))?TEXT("Tower edit recorded."):TEXT("Invalid tower or HP."));}));Add(Target,Button(TEXT("CLEAR BATTLEFIELD"),[D](){D->ClearBattlefield();}));
    for(auto Pair:TArray<TPair<FString,bool*>>{{TEXT("Paths"),&D->ShowPaths},{TEXT("Front / rear sight"),&D->ShowSight},{TEXT("Attack range"),&D->ShowRanges},{TEXT("Target lines"),&D->ShowTargets},{TEXT("Hard locks"),&D->ShowHardLocks},{TEXT("Tile coordinates"),&D->ShowTiles}}){bool* Flag=Pair.Value;Add(Target,Button(Pair.Key+(*Flag?TEXT(" ON"):TEXT(" OFF")),[this,Flag](){*Flag=!*Flag;Navigate(TEXT("Battle"));},*Flag));}
    Add(Target,Button(TEXT("META VALIDATION"),[this](){Tab=TEXT("Validation");Navigate(TEXT("Meta"));}));
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
void URiftUIWidget::Replays()
{
    auto* R=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();auto* P=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();Add(Body,Text(TEXT("RECORDED MATCHES"),19,Brass));Add(Body,Text(TEXT("Playback uses recorded events and positions. AI and randomness are not run again."),14,Muted));
    FileInput=Edit(FPaths::Combine(URiftProfileSubsystem::SaveRoot(),TEXT("Replays"),TEXT("export.json")),TEXT("Import or export JSON file path"));Add(Body,FileInput);Add(Body,Button(TEXT("IMPORT JSON"),[this,R](){Say(R->ImportReplay(FileInput->GetText().ToString())?TEXT("Replay imported."):R->LastError);Navigate(TEXT("Replays"));}));
    if(P->ReplayFiles.IsEmpty())Add(Body,Text(TEXT("Complete a battle or leave a training match to record your first replay."),17));for(int32 I=P->ReplayFiles.Num()-1;I>=0;--I){FString File=P->ReplayFiles[I];auto* Line=Row();Add(Line,Text(File,14),true);Add(Line,Button(TEXT("WATCH"),[this,R,File](){if(R->OpenReplay(File)){OpenedReplay=File;Navigate(TEXT("ReplayView"));}else Say(R->LastError);}));Add(Line,Button(TEXT("ANALYSIS"),[this,R,File](){if(R->OpenReplay(File)){OpenedReplay=File;R->SetSpeed(0);Navigate(TEXT("Analysis"));}else Say(R->LastError);}));Add(Line,Button(TEXT("EXPORT"),[this,R,File](){Say(R->ExportReplay(File,FileInput->GetText().ToString())?TEXT("Replay exported."):TEXT("Export path could not be written."));}));Add(Line,Button(TEXT("REMOVE"),[this,R,File](){if(R->DeleteReplay(File))Navigate(TEXT("Replays"));else Say(R->LastError);}));}
}
void URiftUIWidget::Bookmarks()
{
    auto* R=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();auto Events=R->EventsNear(R->Duration(),R->Duration()+1);TMap<FString,double> Marks;TMap<double,double> SpellValues,SpellTimes;
    for(auto E:Events){FString Type=JS(E,TEXT("type"));double Time=JN(E,TEXT("time"));if(Type==TEXT("damage")&&JN(E,TEXT("targetKind"))>=2&&!Marks.Contains(TEXT("FIRST TOWER DAMAGE")))Marks.Add(TEXT("FIRST TOWER DAMAGE"),Time);if(Type==TEXT("tower_destroy")&&!Marks.Contains(TEXT("TOWER DESTROYED")))Marks.Add(TEXT("TOWER DESTROYED"),Time);if(Type==TEXT("phase")&&JS(E,TEXT("reason"))==TEXT("overtime"))Marks.Add(TEXT("OVERTIME"),Time);const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*JS(E,TEXT("cardId"))));if(Type==TEXT("damage")&&Card&&Card->spell){double Play=JN(E,TEXT("playId"));SpellValues.FindOrAdd(Play)+=JN(E,TEXT("amount"));if(!SpellTimes.Contains(Play))SpellTimes.Add(Play,Time);}}
    double Biggest=0;for(auto Pair:SpellValues)if(Pair.Value>Biggest){Biggest=Pair.Value;Marks.Add(TEXT("BIGGEST SPELL"),SpellTimes[Pair.Key]);}
    Biggest=0;for(auto Sample:R->RecordedStates()){const TArray<TSharedPtr<FJsonValue>>* Entities;if(!Sample->TryGetArrayField(TEXT("entities"),Entities))continue;double Push[2]={0,0};for(auto V:*Entities){auto E=V->AsObject();if(!E||JN(E,TEXT("hp"))<=0||JN(E,TEXT("kind"))>=2)continue;const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*JS(E,TEXT("cardId"))));const TSharedPtr<FJsonObject>* Position;if(!Card||!E->TryGetObjectField(TEXT("position"),Position))continue;int32 Team=FMath::Clamp(int32(JN(E,TEXT("team"))),0,1);if((Team==0&&JN(*Position,TEXT("z"))<0)||(Team==1&&JN(*Position,TEXT("z"))>0))Push[Team]+=double(Card->cost)/Card->count*JN(E,TEXT("hp"))/FMath::Max(1.0,JN(E,TEXT("maxHp")));}double Value=FMath::Max(Push[0],Push[1]);if(Value>Biggest){Biggest=Value;Marks.Add(TEXT("LARGEST PUSH"),JN(Sample,TEXT("time")));}}
    Marks.Add(TEXT("MATCH END"),R->Duration());auto* Line=Row();for(auto Pair:Marks)Add(Line,Button(Pair.Key,[R,Time=Pair.Value](){R->Seek(Time);}));
}
void URiftUIWidget::ReplayView()
{
    auto* R=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();if(!R->IsPlaying()){Navigate(TEXT("Replays"));return;}
    auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetBrushColor(Stone);auto* PanelSlot=Root->AddChildToCanvas(Frame);PanelSlot->SetAnchors(FAnchors(.12,0,.88,0));PanelSlot->SetOffsets(FMargin(0,12,0,132));Body=WidgetTree->ConstructWidget<UVerticalBox>();Frame->SetContent(Body);
    auto* Controls=Row();Add(Controls,Text(TEXT("RECORDED REPLAY"),18,Brass),true);Add(Controls,Button(TEXT("HOME"),[this](){Navigate(TEXT("Home"));}));Add(Controls,Button(TEXT("ANALYSIS"),[this](){Navigate(TEXT("Analysis"));}));for(float Speed:{0.f,.25f,.5f,1.f,2.f,4.f})Add(Controls,Button(Speed==0?TEXT("PAUSE"):FString::Printf(TEXT("%.2gx"),Speed),[R,Speed](){R->SetSpeed(Speed);}));Add(Controls,Button(TEXT("−5s"),[R](){R->Seek(R->Position()-5);}));Add(Controls,Button(TEXT("+5s"),[R](){R->Seek(R->Position()+5);}));
    ReplayPosition=Text(TEXT(""),14,Cyan);Add(Body,ReplayPosition,1);ReplaySeek=WidgetTree->ConstructWidget<URiftValueSlider>();ReplaySeek->SetMaxValue(FMath::Max(1.f,R->Duration()));ReplaySeek->SetValue(R->Position());ReplaySeek->Bind([R](float Position){R->Seek(Position);});Add(Body,ReplaySeek,1);Bookmarks();
    auto* Ledger=WidgetTree->ConstructWidget<UBorder>();Ledger->SetBrushColor(Stone);auto* LedgerSlot=Root->AddChildToCanvas(Ledger);LedgerSlot->SetAnchors(FAnchors(.12,1,.88,1));LedgerSlot->SetOffsets(FMargin(0,-155,0,145));auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Ledger->SetContent(Scroll);Rows=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Rows);NoticeText=Text(TEXT("Recorded events — cyan player, brass enemy. Tower HP remains visible in the arena."),12,Muted);Add(Rows,NoticeText,2);
}
void URiftUIWidget::MatchAnalysis()
{
    auto* R=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();auto Result=R->CurrentAnalysis();if(!Result){Add(Body,Text(TEXT("Open a recorded replay to inspect its match analysis."),18));return;}R->SetSpeed(0);Add(Body,Button(TEXT("WATCH REPLAY"),[this](){Navigate(TEXT("ReplayView"));},true));
    const TArray<TSharedPtr<FJsonValue>>* Teams;if(!Result->TryGetArrayField(TEXT("teams"),Teams))return;auto Samples=R->RecordedStates();double WeightedCost[2]={0,0},Weight=0,Advantage[2]={0,0},LargestPush[2]={0,0};
    for(int32 I=0;I<Samples.Num();++I){auto Sample=Samples[I];const TArray<TSharedPtr<FJsonValue>>* Sides;if(!Sample->TryGetArrayField(TEXT("teams"),Sides)||Sides->Num()!=2)continue;double Dt=I+1<Samples.Num()?FMath::Max(0.0,JN(Samples[I+1],TEXT("time"))-JN(Sample,TEXT("time"))):0;Weight+=Dt;double Banks[2]={0,0};for(int32 T=0;T<2;++T){auto Side=(*Sides)[T]->AsObject();Banks[T]=JN(Side,TEXT("aether"));const TArray<TSharedPtr<FJsonValue>>* Hand;if(Side->TryGetArrayField(TEXT("hand"),Hand)){double Cost=0;for(auto Id:*Hand)if(const auto* C=rift::FindCard(TCHAR_TO_UTF8(*Id->AsString())))Cost+=C->cost;WeightedCost[T]+=Cost/FMath::Max(1,Hand->Num())*Dt;}}Advantage[0]=FMath::Max(Advantage[0],Banks[0]-Banks[1]);Advantage[1]=FMath::Max(Advantage[1],Banks[1]-Banks[0]);
        const TArray<TSharedPtr<FJsonValue>>* Entities;if(Sample->TryGetArrayField(TEXT("entities"),Entities)){double Push[2]={0,0};for(auto V:*Entities){auto E=V->AsObject();if(!E||JN(E,TEXT("hp"))<=0||JN(E,TEXT("kind"))>=2)continue;const auto* C=rift::FindCard(TCHAR_TO_UTF8(*JS(E,TEXT("cardId"))));const TSharedPtr<FJsonObject>* Pos;if(!C||!E->TryGetObjectField(TEXT("position"),Pos))continue;int32 Team=FMath::Clamp(int32(JN(E,TEXT("team"))),0,1);double Z=JN(*Pos,TEXT("z"));if((Team==0&&Z<0)||(Team==1&&Z>0))Push[Team]+=double(C->cost)/C->count*JN(E,TEXT("hp"))/FMath::Max(1.0,JN(E,TEXT("maxHp")));}for(int32 T=0;T<2;++T)LargestPush[T]=FMath::Max(LargestPush[T],Push[T]);}}
    for(int32 T=0;T<FMath::Min(2,Teams->Num());++T){auto Side=(*Teams)[T]->AsObject();Add(Body,Text(T==0?TEXT("PLAYER"):TEXT("OPPONENT"),20,T==0?Cyan:Brass));Add(Body,Text(FString::Printf(TEXT("Crowns %.0f   ·   Aether spent %.1f   ·   Aether leaked %.1f\nLargest bank advantage %.2f   ·   Largest surviving push %.2f Aether\nTime-weighted average hand cost %.2f"),JN(Side,TEXT("crowns")),JN(Side,TEXT("spent")),JN(Side,TEXT("leaked")),Advantage[T],LargestPush[T],Weight?WeightedCost[T]/Weight:0),16));const TSharedPtr<FJsonObject>* Metrics;if(Side->TryGetObjectField(TEXT("telemetry"),Metrics)){FString Most,Least;double Best=-1,Worst=DBL_MAX;for(auto Pair:(*Metrics)->Values){auto Card=Pair.Value->AsObject();double Spent=JN(Card,TEXT("spent"));if(Spent<=0)continue;double Value=(JN(Card,TEXT("troopDamage"))+JN(Card,TEXT("towerDamage"))+JN(Card,TEXT("buildingDamage")))/Spent;if(Value>Best){Best=Value;Most=FString(*Pair.Key);}if(Value<Worst){Worst=Value;Least=FString(*Pair.Key);}}Add(Body,Text(TEXT("Most damage / Aether: ")+Most+TEXT("   ·   Least: ")+Least,15));for(auto Pair:(*Metrics)->Values){auto C=Pair.Value->AsObject();if(JN(C,TEXT("plays"))<=0&&JN(C,TEXT("spawns"))<=0)continue;Add(Body,Text(Pretty(FString(*Pair.Key).Replace(TEXT("_"),TEXT(" "))),16,Muted));ShowJSON(C,Body);}}}
    auto Events=R->EventsNear(R->Duration(),R->Duration()+1);TMap<double,double> Kills,Costs;for(auto E:Events){int32 Team=FMath::Clamp(int32(JN(E,TEXT("team"))),0,1);double Key=JN(E,TEXT("playId"))+Team*1000000.0;if(JS(E,TEXT("type"))==TEXT("card_play"))Costs.Add(Key,JN(E,TEXT("amount")));if(JS(E,TEXT("type"))==TEXT("death")){const TSharedPtr<FJsonObject>* Pos;if(E->TryGetObjectField(TEXT("position"),Pos)){double Z=JN(*Pos,TEXT("z"));if((Team==0&&Z>0)||(Team==1&&Z<0))Kills.FindOrAdd(Key)+=JN(E,TEXT("targetCost"));}}}double Trade=0;for(auto Pair:Kills)Trade=FMath::Max(Trade,Pair.Value-Costs.FindRef(Pair.Key));Add(Body,Text(FString::Printf(TEXT("Best realized defensive trade: %.2f enemy Aether killed minus deployment cost. Damage excludes overkill; Developer actions remain marked in the event record."),Trade),15,Muted));
    for(int32 Metric=0;Metric<2;++Metric){Add(Body,Text(Metric==0?TEXT("AETHER BANK THROUGH THE MATCH"):TEXT("TOTAL CROWN TOWER HP"),17,Brass));auto* Chart=CreateWidget<URiftChartWidget>(GetOwningPlayer());Chart->Caption=TEXT("Time in seconds · cyan player · brass opponent");for(auto Sample:Samples){double Time=JN(Sample,TEXT("time")),Values[2]={0,0};const TArray<TSharedPtr<FJsonValue>>* A;if(Metric==0&&Sample->TryGetArrayField(TEXT("teams"),A)&&A->Num()==2)for(int32 T=0;T<2;++T)Values[T]=JN((*A)[T]->AsObject(),TEXT("aether"));if(Metric==1&&Sample->TryGetArrayField(TEXT("entities"),A))for(auto V:*A){auto E=V->AsObject();if(JN(E,TEXT("kind"))>=2)Values[FMath::Clamp(int32(JN(E,TEXT("team"))),0,1)]+=JN(E,TEXT("hp"));}Chart->SeriesA.Add(FVector2D(Time,Values[0]));Chart->SeriesB.Add(FVector2D(Time,Values[1]));}auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetHeightOverride(180);Size->SetContent(Chart);Add(Body,Size);}
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
    Values.Sort([this](const auto& A,const auto& B){double VA=JN(A,*SortKey,-DBL_MAX),VB=JN(B,*SortKey,-DBL_MAX);return VA==VB?JS(A,TEXT("name"))<JS(B,TEXT("name")):bSortDescending?VA>VB:VA<VB;});return Values;
}
void URiftUIWidget::Meta()
{
    auto* M=GetGameInstance()->GetSubsystem<URiftMetaSimulationSubsystem>();MetaStatus=Text(M->Status(),16,Cyan);Add(Body,MetaStatus);auto* Control=Row();for(int32 Rate:{100,250,500,0})Add(Control,Button(Rate?FString::Printf(TEXT("%d / MIN"),Rate):TEXT("MAX SAFE"),[this,M,Rate](){M->SetRate(Rate);M->Start();bMetaPaused=false;Say(TEXT("Background simulation pauses automatically during live battles."));}));Add(Control,Button(TEXT("PAUSE / RESUME"),[this,M](){bMetaPaused=!bMetaPaused;M->Pause(bMetaPaused);}));Add(Control,Button(TEXT("ARCHIVE & RESET"),[this,M](){M->Reset();DatasetName.Empty();MetaDetail.Reset();Navigate(TEXT("Meta"));}));
    auto* Tabs=Row();for(FString Name:{TEXT("Cards"),TEXT("Matchups"),TEXT("Synergy"),TEXT("Archetypes"),TEXT("AI Styles"),TEXT("Alerts"),TEXT("Trends"),TEXT("Patches"),TEXT("Validation")})Add(Tabs,Button(Name.ToUpper(),[this,Name](){CaptureFilters();Tab=Name;MetaDetail.Reset();Navigate(TEXT("Meta"));},Tab==Name),true);
    auto* Filters=Row();MetaSearch=Edit(Search,TEXT("Card search"));Add(Filters,MetaSearch,true);MetaType=Combo({TEXT("all"),TEXT("Troop"),TEXT("Building"),TEXT("Spell")},TypeFilter);Add(Filters,MetaType);MetaCost=Combo({TEXT("all"),TEXT("2"),TEXT("3"),TEXT("4"),TEXT("5"),TEXT("6")},CostFilter);Add(Filters,MetaCost);MetaTrait=Combo({TEXT("all"),TEXT("Air"),TEXT("Ground"),TEXT("Anti-air"),TEXT("Win condition"),TEXT("Swarm"),TEXT("Splash")},TraitFilter);Add(Filters,MetaTrait);MetaMin=Edit(FString::FromInt(MinSample),TEXT("Minimum N"));auto* NS=WidgetTree->ConstructWidget<USizeBox>();NS->SetWidthOverride(90);NS->SetContent(MetaMin);Add(Filters,NS);
    auto* Slice=Row();MetaStyle=Combo(Styles,StyleFilter);Add(Slice,Text(TEXT("AI style"),13));Add(Slice,MetaStyle,true);MetaArchetype=Combo(Archetypes,ArchetypeFilter);Add(Slice,Text(TEXT("Archetype"),13));Add(Slice,MetaArchetype,true);auto Names=M->DatasetNames();if(Names.IsEmpty())Names.Add(TEXT("Current"));if(!Names.Contains(DatasetName))DatasetName=Names.Contains(M->SelectedDatasetName())?M->SelectedDatasetName():Names.Last();MetaVersion=Combo(Names,DatasetName);Add(Slice,MetaVersion,true);Add(Slice,Button(TEXT("APPLY FILTERS / VERSION"),[this,M](){CaptureFilters();FString Chosen=MetaVersion->GetSelectedOption();if(Chosen!=DatasetName){if(M->SelectDataset(Chosen)){DatasetName=Chosen;bMetaPaused=true;}else Say(M->LastError.IsEmpty()?TEXT("Cannot open that dataset."):M->LastError);}MetaDetail.Reset();MetaRows();}));
    auto* Exports=Row();MetaSubject=Combo({TEXT("cards"),TEXT("matchups"),TEXT("synergy"),TEXT("archetypes"),TEXT("styles"),TEXT("patch")},TEXT("cards"));Add(Exports,MetaSubject);MetaFormat=Combo({TEXT("CSV"),TEXT("JSON")},TEXT("CSV"));Add(Exports,MetaFormat);FileInput=Edit(FPaths::Combine(URiftProfileSubsystem::SaveRoot(),TEXT("Meta"),TEXT("export.csv")),TEXT("Export path"));Add(Exports,FileInput,true);Add(Exports,Button(TEXT("EXPORT FILTERED DATA"),[this](){ExportMeta();}));
    if(Tab==TEXT("Patches")){auto* Compare=Row();Names.Append({TEXT("V12.8"),TEXT("V12.10"),TEXT("V14"),TEXT("V15")});MetaBaseline=Combo(Names,TEXT("V15"));Add(Compare,Text(TEXT("Baseline dataset"),13));Add(Compare,MetaBaseline,true);Add(Compare,Button(TEXT("COMPARE"),[this](){MetaRows();}));}
    if(Tab==TEXT("Trends")){auto* Trend=Row();MetaWindow=Combo({TEXT("100"),TEXT("500"),TEXT("1000"),TEXT("5000"),TEXT("10000"),TEXT("all")},TEXT("all"));Add(Trend,Text(TEXT("Recent cumulative checkpoints"),13));Add(Trend,MetaWindow);MetaMetric=Combo({TEXT("adjustedWinRate"),TEXT("pickRate"),TEXT("damagePerAether")},TEXT("adjustedWinRate"));Add(Trend,MetaMetric);TArray<FString> IDs;for(auto C:rift::Cards())IDs.Add(FS(C.id));auto* Card=Combo(IDs,DetailCard.IsEmpty()?TEXT("ironclad"):DetailCard);Add(Trend,Card);Add(Trend,Button(TEXT("DRAW TREND"),[this,Card](){DetailCard=Card->GetSelectedOption();MetaRows();}));}
    Rows=WidgetTree->ConstructWidget<UVerticalBox>();Add(Body,Rows,2);MetaRows();
}
void URiftUIWidget::MetaRows()
{
    if(!Rows)return;Rows->ClearChildren();auto* M=GetGameInstance()->GetSubsystem<URiftMetaSimulationSubsystem>();CaptureFilters();if(MetaDetail){Add(Rows,Button(TEXT("BACK TO TABLE"),[this](){MetaDetail.Reset();MetaRows();}));ShowJSON(MetaDetail,Rows);return;}
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
        for(const auto* RowCard:VisibleCards){const auto& A=*RowCard;auto* Line=Row(Matrix);auto* Label=WidgetTree->ConstructWidget<USizeBox>();Label->SetWidthOverride(140);Label->SetContent(Text(FS(A.name),13));Add(Line,Label);for(const auto* ColumnCard:VisibleCards){const auto& B=*ColumnCard;FString IA=FS(A.id),IB=FS(B.id),Key=Empirical?(IA<IB?IA+TEXT("+")+IB:IB+TEXT("+")+IA):IA+TEXT("/")+IB;auto Value=Cells.FindRef(Key);double Score=JN(Value,Empirical?TEXT("delta"):TEXT("edge"));bool Available=IA!=IB&&Value&&(!Empirical||JN(Value,TEXT("cleanN"))>=FMath::Max(1,MinSample));auto* ButtonCell=Button(Available?FString::Printf(TEXT("%+.1f"),Score):TEXT("—"),[this,Value](){if(Value){MetaDetail=Value;MetaRows();}});ButtonCell->SetBackgroundColor(Available?(Score>0?FLinearColor(.10,.27,.30,1):Score<0?FLinearColor(.28,.22,.14,1):Stone):Stone);auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(53);Size->SetHeightOverride(32);Size->SetContent(ButtonCell);Add(Line,Size);}}return;
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
    auto* Header=Row(Rows);TArray<FString> Columns=Tab==TEXT("Patches")?TArray<FString>{TEXT("name"),TEXT("delta"),TEXT("baselineN"),TEXT("cleanN")}:Tab==TEXT("Archetypes")?TArray<FString>{TEXT("name"),TEXT("pickRate"),TEXT("adjustedWinRate"),TEXT("cleanN"),TEXT("averageCrowns"),TEXT("averageDuration")}:TArray<FString>{TEXT("name"),TEXT("pickRate"),TEXT("adjustedWinRate"),TEXT("rawWinRate"),TEXT("cleanN"),TEXT("damagePerAether"),TEXT("towerDamagePerAether")};
    for(const auto& Key:Columns)Add(Header,Button(Pretty(Key),[this,Key](){bSortDescending=SortKey==Key?!bSortDescending:true;SortKey=Key;MetaRows();}),true);
    if(Values.IsEmpty())Add(Rows,Text(Tab==TEXT("Alerts")?TEXT("No card currently meets the confidence and sample-size requirements for an alert."):TEXT("No recorded rows meet these filters."),17));
    for(const auto& Value:Values)
    {
        FString Label=JS(Value,TEXT("name"));
        if(Tab==TEXT("Alerts"))Label=JS(Value,TEXT("severity"))+TEXT(" · ")+Label+TEXT(" · ")+JS(Value,TEXT("direction"))+TEXT(" · ")+JS(Value,TEXT("alert"));
        else if(Tab==TEXT("Patches"))Label+=TEXT(" · ")+JS(Value,TEXT("availability"))+TEXT(" · Δ ")+Metric(Value,TEXT("delta"))+TEXT(" · baseline N ")+Metric(Value,TEXT("baselineN"))+TEXT(" · current N ")+Metric(Value,TEXT("cleanN"));
        else if(Tab==TEXT("Archetypes"))Label+=TEXT(" · Pick ")+Metric(Value,TEXT("pickRate"))+TEXT("% · Adjusted ")+Metric(Value,TEXT("adjustedWinRate"))+TEXT("% · N ")+Metric(Value,TEXT("cleanN"))+TEXT(" · Crowns/game ")+Metric(Value,TEXT("averageCrowns"))+TEXT(" · Duration ")+Metric(Value,TEXT("averageDuration"))+TEXT("s");
        else Label+=TEXT(" · Pick ")+Metric(Value,TEXT("pickRate"))+TEXT("% · Adjusted ")+Metric(Value,TEXT("adjustedWinRate"))+TEXT("% · Raw ")+Metric(Value,TEXT("rawWinRate"))+TEXT("% · 95% CI ")+Metric(Value,TEXT("ciLow"))+TEXT("–")+Metric(Value,TEXT("ciHigh"))+TEXT(" · N ")+Metric(Value,TEXT("cleanN"))+TEXT(" · Damage/Aether ")+Metric(Value,TEXT("damagePerAether"))+TEXT(" · Tower/Aether ")+Metric(Value,TEXT("towerDamagePerAether"));
        Add(Rows,Button(Label,[this,Value](){MetaDetail=Value;MetaRows();}));
    }
}
void URiftUIWidget::ExportMeta()
{
    CaptureFilters();auto* M=GetGameInstance()->GetSubsystem<URiftMetaSimulationSubsystem>();FString Subject=MetaSubject->GetSelectedOption();auto Values=Subject==TEXT("matchups")?M->MatchupRows():Subject==TEXT("synergy")?M->SynergyRows(StyleFilter,ArchetypeFilter):Subject==TEXT("archetypes")?M->ArchetypeRows(StyleFilter):Subject==TEXT("styles")?M->StyleRows(ArchetypeFilter):Subject==TEXT("patch")?M->PatchRows(MetaBaseline?MetaBaseline->GetSelectedOption():TEXT("V15")):M->CardRows(StyleFilter,ArchetypeFilter);Values=FilterRows(Values,Subject==TEXT("cards")||Subject==TEXT("patch"));if(Subject==TEXT("matchups")||Subject==TEXT("synergy")){TSet<FString> VisibleIds;for(auto Card:FilterRows(M->CardRows(StyleFilter,ArchetypeFilter),true))VisibleIds.Add(JS(Card,TEXT("id")));Values.RemoveAll([&VisibleIds](const auto& Pair){FString A,B;const FString Id=JS(Pair,TEXT("id"));return !(Id.Split(TEXT("+"),&A,&B)||Id.Split(TEXT("/"),&A,&B))||!VisibleIds.Contains(A)||!VisibleIds.Contains(B);});}FString Output;bool CSV=MetaFormat->GetSelectedOption()==TEXT("CSV");if(CSV){TArray<FString> Keys;for(auto V:Values)for(auto Pair:V->Values)Keys.AddUnique(FString(*Pair.Key));Keys.Sort();auto Quote=[](FString V){return TEXT("\"")+V.Replace(TEXT("\""),TEXT("\"\""))+TEXT("\"");};for(int32 I=0;I<Keys.Num();++I)Output+=(I?TEXT(","):TEXT(""))+Quote(Keys[I]);Output+=TEXT("\r\n");for(auto V:Values){for(int32 I=0;I<Keys.Num();++I){FString Cell;if(auto Found=V->TryGetField(Keys[I])){if(Found->Type==EJson::String)Cell=Found->AsString();else if(Found->Type==EJson::Number)Cell=FString::SanitizeFloat(Found->AsNumber());else if(Found->Type==EJson::Boolean)Cell=Found->AsBool()?TEXT("true"):TEXT("false");else if(Found->Type==EJson::Object||Found->Type==EJson::Array)FJsonSerializer::Serialize(Found,TEXT(""),TJsonWriterFactory<>::Create(&Cell));}Output+=(I?TEXT(","):TEXT(""))+Quote(Cell);}Output+=TEXT("\r\n");}}
    else{auto Object=MakeShared<FJsonObject>();Object->SetStringField(TEXT("subject"),Subject);Object->SetStringField(TEXT("style"),StyleFilter);Object->SetStringField(TEXT("archetype"),ArchetypeFilter);Object->SetStringField(TEXT("search"),Search);Object->SetStringField(TEXT("type"),TypeFilter);Object->SetStringField(TEXT("trait"),TraitFilter);Object->SetStringField(TEXT("cost"),CostFilter);Object->SetNumberField(TEXT("minSample"),MinSample);Object->SetStringField(TEXT("fingerprint"),JS(M->Dataset(),TEXT("fingerprint")));Object->SetStringField(TEXT("model"),JS(M->Dataset(),TEXT("model")));Object->SetStringField(TEXT("version"),JS(M->Dataset(),TEXT("version")));TArray<TSharedPtr<FJsonValue>> RowsJSON;for(auto V:Values)RowsJSON.Add(MakeShared<FJsonValueObject>(V));Object->SetArrayField(TEXT("rows"),RowsJSON);FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Output));}FString Filename=FileInput->GetText().ToString();if(FPaths::GetExtension(Filename).IsEmpty())Filename+=CSV?TEXT(".csv"):TEXT(".json");FString Error;Say(URiftProfileSubsystem::AtomicWrite(Filename,Output,Error)?FString::Printf(TEXT("Exported %d filtered rows to %s"),Values.Num(),*Filename):Error);
}
void URiftUIWidget::PatchNotes()
{
    Add(Body,Text(TEXT("NATIVE WINDOWS REBUILD · 1.0.0"),22,Brass));Add(Body,Text(TEXT("The authoritative C++ simulation powers rendered battle, background Meta Lab and native regression tests. Current card identities and all locked numerical balance values are retained. Ground bridge routes, directional acquisition, dormant Core Towers, Crown Tower hard locks, lane pockets, Aether phases, overtime and the live HP tiebreaker remain part of the rules."),17));Add(Body,Text(TEXT("This release adds native profile migration, five persistent loadout presets, event-recorded replay playback and post-match telemetry, seven AI personalities, a fully connected Developer Lab, settings, and a separate verified Windows launcher/updater. Browser saves remain preserved; native datasets have their own model fingerprint. Historical patch comparisons remain unavailable when exact observations were not recorded."),17));Add(Body,Text(TEXT("Controls: select a hand card or press 1–4, then choose a legal tile center. Right click cancels selection. Developer controls record their actions in replays. Debug overlays start off. Card, deck and Meta reports are available from the menu without covering the battlefield."),17,Muted));
}
void URiftUIWidget::NativeTick(const FGeometry& Geometry,float Delta)
{
    Super::NativeTick(Geometry,Delta);RefreshClock+=Delta;MetaClock+=Delta;if(RefreshClock<.10f)return;RefreshClock=0;
    auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();auto* R=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();const auto* S=M->ViewState();
    if(Page==TEXT("Battle")&&S){int32 Time=FMath::Max(0,FMath::CeilToInt(M->GetTimeRemaining()));if(TimerText)TimerText->SetText(FText::FromString(FString::Printf(TEXT("%d:%02d · %s"),Time/60,Time%60,*M->GetPhase())));if(ScoreText)ScoreText->SetText(FText::FromString(FString::Printf(TEXT("%d — %d"),S->crowns[0],S->crowns[1])));if(AetherText)AetherText->SetText(FText::FromString(FString::Printf(TEXT("AETHER %.1f / 10"),S->aether[0])));if(AetherBar)AetherBar->SetPercent(S->aether[0]/10);if(NextText&&!S->queues[0].empty()){const auto* Next=rift::FindCard(S->queues[0].front());if(Next)NextText->SetText(FText::FromString(TEXT("NEXT · ")+FS(Next->name)));}for(int32 I=0;I<HandText.Num()&&I<4;++I){const auto* C=rift::FindCard(S->hands[0][I]);if(!C)continue;FString CardId=FS(C->id);if(HandArtIds.IsValidIndex(I)&&HandImages.IsValidIndex(I)&&HandArtIds[I]!=CardId){HandArtIds[I]=CardId;if(auto* Art=LoadObject<UTexture2D>(nullptr,*FString::Printf(TEXT("/Game/Rift/CardArt/T_Card_%s.T_Card_%s"),*CardId,*CardId)))HandImages[I]->SetBrushFromTexture(Art);}HandText[I]->SetText(FText::FromString(FS(C->name)+FString::Printf(TEXT(" · %d"),C->cost)));HandButtons[I]->SetBackgroundColor(I==HandIndex?FLinearColor(.12,.36,.40,1):Stone);HandButtons[I]->SetIsEnabled(S->aether[0]>=C->cost&&S->phase!=rift::Phase::Finished);}if(DevReadout)DevReadout->SetText(FText::FromString(GetWorld()->GetSubsystem<URiftAISubsystem>()->Readout()));if(S->phase==rift::Phase::Finished&&!bEndPresented){bEndPresented=true;Navigate(TEXT("Battle"));}if(S->phase!=rift::Phase::Finished)bEndPresented=false;}
    if(Page==TEXT("ReplayView")&&R->IsPlaying()){if(ReplayPosition)ReplayPosition->SetText(FText::FromString(FString::Printf(TEXT("%.1f / %.1fs · %.2gx · Player Aether %.1f · Enemy %.1f · Crowns %d–%d"),R->Position(),R->Duration(),R->Speed(),S?S->aether[0]:0,S?S->aether[1]:0,S?S->crowns[0]:0,S?S->crowns[1]:0)));if(ReplaySeek&&!ReplaySeek->HasMouseCapture())ReplaySeek->SetValue(R->Position());if(Rows&&MetaClock>.5f){MetaClock=0;Rows->ClearChildren();auto Events=R->EventsNear(R->Position(),5);int32 Start=FMath::Max(0,Events.Num()-18);for(int32 I=Start;I<Events.Num();++I){auto E=Events[I];Add(Rows,Text(FString::Printf(TEXT("%.2fs  %s  %s  %.1f  %s"),JN(E,TEXT("time")),*JS(E,TEXT("type")),*JS(E,TEXT("cardId")),JN(E,TEXT("amount")),*JS(E,TEXT("reason"))),12,JN(E,TEXT("team"))==0?Cyan:Brass),1);}}}
    if(Page==TEXT("Meta")){auto* MetaEngine=GetGameInstance()->GetSubsystem<URiftMetaSimulationSubsystem>();if(MetaStatus)MetaStatus->SetText(FText::FromString(MetaEngine->Status()));if(MetaClock>=3){MetaClock=0;if(!MetaDetail)MetaRows();}}
}
