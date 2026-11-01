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
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "RiftMatchSubsystem.h"
#include "RiftReplaySubsystem.h"
#include "RiftCardData.h"
#include "Presentation/RiftArenaPresentation.h"
#include "Presentation/RiftUnitVisual.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftThunderBladePresentationTest,"Rift.Integration.ThunderBladePresentation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftThunderBladePresentationTest::RunTest(const FString& Parameters)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox")))
    {AddError(TEXT("Thunderstorm/Blade Box verification requires isolated automation saves."));return false;}
    auto* Box=LoadObject<URiftCardData>(nullptr,TEXT("/Game/Rift/Cards/DA_blade_box.DA_blade_box"));
    auto* Storm=LoadObject<URiftCardData>(nullptr,TEXT("/Game/Rift/Cards/DA_thunderstorm.DA_thunderstorm"));
    if(!TestNotNull(TEXT("Blade Box is a real imported card"),Box)||!TestNotNull(TEXT("Thunderstorm is a real imported card"),Storm))return false;
    TestNotNull(TEXT("Box has an actual rendered portrait"),Box->Illustration.Get());
    TestNotNull(TEXT("Storm has an actual rendered portrait"),Storm->Illustration.Get());
    if(!TestNotNull(TEXT("Box has its articulated barracks mesh"),Box->CharacterMesh.Get()))return false;
    TestEqual(TEXT("Box imports its ten authored bones and FBX armature wrapper"),Box->CharacterMesh->GetRefSkeleton().GetNum(),11);
    for(const TCHAR* Bone:{TEXT("root"),TEXT("door_l"),TEXT("door_r"),TEXT("latch"),TEXT("signal"),TEXT("spawn_origin"),TEXT("attack_origin"),TEXT("hp_anchor"),TEXT("status_anchor"),TEXT("impact_origin")})
        TestTrue(FString(Bone)+TEXT(" authored mechanism bone is retained"),Box->CharacterMesh->GetRefSkeleton().FindBoneIndex(Bone)!=INDEX_NONE);
    for(const TCHAR* Name:{TEXT("Idle"),TEXT("Locomotion"),TEXT("Attack"),TEXT("Spawn"),TEXT("Hit"),TEXT("Death"),TEXT("Deploy"),TEXT("Status"),TEXT("Turn"),TEXT("Acquire")})
    {
        const auto* Clip=Box->Animations.Find(FName(Name));
        TestTrue(FString(Name)+TEXT(" uses the actual box skeleton"),Clip&&*Clip&&(*Clip)->GetSkeleton()==Box->CharacterMesh->GetSkeleton()&&(*Clip)->GetPlayLength()>0.);
    }
    const auto* Spawn=Box->Animations.Find(TEXT("Spawn"));
    if(!TestTrue(TEXT("Spawn doors have the authored .60-second cycle"),Spawn&&*Spawn&&FMath::IsNearlyEqual((*Spawn)->GetPlayLength(),.6,.002)))return false;
    for(const TCHAR* Name:{TEXT("thunderstorm_cloud"),TEXT("thunderstorm_bolt"),TEXT("thunderstorm_rain"),TEXT("stun_spark")})
    {
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,*FString::Printf(TEXT("/Game/Rift/Environment/SM_%s.SM_%s"),Name,Name));
        if(TestNotNull(FString(Name)+TEXT(" imports actual authored geometry"),Mesh))
            TestTrue(FString(Name)+TEXT(" has finite nonempty bounds and materials"),!Mesh->GetBounds().BoxExtent.ContainsNaN()&&Mesh->GetBounds().BoxExtent.GetMin()>0.01&&Mesh->GetStaticMaterials().Num()>0);
    }
    auto* GI=NewObject<UGameInstance>(GEngine);GI->InitializeStandalone(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto* World=GI->GetWorld();auto* Match=World->GetSubsystem<URiftMatchSubsystem>();
    auto* Arena=World->SpawnActor<ARiftArenaPresentation>();
    if(!TestNotNull(TEXT("Production arena visual spawns"),Arena))return false;
    Arena->DispatchBeginPlay();
    ON_SCOPE_EXIT {Match->LeaveMatch();GI->GetSubsystem<URiftReplaySubsystem>()->FlushPendingWrites();World->DestroyWorld(false);GI->Shutdown();GEngine->DestroyWorldContext(World);};
    auto Start=[&]()
    {
        Match->StartMatch(true);Match->SetSpeed(0);auto* Sim=Match->Simulation();
        Sim->SetAIEnabled(rift::Team::Player,false);Sim->SetAIEnabled(rift::Team::Enemy,false);Sim->ClearField();Match->FlushEvents();Arena->Tick(0);
    };
    auto AdvanceTo=[&](double Until)
    {
        auto* Sim=Match->Simulation();while(Sim->State().elapsed+1.e-8<Until)
        {Sim->Step(1./60.);Match->FlushEvents();Arena->Tick(0.f);}
    };
    auto Diagnostics=[&]()
    {TSharedPtr<FJsonObject> Result;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Arena->NiagaraDiagnosticsJSON()),Result);return Result;};
    auto BatchCount=[&](const TSharedPtr<FJsonObject>& Report,const TCHAR* Name)
    {
        if(!Report)return -1;
        for(const auto& Value:Report->GetArrayField(TEXT("expansionSurfaceEffects")))
        {
            const auto Row=Value->AsObject();if(Row->GetStringField(TEXT("name"))!=Name)continue;
            TestTrue(FString(Name)+TEXT(" is visible, noninteractive and uses its imported mesh"),Row->GetBoolField(TEXT("visible"))&&Row->GetBoolField(TEXT("nonInteractive"))&&Row->GetStringField(TEXT("mesh")).StartsWith(TEXT("/Game/Rift/Environment/SM_")));
            return int32(Row->GetNumberField(TEXT("instances")));
        }
        return 0;
    };
    Start();auto* Sim=Match->Simulation();rift::Vec2 Center{};TArray<rift::EntityId> Victims;
    for(const auto& E:Sim->State().entities)if(E.team==rift::Team::Enemy&&E.kind==rift::EntityKind::Guard&&E.lane<0){Center=E.position;Victims.Add(E.id);break;}
    if(!TestEqual(TEXT("Fixture uses the actual enemy Guard"),Victims.Num(),1))return false;
    const char* Cards[]={"ironclad","storm_raven","archer_tower"};const rift::Vec2 Offsets[]={{2.2,-1.},{-2.,-.5},{0,2.3}};
    for(int32 I=0;I<3;++I)
    {
        const size_t Before=Sim->State().entities.size();
        if(!TestTrue(TEXT("Ground, air and building victims deploy normally"),Sim->Spawn(rift::Team::Enemy,Cards[I],{Center.x+Offsets[I].x,Center.z+Offsets[I].z})))return false;
        for(size_t J=Before;J<Sim->State().entities.size();++J)Victims.Add(Sim->State().entities[J].id);
    }
    Match->FlushEvents();Arena->Tick(0);
    const double Born=Sim->State().elapsed;
    int32 Waves=0;const auto Observer=Match->OnEvent.AddLambda([&](const rift::Event& E){if(E.type=="spell_impact"&&E.cardId=="thunderstorm")++Waves;});
    ON_SCOPE_EXIT {Match->OnEvent.Remove(Observer);};
    if(!TestTrue(TEXT("Thunderstorm casts through real simulation"),Sim->Spawn(rift::Team::Player,"thunderstorm",Center)))return false;
    Match->FlushEvents();Arena->Tick(0);
    for(double Age:{.2,.5,.95,1.4,1.7})
    {
        AdvanceTo(Born+Age);const auto Report=Diagnostics();
        if(!TestTrue(TEXT("Actual weather diagnostics parse"),Report.IsValid()))return false;
        const bool Active=Age<1.57,Strike=Age>.45&&Age<1.57;
        TestEqual(FString::Printf(TEXT("Cloud follows real cast lifetime at %.2f"),Age),BatchCount(Report,TEXT("StormCloud")),Active?1:0);
        TestEqual(FString::Printf(TEXT("Rain follows real cast lifetime at %.2f"),Age),BatchCount(Report,TEXT("StormRain")),Active?48:0);
        TestEqual(FString::Printf(TEXT("Area lightning follows each actual wave at %.2f"),Age),BatchCount(Report,TEXT("StormAreaFlash")),Strike?1:0);
        TestEqual(FString::Printf(TEXT("Each actual target receives lightning at %.2f"),Age),BatchCount(Report,TEXT("StormTargetLightning")),Strike?4:0);
        TestEqual(FString::Printf(TEXT("All four target kinds have visible stun stars at %.2f"),Age),BatchCount(Report,TEXT("UniversalStunStars")),Strike?12:0);
        TestEqual(TEXT("Only elapsed canonical waves have resolved"),Waves,FMath::Clamp(int32(FMath::FloorToInt((Age+1.e-6)/.45)),0,3));
        for(auto Id:Victims)
        {
            auto* Visual=Arena->Visual(Id);if(!TestNotNull(TEXT("Affected target keeps its actual native model"),Visual))return false;
            if(Id!=Victims[0])
            {
                if(Strike)TestEqual(TEXT("Stunned skeletal ground/air/building models use the authored Status clip"),Visual->CurrentAnimation(),FName(TEXT("Status")));
                else TestTrue(TEXT("No Status clip leaks before first wave or after expiry"),Visual->CurrentAnimation()!=TEXT("Status"));
            }
        }
    }
    Start();Sim=Match->Simulation();
    const auto* ShadowCard=rift::FindCard("shadow_surge");const auto* StormCard=rift::FindCard("thunderstorm");
    if(!TestTrue(TEXT("Late-cloak fixture uses canonical spell definitions"),ShadowCard&&StormCard))return false;
    const double CloakBorn=Sim->State().elapsed;const rift::Vec2 CloakCenter{0,-5.};
    if(!TestTrue(TEXT("Late-cloak victim deploys normally"),Sim->Spawn(rift::Team::Enemy,"ironclad",CloakCenter)))return false;
    const rift::EntityId CloakVictim=Sim->State().entities.back().id;
    auto VictimBody=[&]()->const rift::Entity*
    {for(const auto& E:Sim->State().entities)if(E.id==CloakVictim)return &E;return nullptr;};
    const double BeforeLightningHP=VictimBody()->hp;
    if(!TestTrue(TEXT("Enemy starts a real delayed Shadow Surge"),Sim->Spawn(rift::Team::Enemy,"shadow_surge",CloakCenter)))return false;
    Match->FlushEvents();Arena->Tick(0);
    // Land the first wave .10 seconds before cloak activation, within the
    // .17-second bolt lifetime. No fixture edits to HP, status or positions.
    AdvanceTo(CloakBorn+ShadowCard->castDelay-StormCard->castDelay-.10);
    const double FirstLightningAt=Sim->State().elapsed+StormCard->castDelay;
    if(!TestTrue(TEXT("Lightning casts before the pending cloak activates"),Sim->Spawn(rift::Team::Player,"thunderstorm",VictimBody()->position)))return false;
    Match->FlushEvents();Arena->Tick(0);
    AdvanceTo(CloakBorn+ShadowCard->castDelay-.05);
    auto* CloakedVisual=Arena->Visual(CloakVictim);
    if(!TestNotNull(TEXT("Late-cloak victim has a real native visual"),CloakedVisual))return false;
    TestFalse(TEXT("Positive-control victim is visible when lightning lands"),CloakedVisual->HiddenFromViewer());
    TestEqual(TEXT("Visible victim has one live target bolt before cloak"),BatchCount(Diagnostics(),TEXT("StormTargetLightning")),1);
    TestEqual(TEXT("First canonical wave damages the victim once"),VictimBody()->hp,BeforeLightningHP-StormCard->damage);
    AdvanceTo(CloakBorn+ShadowCard->castDelay+1./60.);
    TestTrue(TEXT("The hidden bolt is removed before its normal lifetime ends"),Sim->State().elapsed-FirstLightningAt<.17);
    TestTrue(TEXT("Real Shadow Surge hides the enemy before that bolt expires"),CloakedVisual->HiddenFromViewer()&&VictimBody()->invisibleUntil>Sim->State().elapsed);
    TestEqual(TEXT("A bolt recorded while visible cannot track a newly cloaked enemy"),BatchCount(Diagnostics(),TEXT("StormTargetLightning")),0);
    TestEqual(TEXT("Concealment does not change resolved lightning damage"),VictimBody()->hp,BeforeLightningHP-StormCard->damage);
    Arena->Tick(0);
    TestEqual(TEXT("Paused redraw cannot restore a hidden target bolt"),BatchCount(Diagnostics(),TEXT("StormTargetLightning")),0);

    Start();Sim=Match->Simulation();
    if(!TestTrue(TEXT("Box deploys through real simulation"),Sim->Spawn(rift::Team::Player,"blade_box",{0,6.5})))return false;
    Match->FlushEvents();Arena->Tick(0);rift::EntityId BoxId=0;
    for(const auto& E:Sim->State().entities)if(E.cardId=="blade_box")BoxId=E.id;
    auto* Visual=Arena->Visual(BoxId);if(!TestNotNull(TEXT("Real Box visual is present"),Visual))return false;
    auto* Mechanism=Visual->FindComponentByClass<USkeletalMeshComponent>();if(!TestNotNull(TEXT("Real box skeletal component exists"),Mechanism))return false;
    TestTrue(TEXT("Authored door and child exit bones are imported"),Mechanism->DoesSocketExist(TEXT("door_l"))&&Mechanism->DoesSocketExist(TEXT("door_r"))&&Mechanism->DoesSocketExist(TEXT("spawn_origin")));
    const double BoxBorn=Sim->State().elapsed;AdvanceTo(BoxBorn+2.7);
    const FVector LeftClosed=Mechanism->GetSocketTransform(TEXT("door_l"),RTS_Component).GetTranslation();
    const FVector RightClosed=Mechanism->GetSocketTransform(TEXT("door_r"),RTS_Component).GetTranslation();
    const FVector OpeningDirection=(RightClosed-LeftClosed).GetSafeNormal();
    auto DoorDiagnostic=[&](const TCHAR* Phase)
    {
        const auto Left=Mechanism->GetSocketTransform(TEXT("door_l"),RTS_Component).GetTranslation();
        const auto Right=Mechanism->GetSocketTransform(TEXT("door_r"),RTS_Component).GetTranslation();
        AddInfo(FString::Printf(TEXT("Box doors %s: elapsed=%.6f clip=%s position=%.6f blend=%.6f left=%s right=%s leftDelta=%.6f rightDelta=%.6f componentScale=%s importedRoot=%s"),Phase,Sim->State().elapsed,*Visual->CurrentAnimation().ToString(),Visual->AnimationPosition(),Visual->AnimationBlendAlpha(),*Left.ToString(),*Right.ToString(),FVector::DotProduct(Left-LeftClosed,OpeningDirection),FVector::DotProduct(Right-RightClosed,OpeningDirection),*Mechanism->GetRelativeScale3D().ToString(),*Box->CharacterMesh->GetRefSkeleton().GetBoneName(0).ToString()));
    };
    DoorDiagnostic(TEXT("idle baseline"));
    auto Children=[&](){int32 Count=0;for(const auto& E:Sim->State().entities)if(E.parentEntityId==BoxId&&E.cardId=="twin_blades")++Count;return Count;};
    TestEqual(TEXT("Box creates no premature troop"),Children(),0);
    AdvanceTo(BoxBorn+2.9);DoorDiagnostic(TEXT("anticipation"));TestEqual(TEXT("Door anticipation selects the real Spawn clip"),Visual->CurrentAnimation(),FName(TEXT("Spawn")));
    TestTrue(TEXT("Both imported doors move outward before the spawn"),FVector::DotProduct(Mechanism->GetSocketTransform(TEXT("door_l"),RTS_Component).GetTranslation()-LeftClosed,OpeningDirection)<-1.&&FVector::DotProduct(Mechanism->GetSocketTransform(TEXT("door_r"),RTS_Component).GetTranslation()-RightClosed,OpeningDirection)>1.);
    TestEqual(TEXT("Opening doors do not create a troop early"),Children(),0);
    AdvanceTo(BoxBorn+3.05);DoorDiagnostic(TEXT("open"));TestEqual(TEXT("First real birth creates exactly one Twin Blade"),Children(),1);
    TestEqual(TEXT("Actual building_spawn event continues the open Spawn pose"),Visual->CurrentAnimation(),FName(TEXT("Spawn")));
    TestTrue(TEXT("Authored opening reaches its full .35-meter travel per door"),FMath::IsNearlyEqual(FVector::DotProduct(Mechanism->GetSocketTransform(TEXT("door_l"),RTS_Component).GetTranslation()-LeftClosed,OpeningDirection),-35.,.5)&&FMath::IsNearlyEqual(FVector::DotProduct(Mechanism->GetSocketTransform(TEXT("door_r"),RTS_Component).GetTranslation()-RightClosed,OpeningDirection),35.,.5));
    AdvanceTo(BoxBorn+3.65);DoorDiagnostic(TEXT("closed after Idle blend"));TestEqual(TEXT("Completed door cycle returns to Idle"),Visual->CurrentAnimation(),FName(TEXT("Idle")));
    TestTrue(TEXT("Both physical doors return to their closed rails"),Mechanism->GetSocketTransform(TEXT("door_l"),RTS_Component).GetTranslation().Equals(LeftClosed,.1)&&Mechanism->GetSocketTransform(TEXT("door_r"),RTS_Component).GetTranslation().Equals(RightClosed,.1));
    TestEqual(TEXT("Door animation cannot duplicate the child"),Children(),1);
    AddInfo(TEXT("Actual imported weather meshes, three live waves, four target-kind stun clips/stars, finite cleanup and real one-child articulated hatch timing verified."));
    return !HasAnyErrors();
}
#endif
