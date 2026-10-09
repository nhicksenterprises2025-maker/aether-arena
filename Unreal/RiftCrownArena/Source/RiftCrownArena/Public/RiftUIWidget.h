#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/Slider.h"
#include "Components/ComboBoxString.h"
#include "Dom/JsonObject.h"
#include "RiftUIWidget.generated.h"

class UCanvasPanel; class UVerticalBox; class UHorizontalBox; class UTextBlock; class UEditableTextBox; class UComboBoxString; class UProgressBar; class UImage; class UBorder; class URiftAetherMeter;

UCLASS()
class RIFTCROWNARENA_API URiftComboBox : public UComboBoxString
{
    GENERATED_BODY()
public:
    URiftComboBox(const FObjectInitializer& ObjectInitializer);
};

UCLASS()
class RIFTCROWNARENA_API URiftActionButton : public UButton
{
    GENERATED_BODY()
public:
    TFunction<void()> Action;
    TFunction<void()> ReleaseAction;
    void Bind(TFunction<void()> Callback,bool OnPress=false);
    UFUNCTION() void Invoke();
    UFUNCTION() void InvokeRelease();
};
UCLASS()
class RIFTCROWNARENA_API URiftValueSlider : public USlider
{
    GENERATED_BODY()
public:
    TFunction<void(float)> Action;
    void Bind(TFunction<void(float)> Callback);
    UFUNCTION() void Invoke(float SliderValue);
};
UCLASS()
class RIFTCROWNARENA_API URiftChartWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    TArray<FVector2D> SeriesA,SeriesB;
    FString Caption;
protected:
    TSharedRef<SWidget> RebuildWidget()override;
    int32 NativePaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool)const override;
};

UCLASS()
class RIFTCROWNARENA_API URiftUIWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable) void Navigate(const FString& Destination);
    UFUNCTION(BlueprintCallable) bool IsBattleView()const{return Page==TEXT("Battle")||Page==TEXT("ReplayView");}
    UFUNCTION(BlueprintCallable) bool IsLiveBattleView()const{return Page==TEXT("Battle");}
    bool IsBattleDeveloperVisible()const{return IsLiveBattleView()&&bDev;}
    UFUNCTION(BlueprintCallable) bool CanAcceptBattleInput()const;
    UFUNCTION(BlueprintCallable) void ToggleBattleMenu();
    UFUNCTION(BlueprintCallable) int32 SelectedHand()const{return HandIndex;}
    UFUNCTION(BlueprintCallable) FString SelectedCardId()const;
    UFUNCTION(BlueprintCallable) void SetBattleView(){Navigate(TEXT("Battle"));}
    UFUNCTION(BlueprintCallable) void InspectCard(const FString& CardId){DetailCard=CardId;Navigate(TEXT("CardDetail"));}
    UFUNCTION(BlueprintCallable) void WorldClicked(FVector2D Tile);
    UFUNCTION(BlueprintCallable) void SelectHand(int32 Index){HandIndex=Index;bSpawnArmed=false;}
    UFUNCTION(BlueprintCallable) void Notify(const FString& Message){Say(Message);}
    UFUNCTION(BlueprintCallable) void ToggleDeveloper(){bDev=!bDev;if(!bDev)bSpawnArmed=false;if(Page==TEXT("Battle"))Navigate(TEXT("Battle"));}
    UFUNCTION(BlueprintCallable) bool PlacementIsSandbox()const{return bSpawnArmed;}
    UFUNCTION(BlueprintCallable) int32 PlacementTeam()const;
protected:
    TSharedRef<SWidget> RebuildWidget()override;
    void NativeConstruct()override;
    void NativeTick(const FGeometry&,float Delta)override;
private:
    UPROPERTY() TObjectPtr<UCanvasPanel> Root;
    UPROPERTY() TObjectPtr<UVerticalBox> Body;
    UPROPERTY() TObjectPtr<UVerticalBox> Rows;
    UPROPERTY() TObjectPtr<UTextBlock> NoticeText;
    UPROPERTY() TObjectPtr<UTextBlock> TimerText;
    UPROPERTY() TObjectPtr<UTextBlock> ScoreText;
    UPROPERTY() TObjectPtr<UTextBlock> AetherText;
    UPROPERTY() TObjectPtr<UTextBlock> NextText;
    UPROPERTY() TObjectPtr<UTextBlock> DevReadout;
    UPROPERTY() TObjectPtr<UTextBlock> TrainingStatus;
    UPROPERTY() TObjectPtr<URiftActionButton> SpawnArmButton;
    UPROPERTY() TObjectPtr<URiftActionButton> EnemyAIButton;
    UPROPERTY() TObjectPtr<URiftActionButton> FriendlyAIButton;
    UPROPERTY() TArray<TObjectPtr<URiftActionButton>> TrainingSpeedButtons;
    FString LastTrainingTower;
    UPROPERTY() TObjectPtr<UTextBlock> PhaseText;
    UPROPERTY() TObjectPtr<UTextBlock> EnemyScore;
    UPROPERTY() TObjectPtr<UTextBlock> SurgeText;
    UPROPERTY() TObjectPtr<UTextBlock> SelectedName;
    UPROPERTY() TObjectPtr<UTextBlock> SelectedStats;
    UPROPERTY() TObjectPtr<UTextBlock> AnnouncementText;
    UPROPERTY() TObjectPtr<UImage> NextImage;
    UPROPERTY() TObjectPtr<UImage> DragImage;
    UPROPERTY() TObjectPtr<UBorder> DragFrame;
    FString DragArtId;
    UPROPERTY() TObjectPtr<URiftAetherMeter> AetherMeter;
    UPROPERTY() TObjectPtr<UBorder> AnnouncementPanel;
    UPROPERTY() TObjectPtr<UBorder> BattleMenu;
    UPROPERTY() TObjectPtr<UTextBlock> MetaStatus;
    UPROPERTY() TObjectPtr<UProgressBar> AetherBar;
    UPROPERTY() TObjectPtr<UTextBlock> ReplayPosition;
    UPROPERTY() TObjectPtr<URiftValueSlider> ReplaySeek;
    UPROPERTY() TArray<TObjectPtr<URiftActionButton>> ReplaySpeedButtons;
    double LastReplayLedgerPosition=-1;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> HandText;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> HandCost;
    UPROPERTY() TArray<TObjectPtr<URiftActionButton>> HandButtons;
    UPROPERTY() TObjectPtr<UEditableTextBox> NameInput;
    UPROPERTY() TObjectPtr<UEditableTextBox> PresetName;
    UPROPERTY() TObjectPtr<UEditableTextBox> FileInput;
    UPROPERTY() TObjectPtr<UEditableTextBox> TowerHP;
    UPROPERTY() TObjectPtr<UEditableTextBox> MetaSearch;
    UPROPERTY() TObjectPtr<UEditableTextBox> MetaMin;
    UPROPERTY() TObjectPtr<UComboBoxString> MetaStyle;
    UPROPERTY() TObjectPtr<UComboBoxString> MetaArchetype;
    UPROPERTY() TObjectPtr<UComboBoxString> MetaType;
    UPROPERTY() TObjectPtr<UComboBoxString> MetaTrait;
    UPROPERTY() TObjectPtr<UComboBoxString> MetaCost;
    UPROPERTY() TObjectPtr<UComboBoxString> MetaVersion;
    UPROPERTY() TObjectPtr<UComboBoxString> MetaBaseline;
    UPROPERTY() TObjectPtr<UComboBoxString> MetaWindow;
    UPROPERTY() TObjectPtr<UComboBoxString> MetaMetric;
    UPROPERTY() TObjectPtr<UComboBoxString> MetaSubject;
    UPROPERTY() TObjectPtr<UComboBoxString> MetaFormat;
    UPROPERTY() TObjectPtr<UComboBoxString> DevCard;
    UPROPERTY() TObjectPtr<UComboBoxString> DevTeam;
    UPROPERTY() TObjectPtr<UComboBoxString> DevStyle;
    UPROPERTY() TObjectPtr<UComboBoxString> DevTower;
    UPROPERTY() TArray<TObjectPtr<UImage>> HandImages;
    FString Page=TEXT("Home"),DetailCard,Tab=TEXT("Cards"),Search,TypeFilter=TEXT("all"),TraitFilter=TEXT("all"),CostFilter=TEXT("all"),StyleFilter=TEXT("all"),ArchetypeFilter=TEXT("all"),SortKey=TEXT("adjustedWinRate");
    FString Notice,OpenedReplay,DraftName,DatasetName;
    TArray<FString> HandArtIds;
    FString NextArtId, LastPhase;
    TSharedPtr<FJsonObject> MetaDetail;
    int32 HandIndex=-1,PresetIndex=0,MinSample=0;
    bool bDev=false,bSpawnArmed=false,bSortDescending=true,bMetaPaused=true,bDraftLoaded=false,bEndPresented=false,bEndReplaySaving=false,bReplayListSaving=false,bMenuPausedMatch=false;
    float RefreshClock=0,MetaClock=0;
    float AnnouncementAge=0,NoticeAge=0,MenuResumeSpeed=1;
    bool bDeckAnalysisExpanded=false;
    bool bReplayDetailsExpanded=false;
    bool bProfileDetailsExpanded=false;
    int32 LastSurge=1;
    TArray<FString> Draft;
    UTextBlock* Text(const FString&,int32 Size=16,FLinearColor Color=FLinearColor(.9f,.9f,.85f,1));
    URiftActionButton* Button(const FString&,TFunction<void()> Action,bool Accent=false);
    UWidget* Illustration(const FString& CardId,float Width,UImage** ImageOut=nullptr);
    URiftActionButton* CardButton(const FString& CardId,float Width,TFunction<void()> Action,bool Selected=false);
    UHorizontalBox* Row(UVerticalBox* Target=nullptr);
    UEditableTextBox* Edit(const FString&,const FString& Hint=TEXT(""));
    UComboBoxString* Combo(const TArray<FString>& Values,const FString& Selected);
    void Add(UVerticalBox*,UWidget*,float Spacing=6);
    void Add(UHorizontalBox*,UWidget*,bool Fill=false,float Spacing=4);
    void Shell(const FString& Title);
    UBorder* Panel(UWidget*,FMargin Padding=FMargin(16),FLinearColor Color=FLinearColor(.018f,.035f,.065f,1));
    UWidget* Badge(const FString&,FLinearColor Color,int32 Size=16);
    void Home();void Profile();void Loadout();void Cards();void CardDetail();void Settings();void Battle();void UpdateBattleHUD(float);void Developer(UVerticalBox*);void UpdateTrainingReadout();void FieldManual();void Replays();void ReplayView();void UpdateReplayHUD();void MatchAnalysis();void Meta();void MetaRows();void PatchNotes();
    void SelectPreset(int32 Index);void ToggleDraft(const FString& Id);void Say(const FString& Message);
    void CaptureFilters();
    void ShowJSON(const TSharedPtr<FJsonObject>& Object,UVerticalBox* Target=nullptr,const FString& Prefix=TEXT(""));
    void Bookmarks();
    TArray<TSharedPtr<FJsonObject>> FilterRows(TArray<TSharedPtr<FJsonObject>> Values,bool CardFilter=false)const;
    void ExportMeta();
};
