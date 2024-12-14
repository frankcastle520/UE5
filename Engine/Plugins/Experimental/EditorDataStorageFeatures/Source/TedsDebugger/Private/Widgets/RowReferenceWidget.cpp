// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/RowReferenceWidget.h"

#include "Columns/TedsOutlinerColumns.h"
#include "Elements/Columns/TypedElementHiearchyColumns.h"
#include "Elements/Columns/TypedElementLabelColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "ISceneOutliner.h"
#include "Modules/ModuleManager.h"
#include "TedsDebuggerModule.h"
#include "Elements/Columns/TypedElementSlateWidgetColumns.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "RowReferenceWidget"

namespace UE::Editor::DataStorage::Debug::Private
{
	void OnNavigateHyperlink(const IEditorDataStorageProvider* DataStorage,  RowHandle TargetRowHandle, RowHandle UiRowHandle)
	{
		const FTedsOutlinerColumn* TedsOutlinerColumn = DataStorage->GetColumn<FTedsOutlinerColumn>(UiRowHandle);

		if(!TedsOutlinerColumn)
		{
			return;
		}
		
		TSharedPtr<ISceneOutliner> OwningTableViewer = TedsOutlinerColumn->Outliner.Pin();
		
		if(!OwningTableViewer)
		{
			return;
		}

		// If the item was found in this table viewer, select it and navigate to it
		if(FSceneOutlinerTreeItemPtr TreeItem = OwningTableViewer->GetTreeItem(TargetRowHandle))
		{
			OwningTableViewer->SetSelection([TreeItem](ISceneOutlinerTreeItem& Item)
			{
				return Item.GetID() == TreeItem->GetID();
			});
			
			OwningTableViewer->FrameSelectedItems();

			return;
		}
	}
	
	void CreateInternalWidget(const TWeakPtr<SWidget>& InWidget, RowHandle UiRow, RowHandle TargetRow)
	{
		const TSharedPtr<SWidget> Widget = InWidget.Pin();

		if(!Widget)
		{
			return;
		}
		
		checkf(Widget->GetType() == SBox::StaticWidgetClass().GetWidgetType(),
			TEXT("Stored widget with FTypedElementLabelWidgetConstructor doesn't match type %s, but was a %s."),
			*(SBox::StaticWidgetClass().GetWidgetType().ToString()),
			*(Widget->GetTypeAsString()));

		SBox* WidgetInstance = static_cast<SBox*>(Widget.Get());
		WidgetInstance->SetContent(SNullWidget::NullWidget);

		const IEditorDataStorageProvider* DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);

		// We only navigate to row references that have a label column
		if (const FTypedElementLabelColumn* LabelColumn = DataStorage->GetColumn<FTypedElementLabelColumn>(TargetRow))
		{
			FNumberFormattingOptions NumberFormattingOptions;
			NumberFormattingOptions.SetUseGrouping(false);

			const FText Text = FText::AsNumber(TargetRow, &NumberFormattingOptions);
			const FText TooltipText = FText::FromString(LabelColumn->Label);

			TSharedRef<SWidget> HyperlinkWidget =
				SNew(SHyperlink)
					.Text(Text)
					.Style(FAppStyle::Get(), "Common.GotoBlueprintHyperlink")
					.ToolTipText(TooltipText)
					.OnNavigate(FSimpleDelegate::CreateStatic(&OnNavigateHyperlink, DataStorage, TargetRow, UiRow));
			
			WidgetInstance->SetContent(HyperlinkWidget);
		}
	}


} // namespace UE::Editor::DataStorage::Debug::Private

URowReferenceWidgetFactory::~URowReferenceWidgetFactory()
{
}

void URowReferenceWidgetFactory::RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage, IEditorDataStorageUiProvider& DataStorageUi) const
{
	using namespace UE::Editor::DataStorage::Queries;

	// TEDS UI TODO: We can re-use this widget for FTypedElementParentColumn
	DataStorageUi.RegisterWidgetFactory<FRowReferenceWidgetConstructor>(FName(TEXT("SceneOutliner.Cell")),
		TColumn<FTypedElementRowReferenceColumn>());
}

void URowReferenceWidgetFactory::RegisterQueries(IEditorDataStorageProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage::Queries;
	
	const QueryHandle UpdateRowReferenceWidget = DataStorage.RegisterQuery(
		Select()
			.ReadOnly<FTypedElementRowReferenceColumn>()
		.Where()
			.Any<FTypedElementSyncFromWorldTag, FTypedElementSyncBackToWorldTag>()
		.Compile());

	DataStorage.RegisterQuery(
		Select(
			TEXT("Sync row reference to widget"),
			FProcessor(EQueryTickPhase::FrameEnd, DataStorage.GetQueryTickGroupName(EQueryTickGroups::SyncWidgets))
				.SetExecutionMode(EExecutionMode::GameThread),
			[](
				IQueryContext& Context,
				RowHandle UiRowHandle,
				FTypedElementSlateWidgetReferenceColumn& Widget,
				const FTypedElementRowReferenceColumn& Target)
			{
				Context.RunSubquery(0, Target.Row, CreateSubqueryCallbackBinding(
					[&Widget, UiRowHandle](const FTypedElementRowReferenceColumn& Target)
					{
						Debug::Private::CreateInternalWidget(Widget.Widget, UiRowHandle, Target.Row);
					}));
			})
		.DependsOn()
			.SubQuery( UpdateRowReferenceWidget )
		.Compile());
}

FRowReferenceWidgetConstructor::FRowReferenceWidgetConstructor()
	: Super(FRowReferenceWidgetConstructor::StaticStruct())
{
}

TSharedPtr<SWidget> FRowReferenceWidgetConstructor::CreateWidget(const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	return SNew(SBox)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center);
}

bool FRowReferenceWidgetConstructor::FinalizeWidget(IEditorDataStorageProvider* DataStorage, IEditorDataStorageUiProvider* DataStorageUi, UE::Editor::DataStorage::RowHandle Row, const TSharedPtr<SWidget>& Widget)
{
	using namespace UE::Editor::DataStorage;
	checkf(Widget, TEXT("Referenced widget is not valid. A constructed widget may not have been cleaned up. This can "
		"also happen if this processor is running in the same phase as the processors responsible for cleaning up old "
		"references."));

	// The actual row we want to view in the widget
	RowHandle TargetRowReference = InvalidRowHandle;

	// The target row for which this widget was created
	const RowHandle TargetRow = DataStorage->GetColumn<FTypedElementRowReferenceColumn>(Row)->Row;

	// Check if the target row has a row reference column, if so we want to view the row in there.
	if(const FTypedElementRowReferenceColumn* RowReferenceColumn = DataStorage->GetColumn<FTypedElementRowReferenceColumn>(TargetRow))
	{
		TargetRowReference = RowReferenceColumn->Row;
	}
	
	Debug::Private::CreateInternalWidget(Widget, Row, TargetRowReference);

	return true;
}

#undef LOCTEXT_NAMESPACE