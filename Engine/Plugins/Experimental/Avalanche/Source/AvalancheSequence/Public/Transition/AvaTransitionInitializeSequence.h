// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaSequenceShared.h"
#include "AvaTransitionSequenceTask.h"
#include "AvaTransitionInitializeSequence.generated.h"

USTRUCT()
struct FAvaTransitionInitSequenceTaskInstanceData : public FAvaTransitionSequenceTaskBaseInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Parameter")
	FAvaSequenceTime InitializeTime = FAvaSequenceTime(0.0);

	UPROPERTY(EditAnywhere, Category = "Parameter")
	EAvaSequencePlayMode PlayMode = EAvaSequencePlayMode::Forward;
};

USTRUCT(DisplayName = "Initialize Sequence", Category = "Sequence Playback")
struct AVALANCHESEQUENCE_API FAvaTransitionInitializeSequence : public FAvaTransitionSequenceTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FAvaTransitionInitSequenceTaskInstanceData;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FAvaTransitionInitializeSequence() = default;
	virtual ~FAvaTransitionInitializeSequence() override = default;
	FAvaTransitionInitializeSequence(const FAvaTransitionInitializeSequence&) = default;
	FAvaTransitionInitializeSequence(FAvaTransitionInitializeSequence&&) = default;
	FAvaTransitionInitializeSequence& operator=(const FAvaTransitionInitializeSequence&) = default;
	FAvaTransitionInitializeSequence& operator=(FAvaTransitionInitializeSequence&&) = default;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	//~ Begin FStateTreeNodeBase
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual void PostLoad(FStateTreeDataView InInstanceDataView) override;
	//~ End FStateTreeNodeBase

	//~ Begin FAvaTransitionSequenceTaskBase
	virtual EAvaTransitionSequenceWaitType GetWaitType(FStateTreeExecutionContext& InContext) const override;
	virtual TArray<UAvaSequencePlayer*> ExecuteSequenceTask(FStateTreeExecutionContext& InContext) const override;
	//~ End FAvaTransitionSequenceTaskBase

	UE_DEPRECATED(5.5, "InitializeTime has been moved to Instance Data")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the Instance Data InitializeTime instead"))
	FAvaSequenceTime InitializeTime_DEPRECATED = FAvaSequenceTime(0.0);

	UE_DEPRECATED(5.5, "PlayMode has been moved to Instance Data")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the Instance Data PlayMode instead"))
	EAvaSequencePlayMode PlayMode_DEPRECATED = EAvaSequencePlayMode::Forward;
};
