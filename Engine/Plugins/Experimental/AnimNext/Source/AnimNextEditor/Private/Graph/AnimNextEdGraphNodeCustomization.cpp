// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/AnimNextEdGraphNodeCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "AnimNextEdGraphNode.h"
#include "Editor/RigVMGraphDetailCustomization.h"
#include "Graph/RigDecorator_AnimNextCppTrait.h"
#include "Graph/TraitEditorTabSummoner.h"
#include "Logging/LogScopedVerbosityOverride.h"
#include "InstancedPropertyBagStructureDataProvider.h"
#include "RigVMModel/Nodes/RigVMLibraryNode.h"
#include "RigVMModel/Nodes/RigVMAggregateNode.h"
#include "RigVMModel/RigVMController.h"
#include "RigVMModel/RigVMFunctionLibrary.h"
#include "IWorkspaceEditor.h"
#include "STraitEditorView.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "EdGraphNodeCustomization"

namespace UE::AnimNext::Editor
{

FAnimNextEdGraphNodeCustomization::FAnimNextEdGraphNodeCustomization(const TWeakPtr<UE::Workspace::IWorkspaceEditor>& InWorkspaceEditorWeak)
	: WorkspaceEditorWeak(InWorkspaceEditorWeak)
{
}

void FAnimNextEdGraphNodeCustomization::PendingDelete()
{
	CategoryDetailsData.Reset();
	RigVMGraphDetailCustomizationImpl.Reset();

	if (TSharedPtr<UE::Workspace::IWorkspaceEditor> WorkspaceEditor = WorkspaceEditorWeak.Pin())
	{
		if (TSharedPtr<SDockTab> DockTab = WorkspaceEditor->GetTabManager()->FindExistingLiveTab(FTabId(UE::AnimNext::Editor::TraitEditorTabName)))
		{
			if (TSharedPtr<STraitEditorView> TraitEditorView = StaticCastSharedPtr<STraitEditorView>(DockTab->GetContent().ToSharedPtr()))
			{
				TraitEditorView->SetTraitData(FTraitStackData());
			}
		}
	}
}

void FAnimNextEdGraphNodeCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	RigVMGraphDetailCustomizationImpl.Reset();

	if (Objects.IsEmpty())
	{
		return;
	}

	CustomizeObjects(DetailBuilder, Objects);
}

void FAnimNextEdGraphNodeCustomization::CustomizeObjects(IDetailLayoutBuilder& DetailBuilder, const TArray<TWeakObjectPtr<UObject>>& Objects)
{
	const int32 NumNodes = Objects.Num();
	for (int i = 0; i < NumNodes; i++)
	{
		if (UAnimNextEdGraphNode* EdGraphNode = Cast<UAnimNextEdGraphNode>(Objects[i].Get()))
		{
			if (EdGraphNode->IsTraitStack())
			{
				RigVMGraphDetailCustomizationImpl.Reset();
				GenerateTraitData(EdGraphNode, CategoryDetailsData);
			}
			else 
			{
				if (URigVMNode* ModelNode = EdGraphNode->GetModelNode())
				{
					bool bIsFunction = false;
					bool bIsCollapseNode = false;
					bool bIsAggregate = false;

					if (URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(ModelNode))
					{
						if (URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(ModelNode))
						{
							if (URigVMLibraryNode* ReferencedNode = Cast<URigVMLibraryNode>(FunctionReferenceNode->GetReferencedFunctionHeader().LibraryPointer.GetNodeSoftPath().ResolveObject()))
							{
								LibraryNode = ReferencedNode;
								bIsFunction = true;
							}
						}
						else if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(ModelNode))
						{
							bIsCollapseNode = true;
						}
						bIsAggregate = LibraryNode->IsA<URigVMAggregateNode>();

						if (bIsFunction || bIsCollapseNode || bIsAggregate)
						{
							if (NumNodes > 1) // no multiselection of function / collapse nodes
							{
								continue;
							}

							TWeakPtr<FRigVMEditor> WeakEditor; // passing an empty RigVMEditor to FRigVMCollapseGraphLayout
							IRigVMClientHost* RigVMClientHost = EdGraphNode->GetController()->GetClientHost();
							check(RigVMClientHost);


							RigVMGraphDetailCustomizationImpl = MakeShared<FRigVMGraphDetailCustomizationImpl>();
							RigVMGraphDetailCustomizationImpl->CustomizeDetails(DetailBuilder
								, LibraryNode->GetContainedGraph()
								, RigVMClientHost->GetController(LibraryNode->GetContainedGraph())
								, RigVMClientHost
								, WeakEditor);
						}
					}
				}

				GenerateRigVMData(EdGraphNode, CategoryDetailsData);
			}
		}
	}

	for (TSharedPtr<FCategoryDetailsData>& DetailsData : CategoryDetailsData)
	{
		PopulateCategory(DetailBuilder, DetailsData);
	}

	// Pass the TraitStack EdGraphNode to the TraitEditor (or nullptr if we select other node type, like RigVM)
	if (Objects.Num() == 1)
	{
		if (UAnimNextEdGraphNode* EdGraphNode = Cast<UAnimNextEdGraphNode>(Objects[0].Get()))
		{
			if (TSharedPtr<UE::Workspace::IWorkspaceEditor> WorkspaceEditor = WorkspaceEditorWeak.Pin())
			{
				if (TSharedPtr<SDockTab> DockTab = WorkspaceEditor->GetTabManager()->FindExistingLiveTab(FTabId(UE::AnimNext::Editor::TraitEditorTabName)))
				{
					if (TSharedPtr<STraitEditorView> TraitEditorView = StaticCastSharedPtr<STraitEditorView>(DockTab->GetContent().ToSharedPtr()))
					{
						TraitEditorView->SetTraitData(EdGraphNode->IsTraitStack() ? FTraitStackData(EdGraphNode) : FTraitStackData());
					}
				}
			}
		}
	}
}

void FAnimNextEdGraphNodeCustomization::GenerateTraitData(UAnimNextEdGraphNode* EdGraphNode, TArray<TSharedPtr<FCategoryDetailsData>>& CategoryDetailsData)
{
	if (URigVMNode* ModelNode = EdGraphNode->GetModelNode())
	{
		// Obtain the pins from the stack
		const TArray<URigVMPin*> TraitPins = ModelNode->GetTraitPins();
		if (TraitPins.Num() > 0)
		{
			// For each Trait (represented as a pin in the node)
			for (const URigVMPin* TraitPin : TraitPins)
			{
				if (TraitPin->IsExecuteContext())
				{
					continue;
				}

				// Create a temporary trait instance, in order to get the correct TraitSharedDataStruct
				if (TSharedPtr<FStructOnScope> ScopedTrait = ModelNode->GetTraitInstance(TraitPin->GetFName()))
				{
					// Create a scoped struct with the Trait Shared Instance Data and store it for later use
					// The idea is creating one template per Trait type, but pass all the selected instances
					// So the multiselection works
					const FRigVMTrait* Trait = (FRigVMTrait*)ScopedTrait->GetStructMemory();

					// Import the default pin values into the Trait Shared Instance Data
					FTraitStackDetailsData* TraitData = nullptr;
					if (UScriptStruct* TraitSharedInstanceData = Trait->GetTraitSharedDataStruct())
					{
						const TSharedPtr<FCategoryDetailsData>* TraitDataPtr = CategoryDetailsData.FindByPredicate([TraitSharedInstanceData, TraitPin](const TSharedPtr<FCategoryDetailsData>& InItem)
							{
								return InItem->Type == FCategoryDetailsData::EType::TraitStack && InItem->Name == TraitPin->GetFName();
							});

						if (TraitDataPtr == nullptr)
						{
							TraitDataPtr = &CategoryDetailsData.Add_GetRef(MakeShared<FTraitStackDetailsData>(TraitPin->GetFName()));
						}

						TraitData = static_cast<FTraitStackDetailsData*>(TraitDataPtr->Get());

						// Store EdGraphNode and ScopedTraitData, as we will need that later to transfer data if the user makes modifications to the Traits in the details panel
						TraitData->EdGraphNodes.Add(EdGraphNode);
						const int32 ScopedDataIndex = TraitData->ScopedSharedDataInstances.Add(MakeShared<FStructOnScope>(TraitSharedInstanceData));

						// Fill the scoped data with the pin data
						FRigVMPinDefaultValueImportErrorContext ErrorPipe(ELogVerbosity::Verbose);
						LOG_SCOPE_VERBOSITY_OVERRIDE(LogExec, ErrorPipe.GetMaxVerbosity());
						const FString DefaultValue = TraitPin->GetDefaultValue();
						TraitSharedInstanceData->ImportText(*DefaultValue, TraitData->ScopedSharedDataInstances[ScopedDataIndex]->GetStructMemory(), nullptr, PPF_SerializedAsImportText, &ErrorPipe, TraitSharedInstanceData->GetName());
					}
				}
			}
		}
	}
}

void FAnimNextEdGraphNodeCustomization::GenerateRigVMData(UAnimNextEdGraphNode* EdGraphNode, TArray<TSharedPtr<FCategoryDetailsData>>& CategoryDetailsData)
{
	if (URigVMNode* ModelNode = EdGraphNode->GetModelNode())
	{
		// For nodes that aren't Traits, we display the pins as properties
		const TArray<URigVMPin*> ModelPins = ModelNode->GetPins();
		TArray<TWeakObjectPtr<URigVMPin>> PinsToDisplay;
		PinsToDisplay.Reserve(ModelPins.Num());

		TArray<FString> ModelPinPaths;

		// First, obtain the pins to display
		for (URigVMPin* Pin : ModelPins)
		{
			if (Pin->IsExecuteContext())
			{
				continue;
			}

			const ERigVMPinDirection PinDirection = Pin->GetDirection();
			if (PinDirection != ERigVMPinDirection::Hidden && (PinDirection == ERigVMPinDirection::IO || PinDirection == ERigVMPinDirection::Input))
			{
				PinsToDisplay.Add(Pin);
				ModelPinPaths.Add(Pin->GetPinPath());
			}
		}

		if (PinsToDisplay.Num() > 0)
		{
			const FName NodeName = *EdGraphNode->GetNodeTitle(ENodeTitleType::ListView).ToString(); // TODO : check if this is a good name to use, can't sue GetFName as it comes with instance postfix

			TSharedPtr<FCategoryDetailsData>* RigVMDataPtr = CategoryDetailsData.FindByPredicate([&NodeName](const TSharedPtr<FCategoryDetailsData>& InItem)
				{
					return InItem->Type == FCategoryDetailsData::EType::RigVMNode && InItem->Name == NodeName;
				});

			FRigVMNodeDetailsData* RigVMData = RigVMDataPtr  ? static_cast<FRigVMNodeDetailsData*>(RigVMDataPtr->Get()) : nullptr;
			if (RigVMData == nullptr)
			{
				RigVMDataPtr = &CategoryDetailsData.Add_GetRef(MakeShared<FRigVMNodeDetailsData>(NodeName));
				RigVMData = static_cast<FRigVMNodeDetailsData*>(RigVMDataPtr->Get());

				// Store the model Pin Names that will be shown (only when we create the Data, in case of multiselection)
				for (const TWeakObjectPtr<URigVMPin>& Pin : PinsToDisplay)
				{
					RigVMData->ModelPinsNamesToDisplay.Add(Pin->GetFName());
				}
			}

			// Store EdGraphNode and generated Memory, as we will need that later to transfer data if the user makes modifications in the details panel
			RigVMData->EdGraphNodes.Add(EdGraphNode);
			const int32 MemoryStorageIndex = RigVMData->MemoryStorages.Add(MakeShared<FRigVMMemoryStorageStruct>());

			// Store the Model Pin Paths, needed later to update the value of the correct model pin
			TArray<FString>& PinPaths = RigVMData->ModelPinPaths.Emplace_GetRef();
			Swap(PinPaths, ModelPinPaths);

			// Then, create a custom property bag to store the data, initializing the properties in the struct with the pin default values
			GenerateMemoryStorage(PinsToDisplay, *RigVMData->MemoryStorages[MemoryStorageIndex].Get());
		}
	}
}

void FAnimNextEdGraphNodeCustomization::PopulateCategory(IDetailLayoutBuilder& DetailBuilder, const TSharedPtr<FCategoryDetailsData> &CategoryDetailsData)
{
	switch (CategoryDetailsData->Type)
	{
		case FRigVMNodeDetailsData::EType::TraitStack:
		{
			PopulateCategory(DetailBuilder, StaticCastSharedPtr<FTraitStackDetailsData>(CategoryDetailsData));
			break;
		}
		case FRigVMNodeDetailsData::EType::RigVMNode:
		{
			PopulateCategory(DetailBuilder, StaticCastSharedPtr<FRigVMNodeDetailsData>(CategoryDetailsData));
			break;
		}
		default:
			break;
	}
}

void FAnimNextEdGraphNodeCustomization::PopulateCategory(IDetailLayoutBuilder& DetailBuilder, const TSharedPtr<FTraitStackDetailsData>& TraitData)
{
	if (TraitData == nullptr)
	{
		return;
	}

	check(TraitData->ScopedSharedDataInstances.Num() > 0);
	check(TraitData->ScopedSharedDataInstances.Num() == TraitData->EdGraphNodes.Num());

	// Create a category with the DisplayName of the Trait Shared Data
	const FString TraitDisplayName = *TraitData->ScopedSharedDataInstances[0]->GetStruct()->GetDisplayNameText().ToString();
	const FName CategoryName = (TraitData->EdGraphNodes.Num() == 1)
		? *TraitDisplayName
		: *FString::Printf(TEXT("%s (%d)"), *TraitDisplayName, TraitData->EdGraphNodes.Num());

	IDetailCategoryBuilder& ParameterCategory = DetailBuilder.EditCategory(CategoryName, FText::GetEmpty(), ECategoryPriority::Important);
	
	FAddPropertyParams AddPropertyParams;
	AddPropertyParams.CreateCategoryNodes(true);
	AddPropertyParams.HideRootObjectNode(true);

	IDetailPropertyRow* DetailPropertyRow = ParameterCategory.AddExternalStructureProperty(MakeShared<FStructOnScopeStructureDataProvider>(TraitData->ScopedSharedDataInstances), NAME_None, EPropertyLocation::Default, AddPropertyParams);
	TSharedPtr<IPropertyHandle> PropertyHandle = DetailPropertyRow->GetPropertyHandle();
	if (!PropertyHandle.IsValid())
	{
		return;
	}

	TraitData->RootPropertyHandle = PropertyHandle;

	auto VisitChildren = [](const TSharedRef<IPropertyHandle>& InPropertyHandle, TFunctionRef<void(const TSharedRef<IPropertyHandle>&)> InPredicate)
	{
		auto VisitChildrenRecursive = [](const TSharedRef<IPropertyHandle>& InPropertyHandle, auto& InVisitChildren, TFunctionRef<void(const TSharedRef<IPropertyHandle>&)> InPredicate)
		{
			uint32 NumChildHandles = 0;
			if(InPropertyHandle->GetNumChildren(NumChildHandles) != FPropertyAccess::Success)
			{
				return;
			}

			for(uint32 ChildHandleIndex = 0; ChildHandleIndex < NumChildHandles; ++ChildHandleIndex)
			{
				TSharedPtr<IPropertyHandle> Handle = InPropertyHandle->GetChildHandle(ChildHandleIndex);
				if(Handle.IsValid())
				{
					InPredicate(Handle.ToSharedRef());
					InVisitChildren(Handle.ToSharedRef(), InVisitChildren, InPredicate);
				}
			}
		};

		VisitChildrenRecursive(InPropertyHandle, VisitChildrenRecursive, InPredicate);
	};

	VisitChildren(PropertyHandle.ToSharedRef(), [&TraitData](const TSharedRef<IPropertyHandle>& InPropertyHandle)
	{
		TraitData->PropertyHandles.Add(InPropertyHandle);

		const TWeakPtr<FTraitStackDetailsData> WeakTraitData = TraitData.ToWeakPtr();
		const TWeakPtr<IPropertyHandle> WeakPropertyHandle = InPropertyHandle.ToWeakPtr();
		InPropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([WeakTraitData, WeakPropertyHandle]()
		{
			TSharedPtr<IPropertyHandle> PinnedPropertyHandle = WeakPropertyHandle.Pin(); 
			if(!PinnedPropertyHandle.IsValid())
			{
				return;
			}

			TSharedPtr<FTraitStackDetailsData> PinnedTraitData = WeakTraitData.Pin(); 
			if(!PinnedTraitData.IsValid())
			{
				return;
			}

			// Avoid VM recompilation for each Set Default Value
			URigVMController* Controller = PinnedTraitData->EdGraphNodes[0]->GetController();
			FRigVMControllerCompileBracketScope CompileScope(Controller);

			PinnedPropertyHandle->EnumerateConstRawData([&PinnedTraitData, &PinnedPropertyHandle, Controller](const void* InRawData, const int32 InDataIndex, const int32 InNumDatas)
			{
				check(InNumDatas == PinnedTraitData->ScopedSharedDataInstances.Num());
				check(InNumDatas == PinnedTraitData->EdGraphNodes.Num());
				check(PinnedTraitData->ScopedSharedDataInstances.IsValidIndex(InDataIndex));

				TWeakObjectPtr<UAnimNextEdGraphNode>& EdGraphNode = PinnedTraitData->EdGraphNodes[InDataIndex];
				if (!EdGraphNode.IsValid())
				{
					return true;
				}

				const FProperty* Property = PinnedPropertyHandle->GetProperty();
				const FString ValueStr = FRigVMStruct::ExportToFullyQualifiedText(Property, (uint8*)InRawData, true);

				// Transform property path into pin path
				FString PropertyPath(PinnedPropertyHandle->GetPropertyPath());

				// Replace delimiter
				FString PinPath = PropertyPath.Replace(TEXT("->"), TEXT("."));
				// Replace array element delimiter
				PinPath = PinPath.Replace(TEXT("["), TEXT("."));
				PinPath = PinPath.Replace(TEXT("]."), TEXT("."));
				PinPath = PinPath.Replace(TEXT("]"), TEXT(""));
				// Replace struct root with trait root
				const FString RootPropertyPath(PinnedTraitData->RootPropertyHandle->GetPropertyPath());
				PinPath = PinPath.Replace(*RootPropertyPath, *PinnedTraitData->Name.ToString());
				// Prefix node path
				const FString NodePath = EdGraphNode->GetModelNodePath();
				PinPath = NodePath + TEXT(".") + PinPath;
				Controller->SetPinDefaultValue(PinPath, ValueStr);

				return true;
			});
		}));
	});
}

void FAnimNextEdGraphNodeCustomization::PopulateCategory(IDetailLayoutBuilder& DetailBuilder, const TSharedPtr<FRigVMNodeDetailsData>& RigVMTypeData)
{
	if (RigVMTypeData == nullptr)
	{
		return;
	}

	check(RigVMTypeData->MemoryStorages.Num() > 0);
	check(RigVMTypeData->MemoryStorages.Num() == RigVMTypeData->EdGraphNodes.Num());

	// Create a category with the NodeTitle
	const FName CategoryName = (RigVMTypeData->EdGraphNodes.Num() == 1)
		? RigVMTypeData->Name
		: *FString::Printf(TEXT("%s (% d)"), *RigVMTypeData->Name.ToString(), RigVMTypeData->EdGraphNodes.Num());

	IDetailCategoryBuilder& ParameterCategory = DetailBuilder.EditCategory(CategoryName, FText::GetEmpty(), ECategoryPriority::Default);

	for (const FName& TemplateModelPinName : RigVMTypeData->ModelPinsNamesToDisplay)
	{
		FAddPropertyParams AddPropertyParams;
		IDetailPropertyRow* DetailPropertyRow = ParameterCategory.AddExternalStructureProperty(MakeShared<TInstancedPropertyBagStructureDataProvider<FRigVMMemoryStorageStruct>>(RigVMTypeData->MemoryStorages), TemplateModelPinName, EPropertyLocation::Default, AddPropertyParams);

		if (TSharedPtr<IPropertyHandle> Handle = DetailPropertyRow->GetPropertyHandle(); Handle.IsValid())
		{
			const TWeakPtr<FRigVMNodeDetailsData> RigVMTypeDataWeak = RigVMTypeData.ToWeakPtr();

			const auto UpdatePinDefaultValue = [RigVMTypeDataWeak, TemplateModelPinName](const FPropertyChangedEvent& InEvent)
				{
					if (const TSharedPtr<FRigVMNodeDetailsData> RigVMTypeData = RigVMTypeDataWeak.Pin())
					{
						// Avoid VM recompilation for each Set Default Value
						FRigVMControllerCompileBracketScope CompileScope(RigVMTypeData->EdGraphNodes[0]->GetController());

						const int32 NumInstances = RigVMTypeData->MemoryStorages.Num();
						for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; InstanceIndex++)
						{
							TWeakObjectPtr<UAnimNextEdGraphNode> EdGraphNode = RigVMTypeData->EdGraphNodes[InstanceIndex];
							if (EdGraphNode.IsValid())
							{
								const FString ValueStr = RigVMTypeData->MemoryStorages[InstanceIndex].Get()->GetDataAsStringByName(TemplateModelPinName);

								const TArray<FString>& ModelPinPaths = RigVMTypeData->ModelPinPaths[InstanceIndex];
								for (const FString& PinPath : ModelPinPaths)
								{
									const FString ModelPinName = FString::Printf(TEXT(".%s"), *TemplateModelPinName.ToString());
									if (PinPath.EndsWith(ModelPinName, ESearchCase::CaseSensitive))
									{
										EdGraphNode->GetController()->SetPinDefaultValue(PinPath, ValueStr);
										break;
									}
								}
							}
						}
					}
				};

			Handle->SetOnPropertyValueChangedWithData(TDelegate<void(const FPropertyChangedEvent&)>::CreateLambda(UpdatePinDefaultValue));
			Handle->SetOnChildPropertyValueChangedWithData(TDelegate<void(const FPropertyChangedEvent&)>::CreateLambda(UpdatePinDefaultValue));
		}
	}
}

void FAnimNextEdGraphNodeCustomization::GenerateMemoryStorage(const TArray<TWeakObjectPtr<URigVMPin>> & ModelPinsToDisplay, FRigVMMemoryStorageStruct& MemoryStorage)
{
	TArray<FRigVMPropertyDescription> PropertyDescriptions;
	PropertyDescriptions.Reserve(ModelPinsToDisplay.Num());

	for (const TWeakObjectPtr<URigVMPin>& ModelPin : ModelPinsToDisplay)
	{
		if (ModelPin.IsValid())
		{
			FRigVMPropertyDescription& PropertyDesc = PropertyDescriptions.AddDefaulted_GetRef();

			PropertyDesc.Name = ModelPin->GetFName();
			PropertyDesc.Property = nullptr;
			PropertyDesc.CPPType = ModelPin->GetCPPType();
			PropertyDesc.CPPTypeObject = ModelPin->GetCPPTypeObject();
			if (ModelPin->IsArray())
			{
				PropertyDesc.Containers.Add(EPinContainerType::Array);
			}
			PropertyDesc.DefaultValue = ModelPin->GetDefaultValue();
		}
	}

	MemoryStorage.AddProperties(PropertyDescriptions);
}

} // end namespace UE::AnimNext::Editor

#undef LOCTEXT_NAMESPACE
