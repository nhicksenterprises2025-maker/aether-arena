#include "RiftDiagnostics.h"
#include "RiftProfileSubsystem.h"
#include "RiftMetaSimulationSubsystem.h"
#include "Dom/JsonObject.h"
#include "DynamicRHI.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    FCriticalSection LogMutex;
    FString ActiveRoot,ActiveDay,ActiveFilename;
    int32 Part=0;
    constexpr int64 MaximumFileBytes=4*1024*1024;
    bool AppendRecord(const FString& Filename,const TSharedPtr<FJsonObject>& Record)
    {
        FString Line;
        FJsonSerializer::Serialize(Record,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line));
        Line+=TEXT("\r\n");FTCHARToUTF8 Bytes(*Line);
        auto& Files=FPlatformFileManager::Get().GetPlatformFile();
        TUniquePtr<IFileHandle> Handle(Files.OpenWrite(*Filename,true,true));
        if(!Handle)return false;
        // One complete JSON record is written and flushed while the game-process mutex is held.
        return Handle->Write(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length())&&Handle->Flush(true);
    }
    void PruneGameLogs(const FString& Directory)
    {
        TArray<FString> Files;IFileManager::Get().FindFiles(Files,*FPaths::Combine(Directory,TEXT("RiftGame-*.log")),true,false);
        Files.Sort([](const FString& A,const FString& B){return A>B;});
        const FDateTime Oldest=FDateTime::UtcNow()-FTimespan::FromDays(14);
        for(int32 I=0;I<Files.Num();++I)
        {
            const FString Filename=FPaths::Combine(Directory,FPaths::GetCleanFilename(Files[I]));
            if(Filename==ActiveFilename)continue;
            if(I>=50||IFileManager::Get().GetTimeStamp(*Filename)<Oldest)IFileManager::Get().Delete(*Filename,false,true,true);
        }
    }
    void SystemContext(const FString& Directory)
    {
        auto Record=MakeShared<FJsonObject>();Record->SetNumberField(TEXT("schemaVersion"),1);Record->SetStringField(TEXT("event"),TEXT("system_context"));Record->SetStringField(TEXT("timestamp"),FDateTime::UtcNow().ToIso8601());
        FString GameVersion=TEXT("unknown");if(GConfig)GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"),TEXT("ProjectVersion"),GameVersion,GGameIni);
        Record->SetStringField(TEXT("project"),FApp::GetProjectName());Record->SetStringField(TEXT("gameVersion"),GameVersion);Record->SetStringField(TEXT("engineVersion"),FEngineVersion::Current().ToString());Record->SetStringField(TEXT("platform"),TEXT("Windows x64"));
#if UE_BUILD_SHIPPING
        Record->SetStringField(TEXT("build"),TEXT("Shipping"));
#elif UE_BUILD_DEVELOPMENT
        Record->SetStringField(TEXT("build"),TEXT("Development"));
#else
        Record->SetStringField(TEXT("build"),TEXT("Debug/Test"));
#endif
        Record->SetStringField(TEXT("renderer"),GDynamicRHI?GDynamicRHI->GetName():TEXT("not initialized"));Record->SetStringField(TEXT("configRoot"),FPaths::GeneratedConfigDir());Record->SetStringField(TEXT("saveRoot"),ActiveRoot);Record->SetStringField(TEXT("projectRoot"),FPaths::ProjectDir());Record->SetStringField(TEXT("cardRulesFingerprint"),URiftMetaSimulationSubsystem::Fingerprint());Record->SetNumberField(TEXT("processId"),FPlatformProcess::GetCurrentProcessId());
        AppendRecord(ActiveFilename,Record);PruneGameLogs(Directory);
    }
}

void FRiftDiagnostics::Write(const TCHAR* Level,const FString& Message)
{
    FScopeLock Lock(&LogMutex);
    const FString Root=FPaths::ConvertRelativePathToFull(URiftProfileSubsystem::SaveRoot());const FString Day=FDateTime::UtcNow().ToString(TEXT("%Y-%m-%d"));const FString Directory=FPaths::Combine(Root,TEXT("Logs"));
    auto& Files=FPlatformFileManager::Get().GetPlatformFile();if(!Files.CreateDirectoryTree(*Directory))return;
    const bool NewContext=ActiveRoot!=Root||ActiveDay!=Day||ActiveFilename.IsEmpty()||Files.FileSize(*ActiveFilename)>=MaximumFileBytes;
    if(NewContext)
    {
        if(ActiveRoot!=Root||ActiveDay!=Day)Part=0;else ++Part;ActiveRoot=Root;ActiveDay=Day;
        ActiveFilename=FPaths::Combine(Directory,FString::Printf(TEXT("RiftGame-%s-%u-%s-%d.log"),*Day,FPlatformProcess::GetCurrentProcessId(),*FDateTime::UtcNow().ToString(TEXT("%H-%M-%S")),Part));
        SystemContext(Directory);
    }
    auto Record=MakeShared<FJsonObject>();Record->SetNumberField(TEXT("schemaVersion"),1);Record->SetStringField(TEXT("timestamp"),FDateTime::UtcNow().ToIso8601());Record->SetStringField(TEXT("level"),Level);Record->SetStringField(TEXT("message"),Message.Left(8192));AppendRecord(ActiveFilename,Record);
}
