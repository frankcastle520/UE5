// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaTagHandle.h"
#include "AvaTagHandleContainer.h"
#include "AvaTransitionEnums.h"
#include "Conditions/AvaTransitionCondition.h"
#include "AvaSceneContainsTagAttributeCondition.generated.h"

class UAvaSceneSubsystem;
struct FAvaTransitionScene;

USTRUCT()
struct FAvaSceneContainsTagAttributeConditionInstanceData
{
	GENERATED_BODY()

	/** Whether the scene to check should be this scene or other scene */
	UPROPERTY(EditAnywhere, Category="Parameter")
	EAvaTransitionSceneType SceneType = EAvaTransitionSceneType::This;

	/** Which Layer should be queried for the Scene Attributes */
	UPROPERTY(EditAnywhere, Category="Parameter", meta=(EditCondition="SceneType==EAvaTransitionSceneType::Other", EditConditionHides))
	EAvaTransitionLayerCompareType LayerType = EAvaTransitionLayerCompareType::Same;

	/** Specific layer tags to check */
	UPROPERTY(EditAnywhere, Category="Parameter", meta=(EditCondition="SceneType==EAvaTransitionSceneType::Other && LayerType==EAvaTransitionLayerCompareType::MatchingTag", EditConditionHides))
	FAvaTagHandleContainer SpecificLayers;

	/** The Tag Attribute to check if it's contained in the scene(s) or not */
	UPROPERTY(EditAnywhere, Category="Parameter")
	FAvaTagHandle TagAttribute;
};

USTRUCT(meta=(Hidden))
struct AVALANCHE_API FAvaSceneContainsTagAttributeConditionBase : public FAvaTransitionCondition
{
	GENERATED_BODY()

	using FInstanceDataType = FAvaSceneContainsTagAttributeConditionInstanceData;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FAvaSceneContainsTagAttributeConditionBase() = default;
	virtual ~FAvaSceneContainsTagAttributeConditionBase() override = default;
	FAvaSceneContainsTagAttributeConditionBase(const FAvaSceneContainsTagAttributeConditionBase&) = default;
	FAvaSceneContainsTagAttributeConditionBase(FAvaSceneContainsTagAttributeConditionBase&&) = default;
	FAvaSceneContainsTagAttributeConditionBase& operator=(const FAvaSceneContainsTagAttributeConditionBase&) = default;
	FAvaSceneContainsTagAttributeConditionBase& operator=(FAvaSceneContainsTagAttributeConditionBase&&) = default;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	FAvaSceneContainsTagAttributeConditionBase(bool bInInvertCondition)
		: bInvertCondition(bInInvertCondition)
	{
	}

	//~ Begin FStateTreeNodeBase
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& InId, FStateTreeDataView InInstanceDataView, const IStateTreeBindingLookup& InBindingLookup, EStateTreeNodeFormatting InFormatting) const override;
#endif
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual void PostLoad(FStateTreeDataView InInstanceDataView) override;
	virtual bool Link(FStateTreeLinker& InLinker) override;
	//~ End FStateTreeNodeBase

	//~ Begin FStateTreeConditionBase
	virtual bool TestCondition(FStateTreeExecutionContext& InContext) const override;
	//~ End FStateTreeConditionBase

	bool ContainsTagAttribute(FStateTreeExecutionContext& InContext) const;

	TArray<const FAvaTransitionScene*> GetTransitionScenes(FStateTreeExecutionContext& InContext) const;

	UE_DEPRECATED(5.5, "SceneType has been moved to Instance Data")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the Instance Data SceneType instead"))
	EAvaTransitionSceneType SceneType_DEPRECATED = EAvaTransitionSceneType::This;

	UE_DEPRECATED(5.5, "LayerType has been moved to Instance Data")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the Instance Data LayerType instead"))
	EAvaTransitionLayerCompareType LayerType_DEPRECATED = EAvaTransitionLayerCompareType::None;

	UE_DEPRECATED(5.5, "SpecificLayers has been moved to Instance Data")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the Instance Data SpecificLayers instead"))
	FAvaTagHandleContainer SpecificLayers_DEPRECATED;

	UE_DEPRECATED(5.5, "TagAttribute has been moved to Instance Data")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the Instance Data TagAttribute instead"))
	FAvaTagHandle TagAttribute_DEPRECATED;

protected:
	bool bInvertCondition = false;

	TStateTreeExternalDataHandle<UAvaSceneSubsystem> SceneSubsystemHandle;
};

USTRUCT(DisplayName="A scene contains tag attribute", Category="Scene Attributes")
struct FAvaSceneContainsTagAttributeCondition : public FAvaSceneContainsTagAttributeConditionBase
{
	GENERATED_BODY()

	FAvaSceneContainsTagAttributeCondition()
		: FAvaSceneContainsTagAttributeConditionBase(/*bInvertCondition*/false)
	{
	}
};

USTRUCT(DisplayName="No scene contains tag attribute", Category="Scene Attributes")
struct FAvaNoSceneContainsTagAttributeCondition : public FAvaSceneContainsTagAttributeConditionBase
{
	GENERATED_BODY()

	FAvaNoSceneContainsTagAttributeCondition()
		: FAvaSceneContainsTagAttributeConditionBase(/*bInvertCondition*/true)
	{
	}
};
