// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleCueStackModel.h"

#include "Algo/AllOf.h"
#include "DMXControlConsole.h"
#include "DMXControlConsoleData.h"
#include "DMXControlConsoleEditorData.h"
#include "DMXControlConsoleFaderBase.h"
#include "DMXControlConsoleFaderGroup.h"
#include "Layouts/Controllers/DMXControlConsoleElementController.h"
#include "Layouts/Controllers/DMXControlConsoleFaderGroupController.h"
#include "Layouts/DMXControlConsoleEditorGlobalLayoutBase.h"
#include "Layouts/DMXControlConsoleEditorLayouts.h"
#include "Misc/MessageDialog.h"
#include "ScopedTransaction.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleCueStackModel"

namespace UE::DMX::Private
{
	FDMXControlConsoleCueStackModel::FDMXControlConsoleCueStackModel(UDMXControlConsole* ControlConsole)
		: WeakControlConsole(ControlConsole)
	{}

	UDMXControlConsoleData* FDMXControlConsoleCueStackModel::GetControlConsoleData() const
	{
		return WeakControlConsole.IsValid() ? WeakControlConsole->GetControlConsoleData() : nullptr;
	}

	UDMXControlConsoleEditorData* FDMXControlConsoleCueStackModel::GetControlConsoleEditorData() const
	{
		return WeakControlConsole.IsValid() ? Cast<UDMXControlConsoleEditorData>(WeakControlConsole->ControlConsoleEditorData) : nullptr;
	}

	UDMXControlConsoleEditorLayouts* FDMXControlConsoleCueStackModel::GetControlConsoleEditorLayouts() const
	{
		return WeakControlConsole.IsValid() ? Cast<UDMXControlConsoleEditorLayouts>(WeakControlConsole->ControlConsoleEditorLayouts) : nullptr;
	}

	UDMXControlConsoleCueStack* FDMXControlConsoleCueStackModel::GetControlConsoleCueStack() const
	{
		const UDMXControlConsoleData* ControlConsoleData = GetControlConsoleData();
		return ControlConsoleData ? ControlConsoleData->GetCueStack() : nullptr;
	}

	bool FDMXControlConsoleCueStackModel::IsAddNewCueButtonEnabled() const
	{
		const UDMXControlConsoleData* ControlConsoleData = GetControlConsoleData();
		return ControlConsoleData && !ControlConsoleData->GetAllFaderGroups().IsEmpty();
	}

	bool FDMXControlConsoleCueStackModel::IsStoreCueButtonEnabled(const FDMXControlConsoleCue& Cue) const
	{
		const UDMXControlConsoleEditorData* ControlConsoleEditorData = GetControlConsoleEditorData();
		const UDMXControlConsoleCueStack* ControlConsoleCueStack = GetControlConsoleCueStack();
		if (!ControlConsoleEditorData || !ControlConsoleCueStack)
		{
			return false;
		}

		if (Cue == ControlConsoleEditorData->LoadedCue)
		{
			return ControlConsoleCueStack->CanStore();
		}

		return true;
	}

	void FDMXControlConsoleCueStackModel::AddNewCue() const
	{
		UDMXControlConsoleEditorData* ControlConsoleEditorData = GetControlConsoleEditorData();
		const UDMXControlConsoleData* ControlConsoleData = GetControlConsoleData();
		UDMXControlConsoleCueStack* ControlConsoleCueStack = ControlConsoleData ? ControlConsoleData->GetCueStack() : nullptr;
		const UDMXControlConsoleEditorLayouts* ControlConsoleLayouts = GetControlConsoleEditorLayouts();
		const UDMXControlConsoleEditorGlobalLayoutBase* ActiveLayout = ControlConsoleLayouts ? ControlConsoleLayouts->GetActiveLayout() : nullptr;
		if (!ControlConsoleEditorData || !ControlConsoleCueStack || !ActiveLayout)
		{
			return;
		}

		TArray<UDMXControlConsoleFaderBase*> FadersToCue;
		const TArray<UDMXControlConsoleFaderGroup*> AllFaderGroups = ControlConsoleData->GetAllFaderGroups();
		for (const UDMXControlConsoleFaderGroup* FaderGroup : AllFaderGroups)
		{
			if (!FaderGroup)
			{
				continue;
			}

			const UDMXControlConsoleFaderGroupController* FaderGroupController = Cast<UDMXControlConsoleFaderGroupController>(FaderGroup->GetFaderGroupController());
			if (FaderGroupController && ActiveLayout->ContainsFaderGroupController(FaderGroupController) && FaderGroupController->IsActive())
			{
				FadersToCue.Append(FaderGroup->GetAllFaders());
			}
		}

		if (FadersToCue.IsEmpty())
		{
			return;
		}

		const FScopedTransaction AddNewCueTransaction(LOCTEXT("AddNewCueTransaction", "Add Cue"));

		// Add a new cue with faders data
		ControlConsoleCueStack->PreEditChange(nullptr);
		const FDMXControlConsoleCue* NewCue = ControlConsoleCueStack->AddNewCue(FadersToCue);
		ControlConsoleCueStack->PostEditChange();

		// Update the loaded cue
		if (ensureMsgf(NewCue, TEXT("Invalid newly created cue. Can't load the cue correctly")))
		{
			ControlConsoleEditorData->PreEditChange(UDMXControlConsoleEditorData::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UDMXControlConsoleEditorData, LoadedCue)));
			ControlConsoleEditorData->LoadedCue = *NewCue;
			ControlConsoleEditorData->PostEditChange();
		}
	}

	void FDMXControlConsoleCueStackModel::StoreCue(const FDMXControlConsoleCue& Cue) const
	{
		const UDMXControlConsoleData* ControlConsoleData = GetControlConsoleData();
		UDMXControlConsoleCueStack* ControlConsoleCueStack = ControlConsoleData ? ControlConsoleData->GetCueStack() : nullptr;
		const UDMXControlConsoleEditorLayouts* ControlConsoleLayouts = GetControlConsoleEditorLayouts();
		const UDMXControlConsoleEditorGlobalLayoutBase* ActiveLayout = ControlConsoleLayouts ? ControlConsoleLayouts->GetActiveLayout() : nullptr;
		if (!ControlConsoleCueStack || !ActiveLayout)
		{
			return;
		}

		// Open a message dialog to confirm data overwriting
		const FText DialogText = LOCTEXT("StoreCueMessageDialog", "Are you sure you want to overwrite the data of the selected cue?");
		if (FMessageDialog::Open(EAppMsgType::YesNo, DialogText) != EAppReturnType::Yes)
		{
			return;
		}

		TArray<UDMXControlConsoleFaderBase*> FadersToCue;
		const TArray<UDMXControlConsoleFaderGroup*> AllFaderGroups = ControlConsoleData->GetAllFaderGroups();
		for (const UDMXControlConsoleFaderGroup* FaderGroup : AllFaderGroups)
		{
			if (!FaderGroup)
			{
				continue;
			}

			const UDMXControlConsoleFaderGroupController* FaderGroupController = Cast<UDMXControlConsoleFaderGroupController>(FaderGroup->GetFaderGroupController());
			if (FaderGroupController && ActiveLayout->ContainsFaderGroupController(FaderGroupController) && FaderGroupController->IsActive())
			{
				FadersToCue.Append(FaderGroup->GetAllFaders());
			}
		}

		ControlConsoleCueStack->UpdateCueData(Cue.CueID, FadersToCue);
	}

	void FDMXControlConsoleCueStackModel::RecallCue(const FDMXControlConsoleCue& Cue) const
	{
		UDMXControlConsoleEditorData* ControlConsoleEditorData = GetControlConsoleEditorData();
		UDMXControlConsoleCueStack* ControlConsoleCueStack = GetControlConsoleCueStack();
		if (!ControlConsoleEditorData || !ControlConsoleCueStack)
		{
			return;
		}

		const FScopedTransaction RecallCueClickedTransaction(LOCTEXT("RecallCueTransaction", "Recall Cue"));

		// Update the loaded cue
		ControlConsoleEditorData->PreEditChange(UDMXControlConsoleEditorData::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UDMXControlConsoleEditorData, LoadedCue)));
		ControlConsoleEditorData->LoadedCue = Cue;
		ControlConsoleEditorData->PostEditChange();

		// Sync controllers to the new fader values
		const TMap<TWeakObjectPtr<UDMXControlConsoleFaderBase>, uint32>& FaderToValueMap = Cue.FaderToValueMap;
		for (const TTuple<TWeakObjectPtr<UDMXControlConsoleFaderBase>, uint32>& FaderToValue : FaderToValueMap)
		{
			const UDMXControlConsoleFaderBase* Fader = FaderToValue.Key.Get();
			if (!Fader)
			{
				continue;
			}

			UDMXControlConsoleElementController* ElementController = Cast<UDMXControlConsoleElementController>(Fader->GetElementController());
			if (!ElementController)
			{
				continue;
			}

			const TArray<UDMXControlConsoleFaderBase*> Faders = ElementController->GetFaders();
			if (Faders.IsEmpty())
			{
				continue;
			}

			const UDMXControlConsoleFaderBase* FirstFader = Faders[0];
			const bool bHasUniformDataType = Algo::AllOf(Faders,
				[FirstFader](const UDMXControlConsoleFaderBase* Fader)
				{
					return Fader && Fader->GetDataType() == FirstFader->GetDataType();
				});

			// Sync only if all faders in the controller have the same data type
			if (bHasUniformDataType)
			{
				const uint32 Value = FaderToValue.Value;

				const uint8 NumChannels = static_cast<uint8>(Fader->GetDataType()) + 1;
				const uint32 ValueRange = static_cast<uint32>(FMath::Pow(2.f, 8.f * NumChannels) - 1);
				const float NormalizedValue = static_cast<float>(Value) / ValueRange;

				ElementController->PreEditChange(UDMXControlConsoleElementController::StaticClass()->FindPropertyByName(UDMXControlConsoleElementController::GetValuePropertyName()));
				constexpr bool bSynchElements = false;
				ElementController->SetValue(NormalizedValue, bSynchElements);
				ElementController->PostEditChange();
			}
		}

		// Recall the selected cue
		ControlConsoleCueStack->PreEditChange(nullptr);
		ControlConsoleCueStack->Recall(Cue);
		ControlConsoleCueStack->PostEditChange();
	}

	void FDMXControlConsoleCueStackModel::ClearCueStack() const
	{
		UDMXControlConsoleCueStack* ControlConsoleCueStack = GetControlConsoleCueStack();
		if (!ControlConsoleCueStack)
		{
			return;
		}

		const FScopedTransaction ClearAllCuesTransaction(LOCTEXT("ClearCueStackTransaction", "Clear Cue"));
		ControlConsoleCueStack->PreEditChange(nullptr);
		ControlConsoleCueStack->Clear();
		ControlConsoleCueStack->PostEditChange();
	}
}

#undef LOCTEXT_NAMESPACE
