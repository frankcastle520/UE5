// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SNiagaraHierarchyEditor.h"

#include "NiagaraEditorCommon.h"
#include "UObject/Package.h"
#include "Framework/Commands/GenericCommands.h"
#include "Styling/StyleColors.h"
#include "Styling/AppStyle.h"
#include "ViewModels/HierarchyEditor/NiagaraHierarchyViewModelBase.h"
#include "Widgets/SNiagaraParameterName.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "ToolMenus.h"
#include "ViewModels/HierarchyEditor/NiagaraHierarchyCommands.h"
#include "Widgets/Input/SSearchBox.h"

#define LOCTEXT_NAMESPACE "NiagaraHierarchy"

TSharedRef<SWidget> SummonContextMenu(TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>> Items, bool bFromHierarchy, TSharedPtr<const SWidget> HierarchyWidget)
{
	UNiagaraHierarchyMenuContext* MenuContextObject = NewObject<UNiagaraHierarchyMenuContext>();
	MenuContextObject->Items = Items;
	MenuContextObject->bFromHierarchy = bFromHierarchy;

	FToolMenuContext MenuContext(MenuContextObject);
	TWeakObjectPtr<UNiagaraHierarchyViewModelBase> ViewModel = Items[0]->GetHierarchyViewModel();
	MenuContext.AppendCommandList(ViewModel->GetCommands());
		
	UToolMenu* Menu = UToolMenus::Get()->GenerateMenu("NiagaraHierarchyMenu", MenuContext);
	FToolMenuSection* BaseSection = Menu->FindSection("Base");

	if(ensure(BaseSection != nullptr))
	{
		if(Items.Num() == 1)
		{
			if(Items[0]->CanRename())
			{
				BaseSection->AddMenuEntry(FGenericCommands::Get().Rename);
			}
		}

		if(Items.Num() == 1 && Items[0]->GetData()->IsA<UNiagaraHierarchySection>())
		{
			BaseSection->AddMenuEntry(FNiagaraHierarchyEditorCommands::Get().DeleteSection);
		}
		
		// the generic delete command handles hierarchy items (not sections)
		BaseSection->AddMenuEntry(FGenericCommands::Get().Delete);
		
		if(Items.Num() == 1 && bFromHierarchy == false)
		{
			if(UNiagaraHierarchyItemBase* FoundItem = ViewModel->GetHierarchyRoot()->FindChildWithIdentity(Items[0]->GetData()->GetPersistentIdentity(), true))
			{
				FExecuteAction ExecuteAction = FExecuteAction::CreateLambda([Items, ViewModel, HierarchyWidget, Identity = FoundItem->GetPersistentIdentity()]()
				{
					TSharedPtr<FNiagaraHierarchyItemViewModelBase> RespectiveHierarchyItem = ViewModel->GetHierarchyRootViewModel()->FindViewModelForChild(Identity, true);
					if(RespectiveHierarchyItem.IsValid())
					{
						TSharedPtr<const SNiagaraHierarchyEditor> CastHierarchyWidget = StaticCastSharedPtr<const SNiagaraHierarchyEditor>(HierarchyWidget);
						CastHierarchyWidget->NavigateToHierarchyItem(RespectiveHierarchyItem);
					}
				});
				FToolUIActionChoice Action(ExecuteAction);
				BaseSection->AddMenuEntry(FName("FindInHierarchy"), LOCTEXT("FindInHierarchyLabel", "Find in Hierarchy"), FText::GetEmpty(), FSlateIcon(), Action);
			}			
		}
	}

	if(Items.Num() == 1)
	{
		FToolMenuSection& DynamicSection = Menu->FindOrAddSection("Dynamic");		
		Items[0]->PopulateDynamicContextMenuSection(DynamicSection);
	}	
		
	TSharedRef<SWidget> MenuWidget = UToolMenus::Get()->GenerateWidget(Menu);
	return MenuWidget;
}

void SNiagaraHierarchyCategory::Construct(const FArguments& InArgs,	TSharedPtr<FNiagaraHierarchyCategoryViewModel> InCategoryViewModel)
{
	CategoryViewModel = InCategoryViewModel;
	
	InCategoryViewModel->GetOnRequestRename().BindSP(this, &SNiagaraHierarchyCategory::EnterEditingMode);

	const UNiagaraHierarchyCategory* CategoryData = InCategoryViewModel->GetData<UNiagaraHierarchyCategory>();
	SetToolTipText(TAttribute<FText>::CreateLambda([CategoryData]
	{
		return CategoryData->GetTooltip();
	}));
	
	ChildSlot
	[
		SAssignNew(InlineEditableTextBlock, SInlineEditableTextBlock)
		.Style(&FNiagaraEditorStyle::Get().GetWidgetStyle<FInlineEditableTextBlockStyle>("NiagaraEditor.HierarchyEditor.CategoryTextStyle"))
		.Text(this, &SNiagaraHierarchyCategory::GetCategoryText)
		.OnTextCommitted(this, &SNiagaraHierarchyCategory::OnRenameCategory)
		.OnVerifyTextChanged(this, &SNiagaraHierarchyCategory::OnVerifyCategoryRename)
		.IsSelected(InArgs._IsSelected)
	];
}

void SNiagaraHierarchyCategory::EnterEditingMode() const
{
	if(CategoryViewModel.Pin()->CanRename())
	{
		InlineEditableTextBlock->EnterEditingMode();
	}
}

bool SNiagaraHierarchyCategory::OnVerifyCategoryRename(const FText& NewName, FText& OutTooltip) const
{
	TArray<TSharedPtr<FNiagaraHierarchyCategoryViewModel>> SiblingCategoryViewModels;
	CategoryViewModel.Pin()->GetParent().Pin()->GetChildrenViewModelsForType<UNiagaraHierarchyCategory, FNiagaraHierarchyCategoryViewModel>(SiblingCategoryViewModels);

	if(GetCategoryText().ToString() != NewName.ToString())
	{
		TSet<FString> CategoryNames;
		for(const auto& SiblingCategoryViewModel : SiblingCategoryViewModels)
		{
			CategoryNames.Add(Cast<UNiagaraHierarchyCategory>(SiblingCategoryViewModel->GetDataMutable())->GetCategoryName().ToString());
		}

		if(CategoryNames.Contains(NewName.ToString()))
		{
			OutTooltip = LOCTEXT("HierarchyCategoryCantRename_DuplicateOnLayer", "Another category of the same name already exists!");
			return false;
		}
	}

	return true;
}

FText SNiagaraHierarchyCategory::GetCategoryText() const
{
	return FText::FromString(CategoryViewModel.Pin()->ToString());
}

void SNiagaraHierarchyCategory::OnRenameCategory(const FText& NewText, ETextCommit::Type) const
{
	FScopedTransaction Transaction(LOCTEXT("Transaction_Rename_Category", "Renamed hierarchy category"));
	CategoryViewModel.Pin()->GetHierarchyViewModel()->GetHierarchyRoot()->Modify();
	
	CategoryViewModel.Pin()->Rename(FName(NewText.ToString()));
}

void SNiagaraHierarchySection::Construct(const FArguments& InArgs, TSharedPtr<FNiagaraHierarchySectionViewModel> InSection, TWeakObjectPtr<UNiagaraHierarchyViewModelBase> InHierarchyViewModel)
{
	SectionViewModel = InSection;
	HierarchyViewModel = InHierarchyViewModel;

	IsSectionActive = InArgs._IsSectionActive;
	OnSectionActivatedDelegate = InArgs._OnSectionActivated;
	bForbidDropOn = InArgs._bForbidDropOn;
	
	if(SectionViewModel != nullptr)
	{
		SectionViewModel->GetOnRequestRename().BindSP(this, &SNiagaraHierarchySection::TryEnterEditingMode);

		SDropTarget::FArguments LeftDropTargetArgs;
		SDropTarget::FArguments OntoDropTargetArgs;
		SDropTarget::FArguments RightDropTargetArgs;

		LeftDropTargetArgs
			.OnAllowDrop(this, &SNiagaraHierarchySection::OnCanAcceptDrop, EItemDropZone::AboveItem)
			.OnDropped(this, &SNiagaraHierarchySection::OnDroppedOn, EItemDropZone::AboveItem)
			.VerticalImage(FAppStyle::GetNoBrush())
			.HorizontalImage(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Stack.DropTarget.BorderHorizontal"));

		OntoDropTargetArgs
			.OnAllowDrop(this, &SNiagaraHierarchySection::OnCanAcceptDrop, EItemDropZone::OntoItem)
			.OnDropped(this, &SNiagaraHierarchySection::OnDroppedOn, EItemDropZone::OntoItem);
		
		RightDropTargetArgs
			.OnAllowDrop(this, &SNiagaraHierarchySection::OnCanAcceptDrop, EItemDropZone::BelowItem)
			.OnDropped(this, &SNiagaraHierarchySection::OnDroppedOn, EItemDropZone::BelowItem)
			.VerticalImage(FAppStyle::GetNoBrush())
			.HorizontalImage(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Stack.DropTarget.BorderHorizontal"));

		SetToolTipText(TAttribute<FText>::CreateSP(this, &SNiagaraHierarchySection::GetTooltipText));

		ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SNiagaraSectionDragDropTarget, SectionViewModel, EItemDropZone::AboveItem)
				.DropTargetArgs(LeftDropTargetArgs)
			]
			+ SHorizontalBox::Slot()
			[
				SNew(SNiagaraSectionDragDropTarget, SectionViewModel, EItemDropZone::OntoItem)
				.DropTargetArgs(OntoDropTargetArgs
				.Content()
				[
					SAssignNew(CheckBox, SCheckBox)
					.Visibility(EVisibility::HitTestInvisible)
					.Style(FAppStyle::Get(), "DetailsView.SectionButton")
					.OnCheckStateChanged(this, &SNiagaraHierarchySection::OnSectionCheckChanged)
					.IsChecked(this, &SNiagaraHierarchySection::GetSectionCheckState)
					.Padding(FMargin(8.f, 4.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2.f)
						[
							SNew(SImage).Image(SectionViewModel->GetSectionImage() != nullptr ? SectionViewModel->GetSectionImage() : FStyleDefaults::GetNoBrush())
						]
						+ SHorizontalBox::Slot()
						[
							SAssignNew(InlineEditableTextBlock, SInlineEditableTextBlock)
							.Visibility(EVisibility::HitTestInvisible)
							.Text(this, &SNiagaraHierarchySection::GetText)
							.OnTextCommitted(this, &SNiagaraHierarchySection::OnRenameSection)
							.OnVerifyTextChanged(this, &SNiagaraHierarchySection::OnVerifySectionRename)
							.IsSelected(this, &SNiagaraHierarchySection::IsSectionSelected)
							.IsReadOnly(this, &SNiagaraHierarchySection::IsSectionReadOnly)
						]
					]
				])
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SNiagaraSectionDragDropTarget, SectionViewModel, EItemDropZone::BelowItem)
				.DropTargetArgs(RightDropTargetArgs)				
			]
		];		
	}
	// if this section doesn't represent data, it's the "All" widget
	else
	{
		ChildSlot
		[
			SNew(SDropTarget)
			.OnAllowDrop(this, &SNiagaraHierarchySection::OnCanAcceptDrop, EItemDropZone::OntoItem)
			.OnDropped(this, &SNiagaraHierarchySection::OnDroppedOn, EItemDropZone::OntoItem)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "DetailsView.SectionButton")
				.OnCheckStateChanged(this, &SNiagaraHierarchySection::OnSectionCheckChanged)
				.IsChecked(this, &SNiagaraHierarchySection::GetSectionCheckState)
				.Padding(FMargin(8.f, 4.f))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AllSection", "All"))
				]
			]			
		];
	}
}

SNiagaraHierarchySection::~SNiagaraHierarchySection()
{
	SectionViewModel.Reset();
}

void SNiagaraHierarchySection::TryEnterEditingMode() const
{
	if(SectionViewModel.IsValid() && SectionViewModel->CanRename())
	{
		InlineEditableTextBlock->EnterEditingMode();
	}
}

TSharedPtr<FNiagaraHierarchySectionViewModel> SNiagaraHierarchySection::GetSectionViewModel()
{
	return SectionViewModel;
}

bool SNiagaraHierarchySection::OnCanAcceptDrop(TSharedPtr<FDragDropOperation> DragDropOperation, EItemDropZone ItemDropZone) const
{
	if(bForbidDropOn)
	{
		return false;
	}

	if(DragDropOperation->IsOfType<FNiagaraHierarchyDragDropOp>())
	{
		TSharedPtr<FNiagaraHierarchyDragDropOp> HierarchyDragDropOp = StaticCastSharedPtr<FNiagaraHierarchyDragDropOp>(DragDropOperation);
		TSharedPtr<FNiagaraHierarchyItemViewModelBase> DraggedItem = HierarchyDragDropOp->GetDraggedItem().Pin();
		
		if(SectionViewModel.IsValid())
		{
			return SectionViewModel->CanDropOn(DraggedItem, ItemDropZone).bCanPerform;
		}
		
		// for the All section which has no valid view model, we simply do a check if the sections of the dragged categories are different
		if(DragDropOperation->IsOfType<FNiagaraHierarchyDragDropOp>())
		{
			if(const UNiagaraHierarchyCategory* Category = Cast<UNiagaraHierarchyCategory>(DraggedItem->GetData()))
			{
				return TryGetSectionData() != Category->GetSection();
			}
		}
	}

	return false;
}

FReply SNiagaraHierarchySection::OnDroppedOn(const FGeometry&, const FDragDropEvent& DragDropEvent, EItemDropZone DropZone) const
{
	bDraggedOn = false;
	if(TSharedPtr<FNiagaraHierarchyDragDropOp> HierarchyDragDropOp = DragDropEvent.GetOperationAs<FNiagaraHierarchyDragDropOp>())
	{
		TSharedPtr<FNiagaraHierarchyItemViewModelBase> DraggedItem = HierarchyDragDropOp->GetDraggedItem().Pin();
		if(SectionViewModel.IsValid())
		{
			SectionViewModel->OnDroppedOn(DraggedItem, DropZone);
			return FReply::Handled();
		}
		
		if(UNiagaraHierarchyCategory* Category = Cast<UNiagaraHierarchyCategory>(DraggedItem->GetDataMutable()))
		{
			Category->SetSection(nullptr);
			TArray<UNiagaraHierarchyCategory*> ChildrenCategories;
			Category->GetChildrenOfType<>(ChildrenCategories, true);

			for(UNiagaraHierarchyCategory* ChildCategory : ChildrenCategories)
			{
				ChildCategory->SetSection(nullptr);
			}

			// we only need to reparent if the parent isn't already the root. This stops unnecessary reordering
			if(HierarchyDragDropOp->GetDraggedItem().Pin()->GetParent() != HierarchyViewModel->GetHierarchyRootViewModel())
			{
				HierarchyViewModel->GetHierarchyRootViewModel()->ReparentToThis(HierarchyDragDropOp->GetDraggedItem().Pin());
			}
		
			HierarchyViewModel->RefreshHierarchyView();

			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

FReply SNiagaraHierarchySection::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if(SectionViewModel.IsValid())
	{
		// we handle the event here so we can react on mouse button up
		if(MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton))
		{
			return FReply::Handled();
		}
		else if(MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
		{			
			OnSectionActivatedDelegate.ExecuteIfBound(SectionViewModel);
			return FReply::Handled().DetectDrag(AsShared(), EKeys::LeftMouseButton).SetUserFocus(AsShared());	
		}
	}

	return FReply::Unhandled();
}

FReply SNiagaraHierarchySection::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if(SectionViewModel.IsValid() && SectionViewModel->IsForHierarchy())
	{
		if(MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{			
			// Show menu to choose getter vs setter
			FSlateApplication::Get().PushMenu(
				AsShared(),
				FWidgetPath(),
				SummonContextMenu({GetSectionViewModel()}, true, AsShared()),
				FSlateApplication::Get().GetCursorPos(),
				FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)
			);
				
			OnSectionActivatedDelegate.ExecuteIfBound(SectionViewModel);			
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

void SNiagaraHierarchySection::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	bDraggedOn = true;
	if(DragDropEvent.GetOperationAs<FNiagaraHierarchyDragDropOp>() && DragDropEvent.GetOperationAs<FNiagaraSectionDragDropOp>() == nullptr)
	{
		RegisterActiveTimer(1.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SNiagaraHierarchySection::ActivateSectionIfDragging));
	}
}

FReply SNiagaraHierarchySection::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if(MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) && SectionViewModel.IsValid() && SectionViewModel->CanDrag().bCanPerform)
	{
		TSharedRef<FNiagaraSectionDragDropOp> SectionDragDropOp = MakeShared<FNiagaraSectionDragDropOp>(SectionViewModel);
		SectionDragDropOp->Construct();
		return FReply::Handled().BeginDragDrop(SectionDragDropOp);
	}
	
	return FReply::Unhandled();
}

void SNiagaraHierarchySection::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	bDraggedOn = false;
	if(TSharedPtr<FNiagaraHierarchyDragDropOp> HierarchyDragDropOp = DragDropEvent.GetOperationAs<FNiagaraHierarchyDragDropOp>())
	{
		HierarchyDragDropOp->SetDescription(FText::GetEmpty());
	}
}

FReply SNiagaraHierarchySection::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if(SectionViewModel.IsValid() && InKeyEvent.GetKey() == EKeys::Delete && SectionViewModel->CanDelete())
	{
		SectionViewModel->Delete();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SNiagaraHierarchySection::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	//bDraggedOn = false;
	SCompoundWidget::OnMouseLeave(MouseEvent);
}

UNiagaraHierarchySection* SNiagaraHierarchySection::TryGetSectionData() const
{
	return SectionViewModel.IsValid() ? Cast<UNiagaraHierarchySection>(SectionViewModel->GetDataMutable()) : nullptr;
}

FText SNiagaraHierarchySection::GetText() const
{
	return SectionViewModel->GetSectionNameAsText();
}

FText SNiagaraHierarchySection::GetTooltipText() const
{
	return SectionViewModel->GetSectionTooltip();
}

void SNiagaraHierarchySection::OnRenameSection(const FText& Text, ETextCommit::Type CommitType) const
{
	FScopedTransaction Transaction(LOCTEXT("Transaction_Rename_Section", "Renamed hierarchy section"));
	HierarchyViewModel->GetHierarchyRoot()->Modify();
	
	SectionViewModel->Rename(FName(Text.ToString()));
}

bool SNiagaraHierarchySection::OnVerifySectionRename(const FText& NewName, FText& OutTooltip) const
{
	// this function shouldn't be used in case the section isn't valid but we'll make sure regardless
	if(!SectionViewModel.IsValid())
	{
		return false;
	}

	if(SectionViewModel->GetSectionName().ToString() != NewName.ToString())
	{
		TArray<FString> SectionNames;

		SectionNames.Add("All");
		for(auto& Section : HierarchyViewModel->GetHierarchyRootViewModel()->GetSectionViewModels())
		{
			SectionNames.Add(Section->GetSectionName().ToString());
		}

		if(SectionNames.Contains(NewName.ToString()))
		{
			OutTooltip = LOCTEXT("HierarchySectionCantRename_Duplicate", "A section with that name already exists!");
			return false;
		}
	}

	return true;
}

bool SNiagaraHierarchySection::IsSectionSelected() const
{
	return GetSectionCheckState() == ECheckBoxState::Checked ? true : false;
}

bool SNiagaraHierarchySection::IsSectionReadOnly() const
{
	return SectionViewModel.IsValid() ? !SectionViewModel->CanRename() : true;
}

ECheckBoxState SNiagaraHierarchySection::GetSectionCheckState() const
{
	return IsSectionActive.Get();
}

void SNiagaraHierarchySection::OnSectionCheckChanged(ECheckBoxState NewState)
{
	OnSectionActivatedDelegate.ExecuteIfBound(SectionViewModel);
}

EActiveTimerReturnType SNiagaraHierarchySection::ActivateSectionIfDragging(double CurrentTime, float DeltaTime) const
{
	if(bDraggedOn && FSlateApplication::Get().GetDragDroppingContent().IsValid() && FSlateApplication::Get().GetDragDroppingContent()->IsOfType<FNiagaraHierarchyDragDropOp>())
	{
		if(IsSectionSelected() == false)
		{
			OnSectionActivatedDelegate.ExecuteIfBound(SectionViewModel);
		}
	}

	return EActiveTimerReturnType::Stop;
}

void SNiagaraHierarchyEditor::Construct(const FArguments& InArgs, TObjectPtr<UNiagaraHierarchyViewModelBase> InHierarchyViewModel)
{
	HierarchyViewModel = InHierarchyViewModel;
	
	SourceRoot = NewObject<UNiagaraHierarchyRoot>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UNiagaraHierarchyRoot::StaticClass()));
	SourceRootViewModel = HierarchyViewModel->CreateRootViewModelForData(SourceRoot, false);
	SourceRootViewModel->Initialize();
	SourceRootViewModel->AddChildFilter(FNiagaraHierarchyItemViewModelBase::FOnFilterChild::CreateSP(this, &SNiagaraHierarchyEditor::FilterForSourceSection));
	SourceRootViewModel->OnSyncPropagated().BindSP(this, &SNiagaraHierarchyEditor::RequestRefreshSourceViewNextFrame, false);
	SourceRootViewModel->OnSectionsChanged().BindSP(this, &SNiagaraHierarchyEditor::RefreshSectionsView);

	HierarchyViewModel->OnInitialized().BindSP(this, &SNiagaraHierarchyEditor::Reinitialize);
	HierarchyViewModel->OnNavigateToItemInHierarchyRequested().BindSP(this, &SNiagaraHierarchyEditor::NavigateToHierarchyItem);
	HierarchyViewModel->OnRefreshSourceItemsRequested().BindSP(this, &SNiagaraHierarchyEditor::RefreshSourceItems);
	HierarchyViewModel->OnRefreshViewRequested().BindSP(this, &SNiagaraHierarchyEditor::RefreshAllViews);
	HierarchyViewModel->OnRefreshSourceView().BindSP(this, &SNiagaraHierarchyEditor::RefreshSourceView);
	HierarchyViewModel->OnRefreshHierarchyView().BindSP(this, &SNiagaraHierarchyEditor::RefreshHierarchyView);
	HierarchyViewModel->OnRefreshSectionsView().BindSP(this, &SNiagaraHierarchyEditor::RefreshSectionsView);
	HierarchyViewModel->OnHierarchySectionActivated().BindSP(this, &SNiagaraHierarchyEditor::OnHierarchySectionActivated);
	HierarchyViewModel->OnItemAdded().BindSP(this, &SNiagaraHierarchyEditor::OnItemAdded);

	BindToHierarchyRootViewModel();
	
	OnGenerateRowContentWidget = InArgs._OnGenerateRowContentWidget;
	OnGenerateCustomDetailsPanelNameWidget = InArgs._OnGenerateCustomDetailsPanelNameWidget;
	CategoryRowStyle = InArgs._CategoryRowStyle;
	ItemRowStyle = InArgs._ItemRowStyle;
	
	SSplitter::FSlot* DetailsPanelSlot = nullptr;

	TSharedRef<SWidget> AddSectionButton = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.OnClicked_Lambda([this]()
			{
				HierarchyViewModel.Get()->AddSection();
				return FReply::Handled();
			})
			.ButtonStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.HierarchyEditor.ButtonStyle")
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.PlusCircle"))
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[			
					SNew(STextBlock)
					.Text(LOCTEXT("AddSectionLabel","Add Section"))				
				]
			]
		];

	TSharedRef<SWidget> AddCategoryButton = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.OnClicked_Lambda([this]()
			{
				TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>> SelectedItems;
				HierarchyTreeView->GetSelectedItems(SelectedItems);

				if(SelectedItems.Num() == 1 && SelectedItems[0]->GetData()->IsA<UNiagaraHierarchyCategory>())
				{
					HierarchyViewModel.Get()->AddCategory(SelectedItems[0]);
				}
				else
				{
					HierarchyViewModel.Get()->AddCategory(HierarchyViewModel->GetHierarchyRootViewModel());
				}
				return FReply::Handled();
			})
			.ButtonStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.HierarchyEditor.ButtonStyle")
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.PlusCircle"))
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[			
					SNew(STextBlock)
					.Text(LOCTEXT("AddCategoryLabel","Add Category"))				
				]
			]
		];
	
	ChildSlot
	[
		SNew(SBorder)
		.Padding(0.f)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			.PhysicalSplitterHandleSize(2.f)
			+ SSplitter::Slot()
			.Value(0.3f)
			.MinSize(0.1f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2.f)
				[
					SAssignNew(SourceSearchBox, SSearchBox)
					.OnTextChanged(this, &SNiagaraHierarchyEditor::OnSourceSearchTextChanged)
					.OnTextCommitted(this, &SNiagaraHierarchyEditor::OnSourceSearchTextCommitted)
					.OnSearch(SSearchBox::FOnSearch::CreateSP(this, &SNiagaraHierarchyEditor::OnSearchButtonClicked))
					.DelayChangeNotificationsWhileTyping(true)
					.SearchResultData(this, &SNiagaraHierarchyEditor::GetSearchResultData)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2.f)
				[
					SAssignNew(SourceSectionBox, SWrapBox)
					.UseAllottedSize(true)
				]
				+ SVerticalBox::Slot()
				.Padding(1.f, 2.f)
				[
					SAssignNew(SourceTreeView, STreeView<TSharedPtr<FNiagaraHierarchyItemViewModelBase>>)
					.TreeItemsSource(&GetSourceItems())
					.OnSelectionChanged(this, &SNiagaraHierarchyEditor::OnSelectionChanged, false)
					.OnGenerateRow(this, &SNiagaraHierarchyEditor::GenerateSourceItemRow)
					.OnGetChildren_UObject(InHierarchyViewModel.Get(), &UNiagaraHierarchyViewModelBase::OnGetChildren)
					.OnItemToString_Debug_UObject(InHierarchyViewModel.Get(), &UNiagaraHierarchyViewModelBase::OnItemToStringDebug)
					.OnContextMenuOpening(this, &SNiagaraHierarchyEditor::SummonContextMenuForSelectedRows, false)
				]
			]
			+ SSplitter::Slot()
			.Value(0.4f)
			.MinSize(0.1f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBorder)
						.Padding(0.f)
						.BorderImage(FAppStyle::Get().GetBrush("Brushes.Header"))
						[
							AddSectionButton
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(1.f)
					[
						SAssignNew(HierarchySectionBox, SWrapBox)
						.UseAllottedSize(true)
					]				
				]
				+ SVerticalBox::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBorder)
						.Padding(0.f)
						.BorderImage(FAppStyle::Get().GetBrush("Brushes.Header"))
						[
							AddCategoryButton
						]
					]
					+ SVerticalBox::Slot()
					.FillHeight(0.1f)
					.Padding(1.f, 4.f, 1.f, 0.f)
					[
						SNew(SDropTarget)
						.OnDropped(this, &SNiagaraHierarchyEditor::HandleHierarchyRootDrop)
						.OnAllowDrop(this, &SNiagaraHierarchyEditor::OnCanDropOnRoot)
						.OnDragEnter(this, &SNiagaraHierarchyEditor::OnRootDragEnter)
						.OnDragLeave(this, &SNiagaraHierarchyEditor::OnRootDragLeave)
						[
							SNew(SBorder)
							.Padding(0.f)
							.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
							[
								SNew(SBorder)
								.Padding(1.f)
								.BorderImage(FAppStyle::GetBrush("DashedBorder"))
								.BorderBackgroundColor(FLinearColor(0.2f, 0.2f, 0.2f, 0.5f))
								[
									SNew(SBox)
									.HAlign(HAlign_Center)
									.VAlign(VAlign_Center)
									[
										SNew(SImage)
										.Image(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.HierarchyEditor.RootDropIcon"))
										.ColorAndOpacity(this, &SNiagaraHierarchyEditor::GetRootIconColor)
									]
								]
							]
						]
					]
					+ SVerticalBox::Slot()
					.Padding(1.f, 0.f)
					[
						SAssignNew(HierarchyTreeView, STreeView<TSharedPtr<FNiagaraHierarchyItemViewModelBase>>)
						.TreeItemsSource(&InHierarchyViewModel->GetHierarchyItems())
						.OnSelectionChanged(this, &SNiagaraHierarchyEditor::OnSelectionChanged, true)
						.OnGenerateRow(this, &SNiagaraHierarchyEditor::GenerateHierarchyItemRow)
						.OnGetChildren_UObject(InHierarchyViewModel.Get(), &UNiagaraHierarchyViewModelBase::OnGetChildren)
						.OnItemToString_Debug_UObject(InHierarchyViewModel.Get(), &UNiagaraHierarchyViewModelBase::OnItemToStringDebug)
						.OnContextMenuOpening(this, &SNiagaraHierarchyEditor::SummonContextMenuForSelectedRows, true)
					]
				]
			]
			+ SSplitter::Slot()
			.Value(0.3f)
			.MinSize(0.1f)
			.Expose(DetailsPanelSlot)		
		]
	];

	if(InHierarchyViewModel->SupportsDetailsPanel())
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::ENameAreaSettings::ObjectsUseNameArea;
		DetailsViewArgs.bShowObjectLabel = false;
		DetailsViewArgs.bAllowSearch = false;
		DetailsViewArgs.NotifyHook = this;

		DetailsPanel = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
		
		if(OnGenerateCustomDetailsPanelNameWidget.IsBound())
		{
			TSharedRef<SWidget> CustomDetailsPanelNameWidget = OnGenerateCustomDetailsPanelNameWidget.Execute(nullptr);
			DetailsPanel->SetNameAreaCustomContent(CustomDetailsPanelNameWidget);
		}

		DetailsPanel->SetIsPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateSP(this, &SNiagaraHierarchyEditor::IsDetailsPanelEditingAllowed));
		
		for(auto& Customizations : HierarchyViewModel->GetInstanceCustomizations())
		{
			DetailsPanel->RegisterInstancedCustomPropertyLayout(Customizations.Key, Customizations.Value);
		}
		
		DetailsPanelSlot->AttachWidget(DetailsPanel.ToSharedRef());
	}
		
	HierarchyViewModel->GetCommands()->MapAction(FGenericCommands::Get().Rename,
		FExecuteAction::CreateSP(this, &SNiagaraHierarchyEditor::RequestRenameSelectedItem),
		FCanExecuteAction::CreateSP(this, &SNiagaraHierarchyEditor::CanRequestRenameSelectedItem));

	HierarchyViewModel->GetCommands()->MapAction(FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SNiagaraHierarchyEditor::DeleteSelectedHierarchyItems),
		FCanExecuteAction::CreateSP(this, &SNiagaraHierarchyEditor::CanDeleteSelectedHierarchyItems),
		FIsActionChecked(), FIsActionButtonVisible::CreateSP(this, &SNiagaraHierarchyEditor::CanDeleteSelectedHierarchyItems));

	// HierarchyViewModel->GetCommands()->MapAction(FNiagaraHierarchyEditorCommands::Get().RenameSection,
	// 	FExecuteAction::CreateSP(this, &SNiagaraHierarchy::DeleteActiveSection),
	// 	FCanExecuteAction::CreateSP(this, &SNiagaraHierarchy::CanDeleteActiveSection),
	// 	FIsActionChecked(), FIsActionButtonVisible::CreateSP(this, &SNiagaraHierarchy::CanDeleteActiveSection));
	
	HierarchyViewModel->GetCommands()->MapAction(FNiagaraHierarchyEditorCommands::Get().DeleteSection,
		FExecuteAction::CreateSP(this, &SNiagaraHierarchyEditor::DeleteActiveSection),
		FCanExecuteAction::CreateSP(this, &SNiagaraHierarchyEditor::CanDeleteActiveSection),
		FIsActionChecked(), FIsActionButtonVisible::CreateSP(this, &SNiagaraHierarchyEditor::CanDeleteActiveSection));
	
	HierarchyViewModel->ForceFullRefresh();
}

SNiagaraHierarchyEditor::~SNiagaraHierarchyEditor()
{
	SourceSearchResults.Empty();
	FocusedSearchResult.Reset();
	
	ClearSourceItems();
	
	if(HierarchyViewModel.IsValid())
	{
		HierarchyViewModel->OnInitialized().Unbind();
		HierarchyViewModel->OnNavigateToItemInHierarchyRequested().Unbind();
		HierarchyViewModel->OnRefreshSourceItemsRequested().Unbind();
		HierarchyViewModel->OnRefreshViewRequested().Unbind();
		HierarchyViewModel->OnRefreshSourceView().Unbind();
		HierarchyViewModel->OnRefreshHierarchyView().Unbind();
		HierarchyViewModel->OnRefreshSectionsView().Unbind();
		HierarchyViewModel->OnHierarchySectionActivated().Unbind();
		HierarchyViewModel->OnItemAdded().Unbind();

		UnbindFromHierarchyRootViewModel();
	
		HierarchyViewModel->GetCommands()->UnmapAction(FGenericCommands::Get().Delete);
		HierarchyViewModel->GetCommands()->UnmapAction(FGenericCommands::Get().Rename);			
	}
	
	SourceRootViewModel->OnSyncPropagated().Unbind();
	SourceRootViewModel->OnSectionsChanged().Unbind();
	SourceRootViewModel.Reset();
	SourceRoot->ConditionalBeginDestroy();
	SourceRoot = nullptr;
}

void SNiagaraHierarchyEditor::RefreshSourceItems()
{
	HierarchyViewModel->PrepareSourceItems(SourceRoot, SourceRootViewModel);
	SourceRootViewModel->SyncViewModelsToData();
	RefreshSourceView(false);
	RefreshSectionsView();
}

void SNiagaraHierarchyEditor::RefreshAllViews(bool bFullRefresh)
{
	RefreshSourceView(bFullRefresh);
	RefreshHierarchyView(bFullRefresh);
	RefreshSectionsView();
}

void SNiagaraHierarchyEditor::RequestRefreshAllViewsNextFrame(bool bFullRefresh)
{
	RequestRefreshSourceViewNextFrame(bFullRefresh);
	RequestRefreshHierarchyViewNextFrame(bFullRefresh);
	RequestRefreshSectionsViewNextFrame();
}

FReply SNiagaraHierarchyEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return HierarchyViewModel->GetCommands()->ProcessCommandBindings(InKeyEvent) ? FReply::Handled() : FReply::Unhandled();
}

FReply SNiagaraHierarchyEditor::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// we catch any mouse button down event so that we can continue using our commands
	return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse, true);
}

FReply SNiagaraHierarchyEditor::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse, true);
}

TSharedPtr<SWidget> SNiagaraHierarchyEditor::SummonContextMenuForSelectedRows(bool bFromHierarchy) const
{
	TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>> ViewModels;
	if(bFromHierarchy)
	{
		HierarchyTreeView->GetSelectedItems(ViewModels);
	}
	else
	{
		SourceTreeView->GetSelectedItems(ViewModels);
	}

	if(ViewModels.IsEmpty())
	{
		return nullptr;
	}
	
	return SummonContextMenu(ViewModels, bFromHierarchy, AsShared());
}

void SNiagaraHierarchyEditor::RefreshSourceView(bool bFullRefresh) const
{
	SourceTreeView->SetTreeItemsSource(&GetSourceItems());
	if(bFullRefresh)
	{
		SourceTreeView->RebuildList();
	}
	else
	{
		SourceTreeView->RequestTreeRefresh();
	}
}

void SNiagaraHierarchyEditor::RequestRefreshSourceViewNextFrame(bool bFullRefresh)
{
	if(!RefreshSourceViewNextFrameHandle.IsValid())
	{
		RefreshSourceViewNextFrameHandle = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this, bFullRefresh](double CurrentTime, float DeltaTime)
		{
			RefreshSourceView(bFullRefresh);
			RefreshSourceViewNextFrameHandle.Reset();
			return EActiveTimerReturnType::Stop;
		}));
	}
}

void SNiagaraHierarchyEditor::RefreshHierarchyView(bool bFullRefresh) const
{
	// the top layer objects might have changed due to filtering. We need to refresh these too.
	HierarchyTreeView->SetTreeItemsSource(&HierarchyViewModel->GetHierarchyItems());
	if(bFullRefresh)
	{
		HierarchyTreeView->RebuildList();
	}
	else
	{
		HierarchyTreeView->RequestTreeRefresh();
	}
}

void SNiagaraHierarchyEditor::RequestRefreshHierarchyViewNextFrame(bool bFullRefresh)
{
	if(!RefreshHierarchyViewNextFrameHandle.IsValid())
	{
		RefreshHierarchyViewNextFrameHandle = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this, bFullRefresh](double CurrentTime, float DeltaTime)
		{
			RefreshHierarchyView(bFullRefresh);
			RefreshHierarchyViewNextFrameHandle.Reset();
			return EActiveTimerReturnType::Stop;
		}));
	}
}

void SNiagaraHierarchyEditor::RefreshSectionsView()
{
	SourceSectionBox->ClearChildren();
	HierarchySectionBox->ClearChildren();

	for (TSharedPtr<FNiagaraHierarchySectionViewModel>& SourceSection : SourceRootViewModel->GetSectionViewModels())
	{
		TSharedPtr<SNiagaraHierarchySection> SectionWidget = SNew(SNiagaraHierarchySection, SourceSection, HierarchyViewModel)
		.IsSectionActive_Lambda([this, SourceSection]()
		{
			return GetActiveSourceSection() == SourceSection ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnSectionActivated_Lambda([this](TSharedPtr<FNiagaraHierarchySectionViewModel> SectionViewModel)
		{
			SetActiveSourceSection(SectionViewModel);
		});
		
		SourceSectionBox->AddSlot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SectionWidget.ToSharedRef()		
		];
	}

	if(SourceRootViewModel->GetSectionViewModels().Num() > 0)
	{
		TSharedPtr<SNiagaraHierarchySection> DefaultSourceSection = SNew(SNiagaraHierarchySection, nullptr, HierarchyViewModel)
		.IsSectionActive_Lambda([this]()
		{
			return GetActiveSourceSection() == nullptr ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnSectionActivated(this, &SNiagaraHierarchyEditor::SetActiveSourceSection)
		// we forbid drop on here as the 'All' sections don't have a valid view model to determine it instead
		.bForbidDropOn(true);

		SourceSectionBox->AddSlot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			DefaultSourceSection.ToSharedRef()
		];
	}
	
	for (TSharedPtr<FNiagaraHierarchySectionViewModel>& HierarchySection : HierarchyViewModel->GetHierarchyRootViewModel()->GetSectionViewModels())
	{
		TSharedPtr<SNiagaraHierarchySection> SectionWidget = SNew(SNiagaraHierarchySection, HierarchySection, HierarchyViewModel)
		.IsSectionActive_Lambda([this, HierarchySection]()
		{
			return HierarchyViewModel->GetActiveHierarchySection() == HierarchySection ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnSectionActivated_Lambda([this](TSharedPtr<FNiagaraHierarchySectionViewModel> SectionViewModel)
		{
			HierarchyViewModel->SetActiveHierarchySection(SectionViewModel);
		});
		
		HierarchySectionBox->AddSlot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SectionWidget.ToSharedRef()		
		];
	}
	
	TSharedPtr<SNiagaraHierarchySection> DefaultHierarchySection = SNew(SNiagaraHierarchySection, nullptr, HierarchyViewModel)
	.IsSectionActive_Lambda([this]()
	{
		return HierarchyViewModel->GetActiveHierarchySection() == nullptr ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	})
	.OnSectionActivated_Lambda([this](TSharedPtr<FNiagaraHierarchySectionViewModel> SectionViewModel)
	{
		HierarchyViewModel->SetActiveHierarchySection(SectionViewModel);
	});

	HierarchySectionBox->AddSlot()
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		DefaultHierarchySection.ToSharedRef()
	];
}

void SNiagaraHierarchyEditor::RequestRefreshSectionsViewNextFrame()
{
	if(!RefreshSectionsViewNextFrameHandle.IsValid())
	{
		RefreshSectionsViewNextFrameHandle = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double CurrentTime, float DeltaTime)
		{
			RefreshSectionsView();
			RefreshSectionsViewNextFrameHandle.Reset();
			return EActiveTimerReturnType::Stop;
		}));
	}
}

void SNiagaraHierarchyEditor::NavigateToHierarchyItem(FNiagaraHierarchyIdentity Identity) const
{
	if(TSharedPtr<FNiagaraHierarchyItemViewModelBase> ViewModel = HierarchyViewModel->GetHierarchyRootViewModel()->FindViewModelForChild(Identity, true))
	{
		NavigateToHierarchyItem(ViewModel);
	}
}

void SNiagaraHierarchyEditor::NavigateToHierarchyItem(TSharedPtr<FNiagaraHierarchyItemViewModelBase> Item) const
{
	TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>> ParentChain;
	for(TWeakPtr<FNiagaraHierarchyItemViewModelBase> Parent = Item->GetParent(); Parent.IsValid(); Parent = Parent.Pin()->GetParent())
	{
		ParentChain.Add(Parent.Pin());
	}

	for(int32 ParentIndex = ParentChain.Num()-1; ParentIndex >= 0; ParentIndex--)
	{
		HierarchyTreeView->SetItemExpansion(ParentChain[ParentIndex], true);
	}
		
	HierarchyTreeView->SetSelection(Item);
	HierarchyTreeView->RequestScrollIntoView(Item);
}

bool SNiagaraHierarchyEditor::IsItemSelected(TSharedPtr<FNiagaraHierarchyItemViewModelBase> Item) const
{
	return HierarchyTreeView->IsItemSelected(Item);
}

TSharedRef<ITableRow> SNiagaraHierarchyEditor::GenerateSourceItemRow(TSharedPtr<FNiagaraHierarchyItemViewModelBase> HierarchyItem, const TSharedRef<STableViewBase>& TableViewBase)
{
	return SNew(STableRow<TSharedPtr<FNiagaraHierarchyItemViewModelBase>>, TableViewBase)
	.Style(HierarchyItem->GetData()->IsA<UNiagaraHierarchyItem>() ? ItemRowStyle : CategoryRowStyle)
	.OnDragDetected(HierarchyItem.ToSharedRef(), &FNiagaraHierarchyItemViewModelBase::OnDragDetected, true)
	.Padding(FMargin(2))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(1.f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.HeightOverride(10.f)
			.WidthOverride(10.f)
			.Visibility_Lambda([HierarchyItem, Identity = HierarchyItem->GetData()->GetPersistentIdentity(), this]()
			{
				TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>> AllChildren;
				HierarchyItem->GetChildrenViewModelsForType<UNiagaraHierarchyItemBase, FNiagaraHierarchyItemViewModelBase>(AllChildren, true);

				bool bCanDrag = HierarchyItem->GetHierarchyViewModel()->GetHierarchyRootViewModel()->FindViewModelForChild(HierarchyItem->GetData()->GetPersistentIdentity(), true) == nullptr;			

				if(bCanDrag)
				{
					for(TSharedPtr<FNiagaraHierarchyItemViewModelBase>& ItemViewModel : AllChildren)
					{
						if(HierarchyItem->GetHierarchyViewModel()->GetHierarchyRootViewModel()->FindViewModelForChild(ItemViewModel->GetData()->GetPersistentIdentity(), true) != nullptr)
						{
							bCanDrag = false;
							break;
						}
					}
				}

				return bCanDrag ? EVisibility::Collapsed : EVisibility::Visible;
			})
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.Lock"))
				.ToolTipText(LOCTEXT("CantDragItemAlreadyInHierarchyTooltip", "This item already exists within the hierarchy and can not be dragged. Drag the existing one within the hierarchy directly."))
			]
		]
		+ SHorizontalBox::Slot()
		[
			OnGenerateRowContentWidget.Execute(HierarchyItem.ToSharedRef())
		]
	];
}

TSharedRef<ITableRow> SNiagaraHierarchyEditor::GenerateHierarchyItemRow(TSharedPtr<FNiagaraHierarchyItemViewModelBase> HierarchyItem, const TSharedRef<STableViewBase>& TableViewBase)
{
	return SNew(STableRow<TSharedPtr<FNiagaraHierarchyItemViewModelBase>>, TableViewBase)
	.Style(HierarchyItem->GetData()->IsA<UNiagaraHierarchyItem>() ? ItemRowStyle : CategoryRowStyle)
	.OnAcceptDrop(HierarchyItem.ToSharedRef(), &FNiagaraHierarchyItemViewModelBase::OnDroppedOnRow)
	.OnCanAcceptDrop(HierarchyItem.ToSharedRef(), &FNiagaraHierarchyItemViewModelBase::OnCanRowAcceptDrop)
	.OnDragDetected(HierarchyItem.ToSharedRef(), &FNiagaraHierarchyItemViewModelBase::OnDragDetected, false)
	.OnDragLeave(HierarchyItem.ToSharedRef(), &FNiagaraHierarchyItemViewModelBase::OnRowDragLeave)
	.Padding(FMargin(2))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(1.f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.HeightOverride(10.f)
			.WidthOverride(10.f)
			.Visibility_Lambda([HierarchyItem]()
			{
				return HierarchyItem->IsEditableByUser().bCanPerform ? EVisibility::Collapsed : EVisibility::Visible;
			})
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.Lock"))
				.ToolTipText_Lambda([HierarchyItem]()
				{
					FNiagaraHierarchyItemViewModelBase::FCanPerformActionResults IsEditableResults = HierarchyItem->IsEditableByUser();
					if(IsEditableResults.bCanPerform == false)
					{
						return IsEditableResults.CanPerformMessage;
					}

					return FText::GetEmpty();
				})
			]
		]
		+ SHorizontalBox::Slot()
		[
			OnGenerateRowContentWidget.Execute(HierarchyItem.ToSharedRef())
		]
	];
}

bool SNiagaraHierarchyEditor::FilterForSourceSection(TSharedPtr<const FNiagaraHierarchyItemViewModelBase> ItemViewModel) const
{
	if(ActiveSourceSection.IsValid())
	{
		return GetActiveSourceSectionData() == ItemViewModel->GetSection();
	}

	return true;
}

void SNiagaraHierarchyEditor::Reinitialize()
{
	// the hierarchy root view model has been recreated if the summary view model reinitialized. Therefore we update the bindings.
	BindToHierarchyRootViewModel();
	RefreshSourceItems();
	RefreshAllViews(true);
}

void SNiagaraHierarchyEditor::BindToHierarchyRootViewModel()
{
	HierarchyViewModel->GetHierarchyRootViewModel()->OnSyncPropagated().BindSP(this, &SNiagaraHierarchyEditor::RequestRefreshHierarchyViewNextFrame, false);
	HierarchyViewModel->GetHierarchyRootViewModel()->OnSectionsChanged().BindSP(this, &SNiagaraHierarchyEditor::RefreshSectionsView);
	HierarchyViewModel->GetHierarchyRootViewModel()->OnSectionAdded().BindSP(this, &SNiagaraHierarchyEditor::OnHierarchySectionAdded);
	HierarchyViewModel->GetHierarchyRootViewModel()->OnSectionDeleted().BindSP(this, &SNiagaraHierarchyEditor::OnHierarchySectionDeleted);
}

void SNiagaraHierarchyEditor::UnbindFromHierarchyRootViewModel() const
{
	if(HierarchyViewModel->GetHierarchyRootViewModel().IsValid())
	{
		HierarchyViewModel->GetHierarchyRootViewModel()->OnSyncPropagated().Unbind();
		HierarchyViewModel->GetHierarchyRootViewModel()->OnSectionsChanged().Unbind();
		HierarchyViewModel->GetHierarchyRootViewModel()->OnSectionAdded().Unbind();
		HierarchyViewModel->GetHierarchyRootViewModel()->OnSectionDeleted().Unbind();
	}
}

const TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>>& SNiagaraHierarchyEditor::GetSourceItems() const
{
	return SourceRootViewModel->GetFilteredChildren();
}

bool SNiagaraHierarchyEditor::IsDetailsPanelEditingAllowed() const
{
	return SelectedDetailsPanelItemViewModel.IsValid() && SelectedDetailsPanelItemViewModel.Pin()->IsEditableByUser().bCanPerform;
}

void SNiagaraHierarchyEditor::RequestRenameSelectedItem()
{
	TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>> SelectedItems = HierarchyTreeView->GetSelectedItems();

	if(SelectedItems.Num() == 0)
	{
		TSharedPtr<FNiagaraHierarchySectionViewModel> ActiveSection = HierarchyViewModel->GetActiveHierarchySection();
		if(ActiveSection)
		{
			SelectedItems = { ActiveSection };
		}
	}
	
	if(SelectedItems.Num() == 1)
	{
		SelectedItems[0]->RequestRename();
	}
}

bool SNiagaraHierarchyEditor::CanRequestRenameSelectedItem() const
{
	TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>> SelectedItems = HierarchyTreeView->GetSelectedItems();

	if(SelectedItems.Num() == 0)
	{
		TSharedPtr<FNiagaraHierarchySectionViewModel> ActiveSection = HierarchyViewModel->GetActiveHierarchySection();
		if(ActiveSection)
		{
			SelectedItems = { ActiveSection };
		}
	}
	
	if(SelectedItems.Num() == 1)
	{
		return SelectedItems[0]->CanRename();
	}

	return false;
}

void SNiagaraHierarchyEditor::ClearSourceItems() const
{
	SourceRoot->GetChildrenMutable().Empty();
	SourceRoot->GetSectionDataMutable().Empty();
	SourceRootViewModel->GetChildrenMutable().Empty();
	SourceRootViewModel->GetSectionViewModels().Empty();
}

void SNiagaraHierarchyEditor::DeleteItems(TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>> ItemsToDelete) const
{
	TArray<FNiagaraHierarchyIdentity> DeletionIdentities;
	
	for(TSharedPtr<FNiagaraHierarchyItemViewModelBase>& SelectedItem : ItemsToDelete)
	{
		DeletionIdentities.Add(SelectedItem->GetData()->GetPersistentIdentity());
	}

	HierarchyViewModel->DeleteItemsWithIdentities(DeletionIdentities);
}

void SNiagaraHierarchyEditor::DeleteSelectedHierarchyItems() const
{	
	TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>> SelectedItems = HierarchyTreeView->GetSelectedItems();
	DeleteItems(SelectedItems);
}

bool SNiagaraHierarchyEditor::CanDeleteSelectedHierarchyItems() const
{
	TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>> SelectedItems = HierarchyTreeView->GetSelectedItems();

	if(SelectedItems.Num() > 0)
	{
		bool bCanDelete = true;
		for(TSharedPtr<FNiagaraHierarchyItemViewModelBase> SelectedItem : SelectedItems)
		{
			bCanDelete &= SelectedItems[0]->CanDelete();
		}

		return bCanDelete;
	}

	return false;
}

void SNiagaraHierarchyEditor::DeleteActiveSection() const
{
	if(TSharedPtr<FNiagaraHierarchySectionViewModel> ActiveHierarchySectionViewModel = HierarchyViewModel->GetActiveHierarchySection())
	{
		DeleteItems({ActiveHierarchySectionViewModel});
	}
}

bool SNiagaraHierarchyEditor::CanDeleteActiveSection() const
{
	return HierarchyViewModel->GetActiveHierarchySection() != nullptr;
}

void SNiagaraHierarchyEditor::OnItemAdded(TSharedPtr<FNiagaraHierarchyItemViewModelBase> AddedItem)
{
	// when a new item is created (opposed to dragged & dropped from source view, i.e. only categories so far)
	// we make sure to request a tree refresh, select the row, and request a pending rename since the widget will created a frame later
	if(AddedItem->GetData()->IsA<UNiagaraHierarchyItem>() || AddedItem->GetData()->IsA<UNiagaraHierarchyCategory>())
	{
		HierarchyTreeView->RequestTreeRefresh();
		NavigateToHierarchyItem(AddedItem);
	}
	else if(AddedItem->GetData()->IsA<UNiagaraHierarchySection>())
	{
		RefreshSectionsView();
	}

	AddedItem->RequestRenamePending();
}

void SNiagaraHierarchyEditor::OnHierarchySectionActivated(TSharedPtr<FNiagaraHierarchySectionViewModel> Section)
{
	OnSelectionChanged(Section, ESelectInfo::Direct, true);
}

void SNiagaraHierarchyEditor::OnSourceSectionActivated(TSharedPtr<FNiagaraHierarchySectionViewModel> Section)
{
	OnSelectionChanged(Section, ESelectInfo::Direct, false);
	RunSourceSearch();
}

void SNiagaraHierarchyEditor::OnHierarchySectionAdded(TSharedPtr<FNiagaraHierarchySectionViewModel> AddedSection)
{
	HierarchyViewModel->SetActiveHierarchySection(AddedSection);
	AddedSection->RequestRenamePending();
}

void SNiagaraHierarchyEditor::OnHierarchySectionDeleted(TSharedPtr<FNiagaraHierarchySectionViewModel> DeletedSection)
{
	if(HierarchyViewModel->GetActiveHierarchySection() == DeletedSection)
	{
		HierarchyViewModel->SetActiveHierarchySection(nullptr);
	}
}

void SNiagaraHierarchyEditor::SetActiveSourceSection(TSharedPtr<FNiagaraHierarchySectionViewModel> Section)
{
	ActiveSourceSection = Section;	
	RefreshSourceView(true);
	OnSourceSectionActivated(Section);
}

TSharedPtr<FNiagaraHierarchySectionViewModel> SNiagaraHierarchyEditor::GetActiveSourceSection() const
{
	return ActiveSourceSection.IsValid() ? ActiveSourceSection.Pin() : nullptr;
}

UNiagaraHierarchySection* SNiagaraHierarchyEditor::GetActiveSourceSectionData() const
{
	return ActiveSourceSection.IsValid() ? ActiveSourceSection.Pin()->GetDataMutable<UNiagaraHierarchySection>() : nullptr;
}

void SNiagaraHierarchyEditor::OnSelectionChanged(TSharedPtr<FNiagaraHierarchyItemViewModelBase> HierarchyItem, ESelectInfo::Type Type, bool bFromHierarchy) const
{
	SelectedDetailsPanelItemViewModel.Reset();
	if(DetailsPanel.IsValid())
	{
		if(HierarchyItem.IsValid() && HierarchyItem->AllowEditingInDetailsPanel())
		{
			// when se select a section, and the previous item selection is no longer available due to it, we would get a selection refresh next tick
			// to wipe out the current selection. We want to avoid that, so we manually clear the selected items in that case.
			if(HierarchyItem->GetData()->IsA<UNiagaraHierarchySection>())
			{
				HierarchyTreeView->ClearSelection();
			}

			// we clear the selection of the other tree view to avoid 
			if(bFromHierarchy)
			{
				SourceTreeView->ClearSelection();
			}
			else
			{
				HierarchyTreeView->ClearSelection();
			}
			
			UObject* DataForEditing = HierarchyItem->GetDataForEditing();
			DataForEditing->SetFlags(RF_Transactional);

			// we make sure the object we are editing is transactional
			DetailsPanel->SetObject(DataForEditing);
			SelectedDetailsPanelItemViewModel = HierarchyItem;
		}
		else
		{
			SelectedDetailsPanelItemViewModel.Reset();
			DetailsPanel->SetObject(nullptr);
		}
	}
	
	if(DetailsPanel.IsValid() && OnGenerateCustomDetailsPanelNameWidget.IsBound() && SelectedDetailsPanelItemViewModel.IsValid())
	{
		TSharedRef<SWidget> NameWidget = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush("Icons.Lock"))
			.Visibility(SelectedDetailsPanelItemViewModel.Pin()->IsEditableByUser().bCanPerform ? EVisibility::Collapsed : EVisibility::Visible)
			.ToolTipText(SelectedDetailsPanelItemViewModel.Pin()->IsEditableByUser().CanPerformMessage)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.f)
		[
			OnGenerateCustomDetailsPanelNameWidget.Execute(SelectedDetailsPanelItemViewModel.Pin())
		];
		
		DetailsPanel->SetNameAreaCustomContent(NameWidget);
	}
}

void SNiagaraHierarchyEditor::RunSourceSearch()
{
	if(!SourceSearchBox->GetText().IsEmpty())
	{
		OnSourceSearchTextChanged(SourceSearchBox->GetText());
	}
}

void SNiagaraHierarchyEditor::OnSourceSearchTextChanged(const FText& Text)
{
	SourceSearchResults.Empty();
	FocusedSearchResult.Reset();
	SourceTreeView->ClearSelection();

	if(!Text.IsEmpty())
	{
		FString TextAsString = Text.ToString();;

		TArray<FSearchItem> SearchItems;
		GenerateSearchItems(SourceRootViewModel.ToSharedRef(), {}, SearchItems);
		
		for(const FSearchItem& SearchItem : SearchItems)
		{
			for(const FString& SearchTerm : SearchItem.GetEntry()->GetSearchTerms())
			{
				if(SearchTerm.Contains(TextAsString))
				{
					SourceSearchResults.Add(SearchItem);
				}
			}
		}

		ExpandSourceSearchResults();
		SelectNextSourceSearchResult();
	}
	else
	{
		SourceTreeView->ClearExpandedItems();
	}
}

void SNiagaraHierarchyEditor::OnSourceSearchTextCommitted(const FText& Text, ETextCommit::Type CommitType)
{
	bool bIsShiftDown = FSlateApplication::Get().GetModifierKeys().IsShiftDown();
	if(CommitType == ETextCommit::OnEnter)
	{
		if(bIsShiftDown == false)
		{
			SelectNextSourceSearchResult();
		}
		else
		{
			SelectPreviousSourceSearchResult();
		}
	}
}

void SNiagaraHierarchyEditor::OnSearchButtonClicked(SSearchBox::SearchDirection SearchDirection)
{
	if(SearchDirection == SSearchBox::Next)
	{
		SelectNextSourceSearchResult();
	}
	else
	{
		SelectPreviousSourceSearchResult();
	}
}

void SNiagaraHierarchyEditor::GenerateSearchItems(TSharedRef<FNiagaraHierarchyItemViewModelBase> Root, TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>> ParentChain, TArray<FSearchItem>& OutSearchItems)
{
	const TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>> FilteredChildren = Root->GetFilteredChildren();
	ParentChain.Add(Root);
	OutSearchItems.Add(FSearchItem{ParentChain});
	for(TSharedPtr<FNiagaraHierarchyItemViewModelBase> Child : FilteredChildren)
	{
		GenerateSearchItems(Child.ToSharedRef(), ParentChain, OutSearchItems);
	}
}

void SNiagaraHierarchyEditor::ExpandSourceSearchResults()
{
	SourceTreeView->ClearExpandedItems();

	for(FSearchItem& SearchResult : SourceSearchResults)
	{
		for(TSharedPtr<FNiagaraHierarchyItemViewModelBase>& EntryInPath : SearchResult.Path)
		{
			SourceTreeView->SetItemExpansion(EntryInPath, true);
		}
	}
}

void SNiagaraHierarchyEditor::SelectNextSourceSearchResult()
{
	if(SourceSearchResults.IsEmpty())
	{
		return;
	}
	
	if(!FocusedSearchResult.IsSet())
	{
		FocusedSearchResult = SourceSearchResults[0];
	}
	else
	{
		int32 CurrentSearchResultIndex = SourceSearchResults.Find(FocusedSearchResult.GetValue());
		if(SourceSearchResults.IsValidIndex(CurrentSearchResultIndex+1))
		{
			FocusedSearchResult = SourceSearchResults[CurrentSearchResultIndex+1];
		}
		else
		{
			FocusedSearchResult = SourceSearchResults[0];
		}
	}

	SourceTreeView->ClearSelection();
	SourceTreeView->RequestScrollIntoView(FocusedSearchResult.GetValue().GetEntry());
	SourceTreeView->SetItemSelection(FocusedSearchResult.GetValue().GetEntry(), true);
}

void SNiagaraHierarchyEditor::SelectPreviousSourceSearchResult()
{
	if(SourceSearchResults.IsEmpty())
	{
		return;
	}
	
	if(!FocusedSearchResult.IsSet())
	{
		FocusedSearchResult = SourceSearchResults[0];
	}
	else
	{
		int32 CurrentSearchResultIndex = SourceSearchResults.Find(FocusedSearchResult.GetValue());
		if(SourceSearchResults.IsValidIndex(CurrentSearchResultIndex-1))
		{
			FocusedSearchResult = SourceSearchResults[CurrentSearchResultIndex-1];
		}
		else
		{
			FocusedSearchResult = SourceSearchResults[SourceSearchResults.Num()-1];
		}
	}

	SourceTreeView->ClearSelection();
	SourceTreeView->RequestScrollIntoView(FocusedSearchResult.GetValue().GetEntry());
	SourceTreeView->SetItemSelection(FocusedSearchResult.GetValue().GetEntry(), true);
}

TOptional<SSearchBox::FSearchResultData> SNiagaraHierarchyEditor::GetSearchResultData() const
{
	if(SourceSearchResults.Num() > 0)
	{
		SSearchBox::FSearchResultData SearchResultData;
		SearchResultData.NumSearchResults = SourceSearchResults.Num();

		if(FocusedSearchResult.IsSet())
		{
			// we add one just to make it look nicer as this is merely for cosmetic purposes
			SearchResultData.CurrentSearchResultIndex = SourceSearchResults.Find(FocusedSearchResult.GetValue()) + 1;
		}
		else
		{
			SearchResultData.CurrentSearchResultIndex = INDEX_NONE;
		}

		return SearchResultData;
	}

	return TOptional<SSearchBox::FSearchResultData>();
}

FReply SNiagaraHierarchyEditor::HandleHierarchyRootDrop(const FGeometry& Geometry, const FDragDropEvent& DragDropEvent) const
{
	if(TSharedPtr<FNiagaraHierarchyDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FNiagaraHierarchyDragDropOp>())
	{
		HierarchyViewModel->GetHierarchyRootViewModel()->OnDroppedOn(DragDropOp->GetDraggedItem().Pin(), EItemDropZone::OntoItem);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FNiagaraHierarchyItemViewModelBase::FCanPerformActionResults SNiagaraHierarchyEditor::CanDropOnRoot(TSharedPtr<FNiagaraHierarchyItemViewModelBase> DraggedItem) const
{
	return HierarchyViewModel->GetHierarchyRootViewModel()->CanDropOnInternal(DraggedItem, EItemDropZone::OntoItem);
}

bool SNiagaraHierarchyEditor::OnCanDropOnRoot(TSharedPtr<FDragDropOperation> DragDropOperation) const
{
	if(DragDropOperation->IsOfType<FNiagaraHierarchyDragDropOp>())
	{
		TSharedPtr<FNiagaraHierarchyDragDropOp> HierarchyDragDropOp = StaticCastSharedPtr<FNiagaraHierarchyDragDropOp>(DragDropOperation);
		TSharedPtr<FNiagaraHierarchyItemViewModelBase> DraggedItem = HierarchyDragDropOp->GetDraggedItem().Pin();
		return CanDropOnRoot(DraggedItem).bCanPerform;
	}
	
	return false;
}

void SNiagaraHierarchyEditor::OnRootDragEnter(const FDragDropEvent& DragDropEvent) const
{
	if(TSharedPtr<FNiagaraHierarchyDragDropOp> HierarchyDragDropOp = DragDropEvent.GetOperationAs<FNiagaraHierarchyDragDropOp>())
	{
		FNiagaraHierarchyItemViewModelBase::FCanPerformActionResults CanPerformActionResults = CanDropOnRoot(HierarchyDragDropOp->GetDraggedItem().Pin());
		HierarchyDragDropOp->SetDescription(CanPerformActionResults.CanPerformMessage);
	}
}

void SNiagaraHierarchyEditor::OnRootDragLeave(const FDragDropEvent& DragDropEvent) const
{
	if(TSharedPtr<FNiagaraHierarchyDragDropOp> HierarchyDragDropOp = DragDropEvent.GetOperationAs<FNiagaraHierarchyDragDropOp>())
	{
		HierarchyDragDropOp->SetDescription(FText::GetEmpty());
	}
}

FSlateColor SNiagaraHierarchyEditor::GetRootIconColor() const
{
	if(FSlateApplication::Get().IsDragDropping())
	{
		if(FSlateApplication::Get().GetDragDroppingContent()->IsOfType<FNiagaraHierarchyDragDropOp>())
		{
			TSharedPtr<FNiagaraHierarchyDragDropOp> HierarchyDragDropOp = StaticCastSharedPtr<FNiagaraHierarchyDragDropOp>(FSlateApplication::Get().GetDragDroppingContent());
			if(CanDropOnRoot(HierarchyDragDropOp->GetDraggedItem().Pin()).bCanPerform)
			{
				return FLinearColor(0.8f, 0.8f, 0.8f, 0.8f);
			}
		}
	}

	return FLinearColor(0.2f, 0.2f, 0.2f, 0.5f);
}

FString SNiagaraHierarchyEditor::GetReferencerName() const
{
	return TEXT("Niagara Hierarchy");
}

void SNiagaraHierarchyEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(SourceRoot);
}

void SNiagaraHierarchyEditor::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent,	FProperty* PropertyThatChanged)
{
	HierarchyViewModel->OnHierarchyPropertiesChanged().Broadcast();
}

void SNiagaraSectionDragDropTarget::Construct(const FArguments& InArgs,TSharedPtr<FNiagaraHierarchySectionViewModel> InOwningSection, EItemDropZone InItemDropZone)
{
	OwningSection = InOwningSection;
	DropZone = InItemDropZone;
	
	SDropTarget::Construct(InArgs._DropTargetArgs);
}

FReply SNiagaraSectionDragDropTarget::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if(TSharedPtr<FNiagaraHierarchyDragDropOp> HierarchyDragDropOp = DragDropEvent.GetOperationAs<FNiagaraHierarchyDragDropOp>())
	{
		TSharedPtr<FNiagaraHierarchyItemViewModelBase> DraggedItem = HierarchyDragDropOp->GetDraggedItem().Pin();
		FNiagaraHierarchyItemViewModelBase::FCanPerformActionResults Results = OwningSection->CanDropOn(DraggedItem, DropZone);
		HierarchyDragDropOp->SetDescription(Results.CanPerformMessage);
	}

	return SDropTarget::OnDragOver(MyGeometry, DragDropEvent);
}

void SNiagaraSectionDragDropTarget::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FNiagaraHierarchyDragDropOp> HierarchyDragDropOp = DragDropEvent.GetOperationAs<FNiagaraHierarchyDragDropOp>();

	if(HierarchyDragDropOp.IsValid())
	{
		HierarchyDragDropOp->SetDescription(FText::GetEmpty());
	}
	
	SDropTarget::OnDragLeave(DragDropEvent);
}

#undef LOCTEXT_NAMESPACE
