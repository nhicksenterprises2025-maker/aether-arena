#include "Presentation/RiftArenaPresentation.h"
#include "RiftDiagnostics.h"
#include "Presentation/RiftBattleAudioSubsystem.h"
#include "Presentation/RiftBattleOverlay.h"
#include "Presentation/RiftUnitVisual.h"
#include "RiftAssetLibrary.h"
#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "RiftReplaySubsystem.h"
#include "Components/DecalComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraDataSetAccessor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
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
FName MissileFor(const std::string& Card)
{
    if (Card=="ember_archer" || Card=="archer_tower") return TEXT("arrow_projectile");
    if (Card=="arc_mage") return TEXT("arc_projectile");
    if (Card=="sky_manta") return TEXT("manta_projectile");
    if (Card=="storm_raven") return TEXT("storm_projectile");
    if (Card=="meteor_shards") return TEXT("meteor_shard");
    return TEXT("bullet_round");
}
float ParticleSpriteScale(FName System)
{
    // Niagara's camera-facing quad size does not inherit component scale.
    // The particle material expands its real vertices about their own center.
    if (System==TEXT("NS_RiftArrowFlight")) return 5.6f;
    if (System==TEXT("NS_RiftArcFlight")) return 6.4f;
    if (System==TEXT("NS_RiftMantaFlight")) return 6.f;
    if (System==TEXT("NS_RiftStormFlight")) return 6.8f;
    if (System==TEXT("NS_RiftTowerFlight")) return 6.f;
    if (System==TEXT("NS_RiftNova") || System==TEXT("NS_RiftImpact")) return 3.f;
    return 1.f;
}
FQuat ProjectileMeshOrientation(const std::string& Card, const FVector& Direction, double Age)
{
    FQuat Facing=FRotationMatrix::MakeFromX(Direction.GetSafeNormal(UE_SMALL_NUMBER,FVector::ForwardVector)).ToQuat();
    // New missiles are authored along +X; the existing brass crown round's
    // nose is along +Z. Apply this correction locally, after aiming the flight.
    if (MissileFor(Card)==TEXT("bullet_round"))
        Facing=Facing*FQuat::FindBetweenNormals(FVector::UpVector,FVector::ForwardVector);
    else if (Card=="arc_mage" || Card=="sky_manta" || Card=="storm_raven")
        Facing=Facing*FQuat(FVector::ForwardVector,float(Age*(Card=="arc_mage"?9.:Card=="sky_manta"?5.:12.)));
    return Facing;
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

FVector ARiftArenaPresentation::ProjectilePathPoint(const std::string& Card,FVector Source,FVector Target,double Progress)
{
    const double T=FMath::Clamp(Progress,0.,1.);
    // Exact endpoints matter for short shots and replay endpoints. A bounded
    // visual arc does not change the authoritative travel time or hit target.
    if (T<=0.) return Source;
    if (T>=1.) return Target;
    FVector Position=FMath::Lerp(Source,Target,T);
    const double Distance=FVector::Dist2D(Source,Target);
    const double Envelope=FMath::Sin(T*UE_PI);
    const FVector Side=FVector::CrossProduct((Target-Source).GetSafeNormal(),FVector::UpVector).GetSafeNormal();
    if (Card=="ember_archer" || Card=="archer_tower")
        Position.Z+=Envelope*FMath::Clamp(Distance*.12,24.,90.);
    else if (Card=="arc_mage")
    {
        Position.Z+=Envelope*FMath::Clamp(Distance*.08,18.,64.);
        Position+=Side*(Envelope*FMath::Sin(T*UE_PI*4.)*9.);
    }
    else if (Card=="sky_manta")
    {
        Position.Z+=Envelope*38.;
        Position+=Side*(Envelope*FMath::Sin(T*UE_PI*2.)*14.);
    }
    else if (Card=="storm_raven")
    {
        Position.Z+=Envelope*22.;
        Position+=Side*(Envelope*FMath::Sin(T*UE_PI*6.)*12.);
    }
    else Position.Z+=Envelope*FMath::Clamp(Distance*.05,12.,42.);
    return Position;
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
    // Explicit native QA only: let the paused roster finish deployment through
    // ordinary simulation, then inspect its actual idle animation bindings.
    if(FParse::Param(FCommandLine::Get(),TEXT("RiftBreathSmoke")))
    {
        FTimerHandle StartTimer;GetWorld()->GetTimerManager().SetTimer(StartTimer,[this]()
        {
            if(!Match||!Match->IsActive())return;Match->SetSpeed(1);
            FTimerHandle StopTimer;GetWorld()->GetTimerManager().SetTimer(StopTimer,[this](){if(Match)Match->SetSpeed(0);},.65f,false);
        },.75f,false);
    }
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
    for (auto& Entry:ProjectileBodies) if (Entry.Value) Entry.Value->DestroyComponent();ProjectileBodies.Reset();
    for (auto& Entry:ProjectileTrails) if (Entry.Value) Entry.Value->DestroyComponent();ProjectileTrails.Reset();
    for (auto& Body:SpellDebrisBodies) if (Body) Body->DestroyComponent();SpellDebrisBodies.Reset();SpellDebris.Reset();
    ProjectileBodiesCreated=0;ProjectileBodiesReleased=0;
    for (auto& Entry:Hazards) if (Entry.Value) Entry.Value->DestroyComponent();Hazards.Reset();
    for (auto& Entry:SlowEffects) if (Entry.Value) Entry.Value->DestroyComponent();SlowEffects.Reset();
    for (auto& Entry:StunEffects) if (Entry.Value) Entry.Value->DestroyComponent();StunEffects.Reset();
    for (auto& Effect:TransientEffects) if (Effect.IsValid()) Effect->DestroyComponent();TransientEffects.Reset();
    NextFrostBreath.Reset();FrostBreathPuffs=0;
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
        if(Actor->CurrentAnimation()==TEXT("Breath")&&!Actor->IsDead()&&State->phase!=rift::Phase::Finished)
        {
            auto* Next=NextFrostBreath.Find(Entity.id);
            if(!Next||State->elapsed>=*Next)
            {
                // Idle mouth ambience reuses the authored finite Frost effect;
                // it has no event, damage, status or simulation-side ownership.
                const FVector Mouth=Actor->AttackLocation();
                if(auto* Puff=SpawnEffect(TEXT("Frost"),Mouth,Entity.team,60))
                {Puff->SetWorldRotation(Actor->GetActorRotation());++FrostBreathPuffs;}
                NextFrostBreath.Add(Entity.id,State->elapsed+2.4);
            }
        }
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
    for (uint64 Id:Removed) {Units[Id]->Destroy();Units.Remove(Id);NextFrostBreath.Remove(Id);}
    SynchronizeProjectiles(*State);SynchronizeSpellDebris(*State);SynchronizeHazards(*State);SynchronizeStatuses(*State);
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
void ARiftArenaPresentation::ShowcaseNiagara()
{
    const TCHAR* Names[]={TEXT("Deploy"),TEXT("Impact"),TEXT("ArrowFlight"),TEXT("ArcFlight"),TEXT("MantaFlight"),TEXT("StormFlight"),TEXT("TowerFlight"),TEXT("BulletBurst"),TEXT("Nova"),TEXT("Meteor"),TEXT("MeteorTick"),TEXT("Frost"),TEXT("Slow"),TEXT("Stun"),TEXT("Aura"),TEXT("TowerDestroy"),TEXT("CoreAwaken")};
    for(int32 Index=0;Index<UE_ARRAY_COUNT(Names);++Index)
    {
        const FName Name(Names[Index]);const bool Persistent=Name.ToString().EndsWith(TEXT("Flight"))||Name==TEXT("Slow")||Name==TEXT("Stun");
        auto* Component=SpawnEffect(Name,FVector((-10+5*(Index%5))*100,(-12+7*(Index/5))*100,75),rift::Team::Player,150,Persistent);
        if(Component)ShowcaseEffects.Add(Component);
        if(Component&&Persistent)
        {
            // This explicit capture fixture holds loop components long enough
            // for a second lifecycle sample, then disposes every QA component.
            TransientEffects.Add(Component);const TWeakObjectPtr<UNiagaraComponent> Weak(Component);FTimerHandle Cleanup;
            GetWorld()->GetTimerManager().SetTimer(Cleanup,FTimerDelegate::CreateLambda([Weak](){if(Weak.IsValid())Weak->DestroyComponent();}),3.f,false);
        }
    }
}
void ARiftArenaPresentation::ShowcaseNiagaraAtAge(float Age)
{
    // Explicit QA only. World-frame hitches must not replace the requested
    // short-lived-particle age with the first wall-clock frame's delta.
    if(!FMath::IsFinite(Age)||Age<0.f||Age>5.f)
    {RIFT_LOG(LogRift,Error,TEXT("Invalid Niagara showcase sample age: %g"),Age);return;}
    for(const auto& Component:ShowcaseEffects)if(Component.IsValid())Component->DestroyComponent();
    ShowcaseEffects.Reset();ShowcaseSampleAge=Age;
    ShowcaseNiagara();
    const int32 Steps=FMath::Max(1,FMath::CeilToInt(Age*100.f));
    for(const auto& WeakComponent:ShowcaseEffects)if(auto* Component=WeakComponent.Get())
    {
        // These components alone keep completed systems available for native
        // lifecycle inspection. The existing bounded QA cleanup still runs.
        Component->SetAutoDestroy(false);
        Component->SetForceSolo(true);
        Component->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
        Component->SetDesiredAge(Age);
        Component->SetComponentTickEnabled(false);
        // AdvanceSimulation synchronously executes the authored system graph,
        // including normal spawn, update and death scripts on every substep.
        // Do not use AdvanceSimulationByTime: it rounds to whole lower ticks.
        if(Age>0.f)Component->AdvanceSimulation(Steps,Age/Steps);
        if(auto Controller=Component->GetSystemInstanceController())Controller->WaitForConcurrentTickAndFinalize();
        Component->SetComponentTickEnabled(false);
    }
}
FString ARiftArenaPresentation::NiagaraDiagnosticsJSON()
{
    auto Report=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Entries;
    Report->SetBoolField(TEXT("showcaseAgeSampling"),ShowcaseSampleAge>=0.f);
    if(ShowcaseSampleAge>=0.f)Report->SetNumberField(TEXT("requestedShowcaseAge"),ShowcaseSampleAge);
    TArray<TSharedPtr<FJsonValue>> Animations;
    for(const auto& Pair:Units)if(auto* Unit=Pair.Value.Get())
    {
        auto Entry=MakeShared<FJsonObject>();Entry->SetNumberField(TEXT("entityId"),double(Pair.Key));
        Entry->SetStringField(TEXT("assetId"),Unit->PresentationAssetId());Entry->SetStringField(TEXT("clip"),Unit->CurrentAnimation().ToString());
        Entry->SetStringField(TEXT("animationAsset"),Unit->AnimationAssetPath());Entry->SetNumberField(TEXT("positionSeconds"),Unit->AnimationPosition());
        Entry->SetBoolField(TEXT("hasTakenDamage"),Unit->HasBeenDamaged());
        Entry->SetBoolField(TEXT("healthBarVisible"),!Unit->IsDead()&&Unit->HasBeenDamaged());
        Animations.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Report->SetArrayField(TEXT("unitAnimations"),Animations);
    TArray<TSharedPtr<FJsonValue>> Missiles,Trails;
    if (const auto* State=ViewState()) for (const auto& Projectile:State->projectiles)
    {
        auto Entry=MakeShared<FJsonObject>();Entry->SetNumberField(TEXT("id"),double(Projectile.id));
        Entry->SetStringField(TEXT("cardId"),UTF8_TO_TCHAR(Projectile.cardId.c_str()));
        Entry->SetNumberField(TEXT("sourceEntityId"),double(Projectile.source));Entry->SetNumberField(TEXT("targetEntityId"),double(Projectile.target));
        Entry->SetStringField(TEXT("sourceTeam"),UTF8_TO_TCHAR(rift::TeamName(Projectile.team).c_str()));
        if (const auto* SourceEntity=FindEntity(*State,Projectile.source))
        {
            const TCHAR* Kind=SourceEntity->kind==rift::EntityKind::Core?TEXT("core"):SourceEntity->kind==rift::EntityKind::Guard?TEXT("guard"):
                SourceEntity->kind==rift::EntityKind::Building?TEXT("building"):TEXT("troop");
            Entry->SetStringField(TEXT("sourceKind"),Kind);
            const auto* Card=rift::FindCard(Projectile.cardId);
            Entry->SetStringField(TEXT("displayRole"),Card?UTF8_TO_TCHAR(Card->name.c_str()):SourceEntity->kind==rift::EntityKind::Core?TEXT("Core Tower"):
                SourceEntity->kind==rift::EntityKind::Guard?TEXT("Guard Tower"):TEXT("Unknown source"));
            Entry->SetStringField(TEXT("sourceRole"),Projectile.cardId.empty()?SourceEntity->kind==rift::EntityKind::Core?TEXT("crown_core"):TEXT("crown_guard"):
                UTF8_TO_TCHAR(Projectile.cardId.c_str()));
        }
        Entry->SetNumberField(TEXT("duration"),Projectile.duration);Entry->SetNumberField(TEXT("remaining"),Projectile.remaining);
        Entry->SetNumberField(TEXT("progress"),FMath::Clamp(1.-Projectile.remaining/FMath::Max(.001,Projectile.duration),0.,1.));
        auto* Body=ProjectileBodies.FindRef(Projectile.id).Get();
        Entry->SetBoolField(TEXT("meshPresent"),Body&&Body->GetStaticMesh());
        Entry->SetBoolField(TEXT("meshVisible"),Body&&Body->IsVisible());
        if (Body)
        {
            Entry->SetStringField(TEXT("mesh"),Body->GetStaticMesh()?Body->GetStaticMesh()->GetPathName():TEXT("missing"));
            Entry->SetStringField(TEXT("location"),Body->GetComponentLocation().ToString());
            Entry->SetStringField(TEXT("rotation"),Body->GetComponentRotation().ToString());
            Entry->SetStringField(TEXT("scale"),Body->GetComponentScale().ToString());
            Entry->SetBoolField(TEXT("collisionDisabled"),Body->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
        }
        if (const auto* Origin=ProjectileOrigins.Find(Projectile.id)) Entry->SetStringField(TEXT("launchOrigin"),Origin->ToString());
        if (auto* Target=Visual(Projectile.target)) Entry->SetStringField(TEXT("impactAnchor"),Target->ImpactLocation().ToString());
        if (auto* Glow=Projectiles.FindRef(Projectile.id).Get()) Entry->SetBoolField(TEXT("glowPaused"),Glow->IsPaused());
        Missiles.Add(MakeShared<FJsonValueObject>(Entry));
    }
    for (const auto& Pair:ProjectileTrails) if (auto* Trail=Pair.Value.Get())
    {
        auto Entry=MakeShared<FJsonObject>();Entry->SetStringField(TEXT("name"),Pair.Key.ToString());
        Entry->SetStringField(TEXT("mesh"),Trail->GetStaticMesh()?Trail->GetStaticMesh()->GetPathName():TEXT("missing"));
        Entry->SetNumberField(TEXT("instances"),Trail->GetInstanceCount());
        Trails.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Report->SetArrayField(TEXT("projectileVisuals"),Missiles);Report->SetArrayField(TEXT("projectileTrails"),Trails);
    Report->SetNumberField(TEXT("liveProjectileBodies"),ProjectileBodies.Num());
    Report->SetNumberField(TEXT("projectileBodiesCreated"),double(ProjectileBodiesCreated));
    Report->SetNumberField(TEXT("projectileBodiesReleased"),double(ProjectileBodiesReleased));
    Report->SetNumberField(TEXT("spellDebrisBodies"),SpellDebrisBodies.Num());
    Report->SetBoolField(TEXT("breathSmoke"),FParse::Param(FCommandLine::Get(),TEXT("RiftBreathSmoke")));
    Report->SetNumberField(TEXT("frostBreathPuffs"),FrostBreathPuffs);
    TSet<UNiagaraComponent*> Components;
    for(const auto& Pair:Projectiles)if(Pair.Value)Components.Add(Pair.Value.Get());
    for(const auto& Pair:Hazards)if(Pair.Value)Components.Add(Pair.Value.Get());
    for(const auto& Pair:SlowEffects)if(Pair.Value)Components.Add(Pair.Value.Get());
    for(const auto& Pair:StunEffects)if(Pair.Value)Components.Add(Pair.Value.Get());
    for(const auto& Component:TransientEffects)if(Component.IsValid())Components.Add(Component.Get());
    int32 TotalParticles=0;
    for(auto* Component:Components)
    {
        auto Entry=MakeShared<FJsonObject>();auto* System=Component->GetAsset();
        Entry->SetStringField(TEXT("system"),System?System->GetPathName():TEXT("missing"));
        Entry->SetBoolField(TEXT("ready"),System&&System->IsReadyToRun());
        Entry->SetBoolField(TEXT("valid"),System&&System->IsValid());
        Entry->SetBoolField(TEXT("active"),Component->IsActive());Entry->SetBoolField(TEXT("visible"),Component->IsVisible());
        Entry->SetBoolField(TEXT("complete"),Component->IsComplete());
        Entry->SetStringField(TEXT("location"),Component->GetComponentLocation().ToString());
        Entry->SetStringField(TEXT("scale"),Component->GetComponentScale().ToString());
        TArray<UMaterialInterface*> UsedMaterials;Component->GetUsedMaterials(UsedMaterials);
        TArray<TSharedPtr<FJsonValue>> Materials;for(auto* Material:UsedMaterials)if(Material)Materials.Add(MakeShared<FJsonValueString>(Material->GetPathName()));
        Entry->SetArrayField(TEXT("materials"),Materials);
        float EffectSpriteScale=1.f;bool HasSpriteScale=false;
        for(auto* Material:UsedMaterials)if(Material&&Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("RiftSpriteScale")),EffectSpriteScale))
        {HasSpriteScale=true;break;}
        Entry->SetBoolField(TEXT("materialSpriteScaleAvailable"),HasSpriteScale);
        if(HasSpriteScale)Entry->SetNumberField(TEXT("materialSpriteScale"),EffectSpriteScale);
        TArray<TSharedPtr<FJsonValue>> Emitters;
        if(auto Controller=Component->GetSystemInstanceController();Controller&&Controller->IsValid())
        {
            // Finish concurrent work before reading CPU buffers through the
            // controller's explicitly unsafe access point on the game thread.
            Controller->WaitForConcurrentTickAndFinalize();Entry->SetNumberField(TEXT("age"),Controller->GetAge());
            if(auto* Instance=Controller->GetSystemInstance_Unsafe())for(const auto& Emitter:Instance->GetEmitters())
            {
                auto E=MakeShared<FJsonObject>();const int32 Count=Emitter->GetNumParticles();TotalParticles+=Count;
                E->SetNumberField(TEXT("particles"),Count);E->SetNumberField(TEXT("totalSpawned"),Emitter->GetTotalSpawnedParticles());
                E->SetNumberField(TEXT("executionState"),uint32(Emitter->GetExecutionState()));
                E->SetBoolField(TEXT("localSpace"),Emitter->IsLocalSpace());E->SetNumberField(TEXT("simTarget"),uint32(Emitter->GetSimTarget()));
                if(Count>0&&Emitter->GetSimTarget()==ENiagaraSimTarget::CPUSim)
                {
                    const auto& Data=Emitter->GetParticleData();
                    // Compiled CPU datasets can strip the Particles namespace.
                    // Resolve the actual recorded names, retaining an explicit
                    // unavailable flag instead of interpreting missing data as zero.
                    FName ColorName(TEXT("Particles.Color")),SizeName(TEXT("Particles.SpriteSize"));
                    TArray<TSharedPtr<FJsonValue>> Variables;
                    for(const auto& Variable:Data.GetVariables())
                    {
                        const FString Name=Variable.GetName().ToString();Variables.Add(MakeShared<FJsonValueString>(Name));
                        if(Name==TEXT("Color")||Name.EndsWith(TEXT(".Color")))ColorName=Variable.GetName();
                        if(Name==TEXT("SpriteSize")||Name.EndsWith(TEXT(".SpriteSize")))SizeName=Variable.GetName();
                    }
                    E->SetArrayField(TEXT("dataVariables"),Variables);
                    auto Colors=FNiagaraDataSetAccessor<FLinearColor>::CreateReader(Data,ColorName);
                    E->SetBoolField(TEXT("colorDataAvailable"),Colors.IsValid());
                    if(Colors.IsValid()){FLinearColor Minimum,Maximum;Colors.GetMinMax(Minimum,Maximum);E->SetNumberField(TEXT("alphaMin"),Minimum.A);E->SetNumberField(TEXT("alphaMax"),Maximum.A);}
                    auto Sizes=FNiagaraDataSetAccessor<FVector2f>::CreateReader(Data,SizeName);
                    E->SetBoolField(TEXT("sizeDataAvailable"),Sizes.IsValid());
                    if(Sizes.IsValid())
                    {
                        FVector2f Minimum,Maximum;Sizes.GetMinMax(Minimum,Maximum);E->SetStringField(TEXT("spriteSize"),Sizes.Get(0).ToString());
                        E->SetNumberField(TEXT("spriteWidthMin"),Minimum.X);E->SetNumberField(TEXT("spriteHeightMin"),Minimum.Y);
                        E->SetNumberField(TEXT("spriteWidthMax"),Maximum.X);E->SetNumberField(TEXT("spriteHeightMax"),Maximum.Y);
                        if(HasSpriteScale)
                        {
                            // These are material-derived dimensions. Keep the
                            // actual authored CPU SpriteSize fields unchanged.
                            E->SetNumberField(TEXT("materialScaledSpriteWidthMin"),Minimum.X*EffectSpriteScale);
                            E->SetNumberField(TEXT("materialScaledSpriteHeightMin"),Minimum.Y*EffectSpriteScale);
                            E->SetNumberField(TEXT("materialScaledSpriteWidthMax"),Maximum.X*EffectSpriteScale);
                            E->SetNumberField(TEXT("materialScaledSpriteHeightMax"),Maximum.Y*EffectSpriteScale);
                        }
                    }
                }
                Emitters.Add(MakeShared<FJsonValueObject>(E));
            }
        }
        Entry->SetArrayField(TEXT("emitters"),Emitters);Entries.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Report->SetNumberField(TEXT("totalParticles"),TotalParticles);Report->SetArrayField(TEXT("components"),Entries);
    FString Result;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Result));return Result;
}
void ARiftArenaPresentation::SetEffectParameters(UNiagaraComponent* Component,rift::Team Team,float Radius,FVector Source,FVector Target,FLinearColor Color)
{
    if (!Component) return;if (Color.A<=0.f) Color=ARiftUnitVisual::TeamColor(Team);
    const float EffectSpriteScale=ParticleSpriteScale(Component->GetAsset()?Component->GetAsset()->GetFName():NAME_None);
    const uint64 MaterialKey=uint64(Color.ToFColor(false).ToPackedRGBA())|(uint64(FMath::RoundToInt(EffectSpriteScale*1000.f))<<32);
    if (!ParticleMaterials.Contains(MaterialKey))
    {
        if (auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Rift/Materials/M_RiftParticle.M_RiftParticle")))
        {
            auto* Material=UMaterialInstanceDynamic::Create(Base,this);Material->SetVectorParameterValue(TEXT("Color"),Color);
            Material->SetScalarParameterValue(TEXT("RiftSpriteScale"),EffectSpriteScale);
            ParticleMaterials.Add(MaterialKey,Material);
        }
    }
    Component->SetVariableLinearColor(TEXT("User.Color"),Color);
    Component->SetVariableFloat(TEXT("User.Radius"),Radius);Component->SetVariableFloat(TEXT("User.Strength"),1.f);
    Component->SetVariablePosition(TEXT("User.Source"),Source);Component->SetVariablePosition(TEXT("User.Target"),Target);
    if (auto* Material=ParticleMaterials.Find(MaterialKey)) Component->SetVariableMaterial(TEXT("User.RiftMaterial"),Material->Get());
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
        else if (Name==TEXT("Impact")) Component->SetWorldScale3D(FVector(FMath::Clamp(Radius/25.f,.75f,4.f)));
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
    for (auto& Entry:ProjectileTrails) if (Entry.Value) Entry.Value->ClearInstances();
    auto* Replay=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();
    const float FlightSpeed=State.phase==rift::Phase::Finished?0.f:Replay&&Replay->IsPlaying()?Replay->Speed():Match->GetSpeed();
    TMap<UInstancedStaticMeshComponent*,TArray<FTransform>> TrailTransforms;
    TSet<uint64> Present;
    for (const auto& Projectile:State.projectiles)
    {
        UNiagaraComponent* Component=Projectiles.FindRef(Projectile.id);
        auto* Source=Visual(Projectile.source);auto* Target=Visual(Projectile.target);
        // A cancelled shot never invents a final impact. Damage events alone
        // own contact effects, including target death and terminal results.
        if (!Target || Target->IsDead()) continue;
        Present.Add(Projectile.id);
        if (!Component)
        {
            FVector Start=URiftMatchSubsystem::WorldPoint(Projectile.origin,135);
            if (const auto* Recorded=ProjectileOrigins.Find(Projectile.id)) Start=*Recorded;
            else if (Source)
            {
                // A replay may first expose a projectile after its caster has
                // moved. Translate the real socket offset onto its recorded
                // launch position rather than moving the shot's origin.
                const FVector SourceXY(Source->GetActorLocation().X,Source->GetActorLocation().Y,0.);
                Start=URiftMatchSubsystem::WorldPoint(Projectile.origin)+Source->AttackLocation()-SourceXY;
            }
            Component=SpawnEffect(FlightFor(Projectile.cardId),Start,Projectile.team,18.f,true);
            if (!Component) continue;
            Projectiles.Add(Projectile.id,Component);ProjectileOrigins.Add(Projectile.id,Start);
            if (auto* Body=MakeProjectileBody(Projectile.cardId,Projectile.team))
            {ProjectileBodies.Add(Projectile.id,Body);++ProjectileBodiesCreated;}
        }
        const FVector Start=ProjectileOrigins.FindRef(Projectile.id), End=Target->ImpactLocation();
        const double Duration=FMath::Max(.001,Projectile.duration);
        const double Progress=FMath::Clamp(1.-Projectile.remaining/Duration,0.,1.);
        const double Age=Progress*Duration;
        const FVector Position=ProjectilePathPoint(Projectile.cardId,Start,End,Progress);
        const FVector Tangent=ProjectilePathPoint(Projectile.cardId,Start,End,FMath::Min(1.,Progress+.003))-
            ProjectilePathPoint(Projectile.cardId,Start,End,FMath::Max(0.,Progress-.003));
        Component->SetWorldLocation(Position);Component->SetWorldRotation(Tangent.Rotation());
        Component->SetWorldScale3D(FVector::OneVector);
        SetEffectParameters(Component,Projectile.team,18,Start,End,EffectColor(FlightFor(Projectile.cardId),Projectile.team));
        // A launch or paused replay seek can expose a new system before its
        // first world tick. Seed its real authored graph once, after assigning
        // the complete flight transform/parameters, so pausing cannot hide its
        // first particle. Age also guards delayed asset activation: initialized
        // systems never receive another warm step while paused or running.
        if (auto Controller=Component->GetSystemInstanceController();Controller&&Controller->IsValid()&&Controller->GetAge()==0.f&&
            Component->GetAsset()&&Component->GetAsset()->IsReadyToRun())
        {
            Component->SetPaused(false);Component->SetCustomTimeDilation(1.f);
            Component->AdvanceSimulation(1,1.f/60.f);
            if (auto Initialized=Component->GetSystemInstanceController())Initialized->WaitForConcurrentTickAndFinalize();
        }
        Component->SetCustomTimeDilation(FlightSpeed);Component->SetPaused(FlightSpeed<=0.f);
        if (auto* Body=ProjectileBodies.FindRef(Projectile.id).Get())
        {
            const float Scale=MissileFor(Projectile.cardId)==TEXT("bullet_round")?3.f:1.25f;
            Body->SetWorldTransform(FTransform(ProjectileMeshOrientation(Projectile.cardId,Tangent,Age),Position,FVector(Scale)));
        }
        if (auto* Trail=ProjectileTrail(Projectile.cardId,Projectile.team))
        {
            auto& Instances=TrailTransforms.FindOrAdd(Trail);
            const double TrailAge=FMath::Min(Age,Projectile.cardId=="storm_raven"?.10:.065);
            constexpr int32 Segments=5;
            for (int32 Segment=0;Segment<Segments;++Segment)
            {
                const double A=Progress-TrailAge/Duration*double(Segment)/Segments;
                const double B=Progress-TrailAge/Duration*double(Segment+1)/Segments;
                const FVector Front=ProjectilePathPoint(Projectile.cardId,Start,End,A);
                const FVector Rear=ProjectilePathPoint(Projectile.cardId,Start,End,B);
                const FVector SegmentVector=Front-Rear;
                const double Length=SegmentVector.Length();if (Length<1.) continue;
                const float Width=1.8f*(1.f-float(Segment)/Segments)*(Projectile.cardId=="arc_mage"?1.6f:Projectile.cardId=="storm_raven"?1.4f:.9f);
                const FVector MeshSize=Trail->GetStaticMesh()->GetBounds().BoxExtent*2.;
                const FVector Scale(Length/FMath::Max(1.,MeshSize.X),Width,Width);
                Instances.Add(FTransform(FRotationMatrix::MakeFromX(SegmentVector).ToQuat(),(Front+Rear)*.5,Scale));
            }
        }
    }
    for (auto& Entry:TrailTransforms) Entry.Key->AddInstances(Entry.Value,false,true,false);
    TArray<uint64> Removed;for (const auto& Entry:Projectiles) if (!Present.Contains(Entry.Key)) Removed.Add(Entry.Key);
    for (uint64 Id:Removed) RemoveProjectile(Id);
    Removed.Reset();for (const auto& Entry:ProjectileOrigins) if (!Present.Contains(Entry.Key)) Removed.Add(Entry.Key);
    for (uint64 Id:Removed) ProjectileOrigins.Remove(Id);
}
UStaticMeshComponent* ARiftArenaPresentation::MakeProjectileBody(const std::string& Card,rift::Team Team)
{
    const FName MeshId=MissileFor(Card);
    UStaticMesh* Mesh=ProjectileAssets.FindRef(MeshId);
    if (!ProjectileAssets.Contains(MeshId))
    {
        Mesh=LoadObject<UStaticMesh>(nullptr,*FString::Printf(TEXT("/Game/Rift/Environment/SM_%s.SM_%s"),*MeshId.ToString(),*MeshId.ToString()));
        ProjectileAssets.Add(MeshId,Mesh);
        if (!Mesh) RIFT_LOG(LogRift,Error,TEXT("Authored projectile mesh missing: %s"),*MeshId.ToString());
    }
    if (!Mesh) return nullptr;
    auto* Body=NewObject<UStaticMeshComponent>(this);Body->SetupAttachment(Scene);Body->SetMobility(EComponentMobility::Movable);
    Body->SetStaticMesh(Mesh);Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);Body->SetGenerateOverlapEvents(false);
    Body->SetCastShadow(false);Body->bReceivesDecals=false;Body->RegisterComponent();AddInstanceComponent(Body);
    for (int32 Index=0;Index<Body->GetNumMaterials();++Index)
        if (auto* Material=Body->CreateDynamicMaterialInstance(Index))
        {
            Material->SetVectorParameterValue(TEXT("TeamColor"),ARiftUnitVisual::TeamColor(Team));
            Material->SetScalarParameterValue(TEXT("RiftTeamEmissive"),.2f);
        }
    return Body;
}
UInstancedStaticMeshComponent* ARiftArenaPresentation::ProjectileTrail(const std::string& Card,rift::Team Team)
{
    const FName Flight=FlightFor(Card),Key(*FString::Printf(TEXT("ProjectileTrail_%s_%d"),*Flight.ToString(),int32(Team)));
    if (auto* Existing=ProjectileTrails.Find(Key)) return Existing->Get();
    const FName MeshId(TEXT("projectile_trail"));UStaticMesh* Mesh=ProjectileAssets.FindRef(MeshId);
    if (!ProjectileAssets.Contains(MeshId))
    {
        Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Rift/Environment/SM_projectile_trail.SM_projectile_trail"));
        ProjectileAssets.Add(MeshId,Mesh);
        if (!Mesh) RIFT_LOG(LogRift,Error,TEXT("Authored projectile trail mesh missing"));
    }
    if (!Mesh) return nullptr;
    auto* Component=NewObject<UInstancedStaticMeshComponent>(this,Key);Component->SetupAttachment(Scene);
    Component->SetMobility(EComponentMobility::Movable);Component->SetStaticMesh(Mesh);Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetGenerateOverlapEvents(false);Component->SetCastShadow(false);Component->bReceivesDecals=false;
    Component->RegisterComponent();AddInstanceComponent(Component);ProjectileTrails.Add(Key,Component);
    for (int32 Index=0;Index<Component->GetNumMaterials();++Index)
        if (auto* Material=Component->CreateDynamicMaterialInstance(Index))
        {
            const FLinearColor Color=EffectColor(Flight,Team);
            Material->SetVectorParameterValue(TEXT("Color"),Color);
            Material->SetVectorParameterValue(TEXT("TeamColor"),Color);
            Material->SetScalarParameterValue(TEXT("RiftTeamEmissive"),.7f);
        }
    return Component;
}
void ARiftArenaPresentation::RemoveProjectile(uint64 Id)
{
    if (auto* Component=Projectiles.FindRef(Id).Get()) Component->DestroyComponent();
    if (auto* Body=ProjectileBodies.FindRef(Id).Get()) {Body->DestroyComponent();++ProjectileBodiesReleased;}
    Projectiles.Remove(Id);ProjectileBodies.Remove(Id);ProjectileOrigins.Remove(Id);
}
void ARiftArenaPresentation::SpawnSpellDebris(const rift::Event& Event)
{
    const bool Meteor=Event.cardId=="meteor_shards";
    if (!Meteor && Event.cardId!="bullet_burst") return;
    const auto* Card=rift::FindCard(Event.cardId);if (!Card) return;
    // These two spells resolve immediately in the game rules. The rounds and
    // shattered hot stone radiate from that actual impact; no delayed damage,
    // fictitious pre-impact travel, or projectile gameplay is introduced.
    const int32 Count=Meteor?5:Card->rounds;
    for (int32 Index=0;Index<Count;++Index)
    {
        // Bound seek/event bursts without changing the authoritative event
        // stream. Even extreme replay stepping keeps at most 96 mesh pieces.
        if (SpellDebrisBodies.Num()>=96)
        {
            if (SpellDebrisBodies[0]) SpellDebrisBodies[0]->DestroyComponent();
            SpellDebrisBodies.RemoveAt(0);SpellDebris.RemoveAt(0);
        }
        auto* Body=MakeProjectileBody(Event.cardId,Event.team);if (!Body) continue;
        FSpellDebris Piece;Piece.Meteor=Meteor;Piece.Born=Event.time;Piece.Duration=Meteor?.44:.22;
        Piece.Scale=Meteor?.65f:2.1f;Piece.Gravity=Meteor?1500.f:120.f;
        Piece.Spin=Meteor?float((Index%2?1:-1)*(6.+Index)):0.f;
        const double Angle=UE_TWO_PI*(double(Index)/Count+double(Event.sequence%17)/83.);
        const double Speed=Meteor?150.:Card->spellRadius*100./Piece.Duration*.82;
        Piece.Origin=URiftMatchSubsystem::WorldPoint(Event.position,Meteor?34.:24.);
        Piece.Velocity=FVector(FMath::Cos(Angle)*Speed,FMath::Sin(Angle)*Speed,Meteor?180.+Index*18.:18.+(Index%3)*10.);
        Body->SetWorldTransform(FTransform(ProjectileMeshOrientation("bullet_burst",Piece.Velocity,0.),Piece.Origin,FVector(Piece.Scale)));
        SpellDebrisBodies.Add(Body);SpellDebris.Add(Piece);
    }
}
void ARiftArenaPresentation::SynchronizeSpellDebris(const rift::Snapshot& State)
{
    for (int32 Index=SpellDebris.Num()-1;Index>=0;--Index)
    {
        const auto& Piece=SpellDebris[Index];auto* Body=SpellDebrisBodies[Index].Get();
        const double Age=FMath::Max(0.,State.elapsed-Piece.Born);
        if (!Body || Age>=Piece.Duration || State.phase==rift::Phase::Finished)
        {
            if (Body) Body->DestroyComponent();SpellDebrisBodies.RemoveAtSwap(Index);SpellDebris.RemoveAtSwap(Index);continue;
        }
        FVector Position=Piece.Origin+Piece.Velocity*Age;Position.Z=FMath::Max(8.,Position.Z-Piece.Gravity*Age*Age*.5);
        FVector Direction=Piece.Velocity;Direction.Z-=Piece.Gravity*Age;
        FQuat Rotation=ProjectileMeshOrientation("bullet_burst",Direction,0.);
        if (Piece.Meteor) Rotation=Rotation*FQuat(FVector::ForwardVector,float(Age*Piece.Spin));
        const float Fade=FMath::Clamp(float((Piece.Duration-Age)/.065),0.f,1.f);
        Body->SetWorldTransform(FTransform(Rotation,Position,FVector(Piece.Scale*Fade)));
    }
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
            if (Target) Source->AimAt(Target->ImpactLocation());
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
    else if (Event.type=="projectile_launch")
    {
        const FVector SourceXY=Source?FVector(Source->GetActorLocation().X,Source->GetActorLocation().Y,0.):FVector::ZeroVector;
        const FVector Origin=Source?URiftMatchSubsystem::WorldPoint(Event.position)+Source->AttackLocation()-SourceXY:
            URiftMatchSubsystem::WorldPoint(Event.position,135);
        // Events intentionally do not carry projectile IDs. A fast simulation
        // frame can drain several fixed steps, so an already-resolved launch
        // must not overwrite a newer shot's muzzle. Match the recorded deadline
        // within the one-step creation/update interval, also valid for replay.
        if (State)
        {
            uint64 Shot=0;double BestDeadlineError=.035;
            for (const auto& Projectile:State->projectiles)
                if (Projectile.source==Event.source && Projectile.target==Event.target && Projectile.cardId==Event.cardId)
                {
                    const double Error=FMath::Abs(State->elapsed+Projectile.remaining-Event.until);
                    if (Error<BestDeadlineError) {Shot=Projectile.id;BestDeadlineError=Error;}
                }
            if (Shot) ProjectileOrigins.Add(Shot,Origin);
        }
        auto* Flash=SpawnEffect(TEXT("Impact"),Origin,Event.team,28);
        if (Flash) Flash->SetWorldRotation((Position-Origin).Rotation());
        SetEffectParameters(Flash,Event.team,28,Origin,Position,EffectColor(FlightFor(Event.cardId),Event.team));
    }
    else if (Event.type=="damage")
    {
        if (Target && Event.amount>0) Target->HitAt(Event.time,Event.amount);
        const bool ArcPrimary=Event.cardId=="arc_mage" && Event.damageKind=="attack";
        const FName FX=Event.damageKind=="dot"?TEXT("MeteorTick"):Event.cardId=="frost_fang"?TEXT("Frost"):ArcPrimary?TEXT("Nova"):TEXT("Impact");
        const float Radius=ArcPrimary?155.f:Event.damageKind=="splash"?85.f:Event.cardId=="storm_raven"?80.f:Event.cardId=="sky_manta"?62.f:55.f;
        auto* Impact=SpawnEffect(FX,Position,Event.team,Radius);
        SetEffectParameters(Impact,Event.team,Radius,Position,Position,EffectColor(FlightFor(Event.cardId),Event.team));
        if (Event.damageKind!="initial" && Event.damageKind!="aura" && Event.damageKind!="dot")
        {
            // Contact has a material identity. Structures keep their stone/wood
            // impact; teeth, blunt strikes and elemental hits use matching cues.
            // Shared attack/contact cues are aggregated within the same tick
            // so one authoritative strike cannot produce duplicate loud sounds.
            const FName Sound=Event.targetKind==rift::EntityKind::Guard||Event.targetKind==rift::EntityKind::Core?TEXT("tower_hit"):
                Event.targetKind==rift::EntityKind::Building?TEXT("building_hit"):
                Event.cardId=="vampire_bats"?TEXT("bat_bite"):
                Event.cardId=="rambeast"||Event.cardId=="boulderback"?TEXT("heavy_slam"):
                Event.cardId=="frost_fang"?TEXT("frost_attack"):
                Event.cardId=="storm_raven"?TEXT("storm_bolt"):
                Event.cardId=="ember_archer"||Event.cardId=="archer_tower"?TEXT("arrow_hit"):
                Event.cardId=="arc_mage"?TEXT("arc_impact"):Event.cardId=="sky_manta"?TEXT("manta_impact"):
                TEXT("sword_hit");
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
            SpawnSpellDebris(Event);
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
int32 ARiftArenaPresentation::TrainingOverlayLineCount()const{return Overlay?Overlay->TrainingLineCount():0;}
int32 ARiftArenaPresentation::TrainingOverlayLabelCount()const{return Overlay?Overlay->TrainingLabelCount():0;}
