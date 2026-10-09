#include "RiftProfileSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/UserInterfaceSettings.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "JsonObjectConverter.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "RiftDiagnostics.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Simulation/RiftSimulation.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

DEFINE_LOG_CATEGORY(LogRift);
namespace {
constexpr int64 ProfileSizeLimit = 1024 * 1024;
int32 Count(const TSharedPtr<FJsonObject> &O, const TCHAR *Key, int32 Fallback = 0) {
    double N = Fallback;
    if (!O || !O->TryGetNumberField(Key, N) || !FMath::IsFinite(N) || N < 0)
        return Fallback;
    return int32(FMath::Min(N, double(MAX_int32)));
}
FString String(const TSharedPtr<FJsonObject> &O, const TCHAR *Key, const FString &Fallback = FString()) {
    FString S;
    return O && O->TryGetStringField(Key, S) ? S : Fallback;
}
TArray<FString> Strings(const TSharedPtr<FJsonObject> &O, const TCHAR *Key) {
    TArray<FString> R;
    const TArray<TSharedPtr<FJsonValue>> *A = nullptr;
    if (O && O->TryGetArrayField(Key, A) && A->Num() <= 2000)
        for (const auto &V : *A) {
            FString S;
            if (V && V->TryGetString(S))
                R.Add(S);
        }
    return R;
}
TArray<TSharedPtr<FJsonValue>> Array(const TArray<FString> &A) {
    TArray<TSharedPtr<FJsonValue>> R;
    for (const auto &S : A)
        R.Add(MakeShared<FJsonValueString>(S));
    return R;
}
TSharedPtr<FJsonObject> Object(const TSharedPtr<FJsonObject> &O, const TCHAR *Key) {
    const TSharedPtr<FJsonObject> *V = nullptr;
    return O && O->TryGetObjectField(Key, V) ? *V : nullptr;
}
TSharedRef<FJsonObject> CopyObject(const TSharedPtr<FJsonObject> &Source) {
    auto R = MakeShared<FJsonObject>();
    if (Source)
        R->Values = Source->Values;
    return R;
}
FString CleanName(const FString &Name, bool Deck, bool ImportedUnicode = false) {
    FString Clean;
    bool Space = false;
    for (TCHAR C : Name) {
        if (FChar::IsWhitespace(C)) {
            if (!Clean.IsEmpty())
                Space = true;
            continue;
        }
        const bool ASCII = (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9');
        const bool Allowed =
            Deck ? (C >= 32 && C != '<' && C != '>' && C != '"' && C != '\'' && C != '&' && C != '`')
                 : (ASCII || (ImportedUnicode && C >= 128) || C == '_' || C == '-');
        if (!Allowed)
            continue;
        if (Space)
            Clean.AppendChar(' ');
        Clean.AppendChar(C);
        Space = false;
    }
    return Clean.Left(Deck ? 24 : 18).TrimStartAndEnd();
}
float SafeFloat(float N, float Fallback, float Min, float Max) {
    return FMath::IsFinite(N) ? FMath::Clamp(N, Min, Max) : Fallback;
}
void NormalizeSettings(FRiftSettings &S) {
    S.MasterVolume = SafeFloat(S.MasterVolume, .8f, 0, 1);
    S.MusicVolume = SafeFloat(S.MusicVolume, .45f, 0, 1);
    S.SFXVolume = SafeFloat(S.SFXVolume, .8f, 0, 1);
    S.UIVolume = SafeFloat(S.UIVolume, .7f, 0, 1);
    S.UIScale = SafeFloat(S.UIScale, 1, .65f, 1.6f);
    S.CameraSpeed = SafeFloat(S.CameraSpeed, 1, .3f, 3);
    S.Width = FMath::Clamp(S.Width, 800, 7680);
    S.Height = FMath::Clamp(S.Height, 600, 4320);
    S.WindowMode = FMath::Clamp(S.WindowMode, 0, 2);
    S.FrameCap = FMath::Clamp(S.FrameCap, 0, 360);
    S.Quality = FMath::Clamp(S.Quality, 0, 3);
    S.AA = FMath::Clamp(S.AA, 0, 3);
    S.Shadows = FMath::Clamp(S.Shadows, 0, 3);
    S.Effects = FMath::Clamp(S.Effects, 0, 3);
    S.Textures = FMath::Clamp(S.Textures, 0, 3);
    S.Post = FMath::Clamp(S.Post, 0, 3);
    S.ViewDistance = FMath::Clamp(S.ViewDistance, 0, 3);
}
void ReadSettings(const TSharedPtr<FJsonObject> &O, FRiftSettings &S) {
    if (!O)
        return;
    // Reflected converter spellings are read without converting malformed JSON types into UStruct fields.
#define FLOAT_SETTING(Field, Key)                                                                            \
    {                                                                                                        \
        double N = S.Field;                                                                                  \
        if (O->TryGetNumberField(TEXT(Key), N) && FMath::IsFinite(N))                                        \
            S.Field = float(FMath::Clamp(N, -1e6, 1e6));                                                     \
    }
    FLOAT_SETTING(MasterVolume, "masterVolume");
    FLOAT_SETTING(MusicVolume, "musicVolume");
    FLOAT_SETTING(SFXVolume, "sFXVolume");
    FLOAT_SETTING(UIVolume, "uIVolume");
    FLOAT_SETTING(UIScale, "uIScale");
    FLOAT_SETTING(CameraSpeed, "cameraSpeed");
#undef FLOAT_SETTING
#define INT_SETTING(Field, Key)                                                                              \
    {                                                                                                        \
        double N = S.Field;                                                                                  \
        if (O->TryGetNumberField(TEXT(Key), N) && FMath::IsFinite(N))                                        \
            S.Field = int32(FMath::Clamp(N, -1e6, 1e6));                                                     \
    }
    INT_SETTING(Width, "width");
    INT_SETTING(Height, "height");
    INT_SETTING(WindowMode, "windowMode");
    INT_SETTING(FrameCap, "frameCap");
    INT_SETTING(Quality, "quality");
    INT_SETTING(AA, "aA");
    INT_SETTING(Shadows, "shadows");
    INT_SETTING(Effects, "effects");
    INT_SETTING(Textures, "textures");
    INT_SETTING(Post, "post");
    INT_SETTING(ViewDistance, "viewDistance");
#undef INT_SETTING
    O->TryGetBoolField(TEXT("dragDeploy"), S.DragDeploy);
    O->TryGetBoolField(TEXT("confirmDeploy"), S.ConfirmDeploy);
    O->TryGetBoolField(TEXT("vSync"), S.VSync);
    NormalizeSettings(S);
}
bool ReplaceAtomically(const FString &Destination, const FString &Source) {
#if PLATFORM_WINDOWS
    return ::MoveFileExW(*Source, *Destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return IFileManager::Get().Move(*Destination, *Source, true, true, false, true);
#endif
}
int32 AddCount(int32 N, int32 Delta = 1) {
    return int32(FMath::Min<int64>(MAX_int32, int64(FMath::Max(0, N)) + FMath::Max(0, Delta)));
}
} // namespace
FString URiftProfileSubsystem::SaveRoot() {
    FString Root;
    if (!FParse::Value(FCommandLine::Get(), TEXT("RiftSaveRoot="), Root))
        Root = FPlatformMisc::GetEnvironmentVariable(TEXT("RIFT_SAVE_ROOT"));
    if (Root.IsEmpty())
        Root = FPlatformMisc::GetEnvironmentVariable(TEXT("RIFT_CROWN_SAVE_ROOT"));
    if (Root.IsEmpty())
        Root = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA")),
                               TEXT("RiftCrownArena"));
    return FPaths::ConvertRelativePathToFull(Root);
}
TArray<FString> URiftProfileSubsystem::DefaultDeck() {
    TArray<FString> R;
    for (const auto &Id : rift::DefaultDeck())
        R.Add(UTF8_TO_TCHAR(Id.c_str()));
    return R;
}
bool URiftProfileSubsystem::ValidDeck(const TArray<FString> &Cards) {
    std::vector<std::string> D;
    for (const auto &Id : Cards)
        D.emplace_back(TCHAR_TO_UTF8(*Id));
    return rift::ValidateDeck(D);
}
void URiftProfileSubsystem::Defaults() {
    Username = TEXT("RIFTBOUND");
    PlayerId = TEXT("RC-") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12).ToUpper();
    Wins = Losses = Draws = Matches = Crowns = 0;
    Gems = 1250;
    Gold = 8420;
    ActivePreset = TEXT("deck-1");
    Settings = FRiftSettings{};
    ReplayFiles.Reset();
    MetaFiles.Reset();
    PreservedRoot.Reset();
    ImportedChecksum.Empty();
    bReadOnlyFutureSchema = false;
    Presets.Reset();
    for (int32 I = 0; I < 5; ++I) {
        FRiftDeckPreset P;
        P.Id = FString::Printf(TEXT("deck-%d"), I + 1);
        P.Name = FString::Printf(TEXT("Deck %d"), I + 1);
        P.Cards = DefaultDeck();
        Presets.Add(P);
    }
}
void URiftProfileSubsystem::Initialize(FSubsystemCollectionBase &Collection) {
    Super::Initialize(Collection);
    Defaults();
    LoadStoredProfile();
    ApplySettings();
}
void URiftProfileSubsystem::LoadStoredProfile() {
    auto &Files = FPlatformFileManager::Get().GetPlatformFile();
    const FString Root = SaveRoot();
    for (const TCHAR *Dir : {TEXT("Logs/Game"), TEXT("Meta"), TEXT("Replays"), TEXT("Backups")})
        Files.CreateDirectoryTree(*FPaths::Combine(Root, Dir));
    const FString Native = FPaths::Combine(Root, TEXT("ue_save.json"));
    if (!LoadFile(Native, false) && !bReadOnlyFutureSchema) {
        const bool Recovered = LoadFile(Native + TEXT(".bak"), false);
        if (!bReadOnlyFutureSchema) {
            bool Imported = false;
            if (!Recovered)
                Imported = LoadFile(FPaths::Combine(Root, TEXT("player_save.json")), true);
            // Quarantine the corrupt primary first, so saving a recovered profile cannot replace its good
            // backup.
            if (Files.FileExists(*Native)) {
                const FString Q =
                    FPaths::Combine(Root, TEXT("Backups"),
                                    TEXT("ue_save.corrupt-") +
                                        FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));
                if (!ReplaceAtomically(Q, Native)) {
                    LastError = TEXT("Unable to preserve the unreadable save. Profile changes are disabled.");
                    bReadOnlyFutureSchema = true;
                }
            }
            if (!bReadOnlyFutureSchema) {
                if (Recovered)
                    RIFT_LOG(LogRift, Warning, TEXT("Recovered native save backup"));
                if (Imported)
                    RIFT_LOG(LogRift, Log, TEXT("Imported browser profile; original preserved"));
                Save();
            }
        }
    }
}
void URiftProfileSubsystem::Deinitialize() {
    if (!bReadOnlyFutureSchema)
        Save();
    Super::Deinitialize();
}
bool URiftProfileSubsystem::LoadFile(const FString &Filename, bool Browser) {
    const int64 Size = IFileManager::Get().FileSize(*Filename);
    if (Size < 0)
        return false;
    if (Size > ProfileSizeLimit) {
        LastError = TEXT("Profile exceeds the 1 MB safety limit; original preserved.");
        return false;
    }
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text, *Filename) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root) {
        LastError = TEXT("Unreadable profile; original preserved.");
        return false;
    }
    if (!Browser) {
        double Schema = 0;
        if (!Root->TryGetNumberField(TEXT("schemaVersion"), Schema) || !FMath::IsFinite(Schema) ||
            Schema < 1 || Schema != FMath::FloorToDouble(Schema))
            return false;
        if (Schema > 1) {
            bReadOnlyFutureSchema = true;
            LastError = TEXT("This profile was saved by a newer game version. Update the game to edit it.");
            return false;
        }
    }
    auto Profile = Object(Root, TEXT("profile"));
    auto Decks = Object(Root, TEXT("deckPresets"));
    auto Legacy = Strings(Root, TEXT("deck"));
    if (!Profile && !Decks && !ValidDeck(Legacy))
        return false;
    if (Profile) {
        const FString Name = CleanName(String(Profile, TEXT("username"), Username), false, true);
        if (!Name.IsEmpty())
            Username = Name;
        PlayerId = String(Profile, TEXT("playerId"), PlayerId).Left(128);
        Wins = Count(Profile, TEXT("wins"));
        Losses = Count(Profile, TEXT("losses"));
        Draws = Count(Profile, TEXT("draws"));
        Matches = Count(Profile, TEXT("matches"), AddCount(AddCount(Wins, Losses), Draws));
        Crowns = Count(Profile, TEXT("crowns"));
        Gems = Count(Profile, TEXT("gems"), 1250);
        Gold = Count(Profile, TEXT("gold"), 8420);
    }
    if (ValidDeck(Legacy))
        Presets[0].Cards = Legacy;
    if (Decks) {
        const TArray<TSharedPtr<FJsonValue>> *Records = nullptr;
        TSet<FString> Seen;
        if (Decks->TryGetArrayField(TEXT("presets"), Records) && Records->Num() <= 1000)
            for (const auto &Record : *Records) {
                const TSharedPtr<FJsonObject> *P = nullptr;
                if (!Record || !Record->TryGetObject(P) || !P->IsValid())
                    continue;
                FString Id = String(*P, TEXT("id"));
                const int32 Index = Presets.IndexOfByPredicate([&](const auto &D) { return D.Id == Id; });
                auto Cards = Strings(*P, TEXT("cards"));
                if (Index != INDEX_NONE && !Seen.Contains(Id) && ValidDeck(Cards)) {
                    Seen.Add(Id);
                    Presets[Index].Cards = Cards;
                    const FString Name = CleanName(String(*P, TEXT("name")), true);
                    if (!Name.IsEmpty())
                        Presets[Index].Name = Name;
                }
            }
        FString Active = String(Decks, TEXT("activePresetId"));
        if (Presets.ContainsByPredicate([&](const auto &P) { return P.Id == Active; }))
            ActivePreset = Active;
    }
    if (!Browser) {
        ReadSettings(Object(Root, TEXT("settings")), Settings);
        ReplayFiles = Strings(Root, TEXT("replays"));
        MetaFiles = Strings(Root, TEXT("metaDatasets"));
        ImportedChecksum = String(Root, TEXT("importedBrowserChecksum"));
        for (auto &Name : ReplayFiles)
            Name = FPaths::GetCleanFilename(Name);
        for (auto &Name : MetaFiles)
            Name = FPaths::GetCleanFilename(Name);
    } else
        ImportedChecksum = FMD5::HashAnsiString(*Text);
    PreservedRoot = Root;
    return true;
}
bool URiftProfileSubsystem::AtomicWrite(const FString &Filename, const FString &Contents, FString &Error) {
    FTCHARToUTF8 UTF8(*Contents);
    TArray<uint8> Bytes;
    Bytes.Append(reinterpret_cast<const uint8 *>(UTF8.Get()), UTF8.Length());
    return AtomicWriteBytes(Filename, Bytes, Error);
}
bool URiftProfileSubsystem::AtomicWriteBytes(const FString &Filename, const TArray<uint8> &Contents,
                                             FString &Error) {
    Error.Empty();
    auto &Files = FPlatformFileManager::Get().GetPlatformFile();
    if (!Files.CreateDirectoryTree(*FPaths::GetPath(Filename))) {
        Error = TEXT("Unable to create save folder.");
        return false;
    }
    const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits),
                  Temp = Filename + TEXT(".tmp-") + Suffix;
    if (!FFileHelper::SaveArrayToFile(Contents, *Temp)) {
        Files.DeleteFile(*Temp);
        Error = TEXT("Unable to write save; check free space and permissions.");
        return false;
    }
    if (Files.FileExists(*Filename)) {
        const FString Backup = Filename + TEXT(".bak"), BackupTemp = Backup + TEXT(".tmp-") + Suffix;
        if (!Files.CopyFile(*BackupTemp, *Filename) || !ReplaceAtomically(Backup, BackupTemp)) {
            Files.DeleteFile(*Temp);
            Files.DeleteFile(*BackupTemp);
            Error = TEXT("Unable to preserve the previous save.");
            return false;
        }
    }
    if (!ReplaceAtomically(Filename, Temp)) {
        Files.DeleteFile(*Temp);
        Error = TEXT("Unable to commit save; previous file remains available.");
        return false;
    }
    return true;
}
bool URiftProfileSubsystem::Save() {
    if (bReadOnlyFutureSchema)
        return false;
    NormalizeSettings(Settings);
    auto Root = CopyObject(PreservedRoot);
    Root->SetNumberField(TEXT("schemaVersion"), 1);
    Root->SetStringField(TEXT("savedAt"), FDateTime::UtcNow().ToIso8601());
    Root->SetStringField(TEXT("importedBrowserChecksum"), ImportedChecksum);
    auto P = CopyObject(Object(PreservedRoot, TEXT("profile")));
    P->SetStringField(TEXT("username"), Username);
    P->SetStringField(TEXT("playerId"), PlayerId);
    P->SetNumberField(TEXT("wins"), Wins);
    P->SetNumberField(TEXT("losses"), Losses);
    P->SetNumberField(TEXT("draws"), Draws);
    P->SetNumberField(TEXT("matches"), Matches);
    P->SetNumberField(TEXT("crowns"), Crowns);
    P->SetNumberField(TEXT("gems"), Gems);
    P->SetNumberField(TEXT("gold"), Gold);
    Root->SetObjectField(TEXT("profile"), P);
    auto Decks = CopyObject(Object(PreservedRoot, TEXT("deckPresets")));
    Decks->SetStringField(TEXT("activePresetId"), ActivePreset);
    TArray<TSharedPtr<FJsonValue>> Records;
    const TArray<TSharedPtr<FJsonValue>> *OldRecords = nullptr;
    Decks->TryGetArrayField(TEXT("presets"), OldRecords);
    for (const auto &Preset : Presets) {
        TSharedPtr<FJsonObject> Old;
        if (OldRecords)
            for (const auto &V : *OldRecords) {
                const TSharedPtr<FJsonObject> *R = nullptr;
                if (V && V->TryGetObject(R) && String(*R, TEXT("id")) == Preset.Id) {
                    Old = *R;
                    break;
                }
            }
        auto R = CopyObject(Old);
        R->SetStringField(TEXT("id"), Preset.Id);
        R->SetStringField(TEXT("name"), Preset.Name);
        R->SetArrayField(TEXT("cards"), Array(Preset.Cards));
        Records.Add(MakeShared<FJsonValueObject>(R));
    }
    if (OldRecords)
        for (const auto &V : *OldRecords) {
            const TSharedPtr<FJsonObject> *R = nullptr;
            if (V && V->TryGetObject(R) &&
                !Presets.ContainsByPredicate([&](const auto &D) { return D.Id == String(*R, TEXT("id")); }))
                Records.Add(V);
        }
    Decks->SetArrayField(TEXT("presets"), Records);
    Root->SetObjectField(TEXT("deckPresets"), Decks);
    Root->SetArrayField(TEXT("deck"), Array(ActiveDeck()));
    auto S = CopyObject(Object(PreservedRoot, TEXT("settings")));
    auto Known = FJsonObjectConverter::UStructToJsonObject(Settings);
    for (const auto &Pair : Known->Values)
        S->Values.Add(Pair.Key, Pair.Value);
    Root->SetObjectField(TEXT("settings"), S);
    Root->SetArrayField(TEXT("replays"), Array(ReplayFiles));
    Root->SetArrayField(TEXT("metaDatasets"), Array(MetaFiles));
    FString Text;
    if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text)))
        return false;
    if (!AtomicWrite(FPaths::Combine(SaveRoot(), TEXT("ue_save.json")), Text, LastError)) {
        RIFT_LOG(LogRift, Error, TEXT("%s"), *LastError);
        return false;
    }
    PreservedRoot = Root;
    LastError.Empty();
    return true;
}
bool URiftProfileSubsystem::SetName(const FString &NewName) {
    if (bReadOnlyFutureSchema)
        return false;
    FString Clean = CleanName(NewName, false);
    if (Clean.IsEmpty())
        return false;
    Username = Clean;
    return Save();
}
TArray<FString> URiftProfileSubsystem::ActiveDeck() const {
    for (const auto &P : Presets)
        if (P.Id == ActivePreset)
            return P.Cards;
    return DefaultDeck();
}
bool URiftProfileSubsystem::SaveDeck(int32 Index, const FString &Name, const TArray<FString> &Cards) {
    if (bReadOnlyFutureSchema || !Presets.IsValidIndex(Index) || !ValidDeck(Cards))
        return false;
    Presets[Index].Cards = Cards;
    Presets[Index].Name = CleanName(Name, true);
    if (Presets[Index].Name.IsEmpty())
        Presets[Index].Name = FString::Printf(TEXT("Deck %d"), Index + 1);
    ActivePreset = Presets[Index].Id;
    return Save();
}
bool URiftProfileSubsystem::SelectPreset(int32 Index) {
    if (bReadOnlyFutureSchema || !Presets.IsValidIndex(Index))
        return false;
    ActivePreset = Presets[Index].Id;
    return Save();
}
void URiftProfileSubsystem::RecordResult(int32 Winner, int32 PlayerCrowns) {
    if (bReadOnlyFutureSchema)
        return;
    Matches = AddCount(Matches);
    if (Winner < 0)
        Draws = AddCount(Draws);
    else if (Winner == 0)
        Wins = AddCount(Wins);
    else
        Losses = AddCount(Losses);
    Crowns = AddCount(Crowns, FMath::Clamp(PlayerCrowns, 0, 3));
    Save();
}
void URiftProfileSubsystem::ApplySettings() {
    NormalizeSettings(Settings);
    // The game layer multiplies its viewport DPI curve by this value. Global Slate
    // window scaling is cancelled by SGameLayerManager's viewport normalization.
    GetMutableDefault<UUserInterfaceSettings>()->ApplicationScale = Settings.UIScale;
    auto *G = GEngine ? GEngine->GetGameUserSettings() : nullptr;
    if (!G)
        return;
    int32 DisplayWidth = Settings.Width, DisplayHeight = Settings.Height;
    FString QAOutput;
    if (FParse::Value(FCommandLine::Get(), TEXT("RiftCapture="), QAOutput) ||
        FParse::Value(FCommandLine::Get(), TEXT("RiftPerfReport="), QAOutput) ||
        FParse::Value(FCommandLine::Get(), TEXT("RiftAudioSmoke="), QAOutput) ||
        FParse::Value(FCommandLine::Get(), TEXT("RiftDragSmoke="), QAOutput)) {
        FParse::Value(FCommandLine::Get(), TEXT("ResX="), DisplayWidth);
        FParse::Value(FCommandLine::Get(), TEXT("ResY="), DisplayHeight);
        DisplayWidth = FMath::Clamp(DisplayWidth, 800, 7680);
        DisplayHeight = FMath::Clamp(DisplayHeight, 600, 4320);
    }
    // QA resolution changes the active viewport, while saved profile preferences remain intact.
    G->SetScreenResolution(FIntPoint(DisplayWidth, DisplayHeight));
    G->SetFullscreenMode(EWindowMode::Type(Settings.WindowMode));
    G->SetVSyncEnabled(Settings.VSync);
    G->SetFrameRateLimit(Settings.FrameCap);
    G->SetOverallScalabilityLevel(Settings.Quality);
    G->SetAntiAliasingQuality(Settings.AA);
    G->SetShadowQuality(Settings.Shadows);
    G->SetVisualEffectQuality(Settings.Effects);
    G->SetTextureQuality(Settings.Textures);
    G->SetPostProcessingQuality(Settings.Post);
    G->SetViewDistanceQuality(Settings.ViewDistance);
    G->ApplySettings(false);
}
