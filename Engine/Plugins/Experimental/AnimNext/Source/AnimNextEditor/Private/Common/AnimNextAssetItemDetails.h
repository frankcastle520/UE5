// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IWorkspaceOutlinerItemDetails.h"

class UAnimNextRigVMAssetEditorData;
enum class EAnimNextEditorDataNotifType : uint8;

namespace UE::AnimNext::Editor
{

class FAnimNextAssetItemDetails : public UE::Workspace::IWorkspaceOutlinerItemDetails
{
public:
	FAnimNextAssetItemDetails() = default;

	virtual const FSlateBrush* GetItemIcon(const FWorkspaceOutlinerItemExport& Export) const override;

	static void RegisterToolMenuExtensions();
	static void UnregisterToolMenuExtensions();
};

}
