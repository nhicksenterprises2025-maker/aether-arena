#include "RiftAssetLibrary.h"
#include "RiftDiagnostics.h"
#include "RiftProfileSubsystem.h"
#include "Simulation/RiftSimulation.h"
#include "Serialization/JsonSerializer.h"

namespace {
FString SerializeDefinitions(const std::vector<rift::Card>& Definitions)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    for(const auto& C:Definitions)
    {
        auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("id"),UTF8_TO_TCHAR(C.id.c_str()));O->SetStringField(TEXT("name"),UTF8_TO_TCHAR(C.name.c_str()));
#define RF(K) O->SetNumberField(TEXT(#K),double(C.K))
        RF(cost);RF(count);RF(hp);RF(damage);RF(attackInterval);RF(moveSpeed);RF(range);RF(scale);RF(collisionRadius);RF(targetMask);RF(projectileSpeed);RF(shockwaveSpeed);RF(splash);RF(lifetime);RF(footprint);RF(towerDamage);RF(spellRadius);RF(castDelay);RF(chargeDamage);RF(slowPct);RF(slowDuration);RF(auraDamage);RF(auraInterval);RF(auraRadius);RF(stunDuration);RF(dotDamage);RF(dotDuration);RF(dotInterval);RF(rounds);
        RF(chargeMoveSeconds);RF(chargedMoveSpeed);RF(hpDecayPerSecond);RF(initialAttackCooldown);
        RF(shieldHP);RF(deathSpawnCount);RF(invisibilityDuration);RF(dotStructureDamage);RF(healthCloakThreshold);RF(spawnInterval);
#undef RF
#define RB(K) O->SetBoolField(TEXT(#K),C.K)
        RB(flying);RB(canHitAir);RB(structuresOnly);RB(spell);RB(building);
        RB(hardExpiry);RB(shieldOverflowProtected);RB(globalFriendlyInvisibility);RB(breakExistingLocks);
#undef RB
        O->SetStringField(TEXT("deathSpawnId"),UTF8_TO_TCHAR(C.deathSpawnId.c_str()));
        O->SetStringField(TEXT("spawnId"),UTF8_TO_TCHAR(C.spawnId.c_str()));
        Values.Add(MakeShared<FJsonValueObject>(O));
    }
    FString Output;FJsonSerializer::Serialize(Values,TJsonWriterFactory<>::Create(&Output));return Output;
}
}
FString URiftAssetLibrary::CardDefinitionsJSON(){return SerializeDefinitions(rift::Cards());}
FString URiftAssetLibrary::EntityDefinitionsJSON(){return SerializeDefinitions(rift::EntityDefinitions());}
void URiftAssetCatalogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    for(const auto& C:rift::Cards())
    {
        FString Id=UTF8_TO_TCHAR(C.id.c_str());auto* Data=LoadObject<URiftCardData>(nullptr,*FString::Printf(TEXT("/Game/Rift/Cards/DA_%s.DA_%s"),*Id,*Id));
        if(Data)Cards.Add(Id,Data);else RIFT_LOG(LogRift,Error,TEXT("Card asset missing: %s"),*Id);
    }
    for(const auto& C:rift::EntityDefinitions())
    {
        FString Id=UTF8_TO_TCHAR(C.id.c_str());auto* Data=LoadObject<URiftCardData>(nullptr,*FString::Printf(TEXT("/Game/Rift/Entities/DA_%s.DA_%s"),*Id,*Id));
        if(Data)Cards.Add(Id,Data);else RIFT_LOG(LogRift,Error,TEXT("Entity asset missing: %s"),*Id);
    }
    FString Error;if(!Validate(Error))RIFT_LOG(LogRift,Error,TEXT("Asset catalog validation: %s"),*Error);
}
URiftCardData* URiftAssetCatalogSubsystem::Card(const FString& Id)const{auto* C=Cards.Find(Id);return C?C->Get():nullptr;}
bool URiftAssetCatalogSubsystem::Validate(FString& Error)const
{
    auto Definitions=rift::Cards();
    const auto Children=rift::EntityDefinitions();Definitions.insert(Definitions.end(),Children.begin(),Children.end());
    for(const auto& C:Definitions)
    {
        const bool SkeletalBuilding=C.building&&(C.shockwaveSpeed>0||C.spawnInterval>0);
        auto* A=Card(UTF8_TO_TCHAR(C.id.c_str()));if(!A||(rift::FindCard(C.id)&&!A->Illustration)||(((!C.spell&&!C.building)||SkeletalBuilding)&&!A->CharacterMesh)||(C.building&&!A->StructureMesh&&!(SkeletalBuilding&&A->CharacterMesh)))
        {Error=FString::Printf(TEXT("Incomplete card assets for %s"),UTF8_TO_TCHAR(C.id.c_str()));return false;}
        bool Matches=A->Cost==C.cost&&A->Count==C.count&&A->Rounds==C.rounds&&
            A->Flying==C.flying&&A->GroundAndAir==C.canHitAir&&A->StructuresOnly==C.structuresOnly&&
            A->Spell==C.spell&&A->Building==C.building&&A->HardExpiry==C.hardExpiry&&
            A->ShieldOverflowProtected==C.shieldOverflowProtected&&A->GlobalFriendlyInvisibility==C.globalFriendlyInvisibility&&
            A->BreakExistingLocks==C.breakExistingLocks&&A->DeathSpawnCount==C.deathSpawnCount&&
            A->DeathSpawnId==UTF8_TO_TCHAR(C.deathSpawnId.c_str())&&A->SpawnId==UTF8_TO_TCHAR(C.spawnId.c_str());
#define MATCH_STAT(AssetField,CoreField) Matches=Matches&&FMath::IsNearlyEqual(double(A->AssetField),C.CoreField,.001)
        MATCH_STAT(HP,hp);MATCH_STAT(Damage,damage);MATCH_STAT(AttackSpeed,attackInterval);MATCH_STAT(SpawnInterval,spawnInterval);
        MATCH_STAT(MoveSpeed,moveSpeed);MATCH_STAT(AttackRange,range);MATCH_STAT(BaseModelScale,scale);
        MATCH_STAT(CollisionRadius,collisionRadius);MATCH_STAT(TargetMask,targetMask);
        MATCH_STAT(ProjectileSpeed,projectileSpeed);MATCH_STAT(ShockwaveSpeed,shockwaveSpeed);MATCH_STAT(SplashRadius,splash);MATCH_STAT(Lifetime,lifetime);
        MATCH_STAT(Footprint,footprint);MATCH_STAT(TowerDamage,towerDamage);MATCH_STAT(SpellRadius,spellRadius);
        MATCH_STAT(CastDelay,castDelay);MATCH_STAT(ChargeDamage,chargeDamage);MATCH_STAT(SlowPct,slowPct);
        MATCH_STAT(SlowDuration,slowDuration);MATCH_STAT(AuraDamage,auraDamage);MATCH_STAT(AuraInterval,auraInterval);
        MATCH_STAT(AuraRadius,auraRadius);MATCH_STAT(StunDuration,stunDuration);MATCH_STAT(DotDamage,dotDamage);
        MATCH_STAT(DotDuration,dotDuration);MATCH_STAT(DotInterval,dotInterval);
        MATCH_STAT(ChargeMoveSeconds,chargeMoveSeconds);MATCH_STAT(ChargedMoveSpeed,chargedMoveSpeed);
        MATCH_STAT(HPDecayPerSecond,hpDecayPerSecond);MATCH_STAT(InitialAttackCooldown,initialAttackCooldown);
        MATCH_STAT(ShieldHP,shieldHP);MATCH_STAT(InvisibilityDuration,invisibilityDuration);MATCH_STAT(DotStructureDamage,dotStructureDamage);MATCH_STAT(HealthCloakThreshold,healthCloakThreshold);
        Matches=Matches&&FMath::IsNearlyEqual(double(A->FrontSight),C.projectileSpeed>0?FMath::Max(8.,C.range):8.,.001)&&FMath::IsNearlyEqual(A->RearSight,5.f,.001f);
#undef MATCH_STAT
        if(!Matches)
        {Error=FString::Printf(TEXT("DataAsset does not match the locked roster: %s"),UTF8_TO_TCHAR(C.id.c_str()));return false;}
        if((!C.spell&&!C.building)||SkeletalBuilding)for(FName Action:{FName(TEXT("Idle")),FName(TEXT("Locomotion")),FName(TEXT("Attack")),FName(TEXT("Hit")),FName(TEXT("Death")),FName(TEXT("Deploy")),FName(TEXT("Status")),FName(TEXT("Turn")),FName(TEXT("Acquire"))})if(!A->Animations.Contains(Action)){Error=FString::Printf(TEXT("Missing %s animation for %s"),*Action.ToString(),*A->CardId);return false;}
        if(C.spawnInterval>0&&!A->Animations.Contains(TEXT("Spawn"))){Error=TEXT("Missing reinforcement release animation for ")+A->CardId;return false;}
    }return true;
}
