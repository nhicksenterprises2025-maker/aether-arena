#include "Presentation/RiftBattleOverlay.h"
#include "Presentation/RiftArenaPresentation.h"
#include "Presentation/RiftUnitVisual.h"
#include "RiftGameMode.h"
#include "RiftUIWidget.h"
#include "RiftDiagnostics.h"
#include "RiftProfileSubsystem.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "RiftTypography.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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
    Top=PaintTrainingOverlay(Geometry,Elements,Top);
    const float ViewScale=FMath::Max(.1f,UWidgetLayoutLibrary::GetViewportScale(this));
    const FVector2D Extent=Geometry.GetLocalSize();
    const FBox2D ScreenSafe=RiftPlayer->BattleSafeScreenBounds();
    const FBox2D LabelSafe(ScreenSafe.Min/ViewScale,ScreenSafe.Max/ViewScale);
    const FSlateBrush* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    const float Pixel=1.f/ViewScale;
    // These are world annotations, so zoom/field pitch sets their bar width.
    // Keep text readable in screen space instead of magnifying labels while a
    // larger HUD forces the battlefield itself to occupy fewer pixels.
    const FSlateFontInfo Small=RiftTypography::Font(TEXT("Regular"),FMath::Max(1,FMath::CeilToInt(12.f*Pixel)));
    const FSlateFontInfo TowerFont=RiftTypography::Font(TEXT("Bold"),FMath::Max(1,FMath::CeilToInt(14.f*Pixel)));
    const auto FontMeasure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    auto Project=[&](FVector World,FVector2D& Local)
    {
        FVector2D Screen;if (!Player->ProjectWorldLocationToScreen(World,Screen,true)) return false;
        Local=Screen/ViewScale;return true;
    };
    auto Box=[&](FVector2D Position,FVector2D Size,FLinearColor Color,int32 Offset=0)
    {FSlateDrawElement::MakeBox(Elements,Top+1+Offset,Geometry.ToPaintGeometry(Size,FSlateLayoutTransform(Position)),White,ESlateDrawEffect::None,Color);};
    auto Text=[&](FVector2D Position,const FString& Value,FLinearColor Color,bool Tower=false)
    {FSlateDrawElement::MakeText(Elements,Top+3,Geometry.ToPaintGeometry(FVector2D(100,20),FSlateLayoutTransform(Position)),Value,Tower?TowerFont:Small,ESlateDrawEffect::None,Color);};
    FVector ViewLocation;FRotator ViewRotation;Player->GetPlayerViewPoint(ViewLocation,ViewRotation);
    FVector2D FieldOrigin,FieldStep;
    if(!Project(FVector::ZeroVector,FieldOrigin)||!Project(ViewRotation.RotateVector(FVector(0,100,0)),FieldStep))return Top;
    const float FieldPitch=FMath::Max(.1f,float((FieldStep-FieldOrigin).Size())*ViewScale);
    const float NormalWidth=FMath::Clamp(2.6f*FieldPitch,12.f,64.f);
    const float PairWidth=FMath::Clamp(2.f*FieldPitch,10.f,48.f);
    const float SwarmWidth=FMath::Clamp(1.05f*FieldPitch,6.f,24.f);
    const float StructureWidth=FMath::Clamp(3.8f*FieldPitch,40.f,88.f);
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
    TArray<FSlateRect> Occupied;int32 HealthCount=0,UndamagedCount=0,AnnotationCount=0,OverlapPairs=0,ClampedLabels=0;
    float MaxOverlapPixels=0,SmallestNumericHeight=TNumericLimits<float>::Max();
    for (int32 LabelPriority=0;LabelPriority<3;++LabelPriority) for (const auto& Entity:State->entities)
    {
        const int32 EntityPriority=Entity.kind==rift::EntityKind::Troop?2:Entity.kind==rift::EntityKind::Building?1:0;
        if (EntityPriority!=LabelPriority) continue;
        if (Entity.dead) continue;const auto* Visual=Presentation->Visual(Entity.id);if (!Visual) continue;
        const bool ShowHealth=Visual->HasBeenDamaged();
        if(!ShowHealth)++UndamagedCount;
        const bool Dormant=Entity.kind==rift::EntityKind::Core && !Entity.active;
        const FString StatusText=Entity.stunUntil>State->elapsed?TEXT("STUN"):
            Entity.slowUntil>State->elapsed&&Entity.slowPct>0?FString::Printf(TEXT("-%d%%"),FMath::RoundToInt(Entity.slowPct*100)):
            Entity.charged?TEXT("CHARGE"):TEXT("");
        const auto* LifetimeCard=Entity.kind==rift::EntityKind::Building?rift::FindCard(Entity.cardId):nullptr;
        const bool ShowLifetime=LifetimeCard && LifetimeCard->lifetime>0;
        // First-damage visibility applies only to HP, not independent state
        // labels or the defensive building's remaining lifetime indicator.
        if(!ShowHealth && !Dormant && StatusText.IsEmpty() && !ShowLifetime)continue;
        FVector2D Position;if (!Project(Visual->HealthLocation(),Position)) continue;
        if (Position.X<0 || Position.Y<0 || Position.X>Extent.X || Position.Y>Extent.Y) continue;
        const bool Tower=Entity.kind!=rift::EntityKind::Troop;
        const float Width=(Tower?StructureWidth:Entity.memberCount>=5?SwarmWidth:Entity.memberCount>1?PairWidth:NormalWidth)*Pixel;
        const float Height=ShowHealth?(Tower?5.f:Entity.memberCount>=5?2.5f:3.f)*Pixel:0.f;
        Position-=FVector2D(Width*.5f,10.f*Pixel);
        const FVector2D Anchor=Position;
        const FString Value=Tower && ShowHealth?FString::Printf(TEXT("%d"),FMath::CeilToInt(Entity.hp)):FString();
        const FVector2D ValueSize=Value.IsEmpty()?FVector2D::ZeroVector:FVector2D(FontMeasure->Measure(Value,TowerFont));
        const FVector2D StatusSize=StatusText.IsEmpty()?FVector2D::ZeroVector:FVector2D(FontMeasure->Measure(StatusText,Small));
        const FVector2D DormantSize=Dormant?FVector2D(FontMeasure->Measure(FStringView(TEXT("DORMANT")),Small)):FVector2D::ZeroVector;
        const FVector2D ValueOffset(Width*.5f-ValueSize.X*.5f,-ValueSize.Y-3.f*Pixel);
        const FVector2D DormantOffset(Width*.5f-DormantSize.X*.5f,Height+4.f*Pixel);
        const FVector2D StatusOffset(Width*.5f-StatusSize.X*.5f,Height+4.f*Pixel+(Dormant?DormantSize.Y+3.f*Pixel:0.f));
        const float Left=FMath::Min(0.f,float(FMath::Min3(ValueOffset.X,StatusOffset.X,DormantOffset.X)));
        const float Right=FMath::Max(Width,float(FMath::Max3(ValueOffset.X+ValueSize.X,StatusOffset.X+StatusSize.X,DormantOffset.X+DormantSize.X)));
        const float Above=Tower && ShowHealth?float(ValueOffset.Y):0.f;
        const float Below=!StatusText.IsEmpty()?float(StatusOffset.Y+StatusSize.Y):Dormant?float(DormantOffset.Y+DormantSize.Y):Height+3.f*Pixel;
        // Reserve measured glyph bounds, including overhang on a short swarm
        // bar. Placement remains a bounded search for the visible annotations.
        auto LabelArea=[&](FVector2D Point)
        {return FSlateRect(Point.X+Left-2.f*Pixel,Point.Y+Above-2.f*Pixel,
            Point.X+Right+2.f*Pixel,Point.Y+Below+2.f*Pixel);};
        struct FLabelCandidate {FVector2D Offset;float Distance;};
        TArray<FLabelCandidate,TInlineAllocator<81>> Candidates;
        const float StepX=FMath::Clamp(Right-Left+4.f*Pixel,12.f*Pixel,32.f*Pixel);
        const float StepY=FMath::Clamp(Below-Above+4.f*Pixel,7.f*Pixel,22.f*Pixel);
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
        float BestOverlap=TNumericLimits<float>::Max();bool HasSafePosition=false;
        for (const auto& Candidate:Candidates)
        {
            const FVector2D CandidatePosition=Anchor+Candidate.Offset;
            const FSlateRect Area=LabelArea(CandidatePosition);
            if (Area.Left<LabelSafe.Min.X || Area.Top<LabelSafe.Min.Y || Area.Right>LabelSafe.Max.X || Area.Bottom>LabelSafe.Max.Y) continue;
            float Overlap=0;
            for (const auto& Previous:Occupied)
            {
                const float IntersectionWidth=FMath::Min(Area.Right,Previous.Right)-FMath::Max(Area.Left,Previous.Left);
                const float IntersectionHeight=FMath::Min(Area.Bottom,Previous.Bottom)-FMath::Max(Area.Top,Previous.Top);
                if (IntersectionWidth>0 && IntersectionHeight>0) Overlap+=IntersectionWidth*IntersectionHeight;
            }
            if (Overlap<BestOverlap) {BestOverlap=Overlap;Position=CandidatePosition;HasSafePosition=true;}
            if (Overlap==0) break;
        }
        if(!HasSafePosition)
        {
            // A marker at the rear edge can need a larger shift than the local
            // search. Keep its information and clamp the measured label inside
            // the same header/hand safe area, rather than discarding it.
            Position.X=FMath::Clamp(Anchor.X,LabelSafe.Min.X-Left+2.f*Pixel,LabelSafe.Max.X-Right-2.f*Pixel);
            Position.Y=FMath::Clamp(Anchor.Y,LabelSafe.Min.Y-Above+2.f*Pixel,LabelSafe.Max.Y-Below-2.f*Pixel);
            ++ClampedLabels;
        }
        const FSlateRect PlacedArea=LabelArea(Position);
        for(const auto& Previous:Occupied)
        {
            const float IntersectionWidth=FMath::Min(PlacedArea.Right,Previous.Right)-FMath::Max(PlacedArea.Left,Previous.Left);
            const float IntersectionHeight=FMath::Min(PlacedArea.Bottom,Previous.Bottom)-FMath::Max(PlacedArea.Top,Previous.Top);
            if(IntersectionWidth>0&&IntersectionHeight>0){++OverlapPairs;MaxOverlapPixels=FMath::Max(MaxOverlapPixels,IntersectionWidth*IntersectionHeight*ViewScale*ViewScale);}
        }
        Occupied.Add(PlacedArea);++AnnotationCount;if(ShowHealth)++HealthCount;
        if(Tower && ShowHealth)SmallestNumericHeight=FMath::Min(SmallestNumericHeight,float(ValueSize.Y)*ViewScale);
        const FLinearColor Color=ARiftUnitVisual::TeamColor(Entity.team);
        if (FVector2D::Distance(Anchor,Position)>3.f*Pixel)
        {
            const FVector2D Target=Anchor+FVector2D(Width*.5f,Height+3.f*Pixel);
            const FVector2D Edge(FMath::Clamp(Target.X,Position.X,Position.X+Width),
                FMath::Clamp(Target.Y,Position.Y,Position.Y+Height));
            TArray<FVector2D> Leader{Edge,Target};
            FSlateDrawElement::MakeLines(Elements,Top+1,Geometry.ToPaintGeometry(),Leader,ESlateDrawEffect::None,
                FLinearColor(Color.R,Color.G,Color.B,.35f),true,Pixel);
        }
        if(ShowHealth)
        {
            Box(Position-FVector2D(Pixel,Pixel),FVector2D(Width+2.f*Pixel,Height+2.f*Pixel),FLinearColor(.025f,.038f,.045f,.95f));
            Box(Position,FVector2D(Width,Height),FLinearColor(.15f,.19f,.21f,.9f));
            const float Fraction=float(FMath::Clamp(Entity.hp/FMath::Max(1.,Entity.maxHp),0.,1.));
            Box(Position,FVector2D(Width*Fraction,Height),Color,1);
            if(Tower)Text(Position+ValueOffset,Value,FLinearColor(.94f,.96f,.97f),true);
        }
        if(ShowLifetime)
        {
            const float Life=FMath::Clamp(float(1.-(State->elapsed-Entity.born)/LifetimeCard->lifetime),0.f,1.f);
            Box(Position+FVector2D(0,Height+2.f*Pixel),FVector2D(Width*Life,Pixel),FLinearColor(.74f,.73f,.55f,.85f));
        }
        if(!StatusText.IsEmpty())
            Text(Position+StatusOffset,StatusText,Entity.stunUntil>State->elapsed?FLinearColor(.95f,.86f,.42f):
                Entity.slowUntil>State->elapsed&&Entity.slowPct>0?FLinearColor(.48f,.86f,1.f):
                FLinearColor(.98f,.75f,.36f));
        if(Dormant)Text(Position+DormantOffset,TEXT("DORMANT"),FLinearColor(.58f,.65f,.68f));
    }
    FString CapturePath;
    if(GetWorld()->GetTimeSeconds()>2.f&&FParse::Value(FCommandLine::Get(),TEXT("RiftCapture="),CapturePath))
    {
        static float LastLoggedPitch=-1,LastLoggedScale=-1;
        if(FMath::Abs(FieldPitch-LastLoggedPitch)>.05f||FMath::Abs(ViewScale-LastLoggedScale)>.01f)
        {
            LastLoggedPitch=FieldPitch;LastLoggedScale=ViewScale;
            RIFT_LOG(LogRift,Log,TEXT("Adaptive world health: pitchPx=%.3f normalWidthPx=%.3f pairWidthPx=%.3f swarmWidthPx=%.3f structureWidthPx=%.3f towerFontPx=%.3f smallestNumericGlyphHeightPx=%.3f labels=%d healthBars=%d undamaged=%d overlapPairs=%d maxOverlapAreaPx=%.3f clamped=%d"),
                FieldPitch,NormalWidth,PairWidth,SwarmWidth,StructureWidth,TowerFont.Size*ViewScale,
                SmallestNumericHeight<TNumericLimits<float>::Max()?SmallestNumericHeight:0.f,AnnotationCount,HealthCount,UndamagedCount,OverlapPairs,MaxOverlapPixels,ClampedLabels);
        }
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
