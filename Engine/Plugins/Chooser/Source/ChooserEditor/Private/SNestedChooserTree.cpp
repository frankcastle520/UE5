// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNestedChooserTree.h"
#include "ChooserTableEditor.h"
#include "Chooser.h"
#include "Framework/Application/SlateApplication.h"
#include "ObjectTools.h"
#include "OutputObjectColumn.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/STextEntryPopup.h"

#define LOCTEXT_NAMESPACE "NestedChooserTree"

namespace UE::ChooserEditor
{
	
	TSharedRef<ITableRow> SNestedChooserTree::TreeViewGenerateRow(TSharedPtr<FNestedChooserTreeEntry> InItem, const TSharedRef<STableViewBase>& OwnerTable)
	{
			return SNew(STableRow<TSharedPtr<FNestedChooserTreeEntry>>, OwnerTable)
						.Content()
						[
							SNew(STextBlock).Text(FText::FromString(InItem->Chooser->GetName()))
						];
	}
	
	void SNestedChooserTree::TreeViewGetChildren(TSharedPtr<FNestedChooserTreeEntry> InItem, TArray<TSharedPtr<FNestedChooserTreeEntry>>& OutChildren)
    {
		for(TSharedPtr<FNestedChooserTreeEntry>& Entry : AllChoosers)
		{
			if (Entry->Chooser->GetOuter() == InItem->Chooser)
			{
				OutChildren.Add(Entry);
			}
		}
    }
	
	void SNestedChooserTree::TreeViewDoubleClicked(TSharedPtr<FNestedChooserTreeEntry> SelectedObject)
	{
		ChooserEditor->SetChooserTableToEdit(SelectedObject->Chooser);
	}

	// Replace any references to a nested chooser, from either the Result column, or an OutputObject column
	static void ReplaceReferencesInTable(UChooserTable* ChooserToReplace, UChooserTable* ReplaceWith, UChooserTable* Table)
	{
		Table->Modify();
		
		for(FInstancedStruct& ResultData : Table->ResultsStructs)
		{
			if (FNestedChooser* NestedChooserResult = ResultData.GetMutablePtr<FNestedChooser>())
			{
				if (NestedChooserResult->Chooser == ChooserToReplace)
				{
					NestedChooserResult->Chooser = ReplaceWith;
				}
			}

			if (FNestedChooser* NestedChooserFallback = Table->FallbackResult.GetMutablePtr<FNestedChooser>())
			{
				if (NestedChooserFallback->Chooser == ChooserToReplace)
				{
					NestedChooserFallback->Chooser = ReplaceWith;
				}
			}
		}
		
		for (FInstancedStruct& ColumnData : Table->ColumnsStructs)
		{
			if (FOutputObjectColumn* OutputObjectColumn = ColumnData.GetMutablePtr<FOutputObjectColumn>())
			{
				for(FChooserOutputObjectRowData& RowData : OutputObjectColumn->RowValues)
				{
					if (FNestedChooser* NestedChooserResult = RowData.Value.GetMutablePtr<FNestedChooser>())
					{
						if (NestedChooserResult->Chooser == ChooserToReplace)
						{
							NestedChooserResult->Chooser = ReplaceWith;
						}
					}	
				}
				
				if (FNestedChooser* NestedChooserFallback = OutputObjectColumn->FallbackValue.Value.GetMutablePtr<FNestedChooser>())
				{
					if (NestedChooserFallback->Chooser == ChooserToReplace)
					{
						NestedChooserFallback->Chooser = ReplaceWith;
					}
				}
				
				if (FNestedChooser* NestedChooserDefault = OutputObjectColumn->DefaultRowValue.Value.GetMutablePtr<FNestedChooser>())
				{
					if (NestedChooserDefault->Chooser == ChooserToReplace)
					{
						NestedChooserDefault->Chooser = ReplaceWith;
					}
				}	
			}
		}

	}
	
	static void ReplaceReferences(UChooserTable* ChooserToReplace, UChooserTable* ReplaceWith, UChooserTable* RootTable)
	{
		ReplaceReferencesInTable(ChooserToReplace, ReplaceWith, RootTable);

		for(UChooserTable* NestedChooser : RootTable->NestedChoosers)
		{
			ReplaceReferencesInTable(ChooserToReplace, ReplaceWith, NestedChooser);

			// reparent any child tables to the root
			if (NestedChooser->GetOuter() == ChooserToReplace)
			{
				NestedChooser->Rename(nullptr, RootTable);
			}
		}
	}

	void SNestedChooserTree::DeleteNestedChooser()
	{
		TArray<TSharedPtr<FNestedChooserTreeEntry>>SelectedItems = TreeView->GetSelectedItems();
		if (SelectedItems.Num() == 1)
		{
			UChooserTable* ChooserToDelete = SelectedItems[0]->Chooser;

			if (ChooserToDelete != RootChooser)
			{
				FScopedTransaction Transaction(LOCTEXT("Delete Nested Choosers", "Delete Nested Choosers"));
				static FName DeletedChooserName = "DeletedNestedChooser";
				DeletedChooserName.SetNumber(DeletedChooserName.GetNumber() + 1);
				ChooserToDelete->Rename(*DeletedChooserName.ToString());
				RootChooser->Modify(true);
				RootChooser->RemoveNestedChooser(ChooserToDelete);
				ReplaceReferences(ChooserToDelete, nullptr, RootChooser);
			}
		}
	}

	void SNestedChooserTree::RenameNestedChooser()
	{
		TArray<TSharedPtr<FNestedChooserTreeEntry>>SelectedItems = TreeView->GetSelectedItems();
		if (SelectedItems.Num() == 1)
		{
			UChooserTable* ChooserToRename = SelectedItems[0]->Chooser;

			if (ChooserToRename != RootChooser)
			{
				TSharedRef<STextEntryPopup> TextEntry =
					SNew(STextEntryPopup)
					.DefaultText(FText::FromString(ChooserToRename->GetName()))
					.Label(LOCTEXT("RenameNestedChooserLabel", "Rename Chooser"))
					.OnTextCommitted_Lambda([ChooserToRename, this](FText InText, ETextCommit::Type InCommitType)
					{
						if (InCommitType == ETextCommit::OnEnter)
						{
							FSlateApplication::Get().DismissAllMenus();

							FString NewName = InText.ToString();
							
							for(UChooserTable* NestedChooser : RootChooser->NestedChoosers)
							{
								if (NestedChooser->GetName() == NewName)
								{
									return;
								}
							}
							
							const FScopedTransaction Transaction(LOCTEXT("Rename Nested Chooser", "Rename Nested Chooser"));
				
							ChooserToRename->Modify(true);
							ChooserToRename->Rename(*NewName);
							RefreshAll();
						}
					});

				FSlateApplication& SlateApp = FSlateApplication::Get();
				SlateApp.PushMenu(
					SlateApp.GetInteractiveTopLevelWindows()[0],
					FWidgetPath(),
					TextEntry,
					SlateApp.GetCursorPos(),
					FPopupTransitionEffect::TypeInPopup
					);
			}
		}
	}

	void SNestedChooserTree::Construct(const FArguments& InArgs)
	{
		ChooserEditor = InArgs._ChooserEditor;
		RootChooser = ChooserEditor->GetRootChooser();
		RootChooser->NestedChoosersChanged.AddRaw(this, &SNestedChooserTree::RefreshAll);
		TreeEntries.Add(MakeShared<FNestedChooserTreeEntry>(FNestedChooserTreeEntry({RootChooser})));

		TreeView = SNew(STreeView<TSharedPtr<FNestedChooserTreeEntry>>)
							.OnExpansionChanged_Lambda([](TSharedPtr<FNestedChooserTreeEntry> Entry, bool bExpanded){ Entry->bExpanded = bExpanded; })
							.OnKeyDownHandler_Lambda([this](const FGeometry&, const FKeyEvent& Event)
							{
								if (Event.GetKey() == EKeys::Delete)
								{
									DeleteNestedChooser();
									return FReply::Handled();
								}
								else if (Event.GetKey() == EKeys::F2)
								{
									RenameNestedChooser();
									return FReply::Handled();
								}
								return FReply::Unhandled();
							})
							.OnContextMenuOpening(this, &SNestedChooserTree::TreeViewContextMenuOpening)
							.TreeItemsSource(&TreeEntries)
							.SelectionMode(ESelectionMode::Single)
							.OnGenerateRow(this, &SNestedChooserTree::TreeViewGenerateRow)
							.OnGetChildren(this, &SNestedChooserTree::TreeViewGetChildren)
							.OnMouseButtonDoubleClick(this, &SNestedChooserTree::TreeViewDoubleClicked)
							;
		
		RefreshAll();
	
		ChildSlot
		[
			TreeView.ToSharedRef()
		];

	}

	SNestedChooserTree::~SNestedChooserTree()
	{
		if (RootChooser)
		{
			RootChooser->NestedChoosersChanged.RemoveAll(this);
		}
	}

	static TSharedPtr<FNestedChooserTreeEntry> MakeEntry(TArray<TSharedPtr<FNestedChooserTreeEntry>>& OldValues, UChooserTable* Chooser)
	{
		for(TSharedPtr<FNestedChooserTreeEntry>& Entry : OldValues)
		{
			if (Entry->Chooser == Chooser)
			{
				return Entry;
			}
		}

		return MakeShared<FNestedChooserTreeEntry>(FNestedChooserTreeEntry({Chooser, true}));
	}

	void SNestedChooserTree::RefreshAll()
	{
		TArray<TSharedPtr<FNestedChooserTreeEntry>> OldValues = AllChoosers;
		AllChoosers.SetNum(0);
		RootChooser = ChooserEditor->GetRootChooser();
		AllChoosers.Add(MakeEntry(OldValues, RootChooser));
		for(UChooserTable* Chooser : RootChooser->NestedChoosers)
		{
			AllChoosers.Add(MakeEntry(OldValues, Chooser));
		}

		if (TreeView.IsValid())
		{
			TreeView->RebuildList();
			
			for(TSharedPtr<FNestedChooserTreeEntry>& Entry : AllChoosers)
			{
				TreeView->SetItemExpansion(Entry, Entry->bExpanded);
			}
		}
	}

	TSharedPtr<SWidget> SNestedChooserTree::TreeViewContextMenuOpening()
	{
		FMenuBuilder MenuBuilder(true, nullptr);
		MenuBuilder.AddMenuEntry(LOCTEXT("Delete", "Delete"), LOCTEXT("Delete Tooltip", "Delete Nested Chooser Table"), FSlateIcon(), FUIAction
			(
				FExecuteAction::CreateRaw(this, &SNestedChooserTree::DeleteNestedChooser)
				)
				);
		MenuBuilder.AddMenuEntry(LOCTEXT("Rename", "Rename"), LOCTEXT("Rename Tooltip", "Rename Nested Chooser Table"), FSlateIcon(), FUIAction
			(
				FExecuteAction::CreateRaw(this, &SNestedChooserTree::RenameNestedChooser)
				)
				);

		return MenuBuilder.MakeWidget();
	}
}

#undef LOCTEXT_NAMESPACE
