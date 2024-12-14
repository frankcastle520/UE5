// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorkspaceAssetRegistryInfo.h"
#include "Styling/SlateColor.h"

class UPackage;
struct FToolMenuContext;
struct FSlateBrush;

namespace UE::Workspace
{

typedef FName FOutlinerItemDetailsId;
static FOutlinerItemDetailsId MakeOutlinerDetailsId(const FWorkspaceOutlinerItemExport& InExport)
{
    return InExport.GetData().IsValid() ? InExport.GetData().GetScriptStruct()->GetFName() : NAME_None;
}

class IWorkspaceOutlinerItemDetails : public TSharedFromThis<IWorkspaceOutlinerItemDetails>
{
public:
    virtual ~IWorkspaceOutlinerItemDetails() = default;
    virtual const FSlateBrush* GetItemIcon(const FWorkspaceOutlinerItemExport& Export) const { return nullptr; }
	virtual FSlateColor GetItemColor(const FWorkspaceOutlinerItemExport& Export) const { return FSlateColor::UseForeground(); }
    virtual void HandleDoubleClick(const FToolMenuContext& ToolMenuContext) const {}
	virtual bool CanDelete(const FWorkspaceOutlinerItemExport& Export) const { return true; }
	virtual void Delete(TConstArrayView<FWorkspaceOutlinerItemExport> Exports) const {}
    virtual bool CanRename(const FWorkspaceOutlinerItemExport& Export) const { return false; }
    virtual void Rename(const FWorkspaceOutlinerItemExport& Export, const FText& InName) const {}
    virtual bool ValidateName(const FWorkspaceOutlinerItemExport& Export, const FText& InName, FText& OutErrorMessage) const { return false; }
    virtual UPackage* GetPackage(const FWorkspaceOutlinerItemExport& Export) const { return nullptr; }
	virtual bool HandleSelected(const FToolMenuContext& ToolMenuContext) const { return false; }
};

}
