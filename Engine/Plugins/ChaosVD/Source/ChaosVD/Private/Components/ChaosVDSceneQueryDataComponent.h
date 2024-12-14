// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataWrappers/ChaosVDQueryDataWrappers.h"
#include "ChaosVDSolverDataComponent.h"
#include "ChaosVDSolverDataSelection.h"

#include "ChaosVDSceneQueryDataComponent.generated.h"

struct FChaosVDGameFrameData;

/** Actor Component that contains all the scene queries recorded at the current loaded frame */
UCLASS()
class UChaosVDSceneQueryDataComponent : public UChaosVDSolverDataComponent
{
	GENERATED_BODY()

public:
	UChaosVDSceneQueryDataComponent();
	void ClearSceneQuerySelection();

	virtual void UpdateFromNewGameFrameData(const FChaosVDGameFrameData& InGameFrameData) override;

	TConstArrayView<TSharedPtr<FChaosVDQueryDataWrapper>> GetQueriesByType(EChaosVDSceneQueryType Type) const;
	TConstArrayView<TSharedPtr<FChaosVDQueryDataWrapper>> GetAllQueries() const;
	TSharedPtr<FChaosVDQueryDataWrapper> GetQueryByID(int32 QueryID) const;

	virtual void ClearData() override;

protected:
	
	TMap<EChaosVDSceneQueryType, TArray<TSharedPtr<FChaosVDQueryDataWrapper>>> RecordedQueriesByType;
	TMap<int32, TSharedPtr<FChaosVDQueryDataWrapper>> RecordedQueriesByID;
	TArray<TSharedPtr<FChaosVDQueryDataWrapper>> RecordedQueries;
};
