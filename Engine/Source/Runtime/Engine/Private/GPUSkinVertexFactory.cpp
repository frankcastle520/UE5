// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GPUVertexFactory.cpp: GPU skin vertex factory implementation
=============================================================================*/

#include "GPUSkinVertexFactory.h"
#include "Animation/MeshDeformerProvider.h"
#include "MeshBatch.h"
#include "GPUSkinCache.h"
#include "MeshDrawShaderBindings.h"
#include "MeshMaterialShader.h"
#include "Misc/DelayedAutoRegister.h"
#include "SkeletalRenderGPUSkin.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "RenderGraphResources.h"
#include "RenderUtils.h"
#include "ShaderPlatformCachedIniValue.h"
#include "Engine/RendererSettings.h"
#include "Rendering/RenderCommandPipes.h"

#if INTEL_ISPC
#include "GPUSkinVertexFactory.ispc.generated.h"
#endif

// Changing this is currently unsupported after content has been chunked with the previous setting
// Changing this causes a full shader recompile
static int32 GCVarMaxGPUSkinBones = FGPUBaseSkinVertexFactory::GHardwareMaxGPUSkinBones;
static FAutoConsoleVariableRef CVarMaxGPUSkinBones(
	TEXT("Compat.MAX_GPUSKIN_BONES"),
	GCVarMaxGPUSkinBones,
	TEXT("Max number of bones that can be skinned on the GPU in a single draw call. This setting clamp the per platform project setting URendererSettings::MaxSkinBones. Cannot be changed at runtime."),
	ECVF_ReadOnly);

static int32 GCVarSupport16BitBoneIndex = 0;
static FAutoConsoleVariableRef CVarSupport16BitBoneIndex(
	TEXT("r.GPUSkin.Support16BitBoneIndex"),
	GCVarSupport16BitBoneIndex,
	TEXT("If enabled, a new mesh imported will use 8 bit (if <=256 bones) or 16 bit (if > 256 bones) bone indices for rendering."),
	ECVF_ReadOnly);

// Whether to use 2 bones influence instead of default 4 for GPU skinning
// Changing this causes a full shader recompile
static TAutoConsoleVariable<int32> CVarGPUSkinLimit2BoneInfluences(
	TEXT("r.GPUSkin.Limit2BoneInfluences"),
	0,	
	TEXT("Whether to use 2 bones influence instead of default 4/8 for GPU skinning. Cannot be changed at runtime."),
	ECVF_ReadOnly);

static int32 GCVarUnlimitedBoneInfluences = 0;
static FAutoConsoleVariableRef CVarUnlimitedBoneInfluences(
	TEXT("r.GPUSkin.UnlimitedBoneInfluences"),
	GCVarUnlimitedBoneInfluences,
	TEXT("Whether to use unlimited bone influences instead of default 4/8 for GPU skinning. Cannot be changed at runtime."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static int32 GCVarUnlimitedBoneInfluencesThreshold = EXTRA_BONE_INFLUENCES;
static FAutoConsoleVariableRef CVarUnlimitedBoneInfluencesThreshold(
	TEXT("r.GPUSkin.UnlimitedBoneInfluencesThreshold"),
	GCVarUnlimitedBoneInfluencesThreshold,
	TEXT("Unlimited Bone Influences Threshold to use unlimited bone influences buffer if r.GPUSkin.UnlimitedBoneInfluences is enabled. Should be unsigned int. Cannot be changed at runtime."),
	ECVF_ReadOnly);

static bool GCVarAlwaysUseDeformerForUnlimitedBoneInfluences = false;
static FAutoConsoleVariableRef CVarAlwaysUseDeformerForUnlimitedBoneInfluences(
	TEXT("r.GPUSkin.AlwaysUseDeformerForUnlimitedBoneInfluences"),
	GCVarAlwaysUseDeformerForUnlimitedBoneInfluences,
	TEXT("Any meshes using Unlimited Bone Influences will always be rendered with a Mesh Deformer. This reduces the number of shader permutations needed for skeletal mesh materials, saving memory at the cost of performance. Has no effect if either Unlimited Bone Influences or Deformer Graph is disabled. Cannot be changed at runtime."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<bool> CVarMobileEnableCloth(
	TEXT("r.Mobile.EnableCloth"),
	true,
	TEXT("If enabled, compile cloth shader permutations and render simulated cloth on mobile platforms and Mobile mode on PC. Cannot be changed at runtime"),
	ECVF_ReadOnly);

#define IMPLEMENT_GPUSKINNING_VERTEX_FACTORY_TYPE_INTERNAL(FactoryClass, ShaderFilename, Flags) \
	template <GPUSkinBoneInfluenceType BoneInfluenceType> FVertexFactoryType FactoryClass<BoneInfluenceType>::StaticType( \
	BoneInfluenceType == DefaultBoneInfluence ? TEXT(#FactoryClass) TEXT("Default") : TEXT(#FactoryClass) TEXT("Unlimited"), \
	TEXT(ShaderFilename), \
	Flags | EVertexFactoryFlags::SupportsPrimitiveIdStream, \
	IMPLEMENT_VERTEX_FACTORY_VTABLE(FactoryClass<BoneInfluenceType>) \
	); \
	template <GPUSkinBoneInfluenceType BoneInfluenceType> inline FVertexFactoryType* FactoryClass<BoneInfluenceType>::GetType() const { return &StaticType; }


#define IMPLEMENT_GPUSKINNING_VERTEX_FACTORY_TYPE(FactoryClass, ShaderFilename, Flags) \
	IMPLEMENT_GPUSKINNING_VERTEX_FACTORY_TYPE_INTERNAL(FactoryClass, ShaderFilename, Flags) \
	template class FactoryClass<DefaultBoneInfluence>;	\
	template class FactoryClass<UnlimitedBoneInfluence>;

#define IMPLEMENT_GPUSKINNING_VERTEX_FACTORY_PARAMETER_TYPE(FactoryClass, Frequency, ParameterType) \
	IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FactoryClass<DefaultBoneInfluence>, Frequency, ParameterType); \
	IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FactoryClass<UnlimitedBoneInfluence>, Frequency, ParameterType)

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
static TAutoConsoleVariable<int32> CVarVelocityTest(
	TEXT("r.VelocityTest"),
	0,
	TEXT("Allows to enable some low level testing code for the velocity rendering (Affects object motion blur and TemporalAA).")
	TEXT(" 0: off (default)")
	TEXT(" 1: add random data to the buffer where we store skeletal mesh bone data to test if the code (good to test in PAUSED as well)."),
	ECVF_Cheat | ECVF_RenderThreadSafe);
#endif // if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

// Disable it by default as it seems to be up to 20% slower on current gen platforms
#if !defined(GPU_SKIN_COPY_BONES_ISPC_ENABLED_DEFAULT)
#define GPU_SKIN_COPY_BONES_ISPC_ENABLED_DEFAULT 0
#endif

// Support run-time toggling on supported platforms in non-shipping configurations
#if !INTEL_ISPC || UE_BUILD_SHIPPING
static constexpr bool bGPUSkin_CopyBones_ISPC_Enabled = INTEL_ISPC && GPU_SKIN_COPY_BONES_ISPC_ENABLED_DEFAULT;
#else
static bool bGPUSkin_CopyBones_ISPC_Enabled = GPU_SKIN_COPY_BONES_ISPC_ENABLED_DEFAULT;
static FAutoConsoleVariableRef CVarGPUSkinCopyBonesISPCEnabled(TEXT("r.GPUSkin.CopyBones.ISPC"), bGPUSkin_CopyBones_ISPC_Enabled, TEXT("Whether to use ISPC optimizations when copying bones for GPU skinning"));
#endif

#if INTEL_ISPC
static_assert(sizeof(ispc::FMatrix44f) == sizeof(FMatrix44f), "sizeof(ispc::FMatrix44f) != sizeof(FMatrix44f)");
static_assert(sizeof(ispc::FMatrix3x4) == sizeof(FMatrix3x4), "sizeof(ispc::FMatrix3x4) != sizeof(FMatrix3x4)");
#endif

class FNullMorphVertexBuffer : public FVertexBuffer
{
public:
	FNullMorphVertexBuffer() = default;
	~FNullMorphVertexBuffer() = default;

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override
	{
		// Enough data for 64k vertices mesh
		uint32 Size = sizeof(FMorphGPUSkinVertex) * 65535;
		FRHIResourceCreateInfo CreateInfo(TEXT("FNullMorphVertexBuffer"));
		VertexBufferRHI = RHICmdList.CreateBuffer(Size, BUF_Static | BUF_VertexBuffer | BUF_ShaderResource, 0, ERHIAccess::VertexOrIndexBuffer | ERHIAccess::SRVMask, CreateInfo);
		void* LockedData = RHICmdList.LockBuffer(VertexBufferRHI, 0, Size, RLM_WriteOnly);
		FMemory::Memzero(LockedData, Size);
		RHICmdList.UnlockBuffer(VertexBufferRHI);
	}
};

TGlobalResource<FNullMorphVertexBuffer, FRenderResource::EInitPhase::Pre> GNullMorphVertexBuffer;

/*-----------------------------------------------------------------------------
 FSharedPoolPolicyData
 -----------------------------------------------------------------------------*/
uint32 FSharedPoolPolicyData::GetPoolBucketIndex(uint32 Size)
{
	unsigned long Lower = 0;
	unsigned long Upper = NumPoolBucketSizes;
	unsigned long Middle;
	
	do
	{
		Middle = ( Upper + Lower ) >> 1;
		if( Size <= BucketSizes[Middle-1] )
		{
			Upper = Middle;
		}
		else
		{
			Lower = Middle;
		}
	}
	while( Upper - Lower > 1 );
	
	check( Size <= BucketSizes[Lower] );
	check( (Lower == 0 ) || ( Size > BucketSizes[Lower-1] ) );
	
	return Lower;
}

uint32 FSharedPoolPolicyData::GetPoolBucketSize(uint32 Bucket)
{
	check(Bucket < NumPoolBucketSizes);
	return BucketSizes[Bucket];
}

uint32 FSharedPoolPolicyData::BucketSizes[NumPoolBucketSizes] = {
	16, 48, 96, 192, 384, 768, 1536, 
	3072, 4608, 6144, 7680, 9216, 12288, 
	65536, 131072, 262144, 786432, 1572864 // these 5 numbers are added for large cloth simulation vertices, supports up to 65,536 verts
};

/*-----------------------------------------------------------------------------
 FBoneBufferPoolPolicy
 -----------------------------------------------------------------------------*/
FVertexBufferAndSRV FBoneBufferPoolPolicy::CreateResource(FRHICommandListBase& RHICmdList, CreationArguments Args)
{
	uint32 BufferSize = GetPoolBucketSize(GetPoolBucketIndex(Args));
	// in VisualStudio the copy constructor call on the return argument can be optimized out
	// see https://msdn.microsoft.com/en-us/library/ms364057.aspx#nrvo_cpp05_topic3
	FVertexBufferAndSRV Buffer;
	FRHIResourceCreateInfo CreateInfo(TEXT("FBoneBufferPoolPolicy"));
	Buffer.VertexBufferRHI = RHICmdList.CreateVertexBuffer( BufferSize, (BUF_Dynamic | BUF_ShaderResource), CreateInfo );
	Buffer.VertexBufferSRV = RHICmdList.CreateShaderResourceView( Buffer.VertexBufferRHI, sizeof(FVector4f), PF_A32B32G32R32F );
	return Buffer;
}

FSharedPoolPolicyData::CreationArguments FBoneBufferPoolPolicy::GetCreationArguments(const FVertexBufferAndSRV& Resource)
{
	return Resource.VertexBufferRHI->GetSize();
}

void FBoneBufferPoolPolicy::FreeResource(FVertexBufferAndSRV Resource)
{
}

FVertexBufferAndSRV FClothBufferPoolPolicy::CreateResource(FRHICommandListBase& RHICmdList, CreationArguments Args)
{
	uint32 BufferSize = GetPoolBucketSize(GetPoolBucketIndex(Args));
	// in VisualStudio the copy constructor call on the return argument can be optimized out
	// see https://msdn.microsoft.com/en-us/library/ms364057.aspx#nrvo_cpp05_topic3
	FVertexBufferAndSRV Buffer;
	FRHIResourceCreateInfo CreateInfo(TEXT("FClothBufferPoolPolicy"));
	Buffer.VertexBufferRHI = RHICmdList.CreateVertexBuffer( BufferSize, (BUF_Dynamic | BUF_ShaderResource), CreateInfo );
	Buffer.VertexBufferSRV = RHICmdList.CreateShaderResourceView( Buffer.VertexBufferRHI, sizeof(FVector2f), PF_G32R32F );
	return Buffer;
}

/*-----------------------------------------------------------------------------
 FBoneBufferPool
 -----------------------------------------------------------------------------*/
FBoneBufferPool::~FBoneBufferPool()
{
}

TStatId FBoneBufferPool::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FBoneBufferPool, STATGROUP_Tickables);
}

FClothBufferPool::~FClothBufferPool()
{
}

TStatId FClothBufferPool::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FClothBufferPool, STATGROUP_Tickables);
}

TConsoleVariableData<int32>* FGPUBaseSkinVertexFactory::FShaderDataType::MaxBonesVar = NULL;
uint32 FGPUBaseSkinVertexFactory::FShaderDataType::MaxGPUSkinBones = 0;

void FGPUBaseSkinVertexFactory::FShaderDataType::UpdateBoneData(FRHICommandList& RHICmdList, const TArray<FMatrix44f>& ReferenceToLocalMatrices,
	const TArray<FBoneIndexType>& BoneMap, uint32 RevisionNumber, ERHIFeatureLevel::Type InFeatureLevel, const FName& AssetPathName)
{
	const uint32 NumBones = BoneMap.Num();
	check(NumBones <= MaxGPUSkinBones);
	FMatrix3x4* ChunkMatrices = nullptr;

	FVertexBufferAndSRV* CurrentBoneBuffer = 0;
	{
		check(IsInParallelRenderingThread());

		// make sure current revision is up-to-date
		SetCurrentRevisionNumber(RevisionNumber);

		const bool bPrevious = false;
		CurrentBoneBuffer = &GetBoneBufferForWriting(bPrevious);

		static FSharedPoolPolicyData PoolPolicy;
		uint32 NumVectors = NumBones*3;
		check(NumVectors <= (MaxGPUSkinBones*3));
		uint32 VectorArraySize = NumVectors * sizeof(FVector4f);
		uint32 PooledArraySize = BoneBufferPool.PooledSizeForCreationArguments(VectorArraySize);

		if(!IsValidRef(*CurrentBoneBuffer) || PooledArraySize != CurrentBoneBuffer->VertexBufferRHI->GetSize())
		{
			if(IsValidRef(*CurrentBoneBuffer))
			{
				BoneBufferPool.ReleasePooledResource(*CurrentBoneBuffer);
			}
			*CurrentBoneBuffer = BoneBufferPool.CreatePooledResource(RHICmdList, VectorArraySize);
			check(IsValidRef(*CurrentBoneBuffer));
			CurrentBoneBuffer->VertexBufferRHI->SetOwnerName(AssetPathName);
		}
		if(NumBones)
		{
			ChunkMatrices = (FMatrix3x4*)RHICmdList.LockBuffer(CurrentBoneBuffer->VertexBufferRHI, 0, VectorArraySize, RLM_WriteOnly);
		}
	}

	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FGPUBaseSkinVertexFactory_ShaderDataType_UpdateBoneData_CopyBones);
		//FMatrix3x4 is sizeof() == 48
		// PLATFORM_CACHE_LINE_SIZE (128) / 48 = 2.6
		//  sizeof(FMatrix) == 64
		// PLATFORM_CACHE_LINE_SIZE (128) / 64 = 2

		if (bGPUSkin_CopyBones_ISPC_Enabled)
		{
#if INTEL_ISPC
			ispc::UpdateBoneData_CopyBones(
				(ispc::FMatrix3x4*)&ChunkMatrices[0],
				(ispc::FMatrix44f*)&ReferenceToLocalMatrices[0],
				BoneMap.GetData(),
				NumBones);
#endif
		}
		else
		{
			constexpr int32 PreFetchStride = 2; // FPlatformMisc::Prefetch stride
			for (uint32 BoneIdx = 0; BoneIdx < NumBones; BoneIdx++)
			{
				const FBoneIndexType RefToLocalIdx = BoneMap[BoneIdx];
				FPlatformMisc::Prefetch(ReferenceToLocalMatrices.GetData() + RefToLocalIdx + PreFetchStride);
				FPlatformMisc::Prefetch(ReferenceToLocalMatrices.GetData() + RefToLocalIdx + PreFetchStride, PLATFORM_CACHE_LINE_SIZE);

				FMatrix3x4& BoneMat = ChunkMatrices[BoneIdx];
				const FMatrix44f& RefToLocal = ReferenceToLocalMatrices[RefToLocalIdx];
				// Explicit SIMD implementation seems to be faster than standard implementation
			#if PLATFORM_ENABLE_VECTORINTRINSICS
				VectorRegister4Float InRow0 = VectorLoadAligned(&(RefToLocal.M[0][0]));
				VectorRegister4Float InRow1 = VectorLoadAligned(&(RefToLocal.M[1][0]));
				VectorRegister4Float InRow2 = VectorLoadAligned(&(RefToLocal.M[2][0]));
				VectorRegister4Float InRow3 = VectorLoadAligned(&(RefToLocal.M[3][0]));

				VectorRegister4Float Temp0 = VectorShuffle(InRow0, InRow1, 0, 1, 0, 1);
				VectorRegister4Float Temp1 = VectorShuffle(InRow2, InRow3, 0, 1, 0, 1);
				VectorRegister4Float Temp2 = VectorShuffle(InRow0, InRow1, 2, 3, 2, 3);
				VectorRegister4Float Temp3 = VectorShuffle(InRow2, InRow3, 2, 3, 2, 3);

				VectorStoreAligned(VectorShuffle(Temp0, Temp1, 0, 2, 0, 2), &(BoneMat.M[0][0]));
				VectorStoreAligned(VectorShuffle(Temp0, Temp1, 1, 3, 1, 3), &(BoneMat.M[1][0]));
				VectorStoreAligned(VectorShuffle(Temp2, Temp3, 0, 2, 0, 2), &(BoneMat.M[2][0]));
			#else
				RefToLocal.To3x4MatrixTranspose((float*)BoneMat.M);
			#endif
			}
		}
	}
	{
		if (NumBones)
		{
			check(CurrentBoneBuffer);
			RHICmdList.UnlockBuffer(CurrentBoneBuffer->VertexBufferRHI);
		}
	}
}

int32 FGPUBaseSkinVertexFactory::GetMinimumPerPlatformMaxGPUSkinBonesValue()
{
	const bool bUseGlobalMaxGPUSkinBones = (GCVarMaxGPUSkinBones != FGPUBaseSkinVertexFactory::GHardwareMaxGPUSkinBones);
	//Use the default value in case there is no valid target platform
	int32 MaxGPUSkinBones = GetDefault<URendererSettings>()->MaxSkinBones.GetValue();
#if WITH_EDITORONLY_DATA && WITH_EDITOR
	for (const TPair<FName, int32>& PlatformData : GetDefault<URendererSettings>()->MaxSkinBones.PerPlatform)
	{
		MaxGPUSkinBones = FMath::Min(MaxGPUSkinBones, PlatformData.Value);
	}
#endif
	if (bUseGlobalMaxGPUSkinBones)
	{
		MaxGPUSkinBones = FMath::Min(MaxGPUSkinBones, GCVarMaxGPUSkinBones);
	}
	return MaxGPUSkinBones;
}

int32 FGPUBaseSkinVertexFactory::GetMaxGPUSkinBones(const ITargetPlatform* TargetPlatform /*= nullptr*/)
{
	const bool bUseGlobalMaxGPUSkinBones = (GCVarMaxGPUSkinBones != FGPUBaseSkinVertexFactory::GHardwareMaxGPUSkinBones);
	if (bUseGlobalMaxGPUSkinBones)
	{
		static bool bIsLogged = false;
		if (!bIsLogged)
		{
			UE_LOG(LogSkeletalMesh, Display, TEXT("The Engine config variable [SystemSettings] Compat.MAX_GPUSKIN_BONES (%d) is deprecated, please remove the variable from any engine .ini file. Instead use the per platform project settings - Engine - Rendering - Skinning - Maximum bones per sections. Until the variable is remove we will clamp the per platform value"),
				   GCVarMaxGPUSkinBones);
			bIsLogged = true;
		}
	}
	//Use the default value in case there is no valid target platform
	int32 MaxGPUSkinBones = GetDefault<URendererSettings>()->MaxSkinBones.GetValue();
	
#if WITH_EDITOR
	const ITargetPlatform* TargetPlatformTmp = TargetPlatform;
	if (!TargetPlatformTmp)
	{
		//Get the running platform if the caller did not supply a platform
		ITargetPlatformManagerModule& TargetPlatformManager = GetTargetPlatformManagerRef();
		TargetPlatformTmp = TargetPlatformManager.GetRunningTargetPlatform();
	}
	if (TargetPlatformTmp)
	{
		//Get the platform value
		MaxGPUSkinBones = GetDefault<URendererSettings>()->MaxSkinBones.GetValueForPlatform(*TargetPlatformTmp->IniPlatformName());
	}
#endif

	if (bUseGlobalMaxGPUSkinBones)
	{
		//Make sure we do not go over the global ini console variable GCVarMaxGPUSkinBones
		MaxGPUSkinBones = FMath::Min(MaxGPUSkinBones, GCVarMaxGPUSkinBones);
		
	}

	//We cannot go under MAX_TOTAL_INFLUENCES
	MaxGPUSkinBones = FMath::Max(MaxGPUSkinBones, MAX_TOTAL_INFLUENCES);

	if (GCVarSupport16BitBoneIndex > 0)
	{
		// 16-bit bone index is supported
		return MaxGPUSkinBones;
	}
	else
	{
		// 16-bit bone index is not supported, clamp the max bones to 8-bit
		return FMath::Min(MaxGPUSkinBones, 256);
	}
}

bool FGPUBaseSkinVertexFactory::UseUnlimitedBoneInfluences(uint32 MaxBoneInfluences, const ITargetPlatform* TargetPlatform)
{
	if (!GetUnlimitedBoneInfluences(TargetPlatform))
	{
		return false;
	}

	uint32 UnlimitedBoneInfluencesThreshold = (uint32)GCVarUnlimitedBoneInfluencesThreshold;

#if ALLOW_OTHER_PLATFORM_CONFIG
	if (TargetPlatform)
	{
		const ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
		const bool bIsRunningPlatform = RunningPlatform == TargetPlatform;
		if (bIsRunningPlatform)
		{
			UnlimitedBoneInfluencesThreshold = CVarUnlimitedBoneInfluencesThreshold->GetInt();
		}
		else
		{
			TSharedPtr<IConsoleVariable> VariablePtr = CVarUnlimitedBoneInfluencesThreshold->GetPlatformValueVariable(*TargetPlatform->IniPlatformName());
			if (VariablePtr.IsValid())
			{
				UnlimitedBoneInfluencesThreshold = (uint32)VariablePtr->GetInt();
			}
		}
	}
#endif
	
	return MaxBoneInfluences > UnlimitedBoneInfluencesThreshold;
}

bool FGPUBaseSkinVertexFactory::GetUnlimitedBoneInfluences(const ITargetPlatform* TargetPlatform)
{
#if ALLOW_OTHER_PLATFORM_CONFIG
	if (TargetPlatform)
	{
		const ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
		const bool bIsRunningPlatform = RunningPlatform == TargetPlatform;
		if (bIsRunningPlatform)
		{
			return CVarUnlimitedBoneInfluences->GetBool();
		}
		else
		{
			TSharedPtr<IConsoleVariable> VariablePtr = CVarUnlimitedBoneInfluences->GetPlatformValueVariable(*TargetPlatform->IniPlatformName());
			if (VariablePtr.IsValid())
			{
				return VariablePtr->GetBool();
			}
		}
	}
#endif
	
	return (GCVarUnlimitedBoneInfluences!=0);
}

int32 FGPUBaseSkinVertexFactory::GetBoneInfluenceLimitForAsset(int32 AssetProvidedLimit, const ITargetPlatform* TargetPlatform /*= nullptr*/)
{
	if (AssetProvidedLimit > 0)
	{
		// The asset provided an explicit limit
		return AssetProvidedLimit;
	}

	int32 GlobalDefaultLimit = GetDefault<URendererSettings>()->DefaultBoneInfluenceLimit.GetValue();

#if WITH_EDITOR
	const ITargetPlatform* TargetPlatformTmp = TargetPlatform;
	if (!TargetPlatformTmp)
	{
		// Get the running platform if the caller did not supply a platform
		ITargetPlatformManagerModule& TargetPlatformManager = GetTargetPlatformManagerRef();
		TargetPlatformTmp = TargetPlatformManager.GetRunningTargetPlatform();
	}

	if (TargetPlatformTmp)
	{
		// Get the platform value
		GlobalDefaultLimit = GetDefault<URendererSettings>()->DefaultBoneInfluenceLimit.GetValueForPlatform(*TargetPlatformTmp->IniPlatformName());
	}
#endif

	if (GlobalDefaultLimit > 0)
	{
		// A global default limit has been set for this platform
		return GlobalDefaultLimit;
	}

	// No limit has been set. Return the maximum possible value.
	return MAX_TOTAL_INFLUENCES;
}

bool FGPUBaseSkinVertexFactory::GetAlwaysUseDeformerForUnlimitedBoneInfluences(EShaderPlatform Platform)
{
	auto InnerFunc = [](EShaderPlatform Platform)
	{
		static FShaderPlatformCachedIniValue<bool> UseDeformerForUBICVar(TEXT("r.GPUSkin.AlwaysUseDeformerForUnlimitedBoneInfluences"));
		const IMeshDeformerProvider* MeshDeformerProvider = IMeshDeformerProvider::Get();

		return MeshDeformerProvider && MeshDeformerProvider->IsSupported(Platform) && UseDeformerForUBICVar.Get(Platform);
	};

#if WITH_EDITOR
	return InnerFunc(Platform);
#else
	// This value can't change at runtime in a non-editor build, so it's safe to cache.
	static const bool bCachedResult = InnerFunc(Platform);
	return bCachedResult;
#endif
}

BEGIN_SHADER_PARAMETER_STRUCT(FGPUSkinVertexFactoryCommonShaderParameters, )
	// Bits 0-7 => Size of the bone weight index in bytes / bits 8-15 => Size of the bone weight weights value in bytes
	SHADER_PARAMETER(uint32, InputWeightIndexSize)
	// number of influences for this draw call, 4 or 8
	SHADER_PARAMETER(uint32, NumBoneInfluencesParam)
	SHADER_PARAMETER(uint32, bIsMorphTarget)
	SHADER_PARAMETER(uint32, BoneUpdatedFrameNumber)
	SHADER_PARAMETER(uint32, MorphUpdatedFrameNumber)
	SHADER_PARAMETER_SRV(Buffer<float4>, BoneMatrices)
	SHADER_PARAMETER_SRV(Buffer<float4>, PreviousBoneMatrices)
	SHADER_PARAMETER_SRV(Buffer<uint>, InputWeightStream)
	SHADER_PARAMETER_SRV(Buffer<float>, PreviousMorphBuffer)
END_SHADER_PARAMETER_STRUCT()

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FGPUSkinVertexFactoryUniformShaderParameters, )
	SHADER_PARAMETER_STRUCT(FGPUSkinVertexFactoryCommonShaderParameters, Common)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FGPUSkinVertexFactoryUniformShaderParameters, "GPUSkinVFBase")

void GetGPUSkinVertexFactoryCommonShaderParameters(FGPUSkinVertexFactoryCommonShaderParameters& ShaderParameters, const FGPUBaseSkinVertexFactory* VertexFactory)
{
	const FGPUBaseSkinVertexFactory::FShaderDataType& ShaderData = VertexFactory->GetShaderData();
	const FMorphVertexBuffer* PreviousMorphVertexBuffer = VertexFactory->GetMorphVertexBuffer(true);

	ShaderParameters.BoneMatrices = ShaderData.GetBoneBufferForReading(false).VertexBufferSRV;
	ShaderParameters.PreviousBoneMatrices = ShaderData.GetBoneBufferForReading(true).VertexBufferSRV;
	ShaderParameters.InputWeightIndexSize = ShaderData.InputWeightIndexSize;
	ShaderParameters.InputWeightStream = ShaderData.InputWeightStream ? ShaderData.InputWeightStream : GNullVertexBuffer.VertexBufferSRV;
	ShaderParameters.NumBoneInfluencesParam = VertexFactory->GetNumBoneInfluences();
	ShaderParameters.bIsMorphTarget = VertexFactory->IsMorphTarget() ? 1 : 0;
	ShaderParameters.PreviousMorphBuffer = PreviousMorphVertexBuffer ? PreviousMorphVertexBuffer->GetSRV() : GNullVertexBuffer.VertexBufferSRV.GetReference();
	ShaderParameters.BoneUpdatedFrameNumber = ShaderData.UpdatedFrameNumber;
	ShaderParameters.MorphUpdatedFrameNumber = VertexFactory->GetMorphVertexBufferUpdatedFrameNumber();
}

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FGPUSkinAPEXClothVertexFactoryUniformShaderParameters, )
	SHADER_PARAMETER_STRUCT(FGPUSkinVertexFactoryCommonShaderParameters, Common)
	/** Transform from cloth space (relative to cloth root bone) to local(component) space */
	SHADER_PARAMETER(FMatrix44f, ClothToLocal)
	SHADER_PARAMETER(FMatrix44f, PreviousClothToLocal)
	/** blend weight between simulated positions and original key-framed animation */
	SHADER_PARAMETER(float, ClothBlendWeight)
	/** Scale of the owner actor */
	SHADER_PARAMETER(FVector3f, WorldScale)
	// .x = Draw Index Buffer offset, .y = Offset into Cloth Vertex Buffer
	SHADER_PARAMETER(FUintVector2, GPUSkinApexClothStartIndexOffset)
	SHADER_PARAMETER(uint32, ClothNumInfluencesPerVertex)
	SHADER_PARAMETER(uint32, bEnabled)
	/** Vertex buffer from which to read simulated positions of clothing. */
	SHADER_PARAMETER_SRV(Buffer<float2>, ClothSimulVertsPositionsNormals)
	SHADER_PARAMETER_SRV(Buffer<float2>, PreviousClothSimulVertsPositionsNormals)
	SHADER_PARAMETER_SRV(Buffer<float4>, GPUSkinApexCloth)
END_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FGPUSkinAPEXClothVertexFactoryUniformShaderParameters, "GPUSkinAPEXClothVF");

void GetGPUSkinAPEXClothVertexFactoryUniformShaderParameters(
	FGPUSkinAPEXClothVertexFactoryUniformShaderParameters& UniformParameters,
	const FGPUBaseSkinVertexFactory* VertexFactory)
{
	FGPUBaseSkinAPEXClothVertexFactory const* ClothVertexFactory = VertexFactory->GetClothVertexFactory();
	check(ClothVertexFactory != nullptr);

	const FGPUBaseSkinAPEXClothVertexFactory::ClothShaderType& ClothShaderData = ClothVertexFactory->GetClothShaderData();
	const uint32 BaseVertexIndex = VertexFactory->GetBaseVertexIndex();

	FRHIShaderResourceView* ClothBufferSRV = ClothVertexFactory->GetClothBuffer();

	GetGPUSkinVertexFactoryCommonShaderParameters(UniformParameters.Common, VertexFactory);
	UniformParameters.ClothSimulVertsPositionsNormals = ClothShaderData.HasClothBufferForReading(false) ? ClothShaderData.GetClothBufferForReading(false).VertexBufferSRV : GNullVertexBuffer.VertexBufferSRV;
	UniformParameters.GPUSkinApexCloth = ClothBufferSRV ? ClothBufferSRV : GNullVertexBuffer.VertexBufferSRV.GetReference();
	UniformParameters.ClothToLocal = ClothShaderData.GetClothToLocalForReading(false);
	UniformParameters.ClothBlendWeight = ClothShaderData.ClothBlendWeight;
	UniformParameters.WorldScale = ClothShaderData.WorldScale;
	UniformParameters.GPUSkinApexClothStartIndexOffset = FUintVector2(BaseVertexIndex, ClothVertexFactory->GetClothIndexOffset(BaseVertexIndex));
	UniformParameters.ClothNumInfluencesPerVertex = ClothShaderData.NumInfluencesPerVertex;
	UniformParameters.bEnabled = ClothShaderData.bEnabled;
	UniformParameters.PreviousClothSimulVertsPositionsNormals = ClothShaderData.HasClothBufferForReading(true) ? ClothShaderData.GetClothBufferForReading(true).VertexBufferSRV : GNullVertexBuffer.VertexBufferSRV;
	UniformParameters.PreviousClothToLocal = ClothShaderData.GetClothToLocalForReading(true);
}

void FGPUBaseSkinVertexFactory::SetData(const FGPUSkinDataType* InData)
{
	SetData(FRHICommandListExecutor::GetImmediateCommandList(), InData);
}

void FGPUBaseSkinVertexFactory::SetData(FRHICommandListBase& RHICmdList, const FGPUSkinDataType* InData)
{
	check(InData);

	if (!Data)
	{
		Data = MakeUnique<FGPUSkinDataType>();
	}

	*Data = *InData;
	UpdateRHI(RHICmdList);
}

void FGPUBaseSkinVertexFactory::InitRHI(FRHICommandListBase& RHICmdList)
{
	// The primary vertex factory is used for cached mesh draw commands which needs a valid uniform buffer, so pre-create the uniform buffer with empty contents.
	if (!bUsedForPassthroughVertexFactory)
	{
		if (GetClothVertexFactory())
		{
			UniformBuffer = RHICreateUniformBuffer(nullptr, &FGPUSkinAPEXClothVertexFactoryUniformShaderParameters::GetStructMetadata()->GetLayout(), EUniformBufferUsage::UniformBuffer_MultiFrame);
		}
		else
		{
			UniformBuffer = RHICreateUniformBuffer(nullptr, &FGPUSkinVertexFactoryUniformShaderParameters::GetStructMetadata()->GetLayout(), EUniformBufferUsage::UniformBuffer_MultiFrame);
		}
	}

	MorphDeltaBufferSlot = FRHIStreamSourceSlot::Create(GNullMorphVertexBuffer.VertexBufferRHI.GetReference());
}

void FGPUBaseSkinVertexFactory::ReleaseRHI()
{
	FVertexFactory::ReleaseRHI();
	UniformBuffer.SafeRelease();
}

void FGPUBaseSkinVertexFactory::UpdateUniformBuffer(FRHICommandListBase& RHICmdList)
{
	if (GetClothVertexFactory())
	{
		FGPUSkinAPEXClothVertexFactoryUniformShaderParameters UniformParameters;
		GetGPUSkinAPEXClothVertexFactoryUniformShaderParameters(UniformParameters, this);
		if (UniformBuffer)
		{
			RHICmdList.UpdateUniformBuffer(UniformBuffer, &UniformParameters);
		}
		else
		{
			// If this vertex factory is used for the passthrough one it's still possible to fall back to using this one, but we defer creation of the RHI uniform buffer.
			check(bUsedForPassthroughVertexFactory);
			UniformBuffer = RHICreateUniformBuffer(&UniformParameters, &FGPUSkinAPEXClothVertexFactoryUniformShaderParameters::GetStructMetadata()->GetLayout(), EUniformBufferUsage::UniformBuffer_MultiFrame);
		}
	}
	else
	{
		FGPUSkinVertexFactoryUniformShaderParameters UniformParameters;
		GetGPUSkinVertexFactoryCommonShaderParameters(UniformParameters.Common, this);
		if (UniformBuffer)
		{
			RHICmdList.UpdateUniformBuffer(UniformBuffer, &UniformParameters);
		}
		else
		{
			// If this vertex factory is used for the passthrough one it's still possible to fall back to using this one, but we defer creation of the RHI uniform buffer.
			check(bUsedForPassthroughVertexFactory);
			UniformBuffer = RHICreateUniformBuffer(&UniformParameters, &FGPUSkinVertexFactoryUniformShaderParameters::GetStructMetadata()->GetLayout(), EUniformBufferUsage::UniformBuffer_MultiFrame);
		}
	}
}

void FGPUBaseSkinVertexFactory::UpdateMorphState(FRHICommandListBase& RHICmdList, bool bUseMorphTarget)
{
	check(Data);
	Data->bMorphTarget = bUseMorphTarget;

	if (bUseMorphTarget)
	{
		const FMorphVertexBuffer* MorphVertexBuffer = GetMorphVertexBuffer(false);
		RHICmdList.UpdateStreamSourceSlot(MorphDeltaBufferSlot, MorphVertexBuffer ? MorphVertexBuffer->VertexBufferRHI : GNullMorphVertexBuffer.VertexBufferRHI);
	}
}

void FGPUBaseSkinVertexFactory::CopyDataTypeForLocalVertexFactory(FLocalVertexFactory::FDataType& OutDestData) const
{
	check(Data.IsValid());

	OutDestData.PositionComponent = Data->PositionComponent;
	OutDestData.TangentBasisComponents[0] = Data->TangentBasisComponents[0];
	OutDestData.TangentBasisComponents[1] = Data->TangentBasisComponents[1];
	OutDestData.TextureCoordinates = Data->TextureCoordinates;
	OutDestData.ColorComponent = Data->ColorComponent;
	OutDestData.PreSkinPositionComponent = Data->PositionComponent;
	OutDestData.PositionComponentSRV = Data->PositionComponentSRV;
	OutDestData.PreSkinPositionComponentSRV = Data->PositionComponentSRV;
	OutDestData.TangentsSRV = Data->TangentsSRV;
	OutDestData.ColorComponentsSRV = Data->ColorComponentsSRV;
	OutDestData.ColorIndexMask = Data->ColorIndexMask;
	OutDestData.TextureCoordinatesSRV = Data->TextureCoordinatesSRV;
	OutDestData.LightMapCoordinateIndex = Data->LightMapCoordinateIndex;
	OutDestData.NumTexCoords = Data->NumTexCoords;
	OutDestData.LODLightmapDataIndex = Data->LODLightmapDataIndex;
}

const FMorphVertexBuffer* FGPUBaseSkinVertexFactory::GetMorphVertexBuffer(bool bPrevious) const
{
	check(Data.IsValid() && Data->MorphVertexBufferPool);
	return Data->bMorphTarget ? &Data->MorphVertexBufferPool->GetMorphVertexBufferForReading(bPrevious) : nullptr;
}

uint32 FGPUBaseSkinVertexFactory::GetMorphVertexBufferUpdatedFrameNumber() const
{
	check(Data.IsValid() && Data->MorphVertexBufferPool);
	return Data->bMorphTarget ? Data->MorphVertexBufferPool->GetUpdatedFrameNumber() : 0;
}

void FGPUBaseSkinVertexFactory::GetOverrideVertexStreams(FVertexInputStreamArray& VertexStreams) const
{
	if (MorphDeltaStreamIndex >= 0)
	{
		VertexStreams.Emplace(MorphDeltaStreamIndex, 0, MorphDeltaBufferSlot);
	}
}

/*-----------------------------------------------------------------------------
TGPUSkinVertexFactory
-----------------------------------------------------------------------------*/

TGlobalResource<FBoneBufferPool> FGPUBaseSkinVertexFactory::BoneBufferPool;

template <GPUSkinBoneInfluenceType BoneInfluenceType>
bool TGPUSkinVertexFactory<BoneInfluenceType>::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	static FShaderPlatformCachedIniValue<int32> UBICVar(TEXT("r.GPUSkin.UnlimitedBoneInfluences"));
	const bool bUseUBI = UBICVar.Get(Parameters.Platform) != 0;

	static FShaderPlatformCachedIniValue<bool> UseDeformerForUBICVar(TEXT("r.GPUSkin.AlwaysUseDeformerForUnlimitedBoneInfluences"));
	const bool bUseDeformerForUBI = UseDeformerForUBICVar.Get(Parameters.Platform);
		
	// Compile the shader for UBI if UBI is enabled and we're not forcing the use of a deformer for all UBI meshes
	const bool bUnlimitedBoneInfluences = BoneInfluenceType == UnlimitedBoneInfluence && bUseUBI && !bUseDeformerForUBI;

	return ShouldWeCompileGPUSkinVFShaders(Parameters.Platform, Parameters.MaterialParameters.FeatureLevel) &&
		  (((Parameters.MaterialParameters.bIsUsedWithSkeletalMesh || Parameters.MaterialParameters.bIsUsedWithMorphTargets) && (BoneInfluenceType != UnlimitedBoneInfluence || bUnlimitedBoneInfluences)) 
			  || Parameters.MaterialParameters.bIsSpecialEngineMaterial);
}

template <GPUSkinBoneInfluenceType BoneInfluenceType>
void TGPUSkinVertexFactory<BoneInfluenceType>::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment )
{
	FVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);

	const FStaticFeatureLevel MaxSupportedFeatureLevel = GetMaxSupportedFeatureLevel(Parameters.Platform);
	// TODO: support GPUScene on mobile
	const bool bUseGPUScene = UseGPUScene(Parameters.Platform, MaxSupportedFeatureLevel) && (MaxSupportedFeatureLevel > ERHIFeatureLevel::ES3_1);
	const bool bSupportsPrimitiveIdStream = Parameters.VertexFactoryType->SupportsPrimitiveIdStream();
	{
		const bool bLimit2BoneInfluences = (CVarGPUSkinLimit2BoneInfluences.GetValueOnAnyThread() != 0);
		OutEnvironment.SetDefine(TEXT("GPUSKIN_LIMIT_2BONE_INFLUENCES"), (bLimit2BoneInfluences ? 1 : 0));
	}

	OutEnvironment.SetDefine(TEXT("GPUSKIN_UNLIMITED_BONE_INFLUENCE"), BoneInfluenceType == UnlimitedBoneInfluence ? 1 : 0);

	OutEnvironment.SetDefine(TEXT("GPU_SKINNED_MESH_FACTORY"), 1);

	OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), bSupportsPrimitiveIdStream && bUseGPUScene);

	// Mobile doesn't support motion blur, don't use previous frame morph delta for mobile.
	const bool bIsMobile = IsMobilePlatform(Parameters.Platform);
	OutEnvironment.SetDefine(TEXT("GPUSKIN_MORPH_USE_PREVIOUS"), !bIsMobile);

	// Whether the material supports morph targets
	OutEnvironment.SetDefine(TEXT("GPUSKIN_MORPH_BLEND"), Parameters.MaterialParameters.bIsUsedWithMorphTargets || Parameters.MaterialParameters.bIsSpecialEngineMaterial);
}

/**
 * TGPUSkinVertexFactory does not support manual vertex fetch yet so worst case element set is returned to make sure the PSO can be compiled
 */
template <GPUSkinBoneInfluenceType BoneInfluenceType>
void TGPUSkinVertexFactory<BoneInfluenceType>::GetPSOPrecacheVertexFetchElements(EVertexInputStreamType VertexInputStreamType, FVertexDeclarationElementList& Elements)
{
	check(VertexInputStreamType == EVertexInputStreamType::Default);

	// Position
	Elements.Add(FVertexElement(0, 0, VET_Float3, 0, 0, false));

	// Normals
	Elements.Add(FVertexElement(1, 0, VET_PackedNormal, 1, 0, false));
	Elements.Add(FVertexElement(2, 0, VET_PackedNormal, 2, 0, false));
	
	// Bone data
	uint32 BaseStreamIndex = 3;
	if (BoneInfluenceType == UnlimitedBoneInfluence)
	{
		// Blend offset count
		Elements.Add(FVertexElement(BaseStreamIndex++, 0, VET_UInt, 3, 0, false));
	}
	else
	{
		// Blend indices
		Elements.Add(FVertexElement(BaseStreamIndex++, 0, VET_UByte4, 3, 0, false));
		Elements.Add(FVertexElement(BaseStreamIndex++, 0, VET_UByte4, 14, 0, false));

		// Blend weights
		Elements.Add(FVertexElement(BaseStreamIndex++, 0, VET_UByte4N, 4, 0, false));
		Elements.Add(FVertexElement(BaseStreamIndex++, 0, VET_UByte4N, 15, 0, false));
	}

	// Texcoords
	Elements.Add(FVertexElement(BaseStreamIndex++, 0, VET_Half4, 5, 0, false));
	Elements.Add(FVertexElement(BaseStreamIndex++, 0, VET_Half4, 6, 0, false));

	// Color
	Elements.Add(FVertexElement(BaseStreamIndex++, 0, VET_Color, 13, 0, false));

	// Attribute ID
	Elements.Add(FVertexElement(BaseStreamIndex++, 0, VET_UInt, 16, 0, true));

	// Morph blend data
	Elements.Add(FVertexElement(Elements.Num(), 0, VET_Float3, 9, 0, false));
	Elements.Add(FVertexElement(Elements.Num(), 0, VET_Float3, 10, 0, false));
}

template <GPUSkinBoneInfluenceType BoneInfluenceType>
void TGPUSkinVertexFactory<BoneInfluenceType>::GetVertexElements(ERHIFeatureLevel::Type FeatureLevel, EVertexInputStreamType InputStreamType, FGPUSkinDataType& GPUSkinData, FVertexDeclarationElementList& OutElements, FVertexStreamList& InOutStreams, int32& OutMorphDeltaStreamIndex)
{
	check(InputStreamType == EVertexInputStreamType::Default);

	// Position
	OutElements.Add(AccessStreamComponent(GPUSkinData.PositionComponent, 0, InOutStreams));

	// Tangent basis vector
	OutElements.Add(AccessStreamComponent(GPUSkinData.TangentBasisComponents[0], 1, InOutStreams));
	OutElements.Add(AccessStreamComponent(GPUSkinData.TangentBasisComponents[1], 2, InOutStreams));

	// Texture coordinates
	if (GPUSkinData.TextureCoordinates.Num())
	{
		const uint8 BaseTexCoordAttribute = 5;
		for (int32 CoordinateIndex = 0; CoordinateIndex < GPUSkinData.TextureCoordinates.Num(); ++CoordinateIndex)
		{
			OutElements.Add(AccessStreamComponent(
				GPUSkinData.TextureCoordinates[CoordinateIndex],
				BaseTexCoordAttribute + CoordinateIndex, InOutStreams
			));
		}

		for (int32 CoordinateIndex = GPUSkinData.TextureCoordinates.Num(); CoordinateIndex < MAX_TEXCOORDS; ++CoordinateIndex)
		{
			OutElements.Add(AccessStreamComponent(
				GPUSkinData.TextureCoordinates[GPUSkinData.TextureCoordinates.Num() - 1],
				BaseTexCoordAttribute + CoordinateIndex, InOutStreams
			));
		}
	}

	if (GPUSkinData.ColorComponentsSRV == nullptr)
	{
		GPUSkinData.ColorComponentsSRV = GNullColorVertexBuffer.VertexBufferSRV;
		GPUSkinData.ColorIndexMask = 0;
	}

	// Vertex color - account for the possibility that the mesh has no vertex colors
	if (GPUSkinData.ColorComponent.VertexBuffer)
	{
		OutElements.Add(AccessStreamComponent(GPUSkinData.ColorComponent, 13, InOutStreams));
	}
	else
	{
		// If the mesh has no color component, set the null color buffer on a new stream with a stride of 0.
		// This wastes 4 bytes of memory per vertex, but prevents having to compile out twice the number of vertex factories.
		FVertexStreamComponent NullColorComponent(&GNullColorVertexBuffer, 0, 0, VET_Color, EVertexStreamUsage::ManualFetch);
		OutElements.Add(AccessStreamComponent(NullColorComponent, 13, InOutStreams));
	}

	if (BoneInfluenceType == UnlimitedBoneInfluence)
	{
		// Blend offset count
		OutElements.Add(AccessStreamComponent(GPUSkinData.BlendOffsetCount, 3, InOutStreams));
	}
	else
	{
		// Bone indices
		OutElements.Add(AccessStreamComponent(GPUSkinData.BoneIndices, 3, InOutStreams));

		// Bone weights
		OutElements.Add(AccessStreamComponent(GPUSkinData.BoneWeights, 4, InOutStreams));

		// Extra bone indices & weights
		if (GPUSkinData.NumBoneInfluences > MAX_INFLUENCES_PER_STREAM)
		{
			OutElements.Add(AccessStreamComponent(GPUSkinData.ExtraBoneIndices, 14, InOutStreams));
			OutElements.Add(AccessStreamComponent(GPUSkinData.ExtraBoneWeights, 15, InOutStreams));
		}
		else
		{
			OutElements.Add(AccessStreamComponent(GPUSkinData.BoneIndices, 14, InOutStreams));
			OutElements.Add(AccessStreamComponent(GPUSkinData.BoneWeights, 15, InOutStreams));
		}
	}

	FVertexElement DeltaPositionElement = AccessStreamComponent(GPUSkinData.DeltaPositionComponent, 9, InOutStreams);
	OutElements.Add(DeltaPositionElement);
	OutElements.Add(AccessStreamComponent(GPUSkinData.DeltaTangentZComponent, 10, InOutStreams));

	// Cache delta stream index (position & tangentZ share the same stream)
	OutMorphDeltaStreamIndex = DeltaPositionElement.StreamIndex;
}

template <GPUSkinBoneInfluenceType BoneInfluenceType>
void TGPUSkinVertexFactory<BoneInfluenceType>::GetVertexElements(ERHIFeatureLevel::Type FeatureLevel, EVertexInputStreamType InputStreamType, FGPUSkinDataType& GPUSkinData, FVertexDeclarationElementList& OutElements)
{
	FVertexStreamList VertexStreams;
	int32 MorphDeltaStreamIndex;
	GetVertexElements(FeatureLevel, InputStreamType, GPUSkinData, OutElements, VertexStreams, MorphDeltaStreamIndex);

	if (UseGPUScene(GMaxRHIShaderPlatform, GMaxRHIFeatureLevel) && 
		FeatureLevel > ERHIFeatureLevel::ES3_1) // Skin VF does not use GPUScene on mobile
	{
		OutElements.Add(FVertexElement(VertexStreams.Num(), 0, VET_UInt, 16, 0, true));
	}
}

/**
* Add the vertex declaration elements for the streams.
* @param InData - Type with stream components.
* @param OutElements - Vertex declaration list to modify.
*/
template <GPUSkinBoneInfluenceType BoneInfluenceType>
void TGPUSkinVertexFactory<BoneInfluenceType>::AddVertexElements(FVertexDeclarationElementList& OutElements)
{
	check(Data.IsValid());
	GetVertexElements(GetFeatureLevel(), EVertexInputStreamType::Default, *Data, OutElements, Streams, MorphDeltaStreamIndex);

	AddPrimitiveIdStreamElement(EVertexInputStreamType::Default, OutElements, 16, 0xff);
}

/**
* Creates declarations for each of the vertex stream components and
* initializes the device resource
*/
template <GPUSkinBoneInfluenceType BoneInfluenceType>
void TGPUSkinVertexFactory<BoneInfluenceType>::InitRHI(FRHICommandListBase& RHICmdList)
{
	FGPUBaseSkinVertexFactory::InitRHI(RHICmdList);

	// list of declaration items
	FVertexDeclarationElementList Elements;
	AddVertexElements(Elements);

	// create the actual device decls
	InitDeclaration(Elements);
}

template <GPUSkinBoneInfluenceType BoneInfluenceType>
void TGPUSkinVertexFactory<BoneInfluenceType>::ReleaseRHI()
{
	FGPUBaseSkinVertexFactory::ReleaseRHI();
	ShaderData.ReleaseBoneData();
}

/*-----------------------------------------------------------------------------
TGPUSkinAPEXClothVertexFactory
-----------------------------------------------------------------------------*/

template <GPUSkinBoneInfluenceType BoneInfluenceType>
void TGPUSkinAPEXClothVertexFactory<BoneInfluenceType>::ReleaseRHI()
{
	Super::ReleaseRHI();
	ClothShaderData.ReleaseClothSimulData();

	// Release the RHIResource reference held in FGPUSkinAPEXClothDataType
	if (ClothDataPtr)
	{
		ClothDataPtr->ClothBuffer.SafeRelease();
	}
}

/*-----------------------------------------------------------------------------
TGPUSkinVertexFactoryShaderParameters
-----------------------------------------------------------------------------*/

class FGPUSkinVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FGPUSkinVertexFactoryShaderParameters, NonVirtual);
public:
	void GetElementShaderBindings(
		const FSceneInterface* Scene,
		const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const
	{
		const FGPUBaseSkinVertexFactory* GPUSkinVertexFactory = static_cast<const FGPUBaseSkinVertexFactory*>(VertexFactory);
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FGPUSkinVertexFactoryUniformShaderParameters>(), GPUSkinVertexFactory->GetUniformBuffer());
		GPUSkinVertexFactory->GetOverrideVertexStreams(VertexStreams);
	}
};

IMPLEMENT_TYPE_LAYOUT(FGPUSkinVertexFactoryShaderParameters);

IMPLEMENT_GPUSKINNING_VERTEX_FACTORY_PARAMETER_TYPE(TGPUSkinVertexFactory, SF_Vertex, FGPUSkinVertexFactoryShaderParameters);

/** bind gpu skin vertex factory to its shader file and its shader parameters */
IMPLEMENT_GPUSKINNING_VERTEX_FACTORY_TYPE(TGPUSkinVertexFactory, "/Engine/Private/GpuSkinVertexFactory.ush",
	  EVertexFactoryFlags::UsedWithMaterials 
	| EVertexFactoryFlags::SupportsDynamicLighting
	| EVertexFactoryFlags::SupportsPSOPrecaching
	| EVertexFactoryFlags::SupportsCachingMeshDrawCommands
);

/*-----------------------------------------------------------------------------
	FGPUBaseSkinAPEXClothVertexFactory
-----------------------------------------------------------------------------*/
bool FGPUBaseSkinAPEXClothVertexFactory::IsClothEnabled(EShaderPlatform Platform)
{
	static FShaderPlatformCachedIniValue<bool> MobileEnableClothIniValue(TEXT("r.Mobile.EnableCloth"));
	const bool bEnableClothOnMobile = (MobileEnableClothIniValue.Get(Platform) != 0);
	const bool bIsMobile = IsMobilePlatform(Platform);
	return !bIsMobile || bEnableClothOnMobile;
}

/*-----------------------------------------------------------------------------
	TGPUSkinAPEXClothVertexFactoryShaderParameters
-----------------------------------------------------------------------------*/

class TGPUSkinAPEXClothVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(TGPUSkinAPEXClothVertexFactoryShaderParameters, NonVirtual);
public:
	void GetElementShaderBindings(
		const FSceneInterface* Scene,
		const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const
	{
		const FGPUBaseSkinVertexFactory* GPUSkinVertexFactory = static_cast<const FGPUBaseSkinVertexFactory*>(VertexFactory);
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FGPUSkinAPEXClothVertexFactoryUniformShaderParameters>(), GPUSkinVertexFactory->GetUniformBuffer());
		GPUSkinVertexFactory->GetOverrideVertexStreams(VertexStreams);
	}
};

IMPLEMENT_TYPE_LAYOUT(TGPUSkinAPEXClothVertexFactoryShaderParameters);

/*-----------------------------------------------------------------------------
	TGPUSkinAPEXClothVertexFactory::ClothShaderType
-----------------------------------------------------------------------------*/

void FGPUBaseSkinAPEXClothVertexFactory::ClothShaderType::UpdateClothSimulationData(
	FRHICommandList& RHICmdList,
	TConstArrayView<FVector3f> InSimulPositions,
	TConstArrayView<FVector3f> InSimulNormals,
	uint32 RevisionNumber,
	const FName& AssetPathName)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FGPUBaseSkinAPEXClothVertexFactory_UpdateClothSimulationData);

	uint32 NumSimulVerts = InSimulPositions.Num();

	check(IsInParallelRenderingThread());
	
	SetCurrentRevisionNumber(RevisionNumber);
	FVertexBufferAndSRV* CurrentClothBuffer = &GetClothBufferForWriting();

	NumSimulVerts = FMath::Min(NumSimulVerts, (uint32)MAX_APEXCLOTH_VERTICES_FOR_VB);

	uint32 VectorArraySize = NumSimulVerts * sizeof(float) * 6;
	uint32 PooledArraySize = ClothSimulDataBufferPool.PooledSizeForCreationArguments(VectorArraySize);
	if(!IsValidRef(*CurrentClothBuffer) || PooledArraySize != CurrentClothBuffer->VertexBufferRHI->GetSize())
	{
		if(IsValidRef(*CurrentClothBuffer))
		{
			ClothSimulDataBufferPool.ReleasePooledResource(*CurrentClothBuffer);
		}
		*CurrentClothBuffer = ClothSimulDataBufferPool.CreatePooledResource(RHICmdList, VectorArraySize);
		check(IsValidRef(*CurrentClothBuffer));
		CurrentClothBuffer->VertexBufferRHI->SetOwnerName(AssetPathName);
	}

	if(NumSimulVerts)
	{
		float* RESTRICT Data = (float* RESTRICT)RHICmdList.LockBuffer(CurrentClothBuffer->VertexBufferRHI, 0, VectorArraySize, RLM_WriteOnly);
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_FGPUBaseSkinAPEXClothVertexFactory_UpdateClothSimulationData_CopyData);
			float* RESTRICT Pos = (float* RESTRICT) &InSimulPositions[0].X;
			float* RESTRICT Normal = (float* RESTRICT) &InSimulNormals[0].X;
			for (uint32 Index = 0; Index < NumSimulVerts; Index++)
			{
				FPlatformMisc::Prefetch(Pos + PLATFORM_CACHE_LINE_SIZE);
				FPlatformMisc::Prefetch(Normal + PLATFORM_CACHE_LINE_SIZE);

				FMemory::Memcpy(Data, Pos, sizeof(float) * 3);
				FMemory::Memcpy(Data + 3, Normal, sizeof(float) * 3);
				Data += 6;
				Pos += 3;
				Normal += 3;
			}
		}
		RHICmdList.UnlockBuffer(CurrentClothBuffer->VertexBufferRHI);
	}
}

void FGPUBaseSkinAPEXClothVertexFactory::ClothShaderType::SetCurrentRevisionNumber(uint32 RevisionNumber)
{
	if (bDoubleBuffer)
	{
		// Flip revision number to previous if this is new, otherwise keep current version.
		if (CurrentRevisionNumber != RevisionNumber)
		{
			PreviousRevisionNumber = CurrentRevisionNumber;
			CurrentRevisionNumber = RevisionNumber;
			CurrentBuffer = 1 - CurrentBuffer;
		}
	}
}

FVertexBufferAndSRV& FGPUBaseSkinAPEXClothVertexFactory::ClothShaderType::GetClothBufferForWriting()
{
	uint32 Index = GetClothBufferIndexForWriting();
	return ClothSimulPositionNormalBuffer[Index];
}

bool FGPUBaseSkinAPEXClothVertexFactory::ClothShaderType::HasClothBufferForReading(bool bPrevious) const
{
	uint32 Index = GetClothBufferIndexForReading(bPrevious);
	return bEnabled && ClothSimulPositionNormalBuffer[Index].VertexBufferRHI.IsValid();
}

const FVertexBufferAndSRV& FGPUBaseSkinAPEXClothVertexFactory::ClothShaderType::GetClothBufferForReading(bool bPrevious) const
{
	uint32 Index = GetClothBufferIndexForReading(bPrevious);
	checkf(ClothSimulPositionNormalBuffer[Index].VertexBufferRHI.IsValid(), TEXT("Index: %i Buffer0: %s Buffer1: %s"), Index, ClothSimulPositionNormalBuffer[0].VertexBufferRHI.IsValid() ? TEXT("true") : TEXT("false"), ClothSimulPositionNormalBuffer[1].VertexBufferRHI.IsValid() ? TEXT("true") : TEXT("false"));
	return ClothSimulPositionNormalBuffer[Index];
}

FMatrix44f& FGPUBaseSkinAPEXClothVertexFactory::ClothShaderType::GetClothToLocalForWriting()
{
	uint32 Index = GetClothBufferIndexForWriting();
	return ClothToLocal[Index];
}

const FMatrix44f& FGPUBaseSkinAPEXClothVertexFactory::ClothShaderType::GetClothToLocalForReading(bool bPrevious) const
{
	uint32 Index = GetClothBufferIndexForReading(bPrevious);
	return ClothToLocal[Index];
}

uint32 FGPUBaseSkinAPEXClothVertexFactory::ClothShaderType::GetClothBufferIndexInternal(bool bPrevious) const
{
	uint32 BufferIndex = 0;
	if (bDoubleBuffer)
	{
		if ((CurrentRevisionNumber - PreviousRevisionNumber) > 1)
		{
			// If the revision number has incremented too much, ignore the request and use the current buffer.
			// With ClearMotionVector calls, we intentionally increment revision number to retrieve current buffer for bPrevious true.
			bPrevious = false;
		}

		BufferIndex = CurrentBuffer ^ (uint32)bPrevious;
	}
	return BufferIndex;
}

uint32 FGPUBaseSkinAPEXClothVertexFactory::ClothShaderType::GetClothBufferIndexForWriting() const
{
	return bDoubleBuffer ? GetClothBufferIndexInternal(false) : 0;
}

uint32 FGPUBaseSkinAPEXClothVertexFactory::ClothShaderType::GetClothBufferIndexForReading(bool bPrevious) const
{
	uint32 BufferIndex = 0;
	if (bDoubleBuffer)
	{
		BufferIndex = GetClothBufferIndexInternal(bPrevious);
		if (!ClothSimulPositionNormalBuffer[BufferIndex].VertexBufferRHI.IsValid())
		{
			// This only could happen first time updating when the previous data is not available
			check(bPrevious);
			// If no previous data available, use the current one
			BufferIndex = GetClothBufferIndexInternal(false);
		}
	}
	return BufferIndex;
}

/*-----------------------------------------------------------------------------
	TGPUSkinAPEXClothVertexFactory
-----------------------------------------------------------------------------*/
TGlobalResource<FClothBufferPool> FGPUBaseSkinAPEXClothVertexFactory::ClothSimulDataBufferPool;

/**
* Modify compile environment to enable the apex clothing path
* @param OutEnvironment - shader compile environment to modify
*/
template <GPUSkinBoneInfluenceType BoneInfluenceType>
void TGPUSkinAPEXClothVertexFactory<BoneInfluenceType>::ModifyCompilationEnvironment( const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment )
{
	Super::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("GPUSKIN_APEX_CLOTH"),TEXT("1"));
	
	// Mobile doesn't support motion blur, don't use previous frame data.
	const bool bIsMobile = IsMobilePlatform(Parameters.Platform);
	OutEnvironment.SetDefine(TEXT("GPUSKIN_APEX_CLOTH_PREVIOUS"), !bIsMobile);
}

template <GPUSkinBoneInfluenceType BoneInfluenceType>
bool TGPUSkinAPEXClothVertexFactory<BoneInfluenceType>::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return IsClothEnabled(Parameters.Platform)
		&& (Parameters.MaterialParameters.bIsUsedWithAPEXCloth || Parameters.MaterialParameters.bIsSpecialEngineMaterial)
		&& Super::ShouldCompilePermutation(Parameters);
}

template <GPUSkinBoneInfluenceType BoneInfluenceType>
void TGPUSkinAPEXClothVertexFactory<BoneInfluenceType>::SetData(FRHICommandListBase& RHICmdList, const FGPUSkinDataType* InData)
{
	const FGPUSkinAPEXClothDataType* InClothData = (const FGPUSkinAPEXClothDataType*)(InData);
	check(InClothData);

	if (!this->Data)
	{
		ClothDataPtr = new FGPUSkinAPEXClothDataType();
		this->Data = TUniquePtr<FGPUSkinDataType>(ClothDataPtr);
	}

	*ClothDataPtr = *InClothData;
	FGPUBaseSkinVertexFactory::UpdateRHI(RHICmdList);
}

/**
* Creates declarations for each of the vertex stream components and
* initializes the device resource
*/
template <GPUSkinBoneInfluenceType BoneInfluenceType>
void TGPUSkinAPEXClothVertexFactory<BoneInfluenceType>::InitRHI(FRHICommandListBase& RHICmdList)
{
	Super::InitRHI(RHICmdList);

	// list of declaration items
	FVertexDeclarationElementList Elements;
	Super::AddVertexElements(Elements);

	// create the actual device decls
	FVertexFactory::InitDeclaration(Elements);
}

IMPLEMENT_GPUSKINNING_VERTEX_FACTORY_PARAMETER_TYPE(TGPUSkinAPEXClothVertexFactory, SF_Vertex, TGPUSkinAPEXClothVertexFactoryShaderParameters);

/** bind cloth gpu skin vertex factory to its shader file and its shader parameters */
IMPLEMENT_GPUSKINNING_VERTEX_FACTORY_TYPE(TGPUSkinAPEXClothVertexFactory, "/Engine/Private/GpuSkinVertexFactory.ush",
	  EVertexFactoryFlags::UsedWithMaterials
	| EVertexFactoryFlags::SupportsDynamicLighting
	| EVertexFactoryFlags::SupportsPSOPrecaching
	| EVertexFactoryFlags::SupportsCachingMeshDrawCommands
);


/*-----------------------------------------------------------------------------
FGPUSkinPassthroughVertexFactory
-----------------------------------------------------------------------------*/
FGPUSkinPassthroughVertexFactory::FGPUSkinPassthroughVertexFactory(ERHIFeatureLevel::Type InFeatureLevel, EVertexAttributeFlags InVertexAttributeMask)
	: FLocalVertexFactory(InFeatureLevel, "FGPUSkinPassthroughVertexFactory")
	, VertexAttributesRequested(InVertexAttributeMask)
{
	bGPUSkinPassThrough = true;
}

void FGPUSkinPassthroughVertexFactory::ResetVertexAttributes(FRHICommandListBase& RHICmdList)
{
	for (int32 Index = 0; Index < EVertexAttribute::NumAttributes; ++Index)
	{
		if (FRHIStreamSourceSlot* Slot = StreamSourceSlots[Index])
		{
			RHICmdList.UpdateStreamSourceSlot(Slot, SourceStreamBuffers[Index]);
		}
	}

	for (int32 Index = 0; Index < EShaderResource::NumShaderResources; ++Index)
	{
		SRVs[Index] = nullptr;
	}
	UpdatedFrameNumber = ~0U;
}

void FGPUSkinPassthroughVertexFactory::InitRHI(FRHICommandListBase& RHICmdList)
{
	const bool bSupportsManualVertexFetch = SupportsManualVertexFetch(GetFeatureLevel());

	// Don't bother binding streams that are using manual vertex fetch.
	const auto IsManualVertexFetch = [bSupportsManualVertexFetch] (const FVertexStreamComponent& Component)
	{
		return bSupportsManualVertexFetch && EnumHasAnyFlags(Component.VertexStreamUsage, EVertexStreamUsage::ManualFetch);
	};

	const auto GetVertexBufferRHI = [] (const FVertexBuffer* VertexBuffer) -> FRHIBuffer*
	{
		return VertexBuffer ? VertexBuffer->GetRHI() : GNullVertexBuffer.GetRHI();
	};

	if (EnumHasAnyFlags(VertexAttributesRequested, EVertexAttributeFlags::Position))
	{
		FRHIBuffer* Buffer = GetVertexBufferRHI(Data.PositionComponent.VertexBuffer);
		SourceStreamBuffers[EVertexAttribute::VertexPosition] = Buffer;
		StreamSourceSlots[EVertexAttribute::VertexPosition] = FRHIStreamSourceSlot::Create(Buffer);
		Data.PositionComponent.Offset = 0;
		Data.PositionComponent.VertexStreamUsage |= EVertexStreamUsage::Overridden;
		Data.PositionComponent.Stride = 3 * sizeof(float);
		VertexAttributesToBind |= EVertexAttributeFlags::Position;
	}

	if (EnumHasAnyFlags(VertexAttributesRequested, EVertexAttributeFlags::Color))
	{
		if (!IsManualVertexFetch(Data.ColorComponent))
		{
			FRHIBuffer* Buffer = GetVertexBufferRHI(Data.ColorComponent.VertexBuffer);
			SourceStreamBuffers[EVertexAttribute::VertexColor] = Buffer;
			StreamSourceSlots[EVertexAttribute::VertexColor] = FRHIStreamSourceSlot::Create(Buffer);
			Data.ColorComponent.Offset = 0;
			Data.ColorComponent.Type = VET_Color;
			Data.ColorComponent.VertexStreamUsage |= EVertexStreamUsage::Overridden;
			Data.ColorComponent.Stride = sizeof(uint32);

			VertexAttributesToBind |= EVertexAttributeFlags::Color;
		}
		
		// Set mask to allow full vertex indexing in vertex shader.
		Data.ColorIndexMask = ~0u;
	}

	if (EnumHasAnyFlags(VertexAttributesRequested, EVertexAttributeFlags::Tangent) && !IsManualVertexFetch(Data.TangentBasisComponents[0]))
	{
		FRHIBuffer* Buffer = GetVertexBufferRHI(Data.TangentBasisComponents[0].VertexBuffer);
		SourceStreamBuffers[EVertexAttribute::VertexTangent] = Buffer;
		StreamSourceSlots[EVertexAttribute::VertexTangent] = FRHIStreamSourceSlot::Create(Buffer);
		Data.TangentBasisComponents[0].VertexStreamUsage |= EVertexStreamUsage::Overridden;
		Data.TangentBasisComponents[0].Offset = 0;
		Data.TangentBasisComponents[0].Type = VET_Short4N;
		Data.TangentBasisComponents[0].Stride = 16;
		Data.TangentBasisComponents[1].VertexStreamUsage |= EVertexStreamUsage::Overridden;
		Data.TangentBasisComponents[1].Offset = 8;
		Data.TangentBasisComponents[1].Type = VET_Short4N;
		Data.TangentBasisComponents[1].Stride = 16;
		VertexAttributesToBind |= EVertexAttributeFlags::Tangent;
	}

	FLocalVertexFactory::InitRHI(RHICmdList);
}

void FGPUSkinPassthroughVertexFactory::UpdateUniformBuffer(FRHICommandListBase& RHICmdList, const FGPUBaseSkinVertexFactory* InSourceVertexFactory)
{
	if (RHISupportsManualVertexFetch(GetFeatureLevelShaderPlatform(GetFeatureLevel())))
	{
		Data.TangentsSRV = SRVs[EShaderResource::Tangent] ? SRVs[EShaderResource::Tangent] : (FRHIShaderResourceView*)InSourceVertexFactory->GetTangentsSRV();
		Data.ColorComponentsSRV = SRVs[EShaderResource::Color] ? SRVs[EShaderResource::Color] : (FRHIShaderResourceView*)InSourceVertexFactory->GetColorComponentsSRV();
		Data.ColorIndexMask = SRVs[EShaderResource::Color] ? Data.ColorIndexMask : InSourceVertexFactory->GetColorIndexMask();
		Data.TextureCoordinatesSRV = SRVs[EShaderResource::TexCoord] ? SRVs[EShaderResource::TexCoord] : (FRHIShaderResourceView*)InSourceVertexFactory->GetTextureCoordinatesSRV();

		const int32 DefaultBaseVertexIndex = 0;
		const int32 DefaultPreSkinBaseVertexIndex = 0;
		FLocalVertexFactoryUniformShaderParameters Parameters;
		GetLocalVFUniformShaderParameters(Parameters, this, Data.LODLightmapDataIndex, nullptr, DefaultBaseVertexIndex, DefaultPreSkinBaseVertexIndex);
		UniformBuffer.UpdateUniformBufferImmediate(RHICmdList, Parameters);
	}
}

void FGPUSkinPassthroughVertexFactory::UpdateLooseUniformBuffer(FRHICommandListBase& RHICmdList, FGPUBaseSkinVertexFactory const* InSourceVertexFactory, uint32 InFrameNumber)
{
	FRHIShaderResourceView* PositionSRV = SRVs[EShaderResource::Position] != nullptr ? SRVs[EShaderResource::Position] : (FRHIShaderResourceView*)InSourceVertexFactory->GetPositionsSRV();
	FRHIShaderResourceView* PrevPositionSRV = SRVs[EShaderResource::PreviousPosition] != nullptr ? SRVs[EShaderResource::PreviousPosition] : PositionSRV;

	FLocalVertexFactoryLooseParameters Parameters;
	Parameters.FrameNumber = InFrameNumber;
	Parameters.GPUSkinPassThroughPositionBuffer = PositionSRV;
	Parameters.GPUSkinPassThroughPreviousPositionBuffer = PrevPositionSRV;
	Parameters.GPUSkinPassThroughPreSkinnedTangentBuffer = InSourceVertexFactory->GetTangentsSRV();
	LooseParametersUniformBuffer.UpdateUniformBufferImmediate(RHICmdList, Parameters);
}

void FGPUSkinPassthroughVertexFactory::SetVertexAttributes(FRHICommandListBase& RHICmdList, FGPUBaseSkinVertexFactory const* InSourceVertexFactory, FAddVertexAttributeDesc const& InDesc)
{
	// Check for modified SRVs.
	bool bNeedUniformBufferUpdate = false;
	bool bNeedLooseUniformBufferUpdate = false;
	for (int32 Index = 0; Index < EShaderResource::NumShaderResources; ++Index)
	{
		if (SRVs[Index] != InDesc.SRVs[Index])
		{
			SRVs[Index] = InDesc.SRVs[Index];

			if (Index == EShaderResource::Position || Index == EShaderResource::PreviousPosition)
			{
				// Position SRVs are stored in the special "loose" uniform buffer used only by the passthrough vertex factory.
				bNeedLooseUniformBufferUpdate = true;
			}
			else
			{
				// All other SRVs are stored in the main vertex factory uniform buffer.
				bNeedUniformBufferUpdate = true;
			}
		}
	}

	for (int32 Index = 0; Index < EVertexAttribute::NumAttributes; ++Index)
	{
		if (FRHIStreamSourceSlot* Slot = StreamSourceSlots[Index])
		{
			RHICmdList.UpdateStreamSourceSlot(Slot, InDesc.StreamBuffers[Index] ? InDesc.StreamBuffers[Index] : SourceStreamBuffers[Index]);
		}
	}

	if (UpdatedFrameNumber != InDesc.FrameNumber)
	{
		// Loose uniform buffer include the latest frame number.
		UpdatedFrameNumber = InDesc.FrameNumber;
		bNeedLooseUniformBufferUpdate = true;
	}

	if (bNeedUniformBufferUpdate)
	{
		// Only need to recreate the vertex factory uniform buffer.
		UpdateUniformBuffer(RHICmdList, InSourceVertexFactory);
	}

	if (bNeedLooseUniformBufferUpdate)
	{
		// Update the loose uniform buffer.
		UpdateLooseUniformBuffer(RHICmdList, InSourceVertexFactory, InDesc.FrameNumber);
	}
}

void FGPUSkinPassthroughVertexFactory::GetOverrideVertexStreams(FVertexInputStreamArray& VertexStreams) const
{
	for (int32 Index = 0; Index < EVertexAttribute::NumAttributes; ++Index)
	{
		if (EnumHasAnyFlags(VertexAttributesToBind, static_cast<EVertexAttributeFlags>(1 << Index)))
		{
			VertexStreams.Emplace(Index, 0, StreamSourceSlots[Index]);
		}
	}
}

#undef IMPLEMENT_GPUSKINNING_VERTEX_FACTORY_PARAMETER_TYPE
#undef IMPLEMENT_GPUSKINNING_VERTEX_FACTORY_TYPE