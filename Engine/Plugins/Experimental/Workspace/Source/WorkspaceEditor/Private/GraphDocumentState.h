// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorkspaceDocumentState.h"
#include "GraphDocumentState.generated.h"

// Base struct used to persist workspace document state
USTRUCT()
struct FGraphDocumentState : public FWorkspaceDocumentState
{
	GENERATED_BODY()

	FGraphDocumentState() = default;

	FGraphDocumentState(const UObject* InObject, const FVector2D& InViewLocation, float InZoomAmount)
		: FWorkspaceDocumentState(InObject)
		, ViewLocation(InViewLocation)
		, ZoomAmount(InZoomAmount)
	{}

	UPROPERTY()
	FVector2D ViewLocation = FVector2D::Zero();

	UPROPERTY()
	float ZoomAmount = 0.f;
};
