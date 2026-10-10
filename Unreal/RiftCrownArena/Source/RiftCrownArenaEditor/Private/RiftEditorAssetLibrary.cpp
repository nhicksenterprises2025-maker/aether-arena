#include "RiftEditorAssetLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/MaterialInterface.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "RiftAssetLibrary.h"
#include "RiftCardData.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/ISlateNullRendererModule.h"
#include "Modules/ModuleManager.h"

namespace
{
bool SaveAsset(UObject* Object,const FString& Extension=FPackageName::GetAssetPackageExtension())
{
    UPackage* Package=Object->GetOutermost();Package->MarkPackageDirty();FString Filename=FPackageName::LongPackageNameToFilename(Package->GetName(),Extension);
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
    return UPackage::SavePackage(Package,Object,*Filename,Args);
}
FString Json(const TSharedRef<FJsonObject>& Object){FString Text;FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Text));return Text;}
struct EffectRecipe{const TCHAR* Name;const TCHAR* Emitter;int32 Count;float Life,Size,Speed;};
}
bool URiftEditorAssetLibrary::EnsureFontImportSlate()
{
    if (!FSlateApplication::IsInitialized())
    {
        // A Python commandlet has no Slate application. The null renderer
        // supplies real font services without opening an editor or window.
        auto& Module=FModuleManager::LoadModuleChecked<ISlateNullRendererModule>(TEXT("SlateNullRenderer"));
        FSlateApplication::InitializeAsStandaloneApplication(Module.CreateSlateNullRenderer());
    }
    return FSlateApplication::IsInitialized();
}
FString URiftEditorAssetLibrary::BuildUIFontJSON()
{
    auto Report=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Faces,Errors;
    const TCHAR* Weights[]={TEXT("Regular"),TEXT("Medium"),TEXT("SemiBold"),TEXT("Bold")};
    FCompositeFont Composite;
    for (const TCHAR* Weight:Weights)
    {
        const FString Name=TEXT("FF_BarlowSemiCondensed_")+FString(Weight);
        const FString Path=TEXT("/Game/Rift/Fonts/")+Name+TEXT(".")+Name;
        auto* Face=LoadObject<UFontFace>(nullptr,*Path);
        if (!Face || Face->GetLoadingPolicy()!=EFontLoadingPolicy::Inline || Face->GetFontFaceData()->GetData().Num()==0)
        {Errors.Add(MakeShared<FJsonValueString>(TEXT("Missing or non-inline UI font face: ")+Path));continue;}
        FTypefaceEntry Entry{FName(Weight)};
        Entry.Font=FFontData(Face);
        Composite.DefaultTypeface.Fonts.Add(MoveTemp(Entry));
        auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("weight"),Weight);
        Item->SetStringField(TEXT("path"),Face->GetPathName());Item->SetNumberField(TEXT("bytes"),Face->GetFontFaceData()->GetData().Num());
        Item->SetStringField(TEXT("loadingPolicy"),TEXT("Inline"));Faces.Add(MakeShared<FJsonValueObject>(Item));
    }
    if (Errors.IsEmpty())
    {
        const FString PackagePath=TEXT("/Game/Rift/Fonts/F_RiftUI");
        auto* Font=LoadObject<UFont>(nullptr,*(PackagePath+TEXT(".F_RiftUI")));
        if (!Font)
        {
            Font=NewObject<UFont>(CreatePackage(*PackagePath),TEXT("F_RiftUI"),RF_Public|RF_Standalone|RF_Transactional);
            FAssetRegistryModule::AssetCreated(Font);
        }
        Font->FontCacheType=EFontCacheType::Runtime;
        Font->RuntimeFontSource=ERuntimeFontSource::Asset;
        Font->LegacyFontName=TEXT("Regular");
        auto& SavedComposite=Font->GetMutableInternalCompositeFont();
        SavedComposite=MoveTemp(Composite);SavedComposite.MakeDirty();Font->PostEditChange();
        if (!SaveAsset(Font))Errors.Add(MakeShared<FJsonValueString>(TEXT("Could not save the composite UI font.")));
        const auto& Entries=Font->GetInternalCompositeFont().DefaultTypeface.Fonts;
        if (Entries.Num()!=UE_ARRAY_COUNT(Weights))Errors.Add(MakeShared<FJsonValueString>(TEXT("Saved UI typeface is incomplete.")));
        for (int32 I=0;I<Entries.Num();++I)
            if (Entries[I].Name!=Weights[I] || !Cast<UFontFace>(Entries[I].Font.GetFontFaceAsset()))
                Errors.Add(MakeShared<FJsonValueString>(TEXT("Saved UI typeface names or references differ.")));
        Report->SetStringField(TEXT("font"),Font->GetPathName());
    }
    Report->SetArrayField(TEXT("faces"),Faces);Report->SetArrayField(TEXT("errors"),Errors);
    Report->SetBoolField(TEXT("passed"),Errors.IsEmpty());return Json(Report);
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
        auto* System=LoadObject<UNiagaraSystem>(nullptr,*(Path+TEXT(".")+Name));
        auto* Package=System?System->GetOutermost():CreatePackage(*Path);
        if(System)Package->FullyLoad();
        if(!System){System=NewObject<UNiagaraSystem>(Package,*Name,RF_Public|RF_Standalone|RF_Transactional);UNiagaraSystemFactoryNew::InitializeSystem(System,true);FAssetRegistryModule::AssetCreated(System);}
        // Handles alone are not executable system-graph nodes. Follow the
        // installed Niagara factory's editor utility, including on existing
        // generated assets, so regeneration repairs previously empty graphs.
        TSet<FGuid> PreviousHandles;for(const auto& Handle:System->GetEmitterHandles())PreviousHandles.Add(Handle.GetId());
        if(!PreviousHandles.IsEmpty())System->RemoveEmitterHandlesById(PreviousHandles);
        FNiagaraEditorUtilities::AddEmitterToSystem(*System,*Source,Source->GetExposedVersion().VersionGuid);
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
                    FString Key=Var.GetName().ToString();const FString NormalizedKey=Key.Replace(TEXT(" "),TEXT(""));bool Changed=false;
                    if(Var.GetType()==FNiagaraTypeDefinition::GetFloatDef())
                    {
                        float Value=Script->RapidIterationParameters.GetParameterValue<float>(Var);
                        if(NormalizedKey.Contains(TEXT("Lifetime"))){Value=Recipe.Life;Changed=true;}
                        else if(NormalizedKey.EndsWith(TEXT("VelocityStrength"))||NormalizedKey.EndsWith(TEXT("VelocitySpeed"))){Value=Recipe.Speed;Changed=true;}
                        if(Changed)Script->RapidIterationParameters.SetParameterValue(Value,Var);
                    }
                    else if(Var.GetType()==FNiagaraTypeDefinition::GetIntDef()&&NormalizedKey.Contains(TEXT("SpawnCount")))
                    {Script->RapidIterationParameters.SetParameterValue(Recipe.Count,Var);Changed=true;}
                    else if(Var.GetType()==FNiagaraTypeDefinition::GetVec2Def()&&NormalizedKey.Contains(TEXT("InitializeParticle.SpriteSize")))
                    {Script->RapidIterationParameters.SetParameterValue(FVector2f(Recipe.Size,Recipe.Size),Var);Changed=true;}
                    if(Changed)Tuned.Add(MakeShared<FJsonValueString>(Key));
                }
            }
        }
        System->SetFixedBounds(FBox(FVector(-700,-700,-300),FVector(700,700,700)));System->RequestCompile(true);System->WaitForCompilationComplete(false,false);
        Entry->SetArrayField(TEXT("tunedParameters"),Tuned);Entry->SetBoolField(TEXT("saved"),SaveAsset(System));
        if(!Entry->GetBoolField(TEXT("saved")))Errors.Add(MakeShared<FJsonValueString>(TEXT("Failed saving Niagara system: ")+Name));
        Effects.Add(MakeShared<FJsonValueObject>(Entry));
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
            if(auto* Physics=Mesh->GetPhysicsAsset())
            {
                O->SetNumberField(TEXT("physicsBodies"),Physics->SkeletalBodySetups.Num());O->SetNumberField(TEXT("physicsConstraints"),Physics->ConstraintSetup.Num());
                O->SetNumberField(TEXT("physicsObjectFlags"),uint32(Physics->GetFlags()));O->SetNumberField(TEXT("physicsPackageFlags"),uint32(Physics->GetOutermost()->GetPackageFlags()));
                O->SetBoolField(TEXT("physicsIsAsset"),Physics->IsAsset());auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
                O->SetBoolField(TEXT("physicsRegisteredOnDisk"),Registry.DoesPackageExistOnDisk(Physics->GetOutermost()->GetFName()));
            }
            if(Bounds.Z<15||Bounds.Z>600)Errors.Add(MakeShared<FJsonValueString>(TEXT("Unexpected imported character height ")+Id));
        }
        Entries.Add(MakeShared<FJsonValueObject>(O));
    }
    Report->SetArrayField(TEXT("cards"),Entries);Report->SetArrayField(TEXT("errors"),Errors);return Json(Report);
}

FString URiftEditorAssetLibrary::FinalizeImportedPhysicsAssetsJSON()
{
    auto Report=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Entries,Errors;
    const TCHAR* Ids[]={TEXT("ironclad"),TEXT("ember_archer"),TEXT("twin_blades"),TEXT("boulderback"),TEXT("arc_mage"),TEXT("rambeast"),TEXT("sky_manta"),TEXT("vampire_bats"),TEXT("frost_fang"),TEXT("storm_raven"),TEXT("mini_stampede"),TEXT("stampede"),TEXT("tower_archer")};
    for(const auto* Id:Ids)
    {
        auto* Mesh=LoadObject<USkeletalMesh>(nullptr,*FString::Printf(TEXT("/Game/Rift/Characters/%s/SK_%s.SK_%s"),Id,Id,Id));
        auto* Physics=Mesh?Mesh->GetPhysicsAsset():nullptr;
        if(!Physics||Physics->SkeletalBodySetups.IsEmpty()){Errors.Add(MakeShared<FJsonValueString>(FString(TEXT("Missing generated physics bodies: "))+Id));continue;}
        auto Entry=MakeShared<FJsonObject>();Entry->SetStringField(TEXT("id"),Id);Entry->SetNumberField(TEXT("originalFlags"),uint32(Physics->GetFlags()));
        Physics->ClearFlags(RF_Transient);Physics->SetFlags(RF_Public|RF_Standalone);Physics->MarkPackageDirty();FAssetRegistryModule::AssetCreated(Physics);
        Entry->SetNumberField(TEXT("bodies"),Physics->SkeletalBodySetups.Num());Entry->SetBoolField(TEXT("saved"),SaveAsset(Physics));
        if(!Entry->GetBoolField(TEXT("saved")))Errors.Add(MakeShared<FJsonValueString>(FString(TEXT("Could not persist physics asset: "))+Id));
        Entries.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Report->SetArrayField(TEXT("physicsAssets"),Entries);Report->SetArrayField(TEXT("errors"),Errors);return Json(Report);
}
