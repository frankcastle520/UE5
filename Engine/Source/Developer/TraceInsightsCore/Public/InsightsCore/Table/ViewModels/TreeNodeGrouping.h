// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/StringFwd.h"
#include "Delegates/DelegateCombinations.h"
#include "Internationalization/Text.h"
#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"

#include "InsightsCore/Common/AsyncOperationProgress.h"
#include "InsightsCore/Common/SimpleRtti.h"
#include "InsightsCore/Table/ViewModels/BaseTreeNode.h"
#include "InsightsCore/Table/ViewModels/TableColumn.h"
#include "InsightsCore/Table/ViewModels/TableTreeNode.h"

#include <atomic>

struct FSlateBrush;

namespace UE::Insights
{

class FTable;

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API ITreeNodeGrouping
{
	INSIGHTS_DECLARE_RTTI_BASE(ITreeNodeGrouping)

public:
	virtual FText GetShortName() const = 0;
	virtual FText GetTitleName() const = 0;
	virtual FText GetDescription() const = 0;

	virtual FName GetBrushName() const = 0;
	virtual const FSlateBrush* GetIcon() const = 0;

	virtual FName GetColumnId() const = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

struct TRACEINSIGHTSCORE_API FTreeNodeGroupInfo
{
	FName Name;
	bool IsExpanded;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FTreeNodeGrouping : public ITreeNodeGrouping
{
	INSIGHTS_DECLARE_RTTI(FTreeNodeGrouping, ITreeNodeGrouping)

public:
	FTreeNodeGrouping(const FText& InShortName, const FText& InTitleName, const FText& InDescription, const FName InBrushName, const FSlateBrush* InIcon);
	virtual ~FTreeNodeGrouping() {}

	virtual FText GetShortName() const override { return ShortName; }
	virtual FText GetTitleName() const override { return TitleName; }
	virtual FText GetDescription() const override { return Description; }

	virtual FName GetBrushName() const override { return BrushName; }
	virtual const FSlateBrush* GetIcon() const override { return Icon; }

	virtual FName GetColumnId() const override { return NAME_None; }

	virtual FTreeNodeGroupInfo GetGroupForNode(const FBaseTreeNodePtr InNode) const { return { FName(), false }; }
	virtual void GroupNodes(const TArray<FTableTreeNodePtr>& Nodes, FTableTreeNode& ParentGroup, TWeakPtr<FTable> InParentTable, IAsyncOperationProgress& InAsyncOperationProgress) const;

protected:
	FText ShortName;
	FText TitleName;
	FText Description;
	FName BrushName;
	const FSlateBrush* Icon;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Creates a single group for all nodes. */
class TRACEINSIGHTSCORE_API FTreeNodeGroupingFlat : public FTreeNodeGrouping
{
	INSIGHTS_DECLARE_RTTI(FTreeNodeGroupingFlat, FTreeNodeGrouping)

public:
	FTreeNodeGroupingFlat();
	virtual ~FTreeNodeGroupingFlat() {}

	virtual void GroupNodes(const TArray<FTableTreeNodePtr>& Nodes, FTableTreeNode& ParentGroup, TWeakPtr<FTable> InParentTable, IAsyncOperationProgress& InAsyncOperationProgress) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Creates a group for each unique value. */
class TRACEINSIGHTSCORE_API FTreeNodeGroupingByUniqueValue : public FTreeNodeGrouping
{
	INSIGHTS_DECLARE_RTTI(FTreeNodeGroupingByUniqueValue, FTreeNodeGrouping);

public:
	FTreeNodeGroupingByUniqueValue(TSharedRef<FTableColumn> InColumnRef);
	virtual ~FTreeNodeGroupingByUniqueValue() {}

	virtual FTreeNodeGroupInfo GetGroupForNode(const FBaseTreeNodePtr InNode) const override;

	virtual FName GetColumnId() const override { return ColumnRef->GetId(); }
	TSharedRef<FTableColumn> GetColumn() const { return ColumnRef; }

private:
	TSharedRef<FTableColumn> ColumnRef;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Creates a group for each unique value (assumes data type of cell values is a simple type). */
template<typename Type>
class TTreeNodeGroupingByUniqueValue : public FTreeNodeGroupingByUniqueValue
{
public:
	TTreeNodeGroupingByUniqueValue(TSharedRef<FTableColumn> InColumnRef) : FTreeNodeGroupingByUniqueValue(InColumnRef) {}
	virtual ~TTreeNodeGroupingByUniqueValue() {}

	virtual void GroupNodes(const TArray<FTableTreeNodePtr>& Nodes, FTableTreeNode& ParentGroup, TWeakPtr<FTable> InParentTable, IAsyncOperationProgress& InAsyncOperationProgress) const override;

private:
	static Type GetValue(const FTableCellValue& CellValue);
	static FName GetGroupName(const FTableColumn& Column, const FTableTreeNode& Node);
};

template<typename Type>
FName TTreeNodeGroupingByUniqueValue<Type>::GetGroupName(const FTableColumn& Column, const FTableTreeNode& Node)
{
	FText ValueAsText = Column.GetValueAsGroupingText(Node);

	if (ValueAsText.IsEmpty())
	{
		static FName EmptyGroupName(TEXT("N/A"));
		return EmptyGroupName;
	}

	FStringView StringView(ValueAsText.ToString());
	if (StringView.Len() >= NAME_SIZE)
	{
		StringView = FStringView(StringView.GetData(), NAME_SIZE - 1);
	}
	return FName(StringView, 0);
}

template<typename Type>
void TTreeNodeGroupingByUniqueValue<Type>::GroupNodes(const TArray<FTableTreeNodePtr>& Nodes, FTableTreeNode& ParentGroup, TWeakPtr<FTable> InParentTable, IAsyncOperationProgress& InAsyncOperationProgress) const
{
	TMap<Type, FTableTreeNodePtr> GroupMap;
	FTableTreeNodePtr UnsetGroupPtr = nullptr;

	ParentGroup.ClearChildren();

	for (FTableTreeNodePtr NodePtr : Nodes)
	{
		if (InAsyncOperationProgress.ShouldCancelAsyncOp())
		{
			return;
		}

		if (NodePtr->IsGroup())
		{
			ParentGroup.AddChildAndSetParent(NodePtr);
			continue;
		}

		FTableTreeNodePtr GroupPtr = nullptr;

		const FTableColumn& Column = *GetColumn();
		const TOptional<FTableCellValue> CellValue = Column.GetValue(*NodePtr);
		if (CellValue.IsSet())
		{
			const Type Value = GetValue(CellValue.GetValue());

			FTableTreeNodePtr* GroupPtrPtr = GroupMap.Find(Value);
			if (!GroupPtrPtr)
			{
				const FName GroupName = GetGroupName(Column, *NodePtr);
				GroupPtr = MakeShared<FTableTreeNode>(GroupName, InParentTable);
				GroupPtr->SetExpansion(false);
				ParentGroup.AddChildAndSetParent(GroupPtr);
				GroupMap.Add(Value, GroupPtr);
			}
			else
			{
				GroupPtr = *GroupPtrPtr;
			}
		}
		else
		{
			if (!UnsetGroupPtr)
			{
				UnsetGroupPtr = MakeShared<FTableTreeNode>(FName(TEXT("<unset>")), InParentTable);
				UnsetGroupPtr->SetExpansion(false);
				ParentGroup.AddChildAndSetParent(UnsetGroupPtr);
			}
			GroupPtr = UnsetGroupPtr;
		}

		GroupPtr->AddChildAndSetParent(NodePtr);
	}
}

typedef TTreeNodeGroupingByUniqueValue<bool> FTreeNodeGroupingByUniqueValueBool;
typedef TTreeNodeGroupingByUniqueValue<int64> FTreeNodeGroupingByUniqueValueInt64;
typedef TTreeNodeGroupingByUniqueValue<float> FTreeNodeGroupingByUniqueValueFloat;
typedef TTreeNodeGroupingByUniqueValue<double> FTreeNodeGroupingByUniqueValueDouble;

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Creates a group for each unique value (assumes data type of cell values is const TCHAR*). */
class TRACEINSIGHTSCORE_API FTreeNodeGroupingByUniqueValueCString : public FTreeNodeGroupingByUniqueValue
{
public:
	FTreeNodeGroupingByUniqueValueCString(TSharedRef<FTableColumn> InColumnRef) : FTreeNodeGroupingByUniqueValue(InColumnRef) {}
	virtual ~FTreeNodeGroupingByUniqueValueCString() {}

	virtual void GroupNodes(const TArray<FTableTreeNodePtr>& Nodes, FTableTreeNode& ParentGroup, TWeakPtr<FTable> InParentTable, IAsyncOperationProgress& InAsyncOperationProgress) const override;

private:
	static FName GetGroupName(const TCHAR* Value);
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Creates a group for each first letter of node names. */
class TRACEINSIGHTSCORE_API FTreeNodeGroupingByNameFirstLetter : public FTreeNodeGrouping
{
	INSIGHTS_DECLARE_RTTI(FTreeNodeGroupingByNameFirstLetter, FTreeNodeGrouping);

public:
	FTreeNodeGroupingByNameFirstLetter();
	virtual ~FTreeNodeGroupingByNameFirstLetter() {}

	virtual FTreeNodeGroupInfo GetGroupForNode(const FBaseTreeNodePtr InNode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Creates a group for each type. */
class TRACEINSIGHTSCORE_API FTreeNodeGroupingByType : public FTreeNodeGrouping
{
	INSIGHTS_DECLARE_RTTI(FTreeNodeGroupingByType, FTreeNodeGrouping);

public:
	FTreeNodeGroupingByType();
	virtual ~FTreeNodeGroupingByType() {}

	virtual FTreeNodeGroupInfo GetGroupForNode(const FBaseTreeNodePtr InNode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Creates a tree hierarchy out of the path structure of string values. */
class TRACEINSIGHTSCORE_API FTreeNodeGroupingByPathBreakdown : public FTreeNodeGrouping
{
	INSIGHTS_DECLARE_RTTI(FTreeNodeGroupingByPathBreakdown, FTreeNodeGrouping);

public:
	FTreeNodeGroupingByPathBreakdown(TSharedRef<FTableColumn> InColumnRef);
	virtual ~FTreeNodeGroupingByPathBreakdown() {}

	virtual void GroupNodes(const TArray<FTableTreeNodePtr>& Nodes, FTableTreeNode& ParentGroup, TWeakPtr<FTable> InParentTable, IAsyncOperationProgress& InAsyncOperationProgress) const override;

	virtual FName GetColumnId() const override { return ColumnRef->GetId(); }
	TSharedRef<FTableColumn> GetColumn() const { return ColumnRef; }

private:
	TSharedRef<FTableColumn> ColumnRef;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights
