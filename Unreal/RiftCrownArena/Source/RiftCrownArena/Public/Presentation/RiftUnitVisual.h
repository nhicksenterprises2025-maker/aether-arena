#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Simulation/RiftSimulation.h"
#include "RiftUnitVisual.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class UDecalComponent;
class UAnimSequence;
class URiftCardData;
class UMaterialInstanceDynamic;

/** Visual replica of an authoritative entity. Contains no collision or gameplay logic. */
UCLASS()
class RIFTCROWNARENA_API ARiftUnitVisual : public AActor
{
    GENERATED_BODY()
public:
    ARiftUnitVisual();
    bool InitializeEntity(const rift::Entity& Entity, URiftCardData* Card, double Time);
    void Synchronize(const rift::Entity& Entity, const rift::Snapshot& State, float DeltaSeconds);
    void AttackAt(double Time, double Damage);
    void HitAt(double Time);
    void DieAt(double Time);
    void AdvanceDeath(double Time);
    void SpecialAt(FName Name,double Time,float Duration=.5f);
    void ActivateCore();
    bool IsExpired(double Time) const;
    FVector HealthLocation() const;
    FVector AttackLocation() const;
    FVector ImpactLocation() const;
    FVector StatusLocation() const;
    uint64 EntityId=0;
    bool IsTower() const { return Kind!=rift::EntityKind::Troop; }
    bool IsDead() const { return bDead; }
    const FString& PresentationAssetId() const { return AssetId; }
    FName CurrentAnimation() const { return CurrentClip; }
    FString AnimationAssetPath() const;
    float AnimationPosition() const;
    static FLinearColor TeamColor(rift::Team Team);
private:
    void SetClip(FName Name, double Position, bool Loop=false);
    UAnimSequence* Clip(FName Name);
    void ColorMesh(class UMeshComponent* Component, FLinearColor Color);
    UPROPERTY() TObjectPtr<USceneComponent> Scene;
    UPROPERTY() TObjectPtr<USkeletalMeshComponent> Character;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Structure;
    UPROPERTY() TObjectPtr<UDecalComponent> GroundRing;
    UPROPERTY() TObjectPtr<URiftCardData> CardData;
    UPROPERTY() TMap<FName,TObjectPtr<UAnimSequence>> Clips;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    FString AssetId;
    FName CurrentClip;
    FName SpecialClip;
    rift::EntityKind Kind=rift::EntityKind::Troop;
    rift::Team Team=rift::Team::Player;
    FVector PreviousPosition=FVector::ZeroVector;
    double Born=0, DeathTime=-1, LastAttack=-100, LastHit=-100, AttackInterval=1;
    double TurnTime=-100, AcquireTime=-100;
    double SpecialTime=-100;
    double AnimationTime=-1, GaitClock=0;
    float SpecialDuration=.5f;
    uint64 LastTarget=0;
    float Scale=1.f, AnimationPhase=0.f;
    bool bDead=false, bFlying=false, bCoreActive=false;
};
