// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SRigVMChangesTreeView.h"
#include "Widgets/SRigVMLogWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Input/SSearchBox.h"
#include "Interfaces/IMainFrameModule.h"
#include "ScopedTransaction.h"

DECLARE_DELEGATE_OneParam(FOnPhaseActivated, TSharedRef<FRigVMTreePhase>);

class RIGVMEDITOR_API SRigVMBulkEditWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRigVMBulkEditWidget)
		: _EnableUndo(false)
		, _CloseOnSuccess(false)
		, _PhaseToActivate(INDEX_NONE)
		, _BulkEditConfirmIniField(TEXT("BulkEdit_Warning"))
	{}
	SLATE_ARGUMENT(bool, EnableUndo)
	SLATE_ARGUMENT(bool, CloseOnSuccess)
	SLATE_ARGUMENT(TArray<TSharedRef<FRigVMTreePhase>>, Phases)
	SLATE_ARGUMENT(int32, PhaseToActivate)
	SLATE_EVENT(FOnRigVMTreeNodeSelected, OnNodeSelected)
	SLATE_EVENT(FOnRigVMTreeNodeDoubleClicked, OnNodeDoubleClicked)
	SLATE_EVENT(FOnPhaseActivated, OnPhaseActivated)
	SLATE_ARGUMENT(TSharedPtr<SWidget>, HeaderWidget)
	SLATE_ARGUMENT(TSharedPtr<SWidget>, FooterWidget)
	SLATE_ARGUMENT(TSharedPtr<SWidget>, LeftWidget)
	SLATE_ARGUMENT(TSharedPtr<SWidget>, RightWidget)
	SLATE_ATTRIBUTE(FText, BulkEditTitle)
	SLATE_ATTRIBUTE(FText, BulkEditConfirmMessage)
	SLATE_ATTRIBUTE(FString, BulkEditConfirmIniField)
	SLATE_END_ARGS()

	virtual ~SRigVMBulkEditWidget() override;

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	FText GetDialogTitle() const;

	void QueueTasks(const TArray<TSharedRef<FRigVMTreeTask>>& InTasks);
	void CancelTasks();

	TSharedRef<SRigVMBulkEditWidget> GetBulkEditWidget()
	{
		return SharedThis(this);
	}

	TSharedPtr<SRigVMChangesTreeView> GetTreeView()
	{
		return TreeView;
	}

	TSharedPtr<SRigVMLogWidget> GetLogWidget()
	{
		return BulkEditLogWidget;
	}

	TSharedPtr<FRigVMTreePhase> GetActivePhasePtr() const;
	TSharedRef<FRigVMTreePhase> GetActivePhase() const;
	TSharedPtr<FRigVMTreePhase> FindPhase(int32 InID) const;
	bool ActivatePhase(int32 InID);
	TSharedRef<FRigVMTreeContext> GetContext() const;

	TArray<TSharedRef<FRigVMTreeNode>> GetSelectedNodes() const;
	bool HasAnyVisibleCheckedNode() const;
	TArray<TSharedRef<FRigVMTreeNode>> GetCheckedNodes() const;
	
	FReply OnBackButtonClicked();
	FReply OnCancelButtonClicked();
	FReply OnPrimaryButtonClicked();

	void CloseDialog();

private:

	TArray<TSharedRef<FRigVMTreePhase>> Phases;
	TArray<int32> ActivatedPhaseIDs;
	
	TSharedPtr<SRigVMChangesTreeView> TreeView;
	TSharedPtr<SRigVMLogWidget> BulkEditLogWidget;

	FOnPhaseActivated OnPhaseActivated;

	TAttribute<FText> BulkEditTitle;
	TAttribute<FText> BulkEditConfirmMessage;
	TAttribute<FString> BulkEditConfirmIniField;

	bool AreTasksInProgress() const;
	EVisibility GetTasksProgressVisibility() const;
	TOptional<float> GetTasksProgressPercentage() const;

	void OnLogMessage(const TSharedRef<FTokenizedMessage>& InMessage) const;
	void OnScriptException(ELogVerbosity::Type InVerbosity, const TCHAR* InMessage, const TCHAR* InStackMessage);

	mutable FCriticalSection TasksCriticalSection;
	TArray<TSharedRef<FRigVMTreeTask>> RemainingTasks;
	TArray<TSharedRef<FRigVMTreeTask>> CompletedTasks;
	bool bTasksSucceeded = false;
	bool bShowLog = false;
	bool bEnableUndo = false;
	bool bCloseOnSuccess = false;
	TSharedPtr<FScopedTransaction> Transaction;

	EVisibility GetBackButtonVisibility() const;
	bool IsBackButtonEnabled() const;
	EVisibility GetCancelButtonVisibility() const;
	bool IsCancelButtonEnabled() const;
	EVisibility GetPrimaryButtonVisibility() const;
	bool IsPrimaryButtonEnabled() const;
	FText GetPrimaryButtonText() const;
	bool IsReadyToClose() const;
};
