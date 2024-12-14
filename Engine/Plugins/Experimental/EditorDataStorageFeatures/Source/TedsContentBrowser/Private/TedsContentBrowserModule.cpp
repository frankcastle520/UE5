// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsContentBrowserModule.h"

#include "Framework/Application/SlateApplication.h"
#include "ContentBrowserModule.h"
#include "Columns/SlateDelegateColumns.h"
#include "Elements/Common/TypedElementQueryTypes.h"
#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Framework/TypedElementIndexHasher.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Experimental/ContentBrowserViewExtender.h"
#include "Modules/ModuleManager.h"
#include "QueryStack/FQueryStackNode_RowView.h"
#include "TedsAssetDataColumns.h"
#include "Elements/Columns/TypedElementAlertColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Widgets/STedsTableViewer.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "TedsContentBrowserModule"

namespace UE::Editor::ContentBrowser
{
	using namespace DataStorage;

	static bool bEnableTedsContentBrowser = false;
	
	static FAutoConsoleVariableRef CVarUseTEDSOutliner(
		TEXT("TEDS.UI.EnableTedsContentBrowser"),
		bEnableTedsContentBrowser,
		TEXT("Add the Teds Content Browser as a custom view (requires re-opening any currently open content browsers)")
		, FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* /*CVar*/)
		{
			FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().GetModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
			FTedsContentBrowserModule& TedsContentBrowserModule = FModuleManager::Get().GetModuleChecked<FTedsContentBrowserModule>(TEXT("TedsContentBrowser"));
			
			if(bEnableTedsContentBrowser)
			{
				ContentBrowserModule.SetContentBrowserViewExtender(
					FContentBrowserModule::FCreateViewExtender::CreateStatic(&FTedsContentBrowserModule::CreateContentBrowserViewExtender));
			}
			else
			{
				ContentBrowserModule.SetContentBrowserViewExtender(nullptr);
			}
		}));

	TSharedPtr<IContentBrowserViewExtender> FTedsContentBrowserModule::CreateContentBrowserViewExtender()
	{
		return MakeShared<FTedsContentBrowserViewExtender>();
	}

	void FTedsContentBrowserModule::StartupModule()
	{
		IModuleInterface::StartupModule();
	}

	void FTedsContentBrowserModule::ShutdownModule()
	{
		IModuleInterface::ShutdownModule();
	}

	void FTedsContentBrowserViewExtender::RefreshRows(TArray<TSharedPtr<FAssetViewItem>>* InItemsSource)
	{
		if(!InItemsSource)
		{
			return;
		}

		Rows.Empty();
		ContentBrowserItemMap.Empty();

		for(TSharedPtr<FAssetViewItem> Item : *InItemsSource)
		{
			if(!Item)
			{
				continue;
			}

			AddRow(Item);
		}
		
		RowQueryStack->MarkDirty();
	}

	void FTedsContentBrowserViewExtender::AddRow(const TSharedPtr<FAssetViewItem>& Item)
	{
		RowHandle RowHandle = GetRowFromAssetViewItem(Item);
		
		if(DataStorage->IsRowAssigned(RowHandle))
		{
			ContentBrowserItemMap.Emplace(RowHandle, Item);
			Rows.Add(RowHandle);
		}
	}

	TSharedPtr<FAssetViewItem> FTedsContentBrowserViewExtender::GetAssetViewItemFromRow(RowHandle Row)
	{
		// CB 2.0 TODO: Since FAssetViewItem was private previously, there is no good way to lookup currently aside from storing a map
		if(TWeakPtr<FAssetViewItem>* AssetViewItem = ContentBrowserItemMap.Find(Row))
		{
			if(TSharedPtr<FAssetViewItem> AssetViewItemPin = AssetViewItem->Pin())
			{
				return AssetViewItemPin;
			}
		}

		return nullptr;
	}

	DataStorage::RowHandle FTedsContentBrowserViewExtender::GetRowFromAssetViewItem(const TSharedPtr<FAssetViewItem>& Item)
	{
		FAssetData ItemAssetData;
		FName PackagePath;
		
		RowHandle RowHandle = InvalidRowHandle;
		
		if (Item->GetItem().Legacy_TryGetAssetData(ItemAssetData))
		{
			IndexHash IndexHash = GenerateIndexHash(ItemAssetData.GetSoftObjectPath());
			RowHandle = DataStorage->FindIndexedRow(IndexHash);
		}
		else if(Item->GetItem().Legacy_TryGetPackagePath(PackagePath))
		{
			IndexHash IndexHash = GenerateIndexHash(PackagePath);
			RowHandle = DataStorage->FindIndexedRow(IndexHash);
		}

		return RowHandle;
	}

	FTedsContentBrowserViewExtender::FTedsContentBrowserViewExtender()
	{
		DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);
		
		RowQueryStack = MakeShared<FQueryStackNode_RowView>(&Rows);

		// Sample dynamic column to display the "Skeleton" attribute on skeletal meshes
		// We probably want the dynamic columns in the table viewer to be data driven based on the rows in the future
		const UScriptStruct* DynamicSkeletalMeshSkeletonColumn = DataStorage->GenerateDynamicColumn(FDynamicColumnDescription
						{
							.TemplateType = FItemStringAttributeColumn_Experimental::StaticStruct(),
							.Identifier = "Skeleton"
						});
		
		// Create the table viewer widget
		TableViewer = SNew(STedsTableViewer)
					.QueryStack(RowQueryStack)
					.CellWidgetPurposes({TEXT("General.RowLabel"), TEXT("General.Cell")})
					// Default list of columns to display
					.Columns({ FNameColumn::StaticStruct(), FTypedElementAlertColumn::StaticStruct(),
						FAssetClassColumn::StaticStruct(), FAssetTag::StaticStruct(), FAssetPathColumn_Experimental::StaticStruct(),
						FDiskSizeColumn::StaticStruct(), FVirtualPathColumn_Experimental::StaticStruct(), DynamicSkeletalMeshSkeletonColumn })
					.ListSelectionMode(ESelectionMode::Multi)
					.OnSelectionChanged_Lambda([this](RowHandle Row)
					{
						if(TSharedPtr<FAssetViewItem> AssetViewItem = GetAssetViewItemFromRow(Row))
						{
							// CB 2.0 TODO: Does the CB use ESelectInfo and we need to propagate it from the table viewer?
							OnSelectionChangedDelegate.Execute(AssetViewItem, ESelectInfo::Direct);
						}
					});

		// Bind the delegates the CB view extender requires to delegates in TEDS columns on the widget row that are fired
		// when the event occurs
		const RowHandle WidgetRow = TableViewer->GetWidgetRowHandle();

		if(FWidgetContextMenuColumn* ContextMenuColumn = DataStorage->GetColumn<FWidgetContextMenuColumn>(WidgetRow))
		{
			ContextMenuColumn->OnContextMenuOpening.BindLambda([this]()
			{
				return OnContextMenuOpenedDelegate.Execute();
			});
		}
		if(FWidgetRowScrolledIntoView* ScrolledIntoViewColumn = DataStorage->GetColumn<FWidgetRowScrolledIntoView>(WidgetRow))
		{
			ScrolledIntoViewColumn->OnItemScrolledIntoView.BindLambda([this](FTedsRowHandle Row, const TSharedPtr<ITableRow>& TableRow)
			{
				if(TSharedPtr<FAssetViewItem> AssetViewItem = GetAssetViewItemFromRow(Row))
				{
					return OnItemScrolledIntoViewDelegate.Execute(AssetViewItem, TableRow);
				}
			});
		}
		if(FWidgetDoubleClickedColumn* DoubleClickedColumn = DataStorage->GetColumn<FWidgetDoubleClickedColumn>(WidgetRow))
		{
			DoubleClickedColumn->OnMouseButtonDoubleClick.BindLambda([this](FTedsRowHandle Row)
			{
				if(TSharedPtr<FAssetViewItem> AssetViewItem = GetAssetViewItemFromRow(Row))
				{
					return OnItemDoubleClickedDelegate.Execute(AssetViewItem);
				}
			});
		}
	}

	TSharedRef<SWidget> FTedsContentBrowserViewExtender::CreateView(TArray<TSharedPtr<FAssetViewItem>>* InItemsSource)
	{
		RefreshRows(InItemsSource);
		return TableViewer.ToSharedRef();
	}
	
	void FTedsContentBrowserViewExtender::OnItemListChanged(TArray<TSharedPtr<FAssetViewItem>>* InItemsSource)
	{
		// CB 2.0 TODO: We might want to track individual addition/removals instead of a full refresh for perf
		RefreshRows(InItemsSource);
	}


	TArray<TSharedPtr<FAssetViewItem>> FTedsContentBrowserViewExtender::GetSelectedItems()
	{
		// CB 2.0 TODO: Figure out selection
		TArray<TSharedPtr<FAssetViewItem>> SelectedItems;

		TableViewer->ForEachSelectedRow([this, &SelectedItems](RowHandle Row)
		{
			if(TSharedPtr<FAssetViewItem> AssetViewItem = GetAssetViewItemFromRow(Row))
			{
				SelectedItems.Add(AssetViewItem);
			}
		});
		
		return SelectedItems;
	}

	IContentBrowserViewExtender::FOnSelectionChanged& FTedsContentBrowserViewExtender::OnSelectionChanged()
	{
		return OnSelectionChangedDelegate;
	}

	FOnContextMenuOpening& FTedsContentBrowserViewExtender::OnContextMenuOpened()
	{
		return OnContextMenuOpenedDelegate;
	}

	IContentBrowserViewExtender::FOnItemScrolledIntoView& FTedsContentBrowserViewExtender::OnItemScrolledIntoView()
	{
		return OnItemScrolledIntoViewDelegate;
	}

	IContentBrowserViewExtender::FOnMouseButtonClick& FTedsContentBrowserViewExtender::OnItemDoubleClicked()
	{
		return OnItemDoubleClickedDelegate;
	}

	FText FTedsContentBrowserViewExtender::GetViewDisplayName()
	{
		return LOCTEXT("TedsCBViewName", "TEDS List View");
	}

	FText FTedsContentBrowserViewExtender::GetViewTooltipText()
	{
		return LOCTEXT("TedsCBViewTooltip", "A List view populated using TEDS UI and the asset registry data in TEDS");
	}

	void FTedsContentBrowserViewExtender::FocusList()
	{
		// CB 2.0 TODO: Do we need to focus the internal list? If so, implement using a Teds column
		FSlateApplication::Get().SetKeyboardFocus(TableViewer, EFocusCause::SetDirectly);
	}

	void FTedsContentBrowserViewExtender::SetSelection(const TSharedPtr<FAssetViewItem>& Item, bool bSelected, const ESelectInfo::Type SelectInfo)
	{
		RowHandle Row = GetRowFromAssetViewItem(Item);

		if(DataStorage->IsRowAssigned(Row))
		{
			// We have to defer the selection by a tick because this fires on path change which has to refresh the internal list of assets.
			// The table viewer doesn't refresh immediately but rather on tick by checking if the query stack is dirty. If we set the selection
			// before the refresh happens SListView will deselect the item since it isn't visible in the list yet.
			// Long term selection should also be handled through TEDS so it happens at the proper time automatically.
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this, Row, bSelected, SelectInfo](float)
			{
				TableViewer->SetSelection(Row, bSelected, SelectInfo);
				return false;
			}));
		}
	}

	void FTedsContentBrowserViewExtender::RequestScrollIntoView(const TSharedPtr<FAssetViewItem>& Item)
	{
		RowHandle Row = GetRowFromAssetViewItem(Item);

		if(DataStorage->IsRowAssigned(Row))
		{
			// We have to defer the scroll by a tick because this fires on path change which has to refresh the internal list of assets.
			// The table viewer doesn't refresh immediately but rather on tick by checking if the query stack is dirty. If we request scroll
			// before the refresh happens SListView will ignore the request since the item isn't visible in the list yet.
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this, Row](float)
			{
				TableViewer->ScrollIntoView(Row);
				return false;
			}));
		}
	}

	void FTedsContentBrowserViewExtender::ClearSelection()
	{
		TableViewer->ClearSelection();
	}

	bool FTedsContentBrowserViewExtender::IsRightClickScrolling()
	{
		// CB 2.0 TODO: Implement using a Teds column
		return false;
	}
} // namespace UE::Editor::ContentBrowser

IMPLEMENT_MODULE(UE::Editor::ContentBrowser::FTedsContentBrowserModule, TedsContentBrowser);

#undef LOCTEXT_NAMESPACE