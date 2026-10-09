#include "RiftUIWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/WrapBox.h"

void URiftUIWidget::FieldManual()
{
    const FLinearColor Gold(.82f, .65f, .36f, 1);
    const FLinearColor Paper(.92f, .91f, .86f, 1);
    const FLinearColor Secondary(.62f, .68f, .76f, 1);
    const FLinearColor Blue(.30f, .71f, .92f, 1);
    const FLinearColor Panel(.035f, .055f, .085f, 1);
    const FLinearColor Rule(.14f, .19f, .25f, 1);

    Add(Body, Text(TEXT("THE FIELD MANUAL"), 28, Gold), 3);
    Add(Body, Text(TEXT("Two lanes. Eight cards. One enemy Core."), 19, Paper), 3);
    Add(Body, Text(TEXT("Break a Guard Tower to earn a crown and open its lane. Destroy the enemy Core for an immediate three-crown victory."), 16, Secondary), 3);

    auto* Sections = WidgetTree->ConstructWidget<UWrapBox>();
    Sections->SetInnerSlotPadding(FVector2D(18, 18));
    Add(Body, Sections, 10);
    auto Section = [&](const FString& Number, const FString& Heading)
    {
        auto* Frame = WidgetTree->ConstructWidget<UBorder>();
        Frame->SetBrush(FSlateRoundedBoxBrush(Panel, 6.f, Rule, 1.f));
        Frame->SetPadding(FMargin(20, 18));
        auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
        Frame->SetContent(Column);
        auto* Size = WidgetTree->ConstructWidget<USizeBox>();
        Size->SetWidthOverride(424);
        Size->SetContent(Frame);
        Sections->AddChildToWrapBox(Size);
        auto* HeadingRow = Row(Column);
        Add(HeadingRow, Text(Number, 16, Blue), false, 0);
        Add(HeadingRow, Text(Heading, 20, Gold), true, 6);
        return Column;
    };
    auto Entry = [&](UVerticalBox* Column, const FString& Heading, const FString& Copy)
    {
        Add(Column, Text(Heading, 16, Paper), 3);
        Add(Column, Text(Copy, 14, Secondary), 3);
    };
    auto Key = [&](UVerticalBox* Column, const FString& Label, const FString& Copy)
    {
        auto* Line = Row(Column);
        auto* Cap = WidgetTree->ConstructWidget<UBorder>();
        Cap->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.075f, .105f, .145f, 1), 4.f, Rule, 1.f));
        Cap->SetPadding(FMargin(6, 5));
        auto* LabelText = Text(Label, 13, Paper);
        LabelText->SetJustification(ETextJustify::Center);
        Cap->SetContent(LabelText);
        auto* Size = WidgetTree->ConstructWidget<USizeBox>();
        Size->SetWidthOverride(94);
        Size->SetContent(Cap);
        auto* CapSlot = Line->AddChildToHorizontalBox(Size);
        CapSlot->SetVerticalAlignment(VAlign_Top);
        auto* CopyText = Text(Copy, 14, Secondary);
        CopyText->SetWrapTextAt(262);
        Add(Line, CopyText, true, 6);
    };

    auto* Controls = Section(TEXT("01"), TEXT("COMMAND YOUR HAND"));
    Key(Controls, TEXT("1  2  3  4"), TEXT("Select the matching hand slot during battle."));
    Key(Controls, TEXT("CLICK"), TEXT("Select a card, then click a legal arena tile to deploy."));
    Key(Controls, TEXT("DRAG"), TEXT("Drag a card onto the arena and release. Return it to the hand to cancel."));
    Key(Controls, TEXT("RIGHT CLICK"), TEXT("Cancel a selected card or an armed sandbox spawn."));
    Key(Controls, TEXT("ESC"), TEXT("Open or close the battle pause menu."));
    Key(Controls, TEXT("WHEEL"), TEXT("Zoom the arena camera in or out."));
    Key(Controls, TEXT("F9"), TEXT("Open or close the Developer Lab during battle."));
    Entry(Controls, TEXT("Your preferred deployment"), TEXT("Settings offers click or drag input. With Confirm Deployment enabled, click the same legal tile twice. Hover a hand card to read its stats."));

    auto* Field = Section(TEXT("02"), TEXT("READ THE ARENA"));
    Entry(Field, TEXT("Every square is one tile"), TEXT("Troops and buildings deploy on your half. Spells can target anywhere within the board. The placement preview shows whether a tile is legal."));
    Entry(Field, TEXT("Win a lane, open a pocket"), TEXT("Destroy an enemy Guard Tower to unlock the matching lane's pocket beyond the river. The other lane stays locked, and pockets never reach the enemy Core."));
    Entry(Field, TEXT("Buildings need room"), TEXT("The entire building footprint must fit the legal zone. Living towers and buildings block occupied spaces. An invalid placement spends no Aether and does not cycle your card."));
    Entry(Field, TEXT("Bridges and targeting"), TEXT("Ground troops cross the lane bridges; flying troops cross the river directly. Once an attacker reaches a Crown Tower's attack range and locks on, a fresh defender cannot pull it away."));
    Entry(Field, TEXT("Wake the Core"), TEXT("A Core stays dormant until one of its own Guard Towers falls. Damaging the Core alone does not activate it."));

    auto* Economy = Section(TEXT("03"), TEXT("MANAGE YOUR AETHER"));
    Entry(Economy, TEXT("Five to start. Ten at most."), TEXT("At 1×, one Aether regenerates every 2.8 seconds. Spending a card pays its displayed cost once, including cards that deploy several units. A full bank wastes incoming regeneration."));
    Entry(Economy, TEXT("The surge schedule"), TEXT("1× for regulation's first two minutes. 2× for its final minute and overtime's first minute. 3× for overtime's final minute. The HUD announces each change."));
    Entry(Economy, TEXT("Eight distinct cards, a visible cycle"), TEXT("Your deck's first four cards form the opening hand. Playing a card moves it to the back of the queue; NEXT enters that same hand slot. The opening is not shuffled."));
    Entry(Economy, TEXT("Plan the next match"), TEXT("Loadout saves five deck presets and shows cost, roles, strengths and gaps. Saving a deck during a match changes the next battle's deck."));

    auto* Clock = Section(TEXT("04"), TEXT("KNOW HOW A MATCH ENDS"));
    Entry(Clock, TEXT("Regulation · 3:00"), TEXT("The higher crown score wins when the clock expires. Equal nonzero scores are a draw. A 0–0 score alone unlocks overtime."));
    Entry(Clock, TEXT("Overtime · 2:00"), TEXT("Sudden death: the first Crown Tower destroyed wins. A Core destruction always ends the match immediately."));
    Entry(Clock, TEXT("Tiebreaker"), TEXT("If overtime also ends 0–0, combat freezes. After a brief pause, all surviving Crown Towers lose 180 HP per second. The side whose tower reaches zero first loses."));
    Entry(Clock, TEXT("Explore in Training"), TEXT("Training marks its replay as a sandbox session. Use the Developer Lab to test speed, Aether, friendly and enemy spawns, tower HP, AI styles and debug overlays."));

    auto* Specials = Section(TEXT("05"), TEXT("RECOGNIZE THE SPECIALS"));
    Entry(Specials, TEXT("Frost Fang"), TEXT("Melee hits slow movement by 30% for two seconds. Further hits refresh the slow; they do not stack it."));
    Entry(Specials, TEXT("Storm Raven"), TEXT("Direct attacks target structures. Its electric aura damages nearby ground and air troops every three seconds and briefly stuns them."));
    Entry(Specials, TEXT("Meteor Shards"), TEXT("An initial troop hit is followed by five one-second damage ticks in the zone. It deals no damage to towers or buildings."));
    Entry(Specials, TEXT("Rambeast and swarms"), TEXT("Rambeast builds charge through movement for a stronger hit. Twin Blades deploys two members; Vampire Bats deploys five. Each deployment costs once."));
    Add(Specials, Button(TEXT("VIEW ALL CARD DETAILS"), [this](){ Navigate(TEXT("Cards")); }), 6);

    auto* Study = Section(TEXT("06"), TEXT("LEARN FROM YOUR MATCHES"));
    Entry(Study, TEXT("Replay a finished battle"), TEXT("Replays preserve recorded events and positions. Pause, scrub, change speed, jump to key moments, or open the match analysis. Playback does not rerun the AI."));
    Entry(Study, TEXT("Compare in the Meta Lab"), TEXT("Meta runs the same combat rules in background matches. Browse cards, matchups, synergy, archetypes and AI styles; filter samples and export the measured data. Live battles pause the background worker."));
    Entry(Study, TEXT("Keep your profile"), TEXT("Your name, results, deck presets and settings save locally. Profile and Settings are available from the main menu; your saved deck is ready for the next match."));
    Add(Study, Button(TEXT("EDIT BATTLE DECK"), [this](){ Navigate(TEXT("Loadout")); }), 6);
    Add(Body, Text(TEXT("The card collection is the reference for exact costs, ranges, damage and targeting. The arena preview is the reference for legal placement."), 14, Secondary), 8);
}
