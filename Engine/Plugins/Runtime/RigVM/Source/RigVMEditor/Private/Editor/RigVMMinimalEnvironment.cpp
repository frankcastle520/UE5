// Copyright Epic Games, Inc. All Rights Reserved.

#include "Editor/RigVMMinimalEnvironment.h"
#include "RigVMBlueprint.h"
#include "Editor/RigVMEditorTools.h"

#define LOCTEXT_NAMESPACE "RigVMMinimalEnvironment"

FRigVMMinimalEnvironment::FRigVMMinimalEnvironment(const UClass* InRigVMBlueprintClass)
{
	ModelController = TStrongObjectPtr<URigVMController>(NewObject<URigVMController>());
	ModelGraph = TStrongObjectPtr<URigVMGraph>(NewObject<URigVMGraph>(ModelController.Get()));
	ModelController->SetGraph(ModelGraph.Get());

	EdGraphClass = URigVMEdGraph::StaticClass();
	EdGraphNodeClass = URigVMEdGraphNode::StaticClass();

	SetSchemata(InRigVMBlueprintClass ? InRigVMBlueprintClass : URigVMBlueprint::StaticClass());
}

URigVMGraph* FRigVMMinimalEnvironment::GetModel() const
{
	return ModelGraph.Get();
}

URigVMController* FRigVMMinimalEnvironment::GetController() const
{
	return ModelController.Get();
}

URigVMNode* FRigVMMinimalEnvironment::GetNode() const
{
	if(ModelNode.IsValid())
	{
		return ModelNode.Get();
	}
	return nullptr;
}

void FRigVMMinimalEnvironment::SetNode(URigVMNode* InModelNode)
{
	if(!ModelHandle.IsValid())
	{
		ModelHandle = ModelGraph->OnModified().AddSP(this, &FRigVMMinimalEnvironment::HandleModified);
	}
	if(URigVMNode* PreviousNode = GetNode())
	{
		GetController()->RemoveNode(PreviousNode);
	}
	
	ModelNode = InModelNode;

	if(GetEdGraphNode() == nullptr)
	{
		if(URigVMEdGraph* MyEdGraph = GetEdGraph())
		{
			EdGraphNode = NewObject<URigVMEdGraphNode>(MyEdGraph, EdGraphNodeClass);
			MyEdGraph->Nodes.Add(EdGraphNode.Get());
		}
	}

	HandleModified(ERigVMGraphNotifType::NodeAdded, GetModel(), GetNode());
}

void FRigVMMinimalEnvironment::SetFunctionNode(const FRigVMGraphFunctionIdentifier& InIdentifier)
{
	check(InIdentifier.IsValid());

	const FRigVMGraphFunctionHeader Header = FRigVMGraphFunctionHeader::FindGraphFunctionHeader(InIdentifier);
	if(Header.IsValid())
	{
		const FAssetData AssetData = UE::RigVM::Editor::Tools::FindAssetFromAnyPath(Header.LibraryPointer.GetLibraryNodePath(), true);
		if(AssetData.IsValid())
		{
			if(const UClass* Class = AssetData.GetClass())
			{
				if(Class->IsChildOf(URigVMBlueprint::StaticClass()))
				{
					SetSchemata(Class);
				}
			}
		}

		if(URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(GetNode()))
		{
			(void)GetController()->SwapFunctionReference(FunctionReferenceNode, InIdentifier, false, false, false);
		}
		else
		{
			URigVMNode* Node = GetController()->AddFunctionReferenceNodeFromDescription(Header, FVector2D::ZeroVector, FString(), false, false);
			SetNode(Node);
		}
	}
}

URigVMEdGraph* FRigVMMinimalEnvironment::GetEdGraph() const
{
	return EdGraph.Get();
}

URigVMEdGraphNode* FRigVMMinimalEnvironment::GetEdGraphNode() const
{
	if(EdGraphNode.IsValid())
	{
		return EdGraphNode.Get();
	}
	return nullptr;
}

void FRigVMMinimalEnvironment::SetSchemata(const UClass* InRigVMBlueprintClass)
{
	check(InRigVMBlueprintClass);

	const URigVMBlueprint* CDO = InRigVMBlueprintClass->GetDefaultObject<URigVMBlueprint>();
	check(CDO);

	EdGraphClass = CDO->GetRigVMEdGraphClass();
	EdGraphNodeClass = CDO->GetRigVMEdGraphNodeClass();

	if(!EdGraph.IsValid() || EdGraph->GetClass() != EdGraphClass)
	{
		EdGraph = TStrongObjectPtr<URigVMEdGraph>(NewObject<URigVMEdGraph>(ModelGraph.Get(), EdGraphClass));
	}

	ModelController->SetSchemaClass(CDO->GetRigVMSchemaClass());
	EdGraph->SetBlueprintClass(InRigVMBlueprintClass);
}

FSimpleDelegate& FRigVMMinimalEnvironment::OnChanged()
{
	return ChangedDelegate;
}

void FRigVMMinimalEnvironment::Tick_GameThead(float InDeltaTime)
{
	if(NumModifications.exchange(0) > 0)
	{
		// refresh the EdGraphNode
		if(URigVMNode* MyModelNode = GetNode())
		{
			if(URigVMEdGraphNode* MyEdGraphNode = GetEdGraphNode())
			{
				MyEdGraphNode->SetSubTitleEnabled(false);
				MyEdGraphNode->SetModelNode(MyModelNode);
				(void)ChangedDelegate.ExecuteIfBound();
			}
		}
	}
}

void FRigVMMinimalEnvironment::HandleModified(ERigVMGraphNotifType InNotification, URigVMGraph* InGraph, UObject* InSubject)
{
	if(InGraph != GetModel())
	{
		return;
	}

	switch(InNotification)
	{
		case ERigVMGraphNotifType::NodeSelected:
		case ERigVMGraphNotifType::NodeDeselected:
		case ERigVMGraphNotifType::PinExpansionChanged:
		case ERigVMGraphNotifType::InteractionBracketOpened:
		case ERigVMGraphNotifType::InteractionBracketClosed:
		case ERigVMGraphNotifType::InteractionBracketCanceled:
		case ERigVMGraphNotifType::PinCategoryChanged:
		case ERigVMGraphNotifType::PinCategoriesChanged:
		case ERigVMGraphNotifType::PinCategoryExpansionChanged:
		{
			break;
		}
		default:
		{
			(void)NumModifications.fetch_add(1);
		}
	}
}

#undef LOCTEXT_NAMESPACE
