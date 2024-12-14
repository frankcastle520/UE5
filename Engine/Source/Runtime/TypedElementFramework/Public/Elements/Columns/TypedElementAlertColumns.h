// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Common/TypedElementHandles.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Internationalization/Text.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementAlertColumns.generated.h"

UENUM()
enum class FTypedElementAlertColumnType : uint8
{
	Error,
	Warning,

	MAX
};

/**
 * Column containing information a user needs to be alerted of.
 */
USTRUCT(meta = (DisplayName = "Alert"))
struct FTypedElementAlertColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	UPROPERTY()
	FText Message;

	// Store a copy of the parent row so it's possible to detect if a row has been reparented.
	UE::Editor::DataStorage::RowHandle CachedParent;

	UPROPERTY(meta = (IgnoreForMemberInitializationTest))
	FTypedElementAlertColumnType AlertType;
};

/**
 * Column containing a count for the number of alerts any child rows have.
 */
USTRUCT(meta = (DisplayName = "Child alert"))
struct FTypedElementChildAlertColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	// Store a copy of the parent row so it's possible to detect if a row has been reparented.
	UE::Editor::DataStorage::RowHandle CachedParent;

	uint16 Counts[static_cast<size_t>(FTypedElementAlertColumnType::MAX)];
};

/**
 * Column that can be added to an alert column to have it trigger an action when the alert is clicked.
 */
USTRUCT(meta = (DisplayName = "Alert action"))
struct FTypedElementAlertActionColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	TFunction<void(UE::Editor::DataStorage::RowHandle)> Action;
};