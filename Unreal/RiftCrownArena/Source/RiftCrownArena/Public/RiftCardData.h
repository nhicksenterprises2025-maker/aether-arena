#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RiftCardData.generated.h"

class USkeletalMesh;
class UStaticMesh;
class UAnimSequence;
class UTexture2D;
class UNiagaraSystem;
class USoundBase;

UCLASS(BlueprintType)
class RIFTCROWNARENA_API URiftCardData : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FString CardId;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FText DisplayName;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FText Description;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FText CombatClass;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) int32 Cost=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) int32 Count=1;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float HP=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float Damage=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float AttackSpeed=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float MoveSpeed=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float AttackRange=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float BaseModelScale=1;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float FrontSight=8;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float RearSight=5;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float ProjectileSpeed=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float SplashRadius=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float Lifetime=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float Footprint=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float TowerDamage=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float SpellRadius=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float CastDelay=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float ChargeDamage=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float SlowPct=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float SlowDuration=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float AuraDamage=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float AuraRadius=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float AuraInterval=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float StunDuration=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float DotDamage=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float DotDuration=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float DotInterval=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) int32 Rounds=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) bool Flying=false;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) bool GroundAndAir=false;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) bool StructuresOnly=false;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) bool Spell=false;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) bool Building=false;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TObjectPtr<USkeletalMesh> CharacterMesh;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMesh> StructureMesh;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TObjectPtr<UTexture2D> Illustration;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TMap<FName,TObjectPtr<UAnimSequence>> Animations;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TMap<FName,TObjectPtr<UNiagaraSystem>> Effects;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TMap<FName,TObjectPtr<USoundBase>> Sounds;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FLinearColor Accent=FLinearColor(0.2f,0.7f,1.f);
    FPrimaryAssetId GetPrimaryAssetId()const override{return FPrimaryAssetId(TEXT("RiftCard"),FName(*CardId));}
};
