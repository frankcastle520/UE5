// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowRenderingFactory.h"

#include "Dataflow/DataflowNode.h"
#include "Dataflow/DataflowInputOutput.h"
#include "Misc/MessageDialog.h"

namespace UE::Dataflow
{
	FRenderingFactory* FRenderingFactory::Instance = nullptr;

	void FRenderingFactory::RenderNodeOutput(GeometryCollection::Facades::FRenderingFacade& RenderData, const FGraphRenderingState& State)
	{
		if (CallbackMap.Contains(State.GetRenderKey()))
		{
			CallbackMap[State.GetRenderKey()]->Render(RenderData, State);
		}
		else
		{
			UE_LOG(LogChaos, Warning,
				TEXT("Warning : Dataflow missing output renderer <%s,%s> for node %s"), 
				*State.GetRenderKey().Get<0>(),
				*State.GetRenderKey().Get<1>().ToString(),
				*State.GetNodeName().ToString());
		}
	}

	bool FRenderingFactory::CanRenderNodeOutput(const FGraphRenderingState& State) const
	{
		if (CallbackMap.Contains(State.GetRenderKey()))
		{
			return CallbackMap[State.GetRenderKey()]->CanRender(State.GetViewMode());
		}

		return false;
	}

	FRenderingFactory* FRenderingFactory::GetInstance()
	{
		if (!Instance)
		{
			Instance = new FRenderingFactory();
		}
		return Instance;
	}

	void FRenderingFactory::RegisterCallbacks(TUniquePtr<ICallbackInterface> InCallbacks)
	{
		const FRenderKey Key = InCallbacks->GetRenderKey();
		if (CallbackMap.Contains(Key))
		{
			UE_LOG(LogChaos, Warning,
				TEXT("Warning : Dataflow output rendering callback registration conflicts with existing rendering callback (<%s,%s>)"),
				*Key.Get<0>(),
				*Key.Get<1>().ToString());
		}
		else
		{
			CallbackMap.Add(Key, MoveTemp(InCallbacks));
		}
	}

	void FRenderingFactory::DeregisterCallbacks(const FRenderKey& Key)
	{
		if (!CallbackMap.Contains(Key))
		{
			UE_LOG(LogChaos, Warning,
				TEXT("Warning : Dataflow output rendering callback deregistration. Rendering callback not registered: (<%s,%s>)"),
				*Key.Get<0>(),
				*Key.Get<1>().ToString());
		}
		else
		{
			CallbackMap.Remove(Key);
		}
	}


}

