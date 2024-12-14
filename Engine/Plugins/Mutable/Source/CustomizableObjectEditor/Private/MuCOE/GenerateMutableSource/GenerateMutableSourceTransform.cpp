// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenerateMutableSourceTransform.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSource.h"
#include "MuCOE/Nodes/CustomizableObjectNodeTransformConstant.h"
#include "MuCOE/Nodes/CustomizableObjectNodeTransformParameter.h"
#include "MuT/NodeMatrixConstant.h"
#include "MuT/NodeMatrixParameter.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


mu::Ptr<mu::NodeMatrix> GenerateMutableSourceTransform(const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext)
{
	check(Pin);

	RETURN_ON_CYCLE(*Pin, GenerationContext)

	CheckNumOutputs(*Pin, GenerationContext);

	UCustomizableObjectNode* Node = CastChecked<UCustomizableObjectNode>(Pin->GetOwningNode());
	const FGeneratedKey Key(reinterpret_cast<void*>(&GenerateMutableSourceTransform), *Pin, *Node, GenerationContext);
	if (const FGeneratedData* Generated = GenerationContext.Generated.Find(Key))
	{
		return static_cast<mu::NodeMatrix*>(Generated->Node.get());
	}

	if (Node->IsNodeOutDatedAndNeedsRefresh())
	{
		Node->SetRefreshNodeWarning();
	}

	mu::Ptr<mu::NodeMatrix> Result;
	if (const UCustomizableObjectNodeTransformConstant* TypedNodeTransformConstant = Cast<UCustomizableObjectNodeTransformConstant>(Node))
	{
		mu::Ptr<mu::NodeMatrixConstant> MatrixNode = new mu::NodeMatrixConstant{};
		Result = MatrixNode;
		MatrixNode->Value = FMatrix44f(TypedNodeTransformConstant->Value.ToMatrixWithScale());
	}
	else if (const UCustomizableObjectNodeTransformParameter* TypedNodeTransformParameter = Cast<UCustomizableObjectNodeTransformParameter>(Node))
	{
		mu::Ptr<mu::NodeMatrixParameter> MatrixNode = new mu::NodeMatrixParameter();
		Result = MatrixNode;
		
		MatrixNode->Name = TypedNodeTransformParameter->ParameterName;
		MatrixNode->Uid = GenerationContext.GetNodeIdUnique(Node).ToString();
		MatrixNode->DefaultValue = FMatrix44f(TypedNodeTransformParameter->DefaultValue.ToMatrixWithScale());

		GenerationContext.ParameterUIDataMap.Add(TypedNodeTransformParameter->ParameterName, FMutableParameterData(
			TypedNodeTransformParameter->ParamUIMetadata,
			EMutableParameterType::Transform));
	}
	else
	{
		GenerationContext.Log(LOCTEXT("UnimplementedNode", "Node type not implemented yet."), Node);
	}
	
	FGeneratedData CacheData = FGeneratedData(Node, Result);
	GenerationContext.Generated.Add(Key, CacheData);
	GenerationContext.GeneratedNodes.Add(Node);
	
	if (Result)
	{
		Result->SetMessageContext(Node);
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
