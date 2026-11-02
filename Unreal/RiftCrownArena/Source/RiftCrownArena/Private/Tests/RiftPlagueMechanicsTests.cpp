#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "RiftProfileSubsystem.h"
#include "RiftReplaySubsystem.h"
#include "Serialization/JsonSerializer.h"

namespace {
const rift::Entity* PlagueBody(const rift::Snapshot& State,uint64 Id) {
    for(const auto& Body:State.entities)if(Body.id==Id)return &Body;
    return nullptr;
}
rift::Entity& PlagueFixtureBody(rift::Match& Match,uint64 Id) {
    for(auto& Body:const_cast<rift::Snapshot&>(Match.State()).entities)if(Body.id==Id)return Body;
    checkNoEntry();return const_cast<rift::Snapshot&>(Match.State()).entities.front();
}
TSharedRef<FJsonObject> ClonePlagueJSON(const TSharedRef<FJsonObject>& Object) {
    FString Text;FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Text));
    TSharedPtr<FJsonObject> Copy;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Copy);
    return Copy.ToSharedRef();
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftPlagueMechanicsIntegrationTest,"Rift.Integration.PlagueQueenPoisonReplay",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftPlagueMechanicsIntegrationTest::RunTest(const FString& Parameters) {
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox"))) {
        AddError(TEXT("Plague replay fixture requires isolated RiftSaveRoot/UserDir and RiftAutomationSandbox."));return false;
    }
    auto* GI=NewObject<UGameInstance>();auto* Replay=NewObject<URiftReplaySubsystem>(GI);
    ON_SCOPE_EXIT {Replay->CloseReplay();Replay->FlushPendingWrites();};
    rift::MatchOptions Options;Options.aiEnabled={false,false};
    Options.decks[0]={"plague_queen","ironclad","twin_blades","ember_archer","arc_mage","bullet_burst","nova_flask","thunderstorm"};
    Options.decks[1]=Options.decks[0];rift::Match Match(Options);
    for(auto& Body:const_cast<rift::Snapshot&>(Match.State()).entities)Body.stunUntil=100;
    Replay->BeginRecording(Options,true);Replay->Sample(Match.State());
    auto Feed=[&](){for(const auto& Event:Match.DrainEvents())Replay->RecordEvent(Event);};Feed();
    if(!TestTrue(TEXT("Actual enemy body spawned"),Match.Spawn(rift::Team::Enemy,"ironclad",{0,4})))return false;
    const auto Target=Match.State().entities.back().id;PlagueFixtureBody(Match,Target).stunUntil=100;
    Match.SetAether(rift::Team::Player,10);
    if(!TestTrue(TEXT("Actual two-Aether Queen paid"),Match.Play(rift::Team::Player,0,{0,12})))return false;
    const auto Queen=Match.State().entities.back().id;PlagueFixtureBody(Match,Queen).cooldown=0;Feed();
    Match.Step(1./60.);Feed();PlagueFixtureBody(Match,Queen).stunUntil=100;
    if(!TestEqual(TEXT("Queen launches one actual traveling arrow"),int32(Match.State().projectiles.size()),1))return false;
    while(!Match.State().projectiles.empty()) {Match.Step(1./60.);Feed();}
    const double Impact=Match.State().elapsed;
    if(!TestEqual(TEXT("Impact applies one attached poison"),int32(Match.State().poisons.size()),1))return false;
    Replay->Sample(Match.State());
    auto Snapshot=Replay->SnapshotJSON(Match.State());rift::Snapshot Restored;
    if(!TestTrue(TEXT("Live poison snapshot round-trips"),Replay->SnapshotFromJSON(Snapshot,Restored))||Restored.poisons.empty())return false;
    TestEqual(TEXT("Round-trip keeps attached victim"),Restored.poisons.front().target,Target);
    TestTrue(TEXT("Round-trip keeps first tick deadline"),FMath::IsNearlyEqual(Restored.poisons.front().nextTick,Impact+1.25,1e-8));
    TArray<rift::Snapshot> Probes;
    for(int32 Tick=1;Tick<=156;++Tick) {
        if(Tick==3) {TestTrue(TEXT("Actual Bullet Burst kills Queen before first poison tick"),Match.Spawn(rift::Team::Enemy,"bullet_burst",{0,12}));Feed();}
        Match.Step(1./60.);Feed();
        if(Tick==48)Replay->Sample(Match.State());
        if(Tick==74||Tick==75||Tick==149||Tick==150)Probes.Add(Match.State());
    }
    TestTrue(TEXT("Queen retired before poison completes"),!PlagueBody(Match.State(),Queen));
    TestTrue(TEXT("Direct plus two poison packets total332"),FMath::IsNearlyEqual(PlagueBody(Match.State(),Target)->hp,490.,1e-7));
    TestTrue(TEXT("Dead source retains120poison credit"),FMath::IsNearlyEqual(Match.State().entityTelemetry[0].at(Queen).observed.dotDamage,120.,1e-7));
    Replay->EndRecording(Match.State(),true);
    if(!TestTrue(TEXT("Poison archive writes"),Replay->FlushPendingWrites())||!TestTrue(TEXT("Poison archive opens"),Replay->OpenReplay(Replay->LatestFilename))) {AddError(Replay->LastError);return false;}
    TestEqual(TEXT("Replay captures schema5 per-card sight"),int32(Replay->ReconstructionDiagnostics()->GetNumberField(TEXT("balanceSchema"))),5);
    auto Compare=[&](const rift::Snapshot& Probe) {
        Replay->Seek(float(Probe.elapsed));const auto* Body=PlagueBody(Replay->View,Target);
        if(!TestNotNull(TEXT("Replayed poison victim present"),Body))return;
        TestTrue(TEXT("Sparse HP follows direct hit and exact ticks"),FMath::IsNearlyEqual(Body->hp,PlagueBody(Probe,Target)->hp,1e-7));
        TestEqual(TEXT("Sparse attached poison count"),int32(Replay->View.poisons.size()),int32(Probe.poisons.size()));
        if(!Probe.poisons.empty()&&!Replay->View.poisons.empty()) {
            TestEqual(TEXT("Sparse exact completed ticks"),Replay->View.poisons.front().ticks,Probe.poisons.front().ticks);
            TestTrue(TEXT("Sparse next tick uses captured interval"),FMath::IsNearlyEqual(Replay->View.poisons.front().nextTick,Probe.poisons.front().nextTick,1e-7));
            TestTrue(TEXT("Sparse expiry uses captured duration"),FMath::IsNearlyEqual(Replay->View.poisons.front().expires,Probe.poisons.front().expires,1e-7));
        }
        TestTrue(TEXT("Replay source lifetime cannot erase poison credit"),FMath::IsNearlyEqual(Replay->View.entityTelemetry[0].at(Queen).observed.dotDamage,Probe.entityTelemetry[0].at(Queen).observed.dotDamage,1e-7));
        TestEqual(TEXT("Replay paid aggregate poison tick attempts"),Replay->View.telemetry[0].at("plague_queen").dotTicks,Probe.telemetry[0].at("plague_queen").dotTicks);
    };
    for(const auto& Probe:Probes)Compare(Probe);
    Compare(Probes[0]);Compare(Probes.Last());
    const FString Path=FPaths::Combine(URiftProfileSubsystem::SaveRoot(),TEXT("Replays"),FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT("-plague-controls.json"));
    if(!TestTrue(TEXT("Real compressed archive exports"),Replay->ExportReplay(Replay->LatestFilename,Path)))return false;
    FString Text,Error;FFileHelper::LoadFileToString(Text,*Path);TSharedPtr<FJsonObject> Saved;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Saved);
    if(!TestTrue(TEXT("Actual export validates"),Saved.IsValid()&&Replay->ValidateRecording(Saved,Error)))return false;
    auto Reject=[&](const TCHAR* Label,const TFunction<void(TSharedRef<FJsonObject>)>& Mutate) {
        auto Broken=ClonePlagueJSON(Saved.ToSharedRef());Mutate(Broken);TestFalse(Label,Replay->ValidateRecording(Broken,Error));
    };
    Reject(TEXT("Negative per-card sight is rejected"),[&](auto Object){
        for(const auto& Value:Object->GetObjectField(TEXT("balance"))->GetArrayField(TEXT("cards")))if(Value->AsObject()->GetStringField(TEXT("id"))==TEXT("plague_queen"))Value->AsObject()->SetNumberField(TEXT("sightRear"),-1);
    });
    Reject(TEXT("Poison source cannot become an unrelated card"),[&](auto Object){
        for(const auto& Value:Object->GetArrayField(TEXT("states")))for(const auto& P:Value->AsObject()->GetArrayField(TEXT("poisons")))P->AsObject()->SetStringField(TEXT("cardId"),TEXT("ironclad"));
    });
    Reject(TEXT("Poison expiry cannot contradict captured2.5seconds"),[&](auto Object){
        for(const auto& Value:Object->GetArrayField(TEXT("states")))for(const auto& P:Value->AsObject()->GetArrayField(TEXT("poisons")))P->AsObject()->SetNumberField(TEXT("expires"),P->AsObject()->GetNumberField(TEXT("expires"))+.1);
    });
    Reject(TEXT("Completed poison cannot linger as active state"),[&](auto Object){
        for(const auto& Value:Object->GetArrayField(TEXT("states")))for(const auto& P:Value->AsObject()->GetArrayField(TEXT("poisons")))P->AsObject()->SetNumberField(TEXT("ticks"),2);
    });
    Reject(TEXT("Poison first pulse cannot skip its ordinal"),[&](auto Object){
        for(const auto& Value:Object->GetArrayField(TEXT("events")))if(Value->AsObject()->GetStringField(TEXT("type"))==TEXT("poison_tick")){Value->AsObject()->SetNumberField(TEXT("count"),2);break;}
    });
    Reject(TEXT("Poison cannot claim a third tick"),[&](auto Object){
        for(const auto& Value:Object->GetArrayField(TEXT("events")))if(Value->AsObject()->GetStringField(TEXT("type"))==TEXT("poison_applied")){Value->AsObject()->SetNumberField(TEXT("count"),3);break;}
    });
    // Reconstruction must also work with only initial/final samples after source retirement.
    auto Sparse=ClonePlagueJSON(Saved.ToSharedRef());const auto Samples=Sparse->GetArrayField(TEXT("states"));
    Sparse->SetArrayField(TEXT("states"),{Samples[0],Samples.Last()});
    if(!TestTrue(TEXT("Sparse poison archive remains valid"),Replay->ValidateRecording(Sparse,Error))){AddError(Error);return false;}
    const FString SparsePath=FPaths::Combine(URiftProfileSubsystem::SaveRoot(),TEXT("Replays"),FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT("-plague-sparse.json"));
    Text.Empty();FJsonSerializer::Serialize(Sparse,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*SparsePath);
    if(!TestTrue(TEXT("Sparse poison archive opens"),Replay->OpenReplay(FPaths::GetCleanFilename(SparsePath)))){AddError(Replay->LastError);return false;}
    for(const auto& Probe:Probes)Compare(Probe);Compare(Probes[0]);Compare(Probes.Last());
    Replay->CloseReplay();
    rift::Match Building(Options);
    for(auto& Body:const_cast<rift::Snapshot&>(Building.State()).entities)Body.stunUntil=100;
    Replay->BeginRecording(Options,true);Replay->Sample(Building.State());
    auto BuildingFeed=[&](){for(const auto& Event:Building.DrainEvents())Replay->RecordEvent(Event);};BuildingFeed();
    TestTrue(TEXT("Sparse retired building fixture spawns"),Building.Spawn(rift::Team::Enemy,"blade_box",{0,4}));
    const auto Box=Building.State().entities.back().id;PlagueFixtureBody(Building,Box).stunUntil=100;
    Building.SetAether(rift::Team::Player,10);TestTrue(TEXT("Sparse building fixture pays for Queen"),Building.Play(rift::Team::Player,0,{0,12}));
    const auto Shooter=Building.State().entities.back().id;PlagueFixtureBody(Building,Shooter).cooldown=0;BuildingFeed();
    Building.Step(1./60.);BuildingFeed();PlagueFixtureBody(Building,Shooter).stunUntil=100;
    while(!Building.State().projectiles.empty()){Building.Step(1./60.);BuildingFeed();}
    TestTrue(TEXT("Real Nova Flask prepares building for lethal poison"),Building.Spawn(rift::Team::Player,"nova_flask",{0,4}));BuildingFeed();
    for(int Tick=0;Tick<220;++Tick){Building.Step(1./60.);BuildingFeed();}
    TestTrue(TEXT("Poison-killed building is retired before final sample"),!PlagueBody(Building.State(),Box));
    Replay->EndRecording(Building.State(),true);
    if(!TestTrue(TEXT("Sparse building poison archive writes and validates"),Replay->FlushPendingWrites())||
       !TestTrue(TEXT("Sparse retired building poison archive reopens"),Replay->OpenReplay(Replay->LatestFilename))){AddError(Replay->LastError);return false;}
    return true;
}
#endif
