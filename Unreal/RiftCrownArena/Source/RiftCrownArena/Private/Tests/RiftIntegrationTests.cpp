#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "HAL/PlatformFileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RiftCardData.h"
#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "RiftReplaySubsystem.h"
#include "Serialization/JsonSerializer.h"
#include <limits>

namespace {
constexpr EAutomationTestFlags Flags =
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
TSharedPtr<FJsonObject> Json(const FString &Text) {
    TSharedPtr<FJsonObject> O;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), O);
    return O;
}
FString Serialize(const TSharedRef<FJsonObject> &O) {
    FString Text;
    FJsonSerializer::Serialize(O, TJsonWriterFactory<>::Create(&Text));
    return Text;
}
TSharedRef<FJsonObject> Clone(const TSharedRef<FJsonObject> &O) {
    return Json(Serialize(O)).ToSharedRef();
}
FString Fixture(const TCHAR *Name) {
    return FPaths::Combine(URiftProfileSubsystem::SaveRoot(), TEXT("AutomationFixtures"),
                           FGuid::NewGuid().ToString(EGuidFormats::Digits) + Name);
}
bool Sandbox() {
    return FParse::Param(FCommandLine::Get(), TEXT("RiftAutomationSandbox"));
}
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftProfilePersistenceTest, "Rift.Integration.ProfilePersistence", Flags)
bool FRiftProfilePersistenceTest::RunTest(const FString &Parameters) {
    if (!Sandbox()) {
        AddError(
            TEXT("Use -RiftAutomationSandbox and -RiftSaveRoot=<isolated directory> for persistence tests."));
        return false;
    }
    auto *GI = NewObject<UGameInstance>();
    auto *P = NewObject<URiftProfileSubsystem>(GI);
    P->Defaults();
    FString Error;
    const FString File = Fixture(TEXT("-atomic.json"));
    TestTrue(TEXT("First atomic write"), URiftProfileSubsystem::AtomicWrite(File, TEXT("first"), Error));
    TestTrue(TEXT("Second atomic replacement"),
             URiftProfileSubsystem::AtomicWrite(File, TEXT("second"), Error));
    FString Read;
    FFileHelper::LoadFileToString(Read, *File);
    TestEqual(TEXT("Committed contents"), Read, FString(TEXT("second")));
    FFileHelper::LoadFileToString(Read, *(File + TEXT(".bak")));
    TestEqual(TEXT("Previous version backup"), Read, FString(TEXT("first")));
    TestTrue(TEXT("Abandoned staging files do not block next write"),
             URiftProfileSubsystem::AtomicWrite(File, TEXT("third"), Error));
    const FString Browser = Fixture(TEXT("-browser.json"));
    auto Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("extensionRoot"), TEXT("preserved"));
    auto Profile = MakeShared<FJsonObject>();
    Profile->SetStringField(TEXT("username"), TEXT("  Noah<>   Arena  "));
    Profile->SetStringField(TEXT("playerId"), TEXT("RC-EXISTING"));
    Profile->SetNumberField(TEXT("wins"), 7);
    Profile->SetNumberField(TEXT("losses"), 3);
    Profile->SetNumberField(TEXT("draws"), 2);
    Profile->SetNumberField(TEXT("crowns"), 16);
    Profile->SetStringField(TEXT("extensionProfile"), TEXT("preserved"));
    Root->SetObjectField(TEXT("profile"), Profile);
    auto Decks = MakeShared<FJsonObject>();
    Decks->SetStringField(TEXT("activePresetId"), TEXT("deck-2"));
    Decks->SetStringField(TEXT("extensionDecks"), TEXT("preserved"));
    TArray<TSharedPtr<FJsonValue>> Records;
    for (int I = 0; I < 2; ++I) {
        auto Deck = MakeShared<FJsonObject>();
        Deck->SetStringField(TEXT("id"), TEXT("deck-2"));
        Deck->SetStringField(TEXT("name"),
                             I == 0 ? TEXT(" First <deck> ") : TEXT("Duplicate must not replace"));
        Deck->SetStringField(TEXT("extensionPreset"), TEXT("preserved"));
        TArray<TSharedPtr<FJsonValue>> Cards;
        for (const auto &Id : URiftProfileSubsystem::DefaultDeck())
            Cards.Add(MakeShared<FJsonValueString>(Id));
        Deck->SetArrayField(TEXT("cards"), Cards);
        Records.Add(MakeShared<FJsonValueObject>(Deck));
    }
    Records.Add(MakeShared<FJsonValueNumber>(42));
    Decks->SetArrayField(TEXT("presets"), Records);
    Root->SetObjectField(TEXT("deckPresets"), Decks);
    auto Settings = MakeShared<FJsonObject>();
    Settings->SetStringField(TEXT("extensionSettings"), TEXT("preserved"));
    Root->SetObjectField(TEXT("settings"), Settings);
    TestTrue(TEXT("Write browser fixture"),
             URiftProfileSubsystem::AtomicWrite(Browser, Serialize(Root), Error));
    TestTrue(TEXT("Browser migration"), P->LoadFile(Browser, true));
    TestEqual(TEXT("Sanitized name"), P->Username, FString(TEXT("Noah Arena")));
    TestEqual(TEXT("Existing player ID"), P->PlayerId, FString(TEXT("RC-EXISTING")));
    TestEqual(TEXT("Missing matches derive history"), P->Matches, 12);
    TestEqual(TEXT("First duplicate preset wins"), P->Presets[1].Name, FString(TEXT("First deck")));
    TestEqual(TEXT("Five presets retained"), P->Presets.Num(), 5);
    TestTrue(TEXT("Original checksum captured"), !P->ImportedChecksum.IsEmpty());
    P->Settings.MasterVolume = std::numeric_limits<float>::quiet_NaN();
    P->Settings.Width = -10;
    P->Settings.AA = 99;
    TestTrue(TEXT("Migrated native save"), P->Save());
    const FString Native = FPaths::Combine(URiftProfileSubsystem::SaveRoot(), TEXT("ue_save.json"));
    FFileHelper::LoadFileToString(Read, *Native);
    auto Saved = Json(Read);
    TestNotNull(TEXT("Native JSON"), Saved.Get());
    if (Saved) {
        TestEqual(TEXT("Unknown root field preserved"), Saved->GetStringField(TEXT("extensionRoot")),
                  FString(TEXT("preserved")));
        TestEqual(TEXT("Unknown profile field preserved"),
                  Saved->GetObjectField(TEXT("profile"))->GetStringField(TEXT("extensionProfile")),
                  FString(TEXT("preserved")));
        TestEqual(TEXT("Unknown settings field preserved"),
                  Saved->GetObjectField(TEXT("settings"))->GetStringField(TEXT("extensionSettings")),
                  FString(TEXT("preserved")));
    }
    auto *Reload = NewObject<URiftProfileSubsystem>(GI);
    Reload->Defaults();
    TestTrue(TEXT("Reload native settings"), Reload->LoadFile(Native, false));
    TestEqual(TEXT("Nonfinite volume normalized"), Reload->Settings.MasterVolume, .8f);
    TestEqual(TEXT("Resolution normalized"), Reload->Settings.Width, 800);
    TestEqual(TEXT("Scalability normalized"), Reload->Settings.AA, 3);
    TestEqual(TEXT("History persists"), Reload->Wins, 7);
    const FString Broken = Fixture(TEXT("-broken.json"));
    URiftProfileSubsystem::AtomicWrite(Broken, TEXT("{broken"), Error);
    TestFalse(TEXT("Corrupt JSON rejected"), Reload->LoadFile(Broken, false));
    TestEqual(TEXT("Corrupt load preserves current history"), Reload->Wins, 7);
    const FString Future = Fixture(TEXT("-future.json"));
    URiftProfileSubsystem::AtomicWrite(
        Future, TEXT("{\"schemaVersion\":99,\"profile\":{\"username\":\"future\"}}"), Error);
    TestFalse(TEXT("Future schema rejected"), Reload->LoadFile(Future, false));
    TestFalse(TEXT("Future schema blocks edits"), Reload->SetName(TEXT("Overwrite")));
    TestFalse(TEXT("Future schema blocks saves"), Reload->Save());
    TestEqual(TEXT("Future read cannot mutate current name"), Reload->Username, FString(TEXT("Noah Arena")));
    // Exercise every reflected setting spelling through the actual production converter.
    P->Settings.MusicVolume = .21f;
    P->Settings.SFXVolume = .31f;
    P->Settings.UIVolume = .41f;
    P->Settings.UIScale = 1.25f;
    P->Settings.VSync = false;
    TestTrue(TEXT("Settings save"), P->Save());
    Reload->Defaults();
    TestTrue(TEXT("Settings reload"), Reload->LoadFile(Native, false));
    TestEqual(TEXT("SFX acronym spelling roundtrip"), Reload->Settings.SFXVolume, .31f);
    TestEqual(TEXT("UI acronym spelling roundtrip"), Reload->Settings.UIVolume, .41f);
    TestEqual(TEXT("UI scale roundtrip"), Reload->Settings.UIScale, 1.25f);
    TestFalse(TEXT("VSync roundtrip"), Reload->Settings.VSync);
    FFileHelper::LoadFileToString(Read, *Native);
    TestTrue(TEXT("Prepare known good recovery backup"),
             FFileHelper::SaveStringToFile(Read, *(Native + TEXT(".bak"))));
    TestTrue(TEXT("Corrupt primary fixture"), FFileHelper::SaveStringToFile(TEXT("{truncated"), *Native));
    Reload->Defaults();
    Reload->LoadStoredProfile();
    TestEqual(TEXT("Corrupt primary recovers backup history"), Reload->Wins, 7);
    FString Backup;
    FFileHelper::LoadFileToString(Backup, *(Native + TEXT(".bak")));
    TestEqual(TEXT("Recovery keeps known-good backup intact"), Backup, Read);
    TArray<FString> Quarantined;
    IFileManager::Get().FindFiles(
        Quarantined,
        *FPaths::Combine(URiftProfileSubsystem::SaveRoot(), TEXT("Backups/ue_save.corrupt-*.json")), true,
        false);
    TestTrue(TEXT("Corrupt original is quarantined"), !Quarantined.IsEmpty());
    FFileHelper::SaveStringToFile(TEXT("{broken-primary"), *Native);
    const FString FutureBackup = TEXT("{\"schemaVersion\":99,\"profile\":{\"username\":\"future\"}}");
    FFileHelper::SaveStringToFile(FutureBackup, *(Native + TEXT(".bak")));
    Reload->Defaults();
    Reload->LoadStoredProfile();
    TestTrue(TEXT("Future backup prevents fallback overwrite"), Reload->bReadOnlyFutureSchema);
    FFileHelper::LoadFileToString(Backup, *(Native + TEXT(".bak")));
    TestEqual(TEXT("Future backup preserved byte-for-byte"), Backup, FutureBackup);
    FFileHelper::LoadFileToString(Backup, *Native);
    TestEqual(TEXT("Future recovery leaves original primary untouched"), Backup,
              FString(TEXT("{broken-primary")));
    // Restore a valid isolated test save for other automation tests and subsequent shutdown.
    TestTrue(TEXT("Restore isolated profile fixture"), P->Save());
    const FString Unicode = Fixture(TEXT("-unicode.json"));
    const FString UnicodeName = TEXT("Noah \u00E9\u540D\u5B57");
    auto UnicodeRoot = MakeShared<FJsonObject>();
    UnicodeRoot->SetNumberField(TEXT("schemaVersion"), 1);
    auto UnicodeProfile = MakeShared<FJsonObject>();
    UnicodeProfile->SetStringField(TEXT("username"), UnicodeName);
    UnicodeRoot->SetObjectField(TEXT("profile"), UnicodeProfile);
    URiftProfileSubsystem::AtomicWrite(Unicode, Serialize(UnicodeRoot), Error);
    Reload->Defaults();
    TestTrue(TEXT("Unicode profile import"), Reload->LoadFile(Unicode, false));
    TestEqual(TEXT("Existing Unicode username preserved"), Reload->Username, UnicodeName);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftSnapshotRoundtripTest, "Rift.Integration.SnapshotRoundtrip", Flags)
bool FRiftSnapshotRoundtripTest::RunTest(const FString &Parameters) {
    rift::MatchOptions Options;
    Options.aiEnabled = {false, false};
    rift::Match Match(Options);
    Match.Spawn(rift::Team::Player, "frost_fang", {1, 5});
    Match.Spawn(rift::Team::Enemy, "ironclad", {1, 4});
    Match.Step(.4);
    auto State = Match.State();
    auto &E = State.entities.back();
    E.forcedTarget = 1;
    E.forcedUntil = 4;
    E.slowSource = 88;
    E.stunSource = 99;
    E.slowTrackedFrom = .1;
    E.stunTrackedFrom = .2;
    E.pathTarget = {4, 7};
    E.stuckPosition = {2, 3};
    E.stuckClock = .3;
    E.stuckDistance = 11;
    E.scanClock = .2;
    E.repathClock = .1;
    E.stuckTime = .4;
    E.path = {{1, 2}, {3, 4}};
    State.ai[0].phase = "support";
    State.ai[0].think = .21;
    State.ai[0].lastPunish = -3;
    State.ai[0].lastSpell = 4;
    State.ai[0].anchor = 7;
    State.ai[0].supports = 2;
    State.ai[0].observedCycle = {"ironclad", "frost_fang"};
    auto &T = State.telemetry[0]["meteor_shards"];
    T.openingEligible = 1;
    T.plays = 2;
    T.spawns = 5;
    T.buildingLifetime = 4;
    T.buildingCapacity = 25;
    T.slowTime = .5;
    T.slowTrackedSeconds = 3;
    T.stunTime = .2;
    T.stunTrackedSeconds = 2;
    T.zoneOccupancy = 6;
    T.zoneSeconds = 5;
    T.placementZ = -8;
    T.auraDamage = 82;
    T.dotTicks = 5;
    rift::Projectile P;
    P.id = 99;
    P.source = 1;
    P.target = 2;
    P.playId = 7;
    P.cardId = "arc_mage";
    P.damage = 88;
    P.splash = 1.2;
    P.duration = .7;
    P.remaining = .3;
    P.origin = {1, 2};
    State.projectiles.push_back(P);
    rift::Hazard H;
    H.playId = 9;
    H.cardId = "meteor_shards";
    H.radius = 2.7;
    H.born = .2;
    H.nextTick = 1.2;
    H.expires = 5.2;
    H.ticks = 2;
    H.position = {-3, 4};
    State.hazards.push_back(H);
    auto Original = URiftReplaySubsystem::SnapshotJSON(State);
    rift::Snapshot Parsed;
    TestTrue(TEXT("Full snapshot parses"), URiftReplaySubsystem::SnapshotFromJSON(Original, Parsed));
    TestEqual(TEXT("All snapshot fields roundtrip"), Serialize(URiftReplaySubsystem::SnapshotJSON(Parsed)),
              Serialize(Original));
    TArray<TSharedRef<FJsonObject>> Invalid;
    auto Bad = Clone(Original);
    Bad->SetArrayField(TEXT("teams"), {MakeShared<FJsonValueNumber>(1), MakeShared<FJsonValueNumber>(2)});
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->SetArrayField(TEXT("entities"), {MakeShared<FJsonValueString>(TEXT("wrong type"))});
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->SetNumberField(TEXT("phase"), 99);
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->SetNumberField(TEXT("time"), std::numeric_limits<double>::infinity());
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->SetArrayField(TEXT("hazards"), {MakeShared<FJsonValueBoolean>(false)});
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->SetArrayField(TEXT("projectiles"), {MakeShared<FJsonValueNumber>(42)});
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->GetArrayField(TEXT("entities"))[0]->AsObject()->SetNumberField(TEXT("hp"), -1);
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->GetArrayField(TEXT("entities"))[0]->AsObject()->SetNumberField(TEXT("id"), 1e30);
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->GetArrayField(TEXT("teams"))[0]->AsObject()->SetArrayField(TEXT("hand"),
                                                                    {MakeShared<FJsonValueBoolean>(false)});
    Invalid.Add(Bad);
    Bad = Clone(Original);
    Bad->GetArrayField(TEXT("teams"))[0]->AsObject()->SetStringField(TEXT("telemetry"), TEXT("malformed"));
    Invalid.Add(Bad);
    for (int32 I = 0; I < Invalid.Num(); ++I) {
        rift::Snapshot Unchanged;
        Unchanged.seed = 777;
        TestFalse(FString::Printf(TEXT("Malformed case %d rejected without assertions"), I),
                  URiftReplaySubsystem::SnapshotFromJSON(Invalid[I], Unchanged));
        TestEqual(TEXT("Failed parse leaves destination unchanged"), Unchanged.seed, uint32(777));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftReplayIntegrationTest, "Rift.Integration.ReplayTimeline", Flags)
bool FRiftReplayIntegrationTest::RunTest(const FString &Parameters) {
    if (!Sandbox()) {
        AddError(TEXT("Use an isolated -RiftSaveRoot and -RiftAutomationSandbox."));
        return false;
    }
    auto *GI = NewObject<UGameInstance>();
    auto *R = NewObject<URiftReplaySubsystem>(GI);
    rift::MatchOptions O;
    O.aiEnabled = {false, false};
    rift::Match M(O);
    R->BeginRecording(O, true);
    R->Sample(M.State());
    for (const auto &E : M.DrainEvents())
        R->RecordEvent(E);
    M.Step(.1);
    M.Spawn(rift::Team::Enemy, "vampire_bats", {1, 3});
    M.Spawn(rift::Team::Player, "meteor_shards", {1, 3});
    M.SetAether(rift::Team::Player, 10);
    const auto Tower = M.State().entities[1].id;
    M.SetTowerHP(Tower, 1234);
    for (const auto &E : M.DrainEvents())
        R->RecordEvent(E);
    M.Step(.15);
    for (const auto &E : M.DrainEvents())
        R->RecordEvent(E);
    R->EndRecording(M.State(), true);
    TestTrue(TEXT("Recorded replay saved"),
             IFileManager::Get().FileExists(
                 *FPaths::Combine(URiftProfileSubsystem::SaveRoot(), TEXT("Replays"), R->LatestFilename)));
    TestTrue(TEXT("Open validated replay"), R->OpenReplay(R->LatestFilename));
    R->Seek(.05f);
    TestEqual(TEXT("No future entities before deploy"), int32(R->View.entities.size()), 6);
    R->Seek(.1f);
    TestEqual(TEXT("DEV bank mutation at timestamp"), R->View.aether[0], 10.0);
    int32 DeadBats = 0;
    double TowerHP = 0;
    for (const auto &E : R->View.entities) {
        if (E.cardId == "vampire_bats" && E.dead)
            ++DeadBats;
        if (E.id == Tower)
            TowerHP = E.hp;
    }
    TestEqual(TEXT("All five immediate spell deaths reconstructed"), DeadBats, 5);
    TestEqual(TEXT("Tower edit reconstructed"), TowerHP, 1234.0);
    TestEqual(TEXT("Meteor zone reconstructed"), int32(R->View.hazards.size()), 1);
    R->Seek(0);
    TestEqual(TEXT("Backward seek restores initial state"), int32(R->View.entities.size()), 6);
    R->Advance(.11f);
    TestTrue(TEXT("Forward playback consumes chronological event cursor"), R->EventCursor > 1);
    const int32 Cursor = R->EventCursor;
    R->SetSpeed(0);
    R->Advance(.1f);
    TestEqual(TEXT("Paused playback does not replay effects"), R->EventCursor, Cursor);
    const FString Valid =
        FPaths::Combine(URiftProfileSubsystem::SaveRoot(), TEXT("Replays"), R->LatestFilename);
    FString Text;
    FFileHelper::LoadFileToString(Text, *Valid);
    auto Broken = Json(Text);
    Broken->SetArrayField(TEXT("states"), {MakeShared<FJsonValueBoolean>(false)});
    FString Error;
    TestFalse(TEXT("Invalid recording snapshots rejected"),
              URiftReplaySubsystem::ValidateRecording(Broken, Error));
    const FString Imported = Fixture(TEXT("-invalid-replay.json"));
    URiftProfileSubsystem::AtomicWrite(Imported, Serialize(Broken.ToSharedRef()), Error);
    TestFalse(TEXT("Malformed import rejected before archive write"), R->ImportReplay(Imported));
    TestTrue(TEXT("Invalid import preserves active replay"), R->IsPlaying());
    Broken = Json(Text);
    Broken->SetNumberField(TEXT("formatVersion"), 2);
    TestFalse(TEXT("Future replay version rejected"), URiftReplaySubsystem::ValidateRecording(Broken, Error));
    Broken = Json(Text);
    Broken->GetArrayField(TEXT("states"))[0]->AsObject()->SetNumberField(TEXT("time"), .01);
    TestFalse(TEXT("Missing initial state rejected"), URiftReplaySubsystem::ValidateRecording(Broken, Error));
    Broken = Json(Text);
    Broken->GetArrayField(TEXT("states"))[0]->AsObject()->SetNumberField(TEXT("eventSequence"), 999999);
    TestFalse(TEXT("Future event bookmark rejected"), URiftReplaySubsystem::ValidateRecording(Broken, Error));
    Broken = Json(Text);
    Broken->GetObjectField(TEXT("result"))->SetNumberField(TEXT("time"), .01);
    TestFalse(TEXT("Mismatched analysis duration rejected"),
              URiftReplaySubsystem::ValidateRecording(Broken, Error));
    R->CloseReplay();
    TestFalse(TEXT("Close clears replay state"), R->IsPlaying());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftPausedResultIntegrationTest,
                                 "Rift.Integration.PausedResultAndReplayEvents", Flags)
bool FRiftPausedResultIntegrationTest::RunTest(const FString &Parameters) {
    if (!Sandbox()) {
        AddError(TEXT("Use an isolated -RiftSaveRoot and -RiftAutomationSandbox."));
        return false;
    }
    auto *GI = NewObject<UGameInstance>(GEngine);
    GI->InitializeStandalone(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto *World = GI->GetWorld();
    auto *Match = World->GetSubsystem<URiftMatchSubsystem>();
    auto *Developer = World->GetSubsystem<URiftDeveloperSubsystem>();
    auto *Profile = GI->GetSubsystem<URiftProfileSubsystem>();
    auto *Replay = GI->GetSubsystem<URiftReplaySubsystem>();
    const int32 InitialMatches = Profile->Matches, InitialWins = Profile->Wins;
    Match->StartMatch(true, false);
    Match->SetSpeed(0);
    Match->Simulation()->Step(.2);
    Match->FlushEvents();
    uint64 Core = 0;
    for (const auto &E : Match->Simulation()->State().entities)
        if (E.kind == rift::EntityKind::Core && E.team == rift::Team::Enemy)
            Core = E.id;
    TestTrue(TEXT("Paused DEV Core edit"), Developer->SetTowerHP(Core, 0));
    Match->Tick(.016f);
    TestEqual(TEXT("Paused terminal state records one result"), Profile->Matches, InitialMatches + 1);
    TestEqual(TEXT("Paused terminal win saved"), Profile->Wins, InitialWins + 1);
    TestTrue(TEXT("Paused terminal replay saved"), !Replay->LatestFilename.IsEmpty());
    Match->Tick(.016f);
    TestEqual(TEXT("Repeated finished tick cannot duplicate result"), Profile->Matches, InitialMatches + 1);
    const FString Filename = Replay->LatestFilename;
    std::vector<uint64> CrossedEvents;
    const auto Handle =
        Match->OnEvent.AddLambda([&](const rift::Event &E) { CrossedEvents.push_back(E.sequence); });
    TestTrue(TEXT("World replay opens"), Replay->OpenReplay(Filename));
    Replay->Seek(.15f);
    TestTrue(TEXT("Seeking emits no historical presentation events"), CrossedEvents.empty());
    Replay->Advance(.1f);
    TestTrue(TEXT("Forward playback broadcasts crossed terminal events"), CrossedEvents.size() >= 4);
    TestTrue(TEXT("Presentation events remain chronological"),
             std::is_sorted(CrossedEvents.begin(), CrossedEvents.end()));
    const auto Count = CrossedEvents.size();
    Replay->Seek(0);
    TestEqual(TEXT("Backward seek stays silent"), int32(CrossedEvents.size()), int32(Count));
    Replay->CloseReplay();
    Match->OnEvent.Remove(Handle);
    Match->LeaveMatch();
    World->DestroyWorld(false);
    GI->Shutdown();
    GEngine->DestroyWorldContext(World);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftCardDataIntegrationTest, "Rift.Integration.CardData", Flags)
bool FRiftCardDataIntegrationTest::RunTest(const FString &Parameters) {
    for (const auto &C : rift::Cards()) {
        const FString Id = UTF8_TO_TCHAR(C.id.c_str());
        auto *D = LoadObject<URiftCardData>(nullptr,
                                            *FString::Printf(TEXT("/Game/Rift/Cards/DA_%s.DA_%s"), *Id, *Id));
        TestNotNull(Id + TEXT(" DataAsset imported"), D);
        if (!D)
            continue;
        TestEqual(Id + TEXT(" canonical ID"), D->CardId, Id);
        TestEqual(Id + TEXT(" cost"), D->Cost, C.cost);
        TestEqual(Id + TEXT(" deployment count"), D->Count, C.count);
#define METRIC(UE, Core) TestTrue(Id + TEXT(" " #UE), FMath::IsNearlyEqual(double(D->UE), C.Core, 1e-5));
        METRIC(HP, hp)
        METRIC(Damage, damage)
        METRIC(AttackSpeed, attackInterval)
        METRIC(MoveSpeed, moveSpeed)
        METRIC(AttackRange, range) METRIC(ProjectileSpeed, projectileSpeed) METRIC(SplashRadius, splash)
            METRIC(Lifetime, lifetime) METRIC(Footprint, footprint) METRIC(TowerDamage, towerDamage)
                METRIC(SpellRadius, spellRadius) METRIC(ChargeDamage, chargeDamage) METRIC(SlowPct, slowPct)
                    METRIC(SlowDuration, slowDuration) METRIC(AuraDamage, auraDamage)
                        METRIC(AuraInterval, auraInterval) METRIC(AuraRadius, auraRadius)
                            METRIC(StunDuration, stunDuration) METRIC(DotDamage, dotDamage)
                                METRIC(DotDuration, dotDuration) METRIC(DotInterval, dotInterval)
#undef METRIC
                                    TestEqual(Id + TEXT(" rounds"), D->Rounds, C.rounds);
        TestEqual(Id + TEXT(" air"), D->Flying, C.flying);
        TestEqual(Id + TEXT(" air targeting"), D->GroundAndAir, C.canHitAir);
        TestEqual(Id + TEXT(" structures targeting"), D->StructuresOnly, C.structuresOnly);
        TestEqual(Id + TEXT(" spell type"), D->Spell, C.spell);
        TestEqual(Id + TEXT(" building type"), D->Building, C.building);
        TestNotNull(Id + TEXT(" card illustration"), D->Illustration.Get());
        if (!C.spell && !C.building) {
            TestNotNull(Id + TEXT(" skeletal mesh"), D->CharacterMesh.Get());
            if (const auto *Mesh = D->CharacterMesh.Get()) {
                const double Height = Mesh->GetBounds().BoxExtent.Z * 2;
                TestTrue(Id + TEXT(" imported centimeter scale"), Height >= 15 && Height <= 600);
                TestNotNull(Id + TEXT(" generated physics asset"), Mesh->GetPhysicsAsset());
                TestTrue(Id + TEXT(" imported LOD hierarchy"), Mesh->GetLODNum() >= 3);
                TestTrue(Id + TEXT(" skeletal rig"), Mesh->GetRefSkeleton().GetNum() >= 8);
            }
            TestTrue(Id + TEXT(" animation set bound"), D->Animations.Num() >= 6);
            for (const auto &A : D->Animations)
                TestNotNull(Id + TEXT(" animation ") + A.Key.ToString(), A.Value.Get());
        }
        if (C.building)
            TestNotNull(Id + TEXT(" structure mesh"), D->StructureMesh.Get());
        TestTrue(Id + TEXT(" effects bound"), D->Effects.Num() >= 10);
        for (const auto &E : D->Effects)
            TestNotNull(Id + TEXT(" effect ") + E.Key.ToString(), E.Value.Get());
        TestTrue(Id + TEXT(" sounds bound"), D->Sounds.Num() >= 3);
        for (const auto &A : D->Sounds)
            TestNotNull(Id + TEXT(" sound ") + A.Key.ToString(), A.Value.Get());
    }
    return true;
}
#endif
