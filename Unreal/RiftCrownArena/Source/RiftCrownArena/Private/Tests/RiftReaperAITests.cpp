#if WITH_DEV_AUTOMATION_TESTS
#include "Simulation/RiftSimulation.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include <algorithm>
#include <cmath>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftReaperCloakAITest,"Rift.Integration.ReaperCloakAI",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftReaperCloakAITest::RunTest(const FString&) {
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox"))) {
        AddError(TEXT("Reaper AI fixture requires the isolated automation sandbox."));return false;
    }
    for(const char* CardId:{"shadow_surge","aether_reaper"}) {
        const bool AreaSpell=std::string(CardId)=="shadow_surge";
        const auto* Card=rift::FindCard(CardId);if(!Card)return false;
        rift::MatchOptions Options;Options.seed=74031;Options.aiEnabled={false,false};Options.aiStyles={"control","control"};
        Options.decks[0]={CardId,"boulderback","frost_fang","storm_raven","iron_giant","arc_mage","meteor_shards",AreaSpell?"aether_reaper":"shadow_surge"};
        if(!TestTrue(TEXT("Both compared AI hands originate from legal eight-card decks"),rift::ValidateDeck(Options.decks[0])))return false;
        rift::Match Match(Options);
        // Explicit observable-threat fixture: a wounded stationary ally and a
        // real in-range enemy with a committed target, due to fire in1.1s.
        // Hold crowns quiet so only this public lock informs the AI choice.
        if(!TestTrue(TEXT("Actual friendly target spawns"),Match.Spawn(rift::Team::Player,"ironclad",{.5,8.5}))||
           !TestTrue(TEXT("Actual enemy ranged attacker spawns"),Match.Spawn(rift::Team::Enemy,"ember_archer",{6.5,8.5})))return false;
        rift::EntityId Subject=0,Attacker=0;
        for(auto& Body:const_cast<rift::Snapshot&>(Match.State()).entities) {
            if(Body.kind==rift::EntityKind::Core||Body.kind==rift::EntityKind::Guard)Body.stunUntil=100;
            if(Body.cardId=="ironclad") {Subject=Body.id;Body.hp=100;Body.stunUntil=100;}
            if(Body.cardId=="ember_archer")Attacker=Body.id;
        }
        if(!TestTrue(TEXT("Threat fixture has two distinct real bodies"),Subject>0&&Attacker>0&&Subject!=Attacker))return false;
        for(auto& Body:const_cast<rift::Snapshot&>(Match.State()).entities)
            if(Body.id==Attacker) {Body.target=Body.hardLock=Subject;Body.cooldown=1.1;Body.scanClock=100;}
        Match.SetAether(rift::Team::Player,4);
        const auto Before=Match.State();
        Match.SetAIEnabled(rift::Team::Player,true);Match.Step(1./60.);
        Match.SetAIEnabled(rift::Team::Player,false); // Examine one genuine paid decision, then its delayed effect.
        const rift::Event* Paid=nullptr;int Plays=0;
        for(const auto& Event:Match.Events())if(Event.type=="card_play"&&Event.team==rift::Team::Player&&!Event.sandbox) {Paid=&Event;++Plays;}
        if(!TestEqual(TEXT("Each AI actually plays exactly one affordable card"),Plays,1)||!Paid)return false;
        const auto Play=*Paid;
        TestEqual(TEXT("Compared paid AI decision uses the intended card"),FString(UTF8_TO_TCHAR(Play.cardId.c_str())),FString(UTF8_TO_TCHAR(CardId)));
        TestTrue(TEXT("AI choice spends exactly its real card cost"),std::abs(Play.aetherBefore-Play.aetherAfter-Card->cost)<1e-9&&Play.amount==Card->cost);
        TestTrue(TEXT("Paid AI choice cycles its real hand once"),Match.State().hands[0][0]==Before.queues[0].front()&&Match.State().queues[0].back()==CardId);
        const bool CloakDecision=std::any_of(Match.Events().begin(),Match.Events().end(),[](const rift::Event& Event) {
            return Event.type=="ai_decision"&&Event.team==rift::Team::Player&&Event.reason.find("CLOAK DEFENSE:")==0;
        });
        TestEqual(TEXT("Only the area spell makes a cloak-defense decision"),CloakDecision,AreaSpell);
        TestEqual(TEXT("Paid play distinguishes area protection from troop defense"),FString(UTF8_TO_TCHAR(Play.reason.c_str())),FString(AreaSpell?TEXT("cloak_defense"):TEXT("defend")));
        Match.Step(AreaSpell?.8:.3);
        const auto Body=std::find_if(Match.State().entities.begin(),Match.State().entities.end(),[&](const rift::Entity& Entity){return Entity.id==Subject;});
        if(!TestTrue(TEXT("The protected target remains alive through the spell window"),Body!=Match.State().entities.end()&&!Body->dead))return false;
        const bool Applied=std::any_of(Match.Events().begin(),Match.Events().end(),[&](const rift::Event& Event) {
            return Event.type=="invisibility_apply"&&Event.target==Subject&&Event.playId==Play.playId&&Event.cardId==CardId;
        });
        TestEqual(TEXT("Only paid Shadow Surge actually cloaks the threatened ally"),Applied,AreaSpell);
        TestEqual(TEXT("Reaper deployment cannot give another troop a free cloak"),Body->invisibleUntil>Match.State().elapsed,AreaSpell);
        if(AreaSpell) {
            TestTrue(TEXT("Actual delayed area protection drops the enemy lock"),std::any_of(Match.Events().begin(),Match.Events().end(),[&](const rift::Event& Event) {
                return Event.type=="target_drop"&&Event.source==Attacker&&Event.target==Subject&&Event.playId==Play.playId;
            }));
        } else {
            TestTrue(TEXT("Self-cloak card still deploys as a full-health defensive troop"),std::any_of(Match.State().entities.begin(),Match.State().entities.end(),[&](const rift::Entity& Entity) {
                return Entity.cardId==CardId&&Entity.playId==Play.playId&&Entity.hp==Card->hp&&!Entity.healthCloakTriggered;
            }));
        }
    }
    return !HasAnyErrors();
}
#endif
