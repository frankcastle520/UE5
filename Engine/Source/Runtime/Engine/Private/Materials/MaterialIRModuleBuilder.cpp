// Copyright Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialIRModuleBuilder.h"

#if WITH_EDITOR

#include "Materials/MaterialIRModule.h"
#include "Materials/MaterialIRDebug.h"
#include "Materials/MaterialIR.h"
#include "Materials/MaterialIRTypes.h"
#include "Materials/MaterialIREmitter.h"
#include "Materials/MaterialIRInternal.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/Material.h"
#include "MaterialExpressionIO.h"
#include "MaterialShared.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialInsights.h"
#include "Async/ParallelFor.h"
#include "Engine/Texture.h"
namespace MIR = UE::MIR;

struct FAnalysisContext
{
	UMaterialExpressionMaterialFunctionCall* Call{};
	TSet<UMaterialExpression*> BuiltExpressions{};
	TArray<UMaterialExpression*> ExpressionStack{};
	TMap<const FExpressionInput*, UE::MIR::FValue*> InputValues;
	TMap<const FExpressionOutput*, UE::MIR::FValue*> OutputValues;

	MIR::FValue* GetInputValue(const FExpressionInput* Input)
	{
		MIR::FValue** Value = InputValues.Find(Input);
		return Value ? *Value : nullptr;
	}

	void SetInputValue(const FExpressionInput* Input, MIR::FValue* Value)
	{
		InputValues.Add(Input, Value);
	}

	MIR::FValue* GetOutputValue(const FExpressionOutput* Output)
	{
		MIR::FValue** Value = OutputValues.Find(Output);
		return Value ? *Value : nullptr;
	}
	
	void SetOutputValue(const FExpressionOutput* Output, MIR::FValue* Value)
	{
		OutputValues.Add(Output, Value);
	}
};

struct FMaterialIRModuleBuilderImpl
{
	FMaterialIRModuleBuilder* Builder;
	FMaterialIRModule* Module;
	MIR::FEmitter* Emitter;
	TArray<FAnalysisContext> AnalysisContextStack;
	TArray<MIR::FInstruction*> InstructionStack{};

	void Step_Initialize()
	{
		Module->Empty();
		Module->ShaderPlatform = Builder->ShaderPlatform;
		
		Emitter->Initialize();
		AnalysisContextStack.Emplace();
	}

	void Step_GenerateOutputInstructions()
	{
		// Prepare the array of FSetMaterialOutputInstr outputs from the material attributes inputs.
		FMaterialInputDescription Input;
		for (int32 Index = 0; UE::MIR::Internal::NextMaterialAttributeInput(Builder->Material, Index, Input); ++Index)
		{
			EMaterialProperty Property = (EMaterialProperty)Index;

			MIR::FSetMaterialOutput* Output = Emitter->EmitSetMaterialOutput(Property, nullptr);

			if (Input.bUseConstant)
			{
				Output->Arg = Emitter->EmitConstantFromShaderValue(Input.ConstantValue);
			}
			else if (!Input.Input->IsConnected())
			{
				Output->Arg = UE::MIR::Internal::CreateMaterialAttributeDefaultValue(*Emitter, Builder->Material, Property);
			}
			else
			{
				AnalysisContextStack.Last().ExpressionStack.Add(Input.Input->Expression);
			}
		}
	}

	void Step_BuildMaterialExpressionsToIRGraph()
	{
		while (true)
		{
			FAnalysisContext& Context = AnalysisContextStack.Last();

			if (!Context.ExpressionStack.IsEmpty())
			{
				// Some expression is on the expression stack of this context. Analyze it. This will
				// have the effect of either building the expression or pushing its other expression
				// dependencies onto the stack.
				BuildTopMaterialExpression();
			}
			else if (Context.Call)
			{
				// There are no more expressions to analyze on the stack, this analysis context is complete.
				// Context.Call isn't null so this context is for a function call, which has now been fully analyzed.
				// Pop the callee context from the stack and resume analyzing the parent context (the caller).
				PopFunctionCall();
			}
			else
			{
				// No other expressions on the stack to evaluate, nor this is a function
				// call context but the root context. Nothing left to do so simply quit.
				break;
			}
		}
	}

	void BuildTopMaterialExpression()
	{
		FAnalysisContext& CurrContext = AnalysisContextStack.Last();
		Emitter->Expression = CurrContext.ExpressionStack.Last();

		// If expression is clean, nothing to be done.
		if (CurrContext.BuiltExpressions.Contains(Emitter->Expression))
		{
			CurrContext.ExpressionStack.Pop(EAllowShrinking::No);
			return;
		}

		// Push to the expression stack all dependencies that still need to be analyzed.
		for (FExpressionInputIterator It{ Emitter->Expression }; It; ++It)
		{
			// Ignore disconnected inputs and connected expressions already built.
			if (!It->IsConnected() || CurrContext.BuiltExpressions.Contains(It->Expression))
			{
				continue;
			}

			CurrContext.ExpressionStack.Push(It->Expression);
		}

		// If on top of the stack there's a different expression, we have a dependency to analyze first.
		if (CurrContext.ExpressionStack.Last() != Emitter->Expression) {
			return;
		}

		// Take the top expression out of the stack as ready for analysis. Also mark it as built.
		CurrContext.ExpressionStack.Pop();
		CurrContext.BuiltExpressions.Add(Emitter->Expression);

		// Flow the value into this expression's inputs from their connected outputs.
		for (FExpressionInputIterator It{ Emitter->Expression}; It; ++It)
		{
			FExpressionOutput* ConnectedOutput = It->GetConnectedOutput();
			if (!ConnectedOutput)
			{
				break;
			}

			// Fetch the value flowing through connected output.
			if (MIR::FValue** ValuePtr = CurrContext.OutputValues.Find(ConnectedOutput))
			{
				// ...and flow it into this input.
				CurrContext.InputValues.Add(It.Input, *ValuePtr);
			}
		}

		if (auto Call = Cast<UMaterialExpressionMaterialFunctionCall>(Emitter->Expression))
		{
			// Function calls are handled internally as they manipulate the analysis context stack.
			PushFunctionCall(Call);
		}
		else if (!Cast<UMaterialExpressionFunctionOutput>(Emitter->Expression))
		{
			// Ignore UMaterialExpressionFunctionOutputs as they're handled by PopFunctionCall().

			// Invoke the expression build function. This will perform semantic analysis, error reporting and
			// emit IR values for its outputs (which will flow into connected expressions inputs).
			Emitter->Expression->Build(*Emitter);

			// Populate the insight information about this expression pins.
			AddExpressionConnectionInsights(Emitter->Expression);
		}
	}

	void PushFunctionCall(UMaterialExpressionMaterialFunctionCall* Call)
	{
		FMemMark Mark(FMemStack::Get());
		TArrayView<MIR::FValue*> CallInputValues = MakeTemporaryArray<MIR::FValue*>(Mark, Call->FunctionInputs.Num());

		// Make sure each function input is connected and has a value. If so, cache the values flowing into this
		// funcion call inside the auxiliary value array.
		for (int i = 0; i < Call->FunctionInputs.Num(); ++i)
		{
			FFunctionExpressionInput& FunctionInput = Call->FunctionInputs[i];
			MIR::FValue* Value = Emitter->Get(Call->GetInput(i));
			if (Value)
			{
				MIR::FTypePtr Type = MIR::FType::FromMaterialValueType((EMaterialValueType)FunctionInput.ExpressionInput->GetInputType(0));
				CallInputValues[i] = Emitter->EmitConstruct(Type, Value);
			}
		}

		// If some error occurred (e.g. some function input wasn't linked in) early out.
		if (Emitter->IsInvalid())
		{
			return;
		}

		// Push a new analysis context on the stack dedicated to this function call.
		AnalysisContextStack.Emplace();
		FAnalysisContext& ParentContext = AnalysisContextStack[AnalysisContextStack.Num() - 2];
		FAnalysisContext& NewContext = AnalysisContextStack[AnalysisContextStack.Num() - 1];

		// Set the function call. When the expressions stack in this new context is empty, this
		// will be used to wire all values flowing inside the function outputs to the function call outputs.
		NewContext.Call = Call;

		// Forward values flowing into call inputs to called function inputs
		for (int i = 0; i < Call->FunctionInputs.Num(); ++i)
		{
			FFunctionExpressionInput& FunctionInput = Call->FunctionInputs[i];

			// Bind the value flowing into the function call input to the function input
			// expression (inside the function) in the new context.
			NewContext.SetOutputValue(FunctionInput.ExpressionInput->GetOutput(0), CallInputValues[i]);

			// Mark the function input as built.
			NewContext.BuiltExpressions.Add(FunctionInput.ExpressionInput.Get());
		}

		// Finally push the function outputs to the expression evaluation stack in the new context.
		for (FFunctionExpressionOutput& FunctionOutput : Call->FunctionOutputs)
		{
			NewContext.ExpressionStack.Push(FunctionOutput.ExpressionOutput.Get());
		}
	}

	void PopFunctionCall()
	{
		// Pull the values flowing into the function outputs out of the current
		// context and flow them into the Call outputs in the parent context so that
		// analysis can continue from the call expression.
		FAnalysisContext& ParentContext = AnalysisContextStack[AnalysisContextStack.Num() - 2];
		FAnalysisContext& CurrContext = AnalysisContextStack[AnalysisContextStack.Num() - 1];
		UMaterialExpressionMaterialFunctionCall* Call = CurrContext.Call;

		for (int i = 0; i < Call->FunctionOutputs.Num(); ++i)
		{
			FFunctionExpressionOutput& FunctionOutput = Call->FunctionOutputs[i];

			// Get the value flowing into the function output inside the function in the current context.
			MIR::FValue* Value = Emitter->Get(FunctionOutput.ExpressionOutput->GetInput(0));

			// Get the function output type.
			MIR::FTypePtr OutputType = MIR::FType::FromMaterialValueType((EMaterialValueType)FunctionOutput.ExpressionOutput->GetOutputType(0));

			// Cast the value to the expected output type. This may fail (value will be null). 
			Value = Emitter->EmitConstruct(OutputType, Value);

			// And flow it to the relative function *call* output in the parent context.
			ParentContext.SetOutputValue(Call->GetOutput(i), Value);
		}

		// Finally pop this context (the function call) to return to the caller.
		AnalysisContextStack.Pop();

		// Populate the insight information about this expression pins.
		AddExpressionConnectionInsights(Call);
	}

	void Step_FlowValuesIntoMaterialOutputs()
	{
		FAnalysisContext& Context = AnalysisContextStack.Last();

		for (MIR::FSetMaterialOutput* Output : Module->Outputs)
		{
			FMaterialInputDescription Input;
			ensure(Builder->Material->GetExpressionInputDescription(Output->Property, Input));

			if (!Output->Arg)
			{
				MIR::FValue** ValuePtr = Context.OutputValues.Find(Input.Input->GetConnectedOutput());
				check(ValuePtr && *ValuePtr);

				MIR::Internal::SetInputValue(this, Input.Input, *ValuePtr);

				MIR::FTypePtr OutputArgType = MIR::FType::FromShaderType(Input.Type);
				Output->Arg = Emitter->EmitConstruct(OutputArgType, *ValuePtr);
			}

			if (Builder->TargetInsight)
			{
				check(Output->Arg);
				PushConnectionInsight(Builder->Material, (int)Output->Property, Input.Input->Expression, Input.Input->OutputIndex, Output->Arg->Type);
			}
		}
	}

	void Step_AnalyzeIRGraph()
	{
		InstructionStack.Reserve(64);

		for (MIR::FSetMaterialOutput* Output : Module->Outputs)
		{
			InstructionStack.Push(Output);
		}

		while (!InstructionStack.IsEmpty())
		{
			MIR::FValue* Instr = InstructionStack.Pop();
			for (MIR::FValue* UseValue : Instr->GetUses())
			{
				if (UseValue && !(UseValue->Flags & MIR::VF_ValueAnalyzed))
				{
					UseValue->SetFlags(MIR::VF_ValueAnalyzed);
					AnalyzeValue(UseValue);
				}

				MIR::FInstruction* Use = UseValue->AsInstruction();
				if (!Use)
				{
					continue;
				}

				Use->NumUsers += 1;

				if (!(Use->Flags & MIR::VF_InstructionAnalyzed))
				{
					Use->SetFlags(MIR::VF_InstructionAnalyzed);
					InstructionStack.Push(Use);
				}
			}
		}
	}

	void AnalyzeValue(MIR::FValue* Value)
	{
		if (auto ExternalInput = Value->As<MIR::FExternalInput>())
		{
			Module->Statistics.ExternalInputUsedMask[SF_Vertex][(int)ExternalInput->Id] = true;
			Module->Statistics.ExternalInputUsedMask[SF_Pixel][(int)ExternalInput->Id] = true;
		}
		else if (auto TextureSample = Value->As<MIR::FTextureSample>())
		{
			EMaterialTextureParameterType ParamType = MIR::Internal::TextureMaterialValueTypeToParameterType(TextureSample->Texture->GetMaterialType());

			FMaterialTextureParameterInfo ParamInfo{};
			ParamInfo.ParameterInfo = { "", EMaterialParameterAssociation::GlobalParameter, INDEX_NONE };
			ParamInfo.SamplerSource = SSM_FromTextureAsset; // TODO - Is this needed?

			ParamInfo.TextureIndex = Builder->Material->GetReferencedTextures().Find(TextureSample->Texture);
			check(ParamInfo.TextureIndex != INDEX_NONE);

			TextureSample->TextureParameterIndex = Module->CompilationOutput.UniformExpressionSet.FindOrAddTextureParameter(ParamType, ParamInfo);
		}
	}

	void Step_PopulateBlocks()
	{
		// This function walks the instruction graph and puts each instruction into the inner most possible block.
		InstructionStack.Empty(InstructionStack.Max());

		for (MIR::FSetMaterialOutput* Output : Module->Outputs)
		{
			Output->Block = Module->RootBlock;
			InstructionStack.Add(Output);
		}

		while (!InstructionStack.IsEmpty())
		{
			MIR::FInstruction* Instr = InstructionStack.Pop();
			if (MIR::FSetMaterialOutput* Output = Instr->As<MIR::FSetMaterialOutput>())
			{
				if (Output->Property == EMaterialProperty::MP_BaseColor)
				{
					static int l = 0;
					++l;
				}
			}

			// Push the instruction to its block in reverse order (push front)
			Instr->Next = Instr->Block->Instructions;
			Instr->Block->Instructions = Instr;

			TArrayView<MIR::FValue*> Uses = Instr->GetUses();
			for (int32 UseIndex = 0; UseIndex < Uses.Num(); ++UseIndex)
			{
				MIR::FValue* Use = Uses[UseIndex];
				MIR::FInstruction* UseInstr = Use->AsInstruction();
				if (!UseInstr)
				{
					continue;
				}

				// Get the block into which the dependency instruction should go.
				MIR::FBlock* TargetBlock = Instr->GetDesiredBlockForUse(UseIndex);

				// Update dependency's block to be a child of current instruction's block.
				if (TargetBlock != Instr->Block)
				{
					TargetBlock->Parent = Instr->Block;
					TargetBlock->Level = Instr->Block->Level + 1;
				}

				// Set the dependency's block to the common block betwen its current block and this one.
				UseInstr->Block = UseInstr->Block
					? UseInstr->Block->FindCommonParentWith(TargetBlock)
					: TargetBlock;

				// Increase the number of times this dependency instruction has been considered.
				// When all of its users have processed, we can carry on visiting this instruction.
				++UseInstr->NumProcessedUsers;
				check(UseInstr->NumProcessedUsers <= UseInstr->NumUsers);

				// If all dependants have been processed, we can carry the processing from this dependency.
				if (UseInstr->NumProcessedUsers == UseInstr->NumUsers)
				{
					InstructionStack.Push(UseInstr);
				}
			}
		}
	}

	void Step_Finalize()
	{
		/* Produce the module statistics */
		for (int TexCoordIndex = 0; TexCoordIndex < MIR::TexCoordMaxNum; ++TexCoordIndex)
		{
			MIR::EExternalInput TexCoordInput = MIR::TexCoordIndexToExternalInput(TexCoordIndex);
			if (Module->Statistics.ExternalInputUsedMask[SF_Vertex][(int)TexCoordInput])
			{
				Module->Statistics.NumVertexTexCoords = TexCoordIndex + 1;
			}
			if (Module->Statistics.ExternalInputUsedMask[SF_Pixel][(int)TexCoordInput])
			{
				Module->Statistics.NumPixelTexCoords = TexCoordIndex + 1;
			}
		}

		/* Configure the compilation output */
		FMaterialCompilationOutput& CompilationOutput = Module->CompilationOutput;
		CompilationOutput.NumUsedUVScalars = Module->Statistics.NumPixelTexCoords * 2;
	}

	void AddExpressionConnectionInsights(UMaterialExpression* Expression)
	{
		if (!Builder->TargetInsight)
		{
			return;
		}

		// Update expression inputs insight.
		for (FExpressionInputIterator It{ Expression }; It; ++It)
		{
			if (!It->IsConnected())
			{
				continue;
			}

			MIR::FValue* Value = MIR::Internal::GetInputValue(this, It.Input);
			PushConnectionInsight(Expression, It.Index, It->Expression, It->OutputIndex, Value ? Value->Type : nullptr);
		}
	}
	
	void PushConnectionInsight(const UObject* InputObject, int InputIndex, const UMaterialExpression* OutputExpression, int OutputIndex, MIR::FTypePtr Type)
	{
		FMaterialInsights::FConnectionInsight Insight;
		Insight.InputObject = InputObject,
		Insight.OutputExpression = OutputExpression,
		Insight.InputIndex = InputIndex,
		Insight.OutputIndex = OutputIndex,
		Insight.ValueType = Type ? Type->ToValueType() : UE::Shader::EValueType::Any,
		
		Builder->TargetInsight->ConnectionInsights.Push(Insight);
	}
};

bool FMaterialIRModuleBuilder::Build(FMaterialIRModule* TargetModule)
{
	FMaterialIRModuleBuilderImpl Impl{ this, TargetModule };
	
	MIR::FEmitter Emitter{ &Impl, Material, TargetModule };
	Impl.Emitter = &Emitter;

	Impl.Step_Initialize();
	Impl.Step_GenerateOutputInstructions();
	Impl.Step_BuildMaterialExpressionsToIRGraph();

	if (Impl.Emitter->IsInvalid())
	{
		return false;
	}

	Impl.Step_FlowValuesIntoMaterialOutputs();
	Impl.Step_AnalyzeIRGraph();
	Impl.Step_PopulateBlocks();
	Impl.Step_Finalize();

	UE::MIR::DebugDumpIRUseGraph(*TargetModule);

	return true;
}

namespace UE::MIR::Internal {

FValue* GetInputValue(FMaterialIRModuleBuilderImpl* Builder, const FExpressionInput* Input)
{
	return Builder->AnalysisContextStack.Last().GetInputValue(Input);
}

void SetInputValue(FMaterialIRModuleBuilderImpl* Builder, const FExpressionInput* Input, FValue* Value)
{
	Builder->AnalysisContextStack.Last().SetInputValue(Input, Value);
}

void SetOutputValue(FMaterialIRModuleBuilderImpl* Builder, const FExpressionOutput* Output, FValue* Value)
{
	Builder->AnalysisContextStack.Last().SetOutputValue(Output, Value);
}

} // namespace UE::MIR::Internal

#endif // #if WITH_EDITOR
