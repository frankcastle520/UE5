// Copyright Epic Games, Inc. All Rights Reserved.

#include "TransformInteraction.h"
#include "XRCreativeGizmos.h"
#include "BaseGizmos/CombinedTransformGizmo.h"
#include "BaseGizmos/GizmoBaseComponent.h"
#include "BaseGizmos/GizmoViewContext.h"
#include "BaseGizmos/TransformProxy.h"
#include "BaseGizmos/TransformGizmoUtil.h"
#include "ContextObjectStore.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Engine/World.h"
#include "InteractiveGizmoManager.h"


const FString UXRCreativeTransformInteraction::GizmoBuilderIdentifier("XRCreativeGizmo");


void UXRCreativeTransformInteraction::Initialize(
	TSharedRef<FCombinedTransformGizmoActorFactory> InGizmoActorFactory,
	UTypedElementSelectionSet* InSelectionSet,
	UInteractiveGizmoManager* InGizmoManager,
	TUniqueFunction<bool()> InGizmoEnabledCallback)
{
	check(InSelectionSet && IsValid(InSelectionSet));
	check(InGizmoManager && IsValid(InGizmoManager));

	GizmoActorFactory = InGizmoActorFactory;
	WeakSelectionSet = InSelectionSet;
	WeakGizmoManager = InGizmoManager;
	GizmoEnabledCallback = MoveTemp(InGizmoEnabledCallback);

	UXRCreativeGizmoBuilder* GizmoBuilder = NewObject<UXRCreativeGizmoBuilder>();
	GizmoBuilder->GizmoActorBuilder = GizmoActorFactory;
	InGizmoManager->RegisterGizmoType(GizmoBuilderIdentifier, GizmoBuilder);

	SelectionChangedEventHandle = InSelectionSet->OnChanged().AddWeakLambda(this,
		[this](const UTypedElementSelectionSet* InSelectionSet)
		{
			UpdateGizmoTargets(InSelectionSet);
		}
	);
}


void UXRCreativeTransformInteraction::Shutdown()
{
	if (SelectionChangedEventHandle.IsValid())
	{
		if (UTypedElementSelectionSet* SelectionSet = WeakSelectionSet.Get())
		{
			SelectionSet->OnChanged().Remove(SelectionChangedEventHandle);
		}

		SelectionChangedEventHandle.Reset();
	}

	if (WeakGizmoManager.IsValid())
	{
		UpdateGizmoTargets(nullptr);
	}
}


void UXRCreativeTransformInteraction::SetEnableScaling(bool bEnable)
{
	if (bEnable != bEnableScaling)
	{
		bEnableScaling = bEnable;
		ForceUpdateGizmoState();
	}
}


void UXRCreativeTransformInteraction::SetEnableNonUniformScaling(bool bEnable)
{
	if (bEnable != bEnableNonUniformScaling)
	{
		bEnableNonUniformScaling = bEnable;
		ForceUpdateGizmoState();
	}
}


void UXRCreativeTransformInteraction::ForceUpdateGizmoState()
{
	UTypedElementSelectionSet* SelectionSet = WeakSelectionSet.Get();
	ensure(SelectionSet);
	UpdateGizmoTargets(SelectionSet);
}


void UXRCreativeTransformInteraction::UpdateGizmoTargets(const UTypedElementSelectionSet* InSelectionSet)
{
	UInteractiveGizmoManager* GizmoManager = WeakGizmoManager.Get();
	if (!ensure(GizmoManager))
	{
		return;
	}

	// destroy existing gizmos if we have any
	if (TransformGizmo != nullptr)
	{
		GizmoManager->DestroyAllGizmosByOwner(this);
		TransformGizmo = nullptr;
		TransformProxy = nullptr;
	}

	// if no selection, no gizmo
	if (!InSelectionSet || GizmoEnabledCallback() == false)
	{
		return;
	}

	TArray<AActor*> Selection = InSelectionSet->GetSelectedObjects<AActor>();
	if (Selection.Num() == 0)
	{
		return;
	}

	TransformProxy = NewObject<UTransformProxy>(this);

	for (AActor* Actor : Selection)
	{
		if (Actor && Actor->GetRootComponent())
		{
			TransformProxy->AddComponent(Actor->GetRootComponent());
		}
	}

	ETransformGizmoSubElements GizmoElements = ETransformGizmoSubElements::FullTranslateRotateScale;
	if (bEnableScaling == false)
	{
		GizmoElements = ETransformGizmoSubElements::StandardTranslateRotate;
	}
	else if (bEnableNonUniformScaling == false || Selection.Num() > 1)
	{
		// cannot non-uniform scale multiple objects
		GizmoElements = ETransformGizmoSubElements::TranslateRotateUniformScale;
	}

	//TransformGizmo = UE::TransformGizmoUtil::CreateCustomTransformGizmo(GizmoManager, GizmoElements, this);
	GizmoActorFactory->EnableElements = GizmoElements;
	TransformGizmo = CastChecked<UCombinedTransformGizmo>(GizmoManager->CreateGizmo(GizmoBuilderIdentifier, FString(), this));
	TransformGizmo->SetActiveTarget(TransformProxy);

	AXRCreativeCombinedTransformGizmoActor* NewGizmoActor =
		CastChecked<AXRCreativeCombinedTransformGizmoActor>(TransformGizmo->GetGizmoActor());
	NewGizmoActor->WeakGizmoManager = GizmoManager;

	// optionally ignore coordinate system setting
	//TransformGizmo->bUseContextCoordinateSystem = false;
	//TransformGizmo->CurrentCoordinateSystem = EToolContextCoordinateSystem::Local;
}
