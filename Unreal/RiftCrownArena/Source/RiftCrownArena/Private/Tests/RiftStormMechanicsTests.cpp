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
const rift::Entity* FindStormBody(const rift::Snapshot& State,uint64 Id) {
    for(const auto& Body:State.entities)if(Body.id==Id)return &Body;
    return nullptr;
}
TSharedRef<FJsonObject> CopyStormJSON(const TSharedRef<FJsonObject>& Object) {
    FString Text;FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Text));
    TSharedPtr<FJsonObject> Copy;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Copy);
    return Copy.ToSharedRef();
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftStormMechanicsIntegrationTest,"Rift.Integration.StormAndSpawnerReplay",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRiftStormMechanicsIntegrationTest::RunTest(const FString& Parameters) {
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox"))) {
        AddError(TEXT("Storm replay fixture requires isolated RiftSaveRoot/UserDir and RiftAutomationSandbox."));return false;
    }
    auto* GI=NewObject<UGameInstance>();auto* Replay=NewObject<URiftReplaySubsystem>(GI);
    ON_SCOPE_EXIT {Replay->CloseReplay();Replay->FlushPendingWrites();};
    rift::MatchOptions Options;Options.aiEnabled={false,false};
    Options.decks[0]={"blade_box","thunderstorm","twin_blades","ironclad","ember_archer","arc_mage","bullet_burst","nova_flask"};
    Options.decks[1]=Options.decks[0];rift::Match Match(Options);
    for(auto& Body:const_cast<rift::Snapshot&>(Match.State()).entities)Body.stunUntil=100;
    Replay->BeginRecording(Options,true);Replay->Sample(Match.State());
    auto Feed=[&](){for(const auto& Event:Match.DrainEvents())Replay->RecordEvent(Event);};
    Feed();Match.SetAether(rift::Team::Player,10);
    if(!TestTrue(TEXT("Actual paid five-Aether Box"),Match.Play(rift::Team::Player,0,{.5,9.5})))return false;
    const uint64 Box=Match.State().entities.back().id;
    TestTrue(TEXT("Independent paid Twins share definition without sharing attribution"),Match.Play(rift::Team::Player,2,{-4.5,12.5}));
    Feed();
    TArray<rift::Snapshot> Probes;
    for(int32 Tick=1;Tick<=486;++Tick) {
        if(Tick==7) {
            Match.SetAether(rift::Team::Enemy,10);
            TestTrue(TEXT("Actual paid three-Aether Thunderstorm"),Match.Play(rift::Team::Enemy,1,{.5,9.5}));
            Feed();
        }
        Match.Step(1./60.);Feed();
        if(Tick==42||Tick==120)Replay->Sample(Match.State());
        if(Tick==36||Tick==66||Tick==96||Tick==222||Tick==228||Tick==408)Probes.Add(Match.State());
    }
    Replay->EndRecording(Match.State(),true);
    if(!TestTrue(TEXT("Storm and Box archive writes"),Replay->FlushPendingWrites())||
       !TestTrue(TEXT("Storm and Box archive reopens"),Replay->OpenReplay(Replay->LatestFilename))) {
        AddError(Replay->LastError);return false;
    }
    TestEqual(TEXT("Captured mechanics schema4"),int32(Replay->ReconstructionDiagnostics()->GetNumberField(TEXT("balanceSchema"))),4);
    for(const auto& Probe:Probes) {
        Replay->Seek(float(Probe.elapsed));
        const auto* Live=FindStormBody(Probe,Box);const auto* Restored=FindStormBody(Replay->View,Box);
        if(!TestNotNull(TEXT("Live Box probe exists"),Live)||!TestNotNull(TEXT("Sparse replay Box exists"),Restored))continue;
        TestTrue(TEXT("Sparse replay retains actual Box HP"),FMath::IsNearlyEqual(Restored->hp,Live->hp,1e-6));
        TestTrue(TEXT("Sparse replay advances paused production clock"),FMath::IsNearlyEqual(Restored->spawnClock,Live->spawnClock,1e-5));
        TestEqual(TEXT("Sparse replay counts exact single periodic children"),Restored->periodicSpawns,Live->periodicSpawns);
        TestTrue(TEXT("Sparse replay restores refreshed stun expiry"),FMath::IsNearlyEqual(Restored->stunUntil,Live->stunUntil,1e-6));
        TestEqual(TEXT("Sparse replay pending wave count"),int32(Replay->View.spellCasts.size()),int32(Probe.spellCasts.size()));
        if(Replay->View.spellCasts.size()==1&&Probe.spellCasts.size()==1) {
            TestEqual(TEXT("Sparse replay exact next wave index"),Replay->View.spellCasts[0].round,Probe.spellCasts[0].round);
            TestTrue(TEXT("Sparse replay deadline from original cast"),FMath::IsNearlyEqual(Replay->View.spellCasts[0].impactAt,Probe.spellCasts[0].impactAt,1e-8));
        }
        int Children=0;
        for(const auto& Entry:Replay->View.entityTelemetry[0])if(Entry.second.parentEntityId==Box) {
            ++Children;TestEqual(TEXT("Child remains actual current Twin definition"),FString(UTF8_TO_TCHAR(Entry.second.cardId.c_str())),FString(TEXT("twin_blades")));
            TestEqual(TEXT("Child credits Box, independent paid Twins stay separate"),FString(UTF8_TO_TCHAR(Entry.second.attributionCardId.c_str())),FString(TEXT("blade_box")));
        }
        TestEqual(TEXT("Persistent child ledger follows actual births"),Children,Live->periodicSpawns);
        TestEqual(TEXT("Paid Twins keep one paid play"),Replay->View.telemetry[0].at("twin_blades").plays,uint64(1));
        TestEqual(TEXT("Box keeps one paid play"),Replay->View.telemetry[0].at("blade_box").plays,uint64(1));
    }
    Replay->Seek(6.8f);const auto Later=Replay->View;
    Replay->Seek(.6f);Replay->Seek(6.8f);
    TestEqual(TEXT("Backward/forward seek cannot duplicate children"),Replay->View.telemetry[0].at("blade_box").childSpawns,Later.telemetry[0].at("blade_box").childSpawns);
    const FString Path=FPaths::Combine(URiftProfileSubsystem::SaveRoot(),TEXT("Replays"),FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT("-storm-controls.json"));
    if(!TestTrue(TEXT("Actual compressed archive exports to JSON"),Replay->ExportReplay(Replay->LatestFilename,Path)))return false;
    FString Text,Error;FFileHelper::LoadFileToString(Text,*Path);TSharedPtr<FJsonObject> Saved;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Saved);
    if(!TestTrue(TEXT("Actual exported recording validates"),Saved.IsValid()&&Replay->ValidateRecording(Saved,Error)))return false;
    auto Reject=[&](const TCHAR* Label,const TFunction<void(TSharedRef<FJsonObject>)>& Mutate) {
        auto Broken=CopyStormJSON(Saved.ToSharedRef());Mutate(Broken);
        TestFalse(Label,Replay->ValidateRecording(Broken,Error));
    };
    auto Definition=[](TSharedRef<FJsonObject> Object,const FString& Id) {
        for(const auto& Value:Object->GetObjectField(TEXT("balance"))->GetArrayField(TEXT("cards")))
            if(Value->AsObject()->GetStringField(TEXT("id"))==Id)return Value->AsObject();
        return TSharedPtr<FJsonObject>();
    };
    Reject(TEXT("Zero periodic interval rejected"),[&](auto O){Definition(O,TEXT("blade_box"))->SetNumberField(TEXT("spawnInterval"),0);});
    Reject(TEXT("Spawner cannot release a spell as a troop"),[&](auto O){Definition(O,TEXT("blade_box"))->SetStringField(TEXT("spawnId"),TEXT("meteor_shards"));});
    for(int32 BadRound:{0,4})Reject(TEXT("Impossible pending storm round rejected"),[&](auto O){
        for(const auto& State:O->GetArrayField(TEXT("states")))for(const auto& Cast:State->AsObject()->GetArrayField(TEXT("spellCasts")))
            Cast->AsObject()->SetNumberField(TEXT("round"),BadRound);
    });
    Reject(TEXT("Pending wave must retain authored deadline"),[&](auto O){
        for(const auto& State:O->GetArrayField(TEXT("states")))for(const auto& Cast:State->AsObject()->GetArrayField(TEXT("spellCasts")))
            Cast->AsObject()->SetNumberField(TEXT("impactAt"),1.1);
    });
    Reject(TEXT("Production clock cannot exceed a cycle"),[&](auto O){
        for(const auto& State:O->GetArrayField(TEXT("states")))for(const auto& Body:State->AsObject()->GetArrayField(TEXT("entities")))
            if(Body->AsObject()->GetNumberField(TEXT("id"))==double(Box))Body->AsObject()->SetNumberField(TEXT("spawnClock"),4);
    });
    Reject(TEXT("Periodic event cannot skip its spawn ordinal"),[&](auto O){
        for(const auto& Event:O->GetArrayField(TEXT("events")))if(Event->AsObject()->GetStringField(TEXT("type"))==TEXT("building_spawn")) {
            Event->AsObject()->SetNumberField(TEXT("count"),5);break;
        }
    });
    Reject(TEXT("Child spawn cannot replace its paid parent attribution"),[&](auto O){
        for(const auto& Event:O->GetArrayField(TEXT("events")))if(Event->AsObject()->GetStringField(TEXT("reason"))==TEXT("periodic_spawn")) {
            Event->AsObject()->SetStringField(TEXT("attributionCardId"),TEXT("twin_blades"));break;
        }
    });
    Reject(TEXT("Storm impact cannot skip or repeat a wave"),[&](auto O){
        for(const auto& Event:O->GetArrayField(TEXT("events")))if(Event->AsObject()->GetStringField(TEXT("type"))==TEXT("spell_impact")) {
            Event->AsObject()->SetNumberField(TEXT("count"),2);break;
        }
    });
    Replay->CloseReplay();
    rift::Match Sparse(Options);
    for(auto& Body:const_cast<rift::Snapshot&>(Sparse.State()).entities)Body.stunUntil=100;
    Replay->BeginRecording(Options,true);Replay->Sample(Sparse.State());
    for(const auto& Event:Sparse.DrainEvents())Replay->RecordEvent(Event);
    Sparse.Step(6);Sparse.SetAether(rift::Team::Player,10);
    TestTrue(TEXT("Sparse fixture deploys Box six seconds after match start"),Sparse.Play(rift::Team::Player,0,{4.5,12.5}));
    const uint64 RetiredBox=Sparse.State().entities.back().id;
    for(const auto& Event:Sparse.DrainEvents())Replay->RecordEvent(Event);
    for(int32 Tick=0;Tick<1866;++Tick) {
        Sparse.Step(1./60.);for(const auto& Event:Sparse.DrainEvents())Replay->RecordEvent(Event);
    }
    TestNull(TEXT("Sparse final state contains no retired parent body"),FindStormBody(Sparse.State(),RetiredBox));
    Replay->EndRecording(Sparse.State(),true);
    if(TestTrue(TEXT("Sparse archive with retired nonzero-birth parent writes"),Replay->FlushPendingWrites())&&
       TestTrue(TEXT("Sparse archive recovers source birth from actual spawn event"),Replay->OpenReplay(Replay->LatestFilename))) {
        Replay->Seek(35.9f);const auto* Before=FindStormBody(Replay->View,RetiredBox);
        TestTrue(TEXT("Sparse seek preserves nine children before last release"),Before&&!Before->dead&&Before->periodicSpawns==9);
        Replay->Seek(36.1f);const auto* After=FindStormBody(Replay->View,RetiredBox);
        TestTrue(TEXT("Sparse seek releases tenth child then expires parent"),After&&After->dead&&After->periodicSpawns==10);
    }
    return true;
}
#endif
