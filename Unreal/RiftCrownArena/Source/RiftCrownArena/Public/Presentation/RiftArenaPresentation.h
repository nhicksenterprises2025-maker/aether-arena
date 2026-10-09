#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Simulation/RiftSimulation.h"
#include "RiftArenaPresentation.generated.h"

class ARiftUnitVisual;
class URiftMatchSubsystem;
class URiftBattleOverlay;
class URiftBattleAudioSubsystem;
class UNiagaraComponent;
class UNiagaraSystem;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UDecalComponent;

UCLASS()
class RIFTCROWNARENA_API ARiftArenaPresentation : public AActor
{
    GENERATED_BODY()
public:
    ARiftArenaPresentation();
    void BeginPlay() override;
    void Tick(float DeltaSeconds) override;
    void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION(BlueprintCallable) void SetPlacementPreview(FVector2D Tile, bool Valid, float RadiusTiles=0.f,
        bool Spell=false, float BuildingFootprintTiles=0.f);
    UFUNCTION(BlueprintCallable) void ClearPlacementPreview();
    const rift::Snapshot* ViewState() const;
    ARiftUnitVisual* Visual(uint64 Id) const;
    bool HasPlacementPreview() const { return bPreview; }
    FVector2D PreviewTile() const { return PlacementTile; }
    float PreviewRadius() const { return PlacementRadius; }
    float PreviewFootprint() const { return PlacementFootprint; }
    bool PreviewIsSpell() const { return bPlacementSpell; }
    bool PreviewIsValid() const { return bPlacementValid; }
    // On-demand native QA; never runs during ordinary rendering.
    FString NiagaraDiagnosticsJSON();
    int32 TrainingOverlayLineCount()const;
    int32 TrainingOverlayLabelCount()const;
    void ShowcaseNiagara();
    void ShowcaseNiagaraAtAge(float Age);
private:
    void ConstructArena();
    UHierarchicalInstancedStaticMeshComponent* Instances(FName Mesh, int32 Team=-1);
    void Place(FName Mesh, FVector Position, FRotator Rotation=FRotator::ZeroRotator,
        FVector Scale=FVector::OneVector, int32 Team=-1);
    void Synchronize(float DeltaSeconds);
    void SynchronizeProjectiles(const rift::Snapshot& State);
    void SynchronizeHazards(const rift::Snapshot& State);
    void SynchronizeStatuses(const rift::Snapshot& State);
    void OnSimulationEvent(const rift::Event& Event);
    void OnMatchChanged();
    void ClearVisuals();
    UNiagaraSystem* Effect(FName Name);
    UNiagaraComponent* SpawnEffect(FName Name, FVector Position, rift::Team Team,
        float Radius=80.f, bool Persistent=false);
    void SetEffectParameters(UNiagaraComponent* Effect, rift::Team Team, float Radius,
        FVector Source, FVector Target,FLinearColor Color=FLinearColor::Transparent);
    void PlayEventSound(FName Name, FVector Position, float Gain=1.f);
    UPROPERTY() TObjectPtr<USceneComponent> Scene;
    UPROPERTY() TObjectPtr<URiftMatchSubsystem> Match;
    UPROPERTY() TObjectPtr<URiftBattleAudioSubsystem> Audio;
    UPROPERTY() TObjectPtr<URiftBattleOverlay> Overlay;
    UPROPERTY() TObjectPtr<UDecalComponent> PlacementDecal;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PlacementMaterial;
    UPROPERTY() TMap<uint64,TObjectPtr<ARiftUnitVisual>> Units;
    UPROPERTY() TMap<FName,TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Environment;
    UPROPERTY() TMap<FName,TObjectPtr<UNiagaraSystem>> Effects;
    UPROPERTY() TMap<uint64,TObjectPtr<UNiagaraComponent>> Projectiles;
    UPROPERTY() TMap<uint64,TObjectPtr<UNiagaraComponent>> Hazards;
    UPROPERTY() TMap<uint64,TObjectPtr<UNiagaraComponent>> SlowEffects;
    UPROPERTY() TMap<uint64,TObjectPtr<UNiagaraComponent>> StunEffects;
    UPROPERTY() TMap<uint32,TObjectPtr<UMaterialInstanceDynamic>> ParticleMaterials;
    TMap<uint64,FVector> ProjectileOrigins;
    TMap<uint64,double> NextFrostBreath;
    TArray<TWeakObjectPtr<UNiagaraComponent>> TransientEffects;
    TArray<TWeakObjectPtr<UNiagaraComponent>> ShowcaseEffects;
    float ShowcaseSampleAge=-1.f;
    TSet<FName> MissingAssets;
    FDelegateHandle EventHandle, MatchHandle;
    uint32 LastSeed=0;
    double LastTime=-1;
    float ResultDeathClock=0;
    int32 AetherStage=1;
    int32 FrostBreathPuffs=0;
    FVector2D PlacementTile=FVector2D::ZeroVector;
    float PlacementRadius=0, PlacementFootprint=0;
    bool bPreview=false, bPlacementValid=false, bPlacementSpell=false;
};
