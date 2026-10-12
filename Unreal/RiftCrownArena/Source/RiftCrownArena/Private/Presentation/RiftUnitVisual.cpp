#include "Presentation/RiftUnitVisual.h"
#include "Presentation/RiftArenaGeometry.h"
#include "RiftDiagnostics.h"
#include "RiftCardData.h"
#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "RiftReplaySubsystem.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSingleNodeInstanceProxy.h"
#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "RiftAssetLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#endif

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
        if(Id==TEXT("mini_stampede"))return 1.60f;
        if(Id==TEXT("stampede"))return 1.50f;
        return 1.80f;
    }
    double AuthoredGaitStrideCm(const FString& Id,bool Charging)
    {
        if(Id==TEXT("mini_stampede") || Id==TEXT("stampede"))return 78.;
        if(Id==TEXT("boulderback") || Id==TEXT("rambeast") || Id==TEXT("frost_fang"))return Charging?110.:90.;
        return 105.;
    }
    bool IsBoar(const FString& Id)
    {return Id==TEXT("mini_stampede") || Id==TEXT("stampede");}
    constexpr int32 HoofDustLimit=4,FormationDustLimit=4,SlowRippleSegments=24;
    constexpr int32 ReaperAuraStrands=3,ReaperAuraSegments=10,ReaperAuraWispLimit=4,PulverizerPulseSegments=32;
    constexpr double HoofDustLifetime=.22,FormationDustLifetime=.28,SlowRippleLifetime=.28;
    FVector SurfaceMeshScale(UStaticMesh* Mesh,FVector Size)
    {
        const FVector Bounds=Mesh->GetBounds().BoxExtent*2.;
        return FVector(Size.X/FMath::Max(1.,Bounds.X),Size.Y/FMath::Max(1.,Bounds.Y),Size.Z/FMath::Max(1.,Bounds.Z));
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
    // Team and spell footprints belong on the floor, clear of character surfaces.
    Character->SetReceivesDecals(false);
    Character->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Character->SetComponentTickEnabled(false);Character->bEnableUpdateRateOptimizations=false;
    Character->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Structure=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AuthoredStructure"));Structure->SetupAttachment(Scene);
    Structure->SetCollisionEnabled(ECollisionEnabled::NoCollision);Structure->SetGenerateOverlapEvents(false);
    Cannon=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AuthoredGuardCannon"));Cannon->SetupAttachment(Scene);
    Cannon->SetCollisionEnabled(ECollisionEnabled::NoCollision);Cannon->SetGenerateOverlapEvents(false);
    GroundRing=CreateDefaultSubobject<UDecalComponent>(TEXT("TeamGroundRing"));GroundRing->SetupAttachment(Scene);
    GroundRing->SetRelativeRotation(FRotator(-90,0,0));GroundRing->SetRelativeLocation(FVector(0,0,6*RiftArenaGeometry::WorldScale));
    GroundRing->DecalSize=FVector(32,52,52)*RiftArenaGeometry::WorldScale;GroundRing->SortOrder=1;
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
void ARiftUnitVisual::SetFormationDustLeader(bool Leader,FVector Center)
{bFormationDustLeader=Leader;FormationCenter=Center;}
int32 ARiftUnitVisual::HoofDustCount()const{return HoofDust?HoofDust->GetInstanceCount():0;}
int32 ARiftUnitVisual::FormationDustCount()const{return FormationDust?FormationDust->GetInstanceCount():0;}
int32 ARiftUnitVisual::DeathFleckCount()const
{return (CopperFlecks?CopperFlecks->GetInstanceCount():0)+(GrayFlecks?GrayFlecks->GetInstanceCount():0);}
int32 ARiftUnitVisual::SlowRippleCount()const{return SlowRipple?SlowRipple->GetInstanceCount():0;}
int32 ARiftUnitVisual::ReaperAuraCount()const
{return (ReaperAuraRibbons?ReaperAuraRibbons->GetInstanceCount():0)+(ReaperAuraWisps?ReaperAuraWisps->GetInstanceCount():0);}
int32 ARiftUnitVisual::PulverizerPulseCount()const{return PulverizerPulse?PulverizerPulse->GetInstanceCount():0;}
int32 ARiftUnitVisual::ShieldFragmentCount()const{return ShieldFragments?ShieldFragments->GetInstanceCount():0;}
int32 ARiftUnitVisual::ShieldEdgeCount()const{return ShieldEdges?ShieldEdges->GetInstanceCount():0;}
void ARiftUnitVisual::ShieldHitAt(double Time,double Damage)
{if(Damage>0 && FMath::IsFinite(Damage)){bHasBeenDamaged=true;LastShieldHit=Time;}}
void ARiftUnitVisual::UpdateCloak(const rift::Entity& Entity,double Time)
{
    const bool Cloaked=!Entity.dead && Entity.invisibleUntil>Time;
    const bool Hidden=Cloaked && Team==rift::Team::Enemy;
    if(Cloaked && !Hidden && !bCloaked && Character->GetSkeletalMeshAsset())
    {
        OriginalCharacterMaterials.Reset();CloakMaterials.Reset();
        auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Rift/Materials/M_RiftCloak.M_RiftCloak"));
        if(!Base)RIFT_LOG(LogRift,Error,TEXT("Shadow Surge cloak material is missing"));
        for(int32 I=0;I<Character->GetNumMaterials();++I)
        {
            OriginalCharacterMaterials.Add(Character->GetMaterial(I));
            if(Base)
            {
                auto* Material=UMaterialInstanceDynamic::Create(Base,this);
                Material->SetScalarParameterValue(TEXT("CloakAmount"),.18f);
                CloakMaterials.Add(Material);Character->SetMaterial(I,Material);
            }
        }
    }
    if(!Cloaked && bCloaked)
    {
        for(int32 I=0;I<OriginalCharacterMaterials.Num();++I)Character->SetMaterial(I,OriginalCharacterMaterials[I]);
        OriginalCharacterMaterials.Reset();CloakMaterials.Reset();
    }
    bCloaked=Cloaked;bHiddenFromViewer=Hidden;
    Character->SetVisibility(!Hidden);Character->SetCastShadow(!Hidden);
    GroundRing->SetVisibility(!Hidden && !Entity.dead);
    for(auto* Batch:{HoofDust.Get(),FormationDust.Get(),CopperFlecks.Get(),GrayFlecks.Get(),SlowRipple.Get(),ShieldFragments.Get(),ShieldEdges.Get(),ReaperAuraRibbons.Get(),ReaperAuraWisps.Get(),PulverizerPulse.Get()})
        if(Batch)Batch->SetVisibility(!Hidden);
}
void ARiftUnitVisual::UpdateShield(const rift::Entity& Entity,double Time)
{
    if(AssetId!=TEXT("iron_giant") || !Character->GetSkeletalMeshAsset())return;
    const bool Broken=Entity.maxShieldHP>0 && Entity.shieldHP<=0;
    // Shield feedback follows its six real attachment bones, independently of
    // body-hit materials. A paused frame retains the same bounded edge flash.
    const double FlashAge=Time-LastShieldHit;
    if(!Broken && !Entity.dead && FlashAge>=0 && FlashAge<.12)
    {
        if(!ShieldEdges)
        {
            ShieldEdges=MakeSurfaceBatch(TEXT("SixShieldEdgeFlashes"),FLinearColor(.62f,.78f,.88f),.12f);
            if(ShieldEdges)if(auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Rift/Materials/M_RiftExpansionFX.M_RiftExpansionFX")))
                for(int32 I=0;I<ShieldEdges->GetNumMaterials();++I)
                {ShieldEdges->SetMaterial(I,Base);if(auto* MID=ShieldEdges->CreateDynamicMaterialInstance(I))MID->SetVectorParameterValue(TEXT("EffectColor"),FLinearColor(.62f,.78f,.88f));}
        }
        if(ShieldEdges)
        {
            TArray<FTransform> Edges;Edges.Reserve(24);
            const FVector Forward=GetActorForwardVector(),Right=GetActorRightVector(),Up=FVector::UpVector;
            const double Thickness=.012*RiftArenaGeometry::UnitsPerTile*(1.-FlashAge/.12);
            for(int32 I=0;I<6;++I)
            {
                const bool Side=I==2 || I==3;
                const FVector Across=Side?Forward:Right;
                const FVector Out=Side?Right*(I==2?-1.:1.):Forward*(I<2?1.:-1.);
                const FVector Center=Character->GetSocketLocation(FName(*FString::Printf(TEXT("shield_panel_%d"),I)))+Out*(Side?.105:.045)*100.*Scale;
                const double HalfWidth=(Side?.19:.215)*100.*Scale,HalfHeight=(Side?.145:.175)*100.*Scale;
                const FVector Corners[4]={Center-Across*HalfWidth-Up*HalfHeight,Center+Across*HalfWidth-Up*HalfHeight,
                    Center+Across*HalfWidth+Up*HalfHeight,Center-Across*HalfWidth+Up*HalfHeight};
                for(int32 J=0;J<4;++J)
                {const FVector A=Corners[J],B=Corners[(J+1)%4];Edges.Add(FTransform(FRotationMatrix::MakeFromX(B-A).ToQuat(),(A+B)*.5,
                    SurfaceMeshScale(ShieldEdges->GetStaticMesh(),FVector((B-A).Size(),Thickness,Thickness))));}
            }
            ShieldEdges->ClearInstances();ShieldEdges->AddInstances(Edges,false,true,false);ShieldEdges->SetVisibility(!bHiddenFromViewer);
        }
    }
    else if(ShieldEdges)ShieldEdges->ClearInstances();
    if(Broken!=bShieldBroken)
    {
        for(int32 I=0;I<6;++I)
        {
            const FName Bone(*FString::Printf(TEXT("shield_panel_%d"),I));
            if(Broken)Character->HideBoneByName(Bone,EPhysBodyOp::PBO_None);else Character->UnHideBoneByName(Bone);
        }
        bShieldBroken=Broken;
    }
    const double Age=Time-Entity.shieldBrokenAt;
    if(!Broken || Entity.shieldBrokenAt<0 || Age<0 || Age>=.5 || Entity.dead)
    {if(ShieldFragments)ShieldFragments->ClearInstances();return;}
    if(!ShieldFragments)
    {
        ShieldFragments=MakeSurfaceBatch(TEXT("SixShieldShellFragments"),FLinearColor(.48f,.56f,.64f),.12f);
        if(auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Rift/Environment/SM_iron_shield_fragment.SM_iron_shield_fragment")))
            if(ShieldFragments)ShieldFragments->SetStaticMesh(Mesh);
    }
    if(!ShieldFragments)return;
    const double T=Age/.5;
    TArray<FTransform> Pieces;Pieces.Reserve(6);
    for(int32 I=0;I<6;++I)
    {
        const double Angle=I*UE_TWO_PI/6.+.2;
        const double Radius=(.15+.40*(1.-FMath::Square(1.-T)))*RiftArenaGeometry::UnitsPerTile;
        const FVector Position=GetActorLocation()+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,(.72+.23*FMath::Sin(T*UE_PI)-.62*T)*RiftArenaGeometry::UnitsPerTile);
        Pieces.Add(FTransform(FRotator(float(T*120),FMath::RadiansToDegrees(Angle)+float(T*70),float(T*100)),Position,
            SurfaceMeshScale(ShieldFragments->GetStaticMesh(),FVector(.26,.04,.34)*RiftArenaGeometry::UnitsPerTile)));
    }
    ShieldFragments->ClearInstances();ShieldFragments->AddInstances(Pieces,false,true,false);
    for(int32 I=0;I<ShieldFragments->GetNumMaterials();++I)if(auto* Material=Cast<UMaterialInstanceDynamic>(ShieldFragments->GetMaterial(I)))
        Material->SetScalarParameterValue(TEXT("DissolveAmount"),float(T));
    ShieldFragments->SetVisibility(!bHiddenFromViewer);
}
UInstancedStaticMeshComponent* ARiftUnitVisual::MakeSurfaceBatch(FName Name,FLinearColor Color,float Emissive)
{
    auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Rift/Environment/SM_projectile_trail.SM_projectile_trail"));
    auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Rift/Materials/M_RiftExpansionSurface.M_RiftExpansionSurface"));
    if(!Mesh || !Base){RIFT_LOG(LogRift,Error,TEXT("Selected surface effect is missing its authored mesh/material"));return nullptr;}
    auto* Batch=NewObject<UInstancedStaticMeshComponent>(this,Name);Batch->SetupAttachment(Scene);
    Batch->SetMobility(EComponentMobility::Movable);Batch->SetAbsolute(true,true,true);Batch->SetWorldTransform(FTransform::Identity);
    Batch->SetStaticMesh(Mesh);Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);Batch->SetGenerateOverlapEvents(false);
    Batch->SetCanEverAffectNavigation(false);
    Batch->SetCastShadow(false);Batch->bReceivesDecals=false;Batch->SetComponentTickEnabled(false);
    for(int32 I=0;I<Batch->GetNumMaterials();++I)
    {
        Batch->SetMaterial(I,Base);
        if(auto* Material=Batch->CreateDynamicMaterialInstance(I))
        {Material->SetVectorParameterValue(TEXT("TeamColor"),Color);Material->SetScalarParameterValue(TEXT("RiftTeamEmissive"),Emissive);}
    }
    Batch->RegisterComponent();AddInstanceComponent(Batch);return Batch;
}
UInstancedStaticMeshComponent* ARiftUnitVisual::MakeAuraBatch(FName Name,FName MeshId,FLinearColor Color)
{
    auto* Mesh=LoadObject<UStaticMesh>(nullptr,*FString::Printf(TEXT("/Game/Rift/Environment/SM_%s.SM_%s"),*MeshId.ToString(),*MeshId.ToString()));
    auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Rift/Materials/M_RiftExpansionFX.M_RiftExpansionFX"));
    if(!Mesh || !Base){RIFT_LOG(LogRift,Error,TEXT("Purple aura is missing its authored mesh/material"));return nullptr;}
    auto* Batch=NewObject<UInstancedStaticMeshComponent>(this,Name);Batch->SetupAttachment(Scene);
    Batch->SetMobility(EComponentMobility::Movable);Batch->SetAbsolute(true,true,true);Batch->SetWorldTransform(FTransform::Identity);
    Batch->SetStaticMesh(Mesh);Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);Batch->SetGenerateOverlapEvents(false);
    Batch->SetCanEverAffectNavigation(false);Batch->SetCastShadow(false);Batch->bReceivesDecals=false;Batch->SetComponentTickEnabled(false);
    for(int32 I=0;I<Batch->GetNumMaterials();++I)
    {
        Batch->SetMaterial(I,Base);
        if(auto* Material=Batch->CreateDynamicMaterialInstance(I))
        {
            Material->SetVectorParameterValue(TEXT("EffectColor"),Color);
            Material->SetScalarParameterValue(TEXT("EffectOpacity"),.62f);
            Material->SetScalarParameterValue(TEXT("EffectEmission"),.45f);
        }
    }
    Batch->RegisterComponent();AddInstanceComponent(Batch);return Batch;
}
void ARiftUnitVisual::UpdateReaperAura(double Time,bool Moving)
{
    if(AssetId!=TEXT("aether_reaper"))return;
    if(bDead || bHiddenFromViewer)
    {if(ReaperAuraRibbons)ReaperAuraRibbons->ClearInstances();if(ReaperAuraWisps)ReaperAuraWisps->ClearInstances();return;}
    const FLinearColor Violet(.82f,.16f,1.f);
    if(!ReaperAuraRibbons)ReaperAuraRibbons=MakeAuraBatch(TEXT("ReaperVioletAuraRibbons"),TEXT("projectile_trail"),Violet);
    if(!ReaperAuraWisps)ReaperAuraWisps=MakeAuraBatch(TEXT("ReaperVioletAuraWisps"),TEXT("shadow_surge_wisp"),Violet);
    if(!ReaperAuraRibbons || !ReaperAuraWisps)return;
    // The open spiral surrounds the lower silhouette, leaving the hood, hands
    // and scythe readable. A fixed number of ribbons/wisps works on LOW too;
    // nothing depends on Niagara scalability, bloom or wall-clock material Time.
    const double Clock=FMath::Max(0.,Time-Born),Phase=Clock*1.7+AnimationPhase*UE_TWO_PI;
    const double Breath=.94+.06*FMath::Sin(Clock*2.8+AnimationPhase*UE_TWO_PI);
    const double Height=190.*Scale,Radius=44.*Scale*Breath;
    const FVector Center=GetActorLocation(),Behind=-GetActorForwardVector()*(Moving?18.*Scale:0.);
    TArray<FTransform> Ribbons,Wisps;Ribbons.Reserve(ReaperAuraStrands*ReaperAuraSegments);Wisps.Reserve(ReaperAuraWispLimit);
    for(int32 Strand=0;Strand<ReaperAuraStrands;++Strand)
    {
        auto Path=[&](double T)
        {
            const double Angle=Phase+Strand*UE_TWO_PI/ReaperAuraStrands+T*UE_PI*.95;
            return Center+Behind*(1.-T)+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,Height*(.12+.48*T));
        };
        for(int32 I=0;I<ReaperAuraSegments;++I)
        {
            const FVector A=Path(double(I)/ReaperAuraSegments),B=Path(double(I+1)/ReaperAuraSegments);
            Ribbons.Add(FTransform(FRotationMatrix::MakeFromX(B-A).ToQuat(),(A+B)*.5,
                SurfaceMeshScale(ReaperAuraRibbons->GetStaticMesh(),FVector((B-A).Length(),5.0*Scale,3.0*Scale))));
        }
    }
    for(int32 I=0;I<ReaperAuraWispLimit;++I)
    {
        const double T=FMath::Fmod(Clock*.43+double(I)/ReaperAuraWispLimit,1.);
        const double Angle=Phase*.7+I*UE_TWO_PI/ReaperAuraWispLimit;
        const double Envelope=FMath::Sin(T*UE_PI);
        const FVector Position=Center+Behind+FVector(FMath::Cos(Angle)*Radius*1.08,FMath::Sin(Angle)*Radius*1.08,Height*(.08+.65*T));
        const FVector Direction(-FMath::Sin(Angle)*.35,FMath::Cos(Angle)*.35,1.);
        Wisps.Add(FTransform(FRotationMatrix::MakeFromZ(Direction).ToQuat(),Position,
            SurfaceMeshScale(ReaperAuraWisps->GetStaticMesh(),FVector(9.,6.,22.)*Scale*(.35+.65*Envelope))));
    }
    ReaperAuraRibbons->ClearInstances();ReaperAuraRibbons->AddInstances(Ribbons,false,true,false);
    ReaperAuraWisps->ClearInstances();ReaperAuraWisps->AddInstances(Wisps,false,true,false);
    for(auto* Batch:{ReaperAuraRibbons.Get(),ReaperAuraWisps.Get()})
    {
        Batch->SetVisibility(!bHiddenFromViewer);
        for(int32 I=0;I<Batch->GetNumMaterials();++I)if(auto* Material=Cast<UMaterialInstanceDynamic>(Batch->GetMaterial(I)))
            Material->SetScalarParameterValue(TEXT("EffectOpacity"),bCloaked?.18f:float(.53+.09*Breath));
    }
}
void ARiftUnitVisual::UpdatePulverizerPulse(double Time,double ReleasedAt)
{
    if(AssetId!=TEXT("pulverizer"))return;
    const double Age=Time-ReleasedAt;
    if(bDead || bHiddenFromViewer || ReleasedAt<Born || Age<0 || Age>=.28)
    {if(PulverizerPulse)PulverizerPulse->ClearInstances();return;}
    if(!PulverizerPulse)PulverizerPulse=MakeAuraBatch(TEXT("PulverizerVioletReleaseHalo"),TEXT("projectile_trail"),FLinearColor(.86f,.15f,1.f));
    if(!PulverizerPulse)return;
    // This elevated short pulse makes the actual release visible above the
    // building's base. The arena owns the separate traveling damage front.
    const double T=Age/.28,Radius=(.46+.35*T)*RiftArenaGeometry::UnitsPerTile;
    const FVector Center=GetActorLocation()+FVector(0,0,112.*StructureHeightScale);
    TArray<FTransform> Instances;Instances.Reserve(PulverizerPulseSegments);
    for(int32 I=0;I<PulverizerPulseSegments;++I)
    {
        const double A=I*UE_TWO_PI/PulverizerPulseSegments,B=(I+1)*UE_TWO_PI/PulverizerPulseSegments;
        const FVector Start=Center+FVector(FMath::Cos(A)*Radius,FMath::Sin(A)*Radius,0),End=Center+FVector(FMath::Cos(B)*Radius,FMath::Sin(B)*Radius,0);
        const double Width=(.055*(1.-T)+.012)*RiftArenaGeometry::UnitsPerTile;
        Instances.Add(FTransform(FRotationMatrix::MakeFromX(End-Start).ToQuat(),(Start+End)*.5,
            SurfaceMeshScale(PulverizerPulse->GetStaticMesh(),FVector((End-Start).Length(),Width,Width))));
    }
    PulverizerPulse->ClearInstances();PulverizerPulse->AddInstances(Instances,false,true,false);PulverizerPulse->SetVisibility(!bHiddenFromViewer);
    for(int32 I=0;I<PulverizerPulse->GetNumMaterials();++I)if(auto* Material=Cast<UMaterialInstanceDynamic>(PulverizerPulse->GetMaterial(I)))
        Material->SetScalarParameterValue(TEXT("EffectOpacity"),float(.62*(1.-T)));
}
void ARiftUnitVisual::EmitDeploySurfaceDust(double Time)
{
    if(!IsBoar(AssetId) || Time-Born>=FormationDustLifetime)return;
    const bool Formation=AssetId==TEXT("stampede");
    if(Formation && !bFormationDustLeader)return;
    const int32 Count=Formation?FormationDustLimit:2;
    for(int32 I=0;I<Count;++I)
    {
        FSurfaceMote M;M.Born=Born;M.Lifetime=Formation?FormationDustLifetime:HoofDustLifetime;M.Formation=Formation;
        const double Angle=I*UE_TWO_PI/Count;
        M.Origin=Formation?FormationCenter:GetActorLocation();M.Origin+=FVector(FMath::Cos(Angle)*12,FMath::Sin(Angle)*12,4)*RiftArenaGeometry::WorldScale;
        M.Origin.Z=4*RiftArenaGeometry::WorldScale;M.Velocity=FVector(FMath::Cos(Angle)*18,FMath::Sin(Angle)*18,8)*RiftArenaGeometry::WorldScale;
        M.Size=(Formation?FVector(12,6,2):FVector(6,3,2))*RiftArenaGeometry::WorldScale;M.Yaw=float(I*71);SurfaceMotes.Add(M);
    }
    UpdateSurfaceMotes(Time);
}
void ARiftUnitVisual::EmitHoofDust(double Time,double PreviousPhase,double CurrentPhase)
{
    if(!IsBoar(AssetId) || bDead || CurrentPhase<=PreviousPhase ||
        FMath::FloorToDouble(PreviousPhase*2)==FMath::FloorToDouble(CurrentPhase*2))return;
    const bool Stampede=AssetId==TEXT("stampede");
    // Three selected members of a fifteen-member herd leave sparse contacts;
    // its single elected leader owns the formation emitter, never fifteen clouds.
    if(!Stampede || EntityId%5==0)
    {
        const FName Contacts[]={TEXT("front_L_contact"),TEXT("back_R_contact"),TEXT("front_R_contact"),TEXT("back_L_contact")};
        const int32 Contact=int32(FMath::Fmod(FMath::FloorToDouble(CurrentPhase*2),4.));
        FSurfaceMote M;M.Born=Time;M.Lifetime=HoofDustLifetime;M.Yaw=float(EntityId*37%180);
        M.Origin=Character->DoesSocketExist(Contacts[Contact])?Character->GetSocketLocation(Contacts[Contact]):GetActorLocation();M.Origin.Z=3*RiftArenaGeometry::WorldScale;
        M.Velocity=(-GetActorForwardVector()*14+FVector(0,0,9))*RiftArenaGeometry::WorldScale;M.Size=FVector(5,3,2)*RiftArenaGeometry::WorldScale;SurfaceMotes.Add(M);
    }
    if(Stampede && bFormationDustLeader)
    {
        FSurfaceMote M;M.Born=Time;M.Lifetime=FormationDustLifetime;M.Formation=true;
        M.Origin=FormationCenter;M.Origin.Z=4*RiftArenaGeometry::WorldScale;M.Velocity=(-GetActorForwardVector()*12+FVector(0,0,7))*RiftArenaGeometry::WorldScale;
        M.Size=FVector(12,6,2)*RiftArenaGeometry::WorldScale;M.Yaw=float(EntityId*19%180);SurfaceMotes.Add(M);
    }
    UpdateSurfaceMotes(Time);
}
void ARiftUnitVisual::UpdateSurfaceMotes(double Time)
{
    SurfaceMotes.RemoveAll([&](const FSurfaceMote& M){return Time<M.Born || Time-M.Born>=M.Lifetime || (M.Formation&&!bFormationDustLeader);});
    // A developer/replay position mutation can move a paused entity without
    // advancing its clock. Bound CPU records as well as draw instances, keeping
    // the newest contacts so repeated same-time mutations cannot accumulate.
    int32 HoofRecords=0,FormationRecords=0;
    for(int32 I=SurfaceMotes.Num()-1;I>=0;--I)
    {
        int32& Records=SurfaceMotes[I].Formation?FormationRecords:HoofRecords;
        const int32 Limit=SurfaceMotes[I].Formation?FormationDustLimit:HoofDustLimit;
        if(Records>=Limit)SurfaceMotes.RemoveAt(I,1,EAllowShrinking::No);else ++Records;
    }
    check(SurfaceMotes.Num()<=HoofDustLimit+FormationDustLimit);
    TArray<FTransform> Hoof,Formation;
    for(const auto& M:SurfaceMotes)
    {
        auto& Instances=M.Formation?Formation:Hoof;const int32 Limit=M.Formation?FormationDustLimit:HoofDustLimit;
        if(Instances.Num()>=Limit)continue;
        auto& Batch=M.Formation?FormationDust:HoofDust;
        if(!Batch)Batch=MakeSurfaceBatch(M.Formation?TEXT("BoundedFormationDust"):TEXT("BoundedHoofDust"),FLinearColor(.32f,.29f,.25f),0.f);
        if(!Batch)continue;
        const double Age=Time-M.Born,T=Age/M.Lifetime;
        const FVector Position=M.Origin+M.Velocity*Age;
        const FVector Size=M.Size*(1.-T)*(.85+.35*T);
        Instances.Add(FTransform(FRotator(0,M.Yaw,0),Position,SurfaceMeshScale(Batch->GetStaticMesh(),Size)));
    }
    auto SetDust=[&](UInstancedStaticMeshComponent* Batch,const TArray<FTransform>& Instances)
    {
        if(!Batch)return;Batch->ClearInstances();Batch->AddInstances(Instances,false,true,false);
        for(int32 I=0;I<Batch->GetNumMaterials();++I)
            if(auto* Material=Cast<UMaterialInstanceDynamic>(Batch->GetMaterial(I)))Material->SetScalarParameterValue(TEXT("DissolveAmount"),.76f);
    };
    SetDust(HoofDust,Hoof);SetDust(FormationDust,Formation);
    if(!bDead || !IsBoar(AssetId))return;
    const double Age=Time-DeathTime;
    if(Age<.06 || Age>=CleanupSeconds)
    {if(CopperFlecks)CopperFlecks->ClearInstances();if(GrayFlecks)GrayFlecks->ClearInstances();return;}
    if(!CopperFlecks)CopperFlecks=MakeSurfaceBatch(TEXT("CopperDeathFlecks"),FLinearColor(.55f,.28f,.10f),.03f);
    if(!GrayFlecks)GrayFlecks=MakeSurfaceBatch(TEXT("GrayDeathFlecks"),FLinearColor(.22f,.27f,.29f),0.f);
    const double T=(Age-.06)/FMath::Max(.1,CleanupSeconds-.06);
    TArray<FTransform> Copper,Gray;
    for(int32 I=0;I<3;++I)
    {
        auto* Batch=I<2?CopperFlecks.Get():GrayFlecks.Get();if(!Batch)continue;
        const double Angle=(I*2.1+double(EntityId%13)*.2);
        FVector Local(I<2?42.*Scale:-8.*Scale,(I-1)*10.*Scale,20.*Scale);
        FVector Position=GetActorTransform().TransformPosition(Local)+FVector(FMath::Cos(Angle)*30*Age,FMath::Sin(Angle)*30*Age,45*Age-110*Age*Age)*RiftArenaGeometry::WorldScale;
        Position.Z=FMath::Max(3.*RiftArenaGeometry::WorldScale,Position.Z);
        const FVector Size=FVector(4.5,2.5,1.8)*(1.-.55*T)*RiftArenaGeometry::WorldScale;
        (I<2?Copper:Gray).Add(FTransform(FRotator(float(Age*100),float(I*83+Age*180),float(I*47)),Position,SurfaceMeshScale(Batch->GetStaticMesh(),Size)));
    }
    for(auto* Batch:{CopperFlecks.Get(),GrayFlecks.Get()})if(Batch)
    {
        Batch->ClearInstances();Batch->AddInstances(Batch==CopperFlecks.Get()?Copper:Gray,false,true,false);
        for(int32 I=0;I<Batch->GetNumMaterials();++I)
            if(auto* Material=Cast<UMaterialInstanceDynamic>(Batch->GetMaterial(I)))Material->SetScalarParameterValue(TEXT("DissolveAmount"),float(T));
    }
}
void ARiftUnitVisual::UpdatePulverizerSlow(const rift::Entity& Entity,double Time)
{
    double Latest=-100;
    if(!Entity.dead)for(const auto& Slow:Entity.slows)
        if(Slow.cardId=="pulverizer" && Slow.until>Time && Slow.pct>0)Latest=FMath::Max(Latest,Slow.born);
    const double Age=Time-Latest;
    if(Latest<0 || Age<0 || Age>=SlowRippleLifetime){if(SlowRipple)SlowRipple->ClearInstances();return;}
    if(!SlowRipple)SlowRipple=MakeSurfaceBatch(TEXT("PulverizerAnkleShackle"),FLinearColor(.54f,.09f,.85f),.8f);
    if(!SlowRipple)return;
    const double AnchorRadius=FMath::Clamp(Entity.radius*RiftArenaGeometry::UnitsPerTile+4.*RiftArenaGeometry::WorldScale,16.*RiftArenaGeometry::WorldScale,75.*RiftArenaGeometry::WorldScale);
    const double T=Age/SlowRippleLifetime,Radius=AnchorRadius*(.82+.23*T);
    TArray<FTransform> Instances;
    for(int32 I=0;I<SlowRippleSegments;++I)
    {
        const double A=I*UE_TWO_PI/SlowRippleSegments,B=(I+1)*UE_TWO_PI/SlowRippleSegments;
        const FVector Start=GetActorLocation()+FVector(FMath::Cos(A)*Radius,FMath::Sin(A)*Radius,8*RiftArenaGeometry::WorldScale);
        const FVector End=GetActorLocation()+FVector(FMath::Cos(B)*Radius,FMath::Sin(B)*Radius,8*RiftArenaGeometry::WorldScale);
        Instances.Add(FTransform(FRotationMatrix::MakeFromX(End-Start).ToQuat(),(Start+End)*.5,
            SurfaceMeshScale(SlowRipple->GetStaticMesh(),FVector((End-Start).Length(),2.2*RiftArenaGeometry::WorldScale,2.2*RiftArenaGeometry::WorldScale))));
    }
    SlowRipple->ClearInstances();SlowRipple->AddInstances(Instances,false,true,false);
    for(int32 I=0;I<SlowRipple->GetNumMaterials();++I)
        if(auto* Material=Cast<UMaterialInstanceDynamic>(SlowRipple->GetMaterial(I)))Material->SetScalarParameterValue(TEXT("DissolveAmount"),float(T));
}
void ARiftUnitVisual::UpdateSelectedGlow(double Time,double Cooldown,bool Anticipating)
{
    if(AssetId!=TEXT("pulverizer"))return;
    const double Windup=Anticipating && AttackAnticipation>0?1.-FMath::Clamp(Cooldown/AttackAnticipation,0.,1.):0.;
    const double ContactAge=Time-LastAttack;
    const double Contact=ContactAge>=0?FMath::Clamp(1.-ContactAge/.22,0.,1.):0.;
    const double Collapse=bDead?FMath::Square(1.-FMath::Clamp((Time-DeathTime)/CleanupSeconds,0.,1.)):1.;
    for(auto Material:Materials)Material->SetScalarParameterValue(TEXT("GlowStrength"),float((1.2+.9*Windup+2.0*Contact)*Collapse));
}
bool ARiftUnitVisual::InitializeEntity(const rift::Entity& Entity,URiftCardData* Card,double Time)
{
    EntityId=Entity.id; Kind=Entity.kind; Team=Entity.team; CardData=Card; Born=Entity.born;
    bFlying=Entity.flying; bCoreActive=Entity.active; bDead=false;bHasBeenDamaged=false;
    UpdateDamageHistory(Entity,Time);
    AssetId=UTF8_TO_TCHAR(Entity.cardId.c_str());AnimationPhase=float(Entity.id%17)/17.f;AnimationTime=VisualTime=Time;
    LastSynchronizedCooldown=Entity.cooldown;
    const rift::Card* Definition=rift::FindEntityDefinition(Entity.cardId);
    Scale=(Definition?float(Definition->scale):1.f)*RiftArenaGeometry::WorldScale;
    AttackInterval=Definition?Definition->attackInterval:Kind==rift::EntityKind::Core?.92:1.02;
    AttackContact=Card?FMath::Clamp(double(Card->AttackContactNormalized),.01,.99):.48;
    AttackAnticipation=Card?FMath::Max(0.,double(Card->AttackAnticipationSeconds)):0;
    AttackRecovery=Card?FMath::Max(0.,double(Card->AttackRecoverySeconds)):0;
    AuthoredStrideMeters=Card?FMath::Max(0.,double(Card->GaitStrideMeters)):0;
    bHasAuthoredDeathTiming=Card && Card->DeathCleanupSeconds>0;
    CleanupSeconds=bHasAuthoredDeathTiming?FMath::Clamp(double(Card->DeathCleanupSeconds),.1,1.05):1.05;
    DeathFallSeconds=bHasAuthoredDeathTiming?CleanupSeconds:.65;
    DeathClipSeconds=bHasAuthoredDeathTiming?CleanupSeconds:.8;
    FlightBobTiles=Card?FMath::Clamp(double(Card->FlightBobTiles),0.,.09):.09;
    if (Kind==rift::EntityKind::Troop || (Kind==rift::EntityKind::Building && Card && Card->CharacterMesh))
    {
        if (!Card || !Card->CharacterMesh) {RIFT_LOG(LogRift,Error,TEXT("Missing character for entity %llu"),EntityId);return false;}
        Character->SetSkeletalMeshAsset(Card->CharacterMesh);
        // This multiplier affects only the visual replica. The authoritative
        // radius, tile footprint, range, speed and deck scale remain unchanged.
        if (Kind==rift::EntityKind::Building && Definition && Definition->footprint>0)
        {
            StructureHeightScale=Scale;
            const FVector Bounds=Card->CharacterMesh->GetBounds().BoxExtent*2;
            const float PlanScale=Definition->footprint*RiftArenaGeometry::UnitsPerTile/FMath::Max(Bounds.X,Bounds.Y);
            Character->SetRelativeScale3D(FVector(PlanScale,PlanScale,Scale));
        }
        else
        {
            Scale*=Card->ReadabilityMultiplier>0?Card->ReadabilityMultiplier:ModelReadabilityScale(AssetId);
            Character->SetRelativeScale3D(FVector(Scale));
        }
        Clips=Card->Animations;
        const float Radius=FMath::Max(44.f*RiftArenaGeometry::WorldScale,float(Entity.radius*RiftArenaGeometry::UnitsPerTile*1.18));
        GroundRing->DecalSize=FVector(32*RiftArenaGeometry::WorldScale,Radius,Radius);
    }
    else
    {
        const FString MeshId=Kind==rift::EntityKind::Core?TEXT("tower_core"):Kind==rift::EntityKind::Guard?TEXT("tower_guard"):AssetId;
        UStaticMesh* Mesh=Card?Card->StructureMesh.Get():LoadObject<UStaticMesh>(nullptr,*FString::Printf(TEXT("/Game/Rift/Environment/SM_%s.SM_%s"),*MeshId,*MeshId));
        if (!Mesh) {RIFT_LOG(LogRift,Error,TEXT("Missing authored structure %s"),*MeshId);return false;}
        Structure->SetStaticMesh(Mesh);
        StructureHeightScale=(Kind==rift::EntityKind::Building?1.22f:1.18f)*RiftArenaGeometry::WorldScale;
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
            const float PlanScale=Definition->footprint*RiftArenaGeometry::UnitsPerTile/FMath::Max(Bounds.X,Bounds.Y)*1.15f;
            Structure->SetRelativeScale3D(FVector(PlanScale,PlanScale,StructureHeightScale));
        }
        GroundRing->DecalSize=FVector(32*RiftArenaGeometry::WorldScale,Entity.radius*RiftArenaGeometry::UnitsPerTile,Entity.radius*RiftArenaGeometry::UnitsPerTile);
        if (Kind==rift::EntityKind::Building && AssetId==TEXT("archer_tower"))
        {
            AssetId=TEXT("tower_archer");
            auto* Archer=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Rift/Characters/tower_archer/SK_tower_archer.SK_tower_archer"));
            if (!Archer) {RIFT_LOG(LogRift,Error,TEXT("Archer Tower decoration missing"));return false;}
            Character->SetSkeletalMeshAsset(Archer);ArcherPerchHeight=194*StructureHeightScale;
            Character->SetRelativeLocation(FVector(0,0,ArcherPerchHeight));Character->SetRelativeScale3D(FVector(.60*RiftArenaGeometry::WorldScale));
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
    EmitDeploySurfaceDust(Time);
    UpdateReaperAura(Time,false);
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
        16.f*RiftArenaGeometry::WorldScale*FMath::Sin(float(Age/.18)*UE_PI):0.f;
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
    // contact pose immediately, then recover; do not delay simulated damage.
    SetClip(AttackClip(),AttackContact,false,true);
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
            const double Decay=Definition->hpDecayPerSecond>0?Definition->hpDecayPerSecond:Entity.maxHp/Definition->lifetime;
            UndamagedHP=FMath::Max(0.,Entity.maxHp-Decay*FMath::Max(0.,Time-Entity.born));
            Tolerance=FMath::Max(.000001,Entity.maxHp*.00000001);
        }
    bHasBeenDamaged|=Entity.hp<UndamagedHP-Tolerance;
    bHasBeenDamaged|=Entity.maxShieldHP>0 && Entity.shieldHP<Entity.maxShieldHP;
}
void ARiftUnitVisual::SpecialAt(FName Name,double Time,float Duration)
{if (!bDead) {SpecialClip=Name;SpecialTime=Time;SpecialDuration=FMath::Max(.1f,Duration);}}
void ARiftUnitVisual::AdvanceDeath(double Time)
{
    if(!bDead)return;VisualTime=Time;
    const float T=float(FMath::Clamp((Time-DeathTime)/DeathFallSeconds,0.,1.));
    const float Fall=T*T*(3.f-2.f*T);
    if(bFlying)
    {
        FVector Location=GetActorLocation();Location.Z=FMath::Lerp(DeathFlightHeight,8.*RiftArenaGeometry::WorldScale,double(Fall));SetActorLocation(Location);
    }
    if(ArcherPerchHeight>0)Character->SetRelativeLocation(FVector(0,0,FMath::Lerp(ArcherPerchHeight,12.f*RiftArenaGeometry::WorldScale,Fall)));
    if(bHasAuthoredDeathTiming)
        for(auto Material:Materials)Material->SetScalarParameterValue(TEXT("DissolveAmount"),T);
    if(IsBoar(AssetId))for(auto Material:Materials)
        Material->SetScalarParameterValue(TEXT("HitFlash"),float(.65*FMath::Clamp(1.-(Time-DeathTime)/.08,0.,1.)));
    UpdateSelectedGlow(Time,0,false);UpdateSurfaceMotes(Time);
    if(SlowRipple)SlowRipple->ClearInstances();
    UpdateReaperAura(Time,false);UpdatePulverizerPulse(Time,-100);
    SetClip(TEXT("Death"),(Time-DeathTime)/DeathClipSeconds);
}
void ARiftUnitVisual::DieAt(double Time)
{
    if (bDead) return;bDead=true;DeathTime=Time;VisualTime=Time;DeathFlightHeight=GetActorLocation().Z;GroundRing->SetVisibility(false);
    Cannon->SetVisibility(false);CannonRecoil=0;
    UpdateReaperAura(Time,false);UpdatePulverizerPulse(Time,-100);
    if (Character->GetSkeletalMeshAsset()) SetClip(TEXT("Death"),0);
    if(IsBoar(AssetId))SurfaceMotes.Reset();
    if(IsBoar(AssetId) || AssetId==TEXT("pulverizer"))AdvanceDeath(Time);
    // Static building bodies still become rubble when a decorative archer is
    // attached. Skeletal building bodies perform their authored death instead.
    if(Kind!=rift::EntityKind::Troop && Structure->GetStaticMesh())
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
    return bDead && Kind!=rift::EntityKind::Core && Kind!=rift::EntityKind::Guard &&
        (bHasAuthoredDeathTiming?Time-DeathTime>=CleanupSeconds:Time-DeathTime>CleanupSeconds);
}
void ARiftUnitVisual::Synchronize(const rift::Entity& Entity,const rift::Snapshot& State,float DeltaSeconds)
{
    const double Time=State.elapsed;VisualTime=Time;
    UpdateDamageHistory(Entity,Time);
    // Pose clocks, turn smoothing and visual banking must also stop when the
    // simulation pauses, regardless of the renderer's wall-clock delta.
    const double AnimationDelta=FMath::Clamp(Time-AnimationTime,0.,.5);AnimationTime=Time;
    FVector Position=URiftMatchSubsystem::WorldPoint(Entity.position);
    if (bFlying) Position.Z=bDead?GetActorLocation().Z:95.f*RiftArenaGeometry::WorldScale+FMath::Sin(Time*3.2+AnimationPhase*UE_TWO_PI)*float(FlightBobTiles*RiftArenaGeometry::UnitsPerTile);
    SetActorLocation(Position);
    const float Wanted=FMath::RadiansToDegrees(FMath::Atan2(Entity.facing.z,Entity.facing.x));
    const float Delta=FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,Wanted);
    if (FMath::Abs(Delta)>40 && Time-LastAttack>.3 && Time-TurnTime>.18) TurnTime=Time;
    const float TurnAlpha=1.f-FMath::Exp(-float(AnimationDelta)*11.f);
    SetActorRotation(FRotator(0,GetActorRotation().Yaw+FMath::Clamp(Delta*TurnAlpha,-float(AnimationDelta)*540.f,float(AnimationDelta)*540.f),0));
    UpdateStructureAim(Time);
    if (Kind==rift::EntityKind::Building) GroundRing->SetRelativeRotation(FRotator(-90,0,-GetActorRotation().Yaw));
    if (bFlying) GroundRing->SetRelativeLocation(FVector(0,0,6*RiftArenaGeometry::WorldScale-Position.Z));
    UpdateCloak(Entity,Time);UpdateShield(Entity,Time);
    if (Entity.dead && !bDead) DieAt(Entity.died);
    if (Entity.active && !bCoreActive && Kind==rift::EntityKind::Core) ActivateCore();
    if (bDead)
    {
        AdvanceDeath(Time);
        PreviousPosition=Position;return;
    }
    UpdateSurfaceMotes(Time);UpdatePulverizerSlow(Entity,Time);
    // Newly created surface batches must inherit the observer's cloak state.
    if(bHiddenFromViewer)UpdateCloak(Entity,Time);
    const double Travel=(Position-PreviousPosition).Size2D();PreviousPosition=Position;
    const bool TargetChanged=Entity.target!=LastTarget;
    if (TargetChanged && Entity.target) AcquireTime=Time;LastTarget=Entity.target;
    const bool CooldownChanged=Entity.cooldown!=LastSynchronizedCooldown;LastSynchronizedCooldown=Entity.cooldown;
    // A renderer tick with no new simulation input must retain the exact
    // walking state. Zero distance on a paused tick is not a stop command.
    // Real same-time target/cooldown and combat events still choose new poses.
    const bool FrozenMovingPose=AnimationDelta<=0 && Travel<=.005 && !TargetChanged && !CooldownChanged &&
        (CurrentClip==TEXT("Locomotion") || CurrentClip==TEXT("Charge"));
    UpdateReaperAura(Time,Travel>.005 || FrozenMovingPose);
    double PulseReleasedAt=-100;
    if(AssetId==TEXT("pulverizer"))for(const auto& Wave:State.shockwaves)
        if(Wave.source==EntityId && Wave.cardId=="pulverizer" && Wave.born<=Time && Time<Wave.expires)
            PulseReleasedAt=FMath::Max(PulseReleasedAt,Wave.born);
    UpdatePulverizerPulse(Time,PulseReleasedAt);
    const bool Stunned=Entity.stunUntil>Time;
    const bool Slowed=Entity.slowUntil>Time && Entity.slowPct>0;
    const bool Charging=AssetId==TEXT("rambeast") && Entity.charged;
    const double PreviousGaitPhase=GaitClock+AnimationPhase;
    if (!Stunned)
    {
        // One authored gait cycle covers two steps. Tying it to distance keeps
        // feet in cadence with slow effects and charged running, and prevents
        // movement clips from running in place between simulation updates.
        const double Stride=FMath::Max(45.,double(Scale)*(AuthoredStrideMeters>0?AuthoredStrideMeters*100:AuthoredGaitStrideCm(AssetId,Charging)));
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
            const auto* Definition=rift::FindEntityDefinition(Entity.cardId);
            const double BodyReach=Definition&&Kind==rift::EntityKind::Troop&&Definition->projectileSpeed<=0?Entity.radius:0.;
            TargetInRange=Definition && Distance-Target.radius<=Definition->range+BodyReach+(Kind==rift::EntityKind::Building?.35:.0)+.1;
            break;
        }
    const double RecoveryDuration=AttackRecovery>0?AttackRecovery:FMath::Clamp(AttackInterval*.32,.18,.50);
    const double WindupDuration=AttackAnticipation>0?AttackAnticipation:FMath::Clamp(AttackInterval*.32,.18,.42);
    const double SinceAttack=Time-LastAttack;
    UpdateSelectedGlow(Time,Entity.cooldown,TargetInRange && Entity.cooldown>0 && Entity.cooldown<=WindupDuration);
    if(SinceAttack>=0 && SinceAttack<RecoveryDuration)
    {
        // A release cannot wrap to the windup within the same strike. This
        // bounded second half ends in the authored neutral recovery pose.
        SetClip(AttackClip(),AttackContact+(1.-AttackContact)*SinceAttack/RecoveryDuration);return;
    }
    if(TargetInRange && Travel<.5 && !FrozenMovingPose && Entity.cooldown>0 && Entity.cooldown<=WindupDuration)
    {SetClip(AttackClip(),AttackContact*(1.-Entity.cooldown/WindupDuration));return;}
    if (Time-LastHit<.16 && Time-LastAttack>.22) {SetClip(TEXT("Hit"),(Time-LastHit)/.16);return;}
    if (Time-AcquireTime<.1 && Travel<.5 && !FrozenMovingPose) {SetClip(TEXT("Acquire"),(Time-AcquireTime)/.1);return;}
    if (Time-TurnTime<.12 && Travel<.5 && !FrozenMovingPose) {SetClip(TEXT("Turn"),(Time-TurnTime)/.12);return;}
    if (AssetId==TEXT("storm_raven") && Entity.auraClock>2.4)
    {SetClip(TEXT("AuraCharge"),(Entity.auraClock-2.4)/.6);return;}
    if (Travel>.005)
    {
        SetClip(Charging?TEXT("Charge"):TEXT("Locomotion"),GaitClock+AnimationPhase,true);
        EmitHoofDust(Time,PreviousGaitPhase,GaitClock+AnimationPhase);
    }
    else if(FrozenMovingPose)return;
    else SetClip(bFlying && AssetId!=TEXT("aether_reaper")?TEXT("WingCycle"):AssetId==TEXT("frost_fang")?TEXT("Breath"):TEXT("Idle"),Time*.7+AnimationPhase,true);
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
    if (Kind==rift::EntityKind::Building && Character->DoesSocketExist(TEXT("impact_origin")))
        return Character->GetSocketLocation(TEXT("impact_origin"));
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
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftSelectedSurfaceEffectsTest,"Rift.Presentation.SelectedSurfaceEffects",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftSelectedSurfaceEffectsTest::RunTest(const FString& Parameters)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox")))
    {AddError(TEXT("Selected surface fixture requires isolated RiftSaveRoot/UserDir and RiftAutomationSandbox."));return false;}
    auto* GI=NewObject<UGameInstance>(GEngine);GI->InitializeStandalone(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto* World=GI->GetWorld();if(!TestNotNull(TEXT("Surface fixture has an actual native world"),World)){GI->Shutdown();return false;}
    ON_SCOPE_EXIT {World->DestroyWorld(false);GI->Shutdown();GEngine->DestroyWorldContext(World);};
    auto* Catalog=GI->GetSubsystem<URiftAssetCatalogSubsystem>();
    rift::MatchOptions Options;Options.seed=74021;Options.aiEnabled={false,false};rift::Match Sim(Options);
    if(!TestTrue(TEXT("Visual fixture starts with ordinary authoritative card spawns"),
        Sim.Spawn(rift::Team::Player,"mini_stampede",{-7.5,8.5})&&Sim.Spawn(rift::Team::Player,"stampede",{7.5,8.5})))return false;
    const auto InitialEventCount=Sim.Events().size();const rift::Entity* Mini=nullptr;const rift::Entity* Stamp=nullptr;
    for(const auto& Entity:Sim.State().entities)
    {if(!Mini&&Entity.cardId=="mini_stampede")Mini=&Entity;if(!Stamp&&Entity.cardId=="stampede")Stamp=&Entity;}
    if(!TestNotNull(TEXT("Mini model has a real simulation entity"),Mini)||!TestNotNull(TEXT("Stampede model has a real simulation entity"),Stamp))return false;
    auto* MiniCard=Catalog->Card(TEXT("mini_stampede"));auto* StampCard=Catalog->Card(TEXT("stampede"));
    if(!TestNotNull(TEXT("Mini uses the actual imported card data"),MiniCard)||!TestNotNull(TEXT("Stampede uses the actual imported card data"),StampCard))return false;
    auto* MiniVisual=World->SpawnActor<ARiftUnitVisual>();auto* StampVisual=World->SpawnActor<ARiftUnitVisual>();auto* Follower=World->SpawnActor<ARiftUnitVisual>();
    StampVisual->SetFormationDustLeader(true,URiftMatchSubsystem::WorldPoint(Stamp->position));
    if(!TestTrue(TEXT("Fixture loads real model, socket and material bindings"),MiniVisual->InitializeEntity(*Mini,MiniCard,0)&&
        StampVisual->InitializeEntity(*Stamp,StampCard,0)&&Follower->InitializeEntity(*Stamp,StampCard,0)))return false;
    TestTrue(TEXT("Mini deployment uses sparse bounded surface grains"),MiniVisual->HoofDustCount()>0&&MiniVisual->HoofDustCount()<=HoofDustLimit);
    TestEqual(TEXT("One Stampede owner emits at most four formation grains"),StampVisual->FormationDustCount(),FormationDustLimit);
    TestEqual(TEXT("Non-owner Stampede members never duplicate the formation cloud"),Follower->FormationDustCount(),0);
    auto State=Sim.State();const int32 PausedDust=MiniVisual->HoofDustCount();
    for(int32 I=0;I<20;++I)MiniVisual->Synchronize(*Mini,State,1.f);
    TestEqual(TEXT("Renderer ticks cannot advance or multiply paused surface dust"),MiniVisual->HoofDustCount(),PausedDust);
    State.elapsed=.3;MiniVisual->Synchronize(*Mini,State,0);StampVisual->Synchronize(*Stamp,State,0);
    TestEqual(TEXT("Hoof dust retires on simulation time even with zero renderer delta"),MiniVisual->HoofDustCount(),0);
    TestEqual(TEXT("Formation dust leaves nothing on the tile after its deadline"),StampVisual->FormationDustCount(),0);
    auto PausedMutation=*Mini;State.elapsed=.4;
    for(int32 I=0;I<128;++I)
    {
        PausedMutation.position.x=Mini->position.x+(I%2?8.:0.);
        MiniVisual->Synchronize(PausedMutation,State,0);
    }
    // UpdateSurfaceMotes also checks its private eight-record CPU invariant on
    // every mutation; this fixture exercises it while no mote can age out.
    TestTrue(TEXT("Repeated same-time position mutations keep bounded surface output"),MiniVisual->HoofDustCount()<=HoofDustLimit);
    auto Receiver=*Mini;Receiver.slows.push_back({0,0,rift::Team::Enemy,"pulverizer",.1,1.,2.5});Receiver.slowPct=.1;Receiver.slowUntil=2.5;
    State.elapsed=1.1;MiniVisual->Synchronize(Receiver,State,0);
    TestEqual(TEXT("Actual imported geometry makes one narrow ankle ripple"),MiniVisual->SlowRippleCount(),SlowRippleSegments);
    MiniVisual->Synchronize(Receiver,State,5.f);
    TestEqual(TEXT("Paused slow ripple cannot expire by wall-clock delta"),MiniVisual->SlowRippleCount(),SlowRippleSegments);
    State.elapsed=1.29;MiniVisual->Synchronize(Receiver,State,0);
    TestEqual(TEXT("The short hit ripple retires while the gameplay slow remains active"),MiniVisual->SlowRippleCount(),0);
    State.elapsed=1.1;MiniVisual->Synchronize(Receiver,State,0);
    TestEqual(TEXT("Replay-time reconstruction restores the correct recorded hit ripple"),MiniVisual->SlowRippleCount(),SlowRippleSegments);
    MiniVisual->DieAt(1.1);MiniVisual->AdvanceDeath(1.2);
    TestEqual(TEXT("A boar death creates exactly two copper and one gray fleck"),MiniVisual->DeathFleckCount(),3);
    TestEqual(TEXT("Death clears the movement slow ripple immediately"),MiniVisual->SlowRippleCount(),0);
    for(int32 I=0;I<20;++I)MiniVisual->AdvanceDeath(1.2);
    TestEqual(TEXT("A paused corpse cannot multiply death flecks"),MiniVisual->DeathFleckCount(),3);
    MiniVisual->AdvanceDeath(1.1+MiniCard->DeathCleanupSeconds+.000001);
    TestEqual(TEXT("All boar flecks retire after the authored cleanup deadline"),MiniVisual->DeathFleckCount(),0);
    TestEqual(TEXT("Visual fixtures emit no extra authoritative combat events"),Sim.Events().size(),InitialEventCount);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftVioletAuraPulseTest,"Rift.Presentation.VioletAuraPulse",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftVioletAuraPulseTest::RunTest(const FString& Parameters)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox")))
    {AddError(TEXT("Violet aura fixture requires isolated RiftSaveRoot/UserDir and RiftAutomationSandbox."));return false;}
    auto* GI=NewObject<UGameInstance>(GEngine);GI->InitializeStandalone(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto* World=GI->GetWorld();if(!TestNotNull(TEXT("Aura fixture uses an actual native world"),World)){GI->Shutdown();return false;}
    ON_SCOPE_EXIT {World->DestroyWorld(false);GI->Shutdown();GEngine->DestroyWorldContext(World);};
    auto* Catalog=GI->GetSubsystem<URiftAssetCatalogSubsystem>();
    rift::MatchOptions Options;Options.aiEnabled={false,false};rift::Match Sim(Options);
    if(!TestTrue(TEXT("Imported aura owners originate from real card spawns"),Sim.Spawn(rift::Team::Player,"aether_reaper",{-.5,6.5}) && Sim.Spawn(rift::Team::Player,"pulverizer",{4.5,6.5})))return false;
    const auto InitialEventCount=Sim.Events().size();
    rift::Entity Reaper,Pulverizer;
    for(const auto& Entity:Sim.State().entities){if(Entity.cardId=="aether_reaper")Reaper=Entity;if(Entity.cardId=="pulverizer")Pulverizer=Entity;}
    auto* ReaperData=Catalog->Card(TEXT("aether_reaper"));auto* PulseData=Catalog->Card(TEXT("pulverizer"));
    if(!TestNotNull(TEXT("Reaper uses the actual imported CardData"),ReaperData)||!TestNotNull(TEXT("Pulverizer uses the actual imported CardData"),PulseData))return false;
    auto* ReaperVisual=World->SpawnActor<ARiftUnitVisual>();auto* PulseVisual=World->SpawnActor<ARiftUnitVisual>();
    auto* EnemyVisual=World->SpawnActor<ARiftUnitVisual>();auto Enemy=Reaper;Enemy.team=rift::Team::Enemy;
    if(!TestTrue(TEXT("Purple effects load real meshes and materials"),ReaperVisual->InitializeEntity(Reaper,ReaperData,0) && PulseVisual->InitializeEntity(Pulverizer,PulseData,0) && EnemyVisual->InitializeEntity(Enemy,ReaperData,0)))return false;
    constexpr int32 AuraLimit=ReaperAuraStrands*ReaperAuraSegments+ReaperAuraWispLimit;
    TestEqual(TEXT("An idle Reaper has exactly three open strands and four wisps"),ReaperVisual->ReaperAuraCount(),AuraLimit);
    auto NamedBatch=[](ARiftUnitVisual* Visual,FName Name)->UInstancedStaticMeshComponent*
    {TArray<UInstancedStaticMeshComponent*> Batches;Visual->GetComponents(Batches);for(auto* Batch:Batches)if(Batch->GetFName()==Name)return Batch;return nullptr;};
    auto* Aura=NamedBatch(ReaperVisual,TEXT("ReaperVioletAuraRibbons"));
    if(!TestNotNull(TEXT("Aura is real instanced geometry, independent of Niagara quality"),Aura))return false;
    TestTrue(TEXT("Aura geometry cannot block troops or receive floor decals"),Aura->GetCollisionEnabled()==ECollisionEnabled::NoCollision && !Aura->bReceivesDecals && !Aura->CastShadow);
    auto State=Sim.State();State.elapsed=1.;ReaperVisual->Synchronize(Reaper,State,0);
    FTransform Idle;Aura->GetInstanceTransform(0,Idle,true);
    for(int32 I=0;I<20;++I)ReaperVisual->Synchronize(Reaper,State,3.f);
    FTransform Paused;Aura->GetInstanceTransform(0,Paused,true);
    TestTrue(TEXT("An idle paused aura retains its exact simulation-time transform"),Idle.Equals(Paused,0.));
    State.elapsed=1.2;Reaper.position.x+=.5;ReaperVisual->Synchronize(Reaper,State,0);
    FTransform Moving;Aura->GetInstanceTransform(0,Moving,true);
    TestTrue(TEXT("Walking advances and carries the purple aura with the body"),ReaperVisual->CurrentAnimation()==TEXT("Locomotion") && !Moving.Equals(Idle,.001) && ReaperVisual->ReaperAuraCount()==AuraLimit);
    for(int32 I=0;I<20;++I)ReaperVisual->Synchronize(Reaper,State,3.f);
    Aura->GetInstanceTransform(0,Paused,true);
    TestTrue(TEXT("Paused locomotion does not turn or slide the aura on wall-clock ticks"),Moving.Equals(Paused,0.));
    Reaper.invisibleUntil=2.;Enemy.invisibleUntil=2.;ReaperVisual->Synchronize(Reaper,State,0);EnemyVisual->Synchronize(Enemy,State,0);
    TestEqual(TEXT("An enemy cloak leaves no aura pieces that reveal its location"),EnemyVisual->ReaperAuraCount(),0);
    if(auto* Material=Cast<UMaterialInstanceDynamic>(Aura->GetMaterial(0)))
        TestTrue(TEXT("A cloaked ally's purple aura is subdued with its silhouette"),Material->K2_GetScalarParameterValue(TEXT("EffectOpacity"))<=.180001f);
    else AddError(TEXT("Aura lost its per-actor material instance."));
    State.elapsed=2.;EnemyVisual->Synchronize(Enemy,State,0);
    TestEqual(TEXT("Cloak expiry restores the existing bounded aura"),EnemyVisual->ReaperAuraCount(),AuraLimit);
    // Isolate presentation at the authoritative body-edge melee boundary.
    // The copied target pose changes no simulation entity or hit packet.
    for(auto& Target:State.entities)if(Target.team==rift::Team::Enemy&&Target.kind==rift::EntityKind::Guard) {
        Reaper.invisibleUntil=0;Reaper.target=Target.id;Reaper.cooldown=.1;
        const double Reach=Sim.AttackReach(Reaper,Target);
        Target.position={Reaper.position.x+Reach-.01,Reaper.position.z};State.elapsed=2.3;
        ReaperVisual->Synchronize(Reaper,State,0);
        TestEqual(TEXT("A legal body-edge melee contact shows its attack windup"),ReaperVisual->CurrentAnimation(),FName(TEXT("Attack")));
        break;
    }
    ReaperVisual->DieAt(2.);ReaperVisual->AdvanceDeath(2.1);
    TestEqual(TEXT("Reaper death removes all continuous aura geometry immediately"),ReaperVisual->ReaperAuraCount(),0);
    State.elapsed=1.1;PulseVisual->AttackAt(1.,65);PulseVisual->Synchronize(Pulverizer,State,0);
    TestEqual(TEXT("An attack animation without a recorded wave cannot fabricate a pulse"),PulseVisual->PulverizerPulseCount(),0);
    const auto* Definition=rift::FindCard("pulverizer");if(!TestNotNull(TEXT("Wave fixture uses the authoritative definition"),Definition))return false;
    rift::Shockwave Wave;Wave.source=Pulverizer.id;Wave.cardId="pulverizer";Wave.origin=Pulverizer.position;
    Wave.born=1.;Wave.radius=Definition->splash;Wave.speed=Definition->shockwaveSpeed;Wave.expires=Wave.born+Wave.radius/Wave.speed;State.shockwaves.push_back(Wave);
    PulseVisual->Synchronize(Pulverizer,State,0);
    TestEqual(TEXT("Recorded launch reconstructs one elevated thirty-two-piece release pulse"),PulseVisual->PulverizerPulseCount(),PulverizerPulseSegments);
    auto* Pulse=NamedBatch(PulseVisual,TEXT("PulverizerVioletReleaseHalo"));
    if(!TestNotNull(TEXT("Release pulse uses real imported mesh geometry"),Pulse))return false;
    FTransform Released;Pulse->GetInstanceTransform(0,Released,true);
    for(int32 I=0;I<20;++I)PulseVisual->Synchronize(Pulverizer,State,3.f);
    Pulse->GetInstanceTransform(0,Paused,true);
    TestTrue(TEXT("Paused wave release retains its exact pulse size and height"),Released.Equals(Paused,0.) && PulseVisual->PulverizerPulseCount()==PulverizerPulseSegments);
    State.elapsed=1.29;PulseVisual->Synchronize(Pulverizer,State,0);
    TestEqual(TEXT("The elevated release pulse clears while its traveling damage front continues"),PulseVisual->PulverizerPulseCount(),0);
    State.elapsed=1.1;PulseVisual->Synchronize(Pulverizer,State,0);
    TestEqual(TEXT("A backward replay sample reconstructs the recorded release pulse"),PulseVisual->PulverizerPulseCount(),PulverizerPulseSegments);
    State.shockwaves[0].source=Pulverizer.id+1;PulseVisual->Synchronize(Pulverizer,State,0);
    TestEqual(TEXT("Another source's wave cannot pulse this building"),PulseVisual->PulverizerPulseCount(),0);
    State.shockwaves[0].source=Pulverizer.id;State.elapsed=Wave.expires;PulseVisual->Synchronize(Pulverizer,State,0);
    TestEqual(TEXT("An expired wave leaves no release halo"),PulseVisual->PulverizerPulseCount(),0);
    TestEqual(TEXT("Decorative aura and pulse state emits no gameplay events"),Sim.Events().size(),InitialEventCount);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftExpansionShieldCloakTest,"Rift.Presentation.ExpansionShieldCloak",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftExpansionShieldCloakTest::RunTest(const FString& Parameters)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox")))
    {AddError(TEXT("Expansion visual fixture requires the isolated automation sandbox."));return false;}
    auto* GI=NewObject<UGameInstance>(GEngine);GI->InitializeStandalone(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto* World=GI->GetWorld();if(!TestNotNull(TEXT("Expansion fixture uses an actual native world"),World)){GI->Shutdown();return false;}
    ON_SCOPE_EXIT {World->DestroyWorld(false);GI->Shutdown();GEngine->DestroyWorldContext(World);};
    auto* Catalog=GI->GetSubsystem<URiftAssetCatalogSubsystem>();
    auto* Data=Catalog->Card(TEXT("iron_giant"));
    if(!TestNotNull(TEXT("Giant uses imported canonical CardData"),Data))return false;
    rift::MatchOptions Options;Options.aiEnabled={false,false};rift::Match Sim(Options);
    if(!TestTrue(TEXT("Visual inputs originate from a real spawned body"),Sim.Spawn(rift::Team::Player,"iron_giant",{.5,5.5})))return false;
    rift::Entity Giant;for(const auto& Entity:Sim.State().entities)if(Entity.cardId=="iron_giant")Giant=Entity;
    auto* Owner=World->SpawnActor<ARiftUnitVisual>();auto* Other=World->SpawnActor<ARiftUnitVisual>();auto* Enemy=World->SpawnActor<ARiftUnitVisual>();
    auto Hostile=Giant;Hostile.team=rift::Team::Enemy;
    if(!TestTrue(TEXT("Three replicas load the actual imported mesh"),Owner->InitializeEntity(Giant,Data,0)&&Other->InitializeEntity(Giant,Data,0)&&Enemy->InitializeEntity(Hostile,Data,0)))return false;
    auto* Mesh=Owner->FindComponentByClass<USkeletalMeshComponent>();auto* OtherMesh=Other->FindComponentByClass<USkeletalMeshComponent>();auto* EnemyMesh=Enemy->FindComponentByClass<USkeletalMeshComponent>();
    TArray<UMaterialInterface*> Originals,Others;
    for(int32 I=0;I<Mesh->GetNumMaterials();++I){Originals.Add(Mesh->GetMaterial(I));Others.Add(OtherMesh->GetMaterial(I));}
    for(int32 I=0;I<6;++I)TestTrue(TEXT("Imported shield panels have separate bones"),Mesh->GetBoneIndex(FName(*FString::Printf(TEXT("shield_panel_%d"),I)))!=INDEX_NONE);
    auto ShieldState=Sim.State();ShieldState.elapsed=.4;Owner->ShieldHitAt(.4,10);Owner->Synchronize(Giant,ShieldState,0);
    TestEqual(TEXT("A shield hit flashes four edges on each of six real panels"),Owner->ShieldEdgeCount(),24);
    for(int32 I=0;I<20;++I)Owner->Synchronize(Giant,ShieldState,5.f);
    TestEqual(TEXT("Paused shield feedback retains exactly twenty-four edges"),Owner->ShieldEdgeCount(),24);
    TestTrue(TEXT("Shield feedback leaves full body HP and the actor alive"),Giant.hp==1000&&!Owner->IsDead());
    ShieldState.elapsed=.52;Owner->Synchronize(Giant,ShieldState,0);
    TestEqual(TEXT("Shield edge flash clears after its simulation deadline"),Owner->ShieldEdgeCount(),0);
    auto State=Sim.State();State.elapsed=.5;Giant.invisibleUntil=1.5;Hostile.invisibleUntil=1.5;
    Owner->Synchronize(Giant,State,0);Enemy->Synchronize(Hostile,State,0);
    TestTrue(TEXT("Owner sees a subdued ally silhouette"),Owner->IsCloaked() && Mesh->IsVisible() && !Owner->HiddenFromViewer());
    TestTrue(TEXT("Enemy cloak hides the actual skeletal component and shadows"),Enemy->HiddenFromViewer() && !EnemyMesh->IsVisible() && !EnemyMesh->CastShadow);
    for(int32 I=0;I<Originals.Num();++I)
    {TestTrue(TEXT("Cloak uses per-actor material instances"),Mesh->GetMaterial(I)!=Originals[I]);TestTrue(TEXT("Unchanged allies keep their original instances"),OtherMesh->GetMaterial(I)==Others[I]);}
    for(int32 I=0;I<20;++I)Owner->Synchronize(Giant,State,5.f);
    TestTrue(TEXT("Wall-clock renderer ticks do not expire a paused cloak"),Owner->IsCloaked());
    State.elapsed=1.5;Owner->Synchronize(Giant,State,0);Enemy->Synchronize(Hostile,State,0);
    TestTrue(TEXT("Both visibility treatments expire on simulation time"),!Owner->IsCloaked() && !Enemy->HiddenFromViewer() && EnemyMesh->IsVisible());
    for(int32 I=0;I<Originals.Num();++I)TestTrue(TEXT("Expiry restores exact original material instances"),Mesh->GetMaterial(I)==Originals[I]);
    State.elapsed=1.6;Giant.shieldHP=0;Giant.shieldBrokenAt=1.5;Owner->Synchronize(Giant,State,0);
    TestEqual(TEXT("Shield break creates six bounded non-interactive fragments"),Owner->ShieldFragmentCount(),6);
    TestEqual(TEXT("A broken shield clears its edge flash"),Owner->ShieldEdgeCount(),0);
    for(int32 I=0;I<6;++I)TestTrue(TEXT("Shield break hides panels, leaving the body"),Mesh->IsBoneHiddenByName(FName(*FString::Printf(TEXT("shield_panel_%d"),I))));
    TestTrue(TEXT("Breaking shield preserves body visibility and life"),Mesh->IsVisible() && !Owner->IsDead() && Giant.hp==1000);
    for(int32 I=0;I<20;++I)Owner->Synchronize(Giant,State,5.f);
    TestEqual(TEXT("Paused break cannot multiply fragment output"),Owner->ShieldFragmentCount(),6);
    State.elapsed=2.;Owner->Synchronize(Giant,State,0);TestEqual(TEXT("Fragments clear at the half-second deadline"),Owner->ShieldFragmentCount(),0);
    Giant.shieldHP=310;Giant.shieldBrokenAt=-1;Giant.invisibleUntil=0;State.elapsed=.1;Owner->Synchronize(Giant,State,0);
    for(int32 I=0;I<6;++I)TestFalse(TEXT("Earlier replay pose restores each shield panel"),Mesh->IsBoneHiddenByName(FName(*FString::Printf(TEXT("shield_panel_%d"),I))));
    Giant.invisibleUntil=2.;State.elapsed=.3;Owner->Synchronize(Giant,State,0);Giant.dead=true;Giant.died=.3;Owner->Synchronize(Giant,State,0);
    TestFalse(TEXT("Death releases the cloak treatment"),Owner->IsCloaked());
    for(int32 I=0;I<Originals.Num();++I)TestTrue(TEXT("Death also restores exact original materials"),Mesh->GetMaterial(I)==Originals[I]);
    TestEqual(TEXT("Visual state transitions never spawn paid bodies or damage events"),Sim.State().entities.size(),State.entities.size());
    return !HasAnyErrors();
}
#endif
