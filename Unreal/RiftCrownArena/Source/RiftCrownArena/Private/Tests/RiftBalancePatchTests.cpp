#if WITH_DEV_AUTOMATION_TESTS
#include "RiftCardData.h"
#include "RiftReplaySubsystem.h"
#include "Simulation/RiftSimulation.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include <cmath>
#include <set>
#include <utility>

namespace
{
struct FBalanceCard
{
    const char* Id;
    int Cost, Count;
    double HP, Damage, Interval, Speed, Range, Scale, Projectile, Splash, Lifetime, Footprint;
    bool Flying, AirTargets, StructuresOnly, Spell, Building;
    double BasicDPS;
};
// Independent requested balance contract. DPS is derived from damage and the
// requested hit interval; aura, charge, splash and travel time are separate.
const FBalanceCard ExpectedCards[] = {
    {"ironclad",3,1,822,93,1.1,2.25,1.35,1,0,0,0,0,false,false,false,false,false,93/1.1},
    {"ember_archer",3,1,374,152,1.75,2.35,6,.95,16,0,0,0,false,true,false,false,false,152/1.75},
    {"twin_blades",2,2,262,55,.75,3.35,1.2,.82,0,0,0,0,false,false,false,false,false,55/.75},
    {"boulderback",5,1,1620,118,2,1.3,1.5,1.22,0,0,0,0,false,false,true,false,false,59},
    {"arc_mage",4,1,512,108,1.15,1.7,6,.96,17,2.25,0,0,false,true,false,false,false,108/1.15},
    {"rambeast",4,1,934,156,1.25,2,1.45,1.06,0,0,0,0,false,false,true,false,false,156/1.25},
    {"sky_manta",3,1,480,77,.92,3.05,3.4,1.05,15,0,0,0,true,true,false,false,false,77/.92},
    {"vampire_bats",5,5,174,86,1.05,1.95,2,.68,0,0,0,0,true,true,false,false,false,86/1.05},
    {"frost_fang",5,1,1265,72,.75,2.5,1.1,1.02,0,0,0,0,false,false,false,false,false,96},
    {"storm_raven",6,1,1222,251,1.7,1.18,4.3,1.2,18,0,0,0,true,false,true,false,false,251/1.7},
    {"meteor_shards",5,1,0,262,0,0,0,1,0,0,0,0,false,false,false,true,false,0},
    {"archer_tower",4,1,850,75,.9,0,7,1,18,0,25,1.65,false,true,false,false,true,75/.9},
    {"bullet_burst",2,1,0,175,0,0,0,1,0,0,0,0,false,false,false,true,false,0},
    {"nova_flask",4,1,0,375,0,0,0,1,0,0,0,0,false,false,false,true,false,0},
    {"mini_stampede",4,5,133.2,24,.9,3.5,1,1.3,0,0,0,0,false,false,false,false,false,24/.9},
    {"stampede",7,15,133.2,24,1,3,.8,.90,0,0,0,0,false,false,true,false,false,24},
    {"pulverizer",3,1,700,65,.9,0,4,1.05,0,4,45,1.65,false,false,false,false,true,65/.9},
    {"bone_warden",6,1,1526,101,1.5,1,1.75,1.65,0,.5,0,0,false,false,false,false,false,101/1.5},
    {"aether_reaper",4,1,445,95,1.05,1.85,1.4,1.35,0,1.5,0,0,true,true,false,false,false,95/1.05},
    {"iron_giant",3,1,1000,85,1.3,1.55,1.35,1.65,0,0,0,0,false,false,true,false,false,85/1.3},
    {"shadow_surge",3,1,0,100,0,0,0,1,0,0,0,0,false,true,false,true,false,0},
    {"dart_gun",3,1,420,50,.4,1.5,8.5,1.05,22,0,0,0,false,true,false,false,false,125},
    {"thunderstorm",3,1,0,65,0,0,0,1,0,0,0,0,false,false,false,true,false,0},
    {"blade_box",5,1,500,0,3,0,0,1,0,0,30,1.65,false,false,false,false,true,0}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftBalancePatchTest,"Rift.Integration.BalancePatch",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftBalancePatchTest::RunTest(const FString& Parameters)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox")))
    {AddError(TEXT("Balance patch fixture requires isolated RiftSaveRoot/UserDir and RiftAutomationSandbox."));return false;}
    TestEqual(TEXT("All approved playable cards exist"),int32(rift::Cards().size()),int32(UE_ARRAY_COUNT(ExpectedCards)));
    TestNotNull(TEXT("Pulverizer remains in the approved playable roster"),rift::FindCard("pulverizer"));
    // The 1.7 design moves actual collision/target bodies with their visuals:
    // three more rear tiles and two more lateral tiles on each bridge axis.
    TestEqual(TEXT("Approved arena is twenty-two tiles across"),rift::arena::Width,22);
    TestEqual(TEXT("Approved arena is forty tiles deep"),rift::arena::Height,40);
    TestEqual(TEXT("Each bridge is two more tiles away from the Core axis"),rift::arena::BridgeCenterX,7.76);
    TestEqual(TEXT("Both Guard axes move exactly one tile inward"),rift::arena::GuardX,6.76);
    TestEqual(TEXT("Core depth adds exactly three rear tiles"),rift::arena::CoreDepth,16.84);
    TestEqual(TEXT("Guard depth adds exactly three rear tiles"),rift::arena::GuardDepth,13.72);
    rift::MatchOptions GeometryOptions;GeometryOptions.aiEnabled={false,false};
    rift::Match Geometry(GeometryOptions);
    TestEqual(TEXT("Tower alignment retains six actual initial tower bodies"),int32(Geometry.State().entities.size()),6);
    std::set<std::pair<int,int>> TowerSlots;
    int32 CoreCount=0,GuardCount=0;
    for(const auto& Tower:Geometry.State().entities)
    {
        const bool Core=Tower.kind==rift::EntityKind::Core;
        const bool Guard=Tower.kind==rift::EntityKind::Guard;
        const double Sign=Tower.team==rift::Team::Player?1.:-1.;
        const FString Label=FString::Printf(TEXT("Tower team %d lane %d"),int32(Tower.team),Tower.lane);
        TestTrue(Label+TEXT(" remains a Core or Guard body"),Core||Guard);
        TestTrue(Label+TEXT(" occupies one unique team/lane slot"),TowerSlots.insert({int(Tower.team),Tower.lane}).second);
        TestTrue(Label+TEXT(" retains its tower lane"),Core?Tower.lane==0:(Tower.lane==-1||Tower.lane==1));
        TestTrue(Label+TEXT(" uses its shifted symmetric rear depth"),std::abs(Tower.position.z-Sign*(Core?16.84:13.72))<1.e-9);
        TestTrue(Label+TEXT(" retains its collision radius"),std::abs(Tower.radius-(Core?1.35:1.15))<1.e-9);
        TestTrue(Label+TEXT(" has the requested Core/Guard center"),std::abs(Tower.position.x-(Core?0.:Tower.lane*rift::arena::GuardX))<1.e-9);
        CoreCount+=Core;GuardCount+=Guard;
    }
    TestEqual(TEXT("Both Core Towers stay centered"),CoreCount,2);
    TestEqual(TEXT("All four Guard Towers align with their bridge centers"),GuardCount,4);
    std::set<std::string> IDs;
    TestEqual(TEXT("Death child has one separate subordinate definition"),int32(rift::EntityDefinitions().size()),1);
    const auto* Child=rift::FindEntityDefinition("bone_warden_skeleton");
    TestNull(TEXT("Death child never enters playable card pool"),rift::FindCard("bone_warden_skeleton"));
    if(TestNotNull(TEXT("Bone Warden child mechanical definition exists"),Child)) {
        TestEqual(TEXT("Child body HP"),Child->hp,50.);TestEqual(TEXT("Child damage"),Child->damage,36.);
        TestEqual(TEXT("Child interval"),Child->attackInterval,.75);TestEqual(TEXT("Child movement"),Child->moveSpeed,2.);
        TestEqual(TEXT("Child range"),Child->range,.7);TestEqual(TEXT("Child visual scale"),Child->scale,.65);
        TestEqual(TEXT("Child ground troop/structure target mask"),int32(Child->targetMask),5);
        auto* Asset=LoadObject<URiftCardData>(nullptr,TEXT("/Game/Rift/Entities/DA_bone_warden_skeleton.DA_bone_warden_skeleton"));
        if(TestNotNull(TEXT("Child subordinate model data is imported"),Asset)) {
            TestTrue(TEXT("Child imported HP/damage/interval match subordinate definition"),
                Asset->HP==50&&Asset->Damage==36&&FMath::IsNearlyEqual(Asset->AttackSpeed,.75f));
            TestNotNull(TEXT("Child actual skeletal body is bound"),Asset->CharacterMesh.Get());
            TestTrue(TEXT("Child complete animation set is bound"),Asset->Animations.Num()>=6);
        }
    }
    for(const auto& E:ExpectedCards)
    {
        IDs.insert(E.Id);
        const auto* C=rift::FindCard(E.Id);
        const FString Id=UTF8_TO_TCHAR(E.Id);
        if(!TestNotNull(*(Id+TEXT(" exists in authoritative gameplay")),C))continue;
        auto Near=[&](const TCHAR* Field,double Actual,double Expected)
        {TestTrue(*(Id+TEXT(" ")+Field),std::abs(Actual-Expected)<.000001);};
        TestEqual(*(Id+TEXT(" Aether cost")),C->cost,E.Cost);
        TestEqual(*(Id+TEXT(" members per paid cast")),C->count,E.Count);
        Near(TEXT("HP per member"),C->hp,E.HP);Near(TEXT("damage per basic hit"),C->damage,E.Damage);
        Near(TEXT("seconds per basic hit"),C->attackInterval,E.Interval);Near(TEXT("movement tiles per second"),C->moveSpeed,E.Speed);
        Near(TEXT("range in tiles"),C->range,E.Range);Near(TEXT("canonical visual model scale"),C->scale,E.Scale);
        Near(TEXT("projectile tiles per second"),C->projectileSpeed,E.Projectile);Near(TEXT("splash radius"),C->splash,E.Splash);
        Near(TEXT("building lifetime"),C->lifetime,E.Lifetime);Near(TEXT("building footprint"),C->footprint,E.Footprint);
        TestEqual(*(Id+TEXT(" flying")),C->flying,E.Flying);TestEqual(*(Id+TEXT(" basic air targeting")),C->canHitAir,E.AirTargets);
        TestEqual(*(Id+TEXT(" structure-only basic targeting")),C->structuresOnly,E.StructuresOnly);
        TestEqual(*(Id+TEXT(" spell identity")),C->spell,E.Spell);TestEqual(*(Id+TEXT(" building identity")),C->building,E.Building);
        // Spell effect eligibility is independent of the preserved legacy
        // basic-attack air flag. All approved spells affect both troop layers
        // and structures through their explicit mask.
        if(C->spell)TestEqual(*(Id+TEXT(" explicit ground/air/structure effect mask")),int32(C->targetMask),7);
        if(!C->spell)
        {
            Near(TEXT("basic DPS per member excludes special effects"),C->damage/C->attackInterval,E.BasicDPS);
            const auto Analysis=rift::AnalyzeDeck({C->id});
            Near(TEXT("deck analysis uses the full swarm's basic DPS"),Analysis.deploymentDPS,std::round(E.BasicDPS*E.Count*100.)/100.);
            Near(TEXT("deck analysis uses the full swarm's HP"),Analysis.averageHP,E.HP*E.Count);
        }

        // A stale DataAsset can leave collection tooltips and imported game
        // content behind the authoritative patch even when simulation passes.
        auto* A=LoadObject<URiftCardData>(nullptr,*FString::Printf(TEXT("/Game/Rift/Cards/DA_%s.DA_%s"),*Id,*Id));
        if(!TestNotNull(*(Id+TEXT(" production card DataAsset is imported")),A))continue;
        auto Imported=[&](const TCHAR* Field,double Actual,double Expected)
        {TestTrue(*(Id+TEXT(" imported ")+Field),std::abs(Actual-Expected)<.0001);};
        TestEqual(*(Id+TEXT(" imported card identity")),A->CardId,Id);
        TestEqual(*(Id+TEXT(" imported cost")),A->Cost,C->cost);TestEqual(*(Id+TEXT(" imported member count")),A->Count,C->count);
#define CHECK_IMPORTED(AssetField,CoreField) Imported(TEXT(#AssetField),A->AssetField,C->CoreField)
        CHECK_IMPORTED(HP,hp);CHECK_IMPORTED(Damage,damage);CHECK_IMPORTED(AttackSpeed,attackInterval);
        CHECK_IMPORTED(MoveSpeed,moveSpeed);CHECK_IMPORTED(AttackRange,range);CHECK_IMPORTED(BaseModelScale,scale);CHECK_IMPORTED(ProjectileSpeed,projectileSpeed);CHECK_IMPORTED(ShockwaveSpeed,shockwaveSpeed);
        CHECK_IMPORTED(SplashRadius,splash);CHECK_IMPORTED(Lifetime,lifetime);CHECK_IMPORTED(Footprint,footprint);
        CHECK_IMPORTED(TowerDamage,towerDamage);CHECK_IMPORTED(SpellRadius,spellRadius);CHECK_IMPORTED(CastDelay,castDelay);
        CHECK_IMPORTED(ChargeDamage,chargeDamage);CHECK_IMPORTED(SlowPct,slowPct);CHECK_IMPORTED(SlowDuration,slowDuration);
        CHECK_IMPORTED(AuraDamage,auraDamage);CHECK_IMPORTED(AuraRadius,auraRadius);CHECK_IMPORTED(AuraInterval,auraInterval);
        CHECK_IMPORTED(StunDuration,stunDuration);CHECK_IMPORTED(DotDamage,dotDamage);CHECK_IMPORTED(DotDuration,dotDuration);
        CHECK_IMPORTED(DotInterval,dotInterval);
        CHECK_IMPORTED(CollisionRadius,collisionRadius);CHECK_IMPORTED(ChargeMoveSeconds,chargeMoveSeconds);
        CHECK_IMPORTED(ChargedMoveSpeed,chargedMoveSpeed);CHECK_IMPORTED(HPDecayPerSecond,hpDecayPerSecond);
        CHECK_IMPORTED(InitialAttackCooldown,initialAttackCooldown);CHECK_IMPORTED(ShieldHP,shieldHP);
        CHECK_IMPORTED(InvisibilityDuration,invisibilityDuration);CHECK_IMPORTED(DotStructureDamage,dotStructureDamage);
        CHECK_IMPORTED(HealthCloakThreshold,healthCloakThreshold);
        CHECK_IMPORTED(SpawnInterval,spawnInterval);
#undef CHECK_IMPORTED
        Imported(TEXT("FrontSight"),A->FrontSight,C->projectileSpeed>0?FMath::Max(8.,C->range):8.);Imported(TEXT("RearSight"),A->RearSight,5);
        TestEqual(*(Id+TEXT(" imported visual round count")),A->Rounds,C->rounds);
        TestEqual(*(Id+TEXT(" imported flying flag")),A->Flying,C->flying);
        TestEqual(*(Id+TEXT(" imported air targeting")),A->GroundAndAir,C->canHitAir);
        TestEqual(*(Id+TEXT(" imported structure targeting")),A->StructuresOnly,C->structuresOnly);
        TestEqual(*(Id+TEXT(" imported spell flag")),A->Spell,C->spell);
        TestEqual(*(Id+TEXT(" imported building flag")),A->Building,C->building);
        TestEqual(*(Id+TEXT(" imported target mask")),A->TargetMask,int32(C->targetMask));
        TestEqual(*(Id+TEXT(" imported child count")),A->DeathSpawnCount,C->deathSpawnCount);
        TestEqual(*(Id+TEXT(" imported child definition")),A->DeathSpawnId,FString(UTF8_TO_TCHAR(C->deathSpawnId.c_str())));
        TestEqual(*(Id+TEXT(" imported periodic child definition")),A->SpawnId,FString(UTF8_TO_TCHAR(C->spawnId.c_str())));
        TestEqual(*(Id+TEXT(" imported hard expiry")),A->HardExpiry,C->hardExpiry);
        TestEqual(*(Id+TEXT(" imported protected shield overflow")),A->ShieldOverflowProtected,C->shieldOverflowProtected);
        TestEqual(*(Id+TEXT(" imported cloak scope")),A->GlobalFriendlyInvisibility,C->globalFriendlyInvisibility);
        TestEqual(*(Id+TEXT(" imported cloak target drop")),A->BreakExistingLocks,C->breakExistingLocks);
        TestNotNull(*(Id+TEXT(" actual model portrait is available")),A->Illustration.Get());
    }
    for(const auto& C:rift::Cards())
    {
        TestTrue(TEXT("Every playable card is covered by the independent approved contract"),IDs.count(C.id)==1);
        TestTrue(TEXT("Only the Reaper receives the half-health cloak trigger"),
            std::abs(C.healthCloakThreshold-(C.id=="aether_reaper"?.5:0.))<.000001);
        if(C.id=="pulverizer")continue; // Its independent full contract is tested by Pulverizer.
        TestEqual(TEXT("Existing cards gain no shockwave attack"),C.shockwaveSpeed,0.);
        const FString Id=UTF8_TO_TCHAR(C.id.c_str());
        auto Ability=[&](const TCHAR* Field,double Actual,double Expected)
        {TestTrue(*(Id+TEXT(" preserved ")+Field),std::abs(Actual-Expected)<.000001);};
        Ability(TEXT("charge damage"),C.chargeDamage,C.id=="rambeast"?302:0);
        Ability(TEXT("movement slow fraction"),C.slowPct,C.id=="frost_fang"?.30:0);
        Ability(TEXT("slow duration"),C.slowDuration,C.id=="frost_fang"?2:0);
        Ability(TEXT("aura damage"),C.auraDamage,C.id=="storm_raven"?82:0);
        Ability(TEXT("aura interval"),C.auraInterval,C.id=="storm_raven"?3:0);
        Ability(TEXT("aura radius"),C.auraRadius,C.id=="storm_raven"?2:0);
        Ability(TEXT("stun duration"),C.stunDuration,C.id=="storm_raven"?.4:C.id=="thunderstorm"?.25:0);
        Ability(TEXT("DOT damage"),C.dotDamage,C.id=="meteor_shards"?40:0);
        Ability(TEXT("DOT duration"),C.dotDuration,C.id=="meteor_shards"?5:0);
        Ability(TEXT("DOT or wave interval"),C.dotInterval,C.id=="meteor_shards"?1:C.id=="thunderstorm"?.45:0);
        Ability(TEXT("structure DOT damage"),C.dotStructureDamage,C.id=="meteor_shards"?15:0);
        Ability(TEXT("charge movement threshold"),C.chargeMoveSeconds,C.id=="rambeast"?1.5:0);
        Ability(TEXT("charged movement speed"),C.chargedMoveSpeed,C.id=="rambeast"?3.2:0);
        Ability(TEXT("explicit Archer decay"),C.hpDecayPerSecond,C.id=="archer_tower"?25:0);
        Ability(TEXT("initial attack cooldown"),C.initialAttackCooldown,C.id=="archer_tower"?.35:0);
        Ability(TEXT("initial Giant shield"),C.shieldHP,C.id=="iron_giant"?310:0);
        Ability(TEXT("local cloak duration"),C.invisibilityDuration,C.id=="shadow_surge"?2:C.id=="aether_reaper"?1:0);
        TestEqual(*(Id+TEXT(" hard expiry policy")),C.hardExpiry,C.id=="archer_tower"||C.id=="blade_box");
        TestEqual(*(Id+TEXT(" shield overflow policy")),C.shieldOverflowProtected,C.id=="iron_giant");
        TestFalse(*(Id+TEXT(" cloak is local rather than global")),C.globalFriendlyInvisibility);
        TestEqual(*(Id+TEXT(" cloak drops enemy locks")),C.breakExistingLocks,C.id=="shadow_surge"||C.id=="aether_reaper");
        TestEqual(*(Id+TEXT(" child count")),C.deathSpawnCount,C.id=="bone_warden"?4:0);
        TestEqual(*(Id+TEXT(" child subordinate id")),FString(UTF8_TO_TCHAR(C.deathSpawnId.c_str())),
                  C.id=="bone_warden"?FString(TEXT("bone_warden_skeleton")):FString());
        Ability(TEXT("spell delay"),C.castDelay,C.id=="meteor_shards"?.75:C.id=="bullet_burst"?.30:C.id=="nova_flask"?.85:C.id=="shadow_surge"?.75:C.id=="thunderstorm"?.45:0);
        Ability(TEXT("spell radius"),C.spellRadius,C.id=="meteor_shards"?4.5:C.id=="bullet_burst"?2.5:C.id=="nova_flask"||C.id=="thunderstorm"?3:C.id=="shadow_surge"?2:0);
        Ability(TEXT("spell structure damage"),C.towerDamage,C.id=="meteor_shards"?143:C.id=="bullet_burst"?55:C.id=="nova_flask"?185:C.id=="shadow_surge"?50:C.id=="thunderstorm"?33:0);
        TestEqual(*(Id+TEXT(" visual rounds or authoritative storm waves")),C.rounds,C.id=="bullet_burst"?7:C.id=="thunderstorm"?3:0);
        Ability(TEXT("periodic spawn interval"),C.spawnInterval,C.id=="blade_box"?3:0);
        TestEqual(*(Id+TEXT(" periodic child definition")),FString(UTF8_TO_TCHAR(C.spawnId.c_str())),
                  C.id=="blade_box"?FString(TEXT("twin_blades")):FString());
    }

    // Both swarms use ordinary paid deployment and member-based telemetry,
    // rather than paying once per hog or truncating a fifteen-member cast.
    for(const char* Hog: {"mini_stampede","stampede"})
    {
        rift::MatchOptions O;O.aiEnabled={false,false};
        O.decks[0]={Hog,"ironclad","ember_archer","twin_blades","arc_mage","rambeast","archer_tower","bullet_burst"};
        O.decks[1]=rift::DefaultDeck();rift::Match M(O);M.SetAether(rift::Team::Player,10);
        const auto* C=rift::FindCard(Hog);if(!C)continue;
        const FString Id=UTF8_TO_TCHAR(Hog);
        if(!TestTrue(*(Id+TEXT(" enters an eight-card deck")),rift::ValidateDeck(O.decks[0])) ||
           !TestTrue(*(Id+TEXT(" paid cast succeeds")),M.Play(rift::Team::Player,0,{0,8})))continue;
        TestEqual(*(Id+TEXT(" pays one card's cost")),M.State().spent[0],double(C->cost));
        TestEqual(*(Id+TEXT(" subtracts one card's cost")),M.State().aether[0],10.-C->cost);
        int Members=0;
        for(const auto& E:M.State().entities)if(E.cardId==Hog)
        {
            ++Members;TestEqual(*(Id+TEXT(" each live member receives patched HP")),E.hp,133.2);
            TestEqual(*(Id+TEXT(" each member records complete cast size")),E.memberCount,C->count);
        }
        TestEqual(*(Id+TEXT(" spawns the full requested swarm")),Members,C->count);
        const auto Found=M.State().telemetry[0].find(Hog);
        if(TestTrue(*(Id+TEXT(" has paid telemetry")),Found!=M.State().telemetry[0].end()))
        {
            TestEqual(*(Id+TEXT(" records one paid cast")),Found->second.plays,uint64(1));
            TestEqual(*(Id+TEXT(" records all members separately")),Found->second.spawns,uint64(C->count));
            TestEqual(*(Id+TEXT(" records the paid Aether cost")),Found->second.spent,double(C->cost));
        }
        rift::Entity Source,Victim;Source.id=100;Source.cardId=Hog;Victim.id=101;Victim.team=rift::Team::Enemy;
        Victim.cardId="ironclad";Victim.kind=rift::EntityKind::Troop;
        TestEqual(*(Id+TEXT(" obeys requested ground troop targeting")),M.CanTarget(Source,Victim),!C->structuresOnly);
        Victim.flying=true;TestFalse(*(Id+TEXT(" cannot attack air troops")),M.CanTarget(Source,Victim));
        Victim.flying=false;Victim.kind=rift::EntityKind::Guard;
        TestTrue(*(Id+TEXT(" can attack Guard Towers")),M.CanTarget(Source,Victim));
        Victim.kind=rift::EntityKind::Core;TestTrue(*(Id+TEXT(" can attack the Core")),M.CanTarget(Source,Victim));
        Victim.kind=rift::EntityKind::Building;TestTrue(*(Id+TEXT(" can attack player buildings")),M.CanTarget(Source,Victim));
    }

    // A complete current-roster snapshot must remain accepted as new cards are
    // added; unknown keys and telemetry beyond that exact roster still fail.
    rift::MatchOptions Quiet;Quiet.aiEnabled={false,false};rift::Match ReplayFixture(Quiet);
    auto Original=ReplayFixture.State();
    for(int Team=0;Team<2;++Team)for(const auto& C:rift::Cards())Original.telemetry[Team][C.id]={};
    Original.telemetry[0]["stampede"].spawns=15;
    const auto JSON=URiftReplaySubsystem::SnapshotJSON(Original);rift::Snapshot Restored;
    if(TestTrue(TEXT("Production replay parser accepts every current card's telemetry"),URiftReplaySubsystem::SnapshotFromJSON(JSON,Restored)))
    {
        TestEqual(TEXT("Replay retains every current player telemetry key"),int32(Restored.telemetry[0].size()),int32(rift::Cards().size()));
        TestEqual(TEXT("Replay retains every current enemy telemetry key"),int32(Restored.telemetry[1].size()),int32(rift::Cards().size()));
        TestEqual(TEXT("Replay preserves the fifteen-hog member counter"),Restored.telemetry[0]["stampede"].spawns,uint64(15));
    }
    auto PlayerMetrics=JSON->GetArrayField(TEXT("teams"))[0]->AsObject()->GetObjectField(TEXT("telemetry"));
    PlayerMetrics->RemoveField(TEXT("ironclad"));
    PlayerMetrics->SetObjectField(TEXT("unknown_card"),PlayerMetrics->GetObjectField(TEXT("stampede")));
    TestFalse(TEXT("Replay continues rejecting unknown card keys within the new roster bound"),URiftReplaySubsystem::SnapshotFromJSON(JSON,Restored));
    PlayerMetrics->SetObjectField(TEXT("ironclad"),PlayerMetrics->GetObjectField(TEXT("stampede")));
    TestFalse(TEXT("Replay continues rejecting telemetry exceeding the canonical roster size"),URiftReplaySubsystem::SnapshotFromJSON(JSON,Restored));

    // Meta Lab samples the same generated decks as AI battles. Merely adding
    // swarms to Cards() must not leave them outside the selectable deck pools.
    int MiniAppearances=0,StampedeAppearances=0;
    for(const char* Style:{"beatdown","aggro","control","cycle","split","spell_cycle","counter"})
    {
        int StyleMini=0,StyleStampede=0;
        for(uint32 Seed=1;Seed<=256;++Seed)
        {
            const auto Deck=rift::BuildAIDeck(Style,Seed*1664525U+1013904223U);
            TestTrue(TEXT("Every generated personality deck stays legal under the expanded roster"),rift::ValidateDeck(Deck));
            for(const auto& Id:Deck){StyleMini+=Id=="mini_stampede";StyleStampede+=Id=="stampede";}
        }
        MiniAppearances+=StyleMini;StampedeAppearances+=StyleStampede;
        AddInfo(FString::Printf(TEXT("Balance AI deck pool %s: 256 generated decks, Mini Stampede %d appearances, Stampede %d appearances."),UTF8_TO_TCHAR(Style),StyleMini,StyleStampede));
    }
    TestTrue(TEXT("Mini Stampede is actually selected by AI and Meta generated-deck policy"),MiniAppearances>0);
    TestTrue(TEXT("Stampede is actually selected by AI and Meta generated-deck policy"),StampedeAppearances>0);
    AddInfo(TEXT("Retained balance contract: six aligned tower bodies, exact stats/abilities/imports for all twenty-four approved cards, zero new wave attacks on existing cards, paid hog counts/costs/targets, derived DPS and current-roster replay telemetry verified. Pulverizer has a separate focused contract."));
    return !HasAnyErrors();
}
#endif
