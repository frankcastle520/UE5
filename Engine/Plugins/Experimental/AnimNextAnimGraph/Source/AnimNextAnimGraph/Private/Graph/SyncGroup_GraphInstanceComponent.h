// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimationAsset.h"
#include "TraitCore/TraitPtr.h"
#include "TraitInterfaces/IUpdate.h"
#include "Graph/GraphInstanceComponent.h"

namespace UE::AnimNext
{
	/**
	 * FSyncGroupGraphInstanceComponent
	 *
	 * This component maintains the necessary state to support group based synchronization.
	 */
	struct FSyncGroupGraphInstanceComponent : public FGraphInstanceComponent
	{
		DECLARE_ANIM_GRAPH_INSTANCE_COMPONENT(FSyncGroupGraphInstanceComponent)

		explicit FSyncGroupGraphInstanceComponent(FAnimNextGraphInstance& InOwnerInstance);

		void RegisterWithGroup(FName GroupName, EAnimGroupRole::Type GroupRole, const FWeakTraitPtr& TraitPtr, const FTraitUpdateState& TraitState);

		// FGraphInstanceComponent impl
		virtual void PreUpdate(FExecutionContext& Context) override;
		virtual void PostUpdate(FExecutionContext& Context) override;

	private:
		struct FSyncGroupMember
		{
			FTraitUpdateState					TraitState;
			FWeakTraitPtr						TraitPtr;
			TEnumAsByte<EAnimGroupRole::Type>	GroupRole;
		};

		struct FSyncGroupState
		{
			TArray<FSyncGroupMember> Members;
		};

		TMap<FName, FSyncGroupState> SyncGroupMap;
	};
}
