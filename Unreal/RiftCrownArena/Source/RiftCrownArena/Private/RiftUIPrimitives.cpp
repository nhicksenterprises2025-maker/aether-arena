#include "RiftUIPrimitives.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Rendering/DrawElements.h"

namespace {
void EmptyRoot(UWidgetTree* Tree){if(!Tree->RootWidget){auto* Root=Tree->ConstructWidget<UBorder>();Root->SetBrushColor(FLinearColor::Transparent);Tree->RootWidget=Root;}}
void Box(FSlateWindowElementList& Out,int32 Layer,const FGeometry& G,FVector2D Position,FVector2D Size,const FSlateBrush& Brush,FLinearColor Color){FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(Size,FSlateLayoutTransform(Position)),&Brush,ESlateDrawEffect::None,Color);}
}
TSharedRef<SWidget> URiftCrownIcon::RebuildWidget(){EmptyRoot(WidgetTree);return Super::RebuildWidget();}
int32 URiftCrownIcon::NativePaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled)const{
    Layer=Super::NativePaint(Args,G,Clip,Out,Layer,Style,Enabled);const auto S=G.GetLocalSize();
    TArray<FVector2D> P;for(auto V:TArray<FVector2D>{{.10,.25},{.27,.46},{.50,.12},{.73,.46},{.90,.25},{.81,.72},{.19,.72},{.10,.25}})P.Add(FVector2D(V.X*S.X,V.Y*S.Y));
    FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),P,ESlateDrawEffect::None,Tint*Style.GetColorAndOpacityTint(),true,3.f);
    TArray<FVector2D> Base={{S.X*.22,S.Y*.86},{S.X*.78,S.Y*.86}};FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),Base,ESlateDrawEffect::None,Tint,true,3.f);return Layer+1;
}
TSharedRef<SWidget> URiftAetherMeter::RebuildWidget(){EmptyRoot(WidgetTree);return Super::RebuildWidget();}
int32 URiftAetherMeter::NativePaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled)const{
    Layer=Super::NativePaint(Args,G,Clip,Out,Layer,Style,Enabled);auto S=G.GetLocalSize();float Cell=(S.X-27)/10.f;
    const FSlateRoundedBoxBrush Frame(FLinearColor::White,3.f),Fill(FLinearColor::White,2.f);
    for(int32 I=0;I<10;++I){FVector2D P(I*(Cell+3),0);Box(Out,Layer+1,G,P,{Cell,S.Y},Frame,FLinearColor(.075f,.095f,.16f,1));float Fraction=FMath::Clamp(Bank-I,0.f,1.f);if(Fraction>0){Box(Out,Layer+2,G,P+FVector2D(1,1),{(Cell-2)*Fraction,S.Y-2},Fill,FLinearColor(.54f,.23f,.94f,1));Box(Out,Layer+3,G,P+FVector2D(2,2),{FMath::Max(0.f,(Cell-4)*Fraction),2},Fill,FLinearColor(.82f,.63f,1,1));}}return Layer+3;
}
TSharedRef<SWidget> URiftMenuBackdrop::RebuildWidget(){EmptyRoot(WidgetTree);return Super::RebuildWidget();}
int32 URiftMenuBackdrop::NativePaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled)const{
    Layer=Super::NativePaint(Args,G,Clip,Out,Layer,Style,Enabled);auto S=G.GetLocalSize();const FSlateRoundedBoxBrush B(FLinearColor::White,0.f);Box(Out,Layer+1,G,{0,0},S,B,FLinearColor(.009f,.021f,.042f,1));
    for(float X=-S.Y;X<S.X;X+=96){TArray<FVector2D>P={{X,0},{X+S.Y,S.Y}};FSlateDrawElement::MakeLines(Out,Layer+2,G.ToPaintGeometry(),P,ESlateDrawEffect::None,FLinearColor(.11f,.19f,.29f,.12f),true,1.f);}
    Box(Out,Layer+3,G,{0,0},{S.X,3},B,FLinearColor(.87f,.57f,.16f,1));return Layer+3;
}
