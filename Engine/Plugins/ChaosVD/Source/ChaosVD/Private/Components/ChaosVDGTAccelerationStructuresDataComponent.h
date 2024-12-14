// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosVDSolverDataComponent.h"
#include "ChaosVDGTAccelerationStructuresDataComponent.generated.h"

struct FChaosVDAABBTreeDataWrapper;
struct FChaosVDGameFrameData;

UCLASS()
class UChaosVDGTAccelerationStructuresDataComponent : public UChaosVDSolverDataComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UChaosVDGTAccelerationStructuresDataComponent();

	void UpdateAABBTreeData(TConstArrayView<TSharedPtr<FChaosVDAABBTreeDataWrapper>> AABBTreeDataView);

	TConstArrayView<TSharedPtr<FChaosVDAABBTreeDataWrapper>> GetAABBTreeData() const { return RecordedABBTreeData; }

	virtual void UpdateFromNewGameFrameData(const FChaosVDGameFrameData& InGameFrameData) override;

	virtual void ClearData() override;
protected:
	
	TArray<TSharedPtr<FChaosVDAABBTreeDataWrapper>> RecordedABBTreeData;
};
