// Copyright Epic Games, Inc. All Rights Reserved.
#include "Views/TableDashboardViewFactory.h"

#include "Algo/Transform.h"
#include "AudioInsightsTraceProviderBase.h"
#include "Containers/Array.h"
#include "Framework/Commands/UICommandList.h"
#include "Templates/SharedPointer.h"
#include "Widgets/SBoxPanel.h"

#if WITH_EDITOR
#include "AudioDeviceManager.h"
#include "AudioInsightsDashboardAssetCommands.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Subsystems/AssetEditorSubsystem.h"
#endif // WITH_EDITOR

#define LOCTEXT_NAMESPACE "AudioInsights"

namespace UE::Audio::Insights
{
	void FTraceTableDashboardViewFactory::SRowWidget::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, TSharedPtr<IDashboardDataViewEntry> InData, TSharedRef<FTraceTableDashboardViewFactory> InFactory)
	{
		Data = InData;
		Factory = InFactory;
		SMultiColumnTableRow<TSharedPtr<IDashboardDataViewEntry>>::Construct(FSuperRowType::FArguments(), InOwnerTable);
	}

	TSharedRef<SWidget> FTraceTableDashboardViewFactory::SRowWidget::GenerateWidgetForColumn(const FName& Column)
	{
		return Factory->GenerateWidgetForColumn(Data->AsShared(), Column);
	}

	TSharedRef<SWidget> FTraceTableDashboardViewFactory::GenerateWidgetForColumn(TSharedRef<IDashboardDataViewEntry> InRowData, const FName& Column)
	{
		const IDashboardDataViewEntry& RowData = InRowData.Get();
		const FColumnData& ColumnData = GetColumns()[Column];
		const FText Label = ColumnData.GetDisplayValue(RowData);
		if (Label.IsEmpty())
		{
			return SNullWidget::NullWidget;
		}

		return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().Padding(2)
		[
			SNew(STextBlock).Text(Label)
		];
	}

	FReply FTraceTableDashboardViewFactory::OnDataRowKeyInput(const FGeometry& Geometry, const FKeyEvent& KeyEvent) const
	{
		return FReply::Unhandled();
	}

	EColumnSortMode::Type FTraceTableDashboardViewFactory::GetColumnSortMode(const FName InColumnId) const
	{
		return (SortByColumn == InColumnId) ? SortMode : EColumnSortMode::None;
	}

	void FTraceTableDashboardViewFactory::RequestSort()
	{
		SortTable();

		if (FilteredEntriesListView.IsValid())
		{
			FilteredEntriesListView->RequestListRefresh();
		}
	}

	void FTraceTableDashboardViewFactory::OnColumnSortModeChanged(const EColumnSortPriority::Type InSortPriority, const FName& InColumnId, const EColumnSortMode::Type InSortMode)
	{
		SortByColumn = InColumnId;
		SortMode = InSortMode;
		RequestSort();
	}

	TSharedPtr<SWidget> FTraceTableDashboardViewFactory::OnConstructContextMenu()
	{
		return SNullWidget::NullWidget;
	}

	void FTraceTableDashboardViewFactory::OnSelectionChanged(TSharedPtr<IDashboardDataViewEntry> SelectedItem, ESelectInfo::Type SelectInfo)
	{
		// To be optionally implemented by derived classes
	}

	FSlateColor FTraceTableDashboardViewFactory::GetRowColor(const TSharedPtr<IDashboardDataViewEntry>& InRowDataPtr)
	{
		return FSlateColor(FColor(255, 255, 255));
	}

	TSharedRef<SHeaderRow> FTraceTableDashboardViewFactory::MakeHeaderRowWidget()
	{
		TArray<FName> DefaultHiddenColumns;
		Algo::TransformIf(GetColumns(), DefaultHiddenColumns,
			[](const TPair<FName, FColumnData>& ColumnInfo) { return ColumnInfo.Value.bDefaultHidden; },
			[](const TPair<FName, FColumnData>& ColumnInfo) { return ColumnInfo.Key; });

		TSharedRef<SHeaderRow> HeaderRowWidget = SNew(SHeaderRow)
		.CanSelectGeneratedColumn(true); // Allows for showing/hiding columns

		// This only works if header row columns are added with slots and not programmatically
		// check in SHeaderRow::Construct: for ( FColumn* const Column : InArgs.Slots ) for more info
		// A potential alternative would be to delegate to the derived classes the SHeaderRow creation with slots
		//.HiddenColumnsList(DefaultHiddenColumns);

		for (const auto& [ColumnName, ColumnData] : GetColumns())
		{
			const SHeaderRow::FColumn::FArguments ColumnArgs = SHeaderRow::Column(ColumnName)
				.DefaultLabel(ColumnData.DisplayName)
				.HAlignCell(ColumnData.Alignment)
				.FillWidth(ColumnData.FillWidth)
				.SortMode(this, &FTraceTableDashboardViewFactory::GetColumnSortMode, ColumnName)
				.OnSort(this, &FTraceTableDashboardViewFactory::OnColumnSortModeChanged);

			// .HiddenColumnsList workaround:
			// simulate what SHeaderRow::AddColumn( const FColumn::FArguments& NewColumnArgs ) does but allowing us to modify the bIsVisible property
			// Memory handling (delete) is done by TIndirectArray<FColumn> Columns; defined in SHeaderRow
			SHeaderRow::FColumn* NewColumn = new SHeaderRow::FColumn(ColumnArgs);
			NewColumn->bIsVisible = !DefaultHiddenColumns.Contains(ColumnName);
			HeaderRowWidget->AddColumn(*NewColumn);
		}

		return HeaderRowWidget;
	}

	TSharedRef<SWidget> FTraceTableDashboardViewFactory::MakeWidget()
	{
		if (!DashboardWidget.IsValid())
		{
			FilteredEntriesListView = SNew(SListView<TSharedPtr<IDashboardDataViewEntry>>)
			.ListItemsSource(&DataViewEntries)
			.OnContextMenuOpening(this, &FTraceTableDashboardViewFactory::OnConstructContextMenu)
			.OnSelectionChanged(this, &FTraceTableDashboardViewFactory::OnSelectionChanged)
			.OnGenerateRow_Lambda([this](TSharedPtr<IDashboardDataViewEntry> Item, const TSharedRef<STableViewBase>& OwnerTable)
			{
				return SNew(SRowWidget, OwnerTable, Item, AsShared());
			})
			.HeaderRow
			(
				MakeHeaderRowWidget()
			)
			.SelectionMode(ESelectionMode::Multi)
			.OnKeyDownHandler_Lambda([this](const FGeometry& Geometry, const FKeyEvent& KeyEvent)
			{
				return OnDataRowKeyInput(Geometry, KeyEvent);
			});

			DashboardWidget = SNew(SVerticalBox)
			.Clipping(EWidgetClipping::ClipToBounds)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SAssignNew(SearchBoxWidget, SSearchBox)
					.SelectAllTextWhenFocused(true)
					.HintText(LOCTEXT("TableDashboardView_SearchBoxHintText", "Filter"))
					.MinDesiredWidth(100)
					.OnTextChanged(this, &FTraceTableDashboardViewFactory::SetSearchBoxFilterText)
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(0, 2)
			[
				FilteredEntriesListView->AsShared()
			];
		}

		return DashboardWidget->AsShared();
	}

	void FTraceTableDashboardViewFactory::SetSearchBoxFilterText(const FText& NewText)
	{
		SearchBoxFilterText = NewText;
		UpdateFilterReason = EProcessReason::FilterUpdated;
	}


	void FTraceTableDashboardViewFactory::RebuildFilteredEntriesListView()
	{
		if (FilteredEntriesListView.IsValid())
		{
			FilteredEntriesListView->RebuildList();
		}
	}

	const FText& FTraceTableDashboardViewFactory::GetSearchFilterText() const
	{
		return SearchBoxFilterText;
	}

	void FTraceTableDashboardViewFactory::Tick(float InElapsed)
	{
		for (TSharedPtr<FTraceProviderBase> Provider : Providers)
		{
			const FName ProviderName = Provider->GetName();
			if (const uint64* CurrentUpdateId = UpdateIds.Find(ProviderName))
			{
				const uint64 LastUpdateId = Provider->GetLastUpdateId();
				if (*CurrentUpdateId != LastUpdateId)
				{
					UpdateFilterReason = EProcessReason::EntriesUpdated;
				}
			}
			else
			{
				UpdateFilterReason = EProcessReason::EntriesUpdated;
			}
		}

		if (UpdateFilterReason != EProcessReason::None)
		{
			ProcessEntries(UpdateFilterReason);
			if (UpdateFilterReason == EProcessReason::EntriesUpdated)
			{
				for (TSharedPtr<FTraceProviderBase> Provider : Providers)
				{
					const FName ProviderName = Provider->GetName();
					const uint64 LastUpdateId = Provider->GetLastUpdateId();
					UpdateIds.FindOrAdd(ProviderName) = LastUpdateId;
				}
			}

			RebuildFilteredEntriesListView();
			UpdateFilterReason = EProcessReason::None;
		}

#if WITH_EDITOR
		if (IsDebugDrawEnabled())
		{
			if (FilteredEntriesListView.IsValid())
			{
				TArray<TSharedPtr<IDashboardDataViewEntry>> SelectedItems = FilteredEntriesListView->GetSelectedItems();

				if (FAudioDeviceManager* AudioDeviceManager = FAudioDeviceManager::Get())
				{
					AudioDeviceManager->IterateOverAllDevices([this, &SelectedItems, InElapsed](::Audio::FDeviceId DeviceId, FAudioDevice* Device)
					{
						DebugDraw(InElapsed, SelectedItems, DeviceId);
					});
				}
			}
		}
#endif // WITH_EDITOR
	}

	FTraceTableDashboardViewFactory::FTraceTableDashboardViewFactory()
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("TraceTableDashboardViewFactory"), 0.0f, [this](float DeltaTime)
		{
			Tick(DeltaTime);
			return true;
		});
	}

	FTraceTableDashboardViewFactory::~FTraceTableDashboardViewFactory()
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	}

	FText FSoundAssetDashboardEntry::GetDisplayName() const
	{
		return FText::FromString(FSoftObjectPath(Name).GetAssetName());
	}

	const UObject* FSoundAssetDashboardEntry::GetObject() const
	{
		return FSoftObjectPath(Name).ResolveObject();
	}

	UObject* FSoundAssetDashboardEntry::GetObject()
	{
		return FSoftObjectPath(Name).ResolveObject();
	}

	bool FSoundAssetDashboardEntry::IsValid() const
	{
		return PlayOrder != INDEX_NONE;
	}

	TSharedRef<SWidget> FTraceObjectTableDashboardViewFactory::GenerateWidgetForColumn(TSharedRef<IDashboardDataViewEntry> InRowData, const FName& Column)
	{
		const IDashboardDataViewEntry& RowData = InRowData.Get();
		const FColumnData& ColumnData = GetColumns()[Column];
		const FText Label = ColumnData.GetDisplayValue(RowData);
		if (Label.IsEmpty())
		{
			return SNullWidget::NullWidget;
		}

		TSharedPtr<IDashboardDataViewEntry> RowDataPtr = InRowData;
		return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().Padding(2)
		[
			SNew(STextBlock).Text(Label)
			.ColorAndOpacity(GetRowColor(RowDataPtr))
			.OnDoubleClicked_Lambda([this, RowDataPtr](const FGeometry& MyGeometry, const FPointerEvent& PointerEvent)
			{
#if WITH_EDITOR
				if (GEditor)
				{
					TSharedPtr<IObjectDashboardEntry> ObjectData = StaticCastSharedPtr<IObjectDashboardEntry>(RowDataPtr);
					if (ObjectData.IsValid())
					{
						const UObject* Object = ObjectData->GetObject();
						if (Object && Object->IsAsset())
						{
							GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Object);
							return FReply::Handled();
						}
					}
				}
#endif // WITH_EDITOR
				return FReply::Unhandled();
			})
		];
	}

	FReply FTraceObjectTableDashboardViewFactory::OnDataRowKeyInput(const FGeometry& Geometry, const FKeyEvent& KeyEvent) const
	{
#if WITH_EDITOR
		if (GEditor && FilteredEntriesListView.IsValid())
		{
			if (KeyEvent.GetKey() == EKeys::Enter)
			{
				TArray<TSharedPtr<IDashboardDataViewEntry>> SelectedItems = FilteredEntriesListView->GetSelectedItems();

				for (const TSharedPtr<IDashboardDataViewEntry>& SelectedItem : SelectedItems)
				{
					if (SelectedItem.IsValid())
					{
						IObjectDashboardEntry& RowData = *StaticCastSharedPtr<IObjectDashboardEntry>(SelectedItem).Get();
						if (UObject* Object = RowData.GetObject())
						{
							if (Object && Object->IsAsset())
							{
								GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Object);
							}
						}
					}
				}

				return FReply::Handled();
			}
		}
#endif // WITH_EDITOR
		return FReply::Unhandled();
	}

	TSharedRef<SWidget> FTraceObjectTableDashboardViewFactory::MakeWidget()
	{
		if (!DashboardWidget.IsValid())
		{
			DashboardWidget = SNew(SVerticalBox)
#if WITH_EDITOR
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					MakeAssetMenuBar()
				]
			]
#endif // WITH_EDITOR
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			[
				FTraceTableDashboardViewFactory::MakeWidget()
			];

			if (FilteredEntriesListView.IsValid())
			{
				FilteredEntriesListView->SetSelectionMode(ESelectionMode::Multi);
			}
		}

		return DashboardWidget->AsShared();
	}

#if WITH_EDITOR
	TSharedRef<SWidget> FTraceObjectTableDashboardViewFactory::MakeAssetMenuBar() const
	{
		const FDashboardAssetCommands& Commands = FDashboardAssetCommands::Get();
		TSharedPtr<FUICommandList> ToolkitCommands = MakeShared<FUICommandList>();
		ToolkitCommands->MapAction(Commands.GetOpenCommand(), FExecuteAction::CreateLambda([this]() { OpenAsset(); }));
		ToolkitCommands->MapAction(Commands.GetBrowserSyncCommand(), FExecuteAction::CreateLambda([this]() { BrowseToAsset(); }));

		FToolBarBuilder ToolbarBuilder(ToolkitCommands, FMultiBoxCustomization::None);
		Commands.AddAssetCommands(ToolbarBuilder);

		return ToolbarBuilder.MakeWidget();
	}

	TArray<UObject*> FTraceObjectTableDashboardViewFactory::GetSelectedEditableAssets() const
	{
		TArray<UObject*> Objects;

		if (!FilteredEntriesListView.IsValid())
		{
			return Objects;
		}

		const TArray<TSharedPtr<IDashboardDataViewEntry>> Items = FilteredEntriesListView->GetSelectedItems();
		Algo::TransformIf(Items, Objects,
			[](const TSharedPtr<IDashboardDataViewEntry>& Item)
			{
				if (Item.IsValid())
				{
					IObjectDashboardEntry& RowData = *StaticCastSharedPtr<IObjectDashboardEntry>(Item).Get();
					if (UObject* Object = RowData.GetObject())
					{
						return Object && Object->IsAsset();
					}
				}

				return false;
			},
			[](const TSharedPtr<IDashboardDataViewEntry>& Item)
			{
				IObjectDashboardEntry& RowData = *StaticCastSharedPtr<IObjectDashboardEntry>(Item).Get();
				return RowData.GetObject();
			}
		);

		return Objects;
	}

	bool FTraceObjectTableDashboardViewFactory::OpenAsset() const
	{
		if (GEditor && FilteredEntriesListView.IsValid())
		{
			TArray<UObject*> Objects = GetSelectedEditableAssets();
			if (UAssetEditorSubsystem* AssetSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				return AssetSubsystem->OpenEditorForAssets(Objects);
			}
		}

		return false;
	}

	bool FTraceObjectTableDashboardViewFactory::BrowseToAsset() const
	{
		if (GEditor)
		{
			TArray<UObject*> EditableAssets = GetSelectedEditableAssets();
			GEditor->SyncBrowserToObjects(EditableAssets);
			return true;
		}

		return false;
	}
#endif // WITH_EDITOR
} // namespace UE::Audio::Insights

#undef LOCTEXT_NAMESPACE
