// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/Float16Color.h"
#include "Components/PrimitiveComponent.h"
#include "BakedShallowWaterSimulationComponent.generated.h"

USTRUCT(BlueprintType)
struct WATER_API FShallowWaterSimulationGrid
{
	GENERATED_BODY()

public:
	FShallowWaterSimulationGrid(const TArray<FVector4> &InArrayValues, TObjectPtr<UTexture2D> InBakedWaterTexture, const FIntVector2 &InNumCells, const FVector &InPosition, const FVector2D &InSize)
		: ArrayValues(InArrayValues), 
		NumCells(InNumCells), 
		Position(InPosition), 
		Size(InSize),
		BakedTexture(InBakedWaterTexture)
	{
		Dx = Size / FVector2D(NumCells.X, NumCells.Y);
	}
	FShallowWaterSimulationGrid()
	{				
		NumCells = { 0, 0 };
		Position = { 0.0, 0.0, 0.0 };		
		Size = { 0.0, 0.0 };
		Dx = { 0.0, 0.0 };
	};

	/** Check for a valid sim domain */
	bool IsValid() const
	{
		return NumCells.X > 0 && NumCells.Y > 0 && ArrayValues.Num() == NumCells.X * NumCells.Y && Size.X > SMALL_NUMBER && Size.Y > SMALL_NUMBER;
	}

	/** Linear interpolate sim data at an index */
	void SampleShallowWaterSimulationAtIndex(const FVector2D &QueryFloatIndex, FVector& OutWaterVelocity, float& OutWaterHeight, float& OutWaterDepth) const;
	
	/** Query sim data at an index */
	void QueryShallowWaterSimulationAtIndex(const FIntVector2 &QueryIndex, FVector& OutWaterVelocity, float& OutWaterHeight, float& OutWaterDepth) const;

	/** Compute normal at a world position */
	FVector ComputeShallowWaterSimulationNormalAtPosition(const FVector &QueryPos) const;
	
	/** Query shallow water grid cell values at a world position */
	void FORCEINLINE QueryShallowWaterSimulationAtPosition(const FVector &QueryPos, FVector& OutWaterVelocity, float& OutWaterHeight, float& OutWaterDepth) const
	{
		const FIntVector2 IndexPos = WorldToIndex(QueryPos);
		QueryShallowWaterSimulationAtIndex(IndexPos, OutWaterVelocity, OutWaterHeight, OutWaterDepth);
	}

	/** Linear interpolate for shallow water grid cell values at a world position */
	void FORCEINLINE SampleShallowWaterSimulationAtPosition(const FVector &QueryPos, FVector& OutWaterVelocity, float& OutWaterHeight, float& OutWaterDepth) const
	{
		FVector2D FloatIndex = WorldToFloatIndex(QueryPos);
		SampleShallowWaterSimulationAtIndex(FloatIndex, OutWaterVelocity, OutWaterHeight, OutWaterDepth);
	}

	/** Convert world space to integer index */
	FIntVector2 FORCEINLINE WorldToIndex(const FVector &WorldPos) const
	{
		FVector2D FloatIndex = WorldToFloatIndex(WorldPos);
		return FIntVector2(FloatIndex.X, FloatIndex.Y);
	}

	/** Convert world space to float index */
	FVector2D FORCEINLINE WorldToFloatIndex(const FVector &WorldPos) const
	{
		const FVector LocalPos = WorldPos - Position;
		const FVector2D UnitPos = FVector2D(LocalPos.X, LocalPos.Y) / Size + .5;
		return UnitPos * FVector2D(NumCells.X, NumCells.Y) - .5;
	}

	/** Convert a float index to world space */
	FVector FORCEINLINE FloatIndexToWorld(const FVector2D &Index) const
	{
		const FVector2D UnitPos = (Index + .5) / FVector2D(NumCells.X, NumCells.Y);
		const FVector2D LocalPos = (UnitPos - .5) * Size;
		const FVector LocalPos3D = FVector(LocalPos.X, LocalPos.Y, 0);
		return LocalPos3D + Position;
	}

	/** Convert an integer index to world space */
	FVector FORCEINLINE IndexToWorld(const FIntVector2 &Index) const
	{
		return FloatIndexToWorld(FVector2D(Index.X, Index.Y));
	}

	/** Raw grid data */
	UPROPERTY()
	TArray<FVector4> ArrayValues;
	
	/** Number of cells */
	UPROPERTY()
	FIntVector2 NumCells;
	
	/** World space center */
	UPROPERTY()
	FVector Position;
	
	/** World space size */
	UPROPERTY()
	FVector2D Size;

	/** World space grid cell size */
	UPROPERTY()
	FVector2D Dx;
	
	/** Texture for baked water sim data */
	UPROPERTY()
	TObjectPtr<UTexture2D> BakedTexture;
};

UCLASS(meta = (BlueprintSpawnableComponent), MinimalAPI)
class UBakedShallowWaterSimulationComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()

	UPROPERTY()
	FShallowWaterSimulationGrid SimulationData;
};