// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDGenericDebugDrawDataComponent.h"

#include "ChaosVDRecording.h"

UChaosVDGenericDebugDrawDataComponent::UChaosVDGenericDebugDrawDataComponent()
{
	SetCanEverAffectNavigation(false);
	bNavigationRelevant = false;
	PrimaryComponentTick.bCanEverTick = false;
}

void UChaosVDGenericDebugDrawDataComponent::UpdateFromNewGameFrameData(const FChaosVDGameFrameData& InGameFrameData)
{
	CopyDataFromSourceMap<FChaosVDDebugDrawBoxDataWrapper>(InGameFrameData.RecordedDebugDrawBoxesBySolverID, DebugDrawBoxes, SolverID);
	CopyDataFromSourceMap<FChaosVDDebugDrawLineDataWrapper>(InGameFrameData.RecordedDebugDrawLinesBySolverID, DebugDrawLines, SolverID);
	CopyDataFromSourceMap<FChaosVDDebugDrawSphereDataWrapper>(InGameFrameData.RecordedDebugDrawSpheresBySolverID, DebugDrawSpheres, SolverID);
	CopyDataFromSourceMap<FChaosVDDebugDrawImplicitObjectDataWrapper>(InGameFrameData.RecordedDebugDrawImplicitObjectsBySolverID, DebugDrawImplicitObjects, SolverID);
}

void UChaosVDGenericDebugDrawDataComponent::ClearData()
{
	DebugDrawBoxes.Reset();
	DebugDrawLines.Reset();
	DebugDrawSpheres.Reset();
	DebugDrawLines.Reset();
	DebugDrawImplicitObjects.Reset();
}
