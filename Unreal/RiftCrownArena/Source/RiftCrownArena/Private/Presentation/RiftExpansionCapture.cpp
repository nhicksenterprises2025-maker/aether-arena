#include "Presentation/RiftExpansionCapture.h"
#include "Presentation/RiftArenaPresentation.h"
#include "Presentation/RiftUnitVisual.h"
#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "RiftAssetLibrary.h"
#include "RiftDiagnostics.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Rendering/SkeletalMeshRenderData.h"
#if WITH_EDITOR
#include "Animation/AnimData/IAnimationDataModel.h"
#endif
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"

namespace
{
bool IsIsolatedCapture()
{
    FString Capture,SaveRoot,Scenario;
    if(!FParse::Value(FCommandLine::Get(),TEXT("RiftCapture="),Capture)||Capture.IsEmpty()||FPaths::IsRelative(Capture)||
       !FParse::Value(FCommandLine::Get(),TEXT("RiftSaveRoot="),SaveRoot)||SaveRoot.IsEmpty()||FPaths::IsRelative(SaveRoot)||
       !FParse::Value(FCommandLine::Get(),TEXT("RiftVisualScenario="),Scenario)||(Scenario!=TEXT("expansion_roster")&&Scenario!=TEXT("expansion_new_roster")&&Scenario!=TEXT("expansion_effects")&&Scenario!=TEXT("expansion_warden_death")))return false;
    const FString CaptureRoot=FPaths::GetPath(Capture);
    if(!FPaths::IsUnderDirectory(SaveRoot,CaptureRoot))return false;
    TArray<FString> LiveRoots;
    const FString Local=FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
    if(!Local.IsEmpty())LiveRoots.Add(FPaths::Combine(Local,TEXT("RiftCrownArena")));
    for(const TCHAR* Variable:{TEXT("RIFT_SAVE_ROOT"),TEXT("RIFT_CROWN_SAVE_ROOT")})
    {const FString Root=FPlatformMisc::GetEnvironmentVariable(Variable);if(!Root.IsEmpty())LiveRoots.Add(FPaths::ConvertRelativePathToFull(Root));}
    for(const auto& Root:LiveRoots)if(FPaths::IsSamePath(SaveRoot,Root)||FPaths::IsUnderDirectory(SaveRoot,Root)||FPaths::IsUnderDirectory(Root,SaveRoot))return false;
    return true;
}
rift::Vec2 Position(const std::string& Id,rift::Team Team)
{
    const double Sign=Team==rift::Team::Player?1.:-1.;
    if(Id=="mini_stampede")return {-8.5,Sign*6.5};
    if(Id=="stampede")return {0.5,Sign*4.5};
    if(Id=="bone_warden")return {-6.5,Sign*5.5};
    if(Id=="aether_reaper")return {.5,Sign*4.5};
    if(Id=="iron_giant")return {6.5,Sign*5.5};
    return {8.5,Sign*4.5};
}
TArray<TSharedPtr<FJsonValue>> Hands(const rift::Snapshot& State)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    for(const auto& Hand:State.hands)
    {TArray<TSharedPtr<FJsonValue>> Cards;for(const auto& Id:Hand)Cards.Add(MakeShared<FJsonValueString>(UTF8_TO_TCHAR(Id.c_str())));Result.Add(MakeShared<FJsonValueArray>(Cards));}
    return Result;
}
void RenderState(UWorld* World)
{for(TActorIterator<ARiftArenaPresentation> It(World);It;++It)It->Tick(0.f);}
FVector ReferenceBoneLocation(const USkeletalMesh* Mesh,FName Name)
{
    const auto& Skeleton=Mesh->GetRefSkeleton();int32 Bone=Skeleton.FindBoneIndex(Name);
    if(Bone==INDEX_NONE)return FVector::ZeroVector;
    FTransform Transform=Skeleton.GetRefBonePose()[Bone];
    while((Bone=Skeleton.GetParentIndex(Bone))!=INDEX_NONE)Transform*=Skeleton.GetRefBonePose()[Bone];
    return Transform.GetLocation();
}
bool ExpansionMaterialContract(const USkeletalMesh* Mesh,const FString& Id,const TSharedRef<FJsonObject>& Row)
{
    TSet<FString> Expected{TEXT("/Game/Rift/Materials/M_RiftExpansionSurface.M_RiftExpansionSurface")};
    if(Id==TEXT("pulverizer"))Expected.Add(TEXT("/Game/Rift/Materials/M_RiftExpansionGlow.M_RiftExpansionGlow"));
    if(Id==TEXT("bone_warden")||Id==TEXT("bone_warden_skeleton")||Id==TEXT("aether_reaper")||Id==TEXT("iron_giant"))
    {
        Expected={TEXT("/Game/Rift/Materials/M_RiftExpansionAscensionSurface.M_RiftExpansionAscensionSurface")};
        Expected.Add(TEXT("/Game/Rift/Materials/M_RiftExpansionAscensionGlow.M_RiftExpansionAscensionGlow"));
        if(Id==TEXT("aether_reaper"))Expected.Add(TEXT("/Game/Rift/Materials/M_RiftExpansionAscensionGlass.M_RiftExpansionAscensionGlass"));
    }
    TSet<FString> Observed;TArray<TSharedPtr<FJsonValue>> Slots,Sections;bool Passed=true;
    const auto& Materials=Mesh->GetMaterials();
    for(int32 Index=0;Index<Materials.Num();++Index)
    {
        const auto& Slot=Materials[Index];const FString Path=Slot.MaterialInterface?Slot.MaterialInterface->GetPathName():TEXT("");
        auto Entry=MakeShared<FJsonObject>();Entry->SetNumberField(TEXT("index"),Index);Entry->SetStringField(TEXT("slot"),Slot.MaterialSlotName.ToString());
#if WITH_EDITORONLY_DATA
        Entry->SetStringField(TEXT("importedSlot"),Slot.ImportedMaterialSlotName.ToString());
#endif
        Entry->SetStringField(TEXT("materialInterface"),Path);Entry->SetBoolField(TEXT("manifestOwned"),Expected.Contains(Path));
        Passed&=Expected.Contains(Path);Observed.Add(Path);Slots.Add(MakeShared<FJsonValueObject>(Entry));
    }
    for(const auto& Path:Expected)Passed&=Observed.Contains(Path);
    const auto* RenderData=Mesh->GetResourceForRendering();Passed&=RenderData&&RenderData->LODRenderData.Num()==Mesh->GetLODNum();
    if(RenderData)for(int32 LOD=0;LOD<RenderData->LODRenderData.Num();++LOD)
    {
        const auto& LODData=RenderData->LODRenderData[LOD];const auto* Info=Mesh->GetLODInfo(LOD);Passed&=LODData.RenderSections.Num()>0;
        for(int32 Section=0;Section<LODData.RenderSections.Num();++Section)
        {
            const auto& Part=LODData.RenderSections[Section];int32 Index=Part.MaterialIndex;
            if(Info&&Info->LODMaterialMap.IsValidIndex(Section)&&Info->LODMaterialMap[Section]!=INDEX_NONE)Index=Info->LODMaterialMap[Section];
            const FString Path=Materials.IsValidIndex(Index)&&Materials[Index].MaterialInterface?Materials[Index].MaterialInterface->GetPathName():TEXT("");
            auto Entry=MakeShared<FJsonObject>();Entry->SetNumberField(TEXT("lod"),LOD);Entry->SetNumberField(TEXT("section"),Section);
            Entry->SetNumberField(TEXT("renderMaterialIndex"),Part.MaterialIndex);Entry->SetNumberField(TEXT("resolvedMaterialIndex"),Index);
            Entry->SetNumberField(TEXT("triangles"),Part.NumTriangles);Entry->SetStringField(TEXT("materialInterface"),Path);
            Entry->SetBoolField(TEXT("manifestOwned"),Expected.Contains(Path));Passed&=Expected.Contains(Path);Sections.Add(MakeShared<FJsonValueObject>(Entry));
        }
    }
    Row->SetArrayField(TEXT("materialSlots"),Slots);Row->SetArrayField(TEXT("lodMaterialSections"),Sections);
    Row->SetBoolField(TEXT("expansionMaterialBindingsPassed"),Passed);return Passed;
}
}

ARiftExpansionCapture::ARiftExpansionCapture()
{PrimaryActorTick.bCanEverTick=false;SetActorHiddenInGame(true);SetActorEnableCollision(false);}

bool ARiftExpansionCapture::Build(URiftMatchSubsystem* Match,const TSharedRef<FJsonObject>& Report)
{
    if(!Match||!IsIsolatedCapture())
    {Report->SetBoolField(TEXT("passed"),false);RIFT_LOG(LogRift,Error,TEXT("Selected gallery requires its explicit scenario and isolated absolute capture/save paths."));return false;}
    rift::MatchOptions Options;Options.seed=74021;Options.aiEnabled={false,false};
    FString Scenario;FParse::Value(FCommandLine::Get(),TEXT("RiftVisualScenario="),Scenario);
    WardenDeathCapture=Scenario==TEXT("expansion_warden_death");
    if(WardenDeathCapture)return BuildWardenDeath(Match,Report);
    EffectsCapture=Scenario==TEXT("expansion_effects");
    if(EffectsCapture)
    {
        FParse::Value(FCommandLine::Get(),TEXT("RiftExpansionEffectStage="),EffectStage);
        if(EffectStage!=TEXT("cast")&&EffectStage!=TEXT("impact")&&EffectStage!=TEXT("shield"))
        {Report->SetBoolField(TEXT("passed"),false);RIFT_LOG(LogRift,Error,TEXT("Unknown expansion effect capture stage: %s"),*EffectStage);return false;}
        EffectRequestedAdvance=EffectStage==TEXT("cast")?.12:EffectStage==TEXT("impact")?.37:.95;
        EffectAdvanceSteps=FMath::RoundToInt(EffectRequestedAdvance*60.);
    }
    ExpectedAge=.35+(EffectsCapture?double(EffectAdvanceSteps)/60.:0.);
    const bool NewModels=Scenario!=TEXT("expansion_roster");
    SelectedCards=NewModels?TArray<FString>{TEXT("bone_warden"),TEXT("aether_reaper"),TEXT("iron_giant")}:TArray<FString>{TEXT("mini_stampede"),TEXT("stampede"),TEXT("pulverizer")};
    ExpectedTeamCount=NewModels?3:21;
    for(auto& Deck:Options.decks)Deck=NewModels?std::vector<std::string>{"bone_warden","aether_reaper","iron_giant","shadow_surge","mini_stampede","stampede","pulverizer","nova_flask"}:std::vector<std::string>{"mini_stampede","stampede","pulverizer","ironclad","ember_archer","arc_mage","frost_fang","meteor_shards"};
    if(EffectsCapture)Options.decks[1]={"bone_warden","aether_reaper","iron_giant","nova_flask","mini_stampede","stampede","pulverizer","shadow_surge"};
    if(!rift::ValidateDeck(Options.decks[0])||!rift::ValidateDeck(Options.decks[1]))return false;
    Fixture=std::make_unique<rift::Match>(Options);View=Match;Match->SetSpeed(0);
    // The actor owns this actual simulation for the whole capture lifetime.
    // No copied/edited pose, hand, member position or stats enter the snapshot.
    Match->SetReplayView(&Fixture->State());RenderState(GetWorld());
    Report->SetNumberField(TEXT("schema"),1);Report->SetStringField(TEXT("route"),TEXT("QA-owned real Match / two legal unique eight-card decks / paid Play / fixed steps / production SetReplayView snapshot renderer; not an archived replay"));
    Report->SetNumberField(TEXT("seed"),Options.seed);Report->SetBoolField(TEXT("aiDisabled"),true);
    Report->SetStringField(TEXT("profile"),Scenario);
    Report->SetArrayField(TEXT("openingHands"),Hands(Fixture->State()));
    TArray<TSharedPtr<FJsonValue>> Plays;TSet<uint64> PlayIds;bool Paid=true;
    for(auto Team:{rift::Team::Player,rift::Team::Enemy})for(const auto& SelectedCard:SelectedCards)
    {
        const std::string Id=TCHAR_TO_UTF8(*SelectedCard);
        const auto* Card=rift::FindCard(Id);if(!Card){Paid=false;continue;}
        const int32 Side=int32(Team);int32 Slot=INDEX_NONE;
        for(int32 I=0;I<4;++I)if(Fixture->State().hands[Side][I]==Id){Slot=I;break;}
        const int32 Cost=Card->cost,Expected=Card->count;const auto OldHand=Fixture->State().hands[Side];const auto OldQueue=Fixture->State().queues[Side];
        const double AetherBeforeGrant=Fixture->State().aether[Side];Fixture->SetAether(Team,10);
        const double Spent=Fixture->State().spent[Side];const bool Accepted=Slot!=INDEX_NONE&&Fixture->Play(Team,Slot,Position(Id,Team),"expansion_roster_paid_qa");
        uint64 PlayId=0;bool Sandbox=false;
        for(const auto& Event:Fixture->Events())if(Event.type=="card_play"&&Event.cardId==Id&&Event.team==Team){PlayId=Event.playId;Sandbox=Event.sandbox;}
        TArray<TSharedPtr<FJsonValue>> Members;
        for(const auto& Entity:Fixture->State().entities)if(Entity.playId==PlayId&&Entity.team==Team&&Entity.cardId==Id)
        {Members.Add(MakeShared<FJsonValueNumber>(double(Entity.id)));SelectedIds.Add(Entity.id);}
        const bool Cycled=Accepted&&!OldQueue.empty()&&Fixture->State().hands[Side][Slot]==OldQueue.front()&&Fixture->State().queues[Side].back()==Id;
        bool OtherSlots=true;for(int32 I=0;I<4;++I)if(I!=Slot)OtherSlots&=Fixture->State().hands[Side][I]==OldHand[I];
        const bool Exact=Accepted&&!Sandbox&&PlayId>0&&!PlayIds.Contains(PlayId)&&Cycled&&OtherSlots&&Members.Num()==Expected&&
            FMath::IsNearlyEqual(Fixture->State().spent[Side]-Spent,double(Cost),1.e-9)&&FMath::IsNearlyEqual(Fixture->State().aether[Side],double(10-Cost),1.e-9);
        Paid&=Exact;PlayIds.Add(PlayId);
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("cardId"),UTF8_TO_TCHAR(Id.c_str()));Row->SetNumberField(TEXT("team"),Side);
        Row->SetNumberField(TEXT("handIndex"),Slot);Row->SetNumberField(TEXT("playId"),double(PlayId));Row->SetBoolField(TEXT("accepted"),Accepted);Row->SetBoolField(TEXT("sandbox"),Sandbox);
        Row->SetNumberField(TEXT("fixtureAetherGrant"),10-AetherBeforeGrant);Row->SetNumberField(TEXT("cost"),Cost);Row->SetNumberField(TEXT("spentDelta"),Fixture->State().spent[Side]-Spent);
        Row->SetNumberField(TEXT("aetherAfter"),Fixture->State().aether[Side]);Row->SetNumberField(TEXT("expectedMembers"),Expected);Row->SetNumberField(TEXT("actualMembers"),Members.Num());
        Row->SetBoolField(TEXT("handCycledOnce"),Cycled&&OtherSlots);Row->SetArrayField(TEXT("memberIds"),Members);Row->SetBoolField(TEXT("passed"),Exact);Plays.Add(MakeShared<FJsonValueObject>(Row));
        // Forward only genuine events from this paid simulation. The unused,
        // paused profile match's replay recording is never relabeled as it.
        for(const auto& Event:Fixture->DrainEvents())Match->OnEvent.Broadcast(Event);
        Match->SetReplayView(&Fixture->State());RenderState(GetWorld());
    }
    Report->SetArrayField(TEXT("plays"),Plays);Report->SetNumberField(TEXT("distinctPlayIds"),PlayIds.Num());Report->SetBoolField(TEXT("paidCostAndCyclePassed"),Paid);
    for(int32 Step=0;Step<21;++Step)
    {
        Fixture->Step(1./60.);for(const auto& Event:Fixture->DrainEvents())Match->OnEvent.Broadcast(Event);
        RenderState(GetWorld());
    }
    if(EffectsCapture)
    {
        TArray<TSharedPtr<FJsonValue>> Casts;
        auto Cast=[&](rift::Team Team,const std::string& Id,rift::Team TargetTeam)
        {
            const int32 Side=int32(Team);int32 Slot=INDEX_NONE;
            for(int32 I=0;I<4;++I)if(Fixture->State().hands[Side][I]==Id){Slot=I;break;}
            rift::Vec2 Point;bool Found=false;for(const auto& Body:Fixture->State().entities)
                if(Body.cardId=="iron_giant"&&Body.team==TargetTeam&&!Body.dead){Point=Body.position;Found=true;break;}
            Fixture->SetAether(Team,10);const double Before=Fixture->State().spent[Side];
            const auto* Card=rift::FindCard(Id);const bool Accepted=Found&&Slot!=INDEX_NONE&&Card&&Fixture->Play(Team,Slot,Point,"expansion_effects_paid_qa");
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("cardId"),UTF8_TO_TCHAR(Id.c_str()));Row->SetNumberField(TEXT("team"),Side);
            Row->SetBoolField(TEXT("accepted"),Accepted);Row->SetNumberField(TEXT("spentDelta"),Fixture->State().spent[Side]-Before);
            const bool Exact=Accepted&&FMath::IsNearlyEqual(Fixture->State().spent[Side]-Before,double(Card->cost),1.e-9);
            Paid&=Exact;Row->SetBoolField(TEXT("passed"),Exact);Casts.Add(MakeShared<FJsonValueObject>(Row));
            for(const auto& Event:Fixture->DrainEvents())Match->OnEvent.Broadcast(Event);RenderState(GetWorld());
        };
        Cast(rift::Team::Enemy,"nova_flask",rift::Team::Player);
        Cast(rift::Team::Enemy,"shadow_surge",rift::Team::Enemy);
        Cast(rift::Team::Player,"shadow_surge",rift::Team::Player);
        Report->SetArrayField(TEXT("effectPaidCasts"),Casts);
        for(int32 Step=0;Step<EffectAdvanceSteps;++Step)
        {Fixture->Step(1./60.);for(const auto& Event:Fixture->DrainEvents())Match->OnEvent.Broadcast(Event);RenderState(GetWorld());}
        Report->SetStringField(TEXT("effectStage"),EffectStage);
        Report->SetNumberField(TEXT("effectCastAgeSeconds"),.35);
        Report->SetNumberField(TEXT("effectRequestedAdvanceSeconds"),EffectRequestedAdvance);
        Report->SetNumberField(TEXT("effectFixedAdvanceSeconds"),double(EffectAdvanceSteps)/60.);
        Report->SetNumberField(TEXT("effectFixedAdvanceSteps"),EffectAdvanceSteps);
    }
    Match->SetSpeed(0);RenderState(GetWorld());
    Report->SetNumberField(TEXT("fixedSteps"),EffectsCapture?21+EffectAdvanceSteps:21);
    Report->SetNumberField(TEXT("requestedAgeSeconds"),.35+(EffectsCapture?EffectRequestedAdvance:0.));
    SetupPassed=Paid&&PlayIds.Num()==6&&SelectedIds.Num()==ExpectedTeamCount*2&&FMath::IsNearlyEqual(Fixture->State().elapsed,ExpectedAge,1.e-9);
    Report->SetBoolField(TEXT("setupPassed"),SetupPassed);return SampleReport(Report);
}

bool ARiftExpansionCapture::SampleReport(const TSharedRef<FJsonObject>& Report) const
{
    if(WardenDeathCapture)return SampleWardenDeath(Report);
    if(!Fixture||!View.IsValid())return false;
    const auto& State=Fixture->State();bool Models=true,Facing=true,Walking=true,Materials=true;int32 TeamCounts[2]={0,0};TSet<uint64> Observed;
    TArray<TSharedPtr<FJsonValue>> Members,Cards;
    auto* Catalog=GetWorld()->GetGameInstance()->GetSubsystem<URiftAssetCatalogSubsystem>();
    for(const FString& Id:SelectedCards)
    {
        const auto* Card=Catalog->Card(Id);auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("cardId"),Id);
        auto* Mesh=Card?Card->CharacterMesh.Get():nullptr;Models&=Card&&Mesh;
        if(Card&&Mesh)
        {
            Materials&=ExpansionMaterialContract(Mesh,Id,Row);
            Row->SetStringField(TEXT("mesh"),Mesh->GetPathName());Row->SetNumberField(TEXT("bones"),Mesh->GetRefSkeleton().GetNum());Row->SetNumberField(TEXT("lods"),Mesh->GetLODNum());
            if(Id==TEXT("bone_warden"))
            {
                TArray<TSharedPtr<FJsonValue>> Captives;bool Present=true;
                for(const TCHAR* CaptiveRole:{TEXT("head"),TEXT("arm")})for(int32 I=0;I<4;++I)
                {
                    const FName Bone(*FString::Printf(TEXT("captive_%s_%d"),CaptiveRole,I));const bool Found=Mesh->GetRefSkeleton().FindBoneIndex(Bone)!=INDEX_NONE;
                    auto Entry=MakeShared<FJsonObject>();Entry->SetStringField(TEXT("bone"),Bone.ToString());Entry->SetBoolField(TEXT("present"),Found);
                    Captives.Add(MakeShared<FJsonValueObject>(Entry));Present&=Found;
                }
                Row->SetArrayField(TEXT("captiveBones"),Captives);Row->SetBoolField(TEXT("allCaptiveBonesPresent"),Present);Models&=Present;
            }
            Row->SetNumberField(TEXT("forwardAxisEnum"),int32(Mesh->GetForwardAxis()));Row->SetBoolField(TEXT("forwardAxisIsX"),Mesh->GetForwardAxis()==EAxis::X);
            const FVector Origin=ReferenceBoneLocation(Mesh,TEXT("attack_origin"));Row->SetNumberField(TEXT("referenceAttackOriginXcm"),Origin.X);Row->SetNumberField(TEXT("referenceAttackOriginYcm"),Origin.Y);
            Row->SetNumberField(TEXT("attackContactNormalized"),Card->AttackContactNormalized);Row->SetNumberField(TEXT("attackAnticipationSeconds"),Card->AttackAnticipationSeconds);
            Row->SetNumberField(TEXT("attackRecoverySeconds"),Card->AttackRecoverySeconds);Row->SetNumberField(TEXT("gaitStrideMeters"),Card->GaitStrideMeters);
            Row->SetNumberField(TEXT("deathCleanupSeconds"),Card->DeathCleanupSeconds);Row->SetNumberField(TEXT("baseModelScale"),Card->BaseModelScale);
            Row->SetStringField(TEXT("portrait"),Card->Illustration?Card->Illustration->GetPathName():TEXT(""));
            TArray<TSharedPtr<FJsonValue>> Clips;
            for(const auto& Pair:Card->Animations)if(Pair.Value)
            {
                auto Clip=MakeShared<FJsonObject>();Clip->SetStringField(TEXT("name"),Pair.Key.ToString());Clip->SetStringField(TEXT("asset"),Pair.Value->GetPathName());
                Clip->SetNumberField(TEXT("lengthSeconds"),Pair.Value->GetPlayLength());Clip->SetNumberField(TEXT("targetSamplingFPS"),Pair.Value->GetSamplingFrameRate().AsDecimal());
#if WITH_EDITOR
                if(const auto* Data=Pair.Value->GetDataModel())Clip->SetNumberField(TEXT("sourceFPS"),Data->GetFrameRate().AsDecimal());
#endif
                Clips.Add(MakeShared<FJsonValueObject>(Clip));
            }
            Row->SetArrayField(TEXT("clips"),Clips);
        }
        else Materials=false;
        Cards.Add(MakeShared<FJsonValueObject>(Row));
    }
    for(const auto& Body:State.entities)if(SelectedIds.Contains(Body.id))
    {
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("id"),double(Body.id));Row->SetNumberField(TEXT("playId"),double(Body.playId));Row->SetStringField(TEXT("cardId"),UTF8_TO_TCHAR(Body.cardId.c_str()));
        Row->SetNumberField(TEXT("team"),int32(Body.team));Row->SetNumberField(TEXT("x"),Body.position.x);Row->SetNumberField(TEXT("z"),Body.position.z);
        Row->SetNumberField(TEXT("facingX"),Body.facing.x);Row->SetNumberField(TEXT("facingZ"),Body.facing.z);Row->SetBoolField(TEXT("dead"),Body.dead);
        ARiftUnitVisual* Visual=nullptr;for(TActorIterator<ARiftArenaPresentation> It(GetWorld());It;++It)if((Visual=It->Visual(Body.id)))break;
        const auto* Card=Catalog->Card(UTF8_TO_TCHAR(Body.cardId.c_str()));TArray<USkeletalMeshComponent*> Components;if(Visual)Visual->GetComponents(Components);
        auto* MeshComponent=Components.Num()>0?Components[0]:nullptr;const bool Model=Visual&&Card&&MeshComponent&&MeshComponent->GetSkeletalMeshAsset()==Card->CharacterMesh.Get();
        Models&=Model&&!Body.dead&&SelectedIds.Contains(Body.id);Row->SetBoolField(TEXT("actualImportedModelBound"),Model);
        if(Model)
        {
            Row->SetNumberField(TEXT("actualPredictedLOD"),MeshComponent->GetPredictedLODLevel());
            Row->SetBoolField(TEXT("actualComponentReceivesDecals"),MeshComponent->bReceivesDecals);
            Row->SetBoolField(TEXT("actualComponentVisible"),MeshComponent->IsVisible());
            const FBox ActualBounds=MeshComponent->Bounds.GetBox();
            Row->SetNumberField(TEXT("actualComponentBoundsMinZcm"),ActualBounds.Min.Z);
            Row->SetNumberField(TEXT("actualComponentBoundsMaxZcm"),ActualBounds.Max.Z);
            Row->SetNumberField(TEXT("actualActorZcm"),Visual->GetActorLocation().Z);
            if(Body.cardId=="mini_stampede"||Body.cardId=="stampede")
            {
                TArray<TSharedPtr<FJsonValue>> PoseBones;
                for(const TCHAR* Name:{TEXT("root"),TEXT("pelvis"),TEXT("spine"),TEXT("head"),TEXT("front_l_contact"),TEXT("front_r_contact"),TEXT("back_l_contact"),TEXT("back_r_contact")})
                {
                    const FName Bone(Name);const int32 Index=Card->CharacterMesh->GetRefSkeleton().FindBoneIndex(Bone);
                    auto PoseBone=MakeShared<FJsonObject>();PoseBone->SetStringField(TEXT("bone"),Bone.ToString());PoseBone->SetBoolField(TEXT("present"),Index!=INDEX_NONE);
                    if(Index!=INDEX_NONE)
                    {const FVector Point=MeshComponent->GetBoneTransform(Index).GetLocation();PoseBone->SetNumberField(TEXT("worldXcm"),Point.X);PoseBone->SetNumberField(TEXT("worldYcm"),Point.Y);PoseBone->SetNumberField(TEXT("worldZcm"),Point.Z);}
                    PoseBones.Add(MakeShared<FJsonValueObject>(PoseBone));
                }
                Row->SetArrayField(TEXT("actualBoarPoseBones"),PoseBones);
            }
        }
        bool Interfaces=Model;TArray<TSharedPtr<FJsonValue>> BoundMaterials;
        if(Model)for(int32 Index=0;Index<Card->CharacterMesh->GetMaterials().Num();++Index)
        {
            auto* Interface=MeshComponent->GetMaterial(Index);auto* Base=Interface?Interface->GetMaterial():nullptr;
            const FString Expected=Body.team==rift::Team::Player&&Body.invisibleUntil>State.elapsed?TEXT("/Game/Rift/Materials/M_RiftCloak.M_RiftCloak"):
                Card->CharacterMesh->GetMaterials()[Index].MaterialInterface?Card->CharacterMesh->GetMaterials()[Index].MaterialInterface->GetPathName():TEXT("");
            const FString Actual=Base?Base->GetPathName():TEXT("");Interfaces&=Actual==Expected;
            BoundMaterials.Add(MakeShared<FJsonValueString>(Actual));
        }
        Materials&=Interfaces;Row->SetBoolField(TEXT("actualComponentMaterialsBound"),Interfaces);Row->SetArrayField(TEXT("actualBaseMaterials"),BoundMaterials);
        if(Visual)
        {
            const FVector Forward=Visual->GetActorForwardVector();const double Dot=Forward.X*Body.facing.x+Forward.Y*Body.facing.z;
            const bool CorrectSide=Body.team==rift::Team::Player?Forward.Y<0:Forward.Y>0;Facing&=CorrectSide&&Dot>.6;
            Row->SetStringField(TEXT("modelId"),Visual->PresentationAssetId());Row->SetNumberField(TEXT("visualYawDegrees"),Visual->GetActorRotation().Yaw);
            Row->SetNumberField(TEXT("visualForwardX"),Forward.X);Row->SetNumberField(TEXT("visualForwardY"),Forward.Y);Row->SetNumberField(TEXT("facingAlignmentDot"),Dot);
            Row->SetBoolField(TEXT("friendlyRearEnemyFront"),CorrectSide);Row->SetStringField(TEXT("animation"),Visual->CurrentAnimation().ToString());
            Row->SetStringField(TEXT("animationAsset"),Visual->AnimationAssetPath());Row->SetNumberField(TEXT("animationPosition"),Visual->AnimationPosition());
            Row->SetNumberField(TEXT("animationCycleFraction"),Visual->AnimationCycleFraction());Row->SetNumberField(TEXT("locomotionPhase"),Visual->LocomotionPhase());
            Row->SetNumberField(TEXT("modelScale"),Visual->PresentationScale());
            Row->SetBoolField(TEXT("cloaked"),Visual->IsCloaked());Row->SetBoolField(TEXT("hiddenFromViewer"),Visual->HiddenFromViewer());
            Row->SetNumberField(TEXT("shieldFragments"),Visual->ShieldFragmentCount());
            Row->SetNumberField(TEXT("bodyHP"),Body.hp);Row->SetNumberField(TEXT("shieldHP"),Body.shieldHP);
            if(Body.cardId!="pulverizer")Walking&=Visual->CurrentAnimation()==TEXT("Locomotion");
        }
        else{Facing=false;Walking=false;}
        double TowerGap=1.e9;
        for(const auto& Tower:State.entities)if(Tower.kind>=rift::EntityKind::Guard)
        {const double DX=Body.position.x-Tower.position.x,DZ=Body.position.z-Tower.position.z;TowerGap=FMath::Min(TowerGap,FMath::Sqrt(DX*DX+DZ*DZ)-Body.radius-Tower.radius);}
        Row->SetNumberField(TEXT("nearestTowerBodyGapTiles"),TowerGap);
        ++TeamCounts[int32(Body.team)];Observed.Add(Body.id);Members.Add(MakeShared<FJsonValueObject>(Row));
    }
    const bool Paused=View->GetSpeed()==0&&View->ViewState()==&State;
    const bool Counts=TeamCounts[0]==ExpectedTeamCount&&TeamCounts[1]==ExpectedTeamCount&&Observed.Num()==ExpectedTeamCount*2;
    Report->SetArrayField(TEXT("cards"),Cards);Report->SetArrayField(TEXT("members"),Members);Report->SetArrayField(TEXT("actualHands"),Hands(State));
    Report->SetNumberField(TEXT("sampledAgeSeconds"),State.elapsed);Report->SetNumberField(TEXT("expectedSelectedCount"),ExpectedTeamCount*2);Report->SetNumberField(TEXT("actualSelectedCount"),Members.Num());
    Report->SetNumberField(TEXT("playerMembers"),TeamCounts[0]);Report->SetNumberField(TEXT("enemyMembers"),TeamCounts[1]);Report->SetNumberField(TEXT("distinctMemberIds"),Observed.Num());
    Report->SetBoolField(TEXT("paused"),Paused);Report->SetBoolField(TEXT("allImportedModelsBound"),Models);Report->SetBoolField(TEXT("allTroopsWalking"),Walking);
    Report->SetBoolField(TEXT("allExpansionMaterialsBound"),Materials);
    Report->SetBoolField(TEXT("friendlyRearEnemyFrontPassed"),Facing);Report->SetBoolField(TEXT("countsPassed"),Counts);
    bool Mechanics=true;
    if(EffectsCapture)for(const auto& Body:State.entities)if(Body.cardId=="iron_giant")
    {
        ARiftUnitVisual* Visual=nullptr;for(TActorIterator<ARiftArenaPresentation> It(GetWorld());It;++It)if((Visual=It->Visual(Body.id)))break;
        const bool BeforeImpact=EffectStage==TEXT("cast"),BeforeShieldBreak=EffectStage!=TEXT("shield");
        Mechanics&=Visual&&!Body.dead&&Body.hp==1000;
        if(Visual)Mechanics&=BeforeImpact?
            !Visual->IsCloaked()&&Body.shieldHP==310&&Visual->ShieldFragmentCount()==0&&!Visual->HiddenFromViewer():
            Visual->IsCloaked()&&(Body.team==rift::Team::Player?
                Body.shieldHP==(BeforeShieldBreak?310:0)&&Visual->ShieldFragmentCount()==(BeforeShieldBreak?0:6)&&!Visual->HiddenFromViewer():
                Body.shieldHP==310&&Visual->ShieldFragmentCount()==0&&Visual->HiddenFromViewer());
    }
    if(EffectsCapture)
    {
        const bool Required=EffectStage!=TEXT("shield");
        const FName ExpectedEffect(EffectStage==TEXT("cast")?TEXT("ShadowWisps"):TEXT("ShadowGroundRibbon"));
        int32 Instances=0;bool SourceEffects=true;
        if(Required)for(TActorIterator<ARiftArenaPresentation> It(GetWorld());It;++It)
        {
            TArray<UInstancedStaticMeshComponent*> Batches;It->GetComponents(Batches);
            for(const auto* Batch:Batches)if(Batch&&Batch->GetFName()==ExpectedEffect)
            {
                SourceEffects&=Batch->GetStaticMesh()&&Batch->IsVisible()&&Batch->GetCollisionEnabled()==ECollisionEnabled::NoCollision;
                Instances+=Batch->GetInstanceCount();
            }
        }
        SourceEffects&=!Required||Instances>0;Mechanics&=SourceEffects;
        Report->SetBoolField(TEXT("nativeSourceEffectsRequired"),Required);
        Report->SetStringField(TEXT("actualNativeSourceEffect"),Required?ExpectedEffect.ToString():TEXT(""));
        Report->SetNumberField(TEXT("actualNativeSourceEffectCount"),Instances);
        Report->SetBoolField(TEXT("actualNativeSourceEffectsPassed"),SourceEffects);
    }
    Report->SetBoolField(TEXT("actualMechanicVisualsPassed"),Mechanics);
    const bool Passed=SetupPassed&&Counts&&Models&&Materials&&Facing&&Walking&&Paused&&Mechanics;
    Report->SetBoolField(TEXT("passed"),Passed);
    if(!Passed)RIFT_LOG(LogRift,Error,TEXT("Selected gallery predicates: setup=%d counts=%d models=%d materials=%d facing=%d walking=%d paused=%d mechanics=%d"),
        int32(SetupPassed),int32(Counts),int32(Models),int32(Materials),int32(Facing),int32(Walking),int32(Paused),int32(Mechanics));
    return Passed;
}

bool ARiftExpansionCapture::BuildWardenDeath(URiftMatchSubsystem* Match,const TSharedRef<FJsonObject>& Report)
{
    FParse::Value(FCommandLine::Get(),TEXT("RiftWardenDeathStage="),WardenDeathStage);
    if(WardenDeathStage!=TEXT("before")&&WardenDeathStage!=TEXT("death")&&WardenDeathStage!=TEXT("children")&&WardenDeathStage!=TEXT("cleanup"))return false;
    rift::MatchOptions Options;Options.seed=74021;Options.aiEnabled={false,false};
    Options.decks[0]={"bone_warden","ironclad","ember_archer","twin_blades","arc_mage","mini_stampede","pulverizer","nova_flask"};
    Options.decks[1]={"vampire_bats","ironclad","ember_archer","twin_blades","arc_mage","mini_stampede","pulverizer","nova_flask"};
    if(!rift::ValidateDeck(Options.decks[0])||!rift::ValidateDeck(Options.decks[1]))return false;
    const auto* Definition=rift::FindCard("bone_warden");const auto* Bats=rift::FindCard("vampire_bats");
    auto* Catalog=GetWorld()->GetGameInstance()->GetSubsystem<URiftAssetCatalogSubsystem>();
    const auto* Card=Catalog->Card(TEXT("bone_warden"));
    if(!Definition||!Bats||!Card||Definition->deathSpawnId!="bone_warden_skeleton"||Definition->deathSpawnCount!=4||Card->DeathCleanupSeconds<=0)return false;
    WardenCleanup=FMath::Clamp(double(Card->DeathCleanupSeconds),.1,1.05);
    auto Prepare=[&](rift::Match& Sim,TArray<TSharedPtr<FJsonValue>>* Rows)->bool
    {
        bool Paid=true;
        for(auto Team:{rift::Team::Player,rift::Team::Enemy})
        {
            const auto* C=Team==rift::Team::Player?Definition:Bats;const int32 Side=int32(Team);
            const auto OldHand=Sim.State().hands[Side];const auto OldQueue=Sim.State().queues[Side];
            const double Grant=10-Sim.State().aether[Side];Sim.SetAether(Team,10);
            const double Before=Sim.State().spent[Side];const auto Point=rift::Vec2{rift::arena::BridgeCenterX,Side==0?2.5:-2.5};
            const bool Accepted=OldHand[0]==C->id&&Sim.Play(Team,0,Point,"warden_death_paid_capture");
            uint64 PlayId=0;bool Sandbox=true;for(const auto& E:Sim.Events())if(E.type=="card_play"&&E.cardId==C->id&&E.team==Team){PlayId=E.playId;Sandbox=E.sandbox;}
            int32 Count=0;TArray<TSharedPtr<FJsonValue>> Ids;
            for(const auto& Body:Sim.State().entities)if(Body.cardId==C->id&&Body.team==Team&&Body.playId==PlayId){++Count;Ids.Add(MakeShared<FJsonValueNumber>(double(Body.id)));}
            bool Cycle=Accepted&&!OldQueue.empty()&&Sim.State().hands[Side][0]==OldQueue.front()&&Sim.State().queues[Side].back()==C->id;
            for(int32 I=1;I<4;++I)Cycle&=OldHand[I]==Sim.State().hands[Side][I];
            const bool Exact=Accepted&&!Sandbox&&PlayId>0&&Count==C->count&&Cycle&&FMath::IsNearlyEqual(Sim.State().spent[Side]-Before,double(C->cost),1e-9);
            Paid&=Exact;
            if(Rows)
            {
                auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("cardId"),UTF8_TO_TCHAR(C->id.c_str()));Row->SetNumberField(TEXT("team"),Side);
                Row->SetNumberField(TEXT("playId"),double(PlayId));Row->SetBoolField(TEXT("accepted"),Accepted);Row->SetBoolField(TEXT("sandbox"),Sandbox);
                Row->SetBoolField(TEXT("handCycledOnce"),Cycle);Row->SetNumberField(TEXT("fixtureAetherGrant"),Grant);Row->SetNumberField(TEXT("cost"),C->cost);
                Row->SetNumberField(TEXT("spentDelta"),Sim.State().spent[Side]-Before);Row->SetNumberField(TEXT("actualMembers"),Count);Row->SetNumberField(TEXT("expectedMembers"),C->count);
                Row->SetArrayField(TEXT("memberIds"),Ids);Row->SetBoolField(TEXT("passed"),Exact);Rows->Add(MakeShared<FJsonValueObject>(Row));
            }
        }
        return Paid;
    };
    // Observe the real deterministic lethal step first. The before-stage is
    // then a fresh identical paid match stopped one tick before that event.
    // Neither simulation receives HP, position, death or child mutations.
    rift::Match Reference(Options);if(!Prepare(Reference,nullptr))return false;uint64 ReferenceId=0;
    for(const auto& Body:Reference.State().entities)if(Body.cardId=="bone_warden")ReferenceId=Body.id;
    Reference.DrainEvents();int32 ReferenceDeaths=0;FString ReferenceKiller;
    for(int32 Step=1;Step<=1800&&!WardenDeathStep;++Step)
    {
        Reference.Step(1./60.);for(const auto& E:Reference.DrainEvents())if(E.type=="death"&&E.target==ReferenceId)
        {++ReferenceDeaths;WardenDeathStep=Step;WardenDied=E.time;ReferenceKiller=UTF8_TO_TCHAR(E.cardId.c_str());}
    }
    if(ReferenceDeaths!=1||WardenDeathStep<2||ReferenceKiller!=TEXT("vampire_bats"))return false;
    WardenFixedSteps=WardenDeathStage==TEXT("before")?WardenDeathStep-1:
        WardenDeathStep+(WardenDeathStage==TEXT("death")?1:WardenDeathStage==TEXT("children")?6:FMath::CeilToInt(WardenCleanup*60.)+3);
    Fixture=std::make_unique<rift::Match>(Options);View=Match;Match->SetSpeed(0);Match->SetReplayView(&Fixture->State());RenderState(GetWorld());
    TArray<TSharedPtr<FJsonValue>> Plays;bool Paid=Prepare(*Fixture,&Plays);
    for(const auto& Body:Fixture->State().entities)if(Body.cardId=="bone_warden"){WardenId=Body.id;WardenPlayId=Body.playId;}
    for(const auto& E:Fixture->DrainEvents())Match->OnEvent.Broadcast(E);RenderState(GetWorld());
    auto Visual=[&](uint64 Id)->ARiftUnitVisual*{for(TActorIterator<ARiftArenaPresentation> It(GetWorld());It;++It)if(auto* V=It->Visual(Id))return V;return nullptr;};
    auto BirthRow=[&](const rift::Entity& Body)
    {
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("id"),double(Body.id));Row->SetNumberField(TEXT("parentEntityId"),double(Body.parentEntityId));
        Row->SetNumberField(TEXT("playId"),double(Body.playId));Row->SetStringField(TEXT("cardId"),UTF8_TO_TCHAR(Body.cardId.c_str()));
        Row->SetStringField(TEXT("attributionCardId"),UTF8_TO_TCHAR(Body.attributionCardId.c_str()));Row->SetNumberField(TEXT("bornSeconds"),Body.born);Row->SetBoolField(TEXT("dead"),Body.dead);
        auto* V=Visual(Body.id);auto* Child=Catalog->Card(TEXT("bone_warden_skeleton"));TArray<USkeletalMeshComponent*> Components;if(V)V->GetComponents(Components);
        auto* Component=Components.Num()?Components[0]:nullptr;const bool Bound=V&&Child&&Component&&Component->GetSkeletalMeshAsset()==Child->CharacterMesh.Get();
        Row->SetBoolField(TEXT("actualImportedModelBound"),Bound);Row->SetBoolField(TEXT("noncollidingVisual"),Component&&Component->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
        Row->SetStringField(TEXT("modelId"),V?V->PresentationAssetId():TEXT(""));Row->SetStringField(TEXT("animation"),V?V->CurrentAnimation().ToString():TEXT(""));
        Row->SetStringField(TEXT("animationAsset"),V?V->AnimationAssetPath():TEXT(""));Row->SetNumberField(TEXT("animationPosition"),V?V->AnimationPosition():-1);
        auto* Clip=Child?Child->Animations.Find(TEXT("Deploy")):nullptr;
        Row->SetStringField(TEXT("expectedDeployAnimationAsset"),Clip&&*Clip?(*Clip)->GetPathName():TEXT(""));
        Row->SetBoolField(TEXT("actualDeployAnimationBound"),Bound&&Clip&&*Clip&&V->CurrentAnimation()==TEXT("Deploy")&&V->AnimationAssetPath()==(*Clip)->GetPathName());
        return Row;
    };
    TMap<FName,FTransform> FirstRelative;TMap<FName,double> MaxShift,MaxAngle;TMap<FName,int32> MotionSamples;TMap<FName,FName> MotionParents;
    auto ObserveCaptives=[&]()
    {
        const rift::Entity* Body=nullptr;for(const auto& B:Fixture->State().entities)if(B.id==WardenId){Body=&B;break;}
        auto* V=Visual(WardenId);if(!Body||Body->dead||!V)return;TArray<USkeletalMeshComponent*> Components;V->GetComponents(Components);if(!Components.Num())return;
        auto* Component=Components[0];auto* Mesh=Component->GetSkeletalMeshAsset();if(!Mesh)return;const auto& Skeleton=Mesh->GetRefSkeleton();
        for(const TCHAR* CaptiveRole:{TEXT("head"),TEXT("arm")})for(int32 I=0;I<4;++I)
        {
            const FName Bone(*FString::Printf(TEXT("captive_%s_%d"),CaptiveRole,I));const int32 Index=Skeleton.FindBoneIndex(Bone);if(Index==INDEX_NONE)continue;
            const int32 Parent=Skeleton.GetParentIndex(Index);if(Parent==INDEX_NONE)continue;MotionParents.Add(Bone,Skeleton.GetBoneName(Parent));
            const auto Relative=Component->GetBoneTransform(Index).GetRelativeTransform(Component->GetBoneTransform(Parent));
            if(!FirstRelative.Contains(Bone))FirstRelative.Add(Bone,Relative);
            MaxShift.FindOrAdd(Bone)=FMath::Max(MaxShift.FindOrAdd(Bone),double((Relative.GetLocation()-FirstRelative[Bone].GetLocation()).Size()));
            MaxAngle.FindOrAdd(Bone)=FMath::Max(MaxAngle.FindOrAdd(Bone),double(FMath::RadiansToDegrees(Relative.GetRotation().AngularDistance(FirstRelative[Bone].GetRotation()))));
            ++MotionSamples.FindOrAdd(Bone);
        }
    };
    ObserveCaptives();bool DeathTiming=true;
    for(int32 Step=1;Step<=WardenFixedSteps;++Step)
    {
        Fixture->Step(1./60.);auto Events=Fixture->DrainEvents();
        for(const auto& E:Events)
        {
            if(E.type=="death"&&E.target==WardenId){++WardenDeaths;DeathTiming&=Step==WardenDeathStep&&FMath::IsNearlyEqual(E.time,WardenDied,1e-9)&&E.cardId=="vampire_bats";}
            if(E.type=="entity_spawn"&&E.parentEntityId==WardenId){++WardenChildSpawnEvents;DeathTiming&=E.source>0&&!WardenChildIds.Contains(E.source)&&E.cardId=="bone_warden_skeleton"&&E.playId==WardenPlayId&&E.attributionCardId=="bone_warden"&&FMath::IsNearlyEqual(E.time,WardenDied,1e-9);WardenChildIds.Add(E.source);}
            Match->OnEvent.Broadcast(E);
        }
        RenderState(GetWorld());ObserveCaptives();
        if(Step==WardenDeathStep)
        {
            auto* ParentVisual=Visual(WardenId);auto* Clip=Card->Animations.Find(TEXT("Death"));
            WardenDeathAnimationObserved=ParentVisual&&ParentVisual->IsDead()&&Clip&&*Clip&&ParentVisual->CurrentAnimation()==TEXT("Death")&&ParentVisual->AnimationAssetPath()==(*Clip)->GetPathName();
            for(const auto& Body:Fixture->State().entities)if(Body.parentEntityId==WardenId)WardenChildBirthVisuals.Add(MakeShared<FJsonValueObject>(BirthRow(Body)));
        }
    }
    bool Motion=FirstRelative.Num()==8;for(const TCHAR* CaptiveRole:{TEXT("head"),TEXT("arm")})for(int32 I=0;I<4;++I)
    {
        const FName Bone(*FString::Printf(TEXT("captive_%s_%d"),CaptiveRole,I));auto Row=MakeShared<FJsonObject>();const bool Present=FirstRelative.Contains(Bone);
        Row->SetStringField(TEXT("bone"),Bone.ToString());Row->SetBoolField(TEXT("present"),Present);Row->SetStringField(TEXT("parent"),MotionParents.FindRef(Bone).ToString());
        Row->SetNumberField(TEXT("livingSamples"),MotionSamples.FindRef(Bone));Row->SetNumberField(TEXT("relativeTranslationChangeCm"),MaxShift.FindRef(Bone));Row->SetNumberField(TEXT("relativeRotationChangeDegrees"),MaxAngle.FindRef(Bone));
        const bool Changed=Present&&MotionSamples.FindRef(Bone)>=2&&(MaxShift.FindRef(Bone)>.001||MaxAngle.FindRef(Bone)>.01);
        Row->SetBoolField(TEXT("actualLivingSecondaryMotionObserved"),Changed);Motion&=Changed;WardenCaptiveMotion.Add(MakeShared<FJsonValueObject>(Row));
    }
    Report->SetNumberField(TEXT("schema"),1);Report->SetStringField(TEXT("profile"),TEXT("expansion_warden_death"));Report->SetStringField(TEXT("stage"),WardenDeathStage);
    Report->SetStringField(TEXT("route"),TEXT("Two legal paid cards; explicit initial fixture aether grant; real Vampire Bats lethal combat; 1/60-second steps; production snapshot presentation. Before-stage repeats the identical observed match one tick before its actual death."));
    Report->SetStringField(TEXT("pixelLimitation"),TEXT("State/animation/effect checks bind the actual paused request state. The subsequent PNG still requires physical inspection."));
    Report->SetArrayField(TEXT("plays"),Plays);Report->SetBoolField(TEXT("paidCostAndCyclePassed"),Paid);Report->SetNumberField(TEXT("referenceDeathStep"),WardenDeathStep);
    Report->SetNumberField(TEXT("referenceDeathSeconds"),WardenDied);Report->SetStringField(TEXT("referenceLethalCardId"),ReferenceKiller);
    Report->SetNumberField(TEXT("fixedSteps"),WardenFixedSteps);Report->SetNumberField(TEXT("cleanupSeconds"),WardenCleanup);Report->SetBoolField(TEXT("deterministicDeathTimingPassed"),DeathTiming);
    Report->SetBoolField(TEXT("actualLivingSecondaryMotionPassed"),Motion);
    SetupPassed=Paid&&DeathTiming&&Motion&&WardenId>0&&WardenPlayId>0&&WardenDeaths==(WardenDeathStage==TEXT("before")?0:1)&&WardenChildIds.Num()==(WardenDeathStage==TEXT("before")?0:4)&&WardenChildSpawnEvents==WardenChildIds.Num();
    Match->SetSpeed(0);RenderState(GetWorld());return SampleWardenDeath(Report);
}

bool ARiftExpansionCapture::SampleWardenDeath(const TSharedRef<FJsonObject>& Report) const
{
    if(!Fixture||!View.IsValid())return false;const auto& State=Fixture->State();auto* Catalog=GetWorld()->GetGameInstance()->GetSubsystem<URiftAssetCatalogSubsystem>();
    auto Visual=[&](uint64 Id)->ARiftUnitVisual*{for(TActorIterator<ARiftArenaPresentation> It(GetWorld());It;++It)if(auto* V=It->Visual(Id))return V;return nullptr;};
    const rift::Entity* Parent=nullptr;for(const auto& Body:State.entities)if(Body.id==WardenId){Parent=&Body;break;}
    auto* Corpse=Visual(WardenId);const bool Before=WardenDeathStage==TEXT("before"),Cleanup=WardenDeathStage==TEXT("cleanup");
    bool Births=Before||WardenChildBirthVisuals.Num()==4;
    for(const auto& Value:WardenChildBirthVisuals)
    {
        auto Row=Value->AsObject();Births&=Row->GetNumberField(TEXT("parentEntityId"))==double(WardenId)&&Row->GetNumberField(TEXT("playId"))==double(WardenPlayId)&&
            Row->GetStringField(TEXT("attributionCardId"))==TEXT("bone_warden")&&!Row->GetBoolField(TEXT("dead"))&&Row->GetBoolField(TEXT("actualImportedModelBound"))&&Row->GetBoolField(TEXT("actualDeployAnimationBound"))&&Row->GetBoolField(TEXT("noncollidingVisual"));
    }
    TArray<TSharedPtr<FJsonValue>> ChildRows;int32 LivingChildren=0,LiveChildVisuals=0;bool ChildBindings=true;
    for(const auto& Body:State.entities)if(WardenChildIds.Contains(Body.id))
    {
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("id"),double(Body.id));Row->SetNumberField(TEXT("parentEntityId"),double(Body.parentEntityId));Row->SetNumberField(TEXT("playId"),double(Body.playId));
        Row->SetStringField(TEXT("cardId"),UTF8_TO_TCHAR(Body.cardId.c_str()));Row->SetStringField(TEXT("attributionCardId"),UTF8_TO_TCHAR(Body.attributionCardId.c_str()));Row->SetBoolField(TEXT("dead"),Body.dead);
        auto* V=Visual(Body.id);Row->SetBoolField(TEXT("visualPresent"),V!=nullptr);Row->SetStringField(TEXT("animationAsset"),V?V->AnimationAssetPath():TEXT(""));
        Row->SetStringField(TEXT("animation"),V?V->CurrentAnimation().ToString():TEXT(""));Row->SetNumberField(TEXT("animationPosition"),V?V->AnimationPosition():-1);
        if(!Body.dead){++LivingChildren;LiveChildVisuals+=V?1:0;ChildBindings&=V&&!V->IsDead()&&V->PresentationAssetId()==TEXT("bone_warden_skeleton")&&!V->AnimationAssetPath().IsEmpty();}
        ChildRows.Add(MakeShared<FJsonValueObject>(Row));
    }
    int32 Threads=0;for(TActorIterator<ARiftArenaPresentation> It(GetWorld());It;++It)
    {TArray<UInstancedStaticMeshComponent*> Batches;It->GetComponents(Batches);for(const auto* Batch:Batches)if(Batch&&Batch->GetFName()==TEXT("FourWardenSoulThreads"))Threads+=Batch->GetInstanceCount();}
    const auto* Warden=Catalog->Card(TEXT("bone_warden"));auto* DeathClip=Warden?Warden->Animations.Find(TEXT("Death")):nullptr;
    const bool DeathAnimation=Corpse&&Corpse->IsDead()&&DeathClip&&*DeathClip&&Corpse->CurrentAnimation()==TEXT("Death")&&Corpse->AnimationAssetPath()==(*DeathClip)->GetPathName();
    const bool Lifecycle=Before?Parent&&!Parent->dead&&Corpse&&!Corpse->IsDead()&&ChildRows.Num()==0&&Threads==0:
        Cleanup?Corpse==nullptr&&State.elapsed>WardenDied+WardenCleanup&&Threads==0:
        Parent&&Parent->dead&&DeathAnimation&&LivingChildren==4&&LiveChildVisuals==4&&Threads==20&&State.elapsed<WardenDied+.30;
    const auto Paid=State.telemetry[0].find("bone_warden");const bool Attribution=Paid!=State.telemetry[0].end()&&Paid->second.plays==1&&Paid->second.spawns==1&&
        Paid->second.childSpawns==std::uint64_t(Before?0:4)&&State.telemetry[0].find("bone_warden_skeleton")==State.telemetry[0].end();
    const bool Paused=View->GetSpeed()==0&&View->ViewState()==&State;
    Report->SetNumberField(TEXT("wardenId"),double(WardenId));Report->SetNumberField(TEXT("wardenPlayId"),double(WardenPlayId));Report->SetNumberField(TEXT("sampledAgeSeconds"),State.elapsed);
    Report->SetNumberField(TEXT("actualWardenDeaths"),WardenDeaths);Report->SetNumberField(TEXT("actualChildSpawnEvents"),WardenChildSpawnEvents);
    Report->SetBoolField(TEXT("wardenVisualPresent"),Corpse!=nullptr);Report->SetBoolField(TEXT("wardenBodyPresent"),Parent!=nullptr);Report->SetBoolField(TEXT("wardenDead"),Parent&&Parent->dead);
    Report->SetBoolField(TEXT("actualWardenDeathAnimationBound"),DeathAnimation);Report->SetStringField(TEXT("wardenAnimationAsset"),Corpse?Corpse->AnimationAssetPath():TEXT(""));
    Report->SetNumberField(TEXT("wardenAnimationPosition"),Corpse?Corpse->AnimationPosition():-1);Report->SetNumberField(TEXT("soulThreadInstances"),Threads);
    Report->SetArrayField(TEXT("children"),ChildRows);Report->SetArrayField(TEXT("childBirthVisuals"),WardenChildBirthVisuals);Report->SetArrayField(TEXT("captiveMotion"),WardenCaptiveMotion);
    Report->SetNumberField(TEXT("livingChildren"),LivingChildren);Report->SetNumberField(TEXT("livingChildVisuals"),LiveChildVisuals);
    Report->SetBoolField(TEXT("parentPaidAttributionPassed"),Attribution);Report->SetBoolField(TEXT("childBirthModelsAndAnimationsPassed"),Births&&ChildBindings);
    Report->SetBoolField(TEXT("actualLifecyclePassed"),Lifecycle);Report->SetBoolField(TEXT("setupPassed"),SetupPassed);Report->SetBoolField(TEXT("paused"),Paused);
    Report->SetBoolField(TEXT("actualDeathAnimationObservedAtLethalStep"),WardenDeathAnimationObserved);
    const bool Passed=SetupPassed&&Births&&ChildBindings&&Lifecycle&&Attribution&&Paused&&(Before||WardenDeathAnimationObserved);Report->SetBoolField(TEXT("passed"),Passed);return Passed;
}

void ARiftExpansionCapture::EndPlay(const EEndPlayReason::Type Reason)
{
    if(View.IsValid()&&Fixture&&View->ViewState()==&Fixture->State())View->SetReplayView(nullptr);
    Fixture.reset();Super::EndPlay(Reason);
}
