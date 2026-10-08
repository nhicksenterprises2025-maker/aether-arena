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
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "TimerManager.h"
#include "HAL/PlatformMisc.h"

ARiftGameMode::ARiftGameMode()
{PlayerControllerClass=ARiftPlayerController::StaticClass();DefaultPawnClass=nullptr;HUDClass=nullptr;}
void ARiftGameMode::BeginPlay()
{
    Super::BeginPlay();
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
        GetWorld()->GetSubsystem<URiftMatchSubsystem>()->OnEvent.AddLambda([CapturedEvents](const rift::Event& Event){++CapturedEvents->FindOrAdd(UTF8_TO_TCHAR(Event.type.c_str()));});
        FTimerHandle SetupTimer;GetWorld()->GetTimerManager().SetTimer(SetupTimer,[this,Page,Scenario]()
        {
            auto* PC=Cast<ARiftPlayerController>(GetWorld()->GetFirstPlayerController());if(!PC||!PC->Interface)return;
            auto* Match=GetWorld()->GetSubsystem<URiftMatchSubsystem>();
            if(Page==TEXT("Battle")&&!Match->IsActive())Match->StartMatch(true);
            if(!Scenario.IsEmpty())
            {
                if(!Match->IsActive())Match->StartMatch(true);auto* Sim=Match->Simulation();Sim->SetAIEnabled(rift::Team::Player,false);Sim->SetAIEnabled(rift::Team::Enemy,false);
                if(Scenario==TEXT("roster"))
                {
                    int32 I=0;for(const auto& Card:rift::Cards())if(!Card.spell){Sim->Spawn(rift::Team::Player,Card.id,{double(-10+(I%5)*5),double(3+(I/5)*4)});Sim->Spawn(rift::Team::Enemy,Card.id,{double(-10+(I%5)*5),double(-3-(I/5)*4)});++I;}
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
            PC->Interface->Navigate(Page);
        },.5f,false);
        float Delay=6;FParse::Value(FCommandLine::Get(),TEXT("RiftCaptureDelay="),Delay);bool Quit=FParse::Param(FCommandLine::Get(),TEXT("RiftQuitAfterCapture"));
        FString EffectScenario;FParse::Value(FCommandLine::Get(),TEXT("RiftVisualScenario="),EffectScenario);
        if(EffectScenario==TEXT("effects17"))
        {
            float Age=.12f;FParse::Value(FCommandLine::Get(),TEXT("RiftEffectAge="),Age);FTimerHandle ShowcaseTimer;
            GetWorld()->GetTimerManager().SetTimer(ShowcaseTimer,[this](){for(TActorIterator<ARiftArenaPresentation> It(GetWorld());It;++It){It->ShowcaseNiagara();break;}},FMath::Max(.75f,Delay-Age),false);
        }
        FTimerHandle CaptureTimer;GetWorld()->GetTimerManager().SetTimer(CaptureTimer,[this,CapturePath,Quit,CapturedEvents]()
        {
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(CapturePath),true);
            auto Snapshot=MakeShared<FJsonObject>();auto EventCounts=MakeShared<FJsonObject>();
            for(const auto& Pair:*CapturedEvents)EventCounts->SetNumberField(Pair.Key,Pair.Value);
            Snapshot->SetObjectField(TEXT("events"),EventCounts);
            auto* Match=GetWorld()->GetSubsystem<URiftMatchSubsystem>();Snapshot->SetNumberField(TEXT("speed"),Match->GetSpeed());
            if(const auto* State=Match->ViewState())
            {
                int32 Alive=0,Slowed=0,Stunned=0;for(const auto& Entity:State->entities)if(!Entity.dead){++Alive;if(Entity.slowUntil>State->elapsed)++Slowed;if(Entity.stunUntil>State->elapsed)++Stunned;}
                Snapshot->SetNumberField(TEXT("elapsed"),State->elapsed);Snapshot->SetNumberField(TEXT("aliveEntities"),Alive);
                Snapshot->SetNumberField(TEXT("slowedEntities"),Slowed);Snapshot->SetNumberField(TEXT("stunnedEntities"),Stunned);
                Snapshot->SetNumberField(TEXT("projectiles"),State->projectiles.size());Snapshot->SetNumberField(TEXT("hazards"),State->hazards.size());
            }
            FString PreviewCard;if(FParse::Value(FCommandLine::Get(),TEXT("RiftPreviewCard="),PreviewCard))if(const auto* Card=rift::FindCard(TCHAR_TO_UTF8(*PreviewCard)))if(auto* Sim=Match->Simulation())
            {
                float X=0,Y=7;FParse::Value(FCommandLine::Get(),TEXT("RiftPreviewX="),X);FParse::Value(FCommandLine::Get(),TEXT("RiftPreviewY="),Y);const auto Tile=rift::SnapToTile({X,Y});
                Snapshot->SetStringField(TEXT("previewCard"),PreviewCard);Snapshot->SetNumberField(TEXT("previewX"),Tile.x);Snapshot->SetNumberField(TEXT("previewY"),Tile.z);
                Snapshot->SetBoolField(TEXT("previewValid"),Sim->CanPlace(rift::Team::Player,*Card,Tile));Snapshot->SetNumberField(TEXT("previewFootprintTiles"),Card->footprint);Snapshot->SetNumberField(TEXT("previewRadiusTiles"),Card->spellRadius);
            }
            for(TActorIterator<ARiftArenaPresentation> It(GetWorld());It;++It)
            {
                TSharedPtr<FJsonObject> Niagara;
                if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(It->NiagaraDiagnosticsJSON()),Niagara))Snapshot->SetObjectField(TEXT("niagara"),Niagara);
                break;
            }
            FString SnapshotText;auto Writer=TJsonWriterFactory<>::Create(&SnapshotText);FJsonSerializer::Serialize(Snapshot,Writer);
            FFileHelper::SaveStringToFile(SnapshotText,*(FPaths::ChangeExtension(CapturePath,TEXT("state.json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            FScreenshotRequest::RequestScreenshot(CapturePath,true,false,false,FIntRect(),true);
            if(Quit)FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([](){FPlatformMisc::RequestExit(false);});
        },FMath::Max(1.f,Delay),false);
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
    Interface=CreateWidget<URiftUIWidget>(this,URiftUIWidget::StaticClass());Interface->AddToViewport();
    FInputModeGameAndUI Mode;Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);Mode.SetHideCursorDuringCapture(false);SetInputMode(Mode);
    if(FParse::Param(FCommandLine::Get(),TEXT("RiftTraining"))){GetWorld()->GetSubsystem<URiftMatchSubsystem>()->StartMatch(true);Interface->SetBattleView();}
    if(FParse::Param(FCommandLine::Get(),TEXT("RiftAutoBattle"))){GetWorld()->GetSubsystem<URiftMatchSubsystem>()->StartMatch(true,true);Interface->SetBattleView();}
}
void ARiftPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::LeftMouseButton,IE_Pressed,this,&ARiftPlayerController::Press);InputComponent->BindKey(EKeys::LeftMouseButton,IE_Released,this,&ARiftPlayerController::Release);
    InputComponent->BindKey(EKeys::RightMouseButton,IE_Pressed,this,&ARiftPlayerController::Cancel);
    InputComponent->BindKey(EKeys::Escape,IE_Pressed,this,&ARiftPlayerController::Cancel);
    InputComponent->BindKey(EKeys::F9,IE_Pressed,this,&ARiftPlayerController::Developer);
    InputComponent->BindKey(EKeys::One,IE_Pressed,this,&ARiftPlayerController::Hand0);InputComponent->BindKey(EKeys::Two,IE_Pressed,this,&ARiftPlayerController::Hand1);
    InputComponent->BindKey(EKeys::Three,IE_Pressed,this,&ARiftPlayerController::Hand2);InputComponent->BindKey(EKeys::Four,IE_Pressed,this,&ARiftPlayerController::Hand3);
    InputComponent->BindKey(EKeys::MouseScrollUp,IE_Pressed,this,&ARiftPlayerController::ZoomIn);InputComponent->BindKey(EKeys::MouseScrollDown,IE_Pressed,this,&ARiftPlayerController::ZoomOut);
}
bool ARiftPlayerController::CursorTile(FVector2D& Out)const
{
    FVector Origin,Direction;if(!DeprojectMousePositionToWorld(Origin,Direction)||FMath::Abs(Direction.Z)<.001)return false;
    double T=-Origin.Z/Direction.Z;if(T<0)return false;FVector P=Origin+Direction*T;auto S=rift::SnapToTile(URiftMatchSubsystem::TilePoint(P));Out=FVector2D(S.x,S.z);return FMath::Abs(S.x)<=14&&FMath::Abs(S.z)<=21;
}
void ARiftPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);int32 Width,Height;GetViewportSize(Width,Height);if(ArenaCamera&&Height>0)ArenaCamera->OrthoWidth=5350.f*float(Width)/Height*CameraZoom;
    if(!Arena)for(TActorIterator<ARiftArenaPresentation> It(GetWorld());It;++It){Arena=*It;break;}
    FString CapturePath,PreviewCard;
    if(Arena&&Interface&&Interface->IsBattleView()&&FParse::Value(FCommandLine::Get(),TEXT("RiftCapture="),CapturePath)&&FParse::Value(FCommandLine::Get(),TEXT("RiftPreviewCard="),PreviewCard))
        if(const auto* Preview=rift::FindCard(TCHAR_TO_UTF8(*PreviewCard)))if(auto* Sim=GetWorld()->GetSubsystem<URiftMatchSubsystem>()->Simulation())
        {
            float X=0,Y=7;FParse::Value(FCommandLine::Get(),TEXT("RiftPreviewX="),X);FParse::Value(FCommandLine::Get(),TEXT("RiftPreviewY="),Y);const auto Snapped=rift::SnapToTile({X,Y});
            Arena->SetPlacementPreview(FVector2D(Snapped.x,Snapped.z),Sim->CanPlace(rift::Team::Player,*Preview,Snapped),Preview->spellRadius,Preview->spell,Preview->building?Preview->footprint:0);return;
        }
    FVector2D Tile=FVector2D::ZeroVector;const auto* C=Interface?rift::FindCard(TCHAR_TO_UTF8(*Interface->SelectedCardId())):nullptr;
    bool Visible=Interface&&Interface->IsBattleView()&&C&&CursorTile(Tile);
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
    if(!Interface||!Interface->IsBattleView())return;auto* Profile=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();FString Card=Interface->SelectedCardId();
    if(Profile->Settings.ConfirmDeploy&&(!bConfirmed||ConfirmCard!=Card||!ConfirmTile.Equals(Tile,.001)))
    {bConfirmed=true;ConfirmCard=Card;ConfirmTile=Tile;Interface->Notify(TEXT("Click the highlighted tile again to deploy."));return;}
    bConfirmed=false;Interface->WorldClicked(Tile);
}
void ARiftPlayerController::Press()
{bPressed=true;auto* P=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();FVector2D T;if(!P->Settings.DragDeploy&&CursorTile(T))Deploy(T);}
void ARiftPlayerController::Release()
{auto* P=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();FVector2D T;if(P->Settings.DragDeploy&&CursorTile(T))Deploy(T);bPressed=false;}
void ARiftPlayerController::ReleaseCardAtCursor()
{if(GetGameInstance()->GetSubsystem<URiftProfileSubsystem>()->Settings.DragDeploy){FVector2D T;if(CursorTile(T))Deploy(T);}}
void ARiftPlayerController::Cancel()
{if(!Interface)return;bConfirmed=false;if(Interface->SelectedHand()>=0)Interface->SelectHand(-1);else Interface->Navigate(TEXT("Settings"));}
void ARiftPlayerController::Developer(){if(Interface)Interface->ToggleDeveloper();}
void ARiftPlayerController::Hand0(){if(Interface)Interface->SelectHand(0);bConfirmed=false;}
void ARiftPlayerController::Hand1(){if(Interface)Interface->SelectHand(1);bConfirmed=false;}
void ARiftPlayerController::Hand2(){if(Interface)Interface->SelectHand(2);bConfirmed=false;}
void ARiftPlayerController::Hand3(){if(Interface)Interface->SelectHand(3);bConfirmed=false;}
void ARiftPlayerController::ZoomIn(){CameraZoom=FMath::Clamp(CameraZoom-.025f*GetGameInstance()->GetSubsystem<URiftProfileSubsystem>()->Settings.CameraSpeed,.85f,1.2f);}
void ARiftPlayerController::ZoomOut(){CameraZoom=FMath::Clamp(CameraZoom+.025f*GetGameInstance()->GetSubsystem<URiftProfileSubsystem>()->Settings.CameraSpeed,.85f,1.2f);}
