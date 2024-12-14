// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D12Shaders.cpp: D3D shader RHI implementation.
=============================================================================*/

#include "D3D12RHIPrivate.h"
#include "RHICoreShader.h"

template <typename TShaderType>
static inline bool ReadShaderOptionalData(FShaderCodeReader& InShaderCode, TShaderType& OutShader)
{
	const FShaderCodePackedResourceCounts* PackedResourceCounts = InShaderCode.FindOptionalData<FShaderCodePackedResourceCounts>();
	if (!PackedResourceCounts)
	{
		return false;
	}
	OutShader.ResourceCounts = *PackedResourceCounts;

#if RHI_INCLUDE_SHADER_DEBUG_DATA
	OutShader.Debug.ShaderName = InShaderCode.FindOptionalData(FShaderCodeName::Key);

	int32 UniformBufferTableSize = 0;
	const uint8* UniformBufferData = InShaderCode.FindOptionalDataAndSize(FShaderCodeUniformBuffers::Key, UniformBufferTableSize);
	if (UniformBufferData && UniformBufferTableSize > 0)
	{
		FBufferReader UBReader((void*)UniformBufferData, UniformBufferTableSize, false);
		TArray<FString> Names;
		UBReader << Names;
		check(OutShader.Debug.UniformBufferNames.Num() == 0);
		for (int32 Index = 0; Index < Names.Num(); ++Index)
		{
			OutShader.Debug.UniformBufferNames.Add(FName(*Names[Index]));
		}
	}
#endif

#if D3D12RHI_NEEDS_VENDOR_EXTENSIONS
	int32 VendorExtensionTableSize = 0;
	auto* VendorExtensionData = InShaderCode.FindOptionalDataAndSize(FShaderCodeVendorExtension::Key, VendorExtensionTableSize);
	if (VendorExtensionData && VendorExtensionTableSize > 0)
	{
		FBufferReader Ar((void*)VendorExtensionData, VendorExtensionTableSize, false);
		Ar << OutShader.VendorExtensions;
	}
#endif

#if D3D12RHI_NEEDS_SHADER_FEATURE_CHECKS
	if (const FShaderCodeFeatures* CodeFeatures = InShaderCode.FindOptionalData<FShaderCodeFeatures>())
	{
		OutShader.Features = CodeFeatures->CodeFeatures;
	}
#endif

	int32 ShaderBindingLayoutSize = 0;
	auto* ShaderBindingLayoutData = InShaderCode.FindOptionalDataAndSize(FShaderCodeShaderResourceTableDataDesc::Key, ShaderBindingLayoutSize);
	if (ShaderBindingLayoutData && ShaderBindingLayoutSize > 0)
	{
		check(ShaderBindingLayoutSize == sizeof(OutShader.ShaderBindingLayoutHash));
		FBufferReader Ar((void*)ShaderBindingLayoutData, ShaderBindingLayoutSize, false);
		Ar << OutShader.ShaderBindingLayoutHash;
	}

	UE::RHICore::SetupShaderCodeValidationData(&OutShader, InShaderCode);
	UE::RHICore::SetupShaderDiagnosticData(&OutShader, InShaderCode);

	return true;
}

static bool ValidateShaderIsUsable(FD3D12ShaderData* InShader, EShaderFrequency InFrequency)
{
#if D3D12RHI_NEEDS_SHADER_FEATURE_CHECKS
	if ((InFrequency == SF_Mesh || InFrequency == SF_Amplification) && !GRHISupportsMeshShadersTier0)
	{
		UE_LOG(LogD3D12RHI, Log, TEXT("Trying to create Mesh or Amplication shader but RHI doesn't support MeshShaders"));
		return false;
	}

	// When we're using the SM5 shader library plus raytracing, GRHISupportsWaveOperations is false because DXBC shaders can't do wave ops,
	// but RT shaders are compiled to DXIL and can use wave ops (HW support is guaranteed too).
	if (EnumHasAnyFlags(InShader->Features, EShaderCodeFeatures::WaveOps) && !GRHISupportsWaveOperations && !IsRayTracingShaderFrequency(InFrequency))
	{
		UE_LOG(LogD3D12RHI, Log, TEXT("Trying to create shader with WaveOps but RHI doesn't support WaveOperations"));
		return false;
	}

	if (EnumHasAnyFlags(InShader->Features, EShaderCodeFeatures::BindlessResources | EShaderCodeFeatures::BindlessSamplers))
	{
		if (GRHIBindlessSupport == ERHIBindlessSupport::Unsupported ||
			(GRHIBindlessSupport == ERHIBindlessSupport::RayTracingOnly && !IsRayTracingShaderFrequency(InFrequency)))
		{
			UE_LOG(LogD3D12RHI, Log, TEXT("Trying to create shader with bindless resources or samplers but RHI doesn't support Bindless"));
			return false;
		}
	}

	if (InFrequency == SF_Pixel && EnumHasAnyFlags(InShader->Features, EShaderCodeFeatures::StencilRef) && !GRHISupportsStencilRefFromPixelShader)
	{
		UE_LOG(LogD3D12RHI, Log, TEXT("Trying to create pixel shader with stencil ref but RHI doesn't support StencilRefFromPixelShader"));
		return false;
	}

	if (EnumHasAnyFlags(InShader->Features, EShaderCodeFeatures::BarycentricsSemantic) && !GRHIGlobals.SupportsBarycentricsSemantic)
	{
		UE_LOG(LogD3D12RHI, Log, TEXT("Trying to create shader with BarycentricsSemantic but RHI doesn't support BarycentricsSemantic"));
		return false;
	}
#endif

	return true;
}

template <typename TShaderType>
bool InitShaderCommon(FShaderCodeReader& ShaderCode, int32 Offset, TShaderType* InShader)
{
	if (!ReadShaderOptionalData(ShaderCode, *InShader))
	{
		return false;
	}

	if (!ValidateShaderIsUsable(InShader, InShader->GetFrequency()))
	{
		return false;
	}

	// Copy the native shader data only, skipping any of our own headers.
	InShader->Code = ShaderCode.GetOffsetShaderCode(Offset);
	InShader->SetShaderBundleUsage(EnumHasAnyFlags(InShader->ResourceCounts.UsageFlags, EShaderResourceUsageFlags::ShaderBundle));

	return true;
}

template <typename TShaderType, typename LambdaType>
TShaderType* InitStandardShaderWithCustomSerialization(TShaderType* InShader, TArrayView<const uint8> InCode, const LambdaType& CustomSerializationLambda)
{
	FShaderCodeReader ShaderCode(InCode);

	FMemoryReaderView Ar(InCode, true);
	InShader->SerializeShaderResourceTable(Ar);

	int32 Offset = Ar.Tell();

	CustomSerializationLambda(Ar, InShader, Offset);

	if (!InitShaderCommon(ShaderCode, Offset, InShader))
	{
		InShader->AddRef();
		InShader->Release();
		return nullptr;
	}

	UE::RHICore::InitStaticUniformBufferSlots(InShader);

	return InShader;
}

template <typename TShaderType>
TShaderType* InitStandardShader(TShaderType* InShader, TArrayView<const uint8> InCode)
{
	auto CustomSerialization = [](FMemoryReaderView&, TShaderType*, int32&) {};
	return InitStandardShaderWithCustomSerialization<TShaderType>(InShader, InCode, CustomSerialization);
}

FVertexShaderRHIRef FD3D12DynamicRHI::RHICreateVertexShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return InitStandardShader(new FD3D12VertexShader(), Code);
}

FMeshShaderRHIRef FD3D12DynamicRHI::RHICreateMeshShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return InitStandardShader(new FD3D12MeshShader(), Code);
}

FAmplificationShaderRHIRef FD3D12DynamicRHI::RHICreateAmplificationShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return InitStandardShader(new FD3D12AmplificationShader(), Code);
}

FPixelShaderRHIRef FD3D12DynamicRHI::RHICreatePixelShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return InitStandardShader(new FD3D12PixelShader(), Code);
}

FGeometryShaderRHIRef FD3D12DynamicRHI::RHICreateGeometryShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return InitStandardShader(new FD3D12GeometryShader(), Code);
}

FComputeShaderRHIRef FD3D12DynamicRHI::RHICreateComputeShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	FD3D12ComputeShader* Shader = InitStandardShader(new FD3D12ComputeShader(), Code);
	if (Shader)
	{
		Shader->RootSignature = GetAdapter().GetRootSignature(Shader);
		Shader->SetNoDerivativeOps(EnumHasAnyFlags(Shader->ResourceCounts.UsageFlags, EShaderResourceUsageFlags::NoDerivativeOps));
	}

	return Shader;
}

FWorkGraphShaderRHIRef FD3D12DynamicRHI::RHICreateWorkGraphShader(TArrayView<const uint8> Code, const FSHAHash& Hash, EShaderFrequency ShaderFrequency)
{
	auto CustomSerialization = [](FMemoryReaderView& Ar, FD3D12WorkGraphShader* Shader, int32& Offset)
	{
		Ar << Shader->EntryPoint;
		Offset = Ar.Tell();
	};

	FD3D12WorkGraphShader* Shader = InitStandardShaderWithCustomSerialization(new FD3D12WorkGraphShader(ShaderFrequency), Code, CustomSerialization);
	if (Shader)
	{
		Shader->RootSignature = GetAdapter().GetRootSignature(Shader);
	}

	return Shader;
}

#if D3D12_RHI_RAYTRACING

FRayTracingShaderRHIRef FD3D12DynamicRHI::RHICreateRayTracingShader(TArrayView<const uint8> Code, const FSHAHash& Hash, EShaderFrequency ShaderFrequency)
{
	checkf(GRHISupportsRayTracing && GRHISupportsRayTracingShaders, TEXT("Tried to create RayTracing shader but RHI doesn't support it!"));

	auto CustomSerialization = [this, ShaderFrequency](FMemoryReaderView& Ar, FD3D12RayTracingShader* Shader, int32& Offset)
	{
		Ar << Shader->EntryPoint;
		Ar << Shader->AnyHitEntryPoint;
		Ar << Shader->IntersectionEntryPoint;
		Ar << Shader->RayTracingPayloadType;
		Ar << Shader->RayTracingPayloadSize;
	
		checkf(Shader->RayTracingPayloadType != 0, TEXT("Ray Tracing Shader must not have an empty payload type!"));
		checkf((FMath::CountBits(Shader->RayTracingPayloadType) == 1 && (ShaderFrequency == SF_RayHitGroup || ShaderFrequency == SF_RayMiss || ShaderFrequency == SF_RayCallable)) ||
			(FMath::CountBits(Shader->RayTracingPayloadType) >= 1 && (ShaderFrequency == SF_RayGen)),
			TEXT("Ray Tracing Shader has %d bits set, which is not the expected count for shader frequency %d"), FMath::CountBits(Shader->RayTracingPayloadType), int(ShaderFrequency)
		);

		Offset = Ar.Tell();

		int32 PrecompiledKey = 0;
		Ar << PrecompiledKey;
		if (PrecompiledKey == RayTracingPrecompiledPSOKey)
		{
			Offset += sizeof(PrecompiledKey); // Skip the precompiled PSO marker if it's present
			Shader->bPrecompiledPSO = true;
		}
	};

	FD3D12RayTracingShader* Shader = InitStandardShaderWithCustomSerialization(new FD3D12RayTracingShader(ShaderFrequency), Code, CustomSerialization);

	// Setup local root signature (RayGen only has global root signature)
	if (Shader && ShaderFrequency != SF_RayGen)
	{
		Shader->LocalRootSignature = GetAdapter().GetLocalRootSignature(Shader);
		Shader->LocalBindingDataSize = Shader->LocalRootSignature->GetTotalRootSignatureSizeInBytes();
	}

	return Shader;
}

#endif // D3D12_RHI_RAYTRACING

FShaderBundleRHIRef FD3D12DynamicRHI::RHICreateShaderBundle(const FShaderBundleCreateInfo& CreateInfo)
{
	FD3D12ShaderBundle* ShaderBundle = new FD3D12ShaderBundle(GetRHIDevice(0), CreateInfo);
	return ShaderBundle;
}

void FD3D12CommandContext::RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data)
{
	// Structures are chosen to be directly mappable
	StateCache.SetViewports(Count, reinterpret_cast<const D3D12_VIEWPORT*>(Data));
}

FBoundShaderStateRHIRef FD3D12DynamicRHI::RHICreateBoundShaderState(
	FRHIVertexDeclaration* VertexDeclarationRHI,
	FRHIVertexShader* VertexShaderRHI,
	FRHIPixelShader* PixelShaderRHI,
	FRHIGeometryShader* GeometryShaderRHI
	)
{
	checkNoEntry();
	return nullptr;
}
