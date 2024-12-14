// Copyright Epic Games, Inc. All Rights Reserved.


#include "AnimNextRuntimeTest.h"

#include "TraitCore/TraitReader.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "Graph/RigUnit_AnimNextGraphRoot.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::AnimNext
{
	FScopedClearNodeTemplateRegistry::FScopedClearNodeTemplateRegistry()
	{
		FNodeTemplateRegistry& Registry = FNodeTemplateRegistry::Get();
		Swap(Registry, TmpRegistry);
	}

	FScopedClearNodeTemplateRegistry::~FScopedClearNodeTemplateRegistry()
	{
		FNodeTemplateRegistry& Registry = FNodeTemplateRegistry::Get();
		Swap(Registry, TmpRegistry);
	}

	bool FTestUtils::LoadFromArchiveBuffer(UAnimNextAnimationGraph& AnimationGraph, TArray<FNodeHandle>& NodeHandles, const TArray<uint8>& SharedDataArchiveBuffer)
	{
		FAnimNextGraphEvaluatorExecuteDefinition ExecuteDefinition;
		ExecuteDefinition.Hash = 0;
		ExecuteDefinition.MethodName = TEXT("Execute_0");

		// Manually add our entry point since we didn't go through a full RigVM graph
		AnimationGraph.EntryPoints.Reset();

		FAnimNextGraphEntryPoint& EntryPoint = AnimationGraph.EntryPoints.AddDefaulted_GetRef();
		EntryPoint.EntryPointName = AnimationGraph.DefaultEntryPoint;
		EntryPoint.RootTraitHandle = FAnimNextEntryPointHandle(NodeHandles[0]);
		AnimationGraph.ExecuteDefinition = ExecuteDefinition;
		AnimationGraph.SharedDataArchiveBuffer = SharedDataArchiveBuffer;
		AnimationGraph.GraphReferencedObjects.Empty();

		// Reconstruct our graph shared data
		FMemoryReader GraphSharedDataArchive(SharedDataArchiveBuffer);
		FTraitReader TraitReader(AnimationGraph.GraphReferencedObjects, GraphSharedDataArchive);

		const FTraitReader::EErrorState ErrorState = TraitReader.ReadGraph(AnimationGraph.SharedDataBuffer);
		if (ErrorState == FTraitReader::EErrorState::None)
		{
			AnimationGraph.ResolvedRootTraitHandles.Add(AnimationGraph.DefaultEntryPoint, TraitReader.ResolveEntryPointHandle(AnimationGraph.EntryPoints[0].RootTraitHandle));

			for (FNodeHandle& NodeHandle : NodeHandles)
			{
				NodeHandle = TraitReader.ResolveNodeHandle(NodeHandle);
			}

			// Make sure our execute method is registered
			FRigUnit_AnimNextGraphEvaluator::RegisterExecuteMethod(ExecuteDefinition);
			return true;
		}
		else
		{
			AnimationGraph.SharedDataBuffer.Empty(0);
			AnimationGraph.ResolvedRootTraitHandles.Add(FRigUnit_AnimNextGraphRoot::DefaultEntryPoint, FAnimNextTraitHandle());
			return false;
		}
	}
}
#endif
