#if WITH_DEV_AUTOMATION_TESTS
#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "RiftReplaySubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include <cmath>

namespace
{
double TowerDistance(rift::Vec2 A,rift::Vec2 B){return std::hypot(A.x-B.x,A.z-B.z);}
const rift::Entity* TowerEntity(const rift::Snapshot& State,uint64 Id)
{for(const auto& Entity:State.entities)if(Entity.id==Id)return &Entity;return nullptr;}
double TowerClearance(const rift::Snapshot& State,const rift::Entity& Troop)
{
    double Minimum=1.e9;
    for(const auto& Tower:State.entities)
        if(!Tower.dead&&Tower.kind>=rift::EntityKind::Guard)
            Minimum=FMath::Min(Minimum,TowerDistance(Troop.position,Tower.position)-Tower.radius-Troop.radius-.22);
    return Minimum;
}
double TowerSegmentClearance(const rift::Snapshot& State,const rift::Entity& Troop,rift::Vec2 From)
{
    double Minimum=1.e9;const double X=Troop.position.x-From.x,Z=Troop.position.z-From.z,Length=X*X+Z*Z;
    for(const auto& Tower:State.entities)if(!Tower.dead&&Tower.kind>=rift::EntityKind::Guard)
    {
        const double T=Length>1.e-12?FMath::Clamp(((Tower.position.x-From.x)*X+(Tower.position.z-From.z)*Z)/Length,0.,1.):0.;
        const rift::Vec2 Closest{From.x+T*X,From.z+T*Z};
        Minimum=FMath::Min(Minimum,TowerDistance(Closest,Tower.position)-Tower.radius-Troop.radius-.22);
    }
    return Minimum;
}
double TowerArenaClearance(const rift::Entity& Troop)
{
    const double Edge=.40+Troop.radius*.72;
    return FMath::Min(14.-Edge-FMath::Abs(Troop.position.x),21.-Edge-FMath::Abs(Troop.position.z));
}
rift::MatchOptions TowerOptions(const std::string& First="ironclad")
{
    rift::MatchOptions Options;Options.seed=13331333;Options.aiEnabled={false,false};
    std::vector<std::string> Deck{First};
    for(const auto& Id:rift::DefaultDeck())if(Id!=First&&Deck.size()<8)Deck.push_back(Id);
    Options.decks={Deck,Deck};return Options;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftTowerPathingIntegrationTest,"Rift.Integration.TowerPathing",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftTowerPathingIntegrationTest::RunTest(const FString& Parameters)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox")))
    {AddError(TEXT("Tower pathing requires isolated RiftSaveRoot/UserDir and RiftAutomationSandbox."));return false;}

    int32 PaidCases=0,Members=0;
    // Every ground troop uses its real paid hand, unmodified body radius and
    // ordinary fixed-step movement. Each case begins with only the six towers.
    for(const auto& Card:rift::Cards())if(!Card.spell&&!Card.building&&!Card.flying)
        for(const auto Team:{rift::Team::Player,rift::Team::Enemy})for(const int32 Lane:{0,-1,1})for(const int32 RearDrop:{0,1})
        {
            rift::Match Match(TowerOptions(Card.id));const int32 T=int32(Team);
            rift::Vec2 Drop;uint64 TowerId=0;
            for(const auto& Entity:Match.State().entities)if(Entity.team==Team&&Entity.lane==Lane)
            {Drop=Entity.position;TowerId=Entity.id;break;}
            Drop.z+=RearDrop*(Team==rift::Team::Player?1.:-1.);
            const FString Label=FString::Printf(TEXT("%s team %d tower %llu rear %d"),UTF8_TO_TCHAR(Card.id.c_str()),T,TowerId,RearDrop);
            Match.SetAether(Team,10);const auto Hand=Match.State().hands[T];const auto Queue=Match.State().queues[T];
            if(!TestTrue(Label+TEXT(" accepts its legal paid drop"),Match.Play(Team,0,Drop)))continue;
            ++PaidCases;
            TestEqual(Label+TEXT(" spends the canonical cost exactly once"),Match.State().spent[T],double(Card.cost));
            TestEqual(Label+TEXT(" deducts the bank exactly once"),Match.State().aether[T],double(10-Card.cost));
            TestTrue(Label+TEXT(" cycles only the selected hand slot"),Match.State().hands[T][0]==Queue[0]&&
                Match.State().hands[T][1]==Hand[1]&&Match.State().hands[T][2]==Hand[2]&&Match.State().hands[T][3]==Hand[3]&&
                Match.State().queues[T].back()==Card.id);
            TArray<rift::Entity> Starts;for(const auto& Entity:Match.State().entities)
                if(Entity.kind==rift::EntityKind::Troop)Starts.Add(Entity);
            TestEqual(Label+TEXT(" preserves every actual card member"),Starts.Num(),Card.count);
            for(const auto& Entity:Starts)
            {++Members;TestTrue(Label+TEXT(" begins outside all tower body clearances"),TowerClearance(Match.State(),Entity)>=-1.e-6);}
            bool PathClear=true,SegmentsClear=true;auto Previous=Starts;
            for(int32 Step=0;Step<300;++Step)
            {
                Match.Step(1./60.);
                for(auto& Last:Previous)if(const auto* Entity=TowerEntity(Match.State(),Last.id))
                {
                    PathClear&=!Entity->dead&&TowerClearance(Match.State(),*Entity)>=-1.e-6;
                    SegmentsClear&=TowerSegmentClearance(Match.State(),*Entity,Last.position)>=-1.e-6;
                    Last.position=Entity->position;
                }
            }
            TestTrue(Label+TEXT(" moves around tower bodies without crossing their clearance"),PathClear);
            TestTrue(Label+TEXT(" every continuous movement segment clears the full tower circles"),SegmentsClear);
            for(const auto& Start:Starts)
            {
                const auto* Entity=TowerEntity(Match.State(),Start.id);
                TestTrue(Label+TEXT(" makes real progress within five seconds"),Entity&&!Entity->dead&&
                    TowerDistance(Entity->position,Start.position)>.5&&
                    (Start.position.z-Entity->position.z)*(Team==rift::Team::Player?1.:-1.)>.25);
            }
            int32 Plays=0,Spawns=0;const auto Tile=rift::SnapToTile(Drop);
            for(const auto& Event:Match.Events())
            {
                if(Event.type=="card_play"&&Event.cardId==Card.id)
                {++Plays;TestFalse(Label+TEXT(" remains a paid rather than sandbox event"),Event.sandbox);
                    TestTrue(Label+TEXT(" retains the selected tile in its card-play event"),TowerDistance(Event.position,Tile)<1.e-9);}
                if(Event.type=="entity_spawn"&&Event.cardId==Card.id)
                {++Spawns;const auto* Start=Starts.FindByPredicate([&](const rift::Entity& E){return E.id==Event.source;});
                    TestTrue(Label+TEXT(" records the corrected actual member position"),Start&&TowerDistance(Start->position,Event.position)<1.e-9);}
            }
            TestEqual(Label+TEXT(" emits one paid play"),Plays,1);TestEqual(Label+TEXT(" emits one spawn per member"),Spawns,Card.count);
        }
    TestEqual(TEXT("Both teams' three towers and rear-edge tiles cover all seven ground cards"),PaidCases,84);
    TestEqual(TEXT("All paid single and twin ground members are exercised"),Members,96);

    // Legal hand drops on the outer rows must also begin inside the movement
    // envelope. Continuous segment checks cannot rescue an invalid origin.
    int32 BoundaryCases=0,BoundaryMembers=0;
    const rift::Vec2 BoundaryDrops[]={{.5,20.5},{-12.5,20.5},{12.5,20.5},
        {-13.5,20.5},{13.5,20.5},{-12.5,8.5},{12.5,8.5}};
    for(const auto& Card:rift::Cards())if(!Card.spell&&!Card.building&&!Card.flying)
        for(const auto Team:{rift::Team::Player,rift::Team::Enemy})for(auto Drop:BoundaryDrops)
        {
            const int32 T=int32(Team);Drop.z*=Team==rift::Team::Player?1.:-1.;
            rift::Match Match(TowerOptions(Card.id));Match.SetAether(Team,10);
            const auto Hand=Match.State().hands[T];const auto Queue=Match.State().queues[T];
            const FString Label=FString::Printf(TEXT("Boundary %s team %d tile %.1f,%.1f"),UTF8_TO_TCHAR(Card.id.c_str()),T,Drop.x,Drop.z);
            TestTrue(Label+TEXT(" remains a legal paid placement"),Match.CanPlace(Team,Card,Drop));
            if(!TestTrue(Label+TEXT(" accepts the actual paid drop"),Match.Play(Team,0,Drop)))continue;
            ++BoundaryCases;TestEqual(Label+TEXT(" spends once"),Match.State().spent[T],double(Card.cost));
            TestEqual(Label+TEXT(" deducts once"),Match.State().aether[T],double(10-Card.cost));
            TestTrue(Label+TEXT(" cycles the selected slot once"),Match.State().hands[T][0]==Queue[0]&&
                Match.State().hands[T][1]==Hand[1]&&Match.State().hands[T][2]==Hand[2]&&Match.State().hands[T][3]==Hand[3]&&
                Match.State().queues[T].back()==Card.id);
            TArray<rift::Entity> Starts;for(const auto& Entity:Match.State().entities)if(Entity.kind==rift::EntityKind::Troop)Starts.Add(Entity);
            TestEqual(Label+TEXT(" retains all ground members"),Starts.Num(),Card.count);auto Previous=Starts;
            bool OriginClear=true,SegmentsClear=true;
            for(const auto& Start:Starts){++BoundaryMembers;OriginClear&=TowerArenaClearance(Start)>=-1.e-6&&TowerClearance(Match.State(),Start)>=-1.e-6;}
            TestTrue(Label+TEXT(" begins inside the radius-adjusted arena and outside tower bodies"),OriginClear);
            for(int32 Step=0;Step<300;++Step)
            {
                Match.Step(1./60.);
                for(auto& Last:Previous)if(const auto* Entity=TowerEntity(Match.State(),Last.id))
                {
                    // The arena rectangle is convex, so legal endpoints imply
                    // its whole segment is legal; tower circles need projection.
                    SegmentsClear&=TowerArenaClearance(Last)>=-1.e-6&&TowerArenaClearance(*Entity)>=-1.e-6&&
                        TowerSegmentClearance(Match.State(),*Entity,Last.position)>=-1.e-6;
                    Last.position=Entity->position;
                }
            }
            TestTrue(Label+TEXT(" continuous movement stays in the arena and clears towers"),SegmentsClear);
            for(const auto& Start:Starts)
            {
                const auto* Entity=TowerEntity(Match.State(),Start.id);
                TestTrue(Label+TEXT(" moves inward within five seconds rather than remaining at the rear wall"),Entity&&
                    TowerDistance(Entity->position,Start.position)>.5&&
                    (Start.position.z-Entity->position.z)*(Team==rift::Team::Player?1.:-1.)>.25);
            }
        }
    TestEqual(TEXT("Both teams' rear rows, side rows and extreme corners cover every ground card"),BoundaryCases,98);
    TestEqual(TEXT("Rear/side/corner paid deployments retain all single and twin members"),BoundaryMembers,112);

    auto* GI=NewObject<UGameInstance>(GEngine);GI->InitializeStandalone(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto* World=GI->GetWorld();auto* Replay=GI->GetSubsystem<URiftReplaySubsystem>();
    ON_SCOPE_EXIT{Replay->CloseReplay();Replay->FlushPendingWrites();World->DestroyWorld(false);GI->Shutdown();GEngine->DestroyWorldContext(World);};
    const auto Options=TowerOptions("twin_blades");rift::Match Recorded(Options);
    Replay->BeginRecording(Options,true);
    auto RecordEvents=[&](){for(const auto& Event:Recorded.DrainEvents())Replay->RecordEvent(Event);};
    RecordEvents();Replay->Sample(Recorded.State());Recorded.Step(.1);
    TArray<rift::Entity> Towers;for(const auto& Entity:Recorded.State().entities)Towers.Add(Entity);
    for(const auto Team:{rift::Team::Player,rift::Team::Enemy})
    {
        Recorded.SetAether(Team,10);
        for(const auto& Tower:Towers)if(Tower.team==Team&&Tower.kind==rift::EntityKind::Core)
            TestTrue(TEXT("Replay fixture uses a real paid Core deployment"),Recorded.Play(Team,0,Tower.position));
        for(const auto& Tower:Towers)if(Tower.team==Team&&Tower.kind==rift::EntityKind::Guard)
            TestTrue(TEXT("Replay fixture uses ordinary large-body developer deployment"),Recorded.Spawn(Team,"boulderback",Tower.position));
    }
    TArray<rift::Entity> Spawned;for(const auto& Entity:Recorded.State().entities)if(Entity.kind==rift::EntityKind::Troop)Spawned.Add(Entity);
    TestEqual(TEXT("Replay retains both paid twins and four actual large bodies"),Spawned.Num(),8);
    RecordEvents(); // No post-deployment sample: the first seek must apply entity_spawn.
    TArray<rift::Snapshot> MovementSamples;
    for(int32 Step=0;Step<180;++Step)
    {
        Recorded.Step(1./60.);RecordEvents();
        if((Step+1)%15==0){MovementSamples.Add(Recorded.State());Replay->Sample(Recorded.State());}
    }
    const auto Final=Recorded.State();Replay->EndRecording(Final,true);
    if(!TestTrue(TEXT("Tower pathing recording saves through the actual replay writer"),Replay->FlushPendingWrites())||
        !TestTrue(TEXT("Tower pathing recording opens through the actual replay reader"),Replay->OpenReplay(Replay->LatestFilename)))return false;
    auto* Host=World->GetSubsystem<URiftMatchSubsystem>();
    for(const float Time:{.1f,3.1f,.1f,3.1f})
    {
        Replay->Seek(Time);const auto* View=Host->ViewState();
        if(!TestNotNull(TEXT("Actual replay seek supplies its production view"),View))return false;
        for(const auto& Start:Spawned)
        {
            const auto* Entity=TowerEntity(*View,Start.id);const auto* Expected=Time<1?&Start:TowerEntity(Final,Start.id);
            TestTrue(TEXT("Backward and forward seeks preserve corrected positions and IDs"),Entity&&Expected&&
                Entity->cardId==Start.cardId&&Entity->team==Start.team&&TowerDistance(Entity->position,Expected->position)<1.e-5);
        }
        const double ExpectedBank=Time<1?8:Final.aether[0];
        TestTrue(TEXT("Seeking retains the actual paid bank and cycle"),View->spent[0]==2&&View->spent[1]==2&&
            FMath::IsNearlyEqual(View->aether[0],ExpectedBank,1.e-5)&&FMath::IsNearlyEqual(View->aether[1],ExpectedBank,1.e-5)&&
            View->hands[0][0]==Final.hands[0][0]&&View->hands[1][0]==Final.hands[1][0]);
    }
    for(const auto& Sample:MovementSamples)
    {
        Replay->Seek(float(Sample.elapsed));const auto* View=Host->ViewState();
        for(const auto& Start:Spawned)
        {
            const auto* Entity=View?TowerEntity(*View,Start.id):nullptr;const auto* Expected=TowerEntity(Sample,Start.id);
            TestTrue(TEXT("Real quarter-second replay samples preserve the routed movement around towers"),Entity&&Expected&&
                TowerDistance(Entity->position,Expected->position)<1.e-5);
        }
    }
    return true;
}
#endif
