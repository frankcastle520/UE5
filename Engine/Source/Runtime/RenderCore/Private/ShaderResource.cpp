// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderResource.cpp: ShaderResource implementation.
=============================================================================*/

#include "Shader.h"
#include "Compression/OodleDataCompression.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "Misc/Compression.h"
#include "Misc/CoreMisc.h"
#include "Misc/StringBuilder.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/IShaderFormat.h"
#include "Misc/MemStack.h"
#include "Misc/ScopeLock.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "RHIResources.h"	// Access to FRHIRayTracingShader::RayTracingPayloadType requires this
#include "ShaderCompilerCore.h"
#include "ShaderCore.h"
#include "UObject/RenderingObjectVersion.h"

#if WITH_EDITORONLY_DATA
#include "Interfaces/IShaderFormat.h"
#endif

DECLARE_LOG_CATEGORY_CLASS(LogShaderWarnings, Log, Log);

#if (CSV_PROFILER_STATS && !UE_BUILD_SHIPPING) 
TCsvPersistentCustomStat<int>* CsvStatNumShaderMapsUsedForRendering = nullptr;
#endif

static int32 GShaderCompilerEmitWarningsOnLoad = 0;
static FAutoConsoleVariableRef CVarShaderCompilerEmitWarningsOnLoad(
	TEXT("r.ShaderCompiler.EmitWarningsOnLoad"),
	GShaderCompilerEmitWarningsOnLoad,
	TEXT("When 1, shader compiler warnings are emitted to the log for all shaders as they are loaded."),
	ECVF_Default
);

FName GetShaderCompressionFormat()
{
	// We always use oodle now. This was instituted because UnrealPak recompresses the shaders and doens't have
	// access to the INIs that drive the CVars and would always use default, resulting in mismatches for non
	// default encoder selection.
	return NAME_Oodle;
}

void GetShaderCompressionOodleSettings(FOodleDataCompression::ECompressor& OutCompressor, FOodleDataCompression::ECompressionLevel& OutLevel, const FName& ShaderFormat)
{
	// support an older developer-only CVar for compatibility and make it preempt
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// Since we always use Oodle, we make SkipCompression tell Oodle to not compress.
	static const IConsoleVariable* CVarSkipCompression = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shaders.SkipCompression"));
	static bool bSkipCompression = (CVarSkipCompression && CVarSkipCompression->GetInt() != 0);
	if (UNLIKELY(bSkipCompression))
	{
		OutCompressor = FOodleDataCompression::ECompressor::Selkie;
		OutLevel = FOodleDataCompression::ECompressionLevel::None;
		return;
	}
#endif

	// We just use mermaid/normal here since these settings get overwritten in unrealpak, so this is just for non pak'd builds.
	OutCompressor = FOodleDataCompression::ECompressor::Mermaid;
	OutLevel = FOodleDataCompression::ECompressionLevel::Normal;
}

bool FShaderMapResource::ArePlatformsCompatible(EShaderPlatform CurrentPlatform, EShaderPlatform TargetPlatform)
{
	bool bFeatureLevelCompatible = CurrentPlatform == TargetPlatform;

	if (!bFeatureLevelCompatible && IsPCPlatform(CurrentPlatform) && IsPCPlatform(TargetPlatform))
	{
		bFeatureLevelCompatible = GetMaxSupportedFeatureLevel(CurrentPlatform) >= GetMaxSupportedFeatureLevel(TargetPlatform);

		bool const bIsTargetD3D = IsD3DPlatform(TargetPlatform);

		bool const bIsCurrentPlatformD3D = IsD3DPlatform(CurrentPlatform);

		// For Metal in Editor we can switch feature-levels, but not in cooked projects when using Metal shader librariss.
		bool const bIsCurrentMetal = IsMetalPlatform(CurrentPlatform);
		bool const bIsTargetMetal = IsMetalPlatform(TargetPlatform);
		bool const bIsMetalCompatible = (bIsCurrentMetal == bIsTargetMetal)
#if !WITH_EDITOR	// Static analysis doesn't like (|| WITH_EDITOR)
			&& (!IsMetalPlatform(CurrentPlatform) || (CurrentPlatform == TargetPlatform))
#endif
			;

		bool const bIsCurrentOpenGL = IsOpenGLPlatform(CurrentPlatform);
		bool const bIsTargetOpenGL = IsOpenGLPlatform(TargetPlatform);

		bFeatureLevelCompatible = bFeatureLevelCompatible && (bIsCurrentPlatformD3D == bIsTargetD3D && bIsMetalCompatible && bIsCurrentOpenGL == bIsTargetOpenGL);
	}

	return bFeatureLevelCompatible;
}

#if RHI_RAYTRACING
class FRayTracingShaderLibrary
{
public:
	uint32 AddShader(FRHIRayTracingShader* Shader)
	{
		const int32 PayloadIndex = FMath::CountTrailingZeros(Shader->RayTracingPayloadType);
		FScopeLock Lock(&CS);

		if (UnusedIndicies[PayloadIndex].Num() != 0)
		{
			uint32 Index = UnusedIndicies[PayloadIndex].Pop(EAllowShrinking::No);
			checkSlow(Shaders[PayloadIndex][Index] == nullptr);
			Shaders[PayloadIndex][Index] = Shader;
			return Index;
		}
		else
		{
			return Shaders[PayloadIndex].Add(Shader);
		}
	}

	void RemoveShader(uint32 Index, FRHIRayTracingShader* Shader)
	{
		if (Index != ~0u)
		{
			const int32 PayloadIndex = FMath::CountTrailingZeros(Shader->RayTracingPayloadType);
			FScopeLock Lock(&CS);
			checkSlow(Shaders[PayloadIndex][Index] == Shader);
			UnusedIndicies[PayloadIndex].Push(Index);
			Shaders[PayloadIndex][Index] = nullptr;
		}
	}

	void GetShaders(TArray<FRHIRayTracingShader*>& OutShaders, FRHIRayTracingShader* DefaultShader)
	{
		const int32 PayloadIndex = FMath::CountTrailingZeros(DefaultShader->RayTracingPayloadType);
		const int32 BaseOutIndex = OutShaders.Num();

		FScopeLock Lock(&CS);

		OutShaders.Append(Shaders[PayloadIndex]);

		for (uint32 Index : UnusedIndicies[PayloadIndex])
		{
			OutShaders[BaseOutIndex + Index] = DefaultShader;
		}
	}

private:
	TArray<uint32> UnusedIndicies[32];
	TArray<FRHIRayTracingShader*> Shaders[32];
	FCriticalSection CS;
};

static FRayTracingShaderLibrary GlobalRayTracingHitGroupLibrary;
static FRayTracingShaderLibrary GlobalRayTracingCallableShaderLibrary;
static FRayTracingShaderLibrary GlobalRayTracingMissShaderLibrary;

void FShaderMapResource::GetRayTracingHitGroupLibrary(TArray<FRHIRayTracingShader*>& RayTracingShaders, FRHIRayTracingShader* DefaultShader)
{
	GlobalRayTracingHitGroupLibrary.GetShaders(RayTracingShaders, DefaultShader);
}

void FShaderMapResource::GetRayTracingCallableShaderLibrary(TArray<FRHIRayTracingShader*>& RayTracingCallableShaders, FRHIRayTracingShader* DefaultShader)
{
	GlobalRayTracingCallableShaderLibrary.GetShaders(RayTracingCallableShaders, DefaultShader);
}

void FShaderMapResource::GetRayTracingMissShaderLibrary(TArray<FRHIRayTracingShader*>& RayTracingMissShaders, FRHIRayTracingShader* DefaultShader)
{
	GlobalRayTracingMissShaderLibrary.GetShaders(RayTracingMissShaders, DefaultShader);
}
#endif // RHI_RAYTRACING

static void ApplyResourceStats(FShaderMapResourceCode& Resource)
{
#if STATS
	INC_DWORD_STAT_BY(STAT_Shaders_ShaderResourceMemory, Resource.GetSizeBytes());
	for (const FShaderCodeResource& Shader : Resource.ShaderCodeResources)
	{
		INC_DWORD_STAT_BY_FName(GetMemoryStatType(Shader.GetFrequency()).GetName(), Shader.GetCodeBuffer().GetSize());
	}
#endif // STATS
}

static void RemoveResourceStats(FShaderMapResourceCode& Resource)
{
#if STATS
	DEC_DWORD_STAT_BY(STAT_Shaders_ShaderResourceMemory, Resource.GetSizeBytes());
	for (const FShaderCodeResource& Shader : Resource.ShaderCodeResources)
	{
		DEC_DWORD_STAT_BY_FName(GetMemoryStatType(Shader.GetFrequency()).GetName(), Shader.GetCodeBuffer().GetSize());
	}
#endif // STATS
}

FShaderMapResourceCode::FShaderMapResourceCode(const FShaderMapResourceCode& Other)
{
	ResourceHash = Other.ResourceHash;
	ShaderHashes = Other.ShaderHashes;
	ShaderCodeResources = Other.ShaderCodeResources;

#if WITH_EDITORONLY_DATA
	ShaderEditorOnlyDataEntries = Other.ShaderEditorOnlyDataEntries;
#endif // WITH_EDITORONLY_DATA
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FShaderMapResourceCode::~FShaderMapResourceCode()
{
	RemoveResourceStats(*this);
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void FShaderMapResourceCode::Finalize()
{
	FSHA1 Hasher;
	Hasher.Update((uint8*)ShaderHashes.GetData(), ShaderHashes.Num() * sizeof(FSHAHash));
	Hasher.Final();
	Hasher.GetHash(ResourceHash.Hash);
	ApplyResourceStats(*this);

#if WITH_EDITORONLY_DATA
	LogShaderCompilerWarnings();
#endif
}

uint32 FShaderMapResourceCode::GetSizeBytes() const
{
	uint64 Size = sizeof(*this) + ShaderHashes.GetAllocatedSize() + ShaderCodeResources.GetAllocatedSize();
	for (const FShaderCodeResource& Entry : ShaderCodeResources)
	{
		Size += Entry.GetCacheBuffer().GetSize();
	}
	check(Size <= TNumericLimits<uint32>::Max());
	return static_cast<uint32>(Size);
}

int32 FShaderMapResourceCode::FindShaderIndex(const FSHAHash& InHash) const
{
	return Algo::BinarySearch(ShaderHashes, InHash);
}

void FShaderMapResourceCode::AddShaderCompilerOutput(const FShaderCompilerOutput& Output, const FString& DebugName, FString DebugInfo)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FShaderMapResourceCode::AddShaderCode);

	const FSHAHash& InHash = Output.OutputHash;
	const int32 Index = Algo::LowerBound(ShaderHashes, InHash);
	if (Index >= ShaderHashes.Num() || ShaderHashes[Index] != InHash)
	{
		ShaderHashes.Insert(InHash, Index);

#if WITH_EDITORONLY_DATA
		// Output.Errors contains warnings in the case any exist (no errors since if there were the job would have failed)
		AddEditorOnlyData(Index, DebugName, Output.PlatformDebugData, Output.Errors, Output.ShaderStatistics, DebugInfo);
#endif

		ShaderCodeResources.Insert(Output.GetFinalizedCodeResource(), Index);
	}
#if WITH_EDITORONLY_DATA
	else
	{
		// Output.Errors contains warnings in the case any exist (no errors since if there were the job would have failed)
		// We append the warnings and deduplicate other data like DebugInfo for any additional jobs which resulted in the
		// same bytecode for the sake of determinism in the results saved to DDC. 
		UpdateEditorOnlyData(Index, DebugName, Output.Errors, DebugInfo);
		ValidateShaderStatisticsEditorOnlyData(Index, Output.ShaderStatistics);
	}
#endif
}

#if WITH_EDITORONLY_DATA
void FShaderMapResourceCode::AddEditorOnlyData(int32 Index, const FString& DebugName, TConstArrayView<uint8> InPlatformDebugData, TConstArrayView<FShaderCompilerError> InCompilerWarnings, const TArray<FGenericShaderStat>& ShaderStatistics, const FString& DebugInfo)
{
	FShaderEditorOnlyDataEntry& Entry = ShaderEditorOnlyDataEntries.InsertDefaulted_GetRef(Index);
	Entry.PlatformDebugData = InPlatformDebugData;

	// This should be a newly created shader entry.
	check(Entry.ShaderStatistics.Num() == 0);
	Entry.ShaderStatistics = ShaderStatistics;

	UpdateEditorOnlyData(Index, DebugName, InCompilerWarnings, DebugInfo);
}

void FShaderMapResourceCode::UpdateEditorOnlyData(int32 Index, const FString& DebugName, TConstArrayView<FShaderCompilerError> InCompilerWarnings, const FString& DebugInfo)
{
	FShaderEditorOnlyDataEntry& Entry = ShaderEditorOnlyDataEntries[Index];

	// Keep a single DebugInfo as it doesn't matter which one we use, but make sure it is the same one for determinism
	if (!DebugInfo.IsEmpty() && (Entry.DebugInfo.IsEmpty() || (DebugInfo < Entry.DebugInfo)))
	{
		Entry.DebugInfo = DebugInfo;
	}

	for (const FShaderCompilerError& Warning : InCompilerWarnings)
	{
		FString ModifiedWarning = !DebugName.IsEmpty() ? FString::Printf(TEXT("%s [%s]"), *Warning.GetErrorString(), *DebugName) : Warning.GetErrorString();
		// Maintain sorted order in Entry.CompilerWarnings & deduplicate
		const int32 WarningIndex = Algo::LowerBound(Entry.CompilerWarnings, ModifiedWarning);
		if (WarningIndex >= Entry.CompilerWarnings.Num() || Entry.CompilerWarnings[WarningIndex] != ModifiedWarning)
		{
			Entry.CompilerWarnings.Insert(ModifiedWarning, WarningIndex);
		}
	}
}

void FShaderMapResourceCode::ValidateShaderStatisticsEditorOnlyData(int32 Index, const TArray<FGenericShaderStat>& ShaderStatistics)
{
	check(ShaderEditorOnlyDataEntries.IsValidIndex(Index));
	FShaderEditorOnlyDataEntry& Entry = ShaderEditorOnlyDataEntries[Index];

	if (Entry.ShaderStatistics.Num() != ShaderStatistics.Num())
	{
		UE_LOG(LogShaders, Warning, TEXT("Non-determinism detected in shader statistics.  Multiple duplicate shaders have the same shader statistics."));
		return;
	}

	for (int i = 0; i < ShaderStatistics.Num(); ++i)
	{
		const FGenericShaderStat& StatA = Entry.ShaderStatistics[i];
		const FGenericShaderStat& StatB = ShaderStatistics[i];
		if (!(StatA == StatB))
		{
			UE_LOG(LogShaders, Warning, TEXT("Non-determinism detected in shader statistics.  Multiple duplicate shaders have the same shader statistics."));
			return;
		}
	}
}

void FShaderMapResourceCode::LogShaderCompilerWarnings()
{
	if (ShaderEditorOnlyDataEntries.Num() > 0 && GShaderCompilerEmitWarningsOnLoad != 0)
	{
		// Emit all the compiler warnings seen whilst serializing/loading this shader to the log.
		// Since successfully compiled shaders are stored in the DDC, we'll get the compiler warnings
		// even if we didn't compile the shader this run.
		for (const FShaderEditorOnlyDataEntry& Entry : ShaderEditorOnlyDataEntries)
		{
			for (const FString& CompilerWarning : Entry.CompilerWarnings)
			{
				UE_LOG(LogShaderWarnings, Warning, TEXT("%s"), *CompilerWarning);
			}
		}
	}
}
#endif // WITH_EDITORONLY_DATA

void FShaderMapResourceCode::ToString(FStringBuilderBase& OutString) const
{
	OutString.Appendf(TEXT("Shaders: Num=%d\n"), ShaderHashes.Num());
	for (int32 i = 0; i < ShaderHashes.Num(); ++i)
	{
		const FShaderCodeResource& Res = ShaderCodeResources[i];
		OutString.Appendf(TEXT("    [%d]: { Hash: %s, Freq: %s, Size: %llu, UncompressedSize: %d }\n"),
			i, *ShaderHashes[i].ToString(), GetShaderFrequencyString(Res.GetFrequency()), Res.GetCodeBuffer().GetSize(), Res.GetUncompressedSize());
	}
}

void FShaderMapResourceCode::Serialize(FShaderSerializeContext& Ctx)
{
	FArchive& Ar = Ctx.GetMainArchive();
	Ar << ResourceHash;
	Ar << ShaderHashes;
	if (!Ctx.EnableCustomCodeSerialize())
	{
		Ar << ShaderCodeResources;
	}
	else
	{
		if (Ar.IsLoading())
		{
			ShaderCodeResources.SetNum(ShaderHashes.Num());
		}
		
		if (Ctx.ReserveCodeFunc)
		{
			Ctx.ReserveCodeFunc(ShaderCodeResources.Num());
		}

		for (int32 CodeIndex = 0; CodeIndex < ShaderCodeResources.Num(); ++CodeIndex)
		{
			Ctx.SerializeCode(ShaderCodeResources[CodeIndex], CodeIndex);
		}
	}
	check(ShaderCodeResources.Num() == ShaderHashes.Num());
#if WITH_EDITORONLY_DATA
	const bool bSerializeEditorOnlyData = !Ctx.bLoadingCooked && (!Ar.IsCooking() || Ar.CookingTarget()->HasEditorOnlyData());
	if (bSerializeEditorOnlyData)
	{
		Ar << ShaderEditorOnlyDataEntries;
	}
#endif // WITH_EDITORONLY_DATA
	ApplyResourceStats(*this);

#if WITH_EDITORONLY_DATA
	if (Ar.IsLoading())
	{
		LogShaderCompilerWarnings();
	}
#endif
}

#if WITH_EDITORONLY_DATA
void FShaderMapResourceCode::NotifyShadersCompiled(FName FormatName)
{
#if WITH_ENGINE
	// Notify the platform shader format that this particular shader is being used in the cook.
	// We discard this data in cooked builds unless Ar.CookingTarget()->HasEditorOnlyData() is true.
	if (ShaderEditorOnlyDataEntries.Num())
	{
		if (const IShaderFormat* ShaderFormat = GetTargetPlatformManagerRef().FindShaderFormat(FormatName))
		{
			for (const FShaderEditorOnlyDataEntry& Entry : ShaderEditorOnlyDataEntries)
			{
				ShaderFormat->NotifyShaderCompiled(Entry.PlatformDebugData, FormatName, Entry.DebugInfo);
			}
		}
	}
#endif // WITH_ENGINE
}
#endif // WITH_EDITORONLY_DATA

FShaderMapResource::FShaderMapResource(EShaderPlatform InPlatform, int32 NumShaders)
	: NumRHIShaders(static_cast<uint32>(NumShaders))
	, bAtLeastOneRHIShaderCreated(0)
	, Platform(InPlatform)
	, NumRefs(0)
{
	RHIShaders = MakeUnique<std::atomic<FRHIShader*>[]>(NumRHIShaders); // this MakeUnique() zero-initializes the array
#if RHI_RAYTRACING
	if (GRHISupportsRayTracing && GRHISupportsRayTracingShaders)
	{
		RayTracingLibraryIndices.AddUninitialized(NumShaders);
		FMemory::Memset(RayTracingLibraryIndices.GetData(), 0xff, NumShaders * RayTracingLibraryIndices.GetTypeSize());
	}
#endif // RHI_RAYTRACING
}

FShaderMapResource::~FShaderMapResource()
{
	ReleaseShaders();
	check(NumRefs.load(std::memory_order_relaxed) == 0);
}

void FShaderMapResource::AddRef()
{
	NumRefs.fetch_add(1, std::memory_order_relaxed);
}

void FShaderMapResource::Release()
{
	check(NumRefs.load(std::memory_order_relaxed) > 0);
	if (NumRefs.fetch_sub(1, std::memory_order_release) - 1 == 0 && TryRelease())
	{
		//check https://www.boost.org/doc/libs/1_55_0/doc/html/atomic/usage_examples.html for explanation
		std::atomic_thread_fence(std::memory_order_acquire);
		// Send a release message to the rendering thread when the shader loses its last reference.
		BeginReleaseResource(this);
		BeginCleanup(this);

		DEC_DWORD_STAT_BY(STAT_Shaders_ShaderResourceMemory, GetSizeBytes());
	}
}

void FShaderMapResource::ReleaseShaders()
{
	if (RHIShaders)
	{
		FScopeLock ScopeLock(&RHIShadersCreationGuard);

		int NumReleaseShaders = 0;

		for (uint32 Idx = 0; Idx < NumRHIShaders; ++Idx)
		{
			if (FRHIShader* Shader = RHIShaders[Idx].load(std::memory_order_acquire))
			{
				Shader->Release();
				NumReleaseShaders++;
				DEC_DWORD_STAT(STAT_Shaders_NumShadersCreated);
			}
		}

#if (CSV_PROFILER_STATS && !UE_BUILD_SHIPPING) 
		TCsvPersistentCustomStat<int>* CsvStatNumShadersCreated = FCsvProfiler::Get()->GetOrCreatePersistentCustomStatInt(TEXT("NumShadersCreated"), CSV_CATEGORY_INDEX(Shaders));
		CsvStatNumShadersCreated->Sub(NumReleaseShaders);
#endif

		RHIShaders = nullptr;
		NumRHIShaders = 0;
		if (bAtLeastOneRHIShaderCreated)
		{
			DEC_DWORD_STAT(STAT_Shaders_NumShaderMapsUsedForRendering);

#if (CSV_PROFILER_STATS && !UE_BUILD_SHIPPING) 
			if(CsvStatNumShaderMapsUsedForRendering == nullptr)
			{
				CsvStatNumShaderMapsUsedForRendering = FCsvProfiler::Get()->GetOrCreatePersistentCustomStatInt(TEXT("NumShaderMapsUsedForRendering"), CSV_CATEGORY_INDEX(Shaders));
			}
			CsvStatNumShaderMapsUsedForRendering->Sub(1);
#endif
		}
		bAtLeastOneRHIShaderCreated = false;
	}
}


void FShaderMapResource::ReleaseRHI()
{
#if RHI_RAYTRACING
	if (GRHISupportsRayTracing && GRHISupportsRayTracingShaders)
	{
		check(NumRHIShaders == static_cast<uint32>(RayTracingLibraryIndices.Num()));

		for (uint32 Idx = 0; Idx < NumRHIShaders; ++Idx)
		{
			if (FRHIShader* Shader = RHIShaders[Idx].load(std::memory_order_acquire))
			{
				int32 IndexInLibrary = RayTracingLibraryIndices[Idx];
				switch (Shader->GetFrequency())
				{
				case SF_RayHitGroup:
					GlobalRayTracingHitGroupLibrary.RemoveShader(IndexInLibrary, static_cast<FRHIRayTracingShader*>(Shader));
					break;
				case SF_RayCallable:
					GlobalRayTracingCallableShaderLibrary.RemoveShader(IndexInLibrary, static_cast<FRHIRayTracingShader*>(Shader));
					break;
				case SF_RayMiss:
					GlobalRayTracingMissShaderLibrary.RemoveShader(IndexInLibrary, static_cast<FRHIRayTracingShader*>(Shader));
					break;
				default:
					break;
				}
			}
		}
	}
	RayTracingLibraryIndices.Empty();
#endif // RHI_RAYTRACING

	ReleaseShaders();
}

void FShaderMapResource::BeginCreateAllShaders()
{
	FShaderMapResource* Resource = this;
	ENQUEUE_RENDER_COMMAND(InitCommand)(
		[Resource](FRHICommandListImmediate& RHICmdList)
	{
		for (int32 ShaderIndex = 0; ShaderIndex < Resource->GetNumShaders(); ++ShaderIndex)
		{
			Resource->GetShader(ShaderIndex, true /*bRequired*/);
		}
	});
}

FRHIShader* FShaderMapResource::CreateShaderOrCrash(int32 ShaderIndex, bool bRequired)
{
	FRHIShader* Shader = nullptr;
	// create before taking the lock. This may cause multiple creations, but it's better
	// than a potential oversubscription deadlock, since CreateShader can spawn async tasks
	FRHIShader* CreatedShader = CreateRHIShaderOrCrash(ShaderIndex, bRequired);	// guaranteed to return non-null if required is set
	if (CreatedShader == nullptr)
	{
		check(!bRequired);
		return nullptr;
	}

	{
		// Most shadermaps have <100 shaders, and less than a half of them can be created. 
		// However, if this path is often contended, you can slice this lock (but remember to take care of STAT_Shaders_NumShaderMapsUsedForRendering!)
		FScopeLock ScopeLock(&RHIShadersCreationGuard);

		Shader = RHIShaders[ShaderIndex].load(std::memory_order_relaxed);
		if (UNLIKELY(Shader == nullptr))
		{
			Shader = CreatedShader;
			CreatedShader = nullptr;
			RHIShaders[ShaderIndex].store(Shader, std::memory_order_release);

			if (!bAtLeastOneRHIShaderCreated)
			{
				INC_DWORD_STAT(STAT_Shaders_NumShaderMapsUsedForRendering);

#if (CSV_PROFILER_STATS && !UE_BUILD_SHIPPING) 
				if (CsvStatNumShaderMapsUsedForRendering == nullptr)
				{
					CsvStatNumShaderMapsUsedForRendering = FCsvProfiler::Get()->GetOrCreatePersistentCustomStatInt(TEXT("NumShaderMapsUsedForRendering"), CSV_CATEGORY_INDEX(Shaders));
				}
				CsvStatNumShaderMapsUsedForRendering->Add(1);
#endif
				bAtLeastOneRHIShaderCreated = 1;
			}

#if RHI_RAYTRACING
			// Registers RT shaders in global "libraries" that track all shaders potentially usable in a scene for adding to RTPSO
			EShaderFrequency Frequency = Shader->GetFrequency();
			if (LIKELY(GRHISupportsRayTracing && GRHISupportsRayTracingShaders))
			{
				switch (Frequency)
				{
					case SF_RayHitGroup:
						RayTracingLibraryIndices[ShaderIndex] = GlobalRayTracingHitGroupLibrary.AddShader(static_cast<FRHIRayTracingShader*>(Shader));
						break;
					case SF_RayCallable:
						RayTracingLibraryIndices[ShaderIndex] = GlobalRayTracingCallableShaderLibrary.AddShader(static_cast<FRHIRayTracingShader*>(Shader));
						break;
					case SF_RayMiss:
						RayTracingLibraryIndices[ShaderIndex] = GlobalRayTracingMissShaderLibrary.AddShader(static_cast<FRHIRayTracingShader*>(Shader));
						break;
					case SF_RayGen:
						// NOTE: we do not maintain a library for raygen shaders since the list of rayshaders we care about is usually small and consistent
						break;
					default:
						break;
				}
			}
#endif // RHI_RAYTRACING

			// When using shader library, shader code is usually preloaded during the material load. Release it
			// since we won't need it anymore for this shader.
			ReleasePreloadedShaderCode(ShaderIndex);
		}
	}

	if (LIKELY(CreatedShader))
	{
		// free redundantly created shader
		checkSlow(Shader != nullptr);
		CreatedShader->Release();
	}

	return Shader;
}

FSHAHash FShaderMapResource_InlineCode::GetShaderHash(int32 ShaderIndex)
{
	return Code->ShaderHashes[ShaderIndex];
}

FRHIShader* FShaderMapResource_InlineCode::CreateRHIShaderOrCrash(int32 ShaderIndex, bool bRequired)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FShaderMapResource_InlineCode::CreateRHIShaderOrCrash);

	// we can't have this called on the wrong platform's shaders
	if (!ArePlatformsCompatible(GMaxRHIShaderPlatform, GetPlatform()))
	{
		UE_LOG(LogShaders, Fatal, TEXT("FShaderMapResource_InlineCode::InitRHI got platform %s but it is not compatible with %s"),
			*LegacyShaderPlatformToShaderFormat(GetPlatform()).ToString(), *LegacyShaderPlatformToShaderFormat(GMaxRHIShaderPlatform).ToString());
		// unreachable
		return nullptr;
	}

	FMemStackBase& MemStack = FMemStack::Get();
	const FShaderCodeResource& ShaderCodeResource = Code->ShaderCodeResources[ShaderIndex];
	FSharedBuffer ShaderCode = ShaderCodeResource.GetCodeBuffer();
	TConstArrayView<uint8> ShaderCodeView = ShaderCodeResource.GetCodeView();

	FMemMark Mark(MemStack);
	int32 UncompressedSize = ShaderCodeResource.GetUncompressedSize();
	if (ShaderCode.GetSize() != UncompressedSize)
	{
		void* UncompressedCode = MemStack.Alloc(UncompressedSize, 16);
		bool bSucceed = FCompression::UncompressMemory(GetShaderCompressionFormat(), UncompressedCode, UncompressedSize, ShaderCode.GetData(), ShaderCode.GetSize());
		check(bSucceed);
		ShaderCodeView = MakeArrayView(reinterpret_cast<const uint8*>(UncompressedCode), UncompressedSize);
	}

	const FSHAHash& ShaderHash = Code->ShaderHashes[ShaderIndex];
	const EShaderFrequency Frequency = ShaderCodeResource.GetFrequency();

	TRefCountPtr<FRHIShader> RHIShader;
	switch (Frequency)
	{
	case SF_Vertex: RHIShader = RHICreateVertexShader(ShaderCodeView, ShaderHash); break;
	case SF_Mesh: RHIShader = RHICreateMeshShader(ShaderCodeView, ShaderHash); break;
	case SF_Amplification: RHIShader = RHICreateAmplificationShader(ShaderCodeView, ShaderHash); break;
	case SF_Pixel: RHIShader = RHICreatePixelShader(ShaderCodeView, ShaderHash); break;
	case SF_Geometry: RHIShader = RHICreateGeometryShader(ShaderCodeView, ShaderHash); break;
	case SF_Compute: RHIShader = RHICreateComputeShader(ShaderCodeView, ShaderHash); break;
	case SF_WorkGraphRoot: RHIShader = RHICreateWorkGraphShader(ShaderCodeView, ShaderHash, SF_WorkGraphRoot); break;
	case SF_WorkGraphComputeNode: RHIShader = RHICreateWorkGraphShader(ShaderCodeView, ShaderHash, SF_WorkGraphComputeNode); break;
	case SF_RayGen: case SF_RayMiss: case SF_RayHitGroup: case SF_RayCallable:
#if RHI_RAYTRACING
		if (GRHISupportsRayTracing && GRHISupportsRayTracingShaders)
		{
			RHIShader = RHICreateRayTracingShader(ShaderCodeView, ShaderHash, Frequency);
		}
#endif // RHI_RAYTRACING
		break;
	default:
		checkNoEntry();
		break;
	}
	if (UNLIKELY(RHIShader == nullptr))
	{
		if (bRequired)
		{
			UE_LOG(LogShaders, Fatal, TEXT("FShaderMapResource_InlineCode::InitRHI is unable to create a shader: frequency=%d, hash=%s."), static_cast<int32>(Frequency), *ShaderHash.ToString());
		}
		return nullptr;
	}

	INC_DWORD_STAT(STAT_Shaders_NumShadersCreated);

#if (CSV_PROFILER_STATS && !UE_BUILD_SHIPPING) 
	TCsvPersistentCustomStat<int>* CsvStatNumShadersCreated = FCsvProfiler::Get()->GetOrCreatePersistentCustomStatInt(TEXT("NumShadersCreated"), CSV_CATEGORY_INDEX(Shaders));
	CsvStatNumShadersCreated->Add(1);
#endif

	RHIShader->SetHash(ShaderHash);

	// contract of this function is to return a shader with an already held reference
	RHIShader->AddRef();
	return RHIShader;
}

uint32 FShaderMapResource_InlineCode::GetSizeBytes() const
{
	uint32 TotalSize = 0;

	if (Code)
	{
		TotalSize += Code->GetSizeBytes();
	}

	TotalSize += sizeof(FShaderMapResource_InlineCode);
	TotalSize += GetAllocatedSize();

	return TotalSize;
}
