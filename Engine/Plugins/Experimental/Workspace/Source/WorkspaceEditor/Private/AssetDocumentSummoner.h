// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WorkflowOrientedApp/WorkflowUObjectDocuments.h"

class FWorkflowCentricApplication;
class SGraphEditor;

namespace UE::Workspace
{
	class FWorkspaceEditor;
}

namespace UE::Workspace
{

struct FAssetDocumentSummoner : public FDocumentTabFactoryForObjects<UObject>
{
public:
	// Delegate called to save the state of a document
	DECLARE_DELEGATE_OneParam(FOnSaveDocumentState, UObject*);

	FAssetDocumentSummoner(FName InIdentifier, TSharedPtr<FWorkspaceEditor> InHostingApp, bool bInAllowUnsupportedClasses = false);

	void SetAllowedClassPaths(TConstArrayView<FTopLevelAssetPath> InAllowedClassPaths);

private:
	// FWorkflowTabFactory interface
	virtual void OnTabActivated(TSharedPtr<SDockTab> Tab) const override;
	virtual void OnTabBackgrounded(TSharedPtr<SDockTab> Tab) const override;
	virtual void OnTabRefreshed(TSharedPtr<SDockTab> Tab) const override;
	virtual void SaveState(TSharedPtr<SDockTab> Tab, TSharedPtr<FTabPayload> Payload) const override;
	virtual bool IsPayloadSupported(TSharedRef<FTabPayload> Payload) const override;
	virtual TAttribute<FText> ConstructTabLabelSuffix(const FWorkflowTabSpawnInfo& Info) const override;

	// FDocumentTabFactoryForObjects interface
	virtual TAttribute<FText> ConstructTabNameForObject(UObject* DocumentID) const override;
	virtual const FSlateBrush* GetTabIconForObject(const FWorkflowTabSpawnInfo& Info, UObject* DocumentID) const override;
	virtual TSharedRef<SWidget> CreateTabBodyForObject(const FWorkflowTabSpawnInfo& Info, UObject* DocumentID) const override;

	// The hosting app
	TWeakPtr<FWorkspaceEditor> HostingAppPtr;

	// Command list
	TSharedPtr<FUICommandList> CommandList;

	// Allowed object types
	TArray<FTopLevelAssetPath> AllowedClassPaths;

	// Whether or not to allow objects if AllowedClassPaths does not contain their class
	bool bAllowUnsupportedClasses = false;
};

}
