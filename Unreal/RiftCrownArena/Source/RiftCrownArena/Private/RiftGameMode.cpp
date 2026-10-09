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

class FRiftCaptureInputFilter final : public IInputProcessor
{
public:
    explicit FRiftCaptureInputFilter(bool Consume):bConsumeInput(Consume){}
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
    if(FSlateApplication::IsInitialized()&&(Capture||FParse::Value(FCommandLine::Get(),TEXT("RiftAudioSmoke="),AutomatedPath)||FParse::Value(FCommandLine::Get(),TEXT("RiftPerfReport="),AutomatedPath)))
    {
        // RenderOffScreen uses the Null application on Windows, which still
        // polls external gamepads. Consume their events without disabling or
        // dimming the real UI; explicit fixture callbacks remain available.
        CaptureInputFilter=MakeShared<FRiftCaptureInputFilter>(!Capture||!FParse::Param(FCommandLine::Get(),TEXT("RiftCaptureAllowInput")));
        FSlateApplication::Get().RegisterInputPreProcessor(CaptureInputFilter,0);
    }
    GetWorld()->SpawnActor<ARiftArenaPresentation>();
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
        GetWorld()->GetSubsystem<URiftMatchSubsystem>()->OnEvent.AddLambda([CapturedEvents](const rift::Event& Event){++CapturedEvents->FindOrAdd(UTF8_TO_TCHAR(Event.type.c_str()));});
        FTimerHandle SetupTimer;GetWorld()->GetTimerManager().SetTimer(SetupTimer,[this,Page,Scenario,RecordedCapture,RecordedDuration]()
        {
            auto* PC=Cast<ARiftPlayerController>(GetWorld()->GetFirstPlayerController());if(!PC||!PC->Interface)return;
            auto* Match=GetWorld()->GetSubsystem<URiftMatchSubsystem>();
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
                else if(Scenario==TEXT("placement")||Scenario==TEXT("effects17"))Match->SetSpeed(0);
                if(Scenario!=TEXT("roster")&&Scenario!=TEXT("placement")&&Scenario!=TEXT("effects17"))
                {float CaptureSpeed=1;FParse::Value(FCommandLine::Get(),TEXT("RiftCaptureSpeed="),CaptureSpeed);Match->SetSpeed(FMath::Clamp(CaptureSpeed,.25f,4.f));}
                Match->FlushEvents();
            }
            if(Page==TEXT("CardDetail"))PC->Interface->InspectCard(TEXT("ironclad"));else PC->Interface->Navigate(Page);
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
        FTimerHandle CaptureTimer;GetWorld()->GetTimerManager().SetTimer(CaptureTimer,FTimerDelegate::CreateWeakLambda(this,[this,CapturePath,Quit,CapturedEvents,RecordedCapture,RecordedDuration,Scenario]()
        {
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(CapturePath),true);
            auto Snapshot=MakeShared<FJsonObject>();auto EventCounts=MakeShared<FJsonObject>();
            if(CaptureInputFilter)Snapshot->SetObjectField(TEXT("captureInput"),CaptureInputFilter->Report());
            Snapshot->SetBoolField(TEXT("recordedMatchFixture"),*RecordedCapture);
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
                if(Scenario==TEXT("effects17"))
                {
                    float Age=.12f;FParse::Value(FCommandLine::Get(),TEXT("RiftEffectAge="),Age);
                    Age=FMath::IsFinite(Age)?FMath::Clamp(Age,0.f,2.f):.12f;
                    It->ShowcaseNiagaraAtAge(Age);
                }
                Snapshot->SetNumberField(TEXT("trainingOverlayLines"),It->TrainingOverlayLineCount());Snapshot->SetNumberField(TEXT("trainingOverlayLabels"),It->TrainingOverlayLabelCount());
                TSharedPtr<FJsonObject> Niagara;
                if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(It->NiagaraDiagnosticsJSON()),Niagara))Snapshot->SetObjectField(TEXT("niagara"),Niagara);
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
        if(FParse::Value(FCommandLine::Get(),TEXT("RiftCaptureZoom="),CaptureZoom)&&FMath::IsFinite(CaptureZoom))PendingCaptureZoom=FMath::Clamp(CaptureZoom,.85f,2.f);
    }
    Interface=CreateWidget<URiftUIWidget>(this,URiftUIWidget::StaticClass());Interface->AddToViewport(10);
    FInputModeGameAndUI Mode;Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);Mode.SetHideCursorDuringCapture(false);SetInputMode(Mode);
    if(FParse::Param(FCommandLine::Get(),TEXT("RiftTraining"))){GetWorld()->GetSubsystem<URiftMatchSubsystem>()->StartMatch(true);Interface->SetBattleView();}
    if(FParse::Param(FCommandLine::Get(),TEXT("RiftAutoBattle"))){GetWorld()->GetSubsystem<URiftMatchSubsystem>()->StartMatch(true,true);Interface->SetBattleView();}
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
    FVector Origin,Direction;if(!DeprojectMousePositionToWorld(Origin,Direction)||FMath::Abs(Direction.Z)<.001)return false;
    double T=-Origin.Z/Direction.Z;if(T<0)return false;return GroundPointToTile(Origin+Direction*T,Out);
}
bool ARiftPlayerController::GroundPointToTile(FVector GroundPoint,FVector2D& Out)
{
    const auto Point=URiftMatchSubsystem::TilePoint(GroundPoint);
    // SnapToTile clamps to the nearest board cell. Reject decorative ground
    // first so an off-board click cannot become an unintended edge deployment.
    if(!FMath::IsFinite(Point.x)||!FMath::IsFinite(Point.z)||FMath::Abs(Point.x)>14||FMath::Abs(Point.z)>21)return false;
    const auto Snapped=rift::SnapToTile(Point);Out=FVector2D(Snapped.x,Snapped.z);return true;
}
void ARiftPlayerController::PlayerTick(float DeltaTime)
{
    if(bCardDragStarted&&FSlateApplication::IsInitialized()&&(!FSlateApplication::Get().IsActive()||!FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton)))Cancel();
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
    auto* Match=GetWorld()->GetSubsystem<URiftMatchSubsystem>();const auto* State=Match->ViewState();const auto* Selected=rift::FindCard(TCHAR_TO_UTF8(*Card));
    bool Valid=Match->CanPlace(Interface->SelectedHand(),Tile)&&State&&Selected&&State->aether[0]>=Selected->cost;
    if(Interface->PlacementIsSandbox()&&Match->Simulation()&&Selected)Valid=Match->Simulation()->CanPlace(rift::Team(Interface->PlacementTeam()),*Selected,{Tile.X,Tile.Y},true);
    if(!Valid){bConfirmed=false;Interface->WorldClicked(Tile);return;}
    if(Profile->Settings.ConfirmDeploy&&(!bConfirmed||ConfirmCard!=Card||!ConfirmTile.Equals(Tile,.001)))
    {bConfirmed=true;ConfirmCard=Card;ConfirmTile=Tile;Interface->Notify(TEXT("Click the highlighted tile again to deploy."));return;}
    bConfirmed=false;Interface->WorldClicked(Tile);
}
void ARiftPlayerController::Press()
{
    bPressed=Interface&&Interface->CanAcceptBattleInput()&&!Interface->SelectedCardId().IsEmpty();
    auto* Profile=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();FVector2D Tile;
    if(bPressed&&!Profile->Settings.DragDeploy&&CursorTile(Tile))Deploy(Tile);
}
void ARiftPlayerController::Release()
{
    if(bCardDragStarted){FinishCardDrag(true);return;}
    const bool Armed=bPressed;bPressed=false;bCardDragOriginValid=false;
    auto* Profile=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();FVector2D Tile;
    if(Armed&&Profile->Settings.DragDeploy&&CursorTile(Tile))Deploy(Tile);
}
void ARiftPlayerController::BeginCardDrag()
{
    bCardDragStarted=Interface&&Interface->CanAcceptBattleInput()&&Interface->SelectedHand()>=0;bConfirmed=false;bPressed=false;
    float X=0,Y=0;bCardDragOriginValid=bCardDragStarted&&GetMousePosition(X,Y);CardDragOrigin=FVector2D(X,Y);
}
bool ARiftPlayerController::IsDraggingCard()const
{
    if(!bCardDragStarted||!bCardDragOriginValid||!Interface||!Interface->CanAcceptBattleInput()||Interface->SelectedHand()<0)return false;
    const auto* Profile=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();if(!Profile->Settings.DragDeploy)return false;
    float X=0,Y=0;return GetMousePosition(X,Y)&&FVector2D::DistSquared(CardDragOrigin,FVector2D(X,Y))>49.;
}
void ARiftPlayerController::FinishCardDrag(bool DeployIfOutside)
{
    const bool Dragged=IsDraggingCard();bCardDragStarted=false;bCardDragOriginValid=false;bPressed=false;
    if(!Dragged)return;
    FVector2D Tile;
    if(DeployIfOutside&&CursorTile(Tile))Deploy(Tile);
    else if(Interface&&Interface->CanAcceptBattleInput()){Interface->SelectHand(-1);Interface->Notify(TEXT("Card returned to hand."));}
}
void ARiftPlayerController::ReleaseCardAtCursor()
{FinishCardDrag(true);}
void ARiftPlayerController::Cancel()
{bConfirmed=false;bPressed=false;bCardDragStarted=false;bCardDragOriginValid=false;if(Interface)Interface->SelectHand(-1);}
void ARiftPlayerController::EscapeMenu()
{
    bConfirmed=false;bPressed=false;bCardDragStarted=false;bCardDragOriginValid=false;if(!Interface)return;
    if(Interface->IsLiveBattleView())Interface->ToggleBattleMenu();
    else if(GetGameInstance()->GetSubsystem<URiftReplaySubsystem>()->IsPlaying())Interface->Navigate(TEXT("ReplayView"));
    else if(GetWorld()->GetSubsystem<URiftMatchSubsystem>()->IsActive())Interface->Navigate(TEXT("Battle"));
    else Interface->Navigate(TEXT("Home"));
}
void ARiftPlayerController::Developer(){if(Interface&&Interface->CanAcceptBattleInput())Interface->ToggleDeveloper();}
void ARiftPlayerController::Hand0(){if(Interface&&Interface->CanAcceptBattleInput())Interface->SelectHand(0);bConfirmed=false;}
void ARiftPlayerController::Hand1(){if(Interface&&Interface->CanAcceptBattleInput())Interface->SelectHand(1);bConfirmed=false;}
void ARiftPlayerController::Hand2(){if(Interface&&Interface->CanAcceptBattleInput())Interface->SelectHand(2);bConfirmed=false;}
void ARiftPlayerController::Hand3(){if(Interface&&Interface->CanAcceptBattleInput())Interface->SelectHand(3);bConfirmed=false;}
void ARiftPlayerController::UpdateArenaCamera()
{
    int32 Width=0,Height=0;GetViewportSize(Width,Height);if(!ArenaCamera||!CameraActor||Width<=0||Height<=0)return;
    constexpr float BaselineHeight=5350.f;
    const FVector BaselineLocation(0,3600,5400);
    const FVector Up=ArenaCamera->GetUpVector(),Right=ArenaCamera->GetRightVector();
    const bool BattleView=Interface&&Interface->IsBattleView();
    // Apply after capture navigation; Home's smaller zoom ceiling must not
    // discard a requested battle/replay zoom before its HUD bounds are known.
    if(BattleView&&PendingCaptureZoom>=0){CameraZoom=PendingCaptureZoom;PendingCaptureZoom=-1;}
    CameraUIScale=UWidgetLayoutLibrary::GetViewportScale(this);
    // Match the real canvas dock/header extents, then leave a small gutter so
    // rear tiles and their placement markers remain clear at either zoom limit.
    CameraSafeTop=BattleView?(Interface->IsLiveBattleView()?80.f:84.f)+12.f:0.f;
    CameraSafeBottom=BattleView?(Interface->IsLiveBattleView()?202.f:158.f)+12.f:0.f;
    CameraSafeTop*=CameraUIScale;CameraSafeBottom*=CameraUIScale;
    MinCameraZoom=.85f;MaxCameraZoom=1.2f;
    if(BattleView)
    {
        const float VerticalSpan=2800.f*FMath::Abs(Up.X)+4200.f*FMath::Abs(Up.Y);
        const float HorizontalSpan=2800.f*FMath::Abs(Right.X)+4200.f*FMath::Abs(Right.Y);
        const float AvailableHeight=FMath::Max(1.f,Height-CameraSafeTop-CameraSafeBottom);
        const float AvailableWidth=FMath::Max(1.f,Width-24.f*CameraUIScale);
        const float MinimumHeight=FMath::Max(VerticalSpan*Height/AvailableHeight,HorizontalSpan*Height/AvailableWidth);
        MinCameraZoom=FMath::Max(MinCameraZoom,MinimumHeight/BaselineHeight);
        // Large UI and smaller windows can need a wider view than the original
        // zoom ceiling. Retain useful wheel movement above that safe minimum.
        MaxCameraZoom=FMath::Max(MaxCameraZoom,MinCameraZoom*1.2f);
    }
    CameraZoom=FMath::Clamp(CameraZoom,MinCameraZoom,MaxCameraZoom);
    const float ViewHeight=BaselineHeight*CameraZoom;
    ArenaCamera->OrthoWidth=ViewHeight*float(Width)/Height;
    FVector Location=BaselineLocation;
    if(BattleView)
    {
        const double CurrentCenter=FVector::DotProduct(-BaselineLocation,Up);
        const double DesiredCenter=(CameraSafeBottom-CameraSafeTop)*ViewHeight/(2.f*Height);
        Location+=Up*(CurrentCenter-DesiredCenter);
    }
    // Translating along the unchanged camera up axis keeps the arena's original
    // angle, lighting, proportions and geometry while centering the usable field.
    CameraActor->SetActorLocation(Location);
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
    Report->SetNumberField(TEXT("safeTop"),Height>0?CameraSafeTop/Height:0.f);
    Report->SetNumberField(TEXT("safeBottom"),Height>0?(Height-CameraSafeBottom)/Height:0.f);
    bool Passed=ArenaCamera&&Width>0&&Height>0;TArray<TSharedPtr<FJsonValue>> Corners;
    for(float X:{-14.f,14.f})for(float Y:{-21.f,21.f})
    {
        FVector2D Screen=FVector2D::ZeroVector;
        const bool Projected=ProjectWorldLocationToScreen(URiftMatchSubsystem::WorldPoint({X,Y}),Screen,false);
        const bool Inside=Projected&&Screen.X>=-1&&Screen.X<=Width+1&&Screen.Y>=CameraSafeTop-1&&Screen.Y<=Height-CameraSafeBottom+1;
        Passed=Passed&&Inside;auto Corner=MakeShared<FJsonObject>();Corner->SetNumberField(TEXT("tileX"),X);Corner->SetNumberField(TEXT("tileY"),Y);
        Corner->SetNumberField(TEXT("screenX"),Width>0?Screen.X/Width:0.);Corner->SetNumberField(TEXT("screenY"),Height>0?Screen.Y/Height:0.);
        Corner->SetBoolField(TEXT("projected"),Projected);Corner->SetBoolField(TEXT("insideSafeArea"),Inside);Corners.Add(MakeShared<FJsonValueObject>(Corner));
    }
    Report->SetArrayField(TEXT("legalFieldCorners"),Corners);Report->SetBoolField(TEXT("passed"),Passed);
    FString Result;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Result));return Result;
}
void ARiftPlayerController::ZoomIn(){UpdateArenaCamera();CameraZoom=FMath::Clamp(CameraZoom-.025f*GetGameInstance()->GetSubsystem<URiftProfileSubsystem>()->Settings.CameraSpeed,MinCameraZoom,MaxCameraZoom);UpdateArenaCamera();}
void ARiftPlayerController::ZoomOut(){UpdateArenaCamera();CameraZoom=FMath::Clamp(CameraZoom+.025f*GetGameInstance()->GetSubsystem<URiftProfileSubsystem>()->Settings.CameraSpeed,MinCameraZoom,MaxCameraZoom);UpdateArenaCamera();}
