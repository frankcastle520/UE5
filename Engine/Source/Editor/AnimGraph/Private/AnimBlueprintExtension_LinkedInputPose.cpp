// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimBlueprintExtension_LinkedInputPose.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "IAnimBlueprintCompilerCreationContext.h"
#include "IAnimBlueprintCompilationContext.h"
#include "IAnimBlueprintGeneratedClassCompiledData.h"

void UAnimBlueprintExtension_LinkedInputPose::HandlePreProcessAnimationNodes(TArrayView<UAnimGraphNode_Base*> InAnimNodes, IAnimBlueprintCompilationContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData)
{
	for(UAnimGraphNode_Base* AnimNode : InAnimNodes)
	{
		if(UAnimGraphNode_LinkedInputPose* LinkedInputPose = Cast<UAnimGraphNode_LinkedInputPose>(AnimNode))
		{
			LinkedInputPose->AnalyzeLinks(InAnimNodes);
		}
	}
}