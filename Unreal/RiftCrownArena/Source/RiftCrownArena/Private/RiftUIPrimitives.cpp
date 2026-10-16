#include "RiftUIPrimitives.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Rendering/DrawElements.h"

namespace {
void EmptyRoot(UWidgetTree* Tree){if(!Tree->RootWidget){auto* Root=Tree->ConstructWidget<UBorder>();Root->SetBrushColor(FLinearColor::Transparent);Tree->RootWidget=Root;}}
void Box(FSlateWindowElementList& Out,int32 Layer,const FGeometry& G,FVector2D Position,FVector2D Size,const FSlateBrush& Brush,FLinearColor Color){FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(Size,FSlateLayoutTransform(Position)),&Brush,ESlateDrawEffect::None,Color);}
}
TSharedRef<SWidget> URiftFrameArt::RebuildWidget(){EmptyRoot(WidgetTree);return Super::RebuildWidget();}
int32 URiftFrameArt::NativePaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled)const{
    Layer=Super::NativePaint(Args,G,Clip,Out,Layer,Style,Enabled);const auto S=G.GetLocalSize();if(S.X<2||S.Y<2)return Layer;
    const auto Tint=Style.GetColorAndOpacityTint();const FSlateRoundedBoxBrush Brush(FLinearColor::White,5.f);
    Box(Out,Layer+1,G,{0,3},S,Brush,FLinearColor(0,0,0,.38f)*Tint);
    Box(Out,Layer+2,G,{0,0},S,Brush,FLinearColor(.24f,.25f,.24f,1)*Tint);
    Box(Out,Layer+3,G,{1,1},S-FVector2D(2,2),Brush,Face*Tint);
    auto Line=[&](TArray<FVector2D> Points,FLinearColor Color,float Width=1.f){FSlateDrawElement::MakeLines(Out,Layer+4,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color*Tint,true,Width);};
    Line({{6,2},{S.X-6,2}},FLinearColor(.61f,.58f,.43f,.55f));
    Line({{5,S.Y-2},{S.X-5,S.Y-2}},FLinearColor(0,0,0,.65f));
    if(S.X>120&&S.Y>55){
        const FLinearColor Brass(.60f,.42f,.20f,1);
        for(float X:{4.f,float(S.X-4)})for(float Y:{5.f,float(S.Y-5)}){
            const float DX=X<S.X*.5f?1.f:-1.f,DY=Y<S.Y*.5f?1.f:-1.f;
            Line({{X,Y+10*DY},{X,Y},{X+14*DX,Y}},Brass,2.f);
            Box(Out,Layer+5,G,{X+4*DX-1,Y+4*DY-1},{2,2},Brush,FLinearColor(.79f,.65f,.36f,1)*Tint);
        }
    }
    return Layer+5;
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
    // Quiet carved-stone course lines and a heraldic standard, behind the content.
    for(int32 I=0;I<14;++I){const float Y=I*104.f;TArray<FVector2D>P={{0,Y},{S.X,Y}};FSlateDrawElement::MakeLines(Out,Layer+2,G.ToPaintGeometry(),P,ESlateDrawEffect::None,FLinearColor(.19f,.23f,.25f,.10f),true,1.f);for(float X=(I%2)*140.f;X<S.X;X+=280.f){TArray<FVector2D> Seam={{X,Y},{X,Y+104}};FSlateDrawElement::MakeLines(Out,Layer+2,G.ToPaintGeometry(),Seam,ESlateDrawEffect::None,FLinearColor(.19f,.23f,.25f,.07f),true,1.f);}}
    const float X=S.X-142;Box(Out,Layer+2,G,{X-54,0},{108,FMath::Min(S.Y*.72,600.)},B,FLinearColor(.19f,.105f,.21f,.25f));
    for(float Edge:{X-54,X+54}){TArray<FVector2D> P={{Edge,0},{Edge,S.Y*.72}};FSlateDrawElement::MakeLines(Out,Layer+3,G.ToPaintGeometry(),P,ESlateDrawEffect::None,FLinearColor(.58f,.42f,.22f,.20f),true,2.f);}
    TArray<FVector2D> Crest={{X-32,88},{X-20,108},{X,72},{X+20,108},{X+32,88},{X+26,132},{X-26,132},{X-32,88}};FSlateDrawElement::MakeLines(Out,Layer+3,G.ToPaintGeometry(),Crest,ESlateDrawEffect::None,FLinearColor(.72f,.52f,.23f,.26f),true,3.f);
    Box(Out,Layer+4,G,{0,0},{S.X,3},B,FLinearColor(.65f,.46f,.23f,1));return Layer+4;
}
