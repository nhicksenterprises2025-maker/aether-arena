#include "Presentation/RiftArenaPresentation.h"
#include "RiftDiagnostics.h"
#include "Presentation/RiftBattleAudioSubsystem.h"
#include "Presentation/RiftBattleOverlay.h"
#include "Presentation/RiftArenaGeometry.h"
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
#include "Components/SkeletalMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
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
bool CorpseVisualExpired(const rift::Entity& Entity,const URiftCardData* Card,double Time)
{
    if(!Entity.dead||Entity.kind==rift::EntityKind::Core||Entity.kind==rift::EntityKind::Guard)return false;
    const bool Authored=Card&&Card->DeathCleanupSeconds>0;
    const double Cleanup=Authored?FMath::Clamp(double(Card->DeathCleanupSeconds),.1,1.05):1.05;
    return Authored?Time-Entity.died>=Cleanup:Time-Entity.died>Cleanup;
}
FName FlightFor(const std::string& Card)
{
    if (Card=="meteor_shards") return TEXT("Meteor");
    if (Card=="bullet_burst") return TEXT("BulletBurst");
    if (Card=="ember_archer" || Card=="archer_tower" || Card=="dart_gun") return TEXT("ArrowFlight");
    if (Card=="arc_mage") return TEXT("ArcFlight");
    if (Card=="sky_manta") return TEXT("MantaFlight");
    if (Card=="storm_raven") return TEXT("StormFlight");
    if (Card=="pulverizer") return TEXT("Nova");
    return TEXT("TowerFlight");
}
FName MissileFor(const std::string& Card)
{
    if (Card=="dart_gun") return TEXT("dart_projectile");
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
    // New missiles are authored along +X; brass rounds and meteor shards have
    // their tip along +Z. Apply the local correction after aiming the flight.
    if (MissileFor(Card)==TEXT("bullet_round") || MissileFor(Card)==TEXT("meteor_shard"))
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
    if (Card=="dart_gun") return Position; // Pressure-fired darts travel directly to the committed target.
    const double Distance=FVector::Dist2D(Source,Target);
    const double Envelope=FMath::Sin(T*UE_PI);
    const FVector Side=FVector::CrossProduct((Target-Source).GetSafeNormal(),FVector::UpVector).GetSafeNormal();
    if (Card=="ember_archer" || Card=="archer_tower")
        Position.Z+=Envelope*FMath::Clamp(Distance*.12,24.*RiftArenaGeometry::WorldScale,90.*RiftArenaGeometry::WorldScale);
    else if (Card=="arc_mage")
    {
        Position.Z+=Envelope*FMath::Clamp(Distance*.08,18.*RiftArenaGeometry::WorldScale,64.*RiftArenaGeometry::WorldScale);
        Position+=Side*(Envelope*FMath::Sin(T*UE_PI*4.)*9.*RiftArenaGeometry::WorldScale);
    }
    else if (Card=="sky_manta")
    {
        Position.Z+=Envelope*38.*RiftArenaGeometry::WorldScale;
        Position+=Side*(Envelope*FMath::Sin(T*UE_PI*2.)*14.*RiftArenaGeometry::WorldScale);
    }
    else if (Card=="storm_raven")
    {
        Position.Z+=Envelope*22.*RiftArenaGeometry::WorldScale;
        Position+=Side*(Envelope*FMath::Sin(T*UE_PI*6.)*12.*RiftArenaGeometry::WorldScale);
    }
    else Position.Z+=Envelope*FMath::Clamp(Distance*.05,12.*RiftArenaGeometry::WorldScale,42.*RiftArenaGeometry::WorldScale);
    return Position;
}
FVector ARiftArenaPresentation::SpellCastPathPoint(const std::string& Card,FVector Target,rift::Team Team,int32 Index,int32 Count,double Progress)
{
    const double T=FMath::Clamp(Progress,0.,1.);
    const bool Meteor=Card=="meteor_shards";
    const double Angle=UE_TWO_PI*double(Index)/FMath::Max(1,Count)+.35;
    const double Spread=Meteor?(Index?320.:0.):(Index?155.:0.);
    const FVector Landing=Target+FVector(FMath::Cos(Angle)*Spread,FMath::Sin(Angle)*Spread,12.)*RiftArenaGeometry::WorldScale;
    const double Side=Team==rift::Team::Player?1.:-1.;
    FVector Start=Landing+FVector(Meteor?-130.:65.,Side*(Meteor?220.:300.),Meteor?505.-Index*14.:350.+Index*8.)*RiftArenaGeometry::WorldScale;
    // Edge casts keep their incoming models inside the camera's arena margin.
    const double EntryHalfWidth=(rift::arena::HalfWidth+2.)*RiftArenaGeometry::UnitsPerTile;
    const double EntryHalfHeight=(rift::arena::HalfHeight+2.)*RiftArenaGeometry::UnitsPerTile;
    Start.X=FMath::Clamp(Start.X,-EntryHalfWidth,EntryHalfWidth);Start.Y=FMath::Clamp(Start.Y,-EntryHalfHeight,EntryHalfHeight);
    // Every visible round reaches the selected ground point at the same
    // authoritative deadline. Staggered bullet releases do not imply staggered
    // damage or follow a target after the player has committed the cast.
    const double Release=Meteor?0.:double(Index)/FMath::Max(1,Count-1)*.4;
    const double Flight=FMath::Clamp((T-Release)/(1.-Release),0.,1.);
    return FMath::Lerp(Start,Landing,Meteor?Flight*Flight:Flight);
}
ARiftArenaPresentation::ARiftArenaPresentation()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;
    Scene=CreateDefaultSubobject<USceneComponent>(TEXT("AuthoredArenaRoot"));SetRootComponent(Scene);Scene->SetMobility(EComponentMobility::Static);
    auto* Sun=CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("AfternoonKey"));Sun->SetupAttachment(Scene);
    Sun->SetMobility(EComponentMobility::Movable);Sun->LightSourceAngle=3.5f;Sun->SetForwardShadingPriority(1);
    Sun->SetRelativeRotation(FRotator(-52,-24,0));Sun->SetIntensity(5.7f);
    Sun->SetLightColor(FLinearColor(1.f,.94f,.84f));Sun->CastShadows=true;
    auto* Fill=CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("EquipmentFill"));Fill->SetupAttachment(Scene);
    Fill->SetMobility(EComponentMobility::Movable);Fill->SetRelativeRotation(FRotator(-58,156,0));
    Fill->SetIntensity(1.8f);Fill->SetLightColor(FLinearColor(.68f,.79f,.94f));Fill->CastShadows=false;
    Fill->bAtmosphereSunLight=false;
    Fill->SetForwardShadingPriority(0);
    auto* Sky=CreateDefaultSubobject<USkyLightComponent>(TEXT("SoftSkyFill"));Sky->SetupAttachment(Scene);
    Sky->SetMobility(EComponentMobility::Movable);
    Sky->SetIntensity(1.7f);Sky->SetLightColor(FLinearColor(.77f,.84f,.96f));Sky->bRealTimeCapture=true;
    auto* Atmosphere=CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("RiftSky"));Atmosphere->SetupAttachment(Scene);
    auto* Fog=CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("DistantIslandHaze"));Fog->SetupAttachment(Scene);
    Fog->SetFogDensity(.008f);Fog->SetFogInscatteringColor(FLinearColor(.12f,.20f,.27f));Fog->SetStartDistance(3800.f);
    auto* Post=CreateDefaultSubobject<UPostProcessComponent>(TEXT("BattlefieldGrade"));Post->SetupAttachment(Scene);Post->bUnbound=true;
    Post->Settings.bOverride_BloomIntensity=true;Post->Settings.BloomIntensity=.18f;
    // Spell flashes must not make the entire painted arena breathe in exposure.
    Post->Settings.bOverride_AutoExposureMinBrightness=true;Post->Settings.AutoExposureMinBrightness=1.f;
    Post->Settings.bOverride_AutoExposureMaxBrightness=true;Post->Settings.AutoExposureMaxBrightness=1.f;
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
    {
        if(auto* ArenaMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Rift/Materials/M_RiftArena.M_RiftArena")))
            for(int32 Index=0;Index<Component->GetNumMaterials();++Index)Component->SetMaterial(Index,ArenaMaterial);
        else RIFT_LOG(LogRift,Error,TEXT("Arena grid material is missing; run the targeted RiftArenaOnly import."));
        for (int32 Index=0;Index<Component->GetNumMaterials();++Index)
            if (auto* Dynamic=Component->CreateDynamicMaterialInstance(Index))
            {Dynamic->SetScalarParameterValue(TEXT("RiftGridStrength"),.25f);Dynamic->SetScalarParameterValue(TEXT("RiftBaseColorGain"),1.12f);Dynamic->SetScalarParameterValue(TEXT("RiftTileSize"),RiftArenaGeometry::UnitsPerTile);}
    }
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
{Position.Z*=RiftArenaGeometry::WorldScale;if (auto* Component=Instances(Mesh,Team)) Component->AddInstance(FTransform(Rotation,Position,Scale*RiftArenaGeometry::WorldScale),false);}
void ARiftArenaPresentation::ConstructArena()
{
    FRandomStream Random(150151);
    constexpr float Tile=RiftArenaGeometry::UnitsPerTile;
    constexpr float HalfWidth=rift::arena::HalfWidth*Tile,HalfHeight=rift::arena::HalfHeight*Tile;
    constexpr float RiverHalfWidth=rift::arena::RiverHalfWidth*Tile,BridgeX=rift::arena::BridgeCenterX*Tile;
    // The playable extent, tile centers, river and bridge openings are identical
    // to the simulation. Decoration stays outside deployment and navigation.
    for (int32 X=0;X<rift::arena::Width;++X) for (int32 Y=0;Y<rift::arena::Height;++Y)
    {
        const float PX=(X-rift::arena::LastTileX)*Tile, PY=(Y-rift::arena::LastTileZ)*Tile;
        if (FMath::Abs(PY)<RiverHalfWidth) continue;
        Place(TEXT("floor_tile"),FVector(PX,PY,0),FRotator(0,90*(X%4),0));
    }
    // Place exactly two columns about each authoritative bridge axis. Picking
    // nearby floor cells would become three columns at a fractional lane axis.
    for(int32 Y=0;Y<rift::arena::Height;++Y)
    {
        const float PY=(Y-rift::arena::LastTileZ)*Tile;
        if(FMath::Abs(PY)<RiverHalfWidth||FMath::Abs(PY)>=(rift::arena::GuardDepth+.78)*Tile||Y%2!=0)continue;
        for(float Lane:{-1.f,1.f})for(float Column:{-.5f,.5f})
            Place(TEXT("lane_paver"),FVector(Lane*BridgeX+Column*Tile,PY,1),FRotator(0,Random.FRandRange(-8,8),0));
    }
    for (int32 X=-RiftArenaGeometry::GroundHalfWidth;X<RiftArenaGeometry::GroundHalfWidth;++X)
        for (int32 Y=-RiftArenaGeometry::GroundHalfHeight;Y<RiftArenaGeometry::GroundHalfHeight;++Y)
    {
        const float PX=(X+.5f)*Tile,PY=(Y+.5f)*Tile;
        if (FMath::Abs(PX)<HalfWidth && FMath::Abs(PY)<HalfHeight) continue;
        if (FMath::Abs(PY)<RiverHalfWidth) continue;
        Place(TEXT("floor_tile"),FVector(PX,PY,-18),FRotator(0,90*((X+24)%4),0));
    }
    // Authored stone/wood bridges use local +X forward after FBX conversion.
    for (float X:{-BridgeX,BridgeX}) Place(TEXT("bridge"),FVector(X,0,-16),FRotator(0,90,0));
    const int32 BankCount=FMath::CeilToInt(rift::arena::Width/2.f);
    const int32 SideStoneCount=FMath::CeilToInt(rift::arena::Height*Tile/(140.f*RiftArenaGeometry::WorldScale));
    const int32 EndStoneCount=FMath::CeilToInt(rift::arena::Width*Tile/(150.f*RiftArenaGeometry::WorldScale));
    const int32 ShrubCount=FMath::CeilToInt(rift::arena::Height*Tile/(175.f*RiftArenaGeometry::WorldScale));
    for (int32 Side:{-1,1})
    {
        for (int32 Index=0;Index<BankCount;++Index)
        {
            const float X=(Index-(BankCount-1)*.5f)*200*RiftArenaGeometry::WorldScale;
            const float BridgeOpening=rift::arena::BridgeWidth*Tile*.5f+40.f*RiftArenaGeometry::WorldScale;
            if (FMath::Abs(X-BridgeX)<BridgeOpening || FMath::Abs(X+BridgeX)<BridgeOpening) continue;
            Place(TEXT("bank_segment"),FVector(X,Side*(RiverHalfWidth+10.f*RiftArenaGeometry::WorldScale),-40),FRotator(0,90,0));
        }
        for (int32 Index=0;Index<SideStoneCount;++Index)
            Place(TEXT("boundary_stone"),FVector(Side*(HalfWidth+60.f*RiftArenaGeometry::WorldScale),(Index-(SideStoneCount-1)*.5f)*140*RiftArenaGeometry::WorldScale,-15),FRotator(0,0,0),FVector(1.1));
        for (int32 Index=0;Index<EndStoneCount;++Index)
            Place(TEXT("boundary_stone"),FVector((Index-(EndStoneCount-1)*.5f)*150*RiftArenaGeometry::WorldScale,Side*(HalfHeight+60.f*RiftArenaGeometry::WorldScale),-15),FRotator(0,90,0),FVector(1.1));
        for (int32 Index=0;Index<ShrubCount;++Index)
        {
            const float Y=(Index-(ShrubCount-1)*.5f)*175*RiftArenaGeometry::WorldScale;
            Place(TEXT("shrub"),FVector(Side*(HalfWidth+140.f*RiftArenaGeometry::WorldScale)+Random.FRandRange(-35,35)*RiftArenaGeometry::WorldScale,Y,0),FRotator(0,Random.FRandRange(0,360),0),FVector(Random.FRandRange(.7,1.3)));
            Place(TEXT("grass_tuft"),FVector(Side*(HalfWidth+35.f*RiftArenaGeometry::WorldScale),Y+45*RiftArenaGeometry::WorldScale,5),FRotator(0,Random.FRandRange(0,360),0),FVector(1.4));
            if (Index%4==1) Place(TEXT("tree"),FVector(Side*(HalfWidth+350.f*RiftArenaGeometry::WorldScale),Y+70*RiftArenaGeometry::WorldScale,-20),FRotator(0,Random.FRandRange(0,360),0),FVector(Random.FRandRange(.95,1.35)));
            if(Index%3==0)for(int32 Clump=0;Clump<3;++Clump)
                Place(TEXT("grass_tuft"),FVector(Side*(HalfWidth+(155.f+Clump*32.f)*RiftArenaGeometry::WorldScale),Y+Random.FRandRange(-55,55)*RiftArenaGeometry::WorldScale,2),
                    FRotator(0,Random.FRandRange(0,360),0),FVector(Random.FRandRange(.65f,1.15f)));
        }
        Place(TEXT("ruin"),FVector(Side*(HalfWidth+440.f*RiftArenaGeometry::WorldScale),Side*(rift::arena::GuardDepth*Tile+180.f*RiftArenaGeometry::WorldScale),-25),FRotator(0,Side*32,0),FVector(1.5));
        for (float X:{-(HalfWidth+145.f*RiftArenaGeometry::WorldScale),HalfWidth+145.f*RiftArenaGeometry::WorldScale})
        {
            Place(TEXT("banner"),FVector(X,Side*(rift::arena::CoreDepth*Tile+230.f*RiftArenaGeometry::WorldScale),0),FRotator(0,Side*90,0),FVector(1.05),Side==1?0:1);
            Place(TEXT("crystal_plinth"),FVector(X,Side*900.f*RiftArenaGeometry::WorldScale,0),FRotator::ZeroRotator,FVector(.8),Side==1?0:1);
        }
    }
    // The authored eroded island supports the field rather than a stock cube.
    Place(TEXT("distant_island"),FVector(0,0,-125),FRotator::ZeroRotator,FVector(RiftArenaGeometry::GroundHalfWidth*10.5f/19.f,RiftArenaGeometry::GroundHalfHeight*14.f/24.f,3.8));
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
        Water->AddInstance(FTransform(FRotator::ZeroRotator,FVector(0,0,-22*RiftArenaGeometry::WorldScale),FVector(rift::arena::Width,rift::arena::RiverHalfWidth*2.,.05)*RiftArenaGeometry::WorldScale),false);
    }
}
FString ARiftArenaPresentation::GeometryDiagnosticsJSON() const
{
    auto Report=MakeShared<FJsonObject>();
    constexpr double Tile=RiftArenaGeometry::UnitsPerTile;
    auto Component=[&](const TCHAR* Key)->const UHierarchicalInstancedStaticMeshComponent*
    {const auto* Found=Environment.Find(FName(Key));return Found?Found->Get():nullptr;};
    const auto* Floor=Component(TEXT("floor_tile_-1"));
    const auto* Water=Component(TEXT("floor_tile_9"));
    const auto* Bridges=Component(TEXT("bridge_-1"));
    const auto* Border=Component(TEXT("boundary_stone_-1"));
    const auto* Pavers=Component(TEXT("lane_paver_-1"));
    bool FloorPassed=Floor&&Floor->GetCollisionEnabled()==ECollisionEnabled::NoCollision;
    int32 PlayableCount=0,DecorativeCount=0,ExpectedCount=0;TSet<FIntPoint> Cells;
    FBox2D PlayableCenters(ForceInit),GroundCenters(ForceInit);
    for(int32 X=0;X<rift::arena::Width;++X)for(int32 Z=0;Z<rift::arena::Height;++Z)
        if(FMath::Abs(Z-rift::arena::LastTileZ)>=rift::arena::RiverHalfWidth)++ExpectedCount;
    if(Floor)for(int32 Index=0;Index<Floor->GetInstanceCount();++Index)
    {
        FTransform Transform;if(!Floor->GetInstanceTransform(Index,Transform,true)){FloorPassed=false;continue;}
        const FVector Location=Transform.GetLocation();const FVector2D Center(Location.X/Tile,Location.Y/Tile);
        GroundCenters+=Center;
        FloorPassed=FloorPassed&&Transform.GetScale3D().Equals(FVector(RiftArenaGeometry::WorldScale),.001);
        if(FMath::IsNearlyEqual(Location.Z,-18.*RiftArenaGeometry::WorldScale,.01))
        {++DecorativeCount;FloorPassed=FloorPassed&&(FMath::Abs(Center.X)>=rift::arena::HalfWidth||FMath::Abs(Center.Y)>=rift::arena::HalfHeight);continue;}
        ++PlayableCount;PlayableCenters+=Center;
        const int32 X=FMath::RoundToInt(Center.X+rift::arena::LastTileX),Z=FMath::RoundToInt(Center.Y+rift::arena::LastTileZ);
        const FIntPoint Cell(X,Z);
        FloorPassed=FloorPassed&&FMath::IsNearlyZero(Location.Z,.01)&&X>=0&&X<rift::arena::Width&&Z>=0&&Z<rift::arena::Height
            &&FMath::IsNearlyEqual(Center.X,X-rift::arena::LastTileX,.001)&&FMath::IsNearlyEqual(Center.Y,Z-rift::arena::LastTileZ,.001)
            &&FMath::Abs(Center.Y)>=rift::arena::RiverHalfWidth&&!Cells.Contains(Cell);
        Cells.Add(Cell);
    }
    const int32 RiverRows=rift::arena::Height-ExpectedCount/rift::arena::Width;
    const int32 ExpectedDecorativeCount=RiftArenaGeometry::GroundHalfWidth*2*(RiftArenaGeometry::GroundHalfHeight*2-RiverRows)-ExpectedCount;
    FloorPassed=FloorPassed&&PlayableCount==ExpectedCount&&Cells.Num()==ExpectedCount&&DecorativeCount==ExpectedDecorativeCount
        &&PlayableCenters.bIsValid&&GroundCenters.bIsValid;
    if(PlayableCenters.bIsValid)
        FloorPassed=FloorPassed&&PlayableCenters.Min.Equals(FVector2D(-rift::arena::LastTileX,-rift::arena::LastTileZ),.001)
            &&PlayableCenters.Max.Equals(FVector2D(rift::arena::LastTileX,rift::arena::LastTileZ),.001);
    if(GroundCenters.bIsValid)
        FloorPassed=FloorPassed&&GroundCenters.Min.Equals(FVector2D(-RiftArenaGeometry::GroundHalfWidth+.5,-RiftArenaGeometry::GroundHalfHeight+.5),.001)
            &&GroundCenters.Max.Equals(FVector2D(RiftArenaGeometry::GroundHalfWidth-.5,RiftArenaGeometry::GroundHalfHeight-.5),.001);
    Report->SetNumberField(TEXT("schemaVersion"),1);Report->SetStringField(TEXT("inspection"),TEXT("Actual production HISM instance transforms and collision modes"));
    Report->SetNumberField(TEXT("widthTiles"),rift::arena::Width);Report->SetNumberField(TEXT("heightTiles"),rift::arena::Height);
    Report->SetNumberField(TEXT("worldUnitsPerTile"),Tile);Report->SetNumberField(TEXT("authoredModelScale"),RiftArenaGeometry::WorldScale);
    bool GridPassed=false;
    if(Floor)if(auto* Dynamic=Cast<UMaterialInstanceDynamic>(Floor->GetMaterial(0)))
    {float Value=0;GridPassed=Dynamic->GetScalarParameterValue(FMaterialParameterInfo(TEXT("RiftTileSize")),Value)&&FMath::IsNearlyEqual(Value,RiftArenaGeometry::UnitsPerTile,.001f);Report->SetNumberField(TEXT("gridShaderTileSize"),Value);}
    Report->SetBoolField(TEXT("gridShaderPassed"),GridPassed);
    Report->SetNumberField(TEXT("playableFloorTiles"),PlayableCount);Report->SetNumberField(TEXT("expectedPlayableFloorTiles"),ExpectedCount);
    Report->SetNumberField(TEXT("decorativeFloorTiles"),DecorativeCount);Report->SetNumberField(TEXT("expectedDecorativeFloorTiles"),ExpectedDecorativeCount);
    Report->SetBoolField(TEXT("floorPassed"),FloorPassed);
    auto Bounds=[&](const TCHAR* Name,const FBox2D& Box)
    {
        auto Value=MakeShared<FJsonObject>();Value->SetBoolField(TEXT("valid"),Box.bIsValid);
        if(Box.bIsValid){Value->SetNumberField(TEXT("minX"),Box.Min.X);Value->SetNumberField(TEXT("maxX"),Box.Max.X);
            Value->SetNumberField(TEXT("minZ"),Box.Min.Y);Value->SetNumberField(TEXT("maxZ"),Box.Max.Y);}
        Report->SetObjectField(Name,Value);
    };
    Bounds(TEXT("playableFloorCenters"),PlayableCenters);Bounds(TEXT("decorativeGroundCenters"),GroundCenters);
    FTransform WaterTransform;
    bool WaterPassed=Water&&Water->GetInstanceCount()==1&&Water->GetCollisionEnabled()==ECollisionEnabled::NoCollision
        &&Water->GetInstanceTransform(0,WaterTransform,true);
    if(WaterPassed)
    {
        const FVector Scale=WaterTransform.GetScale3D(),Location=WaterTransform.GetLocation();
        Report->SetNumberField(TEXT("riverWidthTiles"),Scale.X/RiftArenaGeometry::WorldScale);Report->SetNumberField(TEXT("riverDepthTiles"),Scale.Y/RiftArenaGeometry::WorldScale);
        WaterPassed=FMath::IsNearlyEqual(Scale.X,double(rift::arena::Width)*RiftArenaGeometry::WorldScale,.001)
            &&FMath::IsNearlyEqual(Scale.Y,rift::arena::RiverHalfWidth*2.*RiftArenaGeometry::WorldScale,.001)&&Location.Equals(FVector(0,0,-22*RiftArenaGeometry::WorldScale),.01);
    }
    Report->SetBoolField(TEXT("riverPassed"),WaterPassed);
    TArray<TSharedPtr<FJsonValue>> BridgeRows;TSet<int32> Sides;
    bool BridgesPassed=Bridges&&Bridges->GetInstanceCount()==2&&Bridges->GetCollisionEnabled()==ECollisionEnabled::NoCollision;
    if(Bridges)for(int32 Index=0;Index<Bridges->GetInstanceCount();++Index)
    {
        FTransform Transform;if(!Bridges->GetInstanceTransform(Index,Transform,true)){BridgesPassed=false;continue;}
        const FVector Position=Transform.GetLocation();const int32 Side=Position.X<0?-1:1;auto Row=MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("x"),Position.X/Tile);Row->SetNumberField(TEXT("z"),Position.Y/Tile);BridgeRows.Add(MakeShared<FJsonValueObject>(Row));
        BridgesPassed=BridgesPassed&&!Sides.Contains(Side)&&FMath::IsNearlyEqual(Position.X,Side*rift::arena::BridgeCenterX*Tile,.01)
            &&FMath::IsNearlyZero(Position.Y,.01)&&Transform.GetScale3D().Equals(FVector(RiftArenaGeometry::WorldScale),.001);Sides.Add(Side);
    }
    Report->SetArrayField(TEXT("bridges"),BridgeRows);Report->SetBoolField(TEXT("bridgesPassed"),BridgesPassed);
    // Inspect the actual stone instances separately on all four approaches.
    // Their two-column midpoint must agree with each bridge and Guard body.
    TMap<FIntPoint,int32> ExpectedPaverGroups;int32 ExpectedPaverCount=0;
    for(int32 Z=0;Z<rift::arena::Height;++Z)
    {
        const float PZ=(Z-rift::arena::LastTileZ)*Tile;
        if(FMath::Abs(PZ)>=rift::arena::RiverHalfWidth*Tile&&FMath::Abs(PZ)<(rift::arena::GuardDepth+.78)*Tile&&Z%2==0)
            for(int32 Lane:{-1,1}){ExpectedPaverCount+=2;ExpectedPaverGroups.FindOrAdd(FIntPoint(Lane,PZ<0?-1:1))+=2;}
    }
    TMap<FIntPoint,TMap<int32,TArray<double>>> PaverGroups;TArray<TSharedPtr<FJsonValue>> PaverRows,PathRows;
    bool PathsPassed=Pavers&&Pavers->GetCollisionEnabled()==ECollisionEnabled::NoCollision&&Pavers->GetInstanceCount()==ExpectedPaverCount;
    if(Pavers)for(int32 Index=0;Index<Pavers->GetInstanceCount();++Index)
    {
        FTransform Transform;if(!Pavers->GetInstanceTransform(Index,Transform,true)){PathsPassed=false;continue;}
        const FVector Position=Transform.GetLocation();const double X=Position.X/Tile,Z=Position.Y/Tile;
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("x"),X);Row->SetNumberField(TEXT("z"),Z);PaverRows.Add(MakeShared<FJsonValueObject>(Row));
        PathsPassed=PathsPassed&&FMath::IsNearlyEqual(Position.Z,double(RiftArenaGeometry::WorldScale),.01);
        PaverGroups.FindOrAdd(FIntPoint(X<0?-1:1,Z<0?-1:1)).FindOrAdd(FMath::RoundToInt(Z*1000.)).Add(X);
    }
    for(int32 Lane:{-1,1})for(int32 Home:{-1,1})
    {
        const FIntPoint Key(Lane,Home);const auto* Rows=PaverGroups.Find(Key);int32 Count=0;double Min=1.e9,Max=-1.e9;
        bool Aligned=Rows&&Rows->Num()>0;if(Rows)for(const auto& Entry:*Rows)
        {
            double RowMin=1.e9,RowMax=-1.e9;for(double X:Entry.Value){RowMin=FMath::Min(RowMin,X);RowMax=FMath::Max(RowMax,X);}
            Aligned=Aligned&&Entry.Value.Num()==2&&FMath::IsNearlyEqual((RowMin+RowMax)*.5,Lane*rift::arena::BridgeCenterX,1.e-6)
                &&FMath::IsNearlyEqual(RowMax-RowMin,1.,1.e-6);
            Count+=Entry.Value.Num();Min=FMath::Min(Min,RowMin);Max=FMath::Max(Max,RowMax);
        }
        Aligned=Aligned&&Count==ExpectedPaverGroups.FindRef(Key);PathsPassed=PathsPassed&&Aligned;
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("lane"),Lane);Row->SetStringField(TEXT("team"),Home>0?TEXT("player"):TEXT("enemy"));
        Row->SetNumberField(TEXT("centerX"),Count?(Min+Max)*.5:0);Row->SetNumberField(TEXT("minimumX"),Count?Min:0);Row->SetNumberField(TEXT("maximumX"),Count?Max:0);
        Row->SetNumberField(TEXT("pavers"),Count);Row->SetNumberField(TEXT("rows"),Rows?Rows->Num():0);Row->SetBoolField(TEXT("alignmentPassed"),Aligned);PathRows.Add(MakeShared<FJsonValueObject>(Row));
    }
    Report->SetArrayField(TEXT("lanePavers"),PaverRows);Report->SetArrayField(TEXT("lanePaths"),PathRows);Report->SetBoolField(TEXT("lanePathsPassed"),PathsPassed);
    Report->SetNumberField(TEXT("lanePaverCount"),PaverRows.Num());Report->SetNumberField(TEXT("expectedLanePaverCount"),ExpectedPaverCount);
    TArray<TSharedPtr<FJsonValue>> TowerRows;TSet<FIntPoint> TowerSlots;int32 GuardsAligned=0,CoresCentered=0;
    const auto* State=ViewState();bool TowersPassed=State!=nullptr;
    if(State)for(const auto& Tower:State->entities)if(Tower.kind==rift::EntityKind::Guard||Tower.kind==rift::EntityKind::Core)
    {
        const bool Core=Tower.kind==rift::EntityKind::Core;const int32 Home=Tower.team==rift::Team::Player?1:-1;
        const double ExpectedX=Core?0.:Tower.lane*rift::arena::GuardX,ExpectedZ=Home*(Core?rift::arena::CoreDepth:rift::arena::GuardDepth);
        const auto* Actor=Visual(Tower.id);const FVector Position=Actor?Actor->GetActorLocation():FVector::ZeroVector;
        const FIntPoint Slot(int32(Tower.team),Tower.lane);
        const bool VisualAligned=Actor?(FMath::IsNearlyEqual(Position.X/Tile,ExpectedX,1.e-6)&&FMath::IsNearlyEqual(Position.Y/Tile,ExpectedZ,1.e-6)):Tower.dead;
        const bool Aligned=VisualAligned&&!TowerSlots.Contains(Slot)&&FMath::IsNearlyEqual(Tower.position.x,ExpectedX,1.e-9)
            &&FMath::IsNearlyEqual(Tower.position.z,ExpectedZ,1.e-9)&&(Core?Tower.lane==0:FMath::Abs(Tower.lane)==1);
        TowerSlots.Add(Slot);TowersPassed=TowersPassed&&Aligned;if(Aligned){if(Core)++CoresCentered;else ++GuardsAligned;}
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("id"),Tower.id);Row->SetStringField(TEXT("team"),Home>0?TEXT("player"):TEXT("enemy"));
        Row->SetStringField(TEXT("kind"),Core?TEXT("core"):TEXT("guard"));Row->SetBoolField(TEXT("dead"),Tower.dead);Row->SetNumberField(TEXT("lane"),Tower.lane);Row->SetNumberField(TEXT("x"),Tower.position.x);Row->SetNumberField(TEXT("z"),Tower.position.z);
        Row->SetNumberField(TEXT("visualX"),Position.X/Tile);Row->SetNumberField(TEXT("visualZ"),Position.Y/Tile);Row->SetBoolField(TEXT("visualAvailable"),Actor!=nullptr);Row->SetBoolField(TEXT("alignmentPassed"),Aligned);TowerRows.Add(MakeShared<FJsonValueObject>(Row));
    }
    TowersPassed=TowersPassed&&GuardsAligned==4&&CoresCentered==2&&TowerSlots.Num()==6;
    Report->SetArrayField(TEXT("towers"),TowerRows);Report->SetBoolField(TEXT("towerSnapshotAvailable"),State!=nullptr);Report->SetBoolField(TEXT("towerAlignmentPassed"),TowersPassed);
    Report->SetNumberField(TEXT("guardsAligned"),GuardsAligned);Report->SetNumberField(TEXT("coresCentered"),CoresCentered);
    const int32 ExpectedBorderCount=2*(FMath::CeilToInt(rift::arena::Height*Tile/(140.*RiftArenaGeometry::WorldScale))+FMath::CeilToInt(rift::arena::Width*Tile/(150.*RiftArenaGeometry::WorldScale)));
    bool BorderPassed=Border&&Border->GetInstanceCount()==ExpectedBorderCount&&Border->GetCollisionEnabled()==ECollisionEnabled::NoCollision;
    if(Border)for(int32 Index=0;Index<Border->GetInstanceCount();++Index)
    {
        FTransform Transform;if(!Border->GetInstanceTransform(Index,Transform,true)){BorderPassed=false;continue;}
        const FVector Position=Transform.GetLocation();
        BorderPassed=BorderPassed&&(FMath::Abs(Position.X)>=rift::arena::HalfWidth*Tile||FMath::Abs(Position.Y)>=rift::arena::HalfHeight*Tile);
    }
    Report->SetNumberField(TEXT("borderStones"),Border?Border->GetInstanceCount():0);Report->SetBoolField(TEXT("borderPassed"),BorderPassed);
    Report->SetBoolField(TEXT("passed"),GridPassed&&FloorPassed&&WaterPassed&&BridgesPassed&&BorderPassed&&PathsPassed&&(!State||TowersPassed));
    FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));return Text;
}
void ARiftArenaPresentation::ClearVisuals()
{
    for (auto& Entry:Units) if (Entry.Value) Entry.Value->Destroy();Units.Reset();
    for (auto& Entry:Projectiles) if (Entry.Value) Entry.Value->DestroyComponent();Projectiles.Reset();ProjectileOrigins.Reset();
    for (auto& Entry:ProjectileBodies) if (Entry.Value) Entry.Value->DestroyComponent();ProjectileBodies.Reset();
    for (auto& Entry:ProjectileTrails) if (Entry.Value) Entry.Value->DestroyComponent();ProjectileTrails.Reset();
    for (auto& Body:SpellDebrisBodies) if (Body) Body->DestroyComponent();SpellDebrisBodies.Reset();SpellDebris.Reset();
    for (auto& Body:SpellCastBodies) if (Body) Body->DestroyComponent();SpellCastBodies.Reset();SpellCastPieces.Reset();
    for (auto& Glow:SpellCastGlows) if (Glow) Glow->DestroyComponent();SpellCastGlows.Reset();
    for (auto& Ring:SpellCastRings) if (Ring.Value) Ring.Value->DestroyComponent();SpellCastRings.Reset();SpellCastMaterials.Reset();
    SpellImpactEffects.Reset();RecentSpellImpacts.Reset();RecentLightningHits.Reset();SpellImpactEvents=0;
    RecentReaperAttacks.Reset();RecentCloakExpiries.Reset();ContactAccents.Reset();PresentationLag=0;
    for(auto& Pair:ExpansionBatches)if(Pair.Value)Pair.Value->DestroyComponent();ExpansionBatches.Reset();
    ProjectileBodiesCreated=0;ProjectileBodiesReleased=0;
    for (auto& Entry:Hazards) if (Entry.Value) Entry.Value->DestroyComponent();Hazards.Reset();
    for (auto& Entry:ShockwaveRings) if (Entry.Value) Entry.Value->DestroyComponent();ShockwaveRings.Reset();
    for (auto& Entry:SlowEffects) if (Entry.Value) Entry.Value->DestroyComponent();SlowEffects.Reset();
    for (auto& Entry:StunEffects) if (Entry.Value) Entry.Value->DestroyComponent();StunEffects.Reset();
    for (auto& Effect:TransientEffects) if (Effect.IsValid()) Effect->DestroyComponent();TransientEffects.Reset();
    NextFrostBreath.Reset();FrostBreathPuffs=0;
    LastSeed=0;LastTime=-1;ResultDeathClock=0;AetherStage=1;ClearPlacementPreview();
}
void ARiftArenaPresentation::OnMatchChanged()
{
    const auto* State=ViewState();
    if (!State || (LastSeed && State->seed!=LastSeed) || (State && State->elapsed+1e-7<LastTime)) ClearVisuals();
    Synchronize(0.f);
}
void ARiftArenaPresentation::Synchronize(float DeltaSeconds)
{
    const auto* State=ViewState();
    if (!State) {if (Units.Num()) ClearVisuals();return;}
    if ((LastSeed && State->seed!=LastSeed) || State->elapsed+1e-7<LastTime) ClearVisuals();
    LastSeed=State->seed;LastTime=State->elapsed;
    ResultDeathClock=State->phase==rift::Phase::Finished?FMath::Min(1.1f,ResultDeathClock+DeltaSeconds):0.f;
    auto* Catalog=GetGameInstance()->GetSubsystem<URiftAssetCatalogSubsystem>();
    const double VisualTime=State->elapsed+ResultDeathClock;
    const auto* Replay=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();
    const bool Interpolate=State->phase!=rift::Phase::Finished && Match && Match->GetSpeed()>0 &&
        !(Replay&&Replay->IsPlaying()) && Match->Simulation();
    const double MotionAlpha=Interpolate?Match->Simulation()->InterpolationAlpha():1.;
    PresentationLag=(1.-MotionAlpha)/60.;
    struct FVisualHerd {uint64 Leader=MAX_uint64;FVector Center=FVector::ZeroVector;int32 Members=0;};
    TMap<uint64,FVisualHerd> VisualHerds;
    for(const auto& Entity:State->entities)if(!Entity.dead && Entity.cardId=="stampede")
    {
        auto& Herd=VisualHerds.FindOrAdd(Entity.playId);Herd.Leader=FMath::Min(Herd.Leader,Entity.id);
        Herd.Center+=URiftMatchSubsystem::WorldPoint(Entity.position);++Herd.Members;
    }
    TSet<uint64> Present;
    for (const auto& Entity:State->entities)
    {
        auto* Actor=Visual(Entity.id);auto* Card=(!Actor||Entity.dead)?Catalog->Card(UTF8_TO_TCHAR(Entity.cardId.c_str())):nullptr;
        // Dead authoritative bodies can remain until their simulation cleanup.
        // Presentation expires independently, and old replay corpses must never
        // recreate an already completed death animation on a later frame.
        if(CorpseVisualExpired(Entity,Card,VisualTime))continue;
        Present.Add(Entity.id);
        const auto* Herd=Entity.cardId=="stampede"?VisualHerds.Find(Entity.playId):nullptr;
        const bool FormationLeader=Herd && Herd->Leader==Entity.id;
        const FVector FormationCenter=Herd?Herd->Center/FMath::Max(1,Herd->Members):FVector::ZeroVector;
        if (!Actor)
        {
            Actor=GetWorld()->SpawnActor<ARiftUnitVisual>();
            Actor->SetFormationDustLeader(FormationLeader,FormationCenter);
            if (!Actor->InitializeEntity(Entity,Card,State->elapsed)) {Actor->Destroy();continue;}
            Units.Add(Entity.id,Actor);
        }
        Actor->SetFormationDustLeader(FormationLeader,FormationCenter);
        Actor->Synchronize(Entity,*State,DeltaSeconds,MotionAlpha);
        if(Actor->CurrentAnimation()==TEXT("Breath")&&!Actor->IsDead()&&!Actor->HiddenFromViewer()&&State->phase!=rift::Phase::Finished)
        {
            auto* Next=NextFrostBreath.Find(Entity.id);
            if(!Next||State->elapsed>=*Next)
            {
                // Idle mouth ambience reuses the authored finite Frost effect;
                // it has no event, damage, status or simulation-side ownership.
                const FVector Mouth=Actor->AttackLocation();
                if(auto* Puff=SpawnEffect(TEXT("Frost"),Mouth,Entity.team,60*RiftArenaGeometry::WorldScale))
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
    SynchronizeProjectiles(*State);SynchronizeShockwaves(*State);SynchronizeSpellCasts(*State);SynchronizeSpellDebris(*State);SynchronizeHazards(*State);SynchronizeStatuses(*State);SynchronizeExpansionEffects(*State);
    TransientEffects.RemoveAllSwap([](const auto& Effect){return !Effect.IsValid();});
    SpellImpactEffects.RemoveAllSwap([](const auto& Effect){return !Effect.Component.IsValid();});
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
        auto* Component=SpawnEffect(Name,FVector((-10+5*(Index%5))*RiftArenaGeometry::UnitsPerTile,(-12+7*(Index/5))*RiftArenaGeometry::UnitsPerTile,75*RiftArenaGeometry::WorldScale),rift::Team::Player,150*RiftArenaGeometry::WorldScale,Persistent);
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
    Report->SetNumberField(TEXT("presentationLagSeconds"),PresentationLag);
    Report->SetNumberField(TEXT("contactAccentCount"),ContactAccents.Num());
    Report->SetBoolField(TEXT("showcaseAgeSampling"),ShowcaseSampleAge>=0.f);
    if(ShowcaseSampleAge>=0.f)Report->SetNumberField(TEXT("requestedShowcaseAge"),ShowcaseSampleAge);
    TArray<TSharedPtr<FJsonValue>> Animations;
    for(const auto& Pair:Units)if(auto* Unit=Pair.Value.Get())
    {
        auto Entry=MakeShared<FJsonObject>();Entry->SetNumberField(TEXT("entityId"),double(Pair.Key));
        Entry->SetStringField(TEXT("assetId"),Unit->PresentationAssetId());Entry->SetStringField(TEXT("clip"),Unit->CurrentAnimation().ToString());
        Entry->SetStringField(TEXT("animationAsset"),Unit->AnimationAssetPath());Entry->SetNumberField(TEXT("positionSeconds"),Unit->AnimationPosition());
        Entry->SetBoolField(TEXT("hasTakenDamage"),Unit->HasBeenDamaged());
        Entry->SetBoolField(TEXT("healthBarVisible"),!Unit->IsDead()&&Unit->HasBeenDamaged()&&!Unit->HiddenFromViewer());
        Entry->SetBoolField(TEXT("cloaked"),Unit->IsCloaked());Entry->SetBoolField(TEXT("hiddenFromViewer"),Unit->HiddenFromViewer());
        Entry->SetNumberField(TEXT("shieldFragments"),Unit->ShieldFragmentCount());
        Entry->SetNumberField(TEXT("reaperAuraPieces"),Unit->ReaperAuraCount());
        Entry->SetNumberField(TEXT("pulverizerPulsePieces"),Unit->PulverizerPulseCount());
        Entry->SetNumberField(TEXT("renderX"),Unit->GetActorLocation().X/RiftArenaGeometry::UnitsPerTile);
        Entry->SetNumberField(TEXT("renderZ"),Unit->GetActorLocation().Y/RiftArenaGeometry::UnitsPerTile);
        if (const auto* State=ViewState()) if (const auto* Entity=FindEntity(*State,Pair.Key))
        {
            Entry->SetNumberField(TEXT("hp"),Entity->hp);Entry->SetNumberField(TEXT("maxHp"),Entity->maxHp);
            Entry->SetNumberField(TEXT("authoritativeX"),Entity->position.x);Entry->SetNumberField(TEXT("authoritativeZ"),Entity->position.z);
            Entry->SetNumberField(TEXT("shieldHP"),Entity->shieldHP);Entry->SetNumberField(TEXT("maxShieldHP"),Entity->maxShieldHP);
            Entry->SetNumberField(TEXT("parentEntityId"),double(Entity->parentEntityId));Entry->SetNumberField(TEXT("invisibleUntil"),Entity->invisibleUntil);
            Entry->SetStringField(TEXT("team"),UTF8_TO_TCHAR(rift::TeamName(Entity->team).c_str()));
        }
        Animations.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Report->SetArrayField(TEXT("unitAnimations"),Animations);
    TArray<TSharedPtr<FJsonValue>> ExpansionEffects;
    for(const auto& Pair:ExpansionBatches)if(auto* Batch=Pair.Value.Get())
    {
        auto Entry=MakeShared<FJsonObject>();Entry->SetStringField(TEXT("name"),Pair.Key.ToString());
        Entry->SetNumberField(TEXT("instances"),Batch->GetInstanceCount());Entry->SetBoolField(TEXT("visible"),Batch->IsVisible());
        Entry->SetStringField(TEXT("renderMode"),TEXT("instanced_static_meshes"));
        Entry->SetBoolField(TEXT("nonInteractive"),Batch->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
        Entry->SetStringField(TEXT("mesh"),Batch->GetStaticMesh()?Batch->GetStaticMesh()->GetPathName():TEXT(""));
        Entry->SetNumberField(TEXT("forcedLOD"),Batch->GetForcedLodModel());
        Entry->SetBoolField(TEXT("registered"),Batch->IsRegistered());
        Entry->SetBoolField(TEXT("renderProxyReady"),Batch->GetSceneProxy()!=nullptr);
        Entry->SetBoolField(TEXT("psoPrecaching"),Batch->IsPSOPrecaching());
        Entry->SetStringField(TEXT("worldBoundsOrigin"),Batch->Bounds.Origin.ToString());
        Entry->SetStringField(TEXT("worldBoundsExtent"),Batch->Bounds.BoxExtent.ToString());
        FTransform FirstInstance;if(Batch->GetInstanceCount()&&Batch->GetInstanceTransform(0,FirstInstance,true))Entry->SetStringField(TEXT("firstWorldTransform"),FirstInstance.ToString());
        if(auto* Material=Batch->GetMaterial(0))
        {
            Entry->SetStringField(TEXT("material"),Material->GetPathName());
            Entry->SetStringField(TEXT("baseMaterial"),Material->GetMaterial()->GetPathName());
            const bool Opaque=Material->GetBlendMode()==BLEND_Opaque;
            Entry->SetStringField(TEXT("blendMode"),Opaque?TEXT("Opaque"):Material->GetBlendMode()==BLEND_Translucent?TEXT("Translucent"):TEXT("Other"));
            const auto* Resource=Material->GetMaterial()->GetMaterialResource(GMaxRHIShaderPlatform);
            Entry->SetBoolField(TEXT("materialResourcePresent"),Resource!=nullptr);
            Entry->SetBoolField(TEXT("shaderMapPresent"),Resource&&Resource->GetGameThreadShaderMap()!=nullptr);
            Entry->SetBoolField(TEXT("shaderMapComplete"),Resource&&Resource->GetGameThreadShaderMap()&&Resource->IsGameThreadShaderMapComplete());
#if WITH_EDITOR
            Entry->SetBoolField(TEXT("shaderCompilationDiagnosticsAvailable"),true);
            Entry->SetBoolField(TEXT("shaderCompilationFinished"),Resource&&Resource->IsCompilationFinished());
            TArray<TSharedPtr<FJsonValue>> ShaderErrors;
            if(Resource)for(const auto& Error:Resource->GetCompileErrors())ShaderErrors.Add(MakeShared<FJsonValueString>(Error));
            Entry->SetArrayField(TEXT("shaderErrors"),ShaderErrors);
#else
            Entry->SetBoolField(TEXT("shaderCompilationDiagnosticsAvailable"),false);
#endif
            float Opacity=0,Emission=0,GlowStrength=0;
            const bool OpacityBound=Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("EffectOpacity")),Opacity);
            Entry->SetBoolField(TEXT("opacityBound"),OpacityBound);
            Entry->SetStringField(TEXT("opacitySource"),Opaque?TEXT("opaque_material"):OpacityBound?TEXT("EffectOpacity"):TEXT("unbound"));
            Entry->SetBoolField(TEXT("emissionBound"),Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("EffectEmission")),Emission));
            Entry->SetBoolField(TEXT("glowStrengthBound"),Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("GlowStrength")),GlowStrength));
            Entry->SetNumberField(TEXT("opacity"),Opaque?1.:Opacity);Entry->SetNumberField(TEXT("emission"),Emission);
            Entry->SetNumberField(TEXT("glowStrength"),GlowStrength);
        }
        ExpansionEffects.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Report->SetArrayField(TEXT("expansionSurfaceEffects"),ExpansionEffects);
    TArray<TSharedPtr<FJsonValue>> Waves;
    if (const auto* State=ViewState()) for (const auto& Wave:State->shockwaves)
    {
        auto V=MakeShared<FJsonObject>();V->SetNumberField(TEXT("id"),double(Wave.id));
        V->SetNumberField(TEXT("source"),double(Wave.source));V->SetNumberField(TEXT("playId"),double(Wave.playId));
        V->SetNumberField(TEXT("radius"),FMath::Clamp((State->elapsed-Wave.born)*Wave.speed,0.,Wave.radius));
        V->SetNumberField(TEXT("maximumRadius"),Wave.radius);V->SetNumberField(TEXT("speed"),Wave.speed);
        V->SetNumberField(TEXT("born"),Wave.born);V->SetNumberField(TEXT("expires"),Wave.expires);
        auto* Ring=ShockwaveRings.FindRef(Wave.id).Get();V->SetBoolField(TEXT("ringPresent"),Ring!=nullptr);
        V->SetBoolField(TEXT("ringVisible"),Ring&&Ring->IsVisible());
        if (Ring) V->SetNumberField(TEXT("decalRadiusCm"),Ring->DecalSize.Y);
        Waves.Add(MakeShared<FJsonValueObject>(V));
    }
    Report->SetArrayField(TEXT("shockwaveVisuals"),Waves);
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
    TArray<TSharedPtr<FJsonValue>> Casts,Impacts;
    if (const auto* State=ViewState()) for (const auto& Cast:State->spellCasts)
    {
        auto Entry=MakeShared<FJsonObject>();Entry->SetNumberField(TEXT("playId"),double(Cast.playId));
        Entry->SetStringField(TEXT("cardId"),UTF8_TO_TCHAR(Cast.cardId.c_str()));
        Entry->SetStringField(TEXT("team"),UTF8_TO_TCHAR(rift::TeamName(Cast.team).c_str()));
        Entry->SetNumberField(TEXT("born"),Cast.born);Entry->SetNumberField(TEXT("impactAt"),Cast.impactAt);
        Entry->SetNumberField(TEXT("remaining"),FMath::Max(0.,Cast.impactAt-State->elapsed));
        Entry->SetNumberField(TEXT("progress"),FMath::Clamp((State->elapsed-Cast.born)/FMath::Max(.001,Cast.impactAt-Cast.born),0.,1.));
        Entry->SetStringField(TEXT("target"),URiftMatchSubsystem::WorldPoint(Cast.position).ToString());
        auto* Ring=SpellCastRings.FindRef(Cast.playId).Get();Entry->SetBoolField(TEXT("decalVisible"),Ring&&Ring->IsVisible()&&Ring->GetDecalMaterial());
        Entry->SetNumberField(TEXT("radiusTiles"),rift::FindCard(Cast.cardId)?rift::FindCard(Cast.cardId)->spellRadius:0.);
        TArray<TSharedPtr<FJsonValue>> Bodies;
        for (int32 Index=0;Index<SpellCastPieces.Num();++Index) if (SpellCastPieces[Index].PlayId==Cast.playId)
        {
            auto Piece=MakeShared<FJsonObject>();auto* Body=SpellCastBodies[Index].Get();
            Piece->SetNumberField(TEXT("index"),SpellCastPieces[Index].Index);Piece->SetBoolField(TEXT("visible"),Body&&Body->IsVisible());
            Piece->SetBoolField(TEXT("collisionDisabled"),Body&&Body->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
            auto* Glow=SpellCastGlows[Index].Get();Piece->SetBoolField(TEXT("glowPresent"),Glow&&Glow->GetAsset());
            Piece->SetBoolField(TEXT("glowPaused"),Glow&&Glow->IsPaused());
            if (Body)
            {
                Piece->SetStringField(TEXT("mesh"),Body->GetStaticMesh()?Body->GetStaticMesh()->GetPathName():TEXT("missing"));
                Piece->SetStringField(TEXT("location"),Body->GetComponentLocation().ToString());
                Piece->SetStringField(TEXT("rotation"),Body->GetComponentRotation().ToString());
                Piece->SetStringField(TEXT("scale"),Body->GetComponentScale().ToString());
            }
            Bodies.Add(MakeShared<FJsonValueObject>(Piece));
        }
        Entry->SetArrayField(TEXT("bodies"),Bodies);Casts.Add(MakeShared<FJsonValueObject>(Entry));
    }
    for (const auto& Impact:RecentSpellImpacts)
    {
        auto Entry=MakeShared<FJsonObject>();Entry->SetNumberField(TEXT("playId"),double(Impact.playId));
        Entry->SetStringField(TEXT("cardId"),UTF8_TO_TCHAR(Impact.cardId.c_str()));Entry->SetNumberField(TEXT("time"),Impact.time);
        Entry->SetStringField(TEXT("target"),URiftMatchSubsystem::WorldPoint(Impact.position).ToString());
        Impacts.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Report->SetArrayField(TEXT("spellCastVisuals"),Casts);Report->SetArrayField(TEXT("recentSpellImpacts"),Impacts);
    Report->SetNumberField(TEXT("liveSpellCastBodies"),SpellCastBodies.Num());Report->SetNumberField(TEXT("liveSpellCastRings"),SpellCastRings.Num());
    Report->SetNumberField(TEXT("liveSpellCastGlows"),SpellCastGlows.Num());
    Report->SetNumberField(TEXT("spellImpactEvents"),double(SpellImpactEvents));
    Report->SetBoolField(TEXT("breathSmoke"),FParse::Param(FCommandLine::Get(),TEXT("RiftBreathSmoke")));
    Report->SetNumberField(TEXT("frostBreathPuffs"),FrostBreathPuffs);
    TSet<UNiagaraComponent*> Components;
    for(const auto& Pair:Projectiles)if(Pair.Value)Components.Add(Pair.Value.Get());
    for(const auto& Glow:SpellCastGlows)if(Glow)Components.Add(Glow.Get());
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
            Controller->WaitForConcurrentTickAndFinalize();
            // Finalization can complete an auto-destroy effect and release
            // the controller instance before its age is read.
            const bool bInstanceSurvivedFinalize=Controller->IsValid();
            Entry->SetBoolField(TEXT("instanceRetiredDuringFinalize"),!bInstanceSurvivedFinalize);
            if(bInstanceSurvivedFinalize)Entry->SetNumberField(TEXT("age"),Controller->GetAge());
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
    const float EffectSpriteScale=ParticleSpriteScale(Component->GetAsset()?Component->GetAsset()->GetFName():NAME_None)*RiftArenaGeometry::WorldScale;
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
            Component=SpawnEffect(FlightFor(Projectile.cardId),Start,Projectile.team,18.f*RiftArenaGeometry::WorldScale,true);
            if (!Component) continue;
            Projectiles.Add(Projectile.id,Component);ProjectileOrigins.Add(Projectile.id,Start);
            if (auto* Body=MakeProjectileBody(Projectile.cardId,Projectile.team))
            {ProjectileBodies.Add(Projectile.id,Body);++ProjectileBodiesCreated;}
        }
        const FVector Start=ProjectileOrigins.FindRef(Projectile.id), End=Target->ImpactLocation();
        const double Duration=FMath::Max(.001,Projectile.duration);
        const double Progress=FMath::Clamp(1.-(Projectile.remaining+PresentationLag)/Duration,0.,1.);
        const double Age=Progress*Duration;
        const FVector Position=ProjectilePathPoint(Projectile.cardId,Start,End,Progress);
        const FVector Tangent=ProjectilePathPoint(Projectile.cardId,Start,End,FMath::Min(1.,Progress+.003))-
            ProjectilePathPoint(Projectile.cardId,Start,End,FMath::Max(0.,Progress-.003));
        Component->SetWorldLocation(Position);Component->SetWorldRotation(Tangent.Rotation());
        Component->SetWorldScale3D(FVector::OneVector);
        SetEffectParameters(Component,Projectile.team,18*RiftArenaGeometry::WorldScale,Start,End,EffectColor(FlightFor(Projectile.cardId),Projectile.team));
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
            Body->SetWorldTransform(FTransform(ProjectileMeshOrientation(Projectile.cardId,Tangent,Age),Position,FVector(Scale*RiftArenaGeometry::WorldScale)));
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
                const float Width=1.8f*RiftArenaGeometry::WorldScale*(1.f-float(Segment)/Segments)*(Projectile.cardId=="arc_mage"?1.6f:Projectile.cardId=="storm_raven"?1.4f:.9f);
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
    const bool Spell=Card=="meteor_shards"||Card=="bullet_burst";
    for (int32 Index=0;Index<Component->GetNumMaterials();++Index)
    {
        // The authored glow atlas emits its baked blue RGB. Cast trails use
        // the existing surface shader's actual team-mask emissive input so
        // their warm color and luminance are real, rather than unused knobs.
        if (Spell) if (auto* Surface=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Rift/Materials/M_RiftSurface.M_RiftSurface")))
            Component->SetMaterial(Index,Surface);
        if (auto* Material=Component->CreateDynamicMaterialInstance(Index))
        {
            const FLinearColor Color=EffectColor(Flight,Team);
            Material->SetVectorParameterValue(TEXT("Color"),Color);
            Material->SetVectorParameterValue(TEXT("TeamColor"),Color);
            Material->SetScalarParameterValue(TEXT("RiftTeamEmissive"),Spell?2.8f:.7f);
        }
    }
    return Component;
}
void ARiftArenaPresentation::RemoveProjectile(uint64 Id)
{
    if (auto* Component=Projectiles.FindRef(Id).Get()) Component->DestroyComponent();
    if (auto* Body=ProjectileBodies.FindRef(Id).Get()) {Body->DestroyComponent();++ProjectileBodiesReleased;}
    Projectiles.Remove(Id);ProjectileBodies.Remove(Id);ProjectileOrigins.Remove(Id);
}
void ARiftArenaPresentation::RemoveSpellCast(uint64 Id)
{
    if (auto* Ring=SpellCastRings.FindRef(Id).Get()) Ring->DestroyComponent();
    SpellCastRings.Remove(Id);SpellCastMaterials.Remove(Id);
    for (int32 Index=SpellCastPieces.Num()-1;Index>=0;--Index) if (SpellCastPieces[Index].PlayId==Id)
    {
        if (SpellCastBodies[Index]) SpellCastBodies[Index]->DestroyComponent();
        if (SpellCastGlows[Index]) SpellCastGlows[Index]->DestroyComponent();
        SpellCastBodies.RemoveAtSwap(Index);SpellCastPieces.RemoveAtSwap(Index);SpellCastGlows.RemoveAtSwap(Index);
    }
}
void ARiftArenaPresentation::SynchronizeSpellCasts(const rift::Snapshot& State)
{
    TSet<uint64> Present;
    TMap<UInstancedStaticMeshComponent*,TArray<FTransform>> TrailTransforms;
    auto* Replay=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();
    const float CastSpeed=Replay&&Replay->IsPlaying()?Replay->Speed():Match->GetSpeed();
    for (const auto& Cast:State.spellCasts)
    {
        const auto* Card=rift::FindCard(Cast.cardId);
        if (!Card || !Card->spell || State.phase==rift::Phase::Finished) continue;
        Present.Add(Cast.playId);
        const int32 Count=Cast.cardId=="meteor_shards"?5:Cast.cardId=="bullet_burst"?Card->rounds:0;
        const double Duration=FMath::Max(.001,Cast.cardId=="thunderstorm"?Card->dotInterval:Cast.impactAt-Cast.born);
        const double Progress=FMath::Clamp((State.elapsed-(Cast.impactAt-Duration))/Duration,0.,1.);
        const FVector Center=URiftMatchSubsystem::WorldPoint(Cast.position);
        if (!SpellCastRings.Contains(Cast.playId))
        {
            auto* Ring=NewObject<UDecalComponent>(this);Ring->SetupAttachment(Scene);Ring->SetMobility(EComponentMobility::Movable);
            Ring->SetWorldLocation(Center+FVector(0,0,14));Ring->SetWorldRotation(FRotator(-90,0,0));
            Ring->DecalSize=FVector(36,Card->spellRadius*RiftArenaGeometry::UnitsPerTile,Card->spellRadius*RiftArenaGeometry::UnitsPerTile);Ring->SortOrder=3;
            if (auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Rift/Materials/M_RiftPlacement.M_RiftPlacement")))
            {
                auto* Material=UMaterialInstanceDynamic::Create(Base,this);Material->SetScalarParameterValue(TEXT("Footprint"),0.f);
                Material->SetVectorParameterValue(TEXT("RingColor"),Cast.cardId=="thunderstorm"?FLinearColor(.30f,.48f,.78f):Cast.cardId=="shadow_surge"?FLinearColor(.36f,.09f,.66f):ARiftUnitVisual::TeamColor(Cast.team));
                Ring->SetDecalMaterial(Material);SpellCastMaterials.Add(Cast.playId,Material);
            }
            Ring->RegisterComponent();AddInstanceComponent(Ring);SpellCastRings.Add(Cast.playId,Ring);
            for (int32 Piece=0;Piece<Count;++Piece) if (auto* Body=MakeProjectileBody(Cast.cardId,Cast.team))
            {
                for (int32 Slot=0;Slot<Body->GetNumMaterials();++Slot) if (auto* Material=Body->CreateDynamicMaterialInstance(Slot))
                {
                    Material->SetScalarParameterValue(TEXT("GlowStrength"),Cast.cardId=="meteor_shards"?5.f:1.4f);
                    Material->SetScalarParameterValue(TEXT("RiftBaseColorGain"),1.25f);
                }
                const FVector Position=SpellCastPathPoint(Cast.cardId,Center,Cast.team,Piece,Count,Progress);
                auto* Glow=SpawnEffect(TEXT("TowerFlight"),Position,Cast.team,18.f*RiftArenaGeometry::WorldScale,true);
                SpellCastBodies.Add(Body);SpellCastPieces.Add({Cast.playId,Piece});SpellCastGlows.Add(Glow);
            }
        }
        if (auto* Material=SpellCastMaterials.FindRef(Cast.playId).Get())
        {
            // A restrained brightening announces the deadline without washing
            // out the fighters or moving the committed target radius.
            FLinearColor Color=(Cast.cardId=="shadow_surge"?FLinearColor(.36f,.09f,.66f):Cast.cardId=="thunderstorm"?FLinearColor(.28f,.46f,.68f):ARiftUnitVisual::TeamColor(Cast.team))*(1.f+.3f*float(Progress));Color.A=1.f;
            Material->SetVectorParameterValue(TEXT("RingColor"),Color);
        }
        auto* Trail=ProjectileTrail(Cast.cardId,Cast.team);
        for (int32 Piece=0;Piece<SpellCastPieces.Num();++Piece)
        {
            if (SpellCastPieces[Piece].PlayId!=Cast.playId) continue;
            auto* Body=SpellCastBodies[Piece].Get();if (!Body) continue;
            const int32 Index=SpellCastPieces[Piece].Index;
            const double Release=Cast.cardId=="meteor_shards"?0.:double(Index)/FMath::Max(1,Count-1)*.4;
            const bool Visible=Progress>=Release;Body->SetVisibility(Visible);
            auto* Glow=SpellCastGlows[Piece].Get();if (Glow) Glow->SetVisibility(Visible);
            if (!Visible) {if (Glow) Glow->SetPaused(true);continue;}
            const FVector Position=SpellCastPathPoint(Cast.cardId,Center,Cast.team,Index,Count,Progress);
            const FVector Direction=SpellCastPathPoint(Cast.cardId,Center,Cast.team,Index,Count,FMath::Min(1.,Progress+.004))-
                SpellCastPathPoint(Cast.cardId,Center,Cast.team,Index,Count,FMath::Max(Release,Progress-.004));
            FQuat Rotation=ProjectileMeshOrientation(Cast.cardId,Direction,0.);
            if (Cast.cardId=="meteor_shards") Rotation=Rotation*FQuat(FVector::ForwardVector,float((State.elapsed-Cast.born)*(Index%2?-3.:3.)));
            Body->SetWorldTransform(FTransform(Rotation,Position,FVector((Cast.cardId=="meteor_shards"?1.6f:4.4f)*RiftArenaGeometry::WorldScale)));
            if (Glow)
            {
                Glow->SetWorldLocation(Position);
                const FLinearColor Color=Cast.cardId=="meteor_shards"?FLinearColor(1.f,.25f,.045f,1.f):FLinearColor(1.f,.74f,.25f,1.f);
                SetEffectParameters(Glow,Cast.team,18.f*RiftArenaGeometry::WorldScale,Position,Position,Color);
                if (auto Controller=Glow->GetSystemInstanceController();Controller&&Controller->IsValid()&&Controller->GetAge()==0.f&&
                    Glow->GetAsset()&&Glow->GetAsset()->IsReadyToRun())
                {
                    Glow->SetPaused(false);Glow->SetCustomTimeDilation(1.f);Glow->AdvanceSimulation(1,1.f/60.f);
                    if (auto Initialized=Glow->GetSystemInstanceController()) Initialized->WaitForConcurrentTickAndFinalize();
                }
                Glow->SetCustomTimeDilation(CastSpeed);Glow->SetPaused(CastSpeed<=0.f);
            }
            if (!Trail) continue;
            auto& Instances=TrailTransforms.FindOrAdd(Trail);
            const double Tail=Cast.cardId=="meteor_shards"?.11:.18;
            for (int32 Segment=0;Segment<4;++Segment)
            {
                const double FrontTime=FMath::Max(Release,Progress-Tail*double(Segment)/4.);
                const double RearTime=FMath::Max(Release,Progress-Tail*double(Segment+1)/4.);
                const FVector Front=SpellCastPathPoint(Cast.cardId,Center,Cast.team,Index,Count,FrontTime);
                const FVector Rear=SpellCastPathPoint(Cast.cardId,Center,Cast.team,Index,Count,RearTime);
                const FVector Vector=Front-Rear;const double Length=Vector.Length();if (Length<.5) continue;
                const FVector MeshSize=Trail->GetStaticMesh()->GetBounds().BoxExtent*2.;
                const float Width=(Cast.cardId=="meteor_shards"?4.f:2.5f)*RiftArenaGeometry::WorldScale*(1.f-float(Segment)/4.);
                Instances.Add(FTransform(FRotationMatrix::MakeFromX(Vector).ToQuat(),(Front+Rear)*.5,
                    FVector(Length/FMath::Max(1.,MeshSize.X),Width,Width)));
            }
        }
    }
    for (auto& Entry:TrailTransforms) Entry.Key->AddInstances(Entry.Value,false,true,false);
    TArray<uint64> Removed;for (const auto& Pair:SpellCastRings) if (!Present.Contains(Pair.Key)) Removed.Add(Pair.Key);
    for (uint64 Id:Removed) RemoveSpellCast(Id);
}
void ARiftArenaPresentation::SpawnSpellDebris(const rift::Event& Event)
{
    const bool Meteor=Event.cardId=="meteor_shards";
    if (!Meteor && Event.cardId!="bullet_burst") return;
    const auto* Card=rift::FindCard(Event.cardId);if (!Card) return;
    // Rounds and shattered stone radiate only from the recorded impact. The
    // separate pending-cast meshes disappear on that authoritative deadline.
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
        Piece.Scale=(Meteor?.65f:2.1f)*RiftArenaGeometry::WorldScale;Piece.Gravity=(Meteor?1500.f:120.f)*RiftArenaGeometry::WorldScale;
        Piece.Spin=Meteor?float((Index%2?1:-1)*(6.+Index)):0.f;
        const double Angle=UE_TWO_PI*(double(Index)/Count+double(Event.sequence%17)/83.);
        const double Speed=Meteor?150.*RiftArenaGeometry::WorldScale:Card->spellRadius*RiftArenaGeometry::UnitsPerTile/Piece.Duration*.82;
        Piece.Origin=URiftMatchSubsystem::WorldPoint(Event.position,Meteor?34.:24.);
        Piece.Velocity=FVector(FMath::Cos(Angle)*Speed,FMath::Sin(Angle)*Speed,(Meteor?180.+Index*18.:18.+(Index%3)*10.)*RiftArenaGeometry::WorldScale);
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
        FVector Position=Piece.Origin+Piece.Velocity*Age;Position.Z=FMath::Max(8.*RiftArenaGeometry::WorldScale,Position.Z-Piece.Gravity*Age*Age*.5);
        FVector Direction=Piece.Velocity;Direction.Z-=Piece.Gravity*Age;
        FQuat Rotation=ProjectileMeshOrientation("bullet_burst",Direction,0.);
        if (Piece.Meteor) Rotation=Rotation*FQuat(FVector::ForwardVector,float(Age*Piece.Spin));
        const float Fade=FMath::Clamp(float((Piece.Duration-Age)/.065),0.f,1.f);
        Body->SetWorldTransform(FTransform(Rotation,Position,FVector(Piece.Scale*Fade)));
    }
}
void ARiftArenaPresentation::SynchronizeShockwaves(const rift::Snapshot& State)
{
    TSet<uint64> Present;
    for (const auto& Wave:State.shockwaves)
    {
        if (State.phase==rift::Phase::Finished || State.elapsed>=Wave.expires) continue;
        Present.Add(Wave.id);
        auto* Ring=ShockwaveRings.FindRef(Wave.id).Get();
        if (!Ring)
        {
            Ring=NewObject<UDecalComponent>(this);Ring->SetupAttachment(Scene);Ring->SetMobility(EComponentMobility::Movable);
            Ring->SetWorldLocation(URiftMatchSubsystem::WorldPoint(Wave.origin,12));Ring->SetWorldRotation(FRotator(-90,0,0));
            Ring->SortOrder=4;
            if (auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Rift/Materials/M_RiftShockwave.M_RiftShockwave")))
            {
                auto* Material=UMaterialInstanceDynamic::Create(Base,this);
                Material->SetVectorParameterValue(TEXT("RingColor"),FLinearColor(.82f,.16f,1.f,1.f));
                Material->SetScalarParameterValue(TEXT("FrontWidth"),.027f);
                Ring->SetDecalMaterial(Material);
            }
            Ring->RegisterComponent();AddInstanceComponent(Ring);ShockwaveRings.Add(Wave.id,Ring);
        }
        // Radius comes exclusively from simulation time: pause and replay seek
        // reconstruct the same traveling front that owns each damage event.
        const double Radius=FMath::Clamp((State.elapsed-Wave.born)*Wave.speed,0.,Wave.radius);
        Ring->DecalSize=FVector(28,FMath::Max(1.,Radius*RiftArenaGeometry::UnitsPerTile),FMath::Max(1.,Radius*RiftArenaGeometry::UnitsPerTile));
        Ring->SetVisibility(Radius>0.);
    }
    TArray<uint64> Removed;
    for (const auto& Entry:ShockwaveRings) if (!Present.Contains(Entry.Key)) Removed.Add(Entry.Key);
    for (uint64 Id:Removed) {if (ShockwaveRings[Id]) ShockwaveRings[Id]->DestroyComponent();ShockwaveRings.Remove(Id);}
}
void ARiftArenaPresentation::SynchronizeHazards(const rift::Snapshot& State)
{
    TSet<uint64> Present;
    for (const auto& Hazard:State.hazards)
    {
        Present.Add(Hazard.playId);
        if (!Hazards.Contains(Hazard.playId))
        {
            auto* Component=SpawnEffect(TEXT("MeteorTick"),URiftMatchSubsystem::WorldPoint(Hazard.position,8),Hazard.team,Hazard.radius*RiftArenaGeometry::UnitsPerTile,true);
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
            bool PulverizerSlow=false,OtherSlow=false;
            if(Slow)for(const auto& Status:Entity.slows)if(Status.until>State.elapsed && Status.pct>0)
            {if(Status.cardId=="pulverizer")PulverizerSlow=true;else OtherSlow=true;}
            // A Pulverizer hit has an ankle-height native ripple. Keep the
            // above-head legacy slow effect only for a different active slow.
            const bool Active=!Entity.dead && !(Entity.team==rift::Team::Enemy && Entity.invisibleUntil>State.elapsed) && (Slow?Entity.slowUntil:Entity.stunUntil)>State.elapsed &&
                (!Slow || (Entity.slowPct>0 && (!PulverizerSlow || OtherSlow)));
            if (!Active) continue;auto* Unit=Visual(Entity.id);if (!Unit) continue;Present.Add(Entity.id);
            UNiagaraComponent* Component=Components.FindRef(Entity.id).Get();
            if (!Component) {Component=SpawnEffect(Name,Unit->StatusLocation(),Entity.team,35*RiftArenaGeometry::WorldScale,true);if (Component) Components.Add(Entity.id,Component);}
            if (Component) Component->SetWorldLocation(Unit->StatusLocation());
        }
        TArray<uint64> Removed;for (const auto& Entry:Components) if (!Present.Contains(Entry.Key)) Removed.Add(Entry.Key);
        for (uint64 Id:Removed) {if (Components[Id]) Components[Id]->DestroyComponent();Components.Remove(Id);}
    };
    Update(SlowEffects,TEXT("Slow"),true);Update(StunEffects,TEXT("Stun"),false);
}
void ARiftArenaPresentation::PlayEventSound(FName Name,FVector Position,float Gain)
{if (Audio) Audio->PlayEffect(Name,Position,Gain);}
UInstancedStaticMeshComponent* ARiftArenaPresentation::ExpansionBatch(FName Key,FName MeshId,FLinearColor Color)
{
    if(auto* Batch=ExpansionBatches.FindRef(Key).Get())return Batch;
    const FString Path=FString::Printf(TEXT("/Game/Rift/Environment/SM_%s.SM_%s"),*MeshId.ToString(),*MeshId.ToString());
    auto* Mesh=LoadObject<UStaticMesh>(nullptr,*Path);
    const bool Cloud=Key==TEXT("StormCloud");
    const bool WeatherGlow=Key==TEXT("StormRain")||Key==TEXT("StormAreaFlash")||Key==TEXT("StormTargetLightning")||Key==TEXT("UniversalStunStars");
    auto* Base=LoadObject<UMaterialInterface>(nullptr,Cloud?TEXT("/Game/Rift/Materials/M_RiftSurface.M_RiftSurface"):
        WeatherGlow?TEXT("/Game/Rift/Materials/M_RiftGlow.M_RiftGlow"):TEXT("/Game/Rift/Materials/M_RiftExpansionFX.M_RiftExpansionFX"));
    if(!Mesh || !Base){RIFT_LOG(LogRift,Error,TEXT("Expansion effect asset is missing: %s"),*Path);return nullptr;}
    auto* Batch=NewObject<UInstancedStaticMeshComponent>(this,Key);Batch->SetupAttachment(Scene);
    Batch->SetMobility(EComponentMobility::Movable);Batch->SetStaticMesh(Mesh);Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // These authored 32-212-triangle cues are already tiny; their reduced LODs
    // can erase the streak, zigzag and star silhouettes at the arena camera.
    if(WeatherGlow)Batch->SetForcedLodModel(1);
    Batch->SetGenerateOverlapEvents(false);Batch->SetCanEverAffectNavigation(false);Batch->SetCastShadow(false);Batch->bReceivesDecals=false;
    for(int32 I=0;I<Batch->GetNumMaterials();++I)
    {Batch->SetMaterial(I,Base);if(auto* MID=Batch->CreateDynamicMaterialInstance(I))
        {if(Cloud)MID->SetScalarParameterValue(TEXT("RiftBaseColorGain"),1.2f);
         else if(WeatherGlow)MID->SetScalarParameterValue(TEXT("GlowStrength"),Key==TEXT("StormRain")?.45f:Key==TEXT("UniversalStunStars")?1.6f:2.f);
         else MID->SetVectorParameterValue(TEXT("EffectColor"),Color);}}
    Batch->RegisterComponent();AddInstanceComponent(Batch);ExpansionBatches.Add(Key,Batch);return Batch;
}
void ARiftArenaPresentation::SynchronizeExpansionEffects(const rift::Snapshot& State)
{
    for(auto& Pair:ExpansionBatches)if(Pair.Value)Pair.Value->ClearInstances();
    if(State.phase==rift::Phase::Finished){ContactAccents.Reset();return;}
    TMap<UInstancedStaticMeshComponent*,TArray<FTransform>> Transforms;
    auto Piece=[&](FName Key,FName Mesh,FLinearColor Color,FVector Center,FVector Direction,FVector Size)
    {
        auto* Batch=ExpansionBatch(Key,Mesh,Color);if(!Batch)return;
        const FVector Bounds=Batch->GetStaticMesh()->GetBounds().BoxExtent*2.;
        const FVector Scale(Size.X/FMath::Max(1.,Bounds.X),Size.Y/FMath::Max(1.,Bounds.Y),Size.Z/FMath::Max(1.,Bounds.Z));
        Transforms.FindOrAdd(Batch).Add(FTransform(FRotationMatrix::MakeFromX(Direction.GetSafeNormal(UE_SMALL_NUMBER,FVector::ForwardVector)).ToQuat(),Center,Scale));
    };
    auto Ribbon=[&](FName Key,FLinearColor Color,FVector A,FVector B,double Width)
    {Piece(Key,TEXT("shadow_surge_ribbon"),Color,(A+B)*.5,B-A,FVector((B-A).Length(),Width,Width*.6));};
    const double Tile=RiftArenaGeometry::UnitsPerTile;
    const FLinearColor Violet(.37f,.08f,.66f),Cyan(.10f,.75f,.90f),Green(.20f,.70f,.27f);
    ContactAccents.RemoveAllSwap([&](const FContactAccent& Accent){return State.elapsed-Accent.Born>=.18 || State.elapsed<Accent.Born;});
    // Three short directional flecks retain the contact material. Their size
    // stays below a weapon tip, including a full swarm attacking together.
    for(const auto& Accent:ContactAccents)
    {
        const double Age=State.elapsed-Accent.Born,T=Age/.18;
        const FName Key(*FString::Printf(TEXT("Contact_%08x_%d"),Accent.Color.ToFColor(false).ToPackedRGBA(),Accent.Heavy?1:0));
        const FVector Side=FVector::CrossProduct(Accent.Direction,FVector::UpVector).GetSafeNormal(UE_SMALL_NUMBER,FVector::RightVector);
        for(int32 I=0;I<3;++I)
        {
            const FVector Direction=(Accent.Direction*.25+Side*(I-1)*.58+FVector::UpVector*(.7+.15*(I%2))).GetSafeNormal();
            const FVector Center=Accent.Position+Direction*(.08+.48*T)*Tile-FVector(0,0,.24*T*T*Tile);
            const double Length=(Accent.Heavy?.12:.075)*Tile*(1.-T);
            Piece(Key,TEXT("projectile_trail"),Accent.Color,
                Center,Direction,FVector(Length,.012*Tile,.009*Tile)*(1.-T));
        }
    }
    // The damage front is authored mesh geometry as well as a floor decal.
    // This remains visible when LOW suppresses decals/bloom, and remains owned
    // by the paid wave after its source dies. No attack/cooldown or wall-clock
    // approximation can move the ring ahead of the authoritative hit front.
    for(const auto& Wave:State.shockwaves)
    {
        if(Wave.cardId!="pulverizer" || State.phase==rift::Phase::Finished || State.elapsed<Wave.born || State.elapsed>=Wave.expires)continue;
        const double Radius=FMath::Clamp((State.elapsed-Wave.born)*Wave.speed,0.,Wave.radius)*Tile;
        if(Radius<=0.)continue;
        const FLinearColor PulseViolet(.86f,.15f,1.f);
        auto* Front=ExpansionBatch(TEXT("PulverizerTravelingFront"),TEXT("projectile_trail"),PulseViolet);
        if(Front)for(int32 I=0;I<Front->GetNumMaterials();++I)if(auto* MID=Cast<UMaterialInstanceDynamic>(Front->GetMaterial(I)))
        {MID->SetScalarParameterValue(TEXT("EffectOpacity"),.65f);MID->SetScalarParameterValue(TEXT("EffectEmission"),.45f);}
        const FVector Center=URiftMatchSubsystem::WorldPoint(Wave.origin)+FVector(0,0,Tile*.065);
        constexpr int32 Segments=48;
        for(int32 I=0;I<Segments;++I)
        {
            const double A=I*UE_TWO_PI/Segments,B=(I+1)*UE_TWO_PI/Segments;
            const FVector Start=Center+FVector(FMath::Cos(A)*Radius,FMath::Sin(A)*Radius,0),End=Center+FVector(FMath::Cos(B)*Radius,FMath::Sin(B)*Radius,0);
            Piece(TEXT("PulverizerTravelingFront"),TEXT("projectile_trail"),PulseViolet,(Start+End)*.5,End-Start,FVector((End-Start).Length(),.065*Tile,.045*Tile));
            // A subdued inner ribbon makes travel readable without obscuring
            // the tile grid or spawning a cloud around every damaged troop.
            const double TrailRadius=FMath::Max(0.,Radius-.10*Tile);
            if(TrailRadius>0.)
            {
                const FVector TrailStart=Center+FVector(FMath::Cos(A)*TrailRadius,FMath::Sin(A)*TrailRadius,0),TrailEnd=Center+FVector(FMath::Cos(B)*TrailRadius,FMath::Sin(B)*TrailRadius,0);
                Piece(TEXT("PulverizerTravelingTrail"),TEXT("projectile_trail"),FLinearColor(.48f,.08f,.75f),(TrailStart+TrailEnd)*.5,TrailEnd-TrailStart,FVector((TrailEnd-TrailStart).Length(),.023*Tile,.016*Tile));
            }
        }
    }
    for(const auto& Cast:State.spellCasts)if(Cast.cardId=="shadow_surge")
    {
        const double T=FMath::Clamp((State.elapsed-Cast.born)/FMath::Max(.001,Cast.impactAt-Cast.born),0.,1.);
        const FVector Center=URiftMatchSubsystem::WorldPoint(Cast.position,Tile*.10);
        for(int32 I=0;I<3;++I)
        {
            const double Angle=I*UE_TWO_PI/3.+.45*T;
            const FVector Direction(FMath::Cos(Angle),FMath::Sin(Angle),0);
            Piece(TEXT("ShadowWisps"),TEXT("shadow_surge_wisp"),Violet,Center+Direction*((1.85-1.55*T)*Tile),-Direction,FVector(.28,.09,.10)*Tile);
        }
    }
    for(const auto& Impact:RecentSpellImpacts)if(Impact.cardId=="shadow_surge")
    {
        const double Age=State.elapsed-Impact.time;if(Age<0 || Age>=.50)continue;
        const auto* Card=rift::FindCard(Impact.cardId);if(!Card)continue;
        const double T=Age/.50,Radius=Card->spellRadius*(.18+.82*FMath::Sin(T*UE_PI*.5))*Tile;
        const FVector Center=URiftMatchSubsystem::WorldPoint(Impact.position,Tile*.04);
        for(int32 Band=0;Band<3;++Band)for(int32 I=0;I<32;++I)
        {
            const double A=I*UE_TWO_PI/32.,B=(I+1)*UE_TWO_PI/32.;
            const double BandRadius=Radius*(.46+.27*Band);
            Ribbon(TEXT("ShadowGroundRibbon"),Violet,Center+FVector(FMath::Cos(A),FMath::Sin(A),0)*BandRadius,
                Center+FVector(FMath::Cos(B),FMath::Sin(B),0)*BandRadius,(.045*(1.-T)+.006)*Tile);
        }
    }
    for(const auto& Event:RecentReaperAttacks)
    {
        const double Age=State.elapsed-Event.time;if(Age<0 || Age>=.22)continue;
        const auto* Source=FindEntity(State,Event.source);auto* SourceVisual=Visual(Event.source);
        if(!Source || Source->dead || !SourceVisual || SourceVisual->IsDead() || SourceVisual->HiddenFromViewer())continue;
        const bool Ghost=SourceVisual->IsCloaked();
        const FName Key=Ghost?TEXT("ReaperCleaveGhost"):TEXT("ReaperCleaveViolet");
        const FLinearColor SlashViolet(.88f,.17f,1.f);
        auto* Slash=ExpansionBatch(Key,TEXT("projectile_trail"),SlashViolet);
        if(Slash)for(int32 I=0;I<Slash->GetNumMaterials();++I)if(auto* MID=Cast<UMaterialInstanceDynamic>(Slash->GetMaterial(I)))
        {MID->SetScalarParameterValue(TEXT("EffectOpacity"),Ghost?.18f:.65f);MID->SetScalarParameterValue(TEXT("EffectEmission"),.45f);}
        const double T=Age/.22,Radius=(.78+.50*T)*Tile;
        const FVector Center=URiftMatchSubsystem::WorldPoint(Event.position)+FVector(0,0,SourceVisual->GetActorLocation().Z+100.*SourceVisual->PresentationScale());
        const double Facing=FMath::Atan2(Source->facing.z,Source->facing.x);
        constexpr int32 Segments=20;
        for(int32 I=0;I<Segments;++I)
        {
            const double A=Facing-1.05+I*2.10/Segments+.25*T,B=Facing-1.05+(I+1)*2.10/Segments+.25*T;
            const FVector Start=Center+FVector(FMath::Cos(A),FMath::Sin(A),.10*FMath::Sin(A-Facing))*Radius,End=Center+FVector(FMath::Cos(B),FMath::Sin(B),.10*FMath::Sin(B-Facing))*Radius;
            const double Width=(.045*(1.-T)+.008)*Tile;
            Piece(Key,TEXT("projectile_trail"),SlashViolet,(Start+End)*.5,End-Start,FVector((End-Start).Length(),Width,Width*.65));
        }
    }
    for(const auto& Event:RecentCloakExpiries)
    {
        const double Age=State.elapsed-Event.time;if(Age<0 || Age>=.12)continue;
        const auto* Body=FindEntity(State,Event.target);if(!Body || Body->dead || Body->invisibleUntil>State.elapsed)continue;
        const double Radius=(Body->radius+.04)*Tile;
        const FVector Center=URiftMatchSubsystem::WorldPoint(Body->position,Tile*(.15+.85*Age/.12));
        for(int32 I=0;I<16;++I)
        {
            const double A=I*UE_TWO_PI/16.,B=(I+1)*UE_TWO_PI/16.;
            Ribbon(TEXT("CloakExpiryEdge"),Violet,Center+FVector(FMath::Cos(A),FMath::Sin(A),0)*Radius,
                Center+FVector(FMath::Cos(B),FMath::Sin(B),0)*Radius,.008*Tile*(1.-Age/.12));
        }
    }
    for(const auto& Child:State.entities)if(Child.parentEntityId && Child.cardId=="bone_warden_skeleton")
    {
        const double Age=State.elapsed-Child.born;if(Age<0 || Age>=.30)continue;
        const auto* Parent=FindEntity(State,Child.parentEntityId);if(!Parent)continue;
        if(Child.team==rift::Team::Enemy && Child.invisibleUntil>State.elapsed)continue;
        const FVector Start=URiftMatchSubsystem::WorldPoint(Parent->position,Tile*.82),End=URiftMatchSubsystem::WorldPoint(Child.position,Tile*.38);
        for(int32 I=0;I<5;++I)
        {
            const double A=I/5.,B=(I+1)/5.;
            auto Path=[&](double T){return FMath::Lerp(Start,End,T)+FVector(0,0,FMath::Sin(T*UE_PI)*Tile*.20);};
            Ribbon(TEXT("FourWardenSoulThreads"),Green,Path(A),Path(B),Tile*.015*(1.-Age/.30));
        }
    }
    auto Weather=[&](FName Key,FName Mesh,FLinearColor Color,FVector Origin,FVector Size,double Yaw=0.,FVector UpAxis=FVector::ZeroVector)
    {
        auto* Batch=ExpansionBatch(Key,Mesh,Color);if(!Batch)return;
        const FVector Bounds=Batch->GetStaticMesh()->GetBounds().BoxExtent*2.;
        const FVector Scale(Size.X/FMath::Max(.01,Bounds.X),Size.Y/FMath::Max(.01,Bounds.Y),Size.Z/FMath::Max(.01,Bounds.Z));
        const FQuat Rotation=UpAxis.IsNearlyZero()?FRotator(0,Yaw,0).Quaternion():FRotationMatrix::MakeFromZX(UpAxis,FVector(0,1,0)).ToQuat();
        Transforms.FindOrAdd(Batch).Add(FTransform(Rotation,Origin,Scale));
    };
    auto Storm=[&](rift::Vec2 Position,double Born,double Finish)
    {
        const double Age=State.elapsed-Born,Remaining=Finish-State.elapsed;if(Age<0 || Remaining<=0)return;
        const double Fade=FMath::Min(FMath::Clamp(Age/.18,0.,1.),FMath::Clamp(Remaining/.22,0.,1.));
        const FVector Center=URiftMatchSubsystem::WorldPoint(Position,Tile*4.8);
        Weather(TEXT("StormCloud"),TEXT("thunderstorm_cloud"),FLinearColor(.085f,.105f,.16f),Center,
            FVector(6.0,4.8,.95+.04*FMath::Sin(Age*9))*Tile*(.86+.14*Fade),FMath::Sin(Age*2)*3.);
        for(int32 I=0;I<48;++I)
        {
            const double Angle=I*2.3999632297,Radius=2.88*FMath::Sqrt((I+.5)/48.);
            const double Fall=FMath::Fmod(Age*2.8+I*.173,1.);
            const FVector Drop=URiftMatchSubsystem::WorldPoint(Position)+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,.35+(Center.Z/Tile-.35)*(1.-Fall))*Tile;
            Weather(TEXT("StormRain"),TEXT("thunderstorm_rain"),FLinearColor(.55f,.75f,1.f),Drop,FVector(.04,.04,.35)*Tile*Fade);
        }
    };
    for(const auto& Cast:State.spellCasts)if(Cast.cardId=="thunderstorm")
        if(const auto* Card=rift::FindCard(Cast.cardId))Storm(Cast.position,Cast.born,Cast.born+Card->castDelay+(Card->rounds-1)*Card->dotInterval+.22);
    for(const auto& Impact:RecentSpellImpacts)if(Impact.cardId=="thunderstorm")
    {
        const auto* Card=rift::FindCard(Impact.cardId);if(!Card)continue;
        const double Age=State.elapsed-Impact.time;
        if(Impact.count==Card->rounds)Storm(Impact.position,Impact.time-Card->castDelay-(Card->rounds-1)*Card->dotInterval,Impact.time+.22);
        if(Age>=0 && Age<.13)
        {
            const double Flash=1.-Age/.13;
            const FVector Cloud=URiftMatchSubsystem::WorldPoint(Impact.position,Tile*4.8);
            const FVector Top=Cloud+FVector(0,2.6*Tile,-.15*Tile);
            const FVector End=URiftMatchSubsystem::WorldPoint(Impact.position)+FVector(0,2.8*Tile,.35*Tile);
            const FVector Up=Top-End;
            Weather(TEXT("StormAreaFlash"),TEXT("thunderstorm_bolt"),FLinearColor(.82f,.92f,1.f),Top,FVector(.13*Tile,.55*Tile*Flash,Up.Length()),0.,Up);
        }
    }
    RecentLightningHits.RemoveAllSwap([&](const rift::Event& Hit){return State.elapsed-Hit.time>=.17 || State.elapsed<Hit.time;});
    for(const auto& Hit:RecentLightningHits)
    {
        const double Age=State.elapsed-Hit.time,Flash=(1.-Age/.17)*(.80+.20*FMath::Cos(Age*130));
        auto* Target=Visual(Hit.target);if(Target && Target->HiddenFromViewer())continue;
        FVector End=Target?Target->ImpactLocation():URiftMatchSubsystem::WorldPoint(Hit.position,Tile*.4);
        if(const auto* Body=FindEntity(State,Hit.target))End.Y=FMath::Max(End.Y,URiftMatchSubsystem::WorldPoint(Body->position).Y+Body->radius*Tile);
        if(Target)
        {
            TInlineComponentArray<UPrimitiveComponent*> Components(Target);
            for(const auto* Component:Components)
                if(Component->IsVisible() && Component->Bounds.SphereRadius>UE_SMALL_NUMBER &&
                    (Component->IsA<USkeletalMeshComponent>() || (Component->IsA<UStaticMeshComponent>() && !Component->IsA<UInstancedStaticMeshComponent>())))
                    End.Y=FMath::Max(End.Y,Component->Bounds.Origin.Y+Component->Bounds.BoxExtent.Y);
        }
        End.Y+=.12*Tile;
        rift::Vec2 CastCenter=Hit.position;
        for(const auto& Impact:RecentSpellImpacts)if(Impact.cardId=="thunderstorm" && Impact.playId==Hit.playId)CastCenter=Impact.position;
        const FVector Cloud=URiftMatchSubsystem::WorldPoint(CastCenter,Tile*4.8);
        const FVector Top(FMath::Clamp(End.X,Cloud.X-2.7*Tile,Cloud.X+2.7*Tile),Cloud.Y+2.6*Tile,FMath::Max(Cloud.Z-.15*Tile,End.Z+1.25*Tile));
        const FVector Up=Top-End;
        Weather(TEXT("StormTargetLightning"),TEXT("thunderstorm_bolt"),FLinearColor(.82f,.92f,1.f),Top,FVector(.13*Tile,.55*Tile*Flash,Up.Length()),0.,Up);
    }
    for(const auto& Entity:State.entities)if(!Entity.dead && Entity.stunUntil>State.elapsed)
    {
        auto* Unit=Visual(Entity.id);if(!Unit || Unit->HiddenFromViewer())continue;
        const FVector Center=Unit->StatusLocation();
        for(int32 I=0;I<3;++I)
        {
            const double Angle=State.elapsed*10+I*UE_TWO_PI/3.;
            Weather(TEXT("UniversalStunStars"),TEXT("stun_spark"),FLinearColor(.72f,.92f,1.f),Center+FVector(FMath::Cos(Angle)*.55,FMath::Sin(Angle)*.55,.05*FMath::Sin(Angle*2))*Tile,FVector(.32,.32,.045)*Tile,Angle*180./UE_PI);
        }
    }
    for(auto& Pair:Transforms)Pair.Key->AddInstances(Pair.Value,false,true,false);

}
void ARiftArenaPresentation::OnSimulationEvent(const rift::Event& Event)
{
    if(Audio)Audio->OnSimulationEvent(Event,URiftMatchSubsystem::WorldPoint(Event.position,25));
    // The spawn clock resets before the child arrives. Preserve its parent's
    // open hatch through synchronization instead of briefly blending to Idle.
    if(Event.type=="entity_spawn" && Event.reason=="periodic_spawn")
        if(auto* Parent=Visual(Event.parentEntityId))Parent->SpecialAt(TEXT("Spawn"),Event.time-.16,.60f);
    // Deployment events may arrive before this actor's next tick. Build current
    // visual replicas first so attacks, destruction and audio use real anchors.
    if (Event.type=="match_start" || Event.type=="entity_spawn" ||
        (Event.source && !Visual(Event.source)) || (Event.target && !Visual(Event.target))) Synchronize(0.f);
    auto* Source=Visual(Event.source);auto* Target=Visual(Event.target);
    const auto* State=ViewState();
    const FVector Position=Target?Target->ImpactLocation():URiftMatchSubsystem::WorldPoint(Event.position,25);
    const bool HiddenTarget=State && Event.target && FindEntity(*State,Event.target) &&
        FindEntity(*State,Event.target)->team==rift::Team::Enemy && FindEntity(*State,Event.target)->invisibleUntil>State->elapsed;
    const bool HiddenSource=State && Event.source && FindEntity(*State,Event.source) &&
        FindEntity(*State,Event.source)->team==rift::Team::Enemy && FindEntity(*State,Event.source)->invisibleUntil>State->elapsed;
    if(Event.cardId=="thunderstorm" && !HiddenTarget && (Event.type=="damage" || Event.type=="shield_damage"))
    {
        const bool Recorded=RecentLightningHits.ContainsByPredicate([&](const rift::Event& Hit){return Hit.playId==Event.playId && Hit.target==Event.target && FMath::Abs(Hit.time-Event.time)<1.e-7;});
        if(!Recorded){RecentLightningHits.Add(Event);if(RecentLightningHits.Num()>256)RecentLightningHits.RemoveAt(0);}
    }
    if (Event.type=="entity_spawn")
    {
        if(Event.cardId!="mini_stampede" && Event.cardId!="stampede" && Event.cardId!="pulverizer")
            SpawnEffect(TEXT("Deploy"),URiftMatchSubsystem::WorldPoint(Event.position,8),Event.team,65*RiftArenaGeometry::WorldScale);
    }
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
        const FName Sound=Event.cardId=="dart_gun"?TEXT("dart_fire"):Event.cardId=="ironclad"?TEXT("sword_attack"):Event.cardId=="twin_blades"?TEXT("dual_attack"):
            Event.cardId=="ember_archer"||Event.cardId=="archer_tower"?TEXT("bow_release"):Event.cardId=="boulderback"?TEXT("heavy_slam"):
            Event.cardId=="rambeast"?TEXT("charge"):Event.cardId=="arc_mage"?TEXT("arc_cast"):Event.cardId=="sky_manta"?TEXT("manta_cast"):
            Event.cardId=="vampire_bats"?TEXT("bat_bite"):Event.cardId=="frost_fang"?TEXT("frost_attack"):
            Event.cardId=="storm_raven"?TEXT("storm_bolt"):TEXT("heavy_slam");
        PlayEventSound(Sound,Origin,Event.cardId=="vampire_bats"?.18f:Event.cardId=="dart_gun"?.26f:.45f);
        if (Event.cardId=="frost_fang" && !HiddenSource)
        {auto* FX=SpawnEffect(TEXT("Frost"),Origin,Event.team,70*RiftArenaGeometry::WorldScale);SetEffectParameters(FX,Event.team,70*RiftArenaGeometry::WorldScale,Origin,Position);}
    }
    else if (Event.type=="melee_cleave" && Event.cardId=="aether_reaper" && Event.count>=2)
    {
        // The simulation emits this once only after two or more real victims
        // received that strike. One target cannot create a fake cleave flash.
        RecentReaperAttacks.Add(Event);if(RecentReaperAttacks.Num()>64)RecentReaperAttacks.RemoveAt(0);
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
        if(!HiddenSource)
        {
            const float FlashSize=(Event.cardId=="dart_gun"?11.f:28.f)*RiftArenaGeometry::WorldScale;
            auto* Flash=SpawnEffect(TEXT("Impact"),Origin,Event.team,FlashSize);
            if (Flash) Flash->SetWorldRotation((Position-Origin).Rotation());
            SetEffectParameters(Flash,Event.team,FlashSize,Origin,Position,EffectColor(FlightFor(Event.cardId),Event.team));
        }
    }
    else if (Event.type=="damage")
    {
        if (Target && Event.amount>0) Target->HitAt(Event.time,Event.amount);
        if(HiddenTarget)return;
        if(Target && Event.amount>0 && Event.damageKind!="dot" && Event.damageKind!="aura" && Event.cardId!="pulverizer")
        {
            FContactAccent Accent;Accent.Position=Position;Accent.Born=Event.time;
            Accent.Direction=Source?(Position-Source->ImpactLocation()).GetSafeNormal():FVector::UpVector;
            Accent.Heavy=Event.cardId=="boulderback"||Event.cardId=="iron_giant"||Event.cardId=="rambeast";
            const FString& MaterialFamily=Target->PresentationAssetId();
            Accent.Color=MaterialFamily==TEXT("bone_warden")||MaterialFamily==TEXT("bone_warden_skeleton")?FLinearColor(.90f,.80f,.56f):
                MaterialFamily==TEXT("aether_reaper")?FLinearColor(.55f,.18f,.88f):
                MaterialFamily==TEXT("frost_fang")?FLinearColor(.37f,.78f,.94f):
                MaterialFamily==TEXT("ironclad")||MaterialFamily==TEXT("iron_giant")||Target->IsTower()?FLinearColor(1.f,.68f,.24f):FLinearColor(.58f,.49f,.34f);
            ContactAccents.Add(Accent);if(ContactAccents.Num()>64)ContactAccents.RemoveAt(0);
        }
        const bool ArcPrimary=Event.cardId=="arc_mage" && Event.damageKind=="attack";
        const FName FX=Event.damageKind=="dot"?TEXT("MeteorTick"):Event.cardId=="frost_fang"?TEXT("Frost"):ArcPrimary||Event.cardId=="pulverizer"?TEXT("Nova"):TEXT("Impact");
        const float Radius=(Event.cardId=="dart_gun"?22.f:ArcPrimary?155.f:Event.damageKind=="splash"?85.f:Event.cardId=="storm_raven"?80.f:Event.cardId=="sky_manta"?62.f:55.f)*RiftArenaGeometry::WorldScale;
        // The traveling ground front and short ankle ripple carry Pulverizer
        // contact; a full Nova cloud would obscure both the herd and tile grid.
        if(Event.cardId!="pulverizer")
        {
            auto* Impact=SpawnEffect(FX,Position,Event.team,Radius);
            SetEffectParameters(Impact,Event.team,Radius,Position,Position,EffectColor(FlightFor(Event.cardId),Event.team));
        }
        if (Event.damageKind!="initial" && Event.damageKind!="aura" && Event.damageKind!="dot")
        {
            // Contact has a material identity. Structures keep their stone/wood
            // impact; teeth, blunt strikes and elemental hits use matching cues.
            // Shared attack/contact cues are aggregated within the same tick
            // so one authoritative strike cannot produce duplicate loud sounds.
            const FName Sound=Event.targetKind==rift::EntityKind::Guard||Event.targetKind==rift::EntityKind::Core?TEXT("tower_hit"):
                Event.targetKind==rift::EntityKind::Building?TEXT("building_hit"):
                Event.cardId=="vampire_bats"?TEXT("bat_bite"):
                Event.cardId=="dart_gun"?TEXT("dart_hit"):
                Event.cardId=="rambeast"||Event.cardId=="boulderback"?TEXT("heavy_slam"):
                Event.cardId=="frost_fang"?TEXT("frost_attack"):
                Event.cardId=="storm_raven"?TEXT("storm_bolt"):
                Event.cardId=="ember_archer"||Event.cardId=="archer_tower"?TEXT("arrow_hit"):
                Event.cardId=="arc_mage"||Event.cardId=="pulverizer"?TEXT("arc_impact"):Event.cardId=="sky_manta"?TEXT("manta_impact"):
                TEXT("sword_hit");
            PlayEventSound(Sound,Position,Event.count>=5?.14f:.3f);
        }
    }
    else if(Event.type=="shield_damage")
    {
        if(Target)Target->ShieldHitAt(Event.time,Event.amount);
        if(!HiddenTarget)PlayEventSound(Event.shieldOverflowBlocked>0 || (State && FindEntity(*State,Event.target) && FindEntity(*State,Event.target)->shieldHP<=0)?TEXT("shield_break"):TEXT("shield_hit"),Position,.35f);
        if(State)Synchronize(0.f);
    }
    else if(Event.type=="invisibility_apply" || Event.type=="invisibility_expire")
    {
        if(Event.type=="invisibility_expire"){RecentCloakExpiries.Add(Event);if(RecentCloakExpiries.Num()>64)RecentCloakExpiries.RemoveAt(0);}
        if(State)Synchronize(0.f);
    }
    else if (Event.type=="death")
    {
        if (Target) Target->DieAt(Event.time);
        const auto* Dying=State?FindEntity(*State,Event.target):nullptr;
        const bool Pulverizer=(Target && Target->PresentationAssetId()==TEXT("pulverizer")) || (Dying && Dying->cardId=="pulverizer");
        if (Event.targetKind==rift::EntityKind::Building)
        {
            if(!Pulverizer)SpawnEffect(TEXT("TowerDestroy"),Position,Event.targetTeam,140*RiftArenaGeometry::WorldScale);
            PlayEventSound(Pulverizer?TEXT("building_hit"):TEXT("tower_destroy"),Position,Pulverizer?.3f:.55f);
        }
    }
    else if (Event.type=="tower_destroy")
    {SpawnEffect(TEXT("TowerDestroy"),Position,Event.targetTeam,(Event.targetKind==rift::EntityKind::Core?270:190)*RiftArenaGeometry::WorldScale);PlayEventSound(TEXT("tower_destroy"),Position,.8f);}
    else if (Event.type=="core_activate")
    {if (Target) Target->ActivateCore();SpawnEffect(TEXT("CoreAwaken"),Position,Event.team,180*RiftArenaGeometry::WorldScale);PlayEventSound(TEXT("core_awaken"),Position,.6f);}
    else if(Event.type=="building_spawn")
    {if(Source)Source->SpecialAt(TEXT("Spawn"),Event.time-.16,.60f);if(!HiddenSource)PlayEventSound(TEXT("blade_release"),Position,.28f);}
    else if (Event.type=="slow" || Event.type=="stun")
    {if (State) SynchronizeStatuses(*State);if (Event.type=="stun" && Source) Source->SpecialAt(TEXT("StunDischarge"),Event.time,.3f);
        if (Event.cardId!="pulverizer" && Event.cardId!="thunderstorm") PlayEventSound(Event.type=="slow"?TEXT("frost_slow"):TEXT("stun"),Position,.25f);}
    else if (Event.type=="aura")
    {if (Source) Source->SpecialAt(TEXT("Pulse"),Event.time);if(!HiddenSource)SpawnEffect(TEXT("Aura"),Source?Source->GetActorLocation():Position,Event.team,Event.amount*RiftArenaGeometry::UnitsPerTile);PlayEventSound(TEXT("storm_pulse"),Position,.55f);}
    else if (Event.type=="hazard_tick" && State)
    {
        for (const auto& Hazard:State->hazards) if (Hazard.playId==Event.playId)
        {const FVector Center=URiftMatchSubsystem::WorldPoint(Hazard.position,8);SpawnEffect(TEXT("MeteorTick"),Center,Hazard.team,Hazard.radius*RiftArenaGeometry::UnitsPerTile);PlayEventSound(TEXT("meteor_tick"),Center,.35f);break;}
    }
    else if (Event.type=="spell_cast")
    {
        if (State) Synchronize(0.f);
    }
    else if (Event.type=="spell_impact" || (Event.type=="card_play" && Event.until<=Event.time))
    {
        const auto* Card=rift::FindCard(Event.cardId);if (Card && Card->spell)
        {
            if(Event.cardId=="thunderstorm")
            {
                if(Event.count>=Card->rounds)RemoveSpellCast(Event.playId);++SpellImpactEvents;RecentSpellImpacts.Add(Event);
                if(RecentSpellImpacts.Num()>32)RecentSpellImpacts.RemoveAt(0);
                PlayEventSound(TEXT("thunder_strike"),URiftMatchSubsystem::WorldPoint(Event.position),.48f);
                if(State)SynchronizeExpansionEffects(*State);return;
            }
            if(Event.cardId=="shadow_surge")
            {
                RemoveSpellCast(Event.playId);++SpellImpactEvents;RecentSpellImpacts.Add(Event);
                if(RecentSpellImpacts.Num()>16)RecentSpellImpacts.RemoveAt(0);
                PlayEventSound(TEXT("shadow_surge"),URiftMatchSubsystem::WorldPoint(Event.position),.45f);
                if(State)SynchronizeExpansionEffects(*State);return;
            }
            const FName FX=Event.cardId=="bullet_burst"?TEXT("BulletBurst"):Event.cardId=="nova_flask"?TEXT("Nova"):TEXT("Meteor");
            const FName Sound=Event.cardId=="bullet_burst"?TEXT("bullet_burst"):Event.cardId=="nova_flask"?TEXT("nova_impact"):TEXT("meteor_impact");
            auto* EffectComponent=SpawnEffect(FX,URiftMatchSubsystem::WorldPoint(Event.position,12),Event.team,Card->spellRadius*RiftArenaGeometry::UnitsPerTile);
            if (Event.type=="spell_impact")
            {
                RemoveSpellCast(Event.playId);++SpellImpactEvents;RecentSpellImpacts.Add(Event);
                if (RecentSpellImpacts.Num()>16) RecentSpellImpacts.RemoveAt(0);
                if (EffectComponent) SpellImpactEffects.Add({EffectComponent,Event.time});
            }
            PlayEventSound(Sound,Position,.75f);
            SpawnSpellDebris(Event);
        }
    }
    else if (Event.type=="phase") PlayEventSound(Event.reason=="tiebreaker"?TEXT("tiebreaker"):TEXT("overtime"),FVector::ZeroVector,.65f);
    else if (Event.type=="match_end" && State) PlayEventSound(State->winner==0?TEXT("victory"):TEXT("defeat"),FVector::ZeroVector,.75f);
}
void ARiftArenaPresentation::SampleSpellImpactEffectsForQA()
{
    // Explicit capture fixture only: sample real impact graphs at the same
    // battle time as the paused models, before render/PSO warmup frames pass.
    const auto* State=ViewState();if (!State) return;
    for (const auto& Timed:SpellImpactEffects) if (auto* Component=Timed.Component.Get())
    {
        const float Age=float(FMath::Max(1./60.,State->elapsed-Timed.Born));
        Component->SetAutoDestroy(false);Component->SetForceSolo(true);Component->ReinitializeSystem();
        Component->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);Component->SetDesiredAge(Age);
        Component->SetComponentTickEnabled(false);const int32 Steps=FMath::Max(1,FMath::CeilToInt(Age*100.f));
        Component->AdvanceSimulation(Steps,Age/Steps);
        if (auto Controller=Component->GetSystemInstanceController()) Controller->WaitForConcurrentTickAndFinalize();
        Component->SetComponentTickEnabled(false);
    }
}
void ARiftArenaPresentation::SetPlacementPreview(FVector2D Tile,bool Valid,float RadiusTiles,bool Spell,float BuildingFootprintTiles)
{
    PlacementTile=Tile;PlacementRadius=FMath::Max(0.f,RadiusTiles);PlacementFootprint=FMath::Max(0.f,BuildingFootprintTiles);
    bPlacementValid=Valid;bPlacementSpell=Spell;bPreview=true;
    const float Half=Spell?FMath::Max(50.f*RiftArenaGeometry::WorldScale,RadiusTiles*RiftArenaGeometry::UnitsPerTile):FMath::Max(50.f*RiftArenaGeometry::WorldScale,BuildingFootprintTiles*RiftArenaGeometry::UnitsPerTile*.5f);
    PlacementDecal->SetWorldLocation(FVector(Tile.X*RiftArenaGeometry::UnitsPerTile,Tile.Y*RiftArenaGeometry::UnitsPerTile,14*RiftArenaGeometry::WorldScale));PlacementDecal->DecalSize=FVector(36*RiftArenaGeometry::WorldScale,Half,Half);
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
