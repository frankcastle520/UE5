// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/GenerateMutableSource/GenerateMutableSourceFloat.h"

#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveVector.h"
#include "MuCOE/CustomizableObjectCompiler.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSourceImage.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSourceTable.h"
#include "MuCOE/GraphTraversal.h"
#include "MuCOE/Nodes/CustomizableObjectNodeCurve.h"
#include "MuCOE/Nodes/CustomizableObjectNodeEnumParameter.h"
#include "MuCOE/Nodes/CustomizableObjectNodeFloatConstant.h"
#include "MuCOE/Nodes/CustomizableObjectNodeFloatParameter.h"
#include "MuCOE/Nodes/CustomizableObjectNodeFloatSwitch.h"
#include "MuCOE/Nodes/CustomizableObjectNodeFloatVariation.h"
#include "MuCOE/Nodes/CustomizableObjectNodeFloatArithmeticOp.h"
#include "MuCOE/Nodes/CustomizableObjectNodeTable.h"
#include "MuT/NodeScalarConstant.h"
#include "MuT/NodeScalarCurve.h"
#include "MuT/NodeScalarSwitch.h"
#include "MuT/NodeScalarTable.h"
#include "MuT/NodeScalarVariation.h"
#include "MuT/NodeScalarArithmeticOperation.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"



mu::NodeScalarPtr GenerateMutableSourceFloat(const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext)
{
	check(Pin)
	RETURN_ON_CYCLE(*Pin, GenerationContext)

	CheckNumOutputs(*Pin, GenerationContext);
	
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	UCustomizableObjectNode* Node = CastChecked<UCustomizableObjectNode>(Pin->GetOwningNode());

	const FGeneratedKey Key(reinterpret_cast<void*>(&GenerateMutableSourceFloat), *Pin, *Node, GenerationContext);
	if (const FGeneratedData* Generated = GenerationContext.Generated.Find(Key))
	{
		return static_cast<mu::NodeScalar*>(Generated->Node.get());
	}

	if (Node->IsNodeOutDatedAndNeedsRefresh())
	{
		Node->SetRefreshNodeWarning();
	}

	bool bDoNotAddToGeneratedCache = false;

	mu::NodeScalarPtr Result;
	
	if (const UCustomizableObjectNodeFloatConstant* FloatConstantNode = Cast<UCustomizableObjectNodeFloatConstant>(Node))
	{
		mu::NodeScalarConstantPtr ScalarNode = new mu::NodeScalarConstant();
		Result = ScalarNode;

		ScalarNode->SetValue(FloatConstantNode->Value);
	}

	else if (const UCustomizableObjectNodeFloatParameter* FloatParameterNode = Cast<UCustomizableObjectNodeFloatParameter>(Node))
	{
		mu::NodeScalarParameterPtr ScalarNode = new mu::NodeScalarParameter();
		Result = ScalarNode;

		ScalarNode->SetName(FloatParameterNode->ParameterName);
		ScalarNode->SetUid(GenerationContext.GetNodeIdUnique(Node).ToString());
		ScalarNode->SetDefaultValue(FloatParameterNode->DefaultValue);

		GenerationContext.ParameterUIDataMap.Add(FloatParameterNode->ParameterName, FMutableParameterData(
			FloatParameterNode->ParamUIMetadata,
			EMutableParameterType::Float));
	}

	else if (const UCustomizableObjectNodeEnumParameter* EnumParamNode = Cast<UCustomizableObjectNodeEnumParameter>(Node))
	{
		mu::NodeScalarEnumParameterPtr EnumParameterNode = new mu::NodeScalarEnumParameter;

		const int32 NumSelectors = EnumParamNode->Values.Num();

		int32 DefaultValue = FMath::Clamp(EnumParamNode->DefaultIndex, 0, NumSelectors - 1);

		EnumParameterNode->SetName(EnumParamNode->ParameterName);
		EnumParameterNode->SetUid(GenerationContext.GetNodeIdUnique(Node).ToString());
		EnumParameterNode->SetValueCount(NumSelectors);
		EnumParameterNode->SetDefaultValueIndex(DefaultValue);

		FMutableParameterData ParameterUIData(EnumParamNode->ParamUIMetadata, EMutableParameterType::Int);
		ParameterUIData.IntegerParameterGroupType = ECustomizableObjectGroupType::COGT_ONE;

		for (int SelectorIndex = 0; SelectorIndex < NumSelectors; ++SelectorIndex)
		{
			EnumParameterNode->SetValue(SelectorIndex, (float)SelectorIndex, EnumParamNode->Values[SelectorIndex].Name);

			ParameterUIData.ArrayIntegerParameterOption.Add(
				EnumParamNode->Values[SelectorIndex].Name,
				FIntegerParameterUIData(EnumParamNode->Values[SelectorIndex].ParamUIMetadata));
		}

		Result = EnumParameterNode;

		GenerationContext.ParameterUIDataMap.Add(EnumParamNode->ParameterName, ParameterUIData);
	}

	else if (const UCustomizableObjectNodeFloatSwitch* TypedNodeFloatSwitch = Cast<UCustomizableObjectNodeFloatSwitch>(Node))
	{
		// Using a lambda so control flow is easier to manage.
		Result = [&]() -> mu::Ptr<mu::NodeScalar>
		{
			const UEdGraphPin* SwitchParameter = TypedNodeFloatSwitch->SwitchParameter();

			// Check Switch Parameter arity preconditions.
			if (const int32 NumParameters = FollowInputPinArray(*SwitchParameter).Num();
				NumParameters != 1)
			{
				const FText Message = NumParameters
					? LOCTEXT("NoEnumParamInSwitch", "Switch nodes must have an enum switch parameter. Please connect an enum and refesh the switch node.")
					: LOCTEXT("InvalidEnumInSwitch", "Switch nodes must have a single enum with all the options inside. Please remove all the enums but one and refresh the switch node.");

				GenerationContext.Log(Message, Node);
				return nullptr;
			}

			const UEdGraphPin* EnumPin = FollowInputPin(*SwitchParameter);
			mu::NodeScalarPtr SwitchParam = GenerateMutableSourceFloat(EnumPin, GenerationContext);

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

			const int32 NumSwitchOptions = TypedNodeFloatSwitch->GetNumElements();

			mu::NodeScalarEnumParameter* EnumParameter = static_cast<mu::NodeScalarEnumParameter*>(SwitchParam.get());
			if (NumSwitchOptions != EnumParameter->GetValueCount())
			{
				const FText Message = LOCTEXT("MismatchedSwitch", "Switch enum and switch node have different number of options. Please refresh the switch node to make sure the outcomes are labeled properly.");
				GenerationContext.Log(Message, Node);
			}

			mu::Ptr<mu::NodeScalarSwitch> SwitchNode = new mu::NodeScalarSwitch;
			SwitchNode->Parameter = SwitchParam;
			SwitchNode->Options.SetNum(NumSwitchOptions);

			for (int SelectorIndex = 0; SelectorIndex < NumSwitchOptions; ++SelectorIndex)
			{
				if (const UEdGraphPin* const FloatPin = TypedNodeFloatSwitch->GetElementPin(SelectorIndex))
				{
					if (const UEdGraphPin* ConnectedPin = FollowInputPin(*FloatPin))
					{
						SwitchNode->Options[SelectorIndex] = GenerateMutableSourceFloat(ConnectedPin, GenerationContext);
					}
				}
			}

			return SwitchNode;
		}(); // invoke lambda.
	}

	else if (const UCustomizableObjectNodeCurve* TypedNodeCurve = Cast<UCustomizableObjectNodeCurve>(Node))
	{
		mu::Ptr<mu::NodeScalarCurve> CurveNode = new mu::NodeScalarCurve();
		Result = CurveNode;

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeCurve->InputPin()))
		{
			CurveNode->CurveSampleValue = GenerateMutableSourceFloat(ConnectedPin, GenerationContext);
		}

		if (UCurveBase* CurveAsset = TypedNodeCurve->CurveAsset)
		{
			int32 PinIndex = -1;

			for (int32 i = 0; i < TypedNodeCurve->GetNumCurvePins(); ++i)
			{
				if (TypedNodeCurve->CurvePins(i) == Pin)
				{
					PinIndex = i;
					break;
				}
			}

			if (const UCurveLinearColor* const CurveColor = Cast<UCurveLinearColor>(CurveAsset))
			{
				if (PinIndex >= 0 && PinIndex <= 3)
				{
					CurveNode->Curve = CurveColor->FloatCurves[PinIndex];
				}
			}
			else if (const UCurveVector* const CurveVector = Cast<UCurveVector>(CurveAsset))
			{
				if (PinIndex >= 0 && PinIndex <= 2)
				{
					CurveNode->Curve = CurveVector->FloatCurves[PinIndex];
				}
			}
			else if (const UCurveFloat* const CurveFloat = Cast<UCurveFloat>(CurveAsset))
			{
				if (PinIndex == 0)
				{
					CurveNode->Curve = CurveFloat->FloatCurve;
				}
			}
		}
	}

	else if (const UCustomizableObjectNodeFloatVariation* TypedNodeFloatVar = Cast<const UCustomizableObjectNodeFloatVariation>(Node))
	{
		mu::NodeScalarVariationPtr FloatNode = new mu::NodeScalarVariation();
		Result = FloatNode;

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeFloatVar->DefaultPin()))
		{
			mu::NodeScalarPtr ChildNode = GenerateMutableSourceFloat(ConnectedPin, GenerationContext);
			if (ChildNode)
			{
				FloatNode->SetDefaultScalar(ChildNode.get());
			}
			else
			{
				GenerationContext.Log(LOCTEXT("FloatFailed", "Float generation failed."), Node);
			}
		}

		const int32 NumVariations = TypedNodeFloatVar->GetNumVariations();
		FloatNode->SetVariationCount(NumVariations);
		for (int VariationIndex = 0; VariationIndex < NumVariations; ++VariationIndex)
		{
			UEdGraphPin* VariationPin = TypedNodeFloatVar->VariationPin(VariationIndex);
			if (!VariationPin) continue;

			FloatNode->SetVariationTag(VariationIndex, TypedNodeFloatVar->GetVariation(VariationIndex).Tag);
			if (const UEdGraphPin* ConnectedPin = FollowInputPin(*VariationPin))
			{
				mu::NodeScalarPtr ChildNode = GenerateMutableSourceFloat(ConnectedPin, GenerationContext);
				FloatNode->SetVariationScalar(VariationIndex, ChildNode.get());
			}
		}
	}

	else if (const UCustomizableObjectNodeFloatArithmeticOp* TypedNodeFloatArith = Cast<UCustomizableObjectNodeFloatArithmeticOp>(Node))
	{
		mu::NodeScalarArithmeticOperationPtr OpNode = new mu::NodeScalarArithmeticOperation();
		Result = OpNode;

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeFloatArith->XPin()))
		{
			mu::NodeScalarPtr XNode = GenerateMutableSourceFloat(ConnectedPin, GenerationContext);
			OpNode->SetA(XNode);
		}

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeFloatArith->YPin()))
		{
			mu::NodeScalarPtr YNode = GenerateMutableSourceFloat(ConnectedPin, GenerationContext);
			OpNode->SetB(YNode);
		}

		switch (TypedNodeFloatArith->Operation)
		{
		case EFloatArithmeticOperation::E_Add:
			OpNode->SetOperation(mu::NodeScalarArithmeticOperation::OPERATION::AO_ADD);
			break;

		case EFloatArithmeticOperation::E_Sub:
			OpNode->SetOperation(mu::NodeScalarArithmeticOperation::OPERATION::AO_SUBTRACT);
			break;

		case EFloatArithmeticOperation::E_Mul:
			OpNode->SetOperation(mu::NodeScalarArithmeticOperation::OPERATION::AO_MULTIPLY);
			break;

		case EFloatArithmeticOperation::E_Div:
			OpNode->SetOperation(mu::NodeScalarArithmeticOperation::OPERATION::AO_DIVIDE);
			break;

		default:
			unimplemented();
		}
	}

	else if (const UCustomizableObjectNodeTable* TypedNodeTable = Cast<UCustomizableObjectNodeTable>(Node))
	{
		//This node will add a default value in case of error
		mu::NodeScalarConstantPtr ConstantValue = new mu::NodeScalarConstant();
		ConstantValue->SetValue(1.0f);

		Result = ConstantValue;

		if (Pin->PinType.PinCategory == Schema->PC_MaterialAsset)
		{
			// Material pins have to skip the cache of nodes or they will return always the same column node
			bDoNotAddToGeneratedCache = true;
		}

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
					mu::NodeScalarTablePtr ScalarTableNode = new mu::NodeScalarTable();

					if (Pin->PinType.PinCategory == Schema->PC_MaterialAsset)
					{
						// Materials use the parameter id as column names
						ColumnName = GenerationContext.CurrentMaterialTableParameterId;
					}

					// Generating a new Float column if not exists
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
						Result = ScalarTableNode;

						ScalarTableNode->Table = Table;
						ScalarTableNode->SetColumn(ColumnName);
						ScalarTableNode->SetParameterName(TypedNodeTable->ParameterName);
						ScalarTableNode->SetNoneOption(TypedNodeTable->bAddNoneOption);
						ScalarTableNode->SetDefaultRowName(TypedNodeTable->DefaultRowName.ToString());
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
			GenerationContext.Log(LOCTEXT("ScalarTableError", "Couldn't find the data table of the node."), Node);
		}
	}

	else
	{
		GenerationContext.Log(LOCTEXT("UnimplementedNode", "Node type not implemented yet."), Node);
	}

	if (!bDoNotAddToGeneratedCache)
	{
		GenerationContext.Generated.Add(Key, FGeneratedData(Node, Result));
		GenerationContext.GeneratedNodes.Add(Node);
	}

	if (Result)
	{
		Result->SetMessageContext(Node);
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE

