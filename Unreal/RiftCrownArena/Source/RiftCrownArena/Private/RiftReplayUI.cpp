#include "RiftUIWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "RiftProfileSubsystem.h"
#include "RiftMatchSubsystem.h"
#include "RiftReplaySubsystem.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    const FLinearColor Gold(.82f,.65f,.36f,1), Blue(.32f,.74f,.94f,1), Paper(.93f,.93f,.88f,1), Muted(.59f,.67f,.77f,1);
    FString JS(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key){FString Value;if(Object)Object->TryGetStringField(Key,Value);return Value;}
    double JN(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,double Default=0){double Value=Default;if(Object)Object->TryGetNumberField(Key,Value);return FMath::IsFinite(Value)?Value:Default;}
    FString Name(const FString& Id){const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*Id));return Card?UTF8_TO_TCHAR(Card->name.c_str()):Id;}
    FString Timecode(double Seconds){const int32 Time=FMath::Max(0,FMath::FloorToInt(Seconds));return FString::Printf(TEXT("%d:%02d"),Time/60,Time%60);}
    FString ReplayFilePath(FString Value){Value.TrimStartAndEndInline();return FPaths::IsRelative(Value)?FPaths::ConvertRelativePathToFull(FPaths::Combine(URiftProfileSubsystem::SaveRoot(),Value)):Value;}
    FString Readable(FString Value){Value.ReplaceInline(TEXT("_"),TEXT(" "));return Value.IsEmpty()?Value:Value.Left(1).ToUpper()+Value.Mid(1);}
    FString BookmarkLabel(const FString& Value)
    {
        if(Value==TEXT("FIRST TOWER DAMAGE"))return TEXT("FIRST HIT");if(Value==TEXT("BIGGEST SPELL"))return TEXT("SPELL");
        if(Value==TEXT("TOWER DESTROYED"))return TEXT("TOWER FALL");if(Value==TEXT("LARGEST PUSH"))return TEXT("PEAK PUSH");if(Value==TEXT("MATCH END"))return TEXT("END");return Value;
    }
}

void URiftUIWidget::Replays()
{
    auto* Replay=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();auto* Profile=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();bReplayListSaving=Replay->IsSaving();
    auto* Header=Row();Add(Header,Text(TEXT("YOUR RECORDED MATCHES"),27,Paper),true);Add(Header,Badge(FString::Printf(TEXT("%d REPLAYS"),Profile->ReplayFiles.Num()),FLinearColor(.05f,.13f,.20f,1),14));
    Add(Body,Text(TEXT("Watch the battle, find the turning point, and compare each card's actual contribution."),16,Muted),4);
    if(bReplayListSaving)Add(Body,Text(TEXT("SAVING REPLAY…"),14,Blue),5);else if(!Replay->LastError.IsEmpty())Add(Body,Text(Replay->LastError,14,Gold),5);
    auto* Files=WidgetTree->ConstructWidget<UVerticalBox>();Add(Files,Text(TEXT("IMPORT & EXPORT"),16,Gold),3);Add(Files,Text(TEXT("Choose a JSON file path. Relative paths use your local Rift Crown save folder."),13,Muted),3);
    auto* Path=Row(Files);FileInput=Edit(TEXT("Replays/export.json"),TEXT("Import or export JSON file path"));FileInput->SetToolTipText(FText::FromString(TEXT("Relative paths begin in ")+URiftProfileSubsystem::SaveRoot()));Add(Path,FileInput,true);
    Add(Path,Button(TEXT("IMPORT JSON"),[this,Replay](){const bool Imported=Replay->ImportReplay(ReplayFilePath(FileInput->GetText().ToString()));const FString Message=Imported?TEXT("Replay imported."):Replay->LastError;Navigate(TEXT("Replays"));Say(Message);}));Add(Body,Panel(Files,FMargin(16)),8);
    if(Profile->ReplayFiles.IsEmpty())
    {
        auto* Empty=WidgetTree->ConstructWidget<UVerticalBox>();Add(Empty,Text(TEXT("Your first battle story starts here."),23,Gold),4);Add(Empty,Text(TEXT("Complete a battle or leave a Training session to record a replay. Saved matches can be watched, analyzed, exported or removed here."),16,Paper),6);Add(Empty,Button(TEXT("BACK TO BATTLE LOBBY"),[this](){Navigate(TEXT("Home"));},true),6);Add(Body,Panel(Empty,FMargin(24)),12);
    }
    for(int32 I=Profile->ReplayFiles.Num()-1;I>=0;--I)
    {
        const FString File=Profile->ReplayFiles[I];const FString FullPath=FPaths::Combine(URiftProfileSubsystem::SaveRoot(),TEXT("Replays"),File);
        const FDateTime Saved=IFileManager::Get().GetTimeStamp(*FullPath);const int64 Bytes=IFileManager::Get().FileSize(*FullPath);
        const FString SavedText=Saved==FDateTime::MinValue()?TEXT("Save date unavailable"):TEXT("Saved ")+Saved.ToFormattedString(TEXT("%b %d, %Y  ·  %H:%M"));
        auto* Match=WidgetTree->ConstructWidget<UVerticalBox>();auto* Title=Row(Match);Add(Title,Text(FString::Printf(TEXT("RECORDED BATTLE %02d"),I+1),20,Paper),true);if(Bytes>=0)Add(Title,Badge(FString::Printf(TEXT("%.1f MB"),double(Bytes)/(1024*1024)),FLinearColor(.055f,.09f,.14f,1),11));
        Add(Match,Text(SavedText,14,Muted),3);Add(Match,Text(TEXT("Open the recording to view its result, decks and match statistics."),13,Muted),3);
        auto* Actions=WidgetTree->ConstructWidget<UWrapBox>();Actions->SetInnerSlotPadding(FVector2D(7,7));Add(Match,Actions,4);
        Actions->AddChildToWrapBox(Button(TEXT("WATCH"),[this,Replay,File](){if(Replay->OpenReplay(File)){OpenedReplay=File;bReplayDetailsExpanded=false;Navigate(TEXT("ReplayView"));}else Say(Replay->LastError);},true));
        Actions->AddChildToWrapBox(Button(TEXT("ANALYSIS"),[this,Replay,File](){if(Replay->OpenReplay(File)){OpenedReplay=File;bReplayDetailsExpanded=false;Replay->SetSpeed(0);Navigate(TEXT("Analysis"));}else Say(Replay->LastError);}));
        Actions->AddChildToWrapBox(Button(TEXT("EXPORT"),[this,Replay,File](){Say(Replay->ExportReplay(File,ReplayFilePath(FileInput->GetText().ToString()))?TEXT("Replay exported."):TEXT("Export path could not be written."));}));
        auto* Remove=Button(TEXT("REMOVE"),[this,Replay,File](){if(Replay->DeleteReplay(File))Navigate(TEXT("Replays"));else Say(Replay->LastError);});Remove->SetToolTipText(FText::FromString(TEXT("Remove this saved replay. Your profile and other recordings remain available.")));Actions->AddChildToWrapBox(Remove);
        auto* Archive=Text(File,10,Muted);Archive->SetToolTipText(FText::FromString(FullPath));Add(Match,Archive,3);Add(Body,Panel(Match,FMargin(18)),8);
    }
}

void URiftUIWidget::Bookmarks()
{
    auto* Replay=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();const auto Events=Replay->EventsNear(Replay->TimelineDuration(),Replay->TimelineDuration()+1);TMap<FString,double> Marks;TMap<double,double> SpellValues,SpellTimes;
    for(const auto& Event:Events)
    {
        const FString Type=JS(Event,TEXT("type"));const double Time=JN(Event,TEXT("time"));
        if(Type==TEXT("damage")&&JN(Event,TEXT("targetKind"))>=2&&!Marks.Contains(TEXT("FIRST TOWER DAMAGE")))Marks.Add(TEXT("FIRST TOWER DAMAGE"),Time);
        if(Type==TEXT("tower_destroy")&&!Marks.Contains(TEXT("TOWER DESTROYED")))Marks.Add(TEXT("TOWER DESTROYED"),Time);
        if(Type==TEXT("phase")&&JS(Event,TEXT("reason"))==TEXT("overtime"))Marks.Add(TEXT("OVERTIME"),Time);
        const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*JS(Event,TEXT("cardId"))));
        if(Type==TEXT("damage")&&Card&&Card->spell){const double Play=JN(Event,TEXT("playId"));SpellValues.FindOrAdd(Play)+=JN(Event,TEXT("amount"));if(!SpellTimes.Contains(Play))SpellTimes.Add(Play,Time);}
    }
    double Biggest=0;for(const auto& Pair:SpellValues)if(Pair.Value>Biggest){Biggest=Pair.Value;Marks.Add(TEXT("BIGGEST SPELL"),SpellTimes[Pair.Key]);}
    Biggest=0;
    for(const auto& Sample:Replay->RecordedStates())
    {
        const TArray<TSharedPtr<FJsonValue>>* Entities=nullptr;if(!Sample->TryGetArrayField(TEXT("entities"),Entities))continue;double Push[2]={0,0};
        for(const auto& Value:*Entities)
        {
            const auto Entity=Value->AsObject();if(!Entity||JN(Entity,TEXT("hp"))<=0||JN(Entity,TEXT("kind"))>=2)continue;
            const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*JS(Entity,TEXT("cardId"))));const TSharedPtr<FJsonObject>* Position=nullptr;if(!Card||Card->count<=0||!Entity->TryGetObjectField(TEXT("position"),Position))continue;
            const int32 Team=FMath::Clamp(int32(JN(Entity,TEXT("team"))),0,1);const double Z=JN(*Position,TEXT("z"));if((Team==0&&Z<0)||(Team==1&&Z>0))Push[Team]+=double(Card->cost)/Card->count*JN(Entity,TEXT("hp"))/FMath::Max(1.0,JN(Entity,TEXT("maxHp")));
        }
        const double Value=FMath::Max(Push[0],Push[1]);if(Value>Biggest){Biggest=Value;Marks.Add(TEXT("LARGEST PUSH"),JN(Sample,TEXT("time")));}
    }
    Marks.Add(TEXT("MATCH END"),Replay->TimelineDuration());TArray<TPair<FString,double>> Ordered;for(const auto& Pair:Marks)Ordered.Add(Pair);Ordered.Sort([](const auto& A,const auto& B){return A.Value==B.Value?A.Key<B.Key:A.Value<B.Value;});
    auto* Line=WidgetTree->ConstructWidget<UWrapBox>();Line->SetHorizontalAlignment(HAlign_Center);Line->SetInnerSlotPadding(FVector2D(5,5));Add(Body,Line,2);for(const auto& Pair:Ordered){const FString Label=BookmarkLabel(Pair.Key);auto* Jump=Button(Label,[this,Replay,Time=Pair.Value](){Replay->Seek(Time);MetaClock=.51f;UpdateReplayHUD();});Jump->SetContent(Text(Label,12,Paper));auto Style=Jump->GetStyle();Style.SetNormalPadding(FMargin(8,5));Style.SetPressedPadding(FMargin(8,7,8,3));Jump->SetStyle(Style);Jump->SetToolTipText(FText::FromString(Pair.Key+TEXT(" · ")+Timecode(Pair.Value)));Line->AddChildToWrapBox(Jump);}
}

void URiftUIWidget::ReplayView()
{
    auto* Replay=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();if(!Replay->IsPlaying()){Navigate(TEXT("Replays"));return;}
    auto* Heading=WidgetTree->ConstructWidget<UHorizontalBox>();Add(Heading,Badge(TEXT("RECORDED REPLAY"),FLinearColor(.07f,.11f,.17f,1),16),true);Add(Heading,Button(TEXT("HOME"),[this](){Navigate(TEXT("Home"));}));Add(Heading,Button(TEXT("REPLAYS"),[this](){Navigate(TEXT("Replays"));}));Add(Heading,Button(TEXT("ANALYSIS"),[this](){Navigate(TEXT("Analysis"));},true));
    auto* Top=Panel(Heading,FMargin(10,8));auto* TopSlot=Root->AddChildToCanvas(Top);TopSlot->SetAnchors(FAnchors(.5,0));TopSlot->SetOffsets(FMargin(-355,16,710,68));
    Body=WidgetTree->ConstructWidget<UVerticalBox>();auto* Frame=Panel(Body,FMargin(12,8));auto* DockSlot=Root->AddChildToCanvas(Frame);DockSlot->SetAnchors(FAnchors(.5,1));DockSlot->SetOffsets(FMargin(-356,-158,712,150));
    auto* Controls=WidgetTree->ConstructWidget<UWrapBox>();Controls->SetHorizontalAlignment(HAlign_Center);Controls->SetInnerSlotPadding(FVector2D(4,4));Add(Body,Controls,0);
    ReplaySpeedButtons.Reset();for(float Speed:{0.f,.25f,.5f,1.f,2.f,4.f}){auto* Choice=Button(Speed==0?TEXT("PAUSE"):FString::Printf(TEXT("%.2gx"),Speed),[this,Replay,Speed](){Replay->SetSpeed(Speed);UpdateReplayHUD();},FMath::IsNearlyEqual(Replay->Speed(),Speed));ReplaySpeedButtons.Add(Choice);Controls->AddChildToWrapBox(Choice);}
    Controls->AddChildToWrapBox(Button(TEXT("−5s"),[this,Replay](){Replay->Seek(Replay->Position()-5);MetaClock=.51f;UpdateReplayHUD();}));Controls->AddChildToWrapBox(Button(TEXT("+5s"),[this,Replay](){Replay->Seek(Replay->Position()+5);MetaClock=.51f;UpdateReplayHUD();}));
    ReplayPosition=Text(Timecode(Replay->TimelinePosition())+TEXT(" / ")+Timecode(Replay->TimelineDuration()),13,Blue);ReplayPosition->SetJustification(ETextJustify::Center);ReplayPosition->SetToolTipText(FText::FromString(TEXT("Aether and crowns are shown Player – Opponent.")));Add(Body,ReplayPosition,2);
    ReplaySeek=WidgetTree->ConstructWidget<URiftValueSlider>();ReplaySeek->SetMaxValue(FMath::Max(1.f,Replay->Duration()));ReplaySeek->SetValue(Replay->Position());ReplaySeek->SetSliderBarColor(FLinearColor(.19f,.29f,.40f,1));ReplaySeek->SetSliderHandleColor(Gold);ReplaySeek->Bind([this,Replay](float Position){Replay->Seek(Position);MetaClock=.51f;UpdateReplayHUD();});Add(Body,ReplaySeek,3);Bookmarks();
    auto* Feed=WidgetTree->ConstructWidget<UVerticalBox>();Add(Feed,Text(TEXT("BATTLE EVENTS"),17,Gold),3);Add(Feed,Text(TEXT("Cyan: player  ·  Gold: opponent"),11,Muted),2);
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Scroll->SetClipping(EWidgetClipping::ClipToBounds);Scroll->SetScrollbarThickness(FVector2D(4,4));Rows=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Rows);auto* LedgerSlot=Feed->AddChildToVerticalBox(Scroll);LedgerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));LedgerSlot->SetPadding(FMargin(0,3));
    auto* FeedPanel=Panel(Feed,FMargin(12));FeedPanel->SetClipping(EWidgetClipping::ClipToBounds);auto* FeedSlot=Root->AddChildToCanvas(FeedPanel);FeedSlot->SetAnchors(FAnchors(0,0,0,1));FeedSlot->SetOffsets(FMargin(24,110,280,235));
    MetaClock=.51f;UpdateReplayHUD();
}

void URiftUIWidget::UpdateReplayHUD()
{
    auto* Replay=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();if(!Replay->IsPlaying())return;
    const auto* State=GetWorld()->GetSubsystem<URiftMatchSubsystem>()->ViewState();
    constexpr float Speeds[]={0.f,.25f,.5f,1.f,2.f,4.f};
    for(int32 I=0;I<ReplaySpeedButtons.Num()&&I<UE_ARRAY_COUNT(Speeds);++I){auto* Choice=ReplaySpeedButtons[I].Get();const bool Active=FMath::IsNearlyEqual(Replay->Speed(),Speeds[I]);const FLinearColor Color=Active?FLinearColor(.95f,.64f,.19f,1):FLinearColor(.055f,.115f,.205f,1);if(Choice&&Choice->GetBackgroundColor()!=Color){Choice->SetBackgroundColor(Color);if(auto* Label=Cast<UTextBlock>(Choice->GetContent()))Label->SetColorAndOpacity(Active?FLinearColor(.09f,.05f,.015f,1):FLinearColor(.95f,.94f,.89f,1));}}
    const FString Speed=Replay->Speed()==0?TEXT("PAUSED"):FString::Printf(TEXT("%.2gx"),Replay->Speed());
    const FString PositionText=Timecode(Replay->TimelinePosition())+TEXT(" / ")+Timecode(Replay->TimelineDuration())+FString::Printf(TEXT(" · %s · Aether %.1f–%.1f · Crowns %d–%d"),*Speed,State?State->aether[0]:0,State?State->aether[1]:0,State?State->crowns[0]:0,State?State->crowns[1]:0);
    if(ReplayPosition&&ReplayPosition->GetText().ToString()!=PositionText)ReplayPosition->SetText(FText::FromString(PositionText));
    if(ReplaySeek&&!ReplaySeek->HasMouseCapture()&&!FMath::IsNearlyEqual(ReplaySeek->GetValue(),Replay->Position()))ReplaySeek->SetValue(Replay->Position());
    if(!Rows||MetaClock<=.5f)return;MetaClock=0;if(FMath::IsNearlyEqual(LastReplayLedgerPosition,Replay->TimelinePosition(),1.e-6))return;LastReplayLedgerPosition=Replay->TimelinePosition();Rows->ClearChildren();const auto Events=Replay->EventsNear(Replay->TimelinePosition(),5);
    const int32 First=FMath::Max(0,Events.Num()-18);
    if(Events.IsEmpty()){Add(Rows,Text(TEXT("No recorded events in the last five seconds."),12,Muted),4);return;}
    for(int32 I=First;I<Events.Num();++I)
    {
        const auto Event=Events[I];const FString Type=JS(Event,TEXT("type")),Id=JS(Event,TEXT("cardId")),Reason=Readable(JS(Event,TEXT("reason")));
        const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*Id));const FString Actor=Card?Name(Id):TEXT("Crown Tower");
        const double Time=JN(Event,TEXT("time")),Amount=JN(Event,TEXT("amount"));const int32 Kind=int32(JN(Event,TEXT("targetKind")));
        const FString Target=Kind==int32(rift::EntityKind::Core)?TEXT("the Core"):Kind==int32(rift::EntityKind::Guard)?TEXT("a Guard Tower"):Kind==int32(rift::EntityKind::Building)?TEXT("a building"):TEXT("a troop");
        FString Heading,Detail;
        if(Type==TEXT("damage")){Heading=JS(Event,TEXT("damageKind"))==TEXT("tiebreaker")?TEXT("Tiebreaker drains ")+Target:Actor+TEXT(" hits ")+Target;Detail=FString::Printf(TEXT("%.1f HP removed · %.1f HP left"),Amount,JN(Event,TEXT("hp")));}
        else if(Type==TEXT("attack")){Heading=Actor+TEXT(" attacks");Detail=FString::Printf(TEXT("Listed hit: %.1f damage"),Amount);}
        else if(Type==TEXT("projectile_launch")){Heading=Actor+TEXT(" fires a projectile");Detail=FString::Printf(TEXT("Listed hit: %.1f damage"),Amount);}
        else if(Type==TEXT("card_play")){Heading=(Card?Actor:TEXT("Card"))+(Card&&Card->spell?TEXT(" cast"):TEXT(" deployed"));Detail=FString::Printf(TEXT("%.1f Aether spent"),Amount);}
        else if(Type==TEXT("entity_spawn")){Heading=(Card?Actor:TEXT("Unit"))+TEXT(" joins the battle");Detail=TEXT("Deployment recorded");}
        else if(Type==TEXT("death")){Heading=Card?Actor+TEXT(" defeats ")+Target:Readable(Target)+TEXT(" removed");Detail=TEXT("Defeat recorded");}
        else if(Type==TEXT("tower_destroy")){Heading=Kind==int32(rift::EntityKind::Core)?TEXT("Core falls"):TEXT("Guard Tower falls");const double Crowns=JN(Event,TEXT("crowns"));Detail=FString::Printf(TEXT("%.0f %s earned"),Crowns,Crowns==1?TEXT("crown"):TEXT("crowns"));}
        else if(Type==TEXT("core_activate")){Heading=TEXT("Core awakens");Detail=TEXT("Guard Tower loss activates its Core");}
        else if(Type==TEXT("slow")){Heading=Actor+TEXT(" slows a troop");Detail=FString::Printf(TEXT("%.0f%% movement slow · %.1fs"),Amount*100,FMath::Max(0.0,JN(Event,TEXT("until"))-Time));}
        else if(Type==TEXT("stun")){Heading=Actor+TEXT(" stuns a troop");Detail=FString::Printf(TEXT("%.1fs stun"),FMath::Max(0.0,JN(Event,TEXT("until"))-Time));}
        else if(Type==TEXT("aura")){Heading=Actor+TEXT(" pulses electricity");Detail=FString::Printf(TEXT("%.1f-tile radius"),Amount);}
        else if(Type==TEXT("hazard_tick")){Heading=TEXT("Lingering damage zone pulses");Detail=TEXT("Troops in the zone can take damage");}
        else if(Type==TEXT("aether_leak")){Heading=TEXT("Aether bank full");Detail=FString::Printf(TEXT("%.2f regenerated Aether wasted"),Amount);}
        else if(Type==TEXT("aether_grant")){Heading=TEXT("Training Aether changed");Detail=FString::Printf(TEXT("%+.1f Aether · bank %.1f"),Amount,JN(Event,TEXT("aetherAfter")));}
        else if(Type==TEXT("tower_edit")){Heading=TEXT("Training tower HP changed");Detail=FString::Printf(TEXT("Tower HP: %.0f"),JN(Event,TEXT("hp")));}
        else if(Type==TEXT("clear_field")){Heading=TEXT("Training field cleared");Detail=TEXT("Non-crown units removed");}
        else if(Type==TEXT("target_pull")||Type==TEXT("building_pull")){Heading=TEXT("Defender draws an attacker");Detail=Card?Actor:TEXT("Target change recorded");}
        else if(Type==TEXT("match_start")){Heading=TEXT("Battle begins");Detail=TEXT("Opening decks loaded");}
        else if(Type==TEXT("match_end")){Heading=TEXT("Battle ends");Detail=Reason;}
        else if(Type==TEXT("phase")){Heading=TEXT("Phase changes");Detail=Reason;}
        else if(Type==TEXT("ai_decision")){Heading=TEXT("AI chooses its next move");Detail=Reason;}
        else{Heading=Readable(Type);Detail=Reason.IsEmpty()?FString::Printf(TEXT("Recorded amount: %.1f"),Amount):Reason;}
        FString Raw;FJsonSerializer::Serialize(Event.ToSharedRef(),TJsonWriterFactory<>::Create(&Raw));
        auto* Entry=WidgetTree->ConstructWidget<UVerticalBox>();auto* Title=Text(Timecode(Time)+TEXT(" · ")+Heading,12,JN(Event,TEXT("team"))==0?Blue:Gold);Title->SetToolTipText(FText::FromString(Raw));Add(Entry,Title,0);if(!Detail.IsEmpty()){auto* Description=Text(Detail,11,Muted);Description->SetToolTipText(FText::FromString(Raw));Add(Entry,Description,0);}Add(Rows,Entry,5);
    }
}

void URiftUIWidget::MatchAnalysis()
{
    auto* Replay=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();const auto Result=Replay->CurrentAnalysis();
    if(!Result){Add(Body,Text(TEXT("Open a recorded replay to inspect its match analysis."),18,Paper));Add(Body,Button(TEXT("VIEW REPLAYS"),[this](){Navigate(TEXT("Replays"));},true));return;}Replay->SetSpeed(0);
    const TArray<TSharedPtr<FJsonValue>>* Teams=nullptr;if(!Result->TryGetArrayField(TEXT("teams"),Teams)||Teams->Num()!=2)return;
    const int32 Winner=int32(JN(Result,TEXT("winner"),-1));const bool Finished=int32(JN(Result,TEXT("phase")))==int32(rift::Phase::Finished);
    auto* Header=Row();Add(Header,Text(Finished?(Winner==0?TEXT("VICTORY"):Winner==1?TEXT("DEFEAT"):TEXT("DRAW")):TEXT("SESSION REVIEW"),30,Gold),true);Add(Header,Badge(Timecode(Replay->TimelineDuration()),FLinearColor(.05f,.12f,.18f,1),16));Add(Header,Button(TEXT("WATCH REPLAY"),[this](){Navigate(TEXT("ReplayView"));},true));
    Add(Body,Text(FString::Printf(TEXT("%.0f — %.0f CROWNS"),JN((*Teams)[0]->AsObject(),TEXT("crowns")),JN((*Teams)[1]->AsObject(),TEXT("crowns"))),20,Blue),3);
    FString Reason=JS(Result,TEXT("resultReason"));Reason.ReplaceInline(TEXT("_"),TEXT(" "));if(!Reason.IsEmpty())Add(Body,Text(Reason.Left(1).ToUpper()+Reason.Mid(1),14,Muted),3);

    const auto Samples=Replay->RecordedStates();double WeightedCost[2]={0,0},Weight=0,Advantage[2]={0,0},LargestPush[2]={0,0};
    for(int32 I=0;I<Samples.Num();++I)
    {
        const auto Sample=Samples[I];const TArray<TSharedPtr<FJsonValue>>* Sides=nullptr;if(!Sample->TryGetArrayField(TEXT("teams"),Sides)||Sides->Num()!=2)continue;
        const double Dt=I+1<Samples.Num()?FMath::Max(0.0,JN(Samples[I+1],TEXT("time"))-JN(Sample,TEXT("time"))):0;Weight+=Dt;double Banks[2]={0,0};
        for(int32 T=0;T<2;++T){const auto Side=(*Sides)[T]->AsObject();Banks[T]=JN(Side,TEXT("aether"));const TArray<TSharedPtr<FJsonValue>>* Hand=nullptr;if(Side->TryGetArrayField(TEXT("hand"),Hand)){double Cost=0;for(const auto& Id:*Hand)if(const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*Id->AsString())))Cost+=Card->cost;WeightedCost[T]+=Cost/FMath::Max(1,Hand->Num())*Dt;}}
        Advantage[0]=FMath::Max(Advantage[0],Banks[0]-Banks[1]);Advantage[1]=FMath::Max(Advantage[1],Banks[1]-Banks[0]);
        const TArray<TSharedPtr<FJsonValue>>* Entities=nullptr;if(Sample->TryGetArrayField(TEXT("entities"),Entities))
        {
            double Push[2]={0,0};for(const auto& Value:*Entities){const auto Entity=Value->AsObject();if(!Entity||JN(Entity,TEXT("hp"))<=0||JN(Entity,TEXT("kind"))>=2)continue;const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*JS(Entity,TEXT("cardId"))));const TSharedPtr<FJsonObject>* Position=nullptr;if(!Card||Card->count<=0||!Entity->TryGetObjectField(TEXT("position"),Position))continue;const int32 Team=FMath::Clamp(int32(JN(Entity,TEXT("team"))),0,1);const double Z=JN(*Position,TEXT("z"));if((Team==0&&Z<0)||(Team==1&&Z>0))Push[Team]+=double(Card->cost)/Card->count*JN(Entity,TEXT("hp"))/FMath::Max(1.0,JN(Entity,TEXT("maxHp")));}for(int32 T=0;T<2;++T)LargestPush[T]=FMath::Max(LargestPush[T],Push[T]);
        }
    }

    for(int32 T=0;T<2;++T)
    {
        const auto Side=(*Teams)[T]->AsObject();auto* Team=WidgetTree->ConstructWidget<UVerticalBox>();auto* Title=Row(Team);Add(Title,Text(T==0?TEXT("PLAYER"):TEXT("OPPONENT"),23,T==0?Blue:Gold),true);const double Crowns=JN(Side,TEXT("crowns"));Add(Title,Badge(FString::Printf(TEXT("%.0f %s"),Crowns,Crowns==1?TEXT("CROWN"):TEXT("CROWNS")),FLinearColor(.05f,.10f,.17f,1),14));
        auto* Metrics=WidgetTree->ConstructWidget<UWrapBox>();Metrics->SetInnerSlotPadding(FVector2D(8,8));Add(Team,Metrics,4);
        for(const FString& Metric:TArray<FString>{FString::Printf(TEXT("%.1f AETHER SPENT"),JN(Side,TEXT("spent"))),FString::Printf(TEXT("%.1f AETHER LEAKED"),JN(Side,TEXT("leaked"))),FString::Printf(TEXT("%.2f PEAK BANK LEAD"),Advantage[T]),FString::Printf(TEXT("%.2f AVG HAND COST"),Weight?WeightedCost[T]/Weight:0)})Metrics->AddChildToWrapBox(Badge(Metric,FLinearColor(.055f,.105f,.165f,1),12));
        Add(Team,Text(FString::Printf(TEXT("Largest surviving push: %.2f Aether-equivalent · card cost weighted by surviving HP, on the enemy side."),LargestPush[T]),13,Muted),3);
        const TArray<TSharedPtr<FJsonValue>>* Deck=nullptr;if(Side->TryGetArrayField(TEXT("deck"),Deck))
        {
            auto* Cards=WidgetTree->ConstructWidget<UWrapBox>();Cards->SetInnerSlotPadding(FVector2D(7,7));Add(Team,Cards,5);for(const auto& Value:*Deck){const FString Id=Value->AsString();auto* Face=Button(TEXT(""),[this,Id](){DetailCard=Id;Navigate(TEXT("CardDetail"));});Face->SetContent(Illustration(Id,48));Face->SetToolTipText(FText::FromString(Name(Id)));Cards->AddChildToWrapBox(Face);}
        }
        const TSharedPtr<FJsonObject>* Telemetry=nullptr;
        if(Side->TryGetObjectField(TEXT("telemetry"),Telemetry))
        {
            FString Most,Least;double Best=-1,Worst=DBL_MAX;TArray<TPair<FString,TSharedPtr<FJsonObject>>> Played;
            for(const auto& Pair:(*Telemetry)->Values){const auto Card=Pair.Value->AsObject();if(!Card)continue;const double Spent=JN(Card,TEXT("spent"));if(Spent>0){const double Value=(JN(Card,TEXT("troopDamage"))+JN(Card,TEXT("towerDamage"))+JN(Card,TEXT("buildingDamage")))/Spent;if(Value>Best){Best=Value;Most=Pair.Key;}if(Value<Worst){Worst=Value;Least=Pair.Key;}}if(JN(Card,TEXT("plays"))>0||JN(Card,TEXT("spawns"))>0)Played.Emplace(Pair.Key,Card);}
            Add(Team,Text(Most.IsEmpty()?TEXT("No paid card damage efficiency is available for this session."):TEXT("Best damage / Aether: ")+Name(Most)+TEXT("  ·  Lowest: ")+Name(Least),14,Paper),5);
            Played.Sort([](const auto& A,const auto& B){return JN(A.Value,TEXT("troopDamage"))+JN(A.Value,TEXT("towerDamage"))+JN(A.Value,TEXT("buildingDamage"))>JN(B.Value,TEXT("troopDamage"))+JN(B.Value,TEXT("towerDamage"))+JN(B.Value,TEXT("buildingDamage"));});
            auto* Table=WidgetTree->ConstructWidget<UVerticalBox>();
            auto Cell=[&](UHorizontalBox* Line,const FString& Value,float Width,FLinearColor Color,bool Right=false){auto* Label=Text(Value,12,Color);Label->SetAutoWrapText(false);Label->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);Label->SetJustification(Right?ETextJustify::Right:ETextJustify::Left);Label->SetToolTipText(FText::FromString(Value));auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(Width);Size->SetClipping(EWidgetClipping::ClipToBounds);Size->SetContent(Label);Add(Line,Size,false,7);};
            const TArray<float> Widths={70,110,110,110,65,70,80,140};
            const TArray<FString> Labels={TEXT("PLAYS"),TEXT("TROOP DMG"),TEXT("TOWER DMG"),TEXT("BLDG DMG"),TEXT("KILLS"),TEXT("DEATHS"),TEXT("SPENT"),TEXT("DMG / AETHER")};
            auto* Head=Row(Table);Cell(Head,TEXT("CARD"),180,Muted);for(int32 I=0;I<Labels.Num();++I)Cell(Head,Labels[I],Widths[I],Muted,true);
            int32 RowIndex=0;
            for(const auto& Pair:Played)
            {
                const auto Card=Pair.Value;const double Spent=JN(Card,TEXT("spent"));auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();Cell(Line,Name(Pair.Key),180,Paper);int32 ColumnIndex=0;for(const TCHAR* Key:{TEXT("plays"),TEXT("troopDamage"),TEXT("towerDamage"),TEXT("buildingDamage"),TEXT("kills"),TEXT("deaths"),TEXT("spent")})Cell(Line,FString::Printf(TEXT("%.0f"),JN(Card,Key)),Widths[ColumnIndex++],Paper,true);Cell(Line,Spent>0?FString::Printf(TEXT("%.1f"),(JN(Card,TEXT("troopDamage"))+JN(Card,TEXT("towerDamage"))+JN(Card,TEXT("buildingDamage")))/Spent):TEXT("—"),Widths.Last(),Blue,true);
                auto* Stripe=WidgetTree->ConstructWidget<UBorder>();Stripe->SetBrush(FSlateColorBrush(RowIndex++%2?FLinearColor(.022f,.046f,.075f,1):FLinearColor(.032f,.061f,.092f,1)));Stripe->SetPadding(FMargin(0,5));Stripe->SetContent(Line);Add(Table,Stripe,0);
            }
            auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Scroll->SetOrientation(Orient_Horizontal);Scroll->SetScrollbarThickness(FVector2D(5,5));Scroll->AddChild(Table);Add(Team,Scroll,5);
            if(bReplayDetailsExpanded)for(const auto& Pair:Played){Add(Team,Text(Name(Pair.Key)+TEXT(" · ALL RECORDED METRICS"),16,Gold),5);ShowJSON(Pair.Value,Team);}
        }
        Add(Body,Panel(Team,FMargin(18)),9);
    }

    const auto Events=Replay->EventsNear(Replay->TimelineDuration(),Replay->TimelineDuration()+1);TMap<FString,double> Kills,Costs;
    for(const auto& Event:Events)
    {
        const int32 Team=FMath::Clamp(int32(JN(Event,TEXT("team"))),0,1);const FString Key=FString::Printf(TEXT("%d:%.0f"),Team,JN(Event,TEXT("playId")));const FString Type=JS(Event,TEXT("type"));
        if(Type==TEXT("card_play"))Costs.Add(Key,JN(Event,TEXT("amount")));if(Type==TEXT("death")){const TSharedPtr<FJsonObject>* Position=nullptr;if(Event->TryGetObjectField(TEXT("position"),Position)){const double Z=JN(*Position,TEXT("z"));if((Team==0&&Z>0)||(Team==1&&Z<0))Kills.FindOrAdd(Key)+=JN(Event,TEXT("targetCost"));}}
    }
    double Trade=0;for(const auto& Pair:Kills)Trade=FMath::Max(Trade,Pair.Value-Costs.FindRef(Pair.Key));Add(Body,Text(FString::Printf(TEXT("Best realized defensive trade: +%.2f Aether · enemy deployment value killed on the defending half, minus the killing card's deployment cost."),Trade),15,Paper),7);
    Add(Body,Text(TEXT("Damage records actual HP removed and excludes overkill. Ratios divide recorded total card damage by paid Aether spent. Free-only developer cards show no ratio; mixed sandbox spawns can distort efficiency comparisons."),13,Muted),4);
    Add(Body,Button(bReplayDetailsExpanded?TEXT("HIDE ALL RECORDED METRICS"):TEXT("SHOW ALL RECORDED METRICS"),[this](){bReplayDetailsExpanded=!bReplayDetailsExpanded;Navigate(TEXT("Analysis"));}),6);
    for(int32 Metric=0;Metric<2;++Metric)
    {
        auto* ChartPanel=WidgetTree->ConstructWidget<UVerticalBox>();Add(ChartPanel,Text(Metric==0?TEXT("AETHER THROUGH THE MATCH"):TEXT("CROWN TOWER HP THROUGH THE MATCH"),20,Gold),5);
        auto* Chart=CreateWidget<URiftChartWidget>(GetGameInstance(),URiftChartWidget::StaticClass());Chart->Caption=TEXT("Seconds · cyan player · gold opponent");
        for(const auto& Sample:Samples)
        {
            const double Time=JN(Sample,TEXT("time"));double Values[2]={0,0};const TArray<TSharedPtr<FJsonValue>>* Array=nullptr;
            if(Metric==0&&Sample->TryGetArrayField(TEXT("teams"),Array)&&Array->Num()==2)for(int32 T=0;T<2;++T)Values[T]=JN((*Array)[T]->AsObject(),TEXT("aether"));
            if(Metric==1&&Sample->TryGetArrayField(TEXT("entities"),Array))for(const auto& Value:*Array){const auto Entity=Value->AsObject();if(JN(Entity,TEXT("kind"))>=2)Values[FMath::Clamp(int32(JN(Entity,TEXT("team"))),0,1)]+=JN(Entity,TEXT("hp"));}
            Chart->SeriesA.Add(FVector2D(Time,Values[0]));Chart->SeriesB.Add(FVector2D(Time,Values[1]));
        }
        auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetHeightOverride(210);Size->SetContent(Chart);Add(ChartPanel,Size,4);Add(Body,Panel(ChartPanel,FMargin(16)),9);
    }
}
