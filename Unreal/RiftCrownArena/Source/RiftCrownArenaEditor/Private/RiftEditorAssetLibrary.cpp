#include "RiftEditorAssetLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystemFactoryNew.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/MaterialInterface.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "RiftAssetLibrary.h"
#include "RiftCardData.h"

namespace
{
bool SaveAsset(UObject* Object,const FString& Extension=FPackageName::GetAssetPackageExtension())
{
    UPackage* Package=Object->GetOutermost();Package->MarkPackageDirty();FString Filename=FPackageName::LongPackageNameToFilename(Package->GetName(),Extension);
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    return UPackage::SavePackage(Package,Object,*Filename,Args);
}
FString Json(const TSharedRef<FJsonObject>& Object){FString Text;FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Text));return Text;}
struct EffectRecipe{const TCHAR* Name;const TCHAR* Emitter;int32 Count;float Life,Size,Speed;};
}
FString URiftEditorAssetLibrary::BuildPresentationAssetsJSON()
{
    auto Report=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Effects,Errors;
    auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Rift/Materials/M_RiftParticle.M_RiftParticle"));
    const EffectRecipe Recipes[]={
        {TEXT("Deploy"),TEXT("SimpleSpriteBurst"),12,.48f,14,90},{TEXT("Impact"),TEXT("DirectionalBurst"),7,.22f,6,110},
        {TEXT("ArrowFlight"),TEXT("SingleLoopingParticle"),1,.3f,7,0},{TEXT("ArcFlight"),TEXT("SingleLoopingParticle"),1,.3f,19,0},
        {TEXT("MantaFlight"),TEXT("SingleLoopingParticle"),1,.3f,12,0},{TEXT("StormFlight"),TEXT("SingleLoopingParticle"),1,.3f,22,0},
        {TEXT("TowerFlight"),TEXT("SingleLoopingParticle"),1,.3f,10,0},{TEXT("BulletBurst"),TEXT("DirectionalBurst"),7,.25f,8,170},
        {TEXT("Nova"),TEXT("OmnidirectionalBurst"),24,.58f,18,160},{TEXT("Meteor"),TEXT("DirectionalBurst"),18,.65f,18,135},
        {TEXT("MeteorTick"),TEXT("SimpleSpriteBurst"),5,.32f,9,30},{TEXT("Frost"),TEXT("DirectionalBurst"),8,.4f,9,65},
        {TEXT("Slow"),TEXT("SingleLoopingParticle"),1,.3f,10,0},{TEXT("Stun"),TEXT("SingleLoopingParticle"),1,.3f,14,0},
        {TEXT("Aura"),TEXT("SimpleSpriteBurst"),20,.38f,10,125},{TEXT("TowerDestroy"),TEXT("OmnidirectionalBurst"),32,.9f,23,140},
        {TEXT("CoreAwaken"),TEXT("SimpleSpriteBurst"),16,.75f,16,100}};
    for(const auto& Recipe:Recipes)
    {
        FString Name=TEXT("NS_Rift")+FString(Recipe.Name),Path=TEXT("/Game/Rift/VFX/")+Name;
        auto* Source=LoadObject<UNiagaraEmitter>(nullptr,*FString::Printf(TEXT("/Niagara/DefaultAssets/Templates/Emitters/%s.%s"),Recipe.Emitter,Recipe.Emitter));
        if(!Source||!Material){Errors.Add(MakeShared<FJsonValueString>(TEXT("Missing emitter template or authored particle material: ")+Name));continue;}
        auto* Package=CreatePackage(*Path);auto* System=FindObject<UNiagaraSystem>(Package,*Name);
        if(!System){System=NewObject<UNiagaraSystem>(Package,*Name,RF_Public|RF_Standalone);UNiagaraSystemFactoryNew::InitializeSystem(System,true);System->AddEmitterHandle(*Source,FName(Recipe.Name),Source->GetExposedVersion().VersionGuid);FAssetRegistryModule::AssetCreated(System);}
        auto& Params=System->GetExposedParameters();
        Params.SetParameterValue(FLinearColor(.12f,.7f,.95f,1),FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(),TEXT("User.Color")),true);
        Params.SetParameterValue(100.f,FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.Radius")),true);
        Params.SetParameterValue(1.f,FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.Strength")),true);
        Params.SetPositionParameterValue(FVector::ZeroVector,TEXT("User.Source"),true);
        Params.SetPositionParameterValue(FVector::ZeroVector,TEXT("User.Target"),true);
        FNiagaraVariable MaterialParam(FNiagaraTypeDefinition(UMaterialInterface::StaticClass()),TEXT("User.RiftMaterial"));Params.AddParameter(MaterialParam);
        auto Entry=MakeShared<FJsonObject>();Entry->SetStringField(TEXT("name"),Name);Entry->SetStringField(TEXT("emitter"),Recipe.Emitter);TArray<TSharedPtr<FJsonValue>> Tuned;
        for(auto& Handle:System->GetEmitterHandles())
        {
            auto* Data=Handle.GetEmitterData();if(!Data)continue;Data->bLocalSpace=true;
            for(auto* Renderer:Data->GetRenderers())if(auto* Sprite=Cast<UNiagaraSpriteRendererProperties>(Renderer)){Sprite->Material=Material;Sprite->MaterialUserParamBinding.Parameter=MaterialParam;}
            for(auto* Script:{Data->SpawnScriptProps.Script.Get(),Data->UpdateScriptProps.Script.Get(),Data->EmitterSpawnScriptProps.Script.Get(),Data->EmitterUpdateScriptProps.Script.Get()})
            {
                if(!Script)continue;TArray<FNiagaraVariable> Variables;Script->RapidIterationParameters.GetParameters(Variables);
                for(const auto& Var:Variables)
                {
                    FString Key=Var.GetName().ToString();bool Changed=false;
                    if(Var.GetType()==FNiagaraTypeDefinition::GetFloatDef())
                    {
                        float Value=Script->RapidIterationParameters.GetParameterValue<float>(Var);
                        if(Key.Contains(TEXT("Lifetime"))){Value=Recipe.Life;Changed=true;}
                        else if(Key.EndsWith(TEXT("VelocityStrength"))||Key.EndsWith(TEXT("VelocitySpeed"))){Value=Recipe.Speed;Changed=true;}
                        if(Changed)Script->RapidIterationParameters.SetParameterValue(Value,Var);
                    }
                    else if(Var.GetType()==FNiagaraTypeDefinition::GetIntDef()&&(Key.Contains(TEXT("SpawnCount"))||Key.EndsWith(TEXT("SpawnBurstInstantaneous.SpawnCount"))))
                    {Script->RapidIterationParameters.SetParameterValue(Recipe.Count,Var);Changed=true;}
                    else if(Var.GetType()==FNiagaraTypeDefinition::GetVec2Def()&&Key.Contains(TEXT("SpriteSize")))
                    {Script->RapidIterationParameters.SetParameterValue(FVector2f(Recipe.Size,Recipe.Size),Var);Changed=true;}
                    if(Changed)Tuned.Add(MakeShared<FJsonValueString>(Key));
                }
            }
        }
        System->SetFixedBounds(FBox(FVector(-700,-700,-300),FVector(700,700,700)));System->RequestCompile(true);System->WaitForCompilationComplete(false,false);
        Entry->SetArrayField(TEXT("tunedParameters"),Tuned);Entry->SetBoolField(TEXT("saved"),SaveAsset(System));Effects.Add(MakeShared<FJsonValueObject>(Entry));
    }
    FString MapPath=TEXT("/Game/Rift/Maps/Arena"),MapFile=FPackageName::LongPackageNameToFilename(MapPath,FPackageName::GetMapPackageExtension());
    if(!FPaths::FileExists(MapFile))
    {
        auto* Package=CreatePackage(*MapPath);auto* World=UWorld::CreateWorld(EWorldType::Inactive,false,TEXT("Arena"),Package);
        if(World)World->SetFlags(RF_Public|RF_Standalone);
        if(!World||!SaveAsset(World,FPackageName::GetMapPackageExtension()))Errors.Add(MakeShared<FJsonValueString>(TEXT("Arena map could not be saved")));
    }
    Report->SetArrayField(TEXT("effects"),Effects);Report->SetArrayField(TEXT("errors"),Errors);return Json(Report);
}
FString URiftEditorAssetLibrary::InspectImportedAssetsJSON()
{
    auto Report=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Entries,Errors;
    TArray<TSharedPtr<FJsonValue>> Definitions;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(URiftAssetLibrary::CardDefinitionsJSON()),Definitions);
    for(const auto& Card:Definitions)
    {
        FString Id=Card->AsObject()->GetStringField(TEXT("id"));auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("id"),Id);
        auto* Asset=LoadObject<URiftCardData>(nullptr,*FString::Printf(TEXT("/Game/Rift/Cards/DA_%s.DA_%s"),*Id,*Id));
        if(!Asset){Errors.Add(MakeShared<FJsonValueString>(TEXT("Missing card DataAsset ")+Id));continue;}
        O->SetBoolField(TEXT("illustration"),Asset->Illustration!=nullptr);O->SetNumberField(TEXT("animations"),Asset->Animations.Num());O->SetNumberField(TEXT("effects"),Asset->Effects.Num());O->SetNumberField(TEXT("sounds"),Asset->Sounds.Num());
        if(auto* Mesh=Asset->CharacterMesh.Get())
        {
            FVector Bounds=Mesh->GetBounds().BoxExtent*2;O->SetNumberField(TEXT("widthCm"),Bounds.Y);O->SetNumberField(TEXT("lengthCm"),Bounds.X);O->SetNumberField(TEXT("heightCm"),Bounds.Z);
            O->SetNumberField(TEXT("bones"),Mesh->GetRefSkeleton().GetNum());O->SetNumberField(TEXT("lods"),Mesh->GetLODNum());O->SetBoolField(TEXT("physics"),Mesh->GetPhysicsAsset()!=nullptr);
            if(Bounds.Z<15||Bounds.Z>600)Errors.Add(MakeShared<FJsonValueString>(TEXT("Unexpected imported character height ")+Id));
        }
        Entries.Add(MakeShared<FJsonValueObject>(O));
    }
    Report->SetArrayField(TEXT("cards"),Entries);Report->SetArrayField(TEXT("errors"),Errors);return Json(Report);
}
