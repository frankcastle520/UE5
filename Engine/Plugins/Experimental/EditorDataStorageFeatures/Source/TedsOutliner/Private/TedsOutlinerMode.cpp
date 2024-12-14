// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsOutlinerMode.h"

#include "TedsOutlinerFilter.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "TedsOutlinerHierarchy.h"
#include "TedsOutlinerItem.h"
#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementHiearchyColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementSelectionColumns.h"
#include "FolderTreeItem.h"
#include "Elements/Columns/TypedElementLabelColumns.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "TedsOutlinerMode"

namespace UE::Editor::Outliner
{
	namespace Private
	{
		// Drag drop currently disabled as we are missing data marshaling for hierarchies from TEDS to the world
		static bool TedsOutlinerDragDropEnabled = false;
		static FAutoConsoleVariableRef TedsOutlinerDragDropEnabledCvar(TEXT("TEDS.UI.EnableTEDSOutlinerDragDrop"), TedsOutlinerDragDropEnabled, TEXT("Enable drag/drop for the generic TEDS Outliner."));
		static FName ContextMenuName("TedsOutlinerContextMenu");
	} // namespace Private

FTedsOutlinerMode::FTedsOutlinerMode(const FTedsOutlinerParams& InParams)
	: ISceneOutlinerMode(InParams.SceneOutliner)
{	
	TedsOutlinerImpl = MakeShared<FTedsOutlinerImpl>(InParams, this);
	TedsOutlinerImpl->Init();
	TedsOutlinerImpl->OnSelectionChanged().AddRaw(this, &FTedsOutlinerMode::OnSelectionChanged);
	
	TedsOutlinerImpl->IsItemCompatible().BindLambda([](const ISceneOutlinerTreeItem& Item)
	{
		return Item.IsA<FTedsOutlinerTreeItem>();
	});
}

FTedsOutlinerMode::~FTedsOutlinerMode()
{
	
}

void FTedsOutlinerMode::Rebuild()
{
	Hierarchy = CreateHierarchy();
}

void FTedsOutlinerMode::OnSelectionChanged()
{
	TOptional<FName> SelectionSetName = TedsOutlinerImpl->GetSelectionSetName();
	IEditorDataStorageProvider* Storage = TedsOutlinerImpl->GetStorage();
	
	// The selection in TEDS was changed, update the outliner to respond
	SceneOutliner->SetSelection([SelectionSetName, Storage](ISceneOutlinerTreeItem& InItem) -> bool
	{
		if(const FTedsOutlinerTreeItem* TedsItem = InItem.CastTo<FTedsOutlinerTreeItem>())
		{
			const DataStorage::RowHandle RowHandle = TedsItem->GetRowHandle();

			if(const FTypedElementSelectionColumn* SelectionColumn = Storage->GetColumn<FTypedElementSelectionColumn>(RowHandle))
			{
				return SelectionColumn->SelectionSet == SelectionSetName;
			}
		}
		return false;
	});
}

void FTedsOutlinerMode::SynchronizeSelection()
{
	OnSelectionChanged();
}

void FTedsOutlinerMode::OnItemSelectionChanged(FSceneOutlinerTreeItemPtr Item, ESelectInfo::Type SelectionType, const FSceneOutlinerItemSelection& Selection)
{
	if(SelectionType == ESelectInfo::Direct)
	{
		return; // Direct selection means we selected from outside the Outliner i.e through TEDS, so we don't need to redo the column addition
	}

	TArray<DataStorage::RowHandle> RowHandles;
	
	// The selection in the Outliner changed, update TEDS
	Selection.ForEachItem([&RowHandles](FSceneOutlinerTreeItemPtr& Item)
	{
		if(FTedsOutlinerTreeItem* TedsItem = Item->CastTo<FTedsOutlinerTreeItem>())
		{
			RowHandles.Add(TedsItem->GetRowHandle());
		}
	});

	TedsOutlinerImpl->SetSelection(RowHandles);
}

TSharedPtr<FDragDropOperation> FTedsOutlinerMode::CreateDragDropOperation(const FPointerEvent& MouseEvent, const TArray<FSceneOutlinerTreeItemPtr>& InTreeItems) const
{
	const TOptional<FTedsOutlinerHierarchyData>& HierarchyData = TedsOutlinerImpl->GetHierarchyData();

	// We don't want drag/drop if this TEDS Outliner isn't showing any hierarchy data
	if(!HierarchyData.IsSet())
	{
		return nullptr;
	}
	
	TArray<DataStorage::RowHandle> DraggedRowHandles;

	for(const FSceneOutlinerTreeItemPtr& Item :InTreeItems)
	{
		const FTedsOutlinerTreeItem* TedsItem = Item->CastTo<FTedsOutlinerTreeItem>();
		if(ensureMsgf(TedsItem, TEXT("We should only have TEDS items in the TEDS Outliner")))
		{
			DraggedRowHandles.Add(TedsItem->GetRowHandle());
		}
	}

	return FTedsRowDragDropOp::New(DraggedRowHandles);
}

bool FTedsOutlinerMode::ParseDragDrop(FSceneOutlinerDragDropPayload& OutPayload, const FDragDropOperation& Operation) const
{
	if (Operation.IsOfType<FTedsRowDragDropOp>())
	{
		const FTedsRowDragDropOp& TedsOp = static_cast<const FTedsRowDragDropOp&>(Operation);

		for(DataStorage::RowHandle RowHandle : TedsOp.DraggedRows)
		{
			OutPayload.DraggedItems.Add(SceneOutliner->GetTreeItem(RowHandle));
		}
		return true;
	}
	return false;
}

FSceneOutlinerDragValidationInfo FTedsOutlinerMode::ValidateDrop(const ISceneOutlinerTreeItem& DropTarget, const FSceneOutlinerDragDropPayload& Payload) const
{
	const TOptional<FTedsOutlinerHierarchyData>& HierarchyData = TedsOutlinerImpl->GetHierarchyData();
	IEditorDataStorageProvider* Storage = TedsOutlinerImpl->GetStorage();

	// We don't want drag/drop if this TEDS Outliner isn't showing any hierarchy data
	if(!HierarchyData.IsSet())
	{
		return FSceneOutlinerDragValidationInfo(ESceneOutlinerDropCompatibility::IncompatibleGeneric,
			LOCTEXT("DropDisabled", "Drag/Drop is disabled due to missing hierarchy data!"));
	}
	
	TArray<DataStorage::RowHandle> DraggedRowHandles;

	Payload.ForEachItem<FTedsOutlinerTreeItem>([&DraggedRowHandles](FTedsOutlinerTreeItem& TedsItem)
		{
			DraggedRowHandles.Add(TedsItem.GetRowHandle());
		});

	// Dropping onto another item
	// TEDS-Outliner TODO: Need better drag/drop validation and better place for this, TEDS-Outliner does not know about what types these rows are and all types that exist and what attachment is valid
	if(const FTedsOutlinerTreeItem* TedsItem = DropTarget.CastTo<FTedsOutlinerTreeItem>())
	{
		DataStorage::RowHandle DropTargetRowHandle = TedsItem->GetRowHandle();

		FTypedElementClassTypeInfoColumn* DropTargetTypeInfoColumn = Storage->GetColumn<FTypedElementClassTypeInfoColumn>(DropTargetRowHandle);

		// For now only allow attachment to same type
		if(!DropTargetTypeInfoColumn || !DropTargetTypeInfoColumn->TypeInfo.Get())
		{
			return FSceneOutlinerDragValidationInfo(ESceneOutlinerDropCompatibility::IncompatibleGeneric, LOCTEXT("DropTargetInvalidType", "Invalid Drop target"));
		}

		
		// TEDS-Outliner TODO: Currently we detect parent changes by removing the column and then adding the column back with the new parent 
		for(DataStorage::RowHandle RowHandle : DraggedRowHandles)
		{
			FTypedElementClassTypeInfoColumn* TypeInfoColumn = Storage->GetColumn<FTypedElementClassTypeInfoColumn>(RowHandle);

			if(!TypeInfoColumn || !TypeInfoColumn->TypeInfo.Get())
			{
				return FSceneOutlinerDragValidationInfo(ESceneOutlinerDropCompatibility::IncompatibleGeneric, LOCTEXT("DragItemInvalidType", "Invalid Drag item"));
			}

			// TEDS-Outliner TOOD UE-205438: Proper drag/drop validation
			if(TypeInfoColumn->TypeInfo.Get() != DropTargetTypeInfoColumn->TypeInfo.Get())
			{
				return FSceneOutlinerDragValidationInfo(ESceneOutlinerDropCompatibility::IncompatibleGeneric, FText::Format(LOCTEXT("DragDropTypeMismatch", "Cannot drag a {0} into a {1}"), FText::FromName(TypeInfoColumn->TypeInfo.Get()->GetFName()), FText::FromName(DropTargetTypeInfoColumn->TypeInfo.Get()->GetFName())));
			}
		}

		return FSceneOutlinerDragValidationInfo(ESceneOutlinerDropCompatibility::CompatibleAttach, LOCTEXT("ValidDrop", "Valid Drop"));
	}
	// Dropping onto root, remove parent
	else if (const FFolderTreeItem* FolderItem = DropTarget.CastTo<FFolderTreeItem>())
	{
		const FFolder DestinationPath = FolderItem->GetFolder();

		if(DestinationPath.IsNone())
		{
			bool bValidDetach = false;
			
			for(DataStorage::RowHandle RowHandle : DraggedRowHandles)
			{
				if(Storage->HasColumns(RowHandle, MakeArrayView({HierarchyData.GetValue().HierarchyColumn})))
				{
					bValidDetach = true;
					break;
				}
			}

			if(bValidDetach)
			{
				return FSceneOutlinerDragValidationInfo(ESceneOutlinerDropCompatibility::CompatibleDetach, LOCTEXT("MoveToRoot", "Move to root"));
			}
		}
	}

	return FSceneOutlinerDragValidationInfo(ESceneOutlinerDropCompatibility::IncompatibleGeneric, LOCTEXT("InvalidDrop", "Invalid Drop target"));
}

void FTedsOutlinerMode::OnDrop(ISceneOutlinerTreeItem& DropTarget, const FSceneOutlinerDragDropPayload& Payload, const FSceneOutlinerDragValidationInfo& ValidationInfo) const
{
	const TOptional<FTedsOutlinerHierarchyData>& HierarchyData = TedsOutlinerImpl->GetHierarchyData();
	IEditorDataStorageProvider* Storage = TedsOutlinerImpl->GetStorage();

	if(!Private::TedsOutlinerDragDropEnabledCvar->GetBool() || !HierarchyData.IsSet())
	{
		return;
	}
	
	TArray<DataStorage::RowHandle> DraggedRowHandles;

	Payload.ForEachItem<FTedsOutlinerTreeItem>([&DraggedRowHandles](FTedsOutlinerTreeItem& TedsItem)
	{
		DraggedRowHandles.Add(TedsItem.GetRowHandle());
	});

	if(ValidationInfo.CompatibilityType == ESceneOutlinerDropCompatibility::CompatibleDetach)
	{
		for(DataStorage::RowHandle RowHandle : DraggedRowHandles)
		{
			Storage->RemoveColumn(RowHandle, HierarchyData.GetValue().HierarchyColumn);
			Storage->AddColumn<FTypedElementSyncBackToWorldTag>(RowHandle);
		}
	}
	
	if(const FTedsOutlinerTreeItem* TedsItem = DropTarget.CastTo<FTedsOutlinerTreeItem>())
	{
		DataStorage::RowHandle DropTargetRowHandle = TedsItem->GetRowHandle();
		
		for(DataStorage::RowHandle RowHandle : DraggedRowHandles)
		{
			// Add the column
			Storage->AddColumn(RowHandle, HierarchyData.GetValue().HierarchyColumn);

			// Let the hierarchy data fill in the parent row into the column
			HierarchyData.GetValue().SetParent.Execute(Storage->GetColumnData(RowHandle, HierarchyData.GetValue().HierarchyColumn), DropTargetRowHandle);
			
			Storage->AddColumn<FTypedElementSyncBackToWorldTag>(RowHandle);
		}
	}
}

TSharedPtr<SWidget> FTedsOutlinerMode::CreateContextMenu()
{
	UToolMenus* ToolMenus = UToolMenus::Get();

	if (!ToolMenus->IsMenuRegistered(Private::ContextMenuName))
	{
		UToolMenu* Menu = ToolMenus->RegisterMenu(Private::ContextMenuName);
		Menu->AddDynamicSection("DynamicHierarchySection", FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
		{
			if(UTedsOutlinerMenuContext* TedsOutlinerMenuContext = InMenu->FindContext<UTedsOutlinerMenuContext>())
			{
				if(SSceneOutliner* SceneOutliner = TedsOutlinerMenuContext->OwningSceneOutliner)
				{
					TArray<FSceneOutlinerTreeItemPtr> Selection = SceneOutliner->GetTree().GetSelectedItems();

					if(Selection.Num() == 1)
					{
						Selection[0]->GenerateContextMenu(InMenu, *SceneOutliner);
					}
				}
			}
			
		}));
	}

	UTedsOutlinerMenuContext* TedsOutlinerMenuContext = NewObject<UTedsOutlinerMenuContext>();
	TedsOutlinerMenuContext->OwningSceneOutliner = SceneOutliner;
	
	FToolMenuContext MenuContext;
	MenuContext.AddObject(TedsOutlinerMenuContext);

	return UToolMenus::Get()->GenerateWidget(Private::ContextMenuName, MenuContext);
}

TUniquePtr<ISceneOutlinerHierarchy> FTedsOutlinerMode::CreateHierarchy()
{
	return MakeUnique<FTedsOutlinerHierarchy>(this, TedsOutlinerImpl.ToSharedRef());
}
} // namespace UE::Editor::Outliner

#undef LOCTEXT_NAMESPACE
