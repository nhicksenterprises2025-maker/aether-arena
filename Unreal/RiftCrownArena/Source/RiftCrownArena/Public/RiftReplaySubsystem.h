#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Simulation/RiftSimulation.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "RiftReplaySubsystem.generated.h"

struct FRiftReplayWriteJob;
struct FRiftReplayBalanceContext;

UCLASS()
class RIFTCROWNARENA_API URiftReplaySubsystem : public UGameInstanceSubsystem, public FTickableGameObject {
    GENERATED_BODY()
  public:
    void Initialize(FSubsystemCollectionBase &Collection) override;
    void Deinitialize() override;
    void Tick(float DeltaTime) override;
    TStatId GetStatId() const override;
    bool IsTickable() const override;
    void BeginRecording(const rift::MatchOptions &Options, bool Training);
    void RecordEvent(const rift::Event &Event);
    void Sample(const rift::Snapshot &Snapshot);
    void EndRecording(const rift::Snapshot &Snapshot, bool Abandoned);
    bool IsSaving() const {
        return !PendingWrites.IsEmpty();
    }
    // Explicit waits are for shutdown and automation; normal UI uses nonblocking Tick completion.
    bool FlushPendingWrites();
    UFUNCTION(BlueprintCallable) bool OpenReplay(const FString &Filename);
    UFUNCTION(BlueprintCallable) void CloseReplay();
    UFUNCTION(BlueprintCallable) void Seek(float Seconds);
    UFUNCTION(BlueprintCallable) void SetSpeed(float Value);
    UFUNCTION(BlueprintCallable) bool ImportReplay(const FString &Filename);
    UFUNCTION(BlueprintCallable) bool ExportReplay(const FString &Filename, const FString &Destination);
    UFUNCTION(BlueprintCallable) bool DeleteReplay(const FString &Filename);
    void Advance(float DeltaTime);
    float Position() const {
        return float(PlaybackTime);
    }
    float Duration() const {
        return float(PlaybackDuration);
    }
    // Recording timestamps are doubles; sliders retain their float interface.
    // Use the exact timeline for inclusive event queries and final snapshots.
    double TimelinePosition() const {
        return PlaybackTime;
    }
    double TimelineDuration() const {
        return PlaybackDuration;
    }
    float Speed() const {
        return PlaybackSpeed;
    }
    bool IsPlaying() const {
        return Loaded.IsValid();
    }
    FString LastError;
    FString LatestFilename;
    TSharedPtr<FJsonObject> CurrentAnalysis() const;
    // Recorded mechanics are provenance, not authentication of an arbitrary archive.
    // Legacy archives expose any unavailable sparse reconstruction fields here.
    TSharedRef<FJsonObject> ReconstructionDiagnostics() const;
    TArray<TSharedPtr<FJsonObject>> EventsNear(double Time, double Window = 5) const;
    // Immutable recorded first-hit cache permits exact backward/forward seeks.
    bool HasTakenDamageBy(uint64 EntityId, double Time) const;
    TArray<TSharedPtr<FJsonObject>> RecordedStates() const;
    static TSharedRef<FJsonObject> SnapshotJSON(const rift::Snapshot &Snapshot);
    static bool SnapshotFromJSON(const TSharedPtr<FJsonObject> &Object, rift::Snapshot &Snapshot);
    static bool ValidateRecording(const TSharedPtr<FJsonObject> &Object, FString &Error);

  private:
    friend class FRiftReplayIntegrationTest;
    friend class FRiftFullReplayIntegrationTest;
    friend class FRiftReplaySizeBudgetIntegrationTest;
    friend class FRiftSpellCastReplayIntegrationTest;
    friend class FRiftHistoricalReplayIntegrationTest;
    friend class FRiftExpansionMechanicsIntegrationTest;
    TSharedPtr<FJsonObject> Recording, Loaded;
    TArray<TSharedPtr<FJsonValue>> RecordedEvents, RecordedSamples;
    rift::Snapshot View;
    double LastSample = -1;
    double PlaybackTime = 0, PlaybackDuration = 0;
    float PlaybackSpeed = 1;
    int32 EventCursor = 0;
    uint64 RecordedSequence = 0;
    int64 RecordedMinimumBytes = 0;
    FString RecordingBudgetError;
    TArray<rift::Event> PlaybackEvents;
    TSharedPtr<FRiftReplayBalanceContext> PlaybackBalance;
    TMap<uint64,double> FirstDamageTimes;
    TArray<TSharedPtr<FRiftReplayWriteJob, ESPMode::ThreadSafe>> PendingWrites;
    FString LatestQueuedFilename;
    void CompleteWrites(bool Wait, bool Publish = true);
    bool AccountRecordingObject(const TSharedPtr<FJsonObject>& Object);
    void RejectRecording(const FString& Error);
    static bool SerializeRecording(const TSharedPtr<FJsonObject>& Object, TArray<uint8>& UTF8Bytes,
                                   FString& Error, int64 ByteLimit = 256 * 1024 * 1024);
    void UpdateView();
    void ResetEventCursor();
};
