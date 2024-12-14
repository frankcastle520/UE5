// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "Module/AnimNextModuleContextData.h"
#include "Graph/AnimNextGraphContextData.h"
#include "Graph/AnimNextGraphInstance.h"
#include "Misc/TVariant.h"
#include "Module/AnimNextModuleInstance.h"
#include "AnimNextExecuteContext.generated.h"

struct FAnimNextDataInterfaceInstance;
struct FAnimNextGraphInstance;

namespace UE::AnimNext
{
	struct FLatentPropertyHandle;
	struct FModuleEventTickFunction;
}

USTRUCT(BlueprintType)
struct FAnimNextExecuteContext : public FRigVMExecuteContext
{
	GENERATED_BODY()

	FAnimNextExecuteContext() = default;

	virtual void Copy(const FRigVMExecuteContext* InOtherContext) override
	{
		Super::Copy(InOtherContext);

		const FAnimNextExecuteContext* OtherContext = static_cast<const FAnimNextExecuteContext*>(InOtherContext);
		ContextData = OtherContext->ContextData;
	}

	// Get the context data as the specified type. This will assert if the type differs from the last call to SetContextData.
	template<typename ContextType>
	const ContextType& GetContextData() const
	{
		return ContextData.Get<ContextType>();
	}

	// Get the current data interface instance (module or graph) that is executing 
	const FAnimNextDataInterfaceInstance& GetInstance() const
	{
		if(ContextData.IsType<FAnimNextModuleContextData>())
		{
			return ContextData.Get<FAnimNextModuleContextData>().GetModuleInstance();
		}
		return ContextData.Get<FAnimNextGraphContextData>().GetGraphInstance();
	}

protected:
	// Setup the context data to the specified type
	template<typename ContextType, typename... ArgsType>
	void SetContextData(ArgsType&&... Args)
	{
		ContextData.Emplace<ContextType>(Forward<ArgsType>(Args)...);
	}

	// Call this to reset the context to its original state to detect stale usage, can't call it Reset due to virtual in base with that name
	template<typename ContextType>
	void DebugReset()
	{
		ContextData.Get<ContextType>().Reset();
	}

	// All possible known variants of our context data. IF we ever want this to be extensible, this can be converted into an FInstancedStruct
	TVariant<FAnimNextModuleContextData, FAnimNextGraphContextData> ContextData;

	friend struct UE::AnimNext::FModuleEventTickFunction;
	friend struct FAnimNextGraphInstance;
	friend class UAnimNextAnimationGraph;
};

