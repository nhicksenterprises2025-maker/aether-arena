#include "RiftGameMode.h"
#include "RiftDiagnostics.h"
#include "RiftUIWidget.h"
#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "RiftReplaySubsystem.h"
#include "Presentation/RiftBattleAudioSubsystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "RiftMetaSimulationSubsystem.h"
#include "Presentation/RiftArenaPresentation.h"
#include "Presentation/RiftArenaGeometry.h"
#include "Presentation/RiftBattleLayout.h"
#include "Presentation/RiftUnitVisual.h"
#include "EngineUtils.h"
#include "Camera/CameraComponent.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/World.h"
#include "Engine/UserInterfaceSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "TimerManager.h"
#include "HAL/PlatformMisc.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Input/Events.h"

namespace
{
double CaptureTowerSegmentClearance(const rift::Snapshot& State,const rift::Entity& Unit,rift::Vec2 From)
{
    double Minimum=1.e9;const double DX=Unit.position.x-From.x,DZ=Unit.position.z-From.z,Length=DX*DX+DZ*DZ;
    for(const auto& Tower:State.entities)if(!Tower.dead&&Tower.kind>=rift::EntityKind::Guard)
    {
        const double T=Length>1.e-12?FMath::Clamp(((Tower.position.x-From.x)*DX+(Tower.position.z-From.z)*DZ)/Length,0.,1.):0.;
        const double X=From.x+T*DX-Tower.position.x,Z=From.z+T*DZ-Tower.position.z;
        Minimum=FMath::Min(Minimum,FMath::Sqrt(X*X+Z*Z)-Unit.radius-Tower.radius-.22);
    }
    return Minimum;
}
bool BuildTowerRouteCapture(URiftMatchSubsystem* Match,const TSharedRef<FJsonObject>& Report,const FString& Case,float Age)
{
    auto* Sim=Match->Simulation();FString TeamName=TEXT("player");FParse::Value(FCommandLine::Get(),TEXT("RiftRouteTeam="),TeamName);
    const auto Team=TeamName==TEXT("enemy")?rift::Team::Enemy:rift::Team::Player;
    const int32 TeamIndex=int32(Team),Lane=Case==TEXT("left_pocket")?-1:1;const double Side=TeamIndex==0?1.:-1.;
    uint64 GuardId=0,CoreId=0,OppositeId=0;
    TArray<TSharedPtr<FJsonValue>> Towers,Rows,Plays;
    for(const auto& Tower:Sim->State().entities)if(Tower.team!=Team)
    {if(Tower.kind==rift::EntityKind::Core)CoreId=Tower.id;else if(Tower.lane==Lane)GuardId=Tower.id;else OppositeId=Tower.id;}
    Match->SampleBeforeMutation();const bool Destroyed=Sim->SetTowerHP(GuardId,0);Match->FlushEvents();
    for(const auto& Tower:Sim->State().entities)if(Tower.kind>=rift::EntityKind::Guard)
    {
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("id"),Tower.id);Row->SetStringField(TEXT("team"),UTF8_TO_TCHAR(rift::TeamName(Tower.team).c_str()));
        Row->SetStringField(TEXT("kind"),Tower.kind==rift::EntityKind::Core?TEXT("core"):TEXT("guard"));Row->SetNumberField(TEXT("lane"),Tower.lane);
        Row->SetNumberField(TEXT("x"),Tower.position.x);Row->SetNumberField(TEXT("z"),Tower.position.z);Row->SetNumberField(TEXT("radius"),Tower.radius);Row->SetBoolField(TEXT("dead"),Tower.dead);
        Towers.Add(MakeShared<FJsonValueObject>(Row));
    }
    bool Paid=true,SpawnClear=true,StepsClear=true,SegmentsClear=true,CoreSeen=true,BridgeCorrect=true,Progress=true;
    TMap<uint64,rift::Vec2> Previous;TMap<uint64,bool> InBridge;TMap<uint64,int32> LastBridge;
    int32 Expected=0;
    for(const bool Pocket:{false,true})
    {
        int32 Slot=INDEX_NONE;const rift::Card* Selected=nullptr;
        for(int32 I=0;I<4;++I)
        {
            const auto* Card=rift::FindCard(Sim->State().hands[TeamIndex][I]);if(!Card||Card->spell||Card->building||Card->flying)continue;
            if(!Selected||(!Pocket&&Card->moveSpeed<Selected->moveSpeed)||(Pocket&&Card->hp>Selected->hp)){Selected=Card;Slot=I;}
        }
        if(!Selected){Paid=false;break;}
        const FString Role=Pocket?TEXT("pocket"):TEXT("own_half");const std::string CardId=Selected->id;
        const int32 Count=Selected->count;const auto OldHand=Sim->State().hands[TeamIndex];const auto Queue=Sim->State().queues[TeamIndex];
        Match->SampleBeforeMutation();Sim->SetAether(Team,10);Match->FlushEvents();
        const double Spent=Sim->State().spent[TeamIndex];const rift::Vec2 Drop{Lane*(Pocket?8.5:7.5),Side*(Pocket?-5.5:4.5)};
        const size_t Before=Sim->State().entities.size();Match->SampleBeforeMutation();const bool Accepted=Sim->Play(Team,Slot,Drop,"capture_paid_route");Match->FlushEvents();
        auto Play=MakeShared<FJsonObject>();Play->SetStringField(TEXT("role"),Role);Play->SetStringField(TEXT("cardId"),UTF8_TO_TCHAR(CardId.c_str()));
        Play->SetNumberField(TEXT("handIndex"),Slot);Play->SetNumberField(TEXT("cost"),Selected->cost);Play->SetNumberField(TEXT("memberCount"),Count);
        Play->SetNumberField(TEXT("aetherBefore"),10);Play->SetNumberField(TEXT("aetherAfter"),Sim->State().aether[TeamIndex]);
        Play->SetNumberField(TEXT("spentDelta"),Sim->State().spent[TeamIndex]-Spent);Play->SetBoolField(TEXT("accepted"),Accepted);
        const bool Cycled=Accepted&&Sim->State().hands[TeamIndex][Slot]==Queue.front()&&Sim->State().queues[TeamIndex].back()==CardId;
        bool Others=true;for(int32 I=0;I<4;++I)if(I!=Slot)Others&=Sim->State().hands[TeamIndex][I]==OldHand[I];
        Play->SetBoolField(TEXT("handCycledOnce"),Cycled&&Others);Plays.Add(MakeShared<FJsonValueObject>(Play));
        Paid&=Accepted&&Cycled&&Others&&FMath::IsNearlyEqual(Sim->State().spent[TeamIndex]-Spent,double(Selected->cost),1.e-9)&&
            FMath::IsNearlyEqual(Sim->State().aether[TeamIndex],double(10-Selected->cost),1.e-9);Expected+=Count;
        for(size_t I=Before;I<Sim->State().entities.size();++I)
        {
            const auto& Unit=Sim->State().entities[I];const auto Tile=rift::SnapToTile(Drop);const double Clear=CaptureTowerSegmentClearance(Sim->State(),Unit,Unit.position);
            auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("id"),Unit.id);Row->SetStringField(TEXT("team"),UTF8_TO_TCHAR(rift::TeamName(Unit.team).c_str()));
            Row->SetStringField(TEXT("cardId"),UTF8_TO_TCHAR(Unit.cardId.c_str()));Row->SetStringField(TEXT("role"),Role);
            Row->SetNumberField(TEXT("requestedX"),Tile.x);Row->SetNumberField(TEXT("requestedZ"),Tile.z);Row->SetNumberField(TEXT("spawnX"),Unit.position.x);Row->SetNumberField(TEXT("spawnZ"),Unit.position.z);
            Row->SetNumberField(TEXT("x"),Unit.position.x);Row->SetNumberField(TEXT("z"),Unit.position.z);Row->SetBoolField(TEXT("present"),true);Row->SetBoolField(TEXT("alive"),true);
            Row->SetNumberField(TEXT("targetId"),0);Row->SetStringField(TEXT("targetKind"),TEXT("none"));
            Row->SetNumberField(TEXT("radius"),Unit.radius);Row->SetNumberField(TEXT("intendedBridge"),Lane);Row->SetNumberField(TEXT("expectedCoreId"),CoreId);
            Row->SetNumberField(TEXT("spawnTowerClearance"),Clear);Row->SetNumberField(TEXT("minimumStepTowerClearance"),Clear);Row->SetNumberField(TEXT("minimumSegmentTowerClearance"),Clear);
            Row->SetBoolField(TEXT("sawCoreTarget"),false);Row->SetBoolField(TEXT("crossedRiver"),false);Row->SetBoolField(TEXT("bridgeHistoryPassed"),true);
            Row->SetArrayField(TEXT("crossingHistory"),{});Row->SetArrayField(TEXT("bridgeHistory"),{});Rows.Add(MakeShared<FJsonValueObject>(Row));Previous.Add(Unit.id,Unit.position);
            SpawnClear&=Clear>=-1.e-6;
        }
    }
    const double Started=Sim->State().elapsed;const int32 Steps=FMath::RoundToInt(double(Age)*60.);
    for(int32 Step=0;Step<Steps;++Step)
    {
        Sim->Step(1./60.);Match->FlushEvents();
        for(const auto& Value:Rows)
        {
            const auto Row=Value->AsObject();const uint64 Id=uint64(Row->GetNumberField(TEXT("id")));
            for(const auto& Unit:Sim->State().entities)if(Unit.id==Id)
            {
                Row->SetNumberField(TEXT("x"),Unit.position.x);Row->SetNumberField(TEXT("z"),Unit.position.z);Row->SetBoolField(TEXT("alive"),!Unit.dead);Row->SetBoolField(TEXT("present"),true);
                const rift::Entity* Target=nullptr;for(const auto& Entity:Sim->State().entities)if(Entity.id==Unit.target){Target=&Entity;break;}
                Row->SetNumberField(TEXT("targetId"),Unit.target);Row->SetStringField(TEXT("targetKind"),!Target?TEXT("none"):Target->kind==rift::EntityKind::Core?TEXT("core"):Target->kind==rift::EntityKind::Guard?TEXT("guard"):Target->kind==rift::EntityKind::Building?TEXT("building"):TEXT("troop"));
                if(Unit.target==CoreId)Row->SetBoolField(TEXT("sawCoreTarget"),true);
                if(Unit.dead)break;
                const double Clear=CaptureTowerSegmentClearance(Sim->State(),Unit,Unit.position),Segment=CaptureTowerSegmentClearance(Sim->State(),Unit,Previous.FindChecked(Id));
                Row->SetNumberField(TEXT("minimumStepTowerClearance"),FMath::Min(Clear,Row->GetNumberField(TEXT("minimumStepTowerClearance"))));
                Row->SetNumberField(TEXT("minimumSegmentTowerClearance"),FMath::Min(Segment,Row->GetNumberField(TEXT("minimumSegmentTowerClearance"))));StepsClear&=Clear>=-1.e-6;SegmentsClear&=Segment>=-1.e-6;
                const bool Banking=FMath::Abs(Unit.position.z)<rift::arena::RiverHalfWidth+.28;
                const bool Correct=!Unit.bridge||Unit.bridge==Lane;bool RiverCorrect=true;
                if(Banking)RiverCorrect=Unit.position.x*Lane>0&&FMath::Abs(Unit.position.x-Lane*rift::arena::BridgeCenterX)<=rift::arena::BridgeWidth*.5-.16-Unit.radius*.92+1.e-6;
                Row->SetBoolField(TEXT("bridgeHistoryPassed"),Row->GetBoolField(TEXT("bridgeHistoryPassed"))&&Correct&&RiverCorrect);
                auto HistoryItem=[&](const TCHAR* Event){auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("event"),Event);Item->SetNumberField(TEXT("time"),Sim->State().elapsed);Item->SetNumberField(TEXT("x"),Unit.position.x);Item->SetNumberField(TEXT("z"),Unit.position.z);Item->SetNumberField(TEXT("bridge"),Unit.bridge);return MakeShared<FJsonValueObject>(Item);};
                if(!LastBridge.Contains(Id)||LastBridge.FindChecked(Id)!=Unit.bridge){auto History=Row->GetArrayField(TEXT("bridgeHistory"));History.Add(HistoryItem(TEXT("bridge_commit")));Row->SetArrayField(TEXT("bridgeHistory"),History);LastBridge.Add(Id,Unit.bridge);}
                auto Crossing=Row->GetArrayField(TEXT("crossingHistory"));
                if(Banking&&!InBridge.FindRef(Id))Crossing.Add(HistoryItem(TEXT("enter_bridge")));
                if(Previous.FindChecked(Id).z*Side>0&&Unit.position.z*Side<=0){Row->SetBoolField(TEXT("crossedRiver"),true);Crossing.Add(HistoryItem(TEXT("river_center")));}
                if(!Banking&&InBridge.FindRef(Id))Crossing.Add(HistoryItem(TEXT("leave_bridge")));
                Row->SetArrayField(TEXT("crossingHistory"),Crossing);InBridge.Add(Id,Banking);Previous.FindChecked(Id)=Unit.position;
                break;
            }
        }
        for(TActorIterator<ARiftArenaPresentation> It(Match->GetWorld());It;++It)It->Tick(0.f);
    }
    int32 Found=0;const bool RequireCross=Steps>=300;
    for(const auto& Value:Rows)
    {
        const auto Row=Value->AsObject();const double X=Row->GetNumberField(TEXT("x"))-Row->GetNumberField(TEXT("spawnX"));
        const double Z=Row->GetNumberField(TEXT("z"))-Row->GetNumberField(TEXT("spawnZ"));const bool Moved=FMath::Sqrt(X*X+Z*Z)>.5&&-Z*Side>.25;
        bool Present=false;for(const auto& Unit:Sim->State().entities)if(Unit.id==uint64(Row->GetNumberField(TEXT("id")))){Present=true;break;}Row->SetBoolField(TEXT("present"),Present);
        Row->SetNumberField(TEXT("distanceFromSpawn"),FMath::Sqrt(X*X+Z*Z));Row->SetNumberField(TEXT("progressForward"),-Z*Side);Row->SetBoolField(TEXT("progressPassed"),Moved);
        if(Row->GetBoolField(TEXT("present")))++Found;Progress&=Moved;CoreSeen&=Row->GetBoolField(TEXT("sawCoreTarget"));BridgeCorrect&=Row->GetBoolField(TEXT("bridgeHistoryPassed"));
        if(RequireCross&&Row->GetStringField(TEXT("role"))==TEXT("own_half"))BridgeCorrect&=Row->GetBoolField(TEXT("crossedRiver"));
    }
    const bool Passed=Destroyed&&Paid&&Plays.Num()==2&&Found==Expected&&SpawnClear&&StepsClear&&SegmentsClear&&(Steps==0||CoreSeen)&&BridgeCorrect&&(Steps<120||Progress);
    Report->SetNumberField(TEXT("schemaVersion"),2);Report->SetStringField(TEXT("routeCase"),Case);Report->SetStringField(TEXT("routeTeam"),TeamName);
    Report->SetStringField(TEXT("route"),TEXT("SetTowerHP / paid Match Play / fixed steps / live arena presentation"));Report->SetBoolField(TEXT("ordinarySpawnOnly"),false);Report->SetBoolField(TEXT("paidPlayOnly"),true);
    Report->SetNumberField(TEXT("destroyedGuardId"),GuardId);Report->SetNumberField(TEXT("oppositeGuardId"),OppositeId);Report->SetNumberField(TEXT("expectedCoreId"),CoreId);Report->SetNumberField(TEXT("intendedBridge"),Lane);
    Report->SetBoolField(TEXT("sameSideGuardDestroyed"),Destroyed);Report->SetBoolField(TEXT("paidCostAndCyclePassed"),Paid);Report->SetBoolField(TEXT("coreTargetsRequired"),Steps>0);Report->SetBoolField(TEXT("allCoreTargetsObserved"),CoreSeen);Report->SetBoolField(TEXT("allBridgeHistoriesPassed"),BridgeCorrect);Report->SetBoolField(TEXT("crossingRequired"),RequireCross);
    Report->SetNumberField(TEXT("requestedAge"),Age);Report->SetNumberField(TEXT("sampledAge"),Sim->State().elapsed-Started);Report->SetNumberField(TEXT("fixedSteps"),Steps);Report->SetNumberField(TEXT("deploymentCount"),Plays.Num());Report->SetNumberField(TEXT("expectedUnitCount"),Expected);Report->SetNumberField(TEXT("actualUnitCount"),Found);
    Report->SetNumberField(TEXT("towerNavigationPadding"),.22);Report->SetBoolField(TEXT("spawnTowerClearancePassed"),SpawnClear);Report->SetBoolField(TEXT("allStepTowerClearancePassed"),StepsClear);Report->SetBoolField(TEXT("allContinuousSegmentTowerClearancePassed"),SegmentsClear);
    Report->SetBoolField(TEXT("progressRequired"),Steps>=120);Report->SetBoolField(TEXT("progressPassed"),Progress);Report->SetArrayField(TEXT("units"),Rows);Report->SetArrayField(TEXT("towers"),Towers);Report->SetArrayField(TEXT("paidPlays"),Plays);Report->SetBoolField(TEXT("passed"),Passed);
    RIFT_LOG(LogRift,Log,TEXT("Actual tower route capture: case=%s team=%s paid=%d members=%d steps=%d core=%d bridge=%d clearance=%d progress=%d passed=%d"),*Case,*TeamName,Plays.Num(),Found,Steps,CoreSeen,BridgeCorrect,SpawnClear&&StepsClear&&SegmentsClear,Progress,Passed);
    return Passed;
}
bool BuildTowerPathingCapture(URiftMatchSubsystem* Match,const TSharedRef<FJsonObject>& Report)
{
    auto* Sim=Match->Simulation();if(!Sim)return false;Match->SetSpeed(0);
    float Age=0;FParse::Value(FCommandLine::Get(),TEXT("RiftPathingAge="),Age);
    Age=FMath::IsFinite(Age)?FMath::Clamp(Age,0.f,6.f):0.f;
    FString Case=TEXT("clearance");FParse::Value(FCommandLine::Get(),TEXT("RiftRouteCase="),Case);
    if(Case==TEXT("left_pocket")||Case==TEXT("right_pocket"))return BuildTowerRouteCapture(Match,Report,Case,Age);
    TArray<rift::Entity> Towers;for(const auto& Entity:Sim->State().entities)
        if(Entity.kind>=rift::EntityKind::Guard)Towers.Add(Entity);
    TArray<TSharedPtr<FJsonValue>> Rows,TowerRows;
    bool Deployed=true,SpawnClear=true,StepsClear=true,SegmentsClear=true;int32 Deployments=0;
    TMap<uint64,rift::Vec2> PreviousPositions;
    for(const auto& Tower:Towers)
    {
        auto TowerRow=MakeShared<FJsonObject>();TowerRow->SetNumberField(TEXT("id"),Tower.id);
        TowerRow->SetStringField(TEXT("team"),UTF8_TO_TCHAR(rift::TeamName(Tower.team).c_str()));
        TowerRow->SetStringField(TEXT("kind"),Tower.kind==rift::EntityKind::Core?TEXT("core"):TEXT("guard"));
        TowerRow->SetNumberField(TEXT("lane"),Tower.lane);TowerRow->SetNumberField(TEXT("x"),Tower.position.x);
        TowerRow->SetNumberField(TEXT("z"),Tower.position.z);TowerRow->SetNumberField(TEXT("radius"),Tower.radius);
        TowerRows.Add(MakeShared<FJsonValueObject>(TowerRow));
        const char* Card=Tower.kind==rift::EntityKind::Core?"ironclad":Tower.lane<0?"boulderback":"twin_blades";
        const rift::Vec2 Drop{Tower.position.x,Tower.position.z+(Tower.team==rift::Team::Player?1.:-1.)};
        const auto Tile=rift::SnapToTile(Drop);
        const size_t Before=Sim->State().entities.size();const bool Accepted=Sim->Spawn(Tower.team,Card,Drop);
        Deployed&=Accepted;if(Accepted)++Deployments;
        for(size_t I=Before;I<Sim->State().entities.size();++I)
        {
            const auto& Unit=Sim->State().entities[I];const double Clearance=CaptureTowerSegmentClearance(Sim->State(),Unit,Unit.position);
            auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("id"),Unit.id);
            Row->SetStringField(TEXT("team"),UTF8_TO_TCHAR(rift::TeamName(Unit.team).c_str()));
            Row->SetStringField(TEXT("cardId"),UTF8_TO_TCHAR(Unit.cardId.c_str()));
            Row->SetNumberField(TEXT("deploymentTowerId"),Tower.id);Row->SetNumberField(TEXT("requestedX"),Tile.x);
            Row->SetNumberField(TEXT("requestedZ"),Tile.z);Row->SetNumberField(TEXT("spawnX"),Unit.position.x);
            Row->SetNumberField(TEXT("spawnZ"),Unit.position.z);Row->SetNumberField(TEXT("radius"),Unit.radius);
            Row->SetNumberField(TEXT("spawnTowerClearance"),Clearance);Row->SetNumberField(TEXT("minimumStepTowerClearance"),Clearance);
            Row->SetNumberField(TEXT("minimumSegmentTowerClearance"),Clearance);PreviousPositions.Add(Unit.id,Unit.position);
            SpawnClear&=Clearance>=-1.e-6;Rows.Add(MakeShared<FJsonValueObject>(Row));
        }
    }
    Match->FlushEvents();const double Started=Sim->State().elapsed;const int32 Steps=FMath::RoundToInt(double(Age)*60.);
    for(int32 Step=0;Step<Steps;++Step)
    {
        Sim->Step(1./60.);Match->FlushEvents();
        for(const auto& Value:Rows)
        {
            const auto Row=Value->AsObject();const uint64 Id=uint64(Row->GetNumberField(TEXT("id")));
            for(const auto& Unit:Sim->State().entities)if(Unit.id==Id&&!Unit.dead)
            {
                const double Clearance=CaptureTowerSegmentClearance(Sim->State(),Unit,Unit.position);
                const double Segment=CaptureTowerSegmentClearance(Sim->State(),Unit,PreviousPositions.FindChecked(Unit.id));
                Row->SetNumberField(TEXT("minimumStepTowerClearance"),FMath::Min(Clearance,Row->GetNumberField(TEXT("minimumStepTowerClearance"))));
                Row->SetNumberField(TEXT("minimumSegmentTowerClearance"),FMath::Min(Segment,Row->GetNumberField(TEXT("minimumSegmentTowerClearance"))));
                SegmentsClear&=Segment>=-1.e-6;PreviousPositions.FindChecked(Unit.id)=Unit.position;
                StepsClear&=Clearance>=-1.e-6;break;
            }
        }
        for(TActorIterator<ARiftArenaPresentation> It(Match->GetWorld());It;++It)It->Tick(0.f);
    }
    bool Progress=true;int32 Found=0;
    for(const auto& Value:Rows)
    {
        const auto Row=Value->AsObject();const uint64 Id=uint64(Row->GetNumberField(TEXT("id")));
        bool Present=false;
        for(const auto& Unit:Sim->State().entities)if(Unit.id==Id)
        {
            Present=true;++Found;const double X=Unit.position.x-Row->GetNumberField(TEXT("spawnX"));
            const double Z=Unit.position.z-Row->GetNumberField(TEXT("spawnZ"));const double Distance=FMath::Sqrt(X*X+Z*Z);
            const double Forward=-Z*(Unit.team==rift::Team::Player?1.:-1.);const bool Moved=Distance>.5&&Forward>.25;
            Row->SetNumberField(TEXT("x"),Unit.position.x);Row->SetNumberField(TEXT("z"),Unit.position.z);
            Row->SetBoolField(TEXT("alive"),!Unit.dead);Row->SetNumberField(TEXT("distanceFromSpawn"),Distance);
            Row->SetNumberField(TEXT("progressTowardRiver"),Forward);Row->SetBoolField(TEXT("progressPassed"),Moved);
            Progress&=Moved;break;
        }
        Row->SetBoolField(TEXT("present"),Present);if(!Present)Progress=false;
    }
    const bool RequireProgress=Steps>=120;
    Report->SetNumberField(TEXT("schemaVersion"),1);Report->SetStringField(TEXT("route"),TEXT("ordinary Match Spawn / fixed steps / live arena presentation"));
    Report->SetStringField(TEXT("routeCase"),TEXT("clearance"));Report->SetStringField(TEXT("routeTeam"),TEXT("both"));
    Report->SetNumberField(TEXT("requestedAge"),Age);Report->SetNumberField(TEXT("sampledAge"),Sim->State().elapsed-Started);
    Report->SetNumberField(TEXT("fixedSteps"),Steps);Report->SetNumberField(TEXT("deploymentCount"),Deployments);
    Report->SetNumberField(TEXT("expectedUnitCount"),8);Report->SetNumberField(TEXT("actualUnitCount"),Found);
    Report->SetNumberField(TEXT("towerNavigationPadding"),.22);Report->SetBoolField(TEXT("ordinarySpawnOnly"),true);
    Report->SetBoolField(TEXT("spawnTowerClearancePassed"),SpawnClear);Report->SetBoolField(TEXT("allStepTowerClearancePassed"),StepsClear);
    Report->SetBoolField(TEXT("allContinuousSegmentTowerClearancePassed"),SegmentsClear);
    Report->SetBoolField(TEXT("progressRequired"),RequireProgress);Report->SetBoolField(TEXT("progressPassed"),Progress);
    Report->SetArrayField(TEXT("towers"),TowerRows);Report->SetArrayField(TEXT("units"),Rows);
    const bool Passed=Deployed&&Deployments==6&&Rows.Num()==8&&Found==8&&SpawnClear&&StepsClear&&SegmentsClear&&(!RequireProgress||Progress);
    Report->SetBoolField(TEXT("passed"),Passed);
    RIFT_LOG(LogRift,Log,TEXT("Actual tower pathing capture: %d deployments, %d members, %d fixed steps, spawnClear=%d stepClear=%d segmentClear=%d progress=%d passed=%d"),Deployments,Found,Steps,SpawnClear,StepsClear,SegmentsClear,Progress,Passed);
    return Passed;
}

// Capture-only paid formation proof. The normal match and hand own every
// member; this records their actual landing side, movement and authored pose.
bool BuildSwarmSplitCapture(URiftMatchSubsystem* Match,const TSharedRef<FJsonObject>& Report)
{
    auto* Sim=Match->Simulation();if(!Sim)return false;Match->SetSpeed(0);
    FString CardId=TEXT("stampede");FParse::Value(FCommandLine::Get(),TEXT("RiftSwarmCard="),CardId);
    const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*CardId));if(!Card||Card->count<2||Card->spell||Card->building)return false;
    float Age=3,X=.5f,Z=8.5f;FParse::Value(FCommandLine::Get(),TEXT("RiftSwarmAge="),Age);
    FParse::Value(FCommandLine::Get(),TEXT("RiftSwarmX="),X);FParse::Value(FCommandLine::Get(),TEXT("RiftSwarmZ="),Z);
    if(!FMath::IsFinite(Age)||Age<0||Age>12||!FMath::IsFinite(X)||FMath::Abs(FMath::Abs(X)-.5f)>.00001f||!FMath::IsFinite(Z))return false;
    const bool GuardDown=FParse::Param(FCommandLine::Get(),TEXT("RiftSwarmLeftGuardDown"));
    uint64 LeftGuard=0,RightGuard=0,Core=0;for(const auto& Entity:Sim->State().entities)if(Entity.team==rift::Team::Enemy)
    {if(Entity.kind==rift::EntityKind::Core)Core=Entity.id;else if(Entity.kind==rift::EntityKind::Guard)(Entity.lane<0?LeftGuard:RightGuard)=Entity.id;}
    if(!LeftGuard||!RightGuard||!Core)return false;
    if(GuardDown){Match->SampleBeforeMutation();if(!Sim->SetTowerHP(LeftGuard,0))return false;Match->FlushEvents();}
    int32 Slot=INDEX_NONE;for(int32 I=0;I<4;++I)if(Sim->State().hands[0][I]==Card->id)Slot=I;if(Slot==INDEX_NONE)return false;
    const auto Hand=Sim->State().hands[0];const auto Queue=Sim->State().queues[0];const auto Drop=rift::SnapToTile({X,Z});
    Match->SampleBeforeMutation();Sim->SetAether(rift::Team::Player,10);Match->FlushEvents();
    const double Spent=Sim->State().spent[0];const size_t Before=Sim->State().entities.size();
    Match->SampleBeforeMutation();const bool Accepted=Sim->Play(rift::Team::Player,Slot,Drop,"capture_paid_swarm_split");
    bool Cycled=Accepted&&Sim->State().hands[0][Slot]==Queue.front()&&Sim->State().queues[0].back()==Card->id;
    for(int32 I=0;I<4;++I)if(I!=Slot)Cycled&=Sim->State().hands[0][I]==Hand[I];
    const double Debit=Sim->State().spent[0]-Spent,AetherAfter=Sim->State().aether[0];
    const bool Paid=Accepted&&Cycled&&FMath::IsNearlyEqual(Debit,double(Card->cost),1.e-9)&&FMath::IsNearlyEqual(AetherAfter,double(10-Card->cost),1.e-9);
    TArray<TSharedPtr<FJsonValue>> Rows,Samples;TMap<uint64,TSharedPtr<FJsonObject>> Index;
    int32 Left=0,Right=0;bool SpawnLegal=true;for(size_t I=Before;I<Sim->State().entities.size();++I)
    {
        const auto& Unit=Sim->State().entities[I];const int32 Lane=Unit.position.x<0?-1:1;if(Lane<0)++Left;else ++Right;
        SpawnLegal&=Sim->CanPlace(rift::Team::Player,*Card,Unit.position)&&Unit.lane==Lane&&Unit.memberCount==Card->count&&Unit.cardId==Card->id;
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("id"),Unit.id);Row->SetNumberField(TEXT("playId"),Unit.playId);
        Row->SetStringField(TEXT("cardId"),CardId);Row->SetNumberField(TEXT("spawnX"),Unit.position.x);Row->SetNumberField(TEXT("spawnZ"),Unit.position.z);
        Row->SetNumberField(TEXT("landingLane"),Lane);Row->SetNumberField(TEXT("radius"),Unit.radius);Row->SetBoolField(TEXT("flying"),Unit.flying);
        Row->SetBoolField(TEXT("alive"),!Unit.dead);Row->SetBoolField(TEXT("deathObserved"),false);Row->SetBoolField(TEXT("crossedFarBank"),false);
        Row->SetBoolField(TEXT("expectedCrownObserved"),false);Row->SetBoolField(TEXT("bridgeHistoryPassed"),true);Row->SetArrayField(TEXT("routeHistory"),{});
        Rows.Add(MakeShared<FJsonValueObject>(Row));Index.Add(Unit.id,Row);
    }
    Match->FlushEvents();for(TActorIterator<ARiftArenaPresentation> It(Match->GetWorld());It;++It)It->Tick(0.f);
    const double Tolerance=1.e-8;double Minimum=1.e9,MinimumSweep=1.e9;int64 Pairs=0,Sweeps=0;
    bool Clearance=true,SweptClearance=true,SpeedClearance=true,LaneTargets=true,BridgeCorrect=true;
    std::vector<rift::Entity> Previous;
    auto Observe=[&](int32 Step)
    {
        const auto& Entities=Sim->State().entities;auto Sample=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Bodies;
        TMap<uint64,ARiftUnitVisual*> Visuals;for(TActorIterator<ARiftUnitVisual> It(Match->GetWorld());It;++It)Visuals.Add(It->EntityId,*It);
        Sample->SetNumberField(TEXT("step"),Step);Sample->SetNumberField(TEXT("time"),Sim->State().elapsed);
        auto Prior=[&](uint64 Id)->const rift::Entity*{for(const auto& Entity:Previous)if(Entity.id==Id)return &Entity;return nullptr;};
        for(const auto& Unit:Entities)
        {
            auto Body=MakeShared<FJsonObject>();Body->SetNumberField(TEXT("id"),Unit.id);Body->SetNumberField(TEXT("kind"),int32(Unit.kind));
            Body->SetNumberField(TEXT("x"),Unit.position.x);Body->SetNumberField(TEXT("z"),Unit.position.z);Body->SetNumberField(TEXT("hp"),Unit.hp);
            Body->SetNumberField(TEXT("radius"),Unit.radius);Body->SetBoolField(TEXT("dead"),Unit.dead);Body->SetBoolField(TEXT("flying"),Unit.flying);
            Body->SetNumberField(TEXT("lane"),Unit.lane);Body->SetNumberField(TEXT("bridge"),Unit.bridge);Body->SetNumberField(TEXT("targetId"),Unit.target);Bodies.Add(MakeShared<FJsonValueObject>(Body));
            if(const auto* Entry=Index.Find(Unit.id))
            {
                const auto Row=*Entry;const int32 Lane=Row->GetIntegerField(TEXT("landingLane"));Row->SetNumberField(TEXT("x"),Unit.position.x);Row->SetNumberField(TEXT("z"),Unit.position.z);
                if(const auto* Visual=Visuals.Find(Unit.id))
                {Body->SetStringField(TEXT("animation"),(*Visual)->CurrentAnimation().ToString());Body->SetStringField(TEXT("animationAsset"),(*Visual)->AnimationAssetPath());
                 Body->SetNumberField(TEXT("animationPosition"),(*Visual)->AnimationPosition());Body->SetNumberField(TEXT("animationCycleFraction"),(*Visual)->AnimationCycleFraction());Body->SetNumberField(TEXT("locomotionPhase"),(*Visual)->LocomotionPhase());}
                Row->SetNumberField(TEXT("hp"),Unit.hp);Row->SetBoolField(TEXT("alive"),!Unit.dead);if(Unit.dead)Row->SetBoolField(TEXT("deathObserved"),true);
                if(!Unit.dead)
                {
                    const uint64 Guard=Lane<0?LeftGuard:RightGuard,Opposite=Lane<0?RightGuard:LeftGuard;bool GuardLiving=false;
                    for(const auto& Target:Entities)if(Target.id==Guard)GuardLiving=!Target.dead;
                    const uint64 Expected=GuardLiving?Guard:Core;Row->SetNumberField(TEXT("expectedTargetId"),Expected);Row->SetNumberField(TEXT("targetId"),Unit.target);Row->SetNumberField(TEXT("bridge"),Unit.bridge);
                    if(Step>0){LaneTargets&=Unit.target!=Opposite;if(Unit.target==Expected)Row->SetBoolField(TEXT("expectedCrownObserved"),true);}
                    bool Correct=true;if(!Unit.flying)
                    {Correct=(!Unit.bridge||Unit.bridge==Lane)&&(FMath::Abs(Unit.position.z)>=rift::arena::RiverHalfWidth+.28||Unit.position.x*Lane>0);BridgeCorrect&=Correct;}
                    Row->SetBoolField(TEXT("bridgeHistoryPassed"),Row->GetBoolField(TEXT("bridgeHistoryPassed"))&&Correct);
                    if(Unit.position.z<-(rift::arena::RiverHalfWidth+.28))Row->SetBoolField(TEXT("crossedFarBank"),true);
                    if(const auto* Start=Prior(Unit.id))SpeedClearance&=FMath::Sqrt(FMath::Square(Unit.position.x-Start->position.x)+FMath::Square(Unit.position.z-Start->position.z))<=Card->moveSpeed/60.+1.e-6;
                }
                if(Step==0||Step%15==0)
                {auto History=Row->GetArrayField(TEXT("routeHistory"));auto Point=MakeShared<FJsonObject>(*Body);Point->SetNumberField(TEXT("step"),Step);History.Add(MakeShared<FJsonValueObject>(Point));Row->SetArrayField(TEXT("routeHistory"),History);}
            }
        }
        for(size_t I=0;I<Entities.size();++I)for(size_t J=I+1;J<Entities.size();++J)
        {
            const auto& A=Entities[I];const auto& B=Entities[J];if(A.dead||B.dead)continue;
            const bool AirA=A.kind==rift::EntityKind::Troop&&A.flying,AirB=B.kind==rift::EntityKind::Troop&&B.flying;if(AirA!=AirB)continue;
            const double Padding=A.kind!=rift::EntityKind::Troop||B.kind!=rift::EntityKind::Troop?.22:.02;
            const double DX=A.position.x-B.position.x,DZ=A.position.z-B.position.z,Gap=FMath::Sqrt(DX*DX+DZ*DZ)-A.radius-B.radius-Padding;
            ++Pairs;Minimum=FMath::Min(Minimum,Gap);Clearance&=Gap>=-Tolerance;
            if(Step>0)if(const auto* PA=Prior(A.id))if(const auto* PB=Prior(B.id))
            {const double X0=PA->position.x-PB->position.x,Z0=PA->position.z-PB->position.z,VX=DX-X0,VZ=DZ-Z0,Length=VX*VX+VZ*VZ;
             const double Along=Length>0?FMath::Clamp(-(X0*VX+Z0*VZ)/Length,0.,1.):0.;const double GapSweep=FMath::Sqrt(FMath::Square(X0+Along*VX)+FMath::Square(Z0+Along*VZ))-A.radius-B.radius-Padding;
             ++Sweeps;MinimumSweep=FMath::Min(MinimumSweep,GapSweep);SweptClearance&=GapSweep>=-Tolerance;}
        }
        Sample->SetArrayField(TEXT("bodies"),Bodies);Samples.Add(MakeShared<FJsonValueObject>(Sample));Previous=Entities;
    };
    const double Started=Sim->State().elapsed;Observe(0);const int32 Steps=FMath::RoundToInt(double(Age)*60.);
    for(int32 Step=1;Step<=Steps;++Step)
    {Sim->Step(1./60.);Match->FlushEvents();for(TActorIterator<ARiftArenaPresentation> It(Match->GetWorld());It;++It)It->Tick(0.f);Observe(Step);}
    int32 Crossed=0,Moved=0;bool Accounted=true,TargetObserved=true;for(const auto& Value:Rows)
    {
        const auto Row=Value->AsObject();bool Present=false;for(const auto& Unit:Sim->State().entities)if(Unit.id==uint64(Row->GetNumberField(TEXT("id"))))Present=true;
        Row->SetBoolField(TEXT("present"),Present);Accounted&=Present||Row->GetBoolField(TEXT("deathObserved"));
        if(Row->GetBoolField(TEXT("crossedFarBank")))++Crossed;TargetObserved&=Row->GetBoolField(TEXT("expectedCrownObserved"));
        const double Distance=FMath::Sqrt(FMath::Square(Row->GetNumberField(TEXT("x"))-Row->GetNumberField(TEXT("spawnX")))+FMath::Square(Row->GetNumberField(TEXT("z"))-Row->GetNumberField(TEXT("spawnZ"))));
        Row->SetNumberField(TEXT("distanceFromSpawn"),Distance);if(Distance>.1)++Moved;
    }
    const bool RequireProgress=Steps>=120,RequireCross=Steps>=720;
    const bool Passed=Paid&&Rows.Num()==Card->count&&SpawnLegal&&Left>0&&Right>0&&FMath::Abs(Left-Right)<=1&&Accounted&&Clearance&&SweptClearance&&SpeedClearance&&LaneTargets&&BridgeCorrect&&(!Steps||TargetObserved)&&(!RequireProgress||Moved==Card->count)&&(!RequireCross||Crossed==Card->count);
    Report->SetNumberField(TEXT("schemaVersion"),1);Report->SetStringField(TEXT("route"),TEXT("normal preset / StartMatch / paid Play / fixed steps / actual authored visuals"));
    Report->SetStringField(TEXT("cardId"),CardId);Report->SetStringField(TEXT("team"),TEXT("player"));Report->SetBoolField(TEXT("paidPlayOnly"),true);
    Report->SetBoolField(TEXT("paidCostAndCyclePassed"),Paid);Report->SetNumberField(TEXT("cost"),Card->cost);Report->SetNumberField(TEXT("aetherBefore"),10);Report->SetNumberField(TEXT("aetherAfter"),AetherAfter);Report->SetNumberField(TEXT("spentDelta"),Debit);
    Report->SetNumberField(TEXT("handIndex"),Slot);Report->SetBoolField(TEXT("handCycledOnce"),Cycled);Report->SetNumberField(TEXT("requestedX"),Drop.x);Report->SetNumberField(TEXT("requestedZ"),Drop.z);
    TArray<TSharedPtr<FJsonValue>> BeforeCards,AfterCards;for(int32 I=0;I<4;++I)
    {BeforeCards.Add(MakeShared<FJsonValueString>(UTF8_TO_TCHAR(Hand[I].c_str())));AfterCards.Add(MakeShared<FJsonValueString>(UTF8_TO_TCHAR(Sim->State().hands[0][I].c_str())));}
    Report->SetArrayField(TEXT("handBefore"),BeforeCards);Report->SetArrayField(TEXT("handAfter"),AfterCards);
    Report->SetNumberField(TEXT("expectedUnitCount"),Card->count);Report->SetNumberField(TEXT("actualUnitCount"),Rows.Num());Report->SetNumberField(TEXT("leftMembers"),Left);Report->SetNumberField(TEXT("rightMembers"),Right);
    Report->SetNumberField(TEXT("requestedAge"),Age);Report->SetNumberField(TEXT("sampledAge"),Sim->State().elapsed-Started);Report->SetNumberField(TEXT("fixedSteps"),Steps);Report->SetBoolField(TEXT("leftGuardDestroyed"),GuardDown);
    Report->SetNumberField(TEXT("leftGuardId"),LeftGuard);Report->SetNumberField(TEXT("rightGuardId"),RightGuard);Report->SetNumberField(TEXT("coreId"),Core);
    Report->SetBoolField(TEXT("spawnLegalPassed"),SpawnLegal);Report->SetBoolField(TEXT("allMembersAccounted"),Accounted);Report->SetBoolField(TEXT("allClearancePassed"),Clearance);Report->SetBoolField(TEXT("allSweptClearancePassed"),SweptClearance);
    Report->SetBoolField(TEXT("allSpeedLimitsPassed"),SpeedClearance);Report->SetBoolField(TEXT("allLaneTargetsPassed"),LaneTargets);Report->SetBoolField(TEXT("allBridgeHistoriesPassed"),BridgeCorrect);Report->SetBoolField(TEXT("allExpectedCrownTargetsObserved"),TargetObserved);
    Report->SetNumberField(TEXT("collisionSkin"),.02);Report->SetNumberField(TEXT("structurePadding"),.22);Report->SetNumberField(TEXT("tolerance"),Tolerance);Report->SetNumberField(TEXT("minimumGap"),Minimum);Report->SetNumberField(TEXT("minimumSweptGap"),Steps?MinimumSweep:Minimum);
    Report->SetNumberField(TEXT("endpointPairChecks"),Pairs);Report->SetNumberField(TEXT("sweptPairChecks"),Sweeps);Report->SetBoolField(TEXT("progressRequired"),RequireProgress);Report->SetNumberField(TEXT("membersProgressed"),Moved);
    Report->SetBoolField(TEXT("crossingRequired"),RequireCross);Report->SetNumberField(TEXT("farBankMembers"),Crossed);Report->SetNumberField(TEXT("farBankDepth"),rift::arena::RiverHalfWidth+.28);
    Report->SetArrayField(TEXT("units"),Rows);Report->SetArrayField(TEXT("samples"),Samples);Report->SetBoolField(TEXT("passed"),Passed);return Passed;
}

// Acceptance fixture only. Every body comes from ordinary paid hand plays;
// positions, targets, cooldowns and navigation are never edited for the image.
bool BuildUnitCollisionCapture(URiftMatchSubsystem* Match,const TSharedRef<FJsonObject>& Report)
{
    auto* Sim=Match->Simulation();if(!Sim)return false;Match->SetSpeed(0);
    FString Case=TEXT("crowd");FParse::Value(FCommandLine::Get(),TEXT("RiftCollisionCase="),Case);
    float Age=3;FParse::Value(FCommandLine::Get(),TEXT("RiftCollisionAge="),Age);
    Age=FMath::IsFinite(Age)?FMath::Clamp(Age,0.f,12.f):3.f;
    const double Skin=.02,Tolerance=1.e-6;
    TArray<TSharedPtr<FJsonValue>> Plays,Rows,Samples,PairRows;
    TMap<uint64,TSharedPtr<FJsonObject>> UnitRows;
    TSet<uint64> CaptureSeenIds;
    TMap<uint64,rift::Vec2> Previous;
    TMap<FString,TSharedPtr<FJsonObject>> PairIndex;
    bool Paid=true,SpawnClear=true,StepClear=true,SegmentClear=true,BridgeCorrect=true;
    int32 Expected=0,GroundPairs=0,AirPairs=0,CrossLayerOverlaps=0;double Minimum=1.e9,MinimumSegment=1.e9;
    auto SameLayer=[](const rift::Entity& A,const rift::Entity& B)
    {const bool AirA=A.kind==rift::EntityKind::Troop&&A.flying,AirB=B.kind==rift::EntityKind::Troop&&B.flying;return AirA==AirB;};
    auto RecordSample=[&](int32 Step)
    {
        auto Sample=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Bodies;
        Sample->SetNumberField(TEXT("step"),Step);Sample->SetNumberField(TEXT("time"),Sim->State().elapsed);
        const auto& Entities=Sim->State().entities;
        for(const auto& Unit:Entities)
        {
            if(Unit.dead){if(const auto* Value=UnitRows.Find(Unit.id)){(*Value)->SetBoolField(TEXT("alive"),false);(*Value)->SetBoolField(TEXT("deathObserved"),true);}continue;}
            auto Body=MakeShared<FJsonObject>();Body->SetNumberField(TEXT("id"),Unit.id);Body->SetNumberField(TEXT("x"),Unit.position.x);Body->SetNumberField(TEXT("z"),Unit.position.z);
            Body->SetNumberField(TEXT("radius"),Unit.radius);Body->SetBoolField(TEXT("flying"),Unit.flying);Body->SetNumberField(TEXT("kind"),int32(Unit.kind));Body->SetStringField(TEXT("team"),UTF8_TO_TCHAR(rift::TeamName(Unit.team).c_str()));Bodies.Add(MakeShared<FJsonValueObject>(Body));
            if(const auto* Value=UnitRows.Find(Unit.id))
            {
                CaptureSeenIds.Add(Unit.id);
                const auto Row=*Value;Row->SetNumberField(TEXT("x"),Unit.position.x);Row->SetNumberField(TEXT("z"),Unit.position.z);Row->SetBoolField(TEXT("alive"),true);Row->SetNumberField(TEXT("hp"),Unit.hp);
                Row->SetNumberField(TEXT("targetId"),Unit.target);Row->SetNumberField(TEXT("bridge"),Unit.bridge);
                const rift::Entity* Target=nullptr;for(const auto& Candidate:Entities)if(Candidate.id==Unit.target){Target=&Candidate;break;}
                Row->SetStringField(TEXT("targetKind"),!Target?TEXT("none"):Target->kind==rift::EntityKind::Core?TEXT("core"):Target->kind==rift::EntityKind::Guard?TEXT("guard"):Target->kind==rift::EntityKind::Building?TEXT("building"):TEXT("troop"));
                if(!Unit.flying&&Unit.kind==rift::EntityKind::Troop)
                {
                    const int32 Lane=Row->GetIntegerField(TEXT("intendedBridge"));const bool Banking=FMath::Abs(Unit.position.z)<rift::arena::RiverHalfWidth+.28;
                    const bool Correct=(!Unit.bridge||Unit.bridge==Lane)&&(!Banking||(Unit.position.x*Lane>0&&FMath::Abs(Unit.position.x-Lane*rift::arena::BridgeCenterX)<=rift::arena::BridgeWidth*.5-.16-Unit.radius*.92+Tolerance));
                    Row->SetBoolField(TEXT("bridgeHistoryPassed"),Row->GetBoolField(TEXT("bridgeHistoryPassed"))&&Correct);BridgeCorrect&=Correct;
                    if(Unit.position.z*(Unit.team==rift::Team::Player?1.:-1.)<=0)Row->SetBoolField(TEXT("crossedRiver"),true);
                    if(!Row->GetBoolField(TEXT("crossedFarBank"))&&Unit.position.z*(Unit.team==rift::Team::Player?1.:-1.)<-(rift::arena::RiverHalfWidth+.28))
                    {
                        Row->SetBoolField(TEXT("crossedFarBank"),true);auto FarHistory=Row->GetArrayField(TEXT("farBankCrossingHistory"));auto Point=MakeShared<FJsonObject>();
                        Point->SetNumberField(TEXT("step"),Step);Point->SetNumberField(TEXT("time"),Sim->State().elapsed);Point->SetNumberField(TEXT("x"),Unit.position.x);Point->SetNumberField(TEXT("z"),Unit.position.z);Point->SetNumberField(TEXT("bridge"),Lane);Point->SetNumberField(TEXT("liveBridge"),Unit.bridge);
                        FarHistory.Add(MakeShared<FJsonValueObject>(Point));Row->SetArrayField(TEXT("farBankCrossingHistory"),FarHistory);
                    }
                    auto History=Row->GetArrayField(TEXT("routeHistory"));
                    if(Step==0||Step%15==0){auto Point=MakeShared<FJsonObject>();Point->SetNumberField(TEXT("step"),Step);Point->SetNumberField(TEXT("x"),Unit.position.x);Point->SetNumberField(TEXT("z"),Unit.position.z);Point->SetNumberField(TEXT("bridge"),Unit.bridge);Point->SetNumberField(TEXT("targetId"),Unit.target);History.Add(MakeShared<FJsonValueObject>(Point));Row->SetArrayField(TEXT("routeHistory"),History);}
                }
            }
        }
        for(size_t I=0;I<Entities.size();++I)for(size_t J=I+1;J<Entities.size();++J)
        {
            const auto& A=Entities[I];const auto& B=Entities[J];if(A.dead||B.dead)continue;
            const double Padding=(!A.flying&&!B.flying&&(A.kind!=rift::EntityKind::Troop||B.kind!=rift::EntityKind::Troop))?.22:Skin;
            const double DX=A.position.x-B.position.x,DZ=A.position.z-B.position.z,Gap=FMath::Sqrt(DX*DX+DZ*DZ)-A.radius-B.radius-Padding;
            if(!SameLayer(A,B)){if(Gap<0)++CrossLayerOverlaps;continue;}
            const FString Key=FString::Printf(TEXT("%llu:%llu"),A.id,B.id);TSharedPtr<FJsonObject> Pair;
            if(const auto* Existing=PairIndex.Find(Key))Pair=*Existing;
            else
            {
                Pair=MakeShared<FJsonObject>();Pair->SetNumberField(TEXT("a"),A.id);Pair->SetNumberField(TEXT("b"),B.id);Pair->SetStringField(TEXT("layer"),A.flying&&A.kind==rift::EntityKind::Troop?TEXT("air"):TEXT("ground"));
                Pair->SetBoolField(TEXT("opponents"),A.team!=B.team);Pair->SetBoolField(TEXT("stationaryBody"),A.kind!=rift::EntityKind::Troop||B.kind!=rift::EntityKind::Troop);Pair->SetNumberField(TEXT("padding"),Padding);
                Pair->SetNumberField(TEXT("minimumGap"),Gap);Pair->SetNumberField(TEXT("minimumSegmentGap"),Gap);Pair->SetNumberField(TEXT("observations"),0);PairIndex.Add(Key,Pair);PairRows.Add(MakeShared<FJsonValueObject>(Pair));
                if(A.flying&&A.kind==rift::EntityKind::Troop)++AirPairs;else ++GroundPairs;
            }
            Pair->SetNumberField(TEXT("currentGap"),Gap);Pair->SetNumberField(TEXT("minimumGap"),FMath::Min(Gap,Pair->GetNumberField(TEXT("minimumGap"))));Pair->SetNumberField(TEXT("observations"),Pair->GetNumberField(TEXT("observations"))+1);
            Minimum=FMath::Min(Minimum,Gap);StepClear&=Gap>=-Tolerance;if(Step==0)SpawnClear&=Gap>=-Tolerance;
            if(Step>0&&Previous.Contains(A.id)&&Previous.Contains(B.id))
            {
                const auto PA=Previous.FindChecked(A.id),PB=Previous.FindChecked(B.id);const double X=PA.x-PB.x,Z=PA.z-PB.z,VX=DX-X,VZ=DZ-Z,Length=VX*VX+VZ*VZ;
                const double T=Length>0.?FMath::Clamp(-(X*VX+Z*VZ)/Length,0.,1.):0.;const double SX=X+T*VX,SZ=Z+T*VZ;
                const double Segment=FMath::Sqrt(SX*SX+SZ*SZ)-A.radius-B.radius-Padding;MinimumSegment=FMath::Min(MinimumSegment,Segment);SegmentClear&=Segment>=-Tolerance;
                Pair->SetNumberField(TEXT("minimumSegmentGap"),FMath::Min(Segment,Pair->GetNumberField(TEXT("minimumSegmentGap"))));
            }
        }
        Sample->SetArrayField(TEXT("bodies"),Bodies);Samples.Add(MakeShared<FJsonValueObject>(Sample));Previous.Empty();for(const auto& Unit:Entities)if(!Unit.dead)Previous.Add(Unit.id,Unit.position);
    };
    auto PaidDrop=[&](rift::Team Team,int32 Sequence,rift::Vec2 Drop,const FString& Role,const char* Preferred)
    {
        const int32 TeamIndex=int32(Team);int32 Slot=INDEX_NONE;
        if(Preferred)for(int32 I=0;I<4;++I)if(Sim->State().hands[TeamIndex][I]==Preferred){Slot=I;break;}
        if(Slot==INDEX_NONE&&Case==TEXT("contact"))for(int32 I=0;I<4;++I)
        {const auto* Card=rift::FindCard(Sim->State().hands[TeamIndex][I]);if(Card&&!Card->spell&&!Card->flying&&!Card->building&&Card->structuresOnly){Slot=I;break;}}
        if(Slot==INDEX_NONE)for(int32 I=0;I<4;++I)
        {const int32 Candidate=(Sequence+I)%4;const auto* Card=rift::FindCard(Sim->State().hands[TeamIndex][Candidate]);if(Card&&!Card->spell&&((Team==rift::Team::Player&&Case!=TEXT("contact"))||(!Card->flying&&!Card->building&&Card->range<2))){Slot=Candidate;break;}}
        if(Slot==INDEX_NONE)for(int32 I=0;I<4;++I)
        {const auto* Card=rift::FindCard(Sim->State().hands[TeamIndex][I]);if(Card&&!Card->spell&&!Card->flying&&!Card->building){Slot=I;break;}}
        // A legitimate shuffled control hand may begin with air/buildings and
        // spells. Cycle one physical card normally to expose its next ground
        // card, rather than depending on a lucky hand or altering its queue.
        if(Slot==INDEX_NONE)for(int32 I=0;I<4;++I)
        {const auto* Card=rift::FindCard(Sim->State().hands[TeamIndex][I]);if(Card&&!Card->spell){Slot=I;break;}}
        if(Slot==INDEX_NONE){Paid=false;return;}
        const auto* Card=rift::FindCard(Sim->State().hands[TeamIndex][Slot]);const std::string CardId=Card->id;const auto OldHand=Sim->State().hands[TeamIndex];const auto Queue=Sim->State().queues[TeamIndex];
        if(Card->building&&!Sim->CanPlace(Team,*Card,Drop))
        {
            // Keep the original building footprint placement rule. Nearby
            // legal tiles, rather than rejected stack placements, build the
            // stationary obstacle course through normal paid deployment.
            bool Legal=false;for(int32 Ring=1;Ring<=8&&!Legal;++Ring)for(int32 Direction=0;Direction<8&&!Legal;++Direction)
            {const double Angle=Direction*PI*.25;const rift::Vec2 Candidate=rift::SnapToTile({Drop.x+Ring*FMath::Cos(Angle),Drop.z+Ring*FMath::Sin(Angle)});if(Sim->CanPlace(Team,*Card,Candidate)){Drop=Candidate;Legal=true;}}
        }
        Match->SampleBeforeMutation();Sim->SetAether(Team,10);Match->FlushEvents();const double Spent=Sim->State().spent[TeamIndex];const size_t Before=Sim->State().entities.size();
        Match->SampleBeforeMutation();const bool Accepted=Sim->Play(Team,Slot,Drop,"capture_paid_collision");Match->FlushEvents();bool Others=true;for(int32 I=0;I<4;++I)if(I!=Slot)Others&=Sim->State().hands[TeamIndex][I]==OldHand[I];
        const bool Cycled=Accepted&&Others&&Sim->State().hands[TeamIndex][Slot]==Queue.front()&&Sim->State().queues[TeamIndex].back()==CardId;
        auto Play=MakeShared<FJsonObject>();Play->SetStringField(TEXT("team"),UTF8_TO_TCHAR(rift::TeamName(Team).c_str()));Play->SetStringField(TEXT("cardId"),UTF8_TO_TCHAR(CardId.c_str()));Play->SetStringField(TEXT("role"),Role);
        Play->SetNumberField(TEXT("cost"),Card->cost);Play->SetNumberField(TEXT("memberCount"),Card->count);Play->SetNumberField(TEXT("handIndex"),Slot);Play->SetBoolField(TEXT("accepted"),Accepted);Play->SetBoolField(TEXT("handCycledOnce"),Cycled);
        Play->SetNumberField(TEXT("aetherBefore"),10);Play->SetNumberField(TEXT("aetherAfter"),Sim->State().aether[TeamIndex]);Play->SetNumberField(TEXT("spentDelta"),Sim->State().spent[TeamIndex]-Spent);Play->SetNumberField(TEXT("requestedX"),Drop.x);Play->SetNumberField(TEXT("requestedZ"),Drop.z);Plays.Add(MakeShared<FJsonValueObject>(Play));
        Paid&=Accepted&&Cycled&&FMath::IsNearlyEqual(Sim->State().spent[TeamIndex]-Spent,double(Card->cost),1.e-9)&&FMath::IsNearlyEqual(Sim->State().aether[TeamIndex],double(10-Card->cost),1.e-9);
        if(Accepted)Expected+=Card->count;
        for(size_t I=Before;I<Sim->State().entities.size();++I)
        {
            const auto& Unit=Sim->State().entities[I];auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("id"),Unit.id);Row->SetStringField(TEXT("cardId"),UTF8_TO_TCHAR(Unit.cardId.c_str()));Row->SetStringField(TEXT("team"),UTF8_TO_TCHAR(rift::TeamName(Unit.team).c_str()));
            Row->SetStringField(TEXT("role"),Role);Row->SetNumberField(TEXT("kind"),int32(Unit.kind));Row->SetBoolField(TEXT("flying"),Unit.flying);Row->SetNumberField(TEXT("radius"),Unit.radius);Row->SetNumberField(TEXT("spawnX"),Unit.position.x);Row->SetNumberField(TEXT("spawnZ"),Unit.position.z);Row->SetNumberField(TEXT("x"),Unit.position.x);Row->SetNumberField(TEXT("z"),Unit.position.z);
            Row->SetNumberField(TEXT("intendedBridge"),Drop.x<0?-1:1);Row->SetBoolField(TEXT("bridgeHistoryPassed"),true);Row->SetBoolField(TEXT("crossedRiver"),false);Row->SetBoolField(TEXT("crossedFarBank"),false);Row->SetBoolField(TEXT("present"),true);Row->SetBoolField(TEXT("alive"),!Unit.dead);Row->SetBoolField(TEXT("deathObserved"),false);Row->SetArrayField(TEXT("routeHistory"),{});Row->SetArrayField(TEXT("farBankCrossingHistory"),{});Rows.Add(MakeShared<FJsonValueObject>(Row));UnitRows.Add(Unit.id,Row);
        }
    };
    if(Case==TEXT("contact"))for(int32 I=0;I<4;++I)
    {PaidDrop(rift::Team::Player,I,{7.5,3.5},TEXT("player_contact"),"boulderback");PaidDrop(rift::Team::Enemy,I,{7.5,-3.5},TEXT("enemy_contact"),"boulderback");}
    else if(Case==TEXT("layers"))for(int32 I=0;I<12;++I)
    {const char* Preferred=I%3==0?"archer_tower":I%3==1?"vampire_bats":"sky_manta";PaidDrop(rift::Team::Player,I,{-7.5,7.5},TEXT("layered_crowd"),Preferred);}
    else for(int32 I=0;I<16;++I)PaidDrop(rift::Team::Player,I,{7.5,8.5},TEXT("friendly_crowd"),I%3==0?"twin_blades":I%3==1?"boulderback":nullptr);
    const double Started=Sim->State().elapsed;RecordSample(0);const int32 Steps=FMath::RoundToInt(double(Age)*60.);
    for(int32 Step=1;Step<=Steps;++Step)
    {Sim->Step(1./60.);Match->FlushEvents();RecordSample(Step);for(TActorIterator<ARiftArenaPresentation> It(Match->GetWorld());It;++It)It->Tick(0.f);}
    int32 Found=0,Moved=0,Air=0,Ground=0,Buildings=0,Deaths=0,Crossed=0,FarCrossed=0;bool Stationary=true,MembersAccounted=true;
    for(const auto& Value:Rows)
    {
        const auto Row=Value->AsObject();const uint64 Id=uint64(Row->GetNumberField(TEXT("id")));const rift::Entity* Unit=nullptr;for(const auto& Candidate:Sim->State().entities)if(Candidate.id==Id){Unit=&Candidate;break;}
        Row->SetBoolField(TEXT("present"),Unit!=nullptr);Row->SetBoolField(TEXT("alive"),Unit&&!Unit->dead);if(Unit)++Found;if(Row->GetBoolField(TEXT("deathObserved")))++Deaths;MembersAccounted&=Unit||Row->GetBoolField(TEXT("deathObserved"));
        const double DX=Row->GetNumberField(TEXT("x"))-Row->GetNumberField(TEXT("spawnX")),DZ=Row->GetNumberField(TEXT("z"))-Row->GetNumberField(TEXT("spawnZ")),Distance=FMath::Sqrt(DX*DX+DZ*DZ);
        Row->SetNumberField(TEXT("distanceFromSpawn"),Distance);Row->SetBoolField(TEXT("progressPassed"),Distance>.1);if(Distance>.1)++Moved;
        if(Row->GetIntegerField(TEXT("kind"))==int32(rift::EntityKind::Building)){++Buildings;Stationary&=Distance<Tolerance;}else if(Row->GetBoolField(TEXT("flying")))++Air;else{++Ground;if(Row->GetBoolField(TEXT("crossedRiver")))++Crossed;if(Row->GetBoolField(TEXT("crossedFarBank")))++FarCrossed;}
    }
    bool EnemyContact=false;for(const auto& Value:PairRows){const auto Pair=Value->AsObject();EnemyContact|=Pair->GetBoolField(TEXT("opponents"))&&!Pair->GetBoolField(TEXT("stationaryBody"))&&Pair->GetNumberField(TEXT("minimumGap"))<.1;}
    const bool RequireProgress=Steps>=120,RequireContact=Case==TEXT("contact")&&Steps>=180,LayerCoverage=Case!=TEXT("layers")||(Air>=2&&Ground>=1&&Buildings>=1&&AirPairs>0&&CrossLayerOverlaps>0);
    const bool RequireCross=Case==TEXT("crowd")&&Steps>=720;
    const bool AllGroundCrossed=Ground>0&&Crossed==Ground,AllGroundFarCrossed=Ground>0&&FarCrossed==Ground,AllMembersSeen=CaptureSeenIds.Num()==Rows.Num();
    const bool Passed=Paid&&Rows.Num()==Expected&&MembersAccounted&&AllMembersSeen&&Expected>0&&SpawnClear&&StepClear&&SegmentClear&&BridgeCorrect&&Stationary&&LayerCoverage&&(!RequireProgress||Moved>0)&&(!RequireContact||EnemyContact)&&(!RequireCross||(Ground==11&&AllGroundFarCrossed));
    Report->SetNumberField(TEXT("schemaVersion"),1);Report->SetStringField(TEXT("route"),TEXT("paid Match Play / fixed steps / live arena presentation"));Report->SetStringField(TEXT("case"),Case);Report->SetBoolField(TEXT("paidPlayOnly"),true);Report->SetBoolField(TEXT("paidCostAndCyclePassed"),Paid);
    Report->SetNumberField(TEXT("requestedAge"),Age);Report->SetNumberField(TEXT("sampledAge"),Sim->State().elapsed-Started);Report->SetNumberField(TEXT("fixedSteps"),Steps);Report->SetNumberField(TEXT("collisionSkin"),Skin);Report->SetNumberField(TEXT("structurePadding"),.22);Report->SetNumberField(TEXT("tolerance"),Tolerance);Report->SetNumberField(TEXT("deploymentCount"),Plays.Num());Report->SetNumberField(TEXT("expectedUnitCount"),Expected);Report->SetNumberField(TEXT("actualUnitCount"),Rows.Num());Report->SetNumberField(TEXT("presentUnitCount"),Found);Report->SetNumberField(TEXT("observedDeaths"),Deaths);Report->SetBoolField(TEXT("allMembersAccounted"),MembersAccounted);
    Report->SetNumberField(TEXT("groundMembers"),Ground);Report->SetNumberField(TEXT("airMembers"),Air);Report->SetNumberField(TEXT("buildingMembers"),Buildings);Report->SetNumberField(TEXT("groundPairs"),GroundPairs);Report->SetNumberField(TEXT("airPairs"),AirPairs);Report->SetNumberField(TEXT("crossLayerOverlapObservations"),CrossLayerOverlaps);Report->SetNumberField(TEXT("minimumGap"),Minimum);Report->SetNumberField(TEXT("minimumSegmentGap"),Steps>0?MinimumSegment:Minimum);
    Report->SetBoolField(TEXT("spawnClearancePassed"),SpawnClear);Report->SetBoolField(TEXT("allFixedStepClearancePassed"),StepClear);Report->SetBoolField(TEXT("allRelativeMovementSegmentClearancePassed"),SegmentClear);Report->SetBoolField(TEXT("allBridgeHistoriesPassed"),BridgeCorrect);Report->SetBoolField(TEXT("stationaryBuildingsPassed"),Stationary);Report->SetBoolField(TEXT("layerCoveragePassed"),LayerCoverage);Report->SetBoolField(TEXT("progressRequired"),RequireProgress);Report->SetBoolField(TEXT("progressPassed"),Moved>0);Report->SetNumberField(TEXT("membersProgressed"),Moved);Report->SetBoolField(TEXT("enemyContactRequired"),RequireContact);Report->SetBoolField(TEXT("enemyContactObserved"),EnemyContact);
    TArray<TSharedPtr<FJsonValue>> SeenIds;for(const auto& Value:Rows){const uint64 Id=uint64(Value->AsObject()->GetNumberField(TEXT("id")));if(CaptureSeenIds.Contains(Id))SeenIds.Add(MakeShared<FJsonValueNumber>(double(Id)));}
    Report->SetNumberField(TEXT("initialMemberCount"),Rows.Num());Report->SetNumberField(TEXT("initialGroundMemberCount"),Ground);Report->SetNumberField(TEXT("captureSeenMemberCount"),CaptureSeenIds.Num());Report->SetArrayField(TEXT("captureSeenIds"),SeenIds);Report->SetBoolField(TEXT("allMembersSeen"),AllMembersSeen);
    Report->SetBoolField(TEXT("bridgeCrossingRequired"),RequireCross);Report->SetNumberField(TEXT("groundMembersCrossed"),Crossed);Report->SetBoolField(TEXT("allGroundMembersCrossed"),AllGroundCrossed);Report->SetNumberField(TEXT("groundMembersFarBankCrossed"),FarCrossed);Report->SetBoolField(TEXT("allGroundMembersFarBankCrossed"),AllGroundFarCrossed);Report->SetNumberField(TEXT("farBankDepth"),rift::arena::RiverHalfWidth+.28);
    Report->SetArrayField(TEXT("units"),Rows);Report->SetArrayField(TEXT("paidPlays"),Plays);Report->SetArrayField(TEXT("pairs"),PairRows);Report->SetArrayField(TEXT("samples"),Samples);Report->SetBoolField(TEXT("passed"),Passed);
    RIFT_LOG(LogRift,Log,TEXT("Actual unit collision capture: case=%s paid=%d members=%d steps=%d spawn=%d clearance=%d segments=%d bridge=%d progress=%d layers=%d contact=%d passed=%d"),*Case,Plays.Num(),Found,Steps,SpawnClear,StepClear,SegmentClear,BridgeCorrect,Moved>0,LayerCoverage,EnemyContact,Passed);
    return Passed;
}
}

class FRiftCaptureInputFilter final : public IInputProcessor
{
public:
    explicit FRiftCaptureInputFilter(bool Consume):bConsumeInput(Consume){}
    void SetConsumption(bool Consume){bConsumeInput=Consume;}
    void Tick(float,FSlateApplication&,TSharedRef<ICursor>)override{}
    const TCHAR* GetDebugName()const override{return TEXT("Rift automated capture input");}
    bool HandleKeyDownEvent(FSlateApplication&,const FKeyEvent& Event)override
    {Record(TEXT("keyDown"),Event.GetKey(),Event.IsRepeat());return bConsumeInput;}
    bool HandleKeyUpEvent(FSlateApplication&,const FKeyEvent& Event)override
    {Record(TEXT("keyUp"),Event.GetKey(),false);return bConsumeInput;}
    bool HandleAnalogInputEvent(FSlateApplication&,const FAnalogInputEvent& Event)override
    {if(FMath::Abs(Event.GetAnalogValue())>.01f){++Counts.FindOrAdd(TEXT("analog"));++Keys.FindOrAdd(Event.GetKey().ToString());}return bConsumeInput;}
    bool HandleMouseMoveEvent(FSlateApplication&,const FPointerEvent&)override
    {++Counts.FindOrAdd(TEXT("mouseMove"));return bConsumeInput;}
    bool HandleMouseButtonDownEvent(FSlateApplication&,const FPointerEvent& Event)override
    {Record(TEXT("mouseDown"),Event.GetEffectingButton(),false);return bConsumeInput;}
    bool HandleMouseButtonUpEvent(FSlateApplication&,const FPointerEvent& Event)override
    {Record(TEXT("mouseUp"),Event.GetEffectingButton(),false);return bConsumeInput;}
    bool HandleMouseButtonDoubleClickEvent(FSlateApplication&,const FPointerEvent& Event)override
    {Record(TEXT("mouseDoubleClick"),Event.GetEffectingButton(),false);return bConsumeInput;}
    bool HandleMouseWheelOrGestureEvent(FSlateApplication&,const FPointerEvent&,const FPointerEvent*)override
    {++Counts.FindOrAdd(TEXT("wheelOrGesture"));return bConsumeInput;}
    bool HandleMotionDetectedEvent(FSlateApplication&,const FMotionEvent&)override
    {++Counts.FindOrAdd(TEXT("motion"));return bConsumeInput;}
    TSharedRef<FJsonObject> Report()const
    {
        auto Result=MakeShared<FJsonObject>();auto Events=MakeShared<FJsonObject>();auto ObservedKeys=MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("active"),true);Result->SetBoolField(TEXT("consuming"),bConsumeInput);
        for(const auto& Pair:Counts)Events->SetNumberField(Pair.Key,Pair.Value);
        for(const auto& Pair:Keys)ObservedKeys->SetNumberField(Pair.Key,Pair.Value);
        Result->SetObjectField(TEXT("events"),Events);Result->SetObjectField(TEXT("keys"),ObservedKeys);return Result;
    }
private:
    void Record(const TCHAR* Type,const FKey& Key,bool Repeat)
    {
        ++Counts.FindOrAdd(Type);++Keys.FindOrAdd(Key.ToString());if(Repeat)++Counts.FindOrAdd(TEXT("keyRepeat"));
        RIFT_LOG(LogRift,Log,TEXT("QA input %s: %s repeat=%d consumed=%d"),Type,*Key.ToString(),Repeat,bConsumeInput);
    }
    bool bConsumeInput;
    TMap<FString,int32> Counts,Keys;
};

TSharedRef<IInputProcessor> ARiftGameMode::MakeCaptureInputFilter(bool ConsumeInput)
{return MakeShared<FRiftCaptureInputFilter>(ConsumeInput);}

ARiftGameMode::ARiftGameMode()
{PlayerControllerClass=ARiftPlayerController::StaticClass();DefaultPawnClass=nullptr;HUDClass=nullptr;}
void ARiftGameMode::BeginPlay()
{
    Super::BeginPlay();
    FString AutomatedPath;
    const bool Capture=FParse::Value(FCommandLine::Get(),TEXT("RiftCapture="),AutomatedPath);
    if(FSlateApplication::IsInitialized()&&(Capture||FParse::Value(FCommandLine::Get(),TEXT("RiftAudioSmoke="),AutomatedPath)||FParse::Value(FCommandLine::Get(),TEXT("RiftPerfReport="),AutomatedPath)||FParse::Value(FCommandLine::Get(),TEXT("RiftDragSmoke="),AutomatedPath)))
    {
        // RenderOffScreen uses the Null application on Windows, which still
        // polls external gamepads. Consume their events without disabling or
        // dimming the real UI; explicit fixture callbacks remain available.
        CaptureInputFilter=MakeShared<FRiftCaptureInputFilter>(!Capture||!FParse::Param(FCommandLine::Get(),TEXT("RiftCaptureAllowInput")));
        FSlateApplication::Get().RegisterInputPreProcessor(CaptureInputFilter,0);
    }
    GetWorld()->SpawnActor<ARiftArenaPresentation>();
    FString DragSmokePath;
    if(FParse::Value(FCommandLine::Get(),TEXT("RiftDragSmoke="),DragSmokePath))
    {
        if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox")))
        {RIFT_LOG(LogRift,Error,TEXT("Card drag smoke requires an isolated -RiftAutomationSandbox."));FPlatformMisc::RequestExitWithStatus(false,2);return;}
        FTimerHandle DragTimer;GetWorld()->GetTimerManager().SetTimer(DragTimer,FTimerDelegate::CreateWeakLambda(this,[this,DragSmokePath]()
        {
            // Only the synchronous fixture's generated events pass the input
            // filter. External input remains consumed during startup/shutdown.
            if(CaptureInputFilter)CaptureInputFilter->SetConsumption(false);
            if(auto* PC=Cast<ARiftPlayerController>(GetWorld()->GetFirstPlayerController()))PC->RunCardDragSmoke(DragSmokePath);
            else FPlatformMisc::RequestExitWithStatus(false,2);
            if(CaptureInputFilter)CaptureInputFilter->SetConsumption(true);
        }),6.f,false);
    }
    FString AudioSmokePath;
    if(FParse::Value(FCommandLine::Get(),TEXT("RiftAudioSmoke="),AudioSmokePath))
    {
        FTimerHandle AudioTimer;GetWorld()->GetTimerManager().SetTimer(AudioTimer,[this,AudioSmokePath]()
        {
            auto* PC=Cast<ARiftPlayerController>(GetWorld()->GetFirstPlayerController());
            auto* Audio=GetGameInstance()->GetSubsystem<URiftBattleAudioSubsystem>();
            Audio->RunAudioSmoke(PC?PC->Interface.Get():nullptr,[AudioSmokePath](const FString& Result)
            {
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(AudioSmokePath),true);
            bool Saved=FFileHelper::SaveStringToFile(Result,*AudioSmokePath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            TSharedPtr<FJsonObject> Report;auto Reader=TJsonReaderFactory<>::Create(Result);
            bool Passed=Saved&&FJsonSerializer::Deserialize(Reader,Report)&&Report.IsValid()&&Report->GetBoolField(TEXT("passed"));
            RIFT_LOG(LogRift,Log,TEXT("Native audio smoke %s: %s"),Passed?TEXT("passed"):TEXT("failed"),*AudioSmokePath);
            FPlatformMisc::RequestExitWithStatus(false,Passed?0:2);
            });
        },6.f,false);
    }
    FString CapturePath;
    if(FParse::Value(FCommandLine::Get(),TEXT("RiftCapture="),CapturePath))
    {
        FString Page=TEXT("Home"),Scenario;FParse::Value(FCommandLine::Get(),TEXT("RiftCapturePage="),Page);FParse::Value(FCommandLine::Get(),TEXT("RiftVisualScenario="),Scenario);
        auto CapturedEvents=MakeShared<TMap<FString,int32>>();
        auto RecordedCapture=MakeShared<bool>(false);
        auto RecordedDuration=MakeShared<double>(0.);
        auto TowerPathingCapture=MakeShared<FJsonObject>();
        auto UnitCollisionCapture=MakeShared<FJsonObject>();
        auto SwarmSplitCapture=MakeShared<FJsonObject>();
        GetWorld()->GetSubsystem<URiftMatchSubsystem>()->OnEvent.AddLambda([CapturedEvents](const rift::Event& Event){++CapturedEvents->FindOrAdd(UTF8_TO_TCHAR(Event.type.c_str()));});
        FTimerHandle SetupTimer;GetWorld()->GetTimerManager().SetTimer(SetupTimer,[this,Page,Scenario,RecordedCapture,RecordedDuration,TowerPathingCapture,UnitCollisionCapture,SwarmSplitCapture]()
        {
            auto* PC=Cast<ARiftPlayerController>(GetWorld()->GetFirstPlayerController());if(!PC||!PC->Interface)return;
            auto* Match=GetWorld()->GetSubsystem<URiftMatchSubsystem>();
            if(Scenario==TEXT("swarm_split"))
            {
                auto* Profile=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();
                if(Profile->Presets.IsEmpty()){FPlatformMisc::RequestExitWithStatus(false,2);return;}
                const auto Original=Profile->Presets[0].Cards;const auto Active=Profile->ActivePreset;
                Profile->Presets[0].Cards={TEXT("mini_stampede"),TEXT("stampede"),TEXT("twin_blades"),TEXT("vampire_bats"),TEXT("ironclad"),TEXT("ember_archer"),TEXT("arc_mage"),TEXT("meteor_shards")};
                Profile->ActivePreset=Profile->Presets[0].Id;Match->StartMatch(true);Profile->Presets[0].Cards=Original;Profile->ActivePreset=Active;
            }
            if(Scenario==TEXT("unit_collision"))
            {
                // A normal selectable deck feeds the normal match constructor.
                // The isolated capture process restores its in-memory preset
                // immediately; it does not persist a replacement player deck.
                auto* Profile=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();
                if(Profile->Presets.IsEmpty()){FPlatformMisc::RequestExitWithStatus(false,2);return;}
                const auto Original=Profile->Presets[0].Cards;const auto Active=Profile->ActivePreset;
                FString Case=TEXT("crowd");FParse::Value(FCommandLine::Get(),TEXT("RiftCollisionCase="),Case);
                Profile->Presets[0].Cards=Case==TEXT("contact")?
                    TArray<FString>{TEXT("ironclad"),TEXT("ember_archer"),TEXT("twin_blades"),TEXT("boulderback"),TEXT("arc_mage"),TEXT("rambeast"),TEXT("frost_fang"),TEXT("archer_tower")}:
                    TArray<FString>{TEXT("ironclad"),TEXT("twin_blades"),TEXT("boulderback"),TEXT("archer_tower"),TEXT("sky_manta"),TEXT("vampire_bats"),TEXT("storm_raven"),TEXT("frost_fang")};
                Profile->ActivePreset=Profile->Presets[0].Id;Match->StartMatch(true);Profile->Presets[0].Cards=Original;Profile->ActivePreset=Active;
            }
            if(Page==TEXT("ReplayView")||Page==TEXT("Analysis")||FParse::Param(FCommandLine::Get(),TEXT("RiftCaptureRecordedMatch")))
            {
                Match->StartMatch(true,true);Match->SetSpeed(4);
                for(int32 I=0;I<1400&&Match->ViewState()&&Match->ViewState()->phase!=rift::Phase::Finished;++I)Match->Tick(.25f);
                auto* Replay=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();
                if(!Match->ViewState()||Match->ViewState()->phase!=rift::Phase::Finished||!Replay->FlushPendingWrites()||Replay->LatestFilename.IsEmpty())
                {
                    RIFT_LOG(LogRift,Error,TEXT("Recorded capture fixture could not finish/save an actual AI match: %s"),*Replay->LastError);
                    FPlatformMisc::RequestExitWithStatus(false,2);return;
                }
                *RecordedDuration=Match->ViewState()->elapsed;
                *RecordedCapture=true;
                if(Page==TEXT("ReplayView")||Page==TEXT("Analysis"))
                {
                    if(!Replay->OpenReplay(Replay->LatestFilename))
                    {RIFT_LOG(LogRift,Error,TEXT("Recorded capture fixture could not open its actual recording: %s"),*Replay->LastError);FPlatformMisc::RequestExitWithStatus(false,2);return;}
                    Replay->Seek(Replay->Duration()*.55f);Replay->SetSpeed(0);
                }
                else if(Page!=TEXT("Battle"))Match->LeaveMatch();
            }
            if(Page==TEXT("Battle")&&!Match->IsActive())Match->StartMatch(true);
            if(!Scenario.IsEmpty())
            {
                if(!Match->IsActive())Match->StartMatch(true);auto* Sim=Match->Simulation();Sim->SetAIEnabled(rift::Team::Player,false);Sim->SetAIEnabled(rift::Team::Enemy,false);
                if(Scenario==TEXT("roster"))
                {
                    // The finite Breath proof deploys this same real roster
                    // shortly before capture, below, so its first mouth puffs
                    // have not already expired when the image is taken.
                    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftBreathSmoke")))
                    {int32 I=0;for(const auto& Card:rift::Cards())if(!Card.spell){Sim->Spawn(rift::Team::Player,Card.id,{double(-10+(I%5)*5),double(3+(I/5)*4)});Sim->Spawn(rift::Team::Enemy,Card.id,{double(-10+(I%5)*5),double(-3-(I/5)*4)});++I;}}
                    Match->SetSpeed(0);
                }
                else if(Scenario==TEXT("congestion"))
                {
                    for(auto Team:{rift::Team::Player,rift::Team::Enemy})for(int32 Lane:{-1,1})
                    {
                        double Z=Team==rift::Team::Player?5:-5;Sim->Spawn(Team,"boulderback",{Lane*7.,Z});Sim->Spawn(Team,"archer_tower",{Lane*10.,Z+4*(Team==rift::Team::Player?1:-1)});
                        for(int32 I=0;I<4;++I){Sim->Spawn(Team,"twin_blades",{Lane*7.+I*.4,Z+I*.8});Sim->Spawn(Team,"vampire_bats",{Lane*7.+I*.3,Z+I*.5});}
                        Sim->Spawn(Team,"frost_fang",{Lane*5.,Z});Sim->Spawn(Team,"storm_raven",{Lane*9.,Z});
                    }
                }
                else if(Scenario==TEXT("effects"))
                {
                    // All showcase attacks/statuses are emitted by ordinary
                    // simulation mechanics from tagged sandbox deployments.
                    Sim->Spawn(rift::Team::Player,"frost_fang",{-8,3});Sim->Spawn(rift::Team::Enemy,"ironclad",{-8,1});
                    Sim->Spawn(rift::Team::Player,"ember_archer",{-10,6});Sim->Spawn(rift::Team::Enemy,"boulderback",{0,1});
                    Sim->Spawn(rift::Team::Player,"arc_mage",{1,5});Sim->Spawn(rift::Team::Player,"sky_manta",{3,5});
                    Sim->Spawn(rift::Team::Player,"storm_raven",{8,3});Sim->Spawn(rift::Team::Enemy,"boulderback",{8,1});
                    Sim->Spawn(rift::Team::Enemy,"archer_tower",{9,2});
                    Sim->Spawn(rift::Team::Player,"rambeast",{0,8});
                    auto NextSpell=MakeShared<double>(3.0);
                    FTimerHandle SpellsTimer;GetWorld()->GetTimerManager().SetTimer(SpellsTimer,[this,NextSpell]()
                    {
                        auto* Current=GetWorld()->GetSubsystem<URiftMatchSubsystem>();if(auto* Combat=Current->Simulation())
                            if(Combat->State().elapsed>=*NextSpell)
                            {*NextSpell+=2.0;Combat->Spawn(rift::Team::Player,"meteor_shards",{0,3});Combat->Spawn(rift::Team::Enemy,"bullet_burst",{-8,3});Combat->Spawn(rift::Team::Player,"nova_flask",{0,0});Current->FlushEvents();}
                    },.02f,true);
                }
                else if(Scenario==TEXT("projectiles"))
                {
                    // Capture only: ordinary sandbox deployments and fixed
                    // simulation steps produce all five card missiles plus
                    // live core/guard shots. Never insert a projectile or edit
                    // an entity snapshot to manufacture renderer coverage.
                    Match->SetSpeed(0);Sim->ClearField();
                    uint64 WakeGuard=0;
                    for(const auto& Entity:Sim->State().entities)
                        if(Entity.team==rift::Team::Enemy&&Entity.kind==rift::EntityKind::Guard&&Entity.lane==-1)WakeGuard=Entity.id;
                    bool Deployed=WakeGuard&&Sim->SetTowerHP(WakeGuard,0);
                    Deployed&=Sim->Spawn(rift::Team::Player,"archer_tower",{5.5,8.5});
                    // Finish the real tower deployment/cooldown before any
                    // opponents arrive; its normal first shot then joins the
                    // troop releases without changing attack cooldowns.
                    Sim->Step(.35);Match->FlushEvents();
                    for(TActorIterator<ARiftArenaPresentation> It(GetWorld());It;++It)It->Tick(0.f);
                    for(double X:{-10.5,-5.5,.5,5.5})
                        Deployed&=Sim->Spawn(rift::Team::Enemy,"boulderback",{X,2.5});
                    Deployed&=Sim->Spawn(rift::Team::Enemy,"archer_tower",{10.5,-3.5});
                    Deployed&=Sim->Spawn(rift::Team::Player,"boulderback",{.5,-10.5});
                    Deployed&=Sim->Spawn(rift::Team::Player,"boulderback",{8.5,-6.5});
                    // Troop casters arrive after all opponents. Ordinary
                    // nearest-target acquisition picks their adjacent lanes,
                    // rather than an earlier DEV deployment pulling several
                    // already-walking casters onto the first shared target.
                    Deployed&=Sim->Spawn(rift::Team::Player,"ember_archer",{-10.5,8.5});
                    Deployed&=Sim->Spawn(rift::Team::Player,"arc_mage",{-5.5,8.5});
                    Deployed&=Sim->Spawn(rift::Team::Player,"sky_manta",{.5,6.5});
                    Deployed&=Sim->Spawn(rift::Team::Player,"storm_raven",{10.5,1.5});
                    Match->FlushEvents();
                    bool Ready=false;int32 Steps=0;
                    for(;Deployed&&Steps<45;++Steps)
                    {
                        Sim->Step(1./60.);Match->FlushEvents();
                        for(TActorIterator<ARiftArenaPresentation> It(GetWorld());It;++It)It->Tick(0.f);
                        TSet<FString> Coverage;double Minimum=1.,Maximum=0.;
                        const auto& State=Sim->State();
                        for(const auto& Projectile:State.projectiles)
                        {
                            FString Role=UTF8_TO_TCHAR(Projectile.cardId.c_str());
                            if(Role.IsEmpty())for(const auto& Entity:State.entities)if(Entity.id==Projectile.source)
                            {Role=Entity.kind==rift::EntityKind::Core?TEXT("crown_core"):Entity.kind==rift::EntityKind::Guard?TEXT("crown_guard"):TEXT("unknown");break;}
                            Coverage.Add(Role);
                            const double Progress=FMath::Clamp(1.-Projectile.remaining/FMath::Max(.001,Projectile.duration),0.,1.);
                            Minimum=FMath::Min(Minimum,Progress);Maximum=FMath::Max(Maximum,Progress);
                        }
                        Ready=Coverage.Contains(TEXT("ember_archer"))&&Coverage.Contains(TEXT("arc_mage"))&&Coverage.Contains(TEXT("sky_manta"))&&
                            Coverage.Contains(TEXT("archer_tower"))&&Coverage.Contains(TEXT("storm_raven"))&&Coverage.Contains(TEXT("crown_core"))&&
                            Coverage.Contains(TEXT("crown_guard"))&&Minimum>=.12&&Maximum<=.45;
                        if(Ready)break;
                    }
                    Match->SetSpeed(0);
                    for(TActorIterator<ARiftArenaPresentation> It(GetWorld());It;++It)It->Tick(0.f);
                    if(!Ready)
                    {
                        RIFT_LOG(LogRift,Error,TEXT("Actual projectile capture fixture failed within 45 fixed steps: deployed=%d live=%d elapsed=%.6f"),
                            Deployed,int32(Sim->State().projectiles.size()),Sim->State().elapsed);
                        FPlatformMisc::RequestExitWithStatus(false,2);return;
                    }
                    RIFT_LOG(LogRift,Log,TEXT("Actual projectile capture fixture reached all seven source roles: %d live shots, %d fixed steps, elapsed=%.6f"),
                        int32(Sim->State().projectiles.size()),Steps+1,Sim->State().elapsed);
                }
                else if(Scenario==TEXT("spells"))
                {
                    // Real opposing deployments stay well apart, so their
                    // first visible damage is owned by the two spell impacts.
                    Match->SetSpeed(0);Sim->ClearField();
                    for(auto Team:{rift::Team::Player,rift::Team::Enemy})
                    {
                        const double Side=Team==rift::Team::Player?1.:-1.;
                        Sim->Spawn(Team,"boulderback",{Side*6.5,Side*4.5});
                        Sim->Spawn(Team,"ember_archer",{Side*8.5,Side*5.5});
                        Sim->Spawn(Team,"vampire_bats",{Side*4.5,Side*5.5});
                    }
                    Sim->Step(.35);Match->FlushEvents();
                }
                else if(Scenario==TEXT("tower_pathing"))
                {
                    if(!BuildTowerPathingCapture(Match,TowerPathingCapture))
                    {RIFT_LOG(LogRift,Error,TEXT("Tower pathing capture failed its actual deployment/clearance/progress checks"));FPlatformMisc::RequestExitWithStatus(false,2);return;}
                }
                else if(Scenario==TEXT("unit_collision"))
                {
                    if(!BuildUnitCollisionCapture(Match,UnitCollisionCapture))
                    {RIFT_LOG(LogRift,Error,TEXT("Unit collision capture failed its actual paid deployment/layer/clearance/progress checks"));FPlatformMisc::RequestExitWithStatus(false,2);return;}
                }
                else if(Scenario==TEXT("swarm_split"))
                {
                    if(!BuildSwarmSplitCapture(Match,SwarmSplitCapture))
                    {RIFT_LOG(LogRift,Error,TEXT("Swarm split capture failed paid formation/landing-lane/body/speed/progress checks"));FPlatformMisc::RequestExitWithStatus(false,2);return;}
                }
                else if(Scenario==TEXT("placement")||Scenario==TEXT("effects17"))Match->SetSpeed(0);
                if(Scenario!=TEXT("roster")&&Scenario!=TEXT("placement")&&Scenario!=TEXT("effects17")&&Scenario!=TEXT("projectiles")&&Scenario!=TEXT("spells")&&Scenario!=TEXT("tower_pathing")&&Scenario!=TEXT("unit_collision")&&Scenario!=TEXT("swarm_split"))
                {float CaptureSpeed=1;FParse::Value(FCommandLine::Get(),TEXT("RiftCaptureSpeed="),CaptureSpeed);Match->SetSpeed(FMath::Clamp(CaptureSpeed,.25f,4.f));}
                Match->FlushEvents();
            }
            if(Page==TEXT("CardDetail"))
            {
                FString InspectCard=TEXT("ironclad");FParse::Value(FCommandLine::Get(),TEXT("RiftInspectCard="),InspectCard);
                if(!rift::FindCard(TCHAR_TO_UTF8(*InspectCard)))InspectCard=TEXT("ironclad");
                PC->Interface->InspectCard(InspectCard);
            }
            else PC->Interface->Navigate(Page);
            if(Page==TEXT("Battle"))
            {
                // Capture-only fixtures use reachable simulation commands.
                // Advancing an empty, AI-disabled match reaches the real phase
                // clock without changing balance or hand-cycle rules.
                FString CapturePhase;
                if(FParse::Value(FCommandLine::Get(),TEXT("RiftCapturePhase="),CapturePhase))
                    if(auto* Simulation=Match->Simulation())
                    {
                        Simulation->SetAIEnabled(rift::Team::Player,false);Simulation->SetAIEnabled(rift::Team::Enemy,false);Simulation->ClearField();
                        if(CapturePhase==TEXT("victory"))
                        {
                            rift::EntityId Core=0;for(const auto& Entity:Simulation->State().entities)if(Entity.team==rift::Team::Enemy&&Entity.kind==rift::EntityKind::Core)Core=Entity.id;
                            if(Core)Simulation->SetTowerHP(Core,0);
                        }
                        else
                        {
                            const double Target=CapturePhase==TEXT("double")?121.:CapturePhase==TEXT("overtime")?181.:CapturePhase==TEXT("triple")?241.:CapturePhase==TEXT("tiebreaker")?301.:0.;
                            if(CapturePhase==TEXT("triple")&&Simulation->State().elapsed<181.)
                            {
                                Simulation->Step(181.-Simulation->State().elapsed);Match->FlushEvents();
                                PC->Interface->SetBattleView(); // Prime the actual overtime HUD before its 3x transition.
                            }
                            if(Target>Simulation->State().elapsed)Simulation->Step(Target-Simulation->State().elapsed);
                        }
                        Match->SetSpeed(0);Match->FlushEvents();
                    }
                int32 CaptureHand=-1;
                if(FParse::Value(FCommandLine::Get(),TEXT("RiftCaptureHand="),CaptureHand)&&CaptureHand>=0&&CaptureHand<4)PC->Interface->SelectHand(CaptureHand);
                if(FParse::Param(FCommandLine::Get(),TEXT("RiftCaptureDeveloper")))PC->Interface->ToggleDeveloper();
                if(FParse::Param(FCommandLine::Get(),TEXT("RiftCaptureBattleMenu")))PC->Interface->ToggleBattleMenu();
                FString Overlay;
                if(FParse::Value(FCommandLine::Get(),TEXT("RiftCaptureOverlay="),Overlay))
                {
                    auto* Developer=GetWorld()->GetSubsystem<URiftDeveloperSubsystem>();
                    Developer->ShowPaths=Overlay==TEXT("all")||Overlay.Contains(TEXT("paths"));
                    Developer->ShowSight=Overlay==TEXT("all")||Overlay.Contains(TEXT("sight"));
                    Developer->ShowRanges=Overlay==TEXT("all")||Overlay.Contains(TEXT("ranges"));
                    Developer->ShowTargets=Overlay==TEXT("all")||Overlay.Contains(TEXT("targets"));
                    Developer->ShowHardLocks=Overlay==TEXT("all")||Overlay.Contains(TEXT("locks"));
                    Developer->ShowTiles=Overlay==TEXT("all")||Overlay.Contains(TEXT("tiles"));
                }
            }
        },.5f,false);
        float Delay=6;FParse::Value(FCommandLine::Get(),TEXT("RiftCaptureDelay="),Delay);bool Quit=FParse::Param(FCommandLine::Get(),TEXT("RiftQuitAfterCapture"));
        if(Scenario==TEXT("spells"))
        {
            FTimerHandle SpellSampleTimer;
            GetWorld()->GetTimerManager().SetTimer(SpellSampleTimer,FTimerDelegate::CreateWeakLambda(this,[this]()
            {
                auto* Match=GetWorld()->GetSubsystem<URiftMatchSubsystem>();auto* Sim=Match->Simulation();if(!Sim)return;
                float Age=.15f;FParse::Value(FCommandLine::Get(),TEXT("RiftEffectAge="),Age);
                Age=FMath::IsFinite(Age)?FMath::Clamp(Age,.05f,2.f):.15f;
                const bool Deployed=Sim->Spawn(rift::Team::Player,"meteor_shards",{-6.5,-4.5})&&
                    Sim->Spawn(rift::Team::Enemy,"bullet_burst",{6.5,4.5});
                Match->FlushEvents();
                const int32 Steps=FMath::RoundToInt(double(Age)*60.);
                for(int32 Step=0;Step<Steps;++Step)
                {
                    Sim->Step(1./60.);Match->FlushEvents();
                    for(TActorIterator<ARiftArenaPresentation> It(GetWorld());It;++It)It->Tick(0.f);
                }
                Match->SetSpeed(0);
                for(TActorIterator<ARiftArenaPresentation> It(GetWorld());It;++It)
                {It->Tick(0.f);It->SampleSpellImpactEffectsForQA();}
                if(!Deployed){RIFT_LOG(LogRift,Error,TEXT("Spell timing fixture could not deploy its casts"));FPlatformMisc::RequestExitWithStatus(false,2);return;}
                RIFT_LOG(LogRift,Log,TEXT("Spell timing fixture sampled %.6fs after cast: %d pending, %d hazards, elapsed %.6f"),
                    Steps/60.,int32(Sim->State().spellCasts.size()),int32(Sim->State().hazards.size()),Sim->State().elapsed);
            }),FMath::Max(1.5f,Delay)-.5f,false);
        }
        if(Scenario==TEXT("roster")&&FParse::Param(FCommandLine::Get(),TEXT("RiftBreathSmoke")))
        {
            // QA only. Use ordinary deployments and a short real simulation
            // run; pausing ends locomotion and naturally selects Breath. The
            // authored .4s puff remains finite and is sampled about .2s old.
            Delay=FMath::Max(1.6f,Delay);FTimerHandle BreathRosterTimer,BreathStopTimer;
            GetWorld()->GetTimerManager().SetTimer(BreathRosterTimer,FTimerDelegate::CreateWeakLambda(this,[this]()
            {
                auto* Match=GetWorld()->GetSubsystem<URiftMatchSubsystem>();if(auto* Sim=Match->Simulation())
                {
                    int32 I=0;for(const auto& Card:rift::Cards())if(!Card.spell){Sim->Spawn(rift::Team::Player,Card.id,{double(-10+(I%5)*5),double(3+(I/5)*4)});Sim->Spawn(rift::Team::Enemy,Card.id,{double(-10+(I%5)*5),double(-3-(I/5)*4)});++I;}
                    Match->FlushEvents();Match->SetSpeed(1);
                }
            }),Delay-.85f,false);
            // Schedule this before either callback executes. FlushEvents adds
            // finite-effect cleanup timers, so no executing small timer
            // closure should be read again after that timer-array growth.
            GetWorld()->GetTimerManager().SetTimer(BreathStopTimer,FTimerDelegate::CreateWeakLambda(this,[this](){GetWorld()->GetSubsystem<URiftMatchSubsystem>()->SetSpeed(0);}),Delay-.2f,false);
        }
        if(Scenario==TEXT("effects17"))
        {
            // Freeze the genuine graph sample before capture so its scene
            // proxies and asynchronously precached PSOs get real render frames.
            // Creating them in the screenshot callback can yield an empty PNG
            // even while the CPU particle data is already available.
            FTimerHandle EffectWarmTimer;
            GetWorld()->GetTimerManager().SetTimer(EffectWarmTimer,FTimerDelegate::CreateWeakLambda(this,[this]()
            {
                float Age=.12f;FParse::Value(FCommandLine::Get(),TEXT("RiftEffectAge="),Age);
                Age=FMath::IsFinite(Age)?FMath::Clamp(Age,0.f,2.f):.12f;
                for(TActorIterator<ARiftArenaPresentation> It(GetWorld());It;++It)
                {It->ShowcaseNiagaraAtAge(Age);break;}
            }),FMath::Max(1.f,Delay)-.5f,false);
        }
        FTimerHandle CaptureTimer;GetWorld()->GetTimerManager().SetTimer(CaptureTimer,FTimerDelegate::CreateWeakLambda(this,[this,CapturePath,Quit,CapturedEvents,RecordedCapture,RecordedDuration,TowerPathingCapture,UnitCollisionCapture,SwarmSplitCapture]()
        {
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(CapturePath),true);
            auto Snapshot=MakeShared<FJsonObject>();auto EventCounts=MakeShared<FJsonObject>();
            if(CaptureInputFilter)Snapshot->SetObjectField(TEXT("captureInput"),CaptureInputFilter->Report());
            Snapshot->SetBoolField(TEXT("recordedMatchFixture"),*RecordedCapture);
            if(TowerPathingCapture->HasField(TEXT("passed")))Snapshot->SetObjectField(TEXT("towerPathing"),TowerPathingCapture);
            if(UnitCollisionCapture->HasField(TEXT("passed")))Snapshot->SetObjectField(TEXT("unitCollision"),UnitCollisionCapture);
            if(SwarmSplitCapture->HasField(TEXT("passed")))
            {
                TArray<TSharedPtr<FJsonValue>> Visuals;const FString CardId=SwarmSplitCapture->GetStringField(TEXT("cardId"));
                for(TActorIterator<ARiftUnitVisual> It(GetWorld());It;++It)if(It->PresentationAssetId()==CardId)
                {auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("id"),It->EntityId);Row->SetBoolField(TEXT("dead"),It->IsDead());Row->SetStringField(TEXT("animation"),It->CurrentAnimation().ToString());
                 Row->SetStringField(TEXT("animationAsset"),It->AnimationAssetPath());Row->SetNumberField(TEXT("animationPosition"),It->AnimationPosition());Row->SetNumberField(TEXT("animationCycleFraction"),It->AnimationCycleFraction());
                 Row->SetNumberField(TEXT("locomotionPhase"),It->LocomotionPhase());Row->SetNumberField(TEXT("modelScale"),It->PresentationScale());Visuals.Add(MakeShared<FJsonValueObject>(Row));}
                SwarmSplitCapture->SetArrayField(TEXT("visuals"),Visuals);Snapshot->SetObjectField(TEXT("swarmSplit"),SwarmSplitCapture);
            }
            if(*RecordedCapture)
            {
                const auto* Replay=GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();
                Snapshot->SetStringField(TEXT("recordedReplay"),FPaths::GetCleanFilename(Replay->LatestFilename));
                Snapshot->SetNumberField(TEXT("replayDuration"),*RecordedDuration);Snapshot->SetNumberField(TEXT("replayPosition"),Replay->TimelinePosition());
            }
            for(const auto& Pair:*CapturedEvents)EventCounts->SetNumberField(Pair.Key,Pair.Value);
            Snapshot->SetObjectField(TEXT("events"),EventCounts);
            if(const auto* PC=Cast<ARiftPlayerController>(GetWorld()->GetFirstPlayerController()))
            {
                TSharedPtr<FJsonObject> Framing;
                if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(PC->CameraFramingDiagnosticsJSON()),Framing))
                {
                    Snapshot->SetObjectField(TEXT("cameraFraming"),Framing);
                    if(Framing->GetBoolField(TEXT("activeBattleView"))&&!Framing->GetBoolField(TEXT("passed")))
                        RIFT_LOG(LogRift,Error,TEXT("Legal field projection extends beneath battle HUD: %s"),*CapturePath);
                }
            }
            auto* Match=GetWorld()->GetSubsystem<URiftMatchSubsystem>();Snapshot->SetNumberField(TEXT("speed"),Match->GetSpeed());
            if(const auto* State=Match->ViewState())
            {
                int32 Alive=0,Slowed=0,Stunned=0;for(const auto& Entity:State->entities)if(!Entity.dead){++Alive;if(Entity.slowUntil>State->elapsed)++Slowed;if(Entity.stunUntil>State->elapsed)++Stunned;}
                Snapshot->SetNumberField(TEXT("elapsed"),State->elapsed);Snapshot->SetNumberField(TEXT("aliveEntities"),Alive);
                Snapshot->SetNumberField(TEXT("slowedEntities"),Slowed);Snapshot->SetNumberField(TEXT("stunnedEntities"),Stunned);
                Snapshot->SetNumberField(TEXT("projectiles"),State->projectiles.size());Snapshot->SetNumberField(TEXT("hazards"),State->hazards.size());
                Snapshot->SetNumberField(TEXT("spellCasts"),State->spellCasts.size());
                Snapshot->SetStringField(TEXT("phase"),UTF8_TO_TCHAR(rift::PhaseName(State->phase).c_str()));
                if(State->phase==rift::Phase::Finished){Snapshot->SetNumberField(TEXT("winner"),State->winner);Snapshot->SetStringField(TEXT("resultReason"),UTF8_TO_TCHAR(State->resultReason.c_str()));}
                Snapshot->SetNumberField(TEXT("timeRemaining"),State->timeRemaining);Snapshot->SetNumberField(TEXT("playerCrowns"),State->crowns[0]);Snapshot->SetNumberField(TEXT("enemyCrowns"),State->crowns[1]);
            }
            FString PreviewCard;if(FParse::Value(FCommandLine::Get(),TEXT("RiftPreviewCard="),PreviewCard))if(const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*PreviewCard)))if(auto* Sim=Match->Simulation())
            {
                float X=0,Y=7;FParse::Value(FCommandLine::Get(),TEXT("RiftPreviewX="),X);FParse::Value(FCommandLine::Get(),TEXT("RiftPreviewY="),Y);const auto Tile=rift::SnapToTile({X,Y});
                Snapshot->SetStringField(TEXT("previewCard"),PreviewCard);Snapshot->SetNumberField(TEXT("previewX"),Tile.x);Snapshot->SetNumberField(TEXT("previewY"),Tile.z);
                Snapshot->SetBoolField(TEXT("previewValid"),Sim->CanPlace(rift::Team::Player,*Card,Tile));Snapshot->SetNumberField(TEXT("previewFootprintTiles"),Card->footprint);Snapshot->SetNumberField(TEXT("previewRadiusTiles"),Card->spellRadius);
            }
            for(TActorIterator<ARiftArenaPresentation> It(GetWorld());It;++It)
            {
                Snapshot->SetNumberField(TEXT("trainingOverlayLines"),It->TrainingOverlayLineCount());Snapshot->SetNumberField(TEXT("trainingOverlayLabels"),It->TrainingOverlayLabelCount());
                TSharedPtr<FJsonObject> Niagara;
                if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(It->NiagaraDiagnosticsJSON()),Niagara))Snapshot->SetObjectField(TEXT("niagara"),Niagara);
                TSharedPtr<FJsonObject> Geometry;
                if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(It->GeometryDiagnosticsJSON()),Geometry))Snapshot->SetObjectField(TEXT("arenaGeometry"),Geometry);
                break;
            }
            FString SnapshotText;auto Writer=TJsonWriterFactory<>::Create(&SnapshotText);FJsonSerializer::Serialize(Snapshot,Writer);
            FFileHelper::SaveStringToFile(SnapshotText,*(FPaths::ChangeExtension(CapturePath,TEXT("state.json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            FScreenshotRequest::RequestScreenshot(CapturePath,true,false,false,FIntRect(),true);
            if(Quit)FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([](){FPlatformMisc::RequestExit(false);});
        }),FMath::Max(1.f,Delay),false);
    }
    int32 MetaGames=0;
    if(FParse::Value(FCommandLine::Get(),TEXT("RiftMetaValidate="),MetaGames)&&MetaGames>0)
    {
        auto* Meta=GetGameInstance()->GetSubsystem<URiftMetaSimulationSubsystem>();
        if(!FParse::Param(FCommandLine::Get(),TEXT("RiftMetaResume")))Meta->Reset();
        double ExistingGames=0;auto Existing=Meta->Dataset();if(Existing)Existing->TryGetNumberField(TEXT("games"),ExistingGames);
        Meta->SetRate(0);Meta->Start(FMath::Max(1,MetaGames-FMath::RoundToInt(ExistingGames)));
        FTimerHandle MetaTimer;GetWorld()->GetTimerManager().SetTimer(MetaTimer,[Meta,MetaGames]()
        {
            auto Data=Meta->Dataset();double Games=0;if(!Data||!Data->TryGetNumberField(TEXT("games"),Games)||Games<MetaGames)return;
            Meta->Pause();FString Path=FPaths::Combine(URiftProfileSubsystem::SaveRoot(),TEXT("meta-validation.json"));Meta->Export(TEXT("cards"),Path);
            RIFT_LOG(LogRift,Log,TEXT("Native Meta validation completed: %.0f actual games. %s"),Games,*Path);FPlatformMisc::RequestExit(false);
        },2.f,true);
    }
}
void ARiftGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if(CaptureInputFilter&&FSlateApplication::IsInitialized())FSlateApplication::Get().UnregisterInputPreProcessor(CaptureInputFilter);
    CaptureInputFilter.Reset();
    if(auto* World=GetWorld())if(auto* Match=World->GetSubsystem<URiftMatchSubsystem>())Match->LeaveMatch();
    if(auto* GI=GetGameInstance())if(auto* Replay=GI->GetSubsystem<URiftReplaySubsystem>())Replay->FlushPendingWrites();
    Super::EndPlay(EndPlayReason);
}

ARiftPlayerController::ARiftPlayerController()
{bShowMouseCursor=true;PrimaryActorTick.bCanEverTick=true;}
void ARiftPlayerController::BeginPlay()
{
    Super::BeginPlay();
    CameraActor=GetWorld()->SpawnActor<AActor>();
    ArenaCamera=NewObject<UCameraComponent>(CameraActor,TEXT("ArenaCamera"));CameraActor->SetRootComponent(ArenaCamera);ArenaCamera->RegisterComponent();
    CameraActor->SetActorLocation(FVector(0,3600,5400));CameraActor->SetActorRotation(UKismetMathLibrary::FindLookAtRotation(CameraActor->GetActorLocation(),FVector(0,500,0)));
    ArenaCamera->ProjectionMode=ECameraProjectionMode::Orthographic;ArenaCamera->OrthoWidth=8000;ArenaCamera->bConstrainAspectRatio=false;ArenaCamera->bAutoCalculateOrthoPlanes=true;
    ArenaCamera->AspectRatioAxisConstraint=EAspectRatioAxisConstraint::AspectRatio_MaintainXFOV;
    ArenaCamera->bOverrideAspectRatioAxisConstraint=true;
    ArenaCamera->PostProcessSettings.bOverride_AutoExposureMethod=true;ArenaCamera->PostProcessSettings.AutoExposureMethod=EAutoExposureMethod::AEM_Manual;
    ArenaCamera->PostProcessSettings.bOverride_AutoExposureBias=true;ArenaCamera->PostProcessSettings.AutoExposureBias=0;
    ArenaCamera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure=true;
    ArenaCamera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure=false;
    SetViewTarget(CameraActor);
    // These overrides belong only to an isolated screenshot process; they do
    // not call ApplySettings or write a player's display preferences.
    FString CapturePath;
    if(FParse::Value(FCommandLine::Get(),TEXT("RiftCapture="),CapturePath))
    {
        float UIScale=1;
        if(FParse::Value(FCommandLine::Get(),TEXT("RiftCaptureUIScale="),UIScale)&&FMath::IsFinite(UIScale))
        {
            UIScale=FMath::Clamp(UIScale,.7f,1.4f);
            GetGameInstance()->GetSubsystem<URiftProfileSubsystem>()->Settings.UIScale=UIScale;
            GetMutableDefault<UUserInterfaceSettings>()->ApplicationScale=UIScale;
        }
        float CaptureZoom=1;
        if(FParse::Value(FCommandLine::Get(),TEXT("RiftCaptureZoom="),CaptureZoom)&&FMath::IsFinite(CaptureZoom))PendingCaptureZoom=FMath::Clamp(CaptureZoom,.1f,2.f);
    }
    Interface=CreateWidget<URiftUIWidget>(this,URiftUIWidget::StaticClass());Interface->AddToViewport(10);
    FInputModeGameAndUI Mode;Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);Mode.SetHideCursorDuringCapture(false);SetInputMode(Mode);
    if(FParse::Param(FCommandLine::Get(),TEXT("RiftTraining"))){GetWorld()->GetSubsystem<URiftMatchSubsystem>()->StartMatch(true);Interface->SetBattleView();}
    if(FParse::Param(FCommandLine::Get(),TEXT("RiftAutoBattle"))){GetWorld()->GetSubsystem<URiftMatchSubsystem>()->StartMatch(true,true);Interface->SetBattleView();}
    FString DragSmokePath;
    if(FParse::Value(FCommandLine::Get(),TEXT("RiftDragSmoke="),DragSmokePath))
    {auto* Match=GetWorld()->GetSubsystem<URiftMatchSubsystem>();Match->StartMatch(true);Match->SetSpeed(0);Interface->SetBattleView();}
    UpdateArenaCamera();
}
void ARiftPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::LeftMouseButton,IE_Pressed,this,&ARiftPlayerController::Press);InputComponent->BindKey(EKeys::LeftMouseButton,IE_Released,this,&ARiftPlayerController::Release);
    InputComponent->BindKey(EKeys::RightMouseButton,IE_Pressed,this,&ARiftPlayerController::Cancel);
    InputComponent->BindKey(EKeys::Escape,IE_Pressed,this,&ARiftPlayerController::EscapeMenu);
    InputComponent->BindKey(EKeys::F9,IE_Pressed,this,&ARiftPlayerController::Developer);
    InputComponent->BindKey(EKeys::One,IE_Pressed,this,&ARiftPlayerController::Hand0);InputComponent->BindKey(EKeys::Two,IE_Pressed,this,&ARiftPlayerController::Hand1);
    InputComponent->BindKey(EKeys::Three,IE_Pressed,this,&ARiftPlayerController::Hand2);InputComponent->BindKey(EKeys::Four,IE_Pressed,this,&ARiftPlayerController::Hand3);
    InputComponent->BindKey(EKeys::MouseScrollUp,IE_Pressed,this,&ARiftPlayerController::ZoomIn);InputComponent->BindKey(EKeys::MouseScrollDown,IE_Pressed,this,&ARiftPlayerController::ZoomOut);
}
bool ARiftPlayerController::CursorTile(FVector2D& Out)const
{
    if(bCardDragStarted)return ScreenPointToTile(CardDragCursor,Out);
    float X=0,Y=0;int32 Width=0,Height=0;GetViewportSize(Width,Height);
    if(!GetMousePosition(X,Y)||X<0||Y<0||X>=Width||Y>=Height||!BattleSafeScreenBounds().IsInside(FVector2D(X,Y)))return false;
    FVector Origin,Direction;if(!DeprojectScreenPositionToWorld(X,Y,Origin,Direction)||FMath::Abs(Direction.Z)<.001)return false;
    double T=-Origin.Z/Direction.Z;if(T<0)return false;return GroundPointToTile(Origin+Direction*T,Out);
}
bool ARiftPlayerController::ScreenPointToTile(FVector2D ScreenPoint,FVector2D& Out)const
{
    const FGeometry Viewport=UWidgetLayoutLibrary::GetViewportWidgetGeometry(this);
    const FVector2D Local=Viewport.AbsoluteToLocal(ScreenPoint),Size=Viewport.GetLocalSize();
    int32 Width=0,Height=0;GetViewportSize(Width,Height);
    if(Size.X<=0||Size.Y<=0||Width<=0||Height<=0||Local.X<0||Local.Y<0||Local.X>=Size.X||Local.Y>=Size.Y)return false;
    const FVector2D Pixel(Local.X/Size.X*Width,Local.Y/Size.Y*Height);
    // Captured hand releases still route to their original button. Reject HUD,
    // the hand dock and points beyond the viewport before world projection.
    if(!BattleSafeScreenBounds().IsInside(Pixel))return false;
    FVector Origin,Direction;
    if(!DeprojectScreenPositionToWorld(Pixel.X,Pixel.Y,Origin,Direction)||FMath::Abs(Direction.Z)<.001)return false;
    const double T=-Origin.Z/Direction.Z;
    return T>=0&&GroundPointToTile(Origin+Direction*T,Out);
}
bool ARiftPlayerController::GroundPointToTile(FVector GroundPoint,FVector2D& Out)
{
    const auto Point=URiftMatchSubsystem::TilePoint(GroundPoint);
    // SnapToTile clamps to the nearest board cell. Reject decorative ground
    // first so an off-board click cannot become an unintended edge deployment.
    if(!FMath::IsFinite(Point.x)||!FMath::IsFinite(Point.z)||FMath::Abs(Point.x)>rift::arena::HalfWidth||FMath::Abs(Point.z)>rift::arena::HalfHeight)return false;
    const auto Snapped=rift::SnapToTile(Point);Out=FVector2D(Snapped.x,Snapped.z);return true;
}
void ARiftPlayerController::PlayerTick(float DeltaTime)
{
    if(bCardDragStarted&&(!CardDragSelectionIsCurrent()||(FSlateApplication::IsInitialized()&&
        (!FSlateApplication::Get().IsActive()||!FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton)))))CancelCardDrag();
    Super::PlayerTick(DeltaTime);UpdateArenaCamera();
    if(!Arena)for(TActorIterator<ARiftArenaPresentation> It(GetWorld());It;++It){Arena=*It;break;}
    FString CapturePath,PreviewCard;
    if(Arena&&Interface&&Interface->IsBattleView()&&FParse::Value(FCommandLine::Get(),TEXT("RiftCapture="),CapturePath)&&FParse::Value(FCommandLine::Get(),TEXT("RiftPreviewCard="),PreviewCard))
        if(const auto* Preview=rift::FindCard(TCHAR_TO_UTF8(*PreviewCard)))if(auto* Sim=GetWorld()->GetSubsystem<URiftMatchSubsystem>()->Simulation())
        {
            float X=0,Y=7;FParse::Value(FCommandLine::Get(),TEXT("RiftPreviewX="),X);FParse::Value(FCommandLine::Get(),TEXT("RiftPreviewY="),Y);const auto Snapped=rift::SnapToTile({X,Y});
            Arena->SetPlacementPreview(FVector2D(Snapped.x,Snapped.z),Sim->CanPlace(rift::Team::Player,*Preview,Snapped),Preview->spellRadius,Preview->spell,Preview->building?Preview->footprint:0);return;
        }
    FVector2D Tile=FVector2D::ZeroVector;const auto* C=Interface?rift::FindCard(TCHAR_TO_UTF8(*Interface->SelectedCardId())):nullptr;
    bool Visible=Interface&&Interface->CanAcceptBattleInput()&&C&&CursorTile(Tile);
    if(!Visible&&Arena)Arena->ClearPlacementPreview();
    if(Visible)
    {
        auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();bool Valid=M->CanPlace(Interface->SelectedHand(),Tile);
        if(Interface->PlacementIsSandbox()&&M->Simulation())Valid=M->Simulation()->CanPlace(rift::Team(Interface->PlacementTeam()),*C,{Tile.X,Tile.Y},true);
        if(Arena)Arena->SetPlacementPreview(Tile,Valid,C->spellRadius,C->spell,C->building?C->footprint:0);
    }
}
void ARiftPlayerController::Deploy(FVector2D Tile)
{
    if(!Interface||!Interface->CanAcceptBattleInput())return;
    auto* Profile=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();const FString Card=Interface->SelectedCardId();
    if(Card.IsEmpty())return;
    bool Valid=CanDeployTile(Tile);
    if(!Valid){bConfirmed=false;Interface->WorldClicked(Tile);return;}
    if(Profile->Settings.ConfirmDeploy&&(!bConfirmed||ConfirmCard!=Card||!ConfirmTile.Equals(Tile,.001)))
    {bConfirmed=true;ConfirmCard=Card;ConfirmTile=Tile;Interface->Notify(TEXT("Click the highlighted tile again to deploy."));return;}
    bConfirmed=false;Interface->WorldClicked(Tile);
}
void ARiftPlayerController::Press()
{
    if(bCardDragStarted)return;
    bPressed=Interface&&Interface->CanAcceptBattleInput()&&!Interface->SelectedCardId().IsEmpty();
    auto* Profile=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();FVector2D Tile;
    if(bPressed&&!Profile->Settings.DragDeploy&&CursorTile(Tile))Deploy(Tile);
}
void ARiftPlayerController::Release()
{
    // Slate owns captured hand releases. A viewport mouse-up queued before a
    // new hand press must not finish that newer drag transaction.
    if(bCardDragStarted)return;
    const bool Armed=bPressed;bPressed=false;
    auto* Profile=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();FVector2D Tile;
    if(Armed&&Profile->Settings.DragDeploy&&CursorTile(Tile))Deploy(Tile);
}
bool ARiftPlayerController::CanDeployTile(FVector2D Tile)const
{
    if(!Interface||!Interface->CanAcceptBattleInput())return false;
    auto* Match=GetWorld()->GetSubsystem<URiftMatchSubsystem>();
    const auto* State=Match->ViewState();const FString Id=Interface->SelectedCardId();const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*Id));
    if(Interface->PlacementIsSandbox()&&Card)
        return Match->Simulation()&&Match->Simulation()->CanPlace(rift::Team(Interface->PlacementTeam()),*Card,{Tile.X,Tile.Y},true);
    return State&&Card&&State->aether[0]+1e-9>=Card->cost&&Match->CanPlace(Interface->SelectedHand(),Tile);
}
bool ARiftPlayerController::CardDragSelectionIsCurrent()const
{
    return Interface&&Interface->CanAcceptBattleInput()&&Interface->SelectedHand()==CardDragHand&&
        !CardDragId.IsEmpty()&&Interface->SelectedCardId()==CardDragId;
}
void ARiftPlayerController::BeginCardDragAtScreen(FVector2D ScreenPoint)
{
    ResetCardDrag();bConfirmed=false;bPressed=false;
    if(!Interface||!Interface->CanAcceptBattleInput()||Interface->SelectedHand()<0)return;
    bCardDragStarted=true;CardDragOrigin=ScreenPoint;CardDragCursor=ScreenPoint;
    CardDragHand=Interface->SelectedHand();CardDragId=Interface->SelectedCardId();
}
void ARiftPlayerController::BeginCardDrag()
{if(FSlateApplication::IsInitialized())BeginCardDragAtScreen(FSlateApplication::Get().GetCursorPos());}
void ARiftPlayerController::UpdateCardDragAtScreen(FVector2D ScreenPoint)
{
    if(!bCardDragStarted)return;
    CardDragCursor=ScreenPoint;
    if(!CardDragSelectionIsCurrent()){CancelCardDrag();return;}
    const auto* Profile=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();
    // Latch once crossed: returning to the original card remains a drag that
    // must be cancelled, rather than becoming a fresh card-selection click.
    if(Profile->Settings.DragDeploy&&FVector2D::DistSquared(CardDragOrigin,ScreenPoint)>49.)bCardDragActive=true;
}
bool ARiftPlayerController::IsDraggingCard()const
{
    return bCardDragStarted&&bCardDragActive&&CardDragSelectionIsCurrent();
}
void ARiftPlayerController::FinishCardDragAtScreen(FVector2D ScreenPoint)
{UpdateCardDragAtScreen(ScreenPoint);FinishCardDrag(true);}
void ARiftPlayerController::FinishCardDrag(bool DeployIfOutside)
{
    if(!bCardDragStarted)return;
    const bool Current=CardDragSelectionIsCurrent(),Dragged=bCardDragActive;
    FVector2D Tile=FVector2D::ZeroVector;const bool Legal=Current&&DeployIfOutside&&Dragged&&ScreenPointToTile(CardDragCursor,Tile)&&CanDeployTile(Tile);
    ResetCardDrag();bPressed=false;
    if(!Current){CancelCardDrag();return;}
    if(!Dragged)return; // A simple click selects; it never deploys from the hand.
    if(Legal)Deploy(Tile);
    else{CancelCardDrag();if(Interface&&Interface->CanAcceptBattleInput())Interface->Notify(TEXT("Card returned to hand."));}
}
void ARiftPlayerController::ResetCardDrag()
{bCardDragStarted=false;bCardDragActive=false;CardDragHand=INDEX_NONE;CardDragId.Empty();if(Arena)Arena->ClearPlacementPreview();}
void ARiftPlayerController::CancelCardDrag()
{
    const bool HadPointer=bCardDragStarted;bConfirmed=false;bPressed=false;ResetCardDrag();if(Interface)Interface->SelectHand(-1);
    if(HadPointer&&FSlateApplication::IsInitialized())FSlateApplication::Get().ReleaseAllPointerCapture();
}
void ARiftPlayerController::ReleaseCardAtCursor()
{FinishCardDrag(true);}
void ARiftPlayerController::Cancel()
{CancelCardDrag();}
void ARiftPlayerController::EscapeMenu()
{
    if(bCardDragStarted||(Interface&&!Interface->SelectedCardId().IsEmpty())){CancelCardDrag();return;}
    bConfirmed=false;bPressed=false;if(!Interface)return;
    if(Interface->IsLiveBattleView())Interface->ToggleBattleMenu();
    else if(GetGameInstance()->GetSubsystem<URiftReplaySubsystem>()->IsPlaying())Interface->Navigate(TEXT("ReplayView"));
    else if(GetWorld()->GetSubsystem<URiftMatchSubsystem>()->IsActive())Interface->Navigate(TEXT("Battle"));
    else Interface->Navigate(TEXT("Home"));
}
void ARiftPlayerController::Developer(){if(Interface&&Interface->CanAcceptBattleInput())Interface->ToggleDeveloper();}
void ARiftPlayerController::Hand0(){CancelCardDrag();if(Interface&&Interface->CanAcceptBattleInput())Interface->SelectHand(0);}
void ARiftPlayerController::Hand1(){CancelCardDrag();if(Interface&&Interface->CanAcceptBattleInput())Interface->SelectHand(1);}
void ARiftPlayerController::Hand2(){CancelCardDrag();if(Interface&&Interface->CanAcceptBattleInput())Interface->SelectHand(2);}
void ARiftPlayerController::Hand3(){CancelCardDrag();if(Interface&&Interface->CanAcceptBattleInput())Interface->SelectHand(3);}

namespace
{
    // Include wing/body reach outside the shared legal field and the tallest
    // enlarged troop/health anchor at its edges. The full-model contract includes
    // Raven wing reach and the raised anchors on flyers and crown structures.
    void ForEachBattleEnvelopePoint(TFunctionRef<void(const FVector&)> Visit)
    {
        for(float X:{-RiftArenaGeometry::ModelHalfWidth,RiftArenaGeometry::ModelHalfWidth})
            for(float Y:{-RiftArenaGeometry::ModelHalfHeight,RiftArenaGeometry::ModelHalfHeight})
                for(float Z:{0.f,RiftArenaGeometry::ModelHeight})Visit(FVector(X,Y,Z));
        for(float X:{-RiftArenaGeometry::CoreHalfWidth,RiftArenaGeometry::CoreHalfWidth})
            for(float Y:{-RiftArenaGeometry::CoreHalfDepth,RiftArenaGeometry::CoreHalfDepth})
                for(float Z:{0.f,RiftArenaGeometry::ModelHeight})Visit(FVector(X,Y,Z));
    }
}

void ARiftPlayerController::UpdateArenaCamera()
{
    int32 Width=0,Height=0;GetViewportSize(Width,Height);if(!ArenaCamera||!CameraActor||Width<=0||Height<=0)return;
    constexpr float BaselineHeight=5350.f;
    const bool BattleView=Interface&&Interface->IsBattleView();
    const bool LiveBattle=BattleView&&Interface->IsLiveBattleView();
    // A 35-degree battle angle expands the field and vertical model silhouettes
    // while fitting the complete field above the hand dock behind the Core.
    constexpr float BattleElevationDegrees=35.f;
    const FVector BaselineLocation=BattleView
        ?FVector(0,500.f+5400.f/FMath::Tan(FMath::DegreesToRadians(BattleElevationDegrees)),5400)
        :FVector(0,3600,5400);
    CameraActor->SetActorRotation(UKismetMathLibrary::FindLookAtRotation(BaselineLocation,FVector(0,500,0)));
    const FVector Up=ArenaCamera->GetUpVector(),Right=ArenaCamera->GetRightVector();
    CameraUIScale=UWidgetLayoutLibrary::GetViewportScale(this);
    CameraSafeTop=BattleView?(LiveBattle?RiftBattleLayout::HeaderBottom:RiftBattleLayout::ReplayHeaderBottom)+RiftBattleLayout::WorldGutter:0.f;
    CameraSafeBottom=BattleView?(LiveBattle?RiftBattleLayout::HandTop:RiftBattleLayout::ReplayDockTop)+RiftBattleLayout::WorldGutter:0.f;
    CameraSafeLeft=BattleView?(LiveBattle?RiftBattleLayout::NormalLeftSidebar+RiftBattleLayout::WorldGutter:RiftBattleLayout::SideGutter):0.f;
    CameraSafeRight=BattleView?(LiveBattle?(Interface->IsBattleDeveloperVisible()?RiftBattleLayout::DeveloperRightSidebar:RiftBattleLayout::NormalRightSidebar)+RiftBattleLayout::WorldGutter:RiftBattleLayout::SideGutter):0.f;
    CameraSafeTop*=CameraUIScale;CameraSafeBottom*=CameraUIScale;CameraSafeLeft*=CameraUIScale;CameraSafeRight*=CameraUIScale;
    MinCameraZoom=BattleView?.1f:.85f;MaxCameraZoom=1.2f;
    double MinUp=0,MaxUp=0,MinRight=0,MaxRight=0;
    if(BattleView)
    {
        MinUp=TNumericLimits<double>::Max();MaxUp=TNumericLimits<double>::Lowest();
        MinRight=MinUp;MaxRight=MaxUp;
        ForEachBattleEnvelopePoint([&](const FVector& Point)
        {
            const double Vertical=FVector::DotProduct(Point,Up),Horizontal=FVector::DotProduct(Point,Right);
            MinUp=FMath::Min(MinUp,Vertical);MaxUp=FMath::Max(MaxUp,Vertical);
            MinRight=FMath::Min(MinRight,Horizontal);MaxRight=FMath::Max(MaxRight,Horizontal);
        });
        const double VerticalSpan=MaxUp-MinUp,HorizontalSpan=MaxRight-MinRight;
        const float AvailableHeight=FMath::Max(1.f,Height-CameraSafeTop-CameraSafeBottom);
        const float AvailableWidth=FMath::Max(1.f,Width-CameraSafeLeft-CameraSafeRight);
        const float MinimumHeight=float(FMath::Max(VerticalSpan*Height/AvailableHeight,HorizontalSpan*Height/AvailableWidth));
        MinCameraZoom=FMath::Max(MinCameraZoom,MinimumHeight/BaselineHeight);
        // Large UI and smaller windows can need a wider view than the original
        // zoom ceiling. Retain useful wheel movement above that safe minimum.
        MaxCameraZoom=FMath::Max(MaxCameraZoom,MinCameraZoom*1.2f);
    }
    // Fit the entire field and its tallest models as closely as possible on the
    // first battle frame. Later wheel choices retain the same safety envelope.
    if(BattleView&&!bBattleCameraInitialized){CameraZoom=MinCameraZoom;bBattleCameraInitialized=true;}
    if(!BattleView)bBattleCameraInitialized=false;
    // Apply after capture navigation; Home's zoom ceiling must not discard a
    // requested battle/replay zoom before the actual HUD bounds are known.
    if(BattleView&&PendingCaptureZoom>=0){CameraZoom=PendingCaptureZoom;PendingCaptureZoom=-1;}
    CameraZoom=FMath::Clamp(CameraZoom,MinCameraZoom,MaxCameraZoom);
    const float ViewHeight=BaselineHeight*CameraZoom;
    ArenaCamera->OrthoWidth=ViewHeight*float(Width)/Height;
    FVector Location=BaselineLocation;
    if(BattleView)
    {
        const double EnvelopeCenter=(MinUp+MaxUp)*.5;
        const double DesiredCenter=EnvelopeCenter+(CameraSafeTop-CameraSafeBottom)*ViewHeight/(2.f*Height);
        Location+=Up*(DesiredCenter-FVector::DotProduct(BaselineLocation,Up));
        const double DesiredRight=(MinRight+MaxRight)*.5+(CameraSafeRight-CameraSafeLeft)*ArenaCamera->OrthoWidth/(2.f*Width);
        Location+=Right*(DesiredRight-FVector::DotProduct(Location,Right));
    }
    // Translate in camera space to centre the full field in the remaining
    // canvas, leaving both sidebars and all model-height allowances clear.
    CameraActor->SetActorLocation(Location);
}

FBox2D ARiftPlayerController::BattleSafeScreenBounds()const
{
    int32 Width=0,Height=0;GetViewportSize(Width,Height);
    return FBox2D(FVector2D(CameraSafeLeft,CameraSafeTop),FVector2D(FMath::Max(CameraSafeLeft,float(Width)-CameraSafeRight),FMath::Max(CameraSafeTop,float(Height)-CameraSafeBottom)));
}
FString ARiftPlayerController::CameraFramingDiagnosticsJSON()const
{
    auto Report=MakeShared<FJsonObject>();int32 Width=0,Height=0;GetViewportSize(Width,Height);
    const bool BattleView=Interface&&Interface->IsBattleView();
    Report->SetBoolField(TEXT("activeBattleView"),BattleView);
    Report->SetNumberField(TEXT("width"),Width);Report->SetNumberField(TEXT("height"),Height);
    Report->SetNumberField(TEXT("uiScale"),CameraUIScale);Report->SetNumberField(TEXT("zoom"),CameraZoom);
    Report->SetNumberField(TEXT("minimumZoom"),MinCameraZoom);Report->SetNumberField(TEXT("maximumZoom"),MaxCameraZoom);
    Report->SetNumberField(TEXT("orthoWidth"),ArenaCamera?ArenaCamera->OrthoWidth:0.f);
    Report->SetNumberField(TEXT("cameraElevationDegrees"),CameraActor?-CameraActor->GetActorRotation().Pitch:0.f);
    Report->SetNumberField(TEXT("safeTop"),Height>0?CameraSafeTop/Height:0.f);
    Report->SetNumberField(TEXT("safeBottom"),Height>0?(Height-CameraSafeBottom)/Height:0.f);
    Report->SetNumberField(TEXT("safeLeft"),Width>0?CameraSafeLeft/Width:0.f);
    Report->SetNumberField(TEXT("safeRight"),Width>0?(Width-CameraSafeRight)/Width:0.f);
    Report->SetNumberField(TEXT("usableWidthPixels"),FMath::Max(0.f,Width-CameraSafeLeft-CameraSafeRight));
    Report->SetNumberField(TEXT("usableHeightPixels"),FMath::Max(0.f,Height-CameraSafeTop-CameraSafeBottom));
    Report->SetNumberField(TEXT("legalFieldWidthTiles"),rift::arena::Width);Report->SetNumberField(TEXT("legalFieldHeightTiles"),rift::arena::Height);
    Report->SetNumberField(TEXT("legalFieldHalfWidthTiles"),rift::arena::HalfWidth);Report->SetNumberField(TEXT("legalFieldHalfHeightTiles"),rift::arena::HalfHeight);
    const FBox2D Safe=BattleSafeScreenBounds();
    auto InsideSafe=[&](FVector2D Screen){return Screen.X>=Safe.Min.X-1&&Screen.X<=Safe.Max.X+1&&Screen.Y>=Safe.Min.Y-1&&Screen.Y<=Safe.Max.Y+1;};
    FVector2D OriginScreen,RightScreen,DepthScreen,HeightScreen;
    const bool PitchProjected=ArenaCamera&&ProjectWorldLocationToScreen(FVector::ZeroVector,OriginScreen,false)
        &&ProjectWorldLocationToScreen(ArenaCamera->GetRightVector()*100.,RightScreen,false)
        &&ProjectWorldLocationToScreen(FVector(0,100,0),DepthScreen,false)
        &&ProjectWorldLocationToScreen(FVector(0,0,100),HeightScreen,false);
    Report->SetBoolField(TEXT("tilePitchProjected"),PitchProjected);
    if(PitchProjected)
    {
        Report->SetNumberField(TEXT("tilePitchPixels"),(RightScreen-OriginScreen).Size());
        Report->SetNumberField(TEXT("tileDepthPixels"),(DepthScreen-OriginScreen).Size());
        Report->SetNumberField(TEXT("modelHeightPixelsPerMeter"),(HeightScreen-OriginScreen).Size());
        Report->SetNumberField(TEXT("legalFieldWidthPixels"),(RightScreen-OriginScreen).Size()*rift::arena::Width);
        Report->SetNumberField(TEXT("legalFieldDepthPixels"),(DepthScreen-OriginScreen).Size()*rift::arena::Height);
        Report->SetNumberField(TEXT("arenaGroundWidthPixels"),(RightScreen-OriginScreen).Size()*RiftArenaGeometry::GroundHalfWidth*2.);
    }
    bool Passed=ArenaCamera&&Width>0&&Height>0;TArray<TSharedPtr<FJsonValue>> Corners;
    for(double X:{-double(rift::arena::HalfWidth),double(rift::arena::HalfWidth)})
        for(double Y:{-double(rift::arena::HalfHeight),double(rift::arena::HalfHeight)})
    {
        FVector2D Screen=FVector2D::ZeroVector;
        const bool Projected=ProjectWorldLocationToScreen(URiftMatchSubsystem::WorldPoint({X,Y}),Screen,false);
        const bool Inside=Projected&&InsideSafe(Screen);
        Passed=Passed&&Inside;auto Corner=MakeShared<FJsonObject>();Corner->SetNumberField(TEXT("tileX"),X);Corner->SetNumberField(TEXT("tileY"),Y);
        Corner->SetNumberField(TEXT("screenX"),Width>0?Screen.X/Width:0.);Corner->SetNumberField(TEXT("screenY"),Height>0?Screen.Y/Height:0.);
        Corner->SetBoolField(TEXT("projected"),Projected);Corner->SetBoolField(TEXT("insideSafeArea"),Inside);Corners.Add(MakeShared<FJsonValueObject>(Corner));
    }
    Report->SetArrayField(TEXT("legalFieldCorners"),Corners);
    TArray<TSharedPtr<FJsonValue>> Envelope;bool EnvelopePassed=ArenaCamera&&Width>0&&Height>0;
    ForEachBattleEnvelopePoint([&](const FVector& Point)
    {
        FVector2D Screen=FVector2D::ZeroVector;const bool Projected=ProjectWorldLocationToScreen(Point,Screen,false);
        const bool Inside=Projected&&InsideSafe(Screen);
        EnvelopePassed=EnvelopePassed&&Inside;auto Sample=MakeShared<FJsonObject>();
        Sample->SetNumberField(TEXT("worldX"),Point.X);Sample->SetNumberField(TEXT("worldY"),Point.Y);Sample->SetNumberField(TEXT("worldZ"),Point.Z);
        Sample->SetNumberField(TEXT("screenX"),Width>0?Screen.X/Width:0.);Sample->SetNumberField(TEXT("screenY"),Height>0?Screen.Y/Height:0.);
        Sample->SetBoolField(TEXT("projected"),Projected);Sample->SetBoolField(TEXT("insideSafeArea"),Inside);Envelope.Add(MakeShared<FJsonValueObject>(Sample));
    });
    Report->SetArrayField(TEXT("modelEnvelope"),Envelope);Report->SetBoolField(TEXT("modelEnvelopePassed"),EnvelopePassed);
    // Capture the actual production component bounds as well as the conservative
    // edge envelope. These are projection measurements, not gameplay extents.
    TArray<TSharedPtr<FJsonValue>> Models;bool ModelsPassed=true;int32 TroopCount=0;
    double TroopWidthSum=0,TroopHeightSum=0,TroopMinHeight=TNumericLimits<double>::Max(),TroopMaxHeight=0;
    if(const auto* State=Arena?Arena->ViewState():nullptr)
    {
        for(const auto& Entity:State->entities)
        {
            auto* Visual=Arena->Visual(Entity.id);if(Entity.dead||!Visual||Visual->IsHidden())continue;
            const FBox Bounds=Visual->GetComponentsBoundingBox(true);if(!Bounds.IsValid)continue;
            FBox2D ScreenBounds(ForceInit);bool Projected=true;
            for(int32 X=0;X<2;++X)for(int32 Y=0;Y<2;++Y)for(int32 Z=0;Z<2;++Z)
            {
                const FVector Point(X?Bounds.Max.X:Bounds.Min.X,Y?Bounds.Max.Y:Bounds.Min.Y,Z?Bounds.Max.Z:Bounds.Min.Z);
                FVector2D Screen;const bool CornerProjected=ProjectWorldLocationToScreen(Point,Screen,false);
                Projected=Projected&&CornerProjected;if(CornerProjected)ScreenBounds+=Screen;
            }
            FVector2D HealthScreen;const bool HealthProjected=ProjectWorldLocationToScreen(Visual->HealthLocation(),HealthScreen,false);
            const bool Inside=Projected&&ScreenBounds.bIsValid&&InsideSafe(ScreenBounds.Min)&&InsideSafe(ScreenBounds.Max);
            const bool HealthInside=HealthProjected&&InsideSafe(HealthScreen);ModelsPassed=ModelsPassed&&Inside&&HealthInside;
            auto Sample=MakeShared<FJsonObject>();Sample->SetStringField(TEXT("entityId"),LexToString(Entity.id));
            Sample->SetStringField(TEXT("cardId"),UTF8_TO_TCHAR(Entity.cardId.c_str()));
            Sample->SetStringField(TEXT("kind"),Entity.kind==rift::EntityKind::Troop?TEXT("troop"):Entity.kind==rift::EntityKind::Building?TEXT("building"):Entity.kind==rift::EntityKind::Guard?TEXT("guard"):TEXT("core"));
            Sample->SetNumberField(TEXT("team"),int32(Entity.team));Sample->SetBoolField(TEXT("projected"),Projected);
            Sample->SetNumberField(TEXT("widthPixels"),ScreenBounds.bIsValid?ScreenBounds.GetSize().X:0.);
            Sample->SetNumberField(TEXT("heightPixels"),ScreenBounds.bIsValid?ScreenBounds.GetSize().Y:0.);
            Sample->SetNumberField(TEXT("minXPixels"),ScreenBounds.bIsValid?ScreenBounds.Min.X:0.);
            Sample->SetNumberField(TEXT("minYPixels"),ScreenBounds.bIsValid?ScreenBounds.Min.Y:0.);
            Sample->SetNumberField(TEXT("maxXPixels"),ScreenBounds.bIsValid?ScreenBounds.Max.X:0.);
            Sample->SetNumberField(TEXT("maxYPixels"),ScreenBounds.bIsValid?ScreenBounds.Max.Y:0.);
            Sample->SetBoolField(TEXT("insideSafeArea"),Inside);Sample->SetBoolField(TEXT("healthAnchorInsideSafeArea"),HealthInside);
            Models.Add(MakeShared<FJsonValueObject>(Sample));
            if(Entity.kind==rift::EntityKind::Troop&&Projected&&ScreenBounds.bIsValid)
            {
                ++TroopCount;TroopWidthSum+=ScreenBounds.GetSize().X;TroopHeightSum+=ScreenBounds.GetSize().Y;
                TroopMinHeight=FMath::Min(TroopMinHeight,ScreenBounds.GetSize().Y);TroopMaxHeight=FMath::Max(TroopMaxHeight,ScreenBounds.GetSize().Y);
            }
        }
    }
    Report->SetArrayField(TEXT("projectedModelBounds"),Models);Report->SetBoolField(TEXT("projectedModelBoundsPassed"),ModelsPassed);
    Report->SetNumberField(TEXT("troopModelCount"),TroopCount);
    if(TroopCount>0)
    {
        Report->SetNumberField(TEXT("averageTroopWidthPixels"),TroopWidthSum/TroopCount);
        Report->SetNumberField(TEXT("averageTroopHeightPixels"),TroopHeightSum/TroopCount);
        Report->SetNumberField(TEXT("minimumTroopHeightPixels"),TroopMinHeight);
        Report->SetNumberField(TEXT("maximumTroopHeightPixels"),TroopMaxHeight);
    }
    Report->SetBoolField(TEXT("passed"),Passed&&(!BattleView||EnvelopePassed));
    FString Result;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Result));return Result;
}
void ARiftPlayerController::ZoomIn(){UpdateArenaCamera();CameraZoom=FMath::Clamp(CameraZoom-.025f*GetGameInstance()->GetSubsystem<URiftProfileSubsystem>()->Settings.CameraSpeed,MinCameraZoom,MaxCameraZoom);UpdateArenaCamera();}
void ARiftPlayerController::ZoomOut(){UpdateArenaCamera();CameraZoom=FMath::Clamp(CameraZoom+.025f*GetGameInstance()->GetSubsystem<URiftProfileSubsystem>()->Settings.CameraSpeed,MinCameraZoom,MaxCameraZoom);UpdateArenaCamera();}
