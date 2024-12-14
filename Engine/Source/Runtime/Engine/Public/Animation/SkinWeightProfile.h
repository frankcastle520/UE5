// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BoneIndices.h"
#include "Misc/StringBuilder.h"
#include "RHIGPUReadback.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "RenderingThread.h"
#include "String/Join.h"
#include "Templates/UniquePtr.h"
#include "UObject/NameTypes.h"
#include "UObject/PerPlatformProperties.h"

#include "SkinWeightProfile.generated.h"

class USkeletalMesh;
class UDebugSkelMeshComponent;

namespace SkeletalMeshImportData
{
	struct FVertInfluence;
}

extern ENGINE_API int32 GSkinWeightProfilesLoadByDefaultMode;
extern ENGINE_API int32 GSkinWeightProfilesDefaultLODOverride;
extern ENGINE_API int32 GSkinWeightProfilesAllowedFromLOD;

extern ENGINE_API FAutoConsoleVariableRef CVarSkinWeightsLoadByDefaultMode;
extern ENGINE_API FAutoConsoleVariableRef CVarSkinWeightProfilesDefaultLODOverride;
extern ENGINE_API FAutoConsoleVariableRef CVarSkinWeightProfilesAllowedFromLOD;

/** Structure storing user facing properties, and is used to identify profiles at the SkeletalMesh level*/
USTRUCT()
struct FSkinWeightProfileInfo
{
	GENERATED_BODY()

	/** Name of the Skin Weight Profile*/
	UPROPERTY(EditAnywhere, Category = SkinWeights)
	FName Name;
	
	/** Whether or not this Profile should be considered the Default loaded for specific LODs rather than the original Skin Weights of the Skeletal Mesh */
	UPROPERTY(EditAnywhere, Category = SkinWeights)
	FPerPlatformBool DefaultProfile;

	/** When DefaultProfile is set any LOD below this LOD Index will override the Skin Weights of the Skeletal Mesh with the Skin Weights from this Profile */
	UPROPERTY(EditAnywhere, Category = SkinWeights, meta=(EditCondition="DefaultProfile", ClampMin=0, DisplayName = "Default Profile from LOD Index"))
	FPerPlatformInt DefaultProfileFromLODIndex;
	
#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category = SkinWeights)
	TMap<int32, FString> PerLODSourceFiles;
#endif
};

#if WITH_EDITORONLY_DATA

/** Editor only skin weight representation */
struct FRawSkinWeight
{
	// MAX_TOTAL_INFLUENCES for now
	FBoneIndexType InfluenceBones[MAX_TOTAL_INFLUENCES];
	uint16 InfluenceWeights[MAX_TOTAL_INFLUENCES];

	friend FArchive& operator<<(FArchive& Ar, FRawSkinWeight& OverrideEntry);
};

/** Editor only representation of a Skin Weight profile, stored as part of FSkeletalMeshLODModel, used as a base for generating the runtime version (FSkeletalRenderDataSkinWeightProfilesData) */
struct FImportedSkinWeightProfileData
{
	TArray<FRawSkinWeight> SkinWeights;

	//This is the result of the imported data before the chunking
	//We use this data every time we need to re-chunk the skeletal mesh
	TArray<SkeletalMeshImportData::FVertInfluence> SourceModelInfluences;

	friend FArchive& operator<<(FArchive& Ar, FImportedSkinWeightProfileData& ProfileData);
};

#endif // WITH_EDITORONLY_DATA

/** Runtime structure containing the set of override weights and the associated vertex indices */
struct FRuntimeSkinWeightProfileData
{
	/** Structure containing per Skin Weight offset and length */
	struct FSkinWeightOverrideInfo
	{
		/** Offset into FRuntimeSkinWeightOverrideData.Weights */
		uint32 InfluencesOffset;
#if WITH_EDITORONLY_DATA
		/** Number of influences to be read from FRuntimeSkinWeightOverrideData.Weights */
		uint8 NumInfluences_DEPRECATED;
#endif
		friend FArchive& operator<<(FArchive& Ar, FSkinWeightOverrideInfo& OverrideInfo);
	};

	void ApplyOverrides(FSkinWeightVertexBuffer* OverrideBuffer) const;	
	void ApplyDefaultOverride(FSkinWeightVertexBuffer* Buffer) const;

#if WITH_EDITORONLY_DATA
	/** Per skin weight offset into Weights array and number of weights stored */
	TArray<FSkinWeightOverrideInfo> OverridesInfo_DEPRECATED;
	/** Bulk data containing all Weights, stored as bone id in upper and weight in lower (8) bits */
	TArray<uint16> Weights_DEPRECATED;	
#endif 

	// Either contains FBoneIndexType or uint8 bone indices
	TArray<uint8> BoneIDs;
	TArray<uint8> BoneWeights;
	/** Map between Vertex Indices and the influence offset into BoneIDs/BoneWeights (DEPRECATED and entries of OverridesInfo) */
	TMap<uint32, uint32> VertexIndexToInfluenceOffset;

	uint8 NumWeightsPerVertex;
	bool b16BitBoneIndices;
	
	friend FArchive& operator<<(FArchive& Ar, FRuntimeSkinWeightProfileData& OverrideData);
};

struct FSkinweightReadbackData
{
	TUniquePtr<FRHIGPUBufferReadback> BufferReadback;
	TArray<uint8> ReadbackData;
	uint32 ReadbackFinishedFrameIndex = std::numeric_limits<uint32>::max();
};

/** An identifier to identify a skin weight profile layer stack. 
  * Currently limited to two layers, but can be extended if needed.
  */
struct FSkinWeightProfileStack 
{
	static constexpr int32 MaxLayerCount = 2;
	TStaticArray<FName, MaxLayerCount> Layers;
	
	FSkinWeightProfileStack() :
		Layers{InPlace, NAME_None}
	{}

	// A fancy way of specifying a function with FName argument count between 1 and MaxLayerCount
	template <
		typename... ArgTypes
		UE_REQUIRES((sizeof...(ArgTypes) > 0 && sizeof...(ArgTypes) <= MaxLayerCount) && UE::Core::Private::TCanBeConvertedToFromAll_V<FName, ArgTypes...>)
	>
	explicit FSkinWeightProfileStack(ArgTypes&&... Args)
		: Layers{Forward<ArgTypes>(Args)...}
	{
		// Make sure that we mark any remaining elements are NAME_None to avoid corrupted names.  
		for (int32 LayerIndex = sizeof...(ArgTypes); LayerIndex < MaxLayerCount; ++LayerIndex)
		{
			Layers[LayerIndex] = NAME_None;
		}
	}

	explicit FSkinWeightProfileStack(const FName InProfileNames[MaxLayerCount])
	{
		for (int32 LayerIndex = 0; LayerIndex < MaxLayerCount; ++LayerIndex)
		{
			Layers[LayerIndex] = InProfileNames[LayerIndex];
		}
	}

	bool operator==(const FSkinWeightProfileStack& InProfileStack) const
	{
		return Layers == InProfileStack.Layers;
	}
	bool operator!=(const FSkinWeightProfileStack& InProfileStack) const
	{
		return Layers != InProfileStack.Layers;
	}

	FName operator[](int32 InLayerIndex) const
	{
		return Layers[InLayerIndex];
	}

	FName& operator[](int32 InLayerIndex)
	{
		return Layers[InLayerIndex];
	}

	bool IsEmpty() const
	{
		for (int32 LayerIndex = 0; LayerIndex < MaxLayerCount; ++LayerIndex)
		{
			if (!Layers[LayerIndex].IsNone())
			{
				return false;
			}
		}
		return true;
	}

	void CopyIntoArray(FName OutProfileNames[MaxLayerCount]) const
	{
		for (int32 LayerIndex = 0; LayerIndex < MaxLayerCount; ++LayerIndex)
		{
			OutProfileNames[LayerIndex] = Layers[LayerIndex];
		}
	}
	
	/** Returns a normalized layer stack, such that any empty layers are collapsed. */
	FSkinWeightProfileStack Normalized() const
	{
		FSkinWeightProfileStack NormalizedLayers;
		
		for (int32 ReadLayer = 0, WriteLayer = 0; ReadLayer < MaxLayerCount; ++ReadLayer)
		{
			if (!Layers[ReadLayer].IsNone())
			{
				NormalizedLayers.Layers[WriteLayer++] = Layers[ReadLayer];
			}
		}
		return NormalizedLayers;
	}
	
	FString GetUniqueId() const
	{
		TStringBuilder<256> IdString(InPlace, UE::String::JoinBy(Layers, [](FName InLayerName) { return InLayerName.ToString(); }, TEXT("-")));
		return IdString.ToString();
	}

	friend uint32 GetTypeHash(const FSkinWeightProfileStack& InProfileStack)
	{
		uint32 Hash = 0;
		for (int32 LayerIndex = 0; LayerIndex < MaxLayerCount; ++LayerIndex)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(InProfileStack[LayerIndex]));
		}
		return Hash;
	}
};


/** Runtime structure for keeping track of skin weight profile(s) and the associated buffer */
struct FSkinWeightProfilesData
{
	ENGINE_API void Init(FSkinWeightVertexBuffer* InBaseBuffer);

	ENGINE_API ~FSkinWeightProfilesData();
	
	DECLARE_DELEGATE_RetVal_ThreeParams(int32 /** Index into Profiles ArrayView */, FOnPickOverrideSkinWeightProfile, const USkeletalMesh* /** Skeletal Mesh to pick the profile for */, const TArrayView<const FSkinWeightProfileInfo> /** Available skin weight profiles to pick from */, int32 /** LOD Index */);
	static ENGINE_API FOnPickOverrideSkinWeightProfile OnPickOverrideSkinWeightProfile;

#if !WITH_EDITOR
	// Mark this as non-editor only to prevent mishaps from users
	ENGINE_API void OverrideBaseBufferSkinWeightData(USkeletalMesh* Mesh, int32 LODIndex);
#endif 
	ENGINE_API void SetDynamicDefaultSkinWeightProfile(USkeletalMesh* Mesh, int32 LODIndex, bool bSerialization = false);	
	ENGINE_API void ClearDynamicDefaultSkinWeightProfile(USkeletalMesh* Mesh, int32 LODIndex);
	ENGINE_API void SetupDynamicDefaultSkinWeightProfile();
	FSkinWeightVertexBuffer* GetDefaultOverrideBuffer() const { return DefaultOverrideSkinWeightBuffer; }

	// Buffer lookup for layered profiles.
	ENGINE_API FSkinWeightVertexBuffer* GetOverrideBuffer(const FSkinWeightProfileStack& InProfileStack) const;
	bool ContainsOverrideBuffer(const FSkinWeightProfileStack& InProfileStack) const;
	
	// Lookups for individual profiles.
	ENGINE_API bool ContainsProfile(const FName& ProfileName) const;
	ENGINE_API const FRuntimeSkinWeightProfileData* GetOverrideData(const FName& ProfileName) const;
	ENGINE_API FRuntimeSkinWeightProfileData& AddOverrideData(const FName& ProfileName);

	ENGINE_API void ReleaseResources();

	ENGINE_API SIZE_T GetResourcesSize() const;
	ENGINE_API SIZE_T GetCPUAccessMemoryOverhead() const;
 
	friend FArchive& operator<<(FArchive& Ar, FSkinWeightProfilesData& OverrideData);

	ENGINE_API void SerializeMetaData(FArchive& Ar);

	ENGINE_API void ReleaseCPUResources();

	ENGINE_API void CreateRHIBuffers(FRHICommandListBase& RHICmdList, TArray<TPair<FSkinWeightProfileStack, FSkinWeightRHIInfo>>& OutBuffers);

	ENGINE_API void InitRHIForStreaming(const TArray<TPair<FSkinWeightProfileStack, FSkinWeightRHIInfo>>& IntermediateBuffers, FRHIResourceReplaceBatcher& Batcher);
	ENGINE_API void ReleaseRHIForStreaming(FRHIResourceReplaceBatcher& Batcher);

	ENGINE_API bool IsDefaultOverridden() const { return bDefaultOverridden; }
	ENGINE_API bool IsStaticOverridden() const { return bStaticOverridden; }
	ENGINE_API FSkinWeightProfileStack GetDefaultProfileStack() const { return DefaultProfileStack; }
	
private:
	friend class FSkinWeightProfileManager;
	friend class FSkinWeightProfileManagerAsyncTask;
	
	void InitialiseProfileBuffer(const FSkinWeightProfileStack& InProfileStack);
	void ReleaseBuffer(const FSkinWeightProfileStack& InProfileStack, bool bForceRelease = false);
	
	void ApplyOverrideProfileStack(const FSkinWeightProfileStack& InProfileStack, FSkinWeightVertexBuffer* OverrideBuffer, const uint8* BaseBufferData = nullptr);
	bool IsPendingReadback() const;
	void EnqueueGPUReadback();
	bool IsGPUReadbackFinished() const;
	void EnqueueDataReadback();
	bool IsDataReadbackPending() const;
	bool IsDataReadbackFinished() const;
	void ResetGPUReadback();

	FSkinWeightVertexBuffer* BaseBuffer = nullptr;
	FSkinWeightVertexBuffer* DefaultOverrideSkinWeightBuffer = nullptr;

	TMap<FSkinWeightProfileStack, FSkinWeightVertexBuffer*> ProfileStackToBuffer;
	
	TMap<FName, FRuntimeSkinWeightProfileData> OverrideData;

	bool bDefaultOverridden = false;
	bool bStaticOverridden = false;
	FSkinWeightProfileStack DefaultProfileStack;

	FSkinweightReadbackData ReadbackData;
};

