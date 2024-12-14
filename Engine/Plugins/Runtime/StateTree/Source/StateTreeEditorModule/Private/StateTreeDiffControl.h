// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AsyncStateTreeDiff.h"
#include "DiffUtils.h"
#include "Editor/Kismet/Private/DiffControl.h"
#include "Internationalization/Text.h"
#include "UObject/ObjectKey.h"
#include "UObject/StrongObjectPtr.h"

class FBlueprintDifferenceTreeEntry;
class FStateTreeViewModel;
class FUICommandList;
class SLinkableScrollBar;
class SStateTreeView;
class SWidget;
class UStateTree;

namespace UE::StateTree::Diff
{
struct FSingleDiffEntry;

class STATETREEEDITORMODULE_API FDiffWidgets
{
public:
	explicit FDiffWidgets(const UStateTree* InStateTree);

	/** Returns actual widget that is used to display trees */
	TSharedRef<SStateTreeView> GetStateTreeWidget() const;

private:
	TSharedPtr<SStateTreeView> StateTreeTreeView;
	TSharedPtr<FStateTreeViewModel> StateTreeViewModel;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnStateDiffEntryFocused, const FSingleDiffEntry&)

class STATETREEEDITORMODULE_API FDiffControl : public TSharedFromThis<FDiffControl>
{
public:
	FDiffControl() = delete;
	FDiffControl(const FDiffControl& Other) = delete;
	FDiffControl(const FDiffControl&& Other) = delete;
	FDiffControl(const UStateTree* InOldObject, const UStateTree* InNewObject, const FOnDiffEntryFocused& InSelectionCallback);
	~FDiffControl();

	TSharedRef<SStateTreeView> GetDetailsWidget(const UStateTree* Object) const;

	FOnStateDiffEntryFocused& GetOnStateDiffEntryFocused()
	{
		return OnStateDiffEntryFocused;
	};

	void GenerateTreeEntries(TArray<TSharedPtr<FBlueprintDifferenceTreeEntry>>& OutDifferences);
	TConstArrayView<FSingleDiffEntry> GetBindingDifferences() const
	{
		return BindingDiffs;
	}

protected:
	static FText RightRevision;
	static TSharedRef<SWidget> GenerateSingleEntryWidget(FSingleDiffEntry DiffEntry, FText ObjectName);

	TSharedRef<SStateTreeView> InsertObject(gsl::not_null<const UStateTree*> StateTree);

	void OnSelectDiffEntry(const FSingleDiffEntry StateDiff);

	FOnDiffEntryFocused OnDiffEntryFocused;
	FOnStateDiffEntryFocused OnStateDiffEntryFocused;

	TArray<TStrongObjectPtr<const UStateTree>> DisplayedAssets;

	struct FStateTreeTreeDiffPairs
	{
		TSharedPtr<FAsyncDiff> Left;
		TSharedPtr<FAsyncDiff> Right;
	};
	TMap<FObjectKey, FStateTreeTreeDiffPairs> StateTreeDifferences;
	TArray<FSingleDiffEntry> BindingDiffs;
	TMap<FObjectKey, FDiffWidgets> StateTreeDiffWidgets;
};

} // UE::StateTree::Diff