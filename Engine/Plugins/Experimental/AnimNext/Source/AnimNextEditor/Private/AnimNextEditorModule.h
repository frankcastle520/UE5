// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IAnimNextEditorModule.h"
#include "UObject/TopLevelAssetPath.h"

class FAnimNextGraphPanelNodeFactory;
class UAnimNextWorkspaceSchema;

namespace UE::AnimNext::Editor
{
	class FParamNamePropertyTypeIdentifier;
	class FParametersGraphPanelPinFactory;
	class FLocatorContext;
}

namespace UE::Workspace
{
	class IWorkspaceEditorModule;
}

namespace UE::AnimNext::Editor
{

class FAnimNextEditorModule : public IAnimNextEditorModule
{
	friend UAnimNextWorkspaceSchema;
private:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// IAnimNextEditorModule interface
	virtual void RegisterLocatorFragmentEditorType(FName InLocatorFragmentEditorName) override;
	virtual void UnregisterLocatorFragmentEditorType(FName InLocatorFragmentEditorName) override;
	virtual void AddWorkspaceSupportedAssetClass(const FTopLevelAssetPath& InClassAssetPath) override;
	virtual void RemoveWorkspaceSupportedAssetClass(const FTopLevelAssetPath& InClassAssetPath) override;
private:
	void RegisterWorkspaceDocumentTypes(Workspace::IWorkspaceEditorModule& WorkspaceEditorModule);
	void UnregisterWorkspaceDocumentTypes();

private:
	/** Node factory for the AnimNext graph */
	TSharedPtr<FAnimNextGraphPanelNodeFactory> AnimNextGraphPanelNodeFactory;

	/** Type identifier for parameter names */
	TSharedPtr<FParamNamePropertyTypeIdentifier> Identifier;

	/** Registered names for locator fragments */
	TSet<FName> LocatorFragmentEditorNames;

	TArray<FTopLevelAssetPath> SupportedAssetClasses;

	friend class FLocatorContext;
};

}
