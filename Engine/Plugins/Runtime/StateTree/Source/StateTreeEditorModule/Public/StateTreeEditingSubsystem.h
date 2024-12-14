// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTree.h"
#include "StateTreeViewModel.h"
#include "EditorSubsystem.h"
#include "UObject/ObjectKey.h"

#include "StateTreeEditingSubsystem.generated.h"

class SWidget;
class FStateTreeViewModel;
class FUICommandList;
struct FStateTreeCompilerLog;

UCLASS()
class STATETREEEDITORMODULE_API UStateTreeEditingSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()
public:
	UStateTreeEditingSubsystem();
	virtual void BeginDestroy() override;
	
	TSharedRef<FStateTreeViewModel> FindOrAddViewModel(const gsl::not_null<UStateTree*> InStateTree);
	
	static bool CompileStateTree(const gsl::not_null<UStateTree*> InStateTree,  FStateTreeCompilerLog& InOutLog);
	
	/** Create a StateTreeView widget for the viewmodel. */
	static TSharedRef<SWidget> GetStateTreeView(TSharedRef<FStateTreeViewModel> InViewModel, const TSharedRef<FUICommandList>& TreeViewCommandList);
	
	/**
	 * Validates and applies the schema restrictions on the StateTree.
	 * Updates state's link, removes the unused node while validating the StateTree asset.
	 */
	static void ValidateStateTree(const gsl::not_null<UStateTree*> InStateTree);

	/** Calculates editor data hash of the asset. */
	static uint32 CalculateStateTreeHash(const gsl::not_null<const UStateTree*> InStateTree);
	
private:
	void HandlePostGarbageCollect();

protected:
	TMap<FObjectKey, TSharedPtr<FStateTreeViewModel>> StateTreeViewModels;
	FDelegateHandle PostGarbageCollectHandle;
};
