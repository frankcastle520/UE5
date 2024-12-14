// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveGizmoBuilder.h"
#include "Templates/Function.h"
#include "Templates/SubclassOf.h"

#include "GizmoUtil.generated.h"

class FString;
class UInteractiveGizmo;
class UInteractiveGizmoManager;

/**
 * Gizmo builder that simply calls a particular lambda when building a gizmo. Makes it easy to
 *  register gizmo build behavior without writing a new class.
 */
UCLASS(MinimalAPI)
class USimpleLambdaInteractiveGizmoBuilder : public UInteractiveGizmoBuilder
{
	GENERATED_BODY()

public:

	virtual UInteractiveGizmo* BuildGizmo(const FToolBuilderState& SceneState) const override
	{
		if (BuilderFunc)
		{
			return BuilderFunc(SceneState);
		}
		return nullptr;
	}

	TUniqueFunction<UInteractiveGizmo* (const FToolBuilderState& SceneState)> BuilderFunc;
};

namespace UE::GizmoUtil
{
	/**
	 * Uses the gizmo manager to create a gizmo of the given class (assuming that the gizmo type does not need
	 *  any special setup beyond instantiation) without having to register a custom builder for that class ahead of time.
	 * 
	 * This function lets the user bypass the need to define, register, and use a builder class, while still registering
	 *  the gizmo properly with the gizmo manager. Under the hood, it creates and registers a temporary generic builder, 
	 *  uses it to make the gizmo, and then immediately deregisters the builder. 
	 */
	INTERACTIVETOOLSFRAMEWORK_API UInteractiveGizmo* CreateGizmoViaSimpleBuilder(UInteractiveGizmoManager* GizmoManager,
		TSubclassOf<UInteractiveGizmo> GizmoClass, const FString& InstanceIdentifier, void* Owner);

	/**
	 * Template version of CreateGizmoViaSimpleBuilder that does a cast on return.
	 */
	template <typename GizmoClass>
	GizmoClass* CreateGizmoViaSimpleBuilder(UInteractiveGizmoManager* GizmoManager,
		const FString& InstanceIdentifier, void* GizmoOwner)
	{
		return Cast<GizmoClass>(CreateGizmoViaSimpleBuilder(GizmoManager, GizmoClass::StaticClass(), InstanceIdentifier, GizmoOwner));
	}
}