// Copyright Epic Games, Inc. All Rights Reserved.

#include "ValidatorsWidget.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"

#include "Models/SubmitToolUserPrefs.h"
#include "Models/JiraIssue.h"
#include "Styling/StyleColors.h"

#define LOCTEXT_NAMESPACE "ValidatorStatesWidget"

namespace ValidatorsColumns
{
	static const FName Type("Type");
	static const FName Name("Name");
	static const FName Required("Required");
	static const FName Status("Status");
	static const FName Actions("Actions");
};

void SValidatorsWidget::Construct(const FArguments& InArgs)
{
	Columns = {
		FValidatorColumn(ValidatorsColumns::Name, 1, true, [](TWeakPtr<const FValidatorBase> In) {return In.Pin()->GetValidatorName(); }),
		FValidatorColumn(ValidatorsColumns::Required, 100, false, [](TWeakPtr<const FValidatorBase> In) {return In.Pin()->Definition->IsRequired ? TEXT("Yes") : TEXT("No"); }),
		FValidatorColumn(ValidatorsColumns::Status, 200, false, [](TWeakPtr<const FValidatorBase> In) {return In.Pin()->GetStatusText(); }),
		FValidatorColumn(ValidatorsColumns::Actions, 200, false, nullptr),
	};

	ModelInterface = InArgs._ModelInterface;
	TArray<TWeakPtr<const FValidatorBase>> ValidatorsArray = ModelInterface->GetValidators();
	OnViewLog = InArgs._OnViewLog;

	TSharedRef<SVerticalBox> VBox = SNew(SVerticalBox);

	for(const TWeakPtr<const FValidatorBase>& Validator : ValidatorsArray)
	{
		Validators.FindOrAdd(Validator.Pin()->Definition->UIGroup).Add(Validator);
	}

	for(const TPair<FName, TArray<TWeakPtr<const FValidatorBase>>>& Pair : Validators)
	{
		FName DisplayName = Pair.Key;
		if(Pair.Key.IsNone())
		{
			DisplayName = FName(TEXT("General Validations"));
		}

		VBox->AddSlot()
			.Padding(FMargin(0.f, 2.f))
			.AutoHeight()
			.AttachWidget
			(
				SNew(SExpandableArea)
				.InitiallyCollapsed(!FSubmitToolUserPrefs::Get()->UISectionExpandState.FindOrAdd(DisplayName, false))
				.BorderImage(FAppStyle::Get().GetBrush("Brushes.Header"))
				.BodyBorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
				.OnAreaExpansionChanged_Lambda([DisplayName](bool bExpanded) { FSubmitToolUserPrefs::Get()->UISectionExpandState.FindOrAdd(DisplayName, false) = bExpanded; })
				.BorderBackgroundColor_Lambda([&ValidatorArray = Pair.Value] { 
					bool bRunning = false;
					bool bValid = true;
					for(const TWeakPtr<const FValidatorBase>& Validator : ValidatorArray)
					{
						bRunning |= Validator.Pin()->GetIsRunning();
						bValid = bValid && Validator.Pin()->GetHasPassed();

						if(Validator.Pin()->GetValidatorState() == EValidationStates::Failed || 
							Validator.Pin()->GetValidatorState() == EValidationStates::Timeout)
						{
							return FAppStyle::GetColor("ValidatorStateFail");
						}
					}

					if(bValid)
					{
						return FAppStyle::GetColor("ValidatorStateSuccess");
					}

					if(bRunning)
					{
						return FAppStyle::GetColor("ValidatorStateWarning");
					}

					return FAppStyle::GetColor("ValidatorStateNormal");})
				.HeaderPadding(FMargin(4.0f, 2.0f))
				.Padding(1.0f)
				.AllowAnimatedTransition(true)
				.HeaderContent()
				[
					SNew(STextBlock)
					.Text_Lambda([DisplayName] {return FText::FromName(DisplayName); })
				]
				.BodyContent()
				[
					SNew(SBox)
					.Padding(2.5)
					[
						SAssignNew(ValidatorsListView, SListView<TWeakPtr<const FValidatorBase>>)
							.SelectionMode(ESelectionMode::None)
							.ListItemsSource(&Pair.Value)
							.HeaderRow(ConstructHeadersRow(Pair.Key))
							.OnGenerateRow(this, &SValidatorsWidget::GenerateRow)
							.IsFocusable(false)
					]
				]
			);
			
	}

	ChildSlot
	[
		VBox
	];
}

TSharedRef<SHeaderRow> SValidatorsWidget::ConstructHeadersRow(const FName& GroupName)
{
	TSharedRef<SHeaderRow> HeaderRow = SNew(SHeaderRow);

	for (size_t i = 0;i<Columns.Num();++i)
	{
		SHeaderRow::FColumn::FArguments args = SHeaderRow::Column(Columns[i].Name);
		args.DefaultLabel(FText::FromName(Columns[i].Name));

		if(Columns[i].SortingFunc != nullptr)
		{
			args.SortMode(this, &SValidatorsWidget::GetSortMode, Columns[i].Name)
				.InitialSortMode(EColumnSortMode::Ascending)
				.OnSort_Lambda([&GroupName, this](EColumnSortPriority::Type InSortPriority, const FName& InColumnId, EColumnSortMode::Type InSortDirection) {
					OnColumnSort(GroupName, InSortPriority, InColumnId, InSortDirection);
				});
		}

		if(Columns[i].bIsFill)
		{
			args.FillWidth(Columns[i].Width);
		}
		else
		{
			args.FixedWidth(Columns[i].Width);
		}

		HeaderRow->AddColumn(args);
	}

	return HeaderRow;
}

class SValidatorNode : public SMultiColumnTableRow<TWeakPtr<const FValidatorBase>>
{
public:
	SLATE_BEGIN_ARGS(SValidatorNode) {}
		SLATE_ARGUMENT(FModelInterface*, ModelInterface)
		SLATE_EVENT(FOnViewValidatorLog, ViewLogCallback)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, TWeakPtr<const FValidatorBase> InNode)
	{
		ModelInterface = InArgs._ModelInterface;
		Node = InNode;
		ViewLogCallback = InArgs._ViewLogCallback;
		Options = TMap<TSharedPtr<FString>, TArray<TSharedPtr<FString>>>();
		for(const TPair<FString, TMap<FString, FString>>& Pair : Node.Pin()->GetValidatorOptions())
		{
			TArray<TSharedPtr<FString>> Values;
			for (const TPair<FString, FString>& Value : Pair.Value)
			{
				Values.Add(MakeShared<FString>(Value.Key));
			}

			Options.Add(MakeShared<FString>(Pair.Key), Values);
		}

		SMultiColumnTableRow<TWeakPtr<const FValidatorBase>>::Construct(
			FSuperRowType::FArguments()
			.Padding(1.0f),
			InOwnerTable
		);
	}
	
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
	{
		TSharedRef<SBorder> box = SNew(SBorder).VAlign(EVerticalAlignment::VAlign_Center);

		if(InColumnName == ValidatorsColumns::Name)
		{
			const FString NameStr = Node.Pin()->Definition->IsRequired ? Node.Pin()->GetValidatorName() : Node.Pin()->GetValidatorName() + TEXT(" - Optional");
			TSharedRef<STextBlock> Name = SNew(STextBlock)
				.Text(FText::FromString(NameStr))
				.ToolTipText(FText::FromString(Node.Pin()->Definition->ToolTip));

			TSharedRef<SHorizontalBox> Horizontal = SNew(SHorizontalBox);

			Horizontal->AddSlot()
			.VAlign(VAlign_Center)
			.FillWidth(1.0f)
			[
				Name
			];

			if(Options.Num() != 0)
			{
				for (const TPair<TSharedPtr<FString>, TArray<TSharedPtr<FString>>>& OptionItem : Options)
				{
					TSharedRef<STextBlock> ComboContent = SNew(STextBlock)
						.Text_Lambda([this, OptionItem]() {	return FText::FromString(Node.Pin()->GetSelectedOptionKey(*OptionItem.Key));	})
						.Font(FAppStyle::GetFontStyle("SmallFont"))
						.ColorAndOpacity_Lambda([this, OptionItem]
							{
								if (Node.Pin()->GetOptionType(*OptionItem.Key) == EValidatorOptionType::FilePath)
								{
									return IFileManager::Get().FileExists(*Node.Pin()->GetSelectedOptionValue(*OptionItem.Key))
										? FStyleColors::White
										: FStyleColors::Error;
								}

								return FStyleColors::White;
							});

					Horizontal->AddSlot()
					.AutoWidth()
					[
						SNew(SBox)
						.MinDesiredWidth(120)
						[
							SNew(SComboBox<TSharedPtr<FString>>)
							.IsEnabled(OptionItem.Value.Num() > 1)
							.OptionsSource(&OptionItem.Value)
							.OnGenerateWidget_Lambda([this, OptionItem](TSharedPtr<FString> ChoiceEntry)
								{
									return SNew(STextBlock)
										.Text(FText::FromString(*ChoiceEntry))
										.ToolTipText(FText::FromString(Node.Pin()->GetValidatorOptions()[*OptionItem.Key][*ChoiceEntry]))
										.Font(FAppStyle::GetFontStyle("SmallFont"))
										.ColorAndOpacity_Lambda([this, OptionItem, ChoiceEntry]
											{
												if (Node.Pin()->GetOptionType(*OptionItem.Key) == EValidatorOptionType::FilePath)
												{
													return IFileManager::Get().FileExists(*Node.Pin()->GetValidatorOptions()[*OptionItem.Key][*ChoiceEntry])
														? FStyleColors::White
														: FStyleColors::Error;
												}

												return FStyleColors::White;
											});
								})
							.OnSelectionChanged_Lambda([this, OptionItem](TSharedPtr<FString> NewChoice, ESelectInfo::Type SelectType) {
									const_cast<FValidatorBase*>(Node.Pin().Get())->SetSelectedOption(*OptionItem.Key, *NewChoice);
								})
									[
										ComboContent
									]
						]
					];
				}

				box->SetContent(Horizontal);
			}
			else
			{
				box->SetContent(Name);
			}
		}
		else if (InColumnName == ValidatorsColumns::Required)
		{
			TSharedRef<STextBlock> Required = SNew(STextBlock).Text(FText::FromString(Node.Pin()->Definition->IsRequired ? TEXT("Required") : TEXT("Optional")));
			box->SetContent(
				Required
			);
		}


		else if(InColumnName == ValidatorsColumns::Status)
		{
			box->SetContent(
				SNew(STextBlock)
				.Text_Lambda([this]() { return FText::FromString(Node.Pin()->GetStatusText()); })
				.ColorAndOpacity_Lambda([this]{
					switch(Node.Pin()->GetValidatorState())
					{
					case EValidationStates::Failed:
					case EValidationStates::Timeout:
						return FAppStyle::GetColor("ValidatorStateFail");
						break;
					case EValidationStates::Valid:
					case EValidationStates::Skipped:
						return FAppStyle::GetColor("ValidatorStateSuccess");
						break;
					case EValidationStates::Running:
						return FAppStyle::GetColor("ValidatorStateWarning");
						break;
					case EValidationStates::Not_Run:
					default:
						return FAppStyle::GetColor("ValidatorStateNormal");
						break;
					}})
			);
		}
		else if(InColumnName == ValidatorsColumns::Actions)
		{
			box->SetContent(
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.Padding(3,0,0,0)
				[
					SNew(SButton)
					.IsEnabled_Lambda([this]() {return ModelInterface->GetInputEnabled(); })
					.HAlign(EHorizontalAlignment::HAlign_Center)
					.VAlign(EVerticalAlignment::VAlign_Center)
					.OnClicked_Lambda([this]()
					{
						if(Node.Pin()->GetIsRunningOrQueued()){
							ModelInterface->CancelValidations(Node.Pin()->GetValidatorNameId());
						}else{
							ModelInterface->ValidateSingle(Node.Pin()->GetValidatorNameId());
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Justification(ETextJustify::Center)
						.Text_Lambda([this](){ return Node.Pin()->GetIsRunningOrQueued() ? FText::FromString("Stop") : FText::FromString("Run"); })
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
					]
				]
				+SHorizontalBox::Slot()
				.Padding(3, 0, 3, 0)
				[
					SNew(SButton)
					.IsEnabled_Lambda([this]() {return Node.Pin()->GetValidatorState() != EValidationStates::Not_Run && Node.Pin()->GetValidatorState() != EValidationStates::Skipped; })
					.HAlign(EHorizontalAlignment::HAlign_Center)
					.VAlign(EVerticalAlignment::VAlign_Center)
					.OnClicked_Lambda([this]() {ViewLogCallback.ExecuteIfBound(Node.Pin()); return FReply::Handled(); })					
					[
						SNew(STextBlock)
						.Justification(ETextJustify::Center)
						.Text(FText::FromString("View Log"))
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
					]

				]);	
		}

		return box;
	}


	TMap<TSharedPtr<FString>, TArray<TSharedPtr<FString>>> Options;

	TWeakPtr<const FValidatorBase> Node;
	FModelInterface* ModelInterface;
	FOnViewValidatorLog ViewLogCallback;
};

TSharedRef<ITableRow> SValidatorsWidget::GenerateRow(TWeakPtr<const FValidatorBase> InItem, const TSharedRef<STableViewBase>& OwnerTable) const
{
	return SNew(SValidatorNode, OwnerTable, InItem)
		.ModelInterface(ModelInterface)
		.ViewLogCallback(OnViewLog);
}

void SValidatorsWidget::OnColumnSort(const FName& GroupName, EColumnSortPriority::Type InSortPriority, const FName& InColumnId, EColumnSortMode::Type InSortDirection)
{
	SortMode = InSortDirection;
	SortByColumn = InColumnId;

	for (const FValidatorColumn& Column : Columns)
	{
		if(!Column.Name.IsEqual(InColumnId))
		{
			continue;
		}

		if(Column.SortingFunc == nullptr)
		{
			break;
		}

		if(InSortDirection == EColumnSortMode::Ascending)
		{
			Algo::SortBy(Validators[GroupName], Column.SortingFunc);
		}
		else
		{
			Algo::Reverse(Validators[GroupName]);
		}
		
		break;
	}
	
	ValidatorsListView->RequestListRefresh();
}

EColumnSortMode::Type SValidatorsWidget::GetSortMode(const FName ColumnId) const
{
	if(ColumnId == SortByColumn)
	{
		return SortMode;
	}

	return EColumnSortMode::None;
}

#undef LOCTEXT_NAMESPACE