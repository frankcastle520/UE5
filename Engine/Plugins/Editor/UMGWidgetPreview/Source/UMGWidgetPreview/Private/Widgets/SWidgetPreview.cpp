// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWidgetPreview.h"

#include "Blueprint/UserWidget.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "WidgetPreview.h"
#include "Slate/SRetainerWidget.h"
#include "WidgetPreviewToolkit.h"
#include "Widgets/Layout/SBorder.h"

#define LOCTEXT_NAMESPACE "SWidgetPreview"

namespace UE::UMGWidgetPreview::Private
{
	void SWidgetPreview::Construct(const FArguments& Args, const TSharedRef<FWidgetPreviewToolkit>& InToolkit)
	{
		WeakToolkit = InToolkit;

		OnStateChangedHandle = InToolkit->OnStateChanged().AddSP(this, &SWidgetPreview::OnStateChanged);
		OnWidgetChangedHandle = InToolkit->GetPreview()->OnWidgetChanged().AddSP(this, &SWidgetPreview::OnWidgetChanged);

		CreatedSlateWidget = SNullWidget::NullWidget;

		ContainerWidget = SNew(SBorder)
		[
			GetCreatedSlateWidget()
		];

		OnWidgetChanged(EWidgetPreviewWidgetChangeType::Assignment);

		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SAssignNew(RetainerWidget, SRetainerWidget)
				.RenderOnPhase(false)
				.RenderOnInvalidation(false)
				[
					ContainerWidget.ToSharedRef()
				]
			]
		];
	}

	SWidgetPreview::~SWidgetPreview()
	{
		ContainerWidget->ClearContent();

		if (const TSharedPtr<FWidgetPreviewToolkit> Toolkit = WeakToolkit.Pin())
		{
			Toolkit->OnStateChanged().Remove(OnStateChangedHandle);

			if (UWidgetPreview* Preview = Toolkit->GetPreview())
			{
				Preview->OnWidgetChanged().Remove(OnWidgetChangedHandle);
			}
		}
	}

	int32 SWidgetPreview::OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const
	{
		const int32 Result = SCompoundWidget::OnPaint(
			Args,
			AllottedGeometry,
			MyCullingRect,
			OutDrawElements,
			LayerId,
			InWidgetStyle,
			bParentEnabled);

		if (bClearWidgetOnNextPaint)
		{
			SWidgetPreview* MutableThis = const_cast<SWidgetPreview*>(this);

			MutableThis->CreatedSlateWidget = SNullWidget::NullWidget;
			ContainerWidget->SetContent(GetCreatedSlateWidget());
			MutableThis->bClearWidgetOnNextPaint = false;
		}

		return Result;
	}

	void SWidgetPreview::OnStateChanged(FWidgetPreviewToolkitStateBase* InOldState, FWidgetPreviewToolkitStateBase* InNewState)
	{
		const bool bShouldUseLiveWidget = InNewState->CanTick();
		bIsRetainedRender = !bShouldUseLiveWidget;
		bClearWidgetOnNextPaint = bIsRetainedRender;
		RetainerWidget->RequestRender();
		RetainerWidget->SetRetainedRendering(bIsRetainedRender);

		if (bShouldUseLiveWidget)
		{
			OnWidgetChanged(EWidgetPreviewWidgetChangeType::Assignment);
		}
	}

	void SWidgetPreview::OnWidgetChanged(const EWidgetPreviewWidgetChangeType InChangeType)
	{
		// Disallow widget assignment if retaining (cached thumbnail)
		if (bIsRetainedRender)
		{
			return;
		}

		if (InChangeType != EWidgetPreviewWidgetChangeType::Destroyed)
		{
			if (const TSharedPtr<FWidgetPreviewToolkit> Toolkit = WeakToolkit.Pin())
			{
				if (UWidgetPreview* Preview = Toolkit->GetPreview())
				{
					UWorld* World = GetWorld();
					if (const TSharedPtr<SWidget> PreviewSlateWidget = Preview->GetSlateWidgetInstance())
					{
						CreatedSlateWidget = PreviewSlateWidget;
					}
					else if (UUserWidget* PreviewWidget = Preview->GetOrCreateWidgetInstance(World))
					{
						CreatedSlateWidget = PreviewWidget->TakeWidget();
					}
					else
					{
						CreatedSlateWidget = SNullWidget::NullWidget;
					}

					ContainerWidget->SetContent(GetCreatedSlateWidget());
				}
			}
		}
	}

	UWorld* SWidgetPreview::GetWorld() const
	{
		if (const TSharedPtr<FWidgetPreviewToolkit> Toolkit = WeakToolkit.Pin())
		{
			return Toolkit->GetPreviewWorld();
		}

		return nullptr;
	}

	TSharedRef<SWidget> SWidgetPreview::GetCreatedSlateWidget() const
	{
		if (TSharedPtr<SWidget> SlateWidget = CreatedSlateWidget.Pin())
		{
			return SlateWidget.ToSharedRef();
		}

		return SNullWidget::NullWidget;
	}
}

#undef LOCTEXT_NAMESPACE
