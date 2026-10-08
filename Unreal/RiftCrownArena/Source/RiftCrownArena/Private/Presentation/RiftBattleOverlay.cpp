#include "Presentation/RiftBattleOverlay.h"
#include "Presentation/RiftArenaPresentation.h"
#include "Presentation/RiftUnitVisual.h"
#include "RiftGameMode.h"
#include "RiftUIWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

void URiftBattleOverlay::SetPresentation(ARiftArenaPresentation* InPresentation)
{Presentation=InPresentation;SetVisibility(ESlateVisibility::HitTestInvisible);}
int32 URiftBattleOverlay::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled)const
{
    int32 Top=Super::NativePaint(Args,Geometry,CullingRect,Elements,Layer,Style,ParentEnabled);
    APlayerController* Player=GetOwningPlayer();if (!Presentation || !Player) return Top;
    const auto* RiftPlayer=Cast<ARiftPlayerController>(Player);
    if (!RiftPlayer || !RiftPlayer->Interface || !RiftPlayer->Interface->IsBattleView()) return Top;
    const auto* State=Presentation->ViewState();if (!State) return Top;
    const float ViewScale=FMath::Max(.1f,UWidgetLayoutLibrary::GetViewportScale(this));
    const FVector2D Extent=Geometry.GetLocalSize();
    const FSlateBrush* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    const FSlateFontInfo Small=FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10);
    const FSlateFontInfo TowerFont=FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12);
    auto Project=[&](FVector World,FVector2D& Local)
    {
        FVector2D Screen;if (!Player->ProjectWorldLocationToScreen(World,Screen,true)) return false;
        Local=Screen/ViewScale;return true;
    };
    auto Box=[&](FVector2D Position,FVector2D Size,FLinearColor Color,int32 Offset=0)
    {FSlateDrawElement::MakeBox(Elements,Top+1+Offset,Geometry.ToPaintGeometry(Size,FSlateLayoutTransform(Position)),White,ESlateDrawEffect::None,Color);};
    auto Text=[&](FVector2D Position,const FString& Value,FLinearColor Color,bool Tower=false)
    {FSlateDrawElement::MakeText(Elements,Top+3,Geometry.ToPaintGeometry(FVector2D(100,20),FSlateLayoutTransform(Position)),Value,Tower?TowerFont:Small,ESlateDrawEffect::None,Color);};
    for (const auto& Hazard:State->hazards)
    {
        TArray<FVector2D> Boundary;
        const FVector Center(Hazard.position.x*100,Hazard.position.z*100,10);
        for (int32 Index=0;Index<=64;++Index)
        {
            const float Angle=Index*UE_TWO_PI/64;FVector2D Point;
            if (Project(Center+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Hazard.radius*100,Point)) Boundary.Add(Point);
        }
        if (Boundary.Num()>1) FSlateDrawElement::MakeLines(Elements,Top+1,Geometry.ToPaintGeometry(),Boundary,
            ESlateDrawEffect::None,FLinearColor(.94f,.42f,.16f,.50f),true,1.f);
        FVector2D Position;if (Project(Center,Position))
            Text(Position+FVector2D(-7,8),FString::Printf(TEXT("%.0fs"),FMath::Max(0.,Hazard.expires-State->elapsed)),FLinearColor(1.f,.68f,.39f,.9f));
    }
    TArray<FSlateRect> Occupied;
    for (int32 LabelPriority=0;LabelPriority<3;++LabelPriority) for (const auto& Entity:State->entities)
    {
        const int32 EntityPriority=Entity.kind==rift::EntityKind::Troop?2:Entity.kind==rift::EntityKind::Building?1:0;
        if (EntityPriority!=LabelPriority) continue;
        if (Entity.dead) continue;const auto* Visual=Presentation->Visual(Entity.id);if (!Visual) continue;
        FVector2D Position;if (!Project(Visual->HealthLocation(),Position)) continue;
        if (Position.X<0 || Position.Y<0 || Position.X>Extent.X || Position.Y>Extent.Y) continue;
        const bool Tower=Entity.kind!=rift::EntityKind::Troop;
        const float Width=Tower?74.f:Entity.memberCount>=5?18.f:Entity.memberCount>1?38.f:48.f;
        const float Height=Tower?6.f:Entity.memberCount>=5?3.f:4.f;
        Position-=FVector2D(Width*.5f,10);
        const FVector2D Anchor=Position;
        const bool Status=Entity.stunUntil>State->elapsed || Entity.slowUntil>State->elapsed || Entity.charged;
        const bool Dormant=Entity.kind==rift::EntityKind::Core && !Entity.active;
        // Reserve the complete label, including text that is wider than a swarm
        // bar. Search a bounded two-dimensional neighbourhood so bridge queues
        // do not become tall columns of health bars above unrelated units.
        const float LabelWidth=FMath::Max(Width,Status?48.f:Dormant?62.f:0.f);
        auto LabelArea=[&](FVector2D Point)
        {return FSlateRect(Point.X-2,Point.Y-(Tower?18.f:2.f),Point.X+LabelWidth+2,
            Point.Y+Height+(Dormant||Status?19.f:5.f));};
        struct FLabelCandidate {FVector2D Offset;float Distance;};
        TArray<FLabelCandidate,TInlineAllocator<81>> Candidates;
        const float StepX=FMath::Clamp(LabelWidth+6.f,24.f,30.f);
        const float StepY=FMath::Clamp(Height+(Status||Dormant?23.f:6.f),10.f,18.f);
        for (int32 Row=-4;Row<=4;++Row) for (int32 Column=-4;Column<=4;++Column)
        {
            const FVector2D Offset(Column*StepX,Row*StepY);
            Candidates.Add({Offset,float(Offset.SizeSquared())});
        }
        Candidates.Sort([](const FLabelCandidate& A,const FLabelCandidate& B)
        {
            if (A.Distance!=B.Distance) return A.Distance<B.Distance;
            if (A.Offset.Y!=B.Offset.Y) return A.Offset.Y<B.Offset.Y;
            return A.Offset.X<B.Offset.X;
        });
        float BestOverlap=TNumericLimits<float>::Max();
        for (const auto& Candidate:Candidates)
        {
            const FVector2D CandidatePosition=Anchor+Candidate.Offset;
            const FSlateRect Area=LabelArea(CandidatePosition);
            if (Area.Left<2 || Area.Top<2 || Area.Right>Extent.X-2 || Area.Bottom>Extent.Y-2) continue;
            float Overlap=0;
            for (const auto& Previous:Occupied)
            {
                const float IntersectionWidth=FMath::Min(Area.Right,Previous.Right)-FMath::Max(Area.Left,Previous.Left);
                const float IntersectionHeight=FMath::Min(Area.Bottom,Previous.Bottom)-FMath::Max(Area.Top,Previous.Top);
                if (IntersectionWidth>0 && IntersectionHeight>0) Overlap+=IntersectionWidth*IntersectionHeight;
            }
            if (Overlap<BestOverlap) {BestOverlap=Overlap;Position=CandidatePosition;}
            if (Overlap==0) break;
        }
        Occupied.Add(LabelArea(Position));
        const FLinearColor Color=ARiftUnitVisual::TeamColor(Entity.team);
        if (FVector2D::Distance(Anchor,Position)>3)
        {
            const FVector2D Target=Anchor+FVector2D(Width*.5f,Height+3);
            const FVector2D Edge(FMath::Clamp(Target.X,Position.X,Position.X+Width),
                FMath::Clamp(Target.Y,Position.Y,Position.Y+Height));
            TArray<FVector2D> Leader{Edge,Target};
            FSlateDrawElement::MakeLines(Elements,Top+1,Geometry.ToPaintGeometry(),Leader,ESlateDrawEffect::None,
                FLinearColor(Color.R,Color.G,Color.B,.35f),true,1.f);
        }
        Box(Position-FVector2D(1,1),FVector2D(Width+2,Height+2),FLinearColor(.025f,.038f,.045f,.95f));
        Box(Position,FVector2D(Width,Height),FLinearColor(.15f,.19f,.21f,.9f));
        const float Fraction=float(FMath::Clamp(Entity.hp/FMath::Max(1.,Entity.maxHp),0.,1.));
        Box(Position,FVector2D(Width*Fraction,Height),Color,1);
        if (Tower)
        {
            const FString Value=FString::Printf(TEXT("%d"),FMath::CeilToInt(Entity.hp));
            Text(Position+FVector2D(Width*.5f-Value.Len()*3.f,-17),Value,FLinearColor(.94f,.96f,.97f),true);
            if (Entity.kind==rift::EntityKind::Core && !Entity.active)
                Text(Position+FVector2D(8,9),TEXT("DORMANT"),FLinearColor(.58f,.65f,.68f));
            if (Entity.kind==rift::EntityKind::Building)
            {
                const auto* Card=rift::FindCard(Entity.cardId);
                if (Card && Card->lifetime>0)
                {
                    const float Life=FMath::Clamp(float(1.-(State->elapsed-Entity.born)/Card->lifetime),0.f,1.f);
                    Box(Position+FVector2D(0,Height+2),FVector2D(Width*Life,1),FLinearColor(.74f,.73f,.55f,.85f));
                }
            }
        }
        if (Entity.stunUntil>State->elapsed)
            Text(Position+FVector2D(0,Height+4),TEXT("STUN"),FLinearColor(.95f,.86f,.42f));
        else if (Entity.slowUntil>State->elapsed && Entity.slowPct>0)
            Text(Position+FVector2D(0,Height+4),FString::Printf(TEXT("-%d%%"),FMath::RoundToInt(Entity.slowPct*100)),FLinearColor(.48f,.86f,1.f));
        else if (Entity.charged) Text(Position+FVector2D(0,Height+4),TEXT("CHARGE"),FLinearColor(.98f,.75f,.36f));
    }
    if (Presentation->HasPlacementPreview())
    {
        const FVector2D Tile=Presentation->PreviewTile();const FVector Center(Tile.X*100,Tile.Y*100,12);
        const FLinearColor Color=Presentation->PreviewIsValid()?FLinearColor(.22f,.8f,.96f,.9f):FLinearColor(.98f,.3f,.2f,.95f);
        TArray<FVector2D> Points;
        if (Presentation->PreviewIsSpell())
        {
            for (int32 Index=0;Index<=64;++Index)
            {
                const float Angle=Index*UE_TWO_PI/64;FVector2D Point;
                if (Project(Center+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Presentation->PreviewRadius()*100,Point)) Points.Add(Point);
            }
        }
        else
        {
            const float Half=FMath::Max(1.f,Presentation->PreviewFootprint())*50;
            for (FVector2D Corner:{FVector2D(-Half,-Half),FVector2D(Half,-Half),FVector2D(Half,Half),FVector2D(-Half,Half),FVector2D(-Half,-Half)})
            {FVector2D Point;if (Project(Center+FVector(Corner.X,Corner.Y,0),Point)) Points.Add(Point);}
        }
        if (Points.Num()>1) FSlateDrawElement::MakeLines(Elements,Top+5,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,1.5f);
        FVector2D PreviewCenter;if (Project(Center,PreviewCenter))
        {
            Box(PreviewCenter-FVector2D(2,2),FVector2D(4,4),Color,4);
            if (!Presentation->PreviewIsValid()) Text(PreviewCenter+FVector2D(10,6),TEXT("UNAVAILABLE"),Color);
        }
    }
    return Top+6;
}
