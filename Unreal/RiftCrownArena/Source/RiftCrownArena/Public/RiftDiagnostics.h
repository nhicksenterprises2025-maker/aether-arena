#pragma once

#include "CoreMinimal.h"

class RIFTCROWNARENA_API FRiftDiagnostics
{
public:
    static void Write(const TCHAR* Level,const FString& Message);
};

// The independent sink remains available in Shipping. Development builds also retain normal Unreal logs.
#define RIFT_LOG(Category,Verbosity,Format,...) \
    do { \
        FRiftDiagnostics::Write(TEXT(#Verbosity),FString::Printf(Format __VA_OPT__(,) __VA_ARGS__)); \
        UE_LOG(Category,Verbosity,Format __VA_OPT__(,) __VA_ARGS__); \
    } while(false)
