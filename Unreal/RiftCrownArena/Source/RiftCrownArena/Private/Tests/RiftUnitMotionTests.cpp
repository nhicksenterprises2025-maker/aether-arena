#if WITH_DEV_AUTOMATION_TESTS
#include "Presentation/RiftUnitVisual.h"
#include "RiftCardData.h"
#include "RiftReplaySubsystem.h"
#include "RiftMatchSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include <limits>

namespace
{
    bool SameMotionPose(const TArray<FTransform>& A,const TArray<FTransform>& B)
    {
        if(A.Num()!=B.Num() || A.IsEmpty())return false;
        for(int32 Index=0;Index<A.Num();++Index)if(!A[Index].Equals(B[Index],.0001f))return false;
        return true;
    }
    URiftCardData* MotionCard(const TCHAR* Id)
    {return LoadObject<URiftCardData>(nullptr,*FString::Printf(TEXT("/Game/Rift/Cards/DA_%s.DA_%s"),Id,Id));}
    rift::Entity MotionEntity(const char* Id,uint64 Number)
    {
        rift::Entity Entity;Entity.id=Number;Entity.cardId=Id;Entity.kind=rift::EntityKind::Troop;
        if(const auto* Card=rift::FindCard(Id))
        {Entity.hp=Entity.maxHp=Card->hp;Entity.flying=Card->flying;Entity.radius=.4*Card->scale;}
        return Entity;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftUnitMotionIntegrationTest,"Rift.Integration.UnitMotion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftUnitMotionIntegrationTest::RunTest(const FString& Parameters)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox")))
    {AddError(TEXT("Unit motion fixture requires isolated RiftSaveRoot/UserDir and RiftAutomationSandbox."));return false;}
    auto* GI=NewObject<UGameInstance>(GEngine);GI->InitializeStandalone(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto* World=GI->GetWorld();
    ON_SCOPE_EXIT {World->DestroyWorld(false);GI->Shutdown();GEngine->DestroyWorldContext(World);};
    auto Entity=MotionEntity("ironclad",701);rift::Snapshot State;State.elapsed=1.;State.entities={Entity};
    auto* Actor=World->SpawnActor<ARiftUnitVisual>();auto* Card=MotionCard(TEXT("ironclad"));
    if(!TestNotNull(TEXT("Motion test uses the actual imported Ironclad card"),Card) ||
        !TestTrue(TEXT("Actual mesh and simulation-time animator initialize"),Actor->InitializeEntity(Entity,Card,State.elapsed)))return false;
    auto* Mesh=Actor->FindComponentByClass<USkeletalMeshComponent>();
    if(!TestNotNull(TEXT("Production skeletal component exists"),Mesh) ||
        !TestNotNull(TEXT("Production native crossfade instance is bound"),Cast<URiftUnitAnimInstance>(Mesh->GetSingleNodeInstance())))return false;
    TestTrue(TEXT("Troop presentation is larger without changing canonical Card.scale"),FMath::IsNearlyEqual(Actor->PresentationScale(),1.80f) && rift::FindCard("ironclad")->scale==1.);
    Actor->Synchronize(Entity,State,0);const auto IdlePose=Mesh->GetBoneSpaceTransforms();
    TestTrue(TEXT("Native pose actually evaluates every imported bone"),IdlePose.Num()>20);

    auto Target=MotionEntity("ironclad",702);Target.team=rift::Team::Enemy;Target.position={1,0};
    Entity.target=Target.id;Entity.cooldown=.30;State.entities={Entity,Target};
    Actor->Synchronize(Entity,State,.3f);
    TestEqual(TEXT("A target inside real range starts cooldown-aligned anticipation"),Actor->CurrentAnimation(),FName(TEXT("Attack")));
    TestEqual(TEXT("Changing clip starts a real local-pose crossfade"),Actor->AnimationBlendAlpha(),0.f);
    TestTrue(TEXT("First transition pose retains the actual preceding pose"),SameMotionPose(IdlePose,Mesh->GetBoneSpaceTransforms()));
    State.elapsed=1.05;Entity.cooldown=.25;State.entities[0]=Entity;Actor->Synchronize(Entity,State,.05f);
    const auto BlendedPose=Mesh->GetBoneSpaceTransforms();
    TestTrue(TEXT("Half transition evaluates a blended moving skeleton"),FMath::IsNearlyEqual(Actor->AnimationBlendAlpha(),.5f,.001f) && !SameMotionPose(IdlePose,BlendedPose));
    const float PausedPosition=Actor->AnimationPosition(),PausedFraction=Actor->AnimationCycleFraction();
    const FRotator PausedRotation=Actor->GetActorRotation();const double PausedGait=Actor->LocomotionPhase();
    for(int32 Frame=0;Frame<24;++Frame)Actor->Synchronize(Entity,State,.10f);
    TestTrue(TEXT("Pause preserves the exact evaluated bone pose through renderer ticks"),SameMotionPose(BlendedPose,Mesh->GetBoneSpaceTransforms()));
    TestTrue(TEXT("Pause preserves seek position, crossfade, gait and facing"),Actor->AnimationPosition()==PausedPosition && Actor->AnimationCycleFraction()==PausedFraction && Actor->LocomotionPhase()==PausedGait && Actor->GetActorRotation().Equals(PausedRotation,.0001f) && FMath::IsNearlyEqual(Actor->AnimationBlendAlpha(),.5f,.001f));

    Actor->AttackAt(1.30,96);
    TestTrue(TEXT("Authoritative release immediately evaluates the exact authored contact pose"),FMath::IsNearlyEqual(Actor->AnimationCycleFraction(),.48f,.00001f) && Actor->AnimationBlendAlpha()==1.f);
    float PreviousFraction=.48f;
    for(int32 Frame=0;Frame<32;++Frame)
    {
        State.elapsed=1.30+Frame*.01;Entity.cooldown=1.-Frame*.01;State.entities[0]=Entity;Actor->Synchronize(Entity,State,.01f);
        TestTrue(FString::Printf(TEXT("Release recovery stays monotonic at frame %d"),Frame),Actor->CurrentAnimation()==TEXT("Attack") && Actor->AnimationCycleFraction()>=PreviousFraction && Actor->AnimationCycleFraction()<=1.f);
        PreviousFraction=Actor->AnimationCycleFraction();
    }
    const auto RecoveryPose=Mesh->GetBoneSpaceTransforms();State.elapsed=1.63;Entity.cooldown=.67;State.entities[0]=Entity;Actor->Synchronize(Entity,State,.02f);
    TestEqual(TEXT("Completed attack returns to idle instead of wrapping through its windup"),Actor->CurrentAnimation(),FName(TEXT("Idle")));
    TestTrue(TEXT("Recovery-to-idle transition begins without a bone-pose pop"),SameMotionPose(RecoveryPose,Mesh->GetBoneSpaceTransforms()) && Actor->AnimationBlendAlpha()==0.f);
    State.elapsed=1.84;Entity.cooldown=.46;State.entities[0]=Entity;Actor->Synchronize(Entity,State,.21f);
    TestEqual(TEXT("No false attack occurs at the old release-cycle wrap point"),Actor->CurrentAnimation(),FName(TEXT("Idle")));

    Entity.target=0;Entity.position.x+=1.;State.elapsed=2.;State.entities={Entity};const double BeforeTravel=Actor->LocomotionPhase();
    Actor->Synchronize(Entity,State,.16f);const double AfterTravel=Actor->LocomotionPhase();
    TestTrue(TEXT("Locomotion advances a real gait according to distance traveled"),Actor->CurrentAnimation()==TEXT("Locomotion") && FMath::IsNearlyEqual(AfterTravel-BeforeTravel,100./(1.80*105.),.00001));
    State.elapsed=2.4;Actor->Synchronize(Entity,State,.4f);
    TestEqual(TEXT("A stationary unit does not run its gait clock in place"),Actor->LocomotionPhase(),AfterTravel);
    TestTrue(TEXT("Attack and impact anchors follow the actual enlarged mesh sockets"),Actor->AttackLocation().Equals(Mesh->GetSocketLocation(TEXT("attack_origin")),.001f) && Actor->ImpactLocation().Equals(Mesh->GetSocketLocation(TEXT("impact_origin")),.001f));
    TestTrue(TEXT("Enlarged Ironclad health and status markers clear its actual animated model"),Actor->HealthLocation().Z>=Mesh->Bounds.GetBox().Max.Z+29.999 && Actor->StatusLocation().Z>=Actor->HealthLocation().Z+15.999);

    auto Boulder=MotionEntity("boulderback",707);auto* BoulderActor=World->SpawnActor<ARiftUnitVisual>();State.elapsed=2.5;State.entities={Boulder};
    if(!TestTrue(TEXT("Actual larger Boulderback model initializes"),BoulderActor->InitializeEntity(Boulder,MotionCard(TEXT("boulderback")),State.elapsed)))return false;
    BoulderActor->Synchronize(Boulder,State,0);auto* BoulderMesh=BoulderActor->FindComponentByClass<USkeletalMeshComponent>();
    const double BoulderModelTop=FMath::Max(BoulderMesh->Bounds.GetBox().Max.Z,BoulderMesh->GetSkeletalMeshAsset()->GetImportedBounds().GetBox().TransformBy(BoulderMesh->GetComponentTransform()).Max.Z);
    TestTrue(TEXT("Heavy unit scale preserves its canonical gameplay scale and radius"),FMath::IsNearlyEqual(BoulderActor->PresentationScale(),float(1.22*1.65),.00001f) && rift::FindCard("boulderback")->scale==1.22 && Boulder.radius==.4*1.22);
    TestTrue(TEXT("Boulderback health clears the real upper crystals instead of the lower hp bone"),BoulderActor->HealthLocation().Z>=BoulderModelTop+29.999 && BoulderActor->HealthLocation().Z>BoulderMesh->GetSocketLocation(TEXT("hp_anchor")).Z+20.);
    TestTrue(TEXT("Boulderback status effects remain above its health marker"),BoulderActor->StatusLocation().Z>=BoulderActor->HealthLocation().Z+15.999);

    auto FreshArcher=MotionEntity("ember_archer",705);FreshArcher.born=3.;
    auto* FreshActor=World->SpawnActor<ARiftUnitVisual>();State.elapsed=3.05;State.entities={FreshArcher};
    if(!TestTrue(TEXT("Newly deployed production archer initializes"),FreshActor->InitializeEntity(FreshArcher,MotionCard(TEXT("ember_archer")),State.elapsed)))return false;
    FreshActor->AttackAt(State.elapsed,152);FreshArcher.cooldown=1.55;
    FreshActor->Synchronize(FreshArcher,State,0);
    TestTrue(TEXT("An actual early projectile release takes priority over deployment animation"),FreshActor->CurrentAnimation()==TEXT("DrawRelease") && FMath::IsNearlyEqual(FreshActor->AnimationCycleFraction(),.48f,.00001f));

    auto Flyer=MotionEntity("sky_manta",703);auto* FlyingActor=World->SpawnActor<ARiftUnitVisual>();State.elapsed=5.;State.entities={Flyer};
    if(!TestTrue(TEXT("Actual enlarged flying model initializes"),FlyingActor->InitializeEntity(Flyer,MotionCard(TEXT("sky_manta")),State.elapsed)))return false;
    FlyingActor->Synchronize(Flyer,State,0);const double AirHeight=FlyingActor->GetActorLocation().Z;FlyingActor->DieAt(State.elapsed);FlyingActor->AdvanceDeath(State.elapsed);
    AddInfo(FString::Printf(TEXT("Flying death altitude before %.12f cm; at start %.12f cm; displacement %.12f cm."),AirHeight,FlyingActor->GetActorLocation().Z,FlyingActor->GetActorLocation().Z-AirHeight));
    TestTrue(TEXT("Flying death begins at the current altitude without teleporting to ground"),AirHeight>80. && FMath::IsNearlyEqual(FlyingActor->GetActorLocation().Z,AirHeight,.001));
    FlyingActor->AdvanceDeath(5.2);const double FallingHeight=FlyingActor->GetActorLocation().Z;
    TestTrue(TEXT("Flying death has a visible intermediate descent"),FallingHeight>8.f && FallingHeight<AirHeight);
    FlyingActor->AdvanceDeath(5.65);TestTrue(TEXT("Flying death settles at its ground endpoint"),FMath::IsNearlyEqual(FlyingActor->GetActorLocation().Z,8.f,.001f));

    auto Building=MotionEntity("archer_tower",704);Building.kind=rift::EntityKind::Building;Building.flying=false;
    auto* BuildingActor=World->SpawnActor<ARiftUnitVisual>();
    if(!TestTrue(TEXT("Actual Archer Tower structure and turret initialize"),BuildingActor->InitializeEntity(Building,MotionCard(TEXT("archer_tower")),5.)))return false;
    auto* BuildingBase=BuildingActor->FindComponentByClass<UStaticMeshComponent>();const FTransform BuildingBaseTransform=BuildingBase->GetComponentTransform();
    auto* BuildingArcher=BuildingActor->FindComponentByClass<USkeletalMeshComponent>();
    TestTrue(TEXT("Archer Tower presentation enlarges its archer and height"),FMath::IsNearlyEqual(BuildingActor->PresentationScale(),1.22f) && BuildingArcher->GetRelativeScale3D().Equals(FVector(.60),.0001f));
    const double ArcherModelTop=FMath::Max(BuildingArcher->Bounds.GetBox().Max.Z,BuildingArcher->GetSkeletalMeshAsset()->GetImportedBounds().GetBox().TransformBy(BuildingArcher->GetComponentTransform()).Max.Z);
    TestTrue(TEXT("Archer Tower health clears the actual skeletal decoration"),BuildingActor->HealthLocation().Z>=ArcherModelTop+29.999);
    BuildingActor->AimAt(BuildingActor->GetActorLocation()+FVector(400,200,0));
    TestTrue(TEXT("Archer Tower decoration aims while architecture remains fixed"),BuildingBase->GetComponentTransform().Equals(BuildingBaseTransform,.001f) && FMath::Abs(BuildingActor->GetActorRotation().Yaw+90.f)>20.f);
    BuildingActor->DieAt(5.);auto* Structure=BuildingActor->FindComponentByClass<UStaticMeshComponent>();
    TestTrue(TEXT("Archer Tower destruction replaces the real building with rubble"),Structure && Structure->GetStaticMesh() && Structure->GetStaticMesh()->GetName()==TEXT("SM_tower_rubble"));

    rift::Entity Guard;Guard.id=706;Guard.kind=rift::EntityKind::Guard;Guard.hp=Guard.maxHp=3050;Guard.radius=1.;
    auto* GuardActor=World->SpawnActor<ARiftUnitVisual>();
    if(!TestTrue(TEXT("Actual separated guard architecture and authored cannon initialize"),GuardActor->InitializeEntity(Guard,nullptr,5.)))return false;
    auto* GuardBase=GuardActor->FindComponentByClass<UStaticMeshComponent>();UStaticMeshComponent* GuardCannon=nullptr;
    TArray<UStaticMeshComponent*> GuardComponents;GuardActor->GetComponents(GuardComponents);
    for(auto* Component:GuardComponents)if(Component->GetName()==TEXT("AuthoredGuardCannon"))GuardCannon=Component;
    if(!TestNotNull(TEXT("Guard owns a separate authored cannon component"),GuardCannon) || !TestNotNull(TEXT("Separate guard cannon loads its production mesh"),GuardCannon->GetStaticMesh().Get()))return false;
    TestEqual(TEXT("Guard cannon uses the narrowly exported original cannon parts"),GuardCannon->GetStaticMesh()->GetName(),FString(TEXT("SM_guard_cannon")));
    const FTransform GuardBaseTransform=GuardBase->GetComponentTransform();
    GuardActor->AimAt(GuardActor->GetActorLocation()+FVector(400,400,0));GuardActor->AttackAt(5.,86);
    TestTrue(TEXT("Guard aims its cannon at release without rotating its stone castle"),GuardBase->GetComponentTransform().Equals(GuardBaseTransform,.001f) && FMath::IsNearlyEqual(GuardCannon->GetComponentRotation().Yaw,45.f,.001f));
    TestTrue(TEXT("Guard release origin follows the actual visible cannon muzzle"),GuardActor->AttackLocation().Equals(GuardCannon->GetComponentTransform().TransformPosition(FVector(99,0,263)),.001f) && GuardActor->GuardRecoilDistance()==0.f);
    Guard.facing={1,1};Guard.cooldown=.93;State.elapsed=5.09;State.entities={Guard};GuardActor->Synchronize(Guard,State,.09f);
    TestTrue(TEXT("Guard cannon reaches its readable recoil while stone remains still"),FMath::IsNearlyEqual(GuardActor->GuardRecoilDistance(),16.f,.001f) && GuardBase->GetComponentTransform().Equals(GuardBaseTransform,.001f));
    const FTransform PausedCannon=GuardCannon->GetComponentTransform();
    for(int32 Frame=0;Frame<24;++Frame)GuardActor->Synchronize(Guard,State,.1f);
    TestTrue(TEXT("Guard recoil and independent aim freeze with the simulation"),GuardCannon->GetComponentTransform().Equals(PausedCannon,.001f) && FMath::IsNearlyEqual(GuardActor->GuardRecoilDistance(),16.f,.001f));
    State.elapsed=5.18;GuardActor->Synchronize(Guard,State,.09f);
    TestTrue(TEXT("Guard cannon settles at its original perch after recoil"),GuardActor->GuardRecoilDistance()==0.f && GuardCannon->GetRelativeLocation().IsNearlyZero(.001f) && GuardBase->GetComponentTransform().Equals(GuardBaseTransform,.001f));

    // Use separate production actors so hit responses cannot disturb the motion
    // fixture above. The presentation latch survives healing, but not reuse.
    auto HealthTroop=MotionEntity("ironclad",711);auto* HealthActor=World->SpawnActor<ARiftUnitVisual>();
    if(!TestTrue(TEXT("Full-health troop health fixture initializes"),HealthActor->InitializeEntity(HealthTroop,Card,10.)))return false;
    TestFalse(TEXT("An undamaged full-health troop has no HP annotation"),HealthActor->HasBeenDamaged());
    HealthActor->HitAt(10.,0);HealthActor->HitAt(10.,-1);HealthActor->HitAt(10.,std::numeric_limits<double>::infinity());
    TestFalse(TEXT("Zero, negative and nonfinite damage cannot reveal a health bar"),HealthActor->HasBeenDamaged());
    HealthActor->HitAt(10.,1);State.elapsed=10.;State.entities={HealthTroop};HealthActor->Synchronize(HealthTroop,State,0);
    TestTrue(TEXT("A positive actual hit stays revealed after healing to full HP"),HealthActor->HasBeenDamaged());
    HealthActor->InitializeEntity(HealthTroop,Card,10.);
    TestFalse(TEXT("Configuring an undamaged entity resets the previous damage history"),HealthActor->HasBeenDamaged());
    --HealthTroop.hp;HealthActor->InitializeEntity(HealthTroop,Card,10.);
    TestTrue(TEXT("Loading a previously damaged troop reveals its bar from the actual HP snapshot"),HealthActor->HasBeenDamaged());
    TestFalse(TEXT("An undamaged guard retains a hidden HP annotation"),GuardActor->HasBeenDamaged());
    auto HealthCore=Guard;HealthCore.id=712;HealthCore.kind=rift::EntityKind::Core;HealthCore.hp=HealthCore.maxHp=5200;
    auto* CoreActor=World->SpawnActor<ARiftUnitVisual>();
    if(!TestTrue(TEXT("Actual undamaged Core health fixture initializes"),CoreActor->InitializeEntity(HealthCore,nullptr,10.)))return false;
    TestFalse(TEXT("An undamaged Core retains a hidden HP annotation"),CoreActor->HasBeenDamaged());
    CoreActor->HitAt(10.,1);State.entities={HealthCore};CoreActor->Synchronize(HealthCore,State,0);
    TestTrue(TEXT("Core health stays visible after a positive hit and a full heal"),CoreActor->HasBeenDamaged());

    auto Aging=MotionEntity("archer_tower",713);Aging.kind=rift::EntityKind::Building;Aging.born=10.;
    const auto* TowerDefinition=rift::FindCard("archer_tower");auto* AgingActor=World->SpawnActor<ARiftUnitVisual>();
    if(!TestTrue(TEXT("Actual undamaged defensive building health fixture initializes"),AgingActor->InitializeEntity(Aging,MotionCard(TEXT("archer_tower")),10.)))return false;
    TestFalse(TEXT("An undamaged defensive building has no HP annotation"),AgingActor->HasBeenDamaged());
    State.elapsed=15.;Aging.hp=Aging.maxHp-Aging.maxHp/TowerDefinition->lifetime*5.;State.entities={Aging};AgingActor->Synchronize(Aging,State,5.f);
    TestFalse(TEXT("An uncontested building's passive lifetime HP loss does not reveal its health"),AgingActor->HasBeenDamaged());
    Aging.hp-=1;State.entities={Aging};AgingActor->Synchronize(Aging,State,0);
    TestTrue(TEXT("Building HP below the undamaged aging baseline reveals real damage"),AgingActor->HasBeenDamaged());
    Aging.hp=Aging.maxHp;State.entities={Aging};AgingActor->Synchronize(Aging,State,0);
    TestTrue(TEXT("A fully healed building retains its first-damage health annotation"),AgingActor->HasBeenDamaged());
    Aging.hp=Aging.maxHp-Aging.maxHp/TowerDefinition->lifetime*5.;AgingActor->InitializeEntity(Aging,MotionCard(TEXT("archer_tower")),15.);
    TestFalse(TEXT("An aging building loaded without a combat hit remains hidden"),AgingActor->HasBeenDamaged());
    AgingActor->HitAt(15.,1);AgingActor->Synchronize(Aging,State,0);
    TestTrue(TEXT("A positive building damage event reveals health independently of passive aging"),AgingActor->HasBeenDamaged());

    // Record actual native damage and DEV healing, then exercise the immutable
    // first-hit cache through both directions of the public replay timeline.
    auto* Replay=GI->GetSubsystem<URiftReplaySubsystem>();
    if(!TestNotNull(TEXT("Health history uses the production replay subsystem"),Replay))return false;
    rift::MatchOptions Options;Options.aiEnabled={false,false};rift::Match Recorded(Options);
    rift::Entity ReplayGuard,EditedCore;
    for(const auto& InitialEntity:Recorded.State().entities)
    {
        if(InitialEntity.team==rift::Team::Enemy && InitialEntity.kind==rift::EntityKind::Guard)ReplayGuard=InitialEntity;
        if(InitialEntity.team==rift::Team::Player && InitialEntity.kind==rift::EntityKind::Core)EditedCore=InitialEntity;
    }
    if(!TestTrue(TEXT("Recording fixture finds native crown structures"),ReplayGuard.id!=0 && EditedCore.id!=0))return false;
    Replay->BeginRecording(Options,true);Replay->Sample(Recorded.State());
    for(const auto& Event:Recorded.DrainEvents())Replay->RecordEvent(Event);
    Recorded.Step(.1);Recorded.Spawn(rift::Team::Player,"nova_flask",ReplayGuard.position);
    double FirstHit=-1;
    for(const auto& Event:Recorded.DrainEvents())
    {Replay->RecordEvent(Event);if(Event.type=="damage" && Event.target==ReplayGuard.id && Event.amount>0.)FirstHit=Event.time;}
    if(!TestTrue(TEXT("Native Nova Flask produces actual positive tower damage"),FirstHit>=0))return false;
    TestTrue(TEXT("Production DEV command heals the hit guard fully"),Recorded.SetTowerHP(ReplayGuard.id,ReplayGuard.maxHp));
    TestTrue(TEXT("Production DEV command lowers an otherwise undamaged Core"),Recorded.SetTowerHP(EditedCore.id,EditedCore.maxHp-1));
    TestTrue(TEXT("Production DEV command then heals that Core"),Recorded.SetTowerHP(EditedCore.id,EditedCore.maxHp));
    for(const auto& Event:Recorded.DrainEvents())Replay->RecordEvent(Event);
    Replay->EndRecording(Recorded.State(),true);
    if(!TestTrue(TEXT("Actual health-history replay archive saves"),Replay->FlushPendingWrites()) ||
       !TestTrue(TEXT("Actual health-history replay archive validates and opens"),Replay->OpenReplay(Replay->LatestFilename)))return false;
    TestFalse(TEXT("Replay does not reveal a future first hit"),Replay->HasTakenDamageBy(ReplayGuard.id,FirstHit-.0000001));
    TestTrue(TEXT("Replay reveals the exact inclusive first positive hit timestamp"),Replay->HasTakenDamageBy(ReplayGuard.id,FirstHit));
    TestFalse(TEXT("Unknown replay entities do not inherit damage history"),Replay->HasTakenDamageBy(999999,FirstHit));
    TestTrue(TEXT("Replay reconstructs first reduced HP from actual DEV tower edits"),Replay->HasTakenDamageBy(EditedCore.id,FirstHit));
    Replay->Seek(Replay->Duration());auto* MatchView=World->GetSubsystem<URiftMatchSubsystem>()->ViewState();
    if(!TestNotNull(TEXT("Production match view exposes the replay health snapshot"),MatchView))return false;
    for(const auto& ReplayedEntity:MatchView->entities)if(ReplayedEntity.id==ReplayGuard.id)ReplayGuard=ReplayedEntity;
    TestTrue(TEXT("The recorded and replayed guard genuinely healed to its native maximum HP"),ReplayGuard.hp==ReplayGuard.maxHp);
    auto* ReplayActor=World->SpawnActor<ARiftUnitVisual>();
    if(!TestTrue(TEXT("Fully healed replay tower initializes after its actual first hit"),ReplayActor->InitializeEntity(ReplayGuard,nullptr,Replay->TimelinePosition())))return false;
    TestTrue(TEXT("Seeking to a full-health end snapshot preserves earlier damage"),ReplayActor->HasBeenDamaged());
    Replay->Seek(0);MatchView=World->GetSubsystem<URiftMatchSubsystem>()->ViewState();ReplayActor->Synchronize(ReplayGuard,*MatchView,0);
    TestFalse(TEXT("Backward seeking before the first hit hides future health annotations"),ReplayActor->HasBeenDamaged());
    Replay->Seek(Replay->Duration());MatchView=World->GetSubsystem<URiftMatchSubsystem>()->ViewState();ReplayActor->Synchronize(ReplayGuard,*MatchView,0);
    TestTrue(TEXT("Forward seeking restores history even when the tower has healed fully"),ReplayActor->HasBeenDamaged());
    Replay->CloseReplay();TestFalse(TEXT("Closing a replay clears its prior health history"),Replay->HasTakenDamageBy(ReplayGuard.id,FirstHit));
    return true;
}
#endif
