// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorGraphBuilder.h"

#include "Algo/AnyOf.h"
#include "Algo/Sort.h"
#include "Algo/Transform.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GraphEditor.h"
#include "GraphEditorSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Metasound.h"
#include "MetasoundAssetBase.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundDocumentBuilderRegistry.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundEditor.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphCommentNode.h"
#include "MetasoundEditorGraphInputNode.h"
#include "MetasoundEditorGraphMemberDefaults.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphSchema.h"
#include "MetasoundEditorGraphValidation.h"
#include "MetasoundEditorModule.h"
#include "MetasoundEditorSettings.h"
#include "MetasoundEditorSubsystem.h"
#include "MetasoundFactory.h"
#include "MetasoundFrontendDataTypeRegistry.h"
#include "MetasoundFrontendDocumentAccessPtr.h"
#include "MetasoundFrontendDocumentBuilder.h"
#include "MetasoundFrontendQuery.h"
#include "MetasoundFrontendQuerySteps.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundLiteral.h"
#include "MetasoundSettings.h"
#include "MetasoundTime.h"
#include "MetasoundUObjectRegistry.h"
#include "MetasoundVariableNodes.h"
#include "MetasoundVertex.h"
#include "MetasoundWaveTable.h"
#include "Modules/ModuleManager.h"
#include "NodeTemplates/MetasoundFrontendNodeTemplateInput.h"
#include "NodeTemplates/MetasoundFrontendNodeTemplateReroute.h"
#include "Templates/Tuple.h"
#include "Toolkits/ToolkitManager.h"
#include "WaveTable.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "MetaSoundEditor"


namespace Metasound
{
	namespace Editor
	{
		namespace GraphBuilderPrivate
		{
			template <
				typename TEdGraphNode,
				typename FInitMetaSoundNodeFunc = TFunctionRef<void(UMetasoundEditorGraph&, TEdGraphNode&)>>
			TEdGraphNode* AddNode(UObject& InMetaSound, FInitMetaSoundNodeFunc InitNodeFunc, bool bInSelectNewNode)
			{
				FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
				check(MetaSoundAsset);
				UEdGraph& Graph = MetaSoundAsset->GetGraphChecked();
				FGraphNodeCreator<TEdGraphNode> NodeCreator(Graph);
				if (TEdGraphNode* NewGraphNode = NodeCreator.CreateNode(bInSelectNewNode))
				{
					// Required to happen prior to caching title and syncing location in case underlying type requires
					// additional logic to initialize state in order to cache the title/sync the location
					UMetasoundEditorGraph* MetasoundGraph = CastChecked<UMetasoundEditorGraph>(&Graph);
					check(MetasoundGraph);
					InitNodeFunc(*MetasoundGraph, *NewGraphNode);

					NodeCreator.Finalize();

					NewGraphNode->CacheTitle();

					// Override finalized EdGraphNode ID if a location is found.
					constexpr bool bUpdateEditorNodeID = true;
					NewGraphNode->SyncLocationFromFrontendNode(bUpdateEditorNodeID);
					NewGraphNode->SyncCommentFromFrontendNode();

					return NewGraphNode;
				}

				return nullptr;
			}

			FName GenerateUniqueName(const TSet<FName>& InExistingNames, const FString& InBaseName)
			{
				int32 PostFixInt = 0;
				FString NewName = InBaseName;

				while (InExistingNames.Contains(*NewName))
				{
					PostFixInt++;
					NewName = FString::Format(TEXT("{0} {1}"), { InBaseName, PostFixInt });
				}

				return FName(*NewName);
			}

			void RecurseClearDocumentModified(FMetasoundAssetBase& InAssetBase)
			{
				using namespace Metasound::Frontend;

				InAssetBase.GetModifyContext().ClearDocumentModified();

				TArray<FMetasoundAssetBase*> References;
				ensureAlways(IMetaSoundAssetManager::GetChecked().TryLoadReferencedAssets(InAssetBase, References));
				for (FMetasoundAssetBase* Reference : References)
				{
					check(Reference);
					Reference->GetModifyContext().ClearDocumentModified();
					RecurseClearDocumentModified(*Reference);
				}
			};

			void SynchronizeGraphRecursively(const FMetaSoundFrontendDocumentBuilder& InBuilder, UMetasoundEditorGraph& OutGraph, bool bSkipIfModifyContextUnchanged)
			{
				using namespace Engine;
				using namespace Frontend;

				UObject& MetaSound = InBuilder.CastDocumentObjectChecked<UObject>();
				FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&MetaSound);
				check(MetaSoundAsset);

				// Synchronize referenced graphs first to ensure all editor data
				// is up-to-date prior to synchronizing this referencing graph.
				TArray<FMetasoundAssetBase*> References;
				ensureAlways(IMetaSoundAssetManager::GetChecked().TryLoadReferencedAssets(*MetaSoundAsset, References));
				for (FMetasoundAssetBase* Reference : References)
				{
					check(Reference);
					UObject* RefObject = Reference->GetOwningAsset();
					check(RefObject);
					FMetaSoundFrontendDocumentBuilder& RefBuilder = FDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(RefObject);

					UMetasoundEditorGraph* Graph = nullptr;
					FGraphBuilder::BindEditorGraph(RefBuilder, &Graph);
					SynchronizeGraphRecursively(RefBuilder, *Graph, bSkipIfModifyContextUnchanged);
				}

				if (bSkipIfModifyContextUnchanged && !MetaSoundAsset->GetConstModifyContext().GetDocumentModified())
				{
					return;
				}

				FGraphBuilder::SynchronizeComments(InBuilder, OutGraph);
				FGraphBuilder::SynchronizeGraphMembers(InBuilder, OutGraph);
				FGraphBuilder::SynchronizeOutputNodes(InBuilder, OutGraph);
				FGraphBuilder::SynchronizeNodes(InBuilder, OutGraph);
				FGraphBuilder::SynchronizeConnections(InBuilder, OutGraph);
			}
		} // namespace GraphBuilderPrivate

		// Categories corresponding with POD DataTypes
		const FName FGraphBuilder::PinCategoryObject = "object"; // Basket for all UObject proxy types (corresponds to multiple DataTypes)
		const FName FGraphBuilder::PinCategoryBoolean = GetMetasoundDataTypeName<bool>();
		const FName FGraphBuilder::PinCategoryFloat = GetMetasoundDataTypeName<float>();
		const FName FGraphBuilder::PinCategoryInt32 = GetMetasoundDataTypeName<int32>();
		const FName FGraphBuilder::PinCategoryString = GetMetasoundDataTypeName<FString>();

		// Categories corresponding with MetaSound DataTypes with custom visualization
		const FName FGraphBuilder::PinCategoryAudio = GetMetasoundDataTypeName<FAudioBuffer>();
		const FName FGraphBuilder::PinCategoryTime = GetMetasoundDataTypeName<FTime>();
		const FName FGraphBuilder::PinCategoryTimeArray = GetMetasoundDataTypeName<TArray<FTime>>();
		const FName FGraphBuilder::PinCategoryTrigger = GetMetasoundDataTypeName<FTrigger>();
		const FName FGraphBuilder::PinCategoryWaveTable = GetMetasoundDataTypeName<WaveTable::FWaveTable>();

		bool FGraphBuilder::IsPinCategoryMetaSoundCustomDataType(FName InPinCategoryName)
		{
			return InPinCategoryName == PinCategoryAudio
				|| InPinCategoryName == PinCategoryTime
				|| InPinCategoryName == PinCategoryTimeArray
				|| InPinCategoryName == PinCategoryTrigger
				|| InPinCategoryName == PinCategoryWaveTable;
		}

		bool FGraphBuilder::CanInspectPin(const UEdGraphPin* InPin)
		{
			// Can't inspect the value on an invalid pin object.
			if (!InPin || InPin->IsPendingKill())
			{
				return false;
			}

			// Can't inspect the value on an orphaned pin object.
			if (InPin->bOrphanedPin)
			{
				return false;
			}

			// Currently only inspection of connected pins is supported.
			if (InPin->LinkedTo.IsEmpty())
			{
				return false;
			}

			// Can't inspect the value on an unknown pin object or if the owning node is disabled.
			const UEdGraphNode* OwningNode = InPin->GetOwningNodeUnchecked();
			if (!OwningNode || !OwningNode->IsNodeEnabled())
			{
				return false;
			}

			TSharedPtr<FEditor> Editor = GetEditorForPin(*InPin);
			if (!Editor.IsValid())
			{
				return false;
			}

			if (!Editor->IsPlaying())
			{
				return false;
			}

			FName DataType;
			if (InPin->Direction == EGPD_Input)
			{
				Frontend::FConstInputHandle InputHandle = GetConstInputHandleFromPin(InPin);
				DataType = InputHandle->GetDataType();
			}
			else
			{
				Frontend::FConstOutputHandle OutputHandle = GetConstOutputHandleFromPin(InPin);
				DataType = OutputHandle->GetDataType();
			}

			const bool bIsSupportedType = DataType == GetMetasoundDataTypeName<float>()
				|| DataType == GetMetasoundDataTypeName<int32>()
				|| DataType == GetMetasoundDataTypeName<FString>()
				|| DataType == GetMetasoundDataTypeName<bool>()
				|| DataType == GetMetasoundDataTypeName<FAudioBuffer>();

			if (!bIsSupportedType)
			{
				return false;
			}

			const UEdGraphPin* ReroutedPin = FindReroutedOutputPin(InPin);
			if (ReroutedPin != InPin)
			{
				return false;
			}

			return true;
		}

		UMetasoundEditorGraphCommentNode* FGraphBuilder::CreateCommentNode(UObject& InMetaSound, bool bInSelectNewNode, FGuid InCommentID)
		{
			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);
			UEdGraph& Graph = MetaSoundAsset->GetGraphChecked();
			FGraphNodeCreator<UMetasoundEditorGraphCommentNode> NodeCreator(Graph);
			if (UMetasoundEditorGraphCommentNode* NewNode = NodeCreator.CreateNode(bInSelectNewNode))
			{
				NewNode->CommentID = InCommentID;
				NodeCreator.Finalize();
				return NewNode;
			}

			return nullptr;
		}

		FText FGraphBuilder::GetDisplayName(const FMetasoundFrontendClassMetadata& InClassMetadata, FName InNodeName, bool bInIncludeNamespace)
		{
			using namespace Frontend;

			FName Namespace;
			FName ParameterName;
			Audio::FParameterPath::SplitName(InNodeName, Namespace, ParameterName);

			FText DisplayName;
			auto GetAssetDisplayNameFromMetadata = [&DisplayName](const FMetasoundFrontendClassMetadata& Metadata)
			{
				DisplayName = Metadata.GetDisplayName();
				if (DisplayName.IsEmptyOrWhitespace())
				{
					const FNodeRegistryKey RegistryKey = FNodeRegistryKey(Metadata);
					bool bIsClassNative = FMetasoundFrontendRegistryContainer::Get()->IsNodeNative(RegistryKey);
					if (!bIsClassNative)
					{
						const FTopLevelAssetPath Path = IMetaSoundAssetManager::GetChecked().FindAssetPath(RegistryKey);
						if (Path.IsValid())
						{
							DisplayName = FText::FromName(Path.GetAssetName());
						}
					}
				}
			};

			// 1. Try to get display name from metadata or asset if one can be found from the asset manager
			GetAssetDisplayNameFromMetadata(InClassMetadata);

			// 2. If version is missing from the registry or from asset system, then this node
			// will not provide a useful DisplayName.  In that case, attempt to find the next highest
			// class & associated DisplayName.
			if (DisplayName.IsEmptyOrWhitespace())
			{
				FMetasoundFrontendClass ClassWithHighestVersion;
				if (ISearchEngine::Get().FindClassWithHighestVersion(InClassMetadata.GetClassName(), ClassWithHighestVersion))
				{
					GetAssetDisplayNameFromMetadata(ClassWithHighestVersion.Metadata);
				}
			}

			// 3. If that cannot be found, build a title from the cached node registry FName.
			if (DisplayName.IsEmptyOrWhitespace())
			{
				DisplayName = FText::FromString(ParameterName.ToString());
			}

			// 4. Tack on the namespace if requested
			if (bInIncludeNamespace)
			{
				if (!Namespace.IsNone())
				{
					return FText::Format(LOCTEXT("ClassMetadataDisplayNameWithNamespaceFormat", "{0} ({1})"), DisplayName, FText::FromName(Namespace));
				}
			}

			return DisplayName;
		}

		FText FGraphBuilder::GetDisplayName(const Frontend::INodeController& InFrontendNode, bool bInIncludeNamespace)
		{
			using namespace Frontend;

			FText DisplayName = InFrontendNode.GetDisplayName();
			if (!DisplayName.IsEmptyOrWhitespace())
			{
				return DisplayName;
			}

			return GetDisplayName(InFrontendNode.GetClassMetadata(), InFrontendNode.GetNodeName(), bInIncludeNamespace);
		}

		FText FGraphBuilder::GetDisplayName(const Frontend::IInputController& InFrontendInput)
		{
			FText DisplayName = InFrontendInput.GetDisplayName();
			if (DisplayName.IsEmptyOrWhitespace())
			{
				DisplayName = FText::FromName(InFrontendInput.GetName());
			}
			return DisplayName;
		}

		FText FGraphBuilder::GetDisplayName(const Frontend::IOutputController& InFrontendOutput)
		{
			FText DisplayName = InFrontendOutput.GetDisplayName();
			if (DisplayName.IsEmptyOrWhitespace())
			{
				DisplayName = FText::FromName(InFrontendOutput.GetName());
			}
			return DisplayName;
		}

		FText FGraphBuilder::GetDisplayName(const Frontend::IVariableController& InFrontendVariable, bool bInIncludeNamespace)
		{
			FText DisplayName = InFrontendVariable.GetDisplayName();
			if (DisplayName.IsEmptyOrWhitespace())
			{
				FName Namespace;
				FName ParameterName;
				Audio::FParameterPath::SplitName(InFrontendVariable.GetName(), Namespace, ParameterName);

				DisplayName = FText::FromName(ParameterName);
				if (bInIncludeNamespace && !Namespace.IsNone())
				{
					return FText::Format(LOCTEXT("ClassMetadataDisplayNameWithNamespaceFormat", "{0} ({1})"), DisplayName, FText::FromName(Namespace));
				}
			}

			return DisplayName;
		}

		FName FGraphBuilder::GetPinName(const Frontend::IOutputController& InFrontendOutput)
		{
			using namespace VariableNames; 

			Frontend::FConstNodeHandle OwningNode = InFrontendOutput.GetOwningNode();
			EMetasoundFrontendClassType OwningNodeClassType = OwningNode->GetClassMetadata().GetType();

			switch (OwningNodeClassType)
			{
				case EMetasoundFrontendClassType::Variable:
				case EMetasoundFrontendClassType::VariableAccessor:
				case EMetasoundFrontendClassType::VariableDeferredAccessor:
				case EMetasoundFrontendClassType::VariableMutator:
				{
					// All variables nodes use the same pin name for user-modifiable node
					// inputs and outputs and the editor does not display the pin's name. The
					// editor instead displays the variable's name in place of the pin name to
					// maintain a consistent look and behavior to input and output nodes.
					return METASOUND_GET_PARAM_NAME(OutputData);
				}
				case EMetasoundFrontendClassType::Input:
				case EMetasoundFrontendClassType::Output:
				{
					return OwningNode->GetNodeName();
				}

				case EMetasoundFrontendClassType::Template:
				default:
				{
					return InFrontendOutput.GetName();
				}
			}
		}

		FName FGraphBuilder::GetPinName(const Frontend::IInputController& InFrontendInput)
		{
			using namespace VariableNames;
			
			Frontend::FConstNodeHandle OwningNode = InFrontendInput.GetOwningNode();
			EMetasoundFrontendClassType OwningNodeClassType = OwningNode->GetClassMetadata().GetType();

			switch (OwningNodeClassType)
			{
				case EMetasoundFrontendClassType::Variable:
				case EMetasoundFrontendClassType::VariableAccessor:
				case EMetasoundFrontendClassType::VariableDeferredAccessor:
				case EMetasoundFrontendClassType::VariableMutator:
				{
					// All variables nodes use the same pin name for user-modifiable node
					// inputs and outputs and the editor does not display the pin's name. The
					// editor instead displays the variable's name in place of the pin name to
					// maintain a consistent look and behavior to input and output nodes.
					return METASOUND_GET_PARAM_NAME(InputData);
				}

				case EMetasoundFrontendClassType::Input:
				case EMetasoundFrontendClassType::Output:
				{
					return OwningNode->GetNodeName();
				}

				case EMetasoundFrontendClassType::Template:
				default:
				{
					return InFrontendInput.GetName();
				}
			}
		}

		UMetasoundEditorGraphExternalNode* FGraphBuilder::AddTemplateNode(UObject& InMetaSound, const FGuid& InNodeID, const FMetasoundFrontendClassMetadata& InMetadata, bool bInSelectNewNode)
		{
			const EMetasoundFrontendClassType ClassType = InMetadata.GetType();
			if (ensureMsgf(ClassType == EMetasoundFrontendClassType::Template, TEXT("Cannot call 'AddTemplateNode' with node of class type '%s'."), LexToString(ClassType)))
			{
				auto InitNodeFunc = [&InMetadata, &InNodeID](UMetasoundEditorGraph&, UMetasoundEditorGraphExternalNode& NewGraphNode)
				{
					NewGraphNode.NodeID = InNodeID;
					NewGraphNode.ClassName = InMetadata.GetClassName();
				};
				return GraphBuilderPrivate::AddNode<UMetasoundEditorGraphExternalNode>(InMetaSound, InitNodeFunc, bInSelectNewNode);
			}

			return nullptr;
		}

		UMetasoundEditorGraphExternalNode* FGraphBuilder::AddExternalNode(UObject& InMetaSound, const FGuid& InNodeID, const FMetasoundFrontendClassMetadata& InMetadata, bool bInSelectNewNode)
		{
			using namespace Frontend;

			const EMetasoundFrontendClassType ClassType = InMetadata.GetType();
			if (ensureMsgf(ClassType == EMetasoundFrontendClassType::External, TEXT("Cannot call 'AddExternalNode' with node of class type '%s'."), LexToString(ClassType)))
			{
				auto InitNodeFunc = [&InMetadata, &InNodeID](UMetasoundEditorGraph& MetasoundGraph, UMetasoundEditorGraphExternalNode& NewGraphNode)
				{
					const FNodeRegistryKey RegistryKey(InMetadata);
					NewGraphNode.bIsClassNative = FMetasoundFrontendRegistryContainer::Get()->IsNodeNative(RegistryKey);
					NewGraphNode.NodeID = InNodeID;
					NewGraphNode.ClassName = InMetadata.GetClassName();
				};

				return GraphBuilderPrivate::AddNode<UMetasoundEditorGraphExternalNode>(InMetaSound, InitNodeFunc, bInSelectNewNode);
			}

			return nullptr;
		}

		UMetasoundEditorGraphExternalNode* FGraphBuilder::AddExternalNode(UObject& InMetaSound, const FMetasoundFrontendClassMetadata& InMetadata, bool bInSelectNewNode)
		{
			using namespace Frontend;

			FMetaSoundFrontendDocumentBuilder& Builder = IDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(&InMetaSound);
			if (const FMetasoundFrontendNode* NewNode = Builder.AddNodeByClassName(InMetadata.GetClassName(), InMetadata.GetVersion().Major))
			{
				const FMetasoundFrontendClass* Dependency = Builder.FindDependency(NewNode->ClassID);
				if (ensure(Dependency))
				{
					FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
					check(MetaSoundAsset);
					MetaSoundAsset->GetModifyContext().AddNodeIDModified(NewNode->GetID());
					return AddExternalNode(InMetaSound, NewNode->GetID(), Dependency->Metadata, bInSelectNewNode);
				}
			}

			return nullptr;
		}

		Frontend::FNodeHandle FGraphBuilder::AddExternalNodeHandle(UObject& InMetaSound, const FMetasoundFrontendClassName& InClassName)
		{
			using namespace Frontend;

			FMetasoundFrontendClass FrontendClass;
			bool bDidFindClassWithName = ISearchEngine::Get().FindClassWithHighestVersion(InClassName, FrontendClass);
			if (ensure(bDidFindClassWithName))
			{
				FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
				check(MetaSoundAsset);
				return MetaSoundAsset->GetRootGraphHandle()->AddNode(FrontendClass.Metadata);
			}

			return INodeController::GetInvalidHandle();
		}

		UMetasoundEditorGraphVariableNode* FGraphBuilder::AddVariableNode(UObject& InMetaSound, const Frontend::FConstNodeHandle& InNodeHandle, bool bInSelectNewNode)
		{
			using namespace Frontend;

			const FMetasoundFrontendClassMetadata& Metadata = InNodeHandle->GetClassMetadata();
			EMetasoundFrontendClassType ClassType = Metadata.GetType();
			const bool bIsSupportedClassType = (ClassType == EMetasoundFrontendClassType::VariableAccessor) 
				|| (ClassType == EMetasoundFrontendClassType::VariableDeferredAccessor)
				|| (ClassType == EMetasoundFrontendClassType::VariableMutator);

			if (ensureMsgf(bIsSupportedClassType, TEXT("Cannot call 'AddVariableNode' with node of class type '%s'"), LexToString(ClassType)))
			{
				FConstVariableHandle FrontendVariable = InNodeHandle->GetOwningGraph()->FindVariableContainingNode(InNodeHandle->GetID());
				if (ensure(FrontendVariable->IsValid()))
				{
					auto InitNodeFunc = [&FrontendVariable, &InNodeHandle, &Metadata](UMetasoundEditorGraph& MetasoundGraph, UMetasoundEditorGraphVariableNode& NewGraphNode)
					{
						UMetasoundEditorGraphVariable* Variable = MetasoundGraph.FindOrAddVariable(FrontendVariable);
						if (ensure(Variable))
						{
							NewGraphNode.Variable = Variable;
							NewGraphNode.NodeID = InNodeHandle->GetID();
							NewGraphNode.ClassName = Metadata.GetClassName();
							NewGraphNode.ClassType = Metadata.GetType();
						}
					};

					return GraphBuilderPrivate::AddNode<UMetasoundEditorGraphVariableNode>(InMetaSound, InitNodeFunc, bInSelectNewNode);
				}
			}

			return nullptr;
		}

		UMetasoundEditorGraphInputNode* FGraphBuilder::AddInputNode(UObject& InMetaSound, const FGuid& InTemplateNodeID, bool bInSelectNewNode)
		{
			using namespace Frontend;

			using namespace Engine;
			using namespace Frontend;

			FMetaSoundFrontendDocumentBuilder& Builder = FDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(&InMetaSound);
			if (const FMetasoundFrontendNode* TemplateNode = Builder.FindNode(InTemplateNodeID))
			{
				if (const FMetasoundFrontendClass* Class = Builder.FindDependency(TemplateNode->ClassID))
				{
					const EMetasoundFrontendClassType& ClassType = Class->Metadata.GetType();
					if (ensureMsgf(ClassType == EMetasoundFrontendClassType::Template, TEXT("Cannot call 'AddInputNode' with node of class type '%s': Must be input template."), LexToString(ClassType)))
					{
						if (ensureMsgf(Class->Metadata.GetClassName() == FInputNodeTemplate::ClassName, TEXT("Cannot call 'AddInputNode with node handle that is not of class '%s'"), *FInputNodeTemplate::ClassName.ToString()))
						{
							const FGuid& TemplateNodeInputVertexID = TemplateNode->Interface.Inputs.Last().VertexID;
							auto InitNodeFunc = [&Builder, &TemplateNodeInputVertexID, &InTemplateNodeID](UMetasoundEditorGraph& MetasoundGraph, UMetasoundEditorGraphInputNode& NewGraphNode)
							{
								const FMetasoundFrontendNode* ConnectedInputNode = nullptr;
								Builder.FindNodeOutputConnectedToNodeInput(InTemplateNodeID, TemplateNodeInputVertexID, &ConnectedInputNode);
								if (ensureMsgf(ConnectedInputNode, TEXT("Failed to find required input connected to template node")))
								{
									UMetasoundEditorGraphInput* Input = MetasoundGraph.FindOrAddInput(ConnectedInputNode->GetID());
									if (ensure(Input))
									{
										NewGraphNode.Input = Input;
										NewGraphNode.NodeID = InTemplateNodeID;
									}
								}
							};

							return GraphBuilderPrivate::AddNode<UMetasoundEditorGraphInputNode>(InMetaSound, InitNodeFunc, bInSelectNewNode);
						}
					}
				}
			}

			return nullptr;
		}

		UMetasoundEditorGraphOutputNode* FGraphBuilder::AddOutputNode(UObject& InMetaSound, const FGuid& InNodeID, bool bInSelectNewNode)
		{
			using namespace Engine;
			using namespace Frontend;

			FMetaSoundFrontendDocumentBuilder& Builder = FDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(&InMetaSound);
			if (const FMetasoundFrontendNode* Node = Builder.FindNode(InNodeID))
			{
				if (const FMetasoundFrontendClass* Class = Builder.FindDependency(Node->ClassID))
				{
					const EMetasoundFrontendClassType& ClassType = Class->Metadata.GetType();
					if (ensureMsgf(ClassType == EMetasoundFrontendClassType::Output, TEXT("Cannot call 'AddOutputNode' with node of class type '%s'"), LexToString(ClassType)))
					{
						auto InitNodeFunc = [&InNodeID](UMetasoundEditorGraph& MetasoundGraph, UMetasoundEditorGraphOutputNode& NewGraphNode)
						{
							UMetasoundEditorGraphOutput* Output = MetasoundGraph.FindOrAddOutput(InNodeID);
							if (ensure(Output))
							{
								NewGraphNode.Output = Output;
							}
						};

						return GraphBuilderPrivate::AddNode<UMetasoundEditorGraphOutputNode>(InMetaSound, InitNodeFunc, bInSelectNewNode);
					}
				}
			}

			return nullptr;
		}

		FGraphValidationResults FGraphBuilder::ValidateGraph(UObject& InMetaSound)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::Editor::FGraphBuilder::ValidateGraph);

			using namespace Frontend;

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);

			// Validate referenced graphs first to ensure all editor data
			// is up-to-date prior to validating this referencing graph to
			// allow errors to bubble up.
			TArray<FMetasoundAssetBase*> References;
			ensureAlways(IMetaSoundAssetManager::GetChecked().TryLoadReferencedAssets(*MetaSoundAsset, References));
			for (FMetasoundAssetBase* Reference : References)
			{
				check(Reference);
				ValidateGraph(*Reference->GetOwningAsset());
			}

			FGraphValidationResults Results;
			UMetasoundEditorGraph& Graph = *CastChecked<UMetasoundEditorGraph>(&MetaSoundAsset->GetGraphChecked());
			Graph.ValidateInternal(Results);
			return Results;
		}

		UMetaSoundBuilderBase& FGraphBuilder::GetBuilderFromPinChecked(const UEdGraphPin& InPin)
		{
			const UMetasoundEditorGraphNode* Node = CastChecked<UMetasoundEditorGraphNode>(InPin.GetOwningNode());
			check(Node);
			return Node->GetBuilderChecked();
		}

		TArray<FString> FGraphBuilder::GetDataTypeNameCategories(const FName& InDataTypeName)
		{
			FString CategoryString = InDataTypeName.ToString();

			TArray<FString> Categories;
			CategoryString.ParseIntoArray(Categories, TEXT(":"));

			if (Categories.Num() > 0)
			{
				// Remove name
				Categories.RemoveAt(Categories.Num() - 1);
			}

			return Categories;
		}

		FName FGraphBuilder::GenerateUniqueNameByClassType(UObject& InMetaSound, EMetasoundFrontendClassType InClassType, const FString& InBaseName)
		{
			using namespace Engine;

			TSet<FName> ExistingNames;
			FMetaSoundFrontendDocumentBuilder& Builder = FDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(&InMetaSound);
			auto GetName = [&](const FMetasoundFrontendClass&, const FMetasoundFrontendNode& Node) { ExistingNames.Add(Node.Name); };
			Builder.IterateNodesByClassType(GetName, InClassType);

			return GraphBuilderPrivate::GenerateUniqueName(ExistingNames, InBaseName);
		}

		TSharedPtr<FEditor> FGraphBuilder::GetEditorForMetasound(const UObject& Metasound)
		{
			// TODO: FToolkitManager is deprecated. Replace with UAssetEditorSubsystem.
			if (TSharedPtr<IToolkit> FoundAssetEditor = FToolkitManager::Get().FindEditorForAsset(&Metasound))
			{
				if (FEditor::EditorName == FoundAssetEditor->GetToolkitFName())
				{
					return StaticCastSharedPtr<FEditor, IToolkit>(FoundAssetEditor);
				}
			}

			return { };
		}

		TSharedPtr<FEditor> FGraphBuilder::GetEditorForGraph(const UEdGraph& EdGraph)
		{
			if (const UMetasoundEditorGraph* MetasoundGraph = Cast<const UMetasoundEditorGraph>(&EdGraph))
			{
				return GetEditorForMetasound(MetasoundGraph->GetMetasoundChecked());
			}

			return { };
		}

		TSharedPtr<FEditor> FGraphBuilder::GetEditorForNode(const UEdGraphNode& InEdNode)
		{
			if (const UMetasoundEditorGraph* Graph = Cast<UMetasoundEditorGraph>(InEdNode.GetGraph()))
			{
				return FGraphBuilder::GetEditorForGraph(*Graph);
			}

			return { };
		}

		TSharedPtr<FEditor> FGraphBuilder::GetEditorForPin(const UEdGraphPin& InEdPin)
		{
			if (const UMetasoundEditorGraphNode* Node = Cast<UMetasoundEditorGraphNode>(InEdPin.GetOwningNode()))
			{
				return GetEditorForNode(*Node);
			}

			return { };
		}

		FLinearColor FGraphBuilder::GetPinCategoryColor(const FEdGraphPinType& PinType)
		{
			const UMetasoundEditorSettings* Settings = GetDefault<UMetasoundEditorSettings>();
			check(Settings);

			if (PinType.PinCategory == PinCategoryAudio)
			{
				return Settings->AudioPinTypeColor;
			}

			if (PinType.PinCategory == PinCategoryBoolean)
			{
				return Settings->BooleanPinTypeColor;
			}

			if (PinType.PinCategory == PinCategoryFloat)
			{
				return Settings->FloatPinTypeColor;
			}

			if (PinType.PinCategory == PinCategoryInt32)
			{
				return Settings->IntPinTypeColor;
			}

			if (PinType.PinCategory == PinCategoryObject)
			{
				return Settings->ObjectPinTypeColor;
			}

			if (PinType.PinCategory == PinCategoryString)
			{
				return Settings->StringPinTypeColor;
			}

			if (PinType.PinCategory == PinCategoryTime || PinType.PinCategory == PinCategoryTimeArray)
			{
				return Settings->TimePinTypeColor;
			}

			if (PinType.PinCategory == PinCategoryTrigger)
			{
				return Settings->TriggerPinTypeColor;
			}

			if (PinType.PinCategory == PinCategoryWaveTable)
			{
				return Settings->WaveTablePinTypeColor;
			}

			// custom colors
			if (const FLinearColor* Color = Settings->CustomPinTypeColors.Find(PinType.PinCategory))
			{
				return *Color;
			}

			return Settings->DefaultPinTypeColor;
		}

		Frontend::FInputHandle FGraphBuilder::GetInputHandleFromPin(const UEdGraphPin* InPin)
		{
			using namespace Frontend;
			using namespace VariableNames;

			if (InPin && ensure(InPin->Direction == EGPD_Input))
			{
				if (UMetasoundEditorGraphVariableNode* EdVariableNode = Cast<UMetasoundEditorGraphVariableNode>(InPin->GetOwningNode()))
				{
					// UEdGraphPins on variable nodes use the variable's name for display
					// purposes instead of the underlying vertex's name. The frontend vertices
					// of a variable node have consistent names no matter what the 
					// variable is named.
					return EdVariableNode->GetNodeHandle()->GetInputWithVertexName(METASOUND_GET_PARAM_NAME(InputData));
				}
				else if (UMetasoundEditorGraphNode* EdNode = CastChecked<UMetasoundEditorGraphNode>(InPin->GetOwningNode()))
				{
					return EdNode->GetNodeHandle()->GetInputWithVertexName(InPin->GetFName());
				}
			}

			return IInputController::GetInvalidHandle();
		}

		Frontend::FConstInputHandle FGraphBuilder::GetConstInputHandleFromPin(const UEdGraphPin* InPin)
		{
			using namespace Frontend;
			using namespace VariableNames;

			if (InPin && ensure(InPin->Direction == EGPD_Input))
			{
				if (const UMetasoundEditorGraphVariableNode* EdVariableNode = Cast<UMetasoundEditorGraphVariableNode>(InPin->GetOwningNode()))
				{
					// UEdGraphPins on variable nodes use the variable's name for display
					// purposes instead of the underlying vertex's name. The frontend vertices
					// of a variable node have consistent names no matter what the 
					// variable is named.
					return EdVariableNode->GetConstNodeHandle()->GetConstInputWithVertexName(METASOUND_GET_PARAM_NAME(InputData));
				}
				else if (const UMetasoundEditorGraphNode* EdNode = CastChecked<UMetasoundEditorGraphNode>(InPin->GetOwningNode()))
				{
					return EdNode->GetConstNodeHandle()->GetConstInputWithVertexName(InPin->GetFName());
				}
			}

			return IInputController::GetInvalidHandle();
		}

		FName FGraphBuilder::GetPinDataType(const UEdGraphPin* InPin)
		{
			using namespace Frontend;

			if (InPin)
			{
				if (InPin->Direction == EGPD_Input)
				{
					FConstInputHandle InputHandle = GetConstInputHandleFromPin(InPin);
					return InputHandle->GetDataType();
				}
				else // EGPD_Output
				{
					FConstOutputHandle OutputHandle = GetConstOutputHandleFromPin(InPin);
					return OutputHandle->GetDataType();
				}
			}

			return { };
		}

		FMetasoundFrontendVertexHandle FGraphBuilder::GetPinVertexHandle(const FMetaSoundFrontendDocumentBuilder& InBuilder, const UEdGraphPin* InPin)
		{
			using namespace VariableNames;

			if (!InPin)
			{
				return { };
			}

			const UMetasoundEditorGraphNode* OwningNode = CastChecked<UMetasoundEditorGraphNode>(InPin->GetOwningNode());

			const FGuid NodeID = OwningNode->GetNodeID();
			const FMetasoundFrontendNode* Node = InBuilder.FindNode(NodeID);
			if (!Node)
			{
				return { };
			}

			const FMetasoundFrontendClass* Class = InBuilder.FindDependency(Node->ClassID);
			if (!Class)
			{
				return { };
			}

			const FMetasoundFrontendVertex* Vertex = nullptr;
			switch (Class->Metadata.GetType())
			{
				case EMetasoundFrontendClassType::Variable:
				case EMetasoundFrontendClassType::VariableAccessor:
				case EMetasoundFrontendClassType::VariableDeferredAccessor:
				case EMetasoundFrontendClassType::VariableMutator:
				{
					// All variables nodes use the same pin name for user-modifiable node
					// inputs and outputs and the editor does not display the pin's name. The
					// editor instead displays the variable's name in place of the pin name to
					// maintain a consistent look and behavior to input and output nodes.
					Vertex = InPin->Direction == EGPD_Input
						? InBuilder.FindNodeInput(NodeID, METASOUND_GET_PARAM_NAME(InputData))
						: InBuilder.FindNodeOutput(NodeID, METASOUND_GET_PARAM_NAME(OutputData));
					break;
				}

				case EMetasoundFrontendClassType::Input:
				{
					Vertex = &Node->Interface.Outputs.Last();
					break;
				}

				case EMetasoundFrontendClassType::Output:
				{
					Vertex = &Node->Interface.Inputs.Last();
					break;
				}

				default:
				{
					Vertex = InPin->Direction == EGPD_Input
						? InBuilder.FindNodeInput(NodeID, InPin->GetFName())
						: InBuilder.FindNodeOutput(NodeID, InPin->GetFName());
				}
			}

			FMetasoundFrontendVertexHandle VertexHandle { NodeID };
			if (Vertex)
			{
				VertexHandle.VertexID = Vertex->VertexID;
			}
			return VertexHandle;
		}

		const FMetasoundFrontendVertex* FGraphBuilder::GetPinVertex(const FMetaSoundFrontendDocumentBuilder& InBuilder, const UEdGraphPin* InPin, const FMetasoundFrontendNode** Node)
		{
			using namespace VariableNames;

			if (Node)
			{
				*Node = nullptr;
			}

			if (!InPin)
			{
				return nullptr;
			}

			const UMetasoundEditorGraphNode* OwningNode = CastChecked<UMetasoundEditorGraphNode>(InPin->GetOwningNode());

			const FGuid NodeID = OwningNode->GetNodeID();
			const FMetasoundFrontendNode* FoundNode = InBuilder.FindNode(NodeID);
			if (!FoundNode)
			{
				return nullptr;
			}

			if (Node)
			{
				*Node = FoundNode;
			}

			const FMetasoundFrontendClass* Class = InBuilder.FindDependency(FoundNode->ClassID);
			if (!Class)
			{
				return nullptr;
			}

			switch (Class->Metadata.GetType())
			{
				case EMetasoundFrontendClassType::Variable:
				case EMetasoundFrontendClassType::VariableAccessor:
				case EMetasoundFrontendClassType::VariableDeferredAccessor:
				case EMetasoundFrontendClassType::VariableMutator:
				{
					// All variables nodes use the same pin name for user-modifiable node
					// inputs and outputs and the editor does not display the pin's name. The
					// editor instead displays the variable's name in place of the pin name to
					// maintain a consistent look and behavior to input and output nodes.
					return InPin->Direction == EGPD_Input
						? InBuilder.FindNodeInput(NodeID, METASOUND_GET_PARAM_NAME(InputData))
						: InBuilder.FindNodeOutput(NodeID, METASOUND_GET_PARAM_NAME(OutputData));
				}
				case EMetasoundFrontendClassType::Input:
				{
					return &FoundNode->Interface.Inputs.Last();
				}
				case EMetasoundFrontendClassType::Output:
				{
					return &FoundNode->Interface.Outputs.Last();
				}

				default:
				{
					return InPin->Direction == EGPD_Input
						? InBuilder.FindNodeInput(NodeID, InPin->GetFName())
						: InBuilder.FindNodeOutput(NodeID, InPin->GetFName());
				}
			}
		}

		Frontend::FOutputHandle FGraphBuilder::GetOutputHandleFromPin(const UEdGraphPin* InPin)
		{
			using namespace Frontend;
			using namespace VariableNames; 

			if (InPin && ensure(InPin->Direction == EGPD_Output))
			{
				if (UMetasoundEditorGraphVariableNode* EdVariableNode = Cast<UMetasoundEditorGraphVariableNode>(InPin->GetOwningNode()))
				{
					// UEdGraphPins on variable nodes use the variable's name for display
					// purposes instead of the underlying vertex's name. The frontend vertices
					// of a variable node have consistent names no matter what the 
					// variable is named.
					return EdVariableNode->GetNodeHandle()->GetOutputWithVertexName(METASOUND_GET_PARAM_NAME(OutputData));
				}
				else if (UMetasoundEditorGraphNode* EdNode = CastChecked<UMetasoundEditorGraphNode>(InPin->GetOwningNode()))
				{
					return EdNode->GetNodeHandle()->GetOutputWithVertexName(InPin->GetFName());
				}
			}

			return IOutputController::GetInvalidHandle();
		}

		Frontend::FConstOutputHandle FGraphBuilder::GetConstOutputHandleFromPin(const UEdGraphPin* InPin)
		{
			using namespace Frontend;
			using namespace VariableNames;

			if (InPin && ensure(InPin->Direction == EGPD_Output))
			{
				if (const UMetasoundEditorGraphVariableNode* EdVariableNode = Cast<UMetasoundEditorGraphVariableNode>(InPin->GetOwningNode()))
				{
					// UEdGraphPins on variable nodes use the variable's name for display
					// purposes instead of the underlying vertex's name. The frontend vertices
					// of a variable node have consistent names no matter what the 
					// variable is named.
					return EdVariableNode->GetConstNodeHandle()->GetConstOutputWithVertexName(METASOUND_GET_PARAM_NAME(OutputData));
				}
				else if (const UMetasoundEditorGraphNode* EdNode = CastChecked<UMetasoundEditorGraphNode>(InPin->GetOwningNode()))
				{
					return EdNode->GetConstNodeHandle()->GetConstOutputWithVertexName(InPin->GetFName());
				}
			}

			return IOutputController::GetInvalidHandle();
		}

		UEdGraphPin* FGraphBuilder::FindReroutedOutputPin(UEdGraphPin* OutputPin)
		{
			using namespace Frontend;

			if (OutputPin)
			{
				if (UMetasoundEditorGraphExternalNode* ExternalNode = Cast<UMetasoundEditorGraphExternalNode>(OutputPin->GetOwningNode()))
				{
					if (ExternalNode->GetBreadcrumb().ClassName == FRerouteNodeTemplate::ClassName)
					{
						auto IsInput = [](UEdGraphPin* Pin) { check(Pin); return Pin->Direction == EGPD_Input; };
						if (UEdGraphPin* RerouteInput = *ExternalNode->Pins.FindByPredicate(IsInput))
						{
							TArray<UEdGraphPin*>& LinkedTo = RerouteInput->LinkedTo;
							if (!LinkedTo.IsEmpty())
							{
								UEdGraphPin* ReroutedOutput = LinkedTo.Last();
								return FindReroutedOutputPin(ReroutedOutput);
							}
						}
					}
				}
			}

			return OutputPin;
		}

		const UEdGraphPin* FGraphBuilder::FindReroutedOutputPin(const UEdGraphPin* OutputPin)
		{
			using namespace Frontend;

			if (OutputPin)
			{
				if (UMetasoundEditorGraphExternalNode* ExternalNode = Cast<UMetasoundEditorGraphExternalNode>(OutputPin->GetOwningNode()))
				{
					if (ExternalNode->GetBreadcrumb().ClassName == FRerouteNodeTemplate::ClassName)
					{
						auto IsInput = [](const UEdGraphPin* Pin) { check(Pin); return Pin->Direction == EGPD_Input; };
						if (const UEdGraphPin* RerouteInput = *ExternalNode->Pins.FindByPredicate(IsInput))
						{
							const TArray<UEdGraphPin*>& LinkedTo = RerouteInput->LinkedTo;
							if (!LinkedTo.IsEmpty())
							{
								const UEdGraphPin* ReroutedOutput = LinkedTo.Last();
								return FindReroutedOutputPin(ReroutedOutput);
							}
						}
					}
				}
			}

			return OutputPin;
		}

		Frontend::FOutputHandle FGraphBuilder::FindReroutedOutputHandleFromPin(const UEdGraphPin* OutputPin)
		{
			using namespace Frontend;

			if (OutputPin)
			{
				if (UMetasoundEditorGraphExternalNode* ExternalNode = Cast<UMetasoundEditorGraphExternalNode>(OutputPin->GetOwningNode()))
				{
					if (ExternalNode->GetBreadcrumb().ClassName == FRerouteNodeTemplate::ClassName)
					{
						auto IsInput = [](UEdGraphPin* Pin) { check(Pin); return Pin->Direction == EGPD_Input; };
						if (UEdGraphPin* RerouteInput = *ExternalNode->Pins.FindByPredicate(IsInput))
						{
							TArray<UEdGraphPin*>& LinkedTo = RerouteInput->LinkedTo;
							if (!LinkedTo.IsEmpty())
							{
								UEdGraphPin* ReroutedOutput = LinkedTo.Last();
								return FindReroutedOutputHandleFromPin(ReroutedOutput);
							}
						}
					}
				}

				return GetOutputHandleFromPin(OutputPin);
			}

			return IOutputController::GetInvalidHandle();
		}

		Frontend::FConstOutputHandle FGraphBuilder::FindReroutedConstOutputHandleFromPin(const UEdGraphPin* OutputPin)
		{
			using namespace Frontend;

			if (OutputPin)
			{
				if (const UMetasoundEditorGraphExternalNode* ExternalNode = Cast<UMetasoundEditorGraphExternalNode>(OutputPin->GetOwningNode()))
				{
					if (ExternalNode->GetBreadcrumb().ClassName == FRerouteNodeTemplate::ClassName)
					{
						auto IsInput = [](UEdGraphPin* Pin) { check(Pin); return Pin->Direction == EGPD_Input; };
						if (const UEdGraphPin* RerouteInput = *ExternalNode->Pins.FindByPredicate(IsInput))
						{
							const TArray<UEdGraphPin*>& LinkedTo = RerouteInput->LinkedTo;
							if (!LinkedTo.IsEmpty())
							{
								const UEdGraphPin* ReroutedOutput = LinkedTo.Last();
								return FindReroutedConstOutputHandleFromPin(ReroutedOutput);
							}
						}
					}
				}

				return GetConstOutputHandleFromPin(OutputPin);
			}

			return IOutputController::GetInvalidHandle();
		}

		void FGraphBuilder::FindReroutedInputPins(UEdGraphPin* InPinToCheck, TArray<UEdGraphPin*>& InOutInputPins)
		{
			using namespace Frontend;

			if (InPinToCheck && InPinToCheck->Direction == EGPD_Input)
			{
				if (UMetasoundEditorGraphExternalNode* ExternalNode = Cast<UMetasoundEditorGraphExternalNode>(InPinToCheck->GetOwningNode()))
				{
					if (ExternalNode->GetBreadcrumb().ClassName == FRerouteNodeTemplate::ClassName)
					{
						for (UEdGraphPin* Pin : ExternalNode->Pins)
						{
							if (Pin->Direction == EGPD_Output)
							{
								for (UEdGraphPin* LinkedInput : Pin->LinkedTo)
								{
									FindReroutedInputPins(LinkedInput, InOutInputPins);
								}
							}
						}

						return;
					}
				}

				InOutInputPins.Add(InPinToCheck);
			}
		}

		bool FGraphBuilder::GetPinLiteral(const UEdGraphPin& InInputPin, FMetasoundFrontendLiteral& OutDefaultLiteral)
		{
			using namespace Frontend;

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");

			const FMetaSoundFrontendDocumentBuilder& Builder = GetBuilderFromPinChecked(InInputPin).GetConstBuilder();
			FMetasoundFrontendVertexHandle InputHandle = GetPinVertexHandle(Builder, &InInputPin);
			if (!ensure(InputHandle.IsSet()))
			{
				return false;
			}

			const FMetasoundFrontendVertex* Vertex = GetPinVertex(Builder, &InInputPin);
			if (!ensure(Vertex))
			{
				return false;
			}

			const FString& InStringValue = InInputPin.DefaultValue;
			const FName TypeName = Vertex->TypeName;

			FDataTypeRegistryInfo DataTypeInfo;
			IDataTypeRegistry::Get().GetDataTypeInfo(TypeName, DataTypeInfo);
			switch (DataTypeInfo.PreferredLiteralType)
			{
				case ELiteralType::Boolean:
				{
					// Currently don't support triggers being initialized to boolean in-graph
					if (GetMetasoundDataTypeName<FTrigger>() != TypeName)
					{
						OutDefaultLiteral.Set(FCString::ToBool(*InStringValue));
					}
				}
				break;

				case ELiteralType::Float:
				{
					OutDefaultLiteral.Set(FCString::Atof(*InStringValue));
				}
				break;

				case ELiteralType::Integer:
				{
					OutDefaultLiteral.Set(FCString::Atoi(*InStringValue));
				}
				break;

				case ELiteralType::String:
				{
					OutDefaultLiteral.Set(InStringValue);
				}
				break;

				case ELiteralType::UObjectProxy:
				{
					bool bObjectFound = false;
					if (!InInputPin.DefaultValue.IsEmpty())
					{
						if (UClass* Class = IDataTypeRegistry::Get().GetUClassForDataType(TypeName))
						{
							FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

							// Remove class prefix if included in default value path
							FString ObjectPath = InInputPin.DefaultValue;
							ObjectPath.RemoveFromStart(Class->GetName() + TEXT(" "));

							FARFilter Filter;
							Filter.bRecursiveClasses = false;
							Filter.SoftObjectPaths.Add(FSoftObjectPath(ObjectPath));

							TArray<FAssetData> AssetData;
							AssetRegistryModule.Get().GetAssets(Filter, AssetData);
							if (!AssetData.IsEmpty())
							{
								if (UObject* AssetObject = AssetData.GetData()->GetAsset())
								{
									const UClass* AssetClass = AssetObject->GetClass();
									if (ensureAlways(AssetClass))
									{
										if (AssetClass->IsChildOf(Class))
										{
											Filter.ClassPaths.Add(Class->GetClassPathName());
											OutDefaultLiteral.Set(AssetObject);
											bObjectFound = true;
										}
									}
								}
							}
						}
					}

					if (!bObjectFound)
					{
						// If the class default literal is the default (type is None), then the literal should be set to that.
						// However, if the class default literal is set to an object, the literal should be set to a valid, null object.
						// This is used for reset to default behavior, where an valid object literal with a null value is a separate case
						// from an inherited cleared default literal. 
						const TArray<FMetasoundFrontendClassInputDefault>* ClassInputDefaults = Builder.FindNodeClassInputDefaults(InputHandle.NodeID, Vertex->Name);
						if (ClassInputDefaults)
						{
							const FGuid PageID = Engine::FDocumentBuilderRegistry::GetChecked().ResolveTargetPageID(*ClassInputDefaults);
							auto MatchesPageID = [&PageID](const FMetasoundFrontendClassInputDefault& InputDefault) { return PageID == InputDefault.PageID; };
							if (const FMetasoundFrontendClassInputDefault* ClassDefault = ClassInputDefaults->FindByPredicate(MatchesPageID))
							{
								if (ClassDefault->Literal.GetType() == EMetasoundFrontendLiteralType::None)
								{
									OutDefaultLiteral.Clear();
								}
								else
								{
									OutDefaultLiteral = ClassDefault->Literal;
								}
							}
							return true;
						}

						OutDefaultLiteral.Set(static_cast<UObject*>(nullptr));
					}
				}
				break;

				case ELiteralType::BooleanArray:
				{
					OutDefaultLiteral.Set(TArray<bool>());
				}
				break;

				case ELiteralType::FloatArray:
				{
					OutDefaultLiteral.Set(TArray<float>());
				}
				break;

				case ELiteralType::IntegerArray:
				{
					OutDefaultLiteral.Set(TArray<int32>());
				}
				break;

				case ELiteralType::NoneArray:
				{
					OutDefaultLiteral.Set(FMetasoundFrontendLiteral::FDefaultArray());
				}
				break;

				case ELiteralType::StringArray:
				{
					OutDefaultLiteral.Set(TArray<FString>());
				}
				break;

				case ELiteralType::UObjectProxyArray:
				{
					OutDefaultLiteral.Set(TArray<UObject*>());
				}
				break;

				case ELiteralType::None:
				{
					OutDefaultLiteral.Set(FMetasoundFrontendLiteral::FDefault());
				}
				break;

				case ELiteralType::Invalid:
				default:
				{
					static_assert(static_cast<int32>(ELiteralType::COUNT) == 13, "Possible missing ELiteralType case coverage.");
					ensureMsgf(false, TEXT("Failed to set input node default: Literal type not supported"));
					return false;
				}
				break;
			}

			return true;
		}

		FMetasoundFrontendClassInput FGraphBuilder::CreateUniqueClassInput(UObject& InMetaSound, const FCreateNodeVertexParams& InParams, const TArray<FMetasoundFrontendClassInputDefault>& InDefaultLiterals, const FName* InNameBase)
		{
			FMetasoundFrontendClassInput ClassInput;
			ClassInput.Name = GenerateUniqueNameByClassType(InMetaSound, EMetasoundFrontendClassType::Input, InNameBase ? InNameBase->ToString() : TEXT("Input"));
			ClassInput.TypeName = InParams.DataType;
			ClassInput.VertexID = FGuid::NewGuid();
			ClassInput.NodeID = FGuid::NewGuid();

			// Can be unset if attempting to mirror parameters from a reroute, so default to reference
			ClassInput.AccessType = InParams.AccessType == EMetasoundFrontendVertexAccessType::Unset ? EMetasoundFrontendVertexAccessType::Reference : InParams.AccessType;

			// Should always have at least one value
			if (InDefaultLiterals.IsEmpty())
			{
				ClassInput.InitDefault();
			}
			else
			{
				if (const UMetaSoundSettings* Settings = GetDefault<UMetaSoundSettings>())
				{
					TSet<FGuid> ValidPageIDs;
					Settings->IteratePageSettings([&ValidPageIDs](const FMetaSoundPageSettings& PageSetting)
					{
						ValidPageIDs.Add(PageSetting.UniqueId);
					});
					for (const FMetasoundFrontendClassInputDefault& InputDefault : InDefaultLiterals)
					{
						if (ValidPageIDs.Contains(InputDefault.PageID))
						{
							ClassInput.AddDefault(InputDefault.PageID) = InputDefault.Literal;
						}
					}
				}
			}

			return ClassInput;
		}

		FMetasoundFrontendClassOutput FGraphBuilder::CreateUniqueClassOutput(UObject& InMetaSound, const FCreateNodeVertexParams& InParams, const FName* InNameBase)
		{
			FMetasoundFrontendClassOutput ClassOutput;
			ClassOutput.Name = GenerateUniqueNameByClassType(InMetaSound, EMetasoundFrontendClassType::Output, InNameBase ? InNameBase->ToString() : TEXT("Output"));
			ClassOutput.TypeName = InParams.DataType;
			ClassOutput.VertexID = FGuid::NewGuid();
			ClassOutput.NodeID = FGuid::NewGuid();

			// Can be unset if attempting to mirror parameters from a reroute, so default to reference
			ClassOutput.AccessType = InParams.AccessType == EMetasoundFrontendVertexAccessType::Unset ? EMetasoundFrontendVertexAccessType::Reference : InParams.AccessType;

			return ClassOutput;
		}

		FName FGraphBuilder::GenerateUniqueVariableName(const Frontend::FConstGraphHandle& InFrontendGraph, const FString& InBaseName)
		{
			using namespace Frontend;

			TSet<FName> ExistingVariableNames;

			// Get all the names from the existing variables on the graph
			// and place into the ExistingVariableNames array.
			Algo::Transform(InFrontendGraph->GetVariables(), ExistingVariableNames, [](const FConstVariableHandle& Var) { return Var->GetName(); });

			return GraphBuilderPrivate::GenerateUniqueName(ExistingVariableNames, InBaseName);
		}

		FMetasoundAssetBase& FGraphBuilder::GetOutermostMetaSoundChecked(UObject& InSubObject)
		{
			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(InSubObject.GetOutermostObject());
			check(MetaSoundAsset);
			return *MetaSoundAsset;
		}

		const FMetasoundAssetBase& FGraphBuilder::GetOutermostConstMetaSoundChecked(const UObject& InSubObject)
		{
			const FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(InSubObject.GetOutermostObject());
			check(MetaSoundAsset);
			return *MetaSoundAsset;
		}

		Frontend::FVariableHandle FGraphBuilder::AddVariableHandle(UObject& InMetaSound, const FName& InTypeName, const FName* InNameBase)
		{
			using namespace Frontend;

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);

			FGraphHandle FrontendGraph = MetaSoundAsset->GetRootGraphHandle();

			FName VariableName = GenerateUniqueVariableName(FrontendGraph, InNameBase ? InNameBase->ToString() : LOCTEXT("VariableDefaultDisplayName", "Variable").ToString());
			FVariableHandle Variable = FrontendGraph->AddVariable(InTypeName);

			Variable->SetDisplayName(FText::GetEmpty());
			Variable->SetName(VariableName);

			return Variable;
		}

		Frontend::FVariableHandle FGraphBuilder::DuplicateVariableHandle(UObject& InMetaSound, const Frontend::FConstVariableHandle& InHandle)
		{
			Frontend::FVariableHandle NewHandle = AddVariableHandle(InMetaSound, InHandle->GetDataType(), &InHandle->GetName());
			NewHandle->SetDisplayName(FText::GetEmpty());
			NewHandle->SetDescription(InHandle->GetDescription());
			NewHandle->SetLiteral(InHandle->GetLiteral());
			return NewHandle;
		}

		Frontend::FNodeHandle FGraphBuilder::AddVariableNodeHandle(UObject& InMetaSound, const FGuid& InVariableID, const Metasound::FNodeClassName& InVariableNodeClassName, UMetasoundEditorGraphVariableNode* InVariableNode)
		{
			using namespace Frontend;

			FNodeHandle FrontendNode = INodeController::GetInvalidHandle();

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			
			if (ensure(MetaSoundAsset))
			{
				FMetasoundFrontendClass FrontendClass;
				bool bDidFindClassWithName = ISearchEngine::Get().FindClassWithHighestVersion(InVariableNodeClassName, FrontendClass);
				if (ensure(bDidFindClassWithName))
				{
					FGraphHandle Graph = MetaSoundAsset->GetRootGraphHandle();

					switch (FrontendClass.Metadata.GetType())
					{
						case EMetasoundFrontendClassType::VariableDeferredAccessor:
							FrontendNode = Graph->AddVariableDeferredAccessorNode(InVariableID);
							break;

						case EMetasoundFrontendClassType::VariableAccessor:
							FrontendNode = Graph->AddVariableAccessorNode(InVariableID);
							break;

						case EMetasoundFrontendClassType::VariableMutator:
						{
							FConstVariableHandle Variable = Graph->FindVariable(InVariableID);
							FConstNodeHandle ExistingMutator = Variable->FindMutatorNode();
							if (!ExistingMutator->IsValid())
							{
								FrontendNode = Graph->FindOrAddVariableMutatorNode(InVariableID);
							}
							else
							{
								UE_LOG(LogMetaSound, Error, TEXT("Cannot add node because \"%s\" already exists for variable \"%s\""), *ExistingMutator->GetDisplayName().ToString(), *Variable->GetDisplayName().ToString());
							}
						}
						break;

						default:
						{
							checkNoEntry();
						}
					}
				}
			}

			if (InVariableNode)
			{
				InVariableNode->ClassName = FrontendNode->GetClassMetadata().GetClassName();
				InVariableNode->ClassType = FrontendNode->GetClassMetadata().GetType();
				InVariableNode->NodeID = FrontendNode->GetID();
				MetaSoundAsset->GetModifyContext().AddNodeIDsModified({ InVariableNode->GetNodeID() });
			}

			return FrontendNode;
		}

		bool FGraphBuilder::ConnectNodes(UEdGraphPin& InInputPin, UEdGraphPin& InOutputPin, bool bInConnectEdPins)
		{
			using namespace Frontend;

			// When true, will recursively call back into this function
			// from the schema if the editor pins are successfully connected
			if (bInConnectEdPins)
			{
				const UEdGraphSchema* Schema = InInputPin.GetSchema();
				if (ensure(Schema))
				{
					if (!Schema->TryCreateConnection(&InInputPin, &InOutputPin))
					{
						return false;
					}
				}
				else
				{
					return false;
				}
			}

			FInputHandle InputHandle = GetInputHandleFromPin(&InInputPin);
			FOutputHandle OutputHandle = GetOutputHandleFromPin(&InOutputPin);
			if (!InputHandle->IsValid() || !OutputHandle->IsValid())
			{
				return false;
			}

			if (!ensure(InputHandle->Connect(*OutputHandle)))
			{
				InInputPin.BreakLinkTo(&InOutputPin);
				return false;
			}

			return true;
		}

		void FGraphBuilder::DisconnectPinVertex(UEdGraphPin& InPin)
		{
			FMetaSoundFrontendDocumentBuilder& Builder = GetBuilderFromPinChecked(InPin).GetBuilder();
			const FMetasoundFrontendVertexHandle VertexHandle = GetPinVertexHandle(Builder, &InPin);
			if (VertexHandle.IsSet())
			{
				if (InPin.Direction == EGPD_Input)
				{
					Builder.RemoveEdgeToNodeInput(VertexHandle.NodeID, VertexHandle.VertexID);
				}
				else
				{
					Builder.RemoveEdgesFromNodeOutput(VertexHandle.NodeID, VertexHandle.VertexID);
				}
			}
		}

		bool FGraphBuilder::DeleteMemberNodes(const UMetasoundEditorGraphMember& InGraphMember)
		{
			bool bSuccess = true;
			const TArray<UMetasoundEditorGraphMemberNode*> Nodes = InGraphMember.GetNodes();
			for (UMetasoundEditorGraphMemberNode* Node : Nodes)
			{
				if (ensure(Node))
				{
					bSuccess &= Metasound::Editor::FGraphBuilder::DeleteNode(*Node);
				}
			}
			return bSuccess;
		}

		bool FGraphBuilder::DeleteNode(UEdGraphNode& InNode, bool bRemoveUnusedDependencies)
		{
			using namespace Engine;
			using namespace Frontend;

			UMetasoundEditorGraph* Graph = CastChecked<UMetasoundEditorGraph>(InNode.GetGraph());
			UObject& MetaSound = Graph->GetMetasoundChecked();
			UMetaSoundBuilderBase& Builder = FDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(MetaSound);

			if (UMetasoundEditorGraphCommentNode* Node = Cast<UMetasoundEditorGraphCommentNode>(&InNode))
			{
				const bool bFrontendCommentRemoved = Builder.RemoveGraphComment(Node->CommentID);
				if (ensure(bFrontendCommentRemoved))
				{
					return ensure(Graph->RemoveNode(&InNode));
				}
			}

			UMetasoundEditorGraphNode* Node = Cast<UMetasoundEditorGraphNode>(&InNode);
			if (ensure(Node))
			{
//				Need to split out delete ed vs. frontend impl which will happen in subsequent change.
//				For now, just ignore removal failure as a hack as this gets called from ed graph sync.
				EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
				Builder.RemoveNode({ Node->GetNodeID() }, Result, bRemoveUnusedDependencies);
// 				ensure(Result == EMetaSoundBuilderResult::Succeeded);
			}

			return ensure(Graph->RemoveNode(&InNode));
		}

		bool FGraphBuilder::BindEditorGraph(const FMetaSoundFrontendDocumentBuilder& InBuilder, UMetasoundEditorGraph** OutGraph)
		{
			using namespace Frontend;

			bool bNewGraphBound = false;
			FMetasoundAssetBase& MetaSound = InBuilder.GetMetasoundAsset();
			UMetasoundEditorGraph* Graph = Cast<UMetasoundEditorGraph>(MetaSound.GetGraph());
			if (!Graph)
			{
				Graph = NewObject<UMetasoundEditorGraph>(MetaSound.GetOwningAsset(), FName(), RF_Transactional);
				Graph->Schema = UMetasoundEditorGraphSchema::StaticClass();
				MetaSound.SetGraph(Graph);
				bNewGraphBound = true;
			}

			if (OutGraph)
			{
				*OutGraph = Graph;
			}
			return bNewGraphBound;
		}

		void FGraphBuilder::RebuildNodePins(UMetasoundEditorGraphNode& InGraphNode)
		{
			using namespace Frontend;
		
			for (int32 i = InGraphNode.Pins.Num() - 1; i >= 0; i--)
			{
				InGraphNode.RemovePin(InGraphNode.Pins[i]);
			}

			FConstNodeHandle NodeHandle = InGraphNode.GetNodeHandle();
			TArray<FConstInputHandle> InputHandles = NodeHandle->GetConstInputs();
			NodeHandle->GetInputStyle().SortDefaults(InputHandles, [](const Frontend::FConstInputHandle& Handle) { return FGraphBuilder::GetDisplayName(*Handle); });
			for (const FConstInputHandle& InputHandle : InputHandles)
			{
				// Only add pins of the node if the connection is user modifiable. 
				// Connections which the user cannot modify are controlled elsewhere.
				if (InputHandle->IsConnectionUserModifiable())
				{
					AddPinToNode(InGraphNode, InputHandle);
				}
			}

			TArray<FConstOutputHandle> OutputHandles = NodeHandle->GetConstOutputs();
			NodeHandle->GetOutputStyle().SortDefaults(OutputHandles, [](const Frontend::FConstOutputHandle& Handle) { return FGraphBuilder::GetDisplayName(*Handle); });
			for (const FConstOutputHandle& OutputHandle : OutputHandles)
			{
				// Only add pins of the node if the connection is user modifiable. 
				// Connections which the user cannot modify are controlled elsewhere.
				if (OutputHandle->IsConnectionUserModifiable())
				{
					AddPinToNode(InGraphNode, OutputHandle);
				}
			}
		}

		void FGraphBuilder::RefreshPinMetadata(UEdGraphPin& InPin, const FMetasoundFrontendVertexMetadata& InMetadata)
		{
			// Pin ToolTips are no longer cached on pins, and are instead dynamically generated via UMetasoundEditorGraphNode::GetPinHoverText
			InPin.PinToolTip = { };
			InPin.bAdvancedView = InMetadata.bIsAdvancedDisplay;

			//Are the Unconnected hidden for the node
			bool bUnconnectedHidden = false;
			UMetasoundEditorGraphNode* MetasoundGraphNode = Cast<UMetasoundEditorGraphNode>(InPin.GetOwningNode());
			{
				FMetasoundFrontendNode FrontendNode = MetasoundGraphNode->GetFrontendNodeChecked();
				bUnconnectedHidden = FrontendNode.Style.bUnconnectedPinsHidden;
			}

			if (InPin.bAdvancedView || bUnconnectedHidden)
			{
				UEdGraphNode* OwningNode = InPin.GetOwningNode();
				check(OwningNode);
				if (OwningNode->AdvancedPinDisplay == ENodeAdvancedPins::NoPins)
				{
					OwningNode->AdvancedPinDisplay = ENodeAdvancedPins::Hidden;
				}
			}
		}

		void FGraphBuilder::RegisterGraphWithFrontend(UObject& InMetaSound, bool bInForceViewSynchronization)
		{
			using namespace Frontend;

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);

			TArray<FMetasoundAssetBase*> EditedReferencingMetaSounds;
			if (GEditor)
			{
				if (UAssetEditorSubsystem* AssetSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
				{
					TArray<UObject*> EditedAssets = AssetSubsystem->GetAllEditedAssets();
					for (UObject* Asset : EditedAssets)
					{
						if (Asset != &InMetaSound)
						{
							if (FMetasoundAssetBase* EditedMetaSound = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Asset))
							{
								EditedMetaSound->RebuildReferencedAssetClasses();
								if (EditedMetaSound->IsReferencedAsset(*MetaSoundAsset))
								{
									EditedReferencingMetaSounds.Add(EditedMetaSound);
								}
							}
						}
					}
				}
			}

			FMetaSoundAssetRegistrationOptions RegOptions;
			RegOptions.bForceReregister = true;
			RegOptions.bForceViewSynchronization = bInForceViewSynchronization;
			
			// if EditedReferencingMetaSounds is empty, then no MetaSounds are open
			// that reference this MetaSound, so just register this asset. Otherwise,
			// this graph will recursively get updated when the open referencing graphs
			// are registered recursively via bRegisterDependencies flag.
			if (EditedReferencingMetaSounds.IsEmpty())
			{
				MetaSoundAsset->UpdateAndRegisterForExecution(RegOptions);
			}
			else
			{
				for (FMetasoundAssetBase* MetaSound : EditedReferencingMetaSounds)
				{
					MetaSound->UpdateAndRegisterForExecution(RegOptions);
				}
			}
		}

		bool FGraphBuilder::IsMatchingInputHandleAndPin(const Frontend::FConstInputHandle& InInputHandle, const UEdGraphPin& InEditorPin)
		{
			if (InEditorPin.Direction != EGPD_Input)
			{
				return false;
			}

			Frontend::FConstInputHandle PinInputHandle = GetConstInputHandleFromPin(&InEditorPin);
			if (PinInputHandle->GetID() == InInputHandle->GetID())
			{
				return true;
			}

			return false;
		}

		bool FGraphBuilder::IsMatchingOutputHandleAndPin(const Frontend::FConstOutputHandle& InOutputHandle, const UEdGraphPin& InEditorPin)
		{
			if (InEditorPin.Direction != EGPD_Output)
			{
				return false;
			}

			Frontend::FConstOutputHandle PinOutputHandle = GetConstOutputHandleFromPin(&InEditorPin);
			if (PinOutputHandle->GetID() == InOutputHandle->GetID())
			{
				return true;
			}

			return false;
		}

		void FGraphBuilder::DepthFirstTraversal(UEdGraphNode* InInitialNode, FDepthFirstVisitFunction InVisitFunction)
		{
			// Non recursive depth first traversal.
			TArray<UEdGraphNode*> Stack({InInitialNode});
			TSet<UEdGraphNode*> Visited;

			while (Stack.Num() > 0)
			{
				UEdGraphNode* CurrentNode = Stack.Pop();
				if (Visited.Contains(CurrentNode))
				{
					// Do not revisit a node that has already been visited. 
					continue;
				}

				TArray<UEdGraphNode*> Children = InVisitFunction(CurrentNode).Array();
				Stack.Append(Children);

				Visited.Add(CurrentNode);
			}
		}

		UEdGraphPin* FGraphBuilder::AddPinToNode(UMetasoundEditorGraphNode& InEditorNode, Frontend::FConstInputHandle InInputHandle)
		{
			using namespace Frontend;

			FEdGraphPinType PinType;
			FName DataTypeName = InInputHandle->GetDataType();

			const IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
			if (const FEdGraphPinType* RegisteredPinType = EditorModule.FindPinType(DataTypeName))
			{
				PinType = *RegisteredPinType;
			}

			FName PinName = FGraphBuilder::GetPinName(*InInputHandle);
			UEdGraphPin* NewPin = InEditorNode.CreatePin(EGPD_Input, PinType, PinName);
			if (ensure(NewPin))
			{
				RefreshPinMetadata(*NewPin, InInputHandle->GetMetadata());
				const FMetaSoundFrontendDocumentBuilder& Builder = InEditorNode.GetBuilderChecked().GetConstBuilder();
				SynchronizePinLiteral(Builder, *NewPin);
			}

			return NewPin;
		}

		UEdGraphPin* FGraphBuilder::AddPinToNode(UMetasoundEditorGraphNode& InEditorNode, Frontend::FConstOutputHandle InOutputHandle)
		{
			FEdGraphPinType PinType;
			FName DataTypeName = InOutputHandle->GetDataType();

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
			if (const FEdGraphPinType* RegisteredPinType = EditorModule.FindPinType(DataTypeName))
			{
				PinType = *RegisteredPinType;
			}

			FName PinName = FGraphBuilder::GetPinName(*InOutputHandle);
			UEdGraphPin* NewPin = InEditorNode.CreatePin(EGPD_Output, PinType, PinName);
			if (ensure(NewPin))
			{
				RefreshPinMetadata(*NewPin, InOutputHandle->GetMetadata());
			}

			return NewPin;
		}

		bool FGraphBuilder::RecurseGetDocumentModified(FMetasoundAssetBase& InAssetBase)
		{
			using namespace Metasound::Frontend;

			if (InAssetBase.GetConstModifyContext().GetDocumentModified())
			{
				return true;
			}

			TArray<FMetasoundAssetBase*> References;
			ensureAlways(IMetaSoundAssetManager::GetChecked().TryLoadReferencedAssets(InAssetBase, References));
			for (FMetasoundAssetBase* Reference : References)
			{
				check(Reference);
				const bool bReferenceDocumentModified = RecurseGetDocumentModified(*Reference);
				if (bReferenceDocumentModified)
				{
					return true;
				}
			}

			return false;
		}

		bool FGraphBuilder::SynchronizePinType(const IMetasoundEditorModule& InEditorModule, UEdGraphPin& InPin, const FName InDataType)
		{
			FEdGraphPinType PinType;
			if (const FEdGraphPinType* RegisteredPinType = InEditorModule.FindPinType(InDataType))
			{
				PinType = *RegisteredPinType;
			}

			if (InPin.PinType != PinType)
			{
				if (const UMetasoundEditorGraphNode* Node = Cast<UMetasoundEditorGraphNode>(InPin.GetOwningNodeUnchecked()))
				{
					const FString NodeName = Node->GetDisplayName().ToString();
					UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Pin '%s' on Node '%s': Type converted to '%s'"), *NodeName, *InPin.GetName(), *InDataType.ToString());
				}
				InPin.PinType = PinType;
				return true;
			}

			return false;
		}

		bool FGraphBuilder::SynchronizeComments(const FMetaSoundFrontendDocumentBuilder& InBuilder, UMetasoundEditorGraph& OutGraph)
		{
			bool bModified = false;

			TMap<FGuid, UMetasoundEditorGraphCommentNode*> CommentIDToEdNode;
			Algo::TransformIf(OutGraph.Nodes, CommentIDToEdNode,
				[](const UEdGraphNode* Node)
				{
					return Node->IsA<UMetasoundEditorGraphCommentNode>();
				},
				[](UEdGraphNode* Node)
				{
					UMetasoundEditorGraphCommentNode* CommentNode = CastChecked<UMetasoundEditorGraphCommentNode>(Node);
					return TPair<FGuid, UMetasoundEditorGraphCommentNode*>(CommentNode->GetCommentID(), CommentNode);
				});

			const FMetasoundFrontendDocument& Document = InBuilder.GetConstDocumentChecked();
			const TMap<FGuid, FMetaSoundFrontendGraphComment>& Comments = InBuilder.FindConstBuildGraphChecked().Style.Comments;
			for (const TPair<FGuid, FMetaSoundFrontendGraphComment>& Pair : Comments)
			{
				UMetasoundEditorGraphCommentNode* CommentNode = nullptr;
				if (!CommentIDToEdNode.RemoveAndCopyValue(Pair.Key, CommentNode))
				{
					bModified = true;
					constexpr bool bSelectNode = false;

					// Can't use spawn node action because it modifies the transaction stack, so just generate from CDO and backport frontend data accordingly.
					UClass* CommentClass = UMetasoundEditorGraphCommentNode::StaticClass();
					check(CommentClass);
					UMetasoundEditorGraphCommentNode* CommentCDO = CommentClass->GetDefaultObject<UMetasoundEditorGraphCommentNode>();
					check(CommentCDO);

					const UGraphEditorSettings* GraphEditorSettings = GetDefault<UGraphEditorSettings>();
					check(GraphEditorSettings);

					UMetasoundEditorGraphCommentNode* NewNode = CastChecked<UMetasoundEditorGraphCommentNode>(DuplicateObject<UEdGraphNode>(CommentCDO, &OutGraph));
					NewNode->SetFlags(RF_Transactional);
					OutGraph.AddNode(NewNode, false, false);

					NewNode->CreateNewGuid();
					NewNode->bCommentBubbleVisible_InDetailsPanel = GraphEditorSettings->bShowCommentBubbleWhenZoomedOut;

					// Pull position, color, etc. from the existing frontend data.
					NewNode->CommentID = Pair.Key;
					UMetasoundEditorGraphCommentNode::ConvertFromFrontendComment(Pair.Value, *NewNode);
				}
			}

			// Remaining items are stale, so they are removed
			bModified |= !CommentIDToEdNode.IsEmpty();
			for (const TPair<FGuid, UMetasoundEditorGraphCommentNode*>& Comment : CommentIDToEdNode)
			{
				constexpr bool bMarkDirty = false;
				constexpr bool bBreakAllLinks = true;
				OutGraph.RemoveNode(Comment.Value, bBreakAllLinks, bMarkDirty);
			}

			return bModified;
		}

		bool FGraphBuilder::SynchronizeConnections(const FMetaSoundFrontendDocumentBuilder& InBuilder, UMetasoundEditorGraph& OutGraph)
		{
			using namespace Frontend;

			bool bIsGraphDirty = false;

			TArray<UMetasoundEditorGraphNode*> EditorNodes;
			{
				OutGraph.GetNodesOfClass(EditorNodes);
			}

			TMap<FGuid, UMetasoundEditorGraphNode*> EditorNodesByFrontendID;
			for (UMetasoundEditorGraphNode* EditorNode : EditorNodes)
			{
				const FGuid& NodeID = EditorNode->GetNodeID();
				if (EditorNodesByFrontendID.Contains(NodeID))
				{
					UE_LOG(LogMetasoundEditor, Error, TEXT("Multiple editor nodes associated with FrontendDocument node with ID '%s'"), *NodeID.ToString());
				}
				else
				{
					EditorNodesByFrontendID.Add(NodeID, EditorNode);
				}
			}

			// Iterate through all nodes in metasound editor graph and synchronize connections.
			for (UMetasoundEditorGraphNode* EditorNode : EditorNodes)
			{
				bool bIsNodeDirty = false;

				FConstNodeHandle Node = EditorNode->GetConstNodeHandle();

				TArray<UEdGraphPin*> Pins = EditorNode->GetAllPins();
				TArray<FConstInputHandle> NodeInputs = Node->GetConstInputs();
				TArray<FConstOutputHandle> NodeOutputs = Node->GetConstOutputs();

				// Ignore connections which are not handled by the editor.
				NodeInputs.RemoveAll([](const FConstInputHandle& FrontendInput) { return !FrontendInput->IsConnectionUserModifiable(); });
				NodeOutputs.RemoveAll([](const FConstOutputHandle& FrontendOutput) { return !FrontendOutput->IsConnectionUserModifiable(); });

				for (FConstInputHandle& NodeInput : NodeInputs)
				{
					auto IsMatchingInputPin = [&](const UEdGraphPin* Pin) -> bool
					{
						return IsMatchingInputHandleAndPin(NodeInput, *Pin);
					};

					UEdGraphPin* MatchingPin = nullptr;
					if (UEdGraphPin** DoublePointer = Pins.FindByPredicate(IsMatchingInputPin))
					{
						MatchingPin = *DoublePointer;
					}

					if (!ensure(MatchingPin))
					{
						continue;
					}

					// Remove pin so it isn't used twice.
					Pins.Remove(MatchingPin);

					FConstOutputHandle OutputHandle = NodeInput->GetConnectedOutput();
					if (OutputHandle->IsValid())
					{
						// Both input and output handles be user modifiable for a
						// connection to be controlled by the editor.
						check(OutputHandle->IsConnectionUserModifiable());

						bool bAddLink = false;

						if (MatchingPin->LinkedTo.IsEmpty())
						{
							// No link currently exists. Add the appropriate link.
							bAddLink = true;
						}
						else if (!IsMatchingOutputHandleAndPin(OutputHandle, *MatchingPin->LinkedTo[0]))
						{
							// The wrong link exists.
							constexpr bool bNotifyNodes = false;
							constexpr bool bMarkDirty = false;
							MatchingPin->BreakAllPinLinks(bNotifyNodes, bMarkDirty);
							bAddLink = true;
						}

						if (bAddLink)
						{
							const FGuid NodeID = OutputHandle->GetOwningNodeID();
							const FText& OwningNodeName = EditorNode->GetDisplayName();
							UMetasoundEditorGraphNode* OutputEditorNode = EditorNodesByFrontendID.FindRef(NodeID);
							if (OutputEditorNode)
							{
								UEdGraphPin* OutputPin = OutputEditorNode->FindPinChecked(OutputHandle->GetName(), EEdGraphPinDirection::EGPD_Output);

								UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Node '%s' Connection: Linking Pin '%s' to '%s'"), *OwningNodeName.ToString(), *MatchingPin->GetName(), *OutputPin->GetName());

								constexpr bool bAlwaysMarkDirty = false;
								MatchingPin->MakeLinkTo(OutputPin, bAlwaysMarkDirty);
								bIsNodeDirty = true;
							}
							else
							{
								UE_LOG(LogMetasoundEditor, Warning, TEXT("Failed to synchronize Frontend Node '%s' Connection: Pin '%s'"), *OwningNodeName.ToString(), *MatchingPin->GetName());
							}
						}
					}
					else
					{
						// No link should exist.
						if (!MatchingPin->LinkedTo.IsEmpty())
						{
							constexpr bool bNotifyNodes = false;
							constexpr bool bMarkDirty = false;
							MatchingPin->BreakAllPinLinks(bNotifyNodes, bMarkDirty);
							const FText OwningNodeName = EditorNode->GetDisplayName();
							const FText InputName = FGraphBuilder::GetDisplayName(*NodeInput);
							UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Node '%s' Connection: Breaking all pin links to '%s'"), *OwningNodeName.ToString(), *InputName.ToString());
							bIsNodeDirty = true;
						}
					}

					SynchronizePinLiteral(InBuilder, *MatchingPin);
				}

				// Handle node outputs to break connections for the case 
				// where the connected node input is on a node that has been deleted, 
				// so it wasn't handled by the node inputs direction above
				for (FConstOutputHandle& NodeOutput : NodeOutputs)
				{
					// Find pin
					auto IsMatchingOutputPin = [&](const UEdGraphPin* Pin) -> bool
					{
						return IsMatchingOutputHandleAndPin(NodeOutput, *Pin);
					};

					UEdGraphPin* MatchingPin = nullptr;
					if (UEdGraphPin** DoublePointer = Pins.FindByPredicate(IsMatchingOutputPin))
					{
						MatchingPin = *DoublePointer;
					}

					if (!ensure(MatchingPin))
					{
						continue;
					}

					// Remove connected pins from removal list
					TArray<UEdGraphPin*> PinsToBreak = MatchingPin->LinkedTo;
					TArray<FConstInputHandle> InputHandles = NodeOutput->GetConstConnectedInputs();
					for (FConstInputHandle InputHandle : InputHandles)
					{
						UEdGraphPin** InputPin = PinsToBreak.FindByPredicate(
							[&](const UEdGraphPin* Pin) -> bool
							{
								return IsMatchingInputHandleAndPin(InputHandle, *Pin);
							});
						if (InputPin)
						{
							UEdGraphPin* FoundPin = *InputPin;
							if (FoundPin)
							{
								PinsToBreak.Remove(FoundPin);
							}
						}
					}

					// Break remaining invalid connections
					for (UEdGraphPin* PinToBreak : PinsToBreak)
					{
						constexpr bool bMarkDirty = false;
						MatchingPin->BreakLinkTo(PinToBreak, bMarkDirty);
						const FText OwningNodeName = EditorNode->GetDisplayName();
						const FText OutputName = FGraphBuilder::GetDisplayName(*NodeOutput);
						UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Node '%s' Connection: Breaking a pin link to '%s'"), *OwningNodeName.ToString(), *OutputName.ToString());
						bIsNodeDirty = true;
					}
				}

				bIsGraphDirty |= bIsNodeDirty;
			}

			return bIsGraphDirty;
		}

		bool FGraphBuilder::SynchronizeGraph(const FMetaSoundFrontendDocumentBuilder& InBuilder, UMetasoundEditorGraph& OutGraph, bool bSkipIfModifyContextUnchanged)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::Editor::FGraphBuilder::SynchronizeGraph);

			using namespace Frontend;

			UObject& MetaSound = InBuilder.CastDocumentObjectChecked<UObject>();
			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&MetaSound);
			check(MetaSoundAsset);

			if (!bSkipIfModifyContextUnchanged || RecurseGetDocumentModified(*MetaSoundAsset))
			{
				TSet<FMetasoundAssetBase*> EditedReferencingMetaSounds;
				if (GEditor)
				{
					TArray<UObject*> EditedAssets = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->GetAllEditedAssets();
					for (UObject* Asset : EditedAssets)
					{
						if (Asset != &MetaSound)
						{
							if (FMetasoundAssetBase* EditedMetaSound = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Asset))
							{
								if (EditedMetaSound->IsReferencedAsset(*MetaSoundAsset))
								{
									EditedReferencingMetaSounds.Add(EditedMetaSound);
								}
							}
						}
					}
				}

				if (EditedReferencingMetaSounds.IsEmpty())
				{
					MetaSoundAsset->CacheRegistryMetadata();
					GraphBuilderPrivate::SynchronizeGraphRecursively(InBuilder, OutGraph, bSkipIfModifyContextUnchanged);
					GraphBuilderPrivate::RecurseClearDocumentModified(*MetaSoundAsset);
				}
				else
				{
					for (FMetasoundAssetBase* EditedMetaSound : EditedReferencingMetaSounds)
					{
						check(EditedMetaSound);
						UObject* OwningMetaSound = EditedMetaSound->GetOwningAsset();
						check(OwningMetaSound);
						const FMetaSoundFrontendDocumentBuilder& EditedBuilder = IDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(OwningMetaSound);
						SynchronizeGraph(EditedBuilder, *CastChecked<UMetasoundEditorGraph>(&EditedMetaSound->GetGraphChecked()), bSkipIfModifyContextUnchanged);
					}
				}

				return true;
			}

			return false;
		}

		bool FGraphBuilder::SynchronizeOutputNodes(const FMetaSoundFrontendDocumentBuilder& InBuilder, UMetasoundEditorGraph& OutGraph)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::Editor::FGraphBuilder::SynchronizeOutputNodes);

			using namespace Frontend;

			bool bEditorGraphModified = false;

			const UObject& MetaSoundObject = InBuilder.CastDocumentObjectChecked<const UObject>();
			const FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&MetaSoundObject);
			check(MetaSoundAsset);
			FConstGraphHandle GraphHandle = MetaSoundAsset->GetRootGraphHandle();

			TArray<UMetasoundEditorGraphOutputNode*> OutputNodes;
			OutGraph.GetNodesOfClassEx<UMetasoundEditorGraphOutputNode>(OutputNodes);
			for (UMetasoundEditorGraphOutputNode* Node : OutputNodes)
			{
				FConstNodeHandle NodeHandle = Node->GetConstNodeHandle();
				if (!NodeHandle->IsValid())
				{
					for (UEdGraphPin* Pin : Node->Pins)
					{
						check(Pin);
						FConstClassOutputAccessPtr ClassOutputPtr = GraphHandle->FindClassOutputWithName(Pin->PinName);
						if (const FMetasoundFrontendClassOutput* Output = ClassOutputPtr.Get())
						{
							const FGuid& InitialID = Node->GetNodeID();
							if (NodeHandle->GetID() != Output->NodeID)
							{
								Node->SetNodeID(Output->NodeID);
								UE_LOG(LogMetasoundEditor, Verbose, TEXT("Editor Output Node '%s' interface versioned"), *Node->GetDisplayName().ToString());

								bEditorGraphModified = true;
							}
						}
					}
				}
			}

			return bEditorGraphModified;
		}

		void FGraphBuilder::SynchronizeNodes(const FMetaSoundFrontendDocumentBuilder& InBuilder, UMetasoundEditorGraph& OutGraph)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::Editor::FGraphBuilder::SynchronizeNodes);

			using namespace Frontend;

			// Get all external nodes from Frontend graph.  Input and output references will only be added/synchronized
			// if required when synchronizing connections (as they are not required to inhabit editor graph).
			UObject& MetaSoundObject = InBuilder.CastDocumentObjectChecked<UObject>();
			const FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&MetaSoundObject);
			check(MetaSoundAsset);
			FConstGraphHandle GraphHandle = MetaSoundAsset->GetRootGraphHandle();
			TArray<FConstNodeHandle> FrontendNodes = GraphHandle->GetConstNodes();
			TArray<UMetasoundEditorGraphNode*> EditorNodes;
			OutGraph.GetNodesOfClass(EditorNodes);

			// Find existing array of editor nodes associated with Frontend node
			struct FAssociatedNodes
			{
				TArray<UMetasoundEditorGraphNode*> EditorNodes;
				FConstNodeHandle Node = Metasound::Frontend::INodeController::GetInvalidHandle();
			};
			TMap<FGuid, FAssociatedNodes> AssociatedNodes;

			// Input nodes use template input nodes to visually distinguish location, so ignore them.
			FrontendNodes.RemoveAllSwap([](const FConstNodeHandle& Node)
			{
				return Node->GetClassMetadata().GetType() == EMetasoundFrontendClassType::Input;
			},
			EAllowShrinking::No);

			// Reverse iterate so paired nodes can safely be removed from the array.
			for (int32 i = FrontendNodes.Num() - 1; i >= 0; i--)
			{
				const FConstNodeHandle& Node = FrontendNodes[i];
				bool bFoundEditorNode = false;
				for (int32 j = EditorNodes.Num() - 1; j >= 0; --j)
				{
					UMetasoundEditorGraphNode* EditorNode = EditorNodes[j];
					if (EditorNode->GetNodeID() == Node->GetID())
					{
						// Editor node may have the same Frontend NodeID as another page,
						// but may have been assigned a different Editor NodeID, so synchronize
						// from frontend data here only if node location was able to sync.
						constexpr bool bUpdateEditorNodeID = true;
						const bool bLocationFound = EditorNode->SyncLocationFromFrontendNode(bUpdateEditorNodeID);
						if (bLocationFound)
						{
							bFoundEditorNode = true;
							FAssociatedNodes& AssociatedNodeData = AssociatedNodes.FindOrAdd(Node->GetID());
							if (AssociatedNodeData.Node->IsValid())
							{
								ensure(AssociatedNodeData.Node == Node);
							}
							else
							{
								AssociatedNodeData.Node = Node;
							}

							EditorNode->SyncCommentFromFrontendNode();
							AssociatedNodeData.EditorNodes.Add(EditorNode);
							EditorNodes.RemoveAtSwap(j, EAllowShrinking::No);
						}
					}
				}

				if (bFoundEditorNode)
				{
					FrontendNodes.RemoveAtSwap(i, EAllowShrinking::No);
				}
			}

			// FrontendNodes now contains nodes which need to be added to the editor graph.
			// EditorNodes now contains nodes that need to be removed from the editor graph.
			// AssociatedNodes contains pairs which we have to check have synchronized pins

			// Add and remove nodes first in order to make sure correct editor nodes
			// exist before attempting to synchronize connections.
			for (UMetasoundEditorGraphNode* EditorNode : EditorNodes)
			{
				constexpr bool bMarkDirty = false;
				constexpr bool bBreakAllLinks = true;
				OutGraph.RemoveNode(EditorNode, bBreakAllLinks, bMarkDirty);
			}

			// Add missing editor nodes marked as visible.
			for (const FConstNodeHandle& Node : FrontendNodes)
			{
				const FMetasoundFrontendNodeStyle& CurrentStyle = Node->GetNodeStyle();
				for (const TPair<FGuid, FVector2D>& Location : CurrentStyle.Display.Locations)
				{
					UMetasoundEditorGraphNode* NewGraphNode = nullptr;
					const FMetasoundFrontendClassMetadata& ClassMetadata = Node->GetClassMetadata();
					switch (ClassMetadata.GetType())
					{
						case EMetasoundFrontendClassType::External:
						{
							NewGraphNode = CastChecked<UMetasoundEditorGraphNode>(AddExternalNode(MetaSoundObject, Node->GetID(), ClassMetadata, false));
						}
						break;

						case EMetasoundFrontendClassType::Template:
						{
							if (Node->GetClassMetadata().GetClassName() == FInputNodeTemplate::ClassName)
							{
								NewGraphNode = CastChecked<UMetasoundEditorGraphNode>(AddInputNode(MetaSoundObject, Node->GetID(), false));
							}
							else
							{
								NewGraphNode = CastChecked<UMetasoundEditorGraphNode>(AddTemplateNode(MetaSoundObject, Node->GetID(), Node->GetClassMetadata(), false));
							}
						}
						break;

						case EMetasoundFrontendClassType::Output:
						{
							NewGraphNode = CastChecked<UMetasoundEditorGraphNode>(AddOutputNode(MetaSoundObject, Node->GetID(), false));
						}
						break;

						case EMetasoundFrontendClassType::VariableMutator:
						case EMetasoundFrontendClassType::VariableAccessor:
						case EMetasoundFrontendClassType::VariableDeferredAccessor:
						case EMetasoundFrontendClassType::Variable:
						{
							NewGraphNode = CastChecked<UMetasoundEditorGraphNode>(AddVariableNode(MetaSoundObject, Node, false));
						}
						break;

						case EMetasoundFrontendClassType::Invalid:

						// Class type needs to be deprecated 
						case EMetasoundFrontendClassType::Graph:

						// Not supported in editor
						case EMetasoundFrontendClassType::Literal:

						// Since MetaSound Document v1.12 update, the editor uses template input nodes, so no direct node representation of inputs no longer exists
						case EMetasoundFrontendClassType::Input: 
						default:
						{
							checkNoEntry();
							static_assert(static_cast<int32>(EMetasoundFrontendClassType::Invalid) == 10, "Possible missing FMetasoundFrontendClassType case coverage");
						}
						break;
					}

					if (ensure(NewGraphNode))
					{
						ensureMsgf(NewGraphNode->NodeGuid.IsValid(), TEXT("New editor NodeGuid must be valid."));
						ensureMsgf(NewGraphNode->NodeGuid == Location.Key, TEXT("New editor NodeGuid must match location key"));

						FAssociatedNodes& AssociatedNodeData = AssociatedNodes.FindOrAdd(Node->GetID());
						if (AssociatedNodeData.Node->IsValid())
						{
							ensure(AssociatedNodeData.Node == Node);
						}
						else
						{
							AssociatedNodeData.Node = Node;
						}

						AssociatedNodeData.EditorNodes.Add(NewGraphNode);
					}
				}
			}

			// Synchronize pins on node associations.
			for (const TPair<FGuid, FAssociatedNodes>& IdNodePair : AssociatedNodes)
			{
				for (UMetasoundEditorGraphNode* EditorNode : IdNodePair.Value.EditorNodes)
				{
					SynchronizeNodePins(*EditorNode, IdNodePair.Value.Node);
				}
			}
		}

		bool FGraphBuilder::SynchronizeNodePins(UMetasoundEditorGraphNode& InEditorNode, Frontend::FConstNodeHandle InNode, bool bRemoveUnusedPins, bool bLogChanges)
		{
			using namespace Frontend;

			bool bIsNodeDirty = false;

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");

			TArray<FConstInputHandle> InputHandles;
			TArray<FConstOutputHandle> OutputHandles;
			auto GetUserModifiableHandles = [InNode](TArray<FConstInputHandle>& InHandles, TArray<FConstOutputHandle>& OutHandles)
			{
				InHandles = InNode->GetConstInputs();
				OutHandles = InNode->GetConstOutputs();

				// Remove input and output handles which are not user modifiable
				InHandles.RemoveAll([](const FConstInputHandle& FrontendInput) { return !FrontendInput->IsConnectionUserModifiable(); });
				OutHandles.RemoveAll([](const FConstOutputHandle& FrontendOutput) { return !FrontendOutput->IsConnectionUserModifiable(); });
			};
			GetUserModifiableHandles(InputHandles, OutputHandles);

			// Filter out pins which are not paired.
			TArray<UEdGraphPin*> EditorPins = InEditorNode.Pins;
			for (int32 i = EditorPins.Num() - 1; i >= 0; i--)
			{
				UEdGraphPin* Pin = EditorPins[i];

				auto IsMatchingInputHandle = [&](const FConstInputHandle& InputHandle) -> bool
				{
					return IsMatchingInputHandleAndPin(InputHandle, *Pin);
				};

				auto IsMatchingOutputHandle = [&](const FConstOutputHandle& OutputHandle) -> bool
				{
					return IsMatchingOutputHandleAndPin(OutputHandle, *Pin);
				};

				switch (Pin->Direction)
				{
					case EEdGraphPinDirection::EGPD_Input:
					{
						int32 MatchingInputIndex = InputHandles.FindLastByPredicate(IsMatchingInputHandle);
						if (INDEX_NONE != MatchingInputIndex)
						{
							bIsNodeDirty |= SynchronizePinType(EditorModule, *EditorPins[i], InputHandles[MatchingInputIndex]->GetDataType());
							InputHandles.RemoveAtSwap(MatchingInputIndex);
							EditorPins.RemoveAtSwap(i);
						}
					}
					break;

					case EEdGraphPinDirection::EGPD_Output:
					{
						int32 MatchingOutputIndex = OutputHandles.FindLastByPredicate(IsMatchingOutputHandle);
						if (INDEX_NONE != MatchingOutputIndex)
						{
							bIsNodeDirty |= SynchronizePinType(EditorModule, *EditorPins[i], OutputHandles[MatchingOutputIndex]->GetDataType());
							OutputHandles.RemoveAtSwap(MatchingOutputIndex);
							EditorPins.RemoveAtSwap(i);
						}
					}
					break;
				}
			}

			// Remove any unused editor pins.
			if (bRemoveUnusedPins)
			{
				bIsNodeDirty |= !EditorPins.IsEmpty();
				for (UEdGraphPin* Pin : EditorPins)
				{
					if (bLogChanges)
					{
						constexpr bool bIncludeNamespace = true;
						const FText NodeDisplayName = FGraphBuilder::GetDisplayName(*InNode, bIncludeNamespace);
						UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Node '%s' Pins: Removing Excess Editor Pin '%s'"), *NodeDisplayName.ToString(), *Pin->GetName());
					}
					InEditorNode.RemovePin(Pin);
				}
			}


			if (!InputHandles.IsEmpty())
			{
				bIsNodeDirty = true;
				for (FConstInputHandle& InputHandle : InputHandles)
				{
					if (bLogChanges)
					{
						constexpr bool bIncludeNamespace = true;
						const FText NodeDisplayName = FGraphBuilder::GetDisplayName(*InNode, bIncludeNamespace);
						const FText InputDisplayName = FGraphBuilder::GetDisplayName(*InputHandle);
						UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Node '%s' Pins: Adding missing Editor Input Pin '%s'"), *NodeDisplayName.ToString(), *InputDisplayName.ToString());
					}
					AddPinToNode(InEditorNode, InputHandle);
				}
			}

			if (!OutputHandles.IsEmpty())
			{
				bIsNodeDirty = true;
				for (FConstOutputHandle& OutputHandle : OutputHandles)
				{
					if (bLogChanges)
					{
						constexpr bool bIncludeNamespace = true;
						const FText NodeDisplayName = FGraphBuilder::GetDisplayName(*InNode, bIncludeNamespace);
						const FText OutputDisplayName = FGraphBuilder::GetDisplayName(*OutputHandle);
						UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Node '%s' Pins: Adding missing Editor Output Pin '%s'"), *NodeDisplayName.ToString(), *OutputDisplayName.ToString());
					}
					AddPinToNode(InEditorNode, OutputHandle);
				}
			}

			// Order pins
			GetUserModifiableHandles(InputHandles, OutputHandles);

			InNode->GetInputStyle().SortDefaults(InputHandles, [](const FConstInputHandle& Handle) { return FGraphBuilder::GetDisplayName(*Handle); });
			InNode->GetOutputStyle().SortDefaults(OutputHandles, [](const FConstOutputHandle& Handle) { return FGraphBuilder::GetDisplayName(*Handle); });

			auto SwapAndDirty = [&](int32 IndexA, int32 IndexB)
			{
				const bool bRequiresSwap = IndexA != IndexB;
				if (bRequiresSwap)
				{
					InEditorNode.Pins.Swap(IndexA, IndexB);
					bIsNodeDirty |= bRequiresSwap;
				}
			};

			for (int32 i = InEditorNode.Pins.Num() - 1; i >= 0; --i)
			{
				UEdGraphPin* Pin = InEditorNode.Pins[i];
				if (Pin->Direction == EGPD_Input)
				{
					if (!InputHandles.IsEmpty())
					{
						FConstInputHandle InputHandle = InputHandles.Pop(EAllowShrinking::No);
						for (int32 j = i; j >= 0; --j)
						{
							if (IsMatchingInputHandleAndPin(InputHandle, *InEditorNode.Pins[j]))
							{
								SwapAndDirty(i, j);
								break;
							}
						}
					}
				}
				else /* Pin->Direction == EGPD_Output */
				{
					if (!OutputHandles.IsEmpty())
					{
						FConstOutputHandle OutputHandle = OutputHandles.Pop(EAllowShrinking::No);
						for (int32 j = i; j >= 0; --j)
						{
							if (IsMatchingOutputHandleAndPin(OutputHandle, *InEditorNode.Pins[j]))
							{
								SwapAndDirty(i, j);
								break;
							}
						}
					}
				}
			}

			return bIsNodeDirty;
		}

		bool FGraphBuilder::SynchronizePinLiteral(const FMetaSoundFrontendDocumentBuilder& InBuilder, UEdGraphPin& InPin)
		{
			using namespace Engine;
			using namespace Frontend;

			if (!ensure(InPin.Direction == EGPD_Input))
			{
				return false;
			}

			const FString OldValue = InPin.DefaultValue;

			const FMetasoundFrontendVertex* InputVertex = GetPinVertex(InBuilder, &InPin);
			if (!ensure(InputVertex))
			{
				return false;
			}

			FMetasoundFrontendVertexHandle InputHandle = GetPinVertexHandle(InBuilder, &InPin);
			check(InputHandle.IsSet());

			if (const FMetasoundFrontendVertexLiteral* VertexLiteral = InBuilder.FindNodeInputDefault(InputHandle.NodeID, InputVertex->Name))
			{
				InPin.DefaultValue = VertexLiteral->Value.ToString();
				return OldValue != InPin.DefaultValue;
			}

			const TArray<FMetasoundFrontendClassInputDefault>* ClassDefaults = InBuilder.FindNodeClassInputDefaults(InputHandle.NodeID, InputVertex->Name);
			if (ClassDefaults)
			{
				const UMetasoundEditorSettings* EditorSettings = GetDefault<UMetasoundEditorSettings>();
				check(EditorSettings);

				TArray<FGuid> PageIDs;
				Algo::Transform(*ClassDefaults, PageIDs, [](const FMetasoundFrontendClassInputDefault& InputDefault) { return InputDefault.PageID; });
				const FGuid PageID = EditorSettings->ResolveAuditionPage(PageIDs, InBuilder.GetBuildPageID());
				auto MatchesPageID = [&PageID](const FMetasoundFrontendClassInputDefault& InputDefault) { return InputDefault.PageID == PageID; };
				if (const FMetasoundFrontendClassInputDefault* ClassDefault = ClassDefaults->FindByPredicate(MatchesPageID))
				{
					InPin.DefaultValue = ClassDefault->Literal.ToString();
					return OldValue != InPin.DefaultValue;
				}
			}

			FMetasoundFrontendLiteral DefaultLiteral;
			DefaultLiteral.SetFromLiteral(IDataTypeRegistry::Get().CreateDefaultLiteral(InputVertex->TypeName));

			InPin.DefaultValue = DefaultLiteral.ToString();
			return OldValue != InPin.DefaultValue;
		}

		bool FGraphBuilder::SynchronizeGraphMembers(const FMetaSoundFrontendDocumentBuilder& InBuilder, UMetasoundEditorGraph& OutGraph)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::Editor::FGraphBuilder::SynchronizeGraphMembers);

			using namespace Frontend;

			bool bEditorGraphModified = false;

			const UObject& MetaSoundObject = InBuilder.CastDocumentObjectChecked<const UObject>();
			const FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&MetaSoundObject);
			check(MetaSoundAsset);
			FConstGraphHandle GraphHandle = MetaSoundAsset->GetRootGraphHandle();

			TSet<UMetasoundEditorGraphInput*> Inputs;
			TSet<UMetasoundEditorGraphOutput*> Outputs;

			// Collect all editor graph inputs with corresponding frontend inputs. 
			GraphHandle->IterateConstNodes([&](FConstNodeHandle NodeHandle)
			{
				if (UMetasoundEditorGraphInput* Input = OutGraph.FindInput(NodeHandle->GetID()))
				{
					Inputs.Add(Input);
					return;
				}

				// Add an editor input if none exist for a frontend input.
				Inputs.Add(OutGraph.FindOrAddInput(NodeHandle));
				constexpr bool bIncludeNamespace = true;
				FText NodeDisplayName = FGraphBuilder::GetDisplayName(*NodeHandle, bIncludeNamespace);
				UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Inputs: Added missing input '%s'."), *NodeDisplayName.ToString());
				bEditorGraphModified = true;
			}, EMetasoundFrontendClassType::Input);

			// Remove stale inputs.
			{
				for (int32 Index = OutGraph.Inputs.Num() - 1; Index >= 0; --Index)
				{
					const UMetasoundEditorGraphInput* Input = OutGraph.Inputs[Index];
					check(Input);
					if (!Inputs.Contains(Input))
					{
						TArray<UMetasoundEditorGraphMemberNode*> InputNodes = Input->GetNodes();
						for (UMetasoundEditorGraphNode* Node : InputNodes)
						{
							FGraphBuilder::DeleteMemberNodes(*Input);
						}

						bEditorGraphModified = true;
						UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Outputs: Removing stale output '%s'."), *Input->GetName());
						OutGraph.Inputs.RemoveAtSwap(Index, EAllowShrinking::No);
					}
				}
			}

			for (TObjectPtr<UMetasoundEditorGraphInput> Input : OutGraph.Inputs)
			{
				bEditorGraphModified |= Input->Synchronize();
			}

			// Collect all editor graph outputs with corresponding frontend outputs. 
			GraphHandle->IterateConstNodes([&](FConstNodeHandle NodeHandle)
			{
				if (UMetasoundEditorGraphOutput* Output = OutGraph.FindOutput(NodeHandle->GetID()))
				{
					Outputs.Add(Output);
					return;
				}

				// Add an editor output if none exist for a frontend output.
				Outputs.Add(OutGraph.FindOrAddOutput(NodeHandle));
				constexpr bool bIncludeNamespace = true;
				FText NodeDisplayName = FGraphBuilder::GetDisplayName(*NodeHandle, bIncludeNamespace);
				UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Outputs: Added missing output '%s'."), *NodeDisplayName.ToString());
				bEditorGraphModified = true;
			}, EMetasoundFrontendClassType::Output);

			// Remove stale outputs.
			{
				for (int32 Index = OutGraph.Outputs.Num() - 1; Index >= 0; --Index)
				{
					const UMetasoundEditorGraphOutput* Output = OutGraph.Outputs[Index];
					check(Output);
					if (!Outputs.Contains(Output))
					{
						bEditorGraphModified = true;
						UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Outputs: Removing stale output '%s'."), *Output->GetName());
						OutGraph.Outputs.RemoveAtSwap(Index, EAllowShrinking::No);
					}
				}
			}

			for (TObjectPtr<UMetasoundEditorGraphOutput> Output : OutGraph.Outputs)
			{
				bEditorGraphModified |= Output->Synchronize();
			}

			// Enforce all variables exists in the edgraph
			TSet<FGuid> VariableIDs;
			Algo::Transform(OutGraph.Variables, VariableIDs, [](const TObjectPtr<UMetasoundEditorGraphVariable>& Variable) { return Variable->GetVariableID(); });
			TArray<FConstVariableHandle> FrontendVariables = GraphHandle->GetVariables();
			for (const FConstVariableHandle& VariableHandle : FrontendVariables)
			{
				const FGuid VariableID = VariableHandle->GetID();
				if (VariableIDs.Remove(VariableID) == 0)
				{
					UMetasoundEditorGraphVariable* NewVariable = NewObject<UMetasoundEditorGraphVariable>(&OutGraph, FName(), RF_Transactional);
					if (ensure(NewVariable))
					{
						const FMetasoundFrontendLiteral DefaultLiteral = VariableHandle->GetLiteral();
						NewVariable->InitMember(VariableHandle->GetDataType(), DefaultLiteral, VariableID);
						OutGraph.Variables.Add(NewVariable);
						bEditorGraphModified = true;
					}
				}
			}

			for (TObjectPtr<UMetasoundEditorGraphVariable> Variable : OutGraph.Variables)
			{
				bEditorGraphModified |= Variable->Synchronize();
			}

			// Remove empty entries
			bEditorGraphModified |= OutGraph.Inputs.RemoveAllSwap([](const TObjectPtr<UMetasoundEditorGraphInput>& Input) { return !Input; }) > 0;
			bEditorGraphModified |= OutGraph.Outputs.RemoveAllSwap([](const TObjectPtr<UMetasoundEditorGraphOutput>& Output) { return !Output; }) > 0;
			bEditorGraphModified |= OutGraph.Variables.RemoveAllSwap([&VariableIDs](const TObjectPtr<UMetasoundEditorGraphVariable>& Variable)
			{
				return !Variable || VariableIDs.Contains(Variable->GetVariableID());
			}) > 0;

			return bEditorGraphModified;
		}
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
