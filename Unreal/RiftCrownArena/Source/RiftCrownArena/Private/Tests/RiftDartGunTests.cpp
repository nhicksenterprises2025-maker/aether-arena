#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Presentation/RiftUnitVisual.h"
#include "Presentation/RiftArenaPresentation.h"
#include "RiftCardData.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftDartGunPresentationTest,"Rift.Integration.DartGunPresentation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftDartGunPresentationTest::RunTest(const FString& Parameters)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox")))
    {AddError(TEXT("Dart Gun verification requires isolated automation saves."));return false;}
    const auto* Definition=rift::FindCard("dart_gun");
    auto* Card=LoadObject<URiftCardData>(nullptr,TEXT("/Game/Rift/Cards/DA_dart_gun.DA_dart_gun"));
    if(!TestNotNull(TEXT("Dart Gun has a playable authoritative definition"),Definition)||
       !TestNotNull(TEXT("Dart Gun has an imported native card"),Card))return false;
    TestEqual(TEXT("Requested Aether cost"),Definition->cost,3);
    TestEqual(TEXT("Requested damage"),Definition->damage,50.);
    TestEqual(TEXT("Requested cadence"),Definition->attackInterval,.4);
    TestEqual(TEXT("Requested attack range"),Definition->range,8.5);
    TestTrue(TEXT("125 DPS is derived from damage and cadence"),FMath::IsNearlyEqual(Definition->damage/Definition->attackInterval,125.));
    TestTrue(TEXT("The front detection zone reaches the authored firing range"),Card->FrontSight>=Card->AttackRange);
    TestNotNull(TEXT("Actual model portrait is imported"),Card->Illustration.Get());
    if(!TestNotNull(TEXT("Unique veteran skeletal mesh is imported"),Card->CharacterMesh.Get()))return false;
    TestTrue(TEXT("Veteran model has its own identity"),Card->CharacterMesh->GetName()==TEXT("SK_dart_gun"));
    TestTrue(TEXT("Veteran has a full articulated rig"),Card->CharacterMesh->GetRefSkeleton().GetNum()>20);
    for(const FName Name:{FName(TEXT("Idle")),FName(TEXT("Locomotion")),FName(TEXT("Attack")),FName(TEXT("Hit")),
        FName(TEXT("Deploy")),FName(TEXT("Death")),FName(TEXT("Status")),FName(TEXT("Turn")),FName(TEXT("Acquire"))})
    {
        const auto* Found=Card->Animations.Find(Name);
        TestTrue(Name.ToString()+TEXT(" clip is bound to the veteran's own skeleton"),Found&&*Found&&
            (*Found)->GetSkeleton()==Card->CharacterMesh->GetSkeleton()&&(*Found)->GetPlayLength()>0.);
    }
    TestTrue(TEXT("Rapid fire recovery and anticipation fit between shots"),Card->AttackRecoverySeconds>0&&
        Card->AttackAnticipationSeconds>0&&Card->AttackRecoverySeconds+Card->AttackAnticipationSeconds<Card->AttackSpeed);
    auto* Dart=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Rift/Environment/SM_dart_projectile.SM_dart_projectile"));
    TestNotNull(TEXT("Darts have authored projectile geometry"),Dart);
    const FVector From(-120,50,220),To(650,-310,260);
    TestTrue(TEXT("Dart flight has no unrequested lob"),ARiftArenaPresentation::ProjectilePathPoint("dart_gun",From,To,.4)==FMath::Lerp(From,To,.4));

    auto* GI=NewObject<UGameInstance>(GEngine);GI->InitializeStandalone(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto* World=GI->GetWorld();
    ON_SCOPE_EXIT {World->DestroyWorld(false);GI->Shutdown();GEngine->DestroyWorldContext(World);};
    rift::Entity Entity;Entity.id=1901;Entity.cardId="dart_gun";Entity.kind=rift::EntityKind::Troop;
    Entity.hp=Entity.maxHp=Definition->hp;Entity.radius=Definition->collisionRadius;Entity.position={0,7};
    rift::Snapshot State;State.elapsed=1.;State.entities={Entity};
    auto* Actor=World->SpawnActor<ARiftUnitVisual>();
    if(!TestTrue(TEXT("Actual native visual initializes"),Actor->InitializeEntity(Entity,Card,State.elapsed)))return false;
    auto* Mesh=Actor->FindComponentByClass<USkeletalMeshComponent>();
    if(!TestNotNull(TEXT("Native skeletal component is bound"),Mesh))return false;
    TestTrue(TEXT("Authored weapon muzzle socket exists"),Mesh->DoesSocketExist(TEXT("attack_origin")));
    TestTrue(TEXT("Launch position uses the animated weapon muzzle"),Actor->AttackLocation().Equals(Mesh->GetSocketLocation(TEXT("attack_origin")),.001));
    auto SamePose=[](const TArray<FTransform>& A,const TArray<FTransform>& B)
    {if(A.Num()!=B.Num()||A.IsEmpty())return false;for(int32 I=0;I<A.Num();++I)if(!A[I].Equals(B[I],.0001f))return false;return true;};
    for(int32 Shot=0;Shot<3;++Shot)
    {
        const double Time=1.+Shot*.4;
        Actor->AttackAt(Time,50.);
        TestTrue(TEXT("Each shot resets to its authored release pose"),Actor->CurrentAnimation()==TEXT("Attack")&&
            FMath::IsNearlyEqual(Actor->AnimationCycleFraction(),Card->AttackContactNormalized,.0001f));
        const auto Release=Mesh->GetBoneSpaceTransforms();
        State.elapsed=Time+.08;Entity.cooldown=.32;State.entities[0]=Entity;Actor->Synchronize(Entity,State,.08f);
        TestTrue(TEXT("Actual imported recoil animates after every shot"),Actor->CurrentAnimation()==TEXT("Attack")&&
            Actor->AnimationCycleFraction()>Card->AttackContactNormalized&&!SamePose(Release,Mesh->GetBoneSpaceTransforms()));
        State.elapsed=Time+.25;Entity.cooldown=.15;State.entities[0]=Entity;Actor->Synchronize(Entity,State,.17f);
        TestEqual(TEXT("Completed recoil releases the actor into idle before the next shot"),Actor->CurrentAnimation(),FName(TEXT("Idle")));
    }
    State.elapsed=2.5;Entity.position.z-=.025;Entity.cooldown=0;State.entities[0]=Entity;Actor->Synchronize(Entity,State,1.f/60.f);
    TestEqual(TEXT("Moving after a shot resumes the authored slow walk"),Actor->CurrentAnimation(),FName(TEXT("Locomotion")));
    return true;
}
#endif
