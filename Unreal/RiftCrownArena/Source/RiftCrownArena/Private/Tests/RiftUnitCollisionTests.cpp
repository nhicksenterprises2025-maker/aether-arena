#if WITH_DEV_AUTOMATION_TESTS
#include "Simulation/RiftSimulation.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include <algorithm>
#include <cmath>

namespace
{
struct FCollisionObservation
{
    uint64 PairChecks=0,SweepChecks=0;
    double MinimumGap=1.e9,MinimumSweep=1.e9;
    bool Clear=true,SweptClear=true,SpeedClear=true;
};
const rift::Entity* CollisionEntity(const rift::Snapshot& State,uint64 Id)
{for(const auto& Entity:State.entities)if(Entity.id==Id)return &Entity;return nullptr;}
bool CollisionPair(const rift::Entity& A,const rift::Entity& B)
{
    if(A.dead||B.dead||A.id==B.id)return false;
    const bool AirA=A.kind==rift::EntityKind::Troop&&A.flying,AirB=B.kind==rift::EntityKind::Troop&&B.flying;
    return AirA==AirB;
}
double CollisionDistance(rift::Vec2 A,rift::Vec2 B){return std::hypot(A.x-B.x,A.z-B.z);}
void ObserveCollision(const rift::Snapshot& State,FCollisionObservation& Result,const std::vector<rift::Entity>* Previous=nullptr)
{
    if(Previous)for(const auto& Start:*Previous)if(!Start.dead&&Start.kind==rift::EntityKind::Troop)
        if(const auto* End=CollisionEntity(State,Start.id))if(const auto* Card=rift::FindCard(Start.cardId))
            Result.SpeedClear&=CollisionDistance(Start.position,End->position)<=Card->moveSpeed/60.+1.e-6;
    for(size_t I=0;I<State.entities.size();++I)for(size_t J=I+1;J<State.entities.size();++J)
    {
        const auto& A=State.entities[I];const auto& B=State.entities[J];if(!CollisionPair(A,B))continue;
        const double Gap=CollisionDistance(A.position,B.position)-A.radius-B.radius;
        const double Required=A.kind!=rift::EntityKind::Troop||B.kind!=rift::EntityKind::Troop?.22:.02;
        Result.MinimumGap=std::min(Result.MinimumGap,Gap);Result.Clear&=Gap>=Required-1.e-8;++Result.PairChecks;
        if(!Previous)continue;
        const rift::Entity *OldA=nullptr,*OldB=nullptr;
        for(const auto& Entity:*Previous){if(Entity.id==A.id)OldA=&Entity;if(Entity.id==B.id)OldB=&Entity;}
        if(!OldA||!OldB||!CollisionPair(*OldA,*OldB))continue;
        const rift::Vec2 From{OldA->position.x-OldB->position.x,OldA->position.z-OldB->position.z},To{A.position.x-B.position.x,A.position.z-B.position.z};
        const double DX=To.x-From.x,DZ=To.z-From.z,Length=DX*DX+DZ*DZ;
        const double T=Length>0?std::clamp(-(From.x*DX+From.z*DZ)/Length,0.,1.):0.;
        const double Sweep=std::hypot(From.x+T*DX,From.z+T*DZ)-A.radius-B.radius;
        Result.MinimumSweep=std::min(Result.MinimumSweep,Sweep);Result.SweptClear&=Sweep>=Required-1.e-8;++Result.SweepChecks;
    }
}
rift::MatchOptions CollisionDeck(const std::string& First,const std::string& Second)
{
    rift::MatchOptions Options;Options.seed=13551355;Options.aiEnabled={false,false};
    std::vector<std::string> Deck{First};if(Second!=First)Deck.push_back(Second);
    for(const auto& Card:rift::Cards())if(Card.id!=First&&Card.id!=Second&&Deck.size()<8)Deck.push_back(Card.id);
    Options.decks={Deck,Deck};return Options;
}
std::vector<rift::Entity> CollisionPaid(FAutomationTestBase& Test,rift::Match& Match,rift::Team Team,const std::string& Id,rift::Vec2 Drop)
{
    const int Side=int(Team);const FString Label=FString::Printf(TEXT("Collision %s team %d"),UTF8_TO_TCHAR(Id.c_str()),Side);
    const auto* Card=rift::FindCard(Id);int Index=-1;
    for(int N=0;N<4;++N)if(Match.State().hands[Side][N]==Id)Index=N;
    std::vector<rift::Entity> Members;if(!Test.TestTrue(Label+TEXT(" is actually in the hand"),Index>=0))return Members;
    const auto Hand=Match.State().hands[Side];const auto Queue=Match.State().queues[Side];const double Spent=Match.State().spent[Side];
    const auto First=Match.Events().size();Match.SetAether(Team,10);
    if(!Test.TestTrue(Label+TEXT(" accepts a real paid deployment"),Match.Play(Team,Index,Drop)))return Members;
    Test.TestEqual(Label+TEXT(" deducts canonical cost"),Match.State().aether[Side],double(10-Card->cost));
    Test.TestEqual(Label+TEXT(" spends canonical cost once"),Match.State().spent[Side],Spent+Card->cost);
    bool CorrectHand=Match.State().queues[Side].back()==Id;
    for(int N=0;N<4;++N)CorrectHand&=Match.State().hands[Side][N]==(N==Index?Queue[0]:Hand[N]);
    Test.TestTrue(Label+TEXT(" cycles only the selected slot once"),CorrectHand);
    int Plays=0;
    for(size_t N=First;N<Match.Events().size();++N)
    {
        const auto& Event=Match.Events()[N];
        if(Event.type=="card_play"){++Plays;Test.TestTrue(Label+TEXT(" preserves paid event and selected tile"),!Event.sandbox&&Event.cardId==Id&&CollisionDistance(Event.position,rift::SnapToTile(Drop))<1.e-9);}
        if(Event.type=="entity_spawn")if(const auto* Entity=CollisionEntity(Match.State(),Event.source))
        {
            Test.TestTrue(Label+TEXT(" retains member identity and resolved spawn position"),!Event.sandbox&&Entity->cardId==Id&&Entity->team==Team&&Entity->playId==Event.playId&&CollisionDistance(Entity->position,Event.position)<1.e-9);
            Test.TestEqual(Label+TEXT(" preserves authored HP"),Entity->hp,Card->hp);
            Test.TestTrue(Label+TEXT(" preserves authored radius"),FMath::Abs(Entity->radius-(Card->building?Card->footprint*.52:Card->scale*.44))<1.e-12);
            Members.push_back(*Entity);
        }
    }
    Test.TestEqual(Label+TEXT(" emits exactly one paid play"),Plays,1);
    Test.TestEqual(Label+TEXT(" emits every member exactly once"),int(Members.size()),Card->spell?0:Card->count);
    return Members;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftUnitCollisionIntegrationTest,"Rift.Integration.UnitCollision",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftUnitCollisionIntegrationTest::RunTest(const FString& Parameters)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox")))
    {AddError(TEXT("Unit collision requires the isolated RiftAutomationSandbox runner."));return false;}
    int Cases=0,PaidPlays=0,Members=0;FCollisionObservation Total;
    for(const auto& Card:rift::Cards())if(!Card.spell)for(const auto Team:{rift::Team::Player,rift::Team::Enemy})
    {
        const std::string Blocker=Card.flying?(Card.id=="sky_manta"?"vampire_bats":"sky_manta"):(Card.id=="ironclad"?"boulderback":"ironclad");
        rift::Match Match(CollisionDeck(Card.id,Blocker));const rift::Vec2 Drop{7.5,(Team==rift::Team::Player?1.:-1.)*8.5};
        const auto First=CollisionPaid(*this,Match,Team,Card.id,Drop),Second=CollisionPaid(*this,Match,Team,Blocker,Drop);
        ++Cases;PaidPlays+=2;Members+=int(First.size()+Second.size());FCollisionObservation Observed;
        ObserveCollision(Match.State(),Observed);
        for(int Step=0;Step<120;++Step){const auto Before=Match.State().entities;Match.Step(1./60.);ObserveCollision(Match.State(),Observed,&Before);}
        const FString Label=FString::Printf(TEXT("All-card %s team %d"),UTF8_TO_TCHAR(Card.id.c_str()),int(Team));
        TestTrue(Label+TEXT(" all same-layer bodies remain separated throughout movement"),Observed.Clear);
        TestTrue(Label+TEXT(" continuous relative trajectories never tunnel or exceed authored speed"),Observed.SweptClear&&Observed.SpeedClear);
        for(const auto& Start:First)if(const auto* End=CollisionEntity(Match.State(),Start.id))
            TestTrue(Label+TEXT(" has real movement or a stable fixed building"),Start.kind==rift::EntityKind::Troop?(!End->dead&&CollisionDistance(Start.position,End->position)>.5):CollisionDistance(Start.position,End->position)<1.e-9);
        Total.PairChecks+=Observed.PairChecks;Total.SweepChecks+=Observed.SweepChecks;Total.MinimumGap=std::min(Total.MinimumGap,Observed.MinimumGap);Total.MinimumSweep=std::min(Total.MinimumSweep,Observed.MinimumSweep);
    }
    TestEqual(TEXT("Every physical card covers both teams"),Cases,26);TestEqual(TEXT("Paid physical card casts"),PaidPlays,52);TestEqual(TEXT("Every actual paid physical member retained"),Members,106);
    const std::vector<std::string> PocketDeck{"ironclad","twin_blades","boulderback","archer_tower","sky_manta","vampire_bats","storm_raven","frost_fang"};
    const int PocketSlots[]{1,2,2,3,0,1,0,1,0,1,2,3,2,3,2,3};
    for(const auto Team:{rift::Team::Player,rift::Team::Enemy})for(const int Lane:{-1,1})
    {
        auto Options=CollisionDeck("ironclad","twin_blades");Options.seed=135;Options.decks[int(Team)]=PocketDeck;
        rift::Match Pocket(Options);const int Sign=Team==rift::Team::Player?1:-1,Deadline=Team==rift::Team::Player?12:15;
        std::vector<rift::Entity> PaidMembers,Ground,Buildings;int AdjustedDrops=0;
        FCollisionObservation Observed;
        for(int Play=0;Play<16;++Play)
        {
            rift::Vec2 Drop{Lane*7.5,Sign*8.5};if(Play==9)Drop={Lane*6.5,Sign*7.5};if(Play==15)Drop={Lane*8.5,Sign*7.5};
            const auto Requested=Drop;const auto Id=Pocket.State().hands[int(Team)][PocketSlots[Play]];const auto* Card=rift::FindCard(Id);
            if(Card->building&&!Pocket.CanPlace(Team,*Card,Drop))
            {
                bool Legal=false;
                for(int Ring=1;Ring<=8&&!Legal;++Ring)for(int Direction=0;Direction<8&&!Legal;++Direction)
                {
                    const double Angle=Direction*3.14159265358979323846*.25;
                    const auto Candidate=rift::SnapToTile({Requested.x+Ring*std::cos(Angle),Requested.z+Ring*std::sin(Angle)});
                    if(Pocket.CanPlace(Team,*Card,Candidate)){Drop=Candidate;Legal=true;}
                }
                TestTrue(TEXT("Paid building pocket mirror finds a real legal requested building tile"),Legal);++AdjustedDrops;
                AddInfo(FString::Printf(TEXT("Collision building requested tile team %d lane %d play %d: %.1f,%.1f -> %.1f,%.1f."),int(Team),Lane,Play,Requested.x,Requested.z,Drop.x,Drop.z));
            }
            const auto New=CollisionPaid(*this,Pocket,Team,Id,Drop);
            for(const auto& Entity:New)
            {PaidMembers.push_back(Entity);if(Entity.kind==rift::EntityKind::Troop&&!Entity.flying)Ground.push_back(Entity);if(Entity.kind==rift::EntityKind::Building)Buildings.push_back(Entity);}
            ObserveCollision(Pocket.State(),Observed);
        }
        TestEqual(TEXT("Sixteen paid pocket casts retain every actual physical body"),int(PaidMembers.size()),31);
        TestEqual(TEXT("Paid building pocket retains all eleven mobile ground members"),int(Ground.size()),11);
        TestEqual(TEXT("Paid pocket contains three real building blockers"),int(Buildings.size()),3);
        TestEqual(TEXT("Pocket keeps the original building lifetime"),rift::FindCard("archer_tower")->lifetime,25.);
        if(Team==rift::Team::Player&&Lane==1)TestEqual(TEXT("Original Shipping pocket retains every exact requested drop tile"),AdjustedDrops,0);
        std::map<uint64,bool> Crossed;int CrossedAt12=0;bool CorrectBridge=true,FixedLiveBuildings=true,StrictSpeed=true;
        for(int Step=0;Step<Deadline*60;++Step)
        {
            const auto Before=Pocket.State().entities;Pocket.Step(1./60.);ObserveCollision(Pocket.State(),Observed,&Before);
            for(const auto& Start:Before)if(!Start.dead&&Start.kind==rift::EntityKind::Troop)
                if(const auto* End=CollisionEntity(Pocket.State(),Start.id))
                    StrictSpeed&=CollisionDistance(Start.position,End->position)<=rift::FindCard(Start.cardId)->moveSpeed/60.+1.e-8;
            for(const auto& Start:Buildings)
            {const auto* End=CollisionEntity(Pocket.State(),Start.id);FixedLiveBuildings&=End&&!End->dead&&End->hp>0&&CollisionDistance(Start.position,End->position)<1.e-9;}
            for(const auto& Start:Ground)if(const auto* End=CollisionEntity(Pocket.State(),Start.id))
            {if(End->position.z*Sign<-rift::arena::RiverHalfWidth-.28)Crossed[End->id]=true;
             if(!End->dead&&FMath::Abs(End->position.z)<rift::arena::RiverHalfWidth+.28)CorrectBridge&=End->position.x*Lane>0;}
            if(Step==719)CrossedAt12=int(Crossed.size());
        }
        TestTrue(TEXT("Paid building pocket preserves strict endpoint/swept separation and authored speed"),Observed.Clear&&Observed.SweptClear&&Observed.SpeedClear&&StrictSpeed);
        TestTrue(TEXT("All three twenty-five-second paid buildings remain live and stationary through the progress deadline"),FixedLiveBuildings);
        TestTrue(TEXT("Every ground member exits the live building pocket and clears the selected bridge by its authored deadline"),Ground.size()==11&&Crossed.size()==Ground.size()&&CorrectBridge);
        Total.PairChecks+=Observed.PairChecks;Total.SweepChecks+=Observed.SweepChecks;Total.MinimumGap=std::min(Total.MinimumGap,Observed.MinimumGap);Total.MinimumSweep=std::min(Total.MinimumSweep,Observed.MinimumSweep);
        AddInfo(FString::Printf(TEXT("Collision paid building pocket team %d lane %d: %d of 11 ground crossed, %d crossed at 12s; 16 paid casts, 31 members, 3 live fixed buildings, deadline %ds, %d adjusted building requests."),int(Team),Lane,int(Crossed.size()),CrossedAt12,Deadline,AdjustedDrops));
    }
    for(const auto Team:{rift::Team::Player,rift::Team::Enemy})
    {
        rift::Match Match(CollisionDeck("ironclad","sky_manta"));const rift::Vec2 Drop{.5,(Team==rift::Team::Player?1.:-1.)*8.5};
        const auto Ground=CollisionPaid(*this,Match,Team,"ironclad",Drop),Air=CollisionPaid(*this,Match,Team,"sky_manta",Drop);
        if(Ground.size()==1&&Air.size()==1)TestTrue(TEXT("Air may overlap the ground layer"),CollisionDistance(Ground[0].position,Air[0].position)<1.e-9);
        for(const auto& Spell:rift::Cards())if(Spell.spell)
        {
            rift::Match Cast(CollisionDeck(Spell.id,"ironclad"));const auto Troop=CollisionPaid(*this,Cast,Team,"ironclad",Drop);const auto Count=Cast.State().entities.size();
            CollisionPaid(*this,Cast,Team,Spell.id,Drop);TestEqual(TEXT("Spells never create collision bodies"),Cast.State().entities.size(),Count);
            if(Troop.size()==1)if(const auto* End=CollisionEntity(Cast.State(),Troop[0].id))TestTrue(TEXT("A spell cast does not move friendly troops"),CollisionDistance(End->position,Troop[0].position)<1.e-9);
        }
    }
    for(const int Lane:{-1,1})
    {
        rift::Match Melee(CollisionDeck("twin_blades","ironclad"));
        for(const auto Team:{rift::Team::Player,rift::Team::Enemy})CollisionPaid(*this,Melee,Team,"twin_blades",{Lane*7.5,(Team==rift::Team::Player?1.:-1.)*2.5});
        FCollisionObservation Contact;
        for(int Step=0;Step<480;++Step){const auto Before=Melee.State().entities;Melee.Step(1./60.);ObserveCollision(Melee.State(),Contact,&Before);}
        int Hits=0;for(const auto& Event:Melee.Events())Hits+=Event.type=="damage"&&Event.cardId=="twin_blades"&&Event.targetKind==rift::EntityKind::Troop;
        TestTrue(TEXT("Opposing paid melee members touch without overlapping or tunneling"),Contact.Clear&&Contact.SweptClear&&Contact.SpeedClear);
        TestTrue(TEXT("Collision still permits ordinary reciprocal melee damage"),Hits>=4);
        Total.PairChecks+=Contact.PairChecks;Total.SweepChecks+=Contact.SweepChecks;Total.MinimumGap=std::min(Total.MinimumGap,Contact.MinimumGap);Total.MinimumSweep=std::min(Total.MinimumSweep,Contact.MinimumSweep);
        rift::Match Match(CollisionDeck("rambeast","twin_blades"));std::vector<rift::Entity> Starts;
        for(const auto Team:{rift::Team::Player,rift::Team::Enemy})
        {const auto Troops=CollisionPaid(*this,Match,Team,"rambeast",{Lane*7.5,(Team==rift::Team::Player?1.:-1.)*2.5});Starts.insert(Starts.end(),Troops.begin(),Troops.end());}
        FCollisionObservation Observed;bool Crossed[2]={false,false},CorrectBridge=true;
        for(int Step=0;Step<600;++Step)
        {
            const auto Before=Match.State().entities;Match.Step(1./60.);ObserveCollision(Match.State(),Observed,&Before);
            for(const auto& Start:Starts)if(const auto* End=CollisionEntity(Match.State(),Start.id))
            {Crossed[int(Start.team)]|=End->position.z*(Start.team==rift::Team::Player?1.:-1.)<-rift::arena::RiverHalfWidth-.3;
             if(!End->dead&&FMath::Abs(End->position.z)<rift::arena::RiverHalfWidth+.28)CorrectBridge&=End->position.x*Lane>0;}
        }
        TestTrue(TEXT("Opposing structure runners remain solid and do not tunnel"),Observed.Clear&&Observed.SweptClear&&Observed.SpeedClear);
        TestTrue(TEXT("Both structure runners clear the same bridge within ten seconds"),Crossed[0]&&Crossed[1]&&CorrectBridge);
        Total.PairChecks+=Observed.PairChecks;Total.SweepChecks+=Observed.SweepChecks;Total.MinimumGap=std::min(Total.MinimumGap,Observed.MinimumGap);Total.MinimumSweep=std::min(Total.MinimumSweep,Observed.MinimumSweep);
    }
    const std::vector<std::string> Deck{"boulderback","twin_blades","ironclad","rambeast","frost_fang","ember_archer","arc_mage","bullet_burst"};
    for(const auto Team:{rift::Team::Player,rift::Team::Enemy})for(const int Lane:{-1,1})
    {
        auto Options=CollisionDeck("boulderback","twin_blades");Options.decks={Deck,Deck};rift::Match Match(Options);const int Sign=Team==rift::Team::Player?1:-1;
        std::vector<rift::Entity> Starts;FCollisionObservation Observed;
        for(int Cycle=0;Cycle<3;++Cycle)for(const auto& Id:Deck)
        {const auto New=CollisionPaid(*this,Match,Team,Id,Id=="bullet_burst"?rift::Vec2{-Lane*13.5,-Sign*20.5}:rift::Vec2{Lane*7.5,Sign*6.5});Starts.insert(Starts.end(),New.begin(),New.end());ObserveCollision(Match.State(),Observed);}
        TestEqual(TEXT("Dense crowd retains all twenty-four actual paid troop members"),int(Starts.size()),24);std::map<uint64,bool> Crossed;bool CorrectBridge=true;
        for(int Step=0;Step<900;++Step)
        {
            const auto Before=Match.State().entities;Match.Step(1./60.);ObserveCollision(Match.State(),Observed,&Before);
            for(const auto& Start:Starts)if(const auto* End=CollisionEntity(Match.State(),Start.id))
            {if(End->position.z*Sign<-rift::arena::RiverHalfWidth-.28)Crossed[End->id]=true;
             if(!End->dead&&FMath::Abs(End->position.z)<rift::arena::RiverHalfWidth+.28)CorrectBridge&=End->position.x*Lane>0;}
        }
        TestTrue(TEXT("Dense friendly queue retains body separation and continuous contact"),Observed.Clear&&Observed.SweptClear&&Observed.SpeedClear);
        TestTrue(TEXT("At least eighteen paid crowd members clear their chosen bridge within fifteen seconds"),Crossed.size()>=18&&CorrectBridge);
        Total.PairChecks+=Observed.PairChecks;Total.SweepChecks+=Observed.SweepChecks;Total.MinimumGap=std::min(Total.MinimumGap,Observed.MinimumGap);Total.MinimumSweep=std::min(Total.MinimumSweep,Observed.MinimumSweep);
        AddInfo(FString::Printf(TEXT("Collision paid queue team %d lane %d: %d of 24 crossed."),int(Team),Lane,int(Crossed.size())));
    }
    for(const auto Team:{rift::Team::Player,rift::Team::Enemy})
    {
        const int Sign=Team==rift::Team::Player?1:-1;const auto Opponent=Team==rift::Team::Player?rift::Team::Enemy:rift::Team::Player;
        auto Options=CollisionDeck("ember_archer","ironclad");Options.decks={std::vector<std::string>{"ember_archer","ironclad","archer_tower","boulderback","arc_mage","rambeast","frost_fang","bullet_burst"},std::vector<std::string>{"ember_archer","ironclad","archer_tower","boulderback","arc_mage","rambeast","frost_fang","bullet_burst"}};
        rift::Match Stationary(Options);uint64 Guard=0;for(const auto& Entity:Stationary.State().entities)if(Entity.team==Opponent&&Entity.kind==rift::EntityKind::Guard&&Entity.lane==-1)Guard=Entity.id;
        TestTrue(TEXT("Real Guard destruction exposes stationary attack fixture pocket"),Stationary.SetTowerHP(Guard,0));
        CollisionPaid(*this,Stationary,Opponent,"archer_tower",{-7.5,-Sign*8.5});
        const auto Archer=CollisionPaid(*this,Stationary,Team,"ember_archer",{-7.5,-Sign*4.5}),Melee=CollisionPaid(*this,Stationary,Team,"ironclad",{-7.5,-Sign*2.5});
        FCollisionObservation Observed;bool Fixed=true,RoutedPast=false;double Closest=1.e9;
        for(int Step=0;Step<150;++Step)
        {
            const auto Before=Stationary.State().entities;Stationary.Step(1./60.);ObserveCollision(Stationary.State(),Observed,&Before);
            if(Archer.size()==1&&Melee.size()==1)
            {
                const auto* A=CollisionEntity(Stationary.State(),Archer[0].id);const auto* B=CollisionEntity(Stationary.State(),Melee[0].id);
                if(A&&B&&!A->dead&&!B->dead){Fixed&=CollisionDistance(A->position,Archer[0].position)<1.e-9;Closest=std::min(Closest,CollisionDistance(A->position,B->position));RoutedPast|=(B->position.z-A->position.z)*Sign<-.25;}
            }
        }
        int Shots=0;if(Archer.size()==1)for(const auto& Event:Stationary.Events())Shots+=Event.type=="attack"&&Event.source==Archer[0].id;
        TestTrue(TEXT("An attacking stationary body stays solid and is not forcibly moved"),Observed.Clear&&Observed.SweptClear&&Observed.SpeedClear&&Fixed&&Shots>=2);
        TestTrue(TEXT("A paid melee follower approaches and passes the stationary ranged unit"),Closest<1.3&&RoutedPast);
        Total.PairChecks+=Observed.PairChecks;Total.SweepChecks+=Observed.SweepChecks;Total.MinimumGap=std::min(Total.MinimumGap,Observed.MinimumGap);Total.MinimumSweep=std::min(Total.MinimumSweep,Observed.MinimumSweep);

        Options.decks={std::vector<std::string>{"boulderback","ironclad","storm_raven","twin_blades","arc_mage","rambeast","frost_fang","bullet_burst"},std::vector<std::string>{"boulderback","ironclad","storm_raven","twin_blades","arc_mage","rambeast","frost_fang","bullet_burst"}};
        rift::Match Stunned(Options);CollisionPaid(*this,Stunned,Opponent,"storm_raven",{7.5,-Sign*6.5});
        const auto Front=CollisionPaid(*this,Stunned,Team,"boulderback",{7.5,Sign*2.5}),Follower=CollisionPaid(*this,Stunned,Team,"ironclad",{7.5,Sign*6.5});
        FCollisionObservation StunContact;int StoppedFrames=0,IndependentFrames=0;bool StunImmobile=true;
        for(int Step=0;Step<240;++Step)
        {
            const auto Before=Stunned.State().entities;const rift::Entity* Prior=Front.size()==1?CollisionEntity(Stunned.State(),Front[0].id):nullptr;
            const rift::Entity OldFront=Prior?*Prior:rift::Entity{};
            Stunned.Step(1./60.);ObserveCollision(Stunned.State(),StunContact,&Before);
            if(Front.size()==1&&Follower.size()==1)
            {
                const auto* A=CollisionEntity(Stunned.State(),Front[0].id);const auto* B=CollisionEntity(Stunned.State(),Follower[0].id);
                if(A&&B&&!A->dead&&OldFront.stunUntil>Stunned.State().elapsed+1.e-9)
                {++StoppedFrames;StunImmobile&=CollisionDistance(A->position,OldFront.position)<1.e-9;IndependentFrames+=!B->dead&&B->stunUntil<Stunned.State().elapsed&&CollisionDistance(A->position,B->position)<1.5;}
            }
        }
        TestTrue(TEXT("A real Raven aura leaves stunned bodies solid and immobile"),StunContact.Clear&&StunContact.SweptClear&&StunContact.SpeedClear&&StunImmobile);
        TestTrue(TEXT("The real stunned blocker is exercised beside an unstunned paid follower"),StoppedFrames>=12&&IndependentFrames>=6);
        Total.PairChecks+=StunContact.PairChecks;Total.SweepChecks+=StunContact.SweepChecks;Total.MinimumGap=std::min(Total.MinimumGap,StunContact.MinimumGap);Total.MinimumSweep=std::min(Total.MinimumSweep,StunContact.MinimumSweep);
    }
    for(const auto Team:{rift::Team::Player,rift::Team::Enemy})
    {
        rift::Match Full(CollisionDeck("boulderback","sky_manta"));int Buildings=0,Residuals=0,Rejected=0;bool Exhausted=false;
        const rift::Vec2 Drop{7.5,(Team==rift::Team::Player?1.:-1.)*8.5};
        for(double Z=-21.5;Z<=21.5;Z+=2.)for(double X=-13.5;X<=14.5;X+=2.)
            if(Full.Spawn(Team,"archer_tower",{X,Z}))++Buildings;else ++Rejected;
        for(int N=0;N<100;++N){if(!Full.Spawn(Team,"boulderback",Drop)){++Rejected;Exhausted=true;break;}++Residuals;}
        TestTrue(TEXT("Actual large and small sandbox bodies exhaust bounded physical capacity"),Buildings>=300&&Residuals>0&&Exhausted);
        FCollisionObservation Capacity;ObserveCollision(Full.State(),Capacity);
        TestTrue(TEXT("Full-capacity fixture uses genuinely separated unmodified bodies"),Capacity.Clear);
        Full.SetAether(Team,10);
        const auto Before=Full.State();const auto EventCount=Full.Events().size();const uint64 LastSequence=Full.Events().back().sequence;
        uint64 LastEntity=0,LastPlay=0;for(const auto& Entity:Full.State().entities)LastEntity=std::max(LastEntity,Entity.id);
        for(const auto& Event:Full.Events())LastPlay=std::max(LastPlay,Event.playId);
        TestTrue(TEXT("Rejected capacity attempt is an otherwise legal paid zone"),Full.CanPlace(Team,*rift::FindCard("boulderback"),Drop));
        TestFalse(TEXT("A fully occupied physical layer rejects a paid card"),Full.Play(Team,0,Drop));
        TestTrue(TEXT("Capacity rejection preserves bank, spend, hand, queue, events and RNG"),
            Full.State().aether==Before.aether&&Full.State().spent==Before.spent&&Full.State().hands==Before.hands&&
            Full.State().queues==Before.queues&&Full.State().randomState==Before.randomState&&Full.Events().size()==EventCount&&Full.State().entities.size()==Before.entities.size());
        bool Unchanged=Full.State().telemetry[int(Team)].size()==Before.telemetry[int(Team)].size();
        for(const auto& Start:Before.entities)if(const auto* End=CollisionEntity(Full.State(),Start.id))Unchanged&=Start.hp==End->hp&&CollisionDistance(Start.position,End->position)==0.;else Unchanged=false;
        for(const auto& Entry:Before.telemetry[int(Team)])
        {const auto Found=Full.State().telemetry[int(Team)].find(Entry.first);Unchanged&=Found!=Full.State().telemetry[int(Team)].end();if(Found!=Full.State().telemetry[int(Team)].end())Unchanged&=Found->second.plays==Entry.second.plays&&Found->second.spawns==Entry.second.spawns&&Found->second.spent==Entry.second.spent;}
        TestTrue(TEXT("Capacity rejection preserves every existing body and telemetry card count"),Unchanged);
        const auto Air=CollisionPaid(*this,Full,Team,"sky_manta",Drop);
        TestTrue(TEXT("A legal air follow-up establishes unconsumed entity and play IDs"),Air.size()==1&&Air[0].id==LastEntity+1&&Air[0].playId==LastPlay+1);
        TestTrue(TEXT("Capacity rejection consumes no event sequence ID"),Full.Events().size()>EventCount+1&&Full.Events()[EventCount].sequence==LastSequence+1&&Full.Events()[EventCount+1].sequence==LastSequence+2&&Full.Events()[EventCount+1].type=="card_play");
        ObserveCollision(Full.State(),Capacity);TestTrue(TEXT("Air-layer follow-up leaves the full ground layer separated"),Capacity.Clear);
        Total.PairChecks+=Capacity.PairChecks;Total.MinimumGap=std::min(Total.MinimumGap,Capacity.MinimumGap);
        AddInfo(FString::Printf(TEXT("Collision capacity team %d: %d actual buildings, %d residual troops, %d rejected sandbox requests; paid rejection and air follow-up checked."),int(Team),Buildings,Residuals,Rejected));
    }
    AddInfo(FString::Printf(TEXT("Independent native collision checks: %llu endpoint pairs, %llu relative sweeps; minimum gaps %.9f / %.9f tiles."),Total.PairChecks,Total.SweepChecks,Total.MinimumGap,Total.MinimumSweep));
    return !HasAnyErrors();
}
#endif
