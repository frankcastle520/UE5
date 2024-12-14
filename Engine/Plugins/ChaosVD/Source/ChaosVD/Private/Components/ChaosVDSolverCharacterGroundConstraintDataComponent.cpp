// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/ChaosVDSolverCharacterGroundConstraintDataComponent.h"

#include "ChaosVDCharacterGroundConstraintDataProviderInterface.h"
#include "ChaosVDScene.h"
#include "ChaosVDSettingsManager.h"
#include "Selection.h"
#include "Settings/ChaosVDCharacterConstraintsVisualizationSettings.h"

void UChaosVDSolverCharacterGroundConstraintDataComponent::HandleSceneUpdated()
{
	if (const UChaosVDCharacterConstraintsVisualizationSettings* CharacterConstraintsVisualizationSettings = FChaosVDSettingsManager::Get().GetSettingsObject<UChaosVDCharacterConstraintsVisualizationSettings>())
	{
		if (!CharacterConstraintsVisualizationSettings->bAutoSelectConstraintFromSelectedParticle)
		{
			return;
		}

	}

	const TSharedPtr<FChaosVDScene> ScenePtr = SceneWeakPtr.Pin();
	if (!ScenePtr)
	{
		return;
	}
	
	USelection* ActorSelection = ScenePtr->GetActorSelectionObject();
	if (!ActorSelection)
	{
		return;
	}

	TSharedPtr<FChaosVDSolverDataSelection> SolverDataSelection = ScenePtr->GetSolverDataSelectionObject().Pin();
	if(!SolverDataSelection)
	{
		return;
	}

	if (ActorSelection->Num() > 0)
	{
		if (IChaosVDCharacterGroundConstraintDataProviderInterface* DataProvider = Cast<IChaosVDCharacterGroundConstraintDataProviderInterface>(ActorSelection->GetSelectedObject(0)))
		{
			if (DataProvider->HasCharacterGroundConstraintData())
			{
				TArray<TSharedPtr<FChaosVDCharacterGroundConstraint>> FoundConstraintData;
				DataProvider->GetCharacterGroundConstraintData(FoundConstraintData);
				SolverDataSelection->SelectData(SolverDataSelection->MakeSelectionHandle(FoundConstraintData[0]));
			}
		}
	}
}

void UChaosVDSolverCharacterGroundConstraintDataComponent::BeginDestroy()
{
	Super::BeginDestroy();

	const TSharedPtr<FChaosVDScene> ScenePtr = SceneWeakPtr.Pin();
	if (!ScenePtr)
	{
		return;
	}
	
	ScenePtr->OnSceneUpdated().RemoveAll(this);
}

void UChaosVDSolverCharacterGroundConstraintDataComponent::SetScene(const TWeakPtr<FChaosVDScene>& InSceneWeakPtr)
{
	Super::SetScene(InSceneWeakPtr);
	
	const TSharedPtr<FChaosVDScene> ScenePtr = SceneWeakPtr.Pin();
	if (!ScenePtr)
	{
		return;
	}
	
	ScenePtr->OnSceneUpdated().AddUObject(this, &UChaosVDSolverCharacterGroundConstraintDataComponent::HandleSceneUpdated);
}
