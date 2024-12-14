// Copyright Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialIRDebug.h"
#include "Materials/MaterialIRModule.h"
#include "Materials/MaterialIR.h"
#include "Materials/MaterialIRTypes.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

#if WITH_EDITOR

static TAutoConsoleVariable<bool> CVarDumpMaterialIRUseGraph(
	TEXT("r.Material.Translator.DumpUseGraph"),
	true,
	TEXT("Whether the material translator should emit the Module IR 'Uses' graph in Graphviz Dot syntax (to 'MaterialIRDumpGraph.dot')."),
	ECVF_RenderThreadSafe);


static TAutoConsoleVariable<bool> CVarDumpMaterialIRUseGraph_EnableNext(
	TEXT("r.Material.Translator.DumpUseGraphOpts.EnableSuccessors"),
	false,
	TEXT("Whether the Material Module IR 'Uses' graph should also display 'Instruction Next' edges."),
	ECVF_RenderThreadSafe);


namespace UE::MIR
{

static void DumpValueInfo(const MIR::FValue* Value, FString& Out)
{
	if (auto Constant = Value->As<MIR::FConstant>())
	{
		switch (Constant->Type->AsPrimitive()->ScalarKind)
		{
			case MIR::SK_Bool:	Out.Append(Constant->Boolean ? TEXT("true") : TEXT("false")); break;
			case MIR::SK_Int:	Out.Appendf(TEXT("%lld"), Constant->Integer); break;
			case MIR::SK_Float: Out.Appendf(TEXT("%f"), Constant->Float); break;
			default: UE_MIR_UNREACHABLE();
		}
	}
	else if (auto ExternalInput = Value->As<MIR::FExternalInput>())
	{
		Out.Append(MIR::ExternalInputToString(ExternalInput->Id));
	}
	else if (auto SetMaterailOutput = Value->As<MIR::FSetMaterialOutput>())
	{
		const FString& PropertyName = (SetMaterailOutput->Property == MP_SubsurfaceColor)
			? TEXT("Subsurface")
			: FMaterialAttributeDefinitionMap::GetAttributeName(SetMaterailOutput->Property);
		
		Out.Append(PropertyName);
	}
	else if (auto Subscript = Value->As<MIR::FSubscript>())
	{
		if (Subscript->Arg->Type->AsVector())
		{
			static const TCHAR* Suffix[] = { TEXT(".x"), TEXT(".y"), TEXT(".z"), TEXT(".w") };
			check(Subscript->Index < 4); 
			Out.Append(Suffix[Subscript->Index]);
		}
		else
		{
			Out.Appendf(TEXT("Index: %d"), Subscript->Index);
		}
	}
	else if (auto BinaryOperator = Value->As<MIR::FBinaryOperator>())
	{
		Out.Append(MIR::BinaryOperatorToString(BinaryOperator->Operator));
	}
}				

static void DumpUseInfo(const MIR::FValue* Value, const MIR::FValue* Use, int UseIndex, FString& Out)
{
	if (auto Dimensional = Value->As<MIR::FDimensional>())
	{
		if (Dimensional->Type->AsPrimitive()->IsVector())
		{
			check(UseIndex < 4);
			Out.AppendChar(TEXT("xyzw")[UseIndex]);
		}
		else
		{
			Out.AppendInt(UseIndex);
		}
	}
	else if (auto If = Value->As<MIR::FBranch>())
	{
		static const TCHAR* Uses[] = { TEXT("condition"), TEXT("true"), TEXT("false") };
		Out.Append(Uses[UseIndex]);
	}
	else if (auto BinaryOperator = Value->As<MIR::FBinaryOperator>())
	{
		static const TCHAR* Uses[] = { TEXT("lhs"), TEXT("rhs") };
		Out.Append(Uses[UseIndex]);
	}
}

void DebugDumpIRUseGraph(const FMaterialIRModule& Module)
{
	if (!CVarDumpMaterialIRUseGraph.GetValueOnAnyThread())
	{
		return;
	}

	FString Content;
	TSet<const MIR::FValue*> Crawled;
	TArray<const MIR::FValue*> ValueStack;

	Content.Appendf(TEXT(
		"digraph G {\n\n"
		"rankdir=LR\n"
		"node [shape=box,fontname=\"Consolas\"]\n"
		"edge [fontname=\"Consolas\"]\n\n"
	));

	for (const UE::MIR::FSetMaterialOutput* Output : Module.GetOutputs())
	{
		ValueStack.Push(Output);
	}

	bool DumpInstructionSequence = CVarDumpMaterialIRUseGraph_EnableNext.GetValueOnAnyThread();

	while (!ValueStack.IsEmpty())
	{
		const MIR::FValue* Value = ValueStack.Pop();

		// Begin the node declaration
		Content.Appendf(TEXT("\"%p\" [label=< <b>%s</b>  (%s) <br/> "),
						Value,
						MIR::ValueKindToString(Value->Kind),
						Value->Type ? Value->Type->GetSpelling().GetData() : TEXT("???"));

		DumpValueInfo(Value, Content);

		// End the node declaration
		Content.Append(TEXT(">]\n"));

		const MIR::FInstruction* Instr = Value->AsInstruction();
		if (DumpInstructionSequence && Instr && Instr->Next)
		{
			Content.Appendf(TEXT("\"%p\" -> \"%p\" [color=\"red\"]\n"), Instr, Instr->Next);
		}

		int UseIndex = -1;
		for (const MIR::FValue* Use : Value->GetUses())
		{
			++UseIndex;

			if (!Use)
			{
				continue;
			}
			
			Content.Appendf(TEXT("\"%p\" -> \"%p\" [label=\""), Value, Use);

			DumpUseInfo(Value, Use, UseIndex, Content);

			Content.Appendf(TEXT("\"]\n"));

			if (!Crawled.Contains(Use))
			{
				Crawled.Add(Use);
				ValueStack.Push(Use);
			}

			if (DumpInstructionSequence && Instr)
			{
				const MIR::FInstruction* UseInstr = Use->AsInstruction();
				if (UseInstr && UseInstr->Block != Instr->Block)
				{
					Content.Appendf(TEXT("\"%p\" -> \"%p\" [color=\"red\", style=\"dashed\"]\n"), UseInstr, Instr);
				}
			}
		}
	}

	Content.Appendf(TEXT("\n}\n"));
	
	FString FilePath = FPaths::Combine(FPaths::ProjectLogDir(), TEXT("MaterialIRDumpGraph.dot"));
	FFileHelper::SaveStringToFile(Content, *FilePath);
}

} // namespace UE::MIR

#endif // #if WITH_EDITOR
