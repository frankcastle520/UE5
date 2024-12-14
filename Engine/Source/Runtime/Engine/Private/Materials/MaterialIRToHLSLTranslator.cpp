// Copyright Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialIRToHLSLTranslator.h"
#include "Materials/MaterialIRModule.h"
#include "Materials/MaterialIRTypes.h"
#include "Materials/MaterialIR.h"
#include "MaterialIRInternal.h"

#include "ShaderCore.h"
#include "MaterialShared.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "Materials/MaterialExpressionVolumetricAdvancedMaterialOutput.h"
#include "RenderUtils.h"
#include "Engine/Texture.h"

#include <inttypes.h>

#if WITH_EDITOR

namespace MIR = UE::MIR;

enum ENoOp { NoOp };
enum ENewLine { NewLine };
enum EEndOfStatement { EndOfStatement };
enum EOpenBrace { OpenBrace };
enum ECloseBrace { CloseBrace };
enum EIndentation { Indentation };
enum EBeginArgs { BeginArgs };
enum EEndArgs { EndArgs };
enum EListSeparator { ListSeparator };

#define TAB "    "

struct FHLSLPrinter
{
	FString Buffer;
	bool bFirstListItem = false;
	int32 Tabs = 0;

	template <int N, typename... Types>
	void Appendf(const TCHAR (&Format)[N], Types... Args)
	{
		Buffer.Appendf(Format, Args...);
	}

	FHLSLPrinter& operator<<(const TCHAR* Text)
	{
		Buffer.Append(Text);
		return *this;
	}
	
	FHLSLPrinter& operator<<(const FString& Text)
	{
		Buffer.Append(Text);
		return *this;
	}

	FHLSLPrinter& operator<<(int Value)
	{
		Buffer.Appendf(TEXT("%d"), Value);
		return *this;
	}

    FHLSLPrinter& operator<<(ENoOp)
	{
		return *this;
	}

    FHLSLPrinter& operator<<(ENewLine)
    {
		Buffer.AppendChar('\n');
		operator<<(Indentation);
        return *this;
    }

	FHLSLPrinter& operator<<(EIndentation)
	{
		for (int i = 0; i < Tabs; ++i)
		{
			Buffer.AppendChar('\t');
		}
		return *this;
	}

	FHLSLPrinter& operator<<(EEndOfStatement)
	{
		Buffer.AppendChar(';');
        *this << NewLine;
        return *this;
	}

    FHLSLPrinter& operator<<(EOpenBrace)
    {
        Buffer.Append("{");
        ++Tabs;
        *this << NewLine;
        return *this;
    }

    FHLSLPrinter& operator<<(ECloseBrace)
    {
        --Tabs;
        Buffer.LeftChopInline(1); // undo tab
        Buffer.AppendChar('}');
        return *this;
    }
	
    FHLSLPrinter& operator<<(EBeginArgs)
    {
        Buffer.AppendChar('(');
		BeginList();
        return *this;
    }

    FHLSLPrinter& operator<<(EEndArgs)
    {
        Buffer.AppendChar(')');
        return *this;
    }

	FHLSLPrinter& operator<<(EListSeparator)
    {
		PrintListSeparator();
        return *this;
    }

	void BeginList()
	{
		bFirstListItem = true;
	}

	void PrintListSeparator()
	{
		if (!bFirstListItem)
		{
			Buffer.Append(TEXT(", "));
		}
		bFirstListItem = false;
	}
};

static const TCHAR* GetHLSLTypeString(EMaterialValueType Type)
{
	switch (Type)
	{
	case MCT_Float1: return TEXT("MaterialFloat");
	case MCT_Float2: return TEXT("MaterialFloat2");
	case MCT_Float3: return TEXT("MaterialFloat3");
	case MCT_Float4: return TEXT("MaterialFloat4");
	case MCT_Float: return TEXT("MaterialFloat");
	case MCT_Texture2D: return TEXT("texture2D");
	case MCT_TextureCube: return TEXT("textureCube");
	case MCT_Texture2DArray: return TEXT("texture2DArray");
	case MCT_VolumeTexture: return TEXT("volumeTexture");
	case MCT_StaticBool: return TEXT("static bool");
	case MCT_Bool:  return TEXT("bool");
	case MCT_MaterialAttributes: return TEXT("FMaterialAttributes");
	case MCT_TextureExternal: return TEXT("TextureExternal");
	case MCT_TextureVirtual: return TEXT("TextureVirtual");
	case MCT_VTPageTableResult: return TEXT("VTPageTableResult");
	case MCT_ShadingModel: return TEXT("uint");
	case MCT_UInt: return TEXT("uint");
	case MCT_UInt1: return TEXT("uint");
	case MCT_UInt2: return TEXT("uint2");
	case MCT_UInt3: return TEXT("uint3");
	case MCT_UInt4: return TEXT("uint4");
	case MCT_Substrate: return TEXT("FSubstrateData");
	case MCT_TextureCollection: return TEXT("FResourceCollection");
	default: return TEXT("unknown");
	};
}

static const TCHAR* GetShadingModelParameterName(EMaterialShadingModel InModel)
{
	switch (InModel)
	{
		case MSM_Unlit: return TEXT("MATERIAL_SHADINGMODEL_UNLIT");
		case MSM_DefaultLit: return TEXT("MATERIAL_SHADINGMODEL_DEFAULT_LIT");
		case MSM_Subsurface: return TEXT("MATERIAL_SHADINGMODEL_SUBSURFACE");
		case MSM_PreintegratedSkin: return TEXT("MATERIAL_SHADINGMODEL_PREINTEGRATED_SKIN");
		case MSM_ClearCoat: return TEXT("MATERIAL_SHADINGMODEL_CLEAR_COAT");
		case MSM_SubsurfaceProfile: return TEXT("MATERIAL_SHADINGMODEL_SUBSURFACE_PROFILE");
		case MSM_TwoSidedFoliage: return TEXT("MATERIAL_SHADINGMODEL_TWOSIDED_FOLIAGE");
		case MSM_Hair: return TEXT("MATERIAL_SHADINGMODEL_HAIR");
		case MSM_Cloth: return TEXT("MATERIAL_SHADINGMODEL_CLOTH");
		case MSM_Eye: return TEXT("MATERIAL_SHADINGMODEL_EYE");
		case MSM_SingleLayerWater: return TEXT("MATERIAL_SHADINGMODEL_SINGLELAYERWATER");
		case MSM_ThinTranslucent: return TEXT("MATERIAL_SHADINGMODEL_THIN_TRANSLUCENT");
		default: UE_MIR_UNREACHABLE();
	}
}

static bool IsFoldable(const MIR::FInstruction* Instr)
{
	if (auto Branch = Instr->As<MIR::FBranch>())
	{
		return !Branch->TrueBlock.Instructions && !Branch->FalseBlock.Instructions;
	}

	return true;
}

struct FTranslator : FMaterialIRToHLSLTranslation
{
	int32 NumLocals{};
	TMap<const MIR::FInstruction*, FString> LocalIdentifier;
	FHLSLPrinter Printer;
	FString PixelAttributesHLSL;
	FString EvaluateOtherMaterialAttributesHLSL;

	void GenerateHLSL()
	{
		Printer.Tabs = 1;
		Printer << Indentation;

		LowerBlock(Module->GetRootBlock());

		Printer << TEXT("PixelMaterialInputs.FrontMaterial = GetInitialisedSubstrateData()") << EndOfStatement;
		Printer << TEXT("PixelMaterialInputs.Subsurface = 0") << EndOfStatement;

		EvaluateOtherMaterialAttributesHLSL = MoveTemp(Printer.Buffer);

		for (int32 PropertyIndex = 0; PropertyIndex < MP_MAX; ++PropertyIndex)
		{
			EMaterialProperty Property = (EMaterialProperty)PropertyIndex;
			if (!MIR::Internal::IsMaterialPropertyShared(Property))
			{
				continue;
			}
		
			check(FMaterialAttributeDefinitionMap::GetShaderFrequency(Property) == SF_Pixel);
			
			// Special case MP_SubsurfaceColor as the actual property is a combination of the color and the profile but we don't want to expose the profile
			FString PropertyName = (Property == MP_SubsurfaceColor) ? "Subsurface" : FMaterialAttributeDefinitionMap::GetAttributeName(Property);
			EMaterialValueType Type = (Property == MP_SubsurfaceColor) ? MCT_Float4 : FMaterialAttributeDefinitionMap::GetValueType(Property);
			check(PropertyName.Len() > 0);

			PixelAttributesHLSL.Appendf(TEXT(TAB "%s %s;\n"), GetHLSLTypeString(Type), *PropertyName);
		}
	}
	
	ENoOp LowerBlock(const MIR::FBlock& Block)
	{
		int OldNumLocals = NumLocals;
        for (MIR::FInstruction* Instr = Block.Instructions; Instr; Instr = Instr->Next)
		{
            if (Instr->NumUsers == 1 && IsFoldable(Instr))
			{
                continue;
            }
            
			if (Instr->NumUsers >= 1)
			{
                FString LocalStr = FString::Printf(TEXT("l%d"), NumLocals);
                ++NumLocals;

                Printer << LowerType(Instr->Type) << TEXT(" ") << LocalStr;

                LocalIdentifier.Add(Instr, MoveTemp(LocalStr));
                if (IsFoldable(Instr))
				{
                    Printer << TEXT(" = ";)
                }
            }

			LowerInstruction(Instr);

			if (Printer.Buffer.EndsWith(TEXT("}")))
			{
				Printer << NewLine;
			}
			else
			{
				Printer << EndOfStatement;
			}

			if (Instr->Kind == MIR::VK_SetMaterialOutput)
			{
				Printer << NewLine;
			}
        }

        NumLocals = OldNumLocals;

		return NoOp;
	}
	
	ENoOp LowerValue(MIR::FValue* InValue)
	{
		if (MIR::FInstruction* Instr = InValue->AsInstruction())
		{
			if (Instr->NumUsers <= 1 && IsFoldable(Instr))
			{
				Printer << LowerInstruction(Instr);
			}
			else
			{
				Printer << LocalIdentifier[Instr];
			}

			return NoOp;
		}

		switch (InValue->Kind)
		{
			case MIR::VK_Constant: LowerConstant(static_cast<const MIR::FConstant*>(InValue)); break;
			case MIR::VK_ExternalInput: LowerExternalInput(static_cast<const MIR::FExternalInput*>(InValue)); break;
			case MIR::VK_MaterialParameter: LowerMaterialParameter(static_cast<const MIR::FMaterialParameter*>(InValue)); break;
			default: UE_MIR_UNREACHABLE();
		}

		return NoOp;
	}

	ENoOp LowerInstruction(MIR::FInstruction* Instr)
	{
		switch (Instr->Kind)
		{
			case MIR::VK_Dimensional: LowerDimensional(static_cast<MIR::FDimensional*>(Instr)); break;
			case MIR::VK_SetMaterialOutput: LowerSetMaterialOutput(static_cast<MIR::FSetMaterialOutput*>(Instr)); break;
			case MIR::VK_BinaryOperator: LowerBinaryOperator(static_cast<MIR::FBinaryOperator*>(Instr)); break;
			case MIR::VK_Branch: LowerBranch(static_cast<MIR::FBranch*>(Instr)); break;
			case MIR::VK_Subscript: LowerSubscript(static_cast<MIR::FSubscript*>(Instr)); break;
			case MIR::VK_TextureSample: LowerTextureSample(static_cast<MIR::FTextureSample*>(Instr)); break;

			default:
				UE_MIR_UNREACHABLE();
		}
	
		return NoOp;
	}

	void LowerConstant(const MIR::FConstant* Constant)
	{
		MIR::FPrimitiveTypePtr checkNoEntry = Constant->Type->AsPrimitive();
		check(checkNoEntry && checkNoEntry->IsScalar());

		switch (checkNoEntry->ScalarKind)
		{
			case MIR::SK_Bool:  Printer.Buffer.Append(Constant->Boolean ? TEXT("true") : TEXT("false")); break;
			case MIR::SK_Int:   Printer.Buffer.Appendf(TEXT("%") PRId64, Constant->Integer); break;
			case MIR::SK_Float: Printer.Buffer.Appendf(TEXT("%.5ff"), Constant->Float); break;
		}
	}

	void LowerExternalInput(const MIR::FExternalInput* ExternalInput)
	{
		int ExternalInputIndex = (int)ExternalInput->Id;

		if (MIR::IsExternalInputTexCoord(ExternalInput->Id))
		{
			int Index = ExternalInputIndex - (int)MIR::EExternalInput::TexCoord0;
			Printer.Appendf(TEXT("Parameters.TexCoords[%d]"), Index);
		}
		else if (MIR::IsExternalInputTexCoordDdx(ExternalInput->Id))
		{
			int Index = ExternalInputIndex - (int)MIR::EExternalInput::TexCoord0_Ddx;
			Printer.Appendf(TEXT("Parameters.TexCoords_DDX[%d]"), Index);
		}
		else if (MIR::IsExternalInputTexCoordDdy(ExternalInput->Id))
		{
			int Index = ExternalInputIndex - (int)MIR::EExternalInput::TexCoord0_Ddy;
			Printer.Appendf(TEXT("Parameters.TexCoords_DDY[%d]"), Index);
		}
		else
		{
			UE_MIR_UNREACHABLE();
		}
	}

	void LowerMaterialParameter(const MIR::FMaterialParameter* Parameter)
	{
		UE_MIR_UNREACHABLE();
	}
	
	void LowerDimensional(const MIR::FDimensional* Dimensional) 
	{
		MIR::FPrimitiveTypePtr ArithmeticType = Dimensional->Type->AsPrimitive();
		check(ArithmeticType && ArithmeticType->IsVector());

		Printer << ScalarKindToString(ArithmeticType->ScalarKind) << ArithmeticType->NumRows << BeginArgs;

		for (MIR::FValue* Component : Dimensional->GetComponents())
		{
			Printer << ListSeparator << LowerValue(Component);
		}

		Printer << EndArgs;
	}

	void LowerSetMaterialOutput(const MIR::FSetMaterialOutput* Output)
	{
		// Special case MP_SubsurfaceColor as the actual property is a combination of the color and the profile but we don't want to expose the profile
		const FString& PropertyName = (Output->Property == MP_SubsurfaceColor) ? "Subsurface" : FMaterialAttributeDefinitionMap::GetAttributeName(Output->Property);
		Printer << TEXT("PixelMaterialInputs.") << PropertyName << TEXT(" = ") << LowerValue(Output->Arg);
	}

	void LowerBinaryOperator(const MIR::FBinaryOperator* BinaryOperator)
	{
		LowerValue(BinaryOperator->LhsArg);

		const TCHAR* OpString;
		switch (BinaryOperator->Operator)
		{
			case MIR::BO_Add: OpString = TEXT(" + "); break;
			case MIR::BO_Subtract: OpString = TEXT(" - "); break;
			case MIR::BO_Multiply: OpString = TEXT(" * "); break;
			case MIR::BO_Divide: OpString = TEXT(" / "); break;
			case MIR::BO_GreaterThan: OpString = TEXT(" > "); break;
			case MIR::BO_LowerThan: OpString = TEXT(" < "); break;
			case MIR::BO_Equals: OpString = TEXT(" == "); break;
			default: UE_MIR_UNREACHABLE();
		}
		Printer << OpString;

		LowerValue(BinaryOperator->RhsArg);
	}

	void LowerBranch(const MIR::FBranch* Branch)
	{
		if (IsFoldable(Branch))
		{
			Printer << LowerValue(Branch->ConditionArg)
				<< TEXT(" ? ") << LowerValue(Branch->TrueArg)
				<< TEXT(" : ") << LowerValue(Branch->FalseArg);
		}
		else
		{
			Printer << EndOfStatement;
			Printer << TEXT("if (") << LowerValue(Branch->ConditionArg) << TEXT(")") << NewLine << OpenBrace;
			Printer << LowerBlock(Branch->TrueBlock);
			Printer << LocalIdentifier[Branch] << " = " << LowerValue(Branch->TrueArg) << EndOfStatement;
			Printer << CloseBrace << NewLine;
			Printer << TEXT("else") << NewLine << OpenBrace;
			Printer << LowerBlock(Branch->FalseBlock);
			Printer << LocalIdentifier[Branch] << " = " << LowerValue(Branch->FalseArg) << EndOfStatement;
			Printer << CloseBrace;
		}
	}
			
	void LowerSubscript(const MIR::FSubscript* Subscript)
	{
		LowerValue(Subscript->Arg);

		if (MIR::FPrimitiveTypePtr ArgArithmeticType = Subscript->Arg->Type->AsVector())
		{
			const TCHAR* ComponentsStr[] = { TEXT(".x"), TEXT(".y"), TEXT(".z"), TEXT(".w") };
			check(Subscript->Index <= ArgArithmeticType->GetNumComponents());

			Printer << ComponentsStr[Subscript->Index];
		}
	}

	void LowerTextureSample(MIR::FTextureSample* TextureSample)
	{
		bool bUsesSpecialSampler = LowerSamplerType(TextureSample->SamplerType);
		if (bUsesSpecialSampler)
		{
			Printer << TEXT("(");
		}

		switch (TextureSample->Texture->GetMaterialType())
		{
			case MCT_Texture2D:
				Printer << TEXT("Texture2DSample");
				break;

			default:
				UE_MIR_UNREACHABLE();
		}

		Printer << BeginArgs
			<< ListSeparator << LowerTextureReference(TextureSample->Texture->GetMaterialType(), TextureSample->TextureParameterIndex)
			<< ListSeparator << LowerTextureSamplerReference(TextureSample->SamplerSourceMode, TextureSample->Texture->GetMaterialType(), TextureSample->TextureParameterIndex)
			<< ListSeparator << LowerValue(TextureSample->TexCoordArg)
			<< EndArgs;

		if (bUsesSpecialSampler)
		{
			Printer << TEXT(")");
		}
	}

	bool LowerSamplerType(EMaterialSamplerType SamplerType)
	{
		switch (SamplerType)
		{
			case SAMPLERTYPE_External:
				Printer << TEXT("ProcessMaterialExternalTextureLookup");
				break;

			case SAMPLERTYPE_Color:
				Printer << TEXT("ProcessMaterialColorTextureLookup");
				break;
			case SAMPLERTYPE_VirtualColor:
				// has a mobile specific workaround
				Printer << TEXT("ProcessMaterialVirtualColorTextureLookup");
				break;

			case SAMPLERTYPE_LinearColor:
			case SAMPLERTYPE_VirtualLinearColor:
				Printer << TEXT("ProcessMaterialLinearColorTextureLookup");
				break;

			case SAMPLERTYPE_Alpha:
			case SAMPLERTYPE_VirtualAlpha:
			case SAMPLERTYPE_DistanceFieldFont:
				Printer << TEXT("ProcessMaterialAlphaTextureLookup");
				break;

			case SAMPLERTYPE_Grayscale:
			case SAMPLERTYPE_VirtualGrayscale:
				Printer << TEXT("ProcessMaterialGreyscaleTextureLookup");
				break;

			case SAMPLERTYPE_LinearGrayscale:
			case SAMPLERTYPE_VirtualLinearGrayscale:
				Printer <<TEXT("ProcessMaterialLinearGreyscaleTextureLookup");
				break;

			case SAMPLERTYPE_Normal:
			case SAMPLERTYPE_VirtualNormal:
				// Normal maps need to be unpacked in the pixel shader.
				Printer << TEXT("UnpackNormalMap");
				break;

			case SAMPLERTYPE_Masks:
			case SAMPLERTYPE_VirtualMasks:
			case SAMPLERTYPE_Data:
				return false;

			default:
				UE_MIR_UNREACHABLE();
		}

		return true;
	}

	ENoOp LowerTextureSamplerReference(ESamplerSourceMode SamplerSource, EMaterialValueType TextureType, int TextureParameterIndex)
	{
		switch (SamplerSource)
		{
		case SSM_FromTextureAsset:
			LowerTextureReference(TextureType, TextureParameterIndex);
			Printer << TEXT("Sampler");
			break;

		default:
			UE_MIR_UNREACHABLE();
		}

		return NoOp;
	}

	ENoOp LowerTextureReference(EMaterialValueType TextureType, int TextureParameterIndex)
	{
		Printer << TEXT("Material.");

		switch (TextureType)
		{
			case MCT_Texture2D: Printer << TEXT("Texture2D_"); break;
			default: UE_MIR_UNREACHABLE();
		}

		Printer << TextureParameterIndex;

		return NoOp;
	}
	
	/* Finalization */

	void SetMaterialParameters(TMap<FString, FString>& Params)
	{
		const FMaterialIRModule::FStatistics ModuleStatistics = Module->GetStatistics();

		auto SetParamInt = [&] (const TCHAR* InParamName, int InValue)
		{
			Params.Add(InParamName, FString::Printf(TEXT("%d"), InValue));
		};
		
		auto SetParamReturnFloat = [&] (const TCHAR* InParamName, float InValue)
		{
			Params.Add(InParamName, FString::Printf(TEXT(TAB "return %.5f"), InValue));
		};

		Params.Add(TEXT("pixel_material_inputs"), MoveTemp(PixelAttributesHLSL));
		Params.Add(TEXT("calc_pixel_material_inputs_initial_calculations"), EvaluateOtherMaterialAttributesHLSL);
		Params.Add(TEXT("calc_pixel_material_inputs_analytic_derivatives_initial"), MoveTemp(EvaluateOtherMaterialAttributesHLSL));
		
		// MaterialAttributes
		TArray<FGuid> OrderedVisibleAttributes = FMaterialAttributeDefinitionMap::GetOrderedVisibleAttributeList();
		
		FString MaterialDeclarations;
		MaterialDeclarations.Appendf(TEXT("struct FMaterialAttributes\n{\n"));
		for (const FGuid& AttributeID : OrderedVisibleAttributes)
		{
			const FString& PropertyName = FMaterialAttributeDefinitionMap::GetAttributeName(AttributeID);
			const EMaterialValueType PropertyType = FMaterialAttributeDefinitionMap::GetValueType(AttributeID);
			MaterialDeclarations.Appendf(TEXT(TAB "%s %s;\n"), GetHLSLTypeString(PropertyType), *PropertyName);
		}
		MaterialDeclarations.Appendf(TEXT("};"));
		Params.Add(TEXT("material_declarations"), MoveTemp(MaterialDeclarations));
		
		SetParamInt(TEXT("num_material_texcoords_vertex"), ModuleStatistics.NumVertexTexCoords);
		SetParamInt(TEXT("num_material_texcoords"), ModuleStatistics.NumPixelTexCoords);
		SetParamInt(TEXT("num_custom_vertex_interpolators"), 0);
		SetParamInt(TEXT("num_tex_coord_interpolators"), ModuleStatistics.NumPixelTexCoords);

		FString GetMaterialCustomizedUVS;
		for (int CustomUVIndex = 0; CustomUVIndex < ModuleStatistics.NumPixelTexCoords; CustomUVIndex++)
		{
			const FString AttributeName = FMaterialAttributeDefinitionMap::GetAttributeName((EMaterialProperty)(MP_CustomizedUVs0 + CustomUVIndex));
			GetMaterialCustomizedUVS.Appendf(TEXT(TAB "OutTexCoords[%u] = Parameters.MaterialAttributes.%s;\n"), CustomUVIndex, *AttributeName);
		}
		Params.Add(TEXT("get_material_customized_u_vs"), MoveTemp(GetMaterialCustomizedUVS));

		SetParamReturnFloat(TEXT("get_material_emissive_for_cs"), 0.f);
		SetParamReturnFloat(TEXT("get_material_translucency_directional_lighting_intensity"), Material->GetTranslucencyDirectionalLightingIntensity());
		SetParamReturnFloat(TEXT("get_material_translucent_shadow_density_scale"), Material->GetTranslucentShadowDensityScale());
		SetParamReturnFloat(TEXT("get_material_translucent_self_shadow_density_scale"), Material->GetTranslucentSelfShadowDensityScale());
		SetParamReturnFloat(TEXT("get_material_translucent_self_shadow_second_density_scale"), Material->GetTranslucentSelfShadowSecondDensityScale());
		SetParamReturnFloat(TEXT("get_material_translucent_self_shadow_second_opacity"), Material->GetTranslucentSelfShadowSecondOpacity());
		SetParamReturnFloat(TEXT("get_material_translucent_backscattering_exponent"), Material->GetTranslucentBackscatteringExponent());

		FLinearColor Extinction = Material->GetTranslucentMultipleScatteringExtinction();
		Params.Add(TEXT("get_material_translucent_multiple_scattering_extinction"), FString::Printf(TEXT(TAB "return MaterialFloat3(%.5f, %.5f, %.5f)"), Extinction.R, Extinction.G, Extinction.B));

		SetParamReturnFloat(TEXT("get_material_opacity_mask_clip_value"), Material->GetOpacityMaskClipValue());
		Params.Add(TEXT("get_material_world_position_offset_raw"), TEXT(TAB "return 0; // todo"));
		Params.Add(TEXT("get_material_previous_world_position_offset_raw"), TEXT(TAB "return 0; // todo"));
	
		// CustomData0/1 are named ClearCoat/ClearCoatRoughness
		Params.Add(TEXT("get_material_custom_data0"), TEXT(TAB "return 1.0f; // todo"));
		Params.Add(TEXT("get_material_custom_data1"), TEXT(TAB "return 0.1f; // todo"));

		FString EvaluateMaterialDeclaration;
		EvaluateMaterialDeclaration.Append(TEXT("void EvaluateVertexMaterialAttributes(in out FMaterialVertexParameters Parameters)\n{\n"));
		for (int CustomUVIndex = 0; CustomUVIndex < ModuleStatistics.NumPixelTexCoords; CustomUVIndex++)
		{
			EvaluateMaterialDeclaration.Appendf(TEXT(TAB "Parameters.MaterialAttributes.CustomizedUV%d = Parameters.TexCoords[%d].xy;\n"), CustomUVIndex, CustomUVIndex);
		}
		EvaluateMaterialDeclaration.Append(TEXT("\n}\n"));
		Params.Add(TEXT("evaluate_material_attributes"), MoveTemp(EvaluateMaterialDeclaration));
	}
	
	ENoOp LowerType(const MIR::FType* Type)
	{
		if (auto ArithmeticType = Type->AsPrimitive())
		{
			switch (ArithmeticType->ScalarKind)
			{
				case MIR::SK_Bool:	Printer << TEXT("bool"); break;
				case MIR::SK_Int: 	Printer << TEXT("int"); break;
				case MIR::SK_Float:	Printer << TEXT("float"); break;
			}

			if (ArithmeticType->NumRows > 1)
			{
				Printer << ArithmeticType->NumRows;
			}
			
			if (ArithmeticType->NumColumns > 1)
			{
				Printer << TEXT("x") << ArithmeticType->NumColumns;
			}
		}
		else
		{
			UE_MIR_UNREACHABLE();
		}

		return NoOp;
	}
	
	void GetShaderCompilerEnvironment(FShaderCompilerEnvironment& OutEnvironment)
	{
		const FMaterialCompilationOutput& CompilationOutput = Module->GetCompilationOutput();
		EShaderPlatform ShaderPlatform = Module->GetShaderPlatform();

		OutEnvironment.TargetPlatform = TargetPlatform;
		OutEnvironment.SetDefine(TEXT("ENABLE_NEW_HLSL_GENERATOR"), 1);
		OutEnvironment.SetDefine(TEXT("MATERIAL_ATMOSPHERIC_FOG"), false);
		OutEnvironment.SetDefine(TEXT("MATERIAL_SKY_ATMOSPHERE"), false);
		OutEnvironment.SetDefine(TEXT("INTERPOLATE_VERTEX_COLOR"), false);
		OutEnvironment.SetDefine(TEXT("NEEDS_PARTICLE_COLOR"), false);
		OutEnvironment.SetDefine(TEXT("NEEDS_PARTICLE_LOCAL_TO_WORLD"), false);
		OutEnvironment.SetDefine(TEXT("NEEDS_PARTICLE_WORLD_TO_LOCAL"), false);
		OutEnvironment.SetDefine(TEXT("NEEDS_PER_INSTANCE_RANDOM_PS"), false);
		OutEnvironment.SetDefine(TEXT("USES_TRANSFORM_VECTOR"), false);
		OutEnvironment.SetDefine(TEXT("WANT_PIXEL_DEPTH_OFFSET"), CompilationOutput.bUsesPixelDepthOffset);
		OutEnvironment.SetDefineAndCompileArgument(TEXT("USES_WORLD_POSITION_OFFSET"), (bool)CompilationOutput.bUsesWorldPositionOffset);
		OutEnvironment.SetDefineAndCompileArgument(TEXT("USES_DISPLACEMENT"), false);
		OutEnvironment.SetDefine(TEXT("USES_EMISSIVE_COLOR"), false);
		OutEnvironment.SetDefine(TEXT("USES_DISTORTION"), Material->IsDistorted());
		OutEnvironment.SetDefine(TEXT("MATERIAL_ENABLE_TRANSLUCENCY_FOGGING"), Material->ShouldApplyFogging());
		OutEnvironment.SetDefine(TEXT("MATERIAL_ENABLE_TRANSLUCENCY_CLOUD_FOGGING"), Material->ShouldApplyCloudFogging());
		OutEnvironment.SetDefine(TEXT("MATERIAL_IS_SKY"), Material->IsSky());
		OutEnvironment.SetDefine(TEXT("MATERIAL_COMPUTE_FOG_PER_PIXEL"), Material->ComputeFogPerPixel());
		OutEnvironment.SetDefine(TEXT("MATERIAL_FULLY_ROUGH"), false);
		OutEnvironment.SetDefine(TEXT("MATERIAL_USES_ANISOTROPY"), false);
		OutEnvironment.SetDefine(TEXT("MATERIAL_NEURAL_POST_PROCESS"), (CompilationOutput.bUsedWithNeuralNetworks || Material->IsUsedWithNeuralNetworks()) && Material->IsPostProcessMaterial());
		OutEnvironment.SetDefine(TEXT("NUM_VIRTUALTEXTURE_SAMPLES"), 0);
		OutEnvironment.SetDefine(TEXT("MATERIAL_VIRTUALTEXTURE_FEEDBACK"), false);
		OutEnvironment.SetDefine(TEXT("IS_MATERIAL_SHADER"), true);

		FMaterialShadingModelField ShadingModels = Material->GetShadingModels();
		ensure(ShadingModels.IsValid());

		int32 NumActiveShadingModels = 0;
		if (ShadingModels.IsLit())
		{
			// This is to have platforms use the simple single layer water shading similar to mobile: no dynamic lights, only sun and sky, no distortion, no colored transmittance on background, no custom depth read.
			const bool bSingleLayerWaterUsesSimpleShading = FDataDrivenShaderPlatformInfo::GetWaterUsesSimpleForwardShading(ShaderPlatform) && IsForwardShadingEnabled(ShaderPlatform);

			for (int i = 0; i < MSM_NUM; ++i)
			{
				EMaterialShadingModel Model = (EMaterialShadingModel)i;
				if (Model == MSM_Strata || !ShadingModels.HasShadingModel(Model))
				{
					continue;
				}

				if (Model == MSM_SingleLayerWater && !FDataDrivenShaderPlatformInfo::GetRequiresDisableForwardLocalLights(ShaderPlatform))
				{
					continue;
				}

				if (Model == MSM_SingleLayerWater && bSingleLayerWaterUsesSimpleShading)
				{
					// Value must match SINGLE_LAYER_WATER_SHADING_QUALITY_MOBILE_WITH_DEPTH_TEXTURE in SingleLayerWaterCommon.ush!
					OutEnvironment.SetDefine(TEXT("SINGLE_LAYER_WATER_SHADING_QUALITY"), true);
				}

				OutEnvironment.SetDefine(GetShadingModelParameterName(Model), true);
				NumActiveShadingModels += 1;
			}
		}
		else
		{
			// Unlit shading model can only exist by itself
			OutEnvironment.SetDefine(GetShadingModelParameterName(MSM_Unlit), true);
			NumActiveShadingModels += 1;
		}

		if (NumActiveShadingModels == 1)
		{
			OutEnvironment.SetDefine(TEXT("MATERIAL_SINGLE_SHADINGMODEL"), true);
		}
		else if (!ensure(NumActiveShadingModels > 0))
		{
			UE_LOG(LogMaterial, Warning, TEXT("Unknown material shading model(s). Setting to MSM_DefaultLit"));
			OutEnvironment.SetDefine(GetShadingModelParameterName(MSM_DefaultLit), true);
		}

		static IConsoleVariable* CVarLWCIsEnabled = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MaterialEditor.LWCEnabled"));
		OutEnvironment.SetDefine(TEXT("MATERIAL_LWC_ENABLED"), CVarLWCIsEnabled->GetInt());
		OutEnvironment.SetDefine(TEXT("WSVECTOR_IS_TILEOFFSET"), true);
		OutEnvironment.SetDefine(TEXT("WSVECTOR_IS_DOUBLEFLOAT"), false);

		if (Material->GetMaterialDomain() == MD_Volume)
		{
			TArray<const UMaterialExpressionVolumetricAdvancedMaterialOutput*> VolumetricAdvancedExpressions;
			Material->GetMaterialInterface()->GetMaterial()->GetAllExpressionsOfType(VolumetricAdvancedExpressions);
			if (VolumetricAdvancedExpressions.Num() > 0)
			{
				if (VolumetricAdvancedExpressions.Num() > 1)
				{
					UE_LOG(LogMaterial, Fatal, TEXT("Only a single UMaterialExpressionVolumetricAdvancedMaterialOutput node is supported."));
				}

				const UMaterialExpressionVolumetricAdvancedMaterialOutput* VolumetricAdvancedNode = VolumetricAdvancedExpressions[0];
				const TCHAR* Param = VolumetricAdvancedNode->GetEvaluatePhaseOncePerSample() ? TEXT("MATERIAL_VOLUMETRIC_ADVANCED_PHASE_PERSAMPLE") : TEXT("MATERIAL_VOLUMETRIC_ADVANCED_PHASE_PERPIXEL");
				OutEnvironment.SetDefine(Param, true);

				OutEnvironment.SetDefine(TEXT("MATERIAL_VOLUMETRIC_ADVANCED"), true);
				OutEnvironment.SetDefine(TEXT("MATERIAL_VOLUMETRIC_ADVANCED_GRAYSCALE_MATERIAL"), VolumetricAdvancedNode->bGrayScaleMaterial);
				OutEnvironment.SetDefine(TEXT("MATERIAL_VOLUMETRIC_ADVANCED_RAYMARCH_VOLUME_SHADOW"), VolumetricAdvancedNode->bRayMarchVolumeShadow);
				OutEnvironment.SetDefine(TEXT("MATERIAL_VOLUMETRIC_ADVANCED_CLAMP_MULTISCATTERING_CONTRIBUTION"), VolumetricAdvancedNode->bClampMultiScatteringContribution);
				OutEnvironment.SetDefine(TEXT("MATERIAL_VOLUMETRIC_ADVANCED_MULTISCATTERING_OCTAVE_COUNT"), VolumetricAdvancedNode->GetMultiScatteringApproximationOctaveCount());
				OutEnvironment.SetDefine(TEXT("MATERIAL_VOLUMETRIC_ADVANCED_CONSERVATIVE_DENSITY"), VolumetricAdvancedNode->ConservativeDensity.IsConnected());
				OutEnvironment.SetDefine(TEXT("MATERIAL_VOLUMETRIC_ADVANCED_OVERRIDE_AMBIENT_OCCLUSION"), Material->HasAmbientOcclusionConnected());
				OutEnvironment.SetDefine(TEXT("MATERIAL_VOLUMETRIC_ADVANCED_GROUND_CONTRIBUTION"), VolumetricAdvancedNode->bGroundContribution);
			}
		}

		OutEnvironment.SetDefine(TEXT("MATERIAL_IS_SUBSTRATE"), false);
		OutEnvironment.SetDefine(TEXT("DUAL_SOURCE_COLOR_BLENDING_ENABLED"), false);
		OutEnvironment.SetDefine(TEXT("TEXTURE_SAMPLE_DEBUG"), false);
	}
};

void FMaterialIRToHLSLTranslation::Run(TMap<FString, FString>& OutParameters, FShaderCompilerEnvironment& OutEnvironment)
{
	OutParameters.Empty();

	FTranslator Translator{ *this };
	Translator.GenerateHLSL();
	Translator.SetMaterialParameters(OutParameters);
	Translator.GetShaderCompilerEnvironment(OutEnvironment);
}

#undef TAB
#endif // #if WITH_EDITOR