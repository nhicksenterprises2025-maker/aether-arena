#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "RiftGameMode.generated.h"

class URiftUIWidget;
class UCameraComponent;
class ARiftArenaPresentation;

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
    void ReleaseCardAtCursor();
private:
    UPROPERTY() TObjectPtr<AActor> CameraActor;
    UPROPERTY() TObjectPtr<UCameraComponent> ArenaCamera;
    UPROPERTY() TObjectPtr<ARiftArenaPresentation> Arena;
    FVector2D ConfirmTile;
    FString ConfirmCard;
    bool bConfirmed=false,bPressed=false;
    float CameraZoom=1;
    void Press();void Release();void Cancel();void Developer();
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
};
