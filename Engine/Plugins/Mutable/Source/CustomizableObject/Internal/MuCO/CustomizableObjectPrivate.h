// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectCompilerTypes.h"
#include "MuCO/CustomizableObjectStreamedResourceData.h"
#include "MuCO/StateMachine.h"
#include "MuCO/CustomizableObjectUIData.h"
#include "MuCO/CustomizableObjectIdentifier.h"
#include "Serialization/BulkData.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/SoftObjectPtr.h"
#include "MuR/Types.h"

#if WITH_EDITOR
#include "Misc/Guid.h"
#include "Engine/DataTable.h"
#endif

#include "CustomizableObjectPrivate.generated.h"

namespace mu 
{ 
	class Model;
	struct FBoneName;
}

#if WITH_EDITOR
namespace MutablePrivate
{
	struct FClassifyNode;
}

namespace UE::DerivedData
{
	struct FValueId;
	struct FCacheKey;
	enum class ECachePolicy : uint32;
}
#endif

class USkeletalMesh;
class USkeleton;
class UPhysicsAsset;
class UMaterialInterface;
class UTexture;
class UAnimInstance;
class UAssetUserData;
class USkeletalMeshLODSettings;
struct FModelResources;
struct FModelStreamableBulkData;
struct FObjectAndNameAsStringProxyArchive;

FGuid CUSTOMIZABLEOBJECT_API GenerateIdentifier(const UCustomizableObject& CustomizableObject);

// Warning! MutableCompiledDataHeader must be the first data serialized in a stream
struct MutableCompiledDataStreamHeader
{
	int32 InternalVersion=0;
	FGuid VersionId;

	MutableCompiledDataStreamHeader() { }
	MutableCompiledDataStreamHeader(int32 InInternalVersion, FGuid InVersionId) : InternalVersion(InInternalVersion), VersionId(InVersionId) { }

	friend FArchive& operator<<(FArchive& Ar, MutableCompiledDataStreamHeader& Header)
	{
		Ar << Header.InternalVersion;
		Ar << Header.VersionId;

		return Ar;
	}
};

struct FCustomizableObjectStreameableResourceId
{
	enum class EType : uint8
	{
		None                  = 0,
		AssetUserData         = 1,
		RealTimeMorphTarget   = 2,
		Clothing              = 3,
	};

	uint64 Id   : 64 - 8;
	uint64 Type : 8;

	friend bool operator==(FCustomizableObjectStreameableResourceId A, FCustomizableObjectStreameableResourceId B)
	{
		return BitCast<uint64>(A) == BitCast<uint64>(B);
	}
};
static_assert(sizeof(FCustomizableObjectStreameableResourceId) == sizeof(uint64));


USTRUCT()
struct FMutableRemappedBone
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FName Name;
	
	UPROPERTY()
	uint32 Hash = 0;
	
	bool operator==(const FName& InName)
	{
		return Name == InName;
	}

#if WITH_EDITORONLY_DATA
	friend FArchive& operator<<(FArchive& Ar, FMutableRemappedBone& RemappedBone);
#endif
};


USTRUCT()
struct FMutableModelParameterValue
{
	GENERATED_USTRUCT_BODY()

	FMutableModelParameterValue() = default;

	UPROPERTY()
	FString Name;

	UPROPERTY()
	int Value = 0;
};


USTRUCT()
struct FMutableModelParameterProperties
{
	GENERATED_USTRUCT_BODY()

	FMutableModelParameterProperties() = default;
	FString Name;

	UPROPERTY()
	EMutableParameterType Type = EMutableParameterType::None;

	UPROPERTY()
	TArray<FMutableModelParameterValue> PossibleValues;
};


class FMeshCache
{
public:
	USkeletalMesh* Get(const TArray<mu::FResourceID>& Key);

	void Add(const TArray<mu::FResourceID>& Key, USkeletalMesh* Value);

private:
	TMap<TArray<mu::FResourceID>, TWeakObjectPtr<USkeletalMesh>> GeneratedMeshes;
};


class FSkeletonCache
{
public:
	USkeleton* Get(const TArray<uint16>& Key);

	void Add(const TArray<uint16>& Key, USkeleton* Value);

private:
	TMap<TArray<uint16>, TWeakObjectPtr<USkeleton>> MergedSkeletons;
};


struct FCustomizableObjectStatusTypes
{
	enum class EState : uint8
	{
		Loading = 0, // Waiting for PostLoad and Asset Registry to finish.
		ModelLoaded, // Model loaded correctly.
		NoModel, // No model (due to no model not found and automatic compilations disabled).
		// Compiling, // Compiling the CO. Equivalent to UCustomizableObject::IsLocked = true.

		Count,
	};
	
	static constexpr EState StartState = EState::NoModel;

	static constexpr bool ValidTransitions[3][3] =
	{
		// TO
		// Loading, ModelLoaded, NoModel // FROM
		{false,   true,        true},  // Loading
		{false,   true,        true},  // ModelLoaded
		{true,    true,        true},  // NoModel
	};
};

using FCustomizableObjectStatus = FStateMachine<FCustomizableObjectStatusTypes>;


USTRUCT()
struct CUSTOMIZABLEOBJECT_API FMutableModelImageProperties
{
	GENERATED_USTRUCT_BODY()

	FMutableModelImageProperties()
		: Filter(TF_Default)
		, SRGB(0)
		, FlipGreenChannel(0)
		, IsPassThrough(0)
		, LODBias(0)
		, MipGenSettings(TextureMipGenSettings::TMGS_FromTextureGroup)
		, LODGroup(TEXTUREGROUP_World)
		, AddressX(TA_Clamp)
		, AddressY(TA_Clamp)
	{}

	FMutableModelImageProperties(const FString& InTextureParameterName, TextureFilter InFilter, uint32 InSRGB,
		uint32 InFlipGreenChannel, uint32 bInIsPassThrough, int32 InLODBias, TEnumAsByte<TextureMipGenSettings> InMipGenSettings, 
		TEnumAsByte<enum TextureGroup> InLODGroup, TEnumAsByte<enum TextureAddress> InAddressX, TEnumAsByte<enum TextureAddress> InAddressY)
		: TextureParameterName(InTextureParameterName)
		, Filter(InFilter)
		, SRGB(InSRGB)
		, FlipGreenChannel(InFlipGreenChannel)
		, IsPassThrough(bInIsPassThrough)
		, LODBias(InLODBias)
		, MipGenSettings(InMipGenSettings)
		, LODGroup(InLODGroup)
		, AddressX(InAddressX)
		, AddressY(InAddressY)
	{}

	// Name in the material.
	UPROPERTY()
	FString TextureParameterName;

	UPROPERTY()
	TEnumAsByte<enum TextureFilter> Filter;

	UPROPERTY()
	uint32 SRGB : 1;

	UPROPERTY()
	uint32 FlipGreenChannel : 1;

	UPROPERTY()
	uint32 IsPassThrough : 1;

	UPROPERTY()
	int32 LODBias;

	UPROPERTY()
	TEnumAsByte<TextureMipGenSettings> MipGenSettings;

	UPROPERTY()
	TEnumAsByte<enum TextureGroup> LODGroup;

	UPROPERTY()
	TEnumAsByte<enum TextureAddress> AddressX;

	UPROPERTY()
	TEnumAsByte<enum TextureAddress> AddressY;

	bool operator!=(const FMutableModelImageProperties& rhs) const;

#if WITH_EDITORONLY_DATA
	friend FArchive& operator<<(FArchive& Ar, FMutableModelImageProperties& ImageProps);
#endif
};


USTRUCT()
struct CUSTOMIZABLEOBJECT_API FMutableRefSocket
{
	GENERATED_BODY()

	UPROPERTY()
	FName SocketName;
	UPROPERTY()
	FName BoneName;

	UPROPERTY()
	FVector RelativeLocation = FVector::ZeroVector;
	UPROPERTY()
	FRotator RelativeRotation = FRotator::ZeroRotator;
	UPROPERTY()
	FVector RelativeScale = FVector::ZeroVector;

	UPROPERTY()
	bool bForceAlwaysAnimated = false;

	// When two sockets have the same name, the one with higher priority will be picked and the other discarded
	UPROPERTY()
	int32 Priority = -1;

	bool operator ==(const FMutableRefSocket& Other) const;

#if WITH_EDITORONLY_DATA
	friend FArchive& operator<<(FArchive& Ar, FMutableRefSocket& Data);
#endif
};


USTRUCT()
struct FMutableRefLODInfo
{
	GENERATED_BODY()

	UPROPERTY()
	float ScreenSize = 0.f;

	UPROPERTY()
	float LODHysteresis = 0.f;

	UPROPERTY()
	bool bSupportUniformlyDistributedSampling = false;

	UPROPERTY()
	bool bAllowCPUAccess = false;

#if WITH_EDITORONLY_DATA
	friend FArchive& operator<<(FArchive& Ar, FMutableRefLODInfo& Data);
#endif
};


USTRUCT()
struct FMutableRefLODRenderData
{
	GENERATED_BODY()

	UPROPERTY()
	bool bIsLODOptional = false;

	UPROPERTY()
	bool bStreamedDataInlined = false;

#if WITH_EDITORONLY_DATA
	friend FArchive& operator<<(FArchive& Ar, FMutableRefLODRenderData& Data);
#endif
};


USTRUCT()
struct FMutableRefLODData
{
	GENERATED_BODY()

	UPROPERTY()
	FMutableRefLODInfo LODInfo;

	UPROPERTY()
	FMutableRefLODRenderData RenderData;

#if WITH_EDITORONLY_DATA
	friend FArchive& operator<<(FArchive& Ar, FMutableRefLODData& Data);
#endif
};


USTRUCT()
struct FMutableRefSkeletalMeshSettings
{
	GENERATED_BODY()

	UPROPERTY()
	bool bEnablePerPolyCollision = false;

	UPROPERTY()
	float DefaultUVChannelDensity = 0.f;

#if WITH_EDITORONLY_DATA
	friend FArchive& operator<<(FArchive& Ar, FMutableRefSkeletalMeshSettings& Data);
#endif
};


USTRUCT()
struct FMutableRefAssetUserData
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UCustomizableObjectResourceDataContainer> AssetUserData;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	int32 AssetUserDataIndex = INDEX_NONE;
#endif

};


USTRUCT()
struct CUSTOMIZABLEOBJECT_API FMutableRefSkeletalMeshData
{
	GENERATED_BODY()

	// Reference Skeletal Mesh
	UPROPERTY()
	TObjectPtr<USkeletalMesh> SkeletalMesh;

	// Path to load the ReferenceSkeletalMesh
	UPROPERTY()
	TSoftObjectPtr<USkeletalMesh> SoftSkeletalMesh;

	// Optional USkeletalMeshLODSettings
	UPROPERTY()
	TObjectPtr<USkeletalMeshLODSettings> SkeletalMeshLODSettings;

	// LOD info
	UPROPERTY()
	TArray<FMutableRefLODData> LODData;

	// Sockets
	UPROPERTY()
	TArray<FMutableRefSocket> Sockets;

	// Bounding Box
	UPROPERTY()
	FBoxSphereBounds Bounds = FBoxSphereBounds(ForceInitToZero);

	// Settings
	UPROPERTY()
	FMutableRefSkeletalMeshSettings Settings;

	// Skeleton
	UPROPERTY()
	TObjectPtr<USkeleton> Skeleton;

	// PhysicsAsset
	UPROPERTY()
	TObjectPtr<UPhysicsAsset> PhysicsAsset;

	// Post Processing AnimBP
	UPROPERTY()
	TSoftClassPtr<UAnimInstance> PostProcessAnimInst;

	// Shadow PhysicsAsset
	UPROPERTY()
	TObjectPtr<UPhysicsAsset> ShadowPhysicsAsset;

	// Asset user data
	UPROPERTY()
	TArray<FMutableRefAssetUserData> AssetUserData;
	
#if WITH_EDITORONLY_DATA
	friend FArchive& operator<<(FArchive& Ar, FMutableRefSkeletalMeshData& Data);

	void InitResources(UCustomizableObject* InOuter, FModelResources& InModelResources, const ITargetPlatform* InTargetPlatform);
#endif

};


USTRUCT()
struct CUSTOMIZABLEOBJECT_API FAnimBpOverridePhysicsAssetsInfo
{
	GENERATED_BODY()

	UPROPERTY()
	TSoftClassPtr<UAnimInstance> AnimInstanceClass;

	UPROPERTY()
	TSoftObjectPtr<UPhysicsAsset> SourceAsset;

	UPROPERTY()
	int32 PropertyIndex = -1;

	bool operator==(const FAnimBpOverridePhysicsAssetsInfo& Rhs) const;

#if WITH_EDITORONLY_DATA
	friend FArchive& operator<<(FArchive& Ar, FAnimBpOverridePhysicsAssetsInfo& Info);
#endif
};


USTRUCT()
struct CUSTOMIZABLEOBJECT_API FMutableSkinWeightProfileInfo
{
	GENERATED_USTRUCT_BODY()

	FMutableSkinWeightProfileInfo() {};

	FMutableSkinWeightProfileInfo(FName InName, uint32 InNameId, bool InDefaultProfile, int8 InDefaultProfileFromLODIndex) : Name(InName),
		NameId(InNameId), DefaultProfile(InDefaultProfile), DefaultProfileFromLODIndex(InDefaultProfileFromLODIndex) {};

	UPROPERTY()
	FName Name;

	UPROPERTY()
	uint32 NameId = 0;

	UPROPERTY()
	bool DefaultProfile = false;

	UPROPERTY(meta = (ClampMin = 0))
	int8 DefaultProfileFromLODIndex = 0;

	bool operator==(const FMutableSkinWeightProfileInfo& Other) const;

#if WITH_EDITORONLY_DATA
	friend FArchive& operator<<(FArchive& Ar, FMutableSkinWeightProfileInfo& Info);
#endif
};


USTRUCT()
struct CUSTOMIZABLEOBJECT_API FMutableStreamableBlock
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	uint32 FileId = 0;

	/** Used to store properties of the data, necessary for its recovery. For instance if it is high-res. */
	UPROPERTY()
	uint32 Flags = 0;

	UPROPERTY()
	uint64 Offset = 0;

	friend FArchive& operator<<(FArchive& Ar, FMutableStreamableBlock& Data)
	{
		Ar << Data.FileId;
		Ar << Data.Flags;
		Ar << Data.Offset;
		return Ar;
	}
};
template<> struct TCanBulkSerialize<FMutableStreamableBlock> { enum { Value = true }; };
static_assert(sizeof(FMutableStreamableBlock) == 8 * 2);


USTRUCT()
struct FRealTimeMorphStreamable
{
	GENERATED_USTRUCT_BODY()
	
	UPROPERTY()
	TArray<FName> NameResolutionMap;

	UPROPERTY()
	FMutableStreamableBlock Block;

	UPROPERTY()
	uint32 Size = 0;

	UPROPERTY()
	uint32 SourceId = 0;

	friend FArchive& operator<<(FArchive& Ar, FRealTimeMorphStreamable& Elem)
	{
		Ar << Elem.NameResolutionMap;
		Ar << Elem.Size;
		Ar << Elem.Block;
		Ar << Elem.SourceId;

		return Ar;
	}
};

USTRUCT()
struct FMutableMeshMetadata
{
	GENERATED_USTRUCT_BODY()
	
	UPROPERTY()
	uint32 MorphMetadataId = 0;

	UPROPERTY()
	uint32 ClothingMetadataId = 0;

	UPROPERTY()
	uint32 SurfaceMetadataId = 0;

	friend FArchive& operator<<(FArchive& Ar, FMutableMeshMetadata& Elem)
	{
		Ar << Elem.MorphMetadataId;
		Ar << Elem.ClothingMetadataId;
		Ar << Elem.SurfaceMetadataId;

		return Ar;
	}
};


USTRUCT()
struct FMutableSurfaceMetadata
{
	GENERATED_USTRUCT_BODY()
	
	UPROPERTY()
	FName MaterialSlotName = FName{};
	
	UPROPERTY()
	bool bCastShadow = true;

	friend FArchive& operator<<(FArchive& Ar, FMutableSurfaceMetadata& Elem)
	{
		Ar << Elem.MaterialSlotName;
		Ar << Elem.bCastShadow;

		return Ar;
	}
};


USTRUCT()
struct FClothingStreamable
{
	GENERATED_USTRUCT_BODY()
	
	UPROPERTY()
	int32 ClothingAssetIndex = INDEX_NONE;
	
	UPROPERTY()
	int32 ClothingAssetLOD = INDEX_NONE;
	
	UPROPERTY()
	int32 PhysicsAssetIndex = INDEX_NONE;

	UPROPERTY()
	uint32 Size = 0;

	UPROPERTY()
	FMutableStreamableBlock Block;

	UPROPERTY()
	uint32 SourceId = 0;

	friend FArchive& operator<<(FArchive& Ar, FClothingStreamable& Elem)
	{
		Ar << Elem.ClothingAssetIndex;
		Ar << Elem.ClothingAssetLOD;
		Ar << Elem.PhysicsAssetIndex;
		Ar << Elem.Size;
		Ar << Elem.Block;
		Ar << Elem.SourceId;
		return Ar;
	}
};

USTRUCT()
struct FMorphTargetVertexData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FVector3f PositionDelta = FVector3f::ZeroVector;

	UPROPERTY()
	FVector3f TangentZDelta = FVector3f::ZeroVector;
	
	UPROPERTY()
	uint32 MorphNameIndex = 0;

	friend FArchive& operator<<(FArchive& Ar, FMorphTargetVertexData& Data)
	{
		Ar << Data.PositionDelta;
		Ar << Data.TangentZDelta;
		Ar << Data.MorphNameIndex;

		return Ar;
	}
};
static_assert(sizeof(FMorphTargetVertexData) == sizeof(FVector3f)*2 + sizeof(uint32)); // Make sure no padding is present.
template<> struct TCanBulkSerialize<FMorphTargetVertexData> { enum { Value = true }; };


struct FMutableParameterIndex
{
	FMutableParameterIndex(int32 InIndex, int32 InTypedIndex)
	{
		Index = InIndex;
		TypedIndex = InTypedIndex;
	}

	int32 Index = INDEX_NONE;
	int32 TypedIndex = INDEX_NONE;
};

USTRUCT()
struct CUSTOMIZABLEOBJECT_API FIntegerParameterUIData
{
	GENERATED_BODY()

	FIntegerParameterUIData() = default;
	
	FIntegerParameterUIData(const FMutableParamUIMetadata& InParamUIMetadata);
	
	UPROPERTY()
	FMutableParamUIMetadata ParamUIMetadata;

	friend FArchive& operator<<(FArchive& Ar, FIntegerParameterUIData& Struct);
};


USTRUCT()
struct CUSTOMIZABLEOBJECT_API FMutableParameterData
{
	GENERATED_BODY()

	FMutableParameterData() = default;
	
	FMutableParameterData(const FMutableParamUIMetadata& InParamUIMetadata, EMutableParameterType InType);

	UPROPERTY()
	FMutableParamUIMetadata ParamUIMetadata;

	/** Parameter type */
	UPROPERTY()
	EMutableParameterType Type = EMutableParameterType::None;

	/** In the case of an integer parameter, store here all options */
	UPROPERTY()
	TMap<FString, FIntegerParameterUIData> ArrayIntegerParameterOption;

	/** How are the different options selected (one, one or none, etc...) */
	UPROPERTY()
	ECustomizableObjectGroupType IntegerParameterGroupType = ECustomizableObjectGroupType::COGT_ONE_OR_NONE;
	
	friend FArchive& operator<<(FArchive& Ar, FMutableParameterData& Struct);
};


USTRUCT()
struct CUSTOMIZABLEOBJECT_API FMutableStateData
{
	GENERATED_BODY()

	UPROPERTY()
	FMutableStateUIMetadata StateUIMetadata;

	/** In this mode instances and their temp data will be reused between updates. It will be much faster but spend as much as ten times the memory.
	 * Useful for customization lockers with few characters that are going to have their parameters changed many times, not for in-game */
	UPROPERTY()
	bool bLiveUpdateMode = false;

	/** If this is enabled, texture streaming won't be used for this state, and full images will be generated when an instance is first updated. */
	UPROPERTY()
	bool bDisableTextureStreaming = false;

	UPROPERTY()
	bool bReuseInstanceTextures = false;

	UPROPERTY()
	TMap<FString, FString> ForcedParameterValues;

	friend FArchive& operator<<(FArchive& Ar, FMutableStateData& Struct);
};


#if WITH_EDITOR
namespace MutablePrivate
{
	enum class EDataType : uint32 // uint32 for padding and DDC purposes
	{
		None = 0,
		Model,
		RealTimeMorph,
		Clothing,
	};


	struct FBlock
	{
		/** Used on some data types as the index to the block stored in the CustomizableObject */
		uint32 Id;

		/** Used on some data types to group blocks. */
		uint32 SourceId;

		/** Size of the data block. */
		uint32 Size;

		uint32 Padding = 0;

		/** Offset in the full source streamed data file that is created when compiling. */
		uint64 Offset;

		friend FArchive& operator<<(FArchive& Ar, FBlock& Data)
		{
			Ar << Data.Id;
			Ar << Data.SourceId;
			Ar << Data.Size;
			Ar << Data.Offset;
			return Ar;
		};
	};
	//template<> struct TCanBulkSerialize<FBlock> { enum { Value = true }; };

	struct CUSTOMIZABLEOBJECT_API FFile
	{
		EDataType DataType = EDataType::None;

		/** Rom ResourceType. */
		uint16 ResourceType = 0;

		/** Common flags of the data stored in this file. See mu::ERomFlags. */
		uint16 Flags = 0;

		/** Id generated from a hash of the file content + offset to avoid collisions. */
		uint32 Id = 0;

		uint32 Padding = 0;

		/** List of blocks that are contained in the file, in order. */
		TArray<FBlock> Blocks;

		/** Get the total size of blocks in this file. */
		uint64 GetSize() const;

		/** Copy the requested block to the requested buffer and return its size. */
		void GetFileData(struct FMutableCachedPlatformData*, TArray64<uint8>& DataDestination, bool bDropData);

		friend FArchive& operator<<(FArchive& Ar, FFile& Data)
		{
			Ar << Data.DataType;
			Ar << Data.ResourceType;
			Ar << Data.Flags;
			Ar << Data.Id;
			Ar << Data.Blocks;
			return Ar;
		};
	};

	struct FFileCategoryID
	{
		FFileCategoryID(EDataType DataType, uint16 ResourceType, uint16 Flags);

		FFileCategoryID() = default;

		// DATATYPE
		EDataType DataType = EDataType::None;

		/** Rom ResourceType. */
		uint16 ResourceType = 0;

		/** Rom flags  */
		uint16 Flags = 0;

		friend uint32 GetTypeHash(const FFileCategoryID& Key);
		bool operator==(const FFileCategoryID& Other) const = default;
	};


	struct FFileCategory
	{
		FFileCategoryID Id;

		// Accumulated size of resources from this category
		uint64 DataSize = 0;

		// Categories within a bucket with a limited number of files will use sequential ID starting at FirstFile
		// and up to FirstFile + NumFiles.
		uint32 FirstFile = 0;
		uint32 NumFiles = 0;
	};


	/** Group bulk data by categories. */
	struct FFileBucket
	{
		// Resources belonging to these categories will be added to the bucket.
		TArray<FFileCategory> Categories;

		// Accumulated size of the resources of all categories within this bucket
		uint64 DataSize = 0;
	};

	struct FModelStreamableData
	{
		void Get(uint32 Key, TArrayView64<uint8> Destination, bool bDropData)
		{
			TArray64<uint8>* Buffer = Data.Find(Key);
			check(Buffer);
			check(Destination.Num() == Buffer->Num());
			FMemory::Memcpy(Destination.GetData(), Buffer->GetData(), Buffer->Num());

			if (bDropData)
			{
				Buffer->Empty();
			}
		}

		void Set(uint32 Key, const uint8* Source, int64 Size)
		{
			check(Source);
			check(Size);
			TArray64<uint8>& Buffer = Data.Add(Key);
			check(Buffer.Num() == 0);
			Buffer.SetNumUninitialized(Size);
			FMemory::Memcpy(Buffer.GetData(), Source, Size);
		}

		// Temp, to be replaced with disk storage
		TMap<uint32, TArray64<uint8> > Data;
	};


	struct CUSTOMIZABLEOBJECT_API FMutableCachedPlatformData
	{
		/** Serialized mu::Model */
		TArray64<uint8> ModelData;

		/** Serialized FModelResources */
		TArray64<uint8> ModelResourcesData;

		/** Streamable resources info such as files and offsets. */
		TSharedPtr<FModelStreamableBulkData> ModelStreamables;

		/** Struct containing map of RomId to RomBytes. */
		FModelStreamableData ModelStreamableData;

		/** */
		TArray64<uint8> MorphData;

		/** */
		TArray64<uint8> ClothingData;

		/** List of files to serialize. Each file has a list of binary blocks to be serialized. */
		TArray<FFile> BulkDataFiles;
	};


	/** Generate the list of BulkData files with a restriction to the number of files to generate per bucket.
	 *  Resources will be split into two buckets for non-optional and optional BulkData.
	 */
	void CUSTOMIZABLEOBJECT_API GenerateBulkDataFilesListWithFileLimit(
		TSharedPtr<const mu::Model, ESPMode::ThreadSafe> Model,
		FModelStreamableBulkData& ModelStreamableBulkData,
		uint32 NumFilesPerBucket,
		TArray<FFile>& OutBulkDataFiles);

	/** Generate the list of BulkData files with a soft restriction to the size of the files.
	 */
	void CUSTOMIZABLEOBJECT_API GenerateBulkDataFilesListWithSizeLimit(
		TSharedPtr<const mu::Model, ESPMode::ThreadSafe> Model,
		FModelStreamableBulkData& ModelStreamableBulkData,
		const ITargetPlatform* TargetPlatform,
		uint64 TargetBulkDataFileBytes,
		TArray<FFile>& OutBulkDataFiles);

	/** Compute the number of files and sizes the BulkData will be split into and update
	 * the streamable's FileIds and Offsets.
	 */
	void GenerateBulkDataFilesList(
		TSharedPtr<const mu::Model, ESPMode::ThreadSafe> Model,
		FModelStreamableBulkData& StreamableBulkData,
		bool bUseRomTypeAndFlagsToFilter,
		TFunctionRef<void(const FFileCategoryID&, const FClassifyNode&, TArray<FFile>&)> CreateFileList,
		TArray<FFile>& OutBulkDataFiles);

	void CUSTOMIZABLEOBJECT_API SerializeBulkDataFiles(
		FMutableCachedPlatformData& CachedPlatformData,
		TArray<FFile>& BulkDataFiles,
		TFunctionRef<void(FFile&, TArray64<uint8>&, uint32 FileIndex)> WriteFile,
		bool bDropData);

	UE::DerivedData::FValueId CUSTOMIZABLEOBJECT_API GetDerivedDataModelId();
	UE::DerivedData::FValueId CUSTOMIZABLEOBJECT_API GetDerivedDataModelResourcesId();
	UE::DerivedData::FValueId CUSTOMIZABLEOBJECT_API GetDerivedDataModelStreamableBulkDataId();
	UE::DerivedData::FValueId CUSTOMIZABLEOBJECT_API GetDerivedDataBulkDataFilesId();
}
#endif


struct CUSTOMIZABLEOBJECT_API FModelStreamableBulkData
{
	/** Map of Hash to Streaming blocks, used to stream a block of data representing a resource from the BulkData */
	TMap<uint32, FMutableStreamableBlock> ModelStreamables;

	TMap<uint32, FClothingStreamable> ClothingStreamables;

	TMap<uint32, FRealTimeMorphStreamable> RealTimeMorphStreamables;

	TArray<FByteBulkData> StreamableBulkData;

	void Serialize(FArchive& Ar, UObject* Owner, bool bCooked);

#if WITH_EDITORONLY_DATA
	friend FArchive& operator<<(FArchive& Ar, FModelStreamableBulkData& Data)
	{
		Ar << Data.ModelStreamables;
		Ar << Data.ClothingStreamables;
		Ar << Data.RealTimeMorphStreamables;

		// Don't serialize FByteBulkData manually, the data will be skipped.

		return Ar;
	}
#endif
};

/** Interface class to allow custom serialization of FModelStreamableBulkData and its FBulkData. */
UCLASS()
class UModelStreamableData : public UObject
{
	GENERATED_BODY()

	UModelStreamableData();

public:
	virtual void Serialize(FArchive& Ar) override;

	TSharedPtr<FModelStreamableBulkData> StreamingData;
};

USTRUCT()
struct FMutableParamNameSet
{
	GENERATED_BODY()

	TSet<FString> ParamNames;

	friend FArchive& operator<<(FArchive& Ar, FMutableParamNameSet& MutableParamNameSet);
};


/** Struct containing all UE resources derived from a CO compilation. These resources will be embedded in the CO at cook time but not in the editor.
  * Editor compilations will serialize this struct to disk using the Serialize methods. Ensure new fields are serialized, too.
  * Variables and settings that should not change until the CO is re-compiled should be stored here. */
USTRUCT()
struct FModelResources
{
	GENERATED_BODY()

	/** 
	 * All the SkeletalMeshes generated for this CustomizableObject instances will use the Reference Skeletal Mesh
	 * properties for everything that Mutable doesn't create or modify. This struct stores the information used from
	 * the Reference Skeletal Meshes to avoid having them loaded at all times. This includes data like LOD distances,
	 * LOD render data settings, Mesh sockets, Bounding volumes, etc.
	 * 
	 * Index with Component index
	 */
	UPROPERTY()
	TArray<FMutableRefSkeletalMeshData> ReferenceSkeletalMeshesData;

	/** Skeletons used by the compiled mu::Model. */
	UPROPERTY()
	TArray<TSoftObjectPtr<USkeleton>> Skeletons;

	/** Materials used by the compiled mu::Model */
	UPROPERTY()
	TArray<TSoftObjectPtr<UMaterialInterface>> Materials;

	/** PassThrough textures used by the mu::Model. */
	UPROPERTY()
	TArray<TSoftObjectPtr<UTexture>> PassThroughTextures;

	/** PassThrough meshes used by the mu::Model. */
	UPROPERTY()
	TArray<TSoftObjectPtr<USkeletalMesh>> PassThroughMeshes;

#if WITH_EDITORONLY_DATA
	/** Runtime referenced textures used by the mu::Model. */
	UPROPERTY()
	TArray<TSoftObjectPtr<const UTexture>> RuntimeReferencedTextures;
#endif
	
	/** Physics assets gathered from the SkeletalMeshes, to be used in mesh generation in-game */
	UPROPERTY()
	TArray<TSoftObjectPtr<UPhysicsAsset>> PhysicsAssets;

	/** UAnimBlueprint assets gathered from the SkeletalMesh, to be used in mesh generation in-game */
	UPROPERTY()
	TArray<TSoftClassPtr<UAnimInstance>> AnimBPs;

	/** */
	UPROPERTY()
	TArray<FAnimBpOverridePhysicsAssetsInfo> AnimBpOverridePhysiscAssetsInfo;

	/** Material slot names for the materials referenced by the surfaces. */
	UPROPERTY()
	TArray<FName> MaterialSlotNames;

	UPROPERTY()
	TMap<FString, uint32> BoneNamesMap;

	/** Mesh sockets provided by the part skeletal meshes, to be merged in the generated meshes */
	UPROPERTY()
	TArray<FMutableRefSocket> SocketArray;

	UPROPERTY()
	TArray<FMutableSkinWeightProfileInfo> SkinWeightProfilesInfo;

	UPROPERTY()
	TArray<FMutableModelImageProperties> ImageProperties;

	UPROPERTY()
	TMap<uint32, FMutableMeshMetadata> MeshMetadata;

	UPROPERTY()
	TMap<uint32, FMutableSurfaceMetadata> SurfaceMetadata;

	/** Parameter UI metadata information for all the dependencies of this Customizable Object. */
	UPROPERTY()
	TMap<FString, FMutableParameterData> ParameterUIDataMap;

	/** State UI metadata information for all the dependencies of this Customizable Object */
	UPROPERTY()
	TMap<FString, FMutableStateData> StateUIDataMap;

#if WITH_EDITORONLY_DATA
	/** DataTable used by an int parameter and its value. */
	TMap<TTuple<FString, FString>, TSet<TSoftObjectPtr<UDataTable>>> IntParameterOptionDataTable;
#endif
	
	UPROPERTY()
	TArray<FCustomizableObjectClothConfigData> ClothSharedConfigsData;	

	UPROPERTY()
	TArray<FCustomizableObjectClothingAssetData> ClothingAssetsData;


	/** Currently not used, this option should be selectable from editor maybe as a compilation flag */
	UPROPERTY()
	bool bAllowClothingPhysicsEditsPropagation = true;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FMorphTargetVertexData> EditorOnlyMorphTargetReconstructionData;

	UPROPERTY()
	TArray<FCustomizableObjectMeshToMeshVertData> EditorOnlyClothingMeshToMeshVertData;

	// Stores what param names use a certain table as a table can be used from multiple table nodes, useful for partial compilations to restrict params
	UPROPERTY()
	TMap<FString, FMutableParamNameSet> TableToParamNames;

	/** Map to identify what CustomizableObject owns a parameter. Used to display a tooltip when hovering a parameter
	 * in the Prev. instance panel */
	TMap<FString, FString> CustomizableObjectPathMap;

	TMap<FString, FCustomizableObjectIdPair> GroupNodeMap;

	/** If the object is compiled with maximum optimizations. */
	bool bIsCompiledWithOptimization = true;

	/** This is a non-user-controlled flag to disable streaming (set at object compilation time, depending on optimization). */
	bool bIsTextureStreamingDisabled = false;

	/** List of external packages that if changed, a compilation is required.
	  * Key is the package name. Value is the the UPackage::Guid, which is regenerated each time the packages is saved.
	  *
	  * Updated each time the CO is compiled and saved in the Derived Data. */
	TMap<FName, FGuid> ParticipatingObjects;

	// Used to know if roms and other resources must be streamed from the DDC.
	bool bIsStoredInDDC = false;
	UE::DerivedData::FCacheKey DDCKey = UE::DerivedData::FCacheKey::Empty;
	UE::DerivedData::ECachePolicy DDCDefaultPolicy = UE::DerivedData::ECachePolicy::Default;
#endif

	// Constant Resources streamed in on demand when generating meshes
	UPROPERTY()
	TArray<FCustomizableObjectStreamedResourceData> StreamedResourceData;

	/** Max number of LODs in the compiled Model. */
	UPROPERTY()
	uint8 NumLODs = 0;

	/** Max number of LODs to stream. Mutable will always generate at least on LOD. */
	UPROPERTY()
	uint8 NumLODsToStream = 0;

	/** First LOD available, some platforms may remove lower LODs when cooking, this MinLOD represents the first LOD we can generate */
	UPROPERTY()
	uint8 FirstLODAvailable = 0;

	/** Name of all possible components. Index is the ObjectComponentIndex. */
	UPROPERTY()
	TArray<FName> ComponentNames;

	UPROPERTY()
	FString ReleaseVersion;
	
#if WITH_EDITORONLY_DATA
	void CUSTOMIZABLEOBJECT_API Serialize(FObjectAndNameAsStringProxyArchive& Ar, bool bIsCooking);
	bool CUSTOMIZABLEOBJECT_API Unserialize(FObjectAndNameAsStringProxyArchive& Ar, UCustomizableObject& Outer, const ITargetPlatform* InTargetPlatform, bool bIsCooking);
#endif
};


UCLASS(config = Engine)
class CUSTOMIZABLEOBJECT_API UCustomizableObjectBulk : public UObject
{
public:
	GENERATED_BODY()

	//~ Begin UObject Interface
	virtual void PostLoad() override;
	//~ End UObject Interface

	/**  */
	const FString& GetBulkFilePrefix() const { return BulkFilePrefix; }

	TUniquePtr<IAsyncReadFileHandle> OpenFileAsyncRead(uint32 FileId, uint32 Flags) const;

#if WITH_EDITOR

	//~ Begin UObject Interface
	virtual void CookAdditionalFilesOverride(const TCHAR*, const ITargetPlatform*, TFunctionRef<void(const TCHAR*, void*, int64)>) override;
	//~ End UObject Interface
#endif

#if WITH_EDITOR
private:
#endif

	/** Prefix to locate bulkfiles for loading, using the file ids in each FMutableStreamableBlock. */
	FString BulkFilePrefix;
};


USTRUCT()
struct CUSTOMIZABLEOBJECT_API FMutableMeshComponentData
{
	GENERATED_USTRUCT_BODY()

	/** Name to identify this component. */
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	FName Name;

	/** All the SkeletalMeshes generated for this CustomizableObject instances will use the Reference Skeletal Mesh
	* properties for everything that Mutable doesn't create or modify. This includes data like LOD distances, Physics
	* properties, Bounding Volumes, Skeleton, etc.
	*
	* While a CustomizableObject instance is being created for the first time, and in some situation with lots of
	* objects this may require some seconds, the Reference Skeletal Mesh is used for the actor. This works as a better
	* solution than the alternative of not showing anything, although this can be disabled with the function
	* "SetReplaceDiscardedWithReferenceMeshEnabled" (See the c++ section). */
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	TObjectPtr<USkeletalMesh> ReferenceSkeletalMesh;
};


UCLASS()
class CUSTOMIZABLEOBJECT_API UCustomizableObjectPrivate : public UObject
{
	GENERATED_BODY()

	TSharedPtr<mu::Model, ESPMode::ThreadSafe> MutableModel;

	/** Stores streamable data info to be used by MutableModel In-Game. Cooked resources. */
	UPROPERTY()
	TObjectPtr<UModelStreamableData> ModelStreamableData;

	/** Stores resources to be used by MutableModel In-Game. Cooked resources. */
	UPROPERTY()
	FModelResources ModelResources;

#if WITH_EDITORONLY_DATA
	/** 
	 * Stores resources to be used by MutableModel in the Editor. Editor resources.
	 * Editor-Only to avoid packaging assets referenced by editor compilations. 
	 */
	UPROPERTY(Transient)
	FModelResources ModelResourcesEditor;

	/**
	 * Stores streamable data info to be used by MutableModel in the Editor. Editor streaming.
	 */
	TSharedPtr<FModelStreamableBulkData> ModelStreamableDataEditor;
#endif

public:
	/** Must be called after unlocking the CustomizableObject. */
	void SetModel(const TSharedPtr<mu::Model, ESPMode::ThreadSafe>& Model, const FGuid Identifier);
	const TSharedPtr<mu::Model, ESPMode::ThreadSafe>& GetModel();
	const TSharedPtr<const mu::Model, ESPMode::ThreadSafe> GetModel() const;

	const FModelResources& GetModelResources() const;

#if WITH_EDITORONLY_DATA
	FModelResources& GetModelResources(bool bIsCooking);
	const FModelResources& GetModelResources(bool bIsCooking) const;

	void SetModelStreamableBulkData(const TSharedPtr<FModelStreamableBulkData>& StreamableData, bool bIsCooking);
#endif

	USkeletalMesh* GetRefSkeletalMesh(const FName& ComponentName) const;
	
	TSharedPtr<FModelStreamableBulkData> GetModelStreamableBulkData(bool bIsCooking = false);

	// See UCustomizableObjectSystem::LockObject()
	bool IsLocked() const;

	/** Modify the provided mutable parameters so that the forced values for the given customizable object state are applied. */
	void ApplyStateForcedValuesToParameters(int32 State, mu::Parameters* Parameters);

	int32 FindParameter(const FString& Name) const;
	int32 FindParameterTyped(const FString& Name, EMutableParameterType Type) const;

	EMutableParameterType GetParameterType(int32 ParamIndex) const;

	int32 FindIntParameterValue(int32 ParamIndex, const FString& Value) const;

	FString GetStateName(int32 StateIndex) const;

#if WITH_EDITORONLY_DATA
	void PostCompile();
#endif

	/** Returns a pointer to the BulkData subobject, only valid in packaged builds. */
	const UCustomizableObjectBulk* GetStreamableBulkData() const;

	UCustomizableObject* GetPublic() const;

#if WITH_EDITOR
	/** Compose file name. */
	FString GetCompiledDataFileName(bool bIsModel, const ITargetPlatform* InTargetPlatform = nullptr, bool bIsDiskStreamer = false);

	/** DDC helpers. BuildDerivedDataKey is expensive, try to cache it as much as possible. */
	TArray<uint8> BuildDerivedDataKey(FCompilationOptions Options);
	UE::DerivedData::FCacheKey GetDerivedDataCacheKeyForOptions(FCompilationOptions InOptions);

	/** Attempts to load the compiled data from DDC. Builds key if not supplied. */
	void LoadCompiledDataFromDDC(FCompilationOptions Options, UE::DerivedData::ECachePolicy DefaultPolicy, UE::DerivedData::FCacheKey* DDCKey);

#endif
	
	/** Rebuild ParameterProperties from the current compiled model. */
	void UpdateParameterPropertiesFromModel(const TSharedPtr<mu::Model>& Model);

	void AddUncompiledCOWarning(const FString& AdditionalLoggingInfo);

#if WITH_EDITOR
	// Create new GUID for this CO
	void UpdateVersionId();
	
	FGuid GetVersionId() const;

	void SaveEmbeddedData(FArchive& Ar);

	// Compile the object for a specific platform - Compile for Cook Customizable Object
	void CompileForTargetPlatform(UCustomizableObject& CustomizableObject, const ITargetPlatform& TargetPlatform);
	
	// Add a profile that stores the values of the parameters used by the CustomInstance.
	FReply AddNewParameterProfile(FString Name, class UCustomizableObjectInstance& CustomInstance);

	// Compose folder name where the data is stored
	static FString GetCompiledDataFolderPath();

	/** Generic Load methods to read compiled data */
	bool LoadModelResources(FArchive& Ar, const ITargetPlatform* InTargetPlatform, bool bSkipEditorOnlyData = false);
	void LoadModelStreamableBulk(FArchive& Ar, bool bIsCooking);
	void LoadModel(FArchive& Ar);

	/** Load compiled data for the running platform from disk, this is used to load Editor Compilations. */
	void LoadCompiledDataFromDisk();
	
	/** Loads data previously compiled in BeginCacheForCookedPlatformData onto the UProperties in *this,
	  * in preparation for saving the cooked package for *this or for a CustomizableObjectInstance using *this.
      * Returns whether the data was successfully loaded. */
	bool TryLoadCompiledCookDataForPlatform(const ITargetPlatform* TargetPlatform);
#endif

	// Data that may be stored in the asset itself, only in packaged builds.
	void LoadEmbeddedData(FArchive& Ar);
	
	/** Compute bIsChildObject if currently possible to do so. Return whether it was computed. */
	bool TryUpdateIsChildObject();

	void SetIsChildObject(bool bIsChildObject);

	/** Return the names used by mutable to identify which mu::Image should be considered of LowPriority. */
	void GetLowPriorityTextureNames(TArray<FString>& OutTextureNames);

	/** Return the MinLOD index to generate based on the active LODSettings (PerPlatformMinLOD or PerQualityLevelMinLOD) */
	int32 GetMinLODIndex() const;
	
#if WITH_EDITOR
	/** See ICustomizableObjectEditorModule::IsCompilationOutOfDate. */
	bool IsCompilationOutOfDate(bool bSkipIndirectReferences, TArray<FName>& OutOfDatePackages, TArray<FName>& AddedPackages, TArray<FName>& RemovedPackages, bool& bReleaseVersionDiff) const;
#endif

	TArray<FString>& GetCustomizableObjectClassTags();
	
	TArray<FString>& GetPopulationClassTags();

    TMap<FString, FParameterTags>& GetCustomizableObjectParametersTags();

#if WITH_EDITORONLY_DATA
	TArray<FProfileParameterDat>& GetInstancePropertiesProfiles();
#endif
	
	TArray<FCustomizableObjectResourceData>& GetAlwaysLoadedExtensionData();
	const TArray<FCustomizableObjectResourceData>& GetAlwaysLoadedExtensionData() const;

	TArray<FCustomizableObjectStreamedResourceData>& GetStreamedExtensionData();
	const TArray<FCustomizableObjectStreamedResourceData>& GetStreamedExtensionData() const;

	const FCustomizableObjectResourceData* LoadStreamedResource(int32 ResourceIndex);
	void UnloadStreamedResource(int32 ResourceIndex);

#if WITH_EDITORONLY_DATA
	TObjectPtr<UEdGraph>& GetSource() const;

	FCompilationOptions GetCompileOptions() const;
#endif

	void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion);
	
	/** Cache of generated SkeletalMeshes */
	FMeshCache MeshCache;

	/** Cache of merged Skeletons */
	FSkeletonCache SkeletonCache;

	// See UCustomizableObjectSystem::LockObject. Must only be modified from the game thread
	bool bLocked = false;

#if WITH_EDITORONLY_DATA

	UPROPERTY()
	TArray<FMutableMeshComponentData> MutableMeshComponents_DEPRECATED;

	/** Unique Identifier - Deterministic. Used to locate Model and Streamable data on disk. Should not be modified. */
	FGuid Identifier;

	/** Cook requests. */
	TArray<TSharedRef<FCompilationRequest>> CompileRequests;
	
	ECompilationStatePrivate CompilationState = ECompilationStatePrivate::None;
	ECompilationResultPrivate CompilationResult = ECompilationResultPrivate::Unknown;
	
	FPostCompileDelegate PostCompileDelegate;

	/** Map of PlatformName to CachedPlatformData. Only valid while cooking. */
	TMap<FString, MutablePrivate::FMutableCachedPlatformData> CachedPlatformsData;

#endif

	FCustomizableObjectStatus Status;

	// This is information about the parameters in the model that is generated at model compile time.
	UPROPERTY(Transient)
	TArray<FMutableModelParameterProperties> ParameterProperties;

	/** Reference to all UObject used in game. Only updated during the compilation if the user explicitly wants to save all references. */
	UPROPERTY()
	FModelResources References;
	
	// Map of name to index of ParameterProperties.
	// use this to lookup fast by Name
	TMap<FString, FMutableParameterIndex> ParameterPropertiesLookupTable;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	ECustomizableObjectTextureCompression TextureCompression = ECustomizableObjectTextureCompression::Fast;

	/** From 0 to UE_MUTABLE_MAX_OPTIMIZATION */
	UPROPERTY()
	int32 OptimizationLevel = UE_MUTABLE_MAX_OPTIMIZATION;

	/** Use the disk to store intermediate compilation data. This slows down the object compilation
	 * but it may be necessary for huge objects. */
	UPROPERTY()
	bool bUseDiskCompilation = false;

	/** High limit of the size in bytes of the packaged data when cooking this object.
	 * This limit is before any pak or filesystem compression. This limit will be broken if a single piece of data is bigger because data is not fragmented for packaging purposes.	*/
	UPROPERTY()
	uint64 PackagedDataBytesLimit = 256 * 1024 * 1024;
	
	/** High (inclusive) limit of the size in bytes of a data block to be included into the compiled object directly instead of stored in a streamable file. */
	UPROPERTY()
	uint64 EmbeddedDataBytesLimit = 1024;

	UPROPERTY()
	int32 ImageTiling = 0;
#endif
	
	// This is a manual version number for the binary blobs in this asset.
	// Increasing it invalidates all the previously compiled models.
	enum ECustomizableObjectVersions
	{
		FirstEnumeratedVersion = 450,

		DeterminisiticMeshVertexIds,

		NumRuntimeReferencedTextures,
		
		DeterminisiticLayoutBlockIds,

		BackoutDeterminisiticLayoutBlockIds,

		FixWrappingProjectorLayoutBlockId,

		MeshReferenceSupport,

		ImproveMemoryUsageForStreamableBlocks,

		FixClipMeshWithMeshCrash,

		SkeletalMeshLODSettingsSupport,

		RemoveCustomCurve,

		AddEditorGamePlayTags,

		AddedParameterThumbnailsToEditor,

		ComponentsLODsRedesign,

		ComponentsLODsRedesign2,

		LayoutToPOD,

		AddedRomFlags,

		LayoutNodeCleanup,

		AddSurfaceAndMeshMetadata,

		TablesPropertyNameBug,

		DataTablesParamTrackingForCompileOnlySelected,

		CompilationOptimizationsMeshFormat,

		ModelStreamableBulkData,

		LayoutBlocksAsInt32,
		
		IntParameterOptionDataTable,

		RemoveLODCountLimit,

		IntParameterOptionDataTablePartialBackout,

		IntParameterOptionDataTablePartialRestore,

		CorrectlySerializeTableToParamNames,
		
		AddMaterialSlotNameIndexToSurfaceMetadata,

		NodeComponentMesh,
		
		MoveEditNodesToModifiers,

		DerivedDataCache,

		ComponentsArray,

		FixComponentNames,

		AddedFaceCullStrategyToSomeOperations,

		DDCParticipatingObjects,

		GroupRomsBySource,
		
		RemovedGroupRomsBySource,

		ReGroupRomsBySource,

		UIMetadataGameplayTags,

		TransformInMeshModifier,
		
		SurfaceMetadataSlotNameIndexToName,

		BulkDataFilesNumFilesLimit,

		RemoveModifiersHack,

		SurfaceMetadataSerialized,

		// -----<new versions can be added above this line>--------
		LastCustomizableObjectVersion
	};
	
	static constexpr int32 CurrentSupportedVersion = ECustomizableObjectVersions::LastCustomizableObjectVersion;
	
	static constexpr int32 DerivedDataVersion = 1;
};

