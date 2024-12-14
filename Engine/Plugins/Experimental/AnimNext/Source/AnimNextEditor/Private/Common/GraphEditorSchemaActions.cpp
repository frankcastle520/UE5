// Copyright Epic Games, Inc. All Rights Reserved.

#include "GraphEditorSchemaActions.h"
#include "EditorUtils.h"
#include "AnimNextEdGraph.h"
#include "AnimNextEdGraphNode.h"
#include "EdGraphNode_Comment.h"
#include "Editor.h"
#include "Module/AnimNextModule_EditorData.h"
#include "RigVMModel/Nodes/RigVMUnitNode.h"
#include "Settings/ControlRigSettings.h"
#include "Units/RigUnit.h"
#include "EdGraphSchema_K2.h"
#include "GraphEditor.h"

#define LOCTEXT_NAMESPACE "AnimNextSchemaActions"

UEdGraphNode* FAnimNextSchemaAction_RigUnit::PerformAction(UEdGraph* ParentGraph, TArray<UEdGraphPin*>& FromPins, const FVector2D Location, bool bSelectNewNode)
{
	IRigVMClientHost* Host = ParentGraph->GetImplementingOuter<IRigVMClientHost>();
	URigVMEdGraphNode* NewNode = nullptr;
	URigVMEdGraph* EdGraph = Cast<URigVMEdGraph>(ParentGraph);

	UEdGraphPin* FromPin = nullptr;
	if (FromPins.Num() > 0)
	{
		FromPin = FromPins[0];
	}
	
	if (Host != nullptr && EdGraph != nullptr)
	{
		FName Name = UE::AnimNext::Editor::FUtils::ValidateName(Cast<UObject>(Host), StructTemplate->GetFName().ToString());
		URigVMController* Controller = Host->GetRigVMClient()->GetController(ParentGraph);

		Controller->OpenUndoBracket(FString::Printf(TEXT("Add '%s' Node"), *Name.ToString()));

		FRigVMUnitNodeCreatedContext& UnitNodeCreatedContext = Controller->GetUnitNodeCreatedContext();
		FRigVMUnitNodeCreatedContext::FScope ReasonScope(UnitNodeCreatedContext, ERigVMNodeCreatedReason::NodeSpawner);

		if (URigVMUnitNode* ModelNode = Controller->AddUnitNode(StructTemplate, FRigVMStruct::ExecuteName, Location, Name.ToString(), true, false))
		{
			NewNode = Cast<URigVMEdGraphNode>(EdGraph->FindNodeForModelNodeName(ModelNode->GetFName()));
			check(NewNode);

			if (NewNode)
			{
				if(FromPin)
				{
					NewNode->AutowireNewNode(FromPin);
				}

				Controller->ClearNodeSelection(true);
				Controller->SelectNode(ModelNode, true, true);
			}

#if WITH_EDITORONLY_DATA

			// @TODO: switch over to custom settings here
			const FControlRigSettingsPerPinBool* ExpansionMapPtr = UControlRigEditorSettings::Get()->RigUnitPinExpansion.Find(ModelNode->GetScriptStruct()->GetName());
			if (ExpansionMapPtr)
			{
				const FControlRigSettingsPerPinBool& ExpansionMap = *ExpansionMapPtr;

				for (const TPair<FString, bool>& Pair : ExpansionMap.Values)
				{
					FString PinPath = FString::Printf(TEXT("%s.%s"), *ModelNode->GetName(), *Pair.Key);
					Controller->SetPinExpansion(PinPath, Pair.Value, true);
				}
			}
		}
#endif
		
		Controller->CloseUndoBracket();
	}

	return NewNode;
}


UEdGraphNode* FAnimNextSchemaAction_DispatchFactory::PerformAction(UEdGraph* ParentGraph, TArray<UEdGraphPin*>& FromPins, const FVector2D Location, bool bSelectNewNode)
{
	IRigVMClientHost* Host = ParentGraph->GetImplementingOuter<IRigVMClientHost>();
	if(Host == nullptr)
	{
		return nullptr;
	}

	URigVMEdGraph* EdGraph = Cast<URigVMEdGraph>(ParentGraph);
	if(EdGraph == nullptr)
	{
		return nullptr;
	}

	const FRigVMTemplate* Template = FRigVMRegistry::Get().FindTemplate(Notation);
	if (Template == nullptr)
	{
		return nullptr;
	}
	
	URigVMEdGraphNode* NewNode = nullptr;
	UEdGraphPin* FromPin = nullptr;
	if (FromPins.Num() > 0)
	{
		FromPin = FromPins[0];
	}

	const int32 NotationHash = (int32)GetTypeHash(Notation);
	const FString TemplateName = TEXT("RigVMTemplate_") + FString::FromInt(NotationHash);

	FName Name = UE::AnimNext::Editor::FUtils::ValidateName(Cast<UObject>(Host), Template->GetName().ToString());
	URigVMController* Controller = Host->GetRigVMClient()->GetController(ParentGraph);

	Controller->OpenUndoBracket(FString::Printf(TEXT("Add '%s' Node"), *Name.ToString()));

	if (URigVMTemplateNode* ModelNode = Controller->AddTemplateNode(Notation, Location, Name.ToString(), true, false))
	{
		NewNode = Cast<URigVMEdGraphNode>(EdGraph->FindNodeForModelNodeName(ModelNode->GetFName()));

		if (NewNode)
		{
			if(FromPin)
			{
				NewNode->AutowireNewNode(FromPin);
			}

			Controller->ClearNodeSelection(true);
			Controller->SelectNode(ModelNode, true, true);
		}

		Controller->CloseUndoBracket();
	}
	else
	{
		Controller->CancelUndoBracket();
	}

	return NewNode;
}

FAnimNextSchemaAction_Variable::FAnimNextSchemaAction_Variable(const FRigVMExternalVariable& InExternalVariable, bool bInIsGetter)
	: ExternalVariable(InExternalVariable)
	, bIsGetter(bInIsGetter)
{
	static const FText VariablesCategory = LOCTEXT("Variables", "Variables");
	static const FTextFormat GetVariableFormat = LOCTEXT("GetVariableFormat", "Get {0}");
	static const FTextFormat SetVariableFormat = LOCTEXT("SetVariableFormat", "Set {0}");

	FText MenuDesc;
	FText ToolTip;

	if(bInIsGetter)
	{
		MenuDesc = FText::Format(GetVariableFormat, FText::FromName(ExternalVariable.Name));
		ToolTip = FText::FromString(FString::Printf(TEXT("Get the value of variable %s"), *ExternalVariable.Name.ToString()));
	}
	else
	{
		MenuDesc = FText::Format(SetVariableFormat, FText::FromName(ExternalVariable.Name));
		ToolTip = FText::FromString(FString::Printf(TEXT("Set the value of variable %s"), *ExternalVariable.Name.ToString()));
	}

	UpdateSearchData(MenuDesc, ToolTip, VariablesCategory, FText::GetEmpty());

	FEdGraphPinType PinType = RigVMTypeUtils::PinTypeFromExternalVariable(ExternalVariable);
	VariableColor = GetDefault<UEdGraphSchema_K2>()->GetPinTypeColor(PinType);
}

UEdGraphNode* FAnimNextSchemaAction_Variable::PerformAction(UEdGraph* ParentGraph, TArray<UEdGraphPin*>& FromPins, const FVector2D Location, bool bSelectNewNode)
{
	IRigVMClientHost* Host = ParentGraph->GetImplementingOuter<IRigVMClientHost>();
	if(Host == nullptr)
	{
		return nullptr;
	}

	URigVMEdGraph* EdGraph = Cast<URigVMEdGraph>(ParentGraph);
	if(EdGraph == nullptr)
	{
		return nullptr;
	}

	URigVMEdGraphNode* NewNode = nullptr;

	FString ObjectPath;
	if (ExternalVariable.TypeObject)
	{
		ObjectPath = ExternalVariable.TypeObject->GetPathName();
	}

	FString TypeName = ExternalVariable.TypeName.ToString();
	if (ExternalVariable.bIsArray)
	{
		TypeName = FString::Printf(TEXT("TArray<%s>"), *TypeName);
	}

	FString NodeName;

	URigVMController* Controller = Host->GetRigVMClient()->GetController(ParentGraph);
	Controller->OpenUndoBracket(TEXT("Add Variable"));

	if (URigVMNode* ModelNode = Controller->AddVariableNodeFromObjectPath(ExternalVariable.Name, TypeName, ObjectPath, bIsGetter, FString(), Location, NodeName, true, true))
	{
		for (UEdGraphNode* Node : ParentGraph->Nodes)
		{
			if (URigVMEdGraphNode* RigNode = Cast<URigVMEdGraphNode>(Node))
			{
				if (RigNode->GetModelNodeName() == ModelNode->GetFName())
				{
					NewNode = RigNode;
					break;
				}
			}
		}

		if (NewNode)
		{
			Controller->ClearNodeSelection(true);
			Controller->SelectNode(ModelNode, true, true);
		}
		Controller->CloseUndoBracket();
	}
	else
	{
		Controller->CancelUndoBracket();
	}

	return NewNode;
}

FAnimNextSchemaAction_AddComment::FAnimNextSchemaAction_AddComment()
	: FAnimNextSchemaAction(FText(), LOCTEXT("AddComment", "Add Comment..."), LOCTEXT("AddCommentTooltip", "Create a resizable comment box."))
{
}

UEdGraphNode* FAnimNextSchemaAction_AddComment::PerformAction(UEdGraph* ParentGraph, TArray<UEdGraphPin*>& FromPins, const FVector2D Location, bool bSelectNewNode/* = true*/)
{
	UEdGraphNode_Comment* const CommentTemplate = NewObject<UEdGraphNode_Comment>();

	FVector2D SpawnLocation = Location;
	FSlateRect Bounds;

	TSharedPtr<SGraphEditor> GraphEditorPtr = SGraphEditor::FindGraphEditorForGraph(ParentGraph);
	if (GraphEditorPtr.IsValid() && GraphEditorPtr->GetBoundsForSelectedNodes(/*out*/ Bounds, 50.0f))
	{
		CommentTemplate->SetBounds(Bounds);
		SpawnLocation.X = CommentTemplate->NodePosX;
		SpawnLocation.Y = CommentTemplate->NodePosY;
	}

	return FEdGraphSchemaAction_NewNode::SpawnNodeFromTemplate<UEdGraphNode_Comment>(ParentGraph, CommentTemplate, SpawnLocation, bSelectNewNode);
}

// *** Graph Function ***

FAnimNextSchemaAction_Function::FAnimNextSchemaAction_Function(const FRigVMGraphFunctionHeader& InReferencedPublicFunctionHeader, const FText& InNodeCategory, const FText& InMenuDesc, const FText& InToolTip, const FText& InKeywords)
	: FAnimNextSchemaAction(InNodeCategory, InMenuDesc, InToolTip, InKeywords)
{
	ReferencedPublicFunctionHeader = InReferencedPublicFunctionHeader;
	NodeClass = UAnimNextEdGraphNode::StaticClass();
	bIsLocalFunction = true;
}

FAnimNextSchemaAction_Function::FAnimNextSchemaAction_Function(const URigVMLibraryNode* InFunctionLibraryNode, const FText& InNodeCategory, const FText& InMenuDesc, const FText& InToolTip, const FText& InKeywords)
	: FAnimNextSchemaAction(InNodeCategory, InMenuDesc, InToolTip, InKeywords)
{
	ReferencedPublicFunctionHeader = InFunctionLibraryNode->GetFunctionHeader();
	NodeClass = UAnimNextEdGraphNode::StaticClass();
	bIsLocalFunction = true;
}

const FSlateBrush* FAnimNextSchemaAction_Function::GetIconBrush() const
{
	return FAppStyle::GetBrush("GraphEditor.Function_16x");
}

UEdGraphNode* FAnimNextSchemaAction_Function::PerformAction(UEdGraph* ParentGraph, TArray<UEdGraphPin*>& FromPins, const FVector2D Location, bool bSelectNewNode)
{
	IRigVMClientHost* Host = ParentGraph->GetImplementingOuter<IRigVMClientHost>();
	URigVMEdGraphNode* NewNode = nullptr;
	URigVMEdGraph* EdGraph = Cast<URigVMEdGraph>(ParentGraph);

	UEdGraphPin* FromPin = nullptr;
	if (FromPins.Num() > 0)
	{
		FromPin = FromPins[0];
	}

	if (Host != nullptr && EdGraph != nullptr)
	{
		FName Name = UE::AnimNext::Editor::FUtils::ValidateName(Cast<UObject>(Host), ReferencedPublicFunctionHeader.Name.ToString());
		URigVMController* Controller = EdGraph->GetController();

		Controller->OpenUndoBracket(FString::Printf(TEXT("Add '%s' Node"), *Name.ToString()));

		if (URigVMFunctionReferenceNode* ModelNode = Controller->AddFunctionReferenceNodeFromDescription(ReferencedPublicFunctionHeader, Location, Name.ToString(), true, true))
		{
			NewNode = Cast<URigVMEdGraphNode>(EdGraph->FindNodeForModelNodeName(ModelNode->GetFName()));
			check(NewNode);

			if (NewNode)
			{
				Controller->ClearNodeSelection(true);
				Controller->SelectNode(ModelNode, true, true);
			}
			Controller->CloseUndoBracket();
		}
		else
		{
			Controller->CancelUndoBracket();
		}
	}

	return NewNode;
}

#undef LOCTEXT_NAMESPACE
