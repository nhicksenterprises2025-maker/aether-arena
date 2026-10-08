#include "Presentation/RiftUnitVisual.h"
#include "RiftDiagnostics.h"
#include "RiftCardData.h"
#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/DecalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

ARiftUnitVisual::ARiftUnitVisual()
{
    PrimaryActorTick.bCanEverTick=false;
    Scene=CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));SetRootComponent(Scene);
    Character=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("AuthoredCharacter"));Character->SetupAttachment(Scene);
    Character->SetCollisionEnabled(ECollisionEnabled::NoCollision);Character->SetGenerateOverlapEvents(false);
    Character->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Character->SetComponentTickEnabled(false);Character->bEnableUpdateRateOptimizations=false;
    Character->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Structure=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AuthoredStructure"));Structure->SetupAttachment(Scene);
    Structure->SetCollisionEnabled(ECollisionEnabled::NoCollision);Structure->SetGenerateOverlapEvents(false);
    GroundRing=CreateDefaultSubobject<UDecalComponent>(TEXT("TeamGroundRing"));GroundRing->SetupAttachment(Scene);
    GroundRing->SetRelativeRotation(FRotator(-90,0,0));GroundRing->SetRelativeLocation(FVector(0,0,6));
    GroundRing->DecalSize=FVector(32,52,52);GroundRing->SortOrder=1;
}
FLinearColor ARiftUnitVisual::TeamColor(rift::Team Value)
{
    return Value==rift::Team::Player?FLinearColor(.10f,.69f,.91f,1.f):FLinearColor(.94f,.30f,.21f,1.f);
}
void ARiftUnitVisual::ColorMesh(UMeshComponent* Component,FLinearColor Color)
{
    for (int32 Index=0;Index<Component->GetNumMaterials();++Index)
        if (auto* Material=Component->CreateDynamicMaterialInstance(Index))
        {
            Material->SetVectorParameterValue(TEXT("TeamColor"),Color);
            Materials.Add(Material);
        }
}
bool ARiftUnitVisual::InitializeEntity(const rift::Entity& Entity,URiftCardData* Card,double Time)
{
    EntityId=Entity.id; Kind=Entity.kind; Team=Entity.team; CardData=Card; Born=Entity.born;
    bFlying=Entity.flying; bCoreActive=Entity.active; bDead=false;
    AssetId=UTF8_TO_TCHAR(Entity.cardId.c_str());AnimationPhase=float(Entity.id%17)/17.f;AnimationTime=Time;
    const rift::Card* Definition=rift::FindCard(Entity.cardId);
    Scale=Definition?float(Definition->scale):1.f;
    AttackInterval=Definition?Definition->attackInterval:Kind==rift::EntityKind::Core?.92:1.02;
    if (Kind==rift::EntityKind::Troop)
    {
        if (!Card || !Card->CharacterMesh) {RIFT_LOG(LogRift,Error,TEXT("Missing character for entity %llu"),EntityId);return false;}
        Character->SetSkeletalMeshAsset(Card->CharacterMesh);
        Character->SetRelativeScale3D(FVector(Scale));Clips=Card->Animations;
    }
    else
    {
        const FString MeshId=Kind==rift::EntityKind::Core?TEXT("tower_core"):Kind==rift::EntityKind::Guard?TEXT("tower_guard"):AssetId;
        UStaticMesh* Mesh=Card?Card->StructureMesh.Get():LoadObject<UStaticMesh>(nullptr,*FString::Printf(TEXT("/Game/Rift/Environment/SM_%s.SM_%s"),*MeshId,*MeshId));
        if (!Mesh) {RIFT_LOG(LogRift,Error,TEXT("Missing authored structure %s"),*MeshId);return false;}
        Structure->SetStaticMesh(Mesh);
        if (Kind==rift::EntityKind::Building && Definition && Definition->footprint>0)
        {
            const FVector Bounds=Mesh->GetBounds().BoxExtent*2;
            const float PlanScale=Definition->footprint*100/FMath::Max(Bounds.X,Bounds.Y);
            Structure->SetRelativeScale3D(FVector(PlanScale,PlanScale,1));
        }
        GroundRing->DecalSize=FVector(32,Entity.radius*100,Entity.radius*100);
        if (Kind==rift::EntityKind::Building && AssetId==TEXT("archer_tower"))
        {
            AssetId=TEXT("tower_archer");
            auto* Archer=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Rift/Characters/tower_archer/SK_tower_archer.SK_tower_archer"));
            if (!Archer) {RIFT_LOG(LogRift,Error,TEXT("Archer Tower decoration missing"));return false;}
            Character->SetSkeletalMeshAsset(Archer);Character->SetRelativeLocation(FVector(0,0,194));Character->SetRelativeScale3D(FVector(.4));
        }
    }
    const FLinearColor Color=TeamColor(Team);ColorMesh(Character,Color);ColorMesh(Structure,Color);
    if (auto* Ring=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Rift/Materials/M_RiftGroundRing.M_RiftGroundRing")))
    {
        auto* Dynamic=UMaterialInstanceDynamic::Create(Ring,this);Dynamic->SetVectorParameterValue(TEXT("RingColor"),Color);
        GroundRing->SetDecalMaterial(Dynamic);Materials.Add(Dynamic);
    }
    SetActorLocation(URiftMatchSubsystem::WorldPoint(Entity.position));PreviousPosition=GetActorLocation();
    SetActorRotation(FRotator(0,FMath::RadiansToDegrees(FMath::Atan2(Entity.facing.z,Entity.facing.x)),0));
    if (Time-Born<.35) SetClip(TEXT("Deploy"),0); else SetClip(TEXT("Idle"),AnimationPhase,true);
    if (Kind==rift::EntityKind::Core && !bCoreActive)
        for (auto Material:Materials) Material->SetScalarParameterValue(TEXT("GlowStrength"),.25f);
    return true;
}
UAnimSequence* ARiftUnitVisual::Clip(FName Name)
{
    if (auto* Found=Clips.Find(Name)) return Found->Get();
    UAnimSequence* Result=LoadObject<UAnimSequence>(nullptr,*FString::Printf(TEXT("/Game/Rift/Characters/%s/Animations/AN_%s_%s.AN_%s_%s"),
        *AssetId,*AssetId,*Name.ToString(),*AssetId,*Name.ToString()));
    Clips.Add(Name,Result);return Result;
}
void ARiftUnitVisual::SetClip(FName Name,double NormalizedPosition,bool Loop)
{
    if (!Character->GetSkeletalMeshAsset()) return;
    UAnimSequence* Animation=Clip(Name);if (!Animation) return;
    if (CurrentClip!=Name)
    {
        Character->SetAnimation(Animation);if (auto* Node=Character->GetSingleNodeInstance()) Node->SetPlaying(false);CurrentClip=Name;
    }
    const double Fraction=Loop?FMath::Fmod(FMath::Max(0.,NormalizedPosition),1.):FMath::Clamp(NormalizedPosition,0.,1.);
    Character->SetPosition(float(Fraction*Animation->GetPlayLength()),false);
    Character->TickAnimation(0.f,false);Character->RefreshBoneTransforms();
}
void ARiftUnitVisual::AttackAt(double Time,double Damage)
{
    if (bDead) return;
    LastAttack=Time;
    // Contact/release is already authoritative at this event. Seek to the authored
    // .48 contact pose immediately, then recover; do not delay simulated damage.
    FName Action=AssetId==TEXT("frost_fang")?TEXT("FrostAttack"):AssetId==TEXT("arc_mage")?TEXT("Cast"):
        AssetId==TEXT("ember_archer")||AssetId==TEXT("tower_archer")?TEXT("DrawRelease"):
        AssetId==TEXT("twin_blades")?TEXT("AlternateStrike"):AssetId==TEXT("boulderback")?TEXT("HeavyImpact"):TEXT("Attack");
    SetClip(Action,.48);
}
void ARiftUnitVisual::HitAt(double Time){if (!bDead) LastHit=Time;}
void ARiftUnitVisual::SpecialAt(FName Name,double Time,float Duration)
{if (!bDead) {SpecialClip=Name;SpecialTime=Time;SpecialDuration=FMath::Max(.1f,Duration);}}
void ARiftUnitVisual::AdvanceDeath(double Time)
{if (bDead) SetClip(TEXT("Death"),(Time-DeathTime)/.8);}
void ARiftUnitVisual::DieAt(double Time)
{
    if (bDead) return;bDead=true;DeathTime=Time;GroundRing->SetVisibility(false);
    if (Character->GetSkeletalMeshAsset()) SetClip(TEXT("Death"),0);
    else if (auto* Rubble=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Rift/Environment/SM_tower_rubble.SM_tower_rubble")))
    {Structure->SetStaticMesh(Rubble);Structure->SetRelativeScale3D(FVector(Kind==rift::EntityKind::Core?1.55f:1.3f));}
}
void ARiftUnitVisual::ActivateCore()
{
    bCoreActive=true;for (auto Material:Materials) Material->SetScalarParameterValue(TEXT("GlowStrength"),1.4f);
}
bool ARiftUnitVisual::IsExpired(double Time)const
{
    return bDead && Kind!=rift::EntityKind::Core && Kind!=rift::EntityKind::Guard && Time-DeathTime>1.05;
}
void ARiftUnitVisual::Synchronize(const rift::Entity& Entity,const rift::Snapshot& State,float DeltaSeconds)
{
    const double Time=State.elapsed;
    FVector Position=URiftMatchSubsystem::WorldPoint(Entity.position);
    if (bFlying && !bDead) Position.Z=95.f+FMath::Sin(Time*3.2+AnimationPhase*UE_TWO_PI)*7.f;
    SetActorLocation(Position);
    const float Wanted=FMath::RadiansToDegrees(FMath::Atan2(Entity.facing.z,Entity.facing.x));
    const float Delta=FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,Wanted);
    if (FMath::Abs(Delta)>40 && Time-LastAttack>.3) TurnTime=Time;
    SetActorRotation(FRotator(0,FMath::FixedTurn(GetActorRotation().Yaw,Wanted,DeltaSeconds*540.f),0));
    if (Kind==rift::EntityKind::Building) GroundRing->SetRelativeRotation(FRotator(-90,0,-GetActorRotation().Yaw));
    if (bFlying) GroundRing->SetRelativeLocation(FVector(0,0,6-Position.Z));
    if (Entity.dead && !bDead) DieAt(Entity.died);
    if (Entity.active && !bCoreActive && Kind==rift::EntityKind::Core) ActivateCore();
    if (bDead)
    {
        AdvanceDeath(Time);
        PreviousPosition=Position;return;
    }
    const double Travel=(Position-PreviousPosition).Size2D();PreviousPosition=Position;
    if (Entity.target!=LastTarget && Entity.target) AcquireTime=Time;LastTarget=Entity.target;
    const bool Stunned=Entity.stunUntil>Time;
    const bool Slowed=Entity.slowUntil>Time && Entity.slowPct>0;
    const double AnimationDelta=FMath::Clamp(Time-AnimationTime,0.,.5);AnimationTime=Time;
    if (!Stunned) GaitClock+=AnimationDelta*(Slowed?1.-Entity.slowPct:1.)*1.15;
    for (auto Material:Materials)
    {
        Material->SetScalarParameterValue(TEXT("HitFlash"),float(FMath::Clamp(1.-(Time-LastHit)/.18,0.,1.)));
        Material->SetScalarParameterValue(TEXT("SlowAmount"),Slowed?float(Entity.slowPct):0.f);
    }
    if (!Character->GetSkeletalMeshAsset()) return;
    if (Stunned) {SetClip(TEXT("Status"),FMath::Fmod(Time*1.5,1.),true);return;}
    if (Time-Born<.3) {SetClip(TEXT("Deploy"),(Time-Born)/.3);return;}
    if (Time-SpecialTime<SpecialDuration) {SetClip(SpecialClip,(Time-SpecialTime)/SpecialDuration);return;}
    bool TargetInRange=false;
    if (Entity.target)
        for (const auto& Target:State.entities) if (Target.id==Entity.target && !Target.dead)
        {
            const double Distance=FMath::Sqrt(FMath::Square(Target.position.x-Entity.position.x)+FMath::Square(Target.position.z-Entity.position.z));
            const auto* Definition=rift::FindCard(Entity.cardId);
            TargetInRange=Definition && Distance-Target.radius<=Definition->range+.1;
            break;
        }
    if (Time-LastAttack<AttackInterval || (TargetInRange && Entity.cooldown>0 && Entity.cooldown<AttackInterval*.48))
    {
        const double Phase=.48+(Time-LastAttack)/FMath::Max(.1,AttackInterval);
        const double Fraction=Time-LastAttack<AttackInterval?FMath::Fmod(FMath::Max(0.,Phase),1.):.48-Entity.cooldown/AttackInterval;
        const FName Action=AssetId==TEXT("frost_fang")?TEXT("FrostAttack"):AssetId==TEXT("arc_mage")?TEXT("Cast"):
            AssetId==TEXT("ember_archer")||AssetId==TEXT("tower_archer")?TEXT("DrawRelease"):
            AssetId==TEXT("twin_blades")?TEXT("AlternateStrike"):AssetId==TEXT("boulderback")?TEXT("HeavyImpact"):TEXT("Attack");
        SetClip(Action,Fraction,true);return;
    }
    if (Time-LastHit<.16 && Time-LastAttack>.22) {SetClip(TEXT("Hit"),(Time-LastHit)/.16);return;}
    if (Time-AcquireTime<.1 && Travel<.5) {SetClip(TEXT("Acquire"),(Time-AcquireTime)/.1);return;}
    if (Time-TurnTime<.12 && Travel<.5) {SetClip(TEXT("Turn"),(Time-TurnTime)/.12);return;}
    if (AssetId==TEXT("storm_raven") && Entity.auraClock>2.4)
    {SetClip(TEXT("AuraCharge"),(Entity.auraClock-2.4)/.6);return;}
    if (Travel>.05)
    {
        const bool Charging=AssetId==TEXT("rambeast") && Entity.charged;
        SetClip(Charging?TEXT("Charge"):TEXT("Locomotion"),GaitClock+AnimationPhase,true);
    }
    else SetClip(bFlying?TEXT("WingCycle"):TEXT("Idle"),Time*.7+AnimationPhase,true);
}
FVector ARiftUnitVisual::HealthLocation()const
{
    if (Kind==rift::EntityKind::Core) return GetActorLocation()+FVector(0,0,455);
    if (Kind==rift::EntityKind::Guard) return GetActorLocation()+FVector(0,0,305);
    if (Kind==rift::EntityKind::Building) return GetActorLocation()+FVector(0,0,290);
    return Character->DoesSocketExist(TEXT("hp_anchor"))?Character->GetSocketLocation(TEXT("hp_anchor")):
        GetActorLocation()+FVector(0,0,210*Scale);
}
FVector ARiftUnitVisual::AttackLocation()const
{
    if (Kind==rift::EntityKind::Core) return GetActorLocation()+FVector(0,0,388);
    if (Kind==rift::EntityKind::Guard) return GetActorLocation()+GetActorForwardVector()*99+FVector(0,0,263);
    return Character->DoesSocketExist(TEXT("attack_origin"))?Character->GetSocketLocation(TEXT("attack_origin")):
        GetActorLocation()+FVector(0,0,135);
}
FVector ARiftUnitVisual::ImpactLocation()const
{
    if (IsTower()) return GetActorLocation()+FVector(0,0,135);
    return Character->DoesSocketExist(TEXT("impact_origin"))?Character->GetSocketLocation(TEXT("impact_origin")):
        GetActorLocation()+FVector(0,0,85);
}
FVector ARiftUnitVisual::StatusLocation()const
{
    return Character->DoesSocketExist(TEXT("status_anchor"))?Character->GetSocketLocation(TEXT("status_anchor")):HealthLocation();
}
