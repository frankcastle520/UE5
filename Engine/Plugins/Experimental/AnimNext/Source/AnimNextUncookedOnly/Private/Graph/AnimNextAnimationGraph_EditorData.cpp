// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/AnimNextAnimationGraph_EditorData.h"

#include "UncookedOnlyUtils.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Curves/CurveFloat.h"
#include "Module/AnimNextModule.h"
#include "Entries/AnimNextAnimationGraphEntry.h"
#include "Entries/AnimNextDataInterfaceEntry.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "Entries/AnimNextEventGraphEntry.h"
#include "Entries/AnimNextVariableEntry.h"
#include "Graph/AnimNextAnimationGraphSchema.h"
#include "Graph/RigDecorator_AnimNextCppTrait.h"
#include "Graph/RigUnit_AnimNextBeginExecution.h"
#include "Graph/RigUnit_AnimNextShimRoot.h"
#include "Graph/RigUnit_AnimNextTraitStack.h"
#include "TraitCore/NodeTemplateBuilder.h"
#include "TraitCore/TraitRegistry.h"
#include "TraitCore/TraitWriter.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "UObject/LinkerLoad.h"

namespace UE::AnimNext::UncookedOnly::Private
{
	// Represents a trait entry on a node
	struct FTraitEntryMapping
	{
		// The RigVM node that hosts this RigVM decorator
		const URigVMNode* DecoratorStackNode = nullptr;

		// The RigVM decorator pin on our host node
		const URigVMPin* DecoratorEntryPin = nullptr;

		// The AnimNext trait
		const FTrait* Trait = nullptr;

		// A map from latent property names to their corresponding RigVM memory handle index
		TMap<FName, uint16> LatentPropertyNameToIndexMap;

		FTraitEntryMapping(const URigVMNode* InDecoratorStackNode, const URigVMPin* InDecoratorEntryPin, const FTrait* InTrait)
			: DecoratorStackNode(InDecoratorStackNode)
			, DecoratorEntryPin(InDecoratorEntryPin)
			, Trait(InTrait)
		{}
	};

	// Represents a node that contains a trait list
	struct FTraitStackMapping
	{
		// The RigVM node that hosts the RigVM decorators
		const URigVMNode* DecoratorStackNode = nullptr;

		// The trait list on this node
		TArray<FTraitEntryMapping> TraitEntries;

		// The node handle assigned to this RigVM node
		FNodeHandle TraitStackNodeHandle;

		explicit FTraitStackMapping(const URigVMNode* InDecoratorStackNode)
			: DecoratorStackNode(InDecoratorStackNode)
		{}
	};

	struct FTraitGraph
	{
		FName EntryPoint;
		URigVMNode* RootNode;
		TArray<FTraitStackMapping> TraitStackNodes;

		explicit FTraitGraph(const UAnimNextAnimationGraph* InAnimationGraph, URigVMNode* InRootNode)
			: RootNode(InRootNode)
		{
			EntryPoint = FName(InRootNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_AnimNextGraphRoot, EntryPoint))->GetDefaultValue());
		}
	};

	template<typename TraitAction>
	void ForEachTraitInStack(const URigVMNode* DecoratorStackNode, const TraitAction& Action)
	{
		const TArray<URigVMPin*>& Pins = DecoratorStackNode->GetPins();
		for (URigVMPin* Pin : Pins)
		{
			if (!Pin->IsTraitPin())
			{
				continue;	// Not a decorator pin
			}

			if (Pin->GetScriptStruct() == FRigDecorator_AnimNextCppDecorator::StaticStruct())
			{
				TSharedPtr<FStructOnScope> DecoratorScope = Pin->GetTraitInstance();
				FRigDecorator_AnimNextCppDecorator* VMDecorator = (FRigDecorator_AnimNextCppDecorator*)DecoratorScope->GetStructMemory();

				if (const FTrait* Trait = VMDecorator->GetTrait())
				{
					Action(DecoratorStackNode, Pin, Trait);
				}
			}
		}
	}

	TArray<FTraitUID> GetTraitUIDs(const URigVMNode* DecoratorStackNode)
	{
		TArray<FTraitUID> Traits;

		ForEachTraitInStack(DecoratorStackNode,
			[&Traits](const URigVMNode* DecoratorStackNode, const URigVMPin* DecoratorPin, const FTrait* Trait)
			{
				Traits.Add(Trait->GetTraitUID());
			});

		return Traits;
	}

	FNodeHandle RegisterTraitNodeTemplate(FTraitWriter& TraitWriter, const URigVMNode* DecoratorStackNode)
	{
		const TArray<FTraitUID> TraitUIDs = GetTraitUIDs(DecoratorStackNode);

		TArray<uint8> NodeTemplateBuffer;
		const FNodeTemplate* NodeTemplate = FNodeTemplateBuilder::BuildNodeTemplate(TraitUIDs, NodeTemplateBuffer);

		return TraitWriter.RegisterNode(*NodeTemplate);
	}

	FString GetTraitProperty(const FTraitStackMapping& TraitStack, uint32 TraitIndex, FName PropertyName, const TArray<FTraitStackMapping>& TraitStackNodes)
	{
		const TArray<URigVMPin*>& Pins = TraitStack.TraitEntries[TraitIndex].DecoratorEntryPin->GetSubPins();
		for (const URigVMPin* Pin : Pins)
		{
			if (Pin->GetDirection() != ERigVMPinDirection::Input)
			{
				continue;	// We only look for input pins
			}

			if (Pin->GetFName() == PropertyName)
			{
				if (Pin->GetCPPTypeObject() == FAnimNextTraitHandle::StaticStruct())
				{
					// Trait handle pins don't have a value, just an optional link
					const TArray<URigVMLink*>& PinLinks = Pin->GetLinks();
					if (!PinLinks.IsEmpty())
					{
						// Something is connected to us, find the corresponding node handle so that we can encode it as our property value
						check(PinLinks.Num() == 1);

						const URigVMNode* SourceNode = PinLinks[0]->GetSourceNode();

						FNodeHandle SourceNodeHandle;
						int32 SourceTraitIndex = INDEX_NONE;

						const FTraitStackMapping* SourceTraitStack = TraitStackNodes.FindByPredicate([SourceNode](const FTraitStackMapping& Mapping) { return Mapping.DecoratorStackNode == SourceNode; });
						if (SourceTraitStack != nullptr)
						{
							SourceNodeHandle = SourceTraitStack->TraitStackNodeHandle;

							// If the source pin is null, we are a node where the result pin lives on the stack node instead of a decorator sub-pin
							// If this is the case, we bind to the first trait index since we only allowed a single base trait per stack
							// Otherwise we lookup the trait index we are linked to
							const URigVMPin* SourceDecoratorPin = PinLinks[0]->GetSourcePin()->GetParentPin();
							SourceTraitIndex = SourceDecoratorPin != nullptr ? SourceTraitStack->DecoratorStackNode->GetTraitPins().IndexOfByKey(SourceDecoratorPin) : 0;
						}

						if (SourceNodeHandle.IsValid())
						{
							check(SourceTraitIndex != INDEX_NONE);

							const FAnimNextTraitHandle TraitHandle(SourceNodeHandle, SourceTraitIndex);
							const FAnimNextTraitHandle DefaultTraitHandle;

							// We need an instance of a trait handle property to be able to serialize it into text, grab it from the root
							const FProperty* Property = FRigUnit_AnimNextGraphRoot::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_AnimNextGraphRoot, Result));

							FString PropertyValue;
							Property->ExportText_Direct(PropertyValue, &TraitHandle, &DefaultTraitHandle, nullptr, PPF_SerializedAsImportText);

							return PropertyValue;
						}
					}

					// This handle pin isn't connected
					return FString();
				}

				// A regular property pin
				return Pin->GetDefaultValue();
			}
		}

		// Unknown property
		return FString();
	}

	uint16 GetTraitLatentPropertyIndex(const FTraitStackMapping& TraitStack, uint32 TraitIndex, FName PropertyName)
	{
		const FTraitEntryMapping& Entry = TraitStack.TraitEntries[TraitIndex];
		if (const uint16* RigVMIndex = Entry.LatentPropertyNameToIndexMap.Find(PropertyName))
		{
			return *RigVMIndex;
		}

		return MAX_uint16;
	}

	void WriteTraitProperties(FTraitWriter& TraitWriter, const FTraitStackMapping& Mapping, const TArray<FTraitStackMapping>& TraitStackNodes)
	{
		TraitWriter.WriteNode(Mapping.TraitStackNodeHandle,
			[&Mapping, &TraitStackNodes](uint32 TraitIndex, FName PropertyName)
			{
				return GetTraitProperty(Mapping, TraitIndex, PropertyName, TraitStackNodes);
			},
			[&Mapping](uint32 TraitIndex, FName PropertyName)
			{
				return GetTraitLatentPropertyIndex(Mapping, TraitIndex, PropertyName);
			});
	}

	URigVMUnitNode* FindRootNode(const TArray<URigVMNode*>& VMNodes)
	{
		for (URigVMNode* VMNode : VMNodes)
		{
			if (URigVMUnitNode* VMUnitNode = Cast<URigVMUnitNode>(VMNode))
			{
				const UScriptStruct* ScriptStruct = VMUnitNode->GetScriptStruct();
				if (ScriptStruct == FRigUnit_AnimNextGraphRoot::StaticStruct())
				{
					return VMUnitNode;
				}
			}
		}

		return nullptr;
	}

	void AddMissingInputLinks(const URigVMPin* DecoratorPin, URigVMController* VMController)
	{
		const TArray<URigVMPin*>& Pins = DecoratorPin->GetSubPins();
		for (URigVMPin* Pin : Pins)
		{
			const ERigVMPinDirection PinDirection = Pin->GetDirection();
			if (PinDirection != ERigVMPinDirection::Input && PinDirection != ERigVMPinDirection::Hidden)
			{
				continue;	// We only look for hidden or input pins
			}

			if (Pin->GetCPPTypeObject() != FAnimNextTraitHandle::StaticStruct())
			{
				continue;	// We only look for trait handle pins
			}

			const TArray<URigVMLink*>& PinLinks = Pin->GetLinks();
			if (!PinLinks.IsEmpty())
			{
				continue;	// This pin already has a link, all good
			}

			// Add a dummy node that will output a reference pose to ensure every link is valid.
			// RigVM doesn't let us link two decorators on a same node together or linking a child back to a parent
			// as this would create a cycle in the RigVM graph. The AnimNext graph traits do support it
			// and so perhaps we could have a merging pass later on to remove useless dummy nodes like this.

			URigVMUnitNode* VMReferencePoseNode = VMController->AddUnitNode(FRigUnit_AnimNextTraitStack::StaticStruct(), FRigVMStruct::ExecuteName, FVector2D(0.0f, 0.0f), FString(), false);
			check(VMReferencePoseNode != nullptr);

			const UScriptStruct* CppDecoratorStruct = FRigDecorator_AnimNextCppDecorator::StaticStruct();

			FString DefaultValue;
			{
				const UE::AnimNext::FTraitUID ReferencePoseTraitUID(0x7508ab89);	// Trait header is private, reference by UID directly
				const FTrait* Trait = FTraitRegistry::Get().Find(ReferencePoseTraitUID);
				check(Trait != nullptr);

				const FRigDecorator_AnimNextCppDecorator DefaultCppDecoratorStructInstance;
				FRigDecorator_AnimNextCppDecorator CppDecoratorStructInstance;
				CppDecoratorStructInstance.DecoratorSharedDataStruct = Trait->GetTraitSharedDataStruct();

				const FProperty* Prop = FAnimNextCppDecoratorWrapper::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(FAnimNextCppDecoratorWrapper, CppDecorator));
				check(Prop != nullptr);

				Prop->ExportText_Direct(DefaultValue, &CppDecoratorStructInstance, &DefaultCppDecoratorStructInstance, nullptr, PPF_SerializedAsImportText);
			}

			const FName ReferencePoseDecoratorName = VMController->AddTrait(VMReferencePoseNode->GetFName(), *CppDecoratorStruct->GetPathName(), TEXT("ReferencePose"), DefaultValue, INDEX_NONE, false, false);
			check(!ReferencePoseDecoratorName.IsNone());

			URigVMPin* OutputPin = VMReferencePoseNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_AnimNextTraitStack, Result));
			check(OutputPin != nullptr);

			ensure(VMController->AddLink(OutputPin, Pin, false));
		}
	}

	void AddMissingInputLinks(const URigVMGraph* VMGraph, URigVMController* VMController)
	{
		const TArray<URigVMNode*> VMNodes = VMGraph->GetNodes();	// Copy since we might add new nodes
		for (URigVMNode* VMNode : VMNodes)
		{
			if (const URigVMUnitNode* VMUnitNode = Cast<URigVMUnitNode>(VMNode))
			{
				const UScriptStruct* ScriptStruct = VMUnitNode->GetScriptStruct();
				if (ScriptStruct != FRigUnit_AnimNextTraitStack::StaticStruct())
				{
					continue;	// Skip non-trait nodes
				}

				ForEachTraitInStack(VMNode,
					[VMController](const URigVMNode* DecoratorStackNode, const URigVMPin* DecoratorPin, const FTrait* Trait)
					{
						AddMissingInputLinks(DecoratorPin, VMController);
					});
			}
		}
	}

	FTraitGraph CollectGraphInfo(const UAnimNextAnimationGraph* InAnimationGraph, const URigVMGraph* VMGraph, URigVMController* VMController)
	{
		const TArray<URigVMNode*>& VMNodes = VMGraph->GetNodes();
		URigVMUnitNode* VMRootNode = FindRootNode(VMNodes);

		if (VMRootNode == nullptr)
		{
			// Root node wasn't found, add it, we'll need it to compile
			VMRootNode = VMController->AddUnitNode(FRigUnit_AnimNextGraphRoot::StaticStruct(), FRigUnit_AnimNextGraphRoot::EventName, FVector2D(0.0f, 0.0f), FString(), false);
		}

		// Make sure we don't have empty input pins
		AddMissingInputLinks(VMGraph, VMController);

		FTraitGraph TraitGraph(InAnimationGraph, VMRootNode);

		TArray<const URigVMNode*> NodesToVisit;
		NodesToVisit.Add(VMRootNode);

		while (NodesToVisit.Num() != 0)
		{
			const URigVMNode* VMNode = NodesToVisit[0];
			NodesToVisit.RemoveAt(0);

			if (const URigVMUnitNode* VMUnitNode = Cast<URigVMUnitNode>(VMNode))
			{
				const UScriptStruct* ScriptStruct = VMUnitNode->GetScriptStruct();
				if (ScriptStruct == FRigUnit_AnimNextTraitStack::StaticStruct())
				{
					FTraitStackMapping Mapping(VMNode);
					ForEachTraitInStack(VMNode,
						[&Mapping](const URigVMNode* DecoratorStackNode, const URigVMPin* DecoratorPin, const FTrait* Trait)
						{
							Mapping.TraitEntries.Add(FTraitEntryMapping(DecoratorStackNode, DecoratorPin, Trait));
						});

					TraitGraph.TraitStackNodes.Add(MoveTemp(Mapping));
				}
			}

			const TArray<URigVMNode*> SourceNodes = VMNode->GetLinkedSourceNodes();
			NodesToVisit.Append(SourceNodes);
		}

		if (TraitGraph.TraitStackNodes.IsEmpty())
		{
			// If the graph is empty, add a dummy node that just pushes a reference pose
			URigVMUnitNode* VMNode = VMController->AddUnitNode(FRigUnit_AnimNextTraitStack::StaticStruct(), FRigVMStruct::ExecuteName, FVector2D(0.0f, 0.0f), FString(), false);

			UAnimNextController* AnimNextController = CastChecked<UAnimNextController>(VMController);
			constexpr UE::AnimNext::FTraitUID ReferencePoseTraitUID(0x7508ab89); // Trait header is private, reference by UID directly
			const FName RigVMTraitName =  AnimNextController->AddTraitByName(VMNode->GetFName(), *UE::AnimNext::FTraitRegistry::Get().Find(ReferencePoseTraitUID)->GetTraitName(), INDEX_NONE, TEXT(""), false);
		
			check(RigVMTraitName != NAME_None);

			FTraitStackMapping Mapping(VMNode);
			ForEachTraitInStack(VMNode,
				[&Mapping](const URigVMNode* DecoratorStackNode, const URigVMPin* DecoratorPin, const FTrait* Trait)
				{
					Mapping.TraitEntries.Add(FTraitEntryMapping(DecoratorStackNode, DecoratorPin, Trait));
				});

			TraitGraph.TraitStackNodes.Add(MoveTemp(Mapping));
		}

		return TraitGraph;
	}

	void CollectLatentPins(TArray<FTraitStackMapping>& TraitStackNodes, FRigVMPinInfoArray& OutLatentPins, TMap<FName, URigVMPin*>& OutLatentPinMapping)
	{
		for (FTraitStackMapping& TraitStack : TraitStackNodes)
		{
			for (FTraitEntryMapping& TraitEntry : TraitStack.TraitEntries)
			{
				for (URigVMPin* Pin : TraitEntry.DecoratorEntryPin->GetSubPins())
				{
					if (Pin->IsLazy() && !Pin->GetLinks().IsEmpty())
					{
						// This pin has something linked to it, it is a latent pin
						check(OutLatentPins.Num() < ((1 << 16) - 1));	// We reserve MAX_uint16 as an invalid value and we must fit on 15 bits when packed
						TraitEntry.LatentPropertyNameToIndexMap.Add(Pin->GetFName(), (uint16)OutLatentPins.Num());

						const FName LatentPinName(TEXT("LatentPin"), OutLatentPins.Num());	// Create unique latent pin names

						FRigVMPinInfo PinInfo;
						PinInfo.Name = LatentPinName;
						PinInfo.TypeIndex = Pin->GetTypeIndex();

						// All our programmatic pins are lazy inputs
						PinInfo.Direction = ERigVMPinDirection::Input;
						PinInfo.bIsLazy = true;

						OutLatentPins.Pins.Emplace(PinInfo);

						const TArray<URigVMLink*>& PinLinks = Pin->GetLinks();
						check(PinLinks.Num() == 1);

						OutLatentPinMapping.Add(LatentPinName, PinLinks[0]->GetSourcePin());
					}
				}
			}
		}
	}

	FAnimNextGraphEvaluatorExecuteDefinition GetGraphEvaluatorExecuteMethod(const FRigVMPinInfoArray& LatentPins)
	{
		const uint32 LatentPinListHash = GetTypeHash(LatentPins);
		if (const FAnimNextGraphEvaluatorExecuteDefinition* ExecuteDefinition = FRigUnit_AnimNextGraphEvaluator::FindExecuteMethod(LatentPinListHash))
		{
			return *ExecuteDefinition;
		}

		const FRigVMRegistry& Registry = FRigVMRegistry::Get();

		// Generate a new method for this argument list
		FAnimNextGraphEvaluatorExecuteDefinition ExecuteDefinition;
		ExecuteDefinition.Hash = LatentPinListHash;
		ExecuteDefinition.MethodName = FString::Printf(TEXT("Execute_%X"), LatentPinListHash);
		ExecuteDefinition.Arguments.Reserve(LatentPins.Num());

		for (const FRigVMPinInfo& Pin : LatentPins)
		{
			const FRigVMTemplateArgumentType& TypeArg = Registry.GetType(Pin.TypeIndex);

			FAnimNextGraphEvaluatorExecuteArgument Argument;
			Argument.Name = Pin.Name.ToString();
			Argument.CPPType = TypeArg.CPPType.ToString();

			ExecuteDefinition.Arguments.Add(Argument);
		}

		FRigUnit_AnimNextGraphEvaluator::RegisterExecuteMethod(ExecuteDefinition);

		return ExecuteDefinition;
	}
}

void UAnimNextAnimationGraph_EditorData::RecompileVM()
{
	using namespace UE::AnimNext::UncookedOnly;

	if (bIsCompiling)
	{
		return;
	}

	TGuardValue<bool> CompilingGuard(bIsCompiling, true);

	UAnimNextAnimationGraph* AnimationGraph = FUtils::GetAsset<UAnimNextAnimationGraph>(this);

	// Before we re-compile a graph, we need to release and live instances since we need the metadata we are about to replace
	// to call trait destructors etc
	AnimationGraph->FreezeGraphInstances();

	CachedExports.Reset(); // asset variables and other tags will be updated at the end by AssetRegistry->AssetUpdateTags

	bErrorsDuringCompilation = false;

	RigGraphDisplaySettings.MinMicroSeconds = RigGraphDisplaySettings.LastMinMicroSeconds = DBL_MAX;
	RigGraphDisplaySettings.MaxMicroSeconds = RigGraphDisplaySettings.LastMaxMicroSeconds = (double)INDEX_NONE;

	TArray<URigVMGraph*> ProgrammaticGraphs;
	{
		TGuardValue<bool> ReentrantGuardSelf(bSuspendModelNotificationsForSelf, true);
		TGuardValue<bool> ReentrantGuardOthers(RigVMClient.bSuspendModelNotificationsForOthers, true);

		VMCompileSettings.SetExecuteContextStruct(FAnimNextExecuteContext::StaticStruct());
		FRigVMCompileSettings Settings = (bCompileInDebugMode) ? FRigVMCompileSettings::Fast(VMCompileSettings.GetExecuteContextStruct()) : VMCompileSettings;
		Settings.ASTSettings.bSetupTraits = false; // disable the default implementation of decorators for now

		FMessageLog("AnimNextCompilerResults").NewPage(FText::FromName(AnimationGraph->GetFName()));
		Settings.ASTSettings.ReportDelegate.BindLambda([](EMessageSeverity::Type InType, UObject* InObject, const FString& InString)
		{
			FMessageLog("AnimNextCompilerResults").Message(InType, FText::FromString(InString));
		});

		FUtils::RecreateVM(AnimationGraph);

		FUtils::CompileVariables(AnimationGraph);

		AnimationGraph->VMRuntimeSettings = VMRuntimeSettings;
		AnimationGraph->EntryPoints.Empty();
		AnimationGraph->ResolvedRootTraitHandles.Empty();
		AnimationGraph->ResolvedEntryPoints.Empty();
		AnimationGraph->ExecuteDefinition = FAnimNextGraphEvaluatorExecuteDefinition();
		AnimationGraph->SharedDataBuffer.Empty();
		AnimationGraph->GraphReferencedObjects.Empty();
		AnimationGraph->DefaultEntryPoint = NAME_None;

		FRigVMClient* VMClient = GetRigVMClient();

		GetProgrammaticGraphs(Settings, ProgrammaticGraphs);
		for(URigVMGraph* ProgrammaticGraph : ProgrammaticGraphs)
		{
			check(ProgrammaticGraph != nullptr);
		}
	
		TArray<URigVMGraph*> AllGraphs = VMClient->GetAllModels(false, false);
		AllGraphs.Append(ProgrammaticGraphs);

		if(AllGraphs.Num() == 0)
		{
			return;
		}

		TArray<URigVMGraph*> TempGraphs;
		for(const URigVMGraph* SourceGraph : AllGraphs)
		{
			// We use a temporary graph models to build our final graphs that we'll compile
			URigVMGraph* TempGraph = CastChecked<URigVMGraph>(StaticDuplicateObject(SourceGraph, GetTransientPackage(), NAME_None, RF_Transient));
			TempGraph->SetFlags(RF_Transient);
			TempGraphs.Add(TempGraph);
		}

		if(TempGraphs.Num() == 0)
		{
			return;
		}

		UAnimNextController* TempController = CastChecked<UAnimNextController>(VMClient->GetOrCreateController(TempGraphs[0]));

		UE::AnimNext::FTraitWriter TraitWriter;

		FRigVMPinInfoArray LatentPins;
		TMap<FName, URigVMPin*> LatentPinMapping;
		TArray<Private::FTraitGraph> TraitGraphs;

		// Build entry points and extract their required latent pins
		for(const URigVMGraph* TempGraph : TempGraphs)
		{
			if(TempGraph->GetSchemaClass() == UAnimNextAnimationGraphSchema::StaticClass())
			{
				// Gather our trait stacks
				Private::FTraitGraph& TraitGraph = TraitGraphs.Add_GetRef(Private::CollectGraphInfo(AnimationGraph, TempGraph, TempController->GetControllerForGraph(TempGraph)));
				check(!TraitGraph.TraitStackNodes.IsEmpty());

				FAnimNextGraphEntryPoint& EntryPoint = AnimationGraph->EntryPoints.AddDefaulted_GetRef();
				EntryPoint.EntryPointName = TraitGraph.EntryPoint;

				// Extract latent pins for this graph
				Private::CollectLatentPins(TraitGraph.TraitStackNodes, LatentPins, LatentPinMapping);

				// Iterate over every trait stack and register our node templates
				for (Private::FTraitStackMapping& NodeMapping : TraitGraph.TraitStackNodes)
				{
					NodeMapping.TraitStackNodeHandle = Private::RegisterTraitNodeTemplate(TraitWriter, NodeMapping.DecoratorStackNode);
				}

				// Find our root node handle, if we have any stack nodes, the first one is our root stack
				if (TraitGraph.TraitStackNodes.Num() != 0)
				{
					EntryPoint.RootTraitHandle = FAnimNextEntryPointHandle(TraitGraph.TraitStackNodes[0].TraitStackNodeHandle);
				}
			}
		}

		// Set default entry point
		if(AnimationGraph->EntryPoints.Num() > 0)
		{
			AnimationGraph->DefaultEntryPoint = AnimationGraph->EntryPoints[0].EntryPointName;
		}

		// Remove our old root nodes
		for (Private::FTraitGraph& TraitGraph : TraitGraphs)
		{
			URigVMController* GraphController = TempController->GetControllerForGraph(TraitGraph.RootNode->GetGraph());
			GraphController->RemoveNode(TraitGraph.RootNode, false, false);
		}

		if(LatentPins.Num() > 0)
		{
			// We need a unique method name to match our unique argument list
			AnimationGraph->ExecuteDefinition = Private::GetGraphEvaluatorExecuteMethod(LatentPins);

			// Add our runtime shim root node
			URigVMUnitNode* TempShimRootNode = TempController->AddUnitNode(FRigUnit_AnimNextShimRoot::StaticStruct(), FRigUnit_AnimNextShimRoot::EventName, FVector2D::ZeroVector, FString(), false);
			URigVMUnitNode* GraphEvaluatorNode = TempController->AddUnitNodeWithPins(FRigUnit_AnimNextGraphEvaluator::StaticStruct(), LatentPins, *AnimationGraph->ExecuteDefinition.MethodName, FVector2D::ZeroVector, FString(), false);

			// Link our shim and evaluator nodes together using the execution context
			TempController->AddLink(
				TempShimRootNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_AnimNextShimRoot, ExecuteContext)),
				GraphEvaluatorNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_AnimNextGraphEvaluator, ExecuteContext)),
				false);

			// Link our latent pins
			for (const FRigVMPinInfo& LatentPin : LatentPins)
			{
				TempController->AddLink(
					LatentPinMapping[LatentPin.Name],
					GraphEvaluatorNode->FindPin(LatentPin.Name.ToString()),
					false);
			}
		}

		// Write our node shared data
		TraitWriter.BeginNodeWriting();

		for(Private::FTraitGraph& TraitGraph : TraitGraphs)
		{
			for (const Private::FTraitStackMapping& NodeMapping : TraitGraph.TraitStackNodes)
			{
				Private::WriteTraitProperties(TraitWriter, NodeMapping, TraitGraph.TraitStackNodes);
			}
		}

		TraitWriter.EndNodeWriting();

		// Cache our compiled metadata
		AnimationGraph->SharedDataArchiveBuffer = TraitWriter.GetGraphSharedData();
		AnimationGraph->GraphReferencedObjects = TraitWriter.GetGraphReferencedObjects();

		// Populate our runtime metadata
		AnimationGraph->LoadFromArchiveBuffer(AnimationGraph->SharedDataArchiveBuffer);

		URigVMCompiler* Compiler = URigVMCompiler::StaticClass()->GetDefaultObject<URigVMCompiler>();
		Compiler->Compile(Settings, TempGraphs, TempController, AnimationGraph->VM, AnimationGraph->ExtendedExecuteContext, AnimationGraph->GetExternalVariables(), &PinToOperandMap);

		// Initialize right away, in packaged builds we initialize during PostLoad
		AnimationGraph->VM->Initialize(AnimationGraph->ExtendedExecuteContext);
		AnimationGraph->GenerateUserDefinedDependenciesData(AnimationGraph->ExtendedExecuteContext);

		// Notable difference with vanilla RigVM host behavior - we init the VM here at the moment as we only have one 'instance'
		AnimationGraph->InitializeVM(FRigUnit_AnimNextBeginExecution::EventName);

		if (bErrorsDuringCompilation)
		{
			if(Settings.SurpressErrors)
			{
				Settings.Reportf(EMessageSeverity::Info, AnimationGraph, TEXT("Compilation Errors may be suppressed for AnimNext asset: %s. See VM Compile Settings for more Details"), *AnimationGraph->GetName());
			}
		}

		bVMRecompilationRequired = false;
		if(AnimationGraph->VM)
		{
			RigVMCompiledEvent.Broadcast(AnimationGraph, AnimationGraph->VM, AnimationGraph->ExtendedExecuteContext);
		}

		for(URigVMGraph* TempGraph : TempGraphs)
		{
			VMClient->RemoveController(TempGraph);
		}

		// Now that the graph has been re-compiled, re-allocate the previous live instances
		AnimationGraph->ThawGraphInstances();
	}
		
#if WITH_EDITOR
	// Display programmatic graphs
	if(CVarDumpProgrammaticGraphs.GetValueOnGameThread())
	{
		FUtils::OpenProgrammaticGraphs(this, ProgrammaticGraphs);
	}
#endif


#if WITH_EDITOR
//	RefreshBreakpoints(EditorData);
#endif

	// Refresh CachedExports, also updates variables at UAnimNextRigVMAssetEditorData::GetAssetRegistryTags
	if (IAssetRegistry* AssetRegistry = IAssetRegistry::Get())
	{
		AssetRegistry->AssetUpdateTags(GetTypedOuter<UAnimNextAnimationGraph>(), EAssetRegistryTagsCaller::Fast);
	}
}

TConstArrayView<TSubclassOf<UAnimNextRigVMAssetEntry>> UAnimNextAnimationGraph_EditorData::GetEntryClasses() const
{
	static const TSubclassOf<UAnimNextRigVMAssetEntry> Classes[] =
	{
		UAnimNextAnimationGraphEntry::StaticClass(),
		UAnimNextEventGraphEntry::StaticClass(),	// TODO: remove when assets are reworked post-refactor
		UAnimNextVariableEntry::StaticClass(),
		UAnimNextDataInterfaceEntry::StaticClass(),
	};

	return Classes;
}

bool UAnimNextAnimationGraph_EditorData::CanAddNewEntry(TSubclassOf<UAnimNextRigVMAssetEntry> InClass) const
{
	// Prevent users adding more than one animation graph
	if(InClass == UAnimNextAnimationGraphEntry::StaticClass())
	{
		if(Entries.ContainsByPredicate([](UAnimNextRigVMAssetEntry* InEntry)
		{
			return InEntry->IsA<UAnimNextAnimationGraphEntry>();
		}))
		{
			return false;
		};
	}
	
	return true;
}