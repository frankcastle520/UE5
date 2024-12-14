// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScriptableInteractiveTool.h"
#include "Drawing/PointSetComponent.h"

#include "ScriptableToolPointSet.generated.h"


class UScriptableToolPoint;
class UPreviewGeometry;

UCLASS(BlueprintType)
class SCRIPTABLETOOLSFRAMEWORK_API UScriptableToolPointSet : public UObject
{
	GENERATED_BODY()

public:

	void Initialize(TObjectPtr<UPreviewGeometry> PreviewGeometry);

	void OnTick();

	/**
	 * Create and return a new point object. Users should save a reference to this object for future updates or removal from the set.
	 * @return The new point object added to the set.
	 */
	UFUNCTION(BlueprintCallable, Category = "ScriptableTool|Drawing|PointSet")
	UPARAM(DisplayName = "PointComponent") UScriptableToolPoint* AddPoint();


	/**
	 * Remove a specific point object from the set, removing it from the scene.
	 * @param Point A reference to a point to be removed from the set.
	 */
	UFUNCTION(BlueprintCallable, Category = "ScriptableTool|Drawing|PointSet")
	void RemovePoint(UScriptableToolPoint* Point);

	/**
	 * Remove all current points in the set.
	 */
	UFUNCTION(BlueprintCallable, Category = "ScriptableTool|Drawing|PointSet")
	void RemoveAllPoints();

	/**
	 * Set the color of all points in the set simultaneously.
	 */
	UFUNCTION(BlueprintCallable, Category = "ScriptableTool|Drawing|PointSet")
	void SetAllPointsColor(FColor Color);

	/**
	 * Set the size of all points in the set simultaneously.
	 */
	UFUNCTION(BlueprintCallable, Category = "ScriptableTool|Drawing|PointSet")
	void SetAllPointsSize(float Size);

protected:

	UPROPERTY(Transient, DuplicateTransient, NonTransactional, SkipSerialization)
	TObjectPtr<UPreviewGeometry> ToolDrawableGeometry = nullptr;

	UPROPERTY(Transient, DuplicateTransient, NonTransactional, SkipSerialization)
	TObjectPtr<UPointSetComponent> PointSet = nullptr;

	UPROPERTY(Transient, DuplicateTransient, NonTransactional, SkipSerialization)
	TArray<TObjectPtr<UScriptableToolPoint>> PointComponents;

};

