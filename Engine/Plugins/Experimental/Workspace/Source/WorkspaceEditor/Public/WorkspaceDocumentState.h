// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorkspaceDocumentState.generated.h"

// Base struct used to persist workspace document state
USTRUCT()
struct FWorkspaceDocumentState
{
	GENERATED_BODY()

	FWorkspaceDocumentState() = default;

	explicit FWorkspaceDocumentState(const UObject* InObject)
		: Object(InObject)
	{}

	UPROPERTY()
	FSoftObjectPath Object;

	friend bool operator==(const FWorkspaceDocumentState& LHS, const FWorkspaceDocumentState& RHS)
	{
		return LHS.Object == RHS.Object;
	}
};
