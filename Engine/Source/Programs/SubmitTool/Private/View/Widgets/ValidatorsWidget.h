// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

#include "Models/ModelInterface.h"

DECLARE_DELEGATE_OneParam(FOnViewValidatorLog, TSharedPtr<const FValidatorBase>)

struct FValidatorColumn
{
	FValidatorColumn(FName InName, float InWidth, bool InbIsFill, const TFunction<FString(TWeakPtr<const FValidatorBase>)> InSortingFunc)
		: SortingFunc(InSortingFunc)
	{
		Name = InName;
		Width = InWidth;
		bIsFill = InbIsFill;
	}

	FName Name;
	float Width;
	bool bIsFill;
	const TFunction<FString(TWeakPtr<const FValidatorBase>)> SortingFunc;
};

class SValidatorsWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SValidatorsWidget) {}
		SLATE_ARGUMENT(FModelInterface*, ModelInterface)
		SLATE_EVENT(FOnViewValidatorLog, OnViewLog)
	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs);

private:
	FModelInterface* ModelInterface;
	
	TSharedPtr<SListView<TWeakPtr<const FValidatorBase>>> ValidatorsListView;
	
	TMap<FName, TArray<TWeakPtr<const FValidatorBase>>> Validators;
	TArray<FValidatorColumn> Columns;

	TSharedRef<SHeaderRow> ConstructHeadersRow(const FName& GroupName);
	TSharedRef<ITableRow> GenerateRow(TWeakPtr<const FValidatorBase> InItem, const TSharedRef<STableViewBase>& OwnerTable) const;
	
	void OnColumnSort(const FName& GroupName, EColumnSortPriority::Type InSortPriority, const FName& InColumnId, EColumnSortMode::Type InSortDirection);
	EColumnSortMode::Type GetSortMode(const FName ColumnId) const;

	FName SortByColumn;
    EColumnSortMode::Type SortMode;
	FOnViewValidatorLog OnViewLog;
};
