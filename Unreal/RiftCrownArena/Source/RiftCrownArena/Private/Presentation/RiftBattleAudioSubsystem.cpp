#include "Presentation/RiftBattleAudioSubsystem.h"
#include "RiftDiagnostics.h"
#include "RiftProfileSubsystem.h"
#include "RiftUIWidget.h"
#include "AudioDevice.h"
#include "Blueprint/WidgetTree.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWave.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TimerManager.h"

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
    if (Voices.Num()>=32) return;
    UWorld* World=BattleWorld.IsValid()?BattleWorld.Get():GetGameInstance()->GetWorld();
    if (World) if (auto* Source=Sound(Name))
        if (auto* Component=UGameplayStatics::SpawnSound2D(World,Source,Master*UI))
            Voices.Add({Component,1.f,true});
}

FString URiftBattleAudioSubsystem::RunAudioSmokeJSON(URiftUIWidget* Interface)
{
    auto Report=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Checks,Errors;
    auto Check=[&](const TCHAR* Name,bool Passed)
    {
        auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("name"),Name);Item->SetBoolField(TEXT("passed"),Passed);
        Checks.Add(MakeShared<FJsonValueObject>(Item));if(!Passed)Errors.Add(MakeShared<FJsonValueString>(Name));
    };
    auto FloatCheck=[&](const TCHAR* Name,float Actual,float Expected)
    {
        auto Item=MakeShared<FJsonObject>();const bool Passed=FMath::IsNearlyEqual(Actual,Expected,.0001f);
        Item->SetStringField(TEXT("name"),Name);Item->SetNumberField(TEXT("actual"),Actual);Item->SetNumberField(TEXT("expected"),Expected);Item->SetBoolField(TEXT("passed"),Passed);
        Checks.Add(MakeShared<FJsonValueObject>(Item));if(!Passed)Errors.Add(MakeShared<FJsonValueString>(Name));
    };
    auto Result=[&]()
    {
        Report->SetArrayField(TEXT("checks"),Checks);Report->SetArrayField(TEXT("errors"),Errors);
        Report->SetBoolField(TEXT("passed"),Errors.IsEmpty());Report->SetStringField(TEXT("audibleLoopQuality"),TEXT("Not assessed by automated component smoke; requires listening."));
        FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Report,Writer);return Text;
    };
    UWorld* World=BattleWorld.Get();auto* Profile=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();
    Check(TEXT("liveAudioDevice"),World&&World->GetAudioDevice().IsValid());
    Check(TEXT("profileAvailable"),Profile!=nullptr);Check(TEXT("settingsInterfaceAvailable"),Interface&&Interface->WidgetTree);
    if(!World||!Profile||!Interface||!Interface->WidgetTree||!World->GetAudioDevice().IsValid())return Result();
    const int32 DeviceChannels=World->GetAudioDevice()->GetMaxChannels();
    Report->SetNumberField(TEXT("deviceMaxChannels"),DeviceChannels);Check(TEXT("physicalBudgetIncludes32OneShotsAnd2Loops"),DeviceChannels>=34);
    const auto OriginalSettings=Profile->Settings;
    Interface->Navigate(TEXT("Settings"));TArray<URiftValueSlider*> Sliders;
    Interface->WidgetTree->ForEachWidget([&](UWidget* Widget){if(auto* Slider=Cast<URiftValueSlider>(Widget))Sliders.Add(Slider);});
    Check(TEXT("actualSettingsAudioSliders"),Sliders.Num()>=4);
    if(Sliders.Num()<4){Interface->Navigate(TEXT("Home"));return Result();}
    auto SetSlider=[&](int32 Index,float Value)
    {Sliders[Index]->SetValue(Value);Sliders[Index]->OnValueChanged.Broadcast(Value);RefreshVolumes();};
    const TCHAR* SoundNames[]={TEXT("aether_three"),TEXT("aether_two"),TEXT("arc_cast"),TEXT("arc_impact"),TEXT("arrow_flight"),TEXT("arrow_hit"),TEXT("bat_bite"),TEXT("bow_release"),TEXT("building_hit"),TEXT("bullet_burst"),TEXT("charge"),TEXT("core_awaken"),TEXT("defeat"),TEXT("deploy"),TEXT("dual_attack"),TEXT("frost_attack"),TEXT("frost_slow"),TEXT("heavy_slam"),TEXT("manta_cast"),TEXT("manta_impact"),TEXT("meteor_impact"),TEXT("meteor_tick"),TEXT("music_rift"),TEXT("nova_impact"),TEXT("overtime"),TEXT("river_ambience"),TEXT("shield_hit"),TEXT("storm_bolt"),TEXT("storm_pulse"),TEXT("stun"),TEXT("sword_attack"),TEXT("sword_hit"),TEXT("tiebreaker"),TEXT("tower_destroy"),TEXT("tower_hit"),TEXT("ui_click"),TEXT("ui_error"),TEXT("ui_hover"),TEXT("ui_save"),TEXT("victory"),TEXT("wing_air")};
    int32 Loaded=0;for(const auto* Name:SoundNames)if(auto* Asset=Sound(FName(Name)))if(Asset->GetDuration()>0)++Loaded;
    Report->SetNumberField(TEXT("loadedSounds"),Loaded);Check(TEXT("all41ImportedSoundsLoad"),Loaded==41);
    for(auto& Voice:Voices)if(Voice.Component.IsValid())Voice.Component->Stop();Voices.Reset();
    SetSlider(0,.6f);SetSlider(1,.35f);SetSlider(2,.4f);SetSlider(3,.8f);
    FloatCheck(TEXT("masterCallback"),Profile->Settings.MasterVolume,.6f);FloatCheck(TEXT("musicCallback"),Profile->Settings.MusicVolume,.35f);
    FloatCheck(TEXT("sfxCallback"),Profile->Settings.SFXVolume,.4f);FloatCheck(TEXT("uiCallback"),Profile->Settings.UIVolume,.8f);
    if(!Music||!Music->IsPlaying())SetBattleWorld(World);
    Check(TEXT("musicPlaying"),Music&&Music->IsPlaying());Check(TEXT("ambiencePlaying"),Ambience&&Ambience->IsPlaying());
    if(Music){FloatCheck(TEXT("musicMasterProduct"),Music->VolumeMultiplier,.21f);if(auto* Wave=Cast<USoundWave>(Music->Sound)){Check(TEXT("musicLoopConfigured"),Wave->bLooping);Check(TEXT("musicStreamed"),Wave->IsStreaming());}}
    if(Ambience)FloatCheck(TEXT("ambienceMasterProduct"),Ambience->VolumeMultiplier,.0624f);
    PlayEffect(TEXT("sword_hit"),FVector::ZeroVector,.5f);PlayUI(TEXT("ui_save"));
    UAudioComponent* EffectVoice=nullptr;UAudioComponent* UIVoice=nullptr;
    for(const auto& Voice:Voices)if(Voice.UI)UIVoice=Voice.Component.Get();else EffectVoice=Voice.Component.Get();
    Check(TEXT("effectPlaying"),EffectVoice&&EffectVoice->IsPlaying());Check(TEXT("uiPlaying"),UIVoice&&UIVoice->IsPlaying());
    if(EffectVoice)FloatCheck(TEXT("effectMasterProduct"),EffectVoice->VolumeMultiplier,.12f);
    if(UIVoice)FloatCheck(TEXT("uiMasterProduct"),UIVoice->VolumeMultiplier,.48f);
    SetSlider(1,0.f);if(Music)FloatCheck(TEXT("musicIndependentMute"),Music->VolumeMultiplier,0.f);
    if(UIVoice)FloatCheck(TEXT("musicMuteKeepsUI"),UIVoice->VolumeMultiplier,.48f);
    SetSlider(1,.35f);SetSlider(2,0.f);
    if(EffectVoice)FloatCheck(TEXT("sfxIndependentMute"),EffectVoice->VolumeMultiplier,0.f);
    if(UIVoice)FloatCheck(TEXT("sfxMuteKeepsUI"),UIVoice->VolumeMultiplier,.48f);
    SetSlider(2,.4f);SetSlider(3,0.f);
    if(UIVoice)FloatCheck(TEXT("uiIndependentMute"),UIVoice->VolumeMultiplier,0.f);
    if(EffectVoice)FloatCheck(TEXT("uiMuteKeepsSFX"),EffectVoice->VolumeMultiplier,.12f);
    SetSlider(3,.8f);SetSlider(0,0.f);
    if(Music)FloatCheck(TEXT("masterMutesMusic"),Music->VolumeMultiplier,0.f);
    if(EffectVoice)FloatCheck(TEXT("masterMutesSFX"),EffectVoice->VolumeMultiplier,0.f);
    if(UIVoice)FloatCheck(TEXT("masterMutesUI"),UIVoice->VolumeMultiplier,0.f);
    const int32 BeforeMuted=Voices.Num();PlayEffect(TEXT("sword_hit"),FVector::ZeroVector);PlayUI();Check(TEXT("mutedPlayCreatesNoVoice"),Voices.Num()==BeforeMuted);
    SetSlider(0,.6f);
    for(auto& Voice:Voices)if(Voice.Component.IsValid())Voice.Component->Stop();Voices.Reset();
    int32 Maximum=0;for(int32 Index=0;Index<48;++Index){PlayUI(TEXT("ui_save"));PlayEffect(TEXT("sword_hit"),FVector::ZeroVector);Maximum=FMath::Max(Maximum,Voices.Num());}
    Report->SetNumberField(TEXT("maximumOneShotVoices"),Maximum);Check(TEXT("combinedUIAndSFXVoiceLimit"),Maximum==32&&Voices.Num()<=32);
    for(auto& Voice:Voices)if(Voice.Component.IsValid())Voice.Component->Stop();Voices.Reset();
    Profile->Settings=OriginalSettings;RefreshVolumes();Interface->Navigate(TEXT("Home"));
    Check(TEXT("settingsRestored"),Profile->Settings.MasterVolume==OriginalSettings.MasterVolume&&Profile->Settings.MusicVolume==OriginalSettings.MusicVolume&&Profile->Settings.SFXVolume==OriginalSettings.SFXVolume&&Profile->Settings.UIVolume==OriginalSettings.UIVolume);
    return Result();
}

void URiftBattleAudioSubsystem::RunAudioSmoke(URiftUIWidget* Interface,TFunction<void(const FString&)> Completed)
{
    const FString Initial=RunAudioSmokeJSON(Interface);TSharedPtr<FJsonObject> Report;
    auto Reader=TJsonReaderFactory<>::Create(Initial);
    if(!FJsonSerializer::Deserialize(Reader,Report)||!Report.IsValid()||!Report->GetBoolField(TEXT("passed")))
    {Completed(Initial);return;}
    auto* World=BattleWorld.Get();auto* Profile=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();
    const auto OriginalSettings=Profile->Settings;
    Profile->Settings.MasterVolume=.6f;Profile->Settings.MusicVolume=.35f;Profile->Settings.SFXVolume=.4f;Profile->Settings.UIVolume=.8f;RefreshVolumes();
    // Let the audio mixer consume a full bounded burst across several buffers;
    // immediate component registration alone cannot prove that loops survive it.
    for(int32 Index=0;Index<16;++Index){PlayUI(TEXT("ui_save"));PlayEffect(TEXT("sword_hit"),FVector::ZeroVector);}
    const double Started=World->GetTimeSeconds();TWeakObjectPtr<URiftBattleAudioSubsystem> WeakThis(this);
    FTimerHandle MixedTimer;World->GetTimerManager().SetTimer(MixedTimer,[WeakThis,Report,OriginalSettings,Started,Completed=MoveTemp(Completed)]()
    {
        auto* Audio=WeakThis.Get();if(!Audio)return;
        auto Checks=Report->GetArrayField(TEXT("checks"));auto Errors=Report->GetArrayField(TEXT("errors"));
        auto Check=[&](const TCHAR* Name,bool Passed)
        {auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("name"),Name);Item->SetBoolField(TEXT("passed"),Passed);Checks.Add(MakeShared<FJsonValueObject>(Item));if(!Passed)Errors.Add(MakeShared<FJsonValueString>(Name));};
        const auto* CurrentWorld=Audio->BattleWorld.Get();const double Delay=CurrentWorld?CurrentWorld->GetTimeSeconds()-Started:0;
        Report->SetNumberField(TEXT("postMixGameSeconds"),Delay);Check(TEXT("postMixDeferredAcrossGameTicks"),Delay>=.1);
        Check(TEXT("musicSurvivesBoundedBurst"),Audio->Music&&Audio->Music->IsPlaying());
        Check(TEXT("musicRetainsPhysicalVoice"),Audio->Music&&!Audio->Music->IsVirtualized());
        Check(TEXT("ambienceSurvivesBoundedBurst"),Audio->Ambience&&Audio->Ambience->IsPlaying());
        Check(TEXT("ambienceRetainsPhysicalVoice"),Audio->Ambience&&!Audio->Ambience->IsVirtualized());
        int32 ActiveOneShots=0;for(auto& Voice:Audio->Voices)if(Voice.Component.IsValid()){if(Voice.Component->IsPlaying())++ActiveOneShots;Voice.Component->Stop();}
        Report->SetNumberField(TEXT("postMixActiveOneShots"),ActiveOneShots);Audio->Voices.Reset();
        auto* RestoredProfile=Audio->GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();RestoredProfile->Settings=OriginalSettings;Audio->RefreshVolumes();
        Check(TEXT("postMixSettingsRestored"),RestoredProfile->Settings.MasterVolume==OriginalSettings.MasterVolume&&RestoredProfile->Settings.MusicVolume==OriginalSettings.MusicVolume&&RestoredProfile->Settings.SFXVolume==OriginalSettings.SFXVolume&&RestoredProfile->Settings.UIVolume==OriginalSettings.UIVolume);
        Report->SetArrayField(TEXT("checks"),Checks);Report->SetArrayField(TEXT("errors"),Errors);Report->SetBoolField(TEXT("passed"),Errors.IsEmpty());
        FString Result;auto Writer=TJsonWriterFactory<>::Create(&Result);FJsonSerializer::Serialize(Report.ToSharedRef(),Writer);Completed(Result);
    },.12f,false);
}
