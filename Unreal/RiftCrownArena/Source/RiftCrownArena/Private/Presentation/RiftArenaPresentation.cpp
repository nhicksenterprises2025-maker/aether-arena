#include "Presentation/RiftArenaPresentation.h"
#include "RiftDiagnostics.h"
#include "Presentation/RiftBattleAudioSubsystem.h"
#include "Presentation/RiftBattleOverlay.h"
#include "Presentation/RiftUnitVisual.h"
#include "RiftAssetLibrary.h"
#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "Components/DecalComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"

namespace
{
FName FlightFor(const std::string& Card)
{
    if (Card=="ember_archer" || Card=="archer_tower") return TEXT("ArrowFlight");
    if (Card=="arc_mage") return TEXT("ArcFlight");
    if (Card=="sky_manta") return TEXT("MantaFlight");
    if (Card=="storm_raven") return TEXT("StormFlight");
    return TEXT("TowerFlight");
}
const rift::Entity* FindEntity(const rift::Snapshot& State,uint64 Id)
{
    for (const auto& Entity:State.entities) if (Entity.id==Id) return &Entity;
    return nullptr;
}
FLinearColor EffectColor(FName Name,rift::Team Team)
{
    if (Name==TEXT("Frost") || Name==TEXT("Slow")) return FLinearColor(.35f,.82f,1.f,1.f);
    if (Name==TEXT("ArcFlight") || Name==TEXT("Nova")) return FLinearColor(.60f,.25f,1.f,1.f);
    if (Name==TEXT("MantaFlight")) return FLinearColor(.1f,.94f,.80f,1.f);
    if (Name==TEXT("StormFlight") || Name==TEXT("Aura") || Name==TEXT("CoreAwaken")) return FLinearColor(.28f,.67f,1.f,1.f);
    if (Name==TEXT("Stun")) return FLinearColor(1.f,.79f,.27f,1.f);
    if (Name==TEXT("Meteor") || Name==TEXT("MeteorTick")) return FLinearColor(1.f,.26f,.055f,1.f);
    if (Name==TEXT("BulletBurst") || Name==TEXT("ArrowFlight") || Name==TEXT("TowerFlight")) return FLinearColor(.92f,.69f,.31f,1.f);
    return ARiftUnitVisual::TeamColor(Team);
}
}

ARiftArenaPresentation::ARiftArenaPresentation()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;
    Scene=CreateDefaultSubobject<USceneComponent>(TEXT("AuthoredArenaRoot"));SetRootComponent(Scene);Scene->SetMobility(EComponentMobility::Static);
    auto* Sun=CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("AfternoonKey"));Sun->SetupAttachment(Scene);
    Sun->SetMobility(EComponentMobility::Movable);Sun->LightSourceAngle=1.5f;Sun->SetForwardShadingPriority(1);
    Sun->SetRelativeRotation(FRotator(-52,-24,0));Sun->SetIntensity(6.f);
    Sun->SetLightColor(FLinearColor(1.f,.89f,.74f));Sun->CastShadows=true;
    auto* Fill=CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("EquipmentFill"));Fill->SetupAttachment(Scene);
    Fill->SetMobility(EComponentMobility::Movable);Fill->SetRelativeRotation(FRotator(-58,156,0));
    Fill->SetIntensity(2.5f);Fill->SetLightColor(FLinearColor(.64f,.76f,.90f));Fill->CastShadows=false;
    Fill->bAtmosphereSunLight=false;
    Fill->SetForwardShadingPriority(0);
    auto* Sky=CreateDefaultSubobject<USkyLightComponent>(TEXT("SoftSkyFill"));Sky->SetupAttachment(Scene);
    Sky->SetMobility(EComponentMobility::Movable);
    Sky->SetIntensity(2.f);Sky->SetLightColor(FLinearColor(.72f,.81f,.94f));Sky->bRealTimeCapture=true;
    auto* Atmosphere=CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("RiftSky"));Atmosphere->SetupAttachment(Scene);
    auto* Fog=CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("DistantIslandHaze"));Fog->SetupAttachment(Scene);
    Fog->SetFogDensity(.008f);Fog->SetFogInscatteringColor(FLinearColor(.12f,.20f,.27f));Fog->SetStartDistance(3800.f);
    auto* Post=CreateDefaultSubobject<UPostProcessComponent>(TEXT("BattlefieldGrade"));Post->SetupAttachment(Scene);Post->bUnbound=true;
    Post->Settings.bOverride_BloomIntensity=true;Post->Settings.BloomIntensity=.18f;
    Post->Settings.bOverride_AutoExposureMinBrightness=true;Post->Settings.AutoExposureMinBrightness=.8f;
    Post->Settings.bOverride_AutoExposureMaxBrightness=true;Post->Settings.AutoExposureMaxBrightness=1.2f;
    Post->Settings.bOverride_VignetteIntensity=true;Post->Settings.VignetteIntensity=.12f;
    Post->Settings.bOverride_AmbientOcclusionIntensity=true;Post->Settings.AmbientOcclusionIntensity=.5f;
    PlacementDecal=CreateDefaultSubobject<UDecalComponent>(TEXT("PlacementFootprint"));PlacementDecal->SetupAttachment(Scene);
    PlacementDecal->SetRelativeRotation(FRotator(-90,0,0));PlacementDecal->SetVisibility(false);PlacementDecal->SortOrder=4;
}
void ARiftArenaPresentation::BeginPlay()
{
    Super::BeginPlay();
    Match=GetWorld()->GetSubsystem<URiftMatchSubsystem>();
    Audio=GetGameInstance()->GetSubsystem<URiftBattleAudioSubsystem>();
    Audio->SetBattleWorld(GetWorld());
    ConstructArena();
    if (auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Rift/Materials/M_RiftPlacement.M_RiftPlacement")))
    {PlacementMaterial=UMaterialInstanceDynamic::Create(Material,this);PlacementDecal->SetDecalMaterial(PlacementMaterial);}
    if (auto* Player=UGameplayStatics::GetPlayerController(this,0))
    {
        Overlay=CreateWidget<URiftBattleOverlay>(Player,URiftBattleOverlay::StaticClass());
        Overlay->SetPresentation(this);Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);Overlay->AddToViewport(5);
    }
    EventHandle=Match->OnEvent.AddUObject(this,&ARiftArenaPresentation::OnSimulationEvent);
    MatchHandle=Match->OnChanged.AddUObject(this,&ARiftArenaPresentation::OnMatchChanged);
    Synchronize(0.f);
}
void ARiftArenaPresentation::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Match) {Match->OnEvent.Remove(EventHandle);Match->OnChanged.Remove(MatchHandle);}
    if (Overlay) Overlay->RemoveFromParent();
    ClearVisuals();if (Audio) Audio->SetBattleWorld(nullptr);
    Super::EndPlay(Reason);
}
const rift::Snapshot* ARiftArenaPresentation::ViewState()const{return Match?Match->ViewState():nullptr;}
ARiftUnitVisual* ARiftArenaPresentation::Visual(uint64 Id)const
{const auto* Found=Units.Find(Id);return Found?Found->Get():nullptr;}
void ARiftArenaPresentation::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);Synchronize(DeltaSeconds);if (Audio) Audio->RefreshVolumes();
}
UHierarchicalInstancedStaticMeshComponent* ARiftArenaPresentation::Instances(FName MeshId,int32 Team)
{
    const FName Key(*FString::Printf(TEXT("%s_%d"),*MeshId.ToString(),Team));
    if (auto* Found=Environment.Find(Key)) return Found->Get();
    auto* Mesh=LoadObject<UStaticMesh>(nullptr,*FString::Printf(TEXT("/Game/Rift/Environment/SM_%s.SM_%s"),*MeshId.ToString(),*MeshId.ToString()));
    if (!Mesh)
    {
        if (!MissingAssets.Contains(MeshId)) {MissingAssets.Add(MeshId);RIFT_LOG(LogRift,Error,TEXT("Authored environment missing: %s"),*MeshId.ToString());}
        return nullptr;
    }
    auto* Component=NewObject<UHierarchicalInstancedStaticMeshComponent>(this,Key);Component->SetupAttachment(Scene);
    Component->SetStaticMesh(Mesh);Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetGenerateOverlapEvents(false);Component->SetMobility(EComponentMobility::Static);
    Component->RegisterComponent();AddInstanceComponent(Component);Environment.Add(Key,Component);
    if (MeshId==TEXT("floor_tile") && Team!=9)
        for (int32 Index=0;Index<Component->GetNumMaterials();++Index)
            if (auto* Dynamic=Component->CreateDynamicMaterialInstance(Index))
            {Dynamic->SetScalarParameterValue(TEXT("RiftGridStrength"),.35f);Dynamic->SetScalarParameterValue(TEXT("RiftBaseColorGain"),1.12f);}
    if (Team>=0)
        for (int32 Index=0;Index<Component->GetNumMaterials();++Index)
            if (auto* Dynamic=Component->CreateDynamicMaterialInstance(Index))
            {
                Dynamic->SetVectorParameterValue(TEXT("TeamColor"),ARiftUnitVisual::TeamColor(Team==0?rift::Team::Player:rift::Team::Enemy));
                Dynamic->SetScalarParameterValue(TEXT("RiftTeamEmissive"),.12f);
            }
    return Component;
}
void ARiftArenaPresentation::Place(FName Mesh,FVector Position,FRotator Rotation,FVector Scale,int32 Team)
{if (auto* Component=Instances(Mesh,Team)) Component->AddInstance(FTransform(Rotation,Position,Scale),false);}
void ARiftArenaPresentation::ConstructArena()
{
    FRandomStream Random(150151);
    // The playable extent, tile centers, river and bridge openings are identical
    // to the simulation. Decoration stays outside deployment and navigation.
    for (int32 X=0;X<28;++X) for (int32 Y=0;Y<42;++Y)
    {
        const float PX=(X-13.5f)*100, PY=(Y-20.5f)*100;
        if (FMath::Abs(PY)<165.f) continue;
        Place(TEXT("floor_tile"),FVector(PX,PY,0),FRotator(0,90*(X%4),0));
        if ((FMath::Abs(PX-720)<130 || FMath::Abs(PX+720)<130) && FMath::Abs(PY)<1150 && Y%2==0)
            Place(TEXT("lane_paver"),FVector(PX,PY,1),FRotator(0,Random.FRandRange(-8,8),0));
    }
    for (int32 X=-19;X<19;++X) for (int32 Y=-24;Y<24;++Y)
    {
        const float PX=(X+.5f)*100,PY=(Y+.5f)*100;
        if (FMath::Abs(PX)<1400 && FMath::Abs(PY)<2100) continue;
        if (FMath::Abs(PY)<165.f) continue;
        Place(TEXT("floor_tile"),FVector(PX,PY,-18),FRotator(0,90*((X+24)%4),0));
    }
    // Authored stone/wood bridges use local +X forward after FBX conversion.
    for (float X:{-720.f,720.f}) Place(TEXT("bridge"),FVector(X,0,-16),FRotator(0,90,0));
    for (int32 Side:{-1,1})
    {
        for (int32 Index=0;Index<14;++Index)
        {
            const float X=(Index-6.5f)*200;
            if (FMath::Abs(X-720)<250 || FMath::Abs(X+720)<250) continue;
            Place(TEXT("bank_segment"),FVector(X,Side*175.f,-40),FRotator(0,90,0));
        }
        for (int32 Index=0;Index<30;++Index)
            Place(TEXT("boundary_stone"),FVector(Side*1460.f,(Index-14.5f)*140,-15),FRotator(0,0,0),FVector(1.1));
        for (int32 Index=0;Index<19;++Index)
            Place(TEXT("boundary_stone"),FVector((Index-9.f)*150,Side*2160.f,-15),FRotator(0,90,0),FVector(1.1));
        for (int32 Index=0;Index<24;++Index)
        {
            const float Y=(Index-11.5f)*175;
            Place(TEXT("shrub"),FVector(Side*1540.f+Random.FRandRange(-35,35),Y,0),FRotator(0,Random.FRandRange(0,360),0),FVector(Random.FRandRange(.7,1.3)));
            Place(TEXT("grass_tuft"),FVector(Side*1435.f,Y+45,5),FRotator(0,Random.FRandRange(0,360),0),FVector(1.4));
            if (Index%4==1) Place(TEXT("tree"),FVector(Side*1750.f,Y+70,-20),FRotator(0,Random.FRandRange(0,360),0),FVector(Random.FRandRange(.95,1.35)));
        }
        Place(TEXT("ruin"),FVector(Side*1840.f,Side*1420.f,-25),FRotator(0,Side*32,0),FVector(1.5));
        for (float X:{-1220.f,1220.f})
        {
            Place(TEXT("banner"),FVector(X,Side*1860.f,0),FRotator(0,Side*90,0),FVector(1.05),Side==1?0:1);
            Place(TEXT("crystal_plinth"),FVector(X,Side*900.f,0),FRotator::ZeroRotator,FVector(.8),Side==1?0:1);
        }
    }
    // The authored eroded island supports the field rather than a stock cube.
    Place(TEXT("distant_island"),FVector(0,0,-125),FRotator::ZeroRotator,FVector(10.5,14,3.8));
    for (int32 Index=0;Index<6;++Index)
    {
        const float Angle=Index*UE_TWO_PI/6;
        Place(TEXT("distant_island"),FVector(FMath::Cos(Angle)*5300,FMath::Sin(Angle)*6400,-900-Index*110),
            FRotator(0,Index*57,0),FVector(1.6+Index*.2));
    }
    if (auto* Water=Instances(TEXT("floor_tile"),9))
    {
        Water->SetCastShadow(false);
        if (auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Rift/Materials/M_RiftWater.M_RiftWater")))
            Water->SetMaterial(0,Material);
        Water->AddInstance(FTransform(FRotator::ZeroRotator,FVector(0,0,-22),FVector(28,3.3,.05)),false);
    }
}
void ARiftArenaPresentation::ClearVisuals()
{
    for (auto& Entry:Units) if (Entry.Value) Entry.Value->Destroy();Units.Reset();
    for (auto& Entry:Projectiles) if (Entry.Value) Entry.Value->DestroyComponent();Projectiles.Reset();ProjectileOrigins.Reset();
    for (auto& Entry:Hazards) if (Entry.Value) Entry.Value->DestroyComponent();Hazards.Reset();
    for (auto& Entry:SlowEffects) if (Entry.Value) Entry.Value->DestroyComponent();SlowEffects.Reset();
    for (auto& Entry:StunEffects) if (Entry.Value) Entry.Value->DestroyComponent();StunEffects.Reset();
    for (auto& Effect:TransientEffects) if (Effect.IsValid()) Effect->DestroyComponent();TransientEffects.Reset();
    LastSeed=0;LastTime=-1;ResultDeathClock=0;AetherStage=1;ClearPlacementPreview();
}
void ARiftArenaPresentation::OnMatchChanged()
{
    const auto* State=ViewState();
    if (!State || (LastSeed && State->seed!=LastSeed) || (State && State->elapsed+.01<LastTime)) ClearVisuals();
    Synchronize(0.f);
}
void ARiftArenaPresentation::Synchronize(float DeltaSeconds)
{
    const auto* State=ViewState();
    if (!State) {if (Units.Num()) ClearVisuals();return;}
    if ((LastSeed && State->seed!=LastSeed) || State->elapsed+.01<LastTime) ClearVisuals();
    LastSeed=State->seed;LastTime=State->elapsed;
    ResultDeathClock=State->phase==rift::Phase::Finished?FMath::Min(1.1f,ResultDeathClock+DeltaSeconds):0.f;
    auto* Catalog=GetGameInstance()->GetSubsystem<URiftAssetCatalogSubsystem>();
    TSet<uint64> Present;
    for (const auto& Entity:State->entities)
    {
        Present.Add(Entity.id);auto* Actor=Visual(Entity.id);
        if (!Actor)
        {
            Actor=GetWorld()->SpawnActor<ARiftUnitVisual>();
            auto* Card=Catalog->Card(UTF8_TO_TCHAR(Entity.cardId.c_str()));
            if (!Actor->InitializeEntity(Entity,Card,State->elapsed)) {Actor->Destroy();continue;}
            Units.Add(Entity.id,Actor);
        }
        Actor->Synchronize(Entity,*State,DeltaSeconds);
        if (State->phase==rift::Phase::Finished && Actor->IsDead()) Actor->AdvanceDeath(State->elapsed+ResultDeathClock);
    }
    TArray<uint64> Removed;
    for (auto& Entry:Units)
    {
        if (!Present.Contains(Entry.Key))
        {
            if (!Entry.Value->IsDead()) Removed.Add(Entry.Key);
            else {Entry.Value->AdvanceDeath(State->elapsed+ResultDeathClock);if (Entry.Value->IsExpired(State->elapsed+ResultDeathClock)) Removed.Add(Entry.Key);}
        }
    }
    for (uint64 Id:Removed) {Units[Id]->Destroy();Units.Remove(Id);}
    SynchronizeProjectiles(*State);SynchronizeHazards(*State);SynchronizeStatuses(*State);DrawDeveloperOverlay(*State);
    TransientEffects.RemoveAllSwap([](const auto& Effect){return !Effect.IsValid();});
    const int32 Stage=State->phase==rift::Phase::Regulation?(State->elapsed>=120?2:1):State->phaseElapsed>=60?3:2;
    if (Stage>AetherStage && State->phase!=rift::Phase::Finished)
    {PlayEventSound(Stage==3?TEXT("aether_three"):TEXT("aether_two"),FVector::ZeroVector,.5f);AetherStage=Stage;}
}
UNiagaraSystem* ARiftArenaPresentation::Effect(FName Name)
{
    if (const auto* Found=Effects.Find(Name)) return Found->Get();
    auto* System=LoadObject<UNiagaraSystem>(nullptr,*FString::Printf(TEXT("/Game/Rift/VFX/NS_Rift%s.NS_Rift%s"),*Name.ToString(),*Name.ToString()));
    Effects.Add(Name,System);if (!System) RIFT_LOG(LogRift,Error,TEXT("Authored Niagara system missing: %s"),*Name.ToString());
    return System;
}
void ARiftArenaPresentation::SetEffectParameters(UNiagaraComponent* Component,rift::Team Team,float Radius,FVector Source,FVector Target,FLinearColor Color)
{
    if (!Component) return;if (Color.A<=0.f) Color=ARiftUnitVisual::TeamColor(Team);
    const uint32 ColorKey=Color.ToFColor(false).ToPackedRGBA();
    if (!ParticleMaterials.Contains(ColorKey))
    {
        if (auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Rift/Materials/M_RiftParticle.M_RiftParticle")))
        {
            auto* Material=UMaterialInstanceDynamic::Create(Base,this);Material->SetVectorParameterValue(TEXT("Color"),Color);
            ParticleMaterials.Add(ColorKey,Material);
        }
    }
    Component->SetVariableLinearColor(TEXT("User.Color"),Color);
    Component->SetVariableFloat(TEXT("User.Radius"),Radius);Component->SetVariableFloat(TEXT("User.Strength"),1.f);
    Component->SetVariablePosition(TEXT("User.Source"),Source);Component->SetVariablePosition(TEXT("User.Target"),Target);
    if (auto* Material=ParticleMaterials.Find(ColorKey)) Component->SetVariableMaterial(TEXT("User.RiftMaterial"),Material->Get());
}
UNiagaraComponent* ARiftArenaPresentation::SpawnEffect(FName Name,FVector Position,rift::Team Team,float Radius,bool Persistent)
{
    auto* System=Effect(Name);if (!System) return nullptr;
    auto* Component=UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,System,Position,FRotator::ZeroRotator,
        FVector::OneVector,!Persistent,false,ENCPoolMethod::None,false);
    SetEffectParameters(Component,Team,Radius,Position,Position,EffectColor(Name,Team));
    if (Component)
    {
        const bool Radial=Name==TEXT("Nova") || Name==TEXT("Meteor") || Name==TEXT("MeteorTick") ||
            Name==TEXT("Aura") || Name==TEXT("TowerDestroy") || Name==TEXT("CoreAwaken") || Name==TEXT("BulletBurst");
        if (Radial) Component->SetWorldScale3D(FVector(FMath::Max(.1f,Radius/100.f)));
        Component->Activate(true);
        if (!Persistent)
        {
            TransientEffects.Add(Component);
            // Burst assets must never accumulate even if a template's emitter
            // loop setting was accidentally left infinite during authoring.
            const TWeakObjectPtr<UNiagaraComponent> WeakComponent(Component);FTimerHandle Cleanup;
            GetWorld()->GetTimerManager().SetTimer(Cleanup,FTimerDelegate::CreateLambda([WeakComponent]()
            {if (WeakComponent.IsValid()) WeakComponent->DestroyComponent();}),2.5f,false);
        }
    }
    return Component;
}
void ARiftArenaPresentation::SynchronizeProjectiles(const rift::Snapshot& State)
{
    TSet<uint64> Present;
    for (const auto& Projectile:State.projectiles)
    {
        Present.Add(Projectile.id);
        UNiagaraComponent* Component=Projectiles.FindRef(Projectile.id);
        auto* Source=Visual(Projectile.source);auto* Target=Visual(Projectile.target);
        if (!Target) continue;
        if (!Component)
        {
            const FVector Start=Source?Source->AttackLocation():URiftMatchSubsystem::WorldPoint(Projectile.origin,135);
            Component=SpawnEffect(FlightFor(Projectile.cardId),Start,Projectile.team,18.f,true);
            if (!Component) continue;
            Projectiles.Add(Projectile.id,Component);ProjectileOrigins.Add(Projectile.id,Start);
        }
        const FVector Start=ProjectileOrigins.FindRef(Projectile.id), End=Target->ImpactLocation();
        const float Progress=FMath::Clamp(1.-Projectile.remaining/FMath::Max(.001,Projectile.duration),0.,1.);
        FVector Position=FMath::Lerp(Start,End,Progress);
        if (Projectile.cardId=="ember_archer" || Projectile.cardId=="archer_tower") Position.Z+=FMath::Sin(Progress*UE_PI)*30.f;
        Component->SetWorldLocation(Position);Component->SetWorldRotation((End-Position).Rotation());
        SetEffectParameters(Component,Projectile.team,18,Start,End,EffectColor(FlightFor(Projectile.cardId),Projectile.team));
    }
    TArray<uint64> Removed;for (const auto& Entry:Projectiles) if (!Present.Contains(Entry.Key)) Removed.Add(Entry.Key);
    for (uint64 Id:Removed) {if (Projectiles[Id]) Projectiles[Id]->DestroyComponent();Projectiles.Remove(Id);ProjectileOrigins.Remove(Id);}
}
void ARiftArenaPresentation::SynchronizeHazards(const rift::Snapshot& State)
{
    TSet<uint64> Present;
    for (const auto& Hazard:State.hazards)
    {
        Present.Add(Hazard.playId);
        if (!Hazards.Contains(Hazard.playId))
        {
            auto* Component=SpawnEffect(TEXT("MeteorTick"),URiftMatchSubsystem::WorldPoint(Hazard.position,8),Hazard.team,Hazard.radius*100,true);
            if (Component) Hazards.Add(Hazard.playId,Component);
        }
    }
    TArray<uint64> Removed;for (const auto& Entry:Hazards) if (!Present.Contains(Entry.Key)) Removed.Add(Entry.Key);
    for (uint64 Id:Removed) {if (Hazards[Id]) Hazards[Id]->DestroyComponent();Hazards.Remove(Id);}
}
void ARiftArenaPresentation::SynchronizeStatuses(const rift::Snapshot& State)
{
    auto Update=[&](TMap<uint64,TObjectPtr<UNiagaraComponent>>& Components,FName Name,bool Slow)
    {
        TSet<uint64> Present;
        for (const auto& Entity:State.entities)
        {
            const bool Active=!Entity.dead && (Slow?Entity.slowUntil:Entity.stunUntil)>State.elapsed && (!Slow || Entity.slowPct>0);
            if (!Active) continue;auto* Unit=Visual(Entity.id);if (!Unit) continue;Present.Add(Entity.id);
            UNiagaraComponent* Component=Components.FindRef(Entity.id).Get();
            if (!Component) {Component=SpawnEffect(Name,Unit->StatusLocation(),Entity.team,35,true);if (Component) Components.Add(Entity.id,Component);}
            if (Component) Component->SetWorldLocation(Unit->StatusLocation());
        }
        TArray<uint64> Removed;for (const auto& Entry:Components) if (!Present.Contains(Entry.Key)) Removed.Add(Entry.Key);
        for (uint64 Id:Removed) {if (Components[Id]) Components[Id]->DestroyComponent();Components.Remove(Id);}
    };
    Update(SlowEffects,TEXT("Slow"),true);Update(StunEffects,TEXT("Stun"),false);
}
void ARiftArenaPresentation::PlayEventSound(FName Name,FVector Position,float Gain)
{if (Audio) Audio->PlayEffect(Name,Position,Gain);}
void ARiftArenaPresentation::OnSimulationEvent(const rift::Event& Event)
{
    // Deployment events may arrive before this actor's next tick. Build current
    // visual replicas first so attacks, destruction and audio use real anchors.
    if (Event.type=="match_start" || Event.type=="entity_spawn" ||
        (Event.source && !Visual(Event.source)) || (Event.target && !Visual(Event.target))) Synchronize(0.f);
    auto* Source=Visual(Event.source);auto* Target=Visual(Event.target);
    const auto* State=ViewState();
    const FVector Position=Target?Target->ImpactLocation():URiftMatchSubsystem::WorldPoint(Event.position,25);
    if (Event.type=="entity_spawn")
    {SpawnEffect(TEXT("Deploy"),URiftMatchSubsystem::WorldPoint(Event.position,8),Event.team,65);if (Event.count<2 || Event.source%Event.count==0) PlayEventSound(TEXT("deploy"),Position,.45f);}
    else if (Event.type=="attack")
    {
        if (Source)
        {
            if (State) if (const auto* Entity=FindEntity(*State,Event.source))
                Source->SetActorRotation(FRotator(0,FMath::RadiansToDegrees(FMath::Atan2(Entity->facing.z,Entity->facing.x)),0));
            Source->AttackAt(Event.time,Event.amount);
        }
        const FVector Origin=Source?Source->AttackLocation():Position;
        const FName Sound=Event.cardId=="ironclad"?TEXT("sword_attack"):Event.cardId=="twin_blades"?TEXT("dual_attack"):
            Event.cardId=="ember_archer"||Event.cardId=="archer_tower"?TEXT("bow_release"):Event.cardId=="boulderback"?TEXT("heavy_slam"):
            Event.cardId=="rambeast"?TEXT("charge"):Event.cardId=="arc_mage"?TEXT("arc_cast"):Event.cardId=="sky_manta"?TEXT("manta_cast"):
            Event.cardId=="vampire_bats"?TEXT("bat_bite"):Event.cardId=="frost_fang"?TEXT("frost_attack"):
            Event.cardId=="storm_raven"?TEXT("storm_bolt"):TEXT("heavy_slam");
        PlayEventSound(Sound,Origin,Event.cardId=="vampire_bats"?.18f:.45f);
        if (Event.cardId=="frost_fang")
        {auto* FX=SpawnEffect(TEXT("Frost"),Origin,Event.team,70);SetEffectParameters(FX,Event.team,70,Origin,Position);}
    }
    else if (Event.type=="damage")
    {
        if (Target) Target->HitAt(Event.time);
        const FName FX=Event.damageKind=="dot"?TEXT("MeteorTick"):Event.cardId=="frost_fang"?TEXT("Frost"):TEXT("Impact");
        auto* Impact=SpawnEffect(FX,Position,Event.team,Event.damageKind=="splash"?120:45);
        SetEffectParameters(Impact,Event.team,Event.damageKind=="splash"?120:45,Position,Position,EffectColor(FlightFor(Event.cardId),Event.team));
        if (Event.damageKind!="initial" && Event.damageKind!="aura" && Event.damageKind!="dot")
        {
            const FName Sound=Event.cardId=="ember_archer"||Event.cardId=="archer_tower"?TEXT("arrow_hit"):
                Event.cardId=="arc_mage"?TEXT("arc_impact"):Event.cardId=="sky_manta"?TEXT("manta_impact"):
                Event.targetKind==rift::EntityKind::Guard||Event.targetKind==rift::EntityKind::Core?TEXT("tower_hit"):
                Event.targetKind==rift::EntityKind::Building?TEXT("building_hit"):TEXT("sword_hit");
            PlayEventSound(Sound,Position,Event.count>=5?.14f:.3f);
        }
    }
    else if (Event.type=="death")
    {
        if (Target) Target->DieAt(Event.time);
        if (Event.targetKind==rift::EntityKind::Building) {SpawnEffect(TEXT("TowerDestroy"),Position,Event.targetTeam,140);PlayEventSound(TEXT("tower_destroy"),Position,.55f);}
    }
    else if (Event.type=="tower_destroy")
    {SpawnEffect(TEXT("TowerDestroy"),Position,Event.targetTeam,Event.targetKind==rift::EntityKind::Core?270:190);PlayEventSound(TEXT("tower_destroy"),Position,.8f);}
    else if (Event.type=="core_activate")
    {if (Target) Target->ActivateCore();SpawnEffect(TEXT("CoreAwaken"),Position,Event.team,180);PlayEventSound(TEXT("core_awaken"),Position,.6f);}
    else if (Event.type=="slow" || Event.type=="stun")
    {if (State) SynchronizeStatuses(*State);if (Event.type=="stun" && Source) Source->SpecialAt(TEXT("StunDischarge"),Event.time,.3f);
        PlayEventSound(Event.type=="slow"?TEXT("frost_slow"):TEXT("stun"),Position,.25f);}
    else if (Event.type=="aura")
    {if (Source) Source->SpecialAt(TEXT("Pulse"),Event.time);SpawnEffect(TEXT("Aura"),Source?Source->GetActorLocation():Position,Event.team,Event.amount*100);PlayEventSound(TEXT("storm_pulse"),Position,.55f);}
    else if (Event.type=="hazard_tick" && State)
    {
        for (const auto& Hazard:State->hazards) if (Hazard.playId==Event.playId)
        {const FVector Center=URiftMatchSubsystem::WorldPoint(Hazard.position,8);SpawnEffect(TEXT("MeteorTick"),Center,Hazard.team,Hazard.radius*100);PlayEventSound(TEXT("meteor_tick"),Center,.35f);break;}
    }
    else if (Event.type=="card_play")
    {
        const auto* Card=rift::FindCard(Event.cardId);if (Card && Card->spell)
        {
            const FName FX=Event.cardId=="bullet_burst"?TEXT("BulletBurst"):Event.cardId=="nova_flask"?TEXT("Nova"):TEXT("Meteor");
            const FName Sound=Event.cardId=="bullet_burst"?TEXT("bullet_burst"):Event.cardId=="nova_flask"?TEXT("nova_impact"):TEXT("meteor_impact");
            SpawnEffect(FX,URiftMatchSubsystem::WorldPoint(Event.position,12),Event.team,Card->spellRadius*100);PlayEventSound(Sound,Position,.75f);
        }
    }
    else if (Event.type=="phase") PlayEventSound(Event.reason=="tiebreaker"?TEXT("tiebreaker"):TEXT("overtime"),FVector::ZeroVector,.65f);
    else if (Event.type=="match_end" && State) PlayEventSound(State->winner==0?TEXT("victory"):TEXT("defeat"),FVector::ZeroVector,.75f);
}
void ARiftArenaPresentation::SetPlacementPreview(FVector2D Tile,bool Valid,float RadiusTiles,bool Spell,float BuildingFootprintTiles)
{
    PlacementTile=Tile;PlacementRadius=FMath::Max(0.f,RadiusTiles);PlacementFootprint=FMath::Max(0.f,BuildingFootprintTiles);
    bPlacementValid=Valid;bPlacementSpell=Spell;bPreview=true;
    const float Half=Spell?FMath::Max(50.f,RadiusTiles*100):FMath::Max(50.f,BuildingFootprintTiles*50);
    PlacementDecal->SetWorldLocation(FVector(Tile.X*100,Tile.Y*100,14));PlacementDecal->DecalSize=FVector(36,Half,Half);
    if (PlacementMaterial)
    {
        PlacementMaterial->SetVectorParameterValue(TEXT("RingColor"),Valid?FLinearColor(.12f,.74f,.94f,1):FLinearColor(.96f,.24f,.17f,1));
        PlacementMaterial->SetScalarParameterValue(TEXT("Footprint"),Spell?0:1);PlacementMaterial->SetScalarParameterValue(TEXT("Valid"),Valid?1:0);
        PlacementDecal->SetVisibility(true);
    }
}
void ARiftArenaPresentation::ClearPlacementPreview(){bPreview=false;if (PlacementDecal) PlacementDecal->SetVisibility(false);}
void ARiftArenaPresentation::DrawDeveloperOverlay(const rift::Snapshot& State)
{
    const auto* Developer=GetWorld()->GetSubsystem<URiftDeveloperSubsystem>();if (!Developer) return;
    if (!(Developer->ShowPaths||Developer->ShowSight||Developer->ShowRanges||Developer->ShowTargets||Developer->ShowTiles||Developer->ShowHardLocks)) return;
    if (Developer->ShowTiles)
    {
        for (int32 X=-14;X<=14;++X) DrawDebugLine(GetWorld(),FVector(X*100,-2100,5),FVector(X*100,2100,5),FColor(120,147,155),false,0,0,.6f);
        for (int32 Y=-21;Y<=21;++Y) DrawDebugLine(GetWorld(),FVector(-1400,Y*100,5),FVector(1400,Y*100,5),FColor(120,147,155),false,0,0,.6f);
    }
    for (const auto& Entity:State.entities)
    {
        if (Entity.dead) continue;const FVector Origin=URiftMatchSubsystem::WorldPoint(Entity.position,16);
        const FColor Color=ARiftUnitVisual::TeamColor(Entity.team).ToFColor(true);
        if (Developer->ShowPaths)
        {
            FVector Previous=Origin;for (const auto& Point:Entity.path) {const FVector Next=URiftMatchSubsystem::WorldPoint(Point,16);DrawDebugLine(GetWorld(),Previous,Next,Color,false,0,0,2);Previous=Next;}
        }
        if (Developer->ShowRanges)
        {
            const auto* Card=rift::FindCard(Entity.cardId);const float Range=Card?Card->range:Entity.kind==rift::EntityKind::Core?8.9:10;
            DrawDebugCircle(GetWorld(),Origin,Range*100,48,Color,false,0,0,1,FVector::ForwardVector,FVector::RightVector,false);
        }
        if (Developer->ShowSight && Entity.kind==rift::EntityKind::Troop)
        {
            const FVector Forward(Entity.facing.x,Entity.facing.z,0),Side(-Entity.facing.z,Entity.facing.x,0);
            FVector Previous;bool First=true;
            for (int32 Index=0;Index<=48;++Index)
            {
                const float Angle=Index*UE_TWO_PI/48;const float Radius=FMath::Cos(Angle)>=0?800:500;
                const FVector Point=Origin+(Forward*FMath::Cos(Angle)+Side*FMath::Sin(Angle))*Radius;
                if (!First) DrawDebugLine(GetWorld(),Previous,Point,FColor(120,180,140),false,0,0,1);Previous=Point;First=false;
            }
        }
        if (Developer->ShowTargets && Entity.target)
            if (const auto* Target=FindEntity(State,Entity.target)) DrawDebugDirectionalArrow(GetWorld(),Origin,URiftMatchSubsystem::WorldPoint(Target->position,25),25,Color,false,0,0,1.5);
        if (Developer->ShowHardLocks && Entity.hardLock)
            if (const auto* Target=FindEntity(State,Entity.hardLock)) DrawDebugLine(GetWorld(),Origin+FVector(0,0,30),URiftMatchSubsystem::WorldPoint(Target->position,55),FColor::Yellow,false,0,0,3);
    }
}
