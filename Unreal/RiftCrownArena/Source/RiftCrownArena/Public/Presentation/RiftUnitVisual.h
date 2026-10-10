#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "GameFramework/Actor.h"
#include "Simulation/RiftSimulation.h"
#include "RiftUnitVisual.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class UDecalComponent;
class UAnimSequence;
class URiftCardData;
class UMaterialInstanceDynamic;

/** Evaluates authored clips at exact simulation time and blends their local poses. */
UCLASS(Transient, NotBlueprintable)
class RIFTCROWNARENA_API URiftUnitAnimInstance : public UAnimSingleNodeInstance
{
    GENERATED_BODY()
public:
    void SetVisualTime(double Time);
    void BeginVisualTransition(double Time, float Duration, bool Snap);
    float VisualBlendAlpha() const;
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
};

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
    void AimAt(FVector Target);
    void HitAt(double Time, double Damage);
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
    bool HasBeenDamaged() const { return bHasBeenDamaged; }
    const FString& PresentationAssetId() const { return AssetId; }
    FName CurrentAnimation() const { return CurrentClip; }
    FString AnimationAssetPath() const;
    float AnimationPosition() const;
    float AnimationCycleFraction() const { return CurrentFraction; }
    float AnimationBlendAlpha() const;
    double LocomotionPhase() const { return GaitClock; }
    float PresentationScale() const { return Kind==rift::EntityKind::Troop?Scale:StructureHeightScale; }
    float GuardRecoilDistance() const { return CannonRecoil; }
    static FLinearColor TeamColor(rift::Team Team);
private:
    void SetClip(FName Name, double Position, bool Loop=false, bool Snap=false);
    FName AttackClip() const;
    void UpdateStructureAim(double Time);
    void UpdateDamageHistory(const rift::Entity& Entity, double Time);
    UAnimSequence* Clip(FName Name);
    void ColorMesh(class UMeshComponent* Component, FLinearColor Color);
    UPROPERTY() TObjectPtr<USceneComponent> Scene;
    UPROPERTY() TObjectPtr<USkeletalMeshComponent> Character;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Structure;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Cannon;
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
    double AnimationTime=-1, VisualTime=0, GaitClock=0, DeathFlightHeight=0;
    double LastSynchronizedCooldown=-1;
    float SpecialDuration=.5f;
    uint64 LastTarget=0;
    float Scale=1.f, StructureHeightScale=1.f, AnimationPhase=0.f, CurrentFraction=0.f;
    float FlightBank=0.f, ArcherPerchHeight=0.f, StructureBaseYaw=0.f, CannonRecoil=0.f;
    bool bDead=false, bFlying=false, bCoreActive=false, bHasBeenDamaged=false;
};
