// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaSequenceShared.h"
#include "AvaTransitionSequenceTask.h"
#include "AvaTransitionPlaySequenceTask.generated.h"

USTRUCT()
struct FAvaTransitionPlaySequenceTaskInstanceData : public FAvaTransitionSequenceTaskInstanceData
{
	GENERATED_BODY()

	/** Sequence Play Settings */
	UPROPERTY(EditAnywhere, Category="Parameter")
	FAvaSequencePlayParams PlaySettings;
};

USTRUCT(DisplayName = "Play Sequence", Category="Sequence Playback")
struct AVALANCHESEQUENCE_API FAvaTransitionPlaySequenceTask : public FAvaTransitionSequenceTask
{
	GENERATED_BODY()

	using FInstanceDataType = FAvaTransitionPlaySequenceTaskInstanceData;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FAvaTransitionPlaySequenceTask() = default;
	virtual ~FAvaTransitionPlaySequenceTask() override = default;
	FAvaTransitionPlaySequenceTask(const FAvaTransitionPlaySequenceTask&) = default;
	FAvaTransitionPlaySequenceTask(FAvaTransitionPlaySequenceTask&&) = default;
	FAvaTransitionPlaySequenceTask& operator=(const FAvaTransitionPlaySequenceTask&) = default;
	FAvaTransitionPlaySequenceTask& operator=(FAvaTransitionPlaySequenceTask&&) = default;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	//~ Begin FStateTreeNodeBase
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual void PostLoad(FStateTreeDataView InInstanceDataView) override;
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& InId, FStateTreeDataView InInstanceDataView, const IStateTreeBindingLookup& InBindingLookup, EStateTreeNodeFormatting InFormatting) const override;
#endif
	//~ End FStateTreeNodeBase

	//~ Begin FAvaTransitionSequenceTaskBase
	virtual TArray<UAvaSequencePlayer*> ExecuteSequenceTask(FStateTreeExecutionContext& InContext) const override;
	//~ End FAvaTransitionSequenceTaskBase

	UE_DEPRECATED(5.5, "PlaySettings has been moved to Instance Data")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the Instance Data PlaySettings instead"))
	FAvaSequencePlayParams PlaySettings_DEPRECATED;
};
