#if WITH_DEV_AUTOMATION_TESTS
#include "Simulation/RiftSimulation.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include <algorithm>
#include <cmath>

namespace {
rift::Entity* ReaperBody(rift::Match& Match) {
    for(auto& E:const_cast<rift::Snapshot&>(Match.State()).entities)
        if(E.cardId=="aether_reaper")return &E;
    return nullptr;
}
rift::MatchOptions ReaperOptions() {rift::MatchOptions O;O.aiEnabled={false,false};return O;}
void FreezeReaperCrowns(rift::Match& M) {
    for(auto& E:const_cast<rift::Snapshot&>(M.State()).entities)
        if(E.kind==rift::EntityKind::Core||E.kind==rift::EntityKind::Guard)E.cooldown=1000;
}
int HealthCloaks(const rift::Match& M) {
    return int(std::count_if(M.Events().begin(),M.Events().end(),[](const rift::Event& E){return E.type=="invisibility_apply"&&E.cardId=="aether_reaper"&&E.reason=="health_threshold";}));
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftReaperHealthCloakTest,"Rift.Integration.ReaperHealthCloak",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftReaperHealthCloakTest::RunTest(const FString&) {
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox"))){AddError(TEXT("Requires isolated automation sandbox."));return false;}
    rift::Match M(ReaperOptions());FreezeReaperCrowns(M);
    TestTrue(TEXT("Actual Reaper deploys"),M.Spawn(rift::Team::Player,"aether_reaper",{0,7}));
    TestTrue(TEXT("Actual first bullet packet casts"),M.Spawn(rift::Team::Enemy,"bullet_burst",{0,7}));M.Step(.32);
    auto* Body=ReaperBody(M);if(!Body)return false;
    TestEqual(TEXT("Above half health retains270HP"),Body->hp,270.);
    TestEqual(TEXT("Above half health has no proc"),HealthCloaks(M),0);
    Body->hp=397.5; // Isolated boundary fixture; next real175 packet lands at222.5.
    TestTrue(TEXT("Exact boundary bullet packet casts"),M.Spawn(rift::Team::Enemy,"bullet_burst",{0,7}));M.Step(.32);
    Body=ReaperBody(M);if(!Body)return false;
    TestEqual(TEXT("Half health is exactly222.5HP"),Body->hp,222.5);
    TestEqual(TEXT("First surviving threshold crossing procs once"),HealthCloaks(M),1);
    TestTrue(TEXT("Living Reaper owns the cloak flag"),Body->healthCloakTriggered&&Body->invisibleUntil>M.State().elapsed);
    const auto Event=std::find_if(M.Events().begin(),M.Events().end(),[](const rift::Event& E){return E.type=="invisibility_apply"&&E.reason=="health_threshold";});
    if(Event==M.Events().end())return false;
    TestTrue(TEXT("Cloak lasts exactly one simulation second"),std::abs(Event->until-Event->time-1.)<1.e-9);
    const double Deadline=Body->invisibleUntil;M.Step(0);
    TestEqual(TEXT("Zero simulation time keeps the exact deadline"),ReaperBody(M)->invisibleUntil,Deadline);
    M.Step(1.1);Body=ReaperBody(M);
    TestTrue(TEXT("One second expiry restores acquisition"),Body&&!Body->dead&&Body->invisibleUntil==0);
    if(!Body)return false;Body->hp=397.5;
    TestTrue(TEXT("Repeated threshold crossing packet casts"),M.Spawn(rift::Team::Enemy,"bullet_burst",Body->position));M.Step(.32);
    TestEqual(TEXT("RestoredHP cannot re-arm a spent deployment proc"),HealthCloaks(M),1);
    rift::Match Lethal(ReaperOptions());FreezeReaperCrowns(Lethal);
    TestTrue(TEXT("Lethal fixture deploys"),Lethal.Spawn(rift::Team::Player,"aether_reaper",{0,7}));ReaperBody(Lethal)->hp=100;
    TestTrue(TEXT("Lethal real packet casts"),Lethal.Spawn(rift::Team::Enemy,"bullet_burst",{0,7}));Lethal.Step(.32);
    TestTrue(TEXT("Lethal hit kills before any invisibility proc"),ReaperBody(Lethal)&&ReaperBody(Lethal)->dead&&HealthCloaks(Lethal)==0);
    rift::Match Flight(ReaperOptions());FreezeReaperCrowns(Flight);
    TestTrue(TEXT("Committed-hit fixture deploys"),Flight.Spawn(rift::Team::Player,"aether_reaper",{0,7}));ReaperBody(Flight)->hp=397.5;
    // Keep the Reaper in front of the Archer's initial facing. At equal depth,
    // its first movement puts it beyond the Archer's five-tile rear detection.
    TestTrue(TEXT("Actual ranged enemy deploys"),Flight.Spawn(rift::Team::Enemy,"ember_archer",{6,6}));
    const rift::EntityId ReaperId=ReaperBody(Flight)->id;
    const auto Archer=std::find_if(Flight.State().entities.begin(),Flight.State().entities.end(),[](const rift::Entity& E){return E.cardId=="ember_archer";});
    if(Archer==Flight.State().entities.end())return false;
    const rift::EntityId ArcherId=Archer->id;
    TestTrue(TEXT("Threshold packet casts before projectile arrival"),Flight.Spawn(rift::Team::Enemy,"bullet_burst",{0,7}));Flight.Step(.32);
    Body=ReaperBody(Flight);if(!Body)return false;
    const auto Committed=std::find_if(Flight.State().projectiles.begin(),Flight.State().projectiles.end(),[&](const rift::Projectile& P){return P.source==ArcherId&&P.target==ReaperId&&P.cardId=="ember_archer";});
    TestTrue(TEXT("Threshold cloak retains the actual Archer projectile targeting the Reaper"),Body->invisibleUntil>Flight.State().elapsed&&Committed!=Flight.State().projectiles.end());
    const auto Cloak=std::find_if(Flight.Events().begin(),Flight.Events().end(),[&](const rift::Event& E){return E.type=="invisibility_apply"&&E.source==ReaperId&&E.target==ReaperId&&E.reason=="health_threshold";});
    if(Cloak==Flight.Events().end())return false;
    const double CloakTime=Cloak->time,CloakUntil=Cloak->until;
    TestTrue(TEXT("Archer committed the exact projectile before the health cloak"),std::any_of(Flight.Events().begin(),Flight.Events().end(),[&](const rift::Event& E){return E.type=="projectile_launch"&&E.source==ArcherId&&E.target==ReaperId&&E.cardId=="ember_archer"&&E.time<CloakTime;}));
    TestTrue(TEXT("Health cloak drops the actual Archer lock on this Reaper"),std::any_of(Flight.Events().begin(),Flight.Events().end(),[&](const rift::Event& E){return E.type=="target_drop"&&E.source==ArcherId&&E.target==ReaperId&&E.cardId=="aether_reaper"&&E.reason=="invisibility"&&std::abs(E.time-CloakTime)<1.e-9;}));
    const double HP=Body->hp;Flight.Step(.15);Body=ReaperBody(Flight);
    TestTrue(TEXT("Committed projectile damage resolves during invisibility"),Body&&Body->hp<HP&&Body->invisibleUntil>Flight.State().elapsed);
    if(Body)TestEqual(TEXT("The retained Archer projectile applies one real152 damage packet"),Body->hp,HP-152.);
    TestTrue(TEXT("The exact committed Archer hit is recorded inside the cloak interval"),std::any_of(Flight.Events().begin(),Flight.Events().end(),[&](const rift::Event& E){return E.type=="damage"&&E.source==ArcherId&&E.target==ReaperId&&E.cardId=="ember_archer"&&E.amount==152.&&E.time>CloakTime&&E.time<CloakUntil;}));
    for(const auto& E:Flight.State().entities)if(E.id==ArcherId)TestTrue(TEXT("The Archer cannot reacquire the invisible Reaper"),E.target!=ReaperId&&E.hardLock!=ReaperId&&E.forcedTarget!=ReaperId);
    TestFalse(TEXT("No new Archer projectile targets the Reaper during invisibility"),std::any_of(Flight.Events().begin(),Flight.Events().end(),[&](const rift::Event& E){return E.type=="projectile_launch"&&E.source==ArcherId&&E.target==ReaperId&&E.time>=CloakTime&&E.time<CloakUntil;}));
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftReaperCleaveTest,"Rift.Integration.ReaperCleave",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftReaperCleaveTest::RunTest(const FString&) {
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox"))){AddError(TEXT("Requires isolated automation sandbox."));return false;}
    rift::Match M(ReaperOptions());FreezeReaperCrowns(M);
    TestTrue(TEXT("Actual Reaper deploys"),M.Spawn(rift::Team::Player,"aether_reaper",{0,7}));
    TestTrue(TEXT("First eligible enemy deploys"),M.Spawn(rift::Team::Enemy,"sky_manta",{1.1,7}));
    TestTrue(TEXT("Second eligible enemy deploys"),M.Spawn(rift::Team::Enemy,"sky_manta",{2.1,7}));M.Step(.05);
    const auto Event=std::find_if(M.Events().begin(),M.Events().end(),[](const rift::Event& E){return E.type=="melee_cleave"&&E.cardId=="aether_reaper";});
    TestTrue(TEXT("One authoritative contact records two actual victims"),Event!=M.Events().end()&&Event->count==2);
    int Packets=0;for(const auto& E:M.Events())if(E.type=="damage"&&E.cardId=="aether_reaper"){++Packets;TestEqual(TEXT("Each real victim receives one95 damage packet"),E.amount,95.);}
    TestEqual(TEXT("Slash feedback creates no extra hit packet"),Packets,2);
    rift::Match Single(ReaperOptions());FreezeReaperCrowns(Single);Single.Spawn(rift::Team::Player,"aether_reaper",{0,7});Single.Spawn(rift::Team::Enemy,"sky_manta",{1.1,7});Single.Step(.05);
    TestFalse(TEXT("A single victim cannot invent a multi-target slash"),std::any_of(Single.Events().begin(),Single.Events().end(),[](const rift::Event& E){return E.type=="melee_cleave";}));
    return !HasAnyErrors();
}
#endif
