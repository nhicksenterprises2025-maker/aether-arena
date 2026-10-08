#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Simulation/RiftSimulation.h"
#include "Dom/JsonObject.h"
#include "RiftReplaySubsystem.generated.h"

UCLASS()
class RIFTCROWNARENA_API URiftReplaySubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    void BeginRecording(const rift::MatchOptions& Options,bool Training);
    void RecordEvent(const rift::Event& Event);
    void Sample(const rift::Snapshot& Snapshot);
    void EndRecording(const rift::Snapshot& Snapshot,bool Abandoned);
    UFUNCTION(BlueprintCallable) bool OpenReplay(const FString& Filename);
    UFUNCTION(BlueprintCallable) void CloseReplay();
    UFUNCTION(BlueprintCallable) void Seek(float Seconds);
    UFUNCTION(BlueprintCallable) void SetSpeed(float Value);
    UFUNCTION(BlueprintCallable) bool ImportReplay(const FString& Filename);
    UFUNCTION(BlueprintCallable) bool ExportReplay(const FString& Filename,const FString& Destination);
    UFUNCTION(BlueprintCallable) bool DeleteReplay(const FString& Filename);
    void Advance(float DeltaTime);
    float Position()const{return PlaybackTime;}
    float Duration()const{return PlaybackDuration;}
    float Speed()const{return PlaybackSpeed;}
    bool IsPlaying()const{return Loaded.IsValid();}
    FString LastError;
    FString LatestFilename;
    TSharedPtr<FJsonObject> CurrentAnalysis()const;
    TArray<TSharedPtr<FJsonObject>> EventsNear(double Time,double Window=5)const;
    TArray<TSharedPtr<FJsonObject>> RecordedStates()const;
    static TSharedRef<FJsonObject> SnapshotJSON(const rift::Snapshot& Snapshot);
    static bool SnapshotFromJSON(const TSharedPtr<FJsonObject>& Object,rift::Snapshot& Snapshot);
    static bool ValidateRecording(const TSharedPtr<FJsonObject>& Object,FString& Error);
private:
    friend class FRiftReplayIntegrationTest;
    TSharedPtr<FJsonObject> Recording,Loaded;
    TArray<TSharedPtr<FJsonValue>> RecordedEvents,RecordedSamples;
    rift::Snapshot View;
    double LastSample=-1;
    float PlaybackTime=0,PlaybackDuration=0,PlaybackSpeed=1;
    int32 EventCursor=0;
    uint64 RecordedSequence=0;
    TArray<rift::Event> PlaybackEvents;
    void UpdateView();
    void ResetEventCursor();
};
