// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modifiers/ActorModifierCoreDefs.h"
#include "ActorModifierCoreLibraryDefs.generated.h"

class UActorModifierCoreBase;

USTRUCT(BlueprintType)
struct FActorModifierCoreInsertOperation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="Motion Design|Modifiers|Utility")
	TSubclassOf<UActorModifierCoreBase> ModifierClass;

	UPROPERTY(BlueprintReadWrite, Category="Motion Design|Modifiers|Utility")
	EActorModifierCoreStackPosition InsertPosition = EActorModifierCoreStackPosition::Before;

	UPROPERTY(BlueprintReadWrite, Category="Motion Design|Modifiers|Utility")
	TObjectPtr<UActorModifierCoreBase> InsertPositionContext = nullptr;
};

USTRUCT(BlueprintType)
struct FActorModifierCoreCloneOperation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="Motion Design|Modifiers|Utility")
	TObjectPtr<UActorModifierCoreBase> CloneModifier = nullptr;

	UPROPERTY(BlueprintReadWrite, Category="Motion Design|Modifiers|Utility")
	EActorModifierCoreStackPosition ClonePosition = EActorModifierCoreStackPosition::Before;

	UPROPERTY(BlueprintReadWrite, Category="Motion Design|Modifiers|Utility")
	TObjectPtr<UActorModifierCoreBase> ClonePositionContext = nullptr;
};

USTRUCT(BlueprintType)
struct FActorModifierCoreMoveOperation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="Motion Design|Modifiers|Utility")
	TObjectPtr<UActorModifierCoreBase> MoveModifier = nullptr;

	UPROPERTY(BlueprintReadWrite, Category="Motion Design|Modifiers|Utility")
	EActorModifierCoreStackPosition MovePosition = EActorModifierCoreStackPosition::Before;

	UPROPERTY(BlueprintReadWrite, Category="Motion Design|Modifiers|Utility")
	TObjectPtr<UActorModifierCoreBase> MovePositionContext = nullptr;
};

USTRUCT(BlueprintType)
struct FActorModifierCoreRemoveOperation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="Motion Design|Modifiers|Utility")
	TObjectPtr<UActorModifierCoreBase> RemoveModifier = nullptr;

	UPROPERTY(BlueprintReadWrite, Category="Motion Design|Modifiers|Utility")
	bool bRemoveDependencies = false;
};