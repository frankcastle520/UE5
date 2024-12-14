// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/Skeleton.h"
#include "Animation/AnimInstance.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectIdentifier.h"
#include "MuCO/CustomizableObjectCompilerTypes.h"
#include "MuCOE/CustomizableObjectEditorLogger.h"
#include "MuCOE/ExtensionDataCompilerInterface.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMaterialBase.h"
#include "MuCOE/Nodes/CustomizableObjectNodeObject.h"
#include "MuT/NodeComponentNew.h"
#include "MuT/NodeImageConstant.h"
#include "MuT/NodeMeshConstant.h"
#include "MuT/NodeMeshApplyPose.h"
#include "MuT/NodeModifierMeshClipWithMesh.h"
#include "MuT/NodeObject.h"
#include "MuT/NodeProjector.h"
#include "MuT/NodeScalarEnumParameter.h"
#include "MuT/NodeScalarParameter.h"
#include "MuT/NodeSurfaceNew.h"
#include "MuT/Table.h"
#include "MuR/MutableMemory.h"
#include "MuR/Skeleton.h"
#include "MuR/Mesh.h"
#include "MuR/Ptr.h"
#include "UObject/Package.h"

class UCustomizableObjectNodeGroupProjectorParameter;
class UCustomizableObjectNodeMaterialBase;
class UCustomizableObjectNodeModifierClipWithMesh;
class UCustomizableObjectNodeTable;
struct FCustomizableObjectClothingAssetData;

class FCustomizableObjectCompiler;
class UTextureLODSettings;
class UAnimInstance;
class UCompositeDataTable;
class UCustomizableObjectNodeMaterial;
class UCustomizableObjectNodeMeshMorph;
class UCustomizableObjectNodeObjectGroup;
class UEdGraphNode;
class UMaterialInterface;
class UObject;
class UPhysicsAsset;
class UTexture2D;
struct FAnimBpOverridePhysicsAssetsInfo;
struct FMutableGraphGenerationContext;
struct FMutableParameterData;
struct FMutableRefSkeletalMeshData;
struct FMutableRefSocket;
struct FMutableSkinWeightProfileInfo;
struct FMorphTargetVertexData;
enum class EPinMode;

struct FGeneratedImageProperties
{
	/** Name in the Material. */
	FString TextureParameterName;

	/** Name in the mu::Surface. */
	int32 ImagePropertiesIndex = INDEX_NONE;

	TEnumAsByte<TextureCompressionSettings> CompressionSettings = TC_Default;

	TEnumAsByte<TextureFilter> Filter = TF_Bilinear;

	uint32 SRGB = 0;

	uint32 bFlipGreenChannel = 0;

	int32 LODBias = 0;

	TEnumAsByte<TextureMipGenSettings> MipGenSettings = TMGS_SimpleAverage;

	int32 MaxTextureSize = 0;

	TEnumAsByte<TextureGroup> LODGroup = TEnumAsByte<TextureGroup>(TMGS_FromTextureGroup);

	TEnumAsByte<TextureAddress> AddressX = TA_Clamp;
	TEnumAsByte<TextureAddress> AddressY = TA_Clamp;

	bool bIsPassThrough = false;

	// ReferenceTexture source size.
	int32 TextureSize = 0;
};


// Flags that can influence the mesh conversion
enum class EMutableMeshConversionFlags : uint32
{
	// 
	None = 0,
	// Ignore the skeleton and skinning
	IgnoreSkinning = 1 << 0,

	// Ignore Physics assets
	IgnorePhysics = 1 << 1,

	// Prevent this mesh generation from adding per mesh metadata. 
	DoNotCreateMeshMetadata = 1 << 2
};

ENUM_CLASS_FLAGS(EMutableMeshConversionFlags)

struct FLayoutGenerationFlags
{
	bool operator==(const FLayoutGenerationFlags& Other) const = default;

	// Texture pin mode per UV Channel
	TArray<EPinMode> TexturePinModes;
};

/** 
	Struct to store the necessary data to generate the morphs of a skeletal mesh 
	This struct allows the stack morph nodes to use the same functions as the mesh morph nodes
*/
struct FMorphNodeData
{
	// Pointer to the node that owns this morph data
	UCustomizableObjectNode* OwningNode;

	// Name of the morph that will be applied
	FString MorphTargetName;

	// Pin to the node that generates the factor of the morph
	const UEdGraphPin* FactorPin;

	// Pin of the mesh where the morphs will ble apllied
	const UEdGraphPin* MeshPin;

	bool operator==(const FMorphNodeData& Other) const
	{
		return OwningNode == Other.OwningNode && MorphTargetName == Other.MorphTargetName
			&& FactorPin == Other.FactorPin && MeshPin == Other.MeshPin;
	}
};


// Key for the data stored for each processed unreal graph node.
class FGeneratedKey
{
	friend uint32 GetTypeHash(const FGeneratedKey& Key);

public:
	FGeneratedKey(void* InFunctionAddress, const UEdGraphPin& InPin, const UCustomizableObjectNode& Node, FMutableGraphGenerationContext& GenerationContext, const bool UseMesh = false, bool bOnlyConnectedLOD = false);
	
	bool operator==(const FGeneratedKey& Other) const = default;

	/** Used to differentiate pins being cached from different functions (e.g. a PC_Color pin cached from GenerateMutableSourceImage and GenerateMutableSourceColor). */
	void* FunctionAddress;
	
	const UEdGraphPin* Pin;
	
	int32 LOD;

	/** Flag used to generate this mesh. Bit mask of EMutableMeshConversionFlags */
	EMutableMeshConversionFlags Flags = EMutableMeshConversionFlags::None;

	/** Active morphs at the time of mesh generation. */
	TArray<FMorphNodeData> MeshMorphStack;

	/** UV Layout modes */
	FLayoutGenerationFlags LayoutFlags;
	
	FName CurrentMeshComponent;
	
	/** When caching a generated mesh, true if we force to generate the connected LOD when using Automatic LODs From Mesh. */
	bool bOnlyConnectedLOD = false;
};


uint32 GetTypeHash(const FGeneratedKey& Key);


struct FGeneratedImageKey
{
	FGeneratedImageKey(const UEdGraphPin* InPin)
	{
		Pin = InPin;
	}

	bool operator==(const FGeneratedImageKey& Other) const
	{
		return Pin == Other.Pin;
	}

	const UEdGraphPin* Pin;
};


struct FGeneratedImagePropertiesKey
{
	FGeneratedImagePropertiesKey(const UCustomizableObjectNodeMaterialBase* InMaterial, uint32 InImageIndex)
	{
		MaterialReferenceId = (PTRINT)InMaterial;
		ImageIndex = InImageIndex;
	}

	bool operator==(const FGeneratedImagePropertiesKey& Other) const
	{
		return MaterialReferenceId == Other.MaterialReferenceId && ImageIndex == Other.ImageIndex;
	}


	PTRINT MaterialReferenceId = 0;
	uint32 ImageIndex = 0;
};

// Structure storing results to propagate up when generating mutable mesh node expressions.
struct FMutableGraphMeshGenerationData
{
	// Did we find any mesh with vertex colours in the expression?
	bool bHasVertexColors = false;
	bool bHasRealTimeMorphs = false;
	bool bHasClothing = false;

	// Maximum number of texture channels found in the expression.
	int NumTexCoordChannels = 0;

	// Maximum number of bones per vertex found in the expression.
	int MaxNumBonesPerVertex = 0;

	// Maximum size of the vertex bone index type in the expression.
	int MaxBoneIndexTypeSizeBytes = 0;

	int32 MaxNumTriangles = 0;
	int32 MinNumTriangles = TNumericLimits<int32>::Max();

	TArray<int32> SkinWeightProfilesSemanticIndices;

	// Combine another generated data looking for the most general case.
	void Combine(const FMutableGraphMeshGenerationData& Other)
	{
		bHasVertexColors = bHasVertexColors || Other.bHasVertexColors;
		bHasRealTimeMorphs = bHasRealTimeMorphs || Other.bHasRealTimeMorphs;
		bHasClothing = bHasClothing || Other.bHasClothing;
		NumTexCoordChannels = FMath::Max(Other.NumTexCoordChannels, NumTexCoordChannels);
		MaxNumBonesPerVertex = FMath::Max(Other.MaxNumBonesPerVertex, MaxNumBonesPerVertex);
		MaxBoneIndexTypeSizeBytes = FMath::Max(Other.MaxBoneIndexTypeSizeBytes, MaxBoneIndexTypeSizeBytes);
		MaxNumTriangles = FMath::Max(Other.MaxNumTriangles, MaxNumTriangles);
		MinNumTriangles = FMath::Min(Other.MinNumTriangles, MinNumTriangles);

		for (int32 SemanticIndex : Other.SkinWeightProfilesSemanticIndices)
		{
			SkinWeightProfilesSemanticIndices.AddUnique(SemanticIndex);
		}
	}
};


// Data stored for each processed unreal graph node, stored in the cache.
struct FGeneratedData
{
	FGeneratedData(const UEdGraphNode* InSource, mu::NodePtr InNode,
		const FMutableGraphMeshGenerationData* InMeshData = nullptr)
	{
		Source = InSource;
		Node = InNode;
		if (InMeshData)
		{
			meshData = *InMeshData;
		}
	}

	const UEdGraphNode* Source;
	mu::NodePtr Node;

	// Used for mesh nodes only
	FMutableGraphMeshGenerationData meshData;
};


inline uint32 GetTypeHash(const FGeneratedImageKey& Key)
{
	uint32 GuidHash = GetTypeHash(Key.Pin->PinId);

	return GuidHash;
}


inline uint32 GetTypeHash(const FGeneratedImagePropertiesKey& Key)
{
	uint32 GuidHash = HashCombineFast(GetTypeHash(Key.MaterialReferenceId), Key.ImageIndex);

	return GuidHash;
}

struct FPoseBoneData
{
	TArray<FName> ArrayBoneName;
	TArray<FTransform> ArrayTransform;
};

struct FRealTimeMorphMeshData
{
	TArray<FName> NameResolutionMap;
	TArray<FMorphTargetVertexData> Data;

	/* Used to group data when generating bulk data files.
	 * This property should not be taken into consideration when comparing structs.
	 */
	uint32 SourceId = 0;
};

struct FClothingMeshData
{
	int32 ClothingAssetIndex = INDEX_NONE;
	int32 ClothingAssetLOD = INDEX_NONE;
	int32 PhysicsAssetIndex = INDEX_NONE;
	TArray<FCustomizableObjectMeshToMeshVertData> Data;

	/* Used to group data when generating bulk data files. 
	 * This property should not be taken into consideration when comparing structs.
	 */
	uint32 SourceId = 0;
};

struct FGroupProjectorTempData
{
	class UCustomizableObjectNodeGroupProjectorParameter* CustomizableObjectNodeGroupProjectorParameter;
	mu::NodeProjectorParameterPtr NodeProjectorParameterPtr;
	mu::NodeImagePtr NodeImagePtr;
	mu::NodeRangePtr NodeRange;
	mu::NodeScalarParameterPtr NodeOpacityParameter;

	mu::NodeScalarEnumParameterPtr PoseOptionsParameter;
	TArray<FPoseBoneData> PoseBoneDataArray;

	bool bAlternateResStateNameWarningDisplayed = false; // Used to display this warning only once

	int32 TextureSize = 512;
};


struct FGroupNodeIdsTempData
{
	FGroupNodeIdsTempData(FGuid OldGuid, FGuid NewGuid = FGuid()) :
		OldGroupNodeId(OldGuid),
		NewGroupNodeId(NewGuid)
	{

	}

	FGuid OldGroupNodeId;
	FGuid NewGroupNodeId;

	bool operator==(const FGroupNodeIdsTempData& Other) const
	{
		return OldGroupNodeId == Other.OldGroupNodeId;
	}
};

struct FGroupProjectorImageInfo
{
	mu::Ptr<mu::NodeImage> ImageNode;
	mu::Ptr<mu::NodeImage> ImageResizeNode;
	mu::Ptr<mu::NodeSurfaceNew> SurfNode;
	UCustomizableObjectNodeMaterialBase* TypedNodeMat;
	FString TextureName;
	FString RealTextureName;
	FString AlternateResStateName;
	float AlternateProjectionResolutionFactor;
	bool bIsAlternateResolutionResized = false;
	int32 UVLayout = 0;

	FGroupProjectorImageInfo(mu::NodeImagePtr InImageNode, const FString& InTextureName, const FString& InRealTextureName, UCustomizableObjectNodeMaterialBase* InTypedNodeMat,
		float InAlternateProjectionResolutionFactor, const FString& InAlternateResStateName, mu::Ptr<mu::NodeSurfaceNew> InSurfNode, int32 InUVLayout)
		: TypedNodeMat(InTypedNodeMat), TextureName(InTextureName), RealTextureName(InRealTextureName),
		AlternateResStateName(InAlternateResStateName), AlternateProjectionResolutionFactor(InAlternateProjectionResolutionFactor), 
		UVLayout(InUVLayout)
	{
		ImageNode = InImageNode;
		SurfNode = InSurfNode;
	}

	static FString GenerateId(const UCustomizableObjectNode* TypedNodeMat, int32 ImageIndex)
	{
		return TypedNodeMat->GetOutermost()->GetPathName() + TypedNodeMat->NodeGuid.ToString() + FString("-") + FString::FromInt(ImageIndex);
	}
};


/** Struct that defines a mesh (Mesh + LOD + MaterialIndex) */
struct FMeshData
{
	/** Mesh which it may contain multiple LODs and Materials. */
	const UObject* Mesh;

	/** Specific LOD of the mesh. */
	int LOD;

	/** Specific index of the mesh material (MaterialIndex is an alias of Section). */
	int MaterialIndex;

	/** Node where the mesh is defined. Not a UCustomizableObjectNodeMesh due to Table nodes */
	const UCustomizableObjectNode* Node;

	bool operator==(const FMeshData& Other) const
	{
		return Mesh == Other.Mesh &&
			LOD == Other.LOD &&
			MaterialIndex == Other.MaterialIndex &&
			Node == Other.Node;
	}
};

inline uint32 GetTypeHash(const FMeshData& Key)
{
	uint32 MeshHash = GetTypeHash(Key.Mesh);
	uint32 NodeHash = GetTypeHash(Key.Node->GetUniqueID());
	
	return HashCombine(HashCombine(HashCombine(MeshHash, Key.LOD), Key.MaterialIndex), NodeHash);
}


/** Struct used to store info specific to each component during compilation */
struct FMutableComponentInfo
{
	FMutableComponentInfo(FName InComponentName, USkeletalMesh* InRefSkeletalMesh);

	void AccumulateBonesToRemovePerLOD(const TArray<FLODReductionSettings>& LODReductionSettings, int32 NumLODs);

	FName ComponentName;

	// Each component must have a reference SkeletalMesh with a valid Skeleton
	USkeletalMesh* RefSkeletalMesh = nullptr;
	USkeleton* RefSkeleton = nullptr;
	
	UCustomizableObjectNodeComponentMesh* NodeComponentMesh = nullptr;
	
	// Map to check skeleton compatibility
	TMap<const USkeleton*, bool> SkeletonCompatibility;
	
	// Hierarchy hash from parent-bone to root bone, used to check if additional skeletons are compatible with
	// the RefSkeleton
	TMap<FName, uint32> BoneNamesToPathHash;

	// Bones to remove on each LOD, include bones on previous LODs. FName (BoneToRemove) - bool (bOnlyRemoveChildren)
	TArray<TMap<FName, bool>> BonesToRemovePerLOD;
	
	UCustomizableObjectNodeComponentMesh* Node = nullptr;
};


/** Struct of data which is behind a given output pin. 
 *
 * Each newly added field has should also be defined on the Append() function body.
 */
struct FPinDataValue
{
	/** Set of all meshes behind a given output pin. */
	TSet<FMeshData> MeshesData;

	/** Add all data from other PinData. */
	void Append(const FPinDataValue& From);
};

/** Eases the managment and access of data behind a given pin.
 * 
 * We understand as "behind a pin" as all pins which are connected to a given pin directly and indirectly. 
 * 
 * Given the the following pins:
 * C -> B -> A
 *      D ---^
 * 
 * FPinData will propagate the added data in the following way:
 * C (data added on C) -> B (data added on C, B) -> A (data added on C, B, A, D)
 *                           D (data added on D) ---^
 * 
 * FPinDataValue defines which data is going to be saved behind pins.
 *
 * When traversing the graph:
 * - Each time an output pin is explored recursively, call the Push() function.
 * - When exiting the recursion, call the Pop() function.
 * - Add data to be seen behind all the stacked pins using the GetCurrent() function.
 * - To avoid missing any returning branch, use the SCOPE_PIN_DATA wrapper.
 * 
 * The all data from a behind a pin can be queried using the Find() method.
 */
class FPinData // DEPRECATED. DO NOT USE!
{
public:
	/** Get the PinData from a given pin. */
	FPinDataValue* Find(const UEdGraphPin* Pin);

	/** Get the PinData of the current pin. The top pin in the stack. */
	FPinDataValue& GetCurrent();

	/** Pop the current pin from the stack and append all its PinData with to previous pin in the stack. */
	void Pop();

	/** Push a pin to the stack and create its PinData. */
	void Push(const UEdGraphPin* Pin);

private:
	/** Data for each pin. */
	TMap<const UEdGraphPin*, FPinDataValue> Data;

	/** Stack of pins. Used to know which pin the data belongs to during the graph traversal recursion. */
	TArray<const UEdGraphPin*> PinStack;
};

/** Graph cycle key.
 *
 * Pin is not enough since we can call multiple recursive functions with the same pin.
 * Each function has to have an unique identifier.
 */
struct FGraphCycleKey
{
	friend uint32 GetTypeHash(const FGraphCycleKey& Key);

	FGraphCycleKey(const UEdGraphPin& Pin, const FString& Id);

	bool operator==(const FGraphCycleKey& Other) const;
	
	/** Valid pin. */
	const UEdGraphPin& Pin;

	/** Unique id. */
	FString Id;
};

/** Graph Cycle scope.
 *
 * Detect a cycle during the graph traversal.
 */
class FGraphCycle
{
public:
	explicit FGraphCycle(const FGraphCycleKey&& Key, FMutableGraphGenerationContext &Context);
	~FGraphCycle();

	/** Return true if there is a cycle. */
	bool FoundCycle() const;
	
private:
	/** Graph traversal key. */
	FGraphCycleKey Key;

	/** Generation context. */
	FMutableGraphGenerationContext& Context;
};

/** Return the default value if there is a cycle. */
#define RETURN_ON_CYCLE(Pin, GenerationContext) \
	FGraphCycle GraphCycle(FGraphCycleKey(Pin, TEXT(__FILE__ PREPROCESSOR_TO_STRING(__LINE__))), GenerationContext); \
	if (GraphCycle.FoundCycle()) \
	{ \
		return {}; \
	} \



struct FGeneratedGroupProjectorsKey
{
	UCustomizableObjectNodeGroupProjectorParameter* Node = nullptr;
	FName CurrentComponent;

	bool operator==(const FGeneratedGroupProjectorsKey&) const = default;
};

uint32 GetTypeHash(const FGeneratedGroupProjectorsKey& Key);


struct FMutableGraphGenerationContext
{
	FMutableGraphGenerationContext(const UCustomizableObject* CustomizableObject, class FCustomizableObjectCompiler* InCompiler, const FCompilationOptions& InOptions);
	~FMutableGraphGenerationContext();

	/** See FCustomizableObjectPrivateData::ParticipatingObjects. */
	void AddParticipatingObject(const FSoftObjectPath& SoftPath);
	
	/** See FCustomizableObjectPrivateData::ParticipatingObjects. */
	void AddParticipatingObject(const FSoftObjectPtr& SoftObject);

	/** See FCustomizableObjectPrivateData::ParticipatingObjects. */
	template<typename T>
	void AddParticipatingObject(const TSoftClassPtr<T>& SoftClass)
	{
		AddParticipatingObject(SoftClass.ToSoftObjectPath());
	}
	
	/** See FCustomizableObjectPrivateData::ParticipatingObjects. */
	void AddParticipatingObject(const UObject& Object);

	void Log(const FText& Message, const TArray<const UObject*>& UObject, const EMessageSeverity::Type MessageSeverity = EMessageSeverity::Warning, const bool bAddBaseObjectInfo = true, const ELoggerSpamBin SpamBin = ELoggerSpamBin::ShowAll) const;

	void Log(const FText& Message, const UObject* Context = nullptr, const EMessageSeverity::Type MessageSeverity = EMessageSeverity::Warning, const bool bAddBaseObjectInfo = true, const ELoggerSpamBin SpamBin = ELoggerSpamBin::ShowAll) const;
	
	const UCustomizableObject* Object = nullptr;

	/** Full hierarchy root. */
	UCustomizableObjectNodeObject* Root = nullptr;

private:
	// Non-owned reference to the compiler object
	FCustomizableObjectCompiler* Compiler = nullptr;

public:
	// Compilation options, including target platform
	const FCompilationOptions& Options;
	
	// Cache of generated pins per LOD
	TMap<FGeneratedKey, FGeneratedData> Generated;

	/** Set of all generated nodes. */
	TSet<UCustomizableObjectNode*> GeneratedNodes;

	/** Struct that stores the relevant information of a data table generated during the compilation. 
	e.g. all data tables must have the same compilation restrictions */
	struct FGeneratedDataTablesData
	{
		// Pointer to the generated mutable Table
		mu::Ptr<mu::Table> GeneratedTable;

		// Table Node used to fill this info
		const UCustomizableObjectNodeTable* ReferenceNode;

		// Stores the names of the rows that will be compiled
		TArray<FName> RowNames;
		TArray<uint32> RowIds;

		// Compilation Restrictions:
		// If there is a bool column in the table, checked rows will not be compiled
		bool bDisableCheckedRows;

		// Name of the column that determines de version control
		FName VersionColumn;

		// Compare the stored compilation settings with the compilation settings of a Table Node
		// return true if the compilation settings are equal
		bool HasSameSettings(const UCustomizableObjectNodeTable* Node) const;
	};

	// Cache of generated Node Tables
	TMap<FString, FGeneratedDataTablesData> GeneratedTables;

	TMap<FGeneratedGroupProjectorsKey, FGroupProjectorTempData> GeneratedGroupProjectors;

	/** Key is the Node Uid. */
	TMap<FString, mu::Ptr<mu::NodeScalarParameter>> GeneratedScalarParameters;

	/** Key is the Node Uid. */
	TMap<FString, mu::Ptr<mu::NodeScalarEnumParameter>> GeneratedEnumParameters;
	
	struct FGeneratedCompositeDataTablesData
	{
		UScriptStruct* ParentStruct = nullptr;
		TArray<FName> FilterPaths;
		UCompositeDataTable* GeneratedDataTable = nullptr;

		bool operator==(const FGeneratedCompositeDataTablesData& Other) const
		{
			return ParentStruct == Other.ParentStruct && FilterPaths == Other.FilterPaths;
		}
	};

	// Cache of generated Composited Data Tables
	TArray<FGeneratedCompositeDataTablesData> GeneratedCompositeDataTables;

	// Cache of generated images, because sometimes they are reused by LOD, we use this as a second
	// level cache
	TMap<FGeneratedImageKey, mu::NodeImagePtr> GeneratedImages;

	/** Data stored per-generated passthrough texture. */
	struct FGeneratedReferencedTexture
	{
		uint32 ID;
		//mu::FImageDesc ImageDesc;
	};

	/** Data stored per-generated passthrough mesh. */
	struct FGeneratedReferencedMesh
	{
		uint32 ID;
	};

	// Cache of runtime pass-through meshes and their IDs used in the core to identify them.
	// These meshes will remain as external references even in optimized models.
	TMap<TSoftObjectPtr<USkeletalMesh>, FGeneratedReferencedMesh> PassthroughMeshMap;

	// Cache of runtime pass-through images and their IDs used in the core to identify them.
	// These textures will remain as external references even in optimized models.
	TMap<TSoftObjectPtr<UTexture>, FGeneratedReferencedTexture> PassthroughTextureMap;

	// Cache of runtime images and their IDs used in the core to identify them.
	// These textures will remain as external references even in optimized models.
	TMap<TSoftObjectPtr<const UTexture>, FGeneratedReferencedTexture> RuntimeReferencedTextureMap;
	
	// Cache of runtime pass-through images and their IDs used in the core to identify them
	// These textures will become mutable images in the compiled model.
	TMap<TSoftObjectPtr<const UTexture>, FGeneratedReferencedTexture> CompileTimeTextureMap;

    // Global morph selection overrides.
    TArray<FRealTimeMorphSelectionOverride> RealTimeMorphTargetsOverrides;

	// Mutable meshes already build for source UStaticMesh or USkeletalMesh.
	struct FGeneratedMeshData
	{
		struct FKey
		{
			/** Source mesh data. */
			const UObject* Mesh = nullptr;
			int32 LOD = 0; // Mesh Data LOD (i.e., LOD where we are getting the vertices from)
			int32 CurrentLOD = 0; // Derived data LOD (i.e., LOD where we are generating the non-Core Data like morphs)
			int32 MaterialIndex = 0;

			/** Flag used to generate this mesh. Bit mask of EMutableMeshConversionFlags */
			EMutableMeshConversionFlags Flags = EMutableMeshConversionFlags::None;

			/** Tags added at the UE level that go through the Mutable core and are merged in the generated mesh.
			 *  Only add the tags that make the mesh unique and require it not to be cached together with the 
			 *  same exact mesh but with different tags.
			*/
			FString Tags;

			/**
			* SkeletalMeshNode is needed to disambiguate realtime morph selection from diferent nodes.
			* TODO: Consider using the actual selection.
			*/
			const UCustomizableObjectNode* SkeletalMeshNode = nullptr;

			bool operator==( const FKey& OtherKey ) const
			{
				return Mesh == OtherKey.Mesh && LOD == OtherKey.LOD && CurrentLOD == OtherKey.CurrentLOD && MaterialIndex == OtherKey.MaterialIndex
					&& Flags == OtherKey.Flags && Tags == OtherKey.Tags && SkeletalMeshNode == OtherKey.SkeletalMeshNode;
			}
		};

		FKey Key;

		/** Generated mesh. */
		mu::Ptr<mu::Mesh> Generated;
	};
	TArray<FGeneratedMeshData> GeneratedMeshes;

	struct FGeneratedTableImageData
	{
		FString PinName;
		FName PinType;
		const mu::Ptr<mu::Table> Table;
		const UCustomizableObjectNodeTable* TableNode;

		bool operator==(const FGeneratedTableImageData& Other) const
		{
			return PinName == Other.PinName && Table == Other.Table;
		}
	};
	TArray<FGeneratedTableImageData> GeneratedTableImages;

	// Stack of mesh generation flags. The last one is the currently valid.
	// The value is a bit mask of EMutableMeshConversionFlags
	TArray<EMutableMeshConversionFlags> MeshGenerationFlags;

	// Stack of Layout generation flags. The last one is the currently valid.
	TArray<FLayoutGenerationFlags> LayoutGenerationFlags;

	/** Stack of Group Projector nodes. Each time a Group Object node is visited, a set of Group Projector nodes get pushed
	 * When a Mesh Section node is found, it will compile all Group Projector nodes in the stack. */
	TArray<TArray<UCustomizableObjectNodeGroupProjectorParameter*>> CurrentGroupProjectors;

	/** Find a mesh if already generated for a given source and flags. */
	mu::Ptr<mu::Mesh> FindGeneratedMesh(const FGeneratedMeshData::FKey& Key);

	/** Add a resource to the streamed resources array.
	  * OutStreamedResourceContainer - Container to store the streamed resource.
	  * Returns resource index in the array of streamed resources. */
	int32 AddStreamedResource(const uint32 InResourceHash, UCustomizableObjectResourceDataContainer*& OutStreamedResourceContainer);

	/** Adds a streamed resource of type AssetUserData.
	  * Returns resource index in the array of streamed resources. */
	int32 AddAssetUserDataToStreamedResources(UAssetUserData* AssetUserData);

	uint32 GetSkinWeightProfileIdUnique(const FName ProfileName);

	// Check if the Id of the node Node already exists, if it's new adds it to NodeIds array, otherwise, returns new Id
	const FGuid GetNodeIdUnique(const UCustomizableObjectNode* Node);

	/** Generates shared surface IDs for all surface nodes. If one or more nodes are equal, they will use the same SharedSurfaceId */
	void GenerateSharedSurfacesUniqueIds();

	bool FindBone(const FName& BoneName, mu::FBoneName& OutBone) const;

	/** Get unique identifier for BoneName built from its FString. */
	mu::FBoneName GetBoneUnique(const FName& BoneName);

	/** Check if the PhysicsAsset of a given SkeletalMesh has any SkeletalBodySetup with BoneNames not present in the
	* InSkeletalMesh's RefSkeleton, if so, adds the PhysicsAsset to the DiscartedPhysicsAssetMap to display a warning later on */
	//void CheckPhysicsAssetInSkeletalMesh(const USkeletalMesh* InSkeletalMesh);

	/** Get the reference skeletal mesh associated to the current mesh component being generated */
	FMutableComponentInfo* GetCurrentComponentInfo();

	UObject* LoadObject(const FSoftObjectPtr& SoftObject, bool bParticipatingObjectsPassLoad = false);

	template<typename T>
	T* LoadObject(const TSoftObjectPtr<T>& SoftObject, bool bParticipatingObjectsPassLoad = false)
	{
		return bLoadObjects && (!bParticipatingObjectsPass || bParticipatingObjectsPassLoad) ?
			SoftObject.LoadSynchronous() :
			nullptr;
	}

	template<typename T>
	UClass* LoadClass(const TSoftClassPtr<T>& SoftClass, bool bParticipatingObjectsPassLoad = false)
	{
		return bLoadObjects && (!bParticipatingObjectsPass || bParticipatingObjectsPassLoad) ?
			SoftClass.LoadSynchronous() :
			nullptr;
	}

private:
	/** Two operation modes:
	 * 1. bParticipatingObjectsPass = true, add the participating object.
	 * 2. bParticipatingObjectsPass = false, check that the participating object was discovered in the participating object pass. */
	void AddParticipatingObjectChecked(const FName& PackageName, const FGuid& PackageGuid);

public:
	/** Only Mesh Components (no passthrough). */
	TArray<FMutableComponentInfo> ComponentInfos;

	/** Only compiled components. All components types. Index is the ObjectComponentIndex. */
	TArray<FName> ComponentNames;
	
	TArray<FMutableRefSkeletalMeshData> ReferenceSkeletalMeshesData;
	
	TArray<UMaterialInterface*> ReferencedMaterials;
	TArray<FName> ReferencedMaterialSlotNames;
	TMap<FGeneratedImagePropertiesKey, FGeneratedImageProperties> ImageProperties;
	TArray<const UCustomizableObjectNode*> NoNameNodeObjectArray;
	TMap<FString, FCustomizableObjectIdPair> GroupNodeMap;
	TMap<FString, FString> CustomizableObjectPathMap;
	TMap<FString, FMutableParameterData> ParameterUIDataMap;
	TMap<FString, FMutableStateData> StateUIDataMap;
	TMap<TTuple<FString, FString>, TSet<TSoftObjectPtr<UDataTable>>> IntParameterOptionDataTable;
	//TMap<UPhysicsAsset*, uint32> DiscartedPhysicsAssetMap;

	TArray<const USkeleton*> ReferencedSkeletons;

	// Array of unique Bone identifiers. 
	TMap<mu::FBoneName, FString> UniqueBoneNames;
	TMap<FString, mu::FBoneName> RemappedBoneNames; // Bone identifiers that had a collision.

	// Used to aviod Nodes with duplicated ids
	TMap<FGuid, TArray<const UObject*>> NodeIdsMap;
	TMultiMap<const UCustomizableObject*, FGroupNodeIdsTempData> DuplicatedGroupNodeIds;

	// For a given material node (the key is node package path + node uid + image index in node) stores images generated for the same node at a higher quality LOD to reuse that image node
	TMap<FString, FGroupProjectorImageInfo> GroupProjectorLODCache;

	// Data used for MorphTarget reconstruction.
	TMap<uint32, FRealTimeMorphMeshData> RealTimeMorphTargetPerMeshData;

	// Data used for Clothing reconstruction.
	TArray<FCustomizableObjectClothingAssetData> ClothingAssetsData;
	TMap<uint32, FClothingMeshData> ClothingPerMeshData;

	TMap<uint32, FMutableMeshMetadata> MeshMetadata;
	TMap<uint32, FMutableSurfaceMetadata> SurfaceMetadata;

	// Data used for SkinWeightProfiles reconstruction
	TArray<FMutableSkinWeightProfileInfo> SkinWeightProfilesInfo;

	TMap<uint32, FName> UniqueSkinWeightProfileIds;
	TMap<FName, uint32> RemappedSkinWeightProfileIds;

	TArray<FAnimBpOverridePhysicsAssetsInfo> AnimBpOverridePhysicsAssetsInfo;

	uint8 FromLOD = 0; // LOD to append to the CurrentLOD when using AutomaticLODs. 
	uint8 CurrentLOD = 0;
	FName CurrentMeshComponent;

	/** If this is set, we are genreating materials for a "passthrough" component, with a fixed mesh. */
	mu::Ptr<mu::NodeMesh> ComponentMeshOverride;

	uint8 NumLODsInRoot = 0;

	uint8 FirstLODAvailable = MAX_MESH_LOD_COUNT;
	uint8 NumMaxLODsToStream = MAX_MESH_LOD_COUNT;

	bool bEnableLODStreaming = true;

	bool bPartialCompilation = false;

	/** true if performing the Participating Objects pass. */
	bool bParticipatingObjectsPass = false;

	/** true if the Participating Objects have been skipped. */
	bool bSkipParticipatingObjectsPass = false;

	/** Load any Soft Object/Class Pointers. */
	bool bLoadObjects = true;
	
	// Based on the last object visited.
	ECustomizableObjectAutomaticLODStrategy CurrentAutoLODStrategy = ECustomizableObjectAutomaticLODStrategy::Manual;

	// Stores external graph root nodes to be added to the specified group nodes
	TMultiMap<FGuid, UCustomizableObjectNodeObject*> GroupIdToExternalNodeMap;

	// Easily retrieve a parameter name from its node guid
	TMap<FGuid, FString> GuidToParamNameMap;

	// Graph cycle detection
	/** Visited nodes during the DAC recursion traversal.
	 * It acts like stack, pushing pins when recursively exploring a new pin an popping it when exiting the recursion. */
	TMap<FGraphCycleKey, const UCustomizableObject*> VisitedPins;
	const UCustomizableObject* CustomizableObjectWithCycle = nullptr;

	/** Stores the physics assets gathered from the SkeletalMesh nodes during compilation, to be used in mesh generation in-game */
	TArray<TSoftObjectPtr<UPhysicsAsset>> PhysicsAssets;

	/** Stores the anim BP assets gathered from the SkeletalMesh nodes during compilation, to be used in mesh generation in-game */
	TArray<TSoftClassPtr<UAnimInstance>> AnimBPAssets;

	/** Stores the sockets provided by the part skeletal meshes, to be merged in the generated meshes */
	TArray<FMutableRefSocket> SocketArray;

	/** Used to propagate the socket priority defined in group nodes to their child skeletal mesh nodes
	* It's a stack because group nodes are recursive
	*/
	TArray<int32> SocketPriorityStack;
	
	// Stores what param names use a certain table as a table can be used from multiple table nodes, useful for partial compilations to restrict params
	TMap<FString, FMutableParamNameSet> TableToParamNames;

	TArray<const UEdGraphNode*> LimitedParameters;
	int32 ParameterLimitationCount = 0;

	/** Data which is behind a given output pin. 
	 *
	 * We only consider output pins. Do not use input pins as keys. See FPinData class definition. 
	 */
	FPinData PinData; // DEPRECATED. DO NOT USE!

	// Stores all morphs to apply them directly to a skeletal mesh node
	TArray<FMorphNodeData> MeshMorphStack;

	// Current material parameter name to find the corresponding column in a mutable table
	FString CurrentMaterialTableParameter;

	// Current material parameter id to find the corresponding column in a mutable table
	FString CurrentMaterialTableParameterId;

	struct FSharedSurface
	{
		FSharedSurface(uint8 InLOD, const mu::Ptr<mu::NodeSurfaceNew>& InNodeSurfaceNew);

		bool operator==(const FSharedSurface& o) const;

		uint8 LOD = 0;
		mu::Ptr<mu::NodeSurfaceNew> NodeSurfaceNew;

		bool bMakeUnique = false;
		TArray<SIZE_T> NodeModifierIDs;
	};

	// Material to SharedSurfaceId
	TMap<UCustomizableObjectNodeMaterialBase*, TArray<FSharedSurface>> SharedSurfaceIds;

	/** Resource Data constants */
	TMap<uint32, int32> StreamedResourceIndices;
	TArray<TPair<FName, UCustomizableObjectResourceDataContainer*>> StreamedResourceData;

	/** Extension Data constants are collected here */
	FExtensionDataCompilerInterface ExtensionDataCompilerInterface;
	TArray<FCustomizableObjectResourceData> AlwaysLoadedExtensionData;
	TArray<TPair<FName, UCustomizableObjectResourceDataContainer*>> StreamedExtensionData;

	/** See FCustomizableObjectPrivateData::ParticipatingObjects. */
	TMap<FName, FGuid> ParticipatingObjects;

	/** Map to relate a Composite Data Table Row and its original DataTable */
	TMap<UDataTable*,TMap<FName, TArray<UDataTable*>>> CompositeDataTableRowToOriginalDataTableMap;

	/** Version Bridge of the root object */
	TObjectPtr<UObject> RootVersionBridge;
};

/** Pin Data scope wrapper. Pops the pin data on scope exit. */
class FScopedPinData
{
public:
	explicit FScopedPinData(FMutableGraphGenerationContext& Context, const UEdGraphPin* Pin);
	~FScopedPinData();

private:
	FMutableGraphGenerationContext& Context;
};

#define SCOPED_PIN_DATA(Context, Pin) \
	FScopedPinData ScopedPinData(Context, Pin);


namespace Private
{
	template<class HashableType, class HashDataSetType, class HashFuncType, class CompareFuncType>
	uint32 GenerateUniquePersistentHash(const HashableType& HashableData, const HashDataSetType& HashDataSet, HashFuncType&& HashFunc, CompareFuncType&& CompareFunc)
	{
		constexpr uint32 InvalidResourceId = 0;
		
		const uint32 DataHash = HashFunc(HashableData);

		uint32 UniqueHash = DataHash == InvalidResourceId ? DataHash + 1 : DataHash;

		const HashableType* FoundHash = HashDataSet.Find(UniqueHash);

		bool bIsDataAlreadyCollected = false;
		
		if (FoundHash)
		{
			bIsDataAlreadyCollected = CompareFunc(*FoundHash, HashableData); 
		}

		// NOTE: This way of unique hash generation guarantees all valid values can be used but given its 
		// sequential nature a cascade of changes can occur if new meshes are added. Not many hash collisions 
		// are expected so it should not be problematic.
		if (FoundHash && !bIsDataAlreadyCollected)
		{
			uint32 NumTries = 0;
			for (; NumTries < TNumericLimits<uint32>::Max(); ++NumTries)
			{
				FoundHash = HashDataSet.Find(UniqueHash);
				
				if (!FoundHash)
				{
					break;
				}

				bIsDataAlreadyCollected = CompareFunc(*FoundHash, HashableData);

				if (bIsDataAlreadyCollected)
				{
					break;
				}

				UniqueHash = UniqueHash + 1 == InvalidResourceId ? InvalidResourceId + 1 : UniqueHash + 1;
			}

			if (NumTries == TNumericLimits<uint32>::Max())
			{
				UniqueHash = InvalidResourceId;
			}	
		}

		return UniqueHash;
	}
} //Private

//
mu::Ptr<mu::NodeObject> GenerateMutableSource(const class UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext);

/** Populate an array with all the information related to the reference skeletal meshes we might need in-game to generate instances */
void PopulateReferenceSkeletalMeshesData(FMutableGraphGenerationContext& GenerationContext);


void CheckNumOutputs(const UEdGraphPin& Pin, const FMutableGraphGenerationContext& GenerationContext);


// TODO FutureGMT Remove generation context dependency and move to GraphTraversal.
UTexture2D* FindReferenceImage(const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext);


mu::NodeMeshApplyPosePtr CreateNodeMeshApplyPose(FMutableGraphGenerationContext& GenerationContext, mu::NodeMeshPtr InputMeshNode, const TArray<FName>& ArrayBoneName, const TArray<FTransform>& ArrayTransform);


/** Adds Tag to MutableMesh uniquely, returns the index were the tag has been inserted or the index where an intance of the tag has been found */
int32 AddTagToMutableMeshUnique(mu::Mesh& MutableMesh, const FString& Tag);

void AddSocketTagsToMesh(const USkeletalMesh* SourceMesh, mu::Ptr<mu::Mesh> MutableMesh, FMutableGraphGenerationContext& GenerationContext);

// Generates the tag for an animation instance
FString GenerateAnimationInstanceTag(const int32 AnimInstanceIndex, const FName& SlotIndex);


FString GenerateGameplayTag(const FString& GameplayTag);

uint32 GetBaseTextureSize(const FMutableGraphGenerationContext& GenerationContext, const UCustomizableObjectNodeMaterialBase* Material, uint32 ImageIndex);

// Computes the LOD bias for a texture given the current mesh LOD and automatic LOD settings, the reference texture settings
// and whether it's being built for a server or not
uint32 ComputeLODBiasForTexture(const FMutableGraphGenerationContext& GenerationContext, const UTexture2D& Texture, const UTexture2D* ReferenceTexture = nullptr, int32 MaxTextureSizeInGame = 0);

// Max texture size to set on the ImageProperties
int32 GetMaxTextureSize(const UTexture2D& ReferenceTexture, const UTextureLODSettings& LODSettings);

// Max texture size of the texture with per platform MaxTextureSize and LODBias applied.
int32 GetTextureSizeInGame(const UTexture2D& Texture, const UTextureLODSettings& LODSettings, uint8 SurfaceLODBias = 0);

mu::Ptr<mu::Image> GenerateImageConstant(UTexture*, FMutableGraphGenerationContext&, bool bIsReference);
mu::Ptr<mu::Mesh> GenerateMeshConstant(USkeletalMesh*, FMutableGraphGenerationContext&, bool bIsReference);

/** Generates a mutable image descriptor from an unreal engine texture */
mu::FImageDesc GenerateImageDescriptor(UTexture* Texture);

/** 
 * Add SurfaceMetadata gathered form Material and MeshSection to HashSurfaceMetadataSet.
 * 
 * return the unique id for the SurfaceMetadata in HashSurfaceMetadataSet.
 **/
uint32 AddUniqueSurfaceMetadata(const FSkeletalMaterial* Material, const FSkelMeshSection* MeshSection, TMap<uint32, FMutableSurfaceMetadata>& InOutHashSurfaceMetadataSet);
