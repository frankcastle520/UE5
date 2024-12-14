// Copyright Epic Games, Inc. All Rights Reserved.

#include "Toolkits/DMXControlConsoleEditorToolkit.h"

#include "Commands/DMXControlConsoleEditorCommands.h"
#include "DMXControlConsole.h"
#include "DMXControlConsoleData.h"
#include "DMXControlConsoleEditorData.h"
#include "DMXControlConsoleEditorModule.h"
#include "DMXControlConsoleEditorSelection.h"
#include "DMXControlConsoleEditorToolbar.h"
#include "DMXControlConsoleFaderBase.h"
#include "DMXControlConsoleFaderGroup.h"
#include "DMXEditorSettings.h"
#include "DMXEditorUtils.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Layouts/Controllers/DMXControlConsoleElementController.h"
#include "Layouts/Controllers/DMXControlConsoleFaderGroupController.h"
#include "Layouts/Controllers/DMXControlConsoleMatrixCellController.h"
#include "Layouts/DMXControlConsoleEditorGlobalLayoutBase.h"
#include "Layouts/DMXControlConsoleEditorLayouts.h"
#include "Models/DMXControlConsoleCompactEditorModel.h"
#include "Models/DMXControlConsoleCueStackModel.h"
#include "Models/DMXControlConsoleEditorModel.h"
#include "Models/DMXControlConsoleEditorPlayMenuModel.h"
#include "ScopedTransaction.h"
#include "Style/DMXControlConsoleEditorStyle.h"
#include "Views/SDMXControlConsoleEditorCueStackView.h"
#include "Views/SDMXControlConsoleEditorDetailsView.h"
#include "Views/SDMXControlConsoleEditorDMXLibraryView.h"
#include "Views/SDMXControlConsoleEditorFiltersView.h"
#include "Views/SDMXControlConsoleEditorLayoutView.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "ToolMenus.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleEditorToolkit"

namespace UE::DMX::Private
{
	const FName FDMXControlConsoleEditorToolkit::DMXLibraryViewTabID(TEXT("DMXControlConsoleEditorToolkit_DMXLibraryViewTabID"));
	const FName FDMXControlConsoleEditorToolkit::LayoutViewTabID(TEXT("DMXControlConsoleEditorToolkit_LayoutViewTabID"));
	const FName FDMXControlConsoleEditorToolkit::DetailsViewTabID(TEXT("DMXControlConsoleEditorToolkit_DetailsViewTabID"));
	const FName FDMXControlConsoleEditorToolkit::FiltersViewTabID(TEXT("DMXControlConsoleEditorToolkit_FiltersViewTabID"));
	const FName FDMXControlConsoleEditorToolkit::CueStackViewTabID(TEXT("DMXControlConsoleEditorToolkit_CueStackViewTabID"));

	FDMXControlConsoleEditorToolkit::FDMXControlConsoleEditorToolkit()
		: ControlConsole(nullptr)
		, AnalyticsProvider("ControlConsoleEditor")
	{}

	FDMXControlConsoleEditorToolkit::~FDMXControlConsoleEditorToolkit()
	{
		UDMXControlConsoleData* ControlConsoleData = ControlConsole ? ControlConsole->GetControlConsoleData() : nullptr;
		if (ControlConsoleData && ControlConsoleData->IsSendingDMX() && bStopSendingDMXOnDestruct)
		{
			ControlConsoleData->StopSendingDMX();
		}
	}

	void FDMXControlConsoleEditorToolkit::InitControlConsoleEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UDMXControlConsole* InControlConsole)
	{
		checkf(InControlConsole, TEXT("Invalid control console, can't initialize toolkit correctly."));

		ControlConsole = InControlConsole;

		EditorModel = NewObject<UDMXControlConsoleEditorModel>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);
		EditorModel->Initialize(ControlConsole);

		PlayMenuModel = NewObject<UDMXControlConsoleEditorPlayMenuModel>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);
		PlayMenuModel->Initialize(ControlConsole, GetToolkitCommands());

		CueStackModel = MakeShared<FDMXControlConsoleCueStackModel>(ControlConsole);

		UDMXEditorSettings* DMXEditorSettings = GetMutableDefault<UDMXEditorSettings>();
		if (DMXEditorSettings)
		{
			DMXEditorSettings->LastOpenedControlConsolePath = ControlConsole->GetPathName();
			DMXEditorSettings->SaveConfig();
		}

		InitializeInternal(Mode, InitToolkitHost, FGuid::NewGuid());
	}

	UDMXControlConsoleData* FDMXControlConsoleEditorToolkit::GetControlConsoleData() const
	{
		return ControlConsole ? ControlConsole->GetControlConsoleData() : nullptr;
	}

	UDMXControlConsoleEditorData* FDMXControlConsoleEditorToolkit::GetControlConsoleEditorData() const
	{
		return ControlConsole ? Cast<UDMXControlConsoleEditorData>(ControlConsole->ControlConsoleEditorData) : nullptr;
	}

	UDMXControlConsoleEditorLayouts* FDMXControlConsoleEditorToolkit::GetControlConsoleLayouts() const
	{
		return ControlConsole ? Cast<UDMXControlConsoleEditorLayouts>(ControlConsole->ControlConsoleEditorLayouts) : nullptr;
	}

	void FDMXControlConsoleEditorToolkit::RemoveAllSelectedElements()
	{
		const UDMXControlConsoleEditorLayouts* ControlConsoleLayouts = GetControlConsoleLayouts();
		if (!ControlConsoleLayouts || !EditorModel)
		{
			return;
		}

		UDMXControlConsoleEditorGlobalLayoutBase* ActiveLayout = ControlConsoleLayouts->GetActiveLayout();
		if (!ActiveLayout || ActiveLayout == &ControlConsoleLayouts->GetDefaultLayoutChecked())
		{
			return;
		}

		const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = EditorModel->GetSelectionHandler();
		const TArray<TWeakObjectPtr<UObject>> SelectedFaderGroupControllersObjects = SelectionHandler->GetSelectedFaderGroupControllers();
		if (SelectedFaderGroupControllersObjects.IsEmpty())
		{
			return;
		}

		const FScopedTransaction RemoveAllSelectedElementsTransaction(LOCTEXT("RemoveAllSelectedElementsTransaction", "Selected Elements removed"));

		// Delete all selected fader group controllers
		for (const TWeakObjectPtr<UObject>& SelectedFaderGroupControllerObject : SelectedFaderGroupControllersObjects)
		{
			UDMXControlConsoleFaderGroupController* SelectedFaderGroupController = Cast<UDMXControlConsoleFaderGroupController>(SelectedFaderGroupControllerObject);
			if (!SelectedFaderGroupController)
			{
				continue;
			}

			// Remove the controller only if there's no selected element controller or if all its element controllers are selected
			TArray<UDMXControlConsoleElementController*> SelectedElementControllersFromController = SelectionHandler->GetSelectedElementControllersFromFaderGroupController(SelectedFaderGroupController);
			TArray<UDMXControlConsoleElementController*> AllElementControllers = SelectedFaderGroupController->GetAllElementControllers();
			const auto RemoveMatrixCellControllersLambda = 
				[](UDMXControlConsoleElementController* ElementController)
				{
					return IsValid(Cast<UDMXControlConsoleMatrixCellController>(ElementController));
				};

			SelectedElementControllersFromController.RemoveAll(RemoveMatrixCellControllersLambda);
			AllElementControllers.RemoveAll(RemoveMatrixCellControllersLambda);

			const bool bRemoveController =
				SelectedElementControllersFromController.IsEmpty() ||
				SelectedElementControllersFromController.Num() == AllElementControllers.Num();
			
			if (!bRemoveController)
			{
				continue;
			}

			// If there's only one fader group controller to delete, replace it in selection
			if (SelectedFaderGroupControllersObjects.Num() == 1)
			{
				SelectionHandler->ReplaceInSelection(SelectedFaderGroupController);
			}

			constexpr bool bNotifySelectedFaderGroupControllerChange = false;
			SelectionHandler->RemoveFromSelection(SelectedFaderGroupController, bNotifySelectedFaderGroupControllerChange);

			// Destroy all unpatched fader groups in the controller
			const TArray<TWeakObjectPtr<UDMXControlConsoleFaderGroup>>& FaderGroups = SelectedFaderGroupController->GetFaderGroups();
			for (const TWeakObjectPtr<UDMXControlConsoleFaderGroup>& FaderGroup : FaderGroups)
			{
				if (FaderGroup.IsValid() && !FaderGroup->HasFixturePatch())
				{
					FaderGroup->Destroy();
				}
			}
				
			SelectedFaderGroupController->PreEditChange(nullptr);
			SelectedFaderGroupController->Destroy();
			SelectedFaderGroupController->PostEditChange();

			ActiveLayout->PreEditChange(nullptr);
			ActiveLayout->RemoveFromActiveFaderGroupControllers(SelectedFaderGroupController);
			ActiveLayout->PostEditChange();
		}

		// Delete all selected element controllers
		const TArray<TWeakObjectPtr<UObject>> SelectedElementControllers = SelectionHandler->GetSelectedElementControllers();
		if (!SelectedElementControllers.IsEmpty())
		{
			for (const TWeakObjectPtr<UObject>& SelectedElementControllerObject : SelectedElementControllers)
			{
				UDMXControlConsoleElementController* SelectedElementController = Cast<UDMXControlConsoleElementController>(SelectedElementControllerObject);
				if (!SelectedElementController || SelectedElementController->GetOwnerFaderGroupControllerChecked().HasFixturePatch())
				{
					continue;
				}

				// If there's only one element controller to delete, replace it in selection
				if (SelectedElementControllers.Num() == 1)
				{
					SelectionHandler->ReplaceInSelection(SelectedElementController);
				}

				constexpr bool bNotifyFaderSelectionChange = false;
				SelectionHandler->RemoveFromSelection(SelectedElementController, bNotifyFaderSelectionChange);

				// Destroy all elements in the selected element controller
				const TArray<TScriptInterface<IDMXControlConsoleFaderGroupElement>>& Elements = SelectedElementController->GetElements();
				for (const TScriptInterface<IDMXControlConsoleFaderGroupElement>& Element : Elements)
				{
					if (Element && !Element->GetOwnerFaderGroupChecked().HasFixturePatch())
					{
						Element->Destroy();
					}
				}

				SelectedElementController->PreEditChange(nullptr);
				SelectedElementController->Destroy();
				SelectedElementController->PostEditChange();
			}
		}

		SelectionHandler->RemoveInvalidObjectsFromSelection();
	}

	void FDMXControlConsoleEditorToolkit::ClearAll()
	{
		const UDMXControlConsoleEditorLayouts* ControlConsoleLayouts = GetControlConsoleLayouts();
		if (!ensureMsgf(ControlConsoleLayouts, TEXT("Invalid control console layouts, cannot clear the active layout correctly.")))
		{
			return;
		}

		if (!ensureMsgf(EditorModel, TEXT("Invalid control console editor model, cannot clear the active layout correctly.")))
		{
			return;
		}

		const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = EditorModel->GetSelectionHandler();
		SelectionHandler->ClearSelection();

		UDMXControlConsoleEditorGlobalLayoutBase* ActiveLayout = ControlConsoleLayouts->GetActiveLayout();
		if (!ActiveLayout)
		{
			return;
		}

		const FScopedTransaction ClearAllTransaction(LOCTEXT("ClearAllTransaction", "Clear All"));
		ActiveLayout->PreEditChange(nullptr);

		constexpr bool bClearPatchedControllers = true;
		bool bClearUnpatchedControllers = true;
		ActiveLayout->ClearAll(bClearPatchedControllers, bClearUnpatchedControllers);
		if (ActiveLayout == &ControlConsoleLayouts->GetDefaultLayoutChecked())
		{
			bClearUnpatchedControllers = false;
			const TArray<UDMXControlConsoleEditorGlobalLayoutBase*> UserLayouts = ControlConsoleLayouts->GetUserLayouts();
			for (UDMXControlConsoleEditorGlobalLayoutBase* UserLayout : UserLayouts)
			{
				UserLayout->PreEditChange(nullptr);
				UserLayout->ClearAll(bClearPatchedControllers, bClearUnpatchedControllers);
				UserLayout->PostEditChange();
			}

			if (UDMXControlConsoleData* ControlConsoleData = GetControlConsoleData())
			{
				ControlConsoleData->PreEditChange(nullptr);
				constexpr bool bClearPatchedFaderGroups = true;
				ControlConsoleData->Clear(bClearPatchedFaderGroups);
				ControlConsoleData->PostEditChange();
			}
		}
		ActiveLayout->PostEditChange();
	}

	void FDMXControlConsoleEditorToolkit::ResetToDefault()
	{
		const UDMXControlConsoleEditorLayouts* ControlConsoleLayouts = GetControlConsoleLayouts();
		UDMXControlConsoleEditorGlobalLayoutBase* ActiveLayout = ControlConsoleLayouts ? ControlConsoleLayouts->GetActiveLayout() : nullptr;
		if (!ensureMsgf(ActiveLayout, TEXT("Invalid layout, cannot reset to zero correctly.")))
		{
			return;
		}

		const FScopedTransaction ResetToDefaultTransaction(LOCTEXT("ResetToDefaultTransaction", "Reset to default"));
		const TArray<UDMXControlConsoleFaderGroupController*> FaderGroupControllers = ActiveLayout->GetAllFaderGroupControllers();
		for (const UDMXControlConsoleFaderGroupController* FaderGroupController : FaderGroupControllers)
		{
			if (!FaderGroupController)
			{
				continue;
			}

			const TArray<UDMXControlConsoleElementController*> ElementControllers = FaderGroupController->GetAllElementControllers();
			for (UDMXControlConsoleElementController* ElementController : ElementControllers)
			{
				if (!ElementController)
				{
					continue;
				}

				const TArray<UDMXControlConsoleFaderBase*> Faders = ElementController->GetFaders();
				for (UDMXControlConsoleFaderBase* Fader : Faders)
				{
					if (!Fader)
					{
						continue;
					}

					Fader->Modify();
				}

				ElementController->PreEditChange(UDMXControlConsoleElementController::StaticClass()->FindPropertyByName(UDMXControlConsoleElementController::GetValuePropertyName()));
				ElementController->ResetToDefault();
				ElementController->PostEditChange();
			}
		}

		if (TabManager.IsValid())
		{
			FSlateApplication::Get().SetUserFocus(0, TabManager->GetOwnerTab());
		}
	}

	void FDMXControlConsoleEditorToolkit::ResetToZero()
	{
		const UDMXControlConsoleEditorLayouts* ControlConsoleLayouts = GetControlConsoleLayouts();
		UDMXControlConsoleEditorGlobalLayoutBase* ActiveLayout = ControlConsoleLayouts ? ControlConsoleLayouts->GetActiveLayout() : nullptr;
		if (!ensureMsgf(ActiveLayout, TEXT("Invalid layout, cannot reset to zero correctly.")))
		{
			return;
		}

		const FScopedTransaction ResetToZeroTransaction(LOCTEXT("ResetToZeroTransaction", "Reset to zero"));
		const TArray<UDMXControlConsoleFaderGroupController*> FaderGroupControllers = ActiveLayout->GetAllFaderGroupControllers();
		for (const UDMXControlConsoleFaderGroupController* FaderGroupController : FaderGroupControllers)
		{
			if (!FaderGroupController)
			{
				continue;
			}

			const TArray<UDMXControlConsoleElementController*> ElementControllers = FaderGroupController->GetAllElementControllers();
			for (UDMXControlConsoleElementController* ElementController : ElementControllers)
			{
				if (!ElementController)
				{
					continue;
				}

				const TArray<UDMXControlConsoleFaderBase*> Faders = ElementController->GetFaders();
				for (UDMXControlConsoleFaderBase* Fader : Faders)
				{
					if (!Fader)
					{
						continue;
					}

					Fader->Modify();
				}

				ElementController->PreEditChange(UDMXControlConsoleElementController::StaticClass()->FindPropertyByName(UDMXControlConsoleElementController::GetValuePropertyName()));
				ElementController->SetValue(0.f);
				ElementController->PostEditChange();
			}
		}

		if (TabManager.IsValid())
		{
			FSlateApplication::Get().SetUserFocus(0, TabManager->GetOwnerTab());
		}
	}

	void FDMXControlConsoleEditorToolkit::Reload()
	{
		// Don't allow asset reload during PIE
		if (GIsPlayInEditorWorld)
		{
			FNotificationInfo Notification(LOCTEXT("CannotReloadAssetInPIE", "Assets cannot be reloaded while in PIE."));
			Notification.ExpireDuration = 3.0f;
			FSlateNotificationManager::Get().AddNotification(Notification);
			return;
		}

		if (ControlConsole)
		{
			const TArray<UPackage*> PackagesToReload({ ControlConsole->GetOutermost() });
			UPackageTools::ReloadPackages(PackagesToReload);
		}
	}

	void FDMXControlConsoleEditorToolkit::ShowCompactEditor()
	{
		const FDMXControlConsoleEditorModule& EditorModule = FModuleManager::GetModuleChecked<FDMXControlConsoleEditorModule>(TEXT("DMXControlConsoleEditor"));
		if (const TSharedPtr<SDockTab> CompactEditorTab = EditorModule.GetCompactEditorTab())
		{		
			// In the odd case that the compact editor window was docked to this editor, close it so it undocks, then reopen it.
			if (CompactEditorTab->GetTabManagerPtr() == GetTabManager())
			{
				CompactEditorTab->RequestCloseTab();
			}
		}
		
		if (ControlConsole)
		{
			bSwitchingToCompactEditor = true;
			 
			CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);

			UDMXControlConsoleCompactEditorModel* CompactEditorModel = GetMutableDefault<UDMXControlConsoleCompactEditorModel>();
			CompactEditorModel->SetControlConsole(ControlConsole);
		}
	}

	void FDMXControlConsoleEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
	{
		WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_ControlConsoleEditor", "DMX Control Console Editor"));
		TSharedRef<FWorkspaceItem> WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

		FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

		InTabManager->RegisterTabSpawner(DMXLibraryViewTabID, FOnSpawnTab::CreateSP(this, &FDMXControlConsoleEditorToolkit::SpawnTab_DMXLibraryView))
			.SetDisplayName(LOCTEXT("Tab_DMXLibraryView", "DMX Library"))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(FSlateIcon(FDMXControlConsoleEditorStyle::Get().GetStyleSetName(), "DMXControlConsole.DMXLibrary"));

		InTabManager->RegisterTabSpawner(LayoutViewTabID, FOnSpawnTab::CreateSP(this, &FDMXControlConsoleEditorToolkit::SpawnTab_LayoutView))
			.SetDisplayName(LOCTEXT("Tab_LayoutView", "Layout Editor"))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(FSlateIcon(FDMXControlConsoleEditorStyle::Get().GetStyleSetName(), "DMXControlConsole.TabIcon"));

		InTabManager->RegisterTabSpawner(DetailsViewTabID, FOnSpawnTab::CreateSP(this, &FDMXControlConsoleEditorToolkit::SpawnTab_DetailsView))
			.SetDisplayName(LOCTEXT("Tab_EditorView", "Details"))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Details"));

		InTabManager->RegisterTabSpawner(FiltersViewTabID, FOnSpawnTab::CreateSP(this, &FDMXControlConsoleEditorToolkit::SpawnTab_FiltersView))
			.SetDisplayName(LOCTEXT("Tab_FiltersView", "Filters"))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Filter"));

		InTabManager->RegisterTabSpawner(CueStackViewTabID, FOnSpawnTab::CreateSP(this, &FDMXControlConsoleEditorToolkit::SpawnTab_CueStackView))
			.SetDisplayName(LOCTEXT("Tab_CueStackView", "Cue Stack"))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(FSlateIcon(FDMXControlConsoleEditorStyle::Get().GetStyleSetName(), "DMXControlConsole.CueStack"));
	}

	void FDMXControlConsoleEditorToolkit::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
	{
		FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

		InTabManager->UnregisterTabSpawner(DMXLibraryViewTabID);
		InTabManager->UnregisterTabSpawner(LayoutViewTabID);
		InTabManager->UnregisterTabSpawner(DetailsViewTabID);
		InTabManager->UnregisterTabSpawner(FiltersViewTabID);
		InTabManager->UnregisterTabSpawner(CueStackViewTabID);
	}

	const FSlateBrush* FDMXControlConsoleEditorToolkit::GetDefaultTabIcon() const
	{
		return FDMXControlConsoleEditorStyle::Get().GetBrush("DMXControlConsole.TabIcon");
	}

	FText FDMXControlConsoleEditorToolkit::GetBaseToolkitName() const
	{
		return LOCTEXT("AppLabel", "DMX Control Console");
	}

	FName FDMXControlConsoleEditorToolkit::GetToolkitFName() const
	{
		return FName("DMXControlConsole");
	}

	FString FDMXControlConsoleEditorToolkit::GetWorldCentricTabPrefix() const
	{
		return LOCTEXT("WorldCentricTabPrefix", "DMX Control Console ").ToString();
	}

	void FDMXControlConsoleEditorToolkit::AddReferencedObjects(FReferenceCollector& Collector)
	{
		Collector.AddReferencedObject(EditorModel);
		Collector.AddReferencedObject(ControlConsole);
		Collector.AddReferencedObject(PlayMenuModel);
	}

	FString FDMXControlConsoleEditorToolkit::GetReferencerName() const
	{
		return TEXT("FDMXControlConsoleEditorToolkit");
	}

	void FDMXControlConsoleEditorToolkit::InitializeInternal(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, const FGuid& MessageLogGuid)
	{
		const UDMXControlConsoleData* ControlConsoleData = ControlConsole ? ControlConsole->GetControlConsoleData() : nullptr;
		if (!ControlConsole || !ControlConsoleData)
		{
			return;
		}

		bStopSendingDMXOnDestruct = !ControlConsoleData->IsSendingDMX();

		ExtendToolbar();
		GenerateInternalViews();

		TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_ControlConsole_Layout_2.5")
			->AddArea
			(
				FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
				->Split
				(
					FTabManager::NewSplitter()
					->SetOrientation(Orient_Horizontal)
					->Split
					(
						FTabManager::NewStack()
						->AddTab(DMXLibraryViewTabID, ETabState::OpenedTab)
						->SetSizeCoefficient(.2f)
					)
					->Split
					(
						FTabManager::NewStack()
						->AddTab(LayoutViewTabID, ETabState::OpenedTab)
						->SetSizeCoefficient(.6f)
					)
					->Split
					(
						FTabManager::NewStack()
						->AddTab(DetailsViewTabID, ETabState::SidebarTab, ESidebarLocation::Right, .2f)
						->SetSizeCoefficient(.2f)
					)
					->Split
					(
						FTabManager::NewStack()
						->AddTab(CueStackViewTabID, ETabState::SidebarTab, ESidebarLocation::Right, .2f)
						->SetSizeCoefficient(.2f)
					)
					->Split
					(
						FTabManager::NewStack()
						->AddTab(FiltersViewTabID, ETabState::SidebarTab, ESidebarLocation::Right, .1f)
						->SetSizeCoefficient(.1f)
					)
				)
			);

		const bool bCreateDefaultStandaloneMenu = true;
		const bool bCreateDefaultToolbar = true;
		FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, FDMXControlConsoleEditorModule::ControlConsoleEditorAppIdentifier,
			StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, ControlConsole);

		SetupCommands();
		RegenerateMenusAndToolbars();
	}

	void FDMXControlConsoleEditorToolkit::GenerateInternalViews()
	{
		GenerateDMXLibraryView();
		GenerateLayoutView();
		GenerateDetailsView();
		GenerateFiltersView();
		GenerateCueStackView();
	}

	TSharedRef<SDMXControlConsoleEditorDMXLibraryView> FDMXControlConsoleEditorToolkit::GenerateDMXLibraryView()
	{
		if (!DMXLibraryView.IsValid())
		{
			DMXLibraryView = SNew(SDMXControlConsoleEditorDMXLibraryView, EditorModel);
		}

		return DMXLibraryView.ToSharedRef();
	}

	TSharedRef<SDMXControlConsoleEditorLayoutView> FDMXControlConsoleEditorToolkit::GenerateLayoutView()
	{
		if (!LayoutView.IsValid())
		{
			LayoutView = SNew(SDMXControlConsoleEditorLayoutView, EditorModel);
		}

		return LayoutView.ToSharedRef();
	}

	TSharedRef<SDMXControlConsoleEditorDetailsView> FDMXControlConsoleEditorToolkit::GenerateDetailsView()
	{
		if (!DetailsView.IsValid())
		{
			DetailsView = SNew(SDMXControlConsoleEditorDetailsView, EditorModel);
		}

		return DetailsView.ToSharedRef();
	}

	TSharedRef<SDMXControlConsoleEditorFiltersView> FDMXControlConsoleEditorToolkit::GenerateFiltersView()
	{
		if (!FiltersView.IsValid())
		{
			FiltersView = SNew(SDMXControlConsoleEditorFiltersView, Toolbar, EditorModel);
		}

		return FiltersView.ToSharedRef();
	}

	TSharedRef<SDMXControlConsoleEditorCueStackView> FDMXControlConsoleEditorToolkit::GenerateCueStackView()
	{
		if (!CueStackView.IsValid() && CueStackModel.IsValid())
		{
			CueStackView = SNew(SDMXControlConsoleEditorCueStackView, CueStackModel);
		}

		return CueStackView.ToSharedRef();
	}

	TSharedRef<SDockTab> FDMXControlConsoleEditorToolkit::SpawnTab_DMXLibraryView(const FSpawnTabArgs& Args)
	{
		check(Args.GetTabId() == DMXLibraryViewTabID);

		const TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
			.Label(LOCTEXT("DMXLibraryViewTabID", "DMX Library"))
			[
				DMXLibraryView.ToSharedRef()
			];

		return SpawnedTab;
	}

	TSharedRef<SDockTab> FDMXControlConsoleEditorToolkit::SpawnTab_LayoutView(const FSpawnTabArgs& Args)
	{
		check(Args.GetTabId() == LayoutViewTabID);

		const TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
			.Label(LOCTEXT("LayoutViewTabID", "Layout Editor"))
			[
				LayoutView.ToSharedRef()
			];

		return SpawnedTab;
	}

	TSharedRef<SDockTab> FDMXControlConsoleEditorToolkit::SpawnTab_DetailsView(const FSpawnTabArgs& Args)
	{
		check(Args.GetTabId() == DetailsViewTabID);

		const TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
			.Label(LOCTEXT("DetailsViewTabID", "Details"))
			[
				DetailsView.ToSharedRef()
			];

		return SpawnedTab;
	}

	TSharedRef<SDockTab> FDMXControlConsoleEditorToolkit::SpawnTab_FiltersView(const FSpawnTabArgs& Args)
	{
		check(Args.GetTabId() == FiltersViewTabID);

		const TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
			.Label(LOCTEXT("FiltersViewTabID", "Filters"))
			[
				FiltersView.ToSharedRef()
			];

		return SpawnedTab;
	}

	TSharedRef<SDockTab> FDMXControlConsoleEditorToolkit::SpawnTab_CueStackView(const FSpawnTabArgs& Args)
	{
		check(Args.GetTabId() == CueStackViewTabID);

		const TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
			.Label(LOCTEXT("CueStackViewTabID", "Cue Stack"))
			[
				CueStackView.ToSharedRef()
			];

		return SpawnedTab;
	}

	void FDMXControlConsoleEditorToolkit::SetupCommands()
	{
		GetToolkitCommands()->MapAction
		(
			FDMXControlConsoleEditorCommands::Get().RemoveElements,
			FExecuteAction::CreateSP(this, &FDMXControlConsoleEditorToolkit::RemoveAllSelectedElements)
		);

		GetToolkitCommands()->MapAction
		(
			FDMXControlConsoleEditorCommands::Get().ClearAll,
			FExecuteAction::CreateSP(this, &FDMXControlConsoleEditorToolkit::ClearAll)
		);

		GetToolkitCommands()->MapAction
		(
			FDMXControlConsoleEditorCommands::Get().ResetToDefault,
			FExecuteAction::CreateSP(this, &FDMXControlConsoleEditorToolkit::ResetToDefault)
		);

		GetToolkitCommands()->MapAction
		(
			FDMXControlConsoleEditorCommands::Get().ResetToZero,
			FExecuteAction::CreateSP(this, &FDMXControlConsoleEditorToolkit::ResetToZero)
		);

		GetToolkitCommands()->MapAction
		(
			FDMXControlConsoleEditorCommands::Get().Reload,
			FExecuteAction::CreateSP(this, &FDMXControlConsoleEditorToolkit::Reload)
		);

		if (EditorModel)
		{
			const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = EditorModel->GetSelectionHandler();
			constexpr bool bSelectOnlyVisible = true;
			GetToolkitCommands()->MapAction
			(
				FDMXControlConsoleEditorCommands::Get().SelectAll,
				FExecuteAction::CreateSP(SelectionHandler, &FDMXControlConsoleEditorSelection::SelectAll, bSelectOnlyVisible)
			);
		}
	}

	void FDMXControlConsoleEditorToolkit::ExtendToolbar()
	{
		UToolMenu* ToolMenu = UToolMenus::Get()->ExtendMenu(GetToolMenuToolbarName());
		if (ensureMsgf(ToolMenu && PlayMenuModel, TEXT("Cannot find tool menu or play menu model for control console toolkit. Cannot build play menu.")))
		{
			PlayMenuModel->CreatePlayMenu(*ToolMenu);
		}

		Toolbar = MakeShared<FDMXControlConsoleEditorToolbar>(SharedThis(this));

		const TSharedRef<FExtender> ToolbarExtender = MakeShareable(new FExtender);
		Toolbar->BuildToolbar(ToolbarExtender);
		AddToolbarExtender(ToolbarExtender);
	}
}

#undef LOCTEXT_NAMESPACE
