// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/WeakObjectPtr.h"

#if WITH_EDITOR
#include "DerivedDataCachePolicy.h"
#include "DerivedDataCacheKey.h"
#endif

#include "CustomizableObjectCompilerTypes.generated.h"


/** Index of the maximum optimization level when compiling CustomizableObjects */
#define UE_MUTABLE_MAX_OPTIMIZATION			2

class UCustomizableObject;

DECLARE_MULTICAST_DELEGATE(FPostCompileDelegate)

UENUM()
enum class ECustomizableObjectNumBoneInfluences : uint8
{
	// The enum values can be used as the real numeric value of number of bone influences
	Four = 4,
	// 
	Eight = 8,
	//
	Twelve = 12 // This is essentially the same as "Unlimited", but UE ultimately limits to 12
};

//#if WITH_EDITORONLY_DATA

UENUM()
enum class ECustomizableObjectTextureCompression : uint8
{
	// Don't use texture compression
	None = 0,
	// Use Mutable's fast low-quality compression
	Fast,
	// Use Unreal's highest quality compression (100x slower to compress)
	HighQuality
};


struct FCompilationOptions
{
	/** Enum to know what texture compression should be used. This compression is used only in manual compiles in editor.
	 *  When packaging, ECustomizableObjectTextureCompression::HighQuality is always used. */
	ECustomizableObjectTextureCompression TextureCompression = ECustomizableObjectTextureCompression::Fast;

	// From 0 to UE_MUTABLE_MAX_OPTIMIZATION
	int32 OptimizationLevel = UE_MUTABLE_MAX_OPTIMIZATION;

	/** Use the disk to store intermediate compilation data.This slows down the object compilation
	 * but it may be necessary for huge objects.
	 *
	 * This option does not modify the DDC Key.
	 * */
	bool bUseDiskCompilation = false;

	/** High limit of the size in bytes of the packaged data when cooking this object.
	 * This limit is before any pak or filesystem compression. This limit will be broken if a single piece of data is bigger because data is not fragmented for packaging purposes.
	 *
	 * This option does not modify the DDC Key.
	 */
	uint64 PackagedDataBytesLimit = 256 * 1024 * 1024;

	/** High (inclusive) limit of the size in bytes of a data block to be included into the compiled object directly instead of stored in a streamable file.
	 *
	 * This option does not modify the DDC Key.
	 */
	uint64 EmbeddedDataBytesLimit = 1024;

	/** Number of minimum mipmaps that we want to always be available in disk regardless of NumHighResImageLODs. */
	int32 MinDiskMips = 7;

	/** Number of image mipmaps that will be flagged as high-res data (possibly to store separately).
	* This is only used if the total mips in the source image is above the MinDiskMips.
	*/
	int32 NumHighResImageMips = 2;

	// Did we have the extra bones enabled when we compiled?
	ECustomizableObjectNumBoneInfluences CustomizableObjectNumBoneInfluences = ECustomizableObjectNumBoneInfluences::Four;

	// Compiling for cook
	bool bIsCooking = false;

	// This can be set for additional settings
	const ITargetPlatform* TargetPlatform = nullptr;

	// Used to enable the use of real time morph targets.
	bool bRealTimeMorphTargetsEnabled = false;

	// Used to enable the use of clothing.
	bool bClothingEnabled = false;

	// Used to enable 16 bit bone weights
	bool b16BitBoneWeightsEnabled = false;

	// Used to enable skin weight profiles.
	bool bSkinWeightProfilesEnabled = false;

	// Used to enable physics asset merge.
	bool bPhysicsAssetMergeEnabled = false;

	// Used to enable AnimBp override physics mainipualtion.  
	bool bAnimBpPhysicsManipulationEnabled = false;

	// Used to reduce the number of notifications when compiling objects
	bool bSilentCompilation = true;

	// Used to reduce texture size on higher mesh LODs. Only active if LOD strategy is set to Automatic LODs from Mesh
	bool bUseLODAsBias = true;

	/** Force a very big number on the mips to skip during compilation. Useful to debug special cooks of the data. */
	bool bForceLargeLODBias = false;
	int32 DebugBias = 0;

	// Control image tiled generation
	int32 ImageTiling = 0;

	/** If true, gather all game asset references and save them in the Customizable Object. 
	 *
	 * This option does not modify the DDC Key. 
	 */
	bool bGatherReferences = false;

	/** Whether or not the compiler should query a request to load the compiled data from the DDC.
	 *
	 * This option does not modify the DDC Key. 
	 */
	bool bQueryCompiledDatafromDDC = false;

	/** Whether or not the compiler should store the compiled data to the DDC.
	 *
	 * This option does not modify the DDC Key. 
	 */
	bool bStoreCompiledDataInDDC = false;
	
	/** Stores the only option of an Int Param that should be compiled. */
	TMap<FString, FString> ParamNamesToSelectedOptions;

	/** Used to know if the object will be using FByteBulkData.
	 *
	 * This option does not modify the DDC Key. 
	 */
	bool bUseBulkData = false;
};


enum class ECompilationStatePrivate : uint8
{
	None,
	InProgress,
	Completed
};


enum class ECompilationResultPrivate : uint8
{
	Unknown, // Not compiled yet (compilation may be in progress).
	Success, // No errors or warnings.
	Errors, // At least have one error. Can have warnings.
	Warnings, // Only warnings.
};


#if WITH_EDITOR
struct CUSTOMIZABLEOBJECT_API FCompilationRequest
{
	FCompilationRequest(UCustomizableObject& CustomizableObject, bool bAsync);

	UCustomizableObject* GetCustomizableObject();

	FCompilationOptions& GetCompileOptions();

	bool IsAsyncCompilation() const;

	void SetDerivedDataCachePolicy(UE::DerivedData::ECachePolicy InCachePolicy);
	UE::DerivedData::ECachePolicy GetDerivedDataCachePolicy() const;

	void BuildDerivedDataCacheKey();
	UE::DerivedData::FCacheKey GetDerivedDataCacheKey() const;

	void SetCompilationState(ECompilationStatePrivate InState, ECompilationResultPrivate InResult);

	ECompilationStatePrivate GetCompilationState() const;
	ECompilationResultPrivate GetCompilationResult() const;

	TArray<FText>& GetWarnings();
	TArray<FText>& GetErrors();
	
	bool operator==(const FCompilationRequest& Other) const;

private:

	TWeakObjectPtr<UCustomizableObject> CustomizableObject;

	FCompilationOptions Options;

	ECompilationStatePrivate State = ECompilationStatePrivate::None;
	ECompilationResultPrivate Result = ECompilationResultPrivate::Unknown;

	bool bAsync = true;

	UE::DerivedData::ECachePolicy DDCPolicy = UE::DerivedData::ECachePolicy::None;
	UE::DerivedData::FCacheKey DDCKey;
	
	TArray<FText> Warnings;
	TArray<FText> Errors;
};
#endif
