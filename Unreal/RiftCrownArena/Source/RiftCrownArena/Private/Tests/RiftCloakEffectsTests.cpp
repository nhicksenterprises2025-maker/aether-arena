#if WITH_DEV_AUTOMATION_TESTS
#include "Presentation/RiftArenaPresentation.h"
#include "Presentation/RiftUnitVisual.h"
#include "RiftMatchSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    const rift::Entity* CloakBody(const rift::Match& Match,uint64 Id)
    {for(const auto& Body:Match.State().entities)if(Body.id==Id)return &Body;return nullptr;}
    rift::Entity* CloakFixtureBody(rift::Match& Match,uint64 Id)
    {return const_cast<rift::Entity*>(CloakBody(Match,Id));}
    uint64 CloakBodyId(const rift::Match& Match,const char* Card,rift::Team Team)
    {for(const auto& Body:Match.State().entities)if(Body.cardId==Card&&Body.team==Team&&!Body.dead)return Body.id;return 0;}
    void QuietCloakCrowns(rift::Match& Match)
    {
        // Fixture setup only: suppress unrelated tower attacks while the actual
        // troop, spell, projectile and aura implementations run normally.
        for(auto& Body:const_cast<rift::Snapshot&>(Match.State()).entities)
            if(Body.kind==rift::EntityKind::Guard||Body.kind==rift::EntityKind::Core)Body.stunUntil=100.;
    }
    TSharedPtr<FJsonObject> CloakDiagnostics(ARiftArenaPresentation* Arena)
    {
        TSharedPtr<FJsonObject> Result;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Arena->NiagaraDiagnosticsJSON()),Result);
        return Result;
    }
    int32 CloakSystemCount(const TSharedPtr<FJsonObject>& Report,const TCHAR* System)
    {
        if(!Report.IsValid())return -1;
        int32 Count=0;
        for(const auto& Value:Report->GetArrayField(TEXT("components")))
            if(Value->AsObject()->GetStringField(TEXT("system")).EndsWith(System))++Count;
        return Count;
    }
    int32 CloakSurfaceCount(const TSharedPtr<FJsonObject>& Report,const TCHAR* Name)
    {
        if(!Report.IsValid())return -1;
        for(const auto& Value:Report->GetArrayField(TEXT("expansionSurfaceEffects")))
        {
            const auto Entry=Value->AsObject();
            if(Entry->GetStringField(TEXT("name"))==Name)return int32(Entry->GetNumberField(TEXT("instances")));
        }
        return 0;
    }
    int32 CloakSystemAt(const TSharedPtr<FJsonObject>& Report,const TCHAR* System,FVector Position)
    {
        if(!Report.IsValid())return -1;
        int32 Count=0;
        for(const auto& Value:Report->GetArrayField(TEXT("components")))
        {
            const auto Entry=Value->AsObject();FVector Actual;
            if(Entry->GetStringField(TEXT("system")).EndsWith(System)&&Actual.InitFromString(Entry->GetStringField(TEXT("location")))&&Actual.Equals(Position,.1))++Count;
        }
        return Count;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftCloakSourceEffectsIntegrationTest,"Rift.Integration.CloakSourceEffects",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FRiftCloakSourceEffectsIntegrationTest::RunTest(const FString& Parameters)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RiftAutomationSandbox")))
    {AddError(TEXT("Cloak source-effects fixture requires isolated RiftSaveRoot/UserDir and RiftAutomationSandbox."));return false;}
    // Unreal deliberately refuses production Niagara spawns with NullRHI.
    // This is a rendered-effects test: retain its real positive controls and
    // run it in the separate real-RHI automation pass, never as a headless pass.
    if(!FApp::CanEverRender())
    {AddError(TEXT("Cloak source-effects requires a real RHI; production Niagara cannot spawn with NullRHI."));return false;}
    auto* GI=NewObject<UGameInstance>(GEngine);
    GI->InitializeStandalone(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto* World=GI->GetWorld();
    auto* MatchView=World->GetSubsystem<URiftMatchSubsystem>();
    ON_SCOPE_EXIT {if(MatchView)MatchView->SetReplayView(nullptr);World->DestroyWorld(false);GI->Shutdown();GEngine->DestroyWorldContext(World);};
    if(!TestNotNull(TEXT("Cloak fixture uses the production match view"),MatchView))return false;
    const auto* Shadow=rift::FindCard("shadow_surge");
    if(!TestNotNull(TEXT("Cloak fixture uses the compiled local Shadow Surge"),Shadow))return false;
    TestTrue(TEXT("Local cloak has a real animation delay and bounded lifetime"),Shadow->castDelay>0&&Shadow->invisibilityDuration>0&&!Shadow->globalFriendlyInvisibility);

    for(int32 Scenario=0;Scenario<3;++Scenario)
    {
        rift::MatchOptions Options;Options.seed=73100+Scenario;Options.aiEnabled={false,false};
        rift::Match Simulation(Options);QuietCloakCrowns(Simulation);
        const char* SourceCard=Scenario==0?"frost_fang":Scenario==1?"ember_archer":"storm_raven";
        const rift::Vec2 SourcePosition=Scenario==0?rift::Vec2{.5,-8.5}:Scenario==1?rift::Vec2{5.5,-8.5}:rift::Vec2{-5.5,-8.5};
        const rift::Vec2 VictimPosition=Scenario==0?rift::Vec2{.5,-7.5}:Scenario==1?rift::Vec2{5.5,-5.5}:rift::Vec2{-5.5,-6.5};
        if(!TestTrue(TEXT("Native source and victim placement is accepted"),Simulation.Spawn(rift::Team::Enemy,SourceCard,SourcePosition)&&Simulation.Spawn(rift::Team::Player,"ironclad",VictimPosition)))return false;
        const uint64 SourceId=CloakBodyId(Simulation,SourceCard,rift::Team::Enemy),VictimId=CloakBodyId(Simulation,"ironclad",rift::Team::Player);
        if(!TestTrue(TEXT("Fixture finds both authoritative live bodies"),SourceId!=0&&VictimId!=0))return false;
        CloakFixtureBody(Simulation,VictimId)->stunUntil=100.;
        if(Scenario==2)
        {
            auto* Source=CloakFixtureBody(Simulation,SourceId);
            Source->stunUntil=100.;Source->auraClock=rift::FindCard(SourceCard)->auraInterval-.05;
        }
        MatchView->SetReplayView(&Simulation.State());
        auto* Arena=World->SpawnActor<ARiftArenaPresentation>();
        if(!TestNotNull(TEXT("Cloak fixture renders through the production arena"),Arena))return false;
        Arena->DispatchBeginPlay();
        ON_SCOPE_EXIT {MatchView->SetReplayView(nullptr);Arena->Destroy();};
        std::vector<rift::Event> LastEvents;
        auto Forward=[&]()
        {
            LastEvents=Simulation.DrainEvents();
            for(const auto& Event:LastEvents)MatchView->OnEvent.Broadcast(Event);
            Arena->Tick(0.f);
        };
        auto Step=[&](){Simulation.Step(1./60.);Forward();};
        auto Advance=[&](double Duration)
        {const double Until=Simulation.State().elapsed+Duration;while(Simulation.State().elapsed+1e-9<Until)Step();};
        Forward();

        if(Scenario==0)
        {
            if(!TestTrue(TEXT("Real Shadow cast is accepted beside Frost Fang"),Simulation.Spawn(rift::Team::Enemy,"shadow_surge",SourcePosition)))return false;
            Forward();Step();
            const auto Visible=CloakDiagnostics(Arena);
            TestTrue(TEXT("Visible Frost release and contact create actual authored effects"),CloakSystemCount(Visible,TEXT(".NS_RiftFrost"))>=2);
            auto* FirstFrost=Arena->Visual(SourceId);
            if(!TestNotNull(TEXT("Visible Frost positive control initializes its actual model"),FirstFrost))return false;
            const FVector ReleaseOrigin=FirstFrost->AttackLocation();
            const int32 SourcePuffsBefore=CloakSystemAt(Visible,TEXT(".NS_RiftFrost"),ReleaseOrigin);
            TestEqual(TEXT("Visible release creates a real effect at the source's exact mouth anchor"),SourcePuffsBefore,1);
            Advance(.35);
            auto* Frost=Arena->Visual(SourceId);
            if(!TestNotNull(TEXT("Real Frost model remains present while cloaked"),Frost))return false;
            TestTrue(TEXT("A genuinely stationary Frost reaches its actual idle Breath while hidden"),Frost->HiddenFromViewer()&&Frost->CurrentAnimation()==TEXT("Breath"));
            auto Idle=CloakDiagnostics(Arena);
            if(!TestTrue(TEXT("Idle cloak diagnostics parse"),Idle.IsValid()))return false;
            TestEqual(TEXT("Hidden idle Frost never emits its mouth-location ambience"),int32(Idle->GetNumberField(TEXT("frostBreathPuffs"))),0);
            const int32 BeforeAttack=CloakSystemCount(Idle,TEXT(".NS_RiftFrost"));
            bool HiddenAttack=false;double HiddenDamage=0;
            for(int32 Frame=0;Frame<45&&!HiddenAttack;++Frame)
            {
                Step();
                for(const auto& Event:LastEvents)
                {
                    HiddenAttack|=Event.type=="attack"&&Event.source==SourceId&&CloakBody(Simulation,SourceId)->invisibleUntil>Simulation.State().elapsed;
                    if(Event.type=="damage"&&Event.source==SourceId&&Event.target==VictimId)HiddenDamage+=Event.amount;
                }
            }
            TestTrue(TEXT("Actual cloaked Frost still releases and damages its visible victim"),HiddenAttack&&HiddenDamage==rift::FindCard(SourceCard)->damage);
            const auto HiddenRelease=CloakDiagnostics(Arena);
            TestEqual(TEXT("Hidden Frost release creates only the legitimate visible-victim contact effect"),CloakSystemCount(HiddenRelease,TEXT(".NS_RiftFrost"))-BeforeAttack,1);
            TestTrue(TEXT("Stationary releases use the same actual mouth position"),Frost->AttackLocation().Equals(ReleaseOrigin,.1));
            TestEqual(TEXT("Hidden release creates no new particles at that mouth position"),CloakSystemAt(HiddenRelease,TEXT(".NS_RiftFrost"),ReleaseOrigin),SourcePuffsBefore);
            TestEqual(TEXT("Cloak expiry ribbons cannot appear before the actual expiry event"),CloakSurfaceCount(CloakDiagnostics(Arena),TEXT("CloakExpiryEdge")),0);
            bool Expired=false;double ExpiryTime=-1;
            for(int32 Frame=0;Frame<90&&!Expired;++Frame)
            {
                Step();
                for(const auto& Event:LastEvents)if(Event.type=="invisibility_expire"&&Event.target==SourceId)
                {Expired=true;ExpiryTime=Event.time;}
            }
            TestTrue(TEXT("The live simulation emits the authoritative invisibility_expire event"),Expired&&ExpiryTime>0);
            TestFalse(TEXT("An expired cloak restores the actual Frost body"),Frost->HiddenFromViewer());
            TestEqual(TEXT("The real expiry event creates exactly sixteen bounded edge ribbons"),CloakSurfaceCount(CloakDiagnostics(Arena),TEXT("CloakExpiryEdge")),16);
            for(int32 Frame=0;Frame<8;++Frame)Arena->Tick(.1f);
            TestEqual(TEXT("Paused renderer ticks neither duplicate nor age expiry ribbons"),CloakSurfaceCount(CloakDiagnostics(Arena),TEXT("CloakExpiryEdge")),16);
            Advance(.13333333333333333);
            TestEqual(TEXT("Expiry ribbons clear after their authored 0.12-second simulation lifetime"),CloakSurfaceCount(CloakDiagnostics(Arena),TEXT("CloakExpiryEdge")),0);
        }
        else if(Scenario==1)
        {
            Step();
            TestTrue(TEXT("Visible Ember launch is a positive control for the real muzzle effect"),CloakSystemCount(CloakDiagnostics(Arena),TEXT(".NS_RiftImpact"))>=1);
            Advance(.30);
            // Set the next legal attack's remaining cooldown, then let its
            // real countdown and projectile implementation run through cloak.
            CloakFixtureBody(Simulation,SourceId)->cooldown=Shadow->castDelay+.05;
            if(!TestTrue(TEXT("Real Shadow cast is accepted beside Ember Archer"),Simulation.Spawn(rift::Team::Enemy,"shadow_surge",SourcePosition)))return false;
            Forward();Advance(Shadow->castDelay);
            auto* Archer=Arena->Visual(SourceId);
            if(!TestNotNull(TEXT("Actual Ember model exists during cloak"),Archer))return false;
            TestTrue(TEXT("Enemy Ember is hidden before its next actual release"),Archer->HiddenFromViewer());
            const int32 BeforeMuzzle=CloakSystemCount(CloakDiagnostics(Arena),TEXT(".NS_RiftImpact"));
            const double BeforeHP=CloakBody(Simulation,VictimId)->hp;
            bool Launched=false;double ImpactAt=-1;
            for(int32 Frame=0;Frame<12&&!Launched;++Frame)
            {
                Step();
                for(const auto& Event:LastEvents)if(Event.type=="projectile_launch"&&Event.source==SourceId&&Event.target==VictimId)
                {Launched=true;ImpactAt=Event.until;}
            }
            auto Flight=CloakDiagnostics(Arena);bool VisibleCommittedShot=false;
            if(!TestTrue(TEXT("Committed-flight diagnostics parse"),Flight.IsValid()))return false;
            if(Flight.IsValid())for(const auto& Value:Flight->GetArrayField(TEXT("projectileVisuals")))
            {
                const auto Shot=Value->AsObject();
                VisibleCommittedShot|=uint64(Shot->GetNumberField(TEXT("sourceEntityId")))==SourceId&&Shot->GetBoolField(TEXT("meshPresent"))&&Shot->GetBoolField(TEXT("meshVisible"));
            }
            TestTrue(TEXT("Hidden source preserves its real committed flight mesh"),Launched&&VisibleCommittedShot&&ImpactAt>Simulation.State().elapsed);
            TestEqual(TEXT("Hidden Ember creates no muzzle flash revealing its position"),CloakSystemCount(Flight,TEXT(".NS_RiftImpact")),BeforeMuzzle);
            TestEqual(TEXT("The flight does not damage its victim before arrival"),CloakBody(Simulation,VictimId)->hp,BeforeHP);
            bool Contact=false;double ContactDamage=0;
            for(int32 Frame=0;Frame<45&&!Contact;++Frame)
            {
                Step();
                for(const auto& Event:LastEvents)if(Event.type=="damage"&&Event.source==SourceId&&Event.target==VictimId)
                {Contact=true;ContactDamage+=Event.amount;}
            }
            TestTrue(TEXT("Committed projectile damage still resolves while its caster is invisible"),Contact&&Archer->HiddenFromViewer()&&ContactDamage==rift::FindCard(SourceCard)->damage&&CloakBody(Simulation,VictimId)->hp==BeforeHP-ContactDamage);
            TestEqual(TEXT("Actual projectile contact retains its visible-victim impact"),CloakSystemCount(CloakDiagnostics(Arena),TEXT(".NS_RiftImpact")),BeforeMuzzle+1);
        }
        else
        {
            Advance(.05);
            TestEqual(TEXT("Visible Storm pulse is a positive control for the authored aura"),CloakSystemCount(CloakDiagnostics(Arena),TEXT(".NS_RiftAura")),1);
            CloakFixtureBody(Simulation,SourceId)->auraClock=rift::FindCard(SourceCard)->auraInterval-.40;
            if(!TestTrue(TEXT("Real Shadow cast is accepted beside Storm Raven"),Simulation.Spawn(rift::Team::Enemy,"shadow_surge",SourcePosition)))return false;
            Forward();Advance(Shadow->castDelay);
            auto* Raven=Arena->Visual(SourceId);
            if(!TestNotNull(TEXT("Actual Raven model exists during cloak"),Raven))return false;
            TestTrue(TEXT("Enemy Raven is hidden before its actual aura event"),Raven->HiddenFromViewer());
            const int32 BeforeAura=CloakSystemCount(CloakDiagnostics(Arena),TEXT(".NS_RiftAura"));
            const double BeforeHP=CloakBody(Simulation,VictimId)->hp;
            bool Pulsed=false;double AuraDamage=0;
            for(int32 Frame=0;Frame<24&&!Pulsed;++Frame)
            {
                Step();
                for(const auto& Event:LastEvents)
                {
                    Pulsed|=Event.type=="aura"&&Event.source==SourceId;
                    if(Event.type=="damage"&&Event.source==SourceId&&Event.target==VictimId&&Event.damageKind=="aura")AuraDamage+=Event.amount;
                }
            }
            TestTrue(TEXT("Actual aura still damages its victim while its Raven is invisible"),Pulsed&&Raven->HiddenFromViewer()&&AuraDamage==rift::FindCard(SourceCard)->auraDamage&&CloakBody(Simulation,VictimId)->hp==BeforeHP-AuraDamage);
            TestEqual(TEXT("Hidden Storm aura never reveals the source with a radial particle burst"),CloakSystemCount(CloakDiagnostics(Arena),TEXT(".NS_RiftAura")),BeforeAura);
        }
    }
    AddInfo(TEXT("Real delayed local cloaks, Frost idle/source effects, Ember muzzle/committed contact, Storm aura source suppression and authoritative expiry-edge cleanup exercised; no showcase particles or fabricated combat events."));
    return !HasAnyErrors();
}
#endif
