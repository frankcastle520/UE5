// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "ReferencePose.h"

struct FAnimNextGraphInstancePtr;
struct FAnimNextVariableBindingData;
struct FAnimNextGraphLODPose;

namespace UE::AnimNext
{
	class IParameterSource;
	struct FParameterSourceContext;
	struct FTraitEventList;
}

namespace UE::AnimNext
{

class IAnimNextAnimGraph
{
public:
	virtual ~IAnimNextAnimGraph() = default;

	virtual void UpdateGraph(const FAnimNextGraphInstancePtr& GraphInstance, float DeltaTime, FTraitEventList& InputEventList, FTraitEventList& OutputEventList) const = 0;
	virtual void EvaluateGraph(const FAnimNextGraphInstancePtr& GraphInstance, const FReferencePose& RefPose, int32 GraphLODLevel, FAnimNextGraphLODPose& OutputPose) const = 0;
};

class IAnimNextModuleInterface : public IModuleInterface
{
public:
	// WARNING
	// These functions are here because the schedule currently cannot have tasks defined in
	// external plugins. And as such, we must be able to update and evaluate graphs from
	// AnimNext even though they are implementation details of the AnimNextAnimGraph.
	// We thus introduce this interface.
	// When AnimNextAnimGraph's module will load, it will register callbacks that
	// AnimNext will call when we need to update/evaluate graphs.
	static ANIMNEXT_API IAnimNextModuleInterface& Get();
	virtual void RegisterAnimNextAnimGraph(const IAnimNextAnimGraph& InAnimGraphImpl) = 0;
	virtual void UnregisterAnimNextAnimGraph() = 0;
	virtual void UpdateGraph(const FAnimNextGraphInstancePtr& GraphInstance, float DeltaTime, FTraitEventList& InputEventList, FTraitEventList& OutputEventList) = 0;
	virtual void EvaluateGraph(const FAnimNextGraphInstancePtr& GraphInstance, const FReferencePose& RefPose, int32 GraphLODLevel, FAnimNextGraphLODPose& OutputPose) const = 0;
};

}
