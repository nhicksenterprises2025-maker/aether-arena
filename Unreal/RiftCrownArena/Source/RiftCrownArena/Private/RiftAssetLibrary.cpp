#include "RiftAssetLibrary.h"
#include "RiftDiagnostics.h"
#include "RiftProfileSubsystem.h"
#include "Simulation/RiftSimulation.h"
#include "Serialization/JsonSerializer.h"

FString URiftAssetLibrary::CardDefinitionsJSON()
{
    TArray<TSharedPtr<FJsonValue>> Values;
    for(const auto& C:rift::Cards())
    {
        auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("id"),UTF8_TO_TCHAR(C.id.c_str()));O->SetStringField(TEXT("name"),UTF8_TO_TCHAR(C.name.c_str()));
#define RF(K) O->SetNumberField(TEXT(#K),double(C.K))
        RF(cost);RF(count);RF(hp);RF(damage);RF(attackInterval);RF(moveSpeed);RF(range);RF(scale);RF(projectileSpeed);RF(splash);RF(lifetime);RF(footprint);RF(towerDamage);RF(spellRadius);RF(chargeDamage);RF(slowPct);RF(slowDuration);RF(auraDamage);RF(auraInterval);RF(auraRadius);RF(stunDuration);RF(dotDamage);RF(dotDuration);RF(dotInterval);RF(rounds);
#undef RF
#define RB(K) O->SetBoolField(TEXT(#K),C.K)
        RB(flying);RB(canHitAir);RB(structuresOnly);RB(spell);RB(building);
#undef RB
        Values.Add(MakeShared<FJsonValueObject>(O));
    }
    FString Output;FJsonSerializer::Serialize(Values,TJsonWriterFactory<>::Create(&Output));return Output;
}
void URiftAssetCatalogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    for(const auto& C:rift::Cards())
    {
        FString Id=UTF8_TO_TCHAR(C.id.c_str());auto* Data=LoadObject<URiftCardData>(nullptr,*FString::Printf(TEXT("/Game/Rift/Cards/DA_%s.DA_%s"),*Id,*Id));
        if(Data)Cards.Add(Id,Data);else RIFT_LOG(LogRift,Error,TEXT("Card asset missing: %s"),*Id);
    }
    FString Error;if(!Validate(Error))RIFT_LOG(LogRift,Error,TEXT("Asset catalog validation: %s"),*Error);
}
URiftCardData* URiftAssetCatalogSubsystem::Card(const FString& Id)const{auto* C=Cards.Find(Id);return C?C->Get():nullptr;}
bool URiftAssetCatalogSubsystem::Validate(FString& Error)const
{
    for(const auto& C:rift::Cards())
    {
        auto* A=Card(UTF8_TO_TCHAR(C.id.c_str()));if(!A||!A->Illustration||(!C.spell&&!C.building&&!A->CharacterMesh)||(C.building&&!A->StructureMesh))
        {Error=FString::Printf(TEXT("Incomplete card assets for %s"),UTF8_TO_TCHAR(C.id.c_str()));return false;}
        if(A->Cost!=C.cost||A->Count!=C.count||!FMath::IsNearlyEqual(double(A->HP),C.hp,.001)||!FMath::IsNearlyEqual(double(A->Damage),C.damage,.001)||!FMath::IsNearlyEqual(double(A->AttackRange),C.range,.001))
        {Error=FString::Printf(TEXT("DataAsset does not match the locked roster: %s"),UTF8_TO_TCHAR(C.id.c_str()));return false;}
        if(!C.spell&&!C.building)for(FName Action:{FName(TEXT("Idle")),FName(TEXT("Locomotion")),FName(TEXT("Attack")),FName(TEXT("Hit")),FName(TEXT("Death")),FName(TEXT("Deploy")),FName(TEXT("Status")),FName(TEXT("Turn")),FName(TEXT("Acquire"))})if(!A->Animations.Contains(Action)){Error=FString::Printf(TEXT("Missing %s animation for %s"),*Action.ToString(),*A->CardId);return false;}
    }return true;
}
