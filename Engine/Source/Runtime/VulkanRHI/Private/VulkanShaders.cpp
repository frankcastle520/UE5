// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanShaders.cpp: Vulkan shader RHI implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanPendingState.h"
#include "VulkanContext.h"
#include "GlobalShader.h"
#include "Serialization/MemoryReader.h"
#include "VulkanLLM.h"
#include "VulkanDescriptorSets.h"
#include "RHICoreShader.h"

TAutoConsoleVariable<int32> GDynamicGlobalUBs(
	TEXT("r.Vulkan.DynamicGlobalUBs"),
	2,
	TEXT("2 to treat ALL uniform buffers as dynamic [default]\n")\
	TEXT("1 to treat global/packed uniform buffers as dynamic\n")\
	TEXT("0 to treat them as regular"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
);

static int32 GVulkanCompressSPIRV = 0;
static FAutoConsoleVariableRef GVulkanCompressSPIRVCVar(
	TEXT("r.Vulkan.CompressSPIRV"),
	GVulkanCompressSPIRV,
	TEXT("0 SPIRV source is stored in RAM as-is. (default)\n")
	TEXT("1 SPIRV source is compressed on load and decompressed as when needed, this saves RAM but can introduce hitching when creating shaders."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
);
FCriticalSection FVulkanShader::VulkanShaderModulesMapCS;

FVulkanShaderFactory::~FVulkanShaderFactory()
{
	for (auto& Map : ShaderMap)
	{
		Map.Empty();
	}
}


static void ReadShaderOptionalData(FShaderCodeReader& ShaderCode, FRHIShader* RHIShader)
{
	const FShaderCodePackedResourceCounts* PackedResourceCounts = ShaderCode.FindOptionalData<FShaderCodePackedResourceCounts>();
	if (PackedResourceCounts)
	{
		if (RHIShader->GetFrequency() == SF_Compute)
		{
			RHIShader->SetNoDerivativeOps(EnumHasAnyFlags(PackedResourceCounts->UsageFlags, EShaderResourceUsageFlags::NoDerivativeOps));
		}
		RHIShader->SetShaderBundleUsage(EnumHasAnyFlags(PackedResourceCounts->UsageFlags, EShaderResourceUsageFlags::ShaderBundle));
	}

#if RHI_INCLUDE_SHADER_DEBUG_DATA
	RHIShader->Debug.ShaderName = ShaderCode.FindOptionalData(FShaderCodeName::Key);
	UE::RHICore::SetupShaderCodeValidationData(RHIShader, ShaderCode);
#endif
}

template <typename ShaderType> 
ShaderType* FVulkanShaderFactory::CreateShader(TArrayView<const uint8> Code, FVulkanDevice* Device)
{
	static_assert(ShaderType::StaticFrequency != SF_RayCallable && ShaderType::StaticFrequency != SF_RayGen && ShaderType::StaticFrequency != SF_RayHitGroup && ShaderType::StaticFrequency != SF_RayMiss);

	const uint32 ShaderCodeLen = Code.Num();
	const uint32 ShaderCodeCRC = FCrc::MemCrc32(Code.GetData(), Code.Num());
	const uint64 ShaderKey = ((uint64)ShaderCodeLen | ((uint64)ShaderCodeCRC << 32));

	ShaderType* RetShader = LookupShader<ShaderType>(ShaderKey);

	if (RetShader == nullptr)
	{
		// Do serialize outside of lock
		FMemoryReaderView Ar(Code, true);
		FVulkanShaderHeader CodeHeader;
		Ar << CodeHeader;
		FShaderResourceTable SerializedSRT;
		Ar << SerializedSRT;
		FVulkanShader::FSpirvContainer SpirvContainer;
		Ar << SpirvContainer;

		{
			FRWScopeLock ScopedLock(RWLock[ShaderType::StaticFrequency], SLT_Write);
			FVulkanShader* const* FoundShaderPtr = ShaderMap[ShaderType::StaticFrequency].Find(ShaderKey);
			if (FoundShaderPtr)
			{
				RetShader = static_cast<ShaderType*>(*FoundShaderPtr);
			}
			else
			{
				RetShader = new ShaderType(Device, MoveTemp(SerializedSRT), MoveTemp(CodeHeader), MoveTemp(SpirvContainer), ShaderKey);

				ShaderMap[ShaderType::StaticFrequency].Add(ShaderKey, RetShader);

				FShaderCodeReader ShaderCode(Code);
				ReadShaderOptionalData(ShaderCode, RetShader);
			}
		}
	}

	return RetShader;
}

template <EShaderFrequency ShaderFrequency>
FVulkanRayTracingShader* FVulkanShaderFactory::CreateRayTracingShader(TArrayView<const uint8> Code, FVulkanDevice* Device)
{
	static_assert(ShaderFrequency == SF_RayCallable || ShaderFrequency == SF_RayGen || ShaderFrequency == SF_RayHitGroup || ShaderFrequency == SF_RayMiss);

	auto LookupRayTracingShader = [this](uint64 ShaderKey)
	{
		FVulkanRayTracingShader* RTShader = nullptr;
		if (ShaderKey)
		{
			FRWScopeLock ScopedLock(RWLock[ShaderFrequency], SLT_ReadOnly);
			FVulkanShader* const* FoundShaderPtr = ShaderMap[ShaderFrequency].Find(ShaderKey);
			if (FoundShaderPtr)
			{
				RTShader = static_cast<FVulkanRayTracingShader*>(*FoundShaderPtr);
			}
		}
		return RTShader;
	};

	const uint32 ShaderCodeLen = Code.Num();
	const uint32 ShaderCodeCRC = FCrc::MemCrc32(Code.GetData(), Code.Num());
	const uint64 ShaderKey = ((uint64)ShaderCodeLen | ((uint64)ShaderCodeCRC << 32));

	FVulkanRayTracingShader* RetShader = LookupRayTracingShader(ShaderKey);

	if (RetShader == nullptr)
	{
		// Do serialize outside of lock
		FMemoryReaderView Ar(Code, true);
		FVulkanShaderHeader CodeHeader;
		Ar << CodeHeader;
		FShaderResourceTable SerializedSRT;
		Ar << SerializedSRT;
		FVulkanShader::FSpirvContainer SpirvContainer;
		Ar << SpirvContainer;

		const bool bIsHitGroup = (ShaderFrequency == SF_RayHitGroup);
		FVulkanShader::FSpirvContainer AnyHitSpirvContainer;
		FVulkanShader::FSpirvContainer IntersectionSpirvContainer;
		if (bIsHitGroup)
		{
			if (CodeHeader.RayGroupAnyHit == FVulkanShaderHeader::ERayHitGroupEntrypoint::SeparateBlob)
			{
				Ar << AnyHitSpirvContainer;
			}
			if (CodeHeader.RayGroupIntersection == FVulkanShaderHeader::ERayHitGroupEntrypoint::SeparateBlob)
			{
				Ar << IntersectionSpirvContainer;
			}
		}

		{
			FRWScopeLock ScopedLock(RWLock[ShaderFrequency], SLT_Write);
			FVulkanShader* const* FoundShaderPtr = ShaderMap[ShaderFrequency].Find(ShaderKey);
			if (FoundShaderPtr)
			{
				RetShader = static_cast<FVulkanRayTracingShader*>(*FoundShaderPtr);
			}
			else
			{
				RetShader = new FVulkanRayTracingShader(Device, ShaderFrequency, MoveTemp(SerializedSRT), MoveTemp(CodeHeader), MoveTemp(SpirvContainer), ShaderKey);

				if (bIsHitGroup)
				{
					RetShader->AnyHitSpirvContainer = MoveTemp(AnyHitSpirvContainer);
					RetShader->IntersectionSpirvContainer = MoveTemp(IntersectionSpirvContainer);
				}
				RetShader->RayTracingPayloadType = RetShader->CodeHeader.RayTracingPayloadType;
				RetShader->RayTracingPayloadSize = RetShader->CodeHeader.RayTracingPayloadSize;

				ShaderMap[ShaderFrequency].Add(ShaderKey, RetShader);

				FShaderCodeReader ShaderCode(Code);
				ReadShaderOptionalData(ShaderCode, RetShader);
			}
		}
	}

	return RetShader;
}

void FVulkanShaderFactory::LookupGfxShaders(const uint64 InShaderKeys[ShaderStage::NumGraphicsStages], FVulkanShader* OutShaders[ShaderStage::NumGraphicsStages]) const
{
	for (int32 Idx = 0; Idx < ShaderStage::NumGraphicsStages; ++Idx)
	{
		uint64 ShaderKey = InShaderKeys[Idx];
		if (ShaderKey)
		{
			EShaderFrequency ShaderFrequency = ShaderStage::GetFrequencyForGfxStage((ShaderStage::EStage)Idx);
			FRWScopeLock ScopedLock(RWLock[ShaderFrequency], SLT_ReadOnly);
			FVulkanShader* const * FoundShaderPtr = ShaderMap[ShaderFrequency].Find(ShaderKey);
			if (FoundShaderPtr)
			{
				OutShaders[Idx] = *FoundShaderPtr;
			}
		}
	}
}

void FVulkanShaderFactory::OnDeleteShader(const FVulkanShader& Shader)
{
	const uint64 ShaderKey = Shader.GetShaderKey(); 
	FRWScopeLock ScopedLock(RWLock[Shader.Frequency], SLT_Write);
	ShaderMap[Shader.Frequency].Remove(ShaderKey);
}

FArchive& operator<<(FArchive& Ar, FVulkanShader::FSpirvContainer& SpirvContainer)
{
	uint32 SpirvCodeSizeInBytes;
	Ar << SpirvCodeSizeInBytes;
	check(SpirvCodeSizeInBytes);
	check(Ar.IsLoading());

	TArray<uint8>& SpirvCode = SpirvContainer.SpirvCode;

	if (!GVulkanCompressSPIRV)
	{
		SpirvCode.Reserve(SpirvCodeSizeInBytes);
		SpirvCode.SetNumUninitialized(SpirvCodeSizeInBytes);
		Ar.Serialize(SpirvCode.GetData(), SpirvCodeSizeInBytes);
	}
	else
	{
		const int32 CompressedUpperBound = FCompression::CompressMemoryBound(NAME_Oodle, SpirvCodeSizeInBytes);
		SpirvCode.Reserve(CompressedUpperBound);
		SpirvCode.SetNumUninitialized(CompressedUpperBound);

		TArray<uint8> UncompressedSpirv;
		UncompressedSpirv.SetNumUninitialized(SpirvCodeSizeInBytes);
		Ar.Serialize(UncompressedSpirv.GetData(), SpirvCodeSizeInBytes);

		int32 CompressedSizeBytes = CompressedUpperBound;
		if (FCompression::CompressMemory(NAME_Oodle, SpirvCode.GetData(), CompressedSizeBytes, UncompressedSpirv.GetData(), UncompressedSpirv.GetTypeSize() * UncompressedSpirv.Num(), ECompressionFlags::COMPRESS_BiasSpeed))
		{
			SpirvContainer.UncompressedSizeBytes = SpirvCodeSizeInBytes;
			SpirvCode.SetNumUninitialized(CompressedSizeBytes);
		}
		else
		{
			SpirvCode = MoveTemp(UncompressedSpirv);
		}
	}

	return Ar;
}

FVulkanDevice* FVulkanShaderModule::Device = nullptr;

FVulkanShaderModule::~FVulkanShaderModule()
{
	Device->GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue2::EType::ShaderModule, ActualShaderModule);
}

FVulkanShader::FSpirvCode FVulkanShader::GetSpirvCode(const FSpirvContainer& Container)
{
	if (Container.IsCompressed())
	{
		TArray<uint32> UncompressedSpirv;
		const size_t ElementSize = UncompressedSpirv.GetTypeSize();
		UncompressedSpirv.Reserve(Container.GetSizeBytes() / ElementSize);
		UncompressedSpirv.SetNumUninitialized(Container.GetSizeBytes() / ElementSize);
		FCompression::UncompressMemory(NAME_Oodle, UncompressedSpirv.GetData(), Container.GetSizeBytes(), Container.SpirvCode.GetData(), Container.SpirvCode.Num());

		return FSpirvCode(MoveTemp(UncompressedSpirv));
	}
	else
	{
		return FSpirvCode(TArrayView<uint32>((uint32*)Container.SpirvCode.GetData(), Container.SpirvCode.Num() / sizeof(uint32)));
	}
}


FVulkanShader::FVulkanShader(FVulkanDevice* InDevice, EShaderFrequency InFrequency, FVulkanShaderHeader&& InCodeHeader, FSpirvContainer&& InSpirvContainer, uint64 InShaderKey, TArray<FUniformBufferStaticSlot>& InStaticSlots)
	: StaticSlots(InStaticSlots)
	, ShaderKey(InShaderKey)
	, CodeHeader(MoveTemp(InCodeHeader))
	, Frequency(InFrequency)
	, SpirvContainer(MoveTemp(InSpirvContainer))
	, Device(InDevice)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanShaders);
	check(Device);

	checkf(SpirvContainer.GetSizeBytes() != 0, TEXT("Empty SPIR-V! %s"), *CodeHeader.DebugName);

	const int32 NumGlobalPackedBuffer = (CodeHeader.PackedGlobalsSize > 0) ? 1 : 0;
	if (CodeHeader.UniformBufferInfos.Num() > NumGlobalPackedBuffer)
	{
		StaticSlots.Reserve(CodeHeader.UniformBufferInfos.Num());

		for (const FVulkanShaderHeader::FUniformBufferInfo& UBInfo : CodeHeader.UniformBufferInfos)
		{
			if (const FShaderParametersMetadata* Metadata = FindUniformBufferStructByLayoutHash(UBInfo.LayoutHash))
			{
				StaticSlots.Add(Metadata->GetLayout().StaticSlot);
			}
			else
			{
				StaticSlots.Add(MAX_UNIFORM_BUFFER_STATIC_SLOTS);
			}
		}
	}

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	// main_00000000_00000000
	ANSICHAR EntryPoint[24];
	GetEntryPoint(EntryPoint, 24);
	DebugEntryPoint = EntryPoint;
#endif
}

static TRefCountPtr<FVulkanShaderModule> CreateShaderModule(FVulkanDevice* Device, FVulkanShader::FSpirvCode& SpirvCode)
{
	const TArrayView<uint32> Spirv = SpirvCode.GetCodeView();
	VkShaderModule ShaderModule;
	VkShaderModuleCreateInfo ModuleCreateInfo;
	ZeroVulkanStruct(ModuleCreateInfo, VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
	//ModuleCreateInfo.flags = 0;

	ModuleCreateInfo.codeSize = Spirv.Num() * sizeof(uint32);
	ModuleCreateInfo.pCode = Spirv.GetData();

#if VULKAN_SUPPORTS_VALIDATION_CACHE
	VkShaderModuleValidationCacheCreateInfoEXT ValidationInfo;
	if (Device->GetOptionalExtensions().HasEXTValidationCache)
	{
		ZeroVulkanStruct(ValidationInfo, VK_STRUCTURE_TYPE_SHADER_MODULE_VALIDATION_CACHE_CREATE_INFO_EXT);
		ValidationInfo.validationCache = Device->GetValidationCache();
		ModuleCreateInfo.pNext = &ValidationInfo;
	}
#endif

	VERIFYVULKANRESULT(VulkanRHI::vkCreateShaderModule(Device->GetInstanceHandle(), &ModuleCreateInfo, VULKAN_CPU_ALLOCATOR, &ShaderModule));
	
	TRefCountPtr<FVulkanShaderModule> ReturnPtr = TRefCountPtr<FVulkanShaderModule>(new FVulkanShaderModule(Device, ShaderModule));

	return ReturnPtr;
}

/*
 *  Replace all subpassInput declarations with subpassInputMS
 *  Replace all subpassLoad(Input) with subpassLoad(Input, 0)
 */
FVulkanShader::FSpirvCode FVulkanShader::PatchSpirvInputAttachments(FVulkanShader::FSpirvCode& SpirvCode)
{
	TArrayView<uint32> InSpirv = SpirvCode.GetCodeView();
	const uint32 kHeaderLength = 5;
	const uint32 kOpTypeImage = 25;
	const uint32 kDimSubpassData = 6;
	const uint32 kOpImageRead = 98;
	const uint32 kOpLoad = 61;
	const uint32 kOpConstant = 43;
	const uint32 kOpTypeInt = 21;

	const uint32 Len = InSpirv.Num();
	// Make sure we at least have a header
	if (Len < kHeaderLength)
	{
		return SpirvCode;
	}

	TArray<uint32> OutSpirv;
	OutSpirv.Reserve(Len + 2);
	// Copy header
	OutSpirv.Append(&InSpirv[0], kHeaderLength);

	uint32 IntegerType = 0;
	uint32 Constant0 = 0;
	TArray<uint32, TInlineAllocator<4>> SubpassDataImages;
	
	for (uint32 Pos = kHeaderLength; Pos < Len;)
	{
		uint32* SpirvData = &InSpirv[Pos];
		const uint32 InstLen =	SpirvData[0] >> 16;
		const uint32 Opcode =	SpirvData[0] & 0x0000ffffu;
		bool bSkip = false;

		if (Opcode == kOpTypeInt && SpirvData[3] == 1)
		{
			// signed int
			IntegerType = SpirvData[1];
		}
		else if (Opcode == kOpConstant && SpirvData[1] == IntegerType && SpirvData[3] == 0)
		{
			// const signed int == 0
			Constant0 = SpirvData[2];
		}
		else if (Opcode == kOpTypeImage && SpirvData[3] == kDimSubpassData)
		{
			SpirvData[6] = 1; // mark as multisampled
			SubpassDataImages.Add(SpirvData[1]);
		}
		else if (Opcode == kOpLoad && SubpassDataImages.Contains(SpirvData[1]))
		{
			// pointers to our image
			SubpassDataImages.Add(SpirvData[2]);
		}
		else if (Opcode == kOpImageRead && SubpassDataImages.Contains(SpirvData[3]))
		{
			// const int 0, must be present as it's used for coord operand in image sampling
			check(Constant0 != 0);

			OutSpirv.Add((7u << 16) | kOpImageRead); // new instruction with 7 operands
			OutSpirv.Append(&SpirvData[1], 4); // copy existing operands
			OutSpirv.Add(0x40);			// Sample operand
			OutSpirv.Add(Constant0);	// Sample number
			bSkip = true;
		}

		if (!bSkip)
		{
			OutSpirv.Append(&SpirvData[0], InstLen);
		}
		Pos += InstLen;
	}
	return FVulkanShader::FSpirvCode(MoveTemp(OutSpirv));
}

bool FVulkanShader::NeedsSpirvInputAttachmentPatching(const FGfxPipelineDesc& Desc) const
{
	return (Desc.RasterizationSamples > 1 && CodeHeader.InputAttachmentsMask != 0);
}

TRefCountPtr<FVulkanShaderModule> FVulkanShader::CreateHandle(const FGfxPipelineDesc& Desc, const FVulkanLayout* Layout, uint32 LayoutHash)
{
	FScopeLock Lock(&VulkanShaderModulesMapCS);
	FSpirvCode Spirv = GetPatchedSpirvCode(Desc, Layout);

	TRefCountPtr<FVulkanShaderModule> Module = CreateShaderModule(Device, Spirv);
	ShaderModules.Add(LayoutHash, Module);
	return Module;
}

FVulkanShader::FSpirvCode FVulkanShader::GetPatchedSpirvCode(const FGfxPipelineDesc& Desc, const FVulkanLayout* Layout)
{
	FSpirvCode Spirv = GetSpirvCode();

	if (NeedsSpirvInputAttachmentPatching(Desc))
	{
		Spirv = PatchSpirvInputAttachments(Spirv);
	}

	return Spirv;
}

// Bindless variant of function that does not require layout for patching
TRefCountPtr<FVulkanShaderModule> FVulkanShader::GetOrCreateHandle()
{
	check(Device->SupportsBindless());
	FScopeLock Lock(&VulkanShaderModulesMapCS);

	const uint32 MainModuleIndex = 0;
	TRefCountPtr<FVulkanShaderModule>* Found = ShaderModules.Find(MainModuleIndex);
	if (Found)
	{
		return *Found;
	}

	FSpirvCode Spirv = GetSpirvCode();

	TRefCountPtr<FVulkanShaderModule> Module = CreateShaderModule(Device, Spirv);
	ShaderModules.Add(MainModuleIndex, Module);
	if (!CodeHeader.DebugName.IsEmpty())
	{
		VULKAN_SET_DEBUG_NAME((*Device), VK_OBJECT_TYPE_SHADER_MODULE, Module->GetVkShaderModule(), TEXT("%s : (FVulkanShader*)0x%p"), *CodeHeader.DebugName, this);
	}
	return Module;
}

TRefCountPtr<FVulkanShaderModule> FVulkanRayTracingShader::GetOrCreateHandle(uint32 ModuleIdentifier)
{
	check(Device->SupportsBindless());

	const bool bIsAnyHitModuleIdentifier = (ModuleIdentifier == AnyHitModuleIdentifier);
	const bool bIsIntersectionModuleIdentifier = (ModuleIdentifier == IntersectionModuleIdentifier);

	// If we're using a single blob with multiple entry points, forward everything to the main module
	if ((bIsAnyHitModuleIdentifier && (GetCodeHeader().RayGroupAnyHit == FVulkanShaderHeader::ERayHitGroupEntrypoint::CommonBlob)) ||
		(bIsIntersectionModuleIdentifier && (GetCodeHeader().RayGroupIntersection == FVulkanShaderHeader::ERayHitGroupEntrypoint::CommonBlob)))
	{
		return GetOrCreateHandle(MainModuleIdentifier);
	}

	FScopeLock Lock(&VulkanShaderModulesMapCS);

	TRefCountPtr<FVulkanShaderModule>* Found = ShaderModules.Find(ModuleIdentifier);
	if (Found)
	{
		return *Found;
	}

	auto CreateHitGroupHandle = [&](const FSpirvContainer& Container)
	{
		FSpirvCode Spirv = GetSpirvCode(Container);
		TRefCountPtr<FVulkanShaderModule> Module = CreateShaderModule(Device, Spirv);
		ShaderModules.Add(ModuleIdentifier, Module);
		return Module;
	};

	TRefCountPtr<FVulkanShaderModule> Module;
	if (bIsAnyHitModuleIdentifier)
	{
		check(GetFrequency() == SF_RayHitGroup);
		if (GetCodeHeader().RayGroupAnyHit == FVulkanShaderHeader::ERayHitGroupEntrypoint::SeparateBlob)
		{
			Module = CreateHitGroupHandle(AnyHitSpirvContainer);
		}
		else
		{
			return TRefCountPtr<FVulkanShaderModule>();
		}
	}
	else if (bIsIntersectionModuleIdentifier)
	{
		check(GetFrequency() == SF_RayHitGroup);
		if (GetCodeHeader().RayGroupIntersection == FVulkanShaderHeader::ERayHitGroupEntrypoint::SeparateBlob)
		{
			Module = CreateHitGroupHandle(IntersectionSpirvContainer);
		}
		else
		{
			return TRefCountPtr<FVulkanShaderModule>();
		}
	}
	else
	{
		Module = CreateHitGroupHandle(SpirvContainer);
	}

	if (!CodeHeader.DebugName.IsEmpty())
	{
		VULKAN_SET_DEBUG_NAME((*Device), VK_OBJECT_TYPE_SHADER_MODULE, Module->GetVkShaderModule(), TEXT("%s : (FVulkanShader*)0x%p"), *CodeHeader.DebugName, this);
	}

	return Module;
}


TRefCountPtr<FVulkanShaderModule> FVulkanShader::CreateHandle(const FVulkanLayout* Layout, uint32 LayoutHash)
{
	FScopeLock Lock(&VulkanShaderModulesMapCS);
	FSpirvCode Spirv = GetSpirvCode();

	TRefCountPtr<FVulkanShaderModule> Module = CreateShaderModule(Device, Spirv);
	ShaderModules.Add(LayoutHash, Module);
	if (!CodeHeader.DebugName.IsEmpty())
	{
		VULKAN_SET_DEBUG_NAME((*Device), VK_OBJECT_TYPE_SHADER_MODULE, Module->GetVkShaderModule(), TEXT("%s : (FVulkanShader*)0x%p"), *CodeHeader.DebugName, this);
	}
	return Module;
}

FVulkanShader::~FVulkanShader()
{
	PurgeShaderModules();
	Device->GetShaderFactory().OnDeleteShader(*this);
}

void FVulkanShader::PurgeShaderModules()
{
	FScopeLock Lock(&VulkanShaderModulesMapCS);
	ShaderModules.Empty(0);
}

FVertexShaderRHIRef FVulkanDynamicRHI::RHICreateVertexShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return Device->GetShaderFactory().CreateShader<FVulkanVertexShader>(Code, Device);
}

FPixelShaderRHIRef FVulkanDynamicRHI::RHICreatePixelShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return Device->GetShaderFactory().CreateShader<FVulkanPixelShader>(Code, Device);
}

FMeshShaderRHIRef FVulkanDynamicRHI::RHICreateMeshShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return Device->GetShaderFactory().CreateShader<FVulkanMeshShader>(Code, Device);
}

FAmplificationShaderRHIRef FVulkanDynamicRHI::RHICreateAmplificationShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return Device->GetShaderFactory().CreateShader<FVulkanTaskShader>(Code, Device);
}

FGeometryShaderRHIRef FVulkanDynamicRHI::RHICreateGeometryShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{ 
	return Device->GetShaderFactory().CreateShader<FVulkanGeometryShader>(Code, Device);
}

FComputeShaderRHIRef FVulkanDynamicRHI::RHICreateComputeShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{ 
	return Device->GetShaderFactory().CreateShader<FVulkanComputeShader>(Code, Device);
}

FRayTracingShaderRHIRef FVulkanDynamicRHI::RHICreateRayTracingShader(TArrayView<const uint8> Code, const FSHAHash& Hash, EShaderFrequency ShaderFrequency)
{
	switch (ShaderFrequency)
	{
	case EShaderFrequency::SF_RayGen:
		 return Device->GetShaderFactory().CreateRayTracingShader<SF_RayGen>(Code, Device);

	case EShaderFrequency::SF_RayMiss:
		return Device->GetShaderFactory().CreateRayTracingShader<SF_RayMiss>(Code, Device);

	case EShaderFrequency::SF_RayCallable:
		return Device->GetShaderFactory().CreateRayTracingShader<SF_RayCallable>(Code, Device);

	case EShaderFrequency::SF_RayHitGroup:
		return Device->GetShaderFactory().CreateRayTracingShader<SF_RayHitGroup>(Code, Device);

	default:
		check(false);
		return nullptr;
	}
}

FVulkanLayout::FVulkanLayout(FVulkanDevice* InDevice, bool InGfxLayout)
	: VulkanRHI::FDeviceChild(InDevice)
	, bIsGfxLayout(InGfxLayout)
	, DescriptorSetLayout(Device)
	, PipelineLayout(VK_NULL_HANDLE)
{
}

FVulkanLayout::~FVulkanLayout()
{
	if (PipelineLayout != VK_NULL_HANDLE)
	{
		Device->GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue2::EType::PipelineLayout, PipelineLayout);
		PipelineLayout = VK_NULL_HANDLE;
	}
}

void FVulkanLayout::Compile(FVulkanDescriptorSetLayoutMap& DSetLayoutMap)
{
	check(PipelineLayout == VK_NULL_HANDLE);

	DescriptorSetLayout.Compile(DSetLayoutMap);

	if (!Device->SupportsBindless())
	{
		VkPipelineLayoutCreateInfo PipelineLayoutCreateInfo;
		ZeroVulkanStruct(PipelineLayoutCreateInfo, VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);

		const TArray<VkDescriptorSetLayout>& LayoutHandles = DescriptorSetLayout.GetHandles();
		PipelineLayoutCreateInfo.setLayoutCount = LayoutHandles.Num();
		PipelineLayoutCreateInfo.pSetLayouts = LayoutHandles.GetData();
		//PipelineLayoutCreateInfo.pushConstantRangeCount = 0;
		VERIFYVULKANRESULT(VulkanRHI::vkCreatePipelineLayout(Device->GetInstanceHandle(), &PipelineLayoutCreateInfo, VULKAN_CPU_ALLOCATOR, &PipelineLayout));
	}
}

uint32 FVulkanDescriptorSetWriter::SetupDescriptorWrites(
	const TArray<VkDescriptorType>& Types, FVulkanHashableDescriptorInfo* InHashableDescriptorInfos,
	VkWriteDescriptorSet* InWriteDescriptors, VkDescriptorImageInfo* InImageInfo, VkDescriptorBufferInfo* InBufferInfo, uint8* InBindingToDynamicOffsetMap,
	VkWriteDescriptorSetAccelerationStructureKHR* InAccelerationStructuresWriteDescriptors,
	VkAccelerationStructureKHR* InAccelerationStructures,
	const FVulkanSamplerState& DefaultSampler, const FVulkanView::FTextureView& DefaultImageView)
{
	HashableDescriptorInfos = InHashableDescriptorInfos;
	WriteDescriptors = InWriteDescriptors;
	NumWrites = Types.Num();

	BindingToDynamicOffsetMap = InBindingToDynamicOffsetMap;

	InitWrittenMasks(NumWrites);

	uint32 DynamicOffsetIndex = 0;

	for (int32 Index = 0; Index < Types.Num(); ++Index)
	{
		InWriteDescriptors->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		InWriteDescriptors->dstBinding = Index;
		InWriteDescriptors->descriptorCount = 1;
		InWriteDescriptors->descriptorType = Types[Index];

		switch (Types[Index])
		{
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
			BindingToDynamicOffsetMap[Index] = DynamicOffsetIndex;
			++DynamicOffsetIndex;
			InWriteDescriptors->pBufferInfo = InBufferInfo++;
			break;
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			InWriteDescriptors->pBufferInfo = InBufferInfo++;
			break;
		case VK_DESCRIPTOR_TYPE_SAMPLER:
			SetWrittenBase(Index); //samplers have a default setting, don't assert on those yet.
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
		case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
			// Texture.Load() still requires a default sampler...
			if (InHashableDescriptorInfos) // UseVulkanDescriptorCache()
			{
				InHashableDescriptorInfos[Index].Image.SamplerId = DefaultSampler.SamplerId;
				InHashableDescriptorInfos[Index].Image.ImageViewId = DefaultImageView.ViewId;
				InHashableDescriptorInfos[Index].Image.ImageLayout = static_cast<uint32>(VK_IMAGE_LAYOUT_GENERAL);
			}
			InImageInfo->sampler = DefaultSampler.Sampler;
			InImageInfo->imageView = DefaultImageView.View;
			InImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			InWriteDescriptors->pImageInfo = InImageInfo++;
			break;
		case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
		case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
			break;
		case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
			InAccelerationStructuresWriteDescriptors->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
			InAccelerationStructuresWriteDescriptors->pNext = nullptr;
			InAccelerationStructuresWriteDescriptors->accelerationStructureCount = 1;
			InAccelerationStructuresWriteDescriptors->pAccelerationStructures = InAccelerationStructures++;
			InWriteDescriptors->pNext = InAccelerationStructuresWriteDescriptors++;
			break;
		default:
			checkf(0, TEXT("Unsupported descriptor type %d"), (int32)Types[Index]);
			break;
		}
		++InWriteDescriptors;
	}

	return DynamicOffsetIndex;
}

void FVulkanDescriptorSetsLayoutInfo::ProcessBindingsForStage(VkShaderStageFlagBits StageFlags, ShaderStage::EStage DescSetStage, const FVulkanShaderHeader& CodeHeader, FUniformBufferGatherInfo& OutUBGatherInfo) const
{
	OutUBGatherInfo.CodeHeaders[DescSetStage] = &CodeHeader;
}

template<bool bIsCompute>
void FVulkanDescriptorSetsLayoutInfo::FinalizeBindings(const FVulkanDevice& Device, const FUniformBufferGatherInfo& UBGatherInfo, const TArrayView<FRHISamplerState*>& ImmutableSamplers)
{
	// We'll be reusing this struct
	VkDescriptorSetLayoutBinding Binding;
	FMemory::Memzero(Binding);
	Binding.descriptorCount = 1;

	const bool bConvertAllUBsToDynamic = !Device.SupportsBindless() && (GDynamicGlobalUBs.GetValueOnAnyThread() > 1);
	const bool bConvertPackedUBsToDynamic = !Device.SupportsBindless() && (bConvertAllUBsToDynamic || (GDynamicGlobalUBs.GetValueOnAnyThread() == 1));
	const uint32 MaxDescriptorSetUniformBuffersDynamic = Device.GetLimits().maxDescriptorSetUniformBuffersDynamic;

	int32 CurrentImmutableSampler = 0;
	for (int32 Stage = 0; Stage < (bIsCompute ? ShaderStage::NumComputeStages : ShaderStage::NumGraphicsStages); ++Stage)
	{
		checkSlow(StageInfos[Stage].IsEmpty());

		if (const FVulkanShaderHeader* ShaderHeader = UBGatherInfo.CodeHeaders[Stage])
		{
			FStageInfo& StageInfo = StageInfos[Stage];

			Binding.stageFlags = UEFrequencyToVKStageBit(bIsCompute ? SF_Compute : ShaderStage::GetFrequencyForGfxStage((ShaderStage::EStage)Stage));

			StageInfo.PackedGlobalsSize = ShaderHeader->PackedGlobalsSize;
			StageInfo.NumBoundUniformBuffers = ShaderHeader->NumBoundUniformBuffers;

			for (int32 BindingIndex = 0; BindingIndex < ShaderHeader->Bindings.Num(); ++BindingIndex)
			{
				const VkDescriptorType DescriptorType = (VkDescriptorType)ShaderHeader->Bindings[BindingIndex].DescriptorType;

				const bool bIsUniformBuffer = (DescriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
				const bool bIsGlobalPackedConstants = bIsUniformBuffer && ShaderHeader->PackedGlobalsSize && (BindingIndex == 0);

				if (bIsGlobalPackedConstants)
				{
					const VkDescriptorType UBType = bConvertPackedUBsToDynamic ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

					const uint32 NewBindingIndex = StageInfo.Types.Add(UBType);
					checkf(NewBindingIndex == 0, TEXT("Packed globals should always be the first binding!"));

					Binding.binding = NewBindingIndex;
					Binding.descriptorType = UBType;
					AddDescriptor(Stage, Binding);
				}
				else if (bIsUniformBuffer)
				{
					VkDescriptorType UBType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
					if (bConvertAllUBsToDynamic && LayoutTypes[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC] < MaxDescriptorSetUniformBuffersDynamic)
					{
						UBType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
					}

					// Here we might mess up with the stageFlags, so reset them every loop
					Binding.descriptorType = UBType;
					const FVulkanShaderHeader::FUniformBufferInfo& UBInfo = ShaderHeader->UniformBufferInfos[BindingIndex];
					const bool bUBHasConstantData = (BindingIndex < (int32)ShaderHeader->NumBoundUniformBuffers);
					if (bUBHasConstantData)
					{
						const uint32 NewBindingIndex = StageInfo.Types.Add(UBType);
						check(NewBindingIndex == BindingIndex);
						Binding.binding = NewBindingIndex;
						AddDescriptor(Stage, Binding);
					}
				}
				else
				{
					const uint32 NewTypeIndex = StageInfo.Types.Add(DescriptorType);
					check(NewTypeIndex == BindingIndex);
					Binding.binding = BindingIndex;
					Binding.descriptorType = DescriptorType;
					AddDescriptor(Stage, Binding);
				}
			}
		}
	}

	CompileTypesUsageID();
	GenerateHash(ImmutableSamplers, bIsCompute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS);
}

FVulkanBoundShaderState::FVulkanBoundShaderState(FRHIVertexDeclaration* InVertexDeclarationRHI, FRHIVertexShader* InVertexShaderRHI,
	FRHIPixelShader* InPixelShaderRHI, FRHIGeometryShader* InGeometryShaderRHI)
	: CacheLink(InVertexDeclarationRHI, InVertexShaderRHI, InPixelShaderRHI, InGeometryShaderRHI, this)
{
	CacheLink.AddToCache();
}

FVulkanBoundShaderState::~FVulkanBoundShaderState()
{
	CacheLink.RemoveFromCache();
}

FBoundShaderStateRHIRef FVulkanDynamicRHI::RHICreateBoundShaderState(
	FRHIVertexDeclaration* VertexDeclarationRHI,
	FRHIVertexShader* VertexShaderRHI,
	FRHIPixelShader* PixelShaderRHI,
	FRHIGeometryShader* GeometryShaderRHI
	)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanShaders);
	FBoundShaderStateRHIRef CachedBoundShaderState = GetCachedBoundShaderState_Threadsafe(
		VertexDeclarationRHI,
		VertexShaderRHI,
		PixelShaderRHI,
		GeometryShaderRHI
	);
	if (CachedBoundShaderState.GetReference())
	{
		// If we've already created a bound shader state with these parameters, reuse it.
		return CachedBoundShaderState;
	}

	return new FVulkanBoundShaderState(VertexDeclarationRHI, VertexShaderRHI, PixelShaderRHI, GeometryShaderRHI);
}


template void FVulkanDescriptorSetsLayoutInfo::FinalizeBindings<true>(const FVulkanDevice& Device, const FUniformBufferGatherInfo& UBGatherInfo, const TArrayView<FRHISamplerState*>& ImmutableSamplers);
template void FVulkanDescriptorSetsLayoutInfo::FinalizeBindings<false>(const FVulkanDevice& Device, const FUniformBufferGatherInfo& UBGatherInfo, const TArrayView<FRHISamplerState*>& ImmutableSamplers);
