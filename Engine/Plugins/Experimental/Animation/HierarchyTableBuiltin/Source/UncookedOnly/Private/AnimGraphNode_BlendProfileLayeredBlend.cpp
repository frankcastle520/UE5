// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_BlendProfileLayeredBlend.h"
#include "ToolMenus.h"
#include "Kismet2/BlueprintEditorUtils.h"

#include "AnimGraphCommands.h"
#include "ScopedTransaction.h"

#include "DetailLayoutBuilder.h"
#include "Kismet2/CompilerResultsLog.h"

#include "HierarchyTable.h"
#include "MaskProfile/HierarchyTableTypeMask.h"

#define LOCTEXT_NAMESPACE "AnimGraphNode_BlendProfileLayeredBlend"

UAnimGraphNode_BlendProfileLayeredBlend::UAnimGraphNode_BlendProfileLayeredBlend(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FLinearColor UAnimGraphNode_BlendProfileLayeredBlend::GetNodeTitleColor() const
{
	return FLinearColor(0.75f, 0.75f, 0.75f);
}

FText UAnimGraphNode_BlendProfileLayeredBlend::GetTooltipText() const
{
	return LOCTEXT("AnimGraphNode_BlendProfileLayeredBlend_Tooltip", "Profile Blend");
}

FText UAnimGraphNode_BlendProfileLayeredBlend::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("AnimGraphNode_BlendProfileLayeredBlend_Title", "Profile Blend");
}

FString UAnimGraphNode_BlendProfileLayeredBlend::GetNodeCategory() const
{
	return TEXT("Animation|Blends");
}

void UAnimGraphNode_BlendProfileLayeredBlend::PreloadRequiredAssets()
{
	TObjectPtr<UHierarchyTable> BlendMask = Node.BlendMask;
	if (BlendMask)
	{
		PreloadObject(BlendMask);
	}

	Super::PreloadRequiredAssets();
}

void UAnimGraphNode_BlendProfileLayeredBlend::ValidateAnimNodeDuringCompilation(class USkeleton* ForSkeleton, class FCompilerResultsLog& MessageLog)
{
	UAnimGraphNode_Base::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

	bool bCompilationError = false;

	const TObjectPtr<UHierarchyTable> BlendMask = Node.BlendMask;

	if (BlendMask == nullptr)
	{
		MessageLog.Error(*LOCTEXT("InvalidMask", "@@ has a null blend profile.").ToString(), this, BlendMask);
		bCompilationError = true;
	}
	else if (BlendMask->TableType != FHierarchyTableType_Mask::StaticStruct())
	{
		MessageLog.Error(*LOCTEXT("InvalidMaskType", "@@ has a null blend profile that is not a mask type.").ToString(), this, BlendMask);
		bCompilationError = true;
	}

	if (bCompilationError)
	{
		return;
	}

	if (!Node.ArePerBoneBlendWeightsValid(ForSkeleton))
	{
		Node.RebuildPerBoneBlendWeights(ForSkeleton);
	}
}

#undef LOCTEXT_NAMESPACE
