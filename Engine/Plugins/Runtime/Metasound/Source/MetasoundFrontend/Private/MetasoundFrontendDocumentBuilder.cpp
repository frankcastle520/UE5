// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundFrontendDocumentBuilder.h"

#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "Algo/Find.h"
#include "Algo/ForEach.h"
#include "Algo/NoneOf.h"
#include "Algo/Sort.h"
#include "Algo/Transform.h"
#include "AudioParameter.h"
#include "Interfaces/MetasoundFrontendInterfaceBindingRegistry.h"
#include "Interfaces/MetasoundFrontendInterfaceRegistry.h"
#include "MetasoundAssetBase.h"
#include "MetasoundAssetManager.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDataTypeRegistry.h"
#include "MetasoundFrontendDocumentCache.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentIdGenerator.h"
#include "MetasoundFrontendDocumentModifyDelegates.h"
#include "MetasoundFrontendDocumentVersioning.h"
#include "MetasoundFrontendNodeTemplateRegistry.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendRegistryKey.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundTrace.h"
#include "MetasoundVariableNodes.h"
#include "NodeTemplates/MetasoundFrontendNodeTemplateInput.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetasoundFrontendDocumentBuilder)


namespace Metasound::Frontend
{
	namespace DocumentBuilderPrivate
	{
		bool FindInputRegistryClass(FName TypeName, EMetasoundFrontendVertexAccessType AccessType, FMetasoundFrontendClass& OutClass)
		{
			switch (AccessType)
			{
				case EMetasoundFrontendVertexAccessType::Value:
				{
					return IDataTypeRegistry::Get().GetFrontendConstructorInputClass(TypeName, OutClass);
				}
				break;

				case EMetasoundFrontendVertexAccessType::Reference:
				{
					return IDataTypeRegistry::Get().GetFrontendInputClass(TypeName, OutClass);
				}
				break;

				case EMetasoundFrontendVertexAccessType::Unset:
				default:
				{
					checkNoEntry();
				}
				break;
			}

			return false;
		}

		bool FindOutputRegistryClass(FName TypeName, EMetasoundFrontendVertexAccessType AccessType, FMetasoundFrontendClass& OutClass)
		{
			switch (AccessType)
			{
				case EMetasoundFrontendVertexAccessType::Value:
				{
					return IDataTypeRegistry::Get().GetFrontendConstructorOutputClass(TypeName, OutClass);
				}
				break;

				case EMetasoundFrontendVertexAccessType::Reference:
				{
					return IDataTypeRegistry::Get().GetFrontendOutputClass(TypeName, OutClass);
				}
				break;

				case EMetasoundFrontendVertexAccessType::Unset:
				default:
				{
					checkNoEntry();
				}
				break;
			}

			return false;
		}

		bool NameContainsInterfaceNamespace(FName VertexName, FMetasoundFrontendInterface* OutInterface)
		{
			using namespace Metasound::Frontend;

			FName InterfaceNamespace;
			FName ParamName;
			Audio::FParameterPath::SplitName(VertexName, InterfaceNamespace, ParamName);

			FMetasoundFrontendInterface FoundInterface;
			if (!InterfaceNamespace.IsNone() && ISearchEngine::Get().FindInterfaceWithHighestVersion(InterfaceNamespace, FoundInterface))
			{
				if (OutInterface)
				{
					*OutInterface = MoveTemp(FoundInterface);
				}
				return true;
			}

			if (OutInterface)
			{
				*OutInterface = { };
			}
			return false;
		}

		bool IsInterfaceInput(FName InputName, FName TypeName, FMetasoundFrontendInterface* OutInterface)
		{
			FMetasoundFrontendInterface Interface;
			if (NameContainsInterfaceNamespace(InputName, &Interface))
			{
				auto IsInput = [&InputName, &TypeName](const FMetasoundFrontendClassInput& InterfaceInput)
				{
					return InputName == InterfaceInput.Name && InterfaceInput.TypeName == TypeName;
				};

				if (Interface.Inputs.ContainsByPredicate(IsInput))
				{
					if (OutInterface)
					{
						*OutInterface = MoveTemp(Interface);
					}
					return true;
				}
			}

			if (OutInterface)
			{
				*OutInterface = { };
			}
			return false;
		}

		bool IsInterfaceOutput(FName OutputName, FName TypeName, FMetasoundFrontendInterface* OutInterface)
		{
			FMetasoundFrontendInterface Interface;
			if (NameContainsInterfaceNamespace(OutputName, &Interface))
			{
				auto IsOutput = [&OutputName, &TypeName](const FMetasoundFrontendClassInput& InterfaceOutput)
				{
					return OutputName == InterfaceOutput.Name && InterfaceOutput.TypeName == TypeName;
				};

				if (Interface.Outputs.ContainsByPredicate(IsOutput))
				{
					if (OutInterface)
					{
						*OutInterface = MoveTemp(Interface);
					}
					return true;
				}
			}

			if (OutInterface)
			{
				*OutInterface = { };
			}
			return false;
		}

		bool TryGetInterfaceBoundEdges(
			const FGuid& InFromNodeID,
			const TSet<FMetasoundFrontendVersion>& InFromNodeInterfaces,
			const FGuid& InToNodeID,
			const TSet<FMetasoundFrontendVersion>& InToNodeInterfaces,
			TSet<FNamedEdge>& OutNamedEdges)
		{
			OutNamedEdges.Reset();
			TSet<FName> InputNames;
			for (const FMetasoundFrontendVersion& InputInterfaceVersion : InToNodeInterfaces)
			{
				TArray<const FInterfaceBindingRegistryEntry*> BindingEntries;
				if (IInterfaceBindingRegistry::Get().FindInterfaceBindingEntries(InputInterfaceVersion, BindingEntries))
				{
					Algo::Sort(BindingEntries, [](const FInterfaceBindingRegistryEntry* A, const FInterfaceBindingRegistryEntry* B)
					{
						check(A);
						check(B);
						return A->GetBindingPriority() < B->GetBindingPriority();
					});

					// Bindings are sorted in registry with earlier entries being higher priority to apply connections,
					// so earlier listed connections are selected over potential collisions with later entries.
					for (const FInterfaceBindingRegistryEntry* BindingEntry : BindingEntries)
					{
						check(BindingEntry);
						if (InFromNodeInterfaces.Contains(BindingEntry->GetOutputInterfaceVersion()))
						{
							for (const FMetasoundFrontendInterfaceVertexBinding& VertexBinding : BindingEntry->GetVertexBindings())
							{
								if (!InputNames.Contains(VertexBinding.InputName))
								{
									InputNames.Add(VertexBinding.InputName);
									OutNamedEdges.Add(FNamedEdge { InFromNodeID, VertexBinding.OutputName, InToNodeID, VertexBinding.InputName });
								}
							}
						}
					}
				}
			};

			return true;
		}

		void SetNodeAndVertexNames(FMetasoundFrontendNode& InOutNode, const FMetasoundFrontendClassVertex& InVertex)
		{
			InOutNode.Name = InVertex.Name;
			// Set name on related vertices of input node
			auto IsVertexWithTypeName = [&InVertex](const FMetasoundFrontendVertex& Vertex) { return Vertex.TypeName == InVertex.TypeName; };
			if (FMetasoundFrontendVertex* InputVertex = InOutNode.Interface.Inputs.FindByPredicate(IsVertexWithTypeName))
			{
				InputVertex->Name = InVertex.Name;
			}
			else
			{
				UE_LOG(LogMetaSound, Error, TEXT("Node associated with graph vertex of type '%s' does not contain input vertex of matching type."), *InVertex.TypeName.ToString());
			}

			if (FMetasoundFrontendVertex* OutputVertex = InOutNode.Interface.Outputs.FindByPredicate(IsVertexWithTypeName))
			{
				OutputVertex->Name = InVertex.Name;
			}
			else
			{
				UE_LOG(LogMetaSound, Error, TEXT("Node associated with graph vertex of type '%s' does not contain output vertex of matching type."), *InVertex.TypeName.ToString());
			}
		}

		void SetDefaultLiteralOnInputNode(FMetasoundFrontendNode& InOutNode, const FMetasoundFrontendClassInput& InClassInput)
		{
			// Set the default literal on the nodes inputs so that it gets passed to the instantiated TInputNode on a live
			// auditioned MetaSound
			auto IsVertexWithName = [&Name = InClassInput.Name](const FMetasoundFrontendVertex& InVertex)
			{
				return InVertex.Name == Name;
			};

			if (const FMetasoundFrontendVertex* InputVertex = InOutNode.Interface.Inputs.FindByPredicate(IsVertexWithName))
			{
				auto IsVertexLiteralWithVertexID = [&VertexID = InputVertex->VertexID](const FMetasoundFrontendVertexLiteral& VertexLiteral)
				{
					return VertexLiteral.VertexID == VertexID;
				};
				if (FMetasoundFrontendVertexLiteral* VertexLiteral = InOutNode.InputLiterals.FindByPredicate(IsVertexLiteralWithVertexID))
				{
					// Update existing literal default value with value from class input.
					const FMetasoundFrontendLiteral& DefaultLiteral = InClassInput.FindConstDefaultChecked(Frontend::DefaultPageID);
					VertexLiteral->Value = DefaultLiteral;
				}
				else
				{
					// Add literal default value with value from class input.
					const FMetasoundFrontendLiteral& DefaultLiteral = InClassInput.FindConstDefaultChecked(Frontend::DefaultPageID);
					InOutNode.InputLiterals.Add(FMetasoundFrontendVertexLiteral { InputVertex->VertexID, DefaultLiteral } );
				}
			}
			else
			{
				UE_LOG(LogMetaSound, Error, TEXT("Input node associated with graph input vertex of name '%s' does not contain input vertex with matching name."), *InClassInput.Name.ToString());
			}
		}

		class FModifyInterfacesImpl
		{
		public:
			FModifyInterfacesImpl(FMetasoundFrontendDocument& InDocument, FModifyInterfaceOptions&& InOptions)
				: Options(MoveTemp(InOptions))
				, Document(InDocument)
			{
				for (const FMetasoundFrontendInterface& FromInterface : Options.InterfacesToRemove)
				{
					InputsToRemove.Append(FromInterface.Inputs);
					OutputsToRemove.Append(FromInterface.Outputs);
				}

				for (const FMetasoundFrontendInterface& ToInterface : Options.InterfacesToAdd)
				{
					Algo::Transform(ToInterface.Inputs, InputsToAdd, [this, &ToInterface](const FMetasoundFrontendClassInput& Input)
					{
						FMetasoundFrontendClassInput NewInput = Input;
						NewInput.NodeID = FDocumentIDGenerator::Get().CreateNodeID(Document);
						NewInput.VertexID = FDocumentIDGenerator::Get().CreateVertexID(Document);
						return FInputInterfacePair { MoveTemp(NewInput), &ToInterface };
					});

					Algo::Transform(ToInterface.Outputs, OutputsToAdd, [this, &ToInterface](const FMetasoundFrontendClassOutput& Output)
					{
						FMetasoundFrontendClassOutput NewOutput = Output;
						NewOutput.NodeID = FDocumentIDGenerator::Get().CreateNodeID(Document);
						NewOutput.VertexID = FDocumentIDGenerator::Get().CreateVertexID(Document);
						return FOutputInterfacePair { MoveTemp(NewOutput), &ToInterface };
					});
				}

				// Iterate in reverse to allow removal from `InputsToAdd`
				for (int32 AddIndex = InputsToAdd.Num() - 1; AddIndex >= 0; AddIndex--)
				{
					const FMetasoundFrontendClassVertex& VertexToAdd = InputsToAdd[AddIndex].Key;

					const int32 RemoveIndex = InputsToRemove.IndexOfByPredicate([&](const FMetasoundFrontendClassVertex& VertexToRemove)
						{
							if (VertexToAdd.TypeName != VertexToRemove.TypeName)
							{
								return false;
							}

							if (Options.NamePairingFunction)
							{
								return Options.NamePairingFunction(VertexToAdd.Name, VertexToRemove.Name);
							}

							FName ParamA;
							FName ParamB;
							FName Namespace;
							VertexToAdd.SplitName(Namespace, ParamA);
							VertexToRemove.SplitName(Namespace, ParamB);

							return ParamA == ParamB;
						});

					if (INDEX_NONE != RemoveIndex)
					{
						PairedInputs.Add(FVertexPair { InputsToRemove[RemoveIndex], InputsToAdd[AddIndex].Key });
						InputsToRemove.RemoveAtSwap(RemoveIndex, EAllowShrinking::No);
						InputsToAdd.RemoveAtSwap(AddIndex, EAllowShrinking::No);
					}
				}

				// Iterate in reverse to allow removal from `OutputsToAdd`
				for (int32 AddIndex = OutputsToAdd.Num() - 1; AddIndex >= 0; AddIndex--)
				{
					const FMetasoundFrontendClassVertex& VertexToAdd = OutputsToAdd[AddIndex].Key;

					const int32 RemoveIndex = OutputsToRemove.IndexOfByPredicate([&](const FMetasoundFrontendClassVertex& VertexToRemove)
						{
							if (VertexToAdd.TypeName != VertexToRemove.TypeName)
							{
								return false;
							}

							if (Options.NamePairingFunction)
							{
								return Options.NamePairingFunction(VertexToAdd.Name, VertexToRemove.Name);
							}

							FName ParamA;
							FName ParamB;
							FName Namespace;
							VertexToAdd.SplitName(Namespace, ParamA);
							VertexToRemove.SplitName(Namespace, ParamB);

							return ParamA == ParamB;
						});

					if (INDEX_NONE != RemoveIndex)
					{
						PairedOutputs.Add(FVertexPair{ OutputsToRemove[RemoveIndex], OutputsToAdd[AddIndex].Key });
						OutputsToRemove.RemoveAtSwap(RemoveIndex);
						OutputsToAdd.RemoveAtSwap(AddIndex);
					}
				}
			}

		private:
			bool AddMissingVertices(FMetaSoundFrontendDocumentBuilder& OutBuilder) const
			{
				if (!InputsToAdd.IsEmpty() || !OutputsToAdd.IsEmpty())
				{
					for (const FInputInterfacePair& Pair: InputsToAdd)
					{
						OutBuilder.AddGraphInput(Pair.Key);
					}

					for (const FOutputInterfacePair& Pair : OutputsToAdd)
					{
						OutBuilder.AddGraphOutput(Pair.Key);
					}

					return true;
				}

				return false;
			}

			bool RemoveUnsupportedVertices(FMetaSoundFrontendDocumentBuilder& OutBuilder) const
			{
				bool bDidEdit = false;

				for (const TPair<FMetasoundFrontendClassInput, const FMetasoundFrontendInterface*>& Pair : InputsToAdd)
				{
					if (OutBuilder.RemoveGraphInput(Pair.Key.Name))
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Removed existing targeted input '%s' to avoid name collision/member data descrepancies while modifying interface(s). Desired edges may have been removed as a result."), *Pair.Key.Name.ToString());
						bDidEdit = true;
					}
				}

				for (const TPair<FMetasoundFrontendClassOutput, const FMetasoundFrontendInterface*>& Pair : OutputsToAdd)
				{
					if (OutBuilder.RemoveGraphOutput(Pair.Key.Name))
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Removed existing targeted output '%s' to avoid name collision/member data descrepancies while modifying interface(s). Desired edges may have been removed as a result."), *Pair.Key.Name.ToString());
						bDidEdit = true;
					}
				}

				if (!InputsToRemove.IsEmpty() || !OutputsToRemove.IsEmpty())
				{
					// Remove unsupported inputs
					for (const FMetasoundFrontendClassVertex& InputToRemove : InputsToRemove)
					{
						if (OutBuilder.RemoveGraphInput(InputToRemove.Name))
						{
							bDidEdit = true;
						}
						else
						{
							UE_LOG(LogMetaSound, Warning, TEXT("Failed to remove existing input '%s', which was an expected member of a removed interface."), *InputToRemove.Name.ToString());
						}
					}

					// Remove unsupported outputs
					for (const FMetasoundFrontendClassVertex& OutputToRemove : OutputsToRemove)
					{
						if (OutBuilder.RemoveGraphOutput(OutputToRemove.Name))
						{
							bDidEdit = true;
						}
						else
						{
							UE_LOG(LogMetaSound, Warning, TEXT("Failed to remove existing output '%s', which was an expected member of a removed interface."), *OutputToRemove.Name.ToString());
						}
					}

					return true;
				}

				return false;
			}

			bool SwapPairedVertices(FMetaSoundFrontendDocumentBuilder& OutBuilder) const
			{
				bool bDidEdit = false;
				for (const FVertexPair& PairedInput : PairedInputs)
				{
					const bool bSwapped = OutBuilder.SwapGraphInput(PairedInput.Get<0>(), PairedInput.Get<1>());
					bDidEdit |= bSwapped;
				}

				for (const FVertexPair& PairedOutput : PairedOutputs)
				{
					const bool bSwapped = OutBuilder.SwapGraphOutput(PairedOutput.Get<0>(), PairedOutput.Get<1>());
					bDidEdit |= bSwapped;
				}

				return bDidEdit;
			}

#if WITH_EDITORONLY_DATA
			void UpdateAddedVertexNodePositions(
				EMetasoundFrontendClassType ClassType,
				const FMetaSoundFrontendDocumentBuilder& InBuilder,
				TSet<FName>& AddedNames,
				TFunctionRef<int32(const FVertexName&)> InGetSortOrder,
				const FVector2D& InitOffset,
				TArrayView<FMetasoundFrontendNode> OutNodes)
			{
				// Add graph member nodes by sort order
				TSortedMap<int32, FMetasoundFrontendNode*> SortOrderToNode;
				for (FMetasoundFrontendNode& Node : OutNodes)
				{
					if (const FMetasoundFrontendClass* Class = InBuilder.FindDependency(Node.ClassID))
					{
						if (Class->Metadata.GetType() == ClassType)
						{
							const int32 Index = InGetSortOrder(Node.Name);
							SortOrderToNode.Add(Index, &Node);
						}
					}
				}

				// Prime the first location as an offset prior to an existing location (as provided by a swapped member)
				//  to avoid placing away from user's active area if possible.
				FVector2D NextLocation = InitOffset;
				{
					int32 NumBeforeDefined = 1;
					for (const TPair<int32, FMetasoundFrontendNode*>& Pair : SortOrderToNode)
					{
						const FMetasoundFrontendNode* Node = Pair.Value;
						const FName NodeName = Node->Name;
						if (AddedNames.Contains(NodeName))
						{
							NumBeforeDefined++;
						}
						else
						{
							const TMap<FGuid, FVector2D>& Locations = Node->Style.Display.Locations;
							if (!Locations.IsEmpty())
							{
								for (const TPair<FGuid, FVector2D>& Location : Locations)
								{
									NextLocation = Location.Value - (NumBeforeDefined * DisplayStyle::NodeLayout::DefaultOffsetY);
									break;
								}
								break;
							}
						}
					}
				}

				// Iterate through sorted map in sequence, slotting in new locations after
				// existing swapped nodes with predefined locations relative to one another.
				for (TPair<int32, FMetasoundFrontendNode*>& Pair : SortOrderToNode)
				{
					FMetasoundFrontendNode* Node = Pair.Value;
					const FName NodeName = Node->Name;
					if (AddedNames.Contains(NodeName))
					{
						bool bAddedLocation = false;
						for (TPair<FGuid, FVector2D>& LocationPair : Node->Style.Display.Locations)
						{
							bAddedLocation = true;
							LocationPair.Value = NextLocation;
						}
						if (!bAddedLocation)
						{
							Node->Style.Display.Locations.Add(FGuid::NewGuid(), NextLocation);
						}
						NextLocation += DisplayStyle::NodeLayout::DefaultOffsetY;
					}
					else
					{
						for (const TPair<FGuid, FVector2D>& Location : Node->Style.Display.Locations)
						{
							NextLocation = Location.Value + DisplayStyle::NodeLayout::DefaultOffsetY;
						}
					}
				}
			}
#endif // WITH_EDITORONLY_DATA

		public:
			bool Execute(FMetaSoundFrontendDocumentBuilder& OutBuilder, FDocumentModifyDelegates& OutDelegates)
			{
				bool bDidEdit = false;

				for (const FMetasoundFrontendInterface& Interface : Options.InterfacesToRemove)
				{
					if (Document.Interfaces.Contains(Interface.Version))
					{
						OutDelegates.InterfaceDelegates.OnRemovingInterface.Broadcast(Interface);
						bDidEdit = true;
#if WITH_EDITORONLY_DATA
						Document.Metadata.ModifyContext.AddInterfaceModified(Interface.Version.Name);
#endif // WITH_EDITORONLY_DATA
						Document.Interfaces.Remove(Interface.Version);
					}
				}

				for (const FMetasoundFrontendInterface& Interface : Options.InterfacesToAdd)
				{
					bool bAlreadyInSet = false;
					Document.Interfaces.Add(Interface.Version, &bAlreadyInSet);
					if (!bAlreadyInSet)
					{
						OutDelegates.InterfaceDelegates.OnInterfaceAdded.Broadcast(Interface);
						bDidEdit = true;
#if WITH_EDITORONLY_DATA
						Document.Metadata.ModifyContext.AddInterfaceModified(Interface.Version.Name);
#endif // WITH_EDITORONLY_DATA
					}
				}

				bDidEdit |= RemoveUnsupportedVertices(OutBuilder);
				bDidEdit |= SwapPairedVertices(OutBuilder);
				const bool bAddedVertices = AddMissingVertices(OutBuilder);
				bDidEdit |= bAddedVertices;

				if (bDidEdit)
				{
					OutBuilder.RemoveUnusedDependencies();
				}

#if WITH_EDITORONLY_DATA
				if (bAddedVertices && Options.bSetDefaultNodeLocations && !IsRunningCookCommandlet())
				{
					Document.RootGraph.IterateGraphPages([&](FMetasoundFrontendGraph& Graph)
					{
						TArray<FMetasoundFrontendNode>& Nodes = Graph.Nodes;
						// Sort/Place Inputs
						{
							TSet<FName> NamesToSort;
							Algo::Transform(InputsToAdd, NamesToSort, [](const FInputInterfacePair& Pair) { return Pair.Key.Name; });
							auto GetInputSortOrder = [&OutBuilder](const FVertexName& InVertexName)
							{
								const FMetasoundFrontendClassInput* Input = OutBuilder.FindGraphInput(InVertexName);
								checkf(Input, TEXT("Input must exist by this point of modifying the document's interfaces and respective members"));
								return Input->Metadata.SortOrderIndex;
							};
							UpdateAddedVertexNodePositions(EMetasoundFrontendClassType::Input, OutBuilder, NamesToSort, GetInputSortOrder, FVector2D::Zero(), Nodes);
						}

						// Sort/Place Outputs
						{
							TSet<FName> NamesToSort;
							Algo::Transform(OutputsToAdd, NamesToSort, [](const FOutputInterfacePair& OutputInterfacePair) { return OutputInterfacePair.Key.Name; });
							auto GetOutputSortOrder = [&OutBuilder](const FVertexName& InVertexName)
							{
								const FMetasoundFrontendClassOutput* Output = OutBuilder.FindGraphOutput(InVertexName);
								checkf(Output, TEXT("Output must exist by this point of modifying the document's interfaces and respective members"));
								return Output->Metadata.SortOrderIndex;
							};
							UpdateAddedVertexNodePositions(EMetasoundFrontendClassType::Output, OutBuilder, NamesToSort, GetOutputSortOrder, 3 * DisplayStyle::NodeLayout::DefaultOffsetX, Nodes);
						}
					});
				}
#endif // WITH_EDITORONLY_DATA

				return bDidEdit;
			}

			const FModifyInterfaceOptions Options;

		private:
			FMetasoundFrontendDocument& Document;

			using FVertexPair = TTuple<FMetasoundFrontendClassVertex, FMetasoundFrontendClassVertex>;
			TArray<FVertexPair> PairedInputs;
			TArray<FVertexPair> PairedOutputs;

			using FInputInterfacePair = TPair<FMetasoundFrontendClassInput, const FMetasoundFrontendInterface*>;
			using FOutputInterfacePair = TPair<FMetasoundFrontendClassOutput, const FMetasoundFrontendInterface*>;
			TArray<FInputInterfacePair> InputsToAdd;
			TArray<FOutputInterfacePair> OutputsToAdd;

			TArray<FMetasoundFrontendClassInput> InputsToRemove;
			TArray<FMetasoundFrontendClassOutput> OutputsToRemove;
		};
	} // namespace DocumentBuilderPrivate

	FString LexToString(const EInvalidEdgeReason& InReason)
	{
		switch (InReason)
		{
			case EInvalidEdgeReason::None:
				return TEXT("No reason");

			case EInvalidEdgeReason::MismatchedAccessType:
				return TEXT("Mismatched Access Type");

			case EInvalidEdgeReason::MismatchedDataType:
				return TEXT("Mismatched DataType");

			case EInvalidEdgeReason::MissingInput:
				return TEXT("Missing Input");

			case EInvalidEdgeReason::MissingOutput:
				return TEXT("Missing Output");

			default:
				return TEXT("COUNT");
		}

		static_assert(static_cast<uint32>(EInvalidEdgeReason::COUNT) == 5, "Potential missing case coverage for EInvalidEdgeReason");
	}

	FModifyInterfaceOptions::FModifyInterfaceOptions(const TArray<FMetasoundFrontendInterface>& InInterfacesToRemove, const TArray<FMetasoundFrontendInterface>& InInterfacesToAdd)
		: InterfacesToRemove(InInterfacesToRemove)
		, InterfacesToAdd(InInterfacesToAdd)
	{
	}

	FModifyInterfaceOptions::FModifyInterfaceOptions(TArray<FMetasoundFrontendInterface>&& InInterfacesToRemove, TArray<FMetasoundFrontendInterface>&& InInterfacesToAdd)
		: InterfacesToRemove(MoveTemp(InInterfacesToRemove))
		, InterfacesToAdd(MoveTemp(InInterfacesToAdd))
	{
	}

	FModifyInterfaceOptions::FModifyInterfaceOptions(const TArray<FMetasoundFrontendVersion>& InInterfaceVersionsToRemove, const TArray<FMetasoundFrontendVersion>& InInterfaceVersionsToAdd)
	{
		Algo::Transform(InInterfaceVersionsToRemove, InterfacesToRemove, [](const FMetasoundFrontendVersion& Version)
		{
			FMetasoundFrontendInterface Interface;
			const bool bFromInterfaceFound = IInterfaceRegistry::Get().FindInterface(GetInterfaceRegistryKey(Version), Interface);
			if (!ensureAlways(bFromInterfaceFound))
			{
				UE_LOG(LogMetaSound, Error, TEXT("Failed to find interface '%s' to remove"), *Version.ToString());
			}
			return Interface;
		});

		Algo::Transform(InInterfaceVersionsToAdd, InterfacesToAdd, [](const FMetasoundFrontendVersion& Version)
		{
			FMetasoundFrontendInterface Interface;
			const bool bToInterfaceFound = IInterfaceRegistry::Get().FindInterface(GetInterfaceRegistryKey(Version), Interface);
			if (!ensureAlways(bToInterfaceFound))
			{
				UE_LOG(LogMetaSound, Error, TEXT("Failed to find interface '%s' to add"), *Version.ToString());
			}
			return Interface;
		});
	}
} // namespace Metasound::Frontend

UMetaSoundBuilderDocument& UMetaSoundBuilderDocument::Create(const UClass& InMetaSoundUClass)
{
	UMetaSoundBuilderDocument* DocObject = NewObject<UMetaSoundBuilderDocument>();
	check(DocObject);
	DocObject->MetaSoundUClass = &InMetaSoundUClass;
	return *DocObject;
}

UMetaSoundBuilderDocument& UMetaSoundBuilderDocument::Create(const IMetaSoundDocumentInterface& InDocToCopy)
{
	UMetaSoundBuilderDocument* DocObject = NewObject<UMetaSoundBuilderDocument>();
	check(DocObject);
	DocObject->Document = InDocToCopy.GetConstDocument();
	DocObject->MetaSoundUClass = InDocToCopy.GetBaseMetaSoundUClass();
	DocObject->BuilderUClass = InDocToCopy.GetBuilderUClass();
	return *DocObject;
}

bool UMetaSoundBuilderDocument::ConformObjectToDocument()
{
	return false;
}

FTopLevelAssetPath UMetaSoundBuilderDocument::GetAssetPathChecked() const
{
	FTopLevelAssetPath Path;
	ensureAlwaysMsgf(Path.TrySetPath(this), TEXT("Failed to set TopLevelAssetPath from transient MetaSound '%s'. MetaSound must be highest level object in package."), *GetPathName());
	ensureAlwaysMsgf(Path.IsValid(), TEXT("Failed to set TopLevelAssetPath from MetaSound '%s'. This may be caused by calling this function when the asset is being destroyed."), *GetPathName());
	return Path;
}

const FMetasoundFrontendDocument& UMetaSoundBuilderDocument::GetConstDocument() const
{
	return Document;
}

const UClass& UMetaSoundBuilderDocument::GetBaseMetaSoundUClass() const
{
	checkf(MetaSoundUClass, TEXT("BaseMetaSoundUClass must be set upon creation of UMetaSoundBuilderDocument instance"));
	return *MetaSoundUClass;
}

const UClass& UMetaSoundBuilderDocument::GetBuilderUClass() const
{
	checkf(BuilderUClass, TEXT("BuilderUClass must be set upon creation of UMetaSoundBuilderDocument instance"));
	return *BuilderUClass;
}

bool UMetaSoundBuilderDocument::IsActivelyBuilding() const
{
	return true;
}

FMetasoundFrontendDocument& UMetaSoundBuilderDocument::GetDocument()
{
	return Document;
}

void UMetaSoundBuilderDocument::OnBeginActiveBuilder()
{
	// Nothing to do here. UMetaSoundBuilderDocuments are always being used by builders
}

void UMetaSoundBuilderDocument::OnFinishActiveBuilder()
{
	// Nothing to do here. UMetaSoundBuilderDocuments are always being used by builders
}

FMetaSoundFrontendDocumentBuilder::FMetaSoundFrontendDocumentBuilder(TScriptInterface<IMetaSoundDocumentInterface> InDocumentInterface, TSharedPtr<Metasound::Frontend::FDocumentModifyDelegates> InDocumentDelegates, bool bPrimeCache)
	: DocumentInterface(InDocumentInterface)
{
	BeginBuilding(InDocumentDelegates, bPrimeCache);
}

FMetaSoundFrontendDocumentBuilder::~FMetaSoundFrontendDocumentBuilder()
{
	FinishBuilding();
}

const FMetasoundFrontendClass* FMetaSoundFrontendDocumentBuilder::AddDependency(const FMetasoundFrontendClass& InClass)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	const FMetasoundFrontendClass* Dependency = nullptr;

	FMetasoundFrontendClass NewDependency = InClass;

	// All 'Graph' dependencies are listed as 'External' from the perspective of the owning document.
	// This makes them implementation agnostic to accommodate nativization of assets.
	if (NewDependency.Metadata.GetType() == EMetasoundFrontendClassType::Graph)
	{
		NewDependency.Metadata.SetType(EMetasoundFrontendClassType::External);
	}

	NewDependency.ID = FDocumentIDGenerator::Get().CreateClassID(Document);
	Dependency = &Document.Dependencies.Emplace_GetRef(MoveTemp(NewDependency));

	const int32 NewIndex = Document.Dependencies.Num() - 1;
	DocumentDelegates->OnDependencyAdded.Broadcast(NewIndex);

	return Dependency;
}

void FMetaSoundFrontendDocumentBuilder::AddEdge(FMetasoundFrontendEdge&& InNewEdge, const FGuid* InPageID)
{
	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;

#if DO_CHECK
	{
		const IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache(PageID);
		checkf(!EdgeCache.IsNodeInputConnected(InNewEdge.ToNodeID, InNewEdge.ToVertexID), TEXT("Failed to add edge in MetaSound Builder: Destination input already connected"));

		const EInvalidEdgeReason Reason = IsValidEdge(InNewEdge, &PageID);
		checkf(Reason == Metasound::Frontend::EInvalidEdgeReason::None, TEXT("Attempted call to AddEdge in MetaSound Builder where edge is invalid: %s."), *LexToString(Reason));
	}
#endif // DO_CHECK

	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	FMetasoundFrontendGraph& Graph = Document.RootGraph.FindGraphChecked(PageID);
	Graph.Edges.Add(MoveTemp(InNewEdge));
	const int32 NewIndex = Graph.Edges.Num() - 1;
	DocumentDelegates->FindEdgeDelegatesChecked(PageID).OnEdgeAdded.Broadcast(NewIndex);
}

bool FMetaSoundFrontendDocumentBuilder::AddNamedEdges(const TSet<Metasound::Frontend::FNamedEdge>& EdgesToMake, TArray<const FMetasoundFrontendEdge*>* OutNewEdges, bool bReplaceExistingConnections, const FGuid* InPageID)
{
	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;

	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
	FMetasoundFrontendGraph& Graph = Document.RootGraph.FindGraphChecked(PageID);

	if (OutNewEdges)
	{
		OutNewEdges->Reset();
	}

	bool bSuccess = true;

	struct FNewEdgeData
	{
		FMetasoundFrontendEdge NewEdge;
		const FMetasoundFrontendVertex* OutputVertex = nullptr;
		const FMetasoundFrontendVertex* InputVertex = nullptr;
	};

	TArray<FNewEdgeData> EdgesToAdd;
	for (const FNamedEdge& Edge : EdgesToMake)
	{
		const FMetasoundFrontendVertex* OutputVertex = NodeCache.FindOutputVertex(Edge.OutputNodeID, Edge.OutputName);
		const FMetasoundFrontendVertex* InputVertex = NodeCache.FindInputVertex(Edge.InputNodeID, Edge.InputName);

		if (OutputVertex && InputVertex)
		{
			FMetasoundFrontendEdge NewEdge = { Edge.OutputNodeID, OutputVertex->VertexID, Edge.InputNodeID, InputVertex->VertexID };
			const EInvalidEdgeReason InvalidEdgeReason = IsValidEdge(NewEdge);
			if (InvalidEdgeReason == EInvalidEdgeReason::None)
			{
				EdgesToAdd.Add(FNewEdgeData { MoveTemp(NewEdge), OutputVertex, InputVertex });
			}
			else
			{
				bSuccess = false;
				UE_LOG(LogMetaSound, Error, TEXT("Failed to add connections between MetaSound output '%s' and input '%s': '%s'."), *Edge.OutputName.ToString(), *Edge.InputName.ToString(), *LexToString(InvalidEdgeReason));
			}
		}
	}

	const TArray<FMetasoundFrontendEdge>& Edges = Graph.Edges;
	const int32 LastIndex = Edges.Num() - 1;
	for (FNewEdgeData& EdgeToAdd : EdgesToAdd)
	{
		if (bReplaceExistingConnections)
		{
#if !NO_LOGGING
			const FMetasoundFrontendNode* OldOutputNode = nullptr;
			const FMetasoundFrontendVertex* OldOutputVertex = FindNodeOutputConnectedToNodeInput(EdgeToAdd.NewEdge.ToNodeID, EdgeToAdd.NewEdge.ToVertexID, &OldOutputNode, &PageID);
#endif // !NO_LOGGING

			const bool bRemovedEdge = RemoveEdgeToNodeInput(EdgeToAdd.NewEdge.ToNodeID, EdgeToAdd.NewEdge.ToVertexID, &PageID);

#if !NO_LOGGING
			if (bRemovedEdge)
			{
				checkf(OldOutputNode, TEXT("MetaSound edge was removed from output but output node not found."));
				checkf(OldOutputVertex, TEXT("MetaSound edge was removed from output but output vertex not found."));

				const FMetasoundFrontendNode* InputNode = FindNode(EdgeToAdd.NewEdge.ToNodeID);
				checkf(InputNode, TEXT("Edge was deemed valid but input parent node is missing"));

				const FMetasoundFrontendNode* OutputNode = FindNode(EdgeToAdd.NewEdge.FromNodeID);
				checkf(OutputNode, TEXT("Edge was deemed valid but output parent node is missing"));

				UE_LOG(LogMetaSound, Verbose, TEXT("Removed connection from node output '%s:%s' to node '%s:%s' in order to connect to node output '%s:%s'"),
					*OldOutputNode->Name.ToString(),
					*OldOutputVertex->Name.ToString(),
					*InputNode->Name.ToString(),
					*EdgeToAdd.InputVertex->Name.ToString(),
					*OutputNode->Name.ToString(),
					*EdgeToAdd.OutputVertex->Name.ToString());
			}
#endif // !NO_LOGGING

			AddEdge(MoveTemp(EdgeToAdd.NewEdge), &PageID);
		}
		else if (!IsNodeInputConnected(EdgeToAdd.NewEdge.ToNodeID, EdgeToAdd.NewEdge.ToVertexID, &PageID))
		{
			AddEdge(MoveTemp(EdgeToAdd.NewEdge), &PageID);
		}
		else
		{
			bSuccess = false;

#if !NO_LOGGING
			FMetasoundFrontendEdge EdgeToRemove;
			if (const int32* EdgeIndex = DocumentCache->GetEdgeCache(PageID).FindEdgeIndexToNodeInput(EdgeToAdd.NewEdge.ToNodeID, EdgeToAdd.NewEdge.ToVertexID))
			{
				EdgeToRemove = Graph.Edges[*EdgeIndex];
			}

			const FMetasoundFrontendVertex* Input = FindNodeInput(EdgeToAdd.NewEdge.ToNodeID, EdgeToAdd.NewEdge.ToVertexID, &PageID);
			checkf(Input, TEXT("Prior loop to check edge validity should protect against missing input vertex"));

			const FMetasoundFrontendVertex* Output = FindNodeOutput(EdgeToAdd.NewEdge.FromNodeID, EdgeToAdd.NewEdge.FromVertexID, &PageID);
			checkf(Input, TEXT("Prior loop to check edge validity should protect against missing output vertex"));

			UE_LOG(LogMetaSound, Warning, TEXT("Connection between MetaSound output '%s' and input '%s' not added: Input already connected to '%s'."), *Output->Name.ToString(), *Input->Name.ToString(), *Output->Name.ToString());
#endif // !NO_LOGGING
		}
	}

	if (OutNewEdges)
	{
		for (int32 Index = LastIndex + 1; Index < Edges.Num(); ++Index)
		{
			OutNewEdges->Add(&Edges[Index]);
		}
	}

	return bSuccess;
}

bool FMetaSoundFrontendDocumentBuilder::AddEdgesByNodeClassInterfaceBindings(const FGuid& InFromNodeID, const FGuid& InToNodeID, bool bReplaceExistingConnections, const FGuid* InPageID)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;

	TSet<FMetasoundFrontendVersion> FromInterfaceVersions;
	TSet<FMetasoundFrontendVersion> ToInterfaceVersions;
	if (FindNodeClassInterfaces(InFromNodeID, FromInterfaceVersions, PageID) && FindNodeClassInterfaces(InToNodeID, ToInterfaceVersions, PageID))
	{
		TSet<FNamedEdge> NamedEdges;
		if (DocumentBuilderPrivate::TryGetInterfaceBoundEdges(InFromNodeID, FromInterfaceVersions, InToNodeID, ToInterfaceVersions, NamedEdges))
		{
			return AddNamedEdges(NamedEdges, nullptr, bReplaceExistingConnections, &PageID);
		}
	}

	return false;

}

bool FMetaSoundFrontendDocumentBuilder::AddEdgesFromMatchingInterfaceNodeOutputsToGraphOutputs(const FGuid& InNodeID, TArray<const FMetasoundFrontendEdge*>& OutEdgesCreated, bool bReplaceExistingConnections, const FGuid* InPageID)
{
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FMetaSoundFrontendDocumentBuilder::AddEdgesFromMatchingInterfaceNodeOutputsToGraphOutputs);

	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;

	OutEdgesCreated.Reset();

	TSet<FMetasoundFrontendVersion> NodeInterfaces;
	if (!FindNodeClassInterfaces(InNodeID, NodeInterfaces, PageID))
	{
		// Did not find any node interfaces
		return false;
	}

	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
	const IDocumentGraphInterfaceCache& InterfaceCache = DocumentCache->GetInterfaceCache();
	const TSet<FMetasoundFrontendVersion> CommonInterfaces = NodeInterfaces.Intersect(GetDocumentChecked().Interfaces);

	TSet<FNamedEdge> EdgesToMake;
	for (const FMetasoundFrontendVersion& Version : CommonInterfaces)
	{
		const FInterfaceRegistryKey InterfaceKey = GetInterfaceRegistryKey(Version);
		if (const IInterfaceRegistryEntry* RegistryEntry = IInterfaceRegistry::Get().FindInterfaceRegistryEntry(InterfaceKey))
		{
			Algo::Transform(RegistryEntry->GetInterface().Outputs, EdgesToMake, [this, &NodeCache, &InterfaceCache, InNodeID](const FMetasoundFrontendClassOutput& Output)
			{
				const FMetasoundFrontendGraph& Graph = FindConstBuildGraphChecked();
				const FMetasoundFrontendVertex* NodeVertex = NodeCache.FindOutputVertex(InNodeID, Output.Name);
				check(NodeVertex);
				const FMetasoundFrontendClassOutput* OutputClass = InterfaceCache.FindOutput(Output.Name);
				check(OutputClass);
				const FMetasoundFrontendNode* OutputNode = NodeCache.FindNode(OutputClass->NodeID);
				check(OutputNode);
				const TArray<FMetasoundFrontendVertex>& Inputs = OutputNode->Interface.Inputs;
				check(!Inputs.IsEmpty());
				return FNamedEdge { InNodeID, NodeVertex->Name, OutputNode->GetID(), Inputs.Last().Name };
			});
		}
	}

	return AddNamedEdges(EdgesToMake, &OutEdgesCreated, bReplaceExistingConnections, &PageID);
}

bool FMetaSoundFrontendDocumentBuilder::AddEdgesFromMatchingInterfaceNodeInputsToGraphInputs(const FGuid& InNodeID, TArray<const FMetasoundFrontendEdge*>& OutEdgesCreated, bool bReplaceExistingConnections, const FGuid* InPageID)
{
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FMetaSoundFrontendDocumentBuilder::AddEdgesFromMatchingInterfaceNodeInputsToGraphInputs);

	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;

	OutEdgesCreated.Reset();

	TSet<FMetasoundFrontendVersion> NodeInterfaces;
	if (!FindNodeClassInterfaces(InNodeID, NodeInterfaces, PageID))
	{
		// Did not find any node interfaces
		return false;
	}

	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
	const IDocumentGraphInterfaceCache& InterfaceCache = DocumentCache->GetInterfaceCache();
	const TSet<FMetasoundFrontendVersion> CommonInterfaces = NodeInterfaces.Intersect(GetDocumentChecked().Interfaces);

	TSet<FNamedEdge> EdgesToMake;
	const FMetasoundFrontendGraph& Graph = GetDocumentChecked().RootGraph.FindConstGraphChecked(PageID);
	for (const FMetasoundFrontendVersion& Version : CommonInterfaces)
	{
		const FInterfaceRegistryKey InterfaceKey = GetInterfaceRegistryKey(Version);
		if (const IInterfaceRegistryEntry* RegistryEntry = IInterfaceRegistry::Get().FindInterfaceRegistryEntry(InterfaceKey))
		{
			Algo::Transform(RegistryEntry->GetInterface().Inputs, EdgesToMake, [this, &Graph, &NodeCache, &InterfaceCache, InNodeID](const FMetasoundFrontendClassInput& Input)
			{
				const FMetasoundFrontendVertex* NodeVertex = NodeCache.FindInputVertex(InNodeID, Input.Name);
				check(NodeVertex);
				const FMetasoundFrontendClassInput* InputClass = InterfaceCache.FindInput(Input.Name);
				check(InputClass);
				const FMetasoundFrontendNode* InputNode = NodeCache.FindNode(InputClass->NodeID);
				check(InputNode);
				const TArray<FMetasoundFrontendVertex>& Outputs = InputNode->Interface.Outputs;
				check(!Outputs.IsEmpty());
				return FNamedEdge { InputNode->GetID(), Outputs.Last().Name, InNodeID, NodeVertex->Name };
			});
		}
	}

	return AddNamedEdges(EdgesToMake, &OutEdgesCreated, bReplaceExistingConnections, &PageID);
}

const FMetasoundFrontendNode* FMetaSoundFrontendDocumentBuilder::AddGraphInput(const FMetasoundFrontendClassInput& InClassInput, const FGuid* InPageID)
{
	using namespace Metasound::Frontend;

	checkf(InClassInput.NodeID.IsValid(), TEXT("Unassigned NodeID when adding graph input"));
	checkf(InClassInput.VertexID.IsValid(), TEXT("Unassigned VertexID when adding graph input"));

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	if (InClassInput.TypeName.IsNone())
	{
		UE_LOG(LogMetaSound, Error, TEXT("TypeName unset when attempting to add class input '%s'"), *InClassInput.Name.ToString());
		return nullptr;
	}
	else if (const FMetasoundFrontendClassInput* Input = DocumentCache->GetInterfaceCache().FindInput(InClassInput.Name))
	{
		UE_LOG(LogMetaSound, Error, TEXT("Attempting to add MetaSound graph input '%s' when input with name already exists"), *InClassInput.Name.ToString());
		const FMetasoundFrontendNode* OutputNode = DocumentCache->GetNodeCache(PageID).FindNode(Input->NodeID);
		check(OutputNode);
		return OutputNode;
	}
	else if (!IDataTypeRegistry::Get().IsRegistered(InClassInput.TypeName))
	{
		UE_LOG(LogMetaSound, Error, TEXT("Cannot add MetaSound graph input '%s' with unregistered TypeName '%s'"), *InClassInput.Name.ToString(), *InClassInput.TypeName.ToString());
		return nullptr;
	}

	FMetasoundFrontendClass Class;
	if (DocumentBuilderPrivate::FindInputRegistryClass(InClassInput.TypeName, InClassInput.AccessType, Class))
	{
		if(!FindDependency(Class.Metadata))
		{
			AddDependency(Class);
		}

		FMetasoundFrontendDocument& Document = GetDocumentChecked();
		FMetasoundFrontendGraphClass& RootGraph = Document.RootGraph;

		auto FinalizeNode = [this, &InClassInput](FMetasoundFrontendNode& InOutNode, const Metasound::Frontend::FNodeRegistryKey&)
		{
			// Sets the name of the node an vertices on the node to match the class vertex name
			DocumentBuilderPrivate::SetNodeAndVertexNames(InOutNode, InClassInput);

			// Set the default literal on the nodes inputs so that it gets passed to the instantiated TInputNode on a live
			// auditioned MetaSound.
			DocumentBuilderPrivate::SetDefaultLiteralOnInputNode(InOutNode, InClassInput);
		};

#if WITH_EDITORONLY_DATA
		bool bIsRequired = false;
		FMetasoundFrontendInterface Interface;
		if (DocumentBuilderPrivate::IsInterfaceInput(InClassInput.Name, InClassInput.TypeName, &Interface))
		{
			if (Document.Interfaces.Contains(Interface.Version))
			{
				FText RequiredText;
				bIsRequired = Interface.IsMemberInputRequired(InClassInput.Name, RequiredText);
			}
		}
#endif // WITH_EDITORONLY_DATA

		// Must add input node to all paged graphs to maintain API parity for all page implementations
		FMetasoundFrontendNode* NewNode = nullptr;
		RootGraph.IterateGraphPages([&](const FMetasoundFrontendGraph& Graph)
		{
			constexpr int32* NewNodeIndex = nullptr;
			FMetasoundFrontendNode* NewPageNode = AddNodeInternal(Class.Metadata, FinalizeNode, Graph.PageID, InClassInput.NodeID, NewNodeIndex);
			if (Graph.PageID == PageID)
			{
				NewNode = NewPageNode;
			}

#if WITH_EDITORONLY_DATA
			if (bIsRequired)
			{
				// LocationGuid corresponds with the assigned editor graph node guid when dynamically created.
				// This is added if this is an interface member that is required to force page to create visual
				// representation that can inform the user of its required state.
				FGuid LocationGuid = FDocumentIDGenerator::Get().CreateVertexID(Document);
				SetNodeLocation(InClassInput.NodeID, FVector2D::ZeroVector, &LocationGuid, &Graph.PageID);
			}
#endif // WITH_EDITORONLY_DATA

			// Remove the default literal on the node added during the "FinalizeNode" call. This matches how 
			// nodes are serialized in editor. The default literals are only stored on the FMetasoundFrontendClassInputs.
			NewPageNode->InputLiterals.Reset();
		});

		if (NewNode)
		{
			const int32 NewIndex = RootGraph.Interface.Inputs.Num();
			FMetasoundFrontendClassInput& NewInput = RootGraph.Interface.Inputs.Add_GetRef(InClassInput);
			if (!NewInput.VertexID.IsValid())
			{
				NewInput.VertexID = FDocumentIDGenerator::Get().CreateVertexID(Document);
			}

			DocumentDelegates->InterfaceDelegates.OnInputAdded.Broadcast(NewIndex);
#if WITH_EDITORONLY_DATA
			Document.Metadata.ModifyContext.AddMemberIDModified(InClassInput.NodeID);
#endif // WITH_EDITORONLY_DATA

			return NewNode;
		}
	}

	return nullptr;
}

const FMetasoundFrontendNode* FMetaSoundFrontendDocumentBuilder::AddGraphOutput(const FMetasoundFrontendClassOutput& InClassOutput, const FGuid* InPageID)
{
	using namespace Metasound::Frontend;

	checkf(InClassOutput.NodeID.IsValid(), TEXT("Unassigned NodeID when adding graph output"));
	checkf(InClassOutput.VertexID.IsValid(), TEXT("Unassigned VertexID when adding graph output"));

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	if (InClassOutput.TypeName.IsNone())
	{
		UE_LOG(LogMetaSound, Error, TEXT("TypeName unset when attempting to add class output '%s'"), *InClassOutput.Name.ToString());
		return nullptr;
	}
	else if (const FMetasoundFrontendClassOutput* Output = DocumentCache->GetInterfaceCache().FindOutput(InClassOutput.Name))
	{
		UE_LOG(LogMetaSound, Error, TEXT("Attempting to add MetaSound graph output '%s' when output with name already exists"), *InClassOutput.Name.ToString());
		return DocumentCache->GetNodeCache(PageID).FindNode(Output->NodeID);
	}
	else if (!IDataTypeRegistry::Get().IsRegistered(InClassOutput.TypeName))
	{
		UE_LOG(LogMetaSound, Error, TEXT("Cannot add MetaSound graph output '%s' with unregistered TypeName '%s'"), *InClassOutput.Name.ToString(), *InClassOutput.TypeName.ToString());
		return nullptr;
	}

	FMetasoundFrontendClass Class;
	if (DocumentBuilderPrivate::FindOutputRegistryClass(InClassOutput.TypeName, InClassOutput.AccessType, Class))
	{
		if (!FindDependency(Class.Metadata))
		{
			AddDependency(Class);
		}

		auto FinalizeNode = [&InClassOutput](FMetasoundFrontendNode& InOutNode, const Metasound::Frontend::FNodeRegistryKey&)
		{
			DocumentBuilderPrivate::SetNodeAndVertexNames(InOutNode, InClassOutput);
		};

#if WITH_EDITORONLY_DATA
		bool bIsRequired = false;
		FMetasoundFrontendInterface Interface;
		if (DocumentBuilderPrivate::IsInterfaceOutput(InClassOutput.Name, InClassOutput.TypeName, &Interface))
		{
			FText RequiredText;
			bIsRequired = Interface.IsMemberOutputRequired(InClassOutput.Name, RequiredText);
		}
#endif // WITH_EDITORONLY_DATA

		bool bAddedNodes = true;
		FMetasoundFrontendNode* NewNodeToReturn = nullptr;
		FMetasoundFrontendDocument& Document = GetDocumentChecked();
		FMetasoundFrontendGraphClass& RootGraph = Document.RootGraph;
		Document.RootGraph.IterateGraphPages([&](FMetasoundFrontendGraph& Graph)
		{
			FMetasoundFrontendNode* NewNode = AddNodeInternal(Class.Metadata, FinalizeNode, Graph.PageID, InClassOutput.NodeID);
			if (Graph.PageID == PageID)
			{
				NewNodeToReturn = NewNode;
			}

#if WITH_EDITORONLY_DATA
			if (bIsRequired)
			{
				// LocationGuid corresponds with the assigned editor graph node guid when dynamically created.
				// This is added if this is an interface member that is required to force page to create visual
				// representation that can inform the user of its required state.
				FGuid LocationGuid = FDocumentIDGenerator::Get().CreateVertexID(Document);
				SetNodeLocation(InClassOutput.NodeID, FVector2D::ZeroVector, &LocationGuid, &Graph.PageID);
			}
#endif // WITH_EDITORONLY_DATA

			bAddedNodes &= NewNode != nullptr;
		});

		if (bAddedNodes)
		{
			const int32 NewIndex = RootGraph.Interface.Outputs.Num();
			FMetasoundFrontendClassOutput& NewOutput = RootGraph.Interface.Outputs.Add_GetRef(InClassOutput);
			if (!NewOutput.VertexID.IsValid())
			{
				NewOutput.VertexID = FDocumentIDGenerator::Get().CreateVertexID(Document);
			}

			DocumentDelegates->InterfaceDelegates.OnOutputAdded.Broadcast(NewIndex);
#if WITH_EDITORONLY_DATA
			Document.Metadata.ModifyContext.AddMemberIDModified(InClassOutput.NodeID);
#endif // WITH_EDITORONLY_DATA
		}

		check(NewNodeToReturn);
		return NewNodeToReturn;
	}

	return nullptr;
}

bool FMetaSoundFrontendDocumentBuilder::AddInterface(FName InterfaceName)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendInterface Interface;
	if (ISearchEngine::Get().FindInterfaceWithHighestVersion(InterfaceName, Interface))
	{
		if (GetDocumentChecked().Interfaces.Contains(Interface.Version))
		{
			UE_LOG(LogMetaSound, VeryVerbose, TEXT("MetaSound interface '%s' already found on document. MetaSoundBuilder skipping add request."), *InterfaceName.ToString());
			return true;
		}

		const FTopLevelAssetPath BuilderClassPath = GetBuilderClassPath();
		const FInterfaceRegistryKey Key = GetInterfaceRegistryKey(Interface.Version);
		if (const IInterfaceRegistryEntry* Entry = IInterfaceRegistry::Get().FindInterfaceRegistryEntry(Key))
		{
			const FMetasoundFrontendInterfaceUClassOptions* ClassOptions = Entry->GetInterface().FindClassOptions(BuilderClassPath);
			if (ClassOptions && !ClassOptions->bIsModifiable)
			{
				UE_LOG(LogMetaSound, Error, TEXT("DocumentBuilder failed to add MetaSound Interface '%s' to document: is not set to be modifiable for given UClass '%s'"), *InterfaceName.ToString(), *BuilderClassPath.ToString());
				return false;
			}

			TArray<FMetasoundFrontendInterface> InterfacesToAdd;
			InterfacesToAdd.Add(Entry->GetInterface());
			FModifyInterfaceOptions Options({ }, MoveTemp(InterfacesToAdd));
			return ModifyInterfaces(MoveTemp(Options));
		}
	}

	return false;
}

const FMetasoundFrontendNode* FMetaSoundFrontendDocumentBuilder::AddGraphNode(const FMetasoundFrontendGraphClass& InGraphClass, FGuid InNodeID, const FGuid* InPageID)
{
	auto FinalizeNode = [](FMetasoundFrontendNode& InOutNode, const Metasound::Frontend::FNodeRegistryKey& ClassKey)
	{
#if WITH_EDITOR
		using namespace Metasound::Frontend;

		// Cache the asset name on the node if it node is reference to asset-defined graph.
		const FTopLevelAssetPath Path = IMetaSoundAssetManager::GetChecked().FindAssetPath(FAssetKey(ClassKey.ClassName, ClassKey.Version));
		if (Path.IsValid())
		{
			InOutNode.Name = Path.GetAssetName();
			return;
		}

		InOutNode.Name = ClassKey.ClassName.GetFullName();
#endif // WITH_EDITOR
	};

	// Dependency is considered "External" when looked up or added on another graph
	FMetasoundFrontendClassMetadata NewClassMetadata = InGraphClass.Metadata;
	NewClassMetadata.SetType(EMetasoundFrontendClassType::External);

	if (!FindDependency(NewClassMetadata))
	{
		AddDependency(InGraphClass);
	}

	constexpr int32* NewNodeIndex = nullptr;
	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	return AddNodeInternal(NewClassMetadata, FinalizeNode, PageID, InNodeID, NewNodeIndex);
}

const FMetasoundFrontendNode* FMetaSoundFrontendDocumentBuilder::AddNodeByClassName(const FMetasoundFrontendClassName& InClassName, int32 InMajorVersion, FGuid InNodeID, const FGuid* InPageID)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	FMetasoundFrontendClass RegisteredClass;
	if (ISearchEngine::Get().FindClassWithHighestMinorVersion(InClassName, InMajorVersion, RegisteredClass))
	{
		const EMetasoundFrontendClassType ClassType = RegisteredClass.Metadata.GetType();
		if (ClassType != EMetasoundFrontendClassType::External && ClassType != EMetasoundFrontendClassType::Graph)
		{
			UE_LOG(LogMetaSound, Warning, TEXT("Failed to add new node by class name '%s': Class is restricted type '%s' that cannot be added via this function."),
				*InClassName.ToString(),
				LexToString(ClassType));
			return nullptr;
		}

		// Dependency is considered "External" when looked up or added as a dependency to a graph
		RegisteredClass.Metadata.SetType(EMetasoundFrontendClassType::External);

		const FMetasoundFrontendClass* Dependency = FindDependency(RegisteredClass.Metadata);
		if (!Dependency)
		{
			Dependency = AddDependency(RegisteredClass);
		}

		if (Dependency)
		{
			auto FinalizeNode = [](const FMetasoundFrontendNode& Node, const Metasound::Frontend::FNodeRegistryKey& ClassKey) { return Node.Name; };
			constexpr int32* NewNodeIndex = nullptr;
			const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
			return AddNodeInternal(Dependency->Metadata, FinalizeNode, PageID, InNodeID, NewNodeIndex);
		}
	}

	UE_LOG(LogMetaSound, Warning, TEXT("Failed to add new node by class name '%s' and major version '%d': Class not found"), *InClassName.ToString(), InMajorVersion);
	return nullptr;
}

const FMetasoundFrontendNode* FMetaSoundFrontendDocumentBuilder::AddNodeByTemplate(const Metasound::Frontend::INodeTemplate& InTemplate, FNodeTemplateGenerateInterfaceParams Params, FGuid InNodeID, const FGuid* InPageID)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	const FMetasoundFrontendClass& TemplateClass = InTemplate.GetFrontendClass();
	checkf(TemplateClass.Metadata.GetType() == EMetasoundFrontendClassType::Template, TEXT("INodeTemplate ClassType must always be 'Template'"));
	const FMetasoundFrontendClass* Dependency = FindDependency(TemplateClass.Metadata);
	if (!Dependency)
	{
		Dependency = AddDependency(TemplateClass);
	}
	check(Dependency);

	auto FinalizeNodeFunction = [](const FMetasoundFrontendNode& Node, const Metasound::Frontend::FNodeRegistryKey& ClassKey)
	{
		return Node.Name;
	};

	constexpr int32* NewNodeIndex = nullptr;
	const FGuid & PageID = InPageID ? *InPageID : BuildPageID;
	FMetasoundFrontendNode* NewNode = AddNodeInternal(Dependency->Metadata, FinalizeNodeFunction, PageID, InNodeID, NewNodeIndex);
	check(NewNode);
	NewNode->Interface = InTemplate.GenerateNodeInterface(MoveTemp(Params));

	return NewNode;
}

FMetasoundFrontendNode* FMetaSoundFrontendDocumentBuilder::AddNodeInternal(const FMetasoundFrontendClassMetadata& InClassMetadata, FFinalizeNodeFunctionRef FinalizeNode, const FGuid& InPageID, FGuid InNodeID, int32* NewNodeIndex)
{
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FMetaSoundFrontendDocumentBuilder::AddNodeInternal);

	using namespace Metasound::Frontend;

	const FNodeRegistryKey ClassKey = FNodeRegistryKey(InClassMetadata);
	if (const FMetasoundFrontendClass* Dependency = DocumentCache->FindDependency(ClassKey))
	{
		FMetasoundFrontendDocument& Document = GetDocumentChecked();
		FMetasoundFrontendGraph& Graph = Document.RootGraph.FindGraphChecked(InPageID);
		TArray<FMetasoundFrontendNode>& Nodes = Graph.Nodes;
		FMetasoundFrontendNode& Node = Nodes.Emplace_GetRef(*Dependency);
		Node.UpdateID(InNodeID);
		FinalizeNode(Node, ClassKey);

		const int32 NewIndex = Nodes.Num() - 1;
		const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(InPageID);
		DocumentDelegates->FindNodeDelegatesChecked(InPageID).OnNodeAdded.Broadcast(NewIndex);

		if (NewNodeIndex)
		{
			*NewNodeIndex = NewIndex;
		}

#if WITH_EDITORONLY_DATA
		Document.Metadata.ModifyContext.AddNodeIDModified(InNodeID);
#endif // WITH_EDITORONLY_DATA

		return &Node;
	}

	return nullptr;
}

#if WITH_EDITORONLY_DATA
const FMetasoundFrontendGraph& FMetaSoundFrontendDocumentBuilder::AddGraphPage(const FGuid& InPageID, bool bDuplicateLastGraph, bool bSetAsBuildGraph)
{
	using namespace Metasound::Frontend;

	const FMetasoundFrontendGraph& ToReturn = GetDocumentChecked().RootGraph.AddGraphPage(InPageID, bDuplicateLastGraph);
	DocumentDelegates->AddPageDelegates(InPageID);
	if (bSetAsBuildGraph)
	{
		SetBuildPageID(InPageID);
	}
	return ToReturn;
}
#endif // WITH_EDITORONLY_DATA

bool FMetaSoundFrontendDocumentBuilder::CanAddEdge(const FMetasoundFrontendEdge& InEdge, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const FMetasoundFrontendDocument& Document = GetConstDocumentChecked();
	const IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache(PageID);

	if (!EdgeCache.IsNodeInputConnected(InEdge.ToNodeID, InEdge.ToVertexID))
	{
		return IsValidEdge(InEdge, InPageID) == EInvalidEdgeReason::None;
	}

	return false;
}

void FMetaSoundFrontendDocumentBuilder::ClearDocument(TSharedRef<Metasound::Frontend::FDocumentModifyDelegates> ModifyDelegates)
{
	FMetasoundFrontendDocument& Doc = GetDocumentChecked();
	FMetasoundFrontendGraphClass& GraphClass = Doc.RootGraph;

	GraphClass.Interface.Inputs.Empty();
	GraphClass.Interface.Outputs.Empty();

#if WITH_EDITOR
	GraphClass.Interface.SetInputStyle({ });
	GraphClass.Interface.SetOutputStyle({ });
#endif // WITH_EDITOR

	GraphClass.PresetOptions.InputsInheritingDefault.Empty();
	GraphClass.PresetOptions.bIsPreset = false;

	// Removing graph pages is not necessary when editor only data is not available as graph mutation
	// is only supported in builds with editor data loaded. Otherwise, anything calling ClearDocument
	// should only be a transient, non serialized asset graph which does not support page mutation.
#if WITH_EDITORONLY_DATA
	constexpr bool bClearDefaultGraph = true;
	ResetGraphPages(bClearDefaultGraph);
#else // !WITH_EDITORONLY_DATA
	UObject& DocObject = CastDocumentObjectChecked<UObject>();
	checkf(!DocObject.IsAsset(), TEXT("Cannot call clear document on asset '%s': builder API does not support document mutation on serialized objects without editor data loaded"), *GetDebugName());

	GraphClass.IterateGraphPages([] (FMetasoundFrontendGraph& Graph)
	{
		Graph.Nodes.Empty();
		Graph.Edges.Empty();
		Graph.Variables.Empty();
	});
#endif // !WITH_EDITORONLY_DATA

	GraphClass.Interface.Inputs.Empty();
	GraphClass.Interface.Outputs.Empty();
	GraphClass.Interface.Environment.Empty();

	Doc.Interfaces.Empty();
	Doc.Dependencies.Empty();

#if WITH_EDITORONLY_DATA
	Doc.Metadata.MemberMetadata.Empty();
#endif // WITH_EDITORONLY_DATA

	Reload(ModifyDelegates);
}

#if WITH_EDITORONLY_DATA
bool FMetaSoundFrontendDocumentBuilder::ClearMemberMetadata(const FGuid& InMemberID)
{
	return GetDocumentChecked().Metadata.MemberMetadata.Remove(InMemberID) > 0;
}
#endif // WITH_EDITORONLY_DATA

bool FMetaSoundFrontendDocumentBuilder::ConformGraphInputNodeToClass(const FMetasoundFrontendClassInput& GraphInput)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendClass Class;
	const bool bClassFound = DocumentBuilderPrivate::FindInputRegistryClass(GraphInput.TypeName, GraphInput.AccessType, Class);
	if (ensureAlways(bClassFound))
	{
		FMetasoundFrontendDocument& Document = GetDocumentChecked();
		const FMetasoundFrontendClass* Dependency = FindDependency(Class.Metadata);
		if (!Dependency)
		{
			Dependency = AddDependency(Class);
		}

		if (ensureAlways(Dependency))
		{
			Document.RootGraph.IterateGraphPages([this, &Document, &Dependency, &GraphInput](FMetasoundFrontendGraph& Graph)
			{
				const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(Graph.PageID);
				if (const int32* NodeIndexPtr = NodeCache.FindNodeIndex(GraphInput.NodeID))
				{
					TArray<FMetasoundFrontendNode>& Nodes = Graph.Nodes;
					FMetasoundFrontendNode& Node = Nodes[*NodeIndexPtr];
					FNodeModifyDelegates& NodeDelegates = DocumentDelegates->FindNodeDelegatesChecked(Graph.PageID);
					const int32 RemovalIndex = *NodeIndexPtr; // Have to cache as next delegate broadcast invalidates index pointer
					NodeDelegates.OnRemoveSwappingNode.Broadcast(RemovalIndex, Nodes.Num() - 1);
					FMetasoundFrontendNode NewNode = MoveTemp(Node);
					Nodes.RemoveAtSwap(RemovalIndex, EAllowShrinking::No);
					NewNode.ClassID = Dependency->ID;
					NewNode.Interface.Inputs.Last().TypeName = GraphInput.TypeName;
					NewNode.Interface.Outputs.Last().TypeName = GraphInput.TypeName;

#if WITH_EDITORONLY_DATA
					Document.Metadata.ModifyContext.AddNodeIDModified(NewNode.GetID());
#endif // WITH_EDITORONLY_DATA

					// Set the default literal on the nodes inputs so that it gets passed to the instantiated TInputNode on a live
					// auditioned MetaSound.
					DocumentBuilderPrivate::SetDefaultLiteralOnInputNode(NewNode, GraphInput);

					FMetasoundFrontendNode& NewNodeRef = Nodes.Add_GetRef(MoveTemp(NewNode));
					NodeDelegates.OnNodeAdded.Broadcast(Nodes.Num() - 1);

					// Remove the default literal on the node added during the "FinalizeNode" call. This matches how 
					// nodes are serialized in editor. The default literals are only stored on the FMetasoundFrontendClassInputs.
					NewNodeRef.InputLiterals.Reset();
				}
			});

			RemoveUnusedDependencies();
			return true;
		}
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::ConformGraphOutputNodeToClass(const FMetasoundFrontendClassOutput& GraphOutput)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendClass Class;
	const bool bClassFound = DocumentBuilderPrivate::FindOutputRegistryClass(GraphOutput.TypeName, GraphOutput.AccessType, Class);
	if (ensureAlways(bClassFound))
	{
		FMetasoundFrontendDocument& Document = GetDocumentChecked();
		const FMetasoundFrontendClass* Dependency = FindDependency(Class.Metadata);
		if (!Dependency)
		{
			Dependency = AddDependency(Class);
		}

		if (ensureAlways(Dependency))
		{
			Document.RootGraph.IterateGraphPages([this, &Document, &Dependency, &GraphOutput](FMetasoundFrontendGraph& Graph)
			{
				const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(Graph.PageID);
				if (const int32* NodeIndexPtr = NodeCache.FindNodeIndex(GraphOutput.NodeID))
				{
					TArray<FMetasoundFrontendNode>& Nodes = Graph.Nodes;
					FMetasoundFrontendNode& Node = Nodes[*NodeIndexPtr];
					FNodeModifyDelegates& NodeDelegates = DocumentDelegates->FindNodeDelegatesChecked(Graph.PageID);
					const int32 RemovalIndex = *NodeIndexPtr; // Have to cache as next delegate broadcast invalidates index pointer
					NodeDelegates.OnRemoveSwappingNode.Broadcast(RemovalIndex, Nodes.Num() - 1);
					FMetasoundFrontendNode NewNode = MoveTemp(Node);
					Nodes.RemoveAtSwap(RemovalIndex, EAllowShrinking::No);
					NewNode.ClassID = Dependency->ID;
					NewNode.Interface.Inputs.Last().TypeName = GraphOutput.TypeName;
					NewNode.Interface.Outputs.Last().TypeName = GraphOutput.TypeName;

#if WITH_EDITORONLY_DATA
					Document.Metadata.ModifyContext.AddNodeIDModified(NewNode.GetID());
#endif // WITH_EDITORONLY_DATA
					Nodes.Add(MoveTemp(NewNode));
					NodeDelegates.OnNodeAdded.Broadcast(Nodes.Num() - 1);
				}	
			});	

			RemoveUnusedDependencies();
			return true;
		}
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::ContainsDependencyOfType(EMetasoundFrontendClassType ClassType) const
{
	return DocumentCache->ContainsDependencyOfType(ClassType);
}

bool FMetaSoundFrontendDocumentBuilder::ContainsEdge(const FMetasoundFrontendEdge& InEdge, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;
	const IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache(InPageID ? *InPageID : BuildPageID);
	return EdgeCache.ContainsEdge(InEdge);
}

bool FMetaSoundFrontendDocumentBuilder::ContainsNode(const FGuid& InNodeID, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(InPageID ? * InPageID : BuildPageID);
	return NodeCache.ContainsNode(InNodeID);
}

bool FMetaSoundFrontendDocumentBuilder::ConvertFromPreset()
{
	using namespace Metasound::Frontend;

	if (IsPreset())
	{
		FMetasoundFrontendDocument& Document = GetDocumentChecked();
		FMetasoundFrontendGraphClass& RootGraphClass = Document.RootGraph;
		FMetasoundFrontendGraphClassPresetOptions& PresetOptions = RootGraphClass.PresetOptions;
		PresetOptions.bIsPreset = false;

#if WITH_EDITOR
		FMetasoundFrontendGraphStyle& Style = FindBuildGraphChecked().Style;
		Style.bIsGraphEditable = true;
#endif // WITH_EDITOR

		return true;
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::ConvertToPreset(const FMetasoundFrontendDocument& InReferencedDocument, TSharedRef<Metasound::Frontend::FDocumentModifyDelegates> ModifyDelegates)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	ClearDocument(ModifyDelegates);

	FMetasoundFrontendGraphClass& PresetAssetRootGraph = GetDocumentChecked().RootGraph;
	PresetAssetRootGraph.IterateGraphPages([](FMetasoundFrontendGraph& PresetAssetGraph)
	{
#if WITH_EDITORONLY_DATA
		PresetAssetGraph.Style.bIsGraphEditable = false;
#endif // WITH_EDITORONLY_DATA
	});

	// Mark all inputs as inherited by default
	{
		PresetAssetRootGraph.PresetOptions.InputsInheritingDefault.Reset();
		auto GetInputName = [](const FMetasoundFrontendClassInput& Input) { return Input.Name; };
		Algo::Transform(PresetAssetRootGraph.Interface.Inputs, PresetAssetRootGraph.PresetOptions.InputsInheritingDefault, GetInputName);
		PresetAssetRootGraph.PresetOptions.bIsPreset = true;
	}

	// Apply root graph transform
	FRebuildPresetRootGraph RebuildPresetRootGraph(InReferencedDocument);
	if (RebuildPresetRootGraph.Transform(GetDocumentChecked()))
	{
		DocumentInterface->ConformObjectToDocument();

		// TL/DR: Have to reload and assign delegates here due to the rebuild preset transform still being implemented via controllers.
		// Onces its reimplemented with the builder API, this can be removed.
		// 
		// The invalidate cache call when accessing the mutable document handle from within the transform unfortunately doesn't reach this
		// builder's cache indirectly as converting to preset can be called by transient builders that are not registered with the MetaSound
		// builder subsystem.
		Reload(ModifyDelegates);
		return true;
	}

	return false;
}

const FMetasoundFrontendNode* FMetaSoundFrontendDocumentBuilder::DuplicateGraphInput(const FMetasoundFrontendClassInput& InClassInput, const FName InName, const FGuid* InPageID)
{
	using namespace Metasound;

	Frontend::FDocumentIDGenerator& IDGenerator = Frontend::FDocumentIDGenerator::Get();
	const FMetasoundFrontendDocument& Doc = GetConstDocumentChecked();

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;

	FMetasoundFrontendClassInput ClassInput = InClassInput;
	ClassInput.NodeID = IDGenerator.CreateNodeID(Doc);
	ClassInput.VertexID = IDGenerator.CreateVertexID(Doc);
#if WITH_EDITORONLY_DATA
	ClassInput.Metadata.SetDisplayName(FText::GetEmpty());
#endif // WITH_EDITORONLY_DATA
	ClassInput.Name = InName;

	return AddGraphInput(ClassInput, &PageID);
}

const FMetasoundFrontendNode* FMetaSoundFrontendDocumentBuilder::DuplicateGraphOutput(const FMetasoundFrontendClassOutput& InClassOutput, const FName InName, const FGuid* InPageID)
{
	using namespace Metasound::Frontend;

	FDocumentIDGenerator& IDGenerator = FDocumentIDGenerator::Get();
	const FMetasoundFrontendDocument& Doc = GetConstDocumentChecked();

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;

	FMetasoundFrontendClassOutput ClassOutput = InClassOutput;
	ClassOutput.NodeID = IDGenerator.CreateNodeID(Doc);
	ClassOutput.VertexID = IDGenerator.CreateVertexID(Doc);
#if WITH_EDITORONLY_DATA
	ClassOutput.Metadata.SetDisplayName(FText::GetEmpty());
#endif // WITH_EDITORONLY_DATA
	ClassOutput.Name = InName;

	return AddGraphOutput(ClassOutput, &PageID);
}

FMetasoundFrontendGraph& FMetaSoundFrontendDocumentBuilder::FindBuildGraphChecked() const
{
	return GetDocumentChecked().RootGraph.FindGraphChecked(BuildPageID);
}

const FMetasoundFrontendGraph& FMetaSoundFrontendDocumentBuilder::FindConstBuildGraphChecked() const
{
	return GetConstDocumentChecked().RootGraph.FindConstGraphChecked(BuildPageID);
}

bool FMetaSoundFrontendDocumentBuilder::FindDeclaredInterfaces(TArray<const Metasound::Frontend::IInterfaceRegistryEntry*>& OutInterfaces) const
{
	return FindDeclaredInterfaces(GetConstDocumentChecked(), OutInterfaces);
}

bool FMetaSoundFrontendDocumentBuilder::FindDeclaredInterfaces(const FMetasoundFrontendDocument& InDocument, TArray<const Metasound::Frontend::IInterfaceRegistryEntry*>& OutInterfaces)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	bool bInterfacesFound = true;

	Algo::Transform(InDocument.Interfaces, OutInterfaces, [&bInterfacesFound](const FMetasoundFrontendVersion& Version)
	{
		const FInterfaceRegistryKey InterfaceKey = GetInterfaceRegistryKey(Version);
		const IInterfaceRegistryEntry* RegistryEntry = IInterfaceRegistry::Get().FindInterfaceRegistryEntry(InterfaceKey);
		if (!RegistryEntry)
		{
			bInterfacesFound = false;
			UE_LOG(LogMetaSound, Warning, TEXT("No registered interface matching interface version on document [InterfaceVersion:%s]"), *Version.ToString());
		}

		return RegistryEntry;
	});

	return bInterfacesFound;
}

const FMetasoundFrontendClass* FMetaSoundFrontendDocumentBuilder::FindDependency(const FGuid& InClassID) const
{
	return DocumentCache->FindDependency(InClassID);
}

const FMetasoundFrontendClass* FMetaSoundFrontendDocumentBuilder::FindDependency(const FMetasoundFrontendClassMetadata& InMetadata) const
{
	using namespace Metasound::Frontend;

	checkf(InMetadata.GetType() != EMetasoundFrontendClassType::Graph,
		TEXT("Dependencies are never listed as 'Graph' types. Graphs are considered 'External' from the perspective of the parent document to allow for nativization."));
	const FNodeRegistryKey RegistryKey = FNodeRegistryKey(InMetadata);
	return DocumentCache->FindDependency(RegistryKey);
}

TArray<const FMetasoundFrontendEdge*> FMetaSoundFrontendDocumentBuilder::FindEdges(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;

	const IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache(InPageID ? *InPageID : BuildPageID);
	return EdgeCache.FindEdges(InNodeID, InVertexID);
}

#if WITH_EDITORONLY_DATA
const FMetasoundFrontendEdgeStyle* FMetaSoundFrontendDocumentBuilder::FindConstEdgeStyle(const FGuid& InNodeID, FName OutputName, const FGuid* InPageID) const
{
	auto IsEdgeStyle = [&InNodeID, &OutputName](const FMetasoundFrontendEdgeStyle& EdgeStyle)
	{
		return EdgeStyle.NodeID == InNodeID && EdgeStyle.OutputName == OutputName;
	};

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const FMetasoundFrontendDocument& Document = GetConstDocumentChecked();
	const FMetasoundFrontendGraph& Graph = Document.RootGraph.FindConstGraphChecked(PageID);
	return Graph.Style.EdgeStyles.FindByPredicate(IsEdgeStyle);
}

FMetasoundFrontendEdgeStyle* FMetaSoundFrontendDocumentBuilder::FindEdgeStyle(const FGuid& InNodeID, FName OutputName, const FGuid* InPageID)
{
	auto IsEdgeStyle = [&InNodeID, &OutputName](const FMetasoundFrontendEdgeStyle& EdgeStyle)
	{
		return EdgeStyle.NodeID == InNodeID && EdgeStyle.OutputName == OutputName;
	};

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	FMetasoundFrontendGraph& Graph = Document.RootGraph.FindGraphChecked(PageID);
	return Graph.Style.EdgeStyles.FindByPredicate(IsEdgeStyle);
}

FMetasoundFrontendEdgeStyle& FMetaSoundFrontendDocumentBuilder::FindOrAddEdgeStyle(const FGuid& InNodeID, FName OutputName, const FGuid* InPageID)
{
	if (FMetasoundFrontendEdgeStyle* Style = FindEdgeStyle(InNodeID, OutputName, InPageID))
	{
		return *Style;
	}

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	FMetasoundFrontendGraph& Graph = Document.RootGraph.FindGraphChecked(PageID);
	FMetasoundFrontendEdgeStyle& EdgeStyle = Graph.Style.EdgeStyles.AddDefaulted_GetRef();

	checkf(ContainsNode(InNodeID), TEXT("Cannot add edge style for node that does not exist"));
	EdgeStyle.NodeID = InNodeID;
	EdgeStyle.OutputName = OutputName;
	return EdgeStyle;
}

const FMetaSoundFrontendGraphComment* FMetaSoundFrontendDocumentBuilder::FindGraphComment(const FGuid& InCommentID, const FGuid* InPageID) const
{
	check(InCommentID.IsValid());
	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const FMetasoundFrontendDocument& Document = GetConstDocumentChecked();
	const TMap<FGuid, FMetaSoundFrontendGraphComment>& Comments = Document.RootGraph.FindConstGraphChecked(PageID).Style.Comments;
	return Comments.Find(InCommentID);
}

FMetaSoundFrontendGraphComment* FMetaSoundFrontendDocumentBuilder::FindGraphComment(const FGuid& InCommentID, const FGuid* InPageID)
{
	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	TMap<FGuid, FMetaSoundFrontendGraphComment>& Comments = Document.RootGraph.FindGraphChecked(PageID).Style.Comments;
	return Comments.Find(InCommentID);
}
#endif // WITH_EDITORONLY_DATA

bool FMetaSoundFrontendDocumentBuilder::FindInterfaceInputNodes(FName InterfaceName, TArray<const FMetasoundFrontendNode*>& OutInputs, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;

	OutInputs.Reset();

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	FMetasoundFrontendInterface Interface;
	const TSet<FMetasoundFrontendVersion>& Interfaces = GetConstDocumentChecked().Interfaces;
	if (ISearchEngine::Get().FindInterfaceWithHighestVersion(InterfaceName, Interface))
	{
		if (Interfaces.Contains(Interface.Version))
		{
			const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
			const IDocumentGraphInterfaceCache& InterfaceCache = DocumentCache->GetInterfaceCache();

			TArray<const FMetasoundFrontendNode*> InterfaceInputs;
			for (const FMetasoundFrontendClassInput& Input : Interface.Inputs)
			{
				const FMetasoundFrontendClassInput* ClassInput = InterfaceCache.FindInput(Input.Name);
				if (!ClassInput)
				{
					return false;
				}

				if (const FMetasoundFrontendNode* Node = NodeCache.FindNode(ClassInput->NodeID))
				{
					InterfaceInputs.Add(Node);
				}
				else
				{
					return false;
				}
			}

			OutInputs = MoveTemp(InterfaceInputs);
			return true;
		}
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::FindInterfaceOutputNodes(FName InterfaceName, TArray<const FMetasoundFrontendNode*>& OutOutputs, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;

	OutOutputs.Reset();

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;

	FMetasoundFrontendInterface Interface;
	const TSet<FMetasoundFrontendVersion>& Interfaces = GetConstDocumentChecked().Interfaces;
	if (ISearchEngine::Get().FindInterfaceWithHighestVersion(InterfaceName, Interface))
	{
		if (Interfaces.Contains(Interface.Version))
		{
			const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
			const IDocumentGraphInterfaceCache& InterfaceCache = DocumentCache->GetInterfaceCache();

			TArray<const FMetasoundFrontendNode*> InterfaceOutputs;
			for (const FMetasoundFrontendClassOutput& Output : Interface.Outputs)
			{
				const FMetasoundFrontendClassOutput* ClassOutput = InterfaceCache.FindOutput(Output.Name);
				if (!ClassOutput)
				{
					return false;
				}

				if (const FMetasoundFrontendNode* Node = NodeCache.FindNode(ClassOutput->NodeID))
				{
					InterfaceOutputs.Add(Node);
				}
				else
				{
					return false;
				}
			}

			OutOutputs = MoveTemp(InterfaceOutputs);
			return true;
		}
	}

	return false;
}

const FMetasoundFrontendClassInput* FMetaSoundFrontendDocumentBuilder::FindGraphInput(FName InputName) const
{
	return DocumentCache->GetInterfaceCache().FindInput(InputName);
}

const FMetasoundFrontendNode* FMetaSoundFrontendDocumentBuilder::FindGraphInputNode(FName InputName, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;

	if (const FMetasoundFrontendClassInput* InputClass = FindGraphInput(InputName))
	{
		const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
		const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
		return NodeCache.FindNode(InputClass->NodeID);
	}

	return nullptr;
}

const FMetasoundFrontendClassOutput* FMetaSoundFrontendDocumentBuilder::FindGraphOutput(FName OutputName) const
{
	return DocumentCache->GetInterfaceCache().FindOutput(OutputName);
}

const FMetasoundFrontendNode* FMetaSoundFrontendDocumentBuilder::FindGraphOutputNode(FName OutputName, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;

	if (const FMetasoundFrontendClassOutput* OutputClass = FindGraphOutput(OutputName))
	{
		const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
		const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
		return NodeCache.FindNode(OutputClass->NodeID);
	}

	return nullptr;
}

const FMetasoundFrontendVariable* FMetaSoundFrontendDocumentBuilder::FindGraphVariable(FName VariableName, const FGuid* InPageID) const
{
	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const FMetasoundFrontendDocument& Document = GetDocumentChecked();
	const FMetasoundFrontendGraph& Graph = Document.RootGraph.FindConstGraphChecked(PageID);
	auto MatchesName = [&VariableName](const FMetasoundFrontendVariable& Variable) { return Variable.Name == VariableName; };
	return Graph.Variables.FindByPredicate(MatchesName);
}

#if WITH_EDITOR
UMetaSoundFrontendMemberMetadata* FMetaSoundFrontendDocumentBuilder::FindMemberMetadata(const FGuid& InMemberID)
{
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	TMap<FGuid, TObjectPtr<UMetaSoundFrontendMemberMetadata>>& LiteralMetadata = Document.Metadata.MemberMetadata;
	TObjectPtr<UMetaSoundFrontendMemberMetadata> ToReturn = LiteralMetadata.FindRef(InMemberID);
	return ToReturn;
}
#endif // WITH_EDITOR

const FMetasoundFrontendNode* FMetaSoundFrontendDocumentBuilder::FindNode(const FGuid& InNodeID, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;
	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
	return NodeCache.FindNode(InNodeID);
}

bool FMetaSoundFrontendDocumentBuilder::FindNodeClassInterfaces(const FGuid& InNodeID, TSet<FMetasoundFrontendVersion>& OutInterfaces, const FGuid& InPageID) const
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	const FMetasoundFrontendDocument& Document = GetConstDocumentChecked();
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(InPageID);
	if (const FMetasoundFrontendNode* Node = NodeCache.FindNode(InNodeID))
	{
		if (const FMetasoundFrontendClass* NodeClass = DocumentCache->FindDependency(Node->ClassID))
		{
			const FNodeRegistryKey NodeClassRegistryKey = FNodeRegistryKey(NodeClass->Metadata);
			return FMetasoundFrontendRegistryContainer::Get()->FindImplementedInterfacesFromRegistered(NodeClassRegistryKey, OutInterfaces);
		}
	}

	return false;
}

const FMetasoundFrontendVertex* FMetaSoundFrontendDocumentBuilder::FindNodeInput(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;
	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
	return NodeCache.FindInputVertex(InNodeID, InVertexID);
}

const FMetasoundFrontendVertex* FMetaSoundFrontendDocumentBuilder::FindNodeInput(const FGuid& InNodeID, FName InVertexName, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;
	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
	return NodeCache.FindInputVertex(InNodeID, InVertexName);
}

const TArray<FMetasoundFrontendClassInputDefault>* FMetaSoundFrontendDocumentBuilder::FindNodeClassInputDefaults(const FGuid& InNodeID, FName InVertexName, const FGuid* InPageID) const
{
	using namespace Metasound;

	if (const FMetasoundFrontendNode* Node = FindNode(InNodeID, InPageID))
	{
		if (const FMetasoundFrontendClass* Class = FindDependency(Node->ClassID))
		{
			const EMetasoundFrontendClassType ClassType = Class->Metadata.GetType();
			switch (ClassType)
			{
				case EMetasoundFrontendClassType::External:
				{
					auto MatchesName = [&InVertexName](const FMetasoundFrontendClassInput& Input) { return Input.Name == InVertexName; };
					if (const FMetasoundFrontendClassInput* Input = Class->Interface.Inputs.FindByPredicate(MatchesName))
					{
						return &Input->GetDefaults();
					}
				}
				break;

				case EMetasoundFrontendClassType::Input:
				case EMetasoundFrontendClassType::Output:
				case EMetasoundFrontendClassType::Literal:
				{
					return &Class->Interface.Inputs.Last().GetDefaults();
				}
				break;

				case EMetasoundFrontendClassType::Variable:
				case EMetasoundFrontendClassType::VariableDeferredAccessor:
				case EMetasoundFrontendClassType::VariableAccessor:
				case EMetasoundFrontendClassType::VariableMutator:
				{
					using namespace VariableNames;
					auto IsDataInput = [](const FMetasoundFrontendClassInput& Input) { return Input.Name == METASOUND_GET_PARAM_NAME(InputData); };
					if (const FMetasoundFrontendClassInput* Input = Class->Interface.Inputs.FindByPredicate(IsDataInput))
					{
						return &Input->GetDefaults();
					}
				}
				break;

				case EMetasoundFrontendClassType::Template:
				{
					const Frontend::FNodeRegistryKey Key = Frontend::FNodeRegistryKey(Class->Metadata);
					const Frontend::INodeTemplate* Template = Frontend::INodeTemplateRegistry::Get().FindTemplate(Key);
					check(Template);
					const FGuid PageID = InPageID ? *InPageID : BuildPageID;
					return Template->FindNodeClassInputDefaults(*this, PageID, InNodeID, InVertexName);
				}
				break;

				case EMetasoundFrontendClassType::Graph:
				case EMetasoundFrontendClassType::Invalid:
				default:
				{
					checkNoEntry();
				}
				break;
			}
		}
	}

	return nullptr;
}

const FMetasoundFrontendVertexLiteral* FMetaSoundFrontendDocumentBuilder::FindNodeInputDefault(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID) const
{
	if (const FMetasoundFrontendNode* Node = FindNode(InNodeID, InPageID))
	{
		auto VertexLiteralMatchesID = [&InVertexID](const FMetasoundFrontendVertexLiteral& VertexLiteral)
		{
			return VertexLiteral.VertexID == InVertexID;
		};
		return Node->InputLiterals.FindByPredicate(VertexLiteralMatchesID);
	}

	return nullptr;
}

const FMetasoundFrontendVertexLiteral* FMetaSoundFrontendDocumentBuilder::FindNodeInputDefault(const FGuid& InNodeID, FName InVertexName, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;
	if (const FMetasoundFrontendVertex* Vertex = FindNodeInput(InNodeID, InVertexName, InPageID))
	{
		return FindNodeInputDefault(InNodeID, Vertex->VertexID, InPageID);
	}

	return nullptr;
}

TArray<const FMetasoundFrontendVertex*> FMetaSoundFrontendDocumentBuilder::FindNodeInputs(const FGuid& InNodeID, FName TypeName, const FGuid* InPageID) const
{
	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	return DocumentCache->GetNodeCache(PageID).FindNodeInputs(InNodeID, TypeName);
}

TArray<const FMetasoundFrontendVertex*> FMetaSoundFrontendDocumentBuilder::FindNodeInputsConnectedToNodeOutput(const FGuid& InOutputNodeID, const FGuid& InOutputVertexID, TArray<const FMetasoundFrontendNode*>* ConnectedInputNodes, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache(PageID);
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);

	const FMetasoundFrontendDocument& Document = GetConstDocumentChecked();

	if (ConnectedInputNodes)
	{
		ConnectedInputNodes->Reset();
	}

	TArray<const FMetasoundFrontendVertex*> Inputs;
	const FMetasoundFrontendGraph& Graph = Document.RootGraph.FindConstGraphChecked(PageID);
	const TArrayView<const int32> Indices = EdgeCache.FindEdgeIndicesFromNodeOutput(InOutputNodeID, InOutputVertexID);
	Algo::Transform(Indices, Inputs, [&Graph, &Document, &NodeCache, &ConnectedInputNodes](const int32& Index)
	{
		const FMetasoundFrontendEdge& Edge = Graph.Edges[Index];
		if (ConnectedInputNodes)
		{
			ConnectedInputNodes->Add(NodeCache.FindNode(Edge.ToNodeID));
		}
		return NodeCache.FindInputVertex(Edge.ToNodeID, Edge.ToVertexID);
	});
	return Inputs;
}

FMetasoundFrontendNode* FMetaSoundFrontendDocumentBuilder::FindNodeInternal(const FGuid& InNodeID, const FGuid* InPageID)
{
	using namespace Metasound::Frontend;

	using namespace Metasound;
	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
	if (const int32* NodeIndex = NodeCache.FindNodeIndex(InNodeID))
	{
		FMetasoundFrontendGraph& Graph = GetDocumentChecked().RootGraph.FindGraphChecked(PageID);
		return &Graph.Nodes[*NodeIndex];
	}

	return nullptr;
}

const FMetasoundFrontendVertex* FMetaSoundFrontendDocumentBuilder::FindNodeOutput(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;
	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
	return NodeCache.FindOutputVertex(InNodeID, InVertexID);
}

const FMetasoundFrontendVertex* FMetaSoundFrontendDocumentBuilder::FindNodeOutput(const FGuid& InNodeID, FName InVertexName, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;
	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
	return NodeCache.FindOutputVertex(InNodeID, InVertexName);
}

TArray<const FMetasoundFrontendVertex*> FMetaSoundFrontendDocumentBuilder::FindNodeOutputs(const FGuid& InNodeID, FName TypeName, const FGuid* InPageID) const
{
	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	return DocumentCache->GetNodeCache(PageID).FindNodeOutputs(InNodeID, TypeName);
}

const FMetasoundFrontendVertex* FMetaSoundFrontendDocumentBuilder::FindNodeOutputConnectedToNodeInput(const FGuid& InInputNodeID, const FGuid& InInputVertexID, const FMetasoundFrontendNode** ConnectedOutputNode, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache(PageID);
	if (const int32* Index = EdgeCache.FindEdgeIndexToNodeInput(InInputNodeID, InInputVertexID))
	{
		const FMetasoundFrontendDocument& Document = GetConstDocumentChecked();
		const FMetasoundFrontendEdge& Edge = Document.RootGraph.FindConstGraphChecked(PageID).Edges[*Index];
		const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
		if (ConnectedOutputNode)
		{
			(*ConnectedOutputNode) = NodeCache.FindNode(Edge.FromNodeID);
		}
		return NodeCache.FindOutputVertex(Edge.FromNodeID, Edge.FromVertexID);
	}

	if (ConnectedOutputNode)
	{
		*ConnectedOutputNode = nullptr;
	}
	return nullptr;
}

#if WITH_EDITORONLY_DATA
FMetaSoundFrontendGraphComment& FMetaSoundFrontendDocumentBuilder::FindOrAddGraphComment(const FGuid& InCommentID, const FGuid* InPageID)
{
	check(InCommentID.IsValid());
	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	TMap<FGuid, FMetaSoundFrontendGraphComment>& Comments = Document.RootGraph.FindGraphChecked(PageID).Style.Comments;
	return Comments.FindOrAdd(InCommentID);
}
#endif // WITH_EDITORONLY_DATA

FMetasoundFrontendClassName FMetaSoundFrontendDocumentBuilder::GenerateNewClassName()
{
	using namespace Metasound::Frontend;
	FMetasoundFrontendClassMetadata& Metadata = GetDocumentChecked().RootGraph.Metadata;
	const FMetasoundFrontendClassName NewClassName(FName(), FName(*FGuid::NewGuid().ToString()), FName());
	Metadata.SetClassName(NewClassName);
	return NewClassName;
}

const FTopLevelAssetPath FMetaSoundFrontendDocumentBuilder::GetBuilderClassPath() const
{
	IMetaSoundDocumentInterface* Interface = DocumentInterface.GetInterface();
	checkf(Interface, TEXT("Failed to return class path; interface must always be valid while builder is operating on MetaSound UObject!"));
	return Interface->GetBaseMetaSoundUClass().GetClassPathName();
}

const FMetasoundFrontendDocument& FMetaSoundFrontendDocumentBuilder::GetConstDocumentChecked() const
{
	return GetConstDocumentInterfaceChecked().GetConstDocument();
}

const IMetaSoundDocumentInterface& FMetaSoundFrontendDocumentBuilder::GetConstDocumentInterfaceChecked() const
{
	const IMetaSoundDocumentInterface* Interface = DocumentInterface.GetInterface();
	checkf(Interface, TEXT("Failed to return document; interface must always be valid while builder is operating on MetaSound UObject!"));
	return *Interface;
}

const FString FMetaSoundFrontendDocumentBuilder::GetDebugName() const
{
	using namespace Metasound::Frontend;

	UObject& MetaSoundObject = CastDocumentObjectChecked<UObject>();
	return MetaSoundObject.GetPathName();
}

const FMetasoundFrontendDocument& FMetaSoundFrontendDocumentBuilder::GetDocument() const
{
	const IMetaSoundDocumentInterface* Interface = DocumentInterface.GetInterface();
	checkf(Interface, TEXT("Failed to return document; interface must always be valid while builder is operating on MetaSound UObject!"));
	return Interface->GetConstDocument();
}

FMetasoundFrontendDocument& FMetaSoundFrontendDocumentBuilder::GetDocumentChecked() const
{
	return GetDocumentInterfaceChecked().GetDocument();
}

Metasound::Frontend::FDocumentModifyDelegates& FMetaSoundFrontendDocumentBuilder::GetDocumentDelegates()
{
	return *DocumentDelegates;
}

const IMetaSoundDocumentInterface& FMetaSoundFrontendDocumentBuilder::GetDocumentInterface() const
{
	const IMetaSoundDocumentInterface* Interface = DocumentInterface.GetInterface();
	checkf(Interface, TEXT("Failed to return document; interface must always be valid while builder is operating on MetaSound UObject!"));
	return *Interface;
}

IMetaSoundDocumentInterface& FMetaSoundFrontendDocumentBuilder::GetDocumentInterfaceChecked() const
{
	IMetaSoundDocumentInterface* Interface = DocumentInterface.GetInterface();
	checkf(Interface, TEXT("Failed to return document; interface must always be valid while builder is operating on MetaSound UObject!"));
	return *Interface;
}

TArray<const FMetasoundFrontendNode*> FMetaSoundFrontendDocumentBuilder::GetGraphInputTemplateNodes(FName InInputName, const FGuid* InPageID)
{
	using namespace Metasound::Frontend;

	TArray<const FMetasoundFrontendNode*> TemplateNodes;

	const FGuid PageID = InPageID ? *InPageID : BuildPageID;
	FMetasoundFrontendGraphClass& RootGraph = GetDocumentChecked().RootGraph;
	if (const int32* Index = DocumentCache->GetInterfaceCache().FindInputIndex(InInputName))
	{
		const FMetasoundFrontendClassInput& InputClass = RootGraph.Interface.Inputs[*Index];
		const FMetasoundFrontendGraph& Graph = RootGraph.FindConstGraphChecked(PageID);
		const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
		const IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache(PageID);

		if (const FMetasoundFrontendNode* InputNode = NodeCache.FindNode(InputClass.NodeID))
		{
			const FGuid OutputVertexID = InputNode->Interface.Outputs.Last().VertexID;
			const TArray<const FMetasoundFrontendEdge*> ConnectedEdges = EdgeCache.FindEdges(InputClass.NodeID, OutputVertexID);
			for (const FMetasoundFrontendEdge* Edge : ConnectedEdges)
			{
				check(Edge);
				if (const int32* ConnectedNodeIndex = NodeCache.FindNodeIndex(Edge->ToNodeID))
				{
					const FMetasoundFrontendNode& ConnectedNode = Graph.Nodes[*ConnectedNodeIndex];
					if (const FMetasoundFrontendClass* ConnectedNodeClass = FindDependency(ConnectedNode.ClassID))
					{
						if (ConnectedNodeClass->Metadata.GetClassName() == FInputNodeTemplate::ClassName)
						{
							TemplateNodes.Add(&ConnectedNode);
						}
					}
				}
			}
		}
	}

	return TemplateNodes;
}

FMetasoundAssetBase& FMetaSoundFrontendDocumentBuilder::GetMetasoundAsset() const
{
	using namespace Metasound::Frontend;

	UObject* Object = DocumentInterface.GetObject();
	check(Object);
	FMetasoundAssetBase* Asset = IMetaSoundAssetManager::GetChecked().GetAsAsset(*Object);
	check(Asset);
	return *Asset;
}

FMetasoundAssetBase* FMetaSoundFrontendDocumentBuilder::GetReferencedPresetAsset() const
{
	using namespace Metasound::Frontend;
	if (!IsPreset())
	{
		return nullptr;
	}

	// Find the single external node which is the referenced preset asset, 
	// and find the asset with its registry key 
	auto FindExternalNode = [this](const FMetasoundFrontendNode& Node)
	{
		const FMetasoundFrontendClass* Class = FindDependency(Node.ClassID);
		check(Class);
		return Class->Metadata.GetType() == EMetasoundFrontendClassType::External;
	};
	const FMetasoundFrontendNode* Node = FindConstBuildGraphChecked().Nodes.FindByPredicate(FindExternalNode);
	if (Node != nullptr)
	{
		const FMetasoundFrontendClass* NodeClass = FindDependency(Node->ClassID);
		check(NodeClass);
		const FAssetKey NodeAssetKey(NodeClass->Metadata);
		const TArray<FMetasoundAssetBase*> ReferencedAssets = GetMetasoundAsset().GetReferencedAssets();
		for (FMetasoundAssetBase* RefAsset : ReferencedAssets)
		{
			TScriptInterface<IMetaSoundDocumentInterface> RefDocInterface = RefAsset->GetOwningAsset();
			if (RefDocInterface.GetObject() != nullptr)
			{
				const FAssetKey AssetKey(RefDocInterface->GetConstDocument().RootGraph.Metadata);
				if (AssetKey == NodeAssetKey)
				{
					return RefAsset;
				}
			}
		}
	}
	return nullptr;
}

const FGuid& FMetaSoundFrontendDocumentBuilder::GetBuildPageID() const
{
	return BuildPageID;
}

EMetasoundFrontendVertexAccessType FMetaSoundFrontendDocumentBuilder::GetNodeInputAccessType(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
	if (const int32* NodeIndex = NodeCache.FindNodeIndex(InNodeID))
	{
		const FMetasoundFrontendGraph& Graph = GetConstDocumentChecked().RootGraph.FindConstGraphChecked(PageID);
		const FMetasoundFrontendNode& Node = Graph.Nodes[*NodeIndex];
		auto IsVertexID = [&InVertexID](const FMetasoundFrontendVertex& Vertex) { return Vertex.VertexID == InVertexID; };
		if (const FMetasoundFrontendClass* Class = DocumentCache->FindDependency(Node.ClassID))
		{
			const EMetasoundFrontendClassType ClassType = Class->Metadata.GetType();
			switch (ClassType)
			{
				case EMetasoundFrontendClassType::Template:
				{
					const FNodeRegistryKey Key = FNodeRegistryKey(Class->Metadata);
					const INodeTemplate* Template = INodeTemplateRegistry::Get().FindTemplate(Key);
					if (ensureMsgf(Template, TEXT("Failed to find MetaSound node template registered with key '%s'"), *Key.ToString()))
					{
						if (Template->IsInputAccessTypeDynamic())
						{
							return Template->GetNodeInputAccessType(*this, PageID, InNodeID, InVertexID);
						}
					}
				}
				break;

				case EMetasoundFrontendClassType::Output:
				{
					const FMetasoundFrontendClassInput& ClassInput = Class->Interface.Inputs.Last();
					return ClassInput.AccessType;
				}

				default:
				break;
			}
			static_assert(static_cast<uint32>(EMetasoundFrontendClassType::Invalid) == 10, "Potential missing case coverage for EMetasoundFrontendClassType");

			if (const FMetasoundFrontendVertex* Vertex = Node.Interface.Inputs.FindByPredicate(IsVertexID))
			{
				auto IsClassInput = [VertexName = Vertex->Name](const FMetasoundFrontendClassInput& Input) { return Input.Name == VertexName; };
				if (const FMetasoundFrontendClassInput* ClassInput = Class->Interface.Inputs.FindByPredicate(IsClassInput))
				{
					return ClassInput->AccessType;
				}
			}
		}
	}

	return EMetasoundFrontendVertexAccessType::Unset;
}

const FMetasoundFrontendLiteral* FMetaSoundFrontendDocumentBuilder::GetNodeInputClassDefault(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID) const
{
	using namespace Metasound;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const Frontend::IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
	if (const int32* NodeIndex = NodeCache.FindNodeIndex(InNodeID))
	{
		const FMetasoundFrontendDocument& Document = GetConstDocumentChecked();
		const FMetasoundFrontendNode& Node = Document.RootGraph.FindConstGraphChecked(PageID).Nodes[*NodeIndex];
		auto IsVertexID = [&InVertexID](const FMetasoundFrontendVertex& Vertex) { return Vertex.VertexID == InVertexID; };
		if (const FMetasoundFrontendVertex* Vertex = Node.Interface.Inputs.FindByPredicate(IsVertexID))
		{
			if (const FMetasoundFrontendClass* Class = DocumentCache->FindDependency(Node.ClassID))
			{
				const EMetasoundFrontendClassType ClassType = Class->Metadata.GetType();
				switch (ClassType)
				{
					case EMetasoundFrontendClassType::Output:
					{
						const FMetasoundFrontendClassInput& ClassInput = Class->Interface.Inputs.Last();
						return ClassInput.FindConstDefault(Frontend::DefaultPageID);
					}
					break;

					default:
					{
						auto IsClassInput = [VertexName = Vertex->Name](const FMetasoundFrontendClassInput& Input) { return Input.Name == VertexName; };
						if (const FMetasoundFrontendClassInput* ClassInput = Class->Interface.Inputs.FindByPredicate(IsClassInput))
						{
							return ClassInput->FindConstDefault(Frontend::DefaultPageID);
						}
						static_assert(static_cast<uint32>(EMetasoundFrontendClassType::Invalid) == 10, "Potential missing case coverage for EMetasoundFrontendClassType "
							"(default may not be sufficient for newly added class types)");
					}
					break;
				}
				static_assert(static_cast<uint32>(EMetasoundFrontendClassType::Invalid) == 10, "Potential missing case coverage for EMetasoundFrontendClassType");
			}
		}
	}

	return nullptr;
}

const FMetasoundFrontendLiteral* FMetaSoundFrontendDocumentBuilder::GetNodeInputDefault(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
	if (const int32* NodeIndex = NodeCache.FindNodeIndex(InNodeID))
	{
		const FMetasoundFrontendGraph& Graph = GetConstDocumentChecked().RootGraph.FindConstGraphChecked(PageID);
		const FMetasoundFrontendNode& Node = Graph.Nodes[*NodeIndex];

		auto IsVertex = [&InVertexID](const FMetasoundFrontendVertex& Vertex) { return Vertex.VertexID == InVertexID; };
		const int32 VertexIndex = Node.Interface.Inputs.IndexOfByPredicate(IsVertex);
		if (VertexIndex != INDEX_NONE)
		{
			const FMetasoundFrontendVertex& NodeInput = Node.Interface.Inputs[VertexIndex];

			auto IsLiteral = [&InVertexID](const FMetasoundFrontendVertexLiteral& Literal) { return Literal.VertexID == InVertexID; };
			const int32 LiteralIndex = Node.InputLiterals.IndexOfByPredicate(IsLiteral);
			if (LiteralIndex != INDEX_NONE)
			{
				return &Node.InputLiterals[LiteralIndex].Value;
			}
		}
	}

	return nullptr;
}

EMetasoundFrontendVertexAccessType FMetaSoundFrontendDocumentBuilder::GetNodeOutputAccessType(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
	if (const int32* NodeIndex = NodeCache.FindNodeIndex(InNodeID))
	{
		const FMetasoundFrontendGraph& Graph = GetConstDocumentChecked().RootGraph.FindConstGraphChecked(PageID);
		const FMetasoundFrontendNode& Node = Graph.Nodes[*NodeIndex];
		if (const FMetasoundFrontendClass* Class = DocumentCache->FindDependency(Node.ClassID))
		{
			const EMetasoundFrontendClassType ClassType = Class->Metadata.GetType();
			switch (ClassType)
			{
				case EMetasoundFrontendClassType::Template:
				{
					const FNodeRegistryKey Key = FNodeRegistryKey(Class->Metadata);
					const INodeTemplate* Template = INodeTemplateRegistry::Get().FindTemplate(Key);
					if (ensureMsgf(Template, TEXT("Failed to find MetaSound node template registered with key '%s'"), *Key.ToString()))
					{
						if (Template->IsOutputAccessTypeDynamic())
						{
							return Template->GetNodeOutputAccessType(*this, PageID, InNodeID, InVertexID);
						}
					}
				}
				break;

				case EMetasoundFrontendClassType::Input:
				{
					const FMetasoundFrontendClassOutput& ClassOutput = Class->Interface.Outputs.Last();
					return ClassOutput.AccessType;
				}

				default:
				break;
			}
			static_assert(static_cast<uint32>(EMetasoundFrontendClassType::Invalid) == 10, "Potential missing case coverage for EMetasoundFrontendClassType");

			auto IsVertexID = [&InVertexID](const FMetasoundFrontendVertex& Vertex) { return Vertex.VertexID == InVertexID; };
			if (const FMetasoundFrontendVertex* Vertex = Node.Interface.Outputs.FindByPredicate(IsVertexID))
			{
				auto IsClassInput = [VertexName = Vertex->Name](const FMetasoundFrontendClassInput& Output) { return Output.Name == VertexName; };
				if (const FMetasoundFrontendClassOutput* ClassOutput = Class->Interface.Outputs.FindByPredicate(IsClassInput))
				{
					return ClassOutput->AccessType;
				}
			}
		}
	}

	return EMetasoundFrontendVertexAccessType::Unset;
}

#if WITH_EDITORONLY_DATA
const bool FMetaSoundFrontendDocumentBuilder::GetIsAdvancedDisplay(const FName MemberName, const EMetasoundFrontendClassType Type) const
{
	const FMetasoundFrontendDocument& Document = GetConstDocumentChecked();

	//Input
	if (Type == EMetasoundFrontendClassType::Input)
	{
		if (const int32* Index = DocumentCache->GetInterfaceCache().FindInputIndex(MemberName))
		{
			const FMetasoundFrontendClassInput& GraphInput = Document.RootGraph.Interface.Inputs[*Index];
			return GraphInput.Metadata.bIsAdvancedDisplay;
		}
	}
	//Output
	else if (Type == EMetasoundFrontendClassType::Output)
	{
		if (const int32* Index = DocumentCache->GetInterfaceCache().FindOutputIndex(MemberName))
		{
			const FMetasoundFrontendClassOutput& GraphOutput = Document.RootGraph.Interface.Outputs[*Index];
			return GraphOutput.Metadata.bIsAdvancedDisplay;
		}
	}
	return false;
}
#endif // WITH_EDITORONLY_DATA

void FMetaSoundFrontendDocumentBuilder::InitDocument(const FMetasoundFrontendDocument* InDocumentTemplate, const FMetasoundFrontendClassName* InNewClassName, bool bResetVersion)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FMetaSoundFrontendDocumentBuilder::InitDocument);

	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	Document.RootGraph.InitDefaultGraphPage();

	// 1. Set default class Metadata.
	if (InDocumentTemplate)
	{
		// 1a. If template provided, copy that.
		Document = *InDocumentTemplate;
		InitGraphClassMetadata(bResetVersion, InNewClassName);
	}
	else
	{
		// 1a. Initialize class using default data
		FMetasoundFrontendClassMetadata& ClassMetadata = Document.RootGraph.Metadata;
		InitGraphClassMetadata(Document.RootGraph.Metadata, bResetVersion, InNewClassName);

#if WITH_EDITORONLY_DATA
		// 1b. Set default doc version Metadata
		{
			FMetasoundFrontendDocumentMetadata& DocMetadata = Document.Metadata;
			DocMetadata.Version.Number = GetMaxDocumentVersion();
		}
#endif // WITH_EDITORONLY_DATA

		// 1c. Add default interfaces for given UClass
		{
			TArray<FMetasoundFrontendVersion> InitVersions = ISearchEngine::Get().FindUClassDefaultInterfaceVersions(GetBuilderClassPath());
			FModifyInterfaceOptions Options({ }, InitVersions);
			ModifyInterfaces(MoveTemp(Options));
		}
	}
}

bool FMetaSoundFrontendDocumentBuilder::IsValid() const
{
	return DocumentInterface.GetObject() != nullptr;
}

int32 FMetaSoundFrontendDocumentBuilder::GetTransactionCount() const
{
	using namespace Metasound::Frontend;

	if (DocumentCache.IsValid())
	{
		return StaticCastSharedPtr<FDocumentCache>(DocumentCache)->GetTransactionCount();
	}

	return 0;
}

void FMetaSoundFrontendDocumentBuilder::InitGraphClassMetadata(FMetasoundFrontendClassMetadata& InOutMetadata, bool bResetVersion, const FMetasoundFrontendClassName* NewClassName)
{
	if (NewClassName)
	{
		InOutMetadata.SetClassName(*NewClassName);
	}
	else
	{
		InOutMetadata.SetClassName(FMetasoundFrontendClassName(FName(), *FGuid::NewGuid().ToString(), FName()));
	}

	if (bResetVersion)
	{
		InOutMetadata.SetVersion({ 1, 0 });
	}

	InOutMetadata.SetType(EMetasoundFrontendClassType::Graph);
}

void FMetaSoundFrontendDocumentBuilder::InitGraphClassMetadata(bool bResetVersion, const FMetasoundFrontendClassName* NewClassName)
{
	InitGraphClassMetadata(GetDocumentChecked().RootGraph.Metadata, bResetVersion, NewClassName);
}

void FMetaSoundFrontendDocumentBuilder::InitNodeLocations()
{
#if WITH_EDITORONLY_DATA
	using namespace Metasound;
	using namespace Metasound::Frontend;

	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	Document.RootGraph.IterateGraphPages([&](FMetasoundFrontendGraph& Graph)
	{
		FVector2D InputNodeLocation = FVector2D::ZeroVector;
		FVector2D ExternalNodeLocation = InputNodeLocation + DisplayStyle::NodeLayout::DefaultOffsetX;
		FVector2D OutputNodeLocation = ExternalNodeLocation + DisplayStyle::NodeLayout::DefaultOffsetX;

		TArray<FMetasoundFrontendNode>& Nodes = Graph.Nodes;
		for (FMetasoundFrontendNode& Node : Nodes)
		{
			if (const int32* ClassIndex = DocumentCache->FindDependencyIndex(Node.ClassID))
			{
				FMetasoundFrontendClass& Class = Document.Dependencies[*ClassIndex];

				const EMetasoundFrontendClassType NodeType = Class.Metadata.GetType();
				FVector2D NewLocation;
				if (NodeType == EMetasoundFrontendClassType::Input)
				{
					NewLocation = InputNodeLocation;
					InputNodeLocation += DisplayStyle::NodeLayout::DefaultOffsetY;
				}
				else if (NodeType == EMetasoundFrontendClassType::Output)
				{
					NewLocation = OutputNodeLocation;
					OutputNodeLocation += DisplayStyle::NodeLayout::DefaultOffsetY;
				}
				else
				{
					NewLocation = ExternalNodeLocation;
					ExternalNodeLocation += DisplayStyle::NodeLayout::DefaultOffsetY;
				}

				// TODO: Find consistent location for controlling node locations.
				// Currently it is split between MetasoundEditor and MetasoundFrontend modules.
				FMetasoundFrontendNodeStyle& Style = Node.Style;
				if (Style.Display.Locations.IsEmpty())
				{
					Style.Display.Locations = { { FGuid::NewGuid(), NewLocation } };
				}
				// Initialize the position if the location hasn't been assigned yet.  This can happen
				// if default interfaces were assigned to the given MetaSound but not placed with respect
				// to one another.  In this case, node location initialization takes "priority" to avoid
				// visual overlap.
				else if (Style.Display.Locations.Num() == 1 && Style.Display.Locations.Contains(FGuid()))
				{
					Style.Display.Locations = { { FGuid::NewGuid(), NewLocation } };
				}
			}
		}
	});
#endif // WITH_EDITORONLY_DATA
}

bool FMetaSoundFrontendDocumentBuilder::IsDependencyReferenced(const FGuid& InClassID) const
{
	bool bIsReferenced = false;
	GetConstDocumentChecked().RootGraph.IterateGraphPages([this, &InClassID, &bIsReferenced](const FMetasoundFrontendGraph& Graph)
	{
		using namespace Metasound::Frontend;
		const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(Graph.PageID);
		bIsReferenced |= NodeCache.ContainsNodesOfClassID(InClassID);
	});
	return bIsReferenced;
}

bool FMetaSoundFrontendDocumentBuilder::IsNodeInputConnected(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID) const
{
	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	return DocumentCache->GetEdgeCache(PageID).IsNodeInputConnected(InNodeID, InVertexID);
}

bool FMetaSoundFrontendDocumentBuilder::IsNodeOutputConnected(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID) const
{
	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	return DocumentCache->GetEdgeCache(PageID).IsNodeOutputConnected(InNodeID, InVertexID);
}

bool FMetaSoundFrontendDocumentBuilder::IsInterfaceDeclared(FName InInterfaceName) const
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendInterface Interface;
	if (ISearchEngine::Get().FindInterfaceWithHighestVersion(InInterfaceName, Interface))
	{
		return IsInterfaceDeclared(Interface.Version);
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::IsInterfaceDeclared(const FMetasoundFrontendVersion& InInterfaceVersion) const
{
	return GetConstDocumentChecked().Interfaces.Contains(InInterfaceVersion);
}

bool FMetaSoundFrontendDocumentBuilder::IsPreset() const
{
	return GetConstDocumentChecked().RootGraph.PresetOptions.bIsPreset;
}

Metasound::Frontend::EInvalidEdgeReason FMetaSoundFrontendDocumentBuilder::IsValidEdge(const FMetasoundFrontendEdge& InEdge, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);

	const FMetasoundFrontendVertex* OutputVertex = NodeCache.FindOutputVertex(InEdge.FromNodeID, InEdge.FromVertexID);
	if (!OutputVertex)
	{
		return EInvalidEdgeReason::MissingOutput;
	}

	const FMetasoundFrontendVertex* InputVertex = NodeCache.FindInputVertex(InEdge.ToNodeID, InEdge.ToVertexID);
	if (!InputVertex)
	{
		return EInvalidEdgeReason::MissingInput;
	}

	if (OutputVertex->TypeName != InputVertex->TypeName)
	{
		return EInvalidEdgeReason::MismatchedDataType;
	}

	// TODO: Add cycle detection here

	const EMetasoundFrontendVertexAccessType OutputAccessType = GetNodeOutputAccessType(InEdge.FromNodeID, InEdge.FromVertexID, InPageID);
	const EMetasoundFrontendVertexAccessType InputAccessType = GetNodeInputAccessType(InEdge.ToNodeID, InEdge.ToVertexID, InPageID);
	if (!FMetasoundFrontendClassVertex::CanConnectVertexAccessTypes(OutputAccessType, InputAccessType))
	{
		return EInvalidEdgeReason::MismatchedAccessType;
	}

	return EInvalidEdgeReason::None;
}

void FMetaSoundFrontendDocumentBuilder::IterateNodesConnectedWithVertex(const FMetasoundFrontendVertexHandle& Vertex, TFunctionRef<void(const FMetasoundFrontendEdge&, FMetasoundFrontendNode&)> NodeIndexIterFunc, const FGuid& InPageID)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendGraph& Graph = GetDocumentChecked().RootGraph.FindGraphChecked(InPageID);
	TArray<FMetasoundFrontendEdge> EdgesToConnectedNodes; // Have to cache to avoid pointers becoming garbage in subsequent removal loop
	const IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache(InPageID);
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(InPageID);
	const TArray<const FMetasoundFrontendEdge*> Edges = EdgeCache.FindEdges(Vertex.NodeID, Vertex.VertexID);
	Algo::Transform(Edges, EdgesToConnectedNodes, [](const FMetasoundFrontendEdge* Edge) { check(Edge); return *Edge; });
	for (const FMetasoundFrontendEdge& Edge : EdgesToConnectedNodes)
	{
		const FGuid& ConnectedNodeID = Edge.ToNodeID == Vertex.NodeID ? Edge.FromNodeID : Edge.ToNodeID;
		if (const int32* ConnectedNodeIndex = NodeCache.FindNodeIndex(ConnectedNodeID))
		{
			FMetasoundFrontendNode& Node = Graph.Nodes[*ConnectedNodeIndex];
			NodeIndexIterFunc(Edge, Node);
		}
	}
}

void FMetaSoundFrontendDocumentBuilder::IterateNodesByClassType(Metasound::Frontend::FConstClassAndNodeFunctionRef Func, EMetasoundFrontendClassType ClassType, const FGuid* InPageID) const
{
	using namespace Metasound::Frontend;

	check(ClassType != EMetasoundFrontendClassType::Invalid);

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const FMetasoundFrontendDocument& Doc = GetConstDocumentChecked();
	const FMetasoundFrontendGraph& Graph = Doc.RootGraph.FindConstGraphChecked(PageID);
	for (const FMetasoundFrontendNode& Node : Graph.Nodes)
	{
		if (const FMetasoundFrontendClass* Class = FindDependency(Node.ClassID))
		{
			if (Class->Metadata.GetType() == ClassType)
			{
				Func(*Class, Node);
			}
		}
	}
}

bool FMetaSoundFrontendDocumentBuilder::ModifyInterfaces(Metasound::Frontend::FModifyInterfaceOptions&& InOptions)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendDocument& Doc = GetDocumentChecked();
	DocumentBuilderPrivate::FModifyInterfacesImpl Context(Doc, MoveTemp(InOptions));
	return Context.Execute(*this, *DocumentDelegates);
}

#if WITH_EDITORONLY_DATA
bool FMetaSoundFrontendDocumentBuilder::TransformTemplateNodes()
{
	using namespace Metasound::Frontend;

	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FMetaSoundFrontendDocumentBuilder::TransformTemplateNodes);

	struct FTemplateTransformParams
	{
		const Metasound::Frontend::INodeTemplate* Template = nullptr;
		TArray<FGuid> NodeIDs;
	};
	using FTemplateTransformParamsMap = TSortedMap<FGuid, FTemplateTransformParams>;

	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	TArray<FMetasoundFrontendClass>& Dependencies = Document.Dependencies;

	FTemplateTransformParamsMap TemplateParams;
	for (const FMetasoundFrontendClass& Dependency : Dependencies)
	{
		if (Dependency.Metadata.GetType() == EMetasoundFrontendClassType::Template)
		{
			const FNodeRegistryKey Key = FNodeRegistryKey(Dependency.Metadata);
			const INodeTemplate* Template = INodeTemplateRegistry::Get().FindTemplate(Key);
			ensureMsgf(Template, TEXT("Template not found for template class reference '%s'"), *Dependency.Metadata.GetClassName().ToString());
			TemplateParams.Add(Dependency.ID, FTemplateTransformParams { Template });
		}
	}

	if (TemplateParams.IsEmpty())
	{
		return false;
	}

	// 1. Execute generated template node transform on copy of node array,
	// which allows for addition/removal of nodes to/from original array container
	// without template transform having to worry about mutation while iterating
	TArray<FGuid> TemplateNodeIDs;
	bool bModified = false;
	Document.RootGraph.IterateGraphPages([this, &Dependencies, &TemplateParams, &bModified](FMetasoundFrontendGraph& Graph)
	{
		for (const FMetasoundFrontendNode& Node : Graph.Nodes)
		{
			if (FTemplateTransformParams* Params = TemplateParams.Find(Node.ClassID))
			{
				Params->NodeIDs.Add(Node.GetID());
			}
		}

		for (TPair<FGuid, FTemplateTransformParams>& Pair : TemplateParams)
		{
			FTemplateTransformParams& Params = Pair.Value;
			if (Params.Template)
			{
				TUniquePtr<INodeTemplateTransform> NodeTransform = Params.Template->GenerateNodeTransform();
				check(NodeTransform.IsValid());

				for (const FGuid& NodeID : Params.NodeIDs)
				{
					bModified = true;
					NodeTransform->Transform(Graph.PageID, NodeID, *this);
				}
			}
			Params.NodeIDs.Reset();
		}
	});

	// 2. Remove template classes from dependency list
	for (int32 i = Dependencies.Num() - 1; i >= 0; --i)
	{
		const FMetasoundFrontendClass& Class = Dependencies[i];
		if (TemplateParams.Contains(Class.ID))
		{
			DocumentDelegates->OnRemoveSwappingDependency.Broadcast(i, Dependencies.Num() - 1);
			Dependencies.RemoveAtSwap(i, EAllowShrinking::No);
		}
	}
	Dependencies.Shrink();

	return bModified;
}
#endif // WITH_EDITORONLY_DATA

void FMetaSoundFrontendDocumentBuilder::BeginBuilding(TSharedPtr<Metasound::Frontend::FDocumentModifyDelegates> Delegates, bool bPrimeCache)
{
	using namespace Metasound::Frontend;

	if (Delegates.IsValid())
	{
		DocumentDelegates = Delegates;
	}
	else
	{
		if (DocumentInterface)
		{
			const FMetasoundFrontendDocument& Document = GetConstDocumentChecked();
			DocumentDelegates = MakeShared<FDocumentModifyDelegates>(Document);
		}
		else
		{
			DocumentDelegates = MakeShared<FDocumentModifyDelegates>();
		}
	}

	if (DocumentInterface)
	{
		DocumentInterface->OnBeginActiveBuilder();

		const FMetasoundFrontendDocument& Document = GetConstDocumentChecked();
		DocumentCache = FDocumentCache::Create(Document, DocumentDelegates.ToSharedRef(), BuildPageID, bPrimeCache);
	}
}

void FMetaSoundFrontendDocumentBuilder::FinishBuilding()
{
	using namespace Metasound::Frontend;

	if (DocumentInterface)
	{
		DocumentInterface->OnFinishActiveBuilder();
		DocumentInterface = { };
	}

	DocumentDelegates.Reset();
	DocumentCache.Reset();
}

bool FMetaSoundFrontendDocumentBuilder::RemoveDependency(const FGuid& InClassID)
{
	using namespace Metasound::Frontend;

	bool bSuccess = false;
	if (const int32* IndexPtr = DocumentCache->FindDependencyIndex(InClassID))
	{
		FMetasoundFrontendDocument& Document = GetDocumentChecked();
		TArray<FMetasoundFrontendClass>& Dependencies = Document.Dependencies;
		const int32 Index = *IndexPtr;

		bSuccess = true;
		Document.RootGraph.IterateGraphPages([this, &bSuccess, &InClassID](const FMetasoundFrontendGraph& Graph)
		{
			const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(Graph.PageID);
			TArray<const FMetasoundFrontendNode*> Nodes = NodeCache.FindNodesOfClassID(InClassID);
			for (const FMetasoundFrontendNode* Node : Nodes)
			{
				bSuccess &= RemoveNode(Node->GetID());
			}
		});

		RemoveSwapDependencyInternal(Index);
	}

	return bSuccess;
}

bool FMetaSoundFrontendDocumentBuilder::RemoveDependency(EMetasoundFrontendClassType ClassType, const FMetasoundFrontendClassName& InClassName, const FMetasoundFrontendVersionNumber& InClassVersionNumber)
{
	using namespace Metasound::Frontend;

	bool bSuccess = false;
	const FNodeRegistryKey ClassKey(ClassType, InClassName, InClassVersionNumber);
	if (const int32* IndexPtr = DocumentCache->FindDependencyIndex(ClassKey))
	{
		FMetasoundFrontendDocument& Document = GetDocumentChecked();
		TArray<FMetasoundFrontendClass>& Dependencies = Document.Dependencies;
		const int32 Index = *IndexPtr;

		bSuccess = true;
		Document.RootGraph.IterateGraphPages([this, &bSuccess, &Dependencies, &Index](const FMetasoundFrontendGraph& Graph)
		{
			const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(Graph.PageID);
			TArray<const FMetasoundFrontendNode*> Nodes = NodeCache.FindNodesOfClassID(Dependencies[Index].ID);
			for (const FMetasoundFrontendNode* Node : Nodes)
			{
				bSuccess &= RemoveNode(Node->GetID());
			}
		});

		RemoveSwapDependencyInternal(Index);
	}

	return bSuccess;
}

void FMetaSoundFrontendDocumentBuilder::RemoveSwapDependencyInternal(int32 Index)
{
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	TArray<FMetasoundFrontendClass>& Dependencies = Document.Dependencies;
	const int32 LastIndex = Dependencies.Num() - 1;
	DocumentDelegates->OnRemoveSwappingDependency.Broadcast(Index, LastIndex);
	Dependencies.RemoveAtSwap(Index, EAllowShrinking::No);
}

bool FMetaSoundFrontendDocumentBuilder::RemoveEdge(const FMetasoundFrontendEdge& EdgeToRemove, const FGuid* InPageID)
{
	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	FMetasoundFrontendGraph& Graph = GetDocumentChecked().RootGraph.FindGraphChecked(PageID);
	TArray<FMetasoundFrontendEdge>& Edges = Graph.Edges;
	const IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache(PageID);
	if (const int32* IndexPtr = EdgeCache.FindEdgeIndexToNodeInput(EdgeToRemove.ToNodeID, EdgeToRemove.ToVertexID))
	{
		const int32 Index = *IndexPtr;
		FMetasoundFrontendEdge& FoundEdge = Edges[Index];
		if (EdgeToRemove.FromNodeID == FoundEdge.FromNodeID && EdgeToRemove.FromVertexID == FoundEdge.FromVertexID)
		{
			const int32 LastIndex = Edges.Num() - 1;
			DocumentDelegates->FindEdgeDelegatesChecked(PageID).OnRemoveSwappingEdge.Broadcast(Index, LastIndex);
			Edges.RemoveAtSwap(Index, EAllowShrinking::No);
			return true;
		}
	}

	return false;
}

#if WITH_EDITORONLY_DATA
bool FMetaSoundFrontendDocumentBuilder::RemoveEdgeStyle(const FGuid& InNodeID, FName OutputName, const FGuid* InPageID)
{
	auto IsEdgeStyle = [&InNodeID, &OutputName](const FMetasoundFrontendEdgeStyle& EdgeStyle)
	{
		return EdgeStyle.NodeID == InNodeID && EdgeStyle.OutputName == OutputName;
	};

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	FMetasoundFrontendGraph& Graph = Document.RootGraph.FindGraphChecked(PageID);
	return Graph.Style.EdgeStyles.RemoveAllSwap(IsEdgeStyle) > 0;
}
#endif // WITH_EDITORONLY_DATA

bool FMetaSoundFrontendDocumentBuilder::RemoveNamedEdges(const TSet<Metasound::Frontend::FNamedEdge>& InNamedEdgesToRemove, TArray<FMetasoundFrontendEdge>* OutRemovedEdges, const FGuid* InPageID)
{
	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
	const IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache(PageID);

	if (OutRemovedEdges)
	{
		OutRemovedEdges->Reset();
	}

	bool bSuccess = true;

	TArray<FMetasoundFrontendEdge> EdgesToRemove;
	for (const FNamedEdge& NamedEdge : InNamedEdgesToRemove)
	{
		const FMetasoundFrontendVertex* OutputVertex = NodeCache.FindOutputVertex(NamedEdge.OutputNodeID, NamedEdge.OutputName);
		const FMetasoundFrontendVertex* InputVertex = NodeCache.FindInputVertex(NamedEdge.InputNodeID, NamedEdge.InputName);

		if (OutputVertex && InputVertex)
		{
			FMetasoundFrontendEdge NewEdge = { NamedEdge.OutputNodeID, OutputVertex->VertexID, NamedEdge.InputNodeID, InputVertex->VertexID };
			if (EdgeCache.ContainsEdge(NewEdge))
			{
				EdgesToRemove.Add(MoveTemp(NewEdge));
			}
			else
			{
				bSuccess = false;
				UE_LOG(LogMetaSound, Warning, TEXT("Failed to remove connection between MetaSound node output '%s' and input '%s': No connection found."), *NamedEdge.OutputName.ToString(), *NamedEdge.InputName.ToString());
			}
		}
	}

	for (const FMetasoundFrontendEdge& EdgeToRemove : EdgesToRemove)
	{
		const bool bRemovedEdge = RemoveEdgeToNodeInput(EdgeToRemove.ToNodeID, EdgeToRemove.ToVertexID, InPageID);
		if (ensureAlwaysMsgf(bRemovedEdge, TEXT("Failed to remove MetaSound graph edge via DocumentBuilder when prior step validated edge remove was valid")))
		{
			if (OutRemovedEdges)
			{
				OutRemovedEdges->Add(EdgeToRemove);
			}
		}
		else
		{
			bSuccess = false;
		}
	}

	return bSuccess;
}

void FMetaSoundFrontendDocumentBuilder::Reload(TSharedPtr<Metasound::Frontend::FDocumentModifyDelegates> Delegates, bool bPrimeCache)
{
	using namespace Metasound::Frontend;

	if (DocumentInterface)
	{
		DocumentInterface->OnFinishActiveBuilder();
	}

	const FMetasoundFrontendDocument& Document = GetConstDocumentChecked();
	DocumentDelegates = Delegates.IsValid() ? Delegates : MakeShared<FDocumentModifyDelegates>(Document);

	if (DocumentInterface)
	{
		DocumentCache = FDocumentCache::Create(Document, DocumentDelegates.ToSharedRef(), BuildPageID, bPrimeCache);
		DocumentInterface->OnBeginActiveBuilder();
	}
}

#if WITH_EDITORONLY_DATA
bool FMetaSoundFrontendDocumentBuilder::RemoveGraphInputDefault(FName InputName, const FGuid& InPageID, bool bClearInheritsDefault)
{
	using namespace Metasound;

	auto NameMatchesInput = [&InputName](const FMetasoundFrontendClassInput& Input) { return Input.Name == InputName; };
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	TArray<FMetasoundFrontendClassInput>& Inputs = Document.RootGraph.Interface.Inputs;

	const int32 Index = Inputs.IndexOfByPredicate(NameMatchesInput);
	if (Index != INDEX_NONE)
	{
		FMetasoundFrontendClassInput& Input = Inputs[Index];
		const bool bRemovedDefault = Input.RemoveDefault(InPageID);
		if (bRemovedDefault)
		{
			DocumentDelegates->InterfaceDelegates.OnInputDefaultChanged.Broadcast(Index);

			if (bClearInheritsDefault)
			{
				// Set the input as no longer inheriting default for presets
				// (No-ops if MetaSound isn't preset or isn't set to inherit default).
				constexpr bool bInputInheritsDefault = false;
				SetGraphInputInheritsDefault(InputName, bInputInheritsDefault);
			}

			return true;
		}
	}

	return false;
}
#endif // WITH_EDITORONLY_DATA

bool FMetaSoundFrontendDocumentBuilder::RemoveNodeInputDefault(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID)
{
	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
	if (const int32* NodeIndex = NodeCache.FindNodeIndex(InNodeID))
	{
		FMetasoundFrontendGraph& Graph = GetDocumentChecked().RootGraph.FindGraphChecked(PageID);
		FMetasoundFrontendNode& Node = Graph.Nodes[*NodeIndex];

		auto IsVertex = [&InVertexID](const FMetasoundFrontendVertex& Vertex) { return Vertex.VertexID == InVertexID; };
		const int32 VertexIndex = Node.Interface.Inputs.IndexOfByPredicate(IsVertex);
		if (VertexIndex != INDEX_NONE)
		{
			auto IsLiteral = [&InVertexID](const FMetasoundFrontendVertexLiteral& Literal) { return Literal.VertexID == InVertexID; };
			const int32 LiteralIndex = Node.InputLiterals.IndexOfByPredicate(IsLiteral);
			if (LiteralIndex != INDEX_NONE)
			{
				FNodeModifyDelegates& NodeDelegates = DocumentDelegates->FindNodeDelegatesChecked(PageID);
				const FOnMetaSoundFrontendDocumentMutateNodeInputLiteralArray& OnRemovingNodeInputLiteral = NodeDelegates.OnRemovingNodeInputLiteral;
				const int32 LastIndex = Node.InputLiterals.Num() - 1;
				OnRemovingNodeInputLiteral.Broadcast(*NodeIndex, VertexIndex, LastIndex);
				if (LiteralIndex != LastIndex)
				{
					OnRemovingNodeInputLiteral.Broadcast(*NodeIndex, VertexIndex, LiteralIndex);
				}

				Node.InputLiterals.RemoveAtSwap(LiteralIndex, EAllowShrinking::No);
				if (LiteralIndex != LastIndex)
				{
					const FOnMetaSoundFrontendDocumentMutateNodeInputLiteralArray& OnNodeInputLiteralSet = NodeDelegates.OnNodeInputLiteralSet;
					OnNodeInputLiteralSet.Broadcast(*NodeIndex, VertexIndex, LiteralIndex);
				}
				return true;
			}
		}
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::RemoveEdges(const FGuid& InNodeID, const FGuid* InPageID)
{
	using namespace Metasound::Frontend;
	using namespace Metasound::VariableNames;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
	if (const FMetasoundFrontendNode* Node = NodeCache.FindNode(InNodeID))
	{
		const IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache(PageID);

		for (const FMetasoundFrontendVertex& Vertex : Node->Interface.Inputs)
		{
			RemoveEdgeToNodeInput(InNodeID, Vertex.VertexID, InPageID);
		}

		TArray<FMetasoundFrontendVertexHandle> ToVertexHandles;
		for (const FMetasoundFrontendVertex& Vertex : Node->Interface.Outputs)
		{
			RemoveEdgesFromNodeOutput(InNodeID, Vertex.VertexID, InPageID);
		}

		return true;
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::RemoveEdgesByNodeClassInterfaceBindings(const FGuid& InFromNodeID, const FGuid& InToNodeID, const FGuid* InPageID)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	TSet<FMetasoundFrontendVersion> FromInterfaceVersions;
	TSet<FMetasoundFrontendVersion> ToInterfaceVersions;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	if (FindNodeClassInterfaces(InFromNodeID, FromInterfaceVersions, PageID) && FindNodeClassInterfaces(InToNodeID, ToInterfaceVersions, PageID))
	{
		TSet<FNamedEdge> NamedEdges;
		if (DocumentBuilderPrivate::TryGetInterfaceBoundEdges(InFromNodeID, FromInterfaceVersions, InToNodeID, ToInterfaceVersions, NamedEdges))
		{
			constexpr TArray<FMetasoundFrontendEdge>* RemovedEdges = nullptr;
			return RemoveNamedEdges(NamedEdges, RemovedEdges, InPageID);
		}
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::RemoveEdgesFromNodeOutput(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID)
{
	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache(PageID);
	const TArrayView<const int32> Indices = EdgeCache.FindEdgeIndicesFromNodeOutput(InNodeID, InVertexID);
	if (!Indices.IsEmpty())
	{
		FMetasoundFrontendGraph& Graph = GetDocumentChecked().RootGraph.FindGraphChecked(PageID);

		// Copy off indices and sort descending as the edge array will be modified when notifying the cache in the loop below
		TArray<int32> IndicesCopy(Indices.GetData(), Indices.Num());
		Algo::Sort(IndicesCopy, [](const int32& L, const int32& R) { return L > R; });
		FEdgeModifyDelegates& EdgeDelegates = DocumentDelegates->FindEdgeDelegatesChecked(PageID);
		for (int32 Index : IndicesCopy)
		{
#if WITH_EDITORONLY_DATA
			if (const FMetasoundFrontendVertex* Vertex = FindNodeOutput(InNodeID, InVertexID))
			{
				auto IsEdgeStyle = [&InNodeID, OutputName = Vertex->Name](const FMetasoundFrontendEdgeStyle& EdgeStyle)
				{
					return EdgeStyle.NodeID == InNodeID && EdgeStyle.OutputName == OutputName;
				};
				Graph.Style.EdgeStyles.RemoveAllSwap(IsEdgeStyle);
			}
#endif // WITH_EDITORONLY_DATA

			const int32 LastIndex = Graph.Edges.Num() - 1;
			EdgeDelegates.OnRemoveSwappingEdge.Broadcast(Index, LastIndex);
			Graph.Edges.RemoveAtSwap(Index, EAllowShrinking::No);
		}

		return true;
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::RemoveEdgeToNodeInput(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID)
{
	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache(PageID);
	if (const int32* IndexPtr = EdgeCache.FindEdgeIndexToNodeInput(InNodeID, InVertexID))
	{
		FMetasoundFrontendGraph& Graph = GetDocumentChecked().RootGraph.FindGraphChecked(PageID);
		const int32 Index = *IndexPtr; // Copy off indices as the pointer may be modified when notifying the cache below

#if WITH_EDITORONLY_DATA
		if (const FMetasoundFrontendVertex* Vertex = FindNodeOutput(InNodeID, Graph.Edges[Index].FromVertexID))
		{
			auto IsEdgeStyle = [&InNodeID, OutputName = Vertex->Name](const FMetasoundFrontendEdgeStyle& EdgeStyle)
			{
				return EdgeStyle.NodeID == InNodeID && EdgeStyle.OutputName == OutputName;
			};
			Graph.Style.EdgeStyles.RemoveAllSwap(IsEdgeStyle);
		}
#endif // WITH_EDITORONLY_DATA

		const FEdgeModifyDelegates& EdgeDelegates = DocumentDelegates->FindEdgeDelegatesChecked(PageID);
		const int32 LastIndex = Graph.Edges.Num() - 1;
		EdgeDelegates.OnRemoveSwappingEdge.Broadcast(Index, LastIndex);
		Graph.Edges.RemoveAtSwap(Index, EAllowShrinking::No);

#if WITH_EDITORONLY_DATA
		GetDocumentChecked().Metadata.ModifyContext.AddNodeIDModified(InNodeID);
#endif // WITH_EDITORONLY_DATA

		return true;
	}

	return false;
}

#if WITH_EDITORONLY_DATA
bool FMetaSoundFrontendDocumentBuilder::RemoveGraphComment(const FGuid& InCommentID, const FGuid* InPageID)
{
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	FMetasoundFrontendGraph& Graph = Document.RootGraph.FindGraphChecked(InPageID ? *InPageID : BuildPageID);
	if (Graph.Style.Comments.Remove(InCommentID) > 0)
	{
		Document.Metadata.ModifyContext.SetDocumentModified();

		return true;
	}

	return false;
}
#endif // WITH_EDITORONLY_DATA

bool FMetaSoundFrontendDocumentBuilder::RemoveGraphInput(FName InInputName)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	if (const int32* IndexPtr = DocumentCache->GetInterfaceCache().FindInputIndex(InInputName))
	{
		TArray<FMetasoundFrontendClassInput>& Inputs = Document.RootGraph.Interface.Inputs;
		const FGuid NodeID = Inputs[*IndexPtr].NodeID;
		FGuid ClassID;
		bool bNodesRemoved = true;
		Document.RootGraph.IterateGraphPages([this, &Document, &ClassID, &NodeID, &InInputName, &bNodesRemoved](const FMetasoundFrontendGraph& Graph)
		{
			TArray<FGuid> NodeIDsToRemove { NodeID };

			if (const FMetasoundFrontendNode* Node = FindNode(NodeID, &Graph.PageID))
			{
				ClassID = Node->ClassID;
			}
			else
			{
				bNodesRemoved = false;
				return;
			}

			const TArray<const FMetasoundFrontendNode*> TemplateNodes = GetGraphInputTemplateNodes(InInputName, &Graph.PageID);
			Algo::Transform(TemplateNodes, NodeIDsToRemove, [](const FMetasoundFrontendNode* Node) { return Node->GetID(); });

			for (const FGuid& ToRemove : NodeIDsToRemove)
			{
				if (RemoveNode(ToRemove, &Graph.PageID))
				{
#if WITH_EDITORONLY_DATA
					Document.Metadata.ModifyContext.AddNodeIDModified(ToRemove);
#endif // WITH_EDITORONLY_DATA
				}
				else
				{
					bNodesRemoved = false;
				}
			}
		});

		if (bNodesRemoved)
		{
			const int32 Index = *IndexPtr;
			DocumentDelegates->InterfaceDelegates.OnRemovingInput.Broadcast(Index);

			const int32 LastIndex = Inputs.Num() - 1;
			if (Index != LastIndex)
			{
				DocumentDelegates->InterfaceDelegates.OnRemovingInput.Broadcast(LastIndex);
			}
			Inputs.RemoveAtSwap(Index, EAllowShrinking::No);
			if (Index != LastIndex)
			{
				DocumentDelegates->InterfaceDelegates.OnInputAdded.Broadcast(Index);
			}

#if WITH_EDITORONLY_DATA
			ClearMemberMetadata(NodeID);
			Document.Metadata.ModifyContext.AddMemberIDModified(NodeID);
#endif // WITH_EDITORONLY_DATA

			const bool bDependencyReferenced = IsDependencyReferenced(ClassID);
			if (bDependencyReferenced || RemoveDependency(ClassID))
			{
				return true;
			}
		}
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::RemoveGraphOutput(FName InOutputName)
{
	bool bNodesRemoved = true;
	FGuid ClassID;
	FGuid NodeID;
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	Document.RootGraph.IterateGraphPages([this, &InOutputName, &Document, &ClassID, &NodeID, &bNodesRemoved](const FMetasoundFrontendGraph& Graph)
	{
		if (const FMetasoundFrontendNode* Node = FindGraphOutputNode(InOutputName, &Graph.PageID))
		{
			ClassID = Node->ClassID;
			NodeID = Node->GetID();
			if (!RemoveNode(NodeID, &Graph.PageID))
			{
				bNodesRemoved = false;
				return;
			}

#if WITH_EDITORONLY_DATA
			Document.Metadata.ModifyContext.AddNodeIDModified(NodeID);
#endif // WITH_EDITORONLY_DATA
		}
	});

	if (bNodesRemoved)
	{
		TArray<FMetasoundFrontendClassOutput>& Outputs = Document.RootGraph.Interface.Outputs;
		auto OutputNameMatches = [InOutputName](const FMetasoundFrontendClassOutput& Output) { return Output.Name == InOutputName; };
		const int32 Index = Outputs.IndexOfByPredicate(OutputNameMatches);
		if (Index != INDEX_NONE)
		{
			DocumentDelegates->InterfaceDelegates.OnRemovingOutput.Broadcast(Index);

			const int32 LastIndex = Outputs.Num() - 1;
			if (Index != LastIndex)
			{
				DocumentDelegates->InterfaceDelegates.OnRemovingOutput.Broadcast(LastIndex);
			}
			Outputs.RemoveAtSwap(Index, EAllowShrinking::No);
			if (Index != LastIndex)
			{
				DocumentDelegates->InterfaceDelegates.OnOutputAdded.Broadcast(Index);
			}

#if WITH_EDITORONLY_DATA
			ClearMemberMetadata(NodeID);
			Document.Metadata.ModifyContext.AddMemberIDModified(NodeID);
#endif // WITH_EDITORONLY_DATA

			const bool bDependencyReferenced = IsDependencyReferenced(ClassID);
			if (bDependencyReferenced || RemoveDependency(ClassID))
			{
				return true;
			}
		}
	}

	return false;
}

#if WITH_EDITORONLY_DATA
bool FMetaSoundFrontendDocumentBuilder::RemoveGraphPage(const FGuid& InPageID)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	FGuid AdjacentPageID;

	if (Document.RootGraph.ContainsGraphPage(InPageID))
	{
		DocumentDelegates->RemovePageDelegates(InPageID);
	}

	const bool bPageRemoved = Document.RootGraph.RemoveGraphPage(InPageID, &AdjacentPageID);
	if (bPageRemoved)
	{
		if (InPageID == BuildPageID)
		{
			ensureAlwaysMsgf(SetBuildPageID(AdjacentPageID), TEXT("AdjacentPageID returned is always expected to be valid"));
		}
	}

	return bPageRemoved;
}
#endif // WITH_EDITORONLY_DATA

bool FMetaSoundFrontendDocumentBuilder::RemoveInterface(FName InterfaceName)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendInterface Interface;
	if (ISearchEngine::Get().FindInterfaceWithHighestVersion(InterfaceName, Interface))
	{
		if (!GetDocumentChecked().Interfaces.Contains(Interface.Version))
		{
			UE_LOG(LogMetaSound, VeryVerbose, TEXT("MetaSound interface '%s' not found on document. MetaSoundBuilder skipping remove request."), *InterfaceName.ToString());
			return true;
		}

		const FTopLevelAssetPath BuilderClassPath = GetBuilderClassPath();
		const FInterfaceRegistryKey Key = GetInterfaceRegistryKey(Interface.Version);
		if (const IInterfaceRegistryEntry* Entry = IInterfaceRegistry::Get().FindInterfaceRegistryEntry(Key))
		{
			const FMetasoundFrontendInterfaceUClassOptions* ClassOptions = Entry->GetInterface().FindClassOptions(BuilderClassPath);
			if (ClassOptions && !ClassOptions->bIsModifiable)
			{
				UE_LOG(LogMetaSound, Error, TEXT("DocumentBuilder failed to remove MetaSound Interface '%s' to document: is not set to be modifiable for given UClass '%s'"), *InterfaceName.ToString(), *BuilderClassPath.ToString());
				return false;
			}

			TArray<FMetasoundFrontendInterface> InterfacesToRemove;
			InterfacesToRemove.Add(Entry->GetInterface());
			FModifyInterfaceOptions Options(MoveTemp(InterfacesToRemove), { });
			return ModifyInterfaces(MoveTemp(Options));
		}
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::RemoveNode(const FGuid& InNodeID, const FGuid* InPageID)
{
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FMetaSoundFrontendDocumentBuilder::RemoveNode);

	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
	const IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache(PageID);

	if (const int32* IndexPtr = NodeCache.FindNodeIndex(InNodeID))
	{
		const int32 Index = *IndexPtr; // Copy off indices as the pointer may be modified when notifying the cache below

		FMetasoundFrontendGraph& Graph = GetDocumentChecked().RootGraph.FindGraphChecked(PageID);
		TArray<FMetasoundFrontendNode>& Nodes = Graph.Nodes;
		const FMetasoundFrontendNode& Node = Nodes[Index];
		const FGuid& NodeID = Node.GetID();

		const FMetasoundFrontendClass* NodeClass = DocumentCache->FindDependency(Node.ClassID);
		check(NodeClass);
		const EMetasoundFrontendClassType ClassType = NodeClass->Metadata.GetType();
		switch (ClassType)
		{
			case EMetasoundFrontendClassType::Variable:
			case EMetasoundFrontendClassType::VariableDeferredAccessor:
			case EMetasoundFrontendClassType::VariableAccessor:
			case EMetasoundFrontendClassType::VariableMutator:
			{
				const bool bVariableNodeUnlinked = UnlinkVariableNode(NodeID, PageID);
				ensureAlwaysMsgf(bVariableNodeUnlinked, TEXT("Failed to unlink %s node with ID '%s"), LexToString(ClassType), *InNodeID.ToString());
			}
			break;
		}

		RemoveEdges(NodeID, InPageID);
		const int32 LastIndex = Nodes.Num() - 1;
		FNodeModifyDelegates& NodeDelegates = DocumentDelegates->FindNodeDelegatesChecked(PageID);
		NodeDelegates.OnRemoveSwappingNode.Broadcast(Index, LastIndex);
		Nodes.RemoveAtSwap(Index, EAllowShrinking::No);

#if WITH_EDITORONLY_DATA
		GetDocumentChecked().Metadata.ModifyContext.AddNodeIDModified(InNodeID);
#endif // WITH_EDITORONLY_DATA

		return true;
	}

	return false;
}

#if WITH_EDITORONLY_DATA
int32 FMetaSoundFrontendDocumentBuilder::RemoveNodeLocation(const FGuid& InNodeID, const FGuid* InLocationGuid, const FGuid* InPageID)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
	if (const int32* NodeIndex = NodeCache.FindNodeIndex(InNodeID))
	{
		FMetasoundFrontendGraph& Graph = GetDocumentChecked().RootGraph.FindGraphChecked(PageID);
		FMetasoundFrontendNode& Node = Graph.Nodes[*NodeIndex];
		FMetasoundFrontendNodeStyle& Style = Node.Style;
		if (InLocationGuid)
		{
			return Style.Display.Locations.Remove(*InLocationGuid);
		}
		else
		{
			const int32 NumLocationsRemoved = Style.Display.Locations.Num();
			Style.Display.Locations.Reset();
			return NumLocationsRemoved;
		}
	}

	return 0;
}
#endif // WITH_EDITORONLY_DATA

bool FMetaSoundFrontendDocumentBuilder::RemoveUnusedDependencies()
{
	bool bDidEdit = false;

	const FMetasoundFrontendDocument& Document = GetConstDocumentChecked();
	const FMetasoundFrontendGraphClass& RootGraph = Document.RootGraph;
	const TArray<FMetasoundFrontendClass>& Dependencies = Document.Dependencies;

	for (int32 Index = Dependencies.Num() - 1; Index >= 0; --Index)
	{
		const FGuid& ClassID = Dependencies[Index].ID;
		const bool bIsReferenced = IsDependencyReferenced(ClassID);
		if (!bIsReferenced)
		{
			RemoveSwapDependencyInternal(Index);
			bDidEdit = true;
		}
	}

	return bDidEdit;
}

bool FMetaSoundFrontendDocumentBuilder::RenameRootGraphClass(const FMetasoundFrontendClassName& InName)
{
	return false;
}

void FMetaSoundFrontendDocumentBuilder::ReloadCache()
{
	using namespace Metasound::Frontend;

	Reload(DocumentDelegates, true);
}

#if WITH_EDITORONLY_DATA
bool FMetaSoundFrontendDocumentBuilder::ResetGraphInputDefault(FName InputName)
{
	using namespace Metasound;

	auto NameMatchesInput = [&InputName](const FMetasoundFrontendClassInput& Input) { return Input.Name == InputName; };
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	TArray<FMetasoundFrontendClassInput>& Inputs = Document.RootGraph.Interface.Inputs;

	const int32 Index = Inputs.IndexOfByPredicate(NameMatchesInput);
	if (Index != INDEX_NONE)
	{
		FMetasoundFrontendClassInput& Input = Inputs[Index];
		Input.ResetDefaults();

		DocumentDelegates->InterfaceDelegates.OnInputDefaultChanged.Broadcast(Index);

		// Set the input as inheriting default for presets
		// (No-ops if MetaSound isn't preset or is already set to inherit default).
		constexpr bool bInputInheritsDefault = true;
		SetGraphInputInheritsDefault(InputName, bInputInheritsDefault);

		Document.Metadata.ModifyContext.AddMemberIDModified(Input.NodeID);
		return true;
	}

	return false;
}

void FMetaSoundFrontendDocumentBuilder::ResetGraphPages(bool bClearDefaultGraph)
{
	using namespace Metasound;

	FMetasoundFrontendGraphClass& RootGraph = GetDocumentChecked().RootGraph;
	RootGraph.IterateGraphPages([this](FMetasoundFrontendGraph& Graph)
	{
		if (Graph.PageID != Frontend::DefaultPageID)
		{
			DocumentDelegates->PageDelegates.OnRemovingPage.Broadcast(Frontend::FDocumentMutatePageArgs{ Graph.PageID });
		}
	});

	RootGraph.ResetGraphPages(bClearDefaultGraph);
	SetBuildPageID(Frontend::DefaultPageID);
}
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
void FMetaSoundFrontendDocumentBuilder::SetAuthor(const FString& InAuthor)
{
	FMetasoundFrontendClassMetadata& ClassMetadata = GetDocumentChecked().RootGraph.Metadata;
	ClassMetadata.SetAuthor(InAuthor);
}
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
bool FMetaSoundFrontendDocumentBuilder::SetBuildPageID(const FGuid& InBuildPageID, bool bBroadcastDelegate)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	if (const FMetasoundFrontendGraph* BuildGraph = Document.RootGraph.FindConstGraph(InBuildPageID))
	{
		if (BuildPageID != BuildGraph->PageID)
		{
			BuildPageID = BuildGraph->PageID;

			constexpr bool bPrimeCache = false;
			DocumentCache->SetBuildPageID(BuildPageID);
			if (bBroadcastDelegate)
			{
				DocumentDelegates->PageDelegates.OnPageSet.Broadcast({ BuildPageID });
			}
		}
		return true;
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::SetGraphInputAdvancedDisplay(const FName InputName, const bool InAdvancedDisplay)
{
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	FMetasoundFrontendGraphClass& RootGraph = Document.RootGraph;

	if (const int32* Index = DocumentCache->GetInterfaceCache().FindInputIndex(InputName))
	{
		FMetasoundFrontendClassInput& GraphInput = RootGraph.Interface.Inputs[*Index];
		if (GraphInput.Metadata.bIsAdvancedDisplay != InAdvancedDisplay)
		{
			GraphInput.Metadata.SetIsAdvancedDisplay(InAdvancedDisplay);
			Document.Metadata.ModifyContext.AddMemberIDModified(GraphInput.VertexID);
			return true;
		}
	}

	return false;
}
#endif // WITH_EDITORONLY_DATA

bool FMetaSoundFrontendDocumentBuilder::SetGraphInputAccessType(FName InputName, EMetasoundFrontendVertexAccessType AccessType)
{
	using namespace Metasound::Frontend;

	if (!ensureMsgf(AccessType != EMetasoundFrontendVertexAccessType::Unset, TEXT("Cannot set graph input access type to '%s'"), LexToString(AccessType)))
	{
		return false;
	}

	const int32* Index = DocumentCache->GetInterfaceCache().FindInputIndex(InputName);
	if (!Index)
	{
		return false;
	}

	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	FMetasoundFrontendGraphClass& RootGraph = Document.RootGraph;
	FMetasoundFrontendClassInput& GraphInput = RootGraph.Interface.Inputs[*Index];

	if (GraphInput.AccessType != AccessType)
	{
		GraphInput.AccessType = AccessType;
		if (AccessType == EMetasoundFrontendVertexAccessType::Reference)
		{
			RootGraph.IterateGraphPages([this, &GraphInput, &AccessType](FMetasoundFrontendGraph& Graph)
			{
				const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(Graph.PageID);
				if (const int32* NodeIndex = NodeCache.FindNodeIndex(GraphInput.NodeID))
				{
					FMetasoundFrontendNode& Node = Graph.Nodes[*NodeIndex];
					const FMetasoundFrontendVertex& NodeOutput = Node.Interface.Outputs.Last();
					IterateNodesConnectedWithVertex({ GraphInput.NodeID, NodeOutput.VertexID }, [this, &Graph, &AccessType](const FMetasoundFrontendEdge& Edge, FMetasoundFrontendNode& ConnectedNode)
					{
						if (const FMetasoundFrontendClass* ConnectedNodeClass = FindDependency(ConnectedNode.ClassID))
						{
							// If connected to an input template node, disconnect the template node from other nodes as the data type is
							// about to be mismatched.  Otherwise, direct connection to other nodes (i.e. at runtime when template
							// nodes aren't injected) forcefully remove to avoid data type mismatch.
							if (ConnectedNodeClass->Metadata.GetClassName() == FInputNodeTemplate::ClassName)
							{
								const FMetasoundFrontendVertex& ConnectedNodeOutput = ConnectedNode.Interface.Outputs.Last();
								IterateNodesConnectedWithVertex({ Edge.ToNodeID, ConnectedNodeOutput.VertexID }, [this, &Graph, &AccessType](const FMetasoundFrontendEdge& TempEdge, FMetasoundFrontendNode&)
								{
									const EMetasoundFrontendVertexAccessType ConnectedAccessType = GetNodeInputAccessType(TempEdge.ToNodeID, TempEdge.ToVertexID, &Graph.PageID);
									if (!FMetasoundFrontendClassVertex::CanConnectVertexAccessTypes(AccessType, ConnectedAccessType))
									{
										RemoveEdgeToNodeInput(TempEdge.ToNodeID, TempEdge.ToVertexID, &Graph.PageID);
									}
								}, Graph.PageID);
							}
							else
							{
								const EMetasoundFrontendVertexAccessType ConnectedAccessType = GetNodeInputAccessType(Edge.ToNodeID, Edge.ToVertexID, &Graph.PageID);
								if (!FMetasoundFrontendClassVertex::CanConnectVertexAccessTypes(AccessType, ConnectedAccessType))
								{
									RemoveEdgeToNodeInput(Edge.ToNodeID, Edge.ToVertexID, &Graph.PageID);
								}
							}
						}
					}, Graph.PageID);
				}
			});

			const bool bNodeConformed = ConformGraphInputNodeToClass(GraphInput);
			if (!bNodeConformed)
			{
				return false;
			}

#if WITH_EDITORONLY_DATA
			Document.Metadata.ModifyContext.AddMemberIDModified(GraphInput.NodeID);
#endif // WITH_EDITORONLY_DATA
		}
	}

	return true;
}

bool FMetaSoundFrontendDocumentBuilder::SetGraphInputDataType(FName InputName, FName DataType)
{
	using namespace Metasound;

	if (Frontend::IDataTypeRegistry::Get().IsRegistered(DataType))
	{
		const int32* Index = DocumentCache->GetInterfaceCache().FindInputIndex(InputName);
		if (!Index)
		{
			return false;
		}

		FMetasoundFrontendDocument& Document = GetDocumentChecked();
		FMetasoundFrontendGraphClass& RootGraph = Document.RootGraph;
		FMetasoundFrontendClassInput& GraphInput = RootGraph.Interface.Inputs[*Index];
		if (GraphInput.TypeName != DataType)
		{
			GraphInput.TypeName = DataType;
			GraphInput.ResetDefaults();

			RootGraph.IterateGraphPages([this, &DataType, &GraphInput](FMetasoundFrontendGraph& Graph)
			{
				const Frontend::IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(Graph.PageID);
				if (const int32* NodeIndex = NodeCache.FindNodeIndex(GraphInput.NodeID))
				{
					FMetasoundFrontendNode& Node = Graph.Nodes[*NodeIndex];
					FMetasoundFrontendVertex& NodeOutput = Node.Interface.Outputs.Last();
					IterateNodesConnectedWithVertex({ GraphInput.NodeID, NodeOutput.VertexID }, [this, &Graph, &DataType](const FMetasoundFrontendEdge& Edge, FMetasoundFrontendNode& ConnectedNode)
					{
						const FMetasoundFrontendClass* ConnectedNodeClass = FindDependency(ConnectedNode.ClassID);
						if (ensure(ConnectedNodeClass))
						{
							// If connected to an input template node, disconnect the template node from other nodes as the data type is
							// about to be mismatched.  Otherwise, direct connection to other nodes (i.e. at runtime when template
							// nodes aren't injected) forcefully remove to avoid data type mismatch.
							if (ConnectedNodeClass->Metadata.GetClassName() == Frontend::FInputNodeTemplate::ClassName)
							{
								RemoveEdgesFromNodeOutput(Edge.ToNodeID, ConnectedNode.Interface.Outputs.Last().VertexID, &Graph.PageID);
								ConnectedNode.Interface.Inputs.Last().TypeName = DataType;
								ConnectedNode.Interface.Outputs.Last().TypeName = DataType;
							}
							else
							{
								RemoveEdgeToNodeInput(Edge.ToNodeID, Edge.ToVertexID, &Graph.PageID);
							}
						}
					}, Graph.PageID);
				}
			});

			const bool bNodeConformed = ConformGraphInputNodeToClass(GraphInput);
			if (!bNodeConformed)
			{
				return false;
			}

			RemoveUnusedDependencies();

#if WITH_EDITORONLY_DATA
			ClearMemberMetadata(GraphInput.NodeID);
			Document.Metadata.ModifyContext.AddMemberIDModified(GraphInput.NodeID);
			Document.Metadata.ModifyContext.AddNodeIDModified(GraphInput.NodeID);
#endif // WITH_EDITORONLY_DATA
		}
	}

	return true;
}

bool FMetaSoundFrontendDocumentBuilder::SetGraphInputDefault(FName InputName, FMetasoundFrontendLiteral InDefaultLiteral, const FGuid* InPageID)
{
	using namespace Metasound;

	auto NameMatchesInput = [&InputName](const FMetasoundFrontendClassInput& Input) { return Input.Name == InputName; };
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	TArray<FMetasoundFrontendClassInput>& Inputs = Document.RootGraph.Interface.Inputs;

	const int32 Index = Inputs.IndexOfByPredicate(NameMatchesInput);
	if (Index != INDEX_NONE)
	{
		FMetasoundFrontendClassInput& Input = Inputs[Index];
		if (Frontend::IDataTypeRegistry::Get().IsLiteralTypeSupported(Input.TypeName, InDefaultLiteral.GetType()))
		{
			const FGuid PageID = InPageID ? *InPageID : BuildPageID;
			bool bFound = false;
			Input.IterateDefaults([&bFound, &PageID, &InDefaultLiteral](const FGuid& InputPageID, FMetasoundFrontendLiteral& InputLiteral)
			{
				if (!bFound && InputPageID == PageID)
				{
					bFound = true;
					InputLiteral = MoveTemp(InDefaultLiteral);
				}
			});
			if (!bFound)
			{
				Input.AddDefault(PageID) = MoveTemp(InDefaultLiteral);
			}
			DocumentDelegates->InterfaceDelegates.OnInputDefaultChanged.Broadcast(Index);

			// Set the input as inheriting default for presets
			// (No-ops if MetaSound isn't preset or is already set to inherit default).
			constexpr bool bInputInheritsDefault = false;
			SetGraphInputInheritsDefault(InputName, bInputInheritsDefault);

			return true;
		}
		UE_LOG(LogMetaSound, Error, TEXT("Attempting to set graph input of type '%s' with unsupported literal type"), *Input.TypeName.ToString());
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::SetGraphInputDefaults(FName InputName, TArray<FMetasoundFrontendClassInputDefault> Defaults)
{
	using namespace Metasound;

	auto NameMatchesInput = [&InputName](const FMetasoundFrontendClassInput& Input) { return Input.Name == InputName; };
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	TArray<FMetasoundFrontendClassInput>& Inputs = Document.RootGraph.Interface.Inputs;

	const int32 Index = Inputs.IndexOfByPredicate(NameMatchesInput);
	if (Index != INDEX_NONE)
	{
		FMetasoundFrontendClassInput& Input = Inputs[Index];
		TSet<FGuid> ValidPageIDs;
		bool bAllSupported = Algo::AllOf(Defaults, [&Input](const FMetasoundFrontendClassInputDefault& Default)
		{
			return Frontend::IDataTypeRegistry::Get().IsLiteralTypeSupported(Input.TypeName, Default.Literal.GetType());
		});
		if (bAllSupported)
		{
			Input.SetDefaults(MoveTemp(Defaults));
			DocumentDelegates->InterfaceDelegates.OnInputDefaultChanged.Broadcast(Index);

			// Set the input as no longer inheriting default for presets
			// (No-ops if MetaSound isn't preset or isn't set to inherit default).
			constexpr bool bInputInheritsDefault = false;
			SetGraphInputInheritsDefault(InputName, bInputInheritsDefault);
			return true;
		}
		UE_LOG(LogMetaSound, Error, TEXT("Attempting to set graph input of type '%s' with unsupported literal type(s)"), *Input.TypeName.ToString());
	}

	return false;
}

#if WITH_EDITORONLY_DATA
bool FMetaSoundFrontendDocumentBuilder::SetGraphOutputAdvancedDisplay(const FName OutputName, const bool InAdvancedDisplay)
{
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	FMetasoundFrontendGraphClass& RootGraph = Document.RootGraph;

	if (const int32* Index = DocumentCache->GetInterfaceCache().FindOutputIndex(OutputName))
	{
		FMetasoundFrontendClassOutput& GraphOutput = Document.RootGraph.Interface.Outputs[*Index];
		if (GraphOutput.Metadata.bIsAdvancedDisplay != InAdvancedDisplay)
		{
			GraphOutput.Metadata.SetIsAdvancedDisplay(InAdvancedDisplay);
			Document.Metadata.ModifyContext.AddMemberIDModified(GraphOutput.VertexID);
			return true;
		}
	}

	return false;
}
#endif // WITH_EDITORONLY_DATA

bool FMetaSoundFrontendDocumentBuilder::SetGraphInputInheritsDefault(FName InName, bool bInputInheritsDefault)
{
	FMetasoundFrontendGraphClassPresetOptions& PresetOptions = GetDocumentChecked().RootGraph.PresetOptions;
	if (bInputInheritsDefault)
	{
		if (PresetOptions.bIsPreset)
		{
			return PresetOptions.InputsInheritingDefault.Add(InName).IsValidId();
		}
	}
	else
	{
		if (PresetOptions.bIsPreset)
		{
			return PresetOptions.InputsInheritingDefault.Remove(InName) > 0;
		}
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::SetGraphInputName(FName InputName, FName NewName)
{
	using namespace Metasound::Frontend;

	if (InputName == NewName)
	{
		return true;
	}

	const int32* Index = DocumentCache->GetInterfaceCache().FindInputIndex(InputName);
	if (!Index)
	{
		return false;
	}

	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	FMetasoundFrontendGraphClass& RootGraph = Document.RootGraph;

	FMetasoundFrontendClassInput& GraphInput = RootGraph.Interface.Inputs[*Index];
	GraphInput.Name = NewName;

	RootGraph.IterateGraphPages([this, &GraphInput, &NewName](FMetasoundFrontendGraph& Graph)
	{
		const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(Graph.PageID);
		if (const int32* NodeIndex = NodeCache.FindNodeIndex(GraphInput.NodeID))
		{
			FMetasoundFrontendNode& Node = Graph.Nodes[*NodeIndex];
			Node.Name = NewName;
			for (FMetasoundFrontendVertex& Vertex : Node.Interface.Inputs)
			{
				Vertex.Name = NewName;
			}
			for (FMetasoundFrontendVertex& Vertex : Node.Interface.Outputs)
			{
				Vertex.Name = NewName;
			}
		}
	});

	DocumentDelegates->InterfaceDelegates.OnInputNameChanged.Broadcast(InputName, NewName);

#if WITH_EDITORONLY_DATA
	Document.Metadata.ModifyContext.AddMemberIDModified(GraphInput.NodeID);
#endif // WITH_EDITORONLY_DATA

	return true;
}

bool FMetaSoundFrontendDocumentBuilder::SetGraphOutputName(FName OutputName, FName NewName)
{
	using namespace Metasound::Frontend;

	if (OutputName == NewName)
	{
		return true;
	}

	const int32* Index = DocumentCache->GetInterfaceCache().FindOutputIndex(OutputName);
	if (!Index)
	{
		return false;
	}

	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	FMetasoundFrontendGraphClass& GraphClass = Document.RootGraph;
	FMetasoundFrontendClassInterface& Interface = GraphClass.Interface;
	Interface.UpdateChangeID();

	FMetasoundFrontendClassOutput& GraphOutput = Interface.Outputs[*Index];
	GraphOutput.Name = NewName;
	
	GraphClass.IterateGraphPages([this, &GraphOutput, &NewName](FMetasoundFrontendGraph& Graph)
	{
		const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(Graph.PageID);
		if (const int32* NodeIndex = NodeCache.FindNodeIndex(GraphOutput.NodeID))
		{
			FMetasoundFrontendNode& Node = Graph.Nodes[*NodeIndex];
			Node.Name = NewName;
			for (FMetasoundFrontendVertex& Vertex : Node.Interface.Inputs)
			{
				Vertex.Name = NewName;
			}
			for (FMetasoundFrontendVertex& Vertex : Node.Interface.Outputs)
			{
				Vertex.Name = NewName;
			}
		}
	});
	DocumentDelegates->InterfaceDelegates.OnOutputNameChanged.Broadcast(OutputName, NewName);
	
#if WITH_EDITORONLY_DATA
	Document.Metadata.ModifyContext.AddMemberIDModified(GraphOutput.NodeID);
#endif // WITH_EDITORONLY_DATA

	return true;
}

bool FMetaSoundFrontendDocumentBuilder::SetGraphOutputAccessType(FName OutputName, EMetasoundFrontendVertexAccessType AccessType)
{
	using namespace Metasound::Frontend;

	if (!ensureMsgf(AccessType != EMetasoundFrontendVertexAccessType::Unset, TEXT("Cannot set graph output access type to '%s'"), LexToString(AccessType)))
	{
		return false;
	}

	const int32* Index = DocumentCache->GetInterfaceCache().FindOutputIndex(OutputName);
	if (!Index)
	{
		return false;
	}

	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	FMetasoundFrontendGraphClass& RootGraph = Document.RootGraph;
	FMetasoundFrontendClassOutput& GraphOutput = RootGraph.Interface.Outputs[*Index];
	if (GraphOutput.AccessType != AccessType)
	{
		GraphOutput.AccessType = AccessType;
		if (AccessType == EMetasoundFrontendVertexAccessType::Value)
		{
			RootGraph.IterateGraphPages([this, &GraphOutput, &AccessType](FMetasoundFrontendGraph& Graph)
			{
				const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(Graph.PageID);
				if (const int32* NodeIndex = NodeCache.FindNodeIndex(GraphOutput.NodeID))
				{
					FMetasoundFrontendNode& Node = Graph.Nodes[*NodeIndex];
					const FMetasoundFrontendVertex& NodeInput = Node.Interface.Inputs.Last();
					IterateNodesConnectedWithVertex({ GraphOutput.NodeID, NodeInput.VertexID }, [this, &Graph, &AccessType](const FMetasoundFrontendEdge& Edge, FMetasoundFrontendNode& ConnectedNode)
					{
						if (const FMetasoundFrontendClass* ConnectedNodeClass = FindDependency(ConnectedNode.ClassID))
						{
							const FMetasoundFrontendVertex& ConnectedNodeOutput = ConnectedNode.Interface.Outputs.Last();
							const EMetasoundFrontendVertexAccessType ConnectedAccessType = GetNodeOutputAccessType(ConnectedNode.GetID(), ConnectedNodeOutput.VertexID, &Graph.PageID);
							if (!FMetasoundFrontendClassVertex::CanConnectVertexAccessTypes(ConnectedAccessType, AccessType))
							{
								RemoveEdgeToNodeInput(Edge.ToNodeID, Edge.ToVertexID, &Graph.PageID);
							}
						}
					}, Graph.PageID);
				}
			});
		}

		const bool bNodeConformed = ConformGraphOutputNodeToClass(GraphOutput);
		if (!bNodeConformed)
		{
			return false;
		}

#if WITH_EDITORONLY_DATA
		Document.Metadata.ModifyContext.AddMemberIDModified(GraphOutput.NodeID);
#endif // WITH_EDITORONLY_DATA
	}

	return true;
}

bool FMetaSoundFrontendDocumentBuilder::SetGraphOutputDataType(FName OutputName, FName DataType)
{
	using namespace Metasound::Frontend;

	if (!IDataTypeRegistry::Get().IsRegistered(DataType))
	{
		return false;
	}

	const int32* Index = DocumentCache->GetInterfaceCache().FindOutputIndex(OutputName);
	if (!Index)
	{
		return false;
	}

	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	FMetasoundFrontendGraphClass& RootGraph = Document.RootGraph;
	FMetasoundFrontendClassOutput& GraphOutput = RootGraph.Interface.Outputs[*Index];
	if (GraphOutput.TypeName != DataType)
	{
		GraphOutput.TypeName = DataType;

		RootGraph.IterateGraphPages([this, &GraphOutput, &DataType](FMetasoundFrontendGraph& Graph)
		{
			const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(Graph.PageID);
			if (const int32* NodeIndex = NodeCache.FindNodeIndex(GraphOutput.NodeID))
			{
				FMetasoundFrontendNode& Node = Graph.Nodes[*NodeIndex];

				FMetasoundFrontendLiteral DefaultLiteral;
				DefaultLiteral.SetFromLiteral(IDataTypeRegistry::Get().CreateDefaultLiteral(DataType));
				FMetasoundFrontendVertex& NodeInput = Node.Interface.Inputs.Last();
				Node.InputLiterals = { FMetasoundFrontendVertexLiteral { NodeInput.VertexID, MoveTemp(DefaultLiteral) } };

				RemoveEdgeToNodeInput(GraphOutput.NodeID, NodeInput.VertexID);
				GraphOutput.TypeName = DataType;
			}
		});

		const bool bNodeConformed = ConformGraphOutputNodeToClass(GraphOutput);
		if (!bNodeConformed)
		{
			return false;
		}

#if WITH_EDITORONLY_DATA
		ClearMemberMetadata(GraphOutput.NodeID);
		Document.Metadata.ModifyContext.AddMemberIDModified(GraphOutput.NodeID);
#endif // WITH_EDITORONLY_DATA
	}

	return true;
}

bool FMetaSoundFrontendDocumentBuilder::SetGraphVariableDefault(FName VariableName, FMetasoundFrontendLiteral InDefaultLiteral, const FGuid* InPageID)
{
	using namespace Metasound;

	const FGuid PageID = InPageID ? *InPageID : BuildPageID;
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	FMetasoundFrontendGraph& Graph = Document.RootGraph.FindGraphChecked(PageID);

	auto NameMatchesVariable = [&VariableName](const FMetasoundFrontendVariable& Variable) { return Variable.Name == VariableName; };
	if (FMetasoundFrontendVariable* Variable = Graph.Variables.FindByPredicate(NameMatchesVariable))
	{
		if (Frontend::IDataTypeRegistry::Get().IsLiteralTypeSupported(Variable->TypeName, InDefaultLiteral.GetType()))
		{
			Variable->Literal = MoveTemp(InDefaultLiteral);
			return true;
		}
	}

	return false;
}

#if WITH_EDITOR
void FMetaSoundFrontendDocumentBuilder::SetDisplayName(const FText& InDisplayName)
{
	DocumentInterface->GetDocument().RootGraph.Metadata.SetDisplayName(InDisplayName);
}

void FMetaSoundFrontendDocumentBuilder::SetMemberMetadata(UMetaSoundFrontendMemberMetadata& NewMetadata)
{
	check(NewMetadata.MemberID.IsValid());

	TMap<FGuid, TObjectPtr<UMetaSoundFrontendMemberMetadata>>& LiteralMetadata = GetDocumentChecked().Metadata.MemberMetadata;
	LiteralMetadata.Remove(NewMetadata.MemberID);
	LiteralMetadata.Add(NewMetadata.MemberID, NewMetadata);
}

bool FMetaSoundFrontendDocumentBuilder::SetNodeComment(const FGuid& InNodeID, FString&& InNewComment, const FGuid* InPageID)
{
	if (FMetasoundFrontendNode* Node = FindNodeInternal(InNodeID, InPageID))
	{
		Node->Style.Display.Comment = MoveTemp(InNewComment);
		return true;
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::SetNodeCommentVisible(const FGuid& InNodeID, bool bIsVisible, const FGuid* InPageID)
{
	if (FMetasoundFrontendNode* Node = FindNodeInternal(InNodeID, InPageID))
	{
		Node->Style.Display.bCommentVisible = bIsVisible;
		return true;
	}

	return false;
}
#endif // WITH_EDITOR

bool FMetaSoundFrontendDocumentBuilder::SetNodeInputDefault(const FGuid& InNodeID, const FGuid& InVertexID, const FMetasoundFrontendLiteral& InLiteral, const FGuid* InPageID)
{
	using namespace Metasound::Frontend;

	const FGuid& PageID = InPageID ? *InPageID : BuildPageID;
	FMetasoundFrontendGraph& Graph = GetDocumentChecked().RootGraph.FindGraphChecked(PageID);
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(PageID);
	if (const int32* NodeIndex = NodeCache.FindNodeIndex(InNodeID))
	{
		FMetasoundFrontendNode& Node = Graph.Nodes[*NodeIndex];

		auto IsVertex = [&InVertexID](const FMetasoundFrontendVertex& Vertex) { return Vertex.VertexID == InVertexID; };
		int32 VertexIndex = Node.Interface.Inputs.IndexOfByPredicate(IsVertex);
		if (VertexIndex != INDEX_NONE)
		{
			FMetasoundFrontendVertexLiteral NewVertexLiteral;
			NewVertexLiteral.VertexID = InVertexID;
			NewVertexLiteral.Value = InLiteral;

			auto IsLiteral = [&InVertexID](const FMetasoundFrontendVertexLiteral& Literal) { return Literal.VertexID == InVertexID; };
			int32 LiteralIndex = Node.InputLiterals.IndexOfByPredicate(IsLiteral);
			if (LiteralIndex == INDEX_NONE)
			{
				LiteralIndex = Node.InputLiterals.Num();
				Node.InputLiterals.Add(MoveTemp(NewVertexLiteral));
			}
			else
			{
				Node.InputLiterals[LiteralIndex] = MoveTemp(NewVertexLiteral);
			}

			FNodeModifyDelegates& NodeDelegates = DocumentDelegates->FindNodeDelegatesChecked(PageID);
			const FOnMetaSoundFrontendDocumentMutateNodeInputLiteralArray& OnNodeInputLiteralSet = NodeDelegates.OnNodeInputLiteralSet;
			OnNodeInputLiteralSet.Broadcast(*NodeIndex, VertexIndex, LiteralIndex);
			return true;
		}
	}

	return false;
}

#if WITH_EDITOR
bool FMetaSoundFrontendDocumentBuilder::SetNodeLocation(const FGuid& InNodeID, const FVector2D& InLocation, const FGuid* InLocationGuid, const FGuid* InPageID)
{
	if (FMetasoundFrontendNode* Node = FindNodeInternal(InNodeID, InPageID))
	{
		FMetasoundFrontendNodeStyle& Style = Node->Style;
		if (InLocationGuid)
		{
			if (InLocationGuid->IsValid())
			{
				Style.Display.Locations.FindOrAdd(*InLocationGuid) = InLocation;
				return true;
			}

			UE_LOG(LogMetaSound, Display, TEXT("Invalid Location Guid no longer supported, reseting display location for node with ID '%s'"), *InNodeID.ToString());
		}

		if (Style.Display.Locations.IsEmpty())
		{
			Style.Display.Locations = { { FGuid::NewGuid(), InLocation } };
		}
		else
		{
			Algo::ForEach(Style.Display.Locations, [InLocation](TPair<FGuid, FVector2D>& Pair)
			{
				Pair.Value = InLocation;
			});
		}

		return true;
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::SetNodeUnconnectedPinsHidden(const FGuid& InNodeID, const bool bUnconnectedPinsHidden, const FGuid* InPageID)
{
	if (FMetasoundFrontendNode* Node = FindNodeInternal(InNodeID, InPageID))
	{
		Node->Style.bUnconnectedPinsHidden = bUnconnectedPinsHidden;
		return true;
	}

	return false;
}

const FMetasoundFrontendNodeStyle* FMetaSoundFrontendDocumentBuilder::GetNodeStyle(const FGuid& InNodeID, const FGuid* InPageID)
{
	if (const FMetasoundFrontendNode* Node = FindNodeInternal(InNodeID, InPageID))
	{
		return &Node->Style;
	}

	return nullptr;
}

#endif // WITH_EDITOR

void FMetaSoundFrontendDocumentBuilder::SetVersionNumber(const FMetasoundFrontendVersionNumber& InDocumentVersionNumber)
{
	GetDocumentChecked().Metadata.Version.Number = InDocumentVersionNumber;
}

bool FMetaSoundFrontendDocumentBuilder::SpliceVariableNodeFromStack(const FGuid& InNodeID, const FGuid& InPageID)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	FMetasoundFrontendGraph& Graph = Document.RootGraph.FindGraphChecked(InPageID);
	const IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache(InPageID);
	FMetasoundFrontendVertexHandle FromVariableVertexHandle;
	{
		const FMetasoundFrontendVertex* InputVertex = FindNodeInput(InNodeID, VariableNames::InputVariableName, &InPageID);
		check(InputVertex);
		if (const int32* InputEdgeIndex = EdgeCache.FindEdgeIndexToNodeInput(InNodeID, InputVertex->VertexID))
		{
			FromVariableVertexHandle = Graph.Edges[*InputEdgeIndex].GetFromVertexHandle();
			const bool bRemovedEdge = RemoveEdgeToNodeInput(InNodeID, InputVertex->VertexID, &InPageID);
			check(bRemovedEdge);
		}
	}

	if (FromVariableVertexHandle.IsSet())
	{
		if (const FMetasoundFrontendVertex* OutputVertex = FindNodeOutput(InNodeID, VariableNames::OutputVariableName, &InPageID))
		{
			TArray<FMetasoundFrontendVertexHandle> ToVertexHandles;
			const TArrayView<const int32> OutputEdgeIndices = EdgeCache.FindEdgeIndicesFromNodeOutput(InNodeID, OutputVertex->VertexID);
			Algo::Transform(OutputEdgeIndices, ToVertexHandles, [&Graph](const int32& VertIndex) { return Graph.Edges[VertIndex].GetToVertexHandle(); });

			RemoveEdgesFromNodeOutput(InNodeID, OutputVertex->VertexID, &InPageID);

			for (const FMetasoundFrontendVertexHandle& ToHandle : ToVertexHandles)
			{
				AddEdge(FMetasoundFrontendEdge
				{
					FromVariableVertexHandle.NodeID,
					FromVariableVertexHandle.VertexID,
					ToHandle.NodeID,
					ToHandle.VertexID
				}, &InPageID);
			}
			return true;
		}
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::SwapGraphInput(const FMetasoundFrontendClassVertex& InExistingInputVertex, const FMetasoundFrontendClassVertex& InNewInputVertex)
{
	using namespace Metasound::Frontend;

	// 1. Check if equivalent and early out if functionally do not match
	{
		const FMetasoundFrontendClassInput* ClassInput = FindGraphInput(InExistingInputVertex.Name);
		if (!ClassInput || !FMetasoundFrontendVertex::IsFunctionalEquivalent(*ClassInput, InExistingInputVertex))
		{
			return false;
		}
	}

	const IDocumentGraphInterfaceCache& InterfaceCache = DocumentCache->GetInterfaceCache();

#if WITH_EDITOR
	using FPageNodeLocations = TMap<FGuid, FVector2D>;
	TMap<FGuid, FPageNodeLocations> PageNodeLocations;
#endif // WITH_EDITOR

	// 2. Gather data from existing member/node needed to swap
	TMultiMap<FGuid, FMetasoundFrontendEdge> RemovedEdgesPerPage;

	const FMetasoundFrontendClassInput* ExistingInputClass = InterfaceCache.FindInput(InExistingInputVertex.Name);
	checkf(ExistingInputClass, TEXT("'SwapGraphInput' failed to find original graph input"));
	const FGuid NodeID = ExistingInputClass->NodeID;

	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	FMetasoundFrontendGraphClass& RootGraph = Document.RootGraph;
	Document.RootGraph.IterateGraphPages([&](FMetasoundFrontendGraph& Graph)
	{
		const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(Graph.PageID);
		const FMetasoundFrontendNode* ExistingInputNode = NodeCache.FindNode(NodeID);
		check(ExistingInputNode);

#if WITH_EDITOR
		PageNodeLocations.Add(Graph.PageID, ExistingInputNode->Style.Display.Locations);
#endif // WITH_EDITOR

		const FGuid VertexID = ExistingInputNode->Interface.Outputs.Last().VertexID;
		TArray<const FMetasoundFrontendEdge*> Edges = DocumentCache->GetEdgeCache(Graph.PageID).FindEdges(NodeID, VertexID);
		Algo::Transform(Edges, RemovedEdgesPerPage, [PageID = Graph.PageID](const FMetasoundFrontendEdge* Edge)
		{
			return TPair<FGuid, FMetasoundFrontendEdge>(PageID, *Edge);
		});
	});

	// 3. Remove existing graph vertex
	{
		const bool bRemovedVertex = RemoveGraphInput(InExistingInputVertex.Name);
		checkf(bRemovedVertex, TEXT("Failed to swap MetaSound input expected to exist"));
	}

	// 4. Add new graph vertex
	FMetasoundFrontendClassInput NewInput = InNewInputVertex;
	NewInput.NodeID = NodeID;
#if WITH_EDITOR
	NewInput.Metadata.SetSerializeText(InExistingInputVertex.Metadata.GetSerializeText());
#endif // WITH_EDITOR

	const FMetasoundFrontendNode* NewInputNode = AddGraphInput(NewInput);
	checkf(NewInputNode, TEXT("Failed to add new Input node when swapping graph inputs"));
	checkf(NewInputNode->GetID() == NewInput.NodeID, TEXT("Expected new node added to build graph to have same ID as provided input"));

	Document.RootGraph.IterateGraphPages([&](FMetasoundFrontendGraph& Graph)
	{
#if WITH_EDITOR
		// 5a. Add to new copy existing node locations
		if (const FPageNodeLocations* Locations = PageNodeLocations.Find(Graph.PageID))
		{
			const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(Graph.PageID);
			const int32* NodeIndex = NodeCache.FindNodeIndex(NodeID);
			checkf(NodeIndex, TEXT("Cache was not updated to reflect newly added input node"));
			FMetasoundFrontendNode& NewNode = Graph.Nodes[*NodeIndex];
			NewNode.Style.Display.Locations = *Locations;
		}
#endif // WITH_EDITOR

		// 5b. Add to new copy existing node edges
		TArray<FMetasoundFrontendEdge> RemovedEdges;
		RemovedEdgesPerPage.MultiFind(Graph.PageID, RemovedEdges);
		for (const FMetasoundFrontendEdge& RemovedEdge : RemovedEdges)
		{
			FMetasoundFrontendEdge NewEdge = RemovedEdge;
			NewEdge.FromNodeID = NewInputNode->GetID();
			NewEdge.FromVertexID = NewInputNode->Interface.Outputs.Last().VertexID;
			AddEdge(MoveTemp(NewEdge), &Graph.PageID);
		}
	});

	return true;
}

bool FMetaSoundFrontendDocumentBuilder::SwapGraphOutput(const FMetasoundFrontendClassVertex& InExistingOutputVertex, const FMetasoundFrontendClassVertex& InNewOutputVertex)
{
	using namespace Metasound::Frontend;

	// 1. Check if equivalent and early out if functionally do not match
	{
		const FMetasoundFrontendClassOutput* ClassOutput = FindGraphOutput(InExistingOutputVertex.Name);
		if (!ClassOutput || !FMetasoundFrontendVertex::IsFunctionalEquivalent(*ClassOutput, InExistingOutputVertex))
		{
			return false;
		}
	}

	const IDocumentGraphInterfaceCache& InterfaceCache = DocumentCache->GetInterfaceCache();

#if WITH_EDITOR
	using FPageNodeLocations = TMap<FGuid, FVector2D>;
	TMap<FGuid, FPageNodeLocations> PageNodeLocations;
#endif // WITH_EDITOR

	// 2. Gather data from existing page member/node needed to swap
	TMultiMap<FGuid, FMetasoundFrontendEdge> RemovedEdgesPerPage;

	const FMetasoundFrontendClassOutput* ExistingOutputClass = InterfaceCache.FindOutput(InExistingOutputVertex.Name);
	checkf(ExistingOutputClass, TEXT("'SwapGraphOutput' failed to find original graph output"));
	const FGuid NodeID = ExistingOutputClass->NodeID;

	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	FMetasoundFrontendGraphClass& RootGraph = Document.RootGraph;
	Document.RootGraph.IterateGraphPages([&](FMetasoundFrontendGraph& Graph)
	{
		const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(Graph.PageID);
		const FMetasoundFrontendNode* ExistingOutputNode = NodeCache.FindNode(NodeID);
		check(ExistingOutputNode);

#if WITH_EDITOR
		PageNodeLocations.Add(Graph.PageID, ExistingOutputNode->Style.Display.Locations);
#endif // WITH_EDITOR

		const FGuid VertexID = ExistingOutputNode->Interface.Inputs.Last().VertexID;
		TArray<const FMetasoundFrontendEdge*> Edges = DocumentCache->GetEdgeCache(Graph.PageID).FindEdges(NodeID, VertexID);
		Algo::Transform(Edges, RemovedEdgesPerPage, [PageID = Graph.PageID](const FMetasoundFrontendEdge* Edge)
		{
			return TPair<FGuid, FMetasoundFrontendEdge>(PageID, *Edge);
		});
	});

	// 3. Remove existing graph vertex
	{
		const bool bRemovedVertex = RemoveGraphOutput(InExistingOutputVertex.Name);
		checkf(bRemovedVertex, TEXT("Failed to swap output expected to exist while swapping MetaSound outputs"));
	}
	
	// 4. Add new graph vertex
	FMetasoundFrontendClassOutput NewOutput = InNewOutputVertex;
	NewOutput.NodeID = NodeID;
#if WITH_EDITOR
	NewOutput.Metadata.SetSerializeText(InExistingOutputVertex.Metadata.GetSerializeText());
#endif // WITH_EDITOR

	const FMetasoundFrontendNode* NewOutputNode = AddGraphOutput(NewOutput);
	checkf(NewOutputNode, TEXT("Failed to add new output node when swapping graph outputs"));
	checkf(NewOutputNode->GetID() == NewOutput.NodeID, TEXT("Expected new node added to build graph to have same ID as provided output"));

	Document.RootGraph.IterateGraphPages([&](FMetasoundFrontendGraph& Graph)
	{
#if WITH_EDITOR
		// 5a. Add to new copy existing node locations
		if (const FPageNodeLocations* Locations = PageNodeLocations.Find(Graph.PageID))
		{
			const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache(Graph.PageID);
			const int32* NodeIndex = NodeCache.FindNodeIndex(NodeID);
			checkf(NodeIndex, TEXT("Cache was not updated to reflect newly added output node"));
			FMetasoundFrontendNode& NewNode = Graph.Nodes[*NodeIndex];
			NewNode.Style.Display.Locations = *Locations;
		}
#endif // WITH_EDITOR

		// 5b. Add to new copy existing node edges
		TArray<FMetasoundFrontendEdge> RemovedEdges;
		RemovedEdgesPerPage.MultiFind(Graph.PageID, RemovedEdges);
		for (const FMetasoundFrontendEdge& RemovedEdge : RemovedEdges)
		{
			FMetasoundFrontendEdge NewEdge = RemovedEdge;
			NewEdge.ToNodeID = NewOutputNode->GetID();
			NewEdge.ToVertexID = NewOutputNode->Interface.Inputs.Last().VertexID;
			AddEdge(MoveTemp(NewEdge), &Graph.PageID);
		}
	});

	return true;
}

bool FMetaSoundFrontendDocumentBuilder::UnlinkVariableNode(const FGuid& InNodeID, const FGuid& InPageID)
{
	auto IsNodeID = [&InNodeID](const FGuid& TestID) { return TestID == InNodeID; };

	FMetasoundFrontendGraph& Graph = GetDocumentChecked().RootGraph.FindGraphChecked(InPageID);
	for (FMetasoundFrontendVariable& Variable : Graph.Variables)
	{
		if (Variable.MutatorNodeID == InNodeID)
		{
			Variable.MutatorNodeID = FGuid();
			SpliceVariableNodeFromStack(InNodeID, InPageID);
			return true;
		}

		if (Variable.VariableNodeID == InNodeID)
		{
			Variable.VariableNodeID = FGuid();
			SpliceVariableNodeFromStack(InNodeID, InPageID);
			return true;
		}

		// Removal must maintain array order to preserve head/tail positions in stack
		const bool bRemovedDeferredNode = Variable.DeferredAccessorNodeIDs.RemoveAll(IsNodeID) > 0;
		if (bRemovedDeferredNode)
		{
			SpliceVariableNodeFromStack(InNodeID, InPageID);
			return true;
		}
		
		// Removal must maintain array order to preserve head/tail positions in stack
		const bool bRemovedAccessorNode = Variable.AccessorNodeIDs.RemoveAll(IsNodeID) > 0;
		if (bRemovedAccessorNode)
		{
			SpliceVariableNodeFromStack(InNodeID, InPageID);
			return true;
		}
	}

	return false;
}

#if WITH_EDITOR
bool FMetaSoundFrontendDocumentBuilder::UpdateDependencyRegistryData(const TMap<Metasound::Frontend::FNodeRegistryKey, Metasound::Frontend::FNodeRegistryKey>& OldToNewClassKeys)
{
	using namespace Metasound::Frontend;

	bool bUpdated = false;
	if (DocumentDelegates.IsValid())
	{
		FMetasoundFrontendDocument& Document = GetDocumentChecked();
		for (FMetasoundFrontendClass& Dependency : Document.Dependencies)
		{
			const FNodeRegistryKey OldKey(Dependency.Metadata);
			if (const FNodeRegistryKey* NewKey = OldToNewClassKeys.Find(OldKey))
			{
				if (Dependency.Metadata.GetType() == EMetasoundFrontendClassType::External)
				{
					bUpdated = true;
					const int32* DependencyIndex = DocumentCache->FindDependencyIndex(Dependency.ID);
					check(DependencyIndex);
					DocumentDelegates->OnRenamingDependencyClass.Broadcast(*DependencyIndex, NewKey->ClassName);
					Dependency.Metadata.SetType(NewKey->Type);
					Dependency.Metadata.SetClassName(NewKey->ClassName);
					Dependency.Metadata.SetVersion(NewKey->Version);
				}
			}
		}

#if WITH_EDITORONLY_DATA
		if (bUpdated)
		{
			Document.Metadata.ModifyContext.SetDocumentModified();
		}
#endif // WITH_EDITORONLY_DATA
	}

	return bUpdated;
}

bool FMetaSoundFrontendDocumentBuilder::UpdateDependencyClassNames(const TMap<FMetasoundFrontendClassName, FMetasoundFrontendClassName>& OldToNewReferencedClassNames)
{
	using namespace Metasound::Frontend;

	TMap<FNodeRegistryKey, FNodeRegistryKey> OldToNewKeys;
	Algo::Transform(OldToNewReferencedClassNames, OldToNewKeys, [](const TPair<FMetasoundFrontendClassName, FMetasoundFrontendClassName>& ClassNamePair)
	{
		return TPair<FNodeRegistryKey, FNodeRegistryKey>(
			FNodeRegistryKey(EMetasoundFrontendClassType::External, ClassNamePair.Key, FMetasoundFrontendVersionNumber()),
			FNodeRegistryKey(EMetasoundFrontendClassType::External, ClassNamePair.Value, FMetasoundFrontendVersionNumber())
		);
	});
	return UpdateDependencyRegistryData(OldToNewKeys);
}
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
bool FMetaSoundFrontendDocumentBuilder::VersionInterfaces()
{
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	if (Document.RequiresInterfaceVersioning())
	{
		Document.VersionInterfaces();
		return true;
	}

	return false;
}

FMetasoundFrontendDocument& FMetaSoundFrontendDocumentBuilder::IPropertyVersionTransform::GetDocumentUnsafe(const FMetaSoundFrontendDocumentBuilder& Builder)
{
	return Builder.GetDocumentChecked();
}
#endif // WITH_EDITORONLY_DATA
