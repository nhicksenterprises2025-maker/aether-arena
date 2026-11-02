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
#include "Components/InstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "RiftMatchSubsystem.h"
#include "RiftReplaySubsystem.h"
#include "RiftCardData.h"
#include "Presentation/RiftArenaPresentation.h"
#include "Presentation/RiftUnitVisual.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftPlaguePresentationTest,"Rift.Integration.PlagueQueenPresentation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftPlaguePresentationTest::RunTest(const FString& Parameters)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox")))
    {AddError(TEXT("Plague presentation verification requires isolated automation saves."));return false;}
    auto* Card=LoadObject<URiftCardData>(nullptr,TEXT("/Game/Rift/Cards/DA_plague_queen.DA_plague_queen"));
    if(!TestNotNull(TEXT("Queen imports a real native card"),Card))return false;
    TestNotNull(TEXT("Card art is the actual model render"),Card->Illustration.Get());
    if(!TestNotNull(TEXT("Queen has the imported skeletal model"),Card->CharacterMesh.Get()))return false;
    for(const TCHAR* Bone:{TEXT("attack_origin"),TEXT("status_anchor"),TEXT("impact_origin"),TEXT("hp_anchor")})
        TestTrue(FString(Bone)+TEXT(" retains the authored visual anchor"),Card->CharacterMesh->GetRefSkeleton().FindBoneIndex(Bone)!=INDEX_NONE);
    for(const TCHAR* Name:{TEXT("Idle"),TEXT("Locomotion"),TEXT("Attack"),TEXT("DrawRelease"),TEXT("Hit"),TEXT("Death"),TEXT("Deploy"),TEXT("Status"),TEXT("Turn"),TEXT("Acquire")})
    {
        const auto* Clip=Card->Animations.Find(FName(Name));
        TestTrue(FString(Name)+TEXT(" uses the actual Queen skeleton and nonzero clip"),Clip&&*Clip&&(*Clip)->GetSkeleton()==Card->CharacterMesh->GetSkeleton()&&(*Clip)->GetPlayLength()>0.);
    }
    for(const TCHAR* Name:{TEXT("plague_arrow"),TEXT("toxic_bubble"),TEXT("poison_splash")})
    {
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,*FString::Printf(TEXT("/Game/Rift/Environment/SM_%s.SM_%s"),Name,Name));
        if(TestNotNull(FString(Name)+TEXT(" is imported authored geometry"),Mesh))
            TestTrue(FString(Name)+TEXT(" has finite, visible geometry"),!Mesh->GetBounds().BoxExtent.ContainsNaN()&&Mesh->GetBounds().BoxExtent.GetMin()>.01&&Mesh->GetStaticMaterials().Num()>0);
    }
    auto* GI=NewObject<UGameInstance>(GEngine);GI->InitializeStandalone(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto* World=GI->GetWorld();auto* Match=World->GetSubsystem<URiftMatchSubsystem>();
    auto* Arena=World->SpawnActor<ARiftArenaPresentation>();if(!TestNotNull(TEXT("Production presentation actor spawns"),Arena))return false;
    Arena->DispatchBeginPlay();
    ON_SCOPE_EXIT {Match->LeaveMatch();GI->GetSubsystem<URiftReplaySubsystem>()->FlushPendingWrites();World->DestroyWorld(false);GI->Shutdown();GEngine->DestroyWorldContext(World);};
    auto Start=[&]()
    {Match->StartMatch(true);Match->SetSpeed(0);auto* Sim=Match->Simulation();Sim->SetAIEnabled(rift::Team::Player,false);Sim->SetAIEnabled(rift::Team::Enemy,false);Sim->ClearField();Match->FlushEvents();Arena->Tick(0);};
    auto Advance=[&](double Until)
    {auto* Sim=Match->Simulation();while(Sim->State().elapsed+1.e-8<Until){Sim->Step(1./60.);Match->FlushEvents();Arena->Tick(0);}};
    auto Diagnostics=[&]()
    {TSharedPtr<FJsonObject> O;TestTrue(TEXT("Presentation diagnostics deserialize"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Arena->NiagaraDiagnosticsJSON()),O)&&O.IsValid());return O;};
    auto BatchCount=[&](const TCHAR* Name)
    {
        auto O=Diagnostics();if(!O)return -1;
        const TArray<TSharedPtr<FJsonValue>>* Effects=nullptr;
        if(!TestTrue(TEXT("Diagnostics contain surface effects"),O->TryGetArrayField(TEXT("expansionSurfaceEffects"),Effects)))return -1;
        for(const auto& V:*Effects)
        {
            const auto Effect=V.IsValid()?V->AsObject():nullptr;
            if(!TestTrue(TEXT("Surface effect diagnostics are objects"),Effect.IsValid()))return -1;
            if(Effect->GetStringField(TEXT("name"))==Name)return int32(Effect->GetNumberField(TEXT("instances")));
        }
        return 0;
    };
    Start();auto* Sim=Match->Simulation();
    if(!TestTrue(TEXT("Queen and ground victim deploy normally"),Sim->Spawn(rift::Team::Player,"plague_queen",{-3.5,6.5})&&Sim->Spawn(rift::Team::Enemy,"ironclad",{-3.5,-5.5})))return false;
    rift::EntityId Queen=0,Victim=0;for(const auto& E:Sim->State().entities){if(E.cardId=="plague_queen")Queen=E.id;if(E.cardId=="ironclad")Victim=E.id;}
    Match->FlushEvents();Arena->Tick(0);
    TestEqual(TEXT("An uncloaked Queen carries seven finite authored bubbles"),BatchCount(TEXT("QueenToxicBubbles")),7);
    double Impact=-1;int32 PoisonTicks=0;
    const auto Observer=Match->OnEvent.AddLambda([&](const rift::Event& E){if(E.cardId=="plague_queen"&&E.type=="poison_splash"&&Impact<0)Impact=E.time;if(E.cardId=="plague_queen"&&E.type=="poison_tick"&&E.target==Victim)++PoisonTicks;});
    ON_SCOPE_EXIT {Match->OnEvent.Remove(Observer);};
    const double StartTime=Sim->State().elapsed;
    while(Sim->State().projectiles.empty()&&Sim->State().elapsed<StartTime+4.)Advance(Sim->State().elapsed+1./60.);
    bool NativeArrow=false;
    const auto ProjectileDiagnostics=Diagnostics();
    if(!ProjectileDiagnostics.IsValid())return false;
    const TArray<TSharedPtr<FJsonValue>>* ProjectileVisuals=nullptr;
    if(!TestTrue(TEXT("Diagnostics contain projectile visuals"),ProjectileDiagnostics->TryGetArrayField(TEXT("projectileVisuals"),ProjectileVisuals)))return false;
    for(const auto& V:*ProjectileVisuals)
    {
        const auto O=V.IsValid()?V->AsObject():nullptr;
        if(!TestTrue(TEXT("Projectile diagnostics are objects"),O.IsValid()))return false;
        if(O->GetStringField(TEXT("cardId"))==TEXT("plague_queen"))NativeArrow=O->GetBoolField(TEXT("meshPresent"))&&O->GetBoolField(TEXT("collisionDisabled"))&&O->GetStringField(TEXT("mesh")).Contains(TEXT("SM_plague_arrow"));
    }
    TestTrue(TEXT("An actual committed projectile has the authored poison arrow"),NativeArrow);
    while(Impact<0&&Sim->State().elapsed<StartTime+5.)Advance(Sim->State().elapsed+1./60.);
    if(!TestTrue(TEXT("Real projectile contact emits poison impact"),Impact>=0))return false;
    Advance(Impact+.10);
    TestEqual(TEXT("The poison follows its actual victim with five bubbles"),BatchCount(TEXT("PoisonVictimBubbles")),5);
    TestEqual(TEXT("Impact produces bounded radial droplet geometry"),BatchCount(TEXT("PoisonSplashDrops")),36);
    const auto VictimBody=[&]()->const rift::Entity*{for(const auto& E:Sim->State().entities)if(E.id==Victim)return &E;return nullptr;};
    const auto* ImpactVictim=VictimBody();
    if(!TestNotNull(TEXT("The struck victim survives direct impact"),ImpactVictim))return false;
    const double BeforeTicks=ImpactVictim->hp;
    // Start real invisibility after poison lands; status geometry must disappear
    // even though both committed DoT ticks still resolve on that same victim.
    if(!TestTrue(TEXT("Poison victim casts a real cloak"),Sim->Spawn(rift::Team::Enemy,"shadow_surge",ImpactVictim->position)))return false;
    Match->FlushEvents();Arena->Tick(0);Advance(Impact+.90);
    const auto* HiddenVictim=Arena->Visual(Victim);
    if(!TestNotNull(TEXT("The cloaked victim retains its visual actor"),HiddenVictim))return false;
    TestTrue(TEXT("Enemy victim is hidden by actual Shadow Surge"),HiddenVictim->HiddenFromViewer());
    TestEqual(TEXT("Attached poison cannot expose the invisible enemy"),BatchCount(TEXT("PoisonVictimBubbles")),0);
    Advance(Impact+1.30);
    TestEqual(TEXT("First hidden poison tick resolves once"),PoisonTicks,1);
    const auto* FirstTickVictim=VictimBody();
    if(!TestNotNull(TEXT("The victim survives its first poison tick"),FirstTickVictim))return false;
    TestEqual(TEXT("First poison tick does exactly sixty damage"),FirstTickVictim->hp,BeforeTicks-60.);
    TestEqual(TEXT("A hidden tick cannot create tracking burst geometry"),BatchCount(TEXT("PoisonTickBurst")),0);
    Advance(Impact+2.55);
    TestEqual(TEXT("Second hidden poison tick resolves once"),PoisonTicks,2);
    const auto* SecondTickVictim=VictimBody();
    if(!TestNotNull(TEXT("The victim survives both poison ticks"),SecondTickVictim))return false;
    TestEqual(TEXT("Two poison ticks retain their full damage"),SecondTickVictim->hp,BeforeTicks-120.);
    TestEqual(TEXT("Expired poison leaves no attached bubble geometry"),BatchCount(TEXT("PoisonVictimBubbles")),0);
    TestEqual(TEXT("Impact geometry cleans up independently of source attacks"),BatchCount(TEXT("PoisonSplashDrops")),0);
    Start();Sim=Match->Simulation();
    if(!TestTrue(TEXT("Enemy Queen deploys and cloaks normally"),Sim->Spawn(rift::Team::Enemy,"plague_queen",{0,-6.5})&&Sim->Spawn(rift::Team::Enemy,"shadow_surge",{0,-6.5})))return false;
    Match->FlushEvents();Arena->Tick(0);
    TestEqual(TEXT("Enemy Queen bubbles are visible before actual cloak activation"),BatchCount(TEXT("QueenToxicBubbles")),7);
    Advance(Sim->State().elapsed+.80);
    TestEqual(TEXT("Queen source bubbles are removed when she becomes invisible"),BatchCount(TEXT("QueenToxicBubbles")),0);
    Arena->Tick(0);
    TestEqual(TEXT("Paused redraw cannot expose a hidden Queen"),BatchCount(TEXT("QueenToxicBubbles")),0);
    return !HasAnyErrors();
}
#endif
