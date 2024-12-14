// Copyright Epic Games, Inc. All Rights Reserved.

#include "HAL/PlatformMath.h"
#include "Misc/AssertionMacros.h"
#include "MuR/Image.h"
#include "MuR/MutableMath.h"
#include "MuR/MutableTrace.h"
#include "MuR/Operations.h"
#include "MuR/Parameters.h"
#include "MuR/ParametersPrivate.h"
#include "MuR/Platform.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuR/Types.h"
#include "MuT/AST.h"
#include "MuT/ASTOpConditional.h"
#include "MuT/ASTOpParameter.h"
#include "MuT/ASTOpSwitch.h"
#include "MuT/CodeGenerator.h"
#include "MuT/CodeGenerator_FirstPass.h"
#include "MuT/ErrorLog.h"
#include "MuT/ErrorLogPrivate.h"
#include "MuT/Node.h"
#include "MuT/NodeColour.h"
#include "MuT/NodeColourArithmeticOperation.h"
#include "MuT/NodeColourConstant.h"
#include "MuT/NodeColourFromScalars.h"
#include "MuT/NodeColourParameter.h"
#include "MuT/NodeColourSampleImage.h"
#include "MuT/NodeColourSwitch.h"
#include "MuT/NodeColourTable.h"
#include "MuT/NodeColourVariation.h"
#include "MuT/NodeImage.h"
#include "MuT/NodeImageProject.h"
#include "MuT/NodeMeshGeometryOperation.h"
#include "MuT/NodeMeshTable.h"
#include "MuT/NodeRange.h"
#include "MuT/NodeScalar.h"
#include "MuT/NodeScalarConstant.h"
#include "MuT/Table.h"
#include "MuT/TablePrivate.h"


namespace mu
{


	//-------------------------------------------------------------------------------------------------
	void CodeGenerator::GenerateColor(FColorGenerationResult& Result, const FGenericGenerationOptions& Options, const Ptr<const NodeColour>& Untyped)
	{
		if (!Untyped)
		{
			Result = FColorGenerationResult();
			return;
		}

		// See if it was already generated
		FGeneratedCacheKey Key;
		Key.Node = Untyped;
		Key.Options = Options;
		FGeneratedColorsMap::ValueType* it = GeneratedColors.Find(Key);
		if (it)
		{
			Result = *it;
			return;
		}

		// Generate for each different type of node
		if (Untyped->GetType()==NodeColourConstant::GetStaticType())
		{
			const NodeColourConstant* Constant = static_cast<const NodeColourConstant*>(Untyped.get());
			GenerateColor_Constant(Result, Options, Constant);
		}
		else if (Untyped->GetType() == NodeColourParameter::GetStaticType())
		{
			const NodeColourParameter* Param = static_cast<const NodeColourParameter*>(Untyped.get());
			GenerateColor_Parameter(Result, Options, Param);
		}
		else if (Untyped->GetType() == NodeColourSwitch::GetStaticType())
		{
			const NodeColourSwitch* Switch = static_cast<const NodeColourSwitch*>(Untyped.get());
			GenerateColor_Switch(Result, Options, Switch);
		}
		else if (Untyped->GetType() == NodeColourSampleImage::GetStaticType())
		{
			const NodeColourSampleImage* Sample = static_cast<const NodeColourSampleImage*>(Untyped.get());
			GenerateColor_SampleImage(Result, Options, Sample);
		}
		else if (Untyped->GetType() == NodeColourFromScalars::GetStaticType())
		{
			const NodeColourFromScalars* From = static_cast<const NodeColourFromScalars*>(Untyped.get());
			GenerateColor_FromScalars(Result, Options, From);
		}
		else if (Untyped->GetType() == NodeColourArithmeticOperation::GetStaticType())
		{
			const NodeColourArithmeticOperation* Arithmetic = static_cast<const NodeColourArithmeticOperation*>(Untyped.get());
			GenerateColor_Arithmetic(Result, Options, Arithmetic);
		}
		else if (Untyped->GetType() == NodeColourVariation::GetStaticType())
		{
			const NodeColourVariation* Variation = static_cast<const NodeColourVariation*>(Untyped.get());
			GenerateColor_Variation(Result, Options, Variation);
		}
		else if (Untyped->GetType() == NodeColourTable::GetStaticType())
		{
			const NodeColourTable* Table = static_cast<const NodeColourTable*>(Untyped.get());
			GenerateColor_Table(Result, Options, Table);
		}
		else
		{
			check(false);
		}

		// Cache the result
		GeneratedColors.Add(Key, Result);
	}


	//-------------------------------------------------------------------------------------------------
	void CodeGenerator::GenerateColor_Constant(FColorGenerationResult& result, const FGenericGenerationOptions& Options, const Ptr<const NodeColourConstant>& Typed)
	{
		const NodeColourConstant& node = *Typed;

		Ptr<ASTOpFixed> op = new ASTOpFixed();
		op->op.type = OP_TYPE::CO_CONSTANT;
		op->op.args.ColourConstant.value[0] = node.Value.X;
		op->op.args.ColourConstant.value[1] = node.Value.Y;
		op->op.args.ColourConstant.value[2] = node.Value.Z;
		op->op.args.ColourConstant.value[3] = node.Value.W;

		result.op = op;
	}


	//-------------------------------------------------------------------------------------------------
	void CodeGenerator::GenerateColor_Parameter(FColorGenerationResult& result, const FGenericGenerationOptions& Options, const Ptr<const NodeColourParameter>& Typed)
	{
		const NodeColourParameter& node = *Typed;

		Ptr<ASTOpParameter> op;

		Ptr<ASTOpParameter>* it = FirstPass.ParameterNodes.Find(Typed);

		if (!it)
		{
			FParameterDesc param;
			param.m_name = node.Name;
			const TCHAR* CStr = ToCStr(node.Uid);
			param.m_uid.ImportTextItem(CStr, 0, nullptr, nullptr);
			param.m_type = PARAMETER_TYPE::T_COLOUR;

			ParamColorType Value;
			Value[0] =  node.DefaultValue[0];
			Value[1] = node.DefaultValue[1];
			Value[2] = node.DefaultValue[2];
			Value[3] = node.DefaultValue[3];

			param.m_defaultValue.Set<ParamColorType>(Value);
			
			op = new ASTOpParameter();
			op->type = OP_TYPE::CO_PARAMETER;
			op->parameter = param;

			// Generate the code for the ranges
			for (int32 a = 0; a < node.Ranges.Num(); ++a)
			{
				FRangeGenerationResult rangeResult;
				GenerateRange(rangeResult, Options, node.Ranges[a]);
				op->ranges.Emplace(op.get(), rangeResult.sizeOp, rangeResult.rangeName, rangeResult.rangeUID);
			}

			FirstPass.ParameterNodes.Add(Typed, op);
		}
		else
		{
			op = *it;
		}

		result.op = op;
	}


	//-------------------------------------------------------------------------------------------------
	void CodeGenerator::GenerateColor_Switch(FColorGenerationResult& result, const FGenericGenerationOptions& Options, const Ptr<const NodeColourSwitch>& Typed)
	{
		const NodeColourSwitch& node = *Typed;

		MUTABLE_CPUPROFILER_SCOPE(NodeColourSwitch);

		if (node.Options.Num() == 0)
		{
			// No options in the switch!
			Ptr<ASTOp> missingOp = GenerateMissingColourCode(TEXT("Switch option"), Typed->GetMessageContext());
			result.op = missingOp;
			return;
		}

		Ptr<ASTOpSwitch> op = new ASTOpSwitch();
		op->type = OP_TYPE::CO_SWITCH;

		// Variable value
		if (node.Parameter)
		{
			op->variable = Generate_Generic(node.Parameter.get(), Options);
		}
		else
		{
			// This argument is required
			op->variable = GenerateMissingScalarCode(TEXT("Switch variable"), 0.0f, Typed->GetMessageContext());
		}

		// Options
		for (int32 t = 0; t < node.Options.Num(); ++t)
		{
			Ptr<ASTOp> branch;
			if (node.Options[t])
			{
				branch = Generate_Generic(node.Options[t].get(), Options);
			}
			else
			{
				// This argument is required
				branch = GenerateMissingColourCode(TEXT("Switch option"), Typed->GetMessageContext());
			}
			op->cases.Emplace((int16)t, op, branch);
		}

		result.op = op;
	}


	//-------------------------------------------------------------------------------------------------
	void CodeGenerator::GenerateColor_Variation(FColorGenerationResult& result, const FGenericGenerationOptions& Options, const Ptr<const NodeColourVariation>& Typed)
	{
		const NodeColourVariation& node = *Typed;

		Ptr<ASTOp> currentOp;

		// Default case
		if (node.DefaultColour)
		{
			FColorGenerationResult BranchResults;
			GenerateColor(BranchResults, Options, node.DefaultColour);
			currentOp = BranchResults.op;
		}

		// Process variations in reverse order, since conditionals are built bottom-up.
		for (int32 t = node.Variations.Num() - 1; t >= 0; --t)
		{
			int32 tagIndex = -1;
			const FString& tag = node.Variations[t].Tag;
			for (int32 i = 0; i < FirstPass.Tags.Num(); ++i)
			{
				if (FirstPass.Tags[i].Tag == tag)
				{
					tagIndex = i;
				}
			}

			if (tagIndex < 0)
			{
				FString Msg = FString::Printf(TEXT("Unknown tag found in color variation [%s]."), *tag);
				ErrorLog->GetPrivate()->Add(Msg, ELMT_WARNING, Typed->GetMessageContext());
				continue;
			}

			Ptr<ASTOp> variationOp;
			if (node.Variations[t].Colour)
			{
				FColorGenerationResult BranchResults;
				GenerateColor(BranchResults,Options,node.Variations[t].Colour);
				variationOp = BranchResults.op;
			}
			else
			{
				// This argument is required
				variationOp = GenerateMissingColourCode(TEXT("Variation option"), Typed->GetMessageContext());
			}


			Ptr<ASTOpConditional> conditional = new ASTOpConditional;
			conditional->type = OP_TYPE::CO_CONDITIONAL;
			conditional->no = currentOp;
			conditional->yes = variationOp;
			conditional->condition = FirstPass.Tags[tagIndex].GenericCondition;

			currentOp = conditional;
		}

		result.op = currentOp;
	}


	//-------------------------------------------------------------------------------------------------
	void CodeGenerator::GenerateColor_SampleImage(FColorGenerationResult& result, const FGenericGenerationOptions& Options, const Ptr<const NodeColourSampleImage>& Typed)
	{
		const NodeColourSampleImage& node = *Typed;

		// Generate the code
		Ptr<ASTOpFixed> op = new ASTOpFixed();
		op->op.type = OP_TYPE::CO_SAMPLEIMAGE;

		// Source image
		FImageGenerationOptions ImageOptions;
		ImageOptions.State = Options.State;
		ImageOptions.ActiveTags = Options.ActiveTags;

		Ptr<ASTOp> base;
		if (node.Image)
		{
			// Generate
			FImageGenerationResult MapResult;
			GenerateImage(ImageOptions, MapResult, node.Image);
			base = MapResult.op;
		}
		else
		{
			// This argument is required
			base = GenerateMissingImageCode(TEXT("Sample image"), EImageFormat::IF_RGB_UBYTE, Typed->GetMessageContext(), ImageOptions);
		}
		base = GenerateImageFormat(base, EImageFormat::IF_RGB_UBYTE);
		op->SetChild(op->op.args.ColourSampleImage.image, base);

		FScalarGenerationResult ChildResult;

		// X
		if (NodeScalar* pX = node.X.get())
		{
			GenerateScalar(ChildResult, Options, pX);
			op->SetChild(op->op.args.ColourSampleImage.x, ChildResult.op);
		}
		else
		{
			// Set a constant 0.5 value
			NodeScalarConstantPtr pNode = new NodeScalarConstant();
			pNode->SetValue(0.5f);
			GenerateScalar(ChildResult, Options, pNode);
			op->SetChild(op->op.args.ColourSampleImage.x, ChildResult.op);
		}


		// Y
		if (NodeScalar* pY = node.Y.get())
		{
			GenerateScalar(ChildResult, Options, pY);
			op->SetChild(op->op.args.ColourSampleImage.y, ChildResult.op);
		}
		else
		{
			// Set a constant 0.5 value
			NodeScalarConstantPtr pNode = new NodeScalarConstant();
			pNode->SetValue(0.5f);
			GenerateScalar(ChildResult, Options, pNode);
			op->SetChild(op->op.args.ColourSampleImage.y, ChildResult.op);
		}

		// TODO
		op->op.args.ColourSampleImage.filter = 0;

		result.op = op;
	}


	//-------------------------------------------------------------------------------------------------
	void CodeGenerator::GenerateColor_FromScalars(FColorGenerationResult& result, const FGenericGenerationOptions& Options, const Ptr<const NodeColourFromScalars>& Typed)
	{
		const NodeColourFromScalars& node = *Typed;

		Ptr<ASTOpFixed> op = new ASTOpFixed();
		op->op.type = OP_TYPE::CO_FROMSCALARS;

		FScalarGenerationResult ChildResult;

		// X
		if (NodeScalar* pX = node.X.get())
		{
			GenerateScalar(ChildResult, Options, pX);
			op->SetChild(op->op.args.ColourFromScalars.v[0], ChildResult.op );
		}
		else
		{
			NodeScalarConstantPtr pNode = new NodeScalarConstant();
			pNode->SetValue(1.0f);
			GenerateScalar(ChildResult, Options, pNode);
			op->SetChild(op->op.args.ColourFromScalars.v[0], ChildResult.op);
		}

		// Y
		if (NodeScalar* pY = node.Y.get())
		{
			GenerateScalar(ChildResult, Options, pY);
			op->SetChild(op->op.args.ColourFromScalars.v[1], ChildResult.op);
		}
		else
		{
			NodeScalarConstantPtr pNode = new NodeScalarConstant();
			pNode->SetValue(1.0f);
			GenerateScalar(ChildResult, Options, pNode);
			op->SetChild(op->op.args.ColourFromScalars.v[1], ChildResult.op);
		}

		// Z
		if (NodeScalar* pZ = node.Z.get())
		{
			GenerateScalar(ChildResult, Options, pZ);
			op->SetChild(op->op.args.ColourFromScalars.v[2], ChildResult.op);
		}
		else
		{
			NodeScalarConstantPtr pNode = new NodeScalarConstant();
			pNode->SetValue(1.0f);
			GenerateScalar(ChildResult, Options, pNode);
			op->SetChild(op->op.args.ColourFromScalars.v[2], ChildResult.op);
		}

		// W
		if (NodeScalar* pW = node.W.get())
		{
			GenerateScalar(ChildResult, Options, pW);
			op->SetChild(op->op.args.ColourFromScalars.v[3], ChildResult.op);
		}
		else
		{
			NodeScalarConstantPtr pNode = new NodeScalarConstant();
			pNode->SetValue(1.0f);
			GenerateScalar(ChildResult, Options, pNode);
			op->SetChild(op->op.args.ColourFromScalars.v[3], ChildResult.op);
		}

		result.op = op;
	}


	//-------------------------------------------------------------------------------------------------
	void CodeGenerator::GenerateColor_Arithmetic(FColorGenerationResult& result, const FGenericGenerationOptions& Options, const Ptr<const NodeColourArithmeticOperation>& Typed)
	{
		const NodeColourArithmeticOperation& node = *Typed;

		Ptr<ASTOpFixed> op = new ASTOpFixed();
		op->op.type = OP_TYPE::CO_ARITHMETIC;

		switch (node.Operation)
		{
		case NodeColourArithmeticOperation::EOperation::Add: op->op.args.ColourArithmetic.operation = OP::ArithmeticArgs::ADD; break;
		case NodeColourArithmeticOperation::EOperation::Subtract: op->op.args.ColourArithmetic.operation = OP::ArithmeticArgs::SUBTRACT; break;
		case NodeColourArithmeticOperation::EOperation::Multiply: op->op.args.ColourArithmetic.operation = OP::ArithmeticArgs::MULTIPLY; break;
		case NodeColourArithmeticOperation::EOperation::Divide: op->op.args.ColourArithmetic.operation = OP::ArithmeticArgs::DIVIDE; break;
		default:
			checkf(false, TEXT("Unknown arithmetic operation."));
			op->op.args.ColourArithmetic.operation = OP::ArithmeticArgs::NONE;
			break;
		}

		FColorGenerationResult ChildResult;

		// A
		if (NodeColour* pA = node.A.get())
		{
			GenerateColor(ChildResult, Options, pA );
			op->SetChild(op->op.args.ColourArithmetic.a, ChildResult.op);
		}
		else
		{
			op->SetChild(op->op.args.ColourArithmetic.a,
				CodeGenerator::GenerateMissingColourCode(TEXT("ColourArithmetic A"), Typed->GetMessageContext()));
		}

		// B
		if (NodeColour* pB = node.B.get())
		{
			GenerateColor(ChildResult, Options, pB);
			op->SetChild(op->op.args.ColourArithmetic.b, ChildResult.op);
		}
		else
		{
			op->SetChild(op->op.args.ColourArithmetic.b,
				CodeGenerator::GenerateMissingColourCode(TEXT("ColourArithmetic B"), Typed->GetMessageContext()));
		}

		result.op = op;
	}


	//-------------------------------------------------------------------------------------------------
	void CodeGenerator::GenerateColor_Table(FColorGenerationResult& result, const FGenericGenerationOptions& Options, const Ptr<const NodeColourTable>& Typed)
	{
		const NodeColourTable& node = *Typed;

		result.op = GenerateTableSwitch<NodeColourTable, ETableColumnType::Color, OP_TYPE::CO_SWITCH>(node,
			[this, &Options](const NodeColourTable& node, int32 colIndex, int32 row, mu::ErrorLog* pErrorLog)
			{
				Ptr<NodeColourConstant> CellData = new NodeColourConstant();
				FVector4f Colour = node.Table->GetPrivate()->Rows[row].Values[colIndex].Color;
				CellData->Value = Colour;
				FColorGenerationResult BranchResults;
				GenerateColor(BranchResults, Options, CellData );
				return BranchResults.op;
			});
	}


	//---------------------------------------------------------------------------------------------
	mu::Ptr<ASTOp> CodeGenerator::GenerateMissingColourCode(const TCHAR* strWhere, const void* errorContext)
	{
		// Log a warning
		FString Msg = FString::Printf(TEXT("Required connection not found: %s"), strWhere);
		ErrorLog->GetPrivate()->Add(Msg, ELMT_ERROR, errorContext);

		// Create a constant colour node
		Ptr<NodeColourConstant> pNode = new NodeColourConstant();
		pNode->Value = FVector4f(1, 1, 0, 1);

		FColorGenerationResult Result;
		FGenericGenerationOptions Options;
		GenerateColor(Result, Options, pNode);

		return Result.op;
	}


}
