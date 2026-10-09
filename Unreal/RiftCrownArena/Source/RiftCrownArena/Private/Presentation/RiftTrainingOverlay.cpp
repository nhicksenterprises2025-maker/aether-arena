#include "Presentation/RiftBattleOverlay.h"
#include "Presentation/RiftArenaPresentation.h"
#include "Presentation/RiftUnitVisual.h"
#include "RiftMatchSubsystem.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "RiftTypography.h"

int32 URiftBattleOverlay::PaintTrainingOverlay(const FGeometry& Geometry,FSlateWindowElementList& Elements,int32 Layer)const
{
    LastTrainingLineCount=0;LastTrainingLabelCount=0;
    const auto* Dev=GetWorld()->GetSubsystem<URiftDeveloperSubsystem>();
    const auto* State=Presentation?Presentation->ViewState():nullptr;
    const auto* Player=GetOwningPlayer();
    if(!Dev||!State||!Player||!(Dev->ShowPaths||Dev->ShowSight||Dev->ShowRanges||Dev->ShowTargets||Dev->ShowTiles||Dev->ShowHardLocks))return Layer;
    const float ViewScale=FMath::Max(.1f,UWidgetLayoutLibrary::GetViewportScale(this));
    auto Project=[&](FVector World,FVector2D& Local)
    {FVector2D Screen;if(!Player->ProjectWorldLocationToScreen(World,Screen,true))return false;Local=Screen/ViewScale;return true;};
    auto Line=[&](FVector Start,FVector End,FLinearColor Color,float Width=1.f)
    {
        FVector2D A,B;if(Project(Start,A)&&Project(End,B)){FSlateDrawElement::MakeLines(Elements,Layer+1,Geometry.ToPaintGeometry(),TArray<FVector2D>{A,B},ESlateDrawEffect::None,Color,true,Width);++LastTrainingLineCount;}
    };
    auto Circle=[&](FVector Center,double Radius,FLinearColor Color)
    {
        TArray<FVector2D> Points;
        for(int32 I=0;I<=48;++I)
        {
            const double Angle=I*UE_TWO_PI/48;FVector2D Point;
            if(Project(Center+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0),Point))Points.Add(Point);
        }
        if(Points.Num()>1){FSlateDrawElement::MakeLines(Elements,Layer+1,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,1.f);LastTrainingLineCount+=Points.Num()-1;}
    };
    auto Entity=[&](rift::EntityId Id)->const rift::Entity*
    {for(const auto& Current:State->entities)if(Current.id==Id&&!Current.dead)return &Current;return nullptr;};

    // These are ordinary projected Slate primitives, available in the packaged
    // game. UE DrawDebugHelpers are empty functions in a Shipping build.
    if(Dev->ShowTiles)
    {
        const FLinearColor Grid(.63f,.77f,.88f,.30f);
        for(int32 X=-14;X<=14;++X)Line(FVector(X*100,-2100,12),FVector(X*100,2100,12),Grid,.7f);
        for(int32 Z=-21;Z<=21;++Z)Line(FVector(-1400,Z*100,12),FVector(1400,Z*100,12),Grid,.7f);
        const auto Font=RiftTypography::Font(TEXT("Regular"),9);
        const FSlateBrush* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
        for(double X=-12.5;X<14;X+=4)for(double Z=-18.5;Z<21;Z+=4)
        {
            FVector2D Point;if(!Project(FVector(X*100,Z*100,20),Point))continue;
            Point-=FVector2D(28,7);
            FSlateDrawElement::MakeBox(Elements,Layer+2,Geometry.ToPaintGeometry(FVector2D(58,14),FSlateLayoutTransform(Point)),White,ESlateDrawEffect::None,FLinearColor(.025f,.035f,.055f,.72f));
            FSlateDrawElement::MakeText(Elements,Layer+3,Geometry.ToPaintGeometry(FVector2D(58,14),FSlateLayoutTransform(Point+FVector2D(3,1))),FString::Printf(TEXT("%.1f, %.1f"),X,Z),Font,ESlateDrawEffect::None,FLinearColor(.88f,.94f,1.f,.85f));
            ++LastTrainingLabelCount;
        }
    }
    for(const auto& Current:State->entities)
    {
        if(Current.dead)continue;
        const FVector Origin=URiftMatchSubsystem::WorldPoint(Current.position,25);
        FLinearColor Team=ARiftUnitVisual::TeamColor(Current.team);Team.A=.8f;
        if(Dev->ShowPaths&&Current.kind==rift::EntityKind::Troop&&!Current.flying)
        {
            FVector Previous=Origin;
            for(const auto& Point:Current.path){const FVector Next=URiftMatchSubsystem::WorldPoint(Point,25);Line(Previous,Next,Team,1.6f);Previous=Next;}
        }
        if(Dev->ShowRanges)
        {
            const auto* Card=rift::FindCard(Current.cardId);
            const double Range=Card?Card->range:Current.kind==rift::EntityKind::Core?8.9:10.;
            FLinearColor Color=Team;Color.A=.46f;Circle(Origin,Range*100,Color);
        }
        if(Dev->ShowSight&&Current.kind==rift::EntityKind::Troop)
        {
            const FVector Forward(Current.facing.x,Current.facing.z,0),Side(-Current.facing.z,Current.facing.x,0);
            for(int32 Half=0;Half<2;++Half)
            {
                const double Radius=Half==0?800.:500.;
                const FLinearColor Color=Half==0?FLinearColor(.40f,.92f,.64f,.65f):FLinearColor(1.f,.77f,.35f,.65f);
                const double Start=Half==0?-UE_PI/2:UE_PI/2;
                FVector Previous=Origin+(Forward*FMath::Cos(Start)+Side*FMath::Sin(Start))*Radius;
                Line(Origin,Previous,Color);
                for(int32 I=1;I<=24;++I)
                {
                    const double Angle=Start+I*UE_PI/24;const FVector Next=Origin+(Forward*FMath::Cos(Angle)+Side*FMath::Sin(Angle))*Radius;
                    Line(Previous,Next,Color);Previous=Next;
                }
                Line(Previous,Origin,Color);
            }
        }
        if(Dev->ShowTargets&&Current.target)
            if(const auto* Target=Entity(Current.target))
            {
                const FVector End=URiftMatchSubsystem::WorldPoint(Target->position,25);Line(Origin,End,Team,1.3f);
                const FVector Direction=(End-Origin).GetSafeNormal();const FVector Side(-Direction.Y,Direction.X,0);
                Line(End,End-Direction*45+Side*25,Team,1.3f);Line(End,End-Direction*45-Side*25,Team,1.3f);
            }
        if(Dev->ShowHardLocks&&Current.hardLock)
            if(const auto* Target=Entity(Current.hardLock))Line(Origin+FVector(0,0,30),URiftMatchSubsystem::WorldPoint(Target->position,55),FLinearColor(1.f,.79f,.24f,.9f),2.f);
    }
    return Layer+3;
}
