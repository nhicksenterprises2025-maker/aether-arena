#include "RiftUIWidget.h"
#include "RiftUIPrimitives.h"
#include "Simulation/RiftSimulation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Serialization/JsonSerializer.h"

namespace {
#if __has_include("RiftPatchNotes.generated.inc")
#include "RiftPatchNotes.generated.inc"
#else
    // Existing released history remains available until approved notes exist.
    static const TCHAR* RiftPatchNotesJSON=TEXT("{}");
#endif
    FString Str(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key)
    {FString Value;if(Object)Object->TryGetStringField(Key,Value);return Value;}
    double Num(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key)
    {double Value=0;if(Object)Object->TryGetNumberField(Key,Value);return Value;}
    FString Display(double Value)
    {FNumberFormattingOptions Format;Format.MaximumFractionalDigits=2;return FText::AsNumber(Value,&Format).ToString();}
    FString Targets(int32 Mask)
    {
        TArray<FString> Parts;if(Mask&1)Parts.Add(TEXT("ground troops"));if(Mask&2)Parts.Add(TEXT("air troops"));if(Mask&4)Parts.Add(TEXT("structures"));
        return FString::Join(Parts,TEXT(", "));
    }
    const FLinearColor Paper(.95f,.94f,.89f,1),Muted(.49f,.61f,.75f,1),Gold(.95f,.64f,.19f,1);
}

bool URiftUIWidget::ApprovedPatchNotes()
{
    TSharedPtr<FJsonObject> Notes;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(RiftPatchNotesJSON)),Notes)
       || !Notes || Str(Notes,TEXT("status"))!=TEXT("approved")
       || Str(Notes,TEXT("balanceHash"))!=UTF8_TO_TCHAR(rift::CardManifestHash().c_str())
       || Str(Notes,TEXT("version"))!=UTF8_TO_TCHAR(rift::CardManifestVersion().c_str()))return false;
    Add(Body,Text(TEXT("BALANCE EXPANSION · ")+Str(Notes,TEXT("version")),25,Gold));
    Add(Body,Text(TEXT("Changes from ")+Str(Notes,TEXT("baselineVersion"))+TEXT(". Values are per unit unless a group total is shown."),15,Muted));
    const TArray<TSharedPtr<FJsonValue>>* Panels=nullptr;
    if(Notes->TryGetArrayField(TEXT("panels"),Panels))for(const auto& Value:*Panels)
    {
        auto Entry=Value->AsObject();if(!Entry)continue;
        auto* Card=WidgetTree->ConstructWidget<UVerticalBox>();auto* Heading=Row(Card);
        Add(Heading,Illustration(Str(Entry,TEXT("id")),60),false,8);
        auto* Title=WidgetTree->ConstructWidget<UVerticalBox>();
        Add(Title,Text(Str(Entry,TEXT("name")),22,Paper),2);
        Add(Title,Text(Display(Num(Entry,TEXT("cost")))+TEXT(" AETHER"),14,Muted),2);Add(Heading,Title,true,8);
        const FString Label=Str(Entry,TEXT("label"));
        const FLinearColor Color=Label==TEXT("BUFF")?FLinearColor(.30f,.78f,.55f,1):Label==TEXT("NERF")?FLinearColor(.94f,.38f,.30f,1):Gold;
        Add(Heading,Badge(Label,Color,14),false,8);
        const TArray<TSharedPtr<FJsonValue>>* Changes=nullptr;
        if(Entry->TryGetArrayField(TEXT("changes"),Changes))for(const auto& ChangeValue:*Changes)
        {
            auto Change=ChangeValue->AsObject();if(!Change)continue;
            const bool Slow=Str(Change,TEXT("key"))==TEXT("slowPct");const double Factor=Slow?100.:1.;
            auto* Line=Row(Card);Add(Line,Text(Str(Change,TEXT("label")),16,Paper),true);
            const FString Unit=Str(Change,TEXT("unit"));
            Add(Line,Text(Display(Num(Change,TEXT("old"))*Factor)+TEXT(" to ")+Display(Num(Change,TEXT("new"))*Factor)+TEXT(" ")+Unit,16,Paper),false,10);
            double Percent=0;const bool HasPercent=Change->TryGetNumberField(TEXT("percent"),Percent);
            FString Delta=Slow?FString::Printf(TEXT("%+.2f pp"),Num(Change,TEXT("percentagePoints"))):
                FString::Printf(TEXT("%+.2f"),Num(Change,TEXT("delta")));
            Delta+=HasPercent?FString::Printf(TEXT(" · %+.2f%%"),Percent):TEXT(" · ")+Str(Change,TEXT("zeroTransition"));
            Add(Line,Text(Delta,14,Muted),false,8);
        }
        auto Describe=[&](const TSharedPtr<FJsonObject>& Definition,int32 Count,UVerticalBox* Target)
        {
            bool Spell=false;Definition->TryGetBoolField(TEXT("spell"),Spell);
            FString Copy=TEXT("Targets ")+Targets(int32(Num(Definition,TEXT("targetMask"))));
            if(Spell)Copy+=TEXT(". Troops ")+Display(Num(Definition,TEXT("damage")))+TEXT(" · structures ")+Display(Num(Definition,TEXT("towerDamage")))+
                TEXT(" · radius ")+Display(Num(Definition,TEXT("spellRadius")))+TEXT(" tiles · impact delay ")+Display(Num(Definition,TEXT("castDelay")))+TEXT("s.");
            else
            {
                Copy=Display(Count)+TEXT(" unit(s) · ")+Display(Num(Definition,TEXT("hp")))+TEXT(" body HP each · ")+
                    Display(Num(Definition,TEXT("damage")))+TEXT(" damage / ")+Display(Num(Definition,TEXT("attackInterval")))+TEXT("s · ")+
                    Display(Num(Definition,TEXT("basicDPS")))+TEXT(" basic DPS each. ")+Copy+TEXT(".");
                if(Num(Definition,TEXT("shieldHP"))>0)Copy+=TEXT(" Shield: ")+Display(Num(Definition,TEXT("shieldHP")))+TEXT(" each; separate from body HP.");
                Copy+=TEXT(" Movement ")+Display(Num(Definition,TEXT("moveSpeed")))+TEXT(" tiles/s · range ")+Display(Num(Definition,TEXT("range")))+TEXT(" tiles.");
                if(Num(Definition,TEXT("splash"))>0)Copy+=TEXT(" Splash radius ")+Display(Num(Definition,TEXT("splash")))+TEXT(" tiles.");
            }
            Add(Target,Text(Copy,16,Paper),7);
        };
        const TSharedPtr<FJsonObject>* Definition=nullptr;
        if(Entry->TryGetObjectField(TEXT("definition"),Definition))Describe(*Definition,int32(Num(*Definition,TEXT("count"))),Card);
        const TArray<TSharedPtr<FJsonValue>>* Children=nullptr;
        if(Entry->TryGetArrayField(TEXT("children"),Children))for(const auto& ChildValue:*Children)
        {
            auto Child=ChildValue->AsObject();const TSharedPtr<FJsonObject>* ChildDefinition=nullptr;
            if(Child&&Child->TryGetObjectField(TEXT("definition"),ChildDefinition))
            {Add(Card,Text(TEXT("ON DEATH — ")+Str(*ChildDefinition,TEXT("name")),15,Gold),8);Describe(*ChildDefinition,int32(Num(Child,TEXT("count"))),Card);}
        }
        const TArray<TSharedPtr<FJsonValue>>* Mechanics=nullptr;
        if(Entry->TryGetArrayField(TEXT("mechanics"),Mechanics))for(const auto& Mechanic:*Mechanics)Add(Card,Text(Mechanic->AsString(),16,Paper));
        Add(Body,Panel(Card,FMargin(14)),10);
    }
    const TArray<TSharedPtr<FJsonValue>>* Mechanics=nullptr;
    if(Notes->TryGetArrayField(TEXT("mechanics"),Mechanics))for(const auto& Mechanic:*Mechanics)Add(Body,Text(Mechanic->AsString(),16,Paper));
    Add(Body,Text(Str(Notes,TEXT("derivedMetrics")),14,Muted),12);
    return true;
}
