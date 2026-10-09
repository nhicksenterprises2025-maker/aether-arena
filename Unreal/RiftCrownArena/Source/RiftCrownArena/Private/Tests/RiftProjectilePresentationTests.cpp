#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/StaticMesh.h"
#include "Presentation/RiftArenaPresentation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftProjectilePresentationTest,"Rift.Integration.ProjectilePresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRiftProjectilePresentationTest::RunTest(const FString& Parameters)
{
    const FVector Launch(-720.,410.,205.),GroundTarget(-580.,-160.,125.),FlyingTarget(-400.,-320.,310.);
    const std::pair<const char*,const TCHAR*> Bindings[]={
        {"ember_archer",TEXT("arrow_projectile")},{"archer_tower",TEXT("arrow_projectile")},
        {"arc_mage",TEXT("arc_projectile")},{"sky_manta",TEXT("manta_projectile")},
        {"storm_raven",TEXT("storm_projectile")},{"tower_guard",TEXT("bullet_round")},
        {"tower_core",TEXT("bullet_round")}};
    int32 RangedCards=0;
    for (const auto& Card:rift::Cards()) if (Card.projectileSpeed>0.) ++RangedCards;
    TestEqual(TEXT("All five numerical ranged cards are bound, plus both crown towers"),RangedCards,5);
    for (const auto& Binding:Bindings)
    {
        const FString Label=UTF8_TO_TCHAR(Binding.first);
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,*FString::Printf(TEXT("/Game/Rift/Environment/SM_%s.SM_%s"),Binding.second,Binding.second));
        if (TestNotNull(Label+TEXT(" has a real authored missile mesh"),Mesh))
        {
            const FVector Size=Mesh->GetBounds().BoxExtent*2.;
            TestTrue(Label+TEXT(" has finite visible bounded geometry"),!Size.ContainsNaN()&&Size.GetMin()>0.1&&Size.GetMax()<=120.);
            TestTrue(Label+TEXT(" has assigned materials"),Mesh->GetStaticMaterials().Num()>0);
        }
        for (const FVector Target:{GroundTarget,FlyingTarget,Launch})
        {
            TestTrue(Label+TEXT(" begins at its exact launch socket"),ARiftArenaPresentation::ProjectilePathPoint(Binding.first,Launch,Target,0.)==Launch);
            TestTrue(Label+TEXT(" ends exactly at the recorded target"),ARiftArenaPresentation::ProjectilePathPoint(Binding.first,Launch,Target,1.)==Target);
            TestTrue(Label+TEXT(" clamps stale negative progress"),ARiftArenaPresentation::ProjectilePathPoint(Binding.first,Launch,Target,-.25)==Launch);
            TestTrue(Label+TEXT(" clamps completed travel"),ARiftArenaPresentation::ProjectilePathPoint(Binding.first,Launch,Target,1.25)==Target);
            const FVector Paused=ARiftArenaPresentation::ProjectilePathPoint(Binding.first,Launch,Target,.375);
            TestTrue(Label+TEXT(" paused/replayed progress reconstructs identical position"),Paused==ARiftArenaPresentation::ProjectilePathPoint(Binding.first,Launch,Target,.375));
            TestTrue(Label+TEXT(" has finite paths even for coincident anchors"),!Paused.ContainsNaN());
            TestTrue(Label+TEXT(" arc remains below one tile above interpolation"),Paused.Z-FMath::Lerp(Launch,Target,.375).Z<=100.);
        }
    }
    auto* Trail=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Rift/Environment/SM_projectile_trail.SM_projectile_trail"));
    TestNotNull(TEXT("Flight trails have their own authored mesh"),Trail);
    const FVector ShortTarget=Launch+FVector(8.,0.,0.);
    const FVector ShortArc=ARiftArenaPresentation::ProjectilePathPoint("ember_archer",Launch,ShortTarget,.5);
    TestTrue(TEXT("Short arrows remain readable without a large lob"),ShortArc.Z>Launch.Z&&ShortArc.Z<=Launch.Z+30.);
    return true;
}
#endif
