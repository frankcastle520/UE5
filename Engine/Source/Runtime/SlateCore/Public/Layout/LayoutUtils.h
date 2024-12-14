// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Layout/Children.h"
#include "Types/SlateStructs.h"
#include "CoreMinimal.h"
#include "Margin.h"
#include "Visibility.h"
#include "SlateRect.h"
#include "ArrangedChildren.h"
#include "FlowDirection.h"
#include "Templates/UnrealTypeTraits.h"

struct AlignmentArrangeResult
{
	AlignmentArrangeResult( float InOffset, float InSize )
	: Offset(InOffset)
	, Size(InSize)
	{
	}
	
	float Offset;
	float Size;
};

namespace ArrangeUtils
{
	/** Gets the alignment of an axis-agnostic int32 so that we can do alignment on an axis without caring about its orientation */
	template<EOrientation Orientation>
	struct GetChildAlignment
	{
		template<typename SlotType>
		static int32 AsInt(EFlowDirection InFlowDirection, const SlotType& InSlot );
	};

	template<>
	struct GetChildAlignment<Orient_Horizontal>
	{
		template<typename SlotType>
		static int32 AsInt(EFlowDirection InFlowDirection, const SlotType& InSlot )
		{
			switch (InFlowDirection)
			{
			default:
			case EFlowDirection::LeftToRight:
				return static_cast<int32>(InSlot.GetHorizontalAlignment());
			case EFlowDirection::RightToLeft:
				switch (InSlot.GetHorizontalAlignment())
				{
				case HAlign_Left:
					return static_cast<int32>(HAlign_Right);
				case HAlign_Right:
					return static_cast<int32>(HAlign_Left);
				default:
					return static_cast<int32>(InSlot.GetHorizontalAlignment());
				}
			}
		}
	};

	template<>
	struct GetChildAlignment<Orient_Vertical>
	{
		template<typename SlotType>
		static int32 AsInt(EFlowDirection InFlowDirection, const SlotType& InSlot )
		{
			// InFlowDirection has no effect in vertical orientations.
			return static_cast<int32>(InSlot.GetVerticalAlignment());
		}
	};

	/**
	 * Same as AlignChild but force the alignment to be fill.
	 * @return  Offset and Size of widget
	 */
	template<EOrientation Orientation>
	static AlignmentArrangeResult AlignFill(float AllottedSize, const FMargin& SlotPadding, const float ContentScale = 1.0f)
	{
		const FMargin& Margin = SlotPadding;
		const float TotalMargin = Margin.GetTotalSpaceAlong<Orientation>();
		const float MarginPre = (Orientation == Orient_Horizontal) ? Margin.Left : Margin.Top;
		return AlignmentArrangeResult(MarginPre, FMath::Max((AllottedSize - TotalMargin) * ContentScale, 0.f));
	}

	/**
	 * Same as AlignChild but force the alignment to be center.
	 * @return  Offset and Size of widget
	 */
	template<EOrientation Orientation>
	static AlignmentArrangeResult AlignCenter(float AllottedSize, float ChildDesiredSize, const FMargin& SlotPadding, const float ContentScale = 1.0f, bool bClampToParent = true)
	{
		const FMargin& Margin = SlotPadding;
		const float TotalMargin = Margin.GetTotalSpaceAlong<Orientation>();
		const float MarginPre = (Orientation == Orient_Horizontal) ? Margin.Left : Margin.Top;
		const float MarginPost = (Orientation == Orient_Horizontal) ? Margin.Right : Margin.Bottom;
		const float ChildSize = FMath::Max((bClampToParent ? FMath::Min(ChildDesiredSize, AllottedSize - TotalMargin) : ChildDesiredSize), 0.f);
		return AlignmentArrangeResult((AllottedSize - ChildSize) / 2.0f + MarginPre - MarginPost, ChildSize);
	}
}



/**
 * Helper method to BoxPanel::ArrangeChildren.
 * 
 * @param AllottedSize         The size available to arrange the widget along the given orientation
 * @param ChildToArrange       The widget and associated layout information
 * @param SlotPadding          The padding to when aligning the child
 * @param ContentScale         The scale to apply to the child before aligning it.
 * @param bClampToParent       If true the child's size is clamped to the allotted size before alignment occurs, if false, the child's desired size is used, even if larger than the allotted size.
 * 
 * @return  Offset and Size of widget
 */
template<EOrientation Orientation, typename SlotType>
static AlignmentArrangeResult AlignChild(EFlowDirection InLayoutFlow, float AllottedSize, float ChildDesiredSize, const SlotType& ChildToArrange, const FMargin& SlotPadding, const float& ContentScale = 1.0f, bool bClampToParent = true)
{
	const FMargin& Margin = SlotPadding;
	const float TotalMargin = Margin.GetTotalSpaceAlong<Orientation>();
	const float MarginPre = ( Orientation == Orient_Horizontal ) ? Margin.Left : Margin.Top;
	const float MarginPost = ( Orientation == Orient_Horizontal ) ? Margin.Right : Margin.Bottom;

	const int32 Alignment = ArrangeUtils::GetChildAlignment<Orientation>::AsInt(InLayoutFlow, ChildToArrange);

	switch (Alignment)
	{
	case HAlign_Fill:
		return AlignmentArrangeResult(MarginPre, FMath::Max((AllottedSize - TotalMargin) * ContentScale, 0.f));
	}
	
	const float ChildSize = FMath::Max((bClampToParent ? FMath::Min(ChildDesiredSize, AllottedSize - TotalMargin) : ChildDesiredSize), 0.f);

	switch( Alignment )
	{
	case HAlign_Left: // same as Align_Top
		return AlignmentArrangeResult(MarginPre, ChildSize);
	case HAlign_Center:
		return AlignmentArrangeResult(( AllottedSize - ChildSize ) / 2.0f + MarginPre - MarginPost, ChildSize);
	case HAlign_Right: // same as Align_Bottom		
		return AlignmentArrangeResult(AllottedSize - ChildSize - MarginPost, ChildSize);
	}

	// Same as Fill
	return AlignmentArrangeResult(MarginPre, FMath::Max(( AllottedSize - TotalMargin ) * ContentScale, 0.f));
}

template<EOrientation Orientation, typename SlotType>
static AlignmentArrangeResult AlignChild(float AllottedSize, float ChildDesiredSize, const SlotType& ChildToArrange, const FMargin& SlotPadding, const float& ContentScale = 1.0f, bool bClampToParent = true)
{
	return AlignChild<Orientation, SlotType>(EFlowDirection::LeftToRight, AllottedSize, ChildDesiredSize, ChildToArrange, SlotPadding, ContentScale, bClampToParent);
}

template<EOrientation Orientation, typename SlotType>
static AlignmentArrangeResult AlignChild(EFlowDirection InLayoutFlow, float AllottedSize, const SlotType& ChildToArrange, const FMargin& SlotPadding, const float& ContentScale = 1.0f, bool bClampToParent = true)
{
	const FMargin& Margin = SlotPadding;
	const float TotalMargin = Margin.GetTotalSpaceAlong<Orientation>();
	const float MarginPre = ( Orientation == Orient_Horizontal ) ? Margin.Left : Margin.Top;
	const float MarginPost = ( Orientation == Orient_Horizontal ) ? Margin.Right : Margin.Bottom;

	const int32 Alignment = ArrangeUtils::GetChildAlignment<Orientation>::AsInt(InLayoutFlow, ChildToArrange);

	switch (Alignment)
	{
	case HAlign_Fill:
		return AlignmentArrangeResult(MarginPre, FMath::Max((AllottedSize - TotalMargin) * ContentScale, 0.f));
	}

	const float ChildDesiredSize = ( Orientation == Orient_Horizontal )
		? ( ChildToArrange.GetWidget()->GetDesiredSize().X * ContentScale )
		: ( ChildToArrange.GetWidget()->GetDesiredSize().Y * ContentScale );

	const float ChildSize = FMath::Max((bClampToParent ? FMath::Min(ChildDesiredSize, AllottedSize - TotalMargin) : ChildDesiredSize), 0.f);

	switch ( Alignment )
	{
	case HAlign_Left: // same as Align_Top
		return AlignmentArrangeResult(MarginPre, ChildSize);
	case HAlign_Center:
		return AlignmentArrangeResult(( AllottedSize - ChildSize ) / 2.0f + MarginPre - MarginPost, ChildSize);
	case HAlign_Right: // same as Align_Bottom		
		return AlignmentArrangeResult(AllottedSize - ChildSize - MarginPost, ChildSize);
	}

	// Same as Fill
	return AlignmentArrangeResult(MarginPre, FMath::Max((AllottedSize - TotalMargin) * ContentScale, 0.f));
}

template<EOrientation Orientation, typename SlotType>
static AlignmentArrangeResult AlignChild(float AllottedSize, const SlotType& ChildToArrange, const FMargin& SlotPadding, const float& ContentScale = 1.0f, bool bClampToParent = true)
{
	return AlignChild<Orientation, SlotType>(EFlowDirection::LeftToRight, AllottedSize, ChildToArrange, SlotPadding, ContentScale, bClampToParent);
}


/**
 * Arrange a ChildSlot within the AllottedGeometry and populate ArrangedChildren with the arranged result.
 * The code makes certain assumptions about the type of ChildSlot.
 */
template<typename SlotType>
static void ArrangeSingleChild(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren, const SlotType& ChildSlot, const TAttribute<FVector2D>& ContentScale)
{
	ArrangeSingleChild<SlotType>(EFlowDirection::LeftToRight, AllottedGeometry, ArrangedChildren, ChildSlot, ContentScale);
}

template<typename SlotType>
static void ArrangeSingleChild(EFlowDirection InFlowDirection, const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren, const SlotType& ChildSlot, const TAttribute<FVector2D>& ContentScale)
{
	const EVisibility ChildVisibility = ChildSlot.GetWidget()->GetVisibility();
	if ( ArrangedChildren.Accepts(ChildVisibility) )
	{
		const FVector2D ThisContentScale = ContentScale.Get();
		const FMargin SlotPadding(LayoutPaddingWithFlow(InFlowDirection, ChildSlot.GetPadding()));
		const AlignmentArrangeResult XResult = AlignChild<Orient_Horizontal>(InFlowDirection, AllottedGeometry.GetLocalSize().X, ChildSlot, SlotPadding, ThisContentScale.X);
		const AlignmentArrangeResult YResult = AlignChild<Orient_Vertical>(AllottedGeometry.GetLocalSize().Y, ChildSlot, SlotPadding, ThisContentScale.Y);

		ArrangedChildren.AddWidget( ChildVisibility, AllottedGeometry.MakeChild(
				ChildSlot.GetWidget(),
				FVector2D(XResult.Offset, YResult.Offset),
				FVector2D(XResult.Size, YResult.Size)
		) );
	}
}

template<typename SlotType>
static void ArrangeSingleChild(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren, const SlotType& ChildSlot, const FVector2D& ContentScale)
{
	ArrangeSingleChild<SlotType>(EFlowDirection::LeftToRight, AllottedGeometry, ArrangedChildren, ChildSlot, ContentScale);
}

template<typename SlotType>
static void ArrangeSingleChild(EFlowDirection InFlowDirection, const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren, const SlotType& ChildSlot, const FVector2D& ContentScale)
{
	const EVisibility ChildVisibility = ChildSlot.GetWidget()->GetVisibility();
	if (ArrangedChildren.Accepts(ChildVisibility))
	{
		const FVector2D ThisContentScale = ContentScale;
		const FMargin SlotPadding(LayoutPaddingWithFlow(InFlowDirection, ChildSlot.GetPadding()));
		const AlignmentArrangeResult XResult = AlignChild<Orient_Horizontal>(InFlowDirection, AllottedGeometry.GetLocalSize().X, ChildSlot, SlotPadding, ThisContentScale.X);
		const AlignmentArrangeResult YResult = AlignChild<Orient_Vertical>(AllottedGeometry.GetLocalSize().Y, ChildSlot, SlotPadding, ThisContentScale.Y);

		ArrangedChildren.AddWidget(ChildVisibility, AllottedGeometry.MakeChild(
			ChildSlot.GetWidget(),
			FVector2f(XResult.Size, YResult.Size),
			FSlateLayoutTransform(FVector2f(XResult.Offset, YResult.Offset))
		));
	}
}

template<EOrientation Orientation, typename SlotType>
static void ArrangeChildrenInStack(EFlowDirection InLayoutFlow, const TPanelChildren<SlotType>& Children, const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren, float InOffset, bool bInAllowShrink)
{

	if (Children.Num() == 0)
	{
		return;
	}

	// Allotted space will be given to fixed-size children first.
	// Remaining space will be proportionately divided between stretch children (SizeRule_Stretch and SizeRule_StretchContent)
	// based on their stretch coefficient.

	// Helper function to clamp to max size, if the constraint is set.
	auto ClampSize = [](const float Size, const float MinSize, const float MaxSize)
	{
		return FMath::Clamp(
			Size,
			MinSize > 0.0f ? MinSize : 0.0f,
			MaxSize > 0.0f ? MaxSize : std::numeric_limits<float>::max());
	};

	float GrowStretchCoefficientTotal = 0.0f;
	float ShrinkStretchCoefficientTotal = 0.0f;
	float FixedSizeTotal = 0.0f;
	float StretchSizeTotal = 0.0f;

	struct FStretchItem
	{
		// Size of the item
		float Size = 0.0f;
		// Initial size of the item
		float BasisSize = 0.0f;
		// Min size constraint of the item.
		float MinSize = 0.0f;
		// Max size constraint of the item.
		float MaxSize = 0.0f;
		// Stretch coefficient when the items are growing.
		float GrowStretchValue = 0.0f;
		// Stretch coefficient when the items are shrinking.
		float ShrinkStretchValue = 0.0f;
		// True if the constraints of the item has been satisfied.
		bool bFrozen = false;
		// Sizing rule for the item.
		FSizeParam::ESizeRule SizeRule = FSizeParam::ESizeRule::SizeRule_Auto;
	};
	TArray<FStretchItem, TInlineAllocator<16>> StretchItems;
	StretchItems.Init({}, Children.Num());

	bool bAnyChildVisible = false;
	bool bAnyStretchContentItems = false;
	bool bAnyStretchItems = false;

	// Compute the sum of stretch coefficients (SizeRule_Stretch & SizeRule_StretchContent) and space required by fixed-size widgets (SizeRule_Auto),
	// as well as the total desired size.
	for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
	{
		const SlotType& CurChild = Children[ChildIndex];

		if (CurChild.GetWidget()->GetVisibility() != EVisibility::Collapsed)
		{
			bAnyChildVisible = true;

			// All widgets contribute their margin to the fixed space requirement
			FixedSizeTotal += CurChild.GetPadding().template GetTotalSpaceAlong<Orientation>();

			FVector2f ChildDesiredSize = CurChild.GetWidget()->GetDesiredSize();

			// Auto-sized children contribute their desired size to the fixed space requirement
			float ChildSize = (Orientation == Orient_Vertical)
				? ChildDesiredSize.Y
				: ChildDesiredSize.X;

			const float MinSize = CurChild.GetMinSize();
			const float MaxSize = CurChild.GetMaxSize();

			FStretchItem& Item = StretchItems[ChildIndex];
			Item.MinSize = MinSize;
			Item.MaxSize = MaxSize;
			Item.SizeRule = CurChild.GetSizeRule();

			// Clamp to the max size if it was specified
			ChildSize = ClampSize(ChildSize, MinSize, MaxSize);

			if (CurChild.GetSizeRule() == FSizeParam::SizeRule_Stretch)
			{
				// Using same shrink and grow since otherwise the transition would be discontinuous as (reference) basis size is 0.
				Item.GrowStretchValue = CurChild.GetSizeValue();
				Item.ShrinkStretchValue = Item.GrowStretchValue;
				Item.Size = 0.0f;
				Item.BasisSize = 0.0f;

				// For stretch children we save sum up the stretch coefficients
				GrowStretchCoefficientTotal += Item.GrowStretchValue;
				ShrinkStretchCoefficientTotal += Item.ShrinkStretchValue;
				StretchSizeTotal += ChildSize;

				bAnyStretchItems = true;
			}
			else if (CurChild.GetSizeRule() == FSizeParam::SizeRule_StretchContent)
			{
				// Allow separate values from grow and shrink, as the adjustment is relative to the child size.
				Item.GrowStretchValue = FMath::Max(0.f, CurChild.GetSizeValue());
				Item.ShrinkStretchValue = FMath::Max(0.f,CurChild.GetShrinkSizeValue());
				Item.Size = ChildSize;
				Item.BasisSize = ChildSize;

				// For sized stretch we sum to coefficients, but also treat the size as fixed.
				GrowStretchCoefficientTotal += Item.GrowStretchValue;
				ShrinkStretchCoefficientTotal += Item.ShrinkStretchValue;
				StretchSizeTotal += ChildSize;

				bAnyStretchContentItems = true;
			}
			else
			{
				FixedSizeTotal += ChildSize;

				Item.GrowStretchValue = 0.0f;
				Item.ShrinkStretchValue = 0.0f;
				Item.Size = ChildSize;
				Item.BasisSize = ChildSize;
			}
		}
	}

	if (!bAnyChildVisible)
	{
		return;
	}

	// When shrink is not allowed, we'll ensure to use all the space desired by the stretchable widgets.
	const float MinAvailableSpace = bInAllowShrink ? 0.0f : StretchSizeTotal;

	const float AllottedSize = Orientation == Orient_Vertical
		? AllottedGeometry.GetLocalSize().Y
		: AllottedGeometry.GetLocalSize().X;

	// The space available for SizeRule_Stretch and SizeRule_StretchContent widgets is any space that wasn't taken up by fixed-sized widgets.
	float AvailableSpace = FMath::Max(MinAvailableSpace, AllottedSize - FixedSizeTotal);

	// Apply SizeRule_Stretch.
	if (bAnyStretchItems && GrowStretchCoefficientTotal > 0.0f)
	{
		// Distribute available space amongst the SizeRule_Stretch items proportional to the their stretch coefficient.
		float UsedSpace = 0.0f;
		for (FStretchItem& Item : StretchItems)
		{
			if (Item.SizeRule == FSizeParam::SizeRule_Stretch)
			{
				// Stretch widgets get a fraction of the space remaining after all the fixed-space requirements are met.
				// Supporting only one stretch value since otherwise the transition would be discontinuous as (reference) basis size is 0.
				const float Size = AvailableSpace * Item.GrowStretchValue / GrowStretchCoefficientTotal;

				Item.Size = ClampSize(Size, Item.MinSize, Item.MaxSize);
				
				UsedSpace += Item.Size;
			}
		}
		AvailableSpace -= UsedSpace;
	}

	// Apply SizeRule_StretchContent.
	const bool bIsGrowing = AvailableSpace > StretchSizeTotal;

	const bool bCanStretch = bIsGrowing
		? (GrowStretchCoefficientTotal > 0.0f)
		: (ShrinkStretchCoefficientTotal > 0.0f);

	if (bAnyStretchContentItems && bCanStretch)
	{
		// Each StretchContent item starts at desired size and shrinks or grows based on available size.
		// First, consume each items desired size from the available space.
		// The remainder is corrected by growing ot shrinking the items.
		int32 NumStretchContentItems = 0;
		for (FStretchItem& Item : StretchItems)
		{
			if (Item.SizeRule == FSizeParam::SizeRule_StretchContent)
			{
				AvailableSpace -= Item.Size;
				NumStretchContentItems++;

				// If the item cannot shrink or grow, mark it already frozen.
				if (bIsGrowing)
				{
					Item.bFrozen |= FMath::IsNearlyZero(Item.GrowStretchValue);
				}
				else
				{
					Item.bFrozen |= FMath::IsNearlyZero(Item.ShrinkStretchValue);
				}
			}
		}

		// Run number of passes to satisfy the StretchContent constraints.
		// On each pass distribute the available space to non-frozen items.
		// An item gets frozen if it's (min/max) constraints are violated.
		// This makes sure that we distribute all of the available space, event if small items collapse or if items clamp to max size.
		// Each iteration should solve at least one constraint.
		// In practice most layouts solve in 2 passes, we're capping to 5 iterations to keep things in fixed budget.
		const int32 MaxPasses = FMath::Min(NumStretchContentItems, 5);
		for (int32 Pass = 0; Pass < MaxPasses; Pass++)
		{
			// If no available space, stop.
			if (FMath::IsNearlyZero(AvailableSpace))
			{
				break;
			}

			// On each pass calculate the total coefficients for valid items.
			GrowStretchCoefficientTotal = 0.0f;
			ShrinkStretchCoefficientTotal = 0.0f;

			for (const FStretchItem& Item : StretchItems)
			{
				if (Item.SizeRule == FSizeParam::SizeRule_StretchContent
					&& !Item.bFrozen)
				{
					// Items are grown proportional to their stretch value.
					GrowStretchCoefficientTotal += Item.GrowStretchValue;
					// Items are shrank proportional to their stretch value and size. This is to emulate the flexbox behavior.
					ShrinkStretchCoefficientTotal += Item.ShrinkStretchValue * Item.BasisSize;
				}
			}

			const float StretchCoefficientTotal = bIsGrowing
				? GrowStretchCoefficientTotal
				: ShrinkStretchCoefficientTotal;
			
			// If none of the items can stretch, stop.
			if (StretchCoefficientTotal < UE_KINDA_SMALL_NUMBER)
			{
				break;
			}

			float ConsumedSpace = 0.0f;
			
			for (FStretchItem& Item : StretchItems)
			{
				if (Item.SizeRule == FSizeParam::SizeRule_StretchContent
					&& !Item.bFrozen)
				{
					const float SizeAdjust = bIsGrowing
						? (AvailableSpace * (Item.GrowStretchValue / GrowStretchCoefficientTotal))
						: (AvailableSpace * (Item.ShrinkStretchValue * Item.BasisSize / ShrinkStretchCoefficientTotal));

					// If the item cannot be adjusted anymore, mark it frozen.
					if (FMath::IsNearlyZero(SizeAdjust))
					{
						Item.bFrozen = true;
						continue;
					}

					const float MinSize = Item.MinSize;
					const float MaxSize = Item.MaxSize;
					const bool bHasMaxConstraint = MaxSize > 0.0f; 

					if ((Item.Size + SizeAdjust) <= MinSize)
					{
						// Adjustment goes past min constraint, apply what we can and freeze since the item cannot change anymore.
						ConsumedSpace += MinSize - Item.Size;
						Item.Size = MinSize;
						Item.bFrozen = true;
					}
					else if (bHasMaxConstraint
						&& (Item.Size + SizeAdjust) >= MaxSize)
					{
						// Adjustment goes past max constraint, apply what we can and freeze since the item cannot change anymore.
						ConsumedSpace += MaxSize - Item.Size;
						Item.Size = MaxSize;
						Item.bFrozen = true;
					}
					else
					{
						// Within constraints, adjust.
						ConsumedSpace += SizeAdjust;
						Item.Size += SizeAdjust;
					}
				}
			}
			
			AvailableSpace -= ConsumedSpace;
		}
	}
	
	// Now that we have the satisfied size requirements we can
	// arrange widgets top-to-bottom or left-to-right (depending on the orientation).
	float PositionSoFar = 0.0f;

	for (TPanelChildrenConstIterator<SlotType> It(Children, Orientation, InLayoutFlow); It; ++It)
	{
		const SlotType& CurChild = *It;
		const EVisibility ChildVisibility = CurChild.GetWidget()->GetVisibility();

		// Figure out the area allocated to the child in the direction of BoxPanel
		// The area allocated to the slot is ChildSize + the associated margin.
		const float ChildSize = StretchItems[It.GetIndex()].Size;

		const FMargin SlotPadding(LayoutPaddingWithFlow(InLayoutFlow, CurChild.GetPadding()));

		FVector2f SlotSize = (Orientation == Orient_Vertical)
			? FVector2f(AllottedGeometry.GetLocalSize().X, ChildSize + SlotPadding.template GetTotalSpaceAlong<Orient_Vertical>())
			: FVector2f(ChildSize + SlotPadding.template GetTotalSpaceAlong<Orient_Horizontal>(), AllottedGeometry.GetLocalSize().Y);

		// Figure out the size and local position of the child within the slot			
		const AlignmentArrangeResult XAlignmentResult = AlignChild<Orient_Horizontal>(InLayoutFlow, SlotSize.X, CurChild, SlotPadding);
		const AlignmentArrangeResult YAlignmentResult = AlignChild<Orient_Vertical>(SlotSize.Y, CurChild, SlotPadding);

		const FVector2f LocalPosition = (Orientation == Orient_Vertical)
			? FVector2f(XAlignmentResult.Offset, PositionSoFar + YAlignmentResult.Offset + InOffset)
			: FVector2f(PositionSoFar + XAlignmentResult.Offset + InOffset, YAlignmentResult.Offset);

		const FVector2f LocalSize = FVector2f(XAlignmentResult.Size, YAlignmentResult.Size);

		// Add the information about this child to the output list (ArrangedChildren)
		ArrangedChildren.AddWidget(ChildVisibility, AllottedGeometry.MakeChild(
			// The child widget being arranged
			CurChild.GetWidget(),
			// Child's local position (i.e. position within parent)
			LocalPosition,
			// Child's size
			LocalSize
		));

		if (ChildVisibility != EVisibility::Collapsed)
		{
			// Offset the next child by the size of the current child and any post-child (bottom/right) margin
			PositionSoFar += (Orientation == Orient_Vertical) ? SlotSize.Y : SlotSize.X;
		}
	}
}

static FMargin LayoutPaddingWithFlow(EFlowDirection InLayoutFlow, const FMargin& InPadding)
{
	FMargin ReturnPadding(InPadding);
	if (InLayoutFlow == EFlowDirection::RightToLeft)
	{
		float Temp = ReturnPadding.Left;
		ReturnPadding.Left = ReturnPadding.Right;
		ReturnPadding.Right = Temp;
	}
	return ReturnPadding;
}

/**
* Given information about a popup and the space available for displaying that popup, compute best placement for it.
*
* @param InAnchor          Area relative to which popup is being created (e.g. the button part of a combo box)
* @param PopupRect         Proposed placement of popup; position may require adjustment.
* @param Orientation       Are we trying to show the popup above/below or left/right relative to the anchor?
* @param RectToFit         The space available for showing this popup; we want to fit entirely within it without clipping.
*
* @return A best position within the RectToFit such that none of the popup clips outside of the RectToFit.
*/
SLATECORE_API UE::Slate::FDeprecateVector2DResult ComputePopupFitInRect(const FSlateRect& InAnchor, const FSlateRect& PopupRect, const EOrientation& Orientation, const FSlateRect& RectToFit);
