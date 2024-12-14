// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAnimNextModuleInterface.h"

struct FAnimNextGraphInstancePtr;
struct FAnimNextVariableBindingData;

namespace UE::AnimNext
{
	struct FParameterSourceContext;
}

// Enable console commands only in development builds when logging is enabled
#define WITH_ANIMNEXT_CONSOLE_COMMANDS (!UE_BUILD_SHIPPING && !NO_LOGGING)

namespace UE::AnimNext
{

class FAnimNextModuleImpl : public IAnimNextModuleInterface
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// IAnimNextModuleInterface interface
	virtual void RegisterAnimNextAnimGraph(const IAnimNextAnimGraph& InAnimGraphImpl) override;
	virtual void UnregisterAnimNextAnimGraph() override;
	virtual void UpdateGraph(const FAnimNextGraphInstancePtr& GraphInstance, float DeltaTime, FTraitEventList& InputEventList, FTraitEventList& OutputEventList) override;
	virtual void EvaluateGraph(const FAnimNextGraphInstancePtr& GraphInstance, const FReferencePose& RefPose, int32 GraphLODLevel, FAnimNextGraphLODPose& OutputPose) const override;

#if WITH_ANIMNEXT_CONSOLE_COMMANDS
	void ListNodeTemplates(const TArray<FString>& Args);
	void ListAnimationGraphs(const TArray<FString>& Args);

	TArray<IConsoleObject*> ConsoleCommands;
#endif
};

}
