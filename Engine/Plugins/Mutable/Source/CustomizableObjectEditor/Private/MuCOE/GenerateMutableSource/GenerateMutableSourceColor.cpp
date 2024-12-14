// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/GenerateMutableSource/GenerateMutableSourceColor.h"

#include "MuCOE/CustomizableObjectCompiler.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSourceFloat.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSourceImage.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSourceTable.h"
#include "MuCOE/GraphTraversal.h"
#include "MuCOE/Nodes/CustomizableObjectNodeColorArithmeticOp.h"
#include "MuCOE/Nodes/CustomizableObjectNodeColorConstant.h"
#include "MuCOE/Nodes/CustomizableObjectNodeColorFromFloats.h"
#include "MuCOE/Nodes/CustomizableObjectNodeColorParameter.h"
#include "MuCOE/Nodes/CustomizableObjectNodeColorSwitch.h"
#include "MuCOE/Nodes/CustomizableObjectNodeColorVariation.h"
#include "MuCOE/Nodes/CustomizableObjectNodeTable.h"
#include "MuCOE/Nodes/CustomizableObjectNodeTextureSample.h"
#include "MuT/NodeColourArithmeticOperation.h"
#include "MuT/NodeColourConstant.h"
#include "MuT/NodeColourFromScalars.h"
#include "MuT/NodeColourParameter.h"
#include "MuT/NodeColourSampleImage.h"
#include "MuT/NodeColourSwitch.h"
#include "MuT/NodeColourTable.h"
#include "MuT/NodeColourVariation.h"
#include "MuT/NodeImageBinarise.h"


#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


mu::Ptr<mu::NodeColour> GenerateMutableSourceColor(const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext)
{
	check(Pin)
	RETURN_ON_CYCLE(*Pin, GenerationContext)

	CheckNumOutputs(*Pin, GenerationContext);
	
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	UCustomizableObjectNode* Node = CastChecked<UCustomizableObjectNode>(Pin->GetOwningNode());

	const FGeneratedKey Key(reinterpret_cast<void*>(&GenerateMutableSourceColor), *Pin, *Node, GenerationContext);
	if (const FGeneratedData* Generated = GenerationContext.Generated.Find(Key))
	{
		return static_cast<mu::NodeColour*>(Generated->Node.get());
	}

	if (Node->IsNodeOutDatedAndNeedsRefresh())
	{
		Node->SetRefreshNodeWarning();
	}

	mu::Ptr<mu::NodeColour> Result;

	if (const UCustomizableObjectNodeColorConstant* TypedNodeColorConst = Cast<UCustomizableObjectNodeColorConstant>(Node))
	{
		mu::Ptr<mu::NodeColourConstant> ColorNode = new mu::NodeColourConstant();
		Result = ColorNode;

		ColorNode->Value = TypedNodeColorConst->Value;
	}

	else if (const UCustomizableObjectNodeColorParameter* TypedNodeColorParam = Cast<UCustomizableObjectNodeColorParameter>(Node))
	{
		mu::Ptr<mu::NodeColourParameter> ColorNode = new mu::NodeColourParameter();
		Result = ColorNode;

		ColorNode->Name = TypedNodeColorParam->ParameterName;
		ColorNode->Uid = GenerationContext.GetNodeIdUnique(Node).ToString();
		ColorNode->DefaultValue = TypedNodeColorParam->DefaultValue;

		GenerationContext.ParameterUIDataMap.Add(TypedNodeColorParam->ParameterName, FMutableParameterData(
			TypedNodeColorParam->ParamUIMetadata,
			EMutableParameterType::Color));
	}

	else if (const UCustomizableObjectNodeColorSwitch* TypedNodeColorSwitch = Cast<UCustomizableObjectNodeColorSwitch>(Node))
	{
		Result = [&]() -> mu::Ptr<mu::NodeColourSwitch>
		{
			if (const int32 NumParameters = FollowInputPinArray(*TypedNodeColorSwitch->SwitchParameter()).Num();
				NumParameters != 1)
			{
				const FText Message = NumParameters
					? LOCTEXT("NoEnumParamInSwitch", "Switch nodes must have an enum switch parameter. Please connect an enum and refesh the switch node.")
					: LOCTEXT("InvalidEnumInSwitch", "Switch nodes must have a single enum with all the options inside. Please remove all the enums but one and refresh the switch node.");

				GenerationContext.Log(Message, Node);
				return nullptr;
			}

			const UEdGraphPin* EnumPin = FollowInputPin(*TypedNodeColorSwitch->SwitchParameter());
			mu::NodeScalarPtr SwitchParam = EnumPin ? GenerateMutableSourceFloat(EnumPin, GenerationContext) : nullptr;

			// Switch Param not generated
			if (!SwitchParam)
			{
				// Warn about a failure.
				if (EnumPin)
				{
					const FText Message = LOCTEXT("FailedToGenerateSwitchParam", "Could not generate switch enum parameter. Please refesh the switch node and connect an enum.");
					GenerationContext.Log(Message, Node);
				}

				return nullptr;
			}

			if (SwitchParam->GetType() != mu::NodeScalarEnumParameter::GetStaticType())
			{
				const FText Message = LOCTEXT("WrongSwitchParamType", "Switch parameter of incorrect type.");
				GenerationContext.Log(Message, Node);

				return nullptr;
			}

			const int32 NumSwitchOptions = TypedNodeColorSwitch->GetNumElements();

			mu::NodeScalarEnumParameter* EnumParameter = static_cast<mu::NodeScalarEnumParameter*>(SwitchParam.get());
			if (NumSwitchOptions != EnumParameter->GetValueCount())
			{
				const FText Message = LOCTEXT("MismatchedSwitch", "Switch enum and switch node have different number of options. Please refresh the switch node to make sure the outcomes are labeled properly.");
				GenerationContext.Log(Message, Node);
			}

			mu::Ptr<mu::NodeColourSwitch> SwitchNode = new mu::NodeColourSwitch;
			SwitchNode->Parameter = SwitchParam;
			SwitchNode->Options.SetNum(NumSwitchOptions);

			for (int SelectorIndex = 0; SelectorIndex < NumSwitchOptions; ++SelectorIndex)
			{
				const UEdGraphPin* const ColorPin = TypedNodeColorSwitch->GetElementPin(SelectorIndex);
				if (const UEdGraphPin* ConnectedPin = FollowInputPin(*ColorPin))
				{
					SwitchNode->Options[SelectorIndex] = GenerateMutableSourceColor(ConnectedPin, GenerationContext);
				}
			}

			return SwitchNode;
		}(); // invoke lambda;
	}

	else if (const UCustomizableObjectNodeTextureSample* TypedNodeTexSample = Cast<UCustomizableObjectNodeTextureSample>(Node))
	{
		mu::Ptr<mu::NodeColourSampleImage> ColorNode = new mu::NodeColourSampleImage();
		Result = ColorNode;

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeTexSample->TexturePin()))
		{
			mu::NodeImagePtr TextureNode = GenerateMutableSourceImage(ConnectedPin, GenerationContext, 0);
			ColorNode->Image = TextureNode;
		}

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeTexSample->XPin()))
		{
			mu::NodeScalarPtr XNode = GenerateMutableSourceFloat(ConnectedPin, GenerationContext);
			ColorNode->X = XNode;
		}

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeTexSample->YPin()))
		{
			mu::NodeScalarPtr YNode = GenerateMutableSourceFloat(ConnectedPin, GenerationContext);
			ColorNode->Y = YNode;
		}
	}

	else if (const UCustomizableObjectNodeColorArithmeticOp* TypedNodeColorArith = Cast<UCustomizableObjectNodeColorArithmeticOp>(Node))
	{
		mu::Ptr<mu::NodeColourArithmeticOperation> OpNode = new mu::NodeColourArithmeticOperation();
		Result = OpNode;

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeColorArith->XPin()))
		{
			OpNode->A = GenerateMutableSourceColor(ConnectedPin, GenerationContext);
		}

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeColorArith->YPin()))
		{
			OpNode->B = GenerateMutableSourceColor(ConnectedPin, GenerationContext);
		}

		switch (TypedNodeColorArith->Operation)
		{
		case EColorArithmeticOperation::E_Add:
			OpNode->Operation = mu::NodeColourArithmeticOperation::EOperation::Add;
			break;

		case EColorArithmeticOperation::E_Sub:
			OpNode->Operation = mu::NodeColourArithmeticOperation::EOperation::Subtract;
			break;

		case EColorArithmeticOperation::E_Mul:
			OpNode->Operation = mu::NodeColourArithmeticOperation::EOperation::Multiply;
			break;

		case EColorArithmeticOperation::E_Div:
			OpNode->Operation = mu::NodeColourArithmeticOperation::EOperation::Divide;
			break;
		}
	}

	else if (const UCustomizableObjectNodeColorFromFloats* TypedNodeFrom = Cast<UCustomizableObjectNodeColorFromFloats>(Node))
	{
		mu::Ptr<mu::NodeColourFromScalars> OpNode = new mu::NodeColourFromScalars();
		Result = OpNode;

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeFrom->RPin()))
		{
			mu::NodeScalarPtr FloatNode = GenerateMutableSourceFloat(ConnectedPin, GenerationContext);
			OpNode->X = FloatNode;
		}

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeFrom->GPin()))
		{
			mu::NodeScalarPtr FloatNode = GenerateMutableSourceFloat(ConnectedPin, GenerationContext);
			OpNode->Y = FloatNode;
		}

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeFrom->BPin()))
		{
			mu::NodeScalarPtr FloatNode = GenerateMutableSourceFloat(ConnectedPin, GenerationContext);
			OpNode->Z = FloatNode;
		}

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeFrom->APin()))
		{
			mu::NodeScalarPtr FloatNode = GenerateMutableSourceFloat(ConnectedPin, GenerationContext);
			OpNode->W = FloatNode;
		}
	}

	else if (const UCustomizableObjectNodeColorVariation* TypedNodeColorVar = Cast<const UCustomizableObjectNodeColorVariation>(Node))
	{
		mu::Ptr<mu::NodeColourVariation> ColorNode = new mu::NodeColourVariation();
		Result = ColorNode;

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeColorVar->DefaultPin()))
		{
			mu::Ptr<mu::NodeColour> ChildNode = GenerateMutableSourceColor(ConnectedPin, GenerationContext);
			if (ChildNode)
			{
				ColorNode->DefaultColour = ChildNode;
			}
			else
			{
				GenerationContext.Log(LOCTEXT("ColorFailed", "Color generation failed."), Node);
			}
		}

		const int32 NumVariations = TypedNodeColorVar->GetNumVariations();
		ColorNode->Variations.SetNum(NumVariations);
		for (int VariationIndex = 0; VariationIndex < NumVariations; ++VariationIndex)
		{
			const UEdGraphPin* VariationPin = TypedNodeColorVar->VariationPin(VariationIndex);
			if (!VariationPin) continue;

			ColorNode->Variations[VariationIndex].Tag = TypedNodeColorVar->GetVariation(VariationIndex).Tag;
			if (const UEdGraphPin* ConnectedPin = FollowInputPin(*VariationPin))
			{
				mu::Ptr<mu::NodeColour> ChildNode = GenerateMutableSourceColor(ConnectedPin, GenerationContext);
				ColorNode->Variations[VariationIndex].Colour = ChildNode;
			}
		}
	}

	else if (const UCustomizableObjectNodeTable* TypedNodeTable = Cast<UCustomizableObjectNodeTable>(Node))
	{
		//This node will add a white color in case of error
		mu::Ptr<mu::NodeColourConstant> WhiteColorNode = new mu::NodeColourConstant();
		WhiteColorNode->Value = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);

		Result = WhiteColorNode;

		bool bSuccess = true;
		UDataTable* DataTable = GetDataTable(TypedNodeTable, GenerationContext);

		if (DataTable)
		{
			FString ColumnName = Pin->PinFriendlyName.ToString();
			FProperty* Property = DataTable->FindTableProperty(FName(*ColumnName));

			if (!Property)
			{
				FString Msg = FString::Printf(TEXT("Couldn't find the column [%s] in the data table's struct."), *ColumnName);
				GenerationContext.Log(FText::FromString(Msg), Node);

				bSuccess = false;
			}

			if (bSuccess)
			{
				// Generating a new data table if not exists
				mu::TablePtr Table;
				Table = GenerateMutableSourceTable(DataTable, TypedNodeTable, GenerationContext);

				if (Table)
				{
					mu::Ptr<mu::NodeColourTable> ColorTableNode = new mu::NodeColourTable();

					// Generating a new Color column if not exists
					if (Table->FindColumn(ColumnName) == INDEX_NONE)
					{
						int32 Dummy = -1; // TODO MTBL-1512
						bool Dummy2 = false;
						bSuccess = GenerateTableColumn(TypedNodeTable, Pin, Table, ColumnName, Property, Dummy, Dummy, GenerationContext.CurrentLOD, Dummy, 0, Dummy2, GenerationContext);

						if (!bSuccess)
						{
							FString Msg = FString::Printf(TEXT("Failed to generate the mutable table column [%s]"), *ColumnName);
							GenerationContext.Log(FText::FromString(Msg), Node);
						}
					}

					if (bSuccess)
					{
						Result = ColorTableNode;

						ColorTableNode->Table = Table;
						ColorTableNode->ColumnName = ColumnName;
						ColorTableNode->ParameterName = TypedNodeTable->ParameterName;
						ColorTableNode->bNoneOption = TypedNodeTable->bAddNoneOption;
						ColorTableNode->DefaultRowName = TypedNodeTable->DefaultRowName.ToString();
					}
				}
				else
				{
					FString Msg = FString::Printf(TEXT("Couldn't generate a mutable table."), *ColumnName);
					GenerationContext.Log(FText::FromString(Msg), Node);
				}
			}
		}
		else
		{
			GenerationContext.Log(LOCTEXT("ColorTableError", "Couldn't find the data table of the node."), Node);
		}
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
