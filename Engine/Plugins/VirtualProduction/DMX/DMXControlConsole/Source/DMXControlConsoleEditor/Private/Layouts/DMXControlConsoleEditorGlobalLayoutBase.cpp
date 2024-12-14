// Copyright Epic Games, Inc. All Rights Reserved.

#include "Layouts/DMXControlConsoleEditorGlobalLayoutBase.h"

#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "Algo/Find.h"
#include "Algo/StableSort.h"
#include "Algo/Transform.h"
#include "Controllers/DMXControlConsoleCellAttributeController.h"
#include "Controllers/DMXControlConsoleElementController.h"
#include "Controllers/DMXControlConsoleFaderGroupController.h"
#include "DMXControlConsoleData.h"
#include "DMXControlConsoleEditorGlobalLayoutRow.h"
#include "DMXControlConsoleFaderBase.h"
#include "DMXControlConsoleFaderGroup.h"
#include "DMXControlConsoleFaderGroupRow.h"
#include "Layouts/DMXControlConsoleEditorLayouts.h"
#include "Library/DMXEntity.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXLibrary.h"
#include "Misc/ScopedSlowTask.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleEditorGlobalLayoutBase"

UDMXControlConsoleFaderGroupController* UDMXControlConsoleEditorGlobalLayoutBase::AddToLayout(UDMXControlConsoleFaderGroup* InFaderGroup, const FString& ControllerName, const int32 RowIndex, const int32 ColumnIndex)
{
	if (!InFaderGroup)
	{
		return nullptr;
	}

	const TArray<UDMXControlConsoleFaderGroup*> FaderGroupAsArray = { InFaderGroup };
	UDMXControlConsoleFaderGroupController* NewController = AddToLayout(FaderGroupAsArray, ControllerName, RowIndex, ColumnIndex);
	return NewController;
}

UDMXControlConsoleFaderGroupController* UDMXControlConsoleEditorGlobalLayoutBase::AddToLayout(const TArray<UDMXControlConsoleFaderGroup*> InFaderGroups, const FString& ControllerName, const int32 RowIndex, const int32 ColumnIndex)
{
	if (InFaderGroups.IsEmpty() || !LayoutRows.IsValidIndex(RowIndex))
	{
		return nullptr;
	}

	UDMXControlConsoleEditorGlobalLayoutRow* LayoutRow = LayoutRows[RowIndex];
	if (!LayoutRow)
	{
		return nullptr;
	}

	LayoutRow->Modify();
	return LayoutRow->CreateFaderGroupController(InFaderGroups, ControllerName, ColumnIndex);
}

UDMXControlConsoleEditorGlobalLayoutRow* UDMXControlConsoleEditorGlobalLayoutBase::AddNewRowToLayout(const int32 RowIndex)
{
	if (RowIndex > LayoutRows.Num())
	{
		return nullptr;
	}

	UDMXControlConsoleEditorGlobalLayoutRow* LayoutRow = NewObject<UDMXControlConsoleEditorGlobalLayoutRow>(this, NAME_None, RF_Transactional);
	const int32 ValidRowIndex = RowIndex < 0 ? LayoutRows.Num() : RowIndex;
	LayoutRows.Insert(LayoutRow, ValidRowIndex);

	return LayoutRow;
}

UDMXControlConsoleEditorLayouts& UDMXControlConsoleEditorGlobalLayoutBase::GetOwnerEditorLayoutsChecked() const
{
	UDMXControlConsoleEditorLayouts* Outer = Cast<UDMXControlConsoleEditorLayouts>(GetOuter());
	checkf(Outer, TEXT("Invalid outer for '%s', cannot get layout owner correctly."), *GetName());

	return *Outer;
}

UDMXControlConsoleEditorGlobalLayoutRow* UDMXControlConsoleEditorGlobalLayoutBase::GetLayoutRow(const UDMXControlConsoleFaderGroupController* FaderGroupController) const
{
	const int32 RowIndex = GetFaderGroupControllerRowIndex(FaderGroupController);
	return LayoutRows.IsValidIndex(RowIndex) ? LayoutRows[RowIndex] : nullptr;
}

TArray<UDMXControlConsoleFaderGroupController*> UDMXControlConsoleEditorGlobalLayoutBase::GetAllFaderGroupControllers() const
{
	TArray<UDMXControlConsoleFaderGroupController*> AllFaderGroupControllers;
	for (const UDMXControlConsoleEditorGlobalLayoutRow* LayoutRow : LayoutRows)
	{
		if (LayoutRow)
		{
			AllFaderGroupControllers.Append(LayoutRow->GetFaderGroupControllers());
		}
	}

	return AllFaderGroupControllers;
}

void UDMXControlConsoleEditorGlobalLayoutBase::AddToActiveFaderGroupControllers(UDMXControlConsoleFaderGroupController* FaderGroupController)
{
	if (FaderGroupController)
	{
		ActiveFaderGroupControllers.AddUnique(FaderGroupController);
	}
}

void UDMXControlConsoleEditorGlobalLayoutBase::RemoveFromActiveFaderGroupControllers(UDMXControlConsoleFaderGroupController* FaderGroupController)
{
	if (FaderGroupController)
	{
		ActiveFaderGroupControllers.Remove(FaderGroupController);
	}
}

TArray<UDMXControlConsoleFaderGroupController*> UDMXControlConsoleEditorGlobalLayoutBase::GetAllActiveFaderGroupControllers() const
{
	TArray<UDMXControlConsoleFaderGroupController*> AllActiveFaderGroupControllers = GetAllFaderGroupControllers();
	AllActiveFaderGroupControllers.RemoveAll([](const UDMXControlConsoleFaderGroupController* FaderGroupController)
		{
			return FaderGroupController && !FaderGroupController->IsActive();
		});

	return AllActiveFaderGroupControllers;
}

void UDMXControlConsoleEditorGlobalLayoutBase::SetActiveFaderGroupControllersInLayout(bool bActive)
{
	const TArray<UDMXControlConsoleFaderGroupController*> AllFaderGroupControllers = GetAllFaderGroupControllers();
	for (UDMXControlConsoleFaderGroupController* FaderGroupController : AllFaderGroupControllers)
	{
		if (!FaderGroupController)
		{
			continue;
		}

		const bool bActivate = ActiveFaderGroupControllers.Contains(FaderGroupController) ? bActive : !bActive;
		FaderGroupController->Modify();
		FaderGroupController->SetIsActive(bActivate);
	}
}

int32 UDMXControlConsoleEditorGlobalLayoutBase::GetFaderGroupControllerRowIndex(const UDMXControlConsoleFaderGroupController* FaderGroupController) const
{
	if (!FaderGroupController)
	{
		return INDEX_NONE;
	}

	for (const UDMXControlConsoleEditorGlobalLayoutRow* LayoutRow : LayoutRows)
	{
		if (LayoutRow &&
			LayoutRow->GetFaderGroupControllers().Contains(FaderGroupController))
		{
			return LayoutRows.IndexOfByKey(LayoutRow);
		}
	}

	return INDEX_NONE;
}

int32 UDMXControlConsoleEditorGlobalLayoutBase::GetFaderGroupControllerColumnIndex(const UDMXControlConsoleFaderGroupController* FaderGroupController) const
{
	if (!FaderGroupController)
	{
		return INDEX_NONE;
	}

	for (const UDMXControlConsoleEditorGlobalLayoutRow* LayoutRow : LayoutRows)
	{
		if (LayoutRow && 
			LayoutRow->GetFaderGroupControllers().Contains(FaderGroupController))
		{
			return LayoutRow->GetIndex(FaderGroupController);
		}
	}

	return INDEX_NONE;
}

void UDMXControlConsoleEditorGlobalLayoutBase::SetLayoutMode(const EDMXControlConsoleLayoutMode NewLayoutMode)
{
	if (LayoutMode == NewLayoutMode)
	{
		return;
	}

	LayoutMode = NewLayoutMode;

	const UDMXControlConsoleEditorLayouts& OwnerEditorLayouts = GetOwnerEditorLayoutsChecked();
	OwnerEditorLayouts.OnLayoutModeChanged.Broadcast();
}

bool UDMXControlConsoleEditorGlobalLayoutBase::ContainsFaderGroupController(const UDMXControlConsoleFaderGroupController* FaderGroupController) const
{
	return GetFaderGroupControllerRowIndex(FaderGroupController) != INDEX_NONE;
}

bool UDMXControlConsoleEditorGlobalLayoutBase::ContainsFaderGroup(const UDMXControlConsoleFaderGroup* FaderGroup) const
{
	if (!FaderGroup)
	{
		return false;
	}

	const TArray<UDMXControlConsoleFaderGroupController*> AllFaderGroupControllers = GetAllFaderGroupControllers();
	const bool bIsFaderGroupPossessedByAnyControllerInLayout = Algo::AnyOf(AllFaderGroupControllers, 
		[FaderGroup](const UDMXControlConsoleFaderGroupController* FaderGroupController)
		{
			return FaderGroupController && FaderGroupController->GetFaderGroups().Contains(FaderGroup);
		});

	return bIsFaderGroupPossessedByAnyControllerInLayout;
}

UDMXControlConsoleFaderGroupController* UDMXControlConsoleEditorGlobalLayoutBase::FindFaderGroupControllerByFixturePatch(const UDMXEntityFixturePatch* InFixturePatch) const
{
	if (!InFixturePatch)
	{
		return nullptr;
	}

	const TArray<UDMXControlConsoleFaderGroupController*> AllFaderGroupControllers = GetAllFaderGroupControllers();
	UDMXControlConsoleFaderGroupController* const* FaderGroupControllerPtr =
		Algo::FindByPredicate(AllFaderGroupControllers,
			[InFixturePatch](const UDMXControlConsoleFaderGroupController* FaderGroupController)
			{
				if (!FaderGroupController)
				{
					return false;
				}

				const TArray<TWeakObjectPtr<UDMXControlConsoleFaderGroup>>& FaderGroups = FaderGroupController->GetFaderGroups();
				const TWeakObjectPtr<UDMXControlConsoleFaderGroup>* FaderGroupPtr =
					Algo::FindByPredicate(FaderGroups,
						[InFixturePatch](const TWeakObjectPtr<UDMXControlConsoleFaderGroup>& FaderGroup)
						{
							return FaderGroup.IsValid() && FaderGroup->GetFixturePatch() == InFixturePatch;
						});

				return FaderGroupPtr != nullptr;
			});

	return FaderGroupControllerPtr ? *FaderGroupControllerPtr : nullptr;
}

void UDMXControlConsoleEditorGlobalLayoutBase::GenerateLayoutByControlConsoleData(const UDMXControlConsoleData* ControlConsoleData)
{
	if (!ControlConsoleData)
	{
		return;
	}

	LayoutRows.Reset(LayoutRows.Num());

	const TArray<UDMXControlConsoleFaderGroupRow*> FaderGroupRows = ControlConsoleData->GetFaderGroupRows();

	const float NumSteps = FaderGroupRows.Num();
	FScopedSlowTask Task(NumSteps, LOCTEXT("GenerateLayoutByDataSlowTask", "Updating Control Console..."));
	Task.MakeDialogDelayed(.5f);

	for (const UDMXControlConsoleFaderGroupRow* FaderGroupRow : FaderGroupRows)
	{
		Task.EnterProgressFrame();

		if (!FaderGroupRow)
		{
			continue;
		}

		UDMXControlConsoleEditorGlobalLayoutRow* LayoutRow = NewObject<UDMXControlConsoleEditorGlobalLayoutRow>(this, NAME_None, RF_Transactional);
		for (UDMXControlConsoleFaderGroup* FaderGroup : FaderGroupRow->GetFaderGroups())
		{
			if (!FaderGroup)
			{
				continue;
			}

			if (ContainsFaderGroup(FaderGroup))
			{
				continue;
			}

			LayoutRow->Modify();
			LayoutRow->CreateFaderGroupController(FaderGroup, FaderGroup->GetFaderGroupName());
		}

		LayoutRows.Add(LayoutRow);
	}

	// Remove all active controllers no more contained by the layout
	ActiveFaderGroupControllers.RemoveAll(
		[this](const TWeakObjectPtr<UDMXControlConsoleFaderGroupController>& ActiveFaderGroupController)
		{
			return !ActiveFaderGroupController.IsValid() || !ContainsFaderGroupController(ActiveFaderGroupController.Get());
		});

	// The default layout can't contain unpatched fader group controllers
	if (IsDefaultLayout())
	{
		constexpr bool bHasFixturePatch = false;
		CleanLayoutFromFaderGroupControllers(bHasFixturePatch);
	}
}

void UDMXControlConsoleEditorGlobalLayoutBase::SortLayoutByUniverseID()
{
	TMap<int32, TArray<UDMXControlConsoleFaderGroupController*>> UniverseIDToControllersMap = GetUniverseIDToControllersMap();

	constexpr bool bClearPatchedControllers = true;
	constexpr bool bClearUnpatchedControllers = true;
	ClearAll(bClearPatchedControllers, bClearUnpatchedControllers);

	// Sort map keys by universe id
	UniverseIDToControllersMap.KeySort([](const int32 KeyA, const int32 KeyB) 
		{	
			return KeyA < KeyB; 
		});

	// Create the new layout with the correct controllers sorting
	for (const TPair<int32, TArray<UDMXControlConsoleFaderGroupController*>>& UniverseIDToControllers : UniverseIDToControllersMap)
	{
		UDMXControlConsoleEditorGlobalLayoutRow* NewLayoutRow = AddNewRowToLayout();
		if (!NewLayoutRow)
		{
			continue;
		}

		const TArray<UDMXControlConsoleFaderGroupController*>& Controllers = UniverseIDToControllers.Value;

		const float NumSteps = Controllers.Num();
		FScopedSlowTask Task(NumSteps, LOCTEXT("SortLayoutSlowTask", "Updating Control Console..."));
		Task.MakeDialogDelayed(.5f);

		for (UDMXControlConsoleFaderGroupController* Controller : Controllers)
		{
			Task.EnterProgressFrame();

			if (!Controller)
			{
				continue;
			}

			// Remember controller active state before editing
			const bool bIsActive = Controller->IsActive();

			Controller->Modify();
			const TArray<TWeakObjectPtr<UDMXControlConsoleFaderGroup>> FaderGroups = Controller->GetFaderGroups();
			for (const TWeakObjectPtr<UDMXControlConsoleFaderGroup>& FaderGroup : FaderGroups)
			{
				if (FaderGroup.IsValid())
				{
					Controller->UnPossess(FaderGroup.Get());
				}
			}

			TArray<UDMXControlConsoleFaderGroup*> Result;
			Algo::TransformIf(FaderGroups, Result,
				[](const TWeakObjectPtr<UDMXControlConsoleFaderGroup>& FaderGroup)
				{
					return FaderGroup.IsValid();
				},
				[](const TWeakObjectPtr<UDMXControlConsoleFaderGroup>& FaderGroup)
				{
					return FaderGroup.Get();
				});

			NewLayoutRow->Modify();
			UDMXControlConsoleFaderGroupController* SortedController = NewLayoutRow->CreateFaderGroupController(Result, Controller->GetUserName());
			if (SortedController)
			{
				SortedController->Modify();
				if (FaderGroups.Num() > 1)
				{
					SortedController->Group();
				}

				SortedController->SetIsActive(bIsActive);
				if (bIsActive)
				{
					AddToActiveFaderGroupControllers(SortedController);
				}
			}
		}
	}
}

void UDMXControlConsoleEditorGlobalLayoutBase::ClearAll(const bool bPatchedControllers, const bool bUnpatchedControllers)
{
	if (bPatchedControllers && bUnpatchedControllers)
	{
		LayoutRows.Reset();
		ActiveFaderGroupControllers.Reset();
	}
	else if (bPatchedControllers)
	{
		constexpr bool bHasFixturePatch = true;
		CleanLayoutFromFaderGroupControllers(bHasFixturePatch);
	}
	else if (bUnpatchedControllers)
	{
		constexpr bool bHasFixturePatch = false;
		CleanLayoutFromFaderGroupControllers(bHasFixturePatch);
	}
}

void UDMXControlConsoleEditorGlobalLayoutBase::ClearEmptyLayoutRows()
{
	const TArray<UDMXControlConsoleFaderGroupController*> AllFaderGroupControllers = GetAllFaderGroupControllers();
	for (UDMXControlConsoleFaderGroupController* FaderGroupController : AllFaderGroupControllers)
	{
		if (FaderGroupController && FaderGroupController->GetFaderGroups().IsEmpty())
		{
			FaderGroupController->Modify();
			FaderGroupController->Destroy();
		}
	}

	LayoutRows.RemoveAll([](const UDMXControlConsoleEditorGlobalLayoutRow* LayoutRow)
		{
			return LayoutRow && LayoutRow->GetFaderGroupControllers().IsEmpty();
		});
}

void UDMXControlConsoleEditorGlobalLayoutBase::Register(UDMXControlConsoleData* ControlConsoleData)
{
	if (!ensureMsgf(ControlConsoleData, TEXT("Invalid control console data, cannot register layout correctly.")))
	{
		return;
	}

	if (!ensureMsgf(!bIsRegistered, TEXT("Layout already registered to dmx library delegates.")))
	{
		return;
	}

	UDMXControlConsoleEditorLayouts& OwnerEditorLayouts = GetOwnerEditorLayoutsChecked();
	if (!OwnerEditorLayouts.GetOnActiveLayoutChanged().IsBoundToObject(this))
	{
		OwnerEditorLayouts.GetOnActiveLayoutChanged().AddUObject(this, &UDMXControlConsoleEditorGlobalLayoutBase::OnActiveLayoutChanged);
	}

	if (IsDefaultLayout())
	{
		if (!UDMXLibrary::GetOnEntitiesRemoved().IsBoundToObject(this))
		{
			UDMXLibrary::GetOnEntitiesRemoved().AddUObject(this, &UDMXControlConsoleEditorGlobalLayoutBase::OnFixturePatchRemovedFromLibrary);
		}

		if (!ControlConsoleData->GetOnFaderGroupAdded().IsBoundToObject(this))
		{
			ControlConsoleData->GetOnFaderGroupAdded().AddUObject(this, &UDMXControlConsoleEditorGlobalLayoutBase::OnFaderGroupAddedToData);
		}
	}

	bIsRegistered = true;
}

void UDMXControlConsoleEditorGlobalLayoutBase::Unregister(UDMXControlConsoleData* ControlConsoleData)
{
	if (!ensureMsgf(ControlConsoleData, TEXT("Invalid control console data, cannot register layout correctly.")))
	{
		return;
	}

	if (!ensureMsgf(bIsRegistered, TEXT("Layout already unregistered from dmx library delegates.")))
	{
		return;
	}

	UDMXControlConsoleEditorLayouts& OwnerEditorLayouts = GetOwnerEditorLayoutsChecked();
	if (OwnerEditorLayouts.GetOnActiveLayoutChanged().IsBoundToObject(this))
	{
		OwnerEditorLayouts.GetOnActiveLayoutChanged().RemoveAll(this);
	}

	if (IsDefaultLayout())
	{
		if (UDMXLibrary::GetOnEntitiesRemoved().IsBoundToObject(this))
		{
			UDMXLibrary::GetOnEntitiesRemoved().RemoveAll(this);
		}

		if (ControlConsoleData->GetOnFaderGroupAdded().IsBoundToObject(this))
		{
			ControlConsoleData->GetOnFaderGroupAdded().RemoveAll(this);
		}
	}

	bIsRegistered = false;
}

void UDMXControlConsoleEditorGlobalLayoutBase::PostLoad()
{
	Super::PostLoad();

	ClearEmptyLayoutRows();
}

bool UDMXControlConsoleEditorGlobalLayoutBase::IsDefaultLayout() const
{
	const UDMXControlConsoleEditorLayouts& OwnerEditorLayouts = GetOwnerEditorLayoutsChecked();
	return &OwnerEditorLayouts.GetDefaultLayoutChecked() == this;
}

TMap<int32, TArray<UDMXControlConsoleFaderGroupController*>> UDMXControlConsoleEditorGlobalLayoutBase::GetUniverseIDToControllersMap() const
{
	TMap<int32, TArray<UDMXControlConsoleFaderGroupController*>> UniverseIDToControllersMap;
	const TArray<UDMXControlConsoleFaderGroupController*> FaderGroupControllers = GetAllFaderGroupControllers();
	for (UDMXControlConsoleFaderGroupController* FaderGroupController : FaderGroupControllers)
	{
		if (!FaderGroupController)
		{
			continue;
		}

		TArray<TWeakObjectPtr<UDMXControlConsoleFaderGroup>> FaderGroups = FaderGroupController->GetFaderGroups();
		if (FaderGroups.IsEmpty())
		{
			continue;
		}

		int32 UniverseID = 0;
		if (FaderGroupController->HasFixturePatch())
		{
			// Sort fader groups by universe id
			Algo::StableSortBy(FaderGroups,
				[](const TWeakObjectPtr<UDMXControlConsoleFaderGroup>& Item) -> int64
				{
					if (!Item.IsValid())
					{
						return TNumericLimits<int64>::Max();
					}

					const UDMXEntityFixturePatch* FixturePatch = Item.IsValid() ? Item->GetFixturePatch() : nullptr;
					if (FixturePatch)
					{
						return (int64)FixturePatch->GetUniverseID() * DMX_UNIVERSE_SIZE + FixturePatch->GetStartingChannel();
					}
					else
					{
						return TNumericLimits<int64>::Max();
					}
				});

			if (const UDMXEntityFixturePatch* FixturePatch = FaderGroups[0]->GetFixturePatch())
			{
				UniverseID = FixturePatch->GetUniverseID();
			}
		}

		TArray<UDMXControlConsoleFaderGroupController*>& Controllers = UniverseIDToControllersMap.FindOrAdd(UniverseID);
		Controllers.Add(FaderGroupController);
	}

	return UniverseIDToControllersMap;
}

void UDMXControlConsoleEditorGlobalLayoutBase::UpdateActiveLayoutByControllersData() const
{
	const UDMXControlConsoleEditorGlobalLayoutBase* ActiveLayout = GetOwnerEditorLayoutsChecked().GetActiveLayout();
	if (ActiveLayout != this)
	{
		return;
	}

	const TArray<UDMXControlConsoleFaderGroupController*> AllFaderGroupControllers = GetAllFaderGroupControllers();

	const float NumSteps = AllFaderGroupControllers.Num();
	FScopedSlowTask Task(NumSteps, LOCTEXT("UpdateLayoutByControllersDataSlowTask", "Updating Control Console..."));
	Task.MakeDialogDelayed(.5f);

	for (UDMXControlConsoleFaderGroupController* FaderGroupController : AllFaderGroupControllers)
	{
		Task.EnterProgressFrame();

		if (!FaderGroupController)
		{
			continue;
		}

		FaderGroupController->Modify();

		// Ensure that all fader groups are possessed by controllers in the active layout
		const TArray<TWeakObjectPtr<UDMXControlConsoleFaderGroup>>& FaderGroups = FaderGroupController->GetFaderGroups();
		for (const TWeakObjectPtr<UDMXControlConsoleFaderGroup>& FaderGroup : FaderGroups)
		{
			if (FaderGroup.IsValid())
			{
				FaderGroupController->Possess(FaderGroup.Get());
			}
		}

		FaderGroupController->GenerateElementControllers();
		if (FaderGroups.Num() > 1)
		{
			FaderGroupController->Group();
		}
	}
}

void UDMXControlConsoleEditorGlobalLayoutBase::OnActiveLayoutChanged(const UDMXControlConsoleEditorGlobalLayoutBase* ActiveLayout)
{
	if (ActiveLayout == this)
	{
		UpdateActiveLayoutByControllersData();
	}
}

void UDMXControlConsoleEditorGlobalLayoutBase::OnFixturePatchRemovedFromLibrary(UDMXLibrary* Library, TArray<UDMXEntity*> Entities)
{
	if (Entities.IsEmpty())
	{
		return;
	}

	Modify();

	for (const UDMXEntity* Entity : Entities)
	{
		const UDMXEntityFixturePatch* FixturePatch = Cast<UDMXEntityFixturePatch>(Entity);
		if (!FixturePatch)
		{
			continue;
		}

		UDMXControlConsoleFaderGroupController* FaderGroupController = FindFaderGroupControllerByFixturePatch(FixturePatch);
		if (!FaderGroupController)
		{
			continue;
		}

		const TArray<TWeakObjectPtr<UDMXControlConsoleFaderGroup>>& FaderGroups = FaderGroupController->GetFaderGroups();
		const TWeakObjectPtr<UDMXControlConsoleFaderGroup>* PatchedFaderGroup = Algo::FindByPredicate(FaderGroups,
			[FixturePatch](const TWeakObjectPtr<UDMXControlConsoleFaderGroup>& FaderGroup)
			{
				return FaderGroup.IsValid() && FaderGroup->GetFixturePatch() == FixturePatch;
			});

		if (!PatchedFaderGroup)
		{
			continue;
		}

		FaderGroupController->Modify();
		FaderGroupController->UnPossess(PatchedFaderGroup->Get());
		FaderGroupController->GenerateElementControllers();
		if (FaderGroupController->GetFaderGroups().Num() > 1)
		{
			FaderGroupController->Group();
		}
		else if (FaderGroupController->GetFaderGroups().IsEmpty())
		{
			RemoveFromActiveFaderGroupControllers(FaderGroupController);
			FaderGroupController->Destroy();
		}
	}

	constexpr bool bHasFixturePatch = false;
	CleanLayoutFromFaderGroupControllers(bHasFixturePatch);
	ClearEmptyLayoutRows();
}

void UDMXControlConsoleEditorGlobalLayoutBase::OnFaderGroupAddedToData(const UDMXControlConsoleFaderGroup* FaderGroup)
{
	if (!FaderGroup || !FaderGroup->HasFixturePatch() || ContainsFaderGroup(FaderGroup))
	{
		return;
	}
	
	Modify();

	UDMXControlConsoleEditorGlobalLayoutRow* LayoutRow = AddNewRowToLayout();
	LayoutRow->Modify();
	LayoutRow->CreateFaderGroupController(const_cast<UDMXControlConsoleFaderGroup*>(FaderGroup), FaderGroup->GetFaderGroupName());
	LayoutRows.Add(LayoutRow);

	SortLayoutByUniverseID();

	// Update the active layout only if it's not the default layout
	const UDMXControlConsoleEditorGlobalLayoutBase* ActiveLayout = GetOwnerEditorLayoutsChecked().GetActiveLayout();
	if (ActiveLayout && ActiveLayout != &GetOwnerEditorLayoutsChecked().GetDefaultLayoutChecked())
	{
		ActiveLayout->UpdateActiveLayoutByControllersData();
	}
}

void UDMXControlConsoleEditorGlobalLayoutBase::CleanLayoutFromFaderGroupControllers(const bool bHasFixturePatch)
{
	const TArray<UDMXControlConsoleFaderGroupController*> AllFaderGroupControllers = GetAllFaderGroupControllers();
	for (UDMXControlConsoleFaderGroupController* FaderGroupController : AllFaderGroupControllers)
	{
		if (!FaderGroupController)
		{
			continue;
		}

		// True if the two conditions are concordant
		const bool bMatchCondition =
			(FaderGroupController->HasFixturePatch() && bHasFixturePatch) ||
			(!FaderGroupController->HasFixturePatch() && !bHasFixturePatch);

		if (bMatchCondition)
		{
			RemoveFromActiveFaderGroupControllers(FaderGroupController);

			FaderGroupController->Modify();
			FaderGroupController->Destroy();
		}
	}

	if (!bHasFixturePatch)
	{
		CleanDefaultLayoutFromUnpatchedFaderGroups();
	}

	ClearEmptyLayoutRows();
}

void UDMXControlConsoleEditorGlobalLayoutBase::CleanDefaultLayoutFromUnpatchedFaderGroups()
{
	// The default layout can't contain fader group controllers with unpatched fader groups
	if (!IsDefaultLayout())
	{
		return;
	}

	const TArray<UDMXControlConsoleFaderGroupController*> AllFaderGroupControllers = GetAllFaderGroupControllers();
	for (UDMXControlConsoleFaderGroupController* FaderGroupController : AllFaderGroupControllers)
	{
		if (!FaderGroupController)
		{
			continue;
		}

		FaderGroupController->Modify();

		const TArray<TWeakObjectPtr<UDMXControlConsoleFaderGroup>> FaderGroups = FaderGroupController->GetFaderGroups();
		for (const TWeakObjectPtr<UDMXControlConsoleFaderGroup>& FaderGroup : FaderGroups)
		{
			if (!FaderGroup.IsValid() || FaderGroup->HasFixturePatch())
			{
				continue;
			}

			FaderGroupController->UnPossess(FaderGroup.Get());
			FaderGroupController->GenerateElementControllers();
			if (FaderGroupController->GetFaderGroups().Num() > 1)
			{
				FaderGroupController->Group();
			}
			else if (FaderGroupController->GetFaderGroups().IsEmpty())
			{
				RemoveFromActiveFaderGroupControllers(FaderGroupController);
				FaderGroupController->Destroy();
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
