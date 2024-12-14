// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosVDSolverDataComponent.h"
#include "Components/ActorComponent.h"
#include "DataWrappers/ChaosVDDebugShapeDataWrapper.h"
#include "ChaosVDGenericDebugDrawDataComponent.generated.h"

class UChaosVDGenericDebugDrawDataComponent;
struct FChaosVDGameFrameData;

UCLASS()
class CHAOSVD_API UChaosVDGenericDebugDrawDataComponent : public UChaosVDSolverDataComponent
{
	GENERATED_BODY()
public:
	UChaosVDGenericDebugDrawDataComponent();

	virtual void UpdateFromNewGameFrameData(const FChaosVDGameFrameData& InGameFrameData) override;

	virtual void ClearData() override;

	TConstArrayView<TSharedPtr<FChaosVDDebugDrawBoxDataWrapper>> GetDebugDrawBoxesDataView() const { return DebugDrawBoxes; };
	TConstArrayView<TSharedPtr<FChaosVDDebugDrawLineDataWrapper>> GetDebugDrawLinesDataView() const { return DebugDrawLines; };
	TConstArrayView<TSharedPtr<FChaosVDDebugDrawSphereDataWrapper>> GetDebugDrawSpheresDataView() const { return DebugDrawSpheres; };
	TConstArrayView<TSharedPtr<FChaosVDDebugDrawImplicitObjectDataWrapper>> GetDebugDrawImplicitObjectsDataView() const { return DebugDrawImplicitObjects; };

private:

	template<typename RecordedDataType>
	void CopyDataFromSourceMap(const TMap<int32,TArray<TSharedPtr<RecordedDataType>>>& CopyFrom, TArray<TSharedPtr<RecordedDataType>>& CopyTo, int32 SolverID);
	
	TArray<TSharedPtr<FChaosVDDebugDrawBoxDataWrapper>> DebugDrawBoxes;
	TArray<TSharedPtr<FChaosVDDebugDrawLineDataWrapper>> DebugDrawLines;
	TArray<TSharedPtr<FChaosVDDebugDrawSphereDataWrapper>> DebugDrawSpheres;
	TArray<TSharedPtr<FChaosVDDebugDrawImplicitObjectDataWrapper>> DebugDrawImplicitObjects;
};

template <typename RecordedDataType>
void UChaosVDGenericDebugDrawDataComponent::CopyDataFromSourceMap(const TMap<int32, TArray<TSharedPtr<RecordedDataType>>>& CopyFrom, TArray<TSharedPtr<RecordedDataType>>& CopyTo, int32 SolverID)
{
	if (const TArray<TSharedPtr<RecordedDataType>>* RecordedData = CopyFrom.Find(SolverID))
	{
		CopyTo.Reset(RecordedData->Num());
		CopyTo = *RecordedData;
	}
	else
	{
		CopyTo.Reset();	
	}
}

