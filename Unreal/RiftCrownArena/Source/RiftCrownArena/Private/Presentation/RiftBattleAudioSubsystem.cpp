#include "Presentation/RiftBattleAudioSubsystem.h"
#include "RiftDiagnostics.h"
#include "RiftMatchSubsystem.h"
#include "RiftProfileSubsystem.h"
#include "RiftUIWidget.h"
#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioMixerDevice.h"
#include "Blueprint/WidgetTree.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SampleBufferIO.h"
#include "SubmixEffects/AudioMixerSubmixEffectDynamicsProcessor.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TimerManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"

namespace
{
const TCHAR* VariedCues[]={TEXT("sword_hit"),TEXT("shield_hit"),TEXT("tower_hit"),TEXT("building_hit"),TEXT("arrow_hit"),TEXT("sword_attack"),TEXT("dual_attack"),TEXT("bow_release"),TEXT("heavy_slam"),TEXT("bat_bite")};
uint8 CuePriority(FName Name)
{
    if(Name==TEXT("victory")||Name==TEXT("defeat")||Name==TEXT("tower_destroy"))return 95;
    if(Name==TEXT("core_awaken")||Name==TEXT("overtime")||Name==TEXT("tiebreaker"))return 90;
    if(Name==TEXT("meteor_impact")||Name==TEXT("nova_impact")||Name==TEXT("bullet_burst"))return 80;
    if(Name==TEXT("deploy")||Name==TEXT("tower_hit")||Name==TEXT("storm_pulse"))return 60;
    if(Name==TEXT("wing_air")||Name==TEXT("arrow_flight")||Name==TEXT("bat_bite"))return 15;
    return 30;
}
float CueCooldown(FName Name)
{
    if(Name==TEXT("frost_slow")||Name==TEXT("stun"))return .18f;
    if(Name==TEXT("bat_bite")||Name==TEXT("wing_air")||Name==TEXT("arrow_flight"))return .12f;
    if(Name==TEXT("meteor_tick"))return .16f;
    if(Name==TEXT("tower_destroy")||Name==TEXT("core_awaken"))return .09f;
    return .065f;
}
int32 PlayingRiverVoices(UWorld* World)
{
    int32 Count=0;
    for(TObjectIterator<UAudioComponent> Component;Component;++Component)
        if(Component->GetWorld()==World&&Component->IsPlaying()&&Component->Sound&&
            Component->Sound->GetFName()==FName(TEXT("SFX_river_ambience")))++Count;
    return Count;
}
bool SaveMixedOutput(UWorld* World,USoundSubmix* Mix,const TSharedPtr<FJsonObject>& Report,const TCHAR* Field,const TCHAR* FileName,bool ExpectSilence=false)
{
    auto Item=MakeShared<FJsonObject>();Report->SetObjectField(Field,Item);
    auto* Device=FAudioDeviceManager::GetAudioMixerDeviceFromWorldContext(World);
    if(!Device){Item->SetStringField(TEXT("error"),TEXT("No live mixer device"));return false;}
    // UE otherwise silently records its default master when this submix is
    // missing. Require the actual route whose effect chain we are measuring.
    if(!Device->GetSubmixInstance(Mix).IsValid())
    {Item->SetStringField(TEXT("error"),TEXT("MasterMix is not registered with the live mixer"));return false;}
    float Channels=0,SampleRate=0;
    auto& Samples=Device->StopRecording(Mix,Channels,SampleRate);
    double Energy=0;float Peak=0;int32 Clipped=0;
    for(float Sample:Samples){Peak=FMath::Max(Peak,FMath::Abs(Sample));Energy+=double(Sample)*Sample;if(FMath::Abs(Sample)>=1.f)++Clipped;}
    const double RMS=Samples.IsEmpty()?0:FMath::Sqrt(Energy/Samples.Num());
    Item->SetNumberField(TEXT("samples"),Samples.Num());Item->SetNumberField(TEXT("channels"),Channels);
    Item->SetNumberField(TEXT("sampleRate"),SampleRate);Item->SetNumberField(TEXT("peak"),Peak);
    Item->SetNumberField(TEXT("rms"),RMS);Item->SetNumberField(TEXT("clippedFloatSamples"),Clipped);
    Item->SetNumberField(TEXT("seconds"),Channels>0&&SampleRate>0?Samples.Num()/(Channels*SampleRate):0);
    Item->SetStringField(TEXT("captureStage"),TEXT("Live MasterMix output after the submix effect chain; before PCM encoding"));
    Item->SetBoolField(TEXT("silenceExpected"),ExpectSilence);
    if(Samples.IsEmpty()||Channels<1||SampleRate<=0)return false;
    FString SmokePath;FParse::Value(FCommandLine::Get(),TEXT("RiftAudioSmoke="),SmokePath);
    const FString Directory=SmokePath.IsEmpty()?FPaths::Combine(URiftProfileSubsystem::SaveRoot(),TEXT("AudioQA")):FPaths::GetPath(SmokePath);
    Audio::TSampleBuffer<int16> PCM(Samples,Channels,SampleRate);
    Audio::FSoundWavePCMWriter Writer;FString SavedPath;
    const bool Saved=Writer.SynchronouslyWriteToWavFile(PCM,FileName,Directory,&SavedPath);
    Item->SetBoolField(TEXT("wavSaved"),Saved);Item->SetStringField(TEXT("wavFile"),FPaths::GetCleanFilename(SavedPath));
    const bool LevelsPassed=ExpectSilence?Peak<=.00001f&&RMS<=.000001:Peak>.0001f&&RMS>.00001&&Clipped==0&&Peak<.95f;
    return Saved&&Samples.Num()>Channels*SampleRate*.15f&&LevelsPassed;
}
}

void URiftBattleAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Collection.InitializeDependency<URiftProfileSubsystem>();
    // All authored voices route through one linked stereo look-ahead limiter.
    // It normally does no work; it catches overlapping impacts at 4x/stress.
    MasterMix=NewObject<USoundSubmix>(this,TEXT("RiftMasterMix"),RF_Transient);
    MasterMix->bAutoDisable=false;
    Limiter=NewObject<USubmixEffectDynamicsProcessorPreset>(this,TEXT("RiftMixLimiter"),RF_Transient);
    FSubmixEffectDynamicsProcessorSettings Settings;
    Settings.DynamicsProcessorType=ESubmixEffectDynamicsProcessorType::Limiter;
    Settings.PeakMode=ESubmixEffectDynamicsPeakMode::Peak;
    Settings.LinkMode=ESubmixEffectDynamicsChannelLinkMode::Peak;
    Settings.ThresholdDb=-3.f;Settings.KneeBandwidthDb=0.f;
    // UE's Peak mode still smooths its detector when AttackTime is nonzero.
    // Detect isolated samples immediately, then delay the audible signal so
    // gain reduction precedes the impact. The threshold/release also leave
    // margin for the detector's decay during that delay; no PCM clamp is used.
    Settings.LookAheadMsec=5.f;Settings.AttackTimeMsec=0.f;Settings.ReleaseTimeMsec=120.f;
    Settings.bAnalogMode=false;Settings.InputGainDb=0.f;Settings.OutputGainDb=0.f;
    // UE's SetSettings updates the thread-safe runtime copy, while later effect
    // creation calls Init() and copies the public preset Settings again. Seed
    // both so a newly created voice cannot revert to the default compressor.
    Limiter->Settings=Settings;Limiter->Init();Limiter->SetSettings(Settings);
    MasterMix->SubmixEffectChain.Add(Limiter);
    RefreshVolumes();
}
void URiftBattleAudioSubsystem::Deinitialize()
{
    for (auto& Voice:Voices) if (Voice.Component.IsValid()) Voice.Component->Stop();
    if (Music) Music->Stop();
    if(auto* Manager=FAudioDeviceManager::Get())for(const uint32 DeviceId:RegisteredDeviceIds)
        if(auto* Device=Manager->GetAudioDeviceRaw(DeviceId))Device->UnregisterSoundSubmix(MasterMix,false);
    RegisteredDeviceIds.Reset();
    for(const auto& Entry:Sounds)if(Entry.Value&&Entry.Value->SoundSubmixObject==MasterMix)
        Entry.Value->SoundSubmixObject=OriginalSubmixRoutes.FindRef(Entry.Key);
    OriginalSubmixRoutes.Reset();
    Voices.Reset();LastPlayTime.Reset();VariantSequence.Reset();Sounds.Reset();Music=nullptr;BattleWorld.Reset();
    Limiter=nullptr;MasterMix=nullptr;
    Super::Deinitialize();
}
USoundBase* URiftBattleAudioSubsystem::Sound(FName Name)
{
    if (auto* Entry=Sounds.Find(Name)) return Entry->Get();
    const FString Asset=FString::Printf(TEXT("/Game/Rift/Audio/SFX_%s.SFX_%s"),*Name.ToString(),*Name.ToString());
    USoundBase* Result=LoadObject<USoundBase>(nullptr,*Asset);
    Sounds.Add(Name,Result);
    if(Result){OriginalSubmixRoutes.Add(Name,Result->SoundSubmixObject);Result->SoundSubmixObject=MasterMix;}
    if (!Result) RIFT_LOG(LogRift,Error,TEXT("Authored audio asset missing: %s"),*Asset);
    return Result;
}
bool URiftBattleAudioSubsystem::EnsureMixRegistered(UWorld* World)
{
    if(!World)return false;
    auto* Device=FAudioDeviceManager::GetAudioMixerDeviceFromWorldContext(World);
    if(!Device||!MasterMix)return false;
    const uint32 DeviceId=World->GetAudioDevice().GetDeviceID();
    if(!RegisteredDeviceIds.Contains(DeviceId))
    {
        // NewObject does not execute USoundSubmixBase::PostLoad, which normally
        // registers authored submixes. Queue registration before sound playback
        // so explicit base sends resolve to a live mixer instance and parent.
        Device->RegisterSoundSubmix(MasterMix,true);
        RegisteredDeviceIds.Add(DeviceId);
    }
    return true;
}
void URiftBattleAudioSubsystem::SetBattleWorld(UWorld* World)
{
    if (BattleWorld.Get()==World && Music && Music->IsPlaying()) return;
    if (Music) { Music->Stop(); Music->DestroyComponent(); }
    Music=nullptr; BattleWorld=World;
    StopOneShots();MusicDuck=1.f;
    if (!World||!EnsureMixRegistered(World)) return;
    // Preload short foley variations before play, preventing first-hit disk work.
    for(const auto* Name:VariedCues)for(int32 Index=0;Index<3;++Index)
        Sound(Index?FName(*FString::Printf(TEXT("%s_v%d"),Name,Index+1)):FName(Name));
    // The imported music master is streamed by UE; no PCM file is read during play.
    if (auto* Score=Sound(TEXT("music_rift")))
    {
        if (auto* Wave=Cast<USoundWave>(Score)) Wave->bLooping=true;
        Music=UGameplayStatics::SpawnSound2D(World,Score,Master*MusicGain,1.f,0.f,nullptr,false,false);
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
    PruneVoices();
    int32 EffectCount=0,UICount=0;
    for(const auto& Voice:Voices)Voice.UI?++UICount:++EffectCount;
    const float EffectHeadroom=EffectCount>5?FMath::Sqrt(5.f/EffectCount):1.f;
    const float UIHeadroom=UICount>2?FMath::Sqrt(2.f/UICount):1.f;
    if (Music) Music->SetVolumeMultiplier(Master*MusicGain*MusicDuck);
    for (int32 Index=Voices.Num()-1; Index>=0; --Index)
    {
        auto& Voice=Voices[Index];
        if (!Voice.Component.IsValid() || !Voice.Component->IsPlaying()) Voices.RemoveAtSwap(Index);
        else Voice.Component->SetVolumeMultiplier(Master*(Voice.UI?UI*UIHeadroom:SFX*EffectHeadroom)*Voice.Gain);
    }
}
bool URiftBattleAudioSubsystem::IsTickable() const {return !IsTemplate()&&BattleWorld.IsValid();}
TStatId URiftBattleAudioSubsystem::GetStatId() const {RETURN_QUICK_DECLARE_CYCLE_STAT(URiftBattleAudioSubsystem,STATGROUP_Tickables);}
void URiftBattleAudioSubsystem::Tick(float DeltaTime)
{
    PruneVoices();bool Announcement=false;
    for(const auto& Voice:Voices)if(!Voice.UI&&Voice.Priority>=80){Announcement=true;break;}
    MusicDuck=FMath::FInterpTo(MusicDuck,Announcement?.7f:1.f,DeltaTime,Announcement?18.f:3.f);
    RefreshVolumes();
}
void URiftBattleAudioSubsystem::PruneVoices()
{
    for(int32 Index=Voices.Num()-1;Index>=0;--Index)
        if(!Voices[Index].Component.IsValid()||!Voices[Index].Component->IsPlaying())Voices.RemoveAtSwap(Index);
}
void URiftBattleAudioSubsystem::StopOneShots()
{
    for(auto& Voice:Voices)if(Voice.Component.IsValid())Voice.Component->Stop();
    Voices.Reset();LastPlayTime.Reset();
}
void URiftBattleAudioSubsystem::FillSmokeBurst()
{
    StopOneShots();
    const TCHAR* UIExamples[]={TEXT("ui_save"),TEXT("ui_click"),TEXT("ui_error"),TEXT("ui_hover"),TEXT("aether_two"),TEXT("aether_three")};
    for(const auto* Cue:UIExamples)PlayUI(FName(Cue));
    for(const auto* Cue:VariedCues)for(int32 Variant=0;Variant<3;++Variant)
        PlayEffect(Variant?FName(*FString::Printf(TEXT("%s_v%d"),Cue,Variant+1)):FName(Cue),FVector::ZeroVector);
}
bool URiftBattleAudioSubsystem::AdmitVoice(FName Name,bool IsUI,uint8 Priority,float Cooldown)
{
    const double Now=FPlatformTime::Seconds();
    if(const auto* Last=LastPlayTime.Find(Name))if(Now-*Last<Cooldown)return false;
    PruneVoices();int32 Count=0;for(const auto& Voice:Voices)if(Voice.UI==IsUI)++Count;
    // Reserve six physical one-shot voices for the interface. Important events
    // replace a lower priority combat voice rather than vanishing in a swarm.
    const int32 Budget=IsUI?6:26;
    if(Count>=Budget||Voices.Num()>=32)
    {
        int32 Replace=INDEX_NONE;
        for(int32 Index=0;Index<Voices.Num();++Index)
            if(Voices[Index].UI==IsUI&&Voices[Index].Priority<Priority&&
                (Replace==INDEX_NONE||Voices[Index].Priority<Voices[Replace].Priority))Replace=Index;
        if(Replace==INDEX_NONE)return false;
        if(Voices[Replace].Component.IsValid())Voices[Replace].Component->Stop();
        Voices.RemoveAtSwap(Replace);
    }
    LastPlayTime.Add(Name,Now);return true;
}
FName URiftBattleAudioSubsystem::Variation(FName Name)
{
    for(const auto* Cue:VariedCues)if(Name==Cue)
    {
        const uint32 Index=VariantSequence.FindOrAdd(Name)++%3;
        return Index?FName(*FString::Printf(TEXT("%s_v%d"),Cue,Index+1)):Name;
    }
    return Name;
}
void URiftBattleAudioSubsystem::PlayEffect(FName Name,FVector Position,float Gain,float Pitch)
{
    UWorld* World=BattleWorld.Get(); if (!World||!EnsureMixRegistered(World)) return;
    RefreshVolumes(); if (Master*SFX*Gain<=0.f) return;
    const uint8 Priority=CuePriority(Name);
    if(!AdmitVoice(Name,false,Priority,CueCooldown(Name)))return;
    const FName Take=Variation(Name);
    const float VariationPitch=Take==Name?1.f:(Take.ToString().EndsWith(TEXT("2"))?.985f:1.015f);
    if (auto* Source=Sound(Take))
        if (auto* Component=UGameplayStatics::SpawnSoundAtLocation(World,Source,Position,FRotator::ZeroRotator,
            Master*SFX*Gain,Pitch*VariationPitch,0.f,nullptr,nullptr,true))
        {Voices.Add({Component,Gain,false,Priority,Name});RefreshVolumes();}
}
void URiftBattleAudioSubsystem::PlayUI(FName Name)
{
    RefreshVolumes(); if (Master*UI<=0.f) return;
    if(!AdmitVoice(Name,true,Name==TEXT("ui_error")?85:70,Name==TEXT("ui_hover")?.09f:.045f))return;
    UWorld* World=BattleWorld.IsValid()?BattleWorld.Get():GetGameInstance()->GetWorld();
    if (World&&EnsureMixRegistered(World)) if (auto* Source=Sound(Name))
        if (auto* Component=UGameplayStatics::SpawnSound2D(World,Source,Master*UI))
        {Voices.Add({Component,1.f,true,Name==TEXT("ui_error")?uint8(85):uint8(70),Name});RefreshVolumes();}
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
    Report->SetNumberField(TEXT("deviceMaxChannels"),DeviceChannels);Check(TEXT("physicalBudgetIncludes32OneShotsAndMusic"),DeviceChannels>=33);
    Check(TEXT("allVoicesHaveLimiterRoute"),MasterMix&&Limiter&&MasterMix->SubmixEffectChain.Contains(Limiter));
    auto* Mixer=FAudioDeviceManager::GetAudioMixerDeviceFromWorldContext(World);
    Check(TEXT("masterMixRegisteredWithLiveDevice"),Mixer&&Mixer->GetSubmixInstance(MasterMix).IsValid());
    const auto RuntimeDynamics=Limiter?Limiter->GetSettings():FSubmixEffectDynamicsProcessorSettings();
    Check(TEXT("stereoPeakLimiterBelowFullScale"),Limiter&&Limiter->Settings.DynamicsProcessorType==ESubmixEffectDynamicsProcessorType::Limiter&&
        RuntimeDynamics.DynamicsProcessorType==ESubmixEffectDynamicsProcessorType::Limiter&&RuntimeDynamics.LinkMode==ESubmixEffectDynamicsChannelLinkMode::Peak&&RuntimeDynamics.ThresholdDb<0);
    Report->SetNumberField(TEXT("runtimeLimiterThresholdDb"),RuntimeDynamics.ThresholdDb);
    Report->SetNumberField(TEXT("runtimeLimiterLookAheadMs"),RuntimeDynamics.LookAheadMsec);
    Report->SetNumberField(TEXT("runtimeLimiterAttackMs"),RuntimeDynamics.AttackTimeMsec);
    Report->SetNumberField(TEXT("runtimeLimiterReleaseMs"),RuntimeDynamics.ReleaseTimeMsec);
    Check(TEXT("limiterDetectsIsolatedPeaksWithoutAttackSmoothing"),Limiter&&RuntimeDynamics.PeakMode==ESubmixEffectDynamicsPeakMode::Peak&&
        RuntimeDynamics.AttackTimeMsec==0.f&&!RuntimeDynamics.bAnalogMode&&RuntimeDynamics.LookAheadMsec==5.f&&
        RuntimeDynamics.ThresholdDb==-3.f&&RuntimeDynamics.ReleaseTimeMsec==120.f);
    auto RiverLifecycle=MakeShared<FJsonObject>();Report->SetObjectField(TEXT("waterAmbienceLifecycle"),RiverLifecycle);
    RiverLifecycle->SetStringField(TEXT("cue"),TEXT("SFX_river_ambience"));
    RiverLifecycle->SetStringField(TEXT("inspection"),TEXT("Live UAudioComponent instances in the active world, matching river SoundWave and IsPlaying"));
    auto RiverCheck=[&](const TCHAR* Name,const TCHAR* Field)
    {const int32 Count=PlayingRiverVoices(World);RiverLifecycle->SetNumberField(Field,Count);Check(Name,Count==0);};
    // A silent, immediately stopped control proves this scan can see an actual
    // river component; a broken inspection must not report every page clean.
    auto* Control=UGameplayStatics::SpawnSound2D(World,Sound(TEXT("river_ambience")),0.f,1.f,0.f,nullptr,false,false);
    const int32 ControlRiverVoices=PlayingRiverVoices(World);RiverLifecycle->SetNumberField(TEXT("controlPlayingVoices"),ControlRiverVoices);
    Check(TEXT("riverVoiceInspectionDetectsSilentControl"),Control&&ControlRiverVoices==1);
    if(Control){Control->Stop();Control->DestroyComponent();}
    auto* InitialMusic=Music.Get();
    Interface->Navigate(TEXT("Home"));RiverCheck(TEXT("noRiverVoiceInHome"),TEXT("homePlayingVoices"));
    auto* Match=World->GetSubsystem<URiftMatchSubsystem>();
    Check(TEXT("battleLifecycleSubsystemAvailable"),Match!=nullptr);
    if(!Match)return Result();
    Match->StartMatch(true);Match->SetSpeed(0.f);Interface->Navigate(TEXT("Battle"));
    Check(TEXT("actualBattleActiveForWaterCheck"),Match->IsActive());
    RiverCheck(TEXT("noRiverVoiceInBattle"),TEXT("battlePlayingVoices"));
    SetBattleWorld(World);SetBattleWorld(World);
    RiverCheck(TEXT("noRiverVoiceAfterRepeatedWorldBinding"),TEXT("reboundPlayingVoices"));
    Interface->Navigate(TEXT("Home"));RiverCheck(TEXT("noRiverVoiceAfterReturningHome"),TEXT("returnedHomePlayingVoices"));
    Check(TEXT("musicPreservedAcrossPagesAndWorldBinding"),Music.Get()==InitialMusic&&Music&&Music->IsPlaying());
    const auto OriginalSettings=Profile->Settings;
    Interface->Navigate(TEXT("Settings"));TArray<URiftValueSlider*> Sliders;
    RiverCheck(TEXT("noRiverVoiceInSettings"),TEXT("settingsPlayingVoices"));
    Interface->WidgetTree->ForEachWidget([&](UWidget* Widget){if(auto* Slider=Cast<URiftValueSlider>(Widget))Sliders.Add(Slider);});
    Check(TEXT("actualSettingsAudioSliders"),Sliders.Num()>=4);
    if(Sliders.Num()<4){Interface->Navigate(TEXT("Home"));return Result();}
    auto SetSlider=[&](int32 Index,float Value)
    {Sliders[Index]->SetValue(Value);Sliders[Index]->OnValueChanged.Broadcast(Value);RefreshVolumes();};
    const TCHAR* SoundNames[]={TEXT("aether_three"),TEXT("aether_two"),TEXT("arc_cast"),TEXT("arc_impact"),TEXT("arrow_flight"),TEXT("arrow_hit"),TEXT("bat_bite"),TEXT("bow_release"),TEXT("building_hit"),TEXT("bullet_burst"),TEXT("charge"),TEXT("core_awaken"),TEXT("defeat"),TEXT("deploy"),TEXT("dual_attack"),TEXT("frost_attack"),TEXT("frost_slow"),TEXT("heavy_slam"),TEXT("manta_cast"),TEXT("manta_impact"),TEXT("meteor_impact"),TEXT("meteor_tick"),TEXT("music_rift"),TEXT("nova_impact"),TEXT("overtime"),TEXT("river_ambience"),TEXT("shield_hit"),TEXT("storm_bolt"),TEXT("storm_pulse"),TEXT("stun"),TEXT("sword_attack"),TEXT("sword_hit"),TEXT("tiebreaker"),TEXT("tower_destroy"),TEXT("tower_hit"),TEXT("ui_click"),TEXT("ui_error"),TEXT("ui_hover"),TEXT("ui_save"),TEXT("victory"),TEXT("wing_air")};
    int32 Loaded=0;for(const auto* Name:SoundNames)if(auto* Asset=Sound(FName(Name)))if(Asset->GetDuration()>0)++Loaded;
    Report->SetNumberField(TEXT("loadedSounds"),Loaded);Check(TEXT("all41ImportedSoundsLoad"),Loaded==41);
    int32 Variations=0;for(const auto* Cue:VariedCues)for(int32 Variant=2;Variant<=3;++Variant)
        if(auto* Asset=Sound(FName(*FString::Printf(TEXT("%s_v%d"),Cue,Variant))))if(Asset->GetDuration()>0)++Variations;
    Report->SetNumberField(TEXT("loadedCombatVariations"),Variations);Check(TEXT("all20CombatVariationsLoad"),Variations==20);
    StopOneShots();MusicDuck=1.f;
    SetSlider(0,.6f);SetSlider(1,.35f);SetSlider(2,.4f);SetSlider(3,.8f);
    FloatCheck(TEXT("masterCallback"),Profile->Settings.MasterVolume,.6f);FloatCheck(TEXT("musicCallback"),Profile->Settings.MusicVolume,.35f);
    FloatCheck(TEXT("sfxCallback"),Profile->Settings.SFXVolume,.4f);FloatCheck(TEXT("uiCallback"),Profile->Settings.UIVolume,.8f);
    if(!Music||!Music->IsPlaying())SetBattleWorld(World);
    Check(TEXT("musicPlaying"),Music&&Music->IsPlaying());
    if(Music){FloatCheck(TEXT("musicMasterProduct"),Music->VolumeMultiplier,.21f);if(auto* Wave=Cast<USoundWave>(Music->Sound)){Check(TEXT("musicLoopConfigured"),Wave->bLooping);Check(TEXT("musicStreamed"),Wave->IsStreaming());}}
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
    FillSmokeBurst();const int32 Maximum=Voices.Num();
    Report->SetNumberField(TEXT("maximumOneShotVoices"),Maximum);Check(TEXT("combinedUIAndSFXVoiceLimit"),Maximum==32&&Voices.Num()<=32);
    int32 ReservedUI=0;for(const auto& Voice:Voices)if(Voice.UI)++ReservedUI;
    Check(TEXT("sixUIVoicesReservedUnderCombatLoad"),ReservedUI==6);
    const int32 BeforeDuplicate=Voices.Num();PlayEffect(TEXT("sword_hit"),FVector::ZeroVector);
    Check(TEXT("sameTickSwarmCueAggregated"),Voices.Num()==BeforeDuplicate);
    PlayEffect(TEXT("tower_destroy"),FVector::ZeroVector);
    bool CriticalAdmitted=false;for(const auto& Voice:Voices)if(Voice.Cue==TEXT("tower_destroy"))CriticalAdmitted=true;
    Check(TEXT("importantEventReplacesLowerPriorityVoice"),CriticalAdmitted&&Voices.Num()==32);
    bool HasHeadroom=true;
    for(const auto& Voice:Voices)if(!Voice.UI&&Voice.Component.IsValid())
        HasHeadroom&=Voice.Component->VolumeMultiplier<Master*SFX*Voice.Gain;
    Check(TEXT("crowdedEffectMixHasHeadroom"),HasHeadroom);
    StopOneShots();MusicDuck=1.f;
    Profile->Settings=OriginalSettings;RefreshVolumes();Interface->Navigate(TEXT("Home"));
    Check(TEXT("settingsRestored"),Profile->Settings.MasterVolume==OriginalSettings.MasterVolume&&Profile->Settings.MusicVolume==OriginalSettings.MusicVolume&&Profile->Settings.SFXVolume==OriginalSettings.SFXVolume&&Profile->Settings.UIVolume==OriginalSettings.UIVolume);
    return Result();
}

void URiftBattleAudioSubsystem::RunAudioSmoke(URiftUIWidget* Interface,TFunction<void(const FString&)> Completed)
{
    // The automated window is intentionally hidden. Bypass only the app-focus
    // mute during capture, preserving the four actual player volume controls.
    auto* AppVolumeOverride=IConsoleManager::Get().FindConsoleVariable(TEXT("au.DisableAppVolume"));
    const int32 OriginalAppVolumeOverride=AppVolumeOverride?AppVolumeOverride->GetInt():0;
    if(AppVolumeOverride)AppVolumeOverride->Set(1,ECVF_SetByCode);
    const FString Initial=RunAudioSmokeJSON(Interface);TSharedPtr<FJsonObject> Report;
    auto Reader=TJsonReaderFactory<>::Create(Initial);
    if(!FJsonSerializer::Deserialize(Reader,Report)||!Report.IsValid()||!Report->GetBoolField(TEXT("passed")))
    {if(AppVolumeOverride)AppVolumeOverride->Set(OriginalAppVolumeOverride,ECVF_SetByCode);Completed(Initial);return;}
    Report->SetBoolField(TEXT("backgroundAppMuteBypassedDuringCapture"),AppVolumeOverride!=nullptr);
    auto* World=BattleWorld.Get();auto* Profile=GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();
    const auto OriginalSettings=Profile->Settings;
    Profile->Settings.MasterVolume=.6f;Profile->Settings.MusicVolume=.35f;Profile->Settings.SFXVolume=.4f;Profile->Settings.UIVolume=.8f;RefreshVolumes();
    // Let the audio mixer consume a full bounded burst across several buffers;
    // immediate component registration alone cannot prove that loops survive it.
    if(auto* Device=FAudioDeviceManager::GetAudioMixerDeviceFromWorldContext(World))Device->StartRecording(MasterMix,2.f);
    FillSmokeBurst();
    const double Started=World->GetTimeSeconds();TWeakObjectPtr<URiftBattleAudioSubsystem> WeakThis(this);TWeakObjectPtr<URiftUIWidget> WeakInterface(Interface);
    FTimerHandle MixedTimer;World->GetTimerManager().SetTimer(MixedTimer,[WeakThis,WeakInterface,Report,OriginalSettings,OriginalAppVolumeOverride,Started,Completed=MoveTemp(Completed)]() mutable
    {
        auto* Audio=WeakThis.Get();if(!Audio)return;
        auto Checks=Report->GetArrayField(TEXT("checks"));auto Errors=Report->GetArrayField(TEXT("errors"));
        auto Check=[&](const TCHAR* Name,bool Passed)
        {auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("name"),Name);Item->SetBoolField(TEXT("passed"),Passed);Checks.Add(MakeShared<FJsonValueObject>(Item));if(!Passed)Errors.Add(MakeShared<FJsonValueString>(Name));};
        const auto* CurrentWorld=Audio->BattleWorld.Get();const double Delay=CurrentWorld?CurrentWorld->GetTimeSeconds()-Started:0;
        Report->SetNumberField(TEXT("postMixGameSeconds"),Delay);Check(TEXT("postMixDeferredAcrossGameTicks"),Delay>=.1);
        Check(TEXT("musicSurvivesBoundedBurst"),Audio->Music&&Audio->Music->IsPlaying());
        Check(TEXT("musicRetainsPhysicalVoice"),Audio->Music&&!Audio->Music->IsVirtualized());
        const int32 BurstRiverVoices=PlayingRiverVoices(Audio->BattleWorld.Get());
        Report->GetObjectField(TEXT("waterAmbienceLifecycle"))->SetNumberField(TEXT("burstPlayingVoices"),BurstRiverVoices);
        Check(TEXT("noRiverVoiceAfterDeferredBurst"),BurstRiverVoices==0);
        Check(TEXT("actualMixedPCMHasHeadroom"),SaveMixedOutput(Audio->BattleWorld.Get(),Audio->MasterMix,Report,TEXT("normalMixedOutput"),TEXT("mixed-output")));
        int32 ActiveOneShots=0;for(auto& Voice:Audio->Voices)if(Voice.Component.IsValid()){if(Voice.Component->IsPlaying())++ActiveOneShots;Voice.Component->Stop();}
        Report->SetNumberField(TEXT("postMixActiveOneShots"),ActiveOneShots);Audio->StopOneShots();Audio->MusicDuck=1.f;
        Report->SetArrayField(TEXT("checks"),Checks);Report->SetArrayField(TEXT("errors"),Errors);
        // Deliberately overload two large cues. Record raw float output so a
        // PCM writer's conversion cannot conceal clipping from this check.
        auto* StressWorld=Audio->BattleWorld.Get();
        if(auto* Device=FAudioDeviceManager::GetAudioMixerDeviceFromWorldContext(StressWorld))Device->StartRecording(Audio->MasterMix,2.f);
        Audio->PlayEffect(TEXT("tower_destroy"),FVector::ZeroVector,12.f);
        Audio->PlayEffect(TEXT("meteor_impact"),FVector::ZeroVector,12.f);
        FTimerHandle LimitTimer;StressWorld->GetTimerManager().SetTimer(LimitTimer,[WeakThis,WeakInterface,Report,OriginalSettings,OriginalAppVolumeOverride,Completed=MoveTemp(Completed)]() mutable
        {
            auto* MixAudio=WeakThis.Get();if(!MixAudio)return;
            auto FinalChecks=Report->GetArrayField(TEXT("checks"));auto FinalErrors=Report->GetArrayField(TEXT("errors"));
            auto FinalCheck=[&](const TCHAR* Name,bool Passed)
            {auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("name"),Name);Item->SetBoolField(TEXT("passed"),Passed);FinalChecks.Add(MakeShared<FJsonValueObject>(Item));if(!Passed)FinalErrors.Add(MakeShared<FJsonValueString>(Name));};
            FinalCheck(TEXT("limiterOverloadPCMDoesNotClip"),SaveMixedOutput(MixAudio->BattleWorld.Get(),MixAudio->MasterMix,Report,TEXT("limiterOverloadOutput"),TEXT("limiter-overload")));
            const int32 OverloadRiverVoices=PlayingRiverVoices(MixAudio->BattleWorld.Get());
            Report->GetObjectField(TEXT("waterAmbienceLifecycle"))->SetNumberField(TEXT("overloadPlayingVoices"),OverloadRiverVoices);
            FinalCheck(TEXT("noRiverVoiceDuringLimiterOverload"),OverloadRiverVoices==0);
            MixAudio->StopOneShots();MixAudio->MusicDuck=1.f;
            // Muting only music through the actual Settings callback leaves
            // SFX/UI enabled. A settled live mix must then contain no constant
            // environmental bed, including one outside our voice bookkeeping.
            auto* QuietInterface=WeakInterface.Get();TArray<URiftValueSlider*> QuietSliders;
            if(QuietInterface&&QuietInterface->WidgetTree)
            {QuietInterface->Navigate(TEXT("Settings"));QuietInterface->WidgetTree->ForEachWidget([&](UWidget* Widget){if(auto* Slider=Cast<URiftValueSlider>(Widget))QuietSliders.Add(Slider);});}
            FinalCheck(TEXT("quietMusicMuteUsesActualSettingsSlider"),QuietSliders.Num()>=4);
            if(QuietSliders.Num()>=4){QuietSliders[1]->SetValue(0.f);QuietSliders[1]->OnValueChanged.Broadcast(0.f);}
            auto* QuietProfile=MixAudio->GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();
            FinalCheck(TEXT("quietMusicSliderMuteApplied"),QuietProfile->Settings.MusicVolume==0.f&&QuietProfile->Settings.MasterVolume>0.f&&QuietProfile->Settings.SFXVolume>0.f&&QuietProfile->Settings.UIVolume>0.f);
            MixAudio->RefreshVolumes();Report->SetArrayField(TEXT("checks"),FinalChecks);Report->SetArrayField(TEXT("errors"),FinalErrors);
            auto* QuietWorld=MixAudio->BattleWorld.Get();const double QuietStarted=QuietWorld->GetTimeSeconds();
            FTimerHandle QuietSettleTimer;QuietWorld->GetTimerManager().SetTimer(QuietSettleTimer,[WeakThis,WeakInterface,Report,OriginalSettings,OriginalAppVolumeOverride,QuietStarted,Completed=MoveTemp(Completed)]() mutable
            {
                auto* QuietAudio=WeakThis.Get();if(!QuietAudio)return;auto* CaptureWorld=QuietAudio->BattleWorld.Get();
                const double Settled=CaptureWorld->GetTimeSeconds()-QuietStarted;Report->SetNumberField(TEXT("quietTailSettleGameSeconds"),Settled);
                if(auto* Device=FAudioDeviceManager::GetAudioMixerDeviceFromWorldContext(CaptureWorld))Device->StartRecording(QuietAudio->MasterMix,1.f);
                FTimerHandle QuietCaptureTimer;CaptureWorld->GetTimerManager().SetTimer(QuietCaptureTimer,[WeakThis,WeakInterface,Report,OriginalSettings,OriginalAppVolumeOverride,Completed=MoveTemp(Completed)]()
                {
                    auto* RestoredAudio=WeakThis.Get();if(!RestoredAudio)return;
                    auto Checks=Report->GetArrayField(TEXT("checks"));auto Errors=Report->GetArrayField(TEXT("errors"));
                    auto Check=[&](const TCHAR* Name,bool Passed)
                    {auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("name"),Name);Item->SetBoolField(TEXT("passed"),Passed);Checks.Add(MakeShared<FJsonValueObject>(Item));if(!Passed)Errors.Add(MakeShared<FJsonValueString>(Name));};
                    Check(TEXT("quietSceneSettledAcrossGameTicks"),Report->GetNumberField(TEXT("quietTailSettleGameSeconds"))>=.75);
                    const int32 QuietRiverVoices=PlayingRiverVoices(RestoredAudio->BattleWorld.Get());
                    Report->GetObjectField(TEXT("waterAmbienceLifecycle"))->SetNumberField(TEXT("quietPlayingVoices"),QuietRiverVoices);
                    Check(TEXT("noRiverVoiceInQuietScene"),QuietRiverVoices==0);
                    Check(TEXT("quietLiveMixerHasNoConstantSound"),SaveMixedOutput(RestoredAudio->BattleWorld.Get(),RestoredAudio->MasterMix,Report,TEXT("quietMixedOutput"),TEXT("quiet-output"),true));
                    auto* RestoredProfile=RestoredAudio->GetGameInstance()->GetSubsystem<URiftProfileSubsystem>();RestoredProfile->Settings=OriginalSettings;RestoredAudio->RefreshVolumes();
                    Check(TEXT("postMixSettingsRestored"),RestoredProfile->Settings.MasterVolume==OriginalSettings.MasterVolume&&RestoredProfile->Settings.MusicVolume==OriginalSettings.MusicVolume&&RestoredProfile->Settings.SFXVolume==OriginalSettings.SFXVolume&&RestoredProfile->Settings.UIVolume==OriginalSettings.UIVolume);
                    if(auto* RestoredInterface=WeakInterface.Get())RestoredInterface->Navigate(TEXT("Home"));
                    if(auto* AppOverride=IConsoleManager::Get().FindConsoleVariable(TEXT("au.DisableAppVolume")))AppOverride->Set(OriginalAppVolumeOverride,ECVF_SetByCode);
                    Report->SetArrayField(TEXT("checks"),Checks);Report->SetArrayField(TEXT("errors"),Errors);Report->SetBoolField(TEXT("passed"),Errors.IsEmpty());
                    FString Result;auto Writer=TJsonWriterFactory<>::Create(&Result);FJsonSerializer::Serialize(Report.ToSharedRef(),Writer);Completed(Result);
                },.65f,false);
            },.8f,false);
        },1.2f,false);
    },.65f,false);
}
