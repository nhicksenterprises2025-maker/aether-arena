#include "RiftUIWidget.h"
#include "RiftMatchSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/World.h"

namespace
{
    const FLinearColor TrainingGold(.90f,.73f,.39f,1.f);
    const FLinearColor TrainingMuted(.64f,.72f,.81f,1.f);
    const FLinearColor TrainingActive(.19f,.32f,.47f,1.f);
    const FLinearColor TrainingControl(.10f,.15f,.22f,1.f);
    FString TrainingString(const std::string& Value){return UTF8_TO_TCHAR(Value.c_str());}

    void TrainingButtonLabel(UWidget* Widget,const FString& Label)
    {
        if(auto* Text=Cast<UTextBlock>(Widget)){Text->SetText(FText::FromString(Label));return;}
        if(auto* Panel=Cast<UPanelWidget>(Widget))for(int32 I=0;I<Panel->GetChildrenCount();++I)TrainingButtonLabel(Panel->GetChildAt(I),Label);
    }
}

void URiftUIWidget::Developer(UVerticalBox* Target)
{
    auto* Match=GetWorld()->GetSubsystem<URiftMatchSubsystem>();
    auto* Dev=GetWorld()->GetSubsystem<URiftDeveloperSubsystem>();
    auto* AI=GetWorld()->GetSubsystem<URiftAISubsystem>();
    const auto* State=Match->ViewState();
    const FString PreviousCard=DevCard?DevCard->GetSelectedOption():TEXT("ironclad");
    const FString PreviousTeam=DevTeam?DevTeam->GetSelectedOption():TEXT("Friendly");
    const FString PreviousTower=DevTower?DevTower->GetSelectedOption():TEXT("");

    auto Section=[&](const FString& Heading)
    {
        auto* Divider=WidgetTree->ConstructWidget<UBorder>();
        Divider->SetBrushColor(FLinearColor(.26f,.34f,.43f,.5f));
        auto* Height=WidgetTree->ConstructWidget<USizeBox>();Height->SetHeightOverride(1);Height->SetContent(Divider);
        Add(Target,Height,8);Add(Target,Text(Heading,11,TrainingGold),4);
    };
    auto* Head=Row(Target);Add(Head,Text(TEXT("TRAINING LAB"),17,TrainingGold),true);
    Add(Head,Button(TEXT("CLOSE"),[this](){ToggleDeveloper();}),false,0);
    TrainingStatus=Text(TEXT(""),12,TrainingMuted);Add(Target,TrainingStatus,3);
    Add(Target,Text(TEXT("Sandbox actions are recorded in the replay."),11,TrainingMuted),4);

    Section(TEXT("SIMULATION SPEED"));
    const float Speeds[]={0.f,.25f,.5f,1.f,2.f,4.f};
    TrainingSpeedButtons.Reset();
    for(int32 RowIndex=0;RowIndex<2;++RowIndex)
    {
        auto* SpeedRow=Row(Target);
        for(int32 Column=0;Column<3;++Column)
        {
            const float Speed=Speeds[RowIndex*3+Column];
            auto* Control=Button(Speed==0?TEXT("PAUSE"):Speed==.25f?TEXT("¼×"):Speed==.5f?TEXT("½×"):FString::Printf(TEXT("%.0f×"),Speed),[this,Match,Speed](){Match->SetSpeed(Speed);UpdateTrainingReadout();});
            TrainingSpeedButtons.Add(Control);Add(SpeedRow,Control,true,2);
        }
    }

    Section(TEXT("AETHER RESERVES"));
    for(int32 Team=0;Team<2;++Team)
    {
        Add(Target,Text(Team==0?TEXT("FRIENDLY"):TEXT("ENEMY"),11,TrainingMuted),2);
        auto* Bank=Row(Target);
        Add(Bank,Button(TEXT("−1"),[Dev,Team](){Dev->ChangeAether(Team,-1);}),true,2);
        Add(Bank,Button(TEXT("+1"),[Dev,Team](){Dev->ChangeAether(Team,1);}),true,2);
        Add(Bank,Button(TEXT("MAX"),[Dev,Team](){Dev->ChangeAether(Team,0,true);}),true,2);
    }

    Section(TEXT("SPAWN A CARD"));
    TArray<FString> IDs;for(const auto& Card:rift::Cards())IDs.Add(TrainingString(Card.id));
    DevTeam=Combo({TEXT("Friendly"),TEXT("Enemy")},PreviousTeam);Add(Target,DevTeam,3);
    DevCard=Combo(IDs,PreviousCard);Add(Target,DevCard,3);
    SpawnArmButton=Button(TEXT("ARM SPAWN"),[this](){bSpawnArmed=!bSpawnArmed;HandIndex=-1;Say(bSpawnArmed?TEXT("Choose an arena tile to spawn. Right click cancels."):TEXT("Sandbox spawn cancelled."));UpdateTrainingReadout();});
    Add(Target,SpawnArmButton,4);

    Section(TEXT("AI CONTROLS"));
    DevStyle=Combo({TEXT("beatdown"),TEXT("aggro"),TEXT("control"),TEXT("cycle"),TEXT("split"),TEXT("spell_cycle"),TEXT("counter")},State?TrainingString(State->ai[1].style):TEXT("control"));Add(Target,DevStyle,3);
    Add(Target,Button(TEXT("APPLY AI STYLE"),[this,AI](){if(AI->SetStyle(1,DevStyle->GetSelectedOption()))Say(TEXT("Enemy AI style applied."));UpdateTrainingReadout();}),4);
    EnemyAIButton=Button(TEXT("ENEMY AI ON"),[this,Match,AI](){if(const auto* Current=Match->ViewState())AI->SetEnabled(1,!Current->ai[1].enabled);UpdateTrainingReadout();});Add(Target,EnemyAIButton,3);
    FriendlyAIButton=Button(TEXT("FRIENDLY AI OFF"),[this,Match,AI](){if(const auto* Current=Match->ViewState())AI->SetEnabled(0,!Current->ai[0].enabled);UpdateTrainingReadout();});Add(Target,FriendlyAIButton,3);
    DevReadout=Text(AI->Readout(),12,TrainingMuted);Add(Target,DevReadout,6);

    Section(TEXT("TOWER HEALTH"));
    TArray<FString> Towers;
    if(State)for(const auto& Entity:State->entities)if(Entity.kind==rift::EntityKind::Guard||Entity.kind==rift::EntityKind::Core)
        Towers.Add(FString::Printf(TEXT("%llu · %s %s"),Entity.id,Entity.team==rift::Team::Player?TEXT("Friendly"):TEXT("Enemy"),Entity.kind==rift::EntityKind::Core?TEXT("Core"):Entity.lane<0?TEXT("Left Guard"):TEXT("Right Guard")));
    DevTower=Combo(Towers,PreviousTower);Add(Target,DevTower,3);
    TowerHP=Edit(TEXT(""),TEXT("Current tower HP"));Add(Target,TowerHP,3);LastTrainingTower.Empty();
    Add(Target,Button(TEXT("SET TOWER HP"),[this,Dev]()
    {
        const FString Value=TowerHP->GetText().ToString().TrimStartAndEnd();
        const float HP=FCString::Atof(*Value);
        if(!Value.IsNumeric()||!FMath::IsFinite(HP)){Say(TEXT("Enter a valid tower health value."));return;}
        const int64 Id=FCString::Atoi64(*DevTower->GetSelectedOption());
        Say(Dev->SetTowerHP(Id,HP)?TEXT("Tower edit recorded."):TEXT("This tower is destroyed or the health value is invalid."));
        LastTrainingTower.Empty();UpdateTrainingReadout();
    }),4);

    Section(TEXT("BATTLE OVERLAYS"));
    const TArray<TPair<FString,bool*>> Overlays={{TEXT("Paths"),&Dev->ShowPaths},{TEXT("Front / rear sight"),&Dev->ShowSight},{TEXT("Attack range"),&Dev->ShowRanges},{TEXT("Target lines"),&Dev->ShowTargets},{TEXT("Hard locks"),&Dev->ShowHardLocks},{TEXT("Tile coordinates"),&Dev->ShowTiles}};
    for(const auto& Overlay:Overlays)
    {
        bool* Flag=Overlay.Value;
        Add(Target,Button(Overlay.Key+(*Flag?TEXT(" · ON"):TEXT(" · OFF")),[this,Flag](){*Flag=!*Flag;Navigate(TEXT("Battle"));},*Flag),3);
    }
    Section(TEXT("TOOLS"));
    Add(Target,Button(TEXT("CLEAR BATTLEFIELD"),[this,Dev](){Dev->ClearBattlefield();Say(TEXT("Troops and buildings cleared. Tower health is preserved."));}),3);
    Add(Target,Button(TEXT("META VALIDATION"),[this](){Tab=TEXT("Validation");Navigate(TEXT("Meta"));}),3);
    UpdateTrainingReadout();
}

void URiftUIWidget::UpdateTrainingReadout()
{
    if(!TrainingStatus&&!DevReadout)return;
    auto* Match=GetWorld()->GetSubsystem<URiftMatchSubsystem>();
    const auto* State=Match->ViewState();if(!State)return;
    int32 Troops=0,Buildings=0;for(const auto& Entity:State->entities)if(!Entity.dead){Troops+=Entity.kind==rift::EntityKind::Troop;Buildings+=Entity.kind==rift::EntityKind::Building;}
    if(TrainingStatus)TrainingStatus->SetText(FText::FromString(FString::Printf(TEXT("%s  ·  %d troops  ·  %d buildings\nAether  %.1f friendly  /  %.1f enemy"),Match->GetSpeed()==0?TEXT("PAUSED"):*FString::Printf(TEXT("%.2g× speed"),Match->GetSpeed()),Troops,Buildings,State->aether[0],State->aether[1])));
    if(DevReadout)DevReadout->SetText(FText::FromString(GetWorld()->GetSubsystem<URiftAISubsystem>()->Readout()));
    if(EnemyAIButton)TrainingButtonLabel(EnemyAIButton,State->ai[1].enabled?TEXT("ENEMY AI ON"):TEXT("ENEMY AI OFF"));
    if(FriendlyAIButton)TrainingButtonLabel(FriendlyAIButton,State->ai[0].enabled?TEXT("FRIENDLY AI ON"):TEXT("FRIENDLY AI OFF"));
    if(SpawnArmButton)TrainingButtonLabel(SpawnArmButton,bSpawnArmed?TEXT("CANCEL SPAWN"):TEXT("ARM SPAWN"));
    const float Speeds[]={0.f,.25f,.5f,1.f,2.f,4.f};
    for(int32 I=0;I<TrainingSpeedButtons.Num();++I)if(TrainingSpeedButtons[I])TrainingSpeedButtons[I]->SetBackgroundColor(FMath::IsNearlyEqual(Match->GetSpeed(),Speeds[I])?TrainingActive:TrainingControl);
    if(DevTower&&TowerHP)
    {
        const FString Choice=DevTower->GetSelectedOption();
        // Selection changes fill the chosen tower's real HP; ordinary live ticks
        // leave text under edit intact so a paused fixture can enter an exact value.
        if(Choice!=LastTrainingTower)
        {
            const rift::EntityId Id=static_cast<rift::EntityId>(FCString::Atoi64(*Choice));
            for(const auto& Entity:State->entities)if(Entity.id==Id)
            {
                TowerHP->SetText(FText::FromString(FString::Printf(TEXT("%d"),FMath::CeilToInt(Entity.hp))));
                TowerHP->SetHintText(FText::FromString(FString::Printf(TEXT("0–%.0f HP"),Entity.maxHp)));
                TowerHP->SetToolTipText(FText::FromString(FString::Printf(TEXT("Current %.0f / %.0f HP%s"),Entity.hp,Entity.maxHp,Entity.dead?TEXT(" · destroyed"):TEXT(""))));
                break;
            }
            LastTrainingTower=Choice;
        }
    }
}
