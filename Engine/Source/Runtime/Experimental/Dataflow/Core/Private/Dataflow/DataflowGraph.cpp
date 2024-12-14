// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowGraph.h"

#include "Dataflow/DataflowInputOutput.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "Logging/LogMacros.h"
#include "Dataflow/DataflowArchive.h"
#include "UObject/UE5MainStreamObjectVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"

DEFINE_LOG_CATEGORY_STATIC(DATAFLOW_LOG, Error, All);

namespace UE::Dataflow
{
	TSet<FName> FGraph::RegisteredFilters = {};

	FGraph::FGraph(FGuid InGuid)
		: Guid(InGuid)
	{
	}

	void FGraph::Reset()
	{
		Nodes.Reset();
		FilteredNodes.Reset();
		Connections.Reset();
		DisabledNodes.Reset();
	}

	void FGraph::RemoveNode(TSharedPtr<FDataflowNode> Node)
	{
		for (FDataflowOutput* Output : Node->GetOutputs())
		{
			if (Output)
			{
				for (FDataflowInput* Input : Output->GetConnectedInputs())
				{
					if (Input)
					{
						Disconnect(Output, Input);
					}
				}
			}
		}
		for (FDataflowInput* Input : Node->GetInputs())
		{
			if (Input)
			{
				TArray<FDataflowOutput*> Outputs = Input->GetConnectedOutputs();
				for (FDataflowOutput* Output : Outputs)
				{
					if (Output)
					{
						Disconnect(Output, Input);
					}
				}
			}
		}
		Nodes.Remove(Node);
		for(const FName& RegisteredType : RegisteredFilters)
		{
			if (Node->IsA(RegisteredType))
			{
				if(TArray< TSharedPtr<FDataflowNode> >* FoundNodes = FilteredNodes.Find(RegisteredType))
				{
					FoundNodes->Remove(Node);
				}
			}
		}
	}

	void FGraph::ClearConnections(FDataflowConnection* Connection)
	{
		// Todo(dataflow) : do this without triggering a invalidation. 
		//            or implement a better sync for the EdGraph and DataflowGraph
		if (Connection->GetDirection() == FPin::EDirection::INPUT)
		{
			FDataflowInput* ConnectionIn = static_cast<FDataflowInput*>(Connection);
			TArray<FDataflowOutput*> BaseOutputs = ConnectionIn->GetConnectedOutputs();
			for (FDataflowOutput* Output : BaseOutputs)
			{
				Disconnect(Output, ConnectionIn);
			}
		}
		else if (Connection->GetDirection() == FPin::EDirection::OUTPUT)
		{
			FDataflowOutput* ConnectionOut = static_cast<FDataflowOutput*>(Connection);
			TArray<FDataflowInput*> BaseInputs = ConnectionOut->GetConnectedInputs();
			for (FDataflowInput* Input : BaseInputs)
			{
				Disconnect(ConnectionOut, Input);
			}
		}
	}

	void FGraph::ClearConnections(FDataflowInput* InConnection)
	{
		for (FDataflowOutput* Output : InConnection->GetConnectedOutputs())
		{
			Disconnect(Output, InConnection);
		}
	}

	void FGraph::ClearConnections(FDataflowOutput* OutConnection)
	{
		for (FDataflowInput* Input : OutConnection->GetConnectedInputs())
		{
			Disconnect(OutConnection, Input);
		}
	}


	void FGraph::Connect(FDataflowOutput* OutputConnection, FDataflowInput* InputConnection)
	{
		if (ensure(OutputConnection && InputConnection))
		{
			FDataflowOutput* const OldOutputConnection = InputConnection->GetConnection();
			if (OldOutputConnection != OutputConnection)
			{
				if (OldOutputConnection)
				{
					UE_LOG(LogChaosDataflow, Verbose, TEXT("FGraph::Connect(): Disconnecting output [%s:%s] from input [%s:%s]"),
						OldOutputConnection && OldOutputConnection->GetOwningNode() ? *OldOutputConnection->GetOwningNode()->GetName().ToString() : TEXT("Invalid"),
						OldOutputConnection ? *OldOutputConnection->GetName().ToString() : TEXT("Invalid"),
						InputConnection->GetOwningNode() ? *InputConnection->GetOwningNode()->GetName().ToString() : TEXT("Invalid"),
						*InputConnection->GetName().ToString());
					// Note: Do not remove the expired connection from the input to avoid an unnecessary invalidation.
					//       Simply clobber it with calling AddConnection() on the input instead.
					OldOutputConnection->RemoveConnection(InputConnection);
					Connections.RemoveSwap(FLink(
						OldOutputConnection->GetOwningNode()->GetGuid(), OldOutputConnection->GetGuid(),
						InputConnection->GetOwningNode()->GetGuid(), InputConnection->GetGuid()));
				}
				UE_LOG(LogChaosDataflow, Verbose, TEXT("FGraph::Connect(): Connecting output [%s:%s] to input [%s:%s]"),
					OutputConnection->GetOwningNode() ? *OutputConnection->GetOwningNode()->GetName().ToString() : TEXT("Invalid"),
					*OutputConnection->GetName().ToString(),
					InputConnection->GetOwningNode() ? *InputConnection->GetOwningNode()->GetName().ToString() : TEXT("Invalid"),
					*InputConnection->GetName().ToString());
				OutputConnection->AddConnection(InputConnection);
				InputConnection->AddConnection(OutputConnection);
				Connections.Add(FLink(
					OutputConnection->GetOwningNode()->GetGuid(), OutputConnection->GetGuid(),
					InputConnection->GetOwningNode()->GetGuid(), InputConnection->GetGuid()));
			}
		}
	}

	void FGraph::Disconnect(FDataflowOutput* OutputConnection, FDataflowInput* InputConnection)
	{
		UE_LOG(LogChaosDataflow, Verbose, TEXT("FGraph::Disconnect(): Disconnecting output [%s:%s] from input [%s:%s]"),
			OutputConnection->GetOwningNode() ? *OutputConnection->GetOwningNode()->GetName().ToString() : TEXT("Invalid"),
			*OutputConnection->GetName().ToString(),
			InputConnection->GetOwningNode() ? *InputConnection->GetOwningNode()->GetName().ToString() : TEXT("Invalid"),
			*InputConnection->GetName().ToString());
		OutputConnection->RemoveConnection(InputConnection);
		InputConnection->RemoveConnection(OutputConnection);
		Connections.RemoveSwap(FLink(
			OutputConnection->GetOwningNode()->GetGuid(), OutputConnection->GetGuid(),
			InputConnection->GetOwningNode()->GetGuid(), InputConnection->GetGuid()));
	}

	void FGraph::AddReferencedObjects(FReferenceCollector& Collector)
	{
		for (TSharedPtr<FDataflowNode>& Node : Nodes)
		{
			Collector.AddPropertyReferencesWithStructARO(Node->TypedScriptStruct(), Node.Get());
		}
	}

	void FGraph::Serialize(FArchive& Ar, UObject* OwningObject)
	{
		Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);
		Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);

		Ar << Guid;
		if (Ar.IsSaving())
		{
			FGraph::SerializeForSaving(Ar, this, Nodes, Connections);
		}
		else if( Ar.IsLoading())
		{
			FGraph::SerializeForLoading(Ar, this, OwningObject);
		}
	}

	void FGraph::SerializeForSaving(FArchive& Ar, FGraph* InGraph, TArray<TSharedPtr<FDataflowNode>>& InNodes, TArray<FLink>& InConnections)
	{
		FGuid ArGuid;
		FName ArType, ArName;
		int32 ArNum = InNodes.Num();

		Ar << ArNum;
		for (TSharedPtr<FDataflowNode> Node : InNodes)
		{
			ArGuid = Node->GetGuid();
			ArType = Node->GetType();
			ArName = Node->GetName();
			Ar << ArGuid << ArType << ArName;

			DATAFLOW_OPTIONAL_BLOCK_WRITE_BEGIN()
			{
				// Node needs to be serialized first to make sure it registers all the dynamic input/output for when input and output will be deserialized
				Node->SerializeInternal(Ar);

				// keep outputs and inputs separated even though their serialization code looks almost identical
				// this is to make sure we can handle when number of inputs or outputs have changed on the node
				int32 ArNumOutputs = Node->GetOutputs().Num();
				Ar << ArNumOutputs;
				for (FDataflowConnection* Output : Node->GetOutputs())
				{
					ArGuid = Output->GetGuid();
					ArType = Output->GetType();
					ArName = Output->GetName();
					Ar << ArGuid << ArType << ArName;

					bool bIsAnytype = Output->IsAnyType();
					Ar << bIsAnytype;
					bool bIsHidden = Output->GetPinIsHidden();
					Ar << bIsHidden;
				}

				int32 ArNumInputs = Node->GetInputs().Num();
				Ar << ArNumInputs;
				for (FDataflowConnection* Input : Node->GetInputs())
				{
					ArGuid = Input->GetGuid();
					ArType = Input->GetType();
					ArName = Input->GetName();
					Ar << ArGuid << ArType << ArName;

					bool bIsAnytype = Input->IsAnyType();
					Ar << bIsAnytype;
					bool bIsHidden = Input->GetPinIsHidden();
					Ar << bIsHidden;
				}
			}
			DATAFLOW_OPTIONAL_BLOCK_WRITE_END();
		}

		Ar << InConnections;
	}

	void FGraph::SerializeForLoading(FArchive& Ar, FGraph* InGraph, UObject* OwningObject)
	{
		InGraph->Reset();

		const bool bDataflowSeparateInputOutputSerialization = (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::DataflowSeparateInputOutputSerialization);
		const bool bDataflowAnyTypeSupport = (Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::DataflowAnyTypeSupport);
		const bool bDataflowTemplateTypeFix = (Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::DataflowTemplatedTypeFix);

		FGuid ArGuid;
		FName ArType, ArName;
		int32 ArNum = 0;

		TMap<FGuid, TSharedPtr<FDataflowNode> > NodeGuidMap;
		TMap<FGuid, FDataflowConnection* > ConnectionGuidMap;
		TArray<FDataflowConnection*> ConnectionsToFix;

		// returns true if the connection is to be fixed
		auto AddTemplateTypedConnectionToBeFixed = [&ConnectionsToFix, bDataflowTemplateTypeFix](FDataflowConnection* Connection, FName SerializedType) -> bool
			{
				if (Connection && !bDataflowTemplateTypeFix)
				{
					const bool bSametype = (Connection->GetType() == SerializedType);
					const bool bIsOldTemplatedType = !bSametype && Connection->GetType().ToString().StartsWith(SerializedType.ToString());
					if (bIsOldTemplatedType)
					{
						Connection->ForceSimpleType(SerializedType);
						ConnectionsToFix.Add(Connection);
						return true;
					}
				}
				return false;
			};

		Ar << ArNum;
		for (int32 Ndx = ArNum; Ndx > 0; Ndx--)
		{
			FName ArNodeName;
			Ar << ArGuid << ArType << ArNodeName;

			TSharedPtr<FDataflowNode> Node = FNodeFactory::GetInstance()->NewNodeFromRegisteredType(*InGraph, { ArGuid, ArType, ArNodeName, OwningObject });
			DATAFLOW_OPTIONAL_BLOCK_READ_BEGIN(Node != nullptr)
			{
				ensure(!NodeGuidMap.Contains(ArGuid));
				NodeGuidMap.Add(ArGuid, Node);

				const bool bDataflowHideablePinSupport = (Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::DataflowHideablePins);

				if (!bDataflowSeparateInputOutputSerialization)
				{

					// former input / output serialization method where we only store aggregate number of inputs and outputs
					// this has limitation when adding more outputs or inputs
					int ArNumInputsOutputs;
					Ar << ArNumInputsOutputs;
					TArray< FDataflowConnection* > InputsOutputs;
					InputsOutputs.Append(Node->GetOutputs());
					InputsOutputs.Append(Node->GetInputs());

					// skip offset is use to correct the mismatch of outputs have been added
					int SkipOffset = 0;
					for (int ConnectionIndex = 0; ConnectionIndex < ArNumInputsOutputs; ConnectionIndex++)
					{
						Ar << ArGuid << ArType << ArName;

						int AdjustedConnectionIndex = ConnectionIndex + SkipOffset;
						if (InputsOutputs.IsValidIndex(AdjustedConnectionIndex))
						{
							FDataflowConnection* Connection = InputsOutputs[AdjustedConnectionIndex];

							// if the name does not match this means the node has changed since the last serialization 
							// ( added outputs for example that shift the index )
							// in that case we try to recover by finding the next good node
							// note we cannot just find by name as some nodes have inputs and outputs named the same 
							// todo: implement a better way to serialize inputs and outputs seperately to avoid this case
							while (Connection && Connection->GetName() != ArName)
							{
								SkipOffset++;
								AdjustedConnectionIndex = ConnectionIndex + SkipOffset;
								if (InputsOutputs.IsValidIndex(AdjustedConnectionIndex))
								{
									Connection = InputsOutputs[AdjustedConnectionIndex];
								}
								else
								{
									Connection = nullptr;
								}
							}
							if (Connection)
							{
								if (!AddTemplateTypedConnectionToBeFixed(Connection, ArType))
								{
									check(Connection->GetType() == ArType);
								}
								Connection->SetGuid(ArGuid);
								ensure(!ConnectionGuidMap.Contains(ArGuid));
								ConnectionGuidMap.Add(ArGuid, Connection);
							}
						}
					}

					Node->SerializeInternal(Ar);
				}
				else
				{
					// we need to deserilaize the node first because if it may add more inputs that may 
					// be referenced when deserializing them below ( see Dataflow Node AddPin method )
					Node->SerializeInternal(Ar);

					bool bIsAnyType = false;
					bool bIsHidden = true;
					// Outputs deserialization
					{
						int32 ArNumOutputs;
						Ar << ArNumOutputs;

						for (int32 OutputIndex = 0; OutputIndex < ArNumOutputs; OutputIndex++)
						{
							Ar << ArGuid << ArType << ArName;
							if (bDataflowAnyTypeSupport)
							{
								Ar << bIsAnyType;
							}
							if (bDataflowHideablePinSupport)
							{
								Ar << bIsHidden;
							}

							FDataflowOutput* Output = Node->FindOutput(ArName);
							if (!Output)
							{
								// Find out if the output has recently been redirected
								Output = Node->RedirectSerializedOutput(ArName);
								UE_CLOG(Output, LogChaos, Display, TEXT("Output (%s) has been redirected to output (%s) in Dataflow node (%s).")
									, *ArName.ToString(), *Output->GetName().ToString(), *ArNodeName.ToString());
							}
							if (Output)
							{
								if (bIsAnyType)
								{
									Output->SetAsAnyType(bIsAnyType, ArType);
								}
								if (!AddTemplateTypedConnectionToBeFixed(Output, ArType))
								{
									check(Output->GetType() == ArType || bIsAnyType);
								}
								Output->SetPinIsHidden(bIsHidden);
								Output->SetGuid(ArGuid);
								ensure(!ConnectionGuidMap.Contains(ArGuid));
								ConnectionGuidMap.Add(ArGuid, Output);
							}
							else
							{
								// output has been serialized but cannot be found
								// this means the definition of the node has changed and the output is no longer registered
								UE_LOG(LogChaos, Display, TEXT("Cannot find registered output (%s) in Dataflow node (%s) - this may result in missing connection(s).")
									, *ArName.ToString(), *ArNodeName.ToString());
							}
						}
					}

					// Inputs deserialization
					{
						int32 ArNumInputs;
						Ar << ArNumInputs;

						for (int32 InputIndex = 0; InputIndex < ArNumInputs; InputIndex++)
						{
							Ar << ArGuid << ArType << ArName;
							if (bDataflowAnyTypeSupport)
							{
								Ar << bIsAnyType;
							}
							if (bDataflowHideablePinSupport)
							{
								Ar << bIsHidden;
							}

							FDataflowInput* Input = Node->FindInput(ArName);
							if (!Input)
							{
								// Find out if the input has recently been redirected
								Input = Node->RedirectSerializedInput(ArName);
								UE_CLOG(Input, LogChaos, Display, TEXT("Input (%s) has been redirected to input (%s) in Dataflow node (%s).")
									, *ArName.ToString(), *Input->GetName().ToString(), *ArNodeName.ToString());
							}
							if (Input)
							{
								if (bIsAnyType)
								{
									Input->SetAsAnyType(bIsAnyType, ArType);
								}
								if (!AddTemplateTypedConnectionToBeFixed(Input, ArType))
								{
									check(Input->GetType() == ArType || bIsAnyType);
								}
								Input->SetPinIsHidden(bIsHidden);
								Input->SetGuid(ArGuid);
								ensure(!ConnectionGuidMap.Contains(ArGuid));
								ConnectionGuidMap.Add(ArGuid, Input);
							}
							else
							{
								// input has been serialized but cannot be found
								// this means the definition of the node has changed and the input is no longer registered
								UE_LOG(LogChaos, Display, TEXT("Cannot find registered input (%s) in Dataflow node (%s) - this may result in missing connection(s).")
									, *ArName.ToString(), *ArNodeName.ToString());
							}
						}
					}
				}
			}
			DATAFLOW_OPTIONAL_BLOCK_READ_ELSE()
			{
				InGraph->DisabledNodes.Add(ArNodeName);
				ensureMsgf(false,
					TEXT("Error: Missing registered node type (%s) will be removed from graph on load. Graph will fail to evaluate due to missing node (%s).")
					, *ArType.ToString(), *ArName.ToString());
			}
			DATAFLOW_OPTIONAL_BLOCK_READ_END();
		}

		TArray< FLink > LocalConnections;
		Ar << LocalConnections;
		for (const FLink& Con : LocalConnections)
		{
			if (NodeGuidMap.Contains(Con.InputNode) && NodeGuidMap.Contains(Con.OutputNode))
			{
				if (ConnectionGuidMap.Contains(Con.Input) && ConnectionGuidMap.Contains(Con.Output))
				{
					if (ConnectionGuidMap[Con.Output] && ConnectionGuidMap[Con.Output]->Direction == FPin::EDirection::OUTPUT &&
						ConnectionGuidMap[Con.Input] && ConnectionGuidMap[Con.Input]->Direction == FPin::EDirection::INPUT)
					{
						FDataflowOutput* Output = static_cast<FDataflowOutput*>(ConnectionGuidMap[Con.Output]);
						FDataflowInput* Input = static_cast<FDataflowInput*>(ConnectionGuidMap[Con.Input]);
						if (Input->GetType() == Output->GetType())
						{
							InGraph->Connect(Output, Input);
						}
					}
				}
			}
		}

		// fix templated types if any : see bDataflowTemplateTypeFix
		for (FDataflowConnection* ConnectionToFix : ConnectionsToFix)
		{
			ConnectionToFix->FixAndPropagateType();
		}
	}

	void RegisterNodeFilter(const FName& NodeFilter)
	{
		FGraph::RegisteredFilters.Add(NodeFilter);
	}
}

