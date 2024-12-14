// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "AvaTransitionLibrary.generated.h"

class UAvaTransitionTree;
enum class EAvaTransitionComparisonResult : uint8;
enum class EAvaTransitionLayerCompareType : uint8;
enum class EAvaTransitionType : uint8;
struct FAvaTagHandleContainer;

UCLASS()
class UAvaTransitionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Transition Logic", meta=(DefaultToSelf="InTransitionNode"))
	static bool IsTransitionActiveInLayer(UObject* InTransitionNode
		, EAvaTransitionComparisonResult InSceneComparisonType
		, EAvaTransitionLayerCompareType InLayerComparisonType
		, const FAvaTagHandleContainer& InSpecificLayers);

	UFUNCTION(BlueprintCallable, Category="Transition Logic", meta=(DefaultToSelf="InTransitionNode"))
	static EAvaTransitionType GetTransitionType(UObject* InTransitionNode);

	UFUNCTION(BlueprintCallable, Category="Transition Logic", meta=(DefaultToSelf="InTransitionNode"))
	static bool AreScenesTransitioning(UObject* InTransitionNode, const FAvaTagHandleContainer& InLayers, const TArray<TSoftObjectPtr<UWorld>>& InScenesToIgnore);

	UFUNCTION(BlueprintCallable, Category="Transition Logic", meta=(DefaultToSelf="InTransitionNode"))
	static const UAvaTransitionTree* GetTransitionTree(UObject* InTransitionNode);
};
