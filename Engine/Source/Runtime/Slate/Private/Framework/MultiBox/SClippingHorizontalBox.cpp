// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/MultiBox/SClippingHorizontalBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/ArrangedChildren.h"
#include "Styling/ToolBarStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"

void SClippingHorizontalBox::OnArrangeChildren( const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren ) const
{
	// If WrapButton hasn't been initialized, that means AddWrapButton() hasn't 
	// been called and this method isn't going to behave properly
	check(WrapButton.IsValid());

	LastClippedIdx = ClippedIdx;

	NumClippedChildren = 0;
	SHorizontalBox::OnArrangeChildren(AllottedGeometry, ArrangedChildren);

	// Remove children that are clipped by the allotted geometry
	const int32 NumChildren = ArrangedChildren.Num(); 
	int32 IndexClippedAt = NumChildren;
	for (int32 ChildIdx = NumChildren - 2; ChildIdx >= 0; --ChildIdx)
	{
		const FArrangedWidget& CurWidget = ArrangedChildren[ChildIdx];
		// Ceil (Minus a tad for float precision) to ensure contents are not a sub-pixel larger than the box, which will create an unnecessary wrap button
		FVector2D AbsWidgetPos = CurWidget.Geometry.LocalToAbsolute(CurWidget.Geometry.GetLocalSize());
		FVector2D AbsBoxPos = FVector2D(AllottedGeometry.AbsolutePosition) + AllottedGeometry.GetLocalSize() * AllottedGeometry.Scale;
		const FVector2D WidgetPosRounded = FVector2D(FMath::CeilToInt(AbsWidgetPos.X - KINDA_SMALL_NUMBER), FMath::CeilToInt(AbsWidgetPos.Y - KINDA_SMALL_NUMBER));
		const FVector2D BoxPosRounded = FVector2D(FMath::CeilToInt(AbsBoxPos.X - KINDA_SMALL_NUMBER), FMath::CeilToInt(AbsBoxPos.Y - KINDA_SMALL_NUMBER));
		if (WidgetPosRounded.X > BoxPosRounded.X)
		{
			++NumClippedChildren;
			ArrangedChildren.Remove(ChildIdx);
			IndexClippedAt = ChildIdx;
		}
	}

	if (IndexClippedAt == NumChildren)
	{
		// Note do not increment NumClippedChildren here. This is to remove the wrap button
		// None of the children are being clipped, so remove the wrap button
		ArrangedChildren.Remove(ArrangedChildren.Num() - 1);
	}
	else if (ArrangedChildren.Num() > 0)
	{
		// Insert and right align the wrap button, for occlusion checking and insertion below
		FArrangedWidget& ArrangedWrapButton = ArrangedChildren[ArrangedChildren.Num() - 1];
		FGeometry& WrapButtonGeometry = ArrangedWrapButton.Geometry;

		if (const bool bHasSpaceForWrapButton = WrapButtonWidth <= AllottedGeometry.GetLocalSize().X)
		{
			const float AdjustedWrapButtonWidth = FMath::Min(AllottedGeometry.GetLocalSize().X, WrapButtonWidth);

			const FVector2D WrapButtonSize = FVector2D(AdjustedWrapButtonWidth, WrapButtonGeometry.GetLocalSize().Y);
			WrapButtonGeometry = AllottedGeometry.MakeChild(
				WrapButtonSize,
				FSlateLayoutTransform(AllottedGeometry.GetLocalSize() - WrapButtonSize));

			const int32 WrapButtonXPosition = FMath::TruncToInt(WrapButtonGeometry.AbsolutePosition.X);

			// Further remove any children that the wrap button overlaps with
			for (int32 ChildIdx = IndexClippedAt - 1; ChildIdx >= 0; --ChildIdx)
			{
				const FArrangedWidget& CurWidget = ArrangedChildren[ChildIdx];
				if (FMath::TruncToInt(CurWidget.Geometry.AbsolutePosition.X + CurWidget.Geometry.GetLocalSize().X * CurWidget.Geometry.Scale) > WrapButtonXPosition)
				{
					++NumClippedChildren;
					ArrangedChildren.Remove(ChildIdx);
				}
			}
		}
		else // No space left for anything including WrapButton
		{
			ArrangedChildren.Empty();
		}
	}

	ClippedIdx = ArrangedChildren.Num() - 1;
}

int32 SClippingHorizontalBox::OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const
{
	return SHorizontalBox::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

FVector2D SClippingHorizontalBox::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	FVector2D Size = SBoxPanel::ComputeDesiredSize(LayoutScaleMultiplier);
	{
		// If the wrap button isn't being shown, subtract it's size from the total desired size
		const SBoxPanel::FSlot& Child = Children[Children.Num() - 1];
		const FVector2D& ChildDesiredSize = Child.GetWidget()->GetDesiredSize();
		Size.X -= ChildDesiredSize.X;
	}
	return Size;
}

void SClippingHorizontalBox::Construct( const FArguments& InArgs )
{
	OnWrapButtonClicked = InArgs._OnWrapButtonClicked;
	StyleSet = InArgs._StyleSet;
	StyleName = InArgs._StyleName;
	bIsFocusable = InArgs._IsFocusable;

	LastClippedIdx = ClippedIdx = INDEX_NONE;
}

void SClippingHorizontalBox::AddWrapButton()
{
	const FToolBarStyle& ToolBarStyle = StyleSet->GetWidgetStyle<FToolBarStyle>(StyleName);

	// Construct the wrap button used in toolbars and menubars
	// Always allow this to be focusable to prevent the menu from collapsing during interaction
	WrapButton = 
		SNew( SComboButton )
		.HasDownArrow( false )
		.ButtonStyle(&ToolBarStyle.ButtonStyle)
		.ContentPadding( FMargin(4.f, 0.f) )
		.ToolTipText( NSLOCTEXT("Slate", "ExpandToolbar", "Click to expand toolbar") )
		.OnGetMenuContent( OnWrapButtonClicked )
		.Cursor( EMouseCursor::Default )
		.OnMenuOpenChanged(this, &SClippingHorizontalBox::OnWrapButtonOpenChanged)
		.IsFocusable(true)
		.ButtonContent()
		[
			SNew(SImage)
			.Image(&ToolBarStyle.ExpandBrush)
		];

	// Perform a prepass to get a valid DesiredSize value below
	WrapButton->SlatePrepass(1.0f);
	WrapButtonWidth = WrapButton->GetDesiredSize().X;

	// Add the wrap button
	AddSlot()
	.FillWidth(0.0f) // Effectively makes this widget 0 width, so it exists as a slot/child, but isn't considered for layout
	.Padding(0.f)
	[
		WrapButton.ToSharedRef()
	];
}

void SClippingHorizontalBox::OnWrapButtonOpenChanged(bool bIsOpen)
{
	if (bIsOpen && !WrapButtonOpenTimer.IsValid())
	{
		WrapButtonOpenTimer = RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SClippingHorizontalBox::UpdateWrapButtonStatus));
	}
	else if(!bIsOpen && WrapButtonOpenTimer.IsValid())
	{
		UnRegisterActiveTimer(WrapButtonOpenTimer.ToSharedRef());
		WrapButtonOpenTimer.Reset();
	}
}

EActiveTimerReturnType SClippingHorizontalBox::UpdateWrapButtonStatus(double CurrentTime, float DeltaTime)
{
	if (LastClippedIdx != ClippedIdx || !WrapButton->IsOpen())
	{
		WrapButton->SetIsOpen(false);
		WrapButtonOpenTimer.Reset();
		return EActiveTimerReturnType::Stop;
	}

	return EActiveTimerReturnType::Continue;
}
