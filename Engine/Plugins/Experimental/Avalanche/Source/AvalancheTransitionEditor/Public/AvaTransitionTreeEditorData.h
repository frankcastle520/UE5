// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaTagHandle.h"
#include "Containers/Map.h"
#include "Delegates/Delegate.h"
#include "Misc/Guid.h"
#include "State/AvaTransitionStateMetadata.h"
#include "StateTreeEditorData.h"
#include "AvaTransitionTreeEditorData.generated.h"

class UAvaTransitionTreeEditorData;
class UStateTreeState;

namespace UE::AvaTransitionEditor
{
	constexpr FGuid ColorId_Default;
	constexpr FGuid ColorId_In(0x1DDBC788, 0xD5EB400E, 0xBB71E5DA, 0xB27A784D);
	constexpr FGuid ColorId_Out(0xE549EFA0, 0xDEFF45A7, 0xA8D907AF, 0xDB8F7643);
}

UCLASS(MinimalAPI, HideCategories=(Common))
class UAvaTransitionTreeEditorData : public UStateTreeEditorData
{
	GENERATED_BODY()

public:
	UAvaTransitionTreeEditorData();

	AVALANCHETRANSITIONEDITOR_API UStateTreeState& CreateState(const UStateTreeState& InSiblingState, bool bInAfter);

	FAvaTagHandle GetTransitionLayer() const
	{
		return TransitionLayer;
	}

	void SetTransitionLayer(const FAvaTagHandle& InTransitionLayer)
	{
		TransitionLayer = InTransitionLayer;
	}

	static FName GetTransitionLayerPropertyName()
	{
		return GET_MEMBER_NAME_CHECKED(UAvaTransitionTreeEditorData, TransitionLayer);
	}

	const FAvaTransitionStateMetadata* FindStateMetadata(const FGuid& InStateId) const
	{
		return StateMetadata.Find(InStateId);
	}

	FAvaTransitionStateMetadata& FindOrAddStateMetadata(const FGuid& InStateId)
	{
		return StateMetadata.FindOrAdd(InStateId);
	}

	FSimpleMulticastDelegate& GetOnTreeRequestRefresh()
	{
		return OnTreeRequestRefresh;
	}

private:
	/** The Layer this Transition Logic Tree deals with */
	UPROPERTY(EditAnywhere, Category="Transition Logic")
	FAvaTagHandle TransitionLayer;

	/** Map of a state's id to its metadata */
	UPROPERTY()
	TMap<FGuid, FAvaTransitionStateMetadata> StateMetadata;

	FSimpleMulticastDelegate OnTreeRequestRefresh;
};
