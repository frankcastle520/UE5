// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/PropertyGroupCustomization.h"
#include "Dataflow/DataflowGraphEditor.h"
#include "Dataflow/DataflowNode.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "DetailWidgetRow.h"

#define LOCTEXT_NAMESPACE "PropertyGroupCustomization"

namespace UE::Dataflow
{
	const FManagedArrayCollection& FPropertyGroupCustomization::GetPropertyCollection(
		const TSharedPtr<UE::Dataflow::FContext>& Context,
		const TSharedPtr<IPropertyHandle>& ChildPropertyHandle,
		const FName CollectionPropertyName = FName(TEXT("Collection")))
	{
		static const FManagedArrayCollection EmptyCollection;

		TSharedPtr<IPropertyHandle> OwnerHandle = ChildPropertyHandle;
		while (TSharedPtr<IPropertyHandle> ParentHandle = OwnerHandle->GetParentHandle())
		{
			OwnerHandle = MoveTemp(ParentHandle);
		}
		if (const TSharedPtr<IPropertyHandleStruct> OwnerHandleStruct = OwnerHandle->AsStruct())
		{
			if (const TSharedPtr<FStructOnScope> StructOnScope = OwnerHandleStruct->GetStructData())
			{
				if (const UStruct* const Struct = StructOnScope->GetStruct())
				{
					if (Struct->IsChildOf<FDataflowNode>())
					{
						const FDataflowNode* const DataflowNode = reinterpret_cast<FDataflowNode*>(StructOnScope->GetStructMemory());

						if (const FProperty* const Property = Struct->FindPropertyByName(CollectionPropertyName))
						{
							if (const FStructProperty* const StructProperty = CastField<FStructProperty>(Property))
							{
								if (StructProperty->GetCPPType(nullptr, CPPF_None) == TEXT("FManagedArrayCollection"))
								{
									if (const FDataflowInput* const DataflowInput = DataflowNode->FindInput(StructProperty->ContainerPtrToValuePtr<FManagedArrayCollection*>(DataflowNode)))
									{
										UE::Dataflow::FContextThreaded EmptyContext;
										return DataflowInput->GetValue(Context.IsValid() ? *Context : EmptyContext, EmptyCollection);
									}
								}
							}
						}
					}
				}
			}
		}
		return EmptyCollection;
	}

	bool FPropertyGroupCustomization::MakeGroupName(FString& InOutString)
	{
		const FString SourceString = InOutString;
		InOutString = SlugStringForValidName(InOutString, TEXT("_")).Replace(TEXT("\\"), TEXT("_"));
		bool bCharsWereRemoved;
		do { InOutString.TrimCharInline(TEXT('_'), &bCharsWereRemoved); } while (bCharsWereRemoved);
		return InOutString.Equals(SourceString);
	}

	TSharedRef<IPropertyTypeCustomization> FPropertyGroupCustomization::MakeInstance()
	{
		return MakeShareable(new FPropertyGroupCustomization);
	}

	void FPropertyGroupCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
	{
		DataflowGraphEditor = SDataflowGraphEditor::GetSelectedGraphEditor();

		uint32 NumChildren;
		const FPropertyAccess::Result Result = PropertyHandle->GetNumChildren(NumChildren);

		ChildPropertyHandle = (Result == FPropertyAccess::Success && NumChildren) ? PropertyHandle->GetChildHandle(0) : nullptr;

		GroupNames.Reset();

		HeaderRow
			.NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget(PropertyHandle->GetPropertyDisplayName())
			]
			.ValueContent()
			.MinDesiredWidth(250)
			.MaxDesiredWidth(350.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.MaxWidth(145.f)
				[
					SAssignNew(ComboButton, SComboButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.ContentPadding(0)
					.OnGetMenuContent(this, &FPropertyGroupCustomization::OnGetMenuContent)
					.ButtonContent()
					[
						SNew(SEditableTextBox)
						.Text(this, &FPropertyGroupCustomization::GetText)
						.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
						.OnTextCommitted(this, &FPropertyGroupCustomization::OnTextCommitted)
						.OnVerifyTextChanged(this, &FPropertyGroupCustomization::OnVerifyTextChanged)
					]
				]
			];
	}

	FText FPropertyGroupCustomization::GetText() const
	{
		FText Text;
		if (ChildPropertyHandle)
		{
			ChildPropertyHandle->GetValueAsFormattedText(Text);
		}
		return Text;
	}

	void FPropertyGroupCustomization::OnTextCommitted(const FText& NewText, ETextCommit::Type CommitInfo)
	{
		if (ChildPropertyHandle)
		{
			FText CurrentText;
			ChildPropertyHandle->GetValueAsFormattedText(CurrentText);

			if (!NewText.ToString().Equals(CurrentText.ToString(), ESearchCase::CaseSensitive))
			{
				FString String = NewText.ToString();
				MakeGroupName(String);
				ChildPropertyHandle->SetValueFromFormattedString(String);
			}
		}
	}

	void FPropertyGroupCustomization::OnSelectionChanged(TSharedPtr<FText> ItemSelected, ESelectInfo::Type /*SelectInfo*/)
	{
		if (ChildPropertyHandle)
		{
			// Set the child property's value
			if (ItemSelected)
			{
				FText CurrentText;
				ChildPropertyHandle->GetValueAsFormattedText(CurrentText);

				if (!ItemSelected->EqualTo(CurrentText))
				{
					ChildPropertyHandle->SetValueFromFormattedString(ItemSelected->ToString());
				}

				if (TSharedPtr<SComboButton> PinnedComboButton = ComboButton.Pin())
				{
					PinnedComboButton->SetIsOpen(false);
				}
			}
		}
	}

	bool FPropertyGroupCustomization::OnVerifyTextChanged(const FText& Text, FText& OutErrorMessage)
	{
		FString TextString = Text.ToString();
		const bool bIsValidGroupName = MakeGroupName(TextString);
		if (!bIsValidGroupName)
		{
			OutErrorMessage =
				LOCTEXT("NotAValidGroupName",
					"To be a valid group name, this text string musn't start by an underscore,\n"
					"contain whitespaces, or any of the following character: \"',/.:|&!~@#(){}[]=;^%$`");
		}
		return bIsValidGroupName;
	}

	TSharedRef<ITableRow> FPropertyGroupCustomization::MakeCategoryViewWidget(TSharedPtr<FText> Item, const TSharedRef< STableViewBase >& OwnerTable)
	{
		if (Item)
		{
			return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
			[
				SNew(STextBlock).Text(*Item)
			];
		}
		else
		{
			return SNew(STableRow<TSharedPtr<FString>>, OwnerTable);
		}
	}

	TSharedRef<SWidget> FPropertyGroupCustomization::OnGetMenuContent()
	{
		const TSharedPtr<const SDataflowGraphEditor> DataflowGraphEditorPtr = DataflowGraphEditor.Pin();
		const TSharedPtr<UE::Dataflow::FContext> Context = DataflowGraphEditorPtr ? DataflowGraphEditorPtr->GetDataflowContext() : TSharedPtr<UE::Dataflow::FContext>();

		GroupNames.Reset();

		// Retrieve collection
		const FManagedArrayCollection& Collection = GetPropertyCollection(Context, ChildPropertyHandle, GetCollectionPropertyName());

		// Find all group names in the parent node's collection
		TArray<FName> CollectionGroupNames;
		const TArray<FName> TargetGroupNames = GetTargetGroupNames(Collection);

		// Find all group names in the parent selection node's collection
		for (const FName& GroupName : Collection.GroupNames())
		{
			if (TargetGroupNames.Contains(GroupName))
			{
				GroupNames.Add(MakeShareable(new FText(FText::FromName(GroupName))));
			}
		}

		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.MaxHeight(400.0f)
			[
				SNew(SListView<TSharedPtr<FText>>)
					.ListItemsSource(&GroupNames)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &FPropertyGroupCustomization::MakeCategoryViewWidget)
					.OnSelectionChanged(this, &FPropertyGroupCustomization::OnSelectionChanged)
			];
	}
}  // End namespace UE::Chaos::ClothAsset

#undef LOCTEXT_NAMESPACE
