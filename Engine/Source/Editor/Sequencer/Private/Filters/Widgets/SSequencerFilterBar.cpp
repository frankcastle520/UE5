// Copyright Epic Games, Inc. All Rights Reserved.

#include "Filters/Widgets/SSequencerFilterBar.h"
#include "Filters/Filters/SequencerTrackFilter_CustomText.h"
#include "Filters/Filters/SequencerTrackFilter_Text.h"
#include "Filters/Menus/SequencerFilterBarContextMenu.h"
#include "Filters/SequencerFilterBar.h"
#include "Filters/Widgets/SFilterBarClippingHorizontalBox.h"
#include "Filters/Widgets/SFilterExpressionHelpDialog.h"
#include "Interfaces/IMainFrameModule.h"
#include "MovieScene.h"
#include "Sequencer.h"
#include "SequencerLog.h"
#include "SSequencerFilter.h"
#include "SSequencerCustomTextFilterDialog.h"
#include "Widgets/SWindow.h"

using namespace UE::Sequencer;

#define LOCTEXT_NAMESPACE "SSequencerFilterBar"

SSequencerFilterBar::~SSequencerFilterBar()
{
	if (const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin())
	{
		FilterBar->GetOnFiltersChanged().RemoveAll(this);
	}

	ContextMenu.Reset();

	if (SSequencerCustomTextFilterDialog::IsOpen())
	{
		SSequencerCustomTextFilterDialog::CloseWindow();
	}

	if (TextExpressionHelpDialog.IsValid())
	{
		TextExpressionHelpDialog->RequestDestroyWindow();
		TextExpressionHelpDialog.Reset();
	}
}

void SSequencerFilterBar::Construct(const FArguments& InArgs, const TSharedRef<FSequencerFilterBar>& InFilterBar)
{
	WeakFilterBar = InFilterBar;

	WeakSearchBox = InArgs._FilterSearchBox;
	FilterBarLayout = InArgs._FilterBarLayout;
	bCanChangeOrientation = InArgs._CanChangeOrientation;
	FilterPillStyle = InArgs._FilterPillStyle;

	HorizontalContainerWidget = SNew(SFilterBarClippingHorizontalBox)
		.OnWrapButtonClicked(FOnGetContent::CreateSP(this, &SSequencerFilterBar::OnWrapButtonClicked))
		.IsFocusable(false);

	ChildSlot
	[
		SAssignNew(FilterBoxWidget, SWidgetSwitcher)
		.WidgetIndex_Lambda([this]()
			{
				return (FilterBarLayout == EFilterBarLayout::Horizontal) ? 0 : 1;
			})
		+ SWidgetSwitcher::Slot()
		.Padding(FMargin(0.f, 2.f, 0.f, 0.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			[
				HorizontalContainerWidget.ToSharedRef()
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				HorizontalContainerWidget->CreateWrapButton()
			]
		]
		+ SWidgetSwitcher::Slot()
		[
			SAssignNew(VerticalContainerWidget, SScrollBox)
			.Visibility_Lambda([this]()
				{
				   return HasAnyFilterWidgets() ? EVisibility::Visible : EVisibility::Collapsed;
				})
		]
	];

	AttachFilterSearchBox(InArgs._FilterSearchBox);

	ContextMenu = MakeShared<FSequencerFilterBarContextMenu>();

	CreateFilterWidgetsFromConfig();

	InFilterBar->GetOnFiltersChanged().AddSP(this, &SSequencerFilterBar::OnFiltersChanged);
}

FReply SSequencerFilterBar::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return FReply::Unhandled();
	}

	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		const FWidgetPath* EventPath = MouseEvent.GetEventPath();

		FSlateApplication::Get().PushMenu(AsShared()
			, EventPath ? *EventPath : FWidgetPath()
			, ContextMenu->CreateMenu(FilterBar.ToSharedRef())
			, MouseEvent.GetScreenSpacePosition()
			, FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));

		return FReply::Handled().ReleaseMouseCapture();
	}

	return FReply::Unhandled();
}

TSharedPtr<FSequencerFilterBar> SSequencerFilterBar::GetFilterBar() const
{
	return WeakFilterBar.Pin();
}

void SSequencerFilterBar::SetTextFilterString(const FString& InText)
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return;
	}

	if (const TSharedPtr<SFilterSearchBox> SearchBox = WeakSearchBox.Pin())
	{
		if (!InText.Equals(SearchBox->GetText().ToString()))
		{
			SearchBox->SetText(FText::FromString(InText));
		}

		SearchBox->SetError(FilterBar->GetFilterErrorText());
	}
}

FText SSequencerFilterBar::GetFilterErrorText() const
{
	return WeakFilterBar.IsValid() ? WeakFilterBar.Pin()->GetFilterErrorText() : FText::GetEmpty();
}

EFilterBarLayout SSequencerFilterBar::GetLayout() const
{
	return FilterBarLayout;
}

void SSequencerFilterBar::SetLayout(const EFilterBarLayout InFilterBarLayout)
{
	if (!bCanChangeOrientation)
	{
		return;
	}

	FilterBarLayout = InFilterBarLayout;

	HorizontalContainerWidget->ClearChildren();
	VerticalContainerWidget->ClearChildren();

	for (const TSharedRef<SSequencerFilter>& FilterWidget : FilterWidgets)
	{
		AddWidgetToLayout(FilterWidget);
	}
}

void SSequencerFilterBar::AttachFilterSearchBox(const TSharedPtr<SFilterSearchBox>& InFilterSearchBox)
{
	if (InFilterSearchBox)
	{
		WeakSearchBox = InFilterSearchBox;

		InFilterSearchBox->SetOnSaveSearchHandler(
			 SFilterSearchBox::FOnSaveSearchClicked::CreateSP(this, &SSequencerFilterBar::CreateAddCustomTextFilterWindowFromSearch));
	}
}

bool SSequencerFilterBar::HasAnyFilterWidgets() const
{
	return FilterWidgets.Num() > 0;
}

void SSequencerFilterBar::AddWidgetToLayout(const TSharedRef<SWidget>& InWidget)
{
	FMargin SlotPadding = FMargin(1); // default editor-wide is FMargin(4, 2) for vertical only

	if (FilterBarLayout == EFilterBarLayout::Horizontal)
	{
		switch (FilterPillStyle)
		{
		case EFilterPillStyle::Basic:
			SlotPadding = FMargin(1); // default editor-wide is 2
			break;
		case EFilterPillStyle::Default:
		default:
			SlotPadding = FMargin(1); // default editor-wide is 3
		}

		HorizontalContainerWidget->AddSlot()
			.AutoWidth()
			.Padding(SlotPadding)
			[
				InWidget
			];
	}
	else
	{
		VerticalContainerWidget->AddSlot()
			.AutoSize()
			.Padding(SlotPadding)
			[
				InWidget
			];
	}
}

void SSequencerFilterBar::RemoveWidgetFromLayout(const TSharedRef<SWidget>& InWidget)
{
	if (FilterBarLayout == EFilterBarLayout::Horizontal)
	{
		HorizontalContainerWidget->RemoveSlot(InWidget);
	}
	else
	{
		VerticalContainerWidget->RemoveSlot(InWidget);
	}
}

TSharedPtr<SSequencerFilter> SSequencerFilterBar::FindFilterWidget(const TSharedRef<FSequencerTrackFilter>& InFilter) const
{
	for (const TSharedRef<SSequencerFilter>& FilterWidget : FilterWidgets)
	{
		if (FilterWidget->GetFilter() == InFilter)
		{
			return FilterWidget;
		}
	}
	return nullptr;
}

void SSequencerFilterBar::CreateAndAddFilterWidget(const TSharedRef<FSequencerTrackFilter>& InFilter)
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return;
	}

	const TSharedRef<SSequencerFilter> NewFilter = SNew(SSequencerFilter, FilterBar.ToSharedRef(), InFilter)
		.FilterPillStyle(FilterPillStyle);

	AddFilterWidget(NewFilter);
}

void SSequencerFilterBar::AddFilterWidget(const TSharedRef<SSequencerFilter>& InFilterWidget)
{
	FilterWidgets.Add(InFilterWidget);

	AddWidgetToLayout(InFilterWidget);
}

void SSequencerFilterBar::RemoveFilterWidget(const TSharedRef<FSequencerTrackFilter>& InFilter, bool ExecuteOnFilterChanged)
{
	TSharedPtr<SSequencerFilter> FilterToRemove;
	for (const TSharedRef<SSequencerFilter>& Filter : FilterWidgets)
	{
		if (Filter->GetFilter() == InFilter)
		{
			FilterToRemove = Filter;
			break;
		}
	}

	if (FilterToRemove.IsValid())
	{
		if (ExecuteOnFilterChanged)
		{
			RemoveFilterWidgetAndUpdate(FilterToRemove.ToSharedRef());
		}
		else
		{
			RemoveFilterWidget(FilterToRemove.ToSharedRef());
		}
	}
}

void SSequencerFilterBar::RemoveFilterWidget(const TSharedRef<SSequencerFilter>& InFilterWidget)
{
	FilterWidgets.Remove(InFilterWidget);

	RemoveWidgetFromLayout(InFilterWidget);
}

void SSequencerFilterBar::RemoveAllFilterWidgets()
{
	for (const TSharedRef<SSequencerFilter>& FilterWidget : FilterWidgets)
	{
		RemoveWidgetFromLayout(FilterWidget);
	}

	FilterWidgets.Empty();
}

void SSequencerFilterBar::RemoveAllFilterWidgetsButThis(const TSharedRef<SSequencerFilter>& InFilterWidget)
{
	for (const TSharedRef<SSequencerFilter>& FilterWidget : FilterWidgets)
	{
		if (FilterWidget == InFilterWidget)
		{
			continue;
		}

		RemoveWidgetFromLayout(FilterWidget);
	}

	FilterWidgets.Empty();

	AddFilterWidget(InFilterWidget);
}

void SSequencerFilterBar::RemoveFilterWidgetAndUpdate(const TSharedRef<SSequencerFilter>& InFilterWidget)
{
	RemoveFilterWidget(InFilterWidget);
}

void SSequencerFilterBar::OnEnableAllGroupFilters(bool bEnableAll)
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return;
	}

	UMovieSceneSequence* const FocusedMovieSequence = FilterBar->GetSequencer().GetFocusedMovieSceneSequence();
	if (!IsValid(FocusedMovieSequence))
	{
		return;
	}
	
	UMovieScene* const FocusedMovieScene = FocusedMovieSequence->GetMovieScene();
	if (!IsValid(FocusedMovieScene))
	{
		return;
	}
	
	for (UMovieSceneNodeGroup* const NodeGroup : FocusedMovieScene->GetNodeGroups())
	{
		NodeGroup->SetEnableFilter(bEnableAll);
	}
}

void SSequencerFilterBar::OnNodeGroupFilterClicked(UMovieSceneNodeGroup* NodeGroup)
{
	if (NodeGroup)
	{
		NodeGroup->SetEnableFilter(!NodeGroup->GetEnableFilter());
	}
}

UWorld* SSequencerFilterBar::GetWorld() const
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return nullptr;
	}

	UObject* const PlaybackContext = FilterBar->GetSequencer().GetPlaybackContext();
	if (!IsValid(PlaybackContext))
	{
		return nullptr;
	}

	return PlaybackContext->GetWorld();
}

TWeakPtr<SFilterSearchBox> SSequencerFilterBar::GetSearchBox() const
{
	return WeakSearchBox;
}

void SSequencerFilterBar::SetMuted(bool bInMuted)
{
	if (HorizontalContainerWidget.IsValid())
	{
		HorizontalContainerWidget->SetEnabled(!bInMuted);
	}

	if (VerticalContainerWidget.IsValid())
	{
		VerticalContainerWidget->SetEnabled(!bInMuted);
	}

	if (WeakSearchBox.IsValid())
	{
		WeakSearchBox.Pin()->SetEnabled(!bInMuted);
	}
}

void SSequencerFilterBar::OnFiltersChanged(const ESequencerFilterChange InChangeType, const TSharedRef<FSequencerTrackFilter>& InFilter)
{
	switch (InChangeType)
	{
	case ESequencerFilterChange::Enable:
	case ESequencerFilterChange::Activate:
		{
			const TSharedPtr<SSequencerFilter> FilterWidget = FindFilterWidget(InFilter);
			if (!FilterWidget.IsValid())
			{
				CreateAndAddFilterWidget(InFilter);
			}
			break;
		}
	case ESequencerFilterChange::Disable:
		{
			RemoveFilterWidget(InFilter);
			break;
		}
	case ESequencerFilterChange::Deactivate:
		{
			break;
		}
	};
}

void SSequencerFilterBar::CreateAddCustomTextFilterWindowFromSearch(const FText& InSearchText)
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return;
	}

	FCustomTextFilterData CustomTextFilterData;
	CustomTextFilterData.FilterLabel = LOCTEXT("NewFilterName", "New Filter Name");
	CustomTextFilterData.FilterString = InSearchText;

	SSequencerCustomTextFilterDialog::CreateWindow_AddCustomTextFilter(FilterBar.ToSharedRef(), MoveTemp(CustomTextFilterData));
}

void SSequencerFilterBar::OnOpenTextExpressionHelp()
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return;
	}

	if (TextExpressionHelpDialog.IsValid())
	{
		TextExpressionHelpDialog->BringToFront();
	}
	else
	{
		TextExpressionHelpDialog = SNew(SFilterExpressionHelpDialog)
			.DialogTitle(LOCTEXT("SequencerCustomTextFilterHelp", "Sequencer Custom Text Filter Help"))
			.TextFilterExpressionContexts(FilterBar->GetTextFilter()->GetTextFilterExpressionContexts());

		TextExpressionHelpDialog->GetOnWindowClosedEvent().AddLambda([this](const TSharedRef<SWindow>& InWindow)
			{
				TextExpressionHelpDialog.Reset();
			});

		TSharedPtr<SWindow> ParentWindow;
		if (FModuleManager::Get().IsModuleLoaded(TEXT("MainFrame")))
		{
			IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
			ParentWindow = MainFrame.GetParentWindow();
		}

		if (ParentWindow.IsValid())
		{
			FSlateApplication::Get().AddWindowAsNativeChild(TextExpressionHelpDialog.ToSharedRef(), ParentWindow.ToSharedRef());
		}
		else
		{
			FSlateApplication::Get().AddWindow(TextExpressionHelpDialog.ToSharedRef());
		}
	}
}
void SSequencerFilterBar::SaveCurrentFilterSetAsCustomTextFilter()
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return;
	}

	FCustomTextFilterData CustomTextFilterData;
	CustomTextFilterData.FilterString = FText::FromString(FilterBar->GenerateTextFilterStringFromEnabledFilters());
	if (CustomTextFilterData.FilterLabel.IsEmpty())
	{
		CustomTextFilterData.FilterLabel = LOCTEXT("NewFilterName", "New Filter Name");
	}

	SSequencerCustomTextFilterDialog::CreateWindow_AddCustomTextFilter(FilterBar.ToSharedRef(), MoveTemp(CustomTextFilterData));
}

void SSequencerFilterBar::CreateFilterWidgetsFromConfig()
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return;
	}

	USequencerSettings* const SequencerSettings = FilterBar->GetSequencer().GetSequencerSettings();
	check(IsValid(SequencerSettings));

	const FName InstanceIdentifier = FilterBar->GetIdentifier();
	FSequencerFilterBarConfig* const Config = SequencerSettings->FindTrackFilterBar(InstanceIdentifier);
	if (!Config)
	{
		UE_LOG(LogSequencer, Error, TEXT("SSequencerFilterBar requires that you specify a FilterBarIdentifier to load settings"));
		return;
	}

	RemoveAllFilterWidgets();

	const TSet<TSharedRef<FFilterCategory>> DisplayableCategories = FilterBar->GetConfigCategories();

	auto LoadFilterFromConfig = [this, &Config, &DisplayableCategories](const TSharedRef<FSequencerTrackFilter>& InFilter)
	{
		const TSharedPtr<FFilterCategory> FilterCategory = InFilter->GetCategory();
		if (FilterCategory.IsValid() && !DisplayableCategories.Contains(FilterCategory.ToSharedRef()))
		{
			return;
		}

		const FString FilterName = InFilter->GetDisplayName().ToString();
		if (!Config->IsFilterEnabled(FilterName))
		{
			return;
		}

		const TSharedPtr<SSequencerFilter> FilterWidget = FindFilterWidget(InFilter);
		if (!FilterWidget.IsValid())
		{
			CreateAndAddFilterWidget(InFilter);
		}
	};

	for (const TSharedRef<FSequencerTrackFilter>& Filter : FilterBar->GetCommonFilters())
	{
		LoadFilterFromConfig(Filter);
	}

	for (const TSharedRef<FSequencerTrackFilter_CustomText>& Filter : FilterBar->GetAllCustomTextFilters())
	{
		LoadFilterFromConfig(Filter);
	}
}

TSharedRef<SWidget> SSequencerFilterBar::OnWrapButtonClicked()
{
	const TSharedRef<SVerticalBox> VerticalContainer = SNew(SVerticalBox);

	const int32 NumSlots = HorizontalContainerWidget->NumSlots();
	int32 SlotIndex = HorizontalContainerWidget->GetClippedIndex();
	for (; SlotIndex < NumSlots; ++SlotIndex)
	{
		SHorizontalBox::FSlot& Slot = HorizontalContainerWidget->GetSlot(SlotIndex);

		VerticalContainer->AddSlot()
			.AutoHeight()
			.Padding(1.f)
			[
				Slot.GetWidget()
			];
	}

	const TSharedRef<SBorder> ContainerBorder = SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Panel")))
		.Padding(0.f, 2.f, 2.f, 2.f)
		[
			VerticalContainer
		];

	return SNew(SBox)
		.Padding(8.f)
		[
			HorizontalContainerWidget->WrapVerticalListWithHeading(ContainerBorder
				, FPointerEventHandler::CreateSP(this, &SSequencerFilterBar::OnMouseButtonUp))
		];
}

#undef LOCTEXT_NAMESPACE
