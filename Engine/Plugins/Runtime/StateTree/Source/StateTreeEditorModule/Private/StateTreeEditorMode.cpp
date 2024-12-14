// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEditorMode.h"

#include "ContextObjectStore.h"
#include "EditorModeManager.h"
#include "FileHelpers.h"
#include "HAL/IConsoleManager.h"

#include "StateTreeEditorModeToolkit.h"
#include "StateTreeEditorSettings.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeDelegates.h"
#include "EdModeInteractiveToolsContext.h"
#include "IMessageLogListing.h"
#include "InteractiveToolManager.h"
#include "PropertyPath.h"
#include "StateTreeEditorCommands.h"
#include "Misc/UObjectToken.h"
#include "Toolkits/ToolkitManager.h"
#include "Modules/ModuleManager.h"

#include "IStateTreeEditorHost.h"
#include "MessageLogModule.h"
#include "StateTreeEditingSubsystem.h"
#include "Customizations/StateTreeBindingExtension.h"

#define LOCTEXT_NAMESPACE "UStateTreeEditorMode"

class FUObjectToken;
const FEditorModeID UStateTreeEditorMode::EM_StateTree("StateTreeEditorMode");

UStateTreeEditorMode::UStateTreeEditorMode()
{
	Info = FEditorModeInfo(UStateTreeEditorMode::EM_StateTree,
		LOCTEXT("StateTreeEditorModeName", "StateTreeEditorMode"),
		FSlateIcon(),
		false);
}

void UStateTreeEditorMode::Enter()
{
	Super::Enter();
	
	DetailsViewExtensionHandler = MakeShared<FStateTreeBindingExtension>();
	DetailsViewChildrenCustomizationHandler = MakeShared<FStateTreeBindingsChildrenCustomization>();
	
	if (const UContextObjectStore* ContextObjectStore = GetToolManager()->GetContextObjectStore())
	{
		if (const UStateTreeEditorContext* Context = ContextObjectStore->FindContext<UStateTreeEditorContext>())
		{
			TSharedRef<IStateTreeEditorHost> Host = Context->EditorHostInterface.ToSharedRef();
			Host->OnStateTreeChanged().AddUObject(this, &UStateTreeEditorMode::OnStateTreeChanged);

			if (TSharedPtr<IMessageLogListing> MessageLogListing = GetMessageLogListing())
			{
				MessageLogListing->OnMessageTokenClicked().AddUObject(this, &UStateTreeEditorMode::HandleMessageTokenClicked);
			}

			if (TSharedPtr<IDetailsView> DetailsView = GetDetailsView())
			{
				DetailsView->OnFinishedChangingProperties().AddUObject(this, &UStateTreeEditorMode::OnSelectionFinishedChangingProperties);

				DetailsView->SetExtensionHandler(DetailsViewExtensionHandler);
				DetailsView->SetChildrenCustomizationHandler(DetailsViewChildrenCustomizationHandler);
			}
			
			if (TSharedPtr<IDetailsView> AssetDetailsView = GetAssetDetailsView())
			{
				AssetDetailsView->OnFinishedChangingProperties().AddUObject(this, &UStateTreeEditorMode::OnAssetFinishedChangingProperties);
				
				AssetDetailsView->SetExtensionHandler(DetailsViewExtensionHandler);
				AssetDetailsView->SetChildrenCustomizationHandler(DetailsViewChildrenCustomizationHandler);
				bForceAssetDetailViewToRefresh = true;
			}
		}
	}

	UE::StateTree::Delegates::OnIdentifierChanged.AddUObject(this, &UStateTreeEditorMode::OnIdentifierChanged);
	UE::StateTree::Delegates::OnSchemaChanged.AddUObject(this, &UStateTreeEditorMode::OnSchemaChanged);
	UE::StateTree::Delegates::OnParametersChanged.AddUObject(this, &UStateTreeEditorMode::OnRefreshDetailsView);
	UE::StateTree::Delegates::OnGlobalDataChanged.AddUObject(this, &UStateTreeEditorMode::OnRefreshDetailsView);
	UE::StateTree::Delegates::OnStateParametersChanged.AddUObject(this, &UStateTreeEditorMode::OnStateParametersChanged);

	OnStateTreeChanged();
}


void UStateTreeEditorMode::OnIdentifierChanged(const UStateTree& InStateTree)
{
	if (GetStateTree() == &InStateTree)
	{
		UpdateAsset();
	}
}

void UStateTreeEditorMode::OnSchemaChanged(const UStateTree& InStateTree)
{
	if (GetStateTree() == &InStateTree)
	{
		UpdateAsset();

		if(UStateTreeEditingSubsystem* StateTreeEditingSubsystem = GEditor->GetEditorSubsystem<UStateTreeEditingSubsystem>())
		{
			TSharedRef<FStateTreeViewModel> ViewModel = StateTreeEditingSubsystem->FindOrAddViewModel(GetStateTree());		
			ViewModel->NotifyAssetChangedExternally();
		}
		
		ForceRefreshDetailsView();
	}
}

void UStateTreeEditorMode::ForceRefreshDetailsView() const
{ 					
	if (TSharedPtr<IDetailsView> DetailsView = GetDetailsView())
	{
		DetailsView->ForceRefresh();
	}
}

void UStateTreeEditorMode::OnRefreshDetailsView(const UStateTree& InStateTree) const
{
	if (GetStateTree() == &InStateTree)
	{
		// Accessible structs might be different after modifying parameters so forcing refresh
		// so the FStateTreeBindingExtension can rebuild the list of bindable structs
		ForceRefreshDetailsView();
	}
}

void UStateTreeEditorMode::OnStateParametersChanged(const UStateTree& InStateTree, const FGuid ChangedStateID) const
{
	UStateTree* StateTree = GetStateTree(); 
	if (StateTree == &InStateTree)
	{
		if (const UStateTreeEditorData* TreeData = Cast<UStateTreeEditorData>(StateTree->EditorData))
		{
			TreeData->VisitHierarchy([&ChangedStateID](UStateTreeState& State, UStateTreeState* /*ParentState*/)
			{
				if (State.Type == EStateTreeStateType::Linked && State.LinkedSubtree.ID == ChangedStateID)
				{
					State.UpdateParametersFromLinkedSubtree();
				}
				return EStateTreeVisitor::Continue;
			});
		}

		// Accessible structs might be different after modifying parameters so forcing refresh
		// so the FStateTreeBindingExtension can rebuild the list of bindable structs
		ForceRefreshDetailsView();
	}
}

void UStateTreeEditorMode::HandleMessageTokenClicked(const TSharedRef<IMessageToken>& InMessageToken) const
{
	if (InMessageToken->GetType() == EMessageToken::Object)
	{
		const TSharedRef<FUObjectToken> ObjectToken = StaticCastSharedRef<FUObjectToken>(InMessageToken);

		if (UStateTreeState* State = Cast<UStateTreeState>(ObjectToken->GetObject().Get()))
		{
			if(UStateTreeEditingSubsystem* StateTreeEditingSubsystem = GEditor->GetEditorSubsystem<UStateTreeEditingSubsystem>())
			{
				StateTreeEditingSubsystem->FindOrAddViewModel(GetStateTree())->SetSelection(State);
			}
			
		}
	}
}

void UStateTreeEditorMode::Exit()
{
	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}
	
	if (UContextObjectStore* ContextObjectStore = GetToolManager()->GetContextObjectStore())
	{
		if (const UStateTreeEditorContext* Context = ContextObjectStore->FindContext<UStateTreeEditorContext>())
		{
			Context->EditorHostInterface->OnStateTreeChanged().RemoveAll(this);
			
			if (TSharedPtr<IMessageLogListing> MessageLogListing = GetMessageLogListing())
			{
				MessageLogListing->OnMessageTokenClicked().RemoveAll(this);
			}
			
			if (TSharedPtr<IDetailsView> DetailsView = GetDetailsView())
			{
				DetailsView->OnFinishedChangingProperties().RemoveAll(this);
				DetailsView->SetExtensionHandler(nullptr);
				DetailsView->SetChildrenCustomizationHandler(nullptr);
			}
			
			if (TSharedPtr<IDetailsView> AssetDetailsView = GetAssetDetailsView())
			{
				AssetDetailsView->OnFinishedChangingProperties().RemoveAll(this);
				AssetDetailsView->SetExtensionHandler(nullptr);
				AssetDetailsView->SetChildrenCustomizationHandler(nullptr);
				bForceAssetDetailViewToRefresh = true;
			}
		}
	}

	if (CachedStateTree.IsValid())
	{
		if (UStateTreeEditingSubsystem* StateTreeEditingSubsystem = GEditor->GetEditorSubsystem<UStateTreeEditingSubsystem>())
		{		
			TSharedRef<FStateTreeViewModel> ViewModel = StateTreeEditingSubsystem->FindOrAddViewModel(CachedStateTree.Get());
			{
				ViewModel->GetOnAssetChanged().RemoveAll(this);
				ViewModel->GetOnStateAdded().RemoveAll(this);
				ViewModel->GetOnStatesRemoved().RemoveAll(this);
				ViewModel->GetOnStatesMoved().RemoveAll(this);
				ViewModel->GetOnSelectionChanged().RemoveAll(this);
				ViewModel->GetOnBringNodeToFocus().RemoveAll(this);
			}
		}
	}

	UE::StateTree::Delegates::OnIdentifierChanged.RemoveAll(this);
	UE::StateTree::Delegates::OnSchemaChanged.RemoveAll(this);
	UE::StateTree::Delegates::OnParametersChanged.RemoveAll(this);
	UE::StateTree::Delegates::OnGlobalDataChanged.RemoveAll(this);
	UE::StateTree::Delegates::OnStateParametersChanged.RemoveAll(this);
	
	Super::Exit();
}

void UStateTreeEditorMode::CreateToolkit()
{
	Toolkit = MakeShareable(new FStateTreeEditorModeToolkit(this));
}

void UStateTreeEditorMode::OnStateTreeChanged()
{
	UContextObjectStore* ContextStore = GetInteractiveToolsContext()->ToolManager->GetContextObjectStore();
	if (const UStateTreeEditorContext* Context = ContextStore->FindContext<UStateTreeEditorContext>())
	{
		if (UStateTreeEditingSubsystem* StateTreeEditingSubsystem = GEditor->GetEditorSubsystem<UStateTreeEditingSubsystem>())
		{
			if (CachedStateTree.IsValid())
			{
				TSharedRef<FStateTreeViewModel> OldViewModel = StateTreeEditingSubsystem->FindOrAddViewModel(CachedStateTree.Get());
				{
					OldViewModel->GetOnAssetChanged().RemoveAll(this);
					OldViewModel->GetOnStateAdded().RemoveAll(this);
					OldViewModel->GetOnStatesRemoved().RemoveAll(this);
					OldViewModel->GetOnStatesMoved().RemoveAll(this);
					OldViewModel->GetOnSelectionChanged().RemoveAll(this);
					OldViewModel->GetOnBringNodeToFocus().RemoveAll(this);
				}
			}
		}

		UStateTree* StateTree = Context->EditorHostInterface->GetStateTree();
		CachedStateTree = StateTree;
		UpdateAsset();

		if (TSharedPtr<IDetailsView> AssetDetailsView = GetAssetDetailsView())
		{
			AssetDetailsView->SetObject(StateTree ? StateTree->EditorData : nullptr, bForceAssetDetailViewToRefresh);
			bForceAssetDetailViewToRefresh = false;
		}

		if (StateTree)
		{
			if (UStateTreeEditingSubsystem* StateTreeEditingSubsystem = GEditor->GetEditorSubsystem<UStateTreeEditingSubsystem>())
			{				
				TSharedRef<FStateTreeViewModel> NewViewModel = StateTreeEditingSubsystem->FindOrAddViewModel(StateTree);
				{
					NewViewModel->GetOnAssetChanged().AddUObject(this, &UStateTreeEditorMode::HandleModelAssetChanged);
					NewViewModel->GetOnStateAdded().AddUObject(this, &UStateTreeEditorMode::HandleStateAdded);
					NewViewModel->GetOnStatesRemoved().AddUObject(this, &UStateTreeEditorMode::HandleStatesRemoved);
					NewViewModel->GetOnStatesMoved().AddUObject(this, &UStateTreeEditorMode::HandleOnStatesMoved);
					NewViewModel->GetOnSelectionChanged().AddUObject(this, &UStateTreeEditorMode::HandleModelSelectionChanged);
					NewViewModel->GetOnBringNodeToFocus().AddUObject(this, &UStateTreeEditorMode::HandleModelBringNodeToFocus);
				}
			}
		}
	}

	if (Toolkit)
	{
		StaticCastSharedPtr<FStateTreeEditorModeToolkit>(Toolkit)->OnStateTreeChanged();
	}
}


namespace UE::StateTree::Editor::Internal
{
static void SetSaveOnCompileSetting(const EStateTreeSaveOnCompile NewSetting)
{
	UStateTreeEditorSettings* Settings = GetMutableDefault<UStateTreeEditorSettings>();
	Settings->SaveOnCompile = NewSetting;
	Settings->SaveConfig();
}

static bool IsSaveOnCompileOptionSet(const EStateTreeSaveOnCompile Option)
{
	const UStateTreeEditorSettings* Settings = GetDefault<UStateTreeEditorSettings>();
	return (Settings->SaveOnCompile == Option);
}

static IConsoleVariable* GetLogCompilationResultCVar()
{
	static IConsoleVariable* FoundVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("StateTree.Compiler.LogResultOnCompilationCompleted"));
	return FoundVariable;
}

static void ToggleLogCompilationResult()
{
	IConsoleVariable* LogResultCVar = GetLogCompilationResultCVar();
	if (ensure(LogResultCVar))
	{
		LogResultCVar->Set(!LogResultCVar->GetBool(), ECVF_SetByConsole);
	}
}

static bool IsLogCompilationResult()
{
	IConsoleVariable* LogResultCVar = GetLogCompilationResultCVar();
	return LogResultCVar ? LogResultCVar->GetBool() : false;
}
}

void UStateTreeEditorMode::BindToolkitCommands(const TSharedRef<FUICommandList>& ToolkitCommands)
{
	FStateTreeEditorCommands::Register();
	const FStateTreeEditorCommands& Commands = FStateTreeEditorCommands::Get();

	ToolkitCommands->MapAction(
		Commands.Compile,
		FExecuteAction::CreateUObject(this, &UStateTreeEditorMode::Compile),
		FCanExecuteAction::CreateUObject(this, &UStateTreeEditorMode::CanCompile),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateUObject(this, &UStateTreeEditorMode::HasValidStateTree));

	ToolkitCommands->MapAction(
		FStateTreeEditorCommands::Get().SaveOnCompile_Never,
		FExecuteAction::CreateStatic(&UE::StateTree::Editor::Internal::SetSaveOnCompileSetting, EStateTreeSaveOnCompile::Never),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&UE::StateTree::Editor::Internal::IsSaveOnCompileOptionSet, EStateTreeSaveOnCompile::Never),
		FIsActionButtonVisible::CreateUObject(this, &UStateTreeEditorMode::HasValidStateTree)
	);
	ToolkitCommands->MapAction(
		FStateTreeEditorCommands::Get().SaveOnCompile_SuccessOnly,
		FExecuteAction::CreateStatic(&UE::StateTree::Editor::Internal::SetSaveOnCompileSetting, EStateTreeSaveOnCompile::SuccessOnly),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&UE::StateTree::Editor::Internal::IsSaveOnCompileOptionSet,  EStateTreeSaveOnCompile::SuccessOnly),
		FIsActionButtonVisible::CreateUObject(this, &UStateTreeEditorMode::HasValidStateTree)
	);
	ToolkitCommands->MapAction(
		FStateTreeEditorCommands::Get().SaveOnCompile_Always,
		FExecuteAction::CreateStatic(&UE::StateTree::Editor::Internal::SetSaveOnCompileSetting, EStateTreeSaveOnCompile::Always),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&UE::StateTree::Editor::Internal::IsSaveOnCompileOptionSet,  EStateTreeSaveOnCompile::Always),
		FIsActionButtonVisible::CreateUObject(this, &UStateTreeEditorMode::HasValidStateTree)
	);
	ToolkitCommands->MapAction(
		FStateTreeEditorCommands::Get().LogCompilationResult,
		FExecuteAction::CreateStatic(&UE::StateTree::Editor::Internal::ToggleLogCompilationResult),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&UE::StateTree::Editor::Internal::IsLogCompilationResult)
	);
}

void UStateTreeEditorMode::BindCommands()
{
	UEdMode::BindCommands();
	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();
	BindToolkitCommands(CommandList);
}

void UStateTreeEditorMode::Compile()
{
	UStateTree* StateTree = GetStateTree();

	if (!StateTree)
	{
		return;
	}

	UpdateAsset();

	if (TSharedPtr<IMessageLogListing> Listing = GetMessageLogListing())
	{
		Listing->ClearMessages();
	}
	
	FStateTreeCompilerLog Log;
	bLastCompileSucceeded = UStateTreeEditingSubsystem::CompileStateTree(StateTree, Log);

	if (TSharedPtr<IMessageLogListing> Listing = GetMessageLogListing())
	{					
		Log.AppendToLog(Listing.Get());

		if (!bLastCompileSucceeded)
		{
			// Show log
			ShowCompilerTab();
		}
	}
	

	const UStateTreeEditorSettings* Settings = GetMutableDefault<UStateTreeEditorSettings>();
	const bool bShouldSaveOnCompile = ((Settings->SaveOnCompile == EStateTreeSaveOnCompile::Always)
									|| ((Settings->SaveOnCompile == EStateTreeSaveOnCompile::SuccessOnly) && bLastCompileSucceeded));

	if (bShouldSaveOnCompile)
	{
		const TArray<UPackage*> PackagesToSave { StateTree->GetOutermost() };
		FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, /*bCheckDirty =*/true, /*bPromptToSave =*/false);
	}
}

bool UStateTreeEditorMode::CanCompile() const
{
	if (GetStateTree() == nullptr)
	{
		return false;
	}

	// We can't recompile while in PIE
	if (GEditor->IsPlaySessionInProgress())
	{
		return false;
	}

	return true;
}

bool UStateTreeEditorMode::HasValidStateTree() const
{
	return GetStateTree() != nullptr;
}

void UStateTreeEditorMode::HandleModelAssetChanged()
{
	UpdateAsset();
}

void UStateTreeEditorMode::HandleModelSelectionChanged(const TArray<TWeakObjectPtr<UStateTreeState>>& SelectedStates) const
{
	if (TSharedPtr<IDetailsView> DetailsView = GetDetailsView())
	{
		TArray<UObject*> Selected;
		for (const TWeakObjectPtr<UStateTreeState>& WeakState : SelectedStates)
		{
			if (UStateTreeState* State = WeakState.Get())
			{
				Selected.Add(State);
			}
		}
		DetailsView->SetObjects(Selected);
	}
}

void UStateTreeEditorMode::HandleModelBringNodeToFocus(const UStateTreeState* State, const FGuid NodeID) const
{
	TSharedPtr<IDetailsView> DetailsView = GetDetailsView();
	if (DetailsView && State)
	{
		FPropertyPath HighlightPath;
	
		if (!HighlightPath.IsValid())
		{
			FArrayProperty* TasksProperty = CastFieldChecked<FArrayProperty>(UStateTreeState::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UStateTreeState, Tasks)));
			const int32 TaskIndex = State->Tasks.IndexOfByPredicate([&NodeID](const FStateTreeEditorNode& Node)
			{
				return Node.ID == NodeID;
			});
			if (TaskIndex != INDEX_NONE)
			{
				HighlightPath.AddProperty(FPropertyInfo(TasksProperty));
				HighlightPath.AddProperty(FPropertyInfo(TasksProperty->Inner, TaskIndex));
			}
		}
	
		if (!HighlightPath.IsValid())
		{
			FProperty* SingleTaskProperty = CastFieldChecked<FProperty>(UStateTreeState::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UStateTreeState, SingleTask)));
			if (State->SingleTask.ID == NodeID)
			{
				HighlightPath.AddProperty(FPropertyInfo(SingleTaskProperty));
			}
		}
	
		if (!HighlightPath.IsValid())
		{
			FArrayProperty* TransitionsProperty = CastFieldChecked<FArrayProperty>(UStateTreeState::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UStateTreeState, Transitions)));
			const int32 TransitionIndex = State->Transitions.IndexOfByPredicate([&NodeID](const FStateTreeTransition& Transition)
			{
				return Transition.ID == NodeID;
			});
			if (TransitionIndex != INDEX_NONE)
			{
				HighlightPath.AddProperty(FPropertyInfo(TransitionsProperty));
				HighlightPath.AddProperty(FPropertyInfo(TransitionsProperty->Inner, TransitionIndex));
			}
		}
	
		if (!HighlightPath.IsValid())
		{
			FArrayProperty* EnterConditionsProperty = CastFieldChecked<FArrayProperty>(UStateTreeState::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UStateTreeState, EnterConditions)));
			const int32 EnterConditionIndex = State->EnterConditions.IndexOfByPredicate([&NodeID](const FStateTreeEditorNode& Node)
			{
				return Node.ID == NodeID;
			});
			if (EnterConditionIndex != INDEX_NONE)
			{
				HighlightPath.AddProperty(FPropertyInfo(EnterConditionsProperty));
				HighlightPath.AddProperty(FPropertyInfo(EnterConditionsProperty->Inner, EnterConditionIndex));
			}
		}
	
		if (HighlightPath.IsValid())
		{
			DetailsView->ScrollPropertyIntoView(HighlightPath, /*bExpandProperty*/true);
			DetailsView->HighlightProperty(HighlightPath);

			FTimerHandle HighlightTimerHandle;
			GEditor->GetTimerManager()->SetTimer(
				HighlightTimerHandle,
				FTimerDelegate::CreateLambda([SelectionDetailsView = DetailsView]()
				{
					SelectionDetailsView->HighlightProperty({});
				}),
				1.0f,
				/*Loop*/false);
		}
	}
}

void UStateTreeEditorMode::UpdateAsset()
{
	UStateTree* StateTree = GetStateTree();
	if (!StateTree)
	{
		return;
	}

	UStateTreeEditingSubsystem::ValidateStateTree(StateTree);
	EditorDataHash = UStateTreeEditingSubsystem::CalculateStateTreeHash(StateTree);
}

TSharedPtr<IDetailsView> UStateTreeEditorMode::GetDetailsView() const
{
	if (const UContextObjectStore* ContextObjectStore = GetToolManager()->GetContextObjectStore())
	{
		if (const UStateTreeEditorContext* Context = ContextObjectStore->FindContext<UStateTreeEditorContext>())
		{
			return Context->EditorHostInterface->GetDetailsView();
		}
	}

	return nullptr;
}

TSharedPtr<IDetailsView> UStateTreeEditorMode::GetAssetDetailsView() const
{
	if (UContextObjectStore* ContextObjectStore = GetToolManager()->GetContextObjectStore())
	{
		if (const UStateTreeEditorContext* Context = ContextObjectStore->FindContext<UStateTreeEditorContext>())
		{
			return Context->EditorHostInterface->GetAssetDetailsView();
		}
	}

	return nullptr;
}

TSharedPtr<IMessageLogListing> UStateTreeEditorMode::GetMessageLogListing() const
{
	if (UContextObjectStore* ContextObjectStore = GetToolManager()->GetContextObjectStore())
	{
		if (const UStateTreeEditorContext* Context = ContextObjectStore->FindContext<UStateTreeEditorContext>())
		{
			FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
			return MessageLogModule.GetLogListing(Context->EditorHostInterface->GetCompilerLogName());
		}
	}

	return nullptr;
}

void UStateTreeEditorMode::ShowCompilerTab() const
{
	if (UContextObjectStore* ContextObjectStore = GetToolManager()->GetContextObjectStore())
	{
		if (const UStateTreeEditorContext* Context = ContextObjectStore->FindContext<UStateTreeEditorContext>())
		{
			if(TSharedPtr<FTabManager> TabManager = GetModeManager()->GetToolkitHost()->GetTabManager())
			{
				TabManager->TryInvokeTab(Context->EditorHostInterface->GetCompilerTabName());
			}
		}
	}
}

UStateTree* UStateTreeEditorMode::GetStateTree() const
{
	return CachedStateTree.Get();
}

void UStateTreeEditorMode::OnAssetFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent) const
{
	// Make sure nodes get updates when properties are changed.
	if(UStateTreeEditingSubsystem* StateTreeEditingSubsystem = GEditor->GetEditorSubsystem<UStateTreeEditingSubsystem>())
	{
		const int32 NumEditedObjects = PropertyChangedEvent.GetNumObjectsBeingEdited();
		if (NumEditedObjects > 0)
		{			
			for (int32 Index = 0; Index < NumEditedObjects; ++Index)
			{
				if (const UStateTree* EditedStateTree = Cast<UStateTree>(PropertyChangedEvent.GetObjectBeingEdited(Index)))
				{
					if (EditedStateTree == GetStateTree())
					{
						StateTreeEditingSubsystem->FindOrAddViewModel(GetStateTree())->NotifyAssetChangedExternally();
						break;
					}
				}
			}
		}
	}
}

void UStateTreeEditorMode::OnSelectionFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
	// Make sure nodes get updates when properties are changed.
	if(UStateTreeEditingSubsystem* StateTreeEditingSubsystem = GEditor->GetEditorSubsystem<UStateTreeEditingSubsystem>())
	{
		if (TSharedPtr<IDetailsView> DetailsView = GetDetailsView())
		{
			const TArray<TWeakObjectPtr<UObject>> SelectedObjects = GetDetailsView()->GetSelectedObjects();
			TSet<UStateTreeState*> ChangedStates;
			for (const TWeakObjectPtr<UObject>& WeakObject : SelectedObjects)
			{
				if (UObject* Object = WeakObject.Get())
				{
					if (UStateTreeState* State = Cast<UStateTreeState>(Object))
					{
						ChangedStates.Add(State);
					}
				}
			}
			if (ChangedStates.Num() > 0)
			{
				StateTreeEditingSubsystem->FindOrAddViewModel(GetStateTree())->NotifyStatesChangedExternally(ChangedStates, PropertyChangedEvent);
				UpdateAsset();
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE // "UStateTreeEditorMode"
