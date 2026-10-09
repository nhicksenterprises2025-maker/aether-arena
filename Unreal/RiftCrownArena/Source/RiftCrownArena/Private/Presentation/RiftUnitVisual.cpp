#include "Presentation/RiftUnitVisual.h"
#include "RiftDiagnostics.h"
#include "RiftCardData.h"
#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "RiftReplaySubsystem.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSingleNodeInstanceProxy.h"
#include "Components/DecalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameInstance.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
    // Cache local-space transforms, not component-space transforms: blending a
    // whole hierarchy in component space stretches limbs during a transition.
    struct FRiftUnitAnimProxy final : public FAnimSingleNodeInstanceProxy
    {
        explicit FRiftUnitAnimProxy(UAnimInstance* Instance):FAnimSingleNodeInstanceProxy(Instance) {}
        TArray<FTransform> LastPose, TransitionPose;
        double VisualTime=0, TransitionStarted=0;
        float TransitionDuration=0;
        float Alpha() const
        {
            if(TransitionDuration<=0 || TransitionPose.IsEmpty())return 1.f;
            const float T=float(FMath::Clamp((VisualTime-TransitionStarted)/TransitionDuration,0.,1.));
            return T*T*(3.f-2.f*T);
        }
        void Begin(double Time,float Duration,bool Snap)
        {
            VisualTime=Time;TransitionStarted=Time;
            TransitionDuration=Snap?0.f:Duration;
            if(Snap)TransitionPose.Reset();else TransitionPose=LastPose;
        }
        virtual bool Evaluate(FPoseContext& Output) override
        {
            const bool Result=FAnimSingleNodeInstanceProxy::Evaluate(Output);
            const float Weight=Alpha();
            const int32 Count=Output.Pose.GetNumBones();
            if(Weight<1.f && TransitionPose.Num()==Count)
                for(const FCompactPoseBoneIndex Bone:Output.Pose.ForEachBoneIndex())
                {
                    FTransform Blended;
                    Blended.Blend(TransitionPose[Bone.GetInt()],Output.Pose[Bone],Weight);
                    Blended.NormalizeRotation();Output.Pose[Bone]=Blended;
                }
            LastPose.SetNumUninitialized(Count);
            for(const FCompactPoseBoneIndex Bone:Output.Pose.ForEachBoneIndex())LastPose[Bone.GetInt()]=Output.Pose[Bone];
            if(Weight>=1.f)TransitionPose.Reset();
            return Result;
        }
    };
    float ModelReadabilityScale(const FString& Id)
    {
        if(Id==TEXT("ember_archer") || Id==TEXT("arc_mage"))return 1.90f;
        if(Id==TEXT("twin_blades") || Id==TEXT("vampire_bats"))return 1.70f;
        if(Id==TEXT("boulderback"))return 1.65f;
        if(Id==TEXT("rambeast") || Id==TEXT("frost_fang") || Id==TEXT("sky_manta"))return 1.60f;
        if(Id==TEXT("storm_raven"))return 1.55f;
        return 1.80f;
    }
}
FAnimInstanceProxy* URiftUnitAnimInstance::CreateAnimInstanceProxy()
{return new FRiftUnitAnimProxy(this);}
void URiftUnitAnimInstance::SetVisualTime(double Time)
{GetProxyOnGameThread<FRiftUnitAnimProxy>().VisualTime=Time;}
void URiftUnitAnimInstance::BeginVisualTransition(double Time,float Duration,bool Snap)
{GetProxyOnGameThread<FRiftUnitAnimProxy>().Begin(Time,Duration,Snap);}
float URiftUnitAnimInstance::VisualBlendAlpha()const
{return GetProxyOnGameThread<FRiftUnitAnimProxy>().Alpha();}

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
    Cannon=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AuthoredGuardCannon"));Cannon->SetupAttachment(Scene);
    Cannon->SetCollisionEnabled(ECollisionEnabled::NoCollision);Cannon->SetGenerateOverlapEvents(false);
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
            Material->SetScalarParameterValue(TEXT("RiftTeamEmissive"),.12f);
            Materials.Add(Material);
        }
}
bool ARiftUnitVisual::InitializeEntity(const rift::Entity& Entity,URiftCardData* Card,double Time)
{
    EntityId=Entity.id; Kind=Entity.kind; Team=Entity.team; CardData=Card; Born=Entity.born;
    bFlying=Entity.flying; bCoreActive=Entity.active; bDead=false;bHasBeenDamaged=false;
    UpdateDamageHistory(Entity,Time);
    AssetId=UTF8_TO_TCHAR(Entity.cardId.c_str());AnimationPhase=float(Entity.id%17)/17.f;AnimationTime=VisualTime=Time;
    const rift::Card* Definition=rift::FindCard(Entity.cardId);
    Scale=Definition?float(Definition->scale):1.f;
    AttackInterval=Definition?Definition->attackInterval:Kind==rift::EntityKind::Core?.92:1.02;
    if (Kind==rift::EntityKind::Troop)
    {
        if (!Card || !Card->CharacterMesh) {RIFT_LOG(LogRift,Error,TEXT("Missing character for entity %llu"),EntityId);return false;}
        Character->SetSkeletalMeshAsset(Card->CharacterMesh);
        // This multiplier affects only the visual replica. The authoritative
        // radius, tile footprint, range, speed and deck scale remain unchanged.
        Scale*=ModelReadabilityScale(AssetId);
        Character->SetRelativeScale3D(FVector(Scale));Clips=Card->Animations;
        const float Radius=FMath::Max(44.f,float(Entity.radius*100*1.18));
        GroundRing->DecalSize=FVector(32,Radius,Radius);
    }
    else
    {
        const FString MeshId=Kind==rift::EntityKind::Core?TEXT("tower_core"):Kind==rift::EntityKind::Guard?TEXT("tower_guard"):AssetId;
        UStaticMesh* Mesh=Card?Card->StructureMesh.Get():LoadObject<UStaticMesh>(nullptr,*FString::Printf(TEXT("/Game/Rift/Environment/SM_%s.SM_%s"),*MeshId,*MeshId));
        if (!Mesh) {RIFT_LOG(LogRift,Error,TEXT("Missing authored structure %s"),*MeshId);return false;}
        Structure->SetStaticMesh(Mesh);
        StructureHeightScale=Kind==rift::EntityKind::Building?1.22f:1.18f;
        StructureBaseYaw=Team==rift::Team::Player?-90.f:90.f;
        Structure->SetRelativeScale3D(FVector(StructureHeightScale));
        if(Kind==rift::EntityKind::Guard)
        {
            auto* GuardCannon=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Rift/Environment/SM_guard_cannon.SM_guard_cannon"));
            if(!GuardCannon){RIFT_LOG(LogRift,Error,TEXT("Guard cannon assembly missing"));return false;}
            Cannon->SetStaticMesh(GuardCannon);Cannon->SetRelativeScale3D(FVector(StructureHeightScale));
        }
        if (Kind==rift::EntityKind::Building && Definition && Definition->footprint>0)
        {
            const FVector Bounds=Mesh->GetBounds().BoxExtent*2;
            const float PlanScale=Definition->footprint*100/FMath::Max(Bounds.X,Bounds.Y)*1.15f;
            Structure->SetRelativeScale3D(FVector(PlanScale,PlanScale,StructureHeightScale));
        }
        GroundRing->DecalSize=FVector(32,Entity.radius*100,Entity.radius*100);
        if (Kind==rift::EntityKind::Building && AssetId==TEXT("archer_tower"))
        {
            AssetId=TEXT("tower_archer");
            auto* Archer=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Rift/Characters/tower_archer/SK_tower_archer.SK_tower_archer"));
            if (!Archer) {RIFT_LOG(LogRift,Error,TEXT("Archer Tower decoration missing"));return false;}
            Character->SetSkeletalMeshAsset(Archer);ArcherPerchHeight=194*StructureHeightScale;
            Character->SetRelativeLocation(FVector(0,0,ArcherPerchHeight));Character->SetRelativeScale3D(FVector(.60));
        }
    }
    if(Character->GetSkeletalMeshAsset())
    {
        Character->SetAnimInstanceClass(URiftUnitAnimInstance::StaticClass());
        if(auto* Node=Character->GetSingleNodeInstance())Node->bUseMultiThreadedAnimationUpdate=false;
    }
    const FLinearColor Color=TeamColor(Team);ColorMesh(Character,Color);ColorMesh(Structure,Color);ColorMesh(Cannon,Color);
    if (auto* Ring=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Rift/Materials/M_RiftGroundRing.M_RiftGroundRing")))
    {
        auto* Dynamic=UMaterialInstanceDynamic::Create(Ring,this);Dynamic->SetVectorParameterValue(TEXT("RingColor"),Color);
        GroundRing->SetDecalMaterial(Dynamic);Materials.Add(Dynamic);
    }
    SetActorLocation(URiftMatchSubsystem::WorldPoint(Entity.position));PreviousPosition=GetActorLocation();
    SetActorRotation(FRotator(0,FMath::RadiansToDegrees(FMath::Atan2(Entity.facing.z,Entity.facing.x)),0));
    UpdateStructureAim(Time);
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
void ARiftUnitVisual::SetClip(FName Name,double NormalizedPosition,bool Loop,bool Snap)
{
    if (!Character->GetSkeletalMeshAsset()) return;
    UAnimSequence* Animation=Clip(Name);if (!Animation) return;
    auto* Node=Cast<URiftUnitAnimInstance>(Character->GetSingleNodeInstance());
    if(!Node)return;
    Node->SetVisualTime(VisualTime);
    if (CurrentClip!=Name || Snap)
    {
        // Real contact/release events snap to their authored contact pose. All
        // ordinary state changes blend the last evaluated local-space pose.
        const float BlendDuration=Name==TEXT("Death")?.10f:Name==TEXT("Hit")?.045f:.10f;
        Node->BeginVisualTransition(VisualTime,BlendDuration,Snap);
        Node->SetAnimationAsset(Animation,Loop);Node->SetPlaying(false);CurrentClip=Name;
    }
    const double Fraction=Loop?FMath::Fmod(FMath::Max(0.,NormalizedPosition),1.):FMath::Clamp(NormalizedPosition,0.,1.);
    CurrentFraction=float(Fraction);
    Character->SetPosition(float(Fraction*Animation->GetPlayLength()),false);
    Character->TickAnimation(0.f,false);Character->RefreshBoneTransforms();
}
FString ARiftUnitVisual::AnimationAssetPath()const
{
    auto* Node=Character->GetSingleNodeInstance();auto* Asset=Node?Node->GetCurrentAsset():nullptr;
    return Asset?Asset->GetPathName():FString();
}
float ARiftUnitVisual::AnimationPosition()const
{auto* Node=Character->GetSingleNodeInstance();return Node?Node->GetCurrentTime():0.f;}
float ARiftUnitVisual::AnimationBlendAlpha()const
{auto* Node=Cast<URiftUnitAnimInstance>(Character->GetSingleNodeInstance());return Node?Node->VisualBlendAlpha():1.f;}
FName ARiftUnitVisual::AttackClip()const
{
    return AssetId==TEXT("frost_fang")?TEXT("FrostAttack"):AssetId==TEXT("arc_mage")?TEXT("Cast"):
        AssetId==TEXT("ember_archer")||AssetId==TEXT("tower_archer")?TEXT("DrawRelease"):
        AssetId==TEXT("twin_blades")?TEXT("AlternateStrike"):AssetId==TEXT("boulderback")?TEXT("HeavyImpact"):TEXT("Attack");
}
void ARiftUnitVisual::UpdateStructureAim(double Time)
{
    if(!IsTower())return;
    // Stone architecture and its crest retain their team-facing orientation.
    // Only the separate cannon or skeletal Archer Tower decoration aims.
    Structure->SetRelativeRotation(FRotator(0,StructureBaseYaw-GetActorRotation().Yaw,0));
    const double Age=Time-LastAttack;
    CannonRecoil=!bDead && Kind==rift::EntityKind::Guard && Age>0 && Age+1e-9<.18?
        16.f*FMath::Sin(float(Age/.18)*UE_PI):0.f;
    Cannon->SetRelativeLocation(FVector(-CannonRecoil,0,0));
}
void ARiftUnitVisual::AimAt(FVector Target)
{
    if(bDead || !IsTower())return;
    const FVector Direction=Target-GetActorLocation();
    if(Direction.SizeSquared2D()<=1.)return;
    SetActorRotation(FRotator(0,FMath::RadiansToDegrees(FMath::Atan2(Direction.Y,Direction.X)),0));
    UpdateStructureAim(VisualTime);
}
void ARiftUnitVisual::AttackAt(double Time,double Damage)
{
    if (bDead) return;
    LastAttack=Time;VisualTime=Time;
    UpdateStructureAim(Time);
    // Contact/release is already authoritative at this event. Seek to the authored
    // .48 contact pose immediately, then recover; do not delay simulated damage.
    SetClip(AttackClip(),.48,false,true);
}
void ARiftUnitVisual::HitAt(double Time,double Damage)
{
    if(!FMath::IsFinite(Damage) || Damage<=0.)return;
    bHasBeenDamaged=true;
    if(!bDead)LastHit=Time;
}
void ARiftUnitVisual::UpdateDamageHistory(const rift::Entity& Entity,double Time)
{
    if(auto* GI=GetGameInstance())
        if(auto* Replay=GI->GetSubsystem<URiftReplaySubsystem>();Replay && Replay->IsPlaying())
        {
            // Seeking can reuse an actor or start from a fully healed sample.
            // Read its actual first positive recorded hit, never future damage
            // or a presentation latch inherited from a later replay position.
            bHasBeenDamaged=Replay->HasTakenDamageBy(Entity.id,Time);return;
        }
    if(!FMath::IsFinite(Entity.hp) || !FMath::IsFinite(Entity.maxHp) || Entity.maxHp<=0.)return;
    double UndamagedHP=Entity.maxHp,Tolerance=0.;
    if(Entity.kind==rift::EntityKind::Building)
        if(const auto* Definition=rift::FindCard(Entity.cardId);Definition && Definition->lifetime>0.)
        {
            // A defensive building spends HP as its lifetime expires. That
            // passive aging is not its first combat hit and has no damage
            // event; compare the snapshot with its undamaged aging baseline.
            UndamagedHP=FMath::Max(0.,Entity.maxHp-Entity.maxHp/Definition->lifetime*FMath::Max(0.,Time-Entity.born));
            Tolerance=FMath::Max(.000001,Entity.maxHp*.00000001);
        }
    bHasBeenDamaged|=Entity.hp<UndamagedHP-Tolerance;
}
void ARiftUnitVisual::SpecialAt(FName Name,double Time,float Duration)
{if (!bDead) {SpecialClip=Name;SpecialTime=Time;SpecialDuration=FMath::Max(.1f,Duration);}}
void ARiftUnitVisual::AdvanceDeath(double Time)
{
    if(!bDead)return;VisualTime=Time;
    const float T=float(FMath::Clamp((Time-DeathTime)/.65,0.,1.));
    const float Fall=T*T*(3.f-2.f*T);
    if(bFlying)
    {
        FVector Location=GetActorLocation();Location.Z=FMath::Lerp(DeathFlightHeight,8.,double(Fall));SetActorLocation(Location);
    }
    if(ArcherPerchHeight>0)Character->SetRelativeLocation(FVector(0,0,FMath::Lerp(ArcherPerchHeight,12.f,Fall)));
    SetClip(TEXT("Death"),(Time-DeathTime)/.8);
}
void ARiftUnitVisual::DieAt(double Time)
{
    if (bDead) return;bDead=true;DeathTime=Time;VisualTime=Time;DeathFlightHeight=GetActorLocation().Z;GroundRing->SetVisibility(false);
    Cannon->SetVisibility(false);CannonRecoil=0;
    if (Character->GetSkeletalMeshAsset()) SetClip(TEXT("Death"),0);
    if(Kind!=rift::EntityKind::Troop)
        if (auto* Rubble=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Rift/Environment/SM_tower_rubble.SM_tower_rubble")))
        {
            Structure->SetStaticMesh(Rubble);Structure->SetRelativeScale3D(FVector((Kind==rift::EntityKind::Core?1.55f:1.3f)*StructureHeightScale));
            ColorMesh(Structure,TeamColor(Team));
        }
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
    const double Time=State.elapsed;VisualTime=Time;
    UpdateDamageHistory(Entity,Time);
    // Pose clocks, turn smoothing and visual banking must also stop when the
    // simulation pauses, regardless of the renderer's wall-clock delta.
    const double AnimationDelta=FMath::Clamp(Time-AnimationTime,0.,.5);AnimationTime=Time;
    FVector Position=URiftMatchSubsystem::WorldPoint(Entity.position);
    if (bFlying) Position.Z=bDead?GetActorLocation().Z:95.f+FMath::Sin(Time*3.2+AnimationPhase*UE_TWO_PI)*9.f;
    SetActorLocation(Position);
    const float Wanted=FMath::RadiansToDegrees(FMath::Atan2(Entity.facing.z,Entity.facing.x));
    const float Delta=FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,Wanted);
    if (FMath::Abs(Delta)>40 && Time-LastAttack>.3 && Time-TurnTime>.18) TurnTime=Time;
    const float TurnAlpha=1.f-FMath::Exp(-float(AnimationDelta)*11.f);
    SetActorRotation(FRotator(0,GetActorRotation().Yaw+FMath::Clamp(Delta*TurnAlpha,-float(AnimationDelta)*540.f,float(AnimationDelta)*540.f),0));
    UpdateStructureAim(Time);
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
    const bool Charging=AssetId==TEXT("rambeast") && Entity.charged;
    if (!Stunned)
    {
        // One authored gait cycle covers two steps. Tying it to distance keeps
        // feet in cadence with slow effects and charged running, and prevents
        // movement clips from running in place between simulation updates.
        const double Stride=FMath::Max(45.,double(Scale)*(Charging?135.:105.));
        GaitClock+=Travel/Stride;
    }
    FlightBank=FMath::Lerp(FlightBank,bFlying?FMath::Clamp(-Delta*.12f,-9.f,9.f):0.f,1.f-FMath::Exp(-float(AnimationDelta)*8.f));
    Character->SetRelativeRotation(FRotator(Charging?-3.f:0.f,0,FlightBank));
    for (auto Material:Materials)
    {
        Material->SetScalarParameterValue(TEXT("HitFlash"),float(FMath::Clamp(1.-(Time-LastHit)/.18,0.,1.)));
        Material->SetScalarParameterValue(TEXT("SlowAmount"),Slowed?float(Entity.slowPct):0.f);
    }
    if (!Character->GetSkeletalMeshAsset()) return;
    if (Stunned) {SetClip(TEXT("Status"),FMath::Fmod(Time*1.5,1.),true);return;}
    if (Time-Born<.3 && LastAttack<Born) {SetClip(TEXT("Deploy"),(Time-Born)/.3);return;}
    if (Time-SpecialTime<SpecialDuration) {SetClip(SpecialClip,(Time-SpecialTime)/SpecialDuration);return;}
    bool TargetInRange=false;
    if (Entity.target)
        for (const auto& Target:State.entities) if (Target.id==Entity.target && !Target.dead)
        {
            const double Distance=FMath::Sqrt(FMath::Square(Target.position.x-Entity.position.x)+FMath::Square(Target.position.z-Entity.position.z));
            const auto* Definition=rift::FindCard(Entity.cardId);
            TargetInRange=Definition && Distance-Target.radius<=Definition->range+(Kind==rift::EntityKind::Building?.35:.0)+.1;
            break;
        }
    const double RecoveryDuration=FMath::Clamp(AttackInterval*.32,.18,.50);
    const double WindupDuration=FMath::Clamp(AttackInterval*.32,.18,.42);
    const double SinceAttack=Time-LastAttack;
    if(SinceAttack>=0 && SinceAttack<RecoveryDuration)
    {
        // A release cannot wrap to the windup within the same strike. This
        // bounded second half ends in the authored neutral recovery pose.
        SetClip(AttackClip(),.48+.52*SinceAttack/RecoveryDuration);return;
    }
    if(TargetInRange && Travel<.5 && Entity.cooldown>0 && Entity.cooldown<=WindupDuration)
    {SetClip(AttackClip(),.48*(1.-Entity.cooldown/WindupDuration));return;}
    if (Time-LastHit<.16 && Time-LastAttack>.22) {SetClip(TEXT("Hit"),(Time-LastHit)/.16);return;}
    if (Time-AcquireTime<.1 && Travel<.5) {SetClip(TEXT("Acquire"),(Time-AcquireTime)/.1);return;}
    if (Time-TurnTime<.12 && Travel<.5) {SetClip(TEXT("Turn"),(Time-TurnTime)/.12);return;}
    if (AssetId==TEXT("storm_raven") && Entity.auraClock>2.4)
    {SetClip(TEXT("AuraCharge"),(Entity.auraClock-2.4)/.6);return;}
    if (Travel>.05)
    {
        SetClip(Charging?TEXT("Charge"):TEXT("Locomotion"),GaitClock+AnimationPhase,true);
    }
    else SetClip(bFlying?TEXT("WingCycle"):AssetId==TEXT("frost_fang")?TEXT("Breath"):TEXT("Idle"),Time*.7+AnimationPhase,true);
}
FVector ARiftUnitVisual::HealthLocation()const
{
    FVector Anchor=GetActorLocation()+FVector(0,0,
        Kind==rift::EntityKind::Core?455*StructureHeightScale:
        Kind==rift::EntityKind::Guard?305*StructureHeightScale:
        Kind==rift::EntityKind::Building?298*StructureHeightScale:210*Scale);
    if(Character->DoesSocketExist(TEXT("hp_anchor")))Anchor=Character->GetSocketLocation(TEXT("hp_anchor"));
    double ModelTop=GetActorLocation().Z;
    if(auto* Mesh=Character->GetSkeletalMeshAsset())
    {
        // Include both current animated bounds and the complete imported mesh.
        // Bone anchors can sit below tall staffs, crystal ridges or horns.
        ModelTop=FMath::Max(ModelTop,Character->Bounds.GetBox().Max.Z);
        ModelTop=FMath::Max(ModelTop,Mesh->GetImportedBounds().GetBox().TransformBy(Character->GetComponentTransform()).Max.Z);
    }
    for(const auto* Component:{Structure.Get(),Cannon.Get()})
        if(Component->IsVisible())
            if(auto* Mesh=Component->GetStaticMesh().Get())
                ModelTop=FMath::Max(ModelTop,Mesh->GetBounds().GetBox().TransformBy(Component->GetComponentTransform()).Max.Z);
    Anchor.Z=FMath::Max(Anchor.Z,ModelTop+30.);
    return Anchor;
}
FVector ARiftUnitVisual::AttackLocation()const
{
    if (Kind==rift::EntityKind::Core) return GetActorLocation()+FVector(0,0,388*StructureHeightScale);
    if (Kind==rift::EntityKind::Guard) return Cannon->GetComponentTransform().TransformPosition(FVector(99,0,263));
    return Character->DoesSocketExist(TEXT("attack_origin"))?Character->GetSocketLocation(TEXT("attack_origin")):
        GetActorLocation()+FVector(0,0,135*Scale);
}
FVector ARiftUnitVisual::ImpactLocation()const
{
    if (IsTower()) return GetActorLocation()+FVector(0,0,135*StructureHeightScale);
    return Character->DoesSocketExist(TEXT("impact_origin"))?Character->GetSocketLocation(TEXT("impact_origin")):
        GetActorLocation()+FVector(0,0,85*Scale);
}
FVector ARiftUnitVisual::StatusLocation()const
{
    const FVector Health=HealthLocation();
    FVector Anchor=Character->DoesSocketExist(TEXT("status_anchor"))?Character->GetSocketLocation(TEXT("status_anchor")):Health;
    Anchor.Z=FMath::Max(Anchor.Z,Health.Z+16.);
    return Anchor;
}
