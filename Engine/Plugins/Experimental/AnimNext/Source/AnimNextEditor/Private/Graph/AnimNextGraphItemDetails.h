// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IWorkspaceOutlinerItemDetails.h"

namespace UE::AnimNext::Editor
{

class FAnimNextGraphItemDetails : public UE::Workspace::IWorkspaceOutlinerItemDetails
{
public:
	FAnimNextGraphItemDetails() = default;
	virtual ~FAnimNextGraphItemDetails() override = default;
	virtual void HandleDoubleClick(const FToolMenuContext& ToolMenuContext) const override;
	virtual bool CanDelete(const FWorkspaceOutlinerItemExport& Export) const override;
	virtual void Delete(TConstArrayView<FWorkspaceOutlinerItemExport> Exports) const override; 
	virtual bool CanRename(const FWorkspaceOutlinerItemExport& Export) const override;
	virtual void Rename(const FWorkspaceOutlinerItemExport& Export, const FText& InName) const override;
	virtual bool ValidateName(const FWorkspaceOutlinerItemExport& Export, const FText& InName, FText& OutErrorMessage) const override;
	virtual UPackage* GetPackage(const FWorkspaceOutlinerItemExport& Export) const override;
	virtual const FSlateBrush* GetItemIcon(const FWorkspaceOutlinerItemExport& Export) const override;

	static void RegisterToolMenuExtensions();
	static void UnregisterToolMenuExtensions();
};

}
