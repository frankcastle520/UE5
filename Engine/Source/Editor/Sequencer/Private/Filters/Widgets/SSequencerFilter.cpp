// Copyright Epic Games, Inc. All Rights Reserved.

#include "Filters/Widgets/SSequencerFilter.h"
#include "Filters/Menus/SequencerTrackFilterContextMenu.h"
#include "Filters/SequencerFilterBar.h"
#include "Filters/Widgets/SSequencerFilterCheckBox.h"

#define LOCTEXT_NAMESPACE "SSequencerFilter"

void SSequencerFilter::Construct(const FArguments& InArgs
	, const TSharedRef<FSequencerFilterBar>& InFilterBar
	, const TSharedRef<FSequencerTrackFilter>& InFilter)
{
	WeakFilterBar = InFilterBar;
	WeakFilter = InFilter;

	ContextMenu = MakeShared<FSequencerTrackFilterContextMenu>();

	TSharedPtr<SWidget> ContentWidget;
	FName BrushName;

	switch(InArgs._FilterPillStyle)
	{
	case EFilterPillStyle::Basic:
		{
			ContentWidget = ConstructBasicFilterWidget();
			BrushName = TEXT("FilterBar.BasicFilterButton");
			break;
		}
	case EFilterPillStyle::Default:
	default:
		{
			ContentWidget = ConstructDefaultFilterWidget();
			BrushName = TEXT("FilterBar.FilterButton");
			break;
		}
	}

	ChildSlot
	[
		SAssignNew(ToggleButtonPtr, SSequencerFilterCheckBox)
		.Style(FAppStyle::Get(), BrushName)
		.ToolTipText(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(InFilter, &FSequencerTrackFilter::GetToolTipText)))
		.IsChecked(this, &SSequencerFilter::IsChecked)
		.OnCheckStateChanged(this, &SSequencerFilter::OnFilterToggled)
		.CheckBoxContentUsesAutoWidth(false)
		.OnGetMenuContent(this, &SSequencerFilter::GetRightClickMenuContent)
		[
			ContentWidget.ToSharedRef()
		]
	];

	ToggleButtonPtr->SetOnCtrlClick(FOnClicked::CreateSP(this, &SSequencerFilter::OnFilterCtrlClick));
	ToggleButtonPtr->SetOnAltClick(FOnClicked::CreateSP(this, &SSequencerFilter::OnFilterAltClick));
	ToggleButtonPtr->SetOnMiddleButtonClick(FOnClicked::CreateSP(this, &SSequencerFilter::OnFilterMiddleButtonClick));
	ToggleButtonPtr->SetOnDoubleClick(FOnClicked::CreateSP(this, &SSequencerFilter::OnFilterDoubleClick));
}

TSharedRef<SWidget> SSequencerFilter::ConstructBasicFilterWidget()
{
	return SNew(STextBlock)
		.Margin(0.f)
		.TextStyle(FAppStyle::Get(), TEXT("SmallText"))
		.Text(this, &SSequencerFilter::GetFilterDisplayName);
}

TSharedRef<SWidget> SSequencerFilter::ConstructDefaultFilterWidget()
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	const TSharedPtr<FSequencerTrackFilter> Filter = WeakFilter.Pin();
	if (!Filter.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	return SNew(SBorder)
		.Padding(1.f)
		.BorderImage(FAppStyle::Get().GetBrush(TEXT("FilterBar.FilterBackground")))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SImage)
				.DesiredSizeOverride(FVector2D(8, 16)) // was 22
				.Image(FAppStyle::Get().GetBrush(TEXT("FilterBar.FilterImage")))
				.ColorAndOpacity(this, &SSequencerFilter::GetFilterImageColorAndOpacity)
			]
			+ SHorizontalBox::Slot()
			.Padding(TAttribute<FMargin>(this, &SSequencerFilter::GetFilterNamePadding))
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8))
				.Text(this, &SSequencerFilter::GetFilterDisplayName)
				.IsEnabled(this, &SSequencerFilter::IsButtonEnabled)
			]
		];
}

const TSharedPtr<FSequencerTrackFilter> SSequencerFilter::GetFilter() const
{
	return WeakFilter.IsValid() ? WeakFilter.Pin() : nullptr;
}

bool SSequencerFilter::IsActive() const
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return false;
	}

	const TSharedPtr<FSequencerTrackFilter> Filter = WeakFilter.Pin();
	if (!Filter.IsValid())
	{
		return false;
	}

	return FilterBar->IsFilterActive(Filter.ToSharedRef());
}

void SSequencerFilter::OnFilterToggled(const ECheckBoxState NewState)
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return;
	}

	const TSharedPtr<FSequencerTrackFilter> Filter = WeakFilter.Pin();
	if (!Filter.IsValid())
	{
		return;
	}

	const bool bNewActive = NewState == ECheckBoxState::Checked;
	FilterBar->SetFilterActive(Filter.ToSharedRef(), bNewActive, true);
}

FReply SSequencerFilter::OnFilterCtrlClick()
{
	ActivateAllButThis(false);

	return FReply::Handled();
}

FReply SSequencerFilter::OnFilterAltClick()
{
	ActivateAllButThis(true);

	return FReply::Handled();
}

FReply SSequencerFilter::OnFilterMiddleButtonClick()
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return FReply::Handled();
	}

	const TSharedPtr<FSequencerTrackFilter> Filter = WeakFilter.Pin();
	if (!Filter.IsValid())
	{
		return FReply::Handled();
	}

	FilterBar->SetFilterEnabled(Filter.ToSharedRef(), false, true);

	return FReply::Handled();
}

FReply SSequencerFilter::OnFilterDoubleClick()
{
	ActivateAllButThis(false);

	return FReply::Handled();
}

TSharedRef<SWidget> SSequencerFilter::GetRightClickMenuContent()
{
	return ContextMenu->CreateMenuWidget(SharedThis(this));
}

ECheckBoxState SSequencerFilter::IsChecked() const
{
	return IsActive() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

FSlateColor SSequencerFilter::GetFilterImageColorAndOpacity() const
{
	const TSharedPtr<FSequencerTrackFilter> Filter = WeakFilter.Pin();
	if (!Filter.IsValid() || !IsActive())
	{
		return FAppStyle::Get().GetSlateColor(TEXT("Colors.Recessed"));;
	}
	return Filter->GetColor();
}

EVisibility SSequencerFilter::GetFilterOverlayVisibility() const
{
	return IsActive() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
}

FMargin SSequencerFilter::GetFilterNamePadding() const
{
	return ToggleButtonPtr->IsPressed() ? FMargin(3, 1, 3, 0) : FMargin(3, 0, 3, 0);
}

FText SSequencerFilter::GetFilterDisplayName() const
{
	return WeakFilter.IsValid() ? WeakFilter.Pin()->GetDisplayName() : FText::GetEmpty();
}

bool SSequencerFilter::IsButtonEnabled() const
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return false;
	}

	const TSharedPtr<FSequencerTrackFilter> Filter = WeakFilter.Pin();
	if (!Filter.IsValid())
	{
		return false;
	}

	return FilterBar->IsFilterActive(Filter.ToSharedRef());
}

void SSequencerFilter::ActivateAllButThis(const bool bInActive)
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return;
	}

	const TSharedPtr<FSequencerTrackFilter> Filter = WeakFilter.Pin();
	if (!Filter.IsValid())
	{
		return;
	}

	FilterBar->ActivateAllEnabledFilters(bInActive, {});
	FilterBar->SetFilterActive(Filter.ToSharedRef(), !bInActive, true);
}

#undef LOCTEXT_NAMESPACE
