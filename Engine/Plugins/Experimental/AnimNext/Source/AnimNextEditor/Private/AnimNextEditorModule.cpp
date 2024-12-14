// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextEditorModule.h"

#include "AnimNextConfig.h"
#include "EdGraphNode_Comment.h"
#include "ISettingsModule.h"
#include "IWorkspaceEditor.h"
#include "ScopedTransaction.h"
#include "SSimpleButton.h"
#include "UncookedOnlyUtils.h"
#include "Common/SRigVMAssetView.h"
#include "Editor/RigVMEditorTools.h"
#include "Framework/Application/SlateApplication.h"
#include "Module/AnimNextModule.h"
#include "Graph/AnimNextGraphPanelNodeFactory.h"
#include "Graph/AnimNextEdGraphNodeCustomization.h"
#include "Module/AnimNextModule_EditorData.h"
#include "AnimNextEdGraphNode.h"
#include "Graph/TraitEditorTabSummoner.h"
#include "Graph/AnimNextCompilerResultsTabSummoner.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Variables/VariableCustomization.h"
#include "Variables/VariableBindingPropertyCustomization.h"
#include "Param/ParamTypePropertyCustomization.h"
#include "IWorkspaceEditorModule.h"
#include "Common/SActionMenu.h"
#include "Entries/AnimNextRigVMAssetEntry.h"
#include "Editor/RigVMGraphDetailCustomization.h"
#include "FileHelpers.h"
#include "IUniversalObjectLocatorEditorModule.h"
#include "MessageLogModule.h"
#include "Param/AnimNextComponentLocatorEditor.h"
#include "Param/AnimNextLocatorContext.h"
#include "Param/ObjectCastLocatorEditor.h"
#include "Param/ObjectFunctionLocatorEditor.h"
#include "Param/ObjectPropertyLocatorEditor.h"
#include "Variables/SAddVariablesDialog.h"
#include "AnimNextAssetWorkspaceAssetUserData.h"
#include "Graph/AnimNextGraphItemDetails.h"
#include "Graph/AnimNextCollapseNodeItemDetails.h"
#include "Graph/AnimNextFunctionItemDetails.h"
#include "Param/AnimNextActorLocatorEditor.h"
#include "IWorkspaceEditor.h"
#include "Entries/AnimNextAnimationGraphEntry.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "Graph/AnimNextAnimationGraph_EditorData.h"
#include "Common/AnimNextAssetItemDetails.h"
#include "DataInterface/AnimNextDataInterface.h"
#include "Module/RigUnit_AnimNextModuleEvents.h"
#include "Variables/SVariablesView.h"
#include "Variables/VariableOverrideCommands.h"
#include "Variables/VariableProxyCustomization.h"
#include "Workspace/AnimNextWorkspaceSchema.h"

#define LOCTEXT_NAMESPACE "AnimNextEditorModule"

namespace UE::AnimNext::Editor
{

void FAnimNextEditorModule::StartupModule()
{
	FVariableOverrideCommands::Register();

	// Register settings for user editing
	ISettingsModule& SettingsModule = FModuleManager::Get().LoadModuleChecked<ISettingsModule>("Settings");
	SettingsModule.RegisterSettings("Editor", "General", "AnimNext",
		LOCTEXT("SettingsName", "AnimNext"),
		LOCTEXT("SettingsDescription", "Customize AnimNext Settings."),
		GetMutableDefault<UAnimNextConfig>()
	);

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyModule.RegisterCustomPropertyTypeLayout(
		"AnimNextParamType",
		FOnGetPropertyTypeCustomizationInstance::CreateLambda([] { return MakeShared<FParamTypePropertyTypeCustomization>(); }));
	
	PropertyModule.RegisterCustomPropertyTypeLayout(
		"AnimNextVariableBinding",
		FOnGetPropertyTypeCustomizationInstance::CreateLambda([] { return MakeShared<FVariableBindingPropertyCustomization>(); }));

	PropertyModule.RegisterCustomClassLayout("AnimNextVariableEntry", 
		FOnGetDetailCustomizationInstance::CreateLambda([] { return MakeShared<FVariableCustomization>(); }));

	PropertyModule.RegisterCustomClassLayout("AnimNextVariableEntryProxy", 
		FOnGetDetailCustomizationInstance::CreateLambda([] { return MakeShared<FVariableProxyCustomization>(); }));
	
	AnimNextGraphPanelNodeFactory = MakeShared<FAnimNextGraphPanelNodeFactory>();
	FEdGraphUtilities::RegisterVisualNodeFactory(AnimNextGraphPanelNodeFactory);

	Workspace::IWorkspaceEditorModule& WorkspaceEditorModule = FModuleManager::Get().LoadModuleChecked<Workspace::IWorkspaceEditorModule>("WorkspaceEditor");

	WorkspaceEditorModule.OnRegisterTabsForEditor().AddLambda([](FWorkflowAllowedTabSet& TabFactories, const TSharedRef<FTabManager>& InTabManager, TSharedPtr<UE::Workspace::IWorkspaceEditor> InEditorPtr)
		{
			TSharedRef<FTraitEditorTabSummoner> TraitEditorTabSummoner = MakeShared<FTraitEditorTabSummoner>(InEditorPtr);
			TabFactories.RegisterFactory(TraitEditorTabSummoner);
			TraitEditorTabSummoner->RegisterTabSpawner(InTabManager, nullptr);

			TSharedRef<FAnimNextCompilerResultsTabSummoner> CompilerResultsTabSummoner = MakeShared<FAnimNextCompilerResultsTabSummoner>(InEditorPtr);
			TabFactories.RegisterFactory(CompilerResultsTabSummoner);
			CompilerResultsTabSummoner->RegisterTabSpawner(InTabManager, nullptr);

			TSharedRef<FAnimNextVariablesTabSummoner> VariablesTabSummoner = MakeShared<FAnimNextVariablesTabSummoner>(InEditorPtr);
			TabFactories.RegisterFactory(VariablesTabSummoner);
			VariablesTabSummoner->RegisterTabSpawner(InTabManager, nullptr);
		});

	WorkspaceEditorModule.OnExtendTabs().AddLambda([](FLayoutExtender& InLayoutExtender, TSharedPtr<UE::Workspace::IWorkspaceEditor> InEditorPtr)
	{
		FTabManager::FTab TraitEditorTab(FTabId(TraitEditorTabName), ETabState::ClosedTab);
		InLayoutExtender.ExtendLayout(FTabId(Workspace::WorkspaceTabs::TopRightDocumentArea), ELayoutExtensionPosition::After, TraitEditorTab);

		FTabManager::FTab CompilerResultsTab(FTabId(CompilerResultsTabName), ETabState::ClosedTab);
		InLayoutExtender.ExtendLayout(FTabId(Workspace::WorkspaceTabs::BottomMiddleDocumentArea), ELayoutExtensionPosition::After, CompilerResultsTab);

		FTabManager::FTab VariablesTab(FTabId(VariablesTabName), ETabState::OpenedTab);
		InLayoutExtender.ExtendLayout(FTabId(Workspace::WorkspaceTabs::BottomLeftDocumentArea), ELayoutExtensionPosition::After, VariablesTab);
	});

	RegisterWorkspaceDocumentTypes(WorkspaceEditorModule);

	WorkspaceEditorModule.OnRegisterWorkspaceDetailsCustomization().AddLambda([](const TWeakPtr<Workspace::IWorkspaceEditor>& InWorkspaceEditor, TSharedPtr<IDetailsView>& InDetailsView)
		{
			InDetailsView->RegisterInstancedCustomPropertyLayout(UAnimNextEdGraphNode::StaticClass(), FOnGetDetailCustomizationInstance::CreateLambda([InWorkspaceEditor]()
				{
					return MakeShared<FAnimNextEdGraphNodeCustomization>(InWorkspaceEditor);
				}));

			TArray<UScriptStruct*> StructsToCustomize = {
				TBaseStructure<FVector>::Get(),
				TBaseStructure<FVector2D>::Get(),
				TBaseStructure<FVector4>::Get(),
				TBaseStructure<FRotator>::Get(),
				TBaseStructure<FQuat>::Get(),
				TBaseStructure<FTransform>::Get(),
				TBaseStructure<FEulerTransform>::Get(),
			};
			for (UScriptStruct* StructToCustomize : StructsToCustomize)
			{
				InDetailsView->RegisterInstancedCustomPropertyTypeLayout(StructToCustomize->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateLambda([]()
					{
						return FRigVMGraphMathTypeDetailCustomization::MakeInstance();
					}));
			}
		});

	SRigVMAssetView::RegisterCategoryFactory("Variables", [](UAnimNextRigVMAssetEditorData* InEditorData)
	{
		UAnimNextRigVMAssetEditorData* EditorData = CastChecked<UAnimNextRigVMAssetEditorData>(InEditorData);
		UAnimNextRigVMAsset* Asset = UncookedOnly::FUtils::GetAsset(InEditorData);
		return SNew(SSimpleButton)
			.Text(LOCTEXT("AddVariableButton", "Add Variable"))
			.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
			.OnClicked_Lambda([EditorData, Asset]()
			{
				TSharedRef<SAddVariablesDialog> AddVariablesDialog =
					SNew(SAddVariablesDialog, TArray<UAnimNextRigVMAssetEditorData*>({ EditorData }));

				TArray<FVariableToAdd> VariablesToAdd;
				TArray<FDataInterfaceToAdd> DataInterfacesToAdd;
				if(AddVariablesDialog->ShowModal(VariablesToAdd, DataInterfacesToAdd))
				{
					FScopedTransaction Transaction(LOCTEXT("AddVariables", "Add variable(s)"));
					for (const FVariableToAdd& VariableToAdd : VariablesToAdd)
					{
						check(EditorData->FindEntry(VariableToAdd.Name) == nullptr);
						EditorData->AddVariable(VariableToAdd.Name, VariableToAdd.Type);
					}
				}
				return FReply::Handled();
			});
	});

	SRigVMAssetView::RegisterCategoryFactory("Event Graphs", [](UAnimNextRigVMAssetEditorData* InEditorData)
	{
		UAnimNextRigVMAssetEditorData* EditorData = CastChecked<UAnimNextRigVMAssetEditorData>(InEditorData);
		return SNew(SSimpleButton)
			.Text(LOCTEXT("AddEventGraphButton", "Add Event Graph"))
			.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
			.OnClicked_Lambda([EditorData]()
			{
				FScopedTransaction Transaction(LOCTEXT("AddEventGraph", "Add Event Graph"));

				// Create a new entry for the graph
				EditorData->AddEventGraph(TEXT("NewGraph"), FRigUnit_AnimNextPrePhysicsEvent::StaticStruct());

				return FReply::Handled();
			});
	});

	SRigVMAssetView::RegisterCategoryFactory("Animation Graphs", [](UAnimNextRigVMAssetEditorData* InEditorData)
	{
		UAnimNextRigVMAssetEditorData* EditorData = CastChecked<UAnimNextRigVMAssetEditorData>(InEditorData);
		return SNew(SSimpleButton)
			.Text(LOCTEXT("AddGraphButton", "Add Animation Graph"))
			.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
			.OnClicked_Lambda([EditorData]()
			{
				FScopedTransaction Transaction(LOCTEXT("AddAnimationGraph", "Add Animation Graph"));

				// Create a new entry for the graph
				EditorData->AddAnimationGraph(TEXT("NewGraph"));

				return FReply::Handled();
			});
	});

	UE::UniversalObjectLocator::IUniversalObjectLocatorEditorModule& UolEditorModule = FModuleManager::LoadModuleChecked<UE::UniversalObjectLocator::IUniversalObjectLocatorEditorModule>("UniversalObjectLocatorEditor");
	UolEditorModule.RegisterLocatorEditor("AnimNextObjectFunction", MakeShared<FObjectFunctionLocatorEditor>());
	UolEditorModule.RegisterLocatorEditor("AnimNextObjectProperty", MakeShared<FObjectPropertyLocatorEditor>());
	UolEditorModule.RegisterLocatorEditor("AnimNextObjectCast", MakeShared<FObjectCastLocatorEditor>());
	UolEditorModule.RegisterLocatorEditor("AnimNextComponent", MakeShared<FComponentLocatorEditor>());
	UolEditorModule.RegisterLocatorEditor("AnimNextActor", MakeShared<FActorLocatorEditor>());

	UolEditorModule.RegisterEditorContext("AnimNextContext", MakeShared<FLocatorContext>());

	RegisterLocatorFragmentEditorType("Actor");
	RegisterLocatorFragmentEditorType("Asset");
	RegisterLocatorFragmentEditorType("AnimNextScope");
	RegisterLocatorFragmentEditorType("AnimNextGraph");
	RegisterLocatorFragmentEditorType("AnimNextObjectFunction");
	RegisterLocatorFragmentEditorType("AnimNextObjectProperty");
	RegisterLocatorFragmentEditorType("AnimNextObjectCast");
	RegisterLocatorFragmentEditorType("AnimNextComponent");
	RegisterLocatorFragmentEditorType("AnimNextActor");

	Workspace::IWorkspaceEditorModule& WorkspaceModule = FModuleManager::Get().LoadModuleChecked<Workspace::IWorkspaceEditorModule>("WorkspaceEditor");
	WorkspaceModule.RegisterWorkspaceItemDetails(Workspace::FOutlinerItemDetailsId(FAnimNextGraphOutlinerData::StaticStruct()->GetFName()), MakeShared<FAnimNextGraphItemDetails>());
	WorkspaceModule.RegisterWorkspaceItemDetails(Workspace::FOutlinerItemDetailsId(FAnimNextCollapseGraphOutlinerData::StaticStruct()->GetFName()), MakeShared<FAnimNextCollapseNodeItemDetails>());
	WorkspaceModule.RegisterWorkspaceItemDetails(Workspace::FOutlinerItemDetailsId(FAnimNextGraphFunctionOutlinerData::StaticStruct()->GetFName()), MakeShared<FAnimNextFunctionItemDetails>());

	FAnimNextGraphItemDetails::RegisterToolMenuExtensions();
	FAnimNextCollapseNodeItemDetails::RegisterToolMenuExtensions();
	FAnimNextFunctionItemDetails::RegisterToolMenuExtensions();

	const TSharedPtr<FAnimNextAssetItemDetails> AssetItemDetails = MakeShared<FAnimNextAssetItemDetails>();
	WorkspaceModule.RegisterWorkspaceItemDetails(Workspace::FOutlinerItemDetailsId(FAnimNextModuleOutlinerData::StaticStruct()->GetFName()), AssetItemDetails);
	WorkspaceModule.RegisterWorkspaceItemDetails(Workspace::FOutlinerItemDetailsId(FAnimNextAnimationGraphOutlinerData::StaticStruct()->GetFName()), AssetItemDetails);

	FAnimNextGraphItemDetails::RegisterToolMenuExtensions();
	FAnimNextAssetItemDetails::RegisterToolMenuExtensions();

	SupportedAssetClasses.Append(
		{
			UAnimNextAnimationGraph::StaticClass()->GetClassPathName(),
			UAnimNextModule::StaticClass()->GetClassPathName(),
			UAnimNextDataInterface::StaticClass()->GetClassPathName()
		});
}

void FAnimNextEditorModule::ShutdownModule()
{
	if(FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomPropertyTypeLayout("AnimNextParamType");
		PropertyModule.UnregisterCustomPropertyTypeLayout("AnimNextVariableBinding");
		PropertyModule.UnregisterCustomClassLayout("AnimNextVariableEntry");
		PropertyModule.UnregisterCustomClassLayout("AnimNextVariableEntryProxy");
	}

	FEdGraphUtilities::UnregisterVisualNodeFactory(AnimNextGraphPanelNodeFactory);

	UnregisterWorkspaceDocumentTypes();

	SRigVMAssetView::UnregisterCategoryFactory("Parameters");
	SRigVMAssetView::UnregisterCategoryFactory("Event Graphs");
	SRigVMAssetView::UnregisterCategoryFactory("Animation Graphs");

	if(FModuleManager::Get().IsModuleLoaded("UniversalObjectLocatorEditor"))
	{
		UE::UniversalObjectLocator::IUniversalObjectLocatorEditorModule& UolEditorModule = FModuleManager::GetModuleChecked<UE::UniversalObjectLocator::IUniversalObjectLocatorEditorModule>("UniversalObjectLocatorEditor");
		UolEditorModule.UnregisterLocatorEditor("AnimNextObjectCast");
		UolEditorModule.UnregisterLocatorEditor("AnimNextObjectFunction");
		UolEditorModule.UnregisterLocatorEditor("AnimNextObjectProperty");
		UolEditorModule.UnregisterLocatorEditor("AnimNextComponent");
		UolEditorModule.UnregisterLocatorEditor("AnimNextActor");

		UolEditorModule.UnregisterEditorContext("AnimNextContext");
	}
	
	if (UObjectInitialized())
	{
		Workspace::IWorkspaceEditorModule& WorkspaceModule = FModuleManager::Get().LoadModuleChecked<Workspace::IWorkspaceEditorModule>("WorkspaceEditor");
		WorkspaceModule.UnregisterWorkspaceItemDetails(Workspace::FOutlinerItemDetailsId(FAnimNextGraphOutlinerData::StaticStruct()->GetFName()));
		WorkspaceModule.UnregisterWorkspaceItemDetails(Workspace::FOutlinerItemDetailsId(FAnimNextCollapseGraphOutlinerData::StaticStruct()->GetFName()));
		WorkspaceModule.UnregisterWorkspaceItemDetails(Workspace::FOutlinerItemDetailsId(FAnimNextGraphFunctionOutlinerData::StaticStruct()->GetFName()));
		FAnimNextGraphItemDetails::UnregisterToolMenuExtensions();
		WorkspaceModule.UnregisterWorkspaceItemDetails(Workspace::FOutlinerItemDetailsId(FAnimNextModuleOutlinerData::StaticStruct()->GetFName()));
		WorkspaceModule.UnregisterWorkspaceItemDetails(Workspace::FOutlinerItemDetailsId(FAnimNextAnimationGraphOutlinerData::StaticStruct()->GetFName()));
		FAnimNextAssetItemDetails::UnregisterToolMenuExtensions();
	}

	UnregisterLocatorFragmentEditorType("Actor");
	UnregisterLocatorFragmentEditorType("Asset");
	UnregisterLocatorFragmentEditorType("AnimNextScope");
	UnregisterLocatorFragmentEditorType("AnimNextGraph");
	UnregisterLocatorFragmentEditorType("AnimNextObjectFunction");
	UnregisterLocatorFragmentEditorType("AnimNextObjectProperty");
	UnregisterLocatorFragmentEditorType("AnimNextObjectCast");
	UnregisterLocatorFragmentEditorType("AnimNextComponent");
	UnregisterLocatorFragmentEditorType("AnimNextActor");
}

void FAnimNextEditorModule::RegisterLocatorFragmentEditorType(FName InLocatorFragmentEditorName)
{
	LocatorFragmentEditorNames.Add(InLocatorFragmentEditorName);
}

void FAnimNextEditorModule::UnregisterLocatorFragmentEditorType(FName InLocatorFragmentEditorName)
{
	LocatorFragmentEditorNames.Remove(InLocatorFragmentEditorName);
}

void FAnimNextEditorModule::AddWorkspaceSupportedAssetClass(const FTopLevelAssetPath& InClassAssetPath)
{
	if (InClassAssetPath.IsValid())
	{
		SupportedAssetClasses.AddUnique(InClassAssetPath);
	}	
}

void FAnimNextEditorModule::RemoveWorkspaceSupportedAssetClass(const FTopLevelAssetPath& InClassAssetPath)
{
	if (InClassAssetPath.IsValid())
	{
		SupportedAssetClasses.Remove(InClassAssetPath);
	}	
}

void FAnimNextEditorModule::RegisterWorkspaceDocumentTypes(Workspace::IWorkspaceEditorModule& WorkspaceEditorModule)
{
	// --- AnimNextRigVMAsset ---
	Workspace::FObjectDocumentArgs AnimNextAssetDocumentArgs(
			Workspace::FOnMakeDocumentWidget::CreateLambda([](const Workspace::FWorkspaceEditorContext& InContext)
			{
				UAnimNextRigVMAsset* Asset = CastChecked<UAnimNextRigVMAsset>(InContext.Object);
				UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(Asset);

				TWeakPtr<Workspace::IWorkspaceEditor> WeakWorkspaceEditor = InContext.WorkspaceEditor;

				// TODO: For now (so we can convert older assets over to new formats manually) we open the asset view, but in the future we
				// should just open any nested graphs (and set the variables view up)
				TSharedPtr<SRigVMAssetView> SharedAssetView = SNew(SRigVMAssetView, EditorData)
					.OnOpenGraph_Lambda([WeakWorkspaceEditor](URigVMGraph* InGraph)
					{
						if(TSharedPtr<Workspace::IWorkspaceEditor> WorkspaceEditor = WeakWorkspaceEditor.Pin())
						{
							if(IRigVMClientHost* RigVMClientHost = InGraph->GetImplementingOuter<IRigVMClientHost>())
							{
								if(UObject* EditorObject = RigVMClientHost->GetEditorObjectForRigVMGraph(InGraph))
								{
									WorkspaceEditor->OpenObjects({EditorObject});
								}
							}
						}
					})
					.OnDeleteEntries_Lambda([WeakWorkspaceEditor](const TArray<UAnimNextRigVMAssetEntry*>& InEntries)
					{
						if(TSharedPtr<Workspace::IWorkspaceEditor> WorkspaceEditor = WeakWorkspaceEditor.Pin())
						{
							if(InEntries.Num() > 0)
							{
								TArray<UObject*> EdGraphsToClose;
								EdGraphsToClose.Reserve(InEntries.Num());
								for(UAnimNextRigVMAssetEntry* Entry : InEntries)
								{
									if(IAnimNextRigVMGraphInterface* GraphInterface = Cast<IAnimNextRigVMGraphInterface>(Entry))
									{
										if(URigVMEdGraph* EdGraph = GraphInterface->GetEdGraph())
										{
											EdGraphsToClose.Add(EdGraph);
										}
									}
								}

								WorkspaceEditor->CloseObjects(EdGraphsToClose);
							}
						}
					});

				TWeakPtr<SRigVMAssetView> WeakAssetView = SharedAssetView;
				SharedAssetView->SetOnSelectionChanged(SRigVMAssetView::FOnSelectionChanged::CreateLambda([WeakWorkspaceEditor, WeakAssetView](const TArray<UObject*>& InEntries)
				{
					if(TSharedPtr<SRigVMAssetView> SharedAssetView = WeakAssetView.Pin())
					{
						if(TSharedPtr<Workspace::IWorkspaceEditor> WorkspaceEditor = WeakWorkspaceEditor.Pin())
						{	
							WorkspaceEditor->SetGlobalSelection(SharedAssetView, UE::Workspace::FOnClearGlobalSelection::CreateLambda([WeakAssetView](){ if (const TSharedPtr<SRigVMAssetView> SharedAssetView = WeakAssetView.Pin()) { SharedAssetView->ClearSelection(); } }));
							WorkspaceEditor->SetDetailsObjects(InEntries);
						}
				}}));


				return SharedAssetView.ToSharedRef();
			}),
			Workspace::WorkspaceTabs::TopMiddleDocumentArea);
	AnimNextAssetDocumentArgs.OnGetTabName = Workspace::FOnGetTabName::CreateLambda([](const Workspace::FWorkspaceEditorContext& InContext)
	{
		const UAnimNextRigVMAsset* Asset = CastChecked<UAnimNextRigVMAsset>(InContext.Object);
		return FText::FromName(Asset->GetFName());
	});

	AnimNextAssetDocumentArgs.OnGetDocumentBreadcrumbTrail = Workspace::FOnGetDocumentBreadcrumbTrail::CreateLambda([](const Workspace::FWorkspaceEditorContext& InContext, TArray<TSharedPtr<Workspace::FWorkspaceBreadcrumb>>& OutBreadcrumbs)
	{
		if (const UAnimNextRigVMAsset* Asset = Cast<UAnimNextRigVMAsset>(InContext.Object))
		{
			TWeakObjectPtr<const UAnimNextRigVMAsset> WeakAsset = Asset;
			const TSharedPtr<Workspace::FWorkspaceBreadcrumb>& GraphCrumb = OutBreadcrumbs.Add_GetRef(MakeShared<Workspace::FWorkspaceBreadcrumb>());
			GraphCrumb->OnGetLabel = Workspace::FWorkspaceBreadcrumb::FOnGetBreadcrumbLabel::CreateLambda([AssetName = Asset->GetFName()]{ return FText::FromName(AssetName); });
			GraphCrumb->CanSave = Workspace::FWorkspaceBreadcrumb::FCanSaveBreadcrumb::CreateLambda(
				[WeakAsset]
				{
					if (const UAnimNextRigVMAsset* Asset = WeakAsset.Get())
					{
						return Asset->GetPackage()->IsDirty();						
					}

					return false;
				}
			);
			GraphCrumb->OnSave = Workspace::FWorkspaceBreadcrumb::FOnSaveBreadcrumb::CreateLambda(
				[WeakAsset]
				{
					if (const UAnimNextRigVMAsset* Asset = WeakAsset.Get())
					{
						FEditorFileUtils::PromptForCheckoutAndSave({Asset->GetPackage()}, false, /*bPromptToSave=*/ false);
					}
				}
			);
		}
	});

	AnimNextAssetDocumentArgs.OnGetTabIcon = Workspace::FOnGetTabIcon::CreateLambda([](const Workspace::FWorkspaceEditorContext& InContext)
	{
		return FAppStyle::GetBrush(TEXT("ClassIcon.Default"));
	});

	WorkspaceEditorModule.RegisterObjectDocumentType(FTopLevelAssetPath(TEXT("/Script/AnimNext.AnimNextModule")), AnimNextAssetDocumentArgs);

	// --- AnimNextAnimationGraph ---
	Workspace::FObjectDocumentArgs AnimNextAnimationGraphDocumentArgs(
			Workspace::FOnRedirectWorkspaceContext::CreateLambda([](UObject* InObject)
			{
				UAnimNextAnimationGraph* AnimationGraph = CastChecked<UAnimNextAnimationGraph>(InObject);
				UAnimNextAnimationGraph_EditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextAnimationGraph_EditorData>(AnimationGraph);

				// Redirect to the inner graph
				UAnimNextAnimationGraphEntry* AnimationGraphEntry = CastChecked<UAnimNextAnimationGraphEntry>(EditorData->FindEntry(FRigUnit_AnimNextGraphRoot::DefaultEntryPoint));
				return AnimationGraphEntry->GetEdGraph();
			}));

	WorkspaceEditorModule.RegisterObjectDocumentType(FTopLevelAssetPath(TEXT("/Script/AnimNext.AnimNextAnimationGraph")), AnimNextAnimationGraphDocumentArgs);

	// --- AnimNextEdGraph ---
	Workspace::FGraphDocumentWidgetArgs GraphArgs;
	GraphArgs.SpawnLocation = Workspace::WorkspaceTabs::TopMiddleDocumentArea;
	GraphArgs.OnCreateActionMenu = Workspace::FOnCreateActionMenu::CreateLambda([](const Workspace::FWorkspaceEditorContext& InContext, UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed)
	{
		TSharedRef<SActionMenu> ActionMenu = SNew(SActionMenu, InGraph)
			.AutoExpandActionMenu(bAutoExpand)
			.NewNodePosition(InNodePosition)
			.DraggedFromPins(InDraggedPins)
			.OnClosedCallback(InOnMenuClosed);

		TSharedPtr<SWidget> FilterTextBox = StaticCastSharedRef<SWidget>(ActionMenu->GetFilterTextBox());
		return FActionMenuContent(StaticCastSharedRef<SWidget>(ActionMenu), FilterTextBox);
	});
	GraphArgs.OnNodeTextCommitted = Workspace::FOnNodeTextCommitted::CreateLambda([](const Workspace::FWorkspaceEditorContext& InContext, const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged)
	{
		URigVMEdGraph* RigVMEdGraph = Cast<URigVMEdGraph>(NodeBeingChanged->GetGraph());
		if (RigVMEdGraph == nullptr)
		{
			return;
		}

		UEdGraphNode_Comment* CommentBeingChanged = Cast<UEdGraphNode_Comment>(NodeBeingChanged);
		if (CommentBeingChanged == nullptr)
		{
			return;
		}

		RigVMEdGraph->GetController()->SetCommentTextByName(CommentBeingChanged->GetFName(), NewText.ToString(), CommentBeingChanged->FontSize, CommentBeingChanged->bCommentBubbleVisible, CommentBeingChanged->bColorCommentBubble, true, true);
	});
	GraphArgs.OnCanDeleteSelectedNodes = Workspace::FOnCanPerformActionOnSelectedNodes::CreateLambda([](const Workspace::FWorkspaceEditorContext& InContext, const FGraphPanelSelectionSet& InSelectedNodes)
	{
		bool bCanUserDeleteNode = false;

		if(InSelectedNodes.Num() > 0)
		{
			for(UObject* NodeObject : InSelectedNodes)
			{
				// If any nodes allow deleting, then do not disable the delete option
				const UEdGraphNode* Node = Cast<UEdGraphNode>(NodeObject);
				if(Node && Node->CanUserDeleteNode())
				{
					bCanUserDeleteNode = true;
					break;
				}
			}
		}

		return bCanUserDeleteNode;
	});
	GraphArgs.OnDeleteSelectedNodes = Workspace::FOnPerformActionOnSelectedNodes::CreateLambda([](const Workspace::FWorkspaceEditorContext& InContext, const FGraphPanelSelectionSet& InSelectedNodes)
	{
		if(InSelectedNodes.IsEmpty())
		{
			return;
		}

		URigVMController* Controller = nullptr;
		
		bool bRelinkPins = false;
		TArray<URigVMNode*> NodesToRemove;

		for (FGraphPanelSelectionSet::TConstIterator NodeIt(InSelectedNodes); NodeIt; ++NodeIt)
		{
			if (UEdGraphNode* Node = Cast<UEdGraphNode>(*NodeIt))
			{
				URigVMEdGraph* RigVMEdGraph = Cast<URigVMEdGraph>(Node->GetGraph());
				if (RigVMEdGraph == nullptr)
				{
					continue;
				}
				
				if (Node->CanUserDeleteNode())
				{
					if (const URigVMEdGraphNode* RigVMEdGraphNode = Cast<URigVMEdGraphNode>(Node))
					{
						if(Controller == nullptr)
						{
							Controller = RigVMEdGraphNode->GetController();
						}

						bRelinkPins = bRelinkPins || FSlateApplication::Get().GetModifierKeys().IsShiftDown();

						if(URigVMGraph* Model = RigVMEdGraph->GetModel())
						{
							if(URigVMNode* ModelNode = Model->FindNodeByName(*RigVMEdGraphNode->GetModelNodePath()))
							{
								NodesToRemove.Add(ModelNode);
							}
						}
					}
					else if (const UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node))
					{
						if(URigVMGraph* Model = RigVMEdGraph->GetModel())
						{
							if(URigVMNode* ModelNode = Model->FindNodeByName(CommentNode->GetFName()))
							{
								NodesToRemove.Add(ModelNode);
							}
						}
					}
					else
					{
						Node->GetGraph()->RemoveNode(Node);
					}
				}
			}
		}

		if(NodesToRemove.IsEmpty() || Controller == nullptr)
		{
			return;
		}

		Controller->OpenUndoBracket(TEXT("Delete selected nodes"));
		if(bRelinkPins && NodesToRemove.Num() == 1)
		{
			Controller->RelinkSourceAndTargetPins(NodesToRemove[0], true);;
		}
		Controller->RemoveNodes(NodesToRemove, true);
		Controller->CloseUndoBracket();
	});
	GraphArgs.OnCanCopySelectedNodes = Workspace::FOnCanPerformActionOnSelectedNodes::CreateLambda([](const Workspace::FWorkspaceEditorContext& InContext, const FGraphPanelSelectionSet& InSelectedNodes)
	{
		bool bCanUserCopyNode = false;

		if(InSelectedNodes.Num() > 0)
		{
			bCanUserCopyNode = true;
		}

		return bCanUserCopyNode;
	});
	GraphArgs.OnCopySelectedNodes = Workspace::FOnPerformActionOnSelectedNodes::CreateLambda([](const Workspace::FWorkspaceEditorContext& InContext, const FGraphPanelSelectionSet& InSelectedNodes)
	{
		if(InSelectedNodes.IsEmpty())
		{
			return;
		}

		URigVMEdGraph* RigVMEdGraph = Cast<URigVMEdGraph>(InContext.Object);
		if (RigVMEdGraph == nullptr)
		{
			return;
		}

		URigVMController* Controller = RigVMEdGraph->GetController();

		FString ExportedText = Controller->ExportSelectedNodesToText();
		FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
	});
	GraphArgs.OnCanPasteNodes = Workspace::FOnCanPasteNodes::CreateLambda([](const Workspace::FWorkspaceEditorContext& InContext, const FString& InImportData)
	{
		bool bCanUserImportNodes = false;

		if (!InImportData.IsEmpty())
		{
			bCanUserImportNodes = true;
		}

		return bCanUserImportNodes;
	});
	GraphArgs.OnPasteNodes = Workspace::FOnPasteNodes::CreateLambda([](const Workspace::FWorkspaceEditorContext& InContext, const FVector2D& InPasteLocation, const FString& InImportData)
	{
		if(InImportData.IsEmpty())
		{
			return;
		}

		URigVMEdGraph* RigVMEdGraph = Cast<URigVMEdGraph>(InContext.Object);
		if (RigVMEdGraph == nullptr)
		{
			return;
		}

		if (IRigVMClientHost* RigVMClientHost = RigVMEdGraph->GetImplementingOuter<IRigVMClientHost>())
		{
			FString TextToImport;
			FPlatformApplicationMisc::ClipboardPaste(TextToImport);
			URigVMController* Controller = RigVMEdGraph->GetController();

			Controller->OpenUndoBracket(TEXT("Pasted Nodes."));

			if (UE::RigVM::Editor::Tools::PasteNodes(InPasteLocation, TextToImport, Controller, RigVMEdGraph->GetModel(), RigVMClientHost->GetLocalFunctionLibrary(), RigVMClientHost->GetRigVMGraphFunctionHost()))
			{
				Controller->CloseUndoBracket();
			}
			else
			{
				Controller->CancelUndoBracket();
			}
		}
	});
	UE::Workspace::FOnCanPerformActionOnSelectedNodes& OnCanCopySelectedNodes = GraphArgs.OnCanCopySelectedNodes;
	UE::Workspace::FOnCanPerformActionOnSelectedNodes& OnCanDeleteSelectedNodes = GraphArgs.OnCanDeleteSelectedNodes;
	GraphArgs.OnCanCutSelectedNodes = Workspace::FOnCanPerformActionOnSelectedNodes::CreateLambda([OnCanCopySelectedNodes, OnCanDeleteSelectedNodes](const Workspace::FWorkspaceEditorContext& InContext, const FGraphPanelSelectionSet& InSelectedNodes)
		{
			bool bCanUserCopyNode = false;

			if (OnCanCopySelectedNodes.IsBound() && OnCanDeleteSelectedNodes.IsBound())
			{
				bCanUserCopyNode = OnCanCopySelectedNodes.Execute(InContext, InSelectedNodes) && OnCanDeleteSelectedNodes.Execute(InContext, InSelectedNodes);
			}

			return bCanUserCopyNode;
		});
	UE::Workspace::FOnPerformActionOnSelectedNodes& OnCopySelectedNodes = GraphArgs.OnCopySelectedNodes;
	UE::Workspace::FOnPerformActionOnSelectedNodes& OnDeleteSelectedNodes = GraphArgs.OnDeleteSelectedNodes;
	GraphArgs.OnCutSelectedNodes = Workspace::FOnPerformActionOnSelectedNodes::CreateLambda([OnCopySelectedNodes, OnDeleteSelectedNodes](const Workspace::FWorkspaceEditorContext& InContext, const FGraphPanelSelectionSet& InSelectedNodes)
		{
			if (InSelectedNodes.IsEmpty())
			{
				return;
			}

			URigVMEdGraph* RigVMEdGraph = Cast<URigVMEdGraph>(InContext.Object);
			if (RigVMEdGraph == nullptr)
			{
				return;
			}


			if (OnCopySelectedNodes.IsBound() && OnDeleteSelectedNodes.IsBound())
			{
				URigVMController* Controller = RigVMEdGraph->GetController();

				OnCopySelectedNodes.Execute(InContext, InSelectedNodes);

				Controller->OpenUndoBracket(TEXT("Cut Nodes."));
				OnDeleteSelectedNodes.Execute(InContext, InSelectedNodes);
				Controller->CloseUndoBracket();
			}
		});
	UE::Workspace::FOnCanPasteNodes& OnCanPasteNodes = GraphArgs.OnCanPasteNodes;
	GraphArgs.OnCanDuplicateSelectedNodes = Workspace::FOnCanPerformActionOnSelectedNodes::CreateLambda([OnCanCopySelectedNodes, OnCanPasteNodes](const Workspace::FWorkspaceEditorContext& InContext, const FGraphPanelSelectionSet& InSelectedNodes)
		{
			bool bCanUserCopyNode = false;

			if (OnCanCopySelectedNodes.IsBound() && OnCanPasteNodes.IsBound())
			{
				FString TextToImport;
				FPlatformApplicationMisc::ClipboardPaste(TextToImport);

				bCanUserCopyNode = OnCanCopySelectedNodes.Execute(InContext, InSelectedNodes) && OnCanPasteNodes.Execute(InContext, TextToImport);
			}

			return bCanUserCopyNode;
		});
	UE::Workspace::FOnPasteNodes& OnPasteNodes = GraphArgs.OnPasteNodes;
	GraphArgs.OnDuplicateSelectedNodes = Workspace::FOnDuplicateSelectedNodes::CreateLambda([OnCopySelectedNodes, OnPasteNodes](const Workspace::FWorkspaceEditorContext& InContext, const FVector2D& InPasteLocation, const FGraphPanelSelectionSet& InSelectedNodes)
		{
			if (InSelectedNodes.IsEmpty())
			{
				return;
			}

			URigVMEdGraph* RigVMEdGraph = Cast<URigVMEdGraph>(InContext.Object);
			if (RigVMEdGraph == nullptr)
			{
				return;
			}

			if (OnCopySelectedNodes.IsBound() && OnPasteNodes.IsBound())
			{
				OnCopySelectedNodes.Execute(InContext, InSelectedNodes);

				FString TextToImport;
				FPlatformApplicationMisc::ClipboardPaste(TextToImport);

				URigVMController* Controller = RigVMEdGraph->GetController();

				Controller->OpenUndoBracket(TEXT("Duplicate Nodes."));
				OnPasteNodes.Execute(InContext, InPasteLocation, TextToImport);
				Controller->CloseUndoBracket();
			}
		});
	GraphArgs.OnGraphSelectionChanged = Workspace::FOnGraphSelectionChanged::CreateLambda([](const Workspace::FWorkspaceEditorContext& InContext, const FGraphPanelSelectionSet& NewSelection)
	{
		URigVMEdGraph* RigVMEdGraph = Cast<URigVMEdGraph>(InContext.Object);
		if (RigVMEdGraph == nullptr)
		{
			return;
		}

		if (RigVMEdGraph->bIsSelecting || GIsTransacting)
		{
			return;
		}

		TGuardValue<bool> SelectGuard(RigVMEdGraph->bIsSelecting, true);

		TArray<FName> NodeNamesToSelect;
		for (UObject* Object : NewSelection)
		{
			if (URigVMEdGraphNode* RigVMEdGraphNode = Cast<URigVMEdGraphNode>(Object))
			{
				NodeNamesToSelect.Add(RigVMEdGraphNode->GetModelNodeName());
			}
			else if(UEdGraphNode* Node = Cast<UEdGraphNode>(Object))
			{
				NodeNamesToSelect.Add(Node->GetFName());
			}
		}
		RigVMEdGraph->GetController()->SetNodeSelection(NodeNamesToSelect, true, true);

		InContext.WorkspaceEditor->SetDetailsObjects(NewSelection.Array());
	});
	GraphArgs.OnNodeDoubleClicked = Workspace::FOnNodeDoubleClicked::CreateLambda([](const Workspace::FWorkspaceEditorContext& InContext, const UEdGraphNode* InNode)
	{
		if (const URigVMEdGraphNode* RigVMEdGraphNode = Cast<URigVMEdGraphNode>(InNode))
		{
			const URigVMNode* ModelNode = RigVMEdGraphNode->GetModelNode();

			if (const URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(ModelNode))
			{
				URigVMGraph* ContainedGraph = LibraryNode->GetContainedGraph();

				if (const URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(LibraryNode))
				{
					if (URigVMLibraryNode* ReferencedNode = FunctionReferenceNode->LoadReferencedNode())
					{
						ContainedGraph = ReferencedNode->GetContainedGraph();
					}
				}

				if (ContainedGraph)
				{
					if (TSharedPtr<Workspace::IWorkspaceEditor> WorkspaceEditor = InContext.WorkspaceEditor)
					{
						if (IRigVMClientHost* RigVMClientHost = ContainedGraph->GetImplementingOuter<IRigVMClientHost>())
						{
							if (UObject* EditorObject = RigVMClientHost->GetEditorObjectForRigVMGraph(ContainedGraph))
							{
								WorkspaceEditor->OpenObjects({ EditorObject });
							}
						}
					}
				}
			}
		}
	});

	Workspace::FObjectDocumentArgs GraphDocumentArgs = WorkspaceEditorModule.CreateGraphDocumentArgs(GraphArgs);
	Workspace::FOnMakeDocumentWidget WorkspaceMakeDocumentWidgetDelegate = GraphDocumentArgs.OnMakeDocumentWidget;
	GraphDocumentArgs.OnMakeDocumentWidget = Workspace::FOnMakeDocumentWidget::CreateLambda([WorkspaceMakeDocumentWidgetDelegate](const Workspace::FWorkspaceEditorContext& InContext)
	{
		TWeakPtr<Workspace::IWorkspaceEditor> WeakWorkspaceEditor = InContext.WorkspaceEditor;

		if (UAnimNextEdGraph* EdGraph = Cast<UAnimNextEdGraph>(InContext.Object))
		{
			UAnimNextRigVMAssetEditorData* EditorData = EdGraph->GetTypedOuter<UAnimNextRigVMAssetEditorData>();
			if(EditorData)
			{
				EditorData->InteractionBracketFinished.RemoveAll(&InContext.WorkspaceEditor.Get());
				EditorData->InteractionBracketFinished.AddSPLambda(&InContext.WorkspaceEditor.Get(), [WeakWorkspaceEditor](UAnimNextRigVMAssetEditorData* InEditorData)
				{
					if (TSharedPtr<Workspace::IWorkspaceEditor> WorkspaceEditor = WeakWorkspaceEditor.Pin())
					{
						WorkspaceEditor->RefreshDetails();
					}
				});

				EditorData->RigVMCompiledEvent.RemoveAll(&InContext.WorkspaceEditor.Get());
				EditorData->RigVMCompiledEvent.AddSPLambda(&InContext.WorkspaceEditor.Get(), [WeakWorkspaceEditor](UObject*, URigVM*, FRigVMExtendedExecuteContext&)
				{
					if (TSharedPtr<Workspace::IWorkspaceEditor> WorkspaceEditor = WeakWorkspaceEditor.Pin())
					{
						int32 NumEntries = FMessageLog("AnimNextCompilerResults").NumMessages(EMessageSeverity::Warning);
						if(NumEntries > 0)
						{
							WorkspaceEditor->GetTabManager()->TryInvokeTab(FTabId(CompilerResultsTabName));
						}
					}
				});
			}
		}

		if (WorkspaceMakeDocumentWidgetDelegate.IsBound())
		{
			return WorkspaceMakeDocumentWidgetDelegate.Execute(InContext);
		}

		return SNullWidget::NullWidget;
	});

	GraphDocumentArgs.OnGetDocumentBreadcrumbTrail = Workspace::FOnGetDocumentBreadcrumbTrail::CreateLambda([](const Workspace::FWorkspaceEditorContext& InContext, TArray<TSharedPtr<Workspace::FWorkspaceBreadcrumb>>& OutBreadcrumbs)
	{
		if (URigVMEdGraph* EdGraph = Cast<URigVMEdGraph>(InContext.Object))
		{
			if (UAnimNextRigVMAssetEditorData* EditorData = EdGraph->GetTypedOuter<UAnimNextRigVMAssetEditorData>())
			{
				// Iterate model tree, so we display all graph parents until we reach the Entry
				URigVMGraph* ModelGraph = EdGraph->GetModel();
				while (ModelGraph != nullptr)
				{
					URigVMEdGraph* RigVMEdGraph = Cast<URigVMEdGraph>(EditorData->GetEditorObjectForRigVMGraph(ModelGraph));

					if (RigVMEdGraph != nullptr && EditorData->GetLocalFunctionLibrary() != RigVMEdGraph->GetModel())
					{
						const TSharedPtr<Workspace::FWorkspaceBreadcrumb>& GraphCrumb = OutBreadcrumbs.Add_GetRef(MakeShared<Workspace::FWorkspaceBreadcrumb>());

						TWeakObjectPtr<URigVMEdGraph> WeakEdGraph = RigVMEdGraph;
						TWeakPtr<Workspace::IWorkspaceEditor> WeakWorkspaceEditor = InContext.WorkspaceEditor;
						TWeakObjectPtr<const UAnimNextRigVMAssetEditorData> WeakEditorData = EditorData;

						FText GraphName;
						if (WeakEdGraph.IsValid())
						{
							if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(WeakEdGraph->GetModel()->GetOuter()))
							{
								GraphName = FText::FromName(CollapseNode->GetFName());
							}
							else if (URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(WeakEdGraph->GetModel()->GetOuter()))
							{
								if (URigVMLibraryNode* ReferencedNode = Cast<URigVMLibraryNode>(FunctionReferenceNode->GetReferencedFunctionHeader().LibraryPointer.GetNodeSoftPath().ResolveObject()))
								{
									GraphName = FText::FromName(ReferencedNode->GetFName());
								}
							}

							if (GraphName.IsEmpty())
							{
								if (WeakEditorData.IsValid() && WeakEditorData->GetLocalFunctionLibrary() == WeakEdGraph->GetModel())
								{
									GraphName = UE::AnimNext::UncookedOnly::FUtils::GetFunctionLibraryDisplayName();
								}
								else if(UAnimNextRigVMAssetEntry* Entry = WeakEdGraph->GetTypedOuter<UAnimNextRigVMAssetEntry>())
								{
									GraphName = Entry->GetDisplayName();
								}
								else
								{
									GraphName = FText::FromName(WeakEdGraph->GetFName());
								}
							}
						}

						GraphCrumb->OnGetLabel = Workspace::FWorkspaceBreadcrumb::FOnGetBreadcrumbLabel::CreateLambda(
							[GraphName]
							{
								return GraphName;
							});
						GraphCrumb->CanSave = Workspace::FWorkspaceBreadcrumb::FCanSaveBreadcrumb::CreateLambda(
							[WeakEdGraph]
							{
								if (const URigVMEdGraph* Graph = WeakEdGraph.Get())
								{
									return Graph->GetPackage()->IsDirty();
								}
								return false;
							}
						);
						GraphCrumb->OnClicked = Workspace::FWorkspaceBreadcrumb::FOnBreadcrumbClicked::CreateLambda(
							[WeakEditorData, WeakEdGraph, WeakWorkspaceEditor]
							{
								if (const TSharedPtr<Workspace::IWorkspaceEditor> SharedWorkspaceEditor = WeakWorkspaceEditor.Pin())
								{
									SharedWorkspaceEditor->OpenObjects({ WeakEdGraph.Get() });
								}
							}
						);
						GraphCrumb->OnSave = Workspace::FWorkspaceBreadcrumb::FOnSaveBreadcrumb::CreateLambda(
							[WeakEdGraph]
							{
								if (const URigVMEdGraph* Graph = WeakEdGraph.Get())
								{
									FEditorFileUtils::PromptForCheckoutAndSave({ Graph->GetPackage() }, false, /*bPromptToSave=*/ false);
								}
							}
						);
					}

					ModelGraph = ModelGraph->GetTypedOuter<URigVMGraph>();
				}

				// Display the Asset
				if(UAnimNextRigVMAsset* OuterAsset = UncookedOnly::FUtils::GetAsset<UAnimNextRigVMAsset>(EditorData))
				{
					const TSharedPtr<Workspace::FWorkspaceBreadcrumb>& OuterGraphCrumb = OutBreadcrumbs.Add_GetRef(MakeShared<Workspace::FWorkspaceBreadcrumb>());
					TWeakObjectPtr<UAnimNextRigVMAsset> WeakOuterAsset = OuterAsset;
					TWeakPtr<Workspace::IWorkspaceEditor> WeakWorkspaceEditor = InContext.WorkspaceEditor;
					OuterGraphCrumb->OnGetLabel = Workspace::FWorkspaceBreadcrumb::FOnGetBreadcrumbLabel::CreateLambda([AssetName = OuterAsset->GetFName()]{ return FText::FromName(AssetName); });
					OuterGraphCrumb->OnClicked = Workspace::FWorkspaceBreadcrumb::FOnBreadcrumbClicked::CreateLambda(
						[WeakOuterAsset, WeakWorkspaceEditor]
						{
							if (const TSharedPtr<Workspace::IWorkspaceEditor> SharedWorkspaceEditor = WeakWorkspaceEditor.Pin())
							{
								SharedWorkspaceEditor->OpenObjects({WeakOuterAsset.Get()});
							}
						}
					);
					OuterGraphCrumb->CanSave = Workspace::FWorkspaceBreadcrumb::FCanSaveBreadcrumb::CreateLambda(
						[WeakOuterAsset]
						{
							if (UAnimNextRigVMAsset* Asset = WeakOuterAsset.Get())
							{
								return Asset->GetPackage()->IsDirty();
							}

							return false;
						}
					);
					OuterGraphCrumb->OnSave = Workspace::FWorkspaceBreadcrumb::FOnSaveBreadcrumb::CreateLambda(
						[WeakOuterAsset]
							{
								if (UAnimNextRigVMAsset* Asset = WeakOuterAsset.Get())
								{
									FEditorFileUtils::PromptForCheckoutAndSave({Asset->GetPackage()}, false, /*bPromptToSave=*/ false);
								}
							}
						);
				}
			}
		}
	});

	WorkspaceEditorModule.RegisterObjectDocumentType(FTopLevelAssetPath(TEXT("/Script/AnimNextUncookedOnly.AnimNextEdGraph")), GraphDocumentArgs);
}

void FAnimNextEditorModule::UnregisterWorkspaceDocumentTypes()
{
	if(FModuleManager::Get().IsModuleLoaded("WorkspaceEditor"))
	{
		Workspace::IWorkspaceEditorModule& WorkspaceEditorModule = FModuleManager::LoadModuleChecked<Workspace::IWorkspaceEditorModule>("WorkspaceEditor");
		WorkspaceEditorModule.UnregisterObjectDocumentType(FTopLevelAssetPath(TEXT("/Script/AnimNext.AnimNextModule")));
		WorkspaceEditorModule.UnregisterObjectDocumentType(FTopLevelAssetPath(TEXT("/Script/AnimNext.AnimNextAnimationGraph")));
		WorkspaceEditorModule.UnregisterObjectDocumentType(FTopLevelAssetPath(TEXT("/Script/AnimNextUncookedOnly.AnimNextEdGraph")));
	}
}

}

IMPLEMENT_MODULE(UE::AnimNext::Editor::FAnimNextEditorModule, AnimNextEditor);

#undef LOCTEXT_NAMESPACE