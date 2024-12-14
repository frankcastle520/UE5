// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaTransitionSequenceTaskBase.h"
#include "AvaTransitionSequenceTask.generated.h"

USTRUCT()
struct FAvaTransitionSequenceTaskInstanceData : public FAvaTransitionSequenceTaskBaseInstanceData
{
	GENERATED_BODY()

	/** The wait type before this task completes */
	UPROPERTY(EditAnywhere, Category="Parameter")
	EAvaTransitionSequenceWaitType WaitType = EAvaTransitionSequenceWaitType::WaitUntilStop;
};

/** Base Task but with additional Parameters */
USTRUCT(meta=(Hidden))
struct AVALANCHESEQUENCE_API FAvaTransitionSequenceTask : public FAvaTransitionSequenceTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FAvaTransitionSequenceTaskInstanceData;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FAvaTransitionSequenceTask() = default;
	virtual ~FAvaTransitionSequenceTask() override = default;
	FAvaTransitionSequenceTask(const FAvaTransitionSequenceTask&) = default;
	FAvaTransitionSequenceTask(FAvaTransitionSequenceTask&&) = default;
	FAvaTransitionSequenceTask& operator=(const FAvaTransitionSequenceTask&) = default;
	FAvaTransitionSequenceTask& operator=(FAvaTransitionSequenceTask&&) = default;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	//~ Begin FStateTreeNodeBase
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual void PostLoad(FStateTreeDataView InInstanceDataView) override;
	//~ End FStateTreeNodeBase

	//~ Begin FAvaTransitionSequenceTaskBase
	virtual EAvaTransitionSequenceWaitType GetWaitType(FStateTreeExecutionContext& InContext) const override;
	//~ End FAvaTransitionSequenceTaskBase

	UE_DEPRECATED(5.5, "WaitType has been moved to Instance Data")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the Instance Data WaitType instead"))
	EAvaTransitionSequenceWaitType WaitType_DEPRECATED = EAvaTransitionSequenceWaitType::WaitUntilStop;
};
