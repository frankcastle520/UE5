// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDSolverDataSelection.h"

void FChaosVDSolverDataSelectionHandle::SetOwner(const TSharedPtr<FChaosVDSolverDataSelection>& InOwner)
{
	Owner = InOwner;
}

bool FChaosVDSolverDataSelectionHandle::IsSelected()
{
	if (TSharedPtr<FChaosVDSolverDataSelection> OwnerPtr = Owner.Pin())
	{
		return OwnerPtr->IsSelectionHandleSelected(AsShared());
	}

	return false;
}

bool FChaosVDSolverDataSelectionHandle::IsValid() const
{
	return SelectedDataStruct.IsValid() && SelectedDataStruct->IsValid();
}

void FChaosVDSolverDataSelection::SelectData(const TSharedPtr<FChaosVDSolverDataSelectionHandle>& InSelectionHandle)
{
	CurrentSelectedSolverDataHandle = InSelectionHandle;

	SolverDataSelectionChangeDelegate.Broadcast(CurrentSelectedSolverDataHandle);
}

bool FChaosVDSolverDataSelection::IsSelectionHandleSelected(const TSharedPtr<FChaosVDSolverDataSelectionHandle>& InSelectionHandle) const
{
	const bool bBothHandlesAreValid = CurrentSelectedSolverDataHandle && InSelectionHandle;

	if (!bBothHandlesAreValid)
	{
		return false;
	}

	return (*CurrentSelectedSolverDataHandle) == (*InSelectionHandle);
}
