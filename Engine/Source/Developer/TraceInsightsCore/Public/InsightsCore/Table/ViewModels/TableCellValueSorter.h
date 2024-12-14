// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "InsightsCore/Common/AsyncOperationProgress.h"
#include "InsightsCore/Table/ViewModels/TableCellValue.h"
#include "InsightsCore/Table/ViewModels/TableColumn.h"

struct FSlateBrush;

namespace UE::Insights
{

class FBaseTreeNode;
typedef TSharedPtr<class FBaseTreeNode> FBaseTreeNodePtr;

////////////////////////////////////////////////////////////////////////////////////////////////////

enum class TRACEINSIGHTSCORE_API ESortMode
{
	Ascending,
	Descending
};

////////////////////////////////////////////////////////////////////////////////////////////////////

typedef TFunction<bool(const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B)> FTreeNodeCompareFunc;

class TRACEINSIGHTSCORE_API ITableCellValueSorter
{
public:
	virtual FName GetName() const = 0;
	virtual FText GetShortName() const = 0;
	virtual FText GetTitleName() const = 0;
	virtual FText GetDescription() const = 0;
	virtual FName GetColumnId() const = 0;

	virtual FSlateBrush* GetIcon(ESortMode SortMode) const = 0;

	virtual FTreeNodeCompareFunc GetTreeNodeCompareDelegate(ESortMode SortMode) const = 0;

	virtual void Sort(TArray<FBaseTreeNodePtr>& NodesToSort, ESortMode SortMode) const = 0;

	virtual void SetAsyncOperationProgress(IAsyncOperationProgress* AsyncOperationProgress) = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FTableCellValueSorter : public ITableCellValueSorter
{
public:
	FTableCellValueSorter(const FName InName, const FText& InShortName, const FText& InTitleName, const FText& InDescription, TSharedRef<FTableColumn> InColumnRef);
	virtual ~FTableCellValueSorter() {}

	virtual FName GetName() const override { return Name; }
	virtual FText GetShortName() const override { return ShortName; }
	virtual FText GetTitleName() const override { return TitleName; }
	virtual FText GetDescription() const override { return Description; }
	virtual FName GetColumnId() const override { return ColumnRef->GetId(); }

	virtual FSlateBrush* GetIcon(ESortMode SortMode) const override { return SortMode == ESortMode::Ascending ? AscendingIcon : DescendingIcon; }
	virtual FTreeNodeCompareFunc GetTreeNodeCompareDelegate(ESortMode SortMode) const override { return SortMode == ESortMode::Ascending ? AscendingCompareDelegate : DescendingCompareDelegate; }

	virtual void Sort(TArray<FBaseTreeNodePtr>& NodesToSort, ESortMode SortMode) const override;

	virtual void SetAsyncOperationProgress(IAsyncOperationProgress* InAsyncOperationProgress) override { AsyncOperationProgress = InAsyncOperationProgress; }

protected:
	// Attempts to cancel the sort by throwing an exception. If exceptions are not available the return value is meant to be returned from sort predicates to speed up the sort.
	bool CancelSort() const;
	bool ShouldCancelSort() const;

protected:
	FName Name;
	FText ShortName;
	FText TitleName;
	FText Description;

	TSharedRef<FTableColumn> ColumnRef;

	FSlateBrush* AscendingIcon;
	FSlateBrush* DescendingIcon;

	FTreeNodeCompareFunc AscendingCompareDelegate;
	FTreeNodeCompareFunc DescendingCompareDelegate;

	IAsyncOperationProgress* AsyncOperationProgress = nullptr;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FBaseTableColumnSorter : public FTableCellValueSorter
{
public:
	FBaseTableColumnSorter(TSharedRef<FTableColumn> InColumnRef);
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FSorterByName : public FTableCellValueSorter
{
public:
	FSorterByName(TSharedRef<FTableColumn> InColumnRef);
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FSorterByTypeName : public FTableCellValueSorter
{
public:
	FSorterByTypeName(TSharedRef<FTableColumn> InColumnRef);
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FSorterByBoolValue : public FBaseTableColumnSorter
{
public:
	FSorterByBoolValue(TSharedRef<FTableColumn> InColumnRef);
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FSorterByInt64Value : public FBaseTableColumnSorter
{
public:
	FSorterByInt64Value(TSharedRef<FTableColumn> InColumnRef);

	virtual void Sort(TArray<FBaseTreeNodePtr>& NodesToSort, ESortMode SortMode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FSorterByFloatValue : public FBaseTableColumnSorter
{
public:
	FSorterByFloatValue(TSharedRef<FTableColumn> InColumnRef);

	virtual void Sort(TArray<FBaseTreeNodePtr>& NodesToSort, ESortMode SortMode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FSorterByDoubleValue : public FBaseTableColumnSorter
{
public:
	FSorterByDoubleValue(TSharedRef<FTableColumn> InColumnRef);

	virtual void Sort(TArray<FBaseTreeNodePtr>& NodesToSort, ESortMode SortMode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FSorterByCStringValue : public FBaseTableColumnSorter
{
public:
	FSorterByCStringValue(TSharedRef<FTableColumn> InColumnRef);
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FSorterByTextValue : public FBaseTableColumnSorter
{
public:
	FSorterByTextValue(TSharedRef<FTableColumn> InColumnRef);

	virtual void Sort(TArray<FBaseTreeNodePtr>& NodesToSort, ESortMode SortMode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FSorterByTextValueWithId : public FBaseTableColumnSorter
{
public:
	FSorterByTextValueWithId(TSharedRef<FTableColumn> InColumnRef);

	virtual void Sort(TArray<FBaseTreeNodePtr>& NodesToSort, ESortMode SortMode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights
