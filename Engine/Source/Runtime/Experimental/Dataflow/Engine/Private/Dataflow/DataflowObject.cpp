// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowObject.h"
#include "Dataflow/DataflowCore.h"
#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowObjectInterface.h"
#if WITH_EDITOR
#include "EdGraph/EdGraphPin.h"
#endif
#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowObject)

#define LOCTEXT_NAMESPACE "UDataflow"

namespace UE::Dataflow::CVars
{
	/** Enable the simulation dataflow (for now WIP) */
	TAutoConsoleVariable<bool> CVarEnableSimulationDataflow(
			TEXT("p.Dataflow.EnableSimulation"),
			false,
			TEXT("If true enable the use of simulation dataflow (WIP)"),
			ECVF_Default);
}

FDataflowAssetEdit::FDataflowAssetEdit(UDataflow* InAsset, FPostEditFunctionCallback InCallback)
	: PostEditCallback(InCallback)
	, Asset(InAsset)
{
}

FDataflowAssetEdit::~FDataflowAssetEdit()
{
	PostEditCallback();
}

UE::Dataflow::FGraph* FDataflowAssetEdit::GetGraph()
{
	if (Asset)
	{
		return Asset->Dataflow.Get();
	}
	return nullptr;
}

UDataflow::UDataflow(const FObjectInitializer& ObjectInitializer)
	: UEdGraph(ObjectInitializer)
	, Dataflow(new UE::Dataflow::FGraph())
{}

void UDataflow::EvaluateTerminalNodeByName(FName NodeName, UObject* Asset)
{
	ensureAlwaysMsgf(false, TEXT("Deprecated use the dataflow blueprint library from now on"));
}

void UDataflow::PostEditCallback()
{
	// mark as dirty for the UObject
}

void UDataflow::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UDataflow* const This = CastChecked<UDataflow>(InThis);

	for(TObjectPtr<const UDataflowEdNode> Target : This->GetRenderTargets())
	{
		Collector.AddReferencedObject(Target);
	}

	This->Dataflow->AddReferencedObjects(Collector);
	Super::AddReferencedObjects(InThis, Collector);
}

#if WITH_EDITOR

void UDataflow::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
}

#endif

void UDataflow::PostLoad()
{
#if WITH_EDITOR
	const TSet<FName>& DisabledNodes = Dataflow->GetDisabledNodes();

	for (UEdGraphNode* EdNode : Nodes)
	{
		UDataflowEdNode* DataflowEdNode = Cast<UDataflowEdNode>(EdNode);

		// Not all nodes are UDataflowEdNode (There is now UDataflowEdNodeComment)
		if (DataflowEdNode)
		{
			DataflowEdNode->SetDataflowGraph(Dataflow);
			DataflowEdNode->UpdatePinsFromDataflowNode();
		}

		if (DisabledNodes.Contains(FName(EdNode->GetName())))
		{
			EdNode->SetEnabledState(ENodeEnabledState::Disabled);
		}
	}

	// Resync connections (nodes might have redirected connections
	for (const UE::Dataflow::FLink& Link : Dataflow->GetConnections())
	{
		TSharedPtr<const FDataflowNode> OutputNode = Dataflow->FindBaseNode(Link.OutputNode);
		TSharedPtr<const FDataflowNode> InputNode = Dataflow->FindBaseNode(Link.InputNode);
		if (ensure(OutputNode && InputNode))
		{
			const FDataflowOutput* const Output = OutputNode->FindOutput(Link.Output);
			const FDataflowInput* const Input = InputNode->FindInput(Link.Input);
			if (Output && Input)
			{
				TObjectPtr<UDataflowEdNode> OutputEdNode = FindEdNodeByDataflowNodeGuid(Link.OutputNode);
				TObjectPtr<UDataflowEdNode> InputEdNode = FindEdNodeByDataflowNodeGuid(Link.InputNode);

				if (ensure(OutputEdNode && InputEdNode))
				{
					UEdGraphPin* const OutputPin = OutputEdNode->FindPin(Output->GetName(), EEdGraphPinDirection::EGPD_Output);
					UEdGraphPin* const InputPin = InputEdNode->FindPin(Input->GetName(), EEdGraphPinDirection::EGPD_Input);

					if (ensure(OutputPin && InputPin))
					{
						if (OutputPin->LinkedTo.Find(InputPin) == INDEX_NONE)
						{
							OutputPin->MakeLinkTo(InputPin);
						}
					}
				}
			}
		}
	}
#endif

	LastModifiedRenderTarget = UE::Dataflow::FTimestamp::Current();
	UObject::PostLoad();
}

void UDataflow::AddRenderTarget(TObjectPtr<const UDataflowEdNode> InNode)
{
	LastModifiedRenderTarget = UE::Dataflow::FTimestamp::Current();
	check(InNode->ShouldRenderNode());
	RenderTargets.AddUnique(InNode);
}

void UDataflow::RemoveRenderTarget(TObjectPtr<const UDataflowEdNode> InNode)
{
	LastModifiedRenderTarget = UE::Dataflow::FTimestamp::Current();
	check(!InNode->ShouldRenderNode());
	RenderTargets.Remove(InNode);
}

void UDataflow::AddWireframeRenderTarget(TObjectPtr<const UDataflowEdNode> InNode)
{
	LastModifiedRenderTarget = UE::Dataflow::FTimestamp::Current();
	check(InNode->ShouldWireframeRenderNode());
	WireframeRenderTargets.AddUnique(InNode);
}

void UDataflow::RemoveWireframeRenderTarget(TObjectPtr<const UDataflowEdNode> InNode)
{
	LastModifiedRenderTarget = UE::Dataflow::FTimestamp::Current();
	check(!InNode->ShouldWireframeRenderNode());
	WireframeRenderTargets.Remove(InNode);
}


void UDataflow::Serialize(FArchive& Ar)
{
#if WITH_EDITOR
	// Disable per-node serialization (used for transactions, i.e., undo/redo) when serializing the whole graph.
	bEnablePerNodeTransactionSerialization = false;
#endif

	Super::Serialize(Ar);
	Dataflow->Serialize(Ar, this);

#if WITH_EDITOR
	bEnablePerNodeTransactionSerialization = true;
#endif
}

TObjectPtr<const UDataflowEdNode> UDataflow::FindEdNodeByDataflowNodeGuid(const FGuid& Guid) const
{
	for (const UEdGraphNode* const EdNode : Nodes)
	{
		if (const UDataflowEdNode* const DataflowEdNode = Cast<UDataflowEdNode>(EdNode))
		{
			if (DataflowEdNode->GetDataflowNodeGuid() == Guid)
			{
				return TObjectPtr<const UDataflowEdNode>(DataflowEdNode);
			}
		}
	}
	return TObjectPtr<const UDataflowEdNode>(nullptr);
}

TObjectPtr<UDataflowEdNode> UDataflow::FindEdNodeByDataflowNodeGuid(const FGuid& Guid)
{
	for (UEdGraphNode* const EdNode : Nodes)
	{
		if (UDataflowEdNode* const DataflowEdNode = Cast<UDataflowEdNode>(EdNode))
		{
			if (DataflowEdNode->GetDataflowNodeGuid() == Guid)
			{
				return TObjectPtr<UDataflowEdNode>(DataflowEdNode);
			}
		}
	}
	return TObjectPtr<UDataflowEdNode>(nullptr);
}

#if WITH_EDITOR
bool UDataflow::CanEditChange(const FProperty* InProperty) const
{
	if (!Super::CanEditChange(InProperty))
	{
		return false;
	}

	const FName& Name = InProperty->GetFName();

	if (Name == GET_MEMBER_NAME_CHECKED(ThisClass, Type))
	{
		return UE::Dataflow::CVars::CVarEnableSimulationDataflow.GetValueOnGameThread();
	}

	return true;
}
#endif

#undef LOCTEXT_NAMESPACE

