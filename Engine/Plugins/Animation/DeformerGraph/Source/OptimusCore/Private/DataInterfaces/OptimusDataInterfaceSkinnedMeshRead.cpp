// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDataInterfaceSkinnedMeshRead.h"

#include "Components/SkinnedMeshComponent.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "OptimusDataDomain.h"
#include "RenderGraphBuilder.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "ShaderCompilerCore.h"
#include "ShaderParameterMetadataBuilder.h"
#include "SkeletalMeshDeformerHelpers.h"
#include "SkeletalRenderPublic.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "OptimusDeformerInstance.h"
#include "Animation/MeshDeformerInstance.h"
#include "ComputeFramework/ComputeKernelPermutationSet.h"
#include "ComputeFramework/ComputeKernelPermutationVector.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OptimusDataInterfaceSkinnedMeshRead)

const TCHAR* UOptimusSkinnedMeshReadDataInterface::ReadableOutputBufferPermutationName = TEXT("READABLE_OUTPUT_BUFFERS");

FString UOptimusSkinnedMeshReadDataInterface::GetDisplayName() const
{
	return TEXT("Read Skinned Mesh");
}

FName UOptimusSkinnedMeshReadDataInterface::GetCategory() const
{
	return CategoryName::DataInterfaces;
}

TArray<FOptimusCDIPinDefinition> UOptimusSkinnedMeshReadDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;
	Defs.Add({ "Position", "ReadPosition", Optimus::DomainName::Vertex, "ReadNumVertices" });
	Defs.Add({ "TangentX", "ReadTangentX", Optimus::DomainName::Vertex, "ReadNumVertices" });
	Defs.Add({ "TangentZ", "ReadTangentZ", Optimus::DomainName::Vertex, "ReadNumVertices" });
	Defs.Add({ "Color", "ReadColor", Optimus::DomainName::Vertex, "ReadNumVertices" });
	return Defs;
}


TSubclassOf<UActorComponent> UOptimusSkinnedMeshReadDataInterface::GetRequiredComponentClass() const
{
	return USkinnedMeshComponent::StaticClass();
}

// Should be kept in sync with GetSupportedInput
enum class ESkinnedMeshReadDataInterfaceInputSelectorMask : uint64
{
	NumVertices = 1 << 0,
	Position = 1 << 1,
	TangentX = 1 << 2,
	TangentZ = 1 << 3,
	Color = 1 << 4,
};
ENUM_CLASS_FLAGS(ESkinnedMeshReadDataInterfaceInputSelectorMask);

void UOptimusSkinnedMeshReadDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadNumVertices"))
		.AddReturnType(EShaderFundamentalType::Uint);
	
	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadPosition"))
		.AddReturnType(EShaderFundamentalType::Float, 3)
		.AddParam(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadTangentX"))
		.AddReturnType(EShaderFundamentalType::Float, 4)
		.AddParam(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadTangentZ"))
		.AddReturnType(EShaderFundamentalType::Float, 4)
		.AddParam(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadColor"))
		.AddReturnType(EShaderFundamentalType::Float, 4)
		.AddParam(EShaderFundamentalType::Uint);
}

BEGIN_SHADER_PARAMETER_STRUCT(FSkinnedMeshReadDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, NumVertices)
	SHADER_PARAMETER(uint32, InputStreamStart)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float>, PositionBufferUAV)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<SNORM float4>, TangentBufferUAV)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<UNORM float4>, ColorBufferUAV)
	SHADER_PARAMETER_SRV(Buffer<float>, PositionStaticBuffer)
	SHADER_PARAMETER_SRV(Buffer<SNORM float4>, TangentStaticBuffer)
	SHADER_PARAMETER_SRV(Buffer<float4>, ColorStaticBuffer)
	SHADER_PARAMETER(uint32, ColorIndexMask)
END_SHADER_PARAMETER_STRUCT()

void UOptimusSkinnedMeshReadDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const
{
	InOutBuilder.AddNestedStruct<FSkinnedMeshReadDataInterfaceParameters>(UID);
}

TCHAR const* UOptimusSkinnedMeshReadDataInterface::TemplateFilePath = TEXT("/Plugin/Optimus/Private/DataInterfaceSkinnedMeshRead.ush");

TCHAR const* UOptimusSkinnedMeshReadDataInterface::GetShaderVirtualPath() const
{
	return TemplateFilePath;
}

void UOptimusSkinnedMeshReadDataInterface::GetShaderHash(FString& InOutKey) const
{
	GetShaderFileHash(TemplateFilePath, EShaderPlatform::SP_PCD3D_SM5).AppendString(InOutKey);
}

void UOptimusSkinnedMeshReadDataInterface::GetHLSL(FString& OutHLSL, FString const& InDataInterfaceName) const
{
	TMap<FString, FStringFormatArg> TemplateArgs =
	{
		{ TEXT("DataInterfaceName"), InDataInterfaceName },
	};

	FString TemplateFile;
	LoadShaderSourceFile(TemplateFilePath, EShaderPlatform::SP_PCD3D_SM5, &TemplateFile, nullptr);
	OutHLSL += FString::Format(*TemplateFile, TemplateArgs);
}

enum class EOptimusSkinnedMeshReadReadableOutputBuffer: uint32
{
	None = 0,
	Position = 1<<0,
	Tangents = 1<<1,
	VertexColor = 1<<2,
	NumPermutations = 1 << 3
};
ENUM_CLASS_FLAGS(EOptimusSkinnedMeshReadReadableOutputBuffer);

void UOptimusSkinnedMeshReadDataInterface::GetDefines(FComputeKernelDefinitionSet& OutDefinitionSet) const
{
	OutDefinitionSet.Defines.Add(FComputeKernelDefinition(
		TEXT("OPTIMUS_SKINNED_MESH_READ_POSITION"),
		FString::FromInt(static_cast<uint32>(EOptimusSkinnedMeshReadReadableOutputBuffer::Position))));
	OutDefinitionSet.Defines.Add(FComputeKernelDefinition(
		TEXT("OPTIMUS_SKINNED_MESH_READ_TANGENTS"),
		FString::FromInt(static_cast<uint32>(EOptimusSkinnedMeshReadReadableOutputBuffer::Tangents))));
	OutDefinitionSet.Defines.Add(FComputeKernelDefinition(
		TEXT("OPTIMUS_SKINNED_MESH_READ_COLOR"),
		FString::FromInt(static_cast<uint32>(EOptimusSkinnedMeshReadReadableOutputBuffer::VertexColor))));
}



void UOptimusSkinnedMeshReadDataInterface::GetPermutations(FComputeKernelPermutationVector& OutPermutationVector) const
{
	OutPermutationVector.AddPermutation(ReadableOutputBufferPermutationName, static_cast<uint32>(EOptimusSkinnedMeshReadReadableOutputBuffer::NumPermutations));
}

UComputeDataProvider* UOptimusSkinnedMeshReadDataInterface::CreateDataProvider(TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask) const
{
	UOptimusSkinnedMeshReadDataProvider* Provider = NewObject<UOptimusSkinnedMeshReadDataProvider>();
	Provider->SkinnedMesh = Cast<USkinnedMeshComponent>(InBinding);
	Provider->InputMask = InInputMask;
	return Provider;
}

FComputeDataProviderRenderProxy* UOptimusSkinnedMeshReadDataProvider::GetRenderProxy()
{
	return new FOptimusSkinnedMeshReadDataProviderProxy(SkinnedMesh, InputMask, DeformerInstance->OutputBuffersFromPreviousInstances, &LastLodIndexCachedByRenderProxy);
}

void UOptimusSkinnedMeshReadDataProvider::SetDeformerInstance(UOptimusDeformerInstance* InInstance)
{
	DeformerInstance = InInstance;
}

FOptimusSkinnedMeshReadDataProviderProxy::FOptimusSkinnedMeshReadDataProviderProxy(USkinnedMeshComponent* InSkinnedMeshComponent, uint64 InInputMask, EMeshDeformerOutputBuffer InOutputBuffersFromPreviousInstances, int32* InLastLodIndexPtr) 
{
	SkeletalMeshObject = InSkinnedMeshComponent != nullptr ? InSkinnedMeshComponent->MeshObject : nullptr;
	InputMask = InInputMask;
	OutputBuffersFromPreviousInstances = InOutputBuffersFromPreviousInstances;
	LastLodIndexPtr = InLastLodIndexPtr;

	check(LastLodIndexPtr != nullptr);
}

bool FOptimusSkinnedMeshReadDataProviderProxy::IsValid(FValidationData const& InValidationData) const
{
	if (InValidationData.ParameterStructSize != sizeof(FParameters))
	{
		return false;
	}
	if (SkeletalMeshObject == nullptr)
	{
		return false;
	}
	if (SkeletalMeshObject->IsCPUSkinned())
	{
		return false;
	}
	if (SkeletalMeshObject->GetSkeletalMeshRenderData().LODRenderData[SkeletalMeshObject->GetLOD()].RenderSections.Num() != InValidationData.NumInvocations)
	{
		return false;
	}

	return true;
}

void FOptimusSkinnedMeshReadDataProviderProxy::AllocateResources(FRDGBuilder& GraphBuilder)
{
	// Allocate required buffers
	const int32 LodIndex = SkeletalMeshObject->GetLOD();
	
	bool bLodJustChanged = false;
	if (LodIndex != *LastLodIndexPtr)
	{
		bLodJustChanged = true;	
		*LastLodIndexPtr = LodIndex;
	}
	
	if (InputMask & static_cast<uint64>(ESkinnedMeshReadDataInterfaceInputSelectorMask::Position) )
	{
		FRDGBuffer* PositionBuffer = FSkeletalMeshDeformerHelpers::AllocateVertexFactoryPositionBuffer(GraphBuilder, SkeletalMeshObject, LodIndex, bLodJustChanged,TEXT("OptimusSkinnedMeshPosition"));
		PositionBufferUAV = GraphBuilder.CreateUAV(PositionBuffer, PF_R32_FLOAT, ERDGUnorderedAccessViewFlags::SkipBarrier);
	}
	else
	{
		PositionBufferUAV = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(GWhiteVertexBufferWithRDG->Buffer), PF_R32_FLOAT);
	}	

	// OpenGL ES does not support writing to RGBA16_SNORM images, instead pack data into SINT in the shader
	const EPixelFormat TangentsFormat = IsOpenGLPlatform(GMaxRHIShaderPlatform) ? PF_R16G16B16A16_SINT : PF_R16G16B16A16_SNORM;

	if ((InputMask & static_cast<uint64>(ESkinnedMeshReadDataInterfaceInputSelectorMask::TangentX)) ||
		(InputMask & static_cast<uint64>(ESkinnedMeshReadDataInterfaceInputSelectorMask::TangentZ)))
	{
		FRDGBuffer* TangentBuffer = FSkeletalMeshDeformerHelpers::AllocateVertexFactoryTangentBuffer(GraphBuilder, SkeletalMeshObject, LodIndex, TEXT("OptimusSkinnedMeshTangent"));
		TangentBufferUAV = GraphBuilder.CreateUAV(TangentBuffer, TangentsFormat, ERDGUnorderedAccessViewFlags::SkipBarrier);
	}
	else
	{
		TangentBufferUAV = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(GWhiteVertexBufferWithRDG->Buffer), TangentsFormat);
	}

	if (InputMask & static_cast<uint64>(ESkinnedMeshReadDataInterfaceInputSelectorMask::Color))
	{
		FRDGBuffer* ColorBuffer = FSkeletalMeshDeformerHelpers::AllocateVertexFactoryColorBuffer(GraphBuilder, SkeletalMeshObject, LodIndex, TEXT("OptimusSkinnedMeshColor"));
		// using RGBA here and do a manual fetch swizzle in shader instead of BGRA directly because some Mac does not support it. See GMetalBufferFormats[PF_B8G8R8A8] 
		ColorBufferUAV = GraphBuilder.CreateUAV(ColorBuffer, PF_R8G8B8A8, ERDGUnorderedAccessViewFlags::SkipBarrier);
	}
	else
	{
		ColorBufferUAV = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(GWhiteVertexBufferWithRDG->Buffer), PF_A32B32G32R32F);
	}

	FSkeletalMeshDeformerHelpers::UpdateVertexFactoryBufferOverrides(GraphBuilder.RHICmdList, SkeletalMeshObject, LodIndex);
}





void FOptimusSkinnedMeshReadDataProviderProxy::GatherPermutations(FPermutationData& InOutPermutationData) const
{
	EOptimusSkinnedMeshReadReadableOutputBuffer ReadableOutputBuffers = EOptimusSkinnedMeshReadReadableOutputBuffer::None; 
	if (EnumHasAnyFlags(OutputBuffersFromPreviousInstances, EMeshDeformerOutputBuffer::SkinnedMeshPosition))
	{
		ReadableOutputBuffers |= EOptimusSkinnedMeshReadReadableOutputBuffer::Position;
	}
	if (EnumHasAnyFlags(OutputBuffersFromPreviousInstances, EMeshDeformerOutputBuffer::SkinnedMeshTangents))
	{
		ReadableOutputBuffers |= EOptimusSkinnedMeshReadReadableOutputBuffer::Tangents;
	}
	if (EnumHasAnyFlags(OutputBuffersFromPreviousInstances, EMeshDeformerOutputBuffer::SkinnedMeshVertexColor))
	{
		ReadableOutputBuffers |= EOptimusSkinnedMeshReadReadableOutputBuffer::VertexColor;
	}
	static FString Name(UOptimusSkinnedMeshReadDataInterface::ReadableOutputBufferPermutationName);
	static uint32 Hash = GetTypeHash(Name);
	const uint32 ReadableOutputBuffersBits = InOutPermutationData.PermutationVector.GetPermutationBits(Name, Hash, static_cast<uint32>(ReadableOutputBuffers));
	
	for (int32 InvocationIndex = 0; InvocationIndex < InOutPermutationData.NumInvocations; ++InvocationIndex)
	{
		InOutPermutationData.PermutationIds[InvocationIndex] |= ReadableOutputBuffersBits;
	}
}




void FOptimusSkinnedMeshReadDataProviderProxy::GatherDispatchData(FDispatchData const& InDispatchData)
{
	const int32 LodIndex = SkeletalMeshObject->GetLOD();
	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = &SkeletalMeshRenderData.LODRenderData[LodIndex];

	FRHIShaderResourceView* NullSRVBinding = GWhiteVertexBufferWithSRV->ShaderResourceViewRHI.GetReference();


	
	const TStridedView<FParameters> ParameterArray = MakeStridedParameterView<FParameters>(InDispatchData);
	for (int32 InvocationIndex = 0; InvocationIndex < ParameterArray.Num(); ++InvocationIndex)
	{
		FSkelMeshRenderSection const& RenderSection = LodRenderData->RenderSections[InvocationIndex];

		FParameters& Parameters = ParameterArray[InvocationIndex];
		Parameters.NumVertices = InDispatchData.bUnifiedDispatch ? LodRenderData->GetNumVertices() : RenderSection.GetNumVertices();
		Parameters.InputStreamStart= InDispatchData.bUnifiedDispatch ? 0 : RenderSection.GetVertexBufferIndex();

		Parameters.PositionBufferUAV = PositionBufferUAV;
		Parameters.TangentBufferUAV = TangentBufferUAV;
		Parameters.ColorBufferUAV = ColorBufferUAV;
		
		FRHIShaderResourceView* MeshVertexBufferSRV = LodRenderData->StaticVertexBuffers.PositionVertexBuffer.GetSRV();
		FRHIShaderResourceView* MeshTangentBufferSRV = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetTangentsSRV();
		FRHIShaderResourceView* MeshColorBufferSRV = LodRenderData->StaticVertexBuffers.ColorVertexBuffer.GetColorComponentsSRV();

		Parameters.PositionStaticBuffer = MeshVertexBufferSRV != nullptr ? MeshVertexBufferSRV : NullSRVBinding;
		Parameters.TangentStaticBuffer = MeshTangentBufferSRV != nullptr ? MeshTangentBufferSRV : NullSRVBinding;
		Parameters.ColorStaticBuffer = MeshColorBufferSRV != nullptr ? MeshColorBufferSRV : NullSRVBinding;
		
		// Basically when we are accessing GWhiteVertexBufferWithSRV(NullSRVBinding),
		// we should not access beyond index 0 since the buffer is only a few bytes
		
		// See FGPUSkinPassthroughVertexFactory::UpdateUniformBuffer() and LocalVertexFactory.ush :: GetVertexFactoryIntermediates()
		// Ideally we should be getting this value from the GPUBaseSkinVertexFactory but the need for
		// section index make it tricky when we are doing unified dispatch
		Parameters.ColorIndexMask = MeshColorBufferSRV != nullptr ? ~0u : 0;
	}
}
