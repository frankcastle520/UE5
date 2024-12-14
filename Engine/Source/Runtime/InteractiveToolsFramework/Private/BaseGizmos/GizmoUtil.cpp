// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/GizmoUtil.h"

#include "InteractiveGizmoManager.h"
#include "InteractiveGizmo.h"

UInteractiveGizmo* UE::GizmoUtil::CreateGizmoViaSimpleBuilder(UInteractiveGizmoManager* GizmoManager, 
	TSubclassOf<UInteractiveGizmo> GizmoClass, const FString& InstanceIdentifier, void* Owner)
{
	if (!ensure(GizmoManager))
	{
		return nullptr;
	}

	const FString BuilderIdentifier = TEXT("__CreateGizmoViaSimpleBuilder_TemporaryBuilder");

	USimpleLambdaInteractiveGizmoBuilder* Builder = NewObject<USimpleLambdaInteractiveGizmoBuilder>();
	Builder->BuilderFunc = [GizmoManager, GizmoClass](const FToolBuilderState& SceneState) { return NewObject<UInteractiveGizmo>(GizmoManager, GizmoClass.Get()); };

	GizmoManager->RegisterGizmoType(BuilderIdentifier, Builder);
	UInteractiveGizmo* Gizmo = GizmoManager->CreateGizmo(BuilderIdentifier, InstanceIdentifier, Owner);
	GizmoManager->DeregisterGizmoType(BuilderIdentifier);

	return Gizmo;
}
