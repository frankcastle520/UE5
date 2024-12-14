// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "ScriptableInteractiveTool.h"
#include "Drawing/PointSetComponent.h"


#include "ScriptableToolPoint.generated.h"


UCLASS(BlueprintType)
class SCRIPTABLETOOLSFRAMEWORK_API UScriptableToolPoint : public UObject
{
	GENERATED_BODY()

public:

	void SetPointID(int32 PointIDIn);
	int32 GetPointID() const;

	bool IsDirty() const;
	FRenderablePoint GeneratePointDescription();

	/**
	 * Set the position of the point
	 * @param Position The position in space of the point
	 */
	UFUNCTION(BlueprintCallable, Category = "ScriptableTool|Drawing|Points")
	void SetPointPosition(FVector Position);

	/**
	 * Set the point's color
	 * @param Color The color the point should be rendered as
	 */
	UFUNCTION(BlueprintCallable, Category = "ScriptableTool|Drawing|Points")
	void SetPointColor(FColor Color);

	/**
	 * Set the point's size
	 * @param Size The visual size in pixels the point should be rendered as
	 */
	UFUNCTION(BlueprintCallable, Category = "ScriptableTool|Drawing|Points")
	void SetPointSize(float Thickness);

	/**
	 * Set the point's depth bias. The depth bias controls small micro adjustments in effective
	 * displacement towards or away from the camera, to prevent the point from causing clipping
	 * or z-fighting with other objects in the scene it's overlaid directly on top of.
	 * @param DepthBias The depth bias adjustment for the point
	 */
	UFUNCTION(BlueprintCallable, Category = "ScriptableTool|Drawing|Points")
	void SetPointDepthBias(float DepthBias);

private:

	FRenderablePoint PointDescription;

	UPROPERTY()
	bool bIsDirty = false;

	UPROPERTY()
	int32 PointID;

};