// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaEaseCurveTool.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "AvaSequencer.h"
#include "Dialogs/DlgPickAssetPath.h"
#include "EaseCurveTool/AvaEaseCurveKeySelection.h"
#include "EaseCurveTool/AvaEaseCurveToolSettings.h"
#include "EaseCurveTool/Widgets/SAvaEaseCurveTool.h"
#include "Editor.h"
#include "EngineAnalytics.h"
#include "Factories/CurveFactory.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "ISequencer.h"
#include "ISettingsModule.h"
#include "Math/UnrealMathUtility.h"
#include "ScopedTransaction.h"
#include "Settings/AvaSequencerSettings.h"
#include "Widgets/Notifications/SNotificationList.h"

using namespace UE::Sequencer;

#define LOCTEXT_NAMESPACE "AvaEaseCurveTool"

FAvaEaseCurveTool::FAvaEaseCurveTool(const TSharedRef<FAvaSequencer>& InSequencer)
	: AvaSequencerWeak(InSequencer)
{
	EaseCurve = NewObject<UAvaEaseCurve>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);

	UpdateEaseCurveFromSequencerKeySelections();

	InSequencer->GetSequencer()->GetSelectionChangedObjectGuids().AddRaw(this, &FAvaEaseCurveTool::OnSequencerSelectionChanged);
}

FAvaEaseCurveTool::~FAvaEaseCurveTool()
{
	if (const TSharedPtr<FAvaSequencer> AvaSequencer = AvaSequencerWeak.Pin())
	{
		AvaSequencer->GetSequencer()->GetSelectionChangedObjectGuids().RemoveAll(this);
	}
}

void FAvaEaseCurveTool::OnSequencerSelectionChanged(TArray<FGuid> InObjectGuids)
{
	UpdateEaseCurveFromSequencerKeySelections();
}

void FAvaEaseCurveTool::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(EaseCurve);
}

FString FAvaEaseCurveTool::GetReferencerName() const
{
	return TEXT("AvaEaseCurveTool");
}

TSharedRef<SWidget> FAvaEaseCurveTool::GenerateWidget()
{
	UpdateEaseCurveFromSequencerKeySelections();

	if (!ToolWidget.IsValid())
	{
		ToolWidget = SNew(SAvaEaseCurveTool, SharedThis(this))
			.InitialTangents(GetEaseCurveTangents())
			.Visibility(this, &FAvaEaseCurveTool::GetVisibility)
			.ToolOperation(this, &FAvaEaseCurveTool::GetToolOperation);
	}

	return ToolWidget.ToSharedRef();
}

EVisibility FAvaEaseCurveTool::GetVisibility() const
{
	return (KeyCache.GetTotalSelectedKeys() > 0 && !KeyCache.IsLastOnlySelectedKey()) ? EVisibility::Visible : EVisibility::Collapsed;
}

TObjectPtr<UAvaEaseCurve> FAvaEaseCurveTool::GetToolCurve() const
{
	return EaseCurve;
}

FRichCurve* FAvaEaseCurveTool::GetToolRichCurve() const
{
	return &EaseCurve->FloatCurve;
}

FAvaEaseCurveTangents FAvaEaseCurveTool::GetEaseCurveTangents() const
{
	return EaseCurve->GetTangents();
}

void FAvaEaseCurveTool::SetEaseCurveTangents_Internal(const FAvaEaseCurveTangents& InTangents, const EAvaEaseCurveToolOperation InOperation, const bool bInBroadcastUpdate) const
{
	switch (InOperation)
	{
	case EAvaEaseCurveToolOperation::InOut:
		EaseCurve->SetTangents(InTangents);
		break;
	case EAvaEaseCurveToolOperation::In:
		EaseCurve->SetEndTangent(InTangents.End, InTangents.EndWeight);
		break;
	case EAvaEaseCurveToolOperation::Out:
		EaseCurve->SetStartTangent(InTangents.Start, InTangents.StartWeight);
		break;
	}

	if (bInBroadcastUpdate)
	{
		EaseCurve->BroadcastUpdate();
	}
}

void FAvaEaseCurveTool::SetEaseCurveTangents(const FAvaEaseCurveTangents& InTangents
	, const EAvaEaseCurveToolOperation InOperation
	, const bool bInBroadcastUpdate
	, const bool bInSetSequencerTangents
	, const FText& InTransactionText)
{
	if (InTangents == GetEaseCurveTangents())
	{
		return;
	}

	const FScopedTransaction Transaction(InTransactionText, !GIsTransacting);
	EaseCurve->Modify();

	SetEaseCurveTangents_Internal(InTangents, InOperation, bInBroadcastUpdate);

	if (bInSetSequencerTangents)
	{
		SetSequencerKeySelectionTangents(InTangents, InOperation);
	}

	KeyCache = FAvaEaseCurveKeySelection(AvaSequencerWeak.Pin());
}

void FAvaEaseCurveTool::ResetEaseCurveTangents(const EAvaEaseCurveToolOperation InOperation)
{
	FText TransactionText;

	switch (InOperation)
	{
	case EAvaEaseCurveToolOperation::InOut:
		TransactionText = LOCTEXT("ResetTangents", "Reset Tangents");
		break;
	case EAvaEaseCurveToolOperation::In:
		TransactionText = LOCTEXT("ResetEndTangents", "Reset End Tangents");
		break;
	case EAvaEaseCurveToolOperation::Out:
		TransactionText = LOCTEXT("ResetStartTangents", "Reset Start Tangents");
		break;
	}

	const FScopedTransaction Transaction(TransactionText);
	EaseCurve->ModifyOwner();

	const FAvaEaseCurveTangents ZeroTangents;
	SetEaseCurveTangents(ZeroTangents, InOperation, /*bInBroadcastUpdate=*/true, /*bInSetSequencerTangents=*/true, TransactionText);

	// Update ease curve combobox widget and zoom graph editor
	if (ToolWidget.IsValid())
	{
		ToolWidget->SetTangents(ZeroTangents, InOperation, /*bInSetEaseCurve=*/false, /*bInBroadcastUpdate=*/false, /*bInSetSequencerTangents=*/false);
	}
}

void FAvaEaseCurveTool::FlattenOrStraightenTangents(const EAvaEaseCurveToolOperation InOperation, const bool bInFlattenTangents)
{
	FText TransactionText;
	if (bInFlattenTangents)
	{
		switch (InOperation)
		{
		case EAvaEaseCurveToolOperation::InOut:
			TransactionText = LOCTEXT("FlattenTangents", "Flatten Tangents");
			break;
		case EAvaEaseCurveToolOperation::In:
			TransactionText = LOCTEXT("FlattenEndTangents", "Flatten End Tangents");
			break;
		case EAvaEaseCurveToolOperation::Out:
			TransactionText = LOCTEXT("FlattenStartTangents", "Flatten Start Tangents");
			break;
		}
	}
	else
	{
		switch (InOperation)
		{
		case EAvaEaseCurveToolOperation::InOut:
			TransactionText = LOCTEXT("StraightenTangents", "Straighten Tangents");
			break;
		case EAvaEaseCurveToolOperation::In:
			TransactionText = LOCTEXT("StraightenEndTangents", "Straighten End Tangents");
			break;
		case EAvaEaseCurveToolOperation::Out:
			TransactionText = LOCTEXT("StraightenStartTangents", "Straighten Start Tangents");
			break;
		}
	}
	const FScopedTransaction Transaction(TransactionText);
	EaseCurve->ModifyOwner();

	if (InOperation == EAvaEaseCurveToolOperation::Out || InOperation == EAvaEaseCurveToolOperation::InOut)
	{
		EaseCurve->FlattenOrStraightenTangents(EaseCurve->GetStartKeyHandle(), bInFlattenTangents);
	}
	if (InOperation == EAvaEaseCurveToolOperation::In || InOperation == EAvaEaseCurveToolOperation::InOut)
	{
		EaseCurve->FlattenOrStraightenTangents(EaseCurve->GetEndKeyHandle(), bInFlattenTangents);
	}

	const FAvaEaseCurveTangents NewTangents = EaseCurve->GetTangents();
	SetEaseCurveTangents(NewTangents, InOperation, /*bInBroadcastUpdate=*/true, /*bInSetSequencerTangents=*/true, TransactionText);

	// Update ease curve combobox widget and zoom graph editor
	if (ToolWidget.IsValid())
	{
		ToolWidget->SetTangents(NewTangents, InOperation, /*bInSetEaseCurve=*/false, /*bInBroadcastUpdate=*/false, /*bInSetSequencerTangents=*/false);
	}
}

void FAvaEaseCurveTool::ApplyQuickEaseToSequencerKeySelections(const EAvaEaseCurveToolOperation InOperation)
{
	const UAvaEaseCurveToolSettings* const Settings = GetDefault<UAvaEaseCurveToolSettings>();

	FAvaEaseCurveTangents Tangents;
	if (!FAvaEaseCurveTangents::FromString(Settings->GetQuickEaseTangents(), Tangents))
	{
		UE_LOG(LogTemp, Warning, TEXT("Ease curve tool failed to apply quick ease tangents: "
			"Could not parse configured quick ease tangent string."));
		return;
	}

	SetEaseCurveTangents(Tangents, InOperation, /*bInBroadcastUpdate=*/true, /*bInSetSequencerTangents=*/true);

	// Update ease curve combobox widget and zoom graph editor
	if (ToolWidget.IsValid())
	{
		ToolWidget->SetTangents(Tangents, InOperation, /*bInSetEaseCurve=*/false, /*bInBroadcastUpdate=*/false, /*bInSetSequencerTangents=*/false);
	}

	if (FEngineAnalytics::IsAvailable())
	{
		FString ParamValue;
		switch (InOperation)
		{
		case EAvaEaseCurveToolOperation::InOut:
			ParamValue = TEXT("InOut");
			break;
		case EAvaEaseCurveToolOperation::In:
			ParamValue = TEXT("In");
			break;
		case EAvaEaseCurveToolOperation::Out:
			ParamValue = TEXT("Out");
			break;
		}
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.MotionDesign.EaseCurveTool"), TEXT("QuickEase"), ParamValue);
	}
}

void FAvaEaseCurveTool::SetSequencerKeySelectionTangents(const FAvaEaseCurveTangents& InTangents, const EAvaEaseCurveToolOperation InOperation)
{
	KeyCache = FAvaEaseCurveKeySelection(AvaSequencerWeak.Pin());

	if (KeyCache.GetTotalSelectedKeys() == 0)
	{
		return;
	}

	const UAvaEaseCurveToolSettings* const EaseCurveToolSettings = GetDefault<UAvaEaseCurveToolSettings>();
	check(EaseCurveToolSettings);
	const bool bAutoFlipTangents = EaseCurveToolSettings->GetAutoFlipTangents();

	KeyCache.SetTangents(InTangents, InOperation, GetDisplayRate(), GetTickResolution(), bAutoFlipTangents);
}

void FAvaEaseCurveTool::UpdateEaseCurveFromSequencerKeySelections()
{
	KeyCache = FAvaEaseCurveKeySelection(AvaSequencerWeak.Pin());

	const UAvaEaseCurveToolSettings* const EaseCurveToolSettings = GetDefault<UAvaEaseCurveToolSettings>();
	check(EaseCurveToolSettings);
	const bool bAutoFlipTangents = EaseCurveToolSettings->GetAutoFlipTangents();

	const FAvaEaseCurveTangents AverageTangents = KeyCache.AverageTangents(GetDisplayRate(), GetTickResolution(), bAutoFlipTangents);

	SetEaseCurveTangents(AverageTangents, EAvaEaseCurveToolOperation::InOut, /*bInBroadcastUpdate=*/true, false);

	// Update the preset combobox widget
	if (ToolWidget.IsValid())
	{
		ToolWidget->SetTangents(AverageTangents, EAvaEaseCurveToolOperation::InOut
			, /*bInSetEaseCurve=*/false, /*bInBroadcastUpdate=*/false, /*bInSetSequencerTangents*/false);
	}
}

UCurveBase* FAvaEaseCurveTool::CreateCurveAsset() const
{
	const FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));

	FString OutNewPackageName;
	FString OutNewAssetName;
	AssetToolsModule.Get().CreateUniqueAssetName(TEXT("/Game/NewCurve"), TEXT(""), OutNewPackageName, OutNewAssetName);

	const TSharedRef<SDlgPickAssetPath> NewAssetDialog =
		SNew(SDlgPickAssetPath)
		.Title(LOCTEXT("CreateExternalCurve", "Create External Curve"))
		.DefaultAssetPath(FText::FromString(OutNewPackageName));

	if (NewAssetDialog->ShowModal() != EAppReturnType::Cancel)
	{
		const FString PackageName = NewAssetDialog->GetFullAssetPath().ToString();
		const FName AssetName = FName(*NewAssetDialog->GetAssetName().ToString());

		UPackage* const Package = CreatePackage(*PackageName);
		
		// Create curve object
		UObject* NewCurveObject = nullptr;
		
		if (UCurveFactory* const CurveFactory = Cast<UCurveFactory>(NewObject<UFactory>(GetTransientPackage(), UCurveFactory::StaticClass())))
		{
			CurveFactory->CurveClass = UCurveFloat::StaticClass();
			NewCurveObject = CurveFactory->FactoryCreateNew(CurveFactory->GetSupportedClass(), Package, AssetName, RF_Public | RF_Standalone, nullptr, GWarn);
		}

		if (NewCurveObject)
		{
			UCurveBase* AssetCurve = nullptr;
			
			// Copy curve data from current curve to newly create curve
			UCurveFloat* const DestCurve = CastChecked<UCurveFloat>(NewCurveObject);
			if (EaseCurve && DestCurve)
			{
				DestCurve->bIsEventCurve = false;

				AssetCurve = DestCurve;

				for (auto It(EaseCurve->FloatCurve.GetKeyIterator()); It; ++It)
				{
					const FRichCurveKey& Key = *It;
					const FKeyHandle KeyHandle = DestCurve->FloatCurve.AddKey(Key.Time, Key.Value);
					DestCurve->FloatCurve.GetKey(KeyHandle) = Key;
				}
			}

			FAssetRegistryModule::AssetCreated(NewCurveObject);

			Package->GetOutermost()->MarkPackageDirty();

			return AssetCurve;
		}
	}

	return nullptr;
}

EAvaEaseCurveToolOperation FAvaEaseCurveTool::GetToolOperation() const
{
	return OperationMode;
}

void FAvaEaseCurveTool::SetToolOperation(const EAvaEaseCurveToolOperation InNewOperation)
{
	OperationMode = InNewOperation;
}

bool FAvaEaseCurveTool::IsToolOperation(const EAvaEaseCurveToolOperation InNewOperation) const
{
	return OperationMode == InNewOperation;
}

bool FAvaEaseCurveTool::CanCopyTangentsToClipboard() const
{
	return true;
}

void FAvaEaseCurveTool::CopyTangentsToClipboard() const
{
	FPlatformApplicationMisc::ClipboardCopy(*EaseCurve->GetTangents().ToJson());

	ShowNotificationMessage(LOCTEXT("EaseCurveToolTangentsCopied", "Ease Curve Tool Tangents Copied!"));
}

bool FAvaEaseCurveTool::CanPasteTangentsFromClipboard() const
{
	FAvaEaseCurveTangents Tangents;
	return TangentsFromClipboardPaste(Tangents);
}

void FAvaEaseCurveTool::PasteTangentsFromClipboard() const
{
	FAvaEaseCurveTangents Tangents;
	if (TangentsFromClipboardPaste(Tangents))
	{
		EaseCurve->SetTangents(Tangents);
	}
}

bool FAvaEaseCurveTool::TangentsFromClipboardPaste(FAvaEaseCurveTangents& OutTangents)
{
	FString ClipboardString;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardString);

	// Expects four comma separated cubic bezier points that define the curve
	return FAvaEaseCurveTangents::FromString(ClipboardString, OutTangents);
}

bool FAvaEaseCurveTool::IsKeyInterpMode(const ERichCurveInterpMode InInterpMode, const ERichCurveTangentMode InTangentMode) const
{
	const FKeyHandle StartKeyHandle = EaseCurve->GetStartKeyHandle();
	return (EaseCurve->FloatCurve.GetKeyInterpMode(StartKeyHandle) == InInterpMode
		&& EaseCurve->FloatCurve.GetKeyTangentMode(StartKeyHandle) == InTangentMode);
}

void FAvaEaseCurveTool::SetKeyInterpMode(const ERichCurveInterpMode InInterpMode, const ERichCurveTangentMode InTangentMode) const
{
	const FKeyHandle StartKeyHandle = EaseCurve->GetStartKeyHandle();

	const FScopedTransaction Transaction(LOCTEXT("CurveEditor_SetInterpolationMode", "Select Interpolation Mode"));
	EaseCurve->ModifyOwner();

	EaseCurve->FloatCurve.SetKeyInterpMode(StartKeyHandle, InInterpMode);
	EaseCurve->FloatCurve.SetKeyTangentMode(StartKeyHandle, InTangentMode);

	if (InInterpMode != ERichCurveInterpMode::RCIM_Cubic)
	{
		FRichCurveKey& StartKey = EaseCurve->GetStartKey();
		StartKey.LeaveTangentWeight = 0.f;
		
		FRichCurveKey& EndKey = EaseCurve->GetEndKey();
		EndKey.ArriveTangentWeight = 0.f;
	}

	TArray<FRichCurveEditInfo> ChangedCurveEditInfos;
	ChangedCurveEditInfos.Add(FRichCurveEditInfo(&EaseCurve->FloatCurve));
	EaseCurve->OnCurveChanged(ChangedCurveEditInfos);
}

void FAvaEaseCurveTool::BeginTransaction(const FText& InDescription) const
{
	if (GEditor)
	{
		EaseCurve->ModifyOwnerChange();

		GEditor->BeginTransaction(InDescription);
	}
}

void FAvaEaseCurveTool::EndTransaction() const
{
	if (GEditor)
	{
		GEditor->EndTransaction();
	}
}

void FAvaEaseCurveTool::UndoAction()
{
	if (GEditor && GEditor->UndoTransaction())
	{
		UpdateEaseCurveFromSequencerKeySelections();
	}
}

void FAvaEaseCurveTool::RedoAction()
{
	if (GEditor && GEditor->RedoTransaction())
	{
		UpdateEaseCurveFromSequencerKeySelections();
	}
}

void FAvaEaseCurveTool::PostUndo(bool bInSuccess)
{
	UndoAction();
}

void FAvaEaseCurveTool::PostRedo(bool bInSuccess)
{
	RedoAction();
}

void FAvaEaseCurveTool::OpenToolSettings() const
{
	if (ISettingsModule* const SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>(TEXT("Settings")))
	{
		const UAvaEaseCurveToolSettings* const Settings = GetDefault<UAvaEaseCurveToolSettings>();
		check(IsValid(Settings));
		SettingsModule->ShowViewer(Settings->GetContainerName(), Settings->GetCategoryName(), Settings->GetSectionName());
	}
}

FFrameRate FAvaEaseCurveTool::GetTickResolution() const
{
	if (const TSharedPtr<FAvaSequencer> AvaSequencer = AvaSequencerWeak.Pin())
	{
		return AvaSequencer->GetSequencer()->GetFocusedTickResolution();
	}

	return FFrameRate();
}

FFrameRate FAvaEaseCurveTool::GetDisplayRate() const
{
	if (const TSharedPtr<FAvaSequencer> AvaSequencer = AvaSequencerWeak.Pin())
	{
		return AvaSequencer->GetSequencer()->GetFocusedDisplayRate();
	}

	// Fallback to using config display rate if tool is being used outside sequencer
	return GetDefault<UAvaSequencerSettings>()->GetDisplayRate();
}

void FAvaEaseCurveTool::ShowNotificationMessage(const FText& InMessageText)
{
	FNotificationInfo Info(InMessageText);
	Info.ExpireDuration = 3.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
}

bool FAvaEaseCurveTool::HasCachedKeysToEase()
{
	bool bEaseableKeys = false;

	KeyCache.ForEachEaseableKey(/*bInIncludeEqualValueKeys=*/false, [&bEaseableKeys](const FKeyHandle& InKeyHandle
		, const FKeyHandle& InNextKeyHandle, const FAvaEaseCurveKeySelection::FChannelData& InChannelData)
		{
			bEaseableKeys = true;
			return false;
		});

	return bEaseableKeys;
}

bool FAvaEaseCurveTool::AreAllEaseCurves()
{
	return KeyCache.AreAllEaseCurves();
}

#undef LOCTEXT_NAMESPACE
