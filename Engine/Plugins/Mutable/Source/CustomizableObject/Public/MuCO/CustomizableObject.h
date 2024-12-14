// Copyright Epic Games, Inc. All Rights Reserved.
 
#pragma once

#include "RHIDefinitions.h"
#include "MuCO/CustomizableObjectCompilerTypes.h"
#include "MuCO/CustomizableObjectClothingTypes.h"
#include "MuCO/CustomizableObjectResourceData.h"
#include "MuCO/CustomizableObjectResourceDataTypes.h"
#include "MuCO/CustomizableObjectStreamedResourceData.h"
#include "MuCO/CustomizableObjectParameterTypeDefinitions.h"
#include "MuCO/CustomizableObjectUIData.h"
#include "MuCO/CustomizableObject_Deprecated.h"
#include "Templates/TypeCompatibleBytes.h"

#include "CustomizableObject.generated.h"

class FReply;
class FObjectPreSaveContext;
class FText;
class IAsyncReadFileHandle;
class ITargetPlatform;
class UCustomizableObject;
class UEdGraph;
class USkeletalMesh;
class UCustomizableObjectPrivate;
class UCustomizableObjectBulk;
struct FFrame;
struct FStreamableHandle;
template <typename FuncType> class TFunctionRef;
 
// Forward declaration of Mutable classes necessary for the interface
namespace mu
{
	class Model;
	class Parameters;
}


CUSTOMIZABLEOBJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogMutable, Log, All);


USTRUCT()
struct FFParameterOptionsTags
{
	GENERATED_USTRUCT_BODY()

	/** List of tags of a Parameter Options */
	UPROPERTY(Category = "CustomizablePopulation", EditAnywhere)
	TArray<FString> Tags;
};


USTRUCT()
struct FParameterTags
{
	GENERATED_USTRUCT_BODY()

	/** List of tags of a parameter */
	UPROPERTY(Category = "CustomizablePopulation", EditAnywhere)
	TArray<FString> Tags;

	/** Map of options available for a parameter can have and their tags */
	UPROPERTY(Category = "CustomizablePopulation", EditAnywhere)
	TMap<FString, FFParameterOptionsTags> ParameterOptions;
};


USTRUCT()
struct FProfileParameterDat
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FString ProfileName;

	UPROPERTY()
	TArray<FCustomizableObjectBoolParameterValue> BoolParameters;

	////
	UPROPERTY()
	TArray<FCustomizableObjectIntParameterValue> IntParameters;

	////
	UPROPERTY()
	TArray<FCustomizableObjectFloatParameterValue> FloatParameters;

	////
	UPROPERTY()
	TArray<FCustomizableObjectTextureParameterValue> TextureParameters;

	////
	UPROPERTY()
	TArray<FCustomizableObjectVectorParameterValue> VectorParameters;

	////
	UPROPERTY()
	TArray<FCustomizableObjectProjectorParameterValue> ProjectorParameters;
	
	////
	UPROPERTY()
	TArray<FCustomizableObjectTransformParameterValue> TransformParameters;
};

// A USTRUCT version of FMeshToMeshVertData in SkeletalMeshTypes.h
// We are taking advantage of the padding data to store from which asset this data comes from
// maintaining the same memory footprint than the original.
USTRUCT()
struct FCustomizableObjectMeshToMeshVertData
{
	GENERATED_USTRUCT_BODY()

	FCustomizableObjectMeshToMeshVertData() = default;

	explicit FCustomizableObjectMeshToMeshVertData(const FMeshToMeshVertData& Original)
		: Weight(Original.Weight)
	{
		for (int32 i = 0; i < 4; ++i)
		{
			PositionBaryCoordsAndDist[i] = Original.PositionBaryCoordsAndDist[i];
			NormalBaryCoordsAndDist[i] = Original.NormalBaryCoordsAndDist[i];
			TangentBaryCoordsAndDist[i] = Original.TangentBaryCoordsAndDist[i];
			SourceMeshVertIndices[i] = Original.SourceMeshVertIndices[i];
		}
	}
		

	explicit operator FMeshToMeshVertData() const
	{
		FMeshToMeshVertData ReturnValue;

		for (int32 i = 0; i < 4; ++i)
		{
			ReturnValue.PositionBaryCoordsAndDist[i] = PositionBaryCoordsAndDist[i];
			ReturnValue.NormalBaryCoordsAndDist[i] = NormalBaryCoordsAndDist[i];
			ReturnValue.TangentBaryCoordsAndDist[i] = TangentBaryCoordsAndDist[i];
			ReturnValue.SourceMeshVertIndices[i] = SourceMeshVertIndices[i];
		}
		ReturnValue.Weight = Weight;
		ReturnValue.Padding = 0;

		return ReturnValue;
	}

	
	// Barycentric coords and distance along normal for the position of the final vert
	UPROPERTY()
	float PositionBaryCoordsAndDist[4] = {0.f};

	// Barycentric coords and distance along normal for the location of the unit normal endpoint
	// Actual normal = ResolvedNormalPosition - ResolvedPosition
	UPROPERTY()
	float NormalBaryCoordsAndDist[4] = {0.f};

	// Barycentric coords and distance along normal for the location of the unit Tangent endpoint
	// Actual normal = ResolvedNormalPosition - ResolvedPosition
	UPROPERTY()
	float TangentBaryCoordsAndDist[4] = {0.f};

	// Contains the 3 indices for verts in the source mesh forming a triangle, the last element
	// is a flag to decide how the skinning works, 0xffff uses no simulation, and just normal
	// skinning, anything else uses the source mesh and the above skin data to get the final position
	UPROPERTY()
	uint16	 SourceMeshVertIndices[4] = {0u, 0u, 0u, 0u};

	UPROPERTY()
	float Weight = 0.0f;

	// Non serialized, unused padding. This is present in the FMeshToMeshVertData struct as Padding for alignment.
	uint32 UnusedPadding = 0;

	/**
	 * Serializer
	 *
	 * @param Ar - archive to serialize with
	 * @param V - vertex to serialize
	 * @return archive that was used
	 */
	friend FArchive& operator<<(FArchive& Ar, FCustomizableObjectMeshToMeshVertData& V)
	{
		Ar << V.PositionBaryCoordsAndDist[0];
		Ar << V.PositionBaryCoordsAndDist[1];
		Ar << V.PositionBaryCoordsAndDist[2];
		Ar << V.PositionBaryCoordsAndDist[3];
		Ar << V.NormalBaryCoordsAndDist[0];
		Ar << V.NormalBaryCoordsAndDist[1];
		Ar << V.NormalBaryCoordsAndDist[2];
		Ar << V.NormalBaryCoordsAndDist[3];
		Ar << V.TangentBaryCoordsAndDist[0];
		Ar << V.TangentBaryCoordsAndDist[1];
		Ar << V.TangentBaryCoordsAndDist[2];
		Ar << V.TangentBaryCoordsAndDist[3];

		Ar << V.SourceMeshVertIndices[0];
		Ar << V.SourceMeshVertIndices[1];
		Ar << V.SourceMeshVertIndices[2];
		Ar << V.SourceMeshVertIndices[3];

		Ar << V.Weight;

		return Ar;
	}
};
static_assert(sizeof(FCustomizableObjectMeshToMeshVertData) == sizeof(float)*4*3 + sizeof(uint16)*4 + sizeof(float) + sizeof(uint32));
template<> struct TCanBulkSerialize<FCustomizableObjectMeshToMeshVertData> { enum { Value = true }; };

USTRUCT()
struct FMutableLODSettings
{
	GENERATED_BODY()

	/** Minimum LOD to render per Platform. */
	UPROPERTY(EditAnywhere, Category = LODSettings, meta = (DisplayName = "Minimum LOD"))
	FPerPlatformInt MinLOD;

	/** Minimum LOD to render per Quality level.*/
	UPROPERTY(EditAnywhere, Category = LODSettings, meta = (DisplayName = "Quality Level Minimum LOD"))
	FPerQualityLevelInt MinQualityLevelLOD;

#if WITH_EDITORONLY_DATA

	/** Override the LOD Streaming settings from the reference skeletal meshes.*/
	UPROPERTY(EditAnywhere, Category = LODSettings, meta = (DisplayName = "Override LOD Streaming Settings"))
	bool bOverrideLODStreamingSettings = true;

	/** Enabled: streaming LODs will trigger automatic updates to generate and discard LODs. Streaming may decrease the amount of memory used, but will stress the CPU and Streaming of resources.
	  *	Keep in mind that, even though updates may be faster depending on the amount of LODs to generate, there may be more updates to process.
	  * 
	  * Disabled: all LODs will be generated at once. It may increase the amount of memory used by the meshes and the generation may take longer, but less updates will be required. */
	UPROPERTY(EditAnywhere, Category = LODSettings, meta = (EditCondition = "bOverrideLODStreamingSettings", DisplayName = "Enable LOD Streaming"))
	FPerPlatformBool bEnableLODStreaming = true;

	/** Limit the number of LODs to stream. A value of 0 is the same as disabling streaming of LODs.*/
	UPROPERTY(EditAnywhere, Category = LODSettings, meta = (EditCondition = "bOverrideLODStreamingSettings"))
	FPerPlatformInt NumMaxStreamedLODs = MAX_MESH_LOD_COUNT;

#endif
};


UCLASS( BlueprintType, config=Engine )
class CUSTOMIZABLEOBJECT_API UCustomizableObject : public UObject
{
	friend UCustomizableObjectPrivate;
	
public:
	GENERATED_BODY()

	UCustomizableObject();

#if WITH_EDITORONLY_DATA
private:

	UPROPERTY()
	TObjectPtr<USkeletalMesh> ReferenceSkeletalMesh_DEPRECATED;

	UPROPERTY()
	TArray<TObjectPtr<USkeletalMesh>> ReferenceSkeletalMeshes_DEPRECATED;

public:

	/**
	  * The optional VersionBridge asset, which must implement the ICustomizableObjectVersionBridgeInterface, will be used to
	  * decide which Mutable child CustomizableObjects and table rows must be included in a compilation/cook depending on its 
	  * version struct/column by comparing it to the game-specific version system.
	*/
	UPROPERTY(EditAnywhere, Category = Versioning)
	TObjectPtr<UObject> VersionBridge;

	/**
	  * This optional struct is used to define which version this child CustomizableObject belongs to. It will be used during
	  * cook/compilation to decide whether this CO should be included or not in the final compiled CO. To be used, the root
	  * CO must have defined the VersionBridge property, which must implement the ICustomizableObjectVersionBridgeInterface
	*/
	UPROPERTY(EditAnywhere, Category = Versioning)
	FInstancedStruct VersionStruct;
#endif
	
	UPROPERTY(EditAnywhere, Category = CustomizableObject, meta = (DisplayName = "LOD Settings"))
	FMutableLODSettings LODSettings;
	
private:
	// mu::ExtensionData::Index is an index into this array when mu::ExtensionData::Origin is ConstantAlwaysLoaded
	UPROPERTY()
	TArray<FCustomizableObjectResourceData> AlwaysLoadedExtensionData;

	// mu::ExtensionData::Index is an index into this array when mu::ExtensionData::Origin is ConstantStreamed
	UPROPERTY()
	TArray<FCustomizableObjectStreamedResourceData> StreamedExtensionData;

public:
	/** Use the SkeletalMesh of reference as a placeholder until the custom mesh is ready to use.
	  * Note: If disabled, a null mesh will be used to replace the discarded mesh due to 'ReplaceDiscardedWithReferenceMesh' being enabled. */
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	bool bEnableUseRefSkeletalMeshAsPlaceholder = true;

	/** Use the Instance MinLOD, and RequestedLODs in the descriptor when performing the initial generation (ignore LOD Management). */
	UPROPERTY(Category = "CustomizableObject", EditAnywhere, DisplayName = "Preserve User LODs On First Generation")
	bool bPreserveUserLODsOnFirstGeneration = false;

	/** If true, reuse previously generated USkeletalMesh (if still valid and the the number of LOD have not changed)
	 * USkeletalMeshes are only reused between the same CO. */
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	bool bEnableMeshCache = false;

	/** Experimental - If true, Mesh LODs will be streamed on demand. It requires streaming of SkeletalMeshes and Mutable.StreamMeshLODsEnabled to be enabled.
	 *  Does not support Clothing, Morphs, and alternative SkinWeightProfiles yet. */
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	bool bEnableMeshStreaming = false;
	
#if WITH_EDITORONLY_DATA
private:
	UPROPERTY()
	FCompilationOptions_DEPRECATED CompileOptions_DEPRECATED;

public:
	UPROPERTY(EditAnywhere, Category = CompileOptions)
	bool bEnableRealTimeMorphTargets = false;

	UPROPERTY(EditAnywhere, Category = CompileOptions)
	bool bEnableClothing = false;

	UPROPERTY(EditAnywhere, Category = CompileOptions)
	bool bEnable16BitBoneWeights = false;

	UPROPERTY(EditAnywhere, Category = CompileOptions)
	bool bEnableAltSkinWeightProfiles = false;

	UPROPERTY(EditAnywhere, Category = CompileOptions)
	bool bEnablePhysicsAssetMerge = false;

	/** Experimental */
	UPROPERTY(EditAnywhere, Category = CompileOptions)
	bool bEnableAnimBpPhysicsAssetsManipualtion = false;

	// When this is enabled generated meshes will merge the AssetUserData from all of its constituent mesh parts
	UPROPERTY(EditAnywhere, Category = CompileOptions)
	bool bEnableAssetUserDataMerge = false;

	// Disabling the Table Materials parent material check will let the user use any material regardless of its parent when connecting a material from a table column to a material node.
	// Warning! But it will not check if the table material channels connected to the Material node exist in the actual material used in the Instance, and will fail silently at runtime 
	// when setting the value of those channels if they don't exist.
	UPROPERTY(EditAnywhere, Category = CompileOptions)
	bool bDisableTableMaterialsParentCheck = false;

	// Options when compiling this customizable object (see EMutableCompileMeshType declaration for info)
	UPROPERTY(EditAnywhere, Category = CompileOptions)
	EMutableCompileMeshType MeshCompileType = EMutableCompileMeshType::LocalAndChildren;

	// Array of elements to use with compile option CompileType = WorkingSet
	UPROPERTY(EditAnywhere, Category = CompileOptions)
	TArray<TSoftObjectPtr<UCustomizableObject>> WorkingSet;

private:
	// Editor graph
	UPROPERTY()
	TObjectPtr<UEdGraph> Source;

	// Used to verify the derived data matches this version of the Customizable Object.
	UPROPERTY()
	FGuid VersionId;

	UPROPERTY()
	TArray<FProfileParameterDat> InstancePropertiesProfiles;
#endif // WITH_EDITORONLY_DATA

public:
	/** Get the number of components this Customizable Object has. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	int32 GetComponentCount() const;

	/** Return the name of the component.
	 *  @return NAME_None if the component does not exist. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	FName GetComponentName(int32 ObjectComponentIndex) const;
	
	/** Get the number of parameters available in instances of this object. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	int32 GetParameterCount() const;

	/** Get the index of a parameter from its name. Return -1 if the parameter is not found. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	int32 FindParameter(const FString& Name) const;

	/** Get the type of a parameter from its index. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	EMutableParameterType GetParameterType(int32 ParamIndex) const;

	/** Get the type of a parameter from its name. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	EMutableParameterType GetParameterTypeByName(const FString& Name) const;

	/** Get the name of a parameter from its index. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	const FString& GetParameterName(int32 ParamIndex) const;

	/** Deprecated. It will always return 0. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject, meta = (DeprecatedFunction, DeprecationMessage = "Parameter decorations have been removed. This method will be removed in future versions."))
	int32 GetParameterDescriptionCount(const FString& ParamName) const;

	/** Returns how many possible options an int parameter has, if the parameter is an enumeration. Otherwise return 0. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	int32 GetIntParameterNumOptions(int32 ParamIndex) const;

	/** Gets the Name of the option at position K in the list of available options for the int parameter.
	  * Useful to enumerate the int parameter's possible options (Ex: "Hat1", "Hat2", "Cap", "Nothing") */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	const FString& GetIntParameterAvailableOption(int32 ParamIndex, int32 K) const;

	int32 FindIntParameterValue( int32 ParamIndex, const FString& Value ) const;
	FString FindIntParameterValueName(int32 ParamIndex, int32 ParamValue) const;

	// DEPRECATED
	USkeletalMesh* GetRefSkeletalMesh(int32 ObjectComponentIndex = 0) const;

	/** Given a Mesh Component name, return its reference Skeletal Mesh. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	USkeletalMesh* GetComponentMeshReferenceSkeletalMesh(const FName& Name) const;
	
	/** Get the default value of a parameter of type Float.
	  * @param InParameterName The name of the Float parameter to get the default value of.
	  * @return The default value of the provided parameter name. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	float GetFloatParameterDefaultValue(UPARAM(DisplayName = "Parameter Name") const FString& InParameterName) const;
	
	/** Get the default value of a parameter of type Int. 
	  * @param InParameterName The name of the Int parameter to get the default value of.
	  * @return The default value of the provided parameter name. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	int32 GetIntParameterDefaultValue(UPARAM(DisplayName = "Parameter Name") const FString& InParameterName) const;
 
	/** Get the default value of a parameter of type Bool.
	  * @param InParameterName The name of the Bool parameter to get the default value of.
	  * @return The default value of the provided parameter name. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	bool GetBoolParameterDefaultValue(UPARAM(DisplayName = "Parameter Name") const FString& InParameterName) const;

	/** Get the default value of a parameter of type Color.
	  * @param InParameterName The name of the Color parameter to get the default value of.
	  * @return The default value of the provided parameter name. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	FLinearColor GetColorParameterDefaultValue(UPARAM(DisplayName = "Parameter Name") const FString& InParameterName) const;
	
	/** Get the default value of a parameter of type Transform.
	  * @param InParameterName The name of the Transform parameter to get the default value of.
	  * @return The default value of the provided parameter name. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	FTransform GetTransformParameterDefaultValue(UPARAM(DisplayName = "Parameter Name") const FString& InParameterName) const;

	
	/** Get the default value of a parameter of type Projector.
	  * @param InParameterName The name of the Projector parameter to get the default value of.
	  * @param OutPos The default position of the Projector.
	  * @param OutDirection The default projection direction of the Projector.
	  * @param OutUp The default up vector of the Projector.
	  * @param OutScale The default scale of the Projector.
	  * @param OutAngle The default angle of the Projector.
	  * @param OutType The default type of the Projector. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	void GetProjectorParameterDefaultValue(
		UPARAM(DisplayName = "Parameter Name") const FString& InParameterName,
		UPARAM(DisplayName = "Position") FVector3f& OutPos,
		UPARAM(DisplayName = "Direction") FVector3f& OutDirection,
		UPARAM(DisplayName = "Up") FVector3f& OutUp,
		UPARAM(DisplayName = "Scale") FVector3f& OutScale,
		UPARAM(DisplayName = "Angle") float& OutAngle,
		UPARAM(DisplayName = "Type") ECustomizableObjectProjectorType& OutType) const;

	/** Get the default value of a projector with the provided name
	  * @param InParameterName The name of the parameter to get the default value of.
	  * @return A data structure containing all the default data for the targeted projector parameter. */
	FCustomizableObjectProjector GetProjectorParameterDefaultValue(const FString& InParameterName) const;
	
	/** Get the default value of a parameter of type Texture.
	  * @param InParameterName The name of the Projector parameter to get the default value of.
	  * @return An id representing the default parameter's texture. */
	FName GetTextureParameterDefaultValue(const FString& InParameterName) const;

	/** Return true if the parameter at the index provided is multidimensional.
	  * @param InParamIndex The index of the parameter to check.
	  * @return True if the parameter is multidimensional and false if it is not. */
	bool IsParameterMultidimensional(const int32& InParamIndex) const;
	
	/** Return true if the parameter at the index provided is multidimensional.
	  * @param InParameterName The name of the parameter to check.
	  * @return True if the parameter is multidimensional and false if it is not. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	bool IsParameterMultidimensional(UPARAM(DisplayName = "Parameter Name") const FString& InParameterName) const;

	// Begin UObject interface.
	void PostLoad() override;
	void Serialize(FArchive& Ar) override;
#if WITH_EDITOR
	
	/** Compile the object if Automatic Compilation is enabled and has not been already compiled.
	  * Automatic compilation can be enabled/disabled in the Mutable's Plugin Settings.
	  * @return true if compiled */
	bool ConditionalAutoCompile();
	
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;
	UE_DEPRECATED(5.4, "Implement the version that takes FAssetRegistryTagsContext instead.")
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;

	/** Returns a one line description of an object for viewing in the thumbnail view of the generic browser */
	FString GetDesc() override;
	bool IsEditorOnly() const override;
	void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
	void PostSaveRoot(FObjectPostSaveRootContext ObjectSaveContext) override;
	void PostRename(UObject* OldOuter, const FName OldName) override;	
	void BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform) override;
	bool IsCachedCookedPlatformDataLoaded(const ITargetPlatform* TargetPlatform) override;
#endif
	// End UObject interface.

	int32 FindState(const FString& Name) const;

	/** Return the number of object states that are defined in the CustomizableObject. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	int32 GetStateCount() const;

	/** Return the name of an object state from its index. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	FString GetStateName(int32 StateIndex) const;

	int32 GetStateParameterCount(int32 StateIndex) const;
	int32 GetStateParameterIndex(int32 StateIndex, int32 ParameterIndex) const;

	/** Return the number of parameters that are editable at runtime for a specific state. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	int32 GetStateParameterCount(const FString& StateName) const;

	/** Return the name of one of the state's runtime parameters, by its index (from 0 to GetStateParameterCount-1). */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	FString GetStateParameterName(const FString& StateName, int32 ParameterIndex) const;
	FString GetStateParameterName(int32 StateIndex, int32 ParameterIndex) const;
	
	/** Return the metadata associated to a parameter. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	FMutableParamUIMetadata GetParameterUIMetadata(const FString& ParamName) const;

	/** Return the metadata associated to an int parameter option. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	FMutableParamUIMetadata GetIntParameterOptionUIMetadata(const FString& ParamName, const FString& OptionName) const;
	
	/** Returns the group type of the given integer parameter */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	ECustomizableObjectGroupType GetIntParameterGroupType(const FString& ParamName) const;

	/** Return the metadata associated to a state. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	FMutableStateUIMetadata GetStateUIMetadata(const FString& StateName) const;

#if WITH_EDITOR
	/** Return the DataTables used by the given parameter and its value (if any). */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	TArray<TSoftObjectPtr<UDataTable>> GetIntParameterOptionDataTable(const FString& ParamName, const FString& OptionName);
#endif
	
private:
	/** Textures marked as low priority will generate defaulted resident mips (if texture streaming is enabled).
	  * Generating defaulted resident mips greatly reduce initial generation times. */
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	TArray<FName> LowPriorityTextures;

	// Customizable Object Population data start ------------------------------------------------------
	/** Array to store the selected Population Class tags for this Customizable Object */
	UPROPERTY()
	TArray<FString> CustomizableObjectClassTags;
	
	/** Array to store all the Population Class tags */
	UPROPERTY()
	TArray<FString> PopulationClassTags;

	/** Map of parameters available for the Customizable Object and their tags */
	UPROPERTY()
	TMap<FString, FParameterTags> CustomizableObjectParametersTags;
	// Customizable Object Population data end --------------------------------------------------------

#if WITH_EDITORONLY_DATA
	/** True if this object references a parent object. This is used basically to exclude this object
	  * from cooking. This is actually derived from the source graph object node pointing to another
	  * object or not, but it needs to be cached here because the source graph is not always available.
	  * For old objects this may be false even if they are child objects until they are resaved, but 
	  * that is the conservative case and shouldn't cause a problem. */
	UPROPERTY()
	bool bIsChildObject = false;
#endif
	
public:
#if WITH_EDITORONLY_DATA
	FPostCompileDelegate& GetPostCompileDelegate();
#endif

	/** Create a new instance of this object. The instance parameters will be initialized with the object default values. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	UCustomizableObjectInstance* CreateInstance();

	const UCustomizableObjectPrivate* GetPrivate() const;

	UCustomizableObjectPrivate* GetPrivate();

	/** Check if the CustomizableObject asset has been compiled. This will always be true in a packaged game, but it could be false in the editor. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	bool IsCompiled() const;

	int32 GetNumLODs() const;

	bool IsChildObject() const;

private:
	/** BulkData that stores all in-game resources used by Mutable when generating instances.
	  * Only valid in packaged builds */
	UPROPERTY()
	TObjectPtr<UCustomizableObjectBulk> BulkData;

	UPROPERTY()
	TObjectPtr<UCustomizableObjectPrivate> Private;
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "Input/Reply.h"
#endif
