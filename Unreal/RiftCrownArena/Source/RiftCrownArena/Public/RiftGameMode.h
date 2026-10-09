#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "RiftGameMode.generated.h"

class URiftUIWidget;
class UCameraComponent;
class ARiftArenaPresentation;
class IInputProcessor;
class FRiftCaptureInputFilter;

UCLASS()
class RIFTCROWNARENA_API ARiftPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    ARiftPlayerController();
    void BeginPlay()override;
    void PlayerTick(float DeltaTime)override;
    void SetupInputComponent()override;
    UPROPERTY(BlueprintReadOnly) TObjectPtr<URiftUIWidget> Interface;
    bool CursorTile(FVector2D& Out)const;
    static bool GroundPointToTile(FVector GroundPoint,FVector2D& Out);
    void BeginCardDrag();
    void FinishCardDrag(bool DeployIfOutside);
    bool IsDraggingCard()const;
    void ReleaseCardAtCursor();
    FString CameraFramingDiagnosticsJSON()const;
    FBox2D BattleSafeScreenBounds()const;
private:
    UPROPERTY() TObjectPtr<AActor> CameraActor;
    UPROPERTY() TObjectPtr<UCameraComponent> ArenaCamera;
    UPROPERTY() TObjectPtr<ARiftArenaPresentation> Arena;
    FVector2D ConfirmTile;
    FVector2D CardDragOrigin=FVector2D::ZeroVector;
    FString ConfirmCard;
    bool bConfirmed=false,bPressed=false,bCardDragStarted=false,bCardDragOriginValid=false;
    float CameraZoom=1;
    float PendingCaptureZoom=-1;
    float MinCameraZoom=.85f,MaxCameraZoom=1.2f;
    float CameraSafeTop=0,CameraSafeBottom=0,CameraSafeLeft=0,CameraSafeRight=0,CameraUIScale=1;
    bool bBattleCameraInitialized=false;
    void UpdateArenaCamera();
    void Press();void Release();void Cancel();void EscapeMenu();void Developer();
    void Hand0();void Hand1();void Hand2();void Hand3();
    void ZoomIn();void ZoomOut();
    void Deploy(FVector2D Tile);
};

UCLASS()
class RIFTCROWNARENA_API ARiftGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ARiftGameMode();
    void BeginPlay()override;
    void EndPlay(const EEndPlayReason::Type EndPlayReason)override;
    // Automated rendering consumes hardware input before it reaches widgets;
    // direct fixture callbacks still exercise the production controls.
    static TSharedRef<IInputProcessor> MakeCaptureInputFilter(bool ConsumeInput=true);
private:
    TSharedPtr<FRiftCaptureInputFilter> CaptureInputFilter;
};
