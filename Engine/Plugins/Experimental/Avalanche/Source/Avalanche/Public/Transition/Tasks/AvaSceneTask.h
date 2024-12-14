// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaTagHandle.h"
#include "Tasks/AvaTransitionTask.h"
#include "AvaSceneTask.generated.h"

class IAvaSceneInterface;
class UAvaSceneSubsystem;

USTRUCT()
struct FAvaSceneTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Parameter")
	FAvaTagHandle TagAttribute;
};

USTRUCT(meta=(Hidden))
struct AVALANCHE_API FAvaSceneTask : public FAvaTransitionTask
{
	GENERATED_BODY()

	using FInstanceDataType = FAvaSceneTaskInstanceData;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FAvaSceneTask() = default;
	virtual ~FAvaSceneTask() override = default;
	FAvaSceneTask(const FAvaSceneTask&) = default;
	FAvaSceneTask(FAvaSceneTask&&) = default;
	FAvaSceneTask& operator=(const FAvaSceneTask&) = default;
	FAvaSceneTask& operator=(FAvaSceneTask&&) = default;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	//~ Begin FStateTreeNodeBase
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual void PostLoad(FStateTreeDataView InInstanceDataView) override;
	virtual bool Link(FStateTreeLinker& InLinker) override;
	//~ End FStateTreeNodeBase

	IAvaSceneInterface* GetScene(FStateTreeExecutionContext& InContext) const;

	UE_DEPRECATED(5.5, "TagAttribute has been moved to Instance Data")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the Instance Data TagAttribute instead"))
	FAvaTagHandle TagAttribute_DEPRECATED;

	TStateTreeExternalDataHandle<UAvaSceneSubsystem> SceneSubsystemHandle;
};
