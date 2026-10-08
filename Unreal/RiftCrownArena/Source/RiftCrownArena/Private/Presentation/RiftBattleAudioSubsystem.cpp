#include "Presentation/RiftBattleAudioSubsystem.h"
#include "RiftDiagnostics.h"
#include "RiftProfileSubsystem.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWave.h"

void URiftBattleAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Collection.InitializeDependency<URiftProfileSubsystem>();
    RefreshVolumes();
}
void URiftBattleAudioSubsystem::Deinitialize()
{
    for (auto& Voice:Voices) if (Voice.Component.IsValid()) Voice.Component->Stop();
    if (Music) Music->Stop();
    if (Ambience) Ambience->Stop();
    Voices.Reset(); Sounds.Reset(); Music=nullptr; Ambience=nullptr; BattleWorld.Reset();
    Super::Deinitialize();
}
USoundBase* URiftBattleAudioSubsystem::Sound(FName Name)
{
    if (auto* Entry=Sounds.Find(Name)) return Entry->Get();
    const FString Asset=FString::Printf(TEXT("/Game/Rift/Audio/SFX_%s.SFX_%s"),*Name.ToString(),*Name.ToString());
    USoundBase* Result=LoadObject<USoundBase>(nullptr,*Asset);
    Sounds.Add(Name,Result);
    if (!Result) RIFT_LOG(LogRift,Error,TEXT("Authored audio asset missing: %s"),*Asset);
    return Result;
}
void URiftBattleAudioSubsystem::SetBattleWorld(UWorld* World)
{
    if (BattleWorld.Get()==World && Music && Music->IsPlaying()) return;
    if (Music) { Music->Stop(); Music->DestroyComponent(); }
    if (Ambience) { Ambience->Stop(); Ambience->DestroyComponent(); }
    Music=nullptr; Ambience=nullptr; BattleWorld=World;
    if (!World) return;
    // The imported music master is streamed by UE; no PCM file is read during play.
    if (auto* Score=Sound(TEXT("music_rift")))
    {
        if (auto* Wave=Cast<USoundWave>(Score)) Wave->bLooping=true;
        Music=UGameplayStatics::SpawnSound2D(World,Score,Master*MusicGain,1.f,0.f,nullptr,false,false);
    }
    if (auto* River=Sound(TEXT("river_ambience")))
    {
        if (auto* Wave=Cast<USoundWave>(River)) Wave->bLooping=true;
        Ambience=UGameplayStatics::SpawnSound2D(World,River,Master*SFX*.26f,1.f,0.f,nullptr,false,false);
    }
}
void URiftBattleAudioSubsystem::RefreshVolumes()
{
    if (auto* Profile=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>())
    {
        Master=FMath::Clamp(Profile->Settings.MasterVolume,0.f,1.f);
        MusicGain=FMath::Clamp(Profile->Settings.MusicVolume,0.f,1.f);
        SFX=FMath::Clamp(Profile->Settings.SFXVolume,0.f,1.f);
        UI=FMath::Clamp(Profile->Settings.UIVolume,0.f,1.f);
    }
    if (Music) Music->SetVolumeMultiplier(Master*MusicGain);
    if (Ambience) Ambience->SetVolumeMultiplier(Master*SFX*.26f);
    for (int32 Index=Voices.Num()-1; Index>=0; --Index)
    {
        auto& Voice=Voices[Index];
        if (!Voice.Component.IsValid() || !Voice.Component->IsPlaying()) Voices.RemoveAtSwap(Index);
        else Voice.Component->SetVolumeMultiplier(Master*(Voice.UI?UI:SFX)*Voice.Gain);
    }
}
void URiftBattleAudioSubsystem::PlayEffect(FName Name,FVector Position,float Gain,float Pitch)
{
    UWorld* World=BattleWorld.Get(); if (!World) return;
    RefreshVolumes(); if (Master*SFX*Gain<=0.f) return;
    // A bounded mix prevents swarm and area damage events from burying the score.
    if (Voices.Num()>=32) return;
    if (auto* Source=Sound(Name))
        if (auto* Component=UGameplayStatics::SpawnSoundAtLocation(World,Source,Position,FRotator::ZeroRotator,
            Master*SFX*Gain,Pitch,0.f,nullptr,nullptr,true))
            Voices.Add({Component,Gain,false});
}
void URiftBattleAudioSubsystem::PlayUI(FName Name)
{
    RefreshVolumes(); if (Master*UI<=0.f) return;
    UWorld* World=BattleWorld.IsValid()?BattleWorld.Get():GetGameInstance()->GetWorld();
    if (World) if (auto* Source=Sound(Name))
        if (auto* Component=UGameplayStatics::SpawnSound2D(World,Source,Master*UI))
            Voices.Add({Component,1.f,true});
}
