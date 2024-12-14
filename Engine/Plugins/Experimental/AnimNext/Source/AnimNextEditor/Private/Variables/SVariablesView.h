// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ContentBrowserDelegates.h"
#include "SSceneOutliner.h"
#include "Widgets/SCompoundWidget.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"

enum class EAnimNextEditorDataNotifType : uint8;
class UAnimNextRigVMAsset;
struct FWorkspaceOutlinerItemExport;
class SPositiveActionButton;
class UAnimNextRigVMAssetEditorData;

namespace UE::Workspace
{
	class IWorkspaceEditor;
}

namespace UE::AnimNext::Editor
{
	class SVariablesOutliner;
}

namespace UE::AnimNext::Editor
{

extern const FLazyName VariablesTabName;

class SVariablesOutliner : public SSceneOutliner
{
public:
	void Construct(const FArguments& InArgs, const FSceneOutlinerInitializationOptions& InitOptions);

	virtual void CustomAddToToolbar(TSharedPtr<SHorizontalBox> Toolbar) override;

	FReply HandleAddVariablesClicked();

	void SetAssets(TConstArrayView<TSoftObjectPtr<UAnimNextRigVMAsset>> InAssets);

	void OnEditorDataModified(UAnimNextRigVMAssetEditorData* InEditorData, EAnimNextEditorDataNotifType InType, UObject* InSubject);

	void HandleAssetLoaded(const FSoftObjectPath& InSoftObjectPath, UAnimNextRigVMAsset* InAsset);
	
	bool IsEnabled() const;
private:
	friend class FVariablesOutlinerMode;
	friend class FVariablesOutlinerHierarchy;

	TArray<TSoftObjectPtr<UAnimNextRigVMAsset>> Assets;
	TSharedPtr<SPositiveActionButton> AddVariablesButton;
};

class SVariablesView : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SVariablesView) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<UE::Workspace::IWorkspaceEditor> InWorkspaceEditor);

private:
	void HandleWorkspaceOutlinerSelectionChanged(TConstArrayView<FWorkspaceOutlinerItemExport> InExports);

private:
	friend struct FAnimNextVariablesTabSummoner;
	
	TSharedPtr<SVariablesOutliner> VariablesOutliner;
};

struct FAnimNextVariablesTabSummoner : public FWorkflowTabFactory
{
public:
	FAnimNextVariablesTabSummoner(TSharedPtr<UE::Workspace::IWorkspaceEditor> InHostingApp);

private:
	// FWorkflowTabFactory interface
	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;
	virtual FText GetTabToolTipText(const FWorkflowTabSpawnInfo& Info) const override;

	// The widget this tab spawner wraps
	TSharedPtr<SVariablesView> VariablesView;
};


};