#include "RiftUIWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/WrapBox.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"

namespace
{
    const FLinearColor Gold(.82f,.65f,.36f,1), Paper(.93f,.93f,.88f,1), Muted(.59f,.67f,.77f,1);
    const FLinearColor Blue(.32f,.74f,.94f,1), Purple(.20f,.075f,.34f,1), Green(.075f,.22f,.17f,1);
    FString FS(const std::string& Value){return UTF8_TO_TCHAR(Value.c_str());}
    FString CardType(const rift::Card& Card){return Card.spell?TEXT("SPELL"):Card.building?TEXT("BUILDING"):Card.flying?TEXT("AIR TROOP"):TEXT("GROUND TROOP");}
    FString CardName(const std::string& Id){const auto* Card=rift::FindCard(Id);return Card?FS(Card->name):FS(Id);}
}

void URiftUIWidget::Loadout()
{
    auto* Profile=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();
    if(!bDraftLoaded)
    {
        for(int32 I=0;I<Profile->Presets.Num();++I)if(Profile->Presets[I].Id==Profile->ActivePreset)PresetIndex=I;
        if(Profile->Presets.IsValidIndex(PresetIndex)){Draft=Profile->Presets[PresetIndex].Cards;DraftName=Profile->Presets[PresetIndex].Name;}
        bDraftLoaded=true;
    }
    std::vector<std::string> Values;for(const auto& Id:Draft)Values.push_back(TCHAR_TO_UTF8(*Id));
    const auto Analysis=rift::AnalyzeDeck(Values);
    const bool Valid=URiftProfileSubsystem::ValidDeck(Draft);

    auto* Title=Row();Add(Title,Text(TEXT("YOUR BATTLE DECK"),27,Paper),true);
    Add(Title,Badge(FString::Printf(TEXT("%.2f AVG AETHER"),Analysis.averageCost),Purple,15));
    Add(Body,Text(TEXT("Choose eight distinct cards. Your first four slots form the opening hand."),15,Muted),3);
    auto* Presets=Row();
    for(int32 I=0;I<Profile->Presets.Num();++I)
    {
        auto* Preset=Button(Profile->Presets[I].Name,[this,I](){SelectPreset(I);},I==PresetIndex);
        Preset->SetToolTipText(FText::FromString(Profile->Presets[I].Id==Profile->ActivePreset?TEXT("This preset is selected for battle."):TEXT("Edit this saved preset.")));
        Add(Presets,Preset,true,3);
    }

    auto* Deck=WidgetTree->ConstructWidget<UVerticalBox>();
    auto* DeckHeading=Row(Deck);
    Add(DeckHeading,Text(TEXT("CURRENT DECK"),19,Gold),true);
    Add(DeckHeading,Badge(FString::Printf(TEXT("%d / 8 CARDS"),Draft.Num()),Valid?Green:FLinearColor(.28f,.085f,.035f,1),14));
    Add(Deck,Text(TEXT("DECK NAME"),10,Muted),3);PresetName=Edit(DraftName,TEXT("Preset name"));Add(Deck,PresetName,5);
    auto* Slots=WidgetTree->ConstructWidget<UWrapBox>();Slots->SetInnerSlotPadding(FVector2D(10,10));Add(Deck,Slots,5);
    for(int32 I=0;I<8;++I)
    {
        auto* DeckSlot=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* Position=Text(FString::Printf(TEXT("%d  ·  %s"),I+1,I<4?TEXT("OPENING"):TEXT("QUEUED")),10,I<4?Blue:Muted);
        Position->SetJustification(ETextJustify::Center);Add(DeckSlot,Position,3);
        if(Draft.IsValidIndex(I))
        {
            const FString Id=Draft[I];auto* Card=CardButton(Id,132,[this,Id](){ToggleDraft(Id);},true);
            Card->SetToolTipText(FText::FromString(TEXT("Remove ")+CardName(TCHAR_TO_UTF8(*Id))+TEXT(" from the draft.")));
            Add(DeckSlot,Card,0);
        }
        else
        {
            auto* Empty=WidgetTree->ConstructWidget<UVerticalBox>();auto* Mark=Text(TEXT("+"),44,Blue);Mark->SetJustification(ETextJustify::Center);Add(Empty,Mark,10);
            auto* Caption=Text(TEXT("EMPTY SLOT"),12,Muted);Caption->SetJustification(ETextJustify::Center);Add(Empty,Caption,6);
            auto* Frame=Panel(Empty,FMargin(12),FLinearColor(.025f,.045f,.075f,1));
            auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(132);Size->SetHeightOverride(215);Size->SetContent(Frame);Add(DeckSlot,Size,0);
        }
        Slots->AddChildToWrapBox(DeckSlot);
    }
    Add(Deck,Text(Valid?TEXT("Deck ready. Click a selected card to remove it, or save this loadout for your next battle."):FString::Printf(TEXT("Add %d more unique %s to complete this deck."),FMath::Max(0,8-Draft.Num()),8-Draft.Num()==1?TEXT("card"):TEXT("cards")),14,Valid?Blue:Gold),5);
    auto* Actions=Row(Deck);
    auto* Save=Button(TEXT("SAVE & SELECT"),[this,Profile](){if(Profile->SaveDeck(PresetIndex,PresetName->GetText().ToString(),Draft)&&Profile->SelectPreset(PresetIndex)){DraftName=PresetName->GetText().ToString();Navigate(TEXT("Loadout"));Say(TEXT("Deck saved and selected for the next battle."));}else Say(Profile->LastError);},true);
    Save->SetIsEnabled(Valid);Save->SetToolTipText(FText::FromString(Valid?TEXT("Save this preset and select it for battle."):TEXT("A battle deck needs exactly eight different cards.")));Add(Actions,Save,true);
    auto* Test=Button(TEXT("TEST DECK"),[this,Profile](){if(!Profile->SaveDeck(PresetIndex,PresetName->GetText().ToString(),Draft)||!Profile->SelectPreset(PresetIndex)){Say(Profile->LastError);return;}bMenuPausedMatch=false;HandIndex=-1;bSpawnArmed=false;LastPhase.Empty();LastSurge=1;GetWorld()->GetSubsystem<URiftMatchSubsystem>()->StartMatch(true);bDev=true;Navigate(TEXT("Battle"));});Test->SetIsEnabled(Valid);Add(Actions,Test,true);
    Add(Actions,Button(TEXT("RESET DRAFT"),[this](){if(PresetName)DraftName=PresetName->GetText().ToString();Draft=URiftProfileSubsystem::DefaultDeck();Navigate(TEXT("Loadout"));}),true);
    Add(Body,Panel(Deck,FMargin(18)),9);

    auto* Readout=WidgetTree->ConstructWidget<UVerticalBox>();
    auto* Metrics=WidgetTree->ConstructWidget<UWrapBox>();Metrics->SetInnerSlotPadding(FVector2D(7,7));Add(Readout,Metrics,2);
    for(const FString& Metric:TArray<FString>{FString::Printf(TEXT("%d AIR %s"),Analysis.antiAir,Analysis.antiAir==1?TEXT("ANSWER"):TEXT("ANSWERS")),FString::Printf(TEXT("%d WIN %s"),Analysis.winConditions,Analysis.winConditions==1?TEXT("CONDITION"):TEXT("CONDITIONS")),FString::Printf(TEXT("%d %s"),Analysis.spells,Analysis.spells==1?TEXT("SPELL"):TEXT("SPELLS")),FString::Printf(TEXT("%d %s"),Analysis.buildings,Analysis.buildings==1?TEXT("BUILDING"):TEXT("BUILDINGS"))})Metrics->AddChildToWrapBox(Badge(Metric,FLinearColor(.055f,.105f,.165f,1),12));
    FString Archetypes,PrimaryArchetypes;int32 ArchetypeCount=0;for(const auto& Value:Analysis.archetypes){if(!Archetypes.IsEmpty())Archetypes+=TEXT(" / ");Archetypes+=FS(Value);if(ArchetypeCount++<2){if(!PrimaryArchetypes.IsEmpty())PrimaryArchetypes+=TEXT(" / ");PrimaryArchetypes+=FS(Value);}}Add(Readout,Text(PrimaryArchetypes,18,Gold),4);
    for(const auto& Strength:Analysis.strengths)Add(Readout,Text(FS(Strength),14,Paper),3);
    for(const auto& Weakness:Analysis.weaknesses)Add(Readout,Text(FS(Weakness),14,Gold),3);
    Add(Readout,Button(bDeckAnalysisExpanded?TEXT("HIDE DECK ANALYSIS"):TEXT("SHOW DECK ANALYSIS"),[this](){if(PresetName)DraftName=PresetName->GetText().ToString();bDeckAnalysisExpanded=!bDeckAnalysisExpanded;Navigate(TEXT("Loadout"));}),5);
    if(bDeckAnalysisExpanded)
    {
        Add(Readout,Text(TEXT("MECHANICAL DECK ROLES  ")+Archetypes,14,Muted),4);
        Add(Readout,Text(FString::Printf(TEXT("Average HP %.0f  ·  Deployment DPS %.1f  ·  Average range %.2f tiles\nSustained air answers %d  ·  Mechanical synergy %.1f"),Analysis.averageHP,Analysis.deploymentDPS,Analysis.averageRange,Analysis.sustainedAntiAir,Analysis.synergy),15,Paper),4);
        Add(Readout,Text(TEXT("PAIR SYNERGY"),17,Gold),4);Add(Readout,Text(TEXT("Mechanical compatibility scores. Measured win rates are available in the Meta Lab."),13,Muted),3);
        for(int32 I=0;I<Draft.Num();++I)for(int32 J=I+1;J<Draft.Num();++J)
        {
            const auto* A=rift::FindCard(TCHAR_TO_UTF8(*Draft[I]));const auto* B=rift::FindCard(TCHAR_TO_UTF8(*Draft[J]));if(!A||!B)continue;
            auto* Pair=Row(Readout);Add(Pair,Text(FS(A->name)+TEXT(" + ")+FS(B->name),14,Paper),true,2);Add(Pair,Badge(FString::Printf(TEXT("%.2f"),rift::PairSynergy(*A,*B)),FLinearColor(.05f,.12f,.18f,1),12),false,2);
        }
    }
    Add(Body,Panel(Readout,FMargin(16)),7);
    Add(Body,Text(TEXT("CARD COLLECTION"),22,Gold),7);Add(Body,Text(TEXT("Select a card to add it. Cards already in your deck are marked."),14,Muted),3);
    auto* Pool=WidgetTree->ConstructWidget<UWrapBox>();Pool->SetInnerSlotPadding(FVector2D(10,12));Add(Body,Pool,5);
    for(const auto& Card:rift::Cards()){const FString Id=FS(Card.id);Pool->AddChildToWrapBox(CardButton(Id,132,[this,Id](){ToggleDraft(Id);},Draft.Contains(Id)));}
}

void URiftUIWidget::Cards()
{
    auto* Header=Row();Add(Header,Text(TEXT("THE RIFTBOUND COLLECTION"),27,Paper),true);Add(Header,Badge(FString::Printf(TEXT("%d CARDS"),int32(rift::Cards().size())),FLinearColor(.075f,.135f,.20f,1),14));
    Add(Body,Text(TEXT("Know your cards. Read exact stats, special abilities, strong matchups and reliable partners."),16,Muted),4);
    auto* Categories=WidgetTree->ConstructWidget<UWrapBox>();Categories->SetInnerSlotPadding(FVector2D(8,8));Add(Body,Categories,5);
    for(const FString& Category:TArray<FString>{TEXT("GROUND TROOPS"),TEXT("AIR TROOPS"),TEXT("DEFENSIVE BUILDINGS"),TEXT("SPELLS")})Categories->AddChildToWrapBox(Badge(Category,FLinearColor(.045f,.09f,.145f,1),11));
    auto* Pool=WidgetTree->ConstructWidget<UWrapBox>();Pool->SetInnerSlotPadding(FVector2D(16,16));Add(Body,Pool,12);
    for(const auto& Card:rift::Cards()){const FString Id=FS(Card.id);Pool->AddChildToWrapBox(CardButton(Id,164,[this,Id](){DetailCard=Id;Navigate(TEXT("CardDetail"));}));}
    Add(Body,Button(TEXT("BUILD A BATTLE DECK"),[this](){Navigate(TEXT("Loadout"));},true),10);
}

void URiftUIWidget::CardDetail()
{
    const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*DetailCard));if(!Card){Say(TEXT("Card not found."));return;}
    const auto Intelligence=rift::IntelligenceFor(Card->id);
    Add(Body,Button(TEXT("BACK TO COLLECTION"),[this](){Navigate(TEXT("Cards"));}),3);
    auto* Hero=WidgetTree->ConstructWidget<UWrapBox>();Hero->SetInnerSlotPadding(FVector2D(22,18));Add(Body,Hero,8);
    Hero->AddChildToWrapBox(Panel(Illustration(DetailCard,224),FMargin(4),FLinearColor(.07f,.10f,.15f,1)));
    auto* Details=WidgetTree->ConstructWidget<UVerticalBox>();auto* DetailsSize=WidgetTree->ConstructWidget<USizeBox>();DetailsSize->SetWidthOverride(590);DetailsSize->SetContent(Details);Hero->AddChildToWrapBox(DetailsSize);
    Add(Details,Text(FS(Card->name),32,Gold),3);Add(Details,Text(FS(Intelligence.role),18,Paper),3);
    auto* Tags=WidgetTree->ConstructWidget<UWrapBox>();Tags->SetInnerSlotPadding(FVector2D(7,7));Add(Details,Tags,5);
    Tags->AddChildToWrapBox(Badge(FString::Printf(TEXT("%d AETHER"),Card->cost),Purple,14));Tags->AddChildToWrapBox(Badge(CardType(*Card),FLinearColor(.045f,.14f,.20f,1),12));if(!Card->spell)Tags->AddChildToWrapBox(Badge(FString::Printf(TEXT("%d %s"),Card->count,Card->count==1?TEXT("MEMBER"):TEXT("MEMBERS")),FLinearColor(.08f,.115f,.16f,1),12));
    const FString Targets=Card->spell?(Card->towerDamage>0?TEXT("Ground, air and structures"):TEXT("Ground and air troops")):Card->structuresOnly?TEXT("Towers and buildings"):Card->building?TEXT("Ground and air troops"):Card->canHitAir?TEXT("Ground, air and structures"):TEXT("Ground and structures");
    Add(Details,Text(TEXT("TARGETS  ")+Targets,15,Blue),6);
    auto* Stats=WidgetTree->ConstructWidget<UWrapBox>();Stats->SetInnerSlotPadding(FVector2D(8,8));Add(Details,Stats,3);
    auto Stat=[&](const FString& Label,const FString& Value)
    {
        auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();Add(Column,Text(Label,10,Muted),2);Add(Column,Text(Value,20,Paper),2);
        auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(174);Size->SetContent(Panel(Column,FMargin(12,9)));Stats->AddChildToWrapBox(Size);
    };
    if(!Card->spell)
    {
        Stat(TEXT("HP / MEMBER"),FString::Printf(TEXT("%.0f"),Card->hp));Stat(TEXT("DAMAGE / HIT"),FString::Printf(TEXT("%.0f"),Card->damage));Stat(TEXT("HIT INTERVAL"),FString::Printf(TEXT("%.2f s"),Card->attackInterval));
        Stat(TEXT("ATTACK RANGE"),FString::Printf(TEXT("%.2f tiles"),Card->range));Stat(TEXT("MOVEMENT"),FString::Printf(TEXT("%.2f tiles/s"),Card->moveSpeed));Stat(TEXT("FRONT / REAR SIGHT"),TEXT("8 / 5 tiles"));
        if(Card->projectileSpeed>0)Stat(TEXT("PROJECTILE SPEED"),FString::Printf(TEXT("%.1f tiles/s"),Card->projectileSpeed));if(Card->splash>0)Stat(TEXT("SPLASH RADIUS"),FString::Printf(TEXT("%.2f tiles"),Card->splash));
    }
    else
    {
        Stat(TEXT("INITIAL TROOP HIT"),FString::Printf(TEXT("%.0f HP"),Card->damage));Stat(TEXT("STRUCTURE DAMAGE"),FString::Printf(TEXT("%.0f HP"),Card->towerDamage));Stat(TEXT("SPELL RADIUS"),FString::Printf(TEXT("%.2f tiles"),Card->spellRadius));
        if(Card->castDelay>0)Stat(TEXT("TIME TO IMPACT"),FString::Printf(TEXT("%.2f s"),Card->castDelay));
        if(Card->rounds>0)Stat(TEXT("VISUAL ROUNDS"),FString::FromInt(Card->rounds));if(Card->dotDamage>0){Stat(TEXT("DAMAGE / TICK"),FString::Printf(TEXT("%.0f HP"),Card->dotDamage));Stat(TEXT("ZONE DURATION"),FString::Printf(TEXT("%.1f s"),Card->dotDuration));}
    }

    auto* Special=WidgetTree->ConstructWidget<UVerticalBox>();bool HasSpecial=false;
    auto Ability=[&](const FString& Title,const FString& Copy){HasSpecial=true;Add(Special,Text(Title,17,Gold),3);Add(Special,Text(Copy,15,Paper),3);};
    if(Card->building)Ability(TEXT("DEFENSIVE BUILDING"),FString::Printf(TEXT("Remains for %.1f seconds. Its %.2f-tile footprint must fit the legal deployment area."),Card->lifetime,Card->footprint));
    if(Card->castDelay>0)Ability(TEXT("LEAD YOUR CAST"),FString::Printf(TEXT("Hits %.2f seconds after casting. Aim where enemies will be when the animation finishes. The marked area stays fixed; enemies can enter or leave it before impact."),Card->castDelay));
    if(Card->chargeDamage)Ability(TEXT("CHARGED IMPACT"),FString::Printf(TEXT("Builds charge by moving for more than 1.65 seconds. A charged impact deals %.0f damage."),Card->chargeDamage));
    if(Card->slowPct)Ability(TEXT("MOVEMENT SLOW"),FString::Printf(TEXT("Each melee hit slows movement by %.0f%% for %.1f seconds. Repeat hits refresh the effect; slows do not stack."),Card->slowPct*100,Card->slowDuration));
    if(Card->auraDamage)Ability(TEXT("ELECTRIC AURA"),FString::Printf(TEXT("Deals %.0f damage to nearby ground and air troops every %.1f seconds within %.1f tiles. Each pulse stuns for %.1f seconds. Direct attacks remain structure-only."),Card->auraDamage,Card->auraInterval,Card->auraRadius,Card->stunDuration));
    if(Card->dotDamage)Ability(TEXT("LINGERING DAMAGE ZONE"),FString::Printf(TEXT("The initial hit deals %.0f damage. The zone then deals %.0f troop damage once per second for %.0f ticks. Troops entering the zone can be hit by later ticks. Towers and buildings take no damage."),Card->damage,Card->dotDamage,Card->dotDuration));
    if(Card->spell&&Card->rounds>1)Ability(TEXT("ONE DAMAGE APPLICATION"),FString::Printf(TEXT("The burst shows %d rounds, but applies the listed damage once to each eligible target."),Card->rounds));
    if(HasSpecial)Add(Body,Panel(Special,FMargin(18),FLinearColor(.085f,.065f,.11f,1)),8);

    auto* Uses=WidgetTree->ConstructWidget<UVerticalBox>();Add(Uses,Text(TEXT("HOW TO PLAY IT"),20,Gold),4);for(const auto& Use:Intelligence.suggestedUses)Add(Uses,Text(FS(Use),15,Paper),4);Add(Body,Panel(Uses,FMargin(18)),8);
    auto* Relations=WidgetTree->ConstructWidget<UWrapBox>();Relations->SetInnerSlotPadding(FVector2D(16,16));Add(Body,Relations,8);
    auto Group=[&](const FString& Heading,const std::vector<rift::CardRelation>& Entries,FLinearColor Accent)
    {
        auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();Add(Column,Text(Heading,19,Accent),4);
        for(const auto& Entry:Entries)
        {
            const FString Id=FS(Entry.id);auto* Link=Button(TEXT(""),[this,Id](){DetailCard=Id;Navigate(TEXT("CardDetail"));});
            auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();Add(Line,Illustration(Id,46),false,2);auto* Copy=WidgetTree->ConstructWidget<UVerticalBox>();Add(Copy,Text(CardName(Entry.id),15,Paper),2);auto* Reason=Text(FS(Entry.reason),13,Muted);Reason->SetWrapTextAt(250);Add(Copy,Reason,2);Add(Line,Copy,true,6);Link->SetContent(Line);Add(Column,Link,4);
        }
        auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(364);Size->SetContent(Panel(Column,FMargin(14)));Relations->AddChildToWrapBox(Size);
    };
    Group(TEXT("STRONG AGAINST"),Intelligence.bestAgainst,Blue);Group(TEXT("WEAK AGAINST"),Intelligence.weakAgainst,FLinearColor(.94f,.49f,.38f,1));Group(TEXT("BEST PARTNERS"),Intelligence.partners,Gold);
    auto* Recommendations=WidgetTree->ConstructWidget<UVerticalBox>();Add(Recommendations,Text(TEXT("RECOMMENDED ANSWERS & PARTNERS"),19,Gold),4);Add(Recommendations,Text(TEXT("Best defensive answer: ")+CardName(Intelligence.bestDefensiveAnswer.id)+TEXT(" · ")+FS(Intelligence.bestDefensiveAnswer.reason),15,Paper),4);Add(Recommendations,Text(TEXT("Best offensive partner: ")+CardName(Intelligence.bestOffensivePartner.id)+TEXT(" · ")+FS(Intelligence.bestOffensivePartner.reason),15,Paper),4);Add(Body,Panel(Recommendations,FMargin(16)),8);
    Add(Body,Text(TEXT("FULL MECHANICAL MATCHUPS"),19,Gold),7);Add(Body,Text(TEXT("These are mechanical matchup explanations. Use the Meta Lab for sample-supported win rates."),13,Muted),3);
    for(const auto& Other:rift::Cards())if(Other.id!=Card->id)
    {
        const FString Id=FS(Other.id);auto* Link=Button(TEXT(""),[this,Id](){DetailCard=Id;Navigate(TEXT("CardDetail"));});auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();Add(Line,Illustration(Id,34),false,3);auto* Copy=WidgetTree->ConstructWidget<UVerticalBox>();Add(Copy,Text(FS(Other.name),15,Paper),2);Add(Copy,Text(FS(rift::CounterReason(*Card,Other)),14,Muted),2);Add(Line,Copy,true,7);Link->SetContent(Line);Add(Body,Link,4);
    }
}
