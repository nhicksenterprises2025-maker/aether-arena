#if WITH_DEV_AUTOMATION_TESTS
#include "RiftAssetLibrary.h"
#include "RiftCardData.h"
#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "RiftReplaySubsystem.h"
#include "Presentation/RiftArenaPresentation.h"
#include "Presentation/RiftUnitVisual.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include <cmath>
#include <set>

namespace
{
bool SamePulverizerPose(const TArray<FTransform>& A,const TArray<FTransform>& B)
{
    if(A.Num()!=B.Num()||A.IsEmpty())return false;
    for(int32 I=0;I<A.Num();++I)if(!A[I].Equals(B[I],.0001f))return false;
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftPulverizerTest,"Rift.Integration.Pulverizer",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftPulverizerTest::RunTest(const FString& Parameters)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox")))
    {AddError(TEXT("Pulverizer fixture requires isolated RiftSaveRoot/UserDir and RiftAutomationSandbox."));return false;}
    const auto* C=rift::FindCard("pulverizer");
    if(!TestNotNull(TEXT("Pulverizer exists in authoritative gameplay"),C))return false;
    TestEqual(TEXT("Approved expansion has twenty-one playable cards"),int32(rift::Cards().size()),21);
    TestEqual(TEXT("Pulverizer costs three Aether"),C->cost,3);
    TestEqual(TEXT("One building per paid Pulverizer cast"),C->count,1);
    auto Stat=[&](const TCHAR* Field,double Actual,double Expected)
    {TestTrue(FString(TEXT("Pulverizer requested "))+Field,std::abs(Actual-Expected)<.000001);};
    Stat(TEXT("HP"),C->hp,700);Stat(TEXT("damage"),C->damage,65);
    Stat(TEXT("attack interval"),C->attackInterval,.9);Stat(TEXT("basic DPS"),C->damage/C->attackInterval,72.22222222222222);
    Stat(TEXT("movement speed"),C->moveSpeed,0);Stat(TEXT("attack range"),C->range,4);
    Stat(TEXT("wave radius"),C->splash,4);Stat(TEXT("wave travel speed"),C->shockwaveSpeed,8);
    Stat(TEXT("maximum wave travel time"),C->splash/C->shockwaveSpeed,.5);
    Stat(TEXT("lifetime"),C->lifetime,45);Stat(TEXT("footprint"),C->footprint,1.65);
    Stat(TEXT("model scale"),C->scale,1.05);Stat(TEXT("projectile speed"),C->projectileSpeed,0);
    Stat(TEXT("movement slow fraction"),C->slowPct,.10);Stat(TEXT("slow duration"),C->slowDuration,1.5);
    Stat(TEXT("explicit natural HP decay"),C->hpDecayPerSecond,700./45.);
    Stat(TEXT("initial attack cooldown"),C->initialAttackCooldown,.35);
    TestFalse(TEXT("Pulverizer retains natural expiry rather than a hard expiry"),C->hardExpiry);
    TestEqual(TEXT("Pulverizer target mask is ground troops only"),int32(C->targetMask),1);
    TestTrue(TEXT("Pulverizer is a stationary ground defensive building"),C->building&&!C->spell&&!C->flying&&!C->canHitAir&&!C->structuresOnly);
    Stat(TEXT("charge damage"),C->chargeDamage,0);Stat(TEXT("aura damage"),C->auraDamage,0);
    Stat(TEXT("aura interval"),C->auraInterval,0);Stat(TEXT("aura radius"),C->auraRadius,0);
    Stat(TEXT("stun duration"),C->stunDuration,0);Stat(TEXT("DOT damage"),C->dotDamage,0);
    Stat(TEXT("DOT duration"),C->dotDuration,0);Stat(TEXT("DOT interval"),C->dotInterval,0);
    Stat(TEXT("spell cast delay"),C->castDelay,0);Stat(TEXT("spell radius"),C->spellRadius,0);
    Stat(TEXT("spell tower damage"),C->towerDamage,0);TestEqual(TEXT("Pulverizer has no Bullet Burst rounds"),C->rounds,0);
    const auto Analysis=rift::AnalyzeDeck({"pulverizer"});
    Stat(TEXT("analysis DPS"),Analysis.deploymentDPS,72.22);Stat(TEXT("analysis HP"),Analysis.averageHP,700);
    TestEqual(TEXT("Analysis classifies Pulverizer as one building"),Analysis.buildings,1);
    TestEqual(TEXT("Analysis never claims sustained anti-air"),Analysis.sustainedAntiAir,0);

    // Exercise the production export used by asset import and stat sheets.
    TArray<TSharedPtr<FJsonValue>> Definitions;
    if(!TestTrue(TEXT("Native card definition export is valid JSON"),FJsonSerializer::Deserialize(
        TJsonReaderFactory<>::Create(URiftAssetLibrary::CardDefinitionsJSON()),Definitions)))return false;
    TestEqual(TEXT("Native export contains all twenty-one playable definitions"),Definitions.Num(),21);
    std::set<std::string> ExportedIds;
    for(const auto& Value:Definitions)
    {
        const auto Definition=Value->AsObject();
        if(!TestTrue(TEXT("Exported definition is an object"),Definition.IsValid()))continue;
        const FString Id=Definition->GetStringField(TEXT("id"));
        TestTrue(Id+TEXT(" occurs once in the native export"),ExportedIds.insert(TCHAR_TO_UTF8(*Id)).second);
        TestEqual(Id+TEXT(" exports its own wave speed"),Definition->GetNumberField(TEXT("shockwaveSpeed")),Id==TEXT("pulverizer")?8.:0.);
        if(Id==TEXT("pulverizer"))
        {
            TestEqual(TEXT("Export preserves Pulverizer interval"),Definition->GetNumberField(TEXT("attackInterval")),.9);
            TestEqual(TEXT("Export preserves Pulverizer slow duration"),Definition->GetNumberField(TEXT("slowDuration")),1.5);
            TestTrue(TEXT("Export preserves defensive building identity"),Definition->GetBoolField(TEXT("building")));
            TestFalse(TEXT("Export preserves ground-only attacks"),Definition->GetBoolField(TEXT("canHitAir")));
        }
    }

    auto* Card=LoadObject<URiftCardData>(nullptr,TEXT("/Game/Rift/Cards/DA_pulverizer.DA_pulverizer"));
    if(!TestNotNull(TEXT("Production Pulverizer DataAsset is imported"),Card))return false;
    TestEqual(TEXT("Imported card identity"),Card->CardId,FString(TEXT("pulverizer")));
    TestEqual(TEXT("Imported cost"),Card->Cost,3);TestEqual(TEXT("Imported count"),Card->Count,1);
    auto Imported=[&](const TCHAR* Field,double Actual,double Expected)
    {TestTrue(FString(TEXT("Pulverizer imported "))+Field,std::abs(Actual-Expected)<.0001);};
#define PULVERIZER_STAT(AssetField,CoreField) Imported(TEXT(#AssetField),Card->AssetField,C->CoreField)
    PULVERIZER_STAT(HP,hp);PULVERIZER_STAT(Damage,damage);PULVERIZER_STAT(AttackSpeed,attackInterval);
    PULVERIZER_STAT(MoveSpeed,moveSpeed);PULVERIZER_STAT(AttackRange,range);PULVERIZER_STAT(BaseModelScale,scale);
    PULVERIZER_STAT(ProjectileSpeed,projectileSpeed);PULVERIZER_STAT(ShockwaveSpeed,shockwaveSpeed);
    PULVERIZER_STAT(SplashRadius,splash);PULVERIZER_STAT(Lifetime,lifetime);PULVERIZER_STAT(Footprint,footprint);
    PULVERIZER_STAT(SlowPct,slowPct);PULVERIZER_STAT(SlowDuration,slowDuration);PULVERIZER_STAT(TowerDamage,towerDamage);
    PULVERIZER_STAT(SpellRadius,spellRadius);PULVERIZER_STAT(CastDelay,castDelay);PULVERIZER_STAT(ChargeDamage,chargeDamage);
    PULVERIZER_STAT(AuraDamage,auraDamage);PULVERIZER_STAT(AuraInterval,auraInterval);PULVERIZER_STAT(AuraRadius,auraRadius);
    PULVERIZER_STAT(StunDuration,stunDuration);PULVERIZER_STAT(DotDamage,dotDamage);
    PULVERIZER_STAT(DotDuration,dotDuration);PULVERIZER_STAT(DotInterval,dotInterval);
    PULVERIZER_STAT(CollisionRadius,collisionRadius);PULVERIZER_STAT(HPDecayPerSecond,hpDecayPerSecond);
    PULVERIZER_STAT(InitialAttackCooldown,initialAttackCooldown);
#undef PULVERIZER_STAT
    Imported(TEXT("front detection"),Card->FrontSight,rift::detection::Front);
    Imported(TEXT("rear detection"),Card->RearSight,rift::detection::Rear);
    TestTrue(TEXT("Imported classification matches gameplay"),Card->Building&&!Card->Spell&&!Card->Flying&&!Card->GroundAndAir&&!Card->StructuresOnly&&Card->Rounds==0);
    TestEqual(TEXT("Imported ground-troop-only target mask"),Card->TargetMask,1);
    TestFalse(TEXT("Imported natural expiry policy"),Card->HardExpiry);
    auto* Mesh=Card->CharacterMesh.Get();auto* Portrait=Card->Illustration.Get();
    if(!TestNotNull(TEXT("Pulverizer's whole machine is a skeletal model"),Mesh)||
       !TestNotNull(TEXT("Pulverizer has its actual-model card portrait"),Portrait))return false;
    TestNull(TEXT("A duplicate static Pulverizer machine is not bound"),Card->StructureMesh.Get());
    TestEqual(TEXT("Whole mechanism uses its authored skeletal asset"),Mesh->GetPathName(),FString(TEXT("/Game/Rift/Characters/pulverizer/SK_pulverizer.SK_pulverizer")));
    TestEqual(TEXT("Card art uses the Pulverizer model render"),Portrait->GetPathName(),FString(TEXT("/Game/Rift/CardArt/T_Card_pulverizer.T_Card_pulverizer")));
    // NullRHI automation has no GPU texture resource; verify imported source pixels.
    TestTrue(TEXT("Portrait retains the full production source resolution"),Portrait->Source.GetSizeX()==768&&Portrait->Source.GetSizeY()==960);
    TestNotNull(TEXT("Whole mechanism has an imported physics asset"),Mesh->GetPhysicsAsset());
    TestTrue(TEXT("Whole mechanism retains its authored LOD hierarchy"),Mesh->GetLODNum()>=4);
    // The standard FBX path keeps the SKEL_<id> armature wrapper in UE, as it
    // does for every prior skeletal card, in addition to the fifteen authored bones.
    TestEqual(TEXT("Mechanism contains fifteen dedicated bones and its FBX armature root"),Mesh->GetRefSkeleton().GetNum(),16);
    TestTrue(TEXT("Mechanism retains its standard imported armature wrapper"),Mesh->GetRefSkeleton().FindBoneIndex(TEXT("SKEL_pulverizer"))!=INDEX_NONE);
    for(FName Bone:{FName(TEXT("root")),FName(TEXT("hammer")),FName(TEXT("compression_ring")),FName(TEXT("crystal")),FName(TEXT("crystal_fractures")),FName(TEXT("ring_fragment_-1")),FName(TEXT("ring_fragment_1")),FName(TEXT("piston_0")),FName(TEXT("piston_1")),FName(TEXT("piston_2")),FName(TEXT("piston_3")),FName(TEXT("attack_origin")),FName(TEXT("impact_origin")),FName(TEXT("hp_anchor")),FName(TEXT("status_anchor"))})
        TestTrue(Bone.ToString()+TEXT(" is an actual imported mechanism bone"),Mesh->GetRefSkeleton().FindBoneIndex(Bone)!=INDEX_NONE);
    TestEqual(TEXT("Whole mechanism binds all nine standard clips"),Card->Animations.Num(),9);
    for(FName Clip:{FName(TEXT("Idle")),FName(TEXT("Locomotion")),FName(TEXT("Attack")),FName(TEXT("Hit")),FName(TEXT("Death")),FName(TEXT("Deploy")),FName(TEXT("Status")),FName(TEXT("Turn")),FName(TEXT("Acquire"))})
    {
        const auto* Bound=Card->Animations.Find(Clip);auto* Animation=Bound?Bound->Get():nullptr;
        if(!TestNotNull(Clip.ToString()+TEXT(" is bound to an imported animation"),Animation))continue;
        TestEqual(Clip.ToString()+TEXT(" uses Pulverizer's authored asset"),Animation->GetPathName(),FString::Printf(TEXT("/Game/Rift/Characters/pulverizer/Animations/AN_pulverizer_%s.AN_pulverizer_%s"),*Clip.ToString(),*Clip.ToString()));
        TestTrue(Clip.ToString()+TEXT(" shares the whole mechanism skeleton"),Animation->GetSkeleton()==Mesh->GetSkeleton());
        TestTrue(Clip.ToString()+TEXT(" has evaluated animation time"),Animation->GetPlayLength()>0);
        if(Clip==TEXT("Attack"))TestTrue(TEXT("Attack preserves the authored thirty-eight-hundredths-second clip"),FMath::IsNearlyEqual(Animation->GetPlayLength(),.38f,.001f));
    }
    for(const auto& Old:rift::Cards())if(Old.id!="pulverizer")
    {
        TestEqual(TEXT("No prior card gains a wave attack"),Old.shockwaveSpeed,0.);
        const FString Id=UTF8_TO_TCHAR(Old.id.c_str());
        auto* OldAsset=LoadObject<URiftCardData>(nullptr,*FString::Printf(TEXT("/Game/Rift/Cards/DA_%s.DA_%s"),*Id,*Id));
        if(TestNotNull(Id+TEXT(" still has its imported card"),OldAsset))
            TestTrue(Id+TEXT(" retains distinct existing art"),OldAsset->Illustration&&OldAsset->Illustration!=Portrait);
    }

    rift::MatchOptions Options;Options.aiEnabled={false,false};
    Options.decks[0]={"pulverizer","ironclad","ember_archer","twin_blades","arc_mage","rambeast","archer_tower","bullet_burst"};
    Options.decks[1]=rift::DefaultDeck();rift::Match Match(Options);Match.SetAether(rift::Team::Player,10);
    TestTrue(TEXT("Pulverizer fits a legal ordinary eight-card deck"),rift::ValidateDeck(Options.decks[0]));
    if(!TestTrue(TEXT("Ordinary paid Pulverizer placement succeeds"),Match.Play(rift::Team::Player,0,{0,8})))return false;
    TestEqual(TEXT("Paid deployment spends exactly three Aether"),Match.State().spent[0],3.);
    TestEqual(TEXT("Paid deployment subtracts exactly three Aether"),Match.State().aether[0],7.);
    int32 Buildings=0;rift::Entity Building;
    for(const auto& E:Match.State().entities)if(E.cardId=="pulverizer")
    {++Buildings;Building=E;TestTrue(TEXT("Paid cast creates a real stationary building body"),E.kind==rift::EntityKind::Building&&!E.flying);TestEqual(TEXT("Building starts with requested HP"),E.hp,700.);TestEqual(TEXT("Paid building collision retains the canonical footprint clearance"),E.radius,C->footprint*.52);}
    TestEqual(TEXT("Paid deployment creates exactly one building"),Buildings,1);
    if(Buildings!=1)return false;
    const auto& Tele=Match.State().telemetry[0].at("pulverizer");
    TestEqual(TEXT("Telemetry records one ordinary paid cast"),Tele.plays,uint64(1));
    TestEqual(TEXT("Telemetry records one building spawn"),Tele.spawns,uint64(1));
    TestEqual(TEXT("Telemetry records the ordinary Aether cost"),Tele.spent,3.);
    rift::Entity Target;Target.id=8001;Target.team=rift::Team::Enemy;Target.cardId="ironclad";
    Target.hp=Target.maxHp=822;Target.radius=.4;Target.position={0,5};
    TestTrue(TEXT("Wave building can target an enemy ground troop"),Match.CanTarget(Building,Target));
    Target.flying=true;TestFalse(TEXT("Wave building excludes air troops"),Match.CanTarget(Building,Target));Target.flying=false;
    for(auto Kind:{rift::EntityKind::Building,rift::EntityKind::Guard,rift::EntityKind::Core})
    {Target.kind=Kind;TestFalse(TEXT("Approved wave building excludes structures and towers"),Match.CanTarget(Building,Target));}
    Target.kind=rift::EntityKind::Troop;
    TestEqual(TEXT("Shared front detection is eight tiles"),rift::detection::Front,8.);
    TestEqual(TEXT("Shared rear detection is five tiles"),rift::detection::Rear,5.);
    // Use center distance even for large bodies, current targets and forced pulls.
    for(const auto& CardDefinition:rift::Cards())
    {
        auto Source=Building;Source.cardId=CardDefinition.id;Source.facing={0,-1};Source.position={0,0};Source.radius=2;
        Source.target=Source.forcedTarget=Target.id;Source.forcedUntil=100;
        Target.radius=2;Target.position={0,-8};
        TestTrue(TEXT("Every card detects the exact front boundary"),Match.InSight(Source,Target));
        Target.position.z=-8.001;TestFalse(TEXT("No current target, forced pull or body radius enlarges front detection"),Match.InSight(Source,Target));
        Target.position.z=5;TestTrue(TEXT("Every card detects the exact rear boundary"),Match.InSight(Source,Target));
        Target.position.z=5.001;TestFalse(TEXT("No current target, forced pull or body radius enlarges rear detection"),Match.InSight(Source,Target));
    }

    // Native replay JSON retains in-flight rings and independently expiring
    // slows. A weak refresh cannot erase the separate stronger Frost duration.
    Match.Step(1);auto Snapshot=Match.State();Target.radius=.4;Target.position={0,5};
    rift::SlowEffect Frost;Frost.source=8002;Frost.playId=2;Frost.team=rift::Team::Player;Frost.cardId="frost_fang";Frost.pct=.30;Frost.born=.7;Frost.until=2.7;
    rift::SlowEffect Weak;Weak.source=Building.id;Weak.playId=Building.playId;Weak.team=rift::Team::Player;Weak.cardId="pulverizer";Weak.pct=.10;Weak.born=.9;Weak.until=2.4;
    Target.slows={Frost,Weak};Target.slowPct=.30;Target.slowUntil=2.7;Target.slowSource=2;Target.slowTrackedFrom=.7;
    Snapshot.entities.push_back(Target);
    rift::Shockwave Wave;Wave.id=9001;Wave.source=Building.id;Wave.playId=Building.playId;Wave.team=rift::Team::Player;
    Wave.cardId="pulverizer";Wave.origin=Building.position;Wave.radius=4;Wave.speed=8;Wave.progress=2;Wave.born=.75;Wave.expires=1.25;Wave.hitTargets={Target.id};Snapshot.shockwaves={Wave};
    rift::Snapshot Restored;
    if(TestTrue(TEXT("Native replay parser roundtrips a live wave with two independent slows"),URiftReplaySubsystem::SnapshotFromJSON(URiftReplaySubsystem::SnapshotJSON(Snapshot),Restored)))
    {
        TestEqual(TEXT("Wave survives native snapshot roundtrip"),int32(Restored.shockwaves.size()),1);
        if(Restored.shockwaves.size()==1)
        {
            const auto& W=Restored.shockwaves[0];
            TestTrue(TEXT("Wave identity, source, paid cast and team survive"),W.id==Wave.id&&W.source==Wave.source&&W.playId==Wave.playId&&W.team==Wave.team&&W.cardId==Wave.cardId);
            TestTrue(TEXT("Wave origin, progress, radius, speed and lifetime survive"),W.origin.x==Wave.origin.x&&W.origin.z==Wave.origin.z&&W.progress==2&&W.radius==4&&W.speed==8&&W.born==.75&&W.expires==1.25);
            TestTrue(TEXT("Wave remembers already-hit targets"),W.hitTargets==Wave.hitTargets);
        }
        for(const auto& E:Restored.entities)if(E.id==Target.id)
        {
            TestEqual(TEXT("Independent Frost and Pulverizer slow records survive"),int32(E.slows.size()),2);
            TestTrue(TEXT("Effective stronger slow and its own expiration survive"),E.slowPct==.30&&E.slowUntil==2.7);
            if(E.slows.size()==2)TestTrue(TEXT("Replay retains both per-card sources and durations"),E.slows[0].cardId=="frost_fang"&&E.slows[0].pct==.30&&E.slows[0].until==2.7&&E.slows[1].cardId=="pulverizer"&&E.slows[1].pct==.10&&E.slows[1].until==2.4);
        }
    }
    auto Malformed=[&](const TCHAR* Label,auto Mutate)
    {auto JSON=URiftReplaySubsystem::SnapshotJSON(Snapshot);Mutate(JSON);TestFalse(Label,URiftReplaySubsystem::SnapshotFromJSON(JSON,Restored));};
    Malformed(TEXT("Replay rejects duplicate shockwave identities"),[](auto JSON){auto Waves=JSON->GetArrayField(TEXT("shockwaves"));const auto Duplicate=Waves[0];Waves.Add(Duplicate);JSON->SetArrayField(TEXT("shockwaves"),Waves);});
    Malformed(TEXT("Replay rejects duplicate already-hit target identities"),[](auto JSON){auto W=JSON->GetArrayField(TEXT("shockwaves"))[0]->AsObject();auto Hits=W->GetArrayField(TEXT("hitTargets"));const auto Duplicate=Hits[0];Hits.Add(Duplicate);W->SetArrayField(TEXT("hitTargets"),Hits);});
    Malformed(TEXT("Replay rejects zero wave speed"),[](auto JSON){JSON->GetArrayField(TEXT("shockwaves"))[0]->AsObject()->SetNumberField(TEXT("speed"),0);});
    Malformed(TEXT("Replay rejects unknown wave cards"),[](auto JSON){JSON->GetArrayField(TEXT("shockwaves"))[0]->AsObject()->SetStringField(TEXT("cardId"),TEXT("unknown_card"));});
    Malformed(TEXT("Replay rejects a wave card identity that disagrees with its captured source"),[](auto JSON){JSON->GetArrayField(TEXT("shockwaves"))[0]->AsObject()->SetStringField(TEXT("cardId"),TEXT("ironclad"));});
    Malformed(TEXT("Replay rejects a wave team that disagrees with its captured source"),[](auto JSON){JSON->GetArrayField(TEXT("shockwaves"))[0]->AsObject()->SetNumberField(TEXT("team"),1);});
    Malformed(TEXT("Replay rejects a wave cast identity that disagrees with its captured source"),[](auto JSON){auto W=JSON->GetArrayField(TEXT("shockwaves"))[0]->AsObject();W->SetNumberField(TEXT("playId"),W->GetNumberField(TEXT("playId"))+1);});
    Malformed(TEXT("Replay rejects progress beyond the wave edge"),[](auto JSON){JSON->GetArrayField(TEXT("shockwaves"))[0]->AsObject()->SetNumberField(TEXT("progress"),4.01);});
    Malformed(TEXT("Replay rejects a fabricated wave travel lifetime"),[](auto JSON){JSON->GetArrayField(TEXT("shockwaves"))[0]->AsObject()->SetNumberField(TEXT("expires"),1.4);});
    Malformed(TEXT("Replay rejects duplicate per-card slow components"),[](auto JSON){auto E=JSON->GetArrayField(TEXT("entities")).Last()->AsObject();auto Slows=E->GetArrayField(TEXT("slows"));const auto Duplicate=Slows[0];Slows.Add(Duplicate);E->SetArrayField(TEXT("slows"),Slows);});
    auto SourceRetired=URiftReplaySubsystem::SnapshotJSON(Snapshot);auto Remaining=SourceRetired->GetArrayField(TEXT("entities"));
    Remaining.RemoveAll([&](const auto& Value){return uint64(Value->AsObject()->GetNumberField(TEXT("id")))==Building.id;});SourceRetired->SetArrayField(TEXT("entities"),Remaining);
    if(TestTrue(TEXT("An already traveling wave remains readable after its source leaves the snapshot"),URiftReplaySubsystem::SnapshotFromJSON(SourceRetired,Restored)))
        TestTrue(TEXT("Source retirement preserves captured wave identity and mechanics"),Restored.shockwaves.size()==1&&Restored.shockwaves[0].source==Building.id&&Restored.shockwaves[0].playId==Building.playId&&Restored.shockwaves[0].radius==4&&Restored.shockwaves[0].speed==8);
    auto LegacySource=URiftReplaySubsystem::SnapshotJSON(Snapshot);
    for(const auto& Value:LegacySource->GetArrayField(TEXT("entities")))if(uint64(Value->AsObject()->GetNumberField(TEXT("id")))==Building.id)Value->AsObject()->RemoveField(TEXT("playId"));
    if(TestTrue(TEXT("A legacy source without an optional recorded cast ID does not invent an identity mismatch"),URiftReplaySubsystem::SnapshotFromJSON(LegacySource,Restored)))
        for(const auto& E:Restored.entities)if(E.id==Building.id)TestEqual(TEXT("Missing legacy entity cast ID is recovered from its captured body observation"),E.playId,Building.playId);
    auto ConflictingLegacy=URiftReplaySubsystem::SnapshotJSON(Snapshot);
    for(const auto& Value:ConflictingLegacy->GetArrayField(TEXT("entities")))if(uint64(Value->AsObject()->GetNumberField(TEXT("id")))==Building.id)Value->AsObject()->RemoveField(TEXT("playId"));
    ConflictingLegacy->GetArrayField(TEXT("shockwaves"))[0]->AsObject()->SetNumberField(TEXT("playId"),double(Building.playId+1));
    TestFalse(TEXT("A legacy wave cannot contradict an available captured body cast ID"),URiftReplaySubsystem::SnapshotFromJSON(ConflictingLegacy,Restored));
    auto UnavailableLegacy=URiftReplaySubsystem::SnapshotJSON(Snapshot);
    for(const auto& Value:UnavailableLegacy->GetArrayField(TEXT("entities")))if(uint64(Value->AsObject()->GetNumberField(TEXT("id")))==Building.id)Value->AsObject()->RemoveField(TEXT("playId"));
    for(const auto& Team:UnavailableLegacy->GetArrayField(TEXT("teams")))Team->AsObject()->RemoveField(TEXT("entityTelemetry"));
    if(TestTrue(TEXT("Legacy wave stays readable when neither entity nor body observation captured a cast ID"),URiftReplaySubsystem::SnapshotFromJSON(UnavailableLegacy,Restored)))
        for(const auto& E:Restored.entities)if(E.id==Building.id)TestEqual(TEXT("An unavailable legacy entity cast ID stays unknown"),E.playId,rift::PlayId(0));
    auto Legacy=URiftReplaySubsystem::SnapshotJSON(Snapshot);Legacy->RemoveField(TEXT("shockwaves"));
    for(const auto& E:Legacy->GetArrayField(TEXT("entities")))E->AsObject()->RemoveField(TEXT("slows"));
    if(TestTrue(TEXT("Legacy recordings without wave or slow-component arrays remain accepted"),URiftReplaySubsystem::SnapshotFromJSON(Legacy,Restored)))
    {TestTrue(TEXT("Legacy replay does not invent a wave"),Restored.shockwaves.empty());for(const auto& E:Restored.entities)TestTrue(TEXT("Legacy replay does not invent slow components"),E.slows.empty());}

    // Verify evaluated production bones rather than accepting clip filenames.
    auto* GI=NewObject<UGameInstance>(GEngine);GI->InitializeStandalone(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto* World=GI->GetWorld();
    if(!TestNotNull(TEXT("Pulverizer visual fixture has a native world"),World)){GI->Shutdown();return false;}
    ON_SCOPE_EXIT {World->DestroyWorld(false);GI->Shutdown();GEngine->DestroyWorldContext(World);};
    auto* Actor=World->SpawnActor<ARiftUnitVisual>();
    if(!TestNotNull(TEXT("Whole mechanism visual actor spawns"),Actor)||!TestTrue(TEXT("Whole mechanism initializes from the actual paid body and imported card"),Actor->InitializeEntity(Building,Card,1.)))return false;
    auto* ActualMesh=Actor->FindComponentByClass<USkeletalMeshComponent>();
    if(!TestNotNull(TEXT("Whole mechanism has its evaluated skeletal component"),ActualMesh))return false;
    TestTrue(TEXT("Production actor renders the imported whole mechanism"),ActualMesh->GetSkeletalMeshAsset()==Mesh);
    TArray<UStaticMeshComponent*> StaticParts;Actor->GetComponents(StaticParts);
    for(auto* Part:StaticParts)TestNull(TEXT("Living Pulverizer has no duplicate static machinery"),Part->GetStaticMesh().Get());
    const FVector Extents=Mesh->GetBounds().BoxExtent*2;const FVector Scale=ActualMesh->GetRelativeScale3D();
    TestTrue(TEXT("Visual XY fits the unchanged collision footprint"),FMath::IsNearlyEqual(FMath::Max(Extents.X,Extents.Y)*Scale.X,C->footprint*RiftArenaGeometry::UnitsPerTile,.01)&&FMath::IsNearlyEqual(Scale.X,Scale.Y,.00001));
    TestTrue(TEXT("Whole mechanism retains its requested height scale relative to the larger tile"),FMath::IsNearlyEqual(Scale.Z,C->scale*RiftArenaGeometry::WorldScale,.00001));
    auto VisualState=Match.State();VisualState.elapsed=1.;VisualState.entities={Building};Actor->Synchronize(Building,VisualState,0);
    const FTransform RootBefore=ActualMesh->GetSocketTransform(TEXT("root"),RTS_Component);
    const FVector HammerBefore=ActualMesh->GetSocketTransform(TEXT("hammer"),RTS_Component).GetLocation();
    Actor->AttackAt(1.3,65);
    const FVector HammerContact=ActualMesh->GetSocketTransform(TEXT("hammer"),RTS_Component).GetLocation();
    TestEqual(TEXT("Native wave release selects the authored Attack clip"),Actor->CurrentAnimation(),FName(TEXT("Attack")));
    TestTrue(TEXT("Wave release uses the exact imported authored contact pose"),FMath::IsNearlyEqual(Actor->AnimationCycleFraction(),Card->AttackContactNormalized,.00001f)&&Actor->AnimationBlendAlpha()==1.f);
    TestTrue(TEXT("Hydraulic hammer visibly compresses at release"),FVector::Distance(HammerBefore,HammerContact)>10.);
    TestTrue(TEXT("Stationary foundation does not move during the strike"),ActualMesh->GetSocketTransform(TEXT("root"),RTS_Component).Equals(RootBefore,.001));
    TestTrue(TEXT("Wave release follows the grounded attack socket"),Actor->AttackLocation().Equals(ActualMesh->GetSocketLocation(TEXT("attack_origin")),.001)&&Actor->AttackLocation().Z<Actor->GetActorLocation().Z+25.);
    TestTrue(TEXT("Impact follows the grounded mechanism socket"),Actor->ImpactLocation().Equals(ActualMesh->GetSocketLocation(TEXT("impact_origin")),.001));
    Building.cooldown=.9;VisualState.elapsed=1.3;VisualState.entities={Building};Actor->Synchronize(Building,VisualState,0);
    const auto PausedPose=ActualMesh->GetBoneSpaceTransforms();const FVector PausedPosition=Actor->GetActorLocation();
    for(int32 Frame=0;Frame<16;++Frame)Actor->Synchronize(Building,VisualState,.1f);
    TestTrue(TEXT("Paused strike freezes exact real bones and building position"),SamePulverizerPose(PausedPose,ActualMesh->GetBoneSpaceTransforms())&&Actor->GetActorLocation().Equals(PausedPosition,.001));
    VisualState.elapsed=1.444;Building.cooldown=.756;VisualState.entities={Building};Actor->Synchronize(Building,VisualState,.144f);
    TestTrue(TEXT("Hammer visibly recovers after the wave release"),FVector::Distance(HammerContact,ActualMesh->GetSocketTransform(TEXT("hammer"),RTS_Component).GetLocation())>3.&&!SamePulverizerPose(PausedPose,ActualMesh->GetBoneSpaceTransforms()));
    const double ModelTop=FMath::Max(ActualMesh->Bounds.GetBox().Max.Z,Mesh->GetImportedBounds().GetBox().TransformBy(ActualMesh->GetComponentTransform()).Max.Z);
    TestTrue(TEXT("Health marker clears the complete animated mechanism"),Actor->HealthLocation().Z>=ModelTop+29.999);
    TestTrue(TEXT("Status marker clears the health marker"),Actor->StatusLocation().Z>=Actor->HealthLocation().Z+15.999);
    TestFalse(TEXT("Aging without a hit keeps building health hidden"),Actor->HasBeenDamaged());
    Actor->HitAt(VisualState.elapsed,1);TestTrue(TEXT("First real damage reveals the building health marker"),Actor->HasBeenDamaged());
    if(TestTrue(TEXT("New mechanism imports its authored defeat cleanup duration"),Card->DeathCleanupSeconds>0))
    {
        const double Cleanup=Card->DeathCleanupSeconds;Actor->DieAt(0);Actor->AdvanceDeath(Cleanup*.5);
        TestTrue(TEXT("Mechanism defeat retains its skeletal body through the middle of its authored clip"),Actor->CurrentAnimation()==TEXT("Death")&&FMath::IsNearlyEqual(Actor->AnimationCycleFraction(),.5f,.00001f)&&!Actor->IsExpired(Cleanup*.5));
        for(auto* Part:StaticParts)TestNull(TEXT("Skeletal mechanism defeat never adds static tower rubble"),Part->GetStaticMesh().Get());
        Actor->AdvanceDeath(Cleanup);
        TestTrue(TEXT("Mechanism defeat finishes exactly at its authored cleanup deadline"),FMath::IsNearlyEqual(Actor->AnimationCycleFraction(),1.f,.00001f)&&Actor->IsExpired(Cleanup));
    }

    // Record ordinary paid deployment and genuine native combat, with only the
    // initial and final snapshots. Every intermediate seek therefore exercises
    // archived events rather than borrowing a prebuilt in-flight snapshot.
    auto* Replay=GI->GetSubsystem<URiftReplaySubsystem>();
    auto* MatchView=World->GetSubsystem<URiftMatchSubsystem>();
    if(!TestNotNull(TEXT("Wave archive uses the production replay subsystem"),Replay)||
       !TestNotNull(TEXT("Wave seeks use the production match view"),MatchView))return false;
    ON_SCOPE_EXIT {Replay->CloseReplay();Replay->FlushPendingWrites();};
    rift::Match Recorded(Options);Replay->BeginRecording(Options,true);
    rift::Event FirstLaunch;double FirstContact=-1,StrongUntil=-1,FrostDeath=-1;uint64 VictimId=0,FrostId=0;
    auto RecordEvents=[&]()
    {
        for(const auto& Event:Recorded.DrainEvents())
        {
            Replay->RecordEvent(Event);
            if(Event.type=="shockwave_launch"&&!FirstLaunch.sequence)FirstLaunch=Event;
            if(Event.type=="damage"&&Event.damageKind=="shockwave"&&FirstContact<0)FirstContact=Event.time;
            if(Event.type=="slow"&&Event.cardId=="frost_fang"&&Event.target==VictimId&&StrongUntil<0)StrongUntil=Event.until;
            if(Event.type=="death"&&Event.target==FrostId)FrostDeath=Event.time;
        }
    };
    RecordEvents();Replay->Sample(Recorded.State());
    if(!TestTrue(TEXT("Replay fixture pays normally for Pulverizer"),Recorded.Play(rift::Team::Player,0,{.5,8.5}))||
       !TestTrue(TEXT("Replay fixture deploys a genuine enemy ground tank"),Recorded.Spawn(rift::Team::Enemy,"boulderback",{.5,5.5})))return false;
    VictimId=Recorded.State().entities.back().id;
    if(!TestTrue(TEXT("Replay fixture deploys genuine Frost Fang beside that tank"),Recorded.Spawn(rift::Team::Player,"frost_fang",{1.5,5.5})))return false;
    FrostId=Recorded.State().entities.back().id;RecordEvents();bool FrostRemovalCast=false;
    for(int32 Step=0;Step<240;++Step)
    {
        Recorded.Step(1./60.);RecordEvents();
        if(StrongUntil>0&&!FrostRemovalCast)
        {
            // Enemy Bullet Burst kills only the Frost caster after its first
            // actual hit. Pulverizer can continue refreshing its weaker slow.
            rift::Vec2 FrostPoint;
            for(const auto& E:Recorded.State().entities)if(E.id==FrostId)FrostPoint=E.position;
            for(int32 Cast=0;Cast<8;++Cast)TestTrue(TEXT("Fixture's delayed enemy Bullet Burst is a genuine DEV cast"),Recorded.Spawn(rift::Team::Enemy,"bullet_burst",FrostPoint));
            FrostRemovalCast=true;RecordEvents();
        }
    }
    TestTrue(TEXT("Actual native match emits a traveling wave release"),FirstLaunch.sequence>0&&FirstLaunch.until>FirstLaunch.time);
    TestTrue(TEXT("Actual native wave contacts the ground tank after release"),FirstContact>FirstLaunch.time&&FirstContact<FirstLaunch.until);
    TestTrue(TEXT("Actual Frost caster applies a stronger independent slow"),StrongUntil>0);
    TestTrue(TEXT("Actual Frost caster dies before its first slow expires"),FrostDeath>0&&FrostDeath<StrongUntil);
    Replay->EndRecording(Recorded.State(),true);
    if(!TestTrue(TEXT("Real paid wave/slow event archive saves"),Replay->FlushPendingWrites())||
       !TestTrue(TEXT("Real paid wave/slow event archive validates and loads"),Replay->OpenReplay(Replay->LatestFilename)))return false;
    // Keep the actual captured definitions as well as snapshots/events. New
    // archives validate their own mechanics; legacy archives cannot be judged
    // against today's values when their historical definitions were absent.
    const FString ExportPath=FPaths::Combine(URiftProfileSubsystem::SaveRoot(),TEXT("AutomationFixtures"),FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT("-pulverizer-context.json"));
    if(!TestTrue(TEXT("The real paid archive exports with its captured mechanical context"),Replay->ExportReplay(Replay->LatestFilename,ExportPath)))return false;
    FString ArchiveText;TSharedPtr<FJsonObject> Archive;
    if(!TestTrue(TEXT("The real paid archive's exported JSON is readable"),FFileHelper::LoadFileToString(ArchiveText,*ExportPath))||
       !TestTrue(TEXT("The real paid archive's exported JSON parses"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ArchiveText),Archive)))return false;
    TestTrue(TEXT("New paid recordings retain their captured balance definitions"),Archive->HasField(TEXT("balance")));
    FString ArchiveError;
    TestTrue(TEXT("The actual native event archive is the valid launch-parser control"),URiftReplaySubsystem::ValidateRecording(Archive,ArchiveError));
    TSharedPtr<FJsonObject> LegacyArchive;
    if(TestTrue(TEXT("Legacy compatibility control clones actual recorded numeric data"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ArchiveText),LegacyArchive)))
    {LegacyArchive->RemoveField(TEXT("balance"));TestTrue(TEXT("A real legacy archive without captured definitions remains valid"),URiftReplaySubsystem::ValidateRecording(LegacyArchive,ArchiveError));}
    auto MalformedLaunch=[&](const TCHAR* Label,auto Mutate)
    {
        FString Encoded;FJsonSerializer::Serialize(Archive,TJsonWriterFactory<>::Create(&Encoded));
        TSharedPtr<FJsonObject> Copy;
        if(!TestTrue(TEXT("Malformed-event fixture deep-clones the real archive"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Encoded),Copy)))return;
        bool Found=false;
        for(const auto& Event:Copy->GetArrayField(TEXT("events")))
            if(Event->AsObject()->GetStringField(TEXT("type"))==TEXT("shockwave_launch"))
            {Mutate(Event->AsObject());Found=true;break;}
        if(TestTrue(TEXT("Malformed-event fixture locates a real native launch"),Found))
            TestFalse(Label,URiftReplaySubsystem::ValidateRecording(Copy,ArchiveError));
    };
    MalformedLaunch(TEXT("Event archive rejects launches on an existing non-wave card"),[](auto E){E->SetStringField(TEXT("cardId"),TEXT("ironclad"));});
    MalformedLaunch(TEXT("Event archive rejects launches on unknown cards"),[](auto E){E->SetStringField(TEXT("cardId"),TEXT("unknown_card"));});
    MalformedLaunch(TEXT("Event archive rejects a zero wave source"),[](auto E){E->SetNumberField(TEXT("source"),0);});
    MalformedLaunch(TEXT("Event archive rejects a missing paid-or-DEV cast identity"),[](auto E){E->RemoveField(TEXT("playId"));});
    MalformedLaunch(TEXT("Event archive rejects a zero triggering target"),[](auto E){E->SetNumberField(TEXT("target"),0);});
    MalformedLaunch(TEXT("Event archive rejects fabricated wave damage"),[](auto E){E->SetNumberField(TEXT("amount"),64);});
    MalformedLaunch(TEXT("Event archive rejects missing wave damage"),[](auto E){E->RemoveField(TEXT("amount"));});
    MalformedLaunch(TEXT("Event archive rejects fabricated wave travel duration"),[](auto E){E->SetNumberField(TEXT("until"),E->GetNumberField(TEXT("time"))+.6);});
    MalformedLaunch(TEXT("Event archive rejects wave expiration at launch time"),[](auto E){E->SetNumberField(TEXT("until"),E->GetNumberField(TEXT("time")));});
    MalformedLaunch(TEXT("Event archive rejects a missing wave origin"),[](auto E){E->RemoveField(TEXT("position"));});
    auto ViewEntity=[&](uint64 Id)->const rift::Entity*
    {const auto* View=MatchView->ViewState();if(View)for(const auto& E:View->entities)if(E.id==Id)return &E;return nullptr;};
    auto* Arena=World->SpawnActor<ARiftArenaPresentation>();
    if(!TestNotNull(TEXT("Wave replay renders through the production arena actor"),Arena))return false;
    Arena->DispatchBeginPlay();
    Replay->Seek(float(FirstLaunch.time+.15));Arena->Tick(0);
    const auto* View=MatchView->ViewState();
    if(TestNotNull(TEXT("Wave seek publishes a production view"),View))
    {
        TestEqual(TEXT("Event-only seek reconstructs exactly one live wave"),int32(View->shockwaves.size()),1);
        TestEqual(TEXT("Event-only seek preserves the paid building expense"),View->spent[0],3.);
        if(View->shockwaves.size()==1)
        {
            const auto& W=View->shockwaves[0];
            TestTrue(TEXT("Archived event preserves wave identity, source and paid cast"),W.id==FirstLaunch.sequence&&W.source==FirstLaunch.source&&W.playId==FirstLaunch.playId&&W.cardId=="pulverizer");
            TestTrue(TEXT("Archived wave travels at the actual eight tiles per second"),FMath::IsNearlyEqual(W.progress,(Replay->TimelinePosition()-FirstLaunch.time)*8.,.000001)&&W.radius==4&&W.speed==8);
            const double PausedProgress=W.progress,PausedTime=Replay->TimelinePosition();Replay->SetSpeed(0);Replay->Advance(1.f);Arena->Tick(0);
            const auto* Paused=MatchView->ViewState();
            TestTrue(TEXT("Paused archived wave freezes both time and its real radius"),Replay->TimelinePosition()==PausedTime&&Paused&&Paused->shockwaves.size()==1&&Paused->shockwaves[0].progress==PausedProgress);
        }
    }
    TestEqual(TEXT("Event-only replay renders the actual expanding shockwave decal"),Arena->ShockwaveVisualCount(),1);
    Replay->Seek(float(FirstContact+.002));Arena->Tick(0);View=MatchView->ViewState();
    bool RememberedContact=false;
    if(View)for(const auto& W:View->shockwaves)if(W.id==FirstLaunch.sequence)
        for(auto Hit:W.hitTargets)RememberedContact|=Hit==VictimId;
    TestTrue(TEXT("Archived contact marks the victim in that wave's hit memory"),RememberedContact);
    Replay->Seek(float(FirstLaunch.until+.002));Arena->Tick(0);View=MatchView->ViewState();
    bool FirstWavePresent=false;if(View)for(const auto& W:View->shockwaves)FirstWavePresent|=W.id==FirstLaunch.sequence;
    TestFalse(TEXT("Archived wave disappears at its own radius/lifetime limit"),FirstWavePresent);
    TestEqual(TEXT("Expired first wave removes its actual decal before the next strike"),Arena->ShockwaveVisualCount(),0);
    Replay->Seek(float(StrongUntil-.01));
    if(const auto* Victim=ViewEntity(VictimId))TestTrue(TEXT("Weak wave refresh preserves Frost's stronger original lifetime"),Victim->slowPct==.30&&FMath::IsNearlyEqual(Victim->slowUntil,StrongUntil,.000001));
    else AddError(TEXT("Archived tank disappeared before strong-slow expiry."));
    Replay->Seek(float(StrongUntil+.01));
    if(const auto* Victim=ViewEntity(VictimId))TestTrue(TEXT("After Frost expires event-only replay exposes the refreshed weaker slow"),Victim->slowPct==.10&&Victim->slowUntil>Replay->TimelinePosition()&&Victim->slows.size()==1&&Victim->slows[0].cardId=="pulverizer");
    else AddError(TEXT("Archived tank disappeared after strong-slow expiry."));
    Replay->Seek(float(FirstLaunch.time-.002));Arena->Tick(0);View=MatchView->ViewState();
    TestTrue(TEXT("Backward seeking removes future wave events and real decals"),View&&View->shockwaves.empty()&&Arena->ShockwaveVisualCount()==0);
    Replay->Seek(float(FirstLaunch.time+.15));Arena->Tick(0);
    TestEqual(TEXT("Forward seeking restores the actual wave decal without duplication"),Arena->ShockwaveVisualCount(),1);
    Replay->CloseReplay();Arena->Tick(0);
    AddInfo(FString::Printf(TEXT("Real replay witness: first wave %.6f s, first contact %.6f s, Frost death %.6f s, original Frost expiry %.6f s; initial/final-only snapshots and event seeks exercised."),FirstLaunch.time,FirstContact,FrostDeath,StrongUntil));
    AddInfo(TEXT("Pulverizer contract: explicit new stats, all16 prior wave speeds zero, native import/export/model/portrait/nine animations, paid building economy and targets, strict shared detection, wave/slow snapshot rejection and legacy compatibility, real paid combat archive/event seeks/decal/pause/slow expiry, grounded strike/recovery/HP markers verified."));
    return !HasAnyErrors();
}
#endif
