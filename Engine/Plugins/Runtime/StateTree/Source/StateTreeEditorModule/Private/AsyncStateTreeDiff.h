// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AsyncTreeDifferences.h"

class SStateTreeView;
class UStateTreeState;

namespace UE::StateTree::Diff
{
struct FSingleDiffEntry;

class STATETREEEDITORMODULE_API FAsyncDiff : public TAsyncTreeDifferences<TWeakObjectPtr<UStateTreeState>>
{
public:
	FAsyncDiff(const TSharedRef<SStateTreeView>& LeftTree, const TSharedRef<SStateTreeView>& RightTree);

	void GetStateTreeDifferences(TArray<FSingleDiffEntry>& OutDiffEntries) const;

private:
	void GetStatesDifferences(TArray<FSingleDiffEntry>& OutDiffEntries) const;

	static TAttribute<TArray<TWeakObjectPtr<UStateTreeState>>> RootNodesAttribute(TWeakPtr<SStateTreeView> StateTreeView);

	TSharedPtr<SStateTreeView> LeftView;
	TSharedPtr<SStateTreeView> RightView;
};

} // UE::StateTree::Diff

template <>
class STATETREEEDITORMODULE_API TTreeDiffSpecification<TWeakObjectPtr<UStateTreeState>>
{
public:
	bool AreValuesEqual(const TWeakObjectPtr<UStateTreeState>& StateTreeNodeA, const TWeakObjectPtr<UStateTreeState>& StateTreeNodeB, TArray<FPropertySoftPath>* OutDifferingProperties = nullptr) const;

	bool AreMatching(const TWeakObjectPtr<UStateTreeState>& StateTreeNodeA, const TWeakObjectPtr<UStateTreeState>& StateTreeNodeB, TArray<FPropertySoftPath>* OutDifferingProperties = nullptr) const;

	void GetChildren(const TWeakObjectPtr<UStateTreeState>& InParent, TArray<TWeakObjectPtr<UStateTreeState>>& OutChildren) const;

	bool ShouldMatchByValue(const TWeakObjectPtr<UStateTreeState>&) const
	{
		return false;
	}

	bool ShouldInheritEqualFromChildren(const TWeakObjectPtr<UStateTreeState>&, const TWeakObjectPtr<UStateTreeState>&) const
	{
		return false;
	}
};