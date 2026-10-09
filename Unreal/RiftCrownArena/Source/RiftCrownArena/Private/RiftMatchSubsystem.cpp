#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "RiftReplaySubsystem.h"
#include "RiftMetaSimulationSubsystem.h"
#include "RiftDiagnostics.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void URiftMatchSubsystem::Initialize(FSubsystemCollectionBase& Collection){Super::Initialize(Collection);}
void URiftMatchSubsystem::Deinitialize()
{
    // World EndPlay saves the final replay while all GI services are available.
    // GI subsystem teardown order is not a dependency shutdown order.
    Match.reset();ReplayView=nullptr;OnChanged.Clear();OnEvent.Clear();Super::Deinitialize();
}
TStatId URiftMatchSubsystem::GetStatId()const{RETURN_QUICK_DECLARE_CYCLE_STAT(URiftMatchSubsystem,STATGROUP_Tickables);}
bool URiftMatchSubsystem::IsTickable()const{return GetWorld()&&GetWorld()->IsGameWorld()&&(Match||ReplayView);}
void URiftMatchSubsystem::StartMatch(bool Training,bool BothAI)
{
    FString CapturePath;
    if(FParse::Value(FCommandLine::Get(),TEXT("RiftCapture="),CapturePath))
        RIFT_LOG(LogRift,Log,TEXT("QA StartMatch training=%d bothAI=%d previousPhase=%s previousElapsed=%.6f"),Training,BothAI,*GetPhase(),ViewState()?ViewState()->elapsed:0.);
    auto* GI=GetWorld()->GetGameInstance();auto* Replay=GI->GetSubsystem<URiftReplaySubsystem>();
    // Starting live play ends the loaded playback session, including launches
    // from Analysis/Loadout. Otherwise Escape can still route back to its replay.
    // CloseReplay releases playback only; recordings and saved archives survive.
    if(Replay->IsPlaying())Replay->CloseReplay();
    LeaveMatch();bTraining=Training;bResultSaved=false;ReplayView=nullptr;Speed=1;ReplaySampleClock=0;
    auto* Profile=GI->GetSubsystem<URiftProfileSubsystem>();
    rift::MatchOptions Options;Options.seed=FPlatformTime::Cycles();
    for(const auto& Id:Profile->ActiveDeck())Options.decks[0].push_back(TCHAR_TO_UTF8(*Id));
    static const char* Styles[]={"beatdown","aggro","control","cycle","split","spell_cycle","counter"};
    Options.aiStyles[1]=Training&&!BothAI?"control":Styles[Options.seed%7];Options.aiEnabled={BothAI,true};Options.decks[1]=rift::BuildAIDeck(Options.aiStyles[1],Options.seed^0x2CB4U);
    Match=std::make_unique<rift::Match>(Options);
    GI->GetSubsystem<URiftMetaSimulationSubsystem>()->SetBattleActive(true);
    Replay->BeginRecording(Options,Training);
    Replay->Sample(Match->State());
    FlushEvents();OnChanged.Broadcast();
}
void URiftMatchSubsystem::LeaveMatch()
{
    if(auto* GI=GetWorld()?GetWorld()->GetGameInstance():nullptr)
    {
        if(auto* Replay=GI->GetSubsystem<URiftReplaySubsystem>();Replay&&Match)Replay->EndRecording(Match->State(),Match->State().phase!=rift::Phase::Finished);
        if(auto* Meta=GI->GetSubsystem<URiftMetaSimulationSubsystem>())Meta->SetBattleActive(false);
    }
    Match.reset();ReplayView=nullptr;OnChanged.Broadcast();
}
void URiftMatchSubsystem::Tick(float DeltaTime)
{
    auto* GI=GetWorld()->GetGameInstance();auto* Replay=GI->GetSubsystem<URiftReplaySubsystem>();
    if(ReplayView){Replay->Advance(FMath::Min(DeltaTime,.25f));return;}
    if(!Match)return;
    if(Match->State().phase==rift::Phase::Finished){FinalizeResult();return;}
    if(Speed<=0)return;
    const double Dt=FMath::Min(double(DeltaTime),.25)*Speed;Match->Step(Dt);FlushEvents();
    ReplaySampleClock+=Dt;if(ReplaySampleClock>=.25){Replay->Sample(Match->State());ReplaySampleClock=FMath::Fmod(ReplaySampleClock,.25);}
    FinalizeResult();
}
void URiftMatchSubsystem::FinalizeResult()
{
    if(Match&&Match->State().phase==rift::Phase::Finished&&!bResultSaved)
    {
        auto* GI=GetWorld()->GetGameInstance();auto* Replay=GI->GetSubsystem<URiftReplaySubsystem>();
        bResultSaved=true;Replay->EndRecording(Match->State(),false);
        GI->GetSubsystem<URiftProfileSubsystem>()->RecordResult(Match->State().winner,Match->State().crowns[0]);
        GI->GetSubsystem<URiftMetaSimulationSubsystem>()->SetBattleActive(false);OnChanged.Broadcast();
    }
}
void URiftMatchSubsystem::FlushEvents()
{
    if(!Match)return;auto* Replay=GetWorld()->GetGameInstance()->GetSubsystem<URiftReplaySubsystem>();
    bool Mutation=false;
    for(const auto& E:Match->DrainEvents())
    {
        Replay->RecordEvent(E);OnEvent.Broadcast(E);
        Mutation|=E.type=="card_play"||E.type=="aether_grant"||E.type=="tower_edit"||E.type=="clear_field"||E.type=="phase"||(E.type=="ai_decision"&&(E.reason.rfind("AI ENABLED:",0)==0||E.reason.rfind("AI DISABLED:",0)==0||E.reason.rfind("AI STYLE:",0)==0));
    }
    if(Mutation)Replay->Sample(Match->State());
    FinalizeResult();
}
bool URiftMatchSubsystem::PlayCard(int32 Index,FVector2D Tile)
{if(!Match||ReplayView)return false;SampleBeforeMutation();bool Result=Match->Play(rift::Team::Player,Index,{Tile.X,Tile.Y});FlushEvents();return Result;}
void URiftMatchSubsystem::SampleBeforeMutation()
{if(Match&&!ReplayView)GetWorld()->GetGameInstance()->GetSubsystem<URiftReplaySubsystem>()->Sample(Match->State());}
void URiftMatchSubsystem::SetSpeed(float Value){static const float Values[]={0,.25f,.5f,1,2,4};for(float V:Values)if(FMath::IsNearlyEqual(V,Value)){Speed=V;return;}}
const rift::Snapshot* URiftMatchSubsystem::ViewState()const{return ReplayView?ReplayView:(Match?&Match->State():nullptr);}
void URiftMatchSubsystem::SetReplayView(const rift::Snapshot* State){ReplayView=State;OnChanged.Broadcast();}
float URiftMatchSubsystem::GetAether(int32 Team)const{auto* S=ViewState();return S?S->aether[FMath::Clamp(Team,0,1)]:0;}
float URiftMatchSubsystem::GetTimeRemaining()const{auto* S=ViewState();return S?S->timeRemaining:180;}
FString URiftMatchSubsystem::GetPhase()const{auto* S=ViewState();return S?UTF8_TO_TCHAR(rift::PhaseName(S->phase).c_str()):TEXT("HOME");}
bool URiftMatchSubsystem::CanPlace(int32 Index,FVector2D Tile)const
{
    if(!Match||Index<0||Index>3)return false;const auto* Card=rift::FindCard(Match->State().hands[0][Index]);
    return Card&&Match->State().aether[0]>=Card->cost&&Match->CanPlace(rift::Team::Player,*Card,{Tile.X,Tile.Y});
}
bool URiftCombatSubsystem::CanTarget(rift::EntityId Source,rift::EntityId Target)const
{
    auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();const auto* S=M->ViewState();if(!S||!M->Simulation())return false;
    const rift::Entity* A=nullptr;const rift::Entity* B=nullptr;for(const auto& E:S->entities){if(E.id==Source)A=&E;if(E.id==Target)B=&E;}return A&&B&&M->Simulation()->CanTarget(*A,*B);
}
TArray<FVector> URiftPathfindingSubsystem::Route(rift::EntityId Source,rift::EntityId Target)const
{
    TArray<FVector> Points;auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();const auto* S=M->ViewState();if(!S||!M->Simulation())return Points;
    const rift::Entity* A=nullptr;const rift::Entity* B=nullptr;for(const auto& E:S->entities){if(E.id==Source)A=&E;if(E.id==Target)B=&E;}
    if(A&&B)for(auto P:M->Simulation()->FindPath(*A,*B,A->bridge))Points.Add(URiftMatchSubsystem::WorldPoint(P));return Points;
}
void URiftAISubsystem::SetEnabled(int32 Team,bool Enabled){auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();if(M->Simulation()){M->SampleBeforeMutation();M->Simulation()->SetAIEnabled(rift::Team(FMath::Clamp(Team,0,1)),Enabled);M->FlushEvents();}}
bool URiftAISubsystem::SetStyle(int32 Team,const FString& Style){auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();if(!M->Simulation())return false;M->SampleBeforeMutation();bool Result=M->Simulation()->SetAIStyle(rift::Team(FMath::Clamp(Team,0,1)),TCHAR_TO_UTF8(*Style));M->FlushEvents();return Result;}
FString URiftAISubsystem::Readout(int32 Team)const
{
    const auto* State=GetWorld()->GetSubsystem<URiftMatchSubsystem>()->ViewState();if(!State)return TEXT("No active match");
    const int32 Index=FMath::Clamp(Team,0,1);const auto& AI=State->ai[Index];
    FString Cycle;const int32 Start=FMath::Max(0,int32(AI.observedCycle.size())-5);
    for(int32 I=Start;I<int32(AI.observedCycle.size());++I)
    {
        if(!Cycle.IsEmpty())Cycle+=TEXT(" → ");
        const auto* Card=rift::FindCard(AI.observedCycle[I]);Cycle+=UTF8_TO_TCHAR((Card?Card->name:AI.observedCycle[I]).c_str());
    }
    if(Cycle.IsEmpty())Cycle=TEXT("No cards observed");
    FString Style=UTF8_TO_TCHAR(AI.style.c_str());Style.ReplaceInline(TEXT("_"),TEXT(" "));
    return FString::Printf(TEXT("%s · %s\n%s\nAether %.1f · opponent estimate %.1f\nPhase: %s\nObserved cycle: %s"),*Style.ToUpper(),UTF8_TO_TCHAR(AI.decision.c_str()),UTF8_TO_TCHAR(AI.reason.c_str()),State->aether[Index],AI.estimatedOpponentAether,UTF8_TO_TCHAR(AI.phase.c_str()),*Cycle);
}
void URiftDeveloperSubsystem::ChangeAether(int32 Team,float Delta,bool Maximum){auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();if(M->Simulation()){M->SampleBeforeMutation();int32 T=FMath::Clamp(Team,0,1);M->Simulation()->SetAether(rift::Team(T),Maximum?10:M->Simulation()->State().aether[T]+Delta);M->FlushEvents();}}
bool URiftDeveloperSubsystem::SpawnCard(int32 Team,const FString& Card,FVector2D Tile){auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();if(!M->Simulation())return false;M->SampleBeforeMutation();bool R=M->Simulation()->Spawn(rift::Team(FMath::Clamp(Team,0,1)),TCHAR_TO_UTF8(*Card),{Tile.X,Tile.Y});M->FlushEvents();return R;}
bool URiftDeveloperSubsystem::SetTowerHP(int64 Tower,float HP){auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();if(!M->Simulation())return false;M->SampleBeforeMutation();bool R=M->Simulation()->SetTowerHP(Tower,HP);M->FlushEvents();return R;}
void URiftDeveloperSubsystem::ClearBattlefield(){auto* M=GetWorld()->GetSubsystem<URiftMatchSubsystem>();if(M->Simulation()){M->SampleBeforeMutation();M->Simulation()->ClearField();M->FlushEvents();}}
