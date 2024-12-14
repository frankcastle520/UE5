// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/ControlRigGraphNode.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/ControlRigGraphSchema.h"
#include "IControlRigEditorModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ControlRigGraphNode)

#define LOCTEXT_NAMESPACE "ControlRigGraphNode"

UControlRigGraphNode::UControlRigGraphNode()
: URigVMEdGraphNode()
{
}

#if WITH_EDITOR

void UControlRigGraphNode::AddPinSearchMetaDataInfo(const UEdGraphPin* Pin, TArray<FSearchTagDataPair>& OutTaggedMetaData) const
{
	Super::AddPinSearchMetaDataInfo(Pin, OutTaggedMetaData);

	if(const URigVMPin* ModelPin = FindModelPinFromGraphPin(Pin))
	{
		if(ModelPin->GetCPPTypeObject() == FRigElementKey::StaticStruct())
		{
			const FString DefaultValue = ModelPin->GetDefaultValue();
			if(!DefaultValue.IsEmpty())
			{
				FString RigElementKeys;
				if(ModelPin->IsArray())
				{
					RigElementKeys = DefaultValue;
				}
				else
				{
					RigElementKeys = FString::Printf(TEXT("(%s)"), *DefaultValue);
				}
				if(!RigElementKeys.IsEmpty())
				{
					RigElementKeys.ReplaceInline(TEXT("="), TEXT(","));
					RigElementKeys.ReplaceInline(TEXT("\""), TEXT(""));
					OutTaggedMetaData.Emplace(FText::FromString(TEXT("Rig Items")), FText::FromString(RigElementKeys));
				}
			}
		}
	}
}

#endif

#undef LOCTEXT_NAMESPACE

