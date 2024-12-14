// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/StrongObjectPtr.h"
#include "UObject/ObjectPtr.h"
#include "WidgetPreview.h"
#include "Widgets/SCompoundWidget.h"

class UWidget;
class UUserWidget;
class SBorder;
class SRetainerWidget;

namespace UE::UMGWidgetPreview::Private
{
	struct FWidgetPreviewToolkitStateBase;
	class FWidgetPreviewToolkit;

	class SWidgetPreview
		: public SCompoundWidget
	{
	public:
		using FSlotWidgetMap = TMap<FName, TObjectPtr<UWidget>>;

		SLATE_BEGIN_ARGS(SWidgetPreview) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& Args, const TSharedRef<FWidgetPreviewToolkit>& InToolkit);

		virtual ~SWidgetPreview() override;

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	private:
		void OnStateChanged(FWidgetPreviewToolkitStateBase* InOldState, FWidgetPreviewToolkitStateBase* InNewState);
		void OnWidgetChanged(const EWidgetPreviewWidgetChangeType InChangeType);

		/** Convenience method to get world from the associated viewport. */
		UWorld* GetWorld() const;

		TSharedRef<SWidget> GetCreatedSlateWidget() const;

	private:
		TWeakPtr<FWidgetPreviewToolkit> WeakToolkit;

		TSharedPtr<SRetainerWidget> RetainerWidget;
		TSharedPtr<SBorder> ContainerWidget;
		TWeakPtr<SWidget> CreatedSlateWidget;

		bool bClearWidgetOnNextPaint = false;
		bool bIsRetainedRender = false;

		FDelegateHandle OnStateChangedHandle;
		FDelegateHandle OnWidgetChangedHandle;
	};
}
