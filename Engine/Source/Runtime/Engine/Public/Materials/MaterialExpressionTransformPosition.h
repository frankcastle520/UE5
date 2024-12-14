// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MaterialExpressionIO.h"
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionTransformPosition.generated.h"

UENUM()
enum EMaterialPositionTransformSource : int
{
	/** Local space */
	TRANSFORMPOSSOURCE_Local UMETA(DisplayName="Local Space"),
	
	/** Absolute world space */
	TRANSFORMPOSSOURCE_World UMETA(DisplayName="Absolute World Space"),

	/**
	  Like absolute world space, but the world origin is moved to the center of the tile the camera is in.
	  Logically similar to `fmod(CameraAbsoluteWorldPosition, TileSize) + CameraRelativeWorldPosition`.
	  This offers better precision and scalability than absolute world position.
	  Suitable as a position input for functions that tile based on world position, e.g. frac(Position / TileSize).
	  Works best when the tile size is a power of two.
	*/
	TRANSFORMPOSSOURCE_PeriodicWorld UMETA(DisplayName="Periodic World Space"),
	
	/** Translated world space, i.e. world space rotation and scale but with a position relative to the camera */
	TRANSFORMPOSSOURCE_TranslatedWorld  UMETA(DisplayName="Camera Relative World Space"),

	/** View space (differs from camera space in the shadow passes) */
	TRANSFORMPOSSOURCE_View  UMETA(DisplayName="View Space"),

	/** Camera space */
	TRANSFORMPOSSOURCE_Camera  UMETA(DisplayName="Camera Space"),

	/** Particle space, deprecated value will be removed in a future release use instance space. */
	TRANSFORMPOSSOURCE_Particle UMETA(Hidden, DisplayName = "Mesh Particle Space"),

	/** Instance space (used to provide per instance transform, i.e. for Instanced Static Mesh / Particles). */
	TRANSFORMPOSSOURCE_Instance UMETA(DisplayName = "Instance & Particle Space"),

	TRANSFORMPOSSOURCE_MAX,
};

UCLASS(collapsecategories, hidecategories=Object, MinimalAPI)
class UMaterialExpressionTransformPosition : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	/** input expression for this transform */
	UPROPERTY()
	FExpressionInput Input;

	/** source format of the position that will be transformed */
	UPROPERTY(EditAnywhere, Category=MaterialExpressionTransformPosition, meta=(DisplayName = "Source", ShowAsInputPin = "Advanced"))
	TEnumAsByte<enum EMaterialPositionTransformSource> TransformSourceType;

	/** type of transform to apply to the input expression */
	UPROPERTY(EditAnywhere, Category=MaterialExpressionTransformPosition, meta=(DisplayName = "Destination", ShowAsInputPin = "Advanced"))
	TEnumAsByte<enum EMaterialPositionTransformSource> TransformType;

	/** scale of the tiles used in Periodic World Space */
	UPROPERTY(meta=(DisplayName = "Periodic World Tile Size", ToolTip = "Distance the camera can move before the world origin is moved", RequiredInput = "false"))
	FExpressionInput PeriodicWorldTileSize;

	/** only used if PeriodicWorldTileSize is not hooked up */
	UPROPERTY(EditAnywhere, Category=MaterialExpressionTransformPosition, meta=(DisplayName = "Periodic World Tile Size", RequiredInput="false", ClampMin=0.0001, UIMax=524288.0, OverridingInputProperty = "PeriodicWorldTileSize", EditCondition=bUsesPeriodicWorldPosition, EditConditionHides))
	float ConstPeriodicWorldTileSize = 32.0;

private:
	UPROPERTY()
	bool bUsesPeriodicWorldPosition = false;

public:

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual bool GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression const*& OutExpression) const override;
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual TArrayView<FExpressionInput*> GetInputsView() override;
	virtual FExpressionInput* GetInput(int32 InputIndex) override;
	virtual FName GetInputName(int32 InputIndex) const override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UMaterialExpression Interface
};

