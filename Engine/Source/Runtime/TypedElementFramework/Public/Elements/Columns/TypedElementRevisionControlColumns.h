// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Common/TypedElementCommonTypes.h"

#include "TypedElementRevisionControlColumns.generated.h"

USTRUCT(meta = (DisplayName = "In a changelist"))
struct FSCCInChangelistTag final : public FEditorDataStorageTag
{
	GENERATED_BODY()
};

USTRUCT(meta = (DisplayName = "Staged"))
struct FSCCStagedTag final : public FEditorDataStorageTag
{
	GENERATED_BODY()
};

USTRUCT(meta = (DisplayName = "Locked by you"))
struct FSCCLockedTag final : public FEditorDataStorageTag
{
	GENERATED_BODY()
};

USTRUCT(meta = (DisplayName = "Edited by others"))
struct FSCCExternallyEditedTag final : public FEditorDataStorageTag
{
	GENERATED_BODY()
};

USTRUCT(meta = (DisplayName = "Not at the latest revision"))
struct FSCCNotCurrentTag final : public FEditorDataStorageTag
{
	GENERATED_BODY()
};

UENUM()
enum class ESCCModification
{
	Modified,
	Added,
	Removed,
	Conflicted,
};

USTRUCT(meta = (DisplayName = "Revision Control Status"))
struct FSCCStatusColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	UPROPERTY(meta = (IgnoreForMemberInitializationTest))
	ESCCModification Modification = ESCCModification::Modified;
};

USTRUCT()
struct FSCCRevisionId
{
	GENERATED_BODY()

	UPROPERTY(meta = (IgnoreForMemberInitializationTest))
	uint32 Id[5] = {0};
};

USTRUCT(meta = (DisplayName = "Revision ID"))
struct FSCCRevisionIdColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	UPROPERTY()
	FSCCRevisionId RevisionId; 
};

USTRUCT(meta = (DisplayName = "Revision ID from server"))
struct FSCCExternalRevisionIdColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	UPROPERTY()
	FSCCRevisionId RevisionId; 
};

USTRUCT()
struct FSCCUserInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;
};

USTRUCT(meta = (DisplayName = "Locked by others"))
struct FSCCExternallyLockedColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	UPROPERTY()
	FSCCUserInfo LockedBy;
};
