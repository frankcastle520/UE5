// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAddVariablesDialog.h"

#include "AddVariableDialogMenuContext.h"
#include "AnimNextRigVMAsset.h"
#include "AnimNextVariableSettings.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "DetailLayoutBuilder.h"
#include "IContentBrowserSingleton.h"
#include "EditorUtils.h"
#include "UncookedOnlyUtils.h"
#include "PropertyBagDetails.h"
#include "PropertyCustomizationHelpers.h"
#include "SPinTypeSelector.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "SSimpleButton.h"
#include "SSimpleComboButton.h"
#include "ToolMenus.h"
#include "DataInterface/AnimNextDataInterface.h"
#include "DataInterface/AnimNextDataInterface_EditorData.h"
#include "Entries/AnimNextDataInterfaceEntry.h"
#include "Entries/AnimNextRigVMAssetEntry.h"
#include "Entries/AnimNextVariableEntry.h"
#include "String/ParseTokens.h"
#include "Widgets/Input/SEditableTextBox.h"

#define LOCTEXT_NAMESPACE "SAddVariablesDialog"

namespace UE::AnimNext::Editor
{

namespace AddVariablesDialog
{
static FName Column_Name(TEXT("Name"));
static FName Column_Type(TEXT("Type"));
static FName SelectLibraryMenuName(TEXT("AnimNext.AddVariablesDialog.SelectedLibraryMenu"));
}

bool SAddVariablesDialog::FVariableToAddEntry::IsValid(FText& OutReason) const
{
	if(Name == NAME_None)
	{
		OutReason = LOCTEXT("InvalidVariableName", "Invalid Variable Name");
		return false;
	}

	if(!Type.IsValid())
	{
		OutReason = LOCTEXT("InvalidVariableType", "Invalid Variable Type");
		return false;
	}

	TSharedPtr<SAddVariablesDialog> PinnedDialog = Dialog.Pin();
	if(PinnedDialog.IsValid())
	{
		TArray<FName> PendingNames;
		PinnedDialog->GetPendingNames(PendingNames);
		for(FName PendingName : PendingNames)
		{
			if(PendingName == Name)
			{
				OutReason = LOCTEXT("DuplicateVariableName", "Duplicate Variable Name");
				return false;
			}
		}

		for(const TSharedRef<FEntry>& Entry : PinnedDialog->RootEntries)
		{
			if(&Entry.Get() == this)
			{
				continue;
			}
			
			switch(Entry->EntryType)
			{
			case EEntryType::Variable:
				if(StaticCastSharedRef<FVariableToAddEntry>(Entry)->Name == Name)
				{
					OutReason = LOCTEXT("DuplicateVariableName", "Duplicate Variable Name");
					return false;
				}
				break;
			case EEntryType::DataInterface:
				for(const TSharedRef<FEntry>& SubEntry : Entry->Children)
				{
					if(&SubEntry.Get() == this)
					{
						continue;
					}

					check(SubEntry->EntryType == EEntryType::Variable);
					if(StaticCastSharedRef<FVariableToAddEntry>(SubEntry)->Name == Name)
					{
						OutReason = LOCTEXT("DuplicateVariableName", "Duplicate Variable Name");
						return false;
					}
				}
				break;
			}
		}
	}

	return true; 
}

bool SAddVariablesDialog::FDataInterfaceToAddEntry::IsValid(FText& OutReason) const
{
	if(DataInterface == nullptr)
	{
		OutReason = LOCTEXT("InvalidDataInterface", "Invalid Data Interface");
		return false;
	}

	return true; 
}

void SAddVariablesDialog::Construct(const FArguments& InArgs, const TArray<UAnimNextRigVMAssetEditorData*>& InAssetEditorDatas)
{
	using namespace AddVariablesDialog;

	OnFilterVariableType = InArgs._OnFilterVariableType;
	AssetEditorDatas = InAssetEditorDatas;

	SWindow::Construct(SWindow::FArguments()
		.Title(LOCTEXT("WindowTitle", "Add Variables"))
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(InArgs._AllowMultiple ? FVector2D(500.f, 500.f) : FVector2D(500.f, 100.f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SNew(SBox)
			.Padding(5.0f)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Left)
				.Padding(0.0f, 5.0f)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.3f, 0.0f)
					[
						SNew(SSimpleButton)
						.Visibility(InArgs._AllowMultiple ? EVisibility::Visible : EVisibility::Collapsed)
						.Text(LOCTEXT("AddVariableButton", "Add Variable"))
						.ToolTipText(LOCTEXT("AddVariableButtonTooltip", "Queue a new variable for adding. New variables will re-use the settings from the last queued variable."))
						.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
						.OnClicked_Lambda([this]()
						{
							AddEntry();
							return FReply::Handled();
						})
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.3f, 0.0f)
					[
						SNew(SSimpleComboButton)
						.Visibility(InArgs._AllowMultiple ? EVisibility::Visible : EVisibility::Collapsed)
						.Text(LOCTEXT("AddDataInterfaceButton", "Add Data Interface"))
						.ToolTipText(LOCTEXT("AddDataInterfaceButtonTooltip", "Select a new data interface for adding."))
						.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
						.OnGetMenuContent_Lambda([this]()
						{
							FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

							FAssetPickerConfig AssetPickerConfig;
							AssetPickerConfig.Filter.bRecursiveClasses = true;
							AssetPickerConfig.Filter.ClassPaths.Add(UAnimNextDataInterface::StaticClass()->GetClassPathName());
							AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
							AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateLambda([this](const FAssetData& InAssetData)
							{
								FSlateApplication::Get().DismissAllMenus();
								if(UAnimNextDataInterface* DataInterface = Cast<UAnimNextDataInterface>(InAssetData.GetAsset()))
								{
									AddDataInterface(DataInterface);
								}
							});
							AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateLambda([](const FAssetData& InAssetData)
							{
								FAnimNextAssetRegistryExports Exports;
								UncookedOnly::FUtils::GetExportedVariablesForAsset(InAssetData, Exports);
								if(Exports.Variables.Num() == 0)
								{
									return true;
								}
								return false;
							});

							return SNew(SBox)
								.WidthOverride(300.0f)
								.HeightOverride(400.0f)
								[
									ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
								];
						})
					]
				]
				+SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SAssignNew(EntriesTree, STreeView<TSharedRef<FEntry>>)
					.TreeItemsSource(&RootEntries)
					.OnGenerateRow(this, &SAddVariablesDialog::HandleGenerateRow)
					.OnGetChildren(this, &SAddVariablesDialog::HandleGetChildren)
					.HeaderRow(
						SNew(SHeaderRow)
						+SHeaderRow::Column(Column_Name)
						.DefaultLabel(LOCTEXT("NameColumnHeader", "Name"))
						.ToolTipText(LOCTEXT("NameColumnHeaderTooltip", "The name of the new variable"))
						.FillWidth(0.25f)

						+SHeaderRow::Column(Column_Type)
						.DefaultLabel(LOCTEXT("TypeColumnHeader", "Type"))
						.ToolTipText(LOCTEXT("TypeColumnHeaderTooltip", "The type of the new variable"))
						.FillWidth(0.25f)
					)
				]
				+SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				[
					SNew(SUniformGridPanel)
					.SlotPadding(FAppStyle::Get().GetMargin("StandardDialog.SlotPadding"))
					.MinDesiredSlotWidth(FAppStyle::GetFloat("StandardDialog.MinDesiredSlotWidth"))
					.MinDesiredSlotHeight(FAppStyle::GetFloat("StandardDialog.MinDesiredSlotHeight"))
					+SUniformGridPanel::Slot(0, 0)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
						.IsEnabled_Lambda([this]()
						{
							return bCanCreateVariables;
						})
						.Text(LOCTEXT("AddVariablesButtonFormat", "Add Variable(s)"))
						.ToolTipText_Lambda([this]()
						{
							if(bCanCreateVariables)
							{
								return LOCTEXT("AddVariablesButtonTooltip", "Add the selected variables to the current graph");
							}
							else
							{
								return FText::Format(LOCTEXT("AddVariablesButtonTooltip_InvalidEntry", "A variable to add is not valid: {0}"), CreateErrorMessage);
							}
						})
						.OnClicked_Lambda([this]()
						{
							bOKPressed = true;
							RequestDestroyWindow();
							return FReply::Handled();
						})
					]
					+SUniformGridPanel::Slot(1, 0)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
						.Text(LOCTEXT("CancelButton", "Cancel"))
						.ToolTipText(LOCTEXT("CancelButtonTooltip", "Cancel adding new variables"))
						.OnClicked_Lambda([this]()
						{
							RequestDestroyWindow();
							return FReply::Handled();
						})
					]
				]
			]
		]);

	if(InArgs._ShouldAddInitialVariable)
	{
		// Add an initial item
		AddEntry(InArgs._InitialParamType);
	}
}

FReply SAddVariablesDialog::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if(InKeyEvent.GetKey() == EKeys::Escape)
	{
		RequestDestroyWindow();
		return FReply::Handled();
	}
	else if(InKeyEvent.GetKey() == EKeys::Delete)
	{
		DeleteSelectedItems();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SAddVariablesDialog::DeleteSelectedItems()
{
	TArray<TSharedRef<FEntry>> SelectedItems;
	EntriesTree->GetSelectedItems(SelectedItems);
	for(const TSharedRef<FEntry>& SelectedItem : SelectedItems)
	{
		RootEntries.Remove(SelectedItem);
	}

	RefreshEntries();
}

static FName GetNewVariableName(FName InBaseName, TArrayView<FName> InExistingNames)
{
	auto NameExists = [&InExistingNames](FName InName)
	{
		for(FName AdditionalName : InExistingNames)
		{
			if(AdditionalName == InName)
			{
				return true;
			}
		}

		return false;
	};

	if(!NameExists(InBaseName))
	{
		// Early out - name is valid
		return InBaseName;
	}

	int32 PostFixIndex = 0;
	TStringBuilder<128> StringBuilder;
	while(true)
	{
		StringBuilder.Reset();
		InBaseName.GetDisplayNameEntry()->AppendNameToString(StringBuilder);
		StringBuilder.Appendf(TEXT("_%d"), PostFixIndex++);

		FName TestName(StringBuilder.ToString()); 
		if(!NameExists(TestName))
		{
			return TestName;
		}
	}

	return NAME_None;
}

void SAddVariablesDialog::GetPendingNamesRecursive(UAnimNextRigVMAssetEditorData* InEditorData, TArray<FName>& OutPendingNames) const
{
	for(UAnimNextRigVMAssetEntry* Entry : InEditorData->Entries)
	{
		if(UAnimNextVariableEntry* VariableEntry = Cast<UAnimNextVariableEntry>(Entry))
		{
			if(VariableEntry->GetExportAccessSpecifier() == EAnimNextExportAccessSpecifier::Public)
			{
				OutPendingNames.Add(Entry->GetEntryName());
			}
		}
		else if(UAnimNextDataInterfaceEntry* DataInterfaceEntry = Cast<UAnimNextDataInterfaceEntry>(Entry))
		{
			if(DataInterfaceEntry->DataInterface)
			{
				UAnimNextDataInterface_EditorData* EditorData = UE::AnimNext::UncookedOnly::FUtils::GetEditorData<UAnimNextDataInterface_EditorData>(DataInterfaceEntry->DataInterface.Get());
				GetPendingNamesRecursive(EditorData, OutPendingNames);
			}
		}
	}
}

void SAddVariablesDialog::GetPendingNames(TArray<FName>& OutPendingNames) const
{
	for(UAnimNextRigVMAssetEditorData* EditorData : AssetEditorDatas)
	{
		GetPendingNamesRecursive(EditorData, OutPendingNames);
	}
}

void SAddVariablesDialog::AddEntry(const FAnimNextParamType& InParamType)
{
	const UAnimNextVariableSettings* Settings = GetDefault<UAnimNextVariableSettings>();

	TArray<FName> PendingNames;
	PendingNames.Reserve(RootEntries.Num());
	for(const TSharedRef<FEntry>& QueuedAdd : RootEntries)
	{
		switch(QueuedAdd->EntryType)
		{
		case EEntryType::Variable:
			PendingNames.Add(StaticCastSharedRef<FVariableToAddEntry>(QueuedAdd)->Name);
			break;
		case EEntryType::DataInterface:
			for(const TSharedRef<FEntry>& SubEntry : QueuedAdd->Children)
			{
				check(SubEntry->EntryType == EEntryType::Variable);
				PendingNames.Add(StaticCastSharedRef<FVariableToAddEntry>(SubEntry)->Name);
			}
			break;
		}
	}

	GetPendingNames(PendingNames);

	FName VariableName = GetNewVariableName(Settings->GetLastVariableName(), PendingNames);
	RootEntries.Add(MakeShared<FVariableToAddEntry>(InParamType.IsValid() ? InParamType : Settings->GetLastVariableType(), VariableName, SharedThis(this)));

	RefreshEntries();
}

void SAddVariablesDialog::AddDataInterface(UAnimNextDataInterface* InDataInterface)
{
	
	TSharedRef<FDataInterfaceToAddEntry> NewEntry = MakeShared<FDataInterfaceToAddEntry>(InDataInterface, SharedThis(this));

	auto AddVariable = [this, &NewEntry](UAnimNextVariableEntry* InVariableEntry)
	{
		if(InVariableEntry->GetExportAccessSpecifier() == EAnimNextExportAccessSpecifier::Public)
		{
			TSharedRef<FVariableToAddEntry> NewSubEntry = MakeShared<FVariableToAddEntry>(InVariableEntry->GetType(), InVariableEntry->GetEntryName(), SharedThis(this));
			NewSubEntry->Parent = NewEntry;
			NewSubEntry->bIsNew = false;
			NewEntry->Children.Add(NewSubEntry);
		}
	};

	auto AddDataInterfaceInternal = [this, &AddVariable](UAnimNextDataInterface* InDataInterfaceToAdd, auto& InAddDataInterfaceInternal) -> void
	{
		// Add the child entries
		UAnimNextDataInterface_EditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextDataInterface_EditorData>(InDataInterfaceToAdd);
		for(UAnimNextRigVMAssetEntry* AssetEntry : EditorData->Entries)
		{
			if(UAnimNextVariableEntry* VariableEntry = Cast<UAnimNextVariableEntry>(AssetEntry))
			{
				AddVariable(VariableEntry);
			}
			else if(UAnimNextDataInterfaceEntry* DataInterfaceEntry = Cast<UAnimNextDataInterfaceEntry>(AssetEntry))
			{
				if(DataInterfaceEntry->GetDataInterface())
				{
					InAddDataInterfaceInternal(DataInterfaceEntry->GetDataInterface(), InAddDataInterfaceInternal);
				}
			}
		}
	};

	// Add the selected data interface and its recursive dependents
	AddDataInterfaceInternal(InDataInterface, AddDataInterfaceInternal);

	if(NewEntry->Children.Num() > 0)
	{
		RootEntries.Add(NewEntry);
		RefreshEntries();
	}
}

void SAddVariablesDialog::RefreshCanCreate()
{
	bCanCreateVariables = true;
	for(TSharedRef<FEntry> Entry : RootEntries)
	{
		FText Reason;
		if(!Entry->IsValid(Reason))
		{
			CreateErrorMessage = Reason;
			bCanCreateVariables = false;
			return;
		}

		for(const TSharedRef<FEntry>& SubEntry : Entry->Children)
		{
			if(!SubEntry->IsValid(Reason))
			{
				CreateErrorMessage = Reason;
				bCanCreateVariables = false;
				return;
			}
		}
	}
}

void SAddVariablesDialog::RefreshEntries()
{
	EntriesTree->RequestTreeRefresh();

	RefreshCanCreate();
}

class SVariableToAdd : public SMultiColumnTableRow<TSharedRef<SAddVariablesDialog::FEntry>>
{
	SLATE_BEGIN_ARGS(SVariableToAdd) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, TSharedRef<SAddVariablesDialog::FEntry> InEntry, TSharedRef<SAddVariablesDialog> InDialog)
	{
		Entry = InEntry;
		WeakDialog = InDialog;
		
		SMultiColumnTableRow<TSharedRef<SAddVariablesDialog::FEntry>>::Construct( SMultiColumnTableRow<TSharedRef<SAddVariablesDialog::FEntry>>::FArguments(), InOwnerTableView);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
	{
		using namespace AddVariablesDialog;

		if(InColumnName == Column_Name)
		{
			TSharedPtr<SWidget> EntryWidget = SNullWidget::NullWidget;
			if(Entry->EntryType == SAddVariablesDialog::EEntryType::Variable)
			{
				EditableTextBox = SAssignNew(EntryWidget, SEditableTextBox)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.IsReadOnly_Lambda([this]()
				{
					// Cant rename entries from data interfaces (i.e. with parents)
					check(Entry->EntryType == SAddVariablesDialog::EEntryType::Variable);
					return Entry->Parent.IsValid();
				})
				.ToolTipText_Lambda([this]()
				{
					if(!CurrentError.IsEmpty())
					{
						return CurrentError;
					}
					return LOCTEXT("NameTooltip", "The name of the new variable");
				})
				.Text_Lambda([this]()
				{
					check(Entry->EntryType == SAddVariablesDialog::EEntryType::Variable);
					return FText::FromName(StaticCastSharedPtr<SAddVariablesDialog::FVariableToAddEntry>(Entry)->Name);
				})
				.OnTextCommitted_Lambda([this](const FText& InText, ETextCommit::Type InCommitType)
				{
					check(Entry->EntryType == SAddVariablesDialog::EEntryType::Variable);
					TSharedPtr<SAddVariablesDialog::FVariableToAddEntry> VariableEntry = StaticCastSharedPtr<SAddVariablesDialog::FVariableToAddEntry>(Entry);
					VariableEntry->Name = *InText.ToString();

					UAnimNextVariableSettings* Settings = GetMutableDefault<UAnimNextVariableSettings>();
					Settings->SetLastVariableName(VariableEntry->Name);
					RefreshErrors();
					WeakDialog.Pin()->RefreshCanCreate();
				});
			}
			else if(Entry->EntryType == SAddVariablesDialog::EEntryType::DataInterface)
			{
				SAssignNew(EntryWidget, STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
				.Text_Lambda([this]()
				{
					TSharedPtr<SAddVariablesDialog::FDataInterfaceToAddEntry> InterfaceEntry = StaticCastSharedPtr<SAddVariablesDialog::FDataInterfaceToAddEntry>(Entry);
					FName Name = InterfaceEntry->DataInterface ? InterfaceEntry->DataInterface->GetFName() : NAME_None; 
					return FText::FromName(Name);
				});
				
				if(Entry->bIsNew)
				{
					TWeakPtr<STreeView<TSharedRef<SAddVariablesDialog::FEntry>>> WeakTreeView = WeakDialog.Pin()->EntriesTree;
					EntryWidget->RegisterActiveTimer(1/60.0f, FWidgetActiveTimerDelegate::CreateSPLambda(WeakDialog.Pin()->EntriesTree.Get(), [WeakTreeView, WeakEntry = TWeakPtr<SAddVariablesDialog::FEntry>(Entry)](double, float)
					{
						TSharedPtr<STreeView<TSharedRef<SAddVariablesDialog::FEntry>>> PinnedTreeView = WeakTreeView.Pin();
						TSharedPtr<SAddVariablesDialog::FEntry> PinnedEntry = WeakEntry.Pin();
						if(PinnedTreeView.IsValid() && PinnedEntry.IsValid())
						{
							PinnedTreeView->SetItemExpansion(PinnedEntry.ToSharedRef(), true);
						}
						return EActiveTimerReturnType::Stop;
					}));

					Entry->bIsNew = false;
				}
			}

			TSharedRef<SWidget> Widget =
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SExpanderArrow, SharedThis(this))
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					[
						EntryWidget.ToSharedRef()
					]
				];

			return Widget;
		}
		else if(InColumnName == Column_Type)
		{
			if(Entry->EntryType == SAddVariablesDialog::EEntryType::Variable)
			{
				auto GetPinInfo = [this]()
				{
					check(Entry->EntryType == SAddVariablesDialog::EEntryType::Variable);
					return UncookedOnly::FUtils::GetPinTypeFromParamType(StaticCastSharedPtr<SAddVariablesDialog::FVariableToAddEntry>(Entry)->Type);
				};

				auto PinInfoChanged = [this](const FEdGraphPinType& PinType)
				{
					check(Entry->EntryType == SAddVariablesDialog::EEntryType::Variable);
					TSharedPtr<SAddVariablesDialog::FVariableToAddEntry> VariableEntry = StaticCastSharedPtr<SAddVariablesDialog::FVariableToAddEntry>(Entry);
					VariableEntry->Type = UncookedOnly::FUtils::GetParamTypeFromPinType(PinType);

					UAnimNextVariableSettings* Settings = GetMutableDefault<UAnimNextVariableSettings>();
					Settings->SetLastVariableType(VariableEntry->Type);
				};
				
				auto GetFilteredVariableTypeTree = [this](TArray<TSharedPtr<UEdGraphSchema_K2::FPinTypeTreeInfo>>& TypeTree, ETypeTreeFilter TypeTreeFilter)
				{
					FUtils::GetFilteredVariableTypeTree(TypeTree, TypeTreeFilter);

					if(TSharedPtr<SAddVariablesDialog> Dialog = WeakDialog.Pin())
					{
						if(Dialog->OnFilterVariableType.IsBound())
						{
							auto IsPinTypeAllowed = [&Dialog](const FEdGraphPinType& InType)
							{
								FAnimNextParamType Type = UncookedOnly::FUtils::GetParamTypeFromPinType(InType);
								if(Type.IsValid())
								{
									return Dialog->OnFilterVariableType.Execute(Type) == EFilterVariableResult::Include;
								}
								return false;
							};

							// Additionally filter by allowed types
							for (int32 Index = 0; Index < TypeTree.Num(); )
							{
								TSharedPtr<UEdGraphSchema_K2::FPinTypeTreeInfo>& PinType = TypeTree[Index];

								if (PinType->Children.Num() == 0 && !IsPinTypeAllowed(PinType->GetPinType(/*bForceLoadSubCategoryObject*/false)))
								{
									TypeTree.RemoveAt(Index);
									continue;
								}

								for (int32 ChildIndex = 0; ChildIndex < PinType->Children.Num(); )
								{
									TSharedPtr<UEdGraphSchema_K2::FPinTypeTreeInfo> Child = PinType->Children[ChildIndex];
									if (Child.IsValid())
									{
										if (!IsPinTypeAllowed(Child->GetPinType(/*bForceLoadSubCategoryObject*/false)))
										{
											PinType->Children.RemoveAt(ChildIndex);
											continue;
										}
									}
									++ChildIndex;
								}

								++Index;
							}
						}
					}
				};
					
				return
					SNew(SBox)
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					[
						SNew(SPinTypeSelector, FGetPinTypeTree::CreateLambda(GetFilteredVariableTypeTree))
							.IsEnabled_Lambda([this]()
							{
								// Cant rename entries from data interfaces (i.e. with parents)
								check(Entry->EntryType == SAddVariablesDialog::EEntryType::Variable);
								return !Entry->Parent.IsValid();
							})
							.TargetPinType_Lambda(GetPinInfo)
							.OnPinTypeChanged_Lambda(PinInfoChanged)
							.Schema(GetDefault<UPropertyBagSchema>())
							.bAllowArrays(true)
							.TypeTreeFilter(ETypeTreeFilter::None)
							.Font(IDetailLayoutBuilder::GetDetailFont())
					];
			}
		}

		return SNullWidget::NullWidget;
	}

	void RefreshErrors()
	{
		if(EditableTextBox.IsValid())
		{
			FText Reason;
			if(!Entry->IsValid(Reason))
			{
				EditableTextBox->SetError(Reason);
				CurrentError = Reason;
			}
			else
			{
				EditableTextBox->SetError(FText::GetEmpty());
				CurrentError = FText::GetEmpty();
			}
		}
		else
		{
			CurrentError = FText::GetEmpty();
		}
	}

	FText CurrentError;
	TSharedPtr<SEditableTextBox> EditableTextBox;
	TSharedPtr<SAddVariablesDialog::FEntry> Entry;
	TWeakPtr<SAddVariablesDialog> WeakDialog;
};

TSharedRef<ITableRow> SAddVariablesDialog::HandleGenerateRow(TSharedRef<FEntry> InEntry, const TSharedRef<STableViewBase>& InOwnerTable)
{
	TSharedRef<SVariableToAdd> RowWidget = SNew(SVariableToAdd, InOwnerTable, InEntry, SharedThis(this));
	RowWidget->RefreshErrors();
	return RowWidget;
}

void SAddVariablesDialog::HandleGetChildren(TSharedRef<FEntry> InEntry, TArray<TSharedRef<FEntry>>& OutChildren)
{
	OutChildren = InEntry->Children;
}

bool SAddVariablesDialog::ShowModal(TArray<FVariableToAdd>& OutVariables, TArray<FDataInterfaceToAdd>& OutDataInterfaces)
{
	FSlateApplication::Get().AddModalWindow(SharedThis(this), FGlobalTabmanager::Get()->GetRootWindow());

	if(bOKPressed)
	{
		bool bHasValid = false;
		for(TSharedRef<FEntry>& Entry : RootEntries)
		{
			FText Reason;
			if(Entry->IsValid(Reason))
			{
				switch(Entry->EntryType)
				{
				case EEntryType::Variable:
					OutVariables.Add(*StaticCastSharedRef<FVariableToAddEntry>(Entry));
					break;
				case EEntryType::DataInterface:
					OutDataInterfaces.Add(*StaticCastSharedRef<FDataInterfaceToAddEntry>(Entry));
					break;
				}
				bHasValid = true;
			}
		}
		return bHasValid;
	}
	return false;
}

TSharedRef<SWidget> SAddVariablesDialog::HandleGetAddVariableMenuContent(TSharedPtr<FEntry> InEntry)
{
	using namespace AddVariablesDialog;

	UToolMenus* ToolMenus = UToolMenus::Get();

	UAddVariableDialogMenuContext* MenuContext = NewObject<UAddVariableDialogMenuContext>();
	MenuContext->AddVariablesDialog = SharedThis(this);
	MenuContext->Entry = InEntry;
	return ToolMenus->GenerateWidget(SelectLibraryMenuName, FToolMenuContext(MenuContext));
}

}

#undef LOCTEXT_NAMESPACE