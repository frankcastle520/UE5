// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/TaskGraphFwd.h"
#include "MuCO/CustomizableObjectInstanceDescriptor.h"
#include "Templates/SubclassOf.h"
#include "Math/RandomStream.h"

#include "CustomizableObjectInstance.generated.h"

namespace mu
{
	class Image;
}

class UCustomizableObjectSystemPrivate;
class USkeletalMesh;
class AActor;
class FProperty;
class UAnimInstance;
class UCustomizableObject;
class UTexture2D;
class FUpdateContextPrivate;
class UCustomizableObjectInstanceUsage;
class UCustomizableObjectExtension;
class UMaterialInterface;
struct FFrame;
struct FGameplayTagContainer;
struct FPropertyChangedEvent;
struct FTexturePlatformData;
struct FMutableModelImageProperties;



/**
 * Represents what kind of saving procedure was performed to save the package
 */
UENUM()
enum class EPackageSaveResolutionType : uint8
{
	None = 0,

	/** The package got saved as a new file. */
	NewFile,

	/** The package was already present on disk so the old package was deleted and a new one was saved on its place */
	Overriden,

	/** Error type : An override was required but due to an error or lack of user permission it could not be done. */
	UnableToOverride
};


/**
 * Data structure that exposes the path to a baked package and also what type of save was performed (an override, a standard save with a new file...)
 */
USTRUCT(BlueprintType, Blueprintable)
struct FBakedResourceData
{
	GENERATED_BODY()

	/**
	 * The way the package represented by this object was saved onto disk.
	 */
	UPROPERTY(BlueprintReadOnly, Category = CustomizableObjectInstanceBaker)
	EPackageSaveResolutionType SaveType = EPackageSaveResolutionType::None;

	/**
	 * The path used by the saved package.
	 */
	UPROPERTY(BlueprintReadOnly, Category = CustomizableObjectInstanceBaker)
	FString AssetPath;
};


/**
 * Structure returned as output of the baking operation. May contain a filled collection of FBakedResourceData objects and also the success end state of the
 * baking operation. 
 */
USTRUCT(BlueprintType, Blueprintable)
struct FCustomizableObjectInstanceBakeOutput
{
	GENERATED_BODY()

	/**
	 * Success state for the baking operation. True for success and false for failure.
	 */
	UPROPERTY(BlueprintReadOnly, Category = CustomizableObjectInstanceBaker)
	bool bWasBakeSuccessful = false;

	/**
	 * Collection of FBakedResourceData representing all saved packages during the baking operation. It may be empty if the operation failed.
	 */
	UPROPERTY(BlueprintReadOnly, Category = CustomizableObjectInstanceBaker)
	TArray<FBakedResourceData> SavedPackages;
};


DECLARE_DYNAMIC_DELEGATE_OneParam(FBakeOperationCompletedDelegate, const FCustomizableObjectInstanceBakeOutput, BakeOperationOutput);


/**
 * Configuration data structure designed to serve as variable container for the customizable object instance baking methods.
 */
USTRUCT(BlueprintType, Blueprintable)
struct FBakingConfiguration
{
	GENERATED_BODY()

	/**
	 * The path where to save the baked resources. EX /Game/MyBakingTest
	 */
	UPROPERTY(BlueprintReadWrite, Blueprintable, Category = CustomizableObjectInstanceBaker)
	FString OutputPath = TEXT("/Game");
	
	/**
	 * The name to be used as base (prefix) during the naming of the exported resources
	 */
	UPROPERTY(BlueprintReadWrite, Blueprintable, Category = CustomizableObjectInstanceBaker)
	FString OutputFilesBaseName;

	/**
	 * Determines if we want a full or partial export
	 */
	UPROPERTY(BlueprintReadWrite, Blueprintable, Category = CustomizableObjectInstanceBaker)
	bool bExportAllResourcesOnBake = false;
	
	/**
	 *  Determines if we want (or not) to generate constant material instances for each of the material instances found in the mutable instance
	 */
	UPROPERTY(BlueprintReadWrite, Blueprintable, Category = CustomizableObjectInstanceBaker)
	bool bGenerateConstantMaterialInstancesOnBake = false;
	
	/**
	 * Flag that determines if we should override already exported files or if we should not. If we encounter files to override and we have not permission to override them then the baking operation will fail.
	 */
	UPROPERTY(BlueprintReadWrite, Blueprintable, Category = CustomizableObjectInstanceBaker)
	bool bAllowOverridingOfFiles = false;

	/**
	 * Callback executed once the baking operation gets completed. It will return the end success state and also some data about the assets saved.
	 */
	UPROPERTY(BlueprintReadWrite, Blueprintable, Category = CustomizableObjectInstanceBaker)
	FBakeOperationCompletedDelegate OnBakeOperationCompletedCallback;
};


// Priority for the mutable update queue, Low is the normal distance-based priority, High is normally used for discards and Mid for LOD downgrades
enum class EQueuePriorityType : uint8 { High, Med, Med_Low, Low };

/** Result of all the checks just before beginning an update. */
enum class EUpdateRequired : uint8
{
	NoUpdate, // No work required.
	Update, // Normal update.
	Discard // Discard instead of update.
};


/** Instance Update Result. */
UENUM(BlueprintType)
enum class EUpdateResult : uint8
{
	Success, // Update finished without issues.
	Warning, // Generic warning. Update finished but with warnings.
	
	Error, // Generic error.
	ErrorOptimized, // The update was skipped since its result would have been the same as the current customization.
	ErrorReplaced, // The update was replaced by a newer update request.
	ErrorDiscarded, // The update was not finished since due to the LOD management discarding the data.
	Error16BitBoneIndex // The update finish unsuccessfully due to Instance not supporting 16 Bit Bone Indexing required by the Engine.
};


/** Indicates the status of the generated Skeletal Mesh. */
enum class ESkeletalMeshStatus : uint8
{
	NotGenerated, // Set only when loading the Instance for the first time or after compiling. Any generation, successful or not, can not end up in this state.
	Success, // Generated successfully.
	Error // Generated with errors.
};


/** Instance Update Context.
 * Used to avoid changing the delegate signature in the future.  */
USTRUCT(BlueprintType)
struct FUpdateContext
{	
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category=UpdateResult)
	EUpdateResult UpdateResult = EUpdateResult::Success;
};


DECLARE_DYNAMIC_DELEGATE_OneParam(FInstanceUpdateDelegate, const FUpdateContext&, Result);
DECLARE_MULTICAST_DELEGATE_OneParam(FInstanceUpdateNativeDelegate, const FUpdateContext&);

/* When creating new delegates use the following conventions:
 *
 * - All delegates must be multicast.
 * - If the delegate is exposed to the API create both, dynamic and native versions (non-dynamic).
 * - Dynamic delegates should not be transient. Use the native version if you do not want it to be saved.
 * - Native delegates names should end with "NativeDelegate".
 * - Dynamic delegates broadcast before native delegates. */

/** Broadcast when an Instance update has completed.
 * Notice that Mutable internally can also start an Instance update. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FObjectInstanceUpdatedDelegate, UCustomizableObjectInstance*, Instance);
DECLARE_MULTICAST_DELEGATE_OneParam(FObjectInstanceUpdatedNativeDelegate, UCustomizableObjectInstance*);

DECLARE_DELEGATE_OneParam(FProjectorStateChangedDelegate, FString);

DECLARE_DYNAMIC_DELEGATE_TwoParams(FEachComponentAnimInstanceClassDelegate, FName, SlotIndex, TSubclassOf<UAnimInstance>, AnimInstClass);

DECLARE_DELEGATE_TwoParams(FEachComponentAnimInstanceClassNativeDelegate, FName /*SlotIndex*/, TSubclassOf<UAnimInstance> /*AnimInstClass*/);


UCLASS( Blueprintable, BlueprintType, HideCategories=(CustomizableObjectInstance) )
class CUSTOMIZABLEOBJECT_API UCustomizableObjectInstance : public UObject
{
	GENERATED_BODY()

	// Friends
	friend UCustomizableInstancePrivate;
	friend FMutableUpdateCandidate;

public:
	UCustomizableObjectInstance();
	
	/** Broadcast when the Customizable Object Instance is updated. */
	UPROPERTY(Transient, BlueprintAssignable, Category = CustomizableObjectInstance)
	FObjectInstanceUpdatedDelegate UpdatedDelegate;

	/** Broadcast when the Customizable Object Instance is updated. */
	FObjectInstanceUpdatedNativeDelegate UpdatedNativeDelegate;

	// UObject interface.
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange( const FProperty* InProperty ) const override;
	virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent) override;
#endif //WITH_EDITOR

	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	virtual bool IsReadyForFinishDestroy() override;
	virtual FString GetDesc() override;
	virtual bool IsEditorOnly() const override;
	virtual void PostInitProperties() override;

	/** Set the CustomizableObject this instance will be generated from. 
	  * It is usually not necessary to call this since instances are already generated from a CustomizableObject. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetObject(UCustomizableObject* InObject);

	/** Get the CustomizableObject that this is an instance of. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	UCustomizableObject* GetCustomizableObject() const;

	/** Return true if the parameter relevancy will be updated when this instance is generated. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	bool GetBuildParameterRelevancy() const;

	/** Set the flag that controls if parameter relevancy will be updated when this instance is generated. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetBuildParameterRelevancy(bool Value);

	/** Return the name of the current CustomizableObject state this is instance is set to. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	FString GetCurrentState() const;

	/** Set the CustomizableObject state that this instance will be generated into. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetCurrentState(const FString& StateName);

	// DEPRECATED
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	USkeletalMesh* GetSkeletalMesh(int32 ComponentIndex = 0) const;

	/** Given a Mesh Component name, return its generated Skeletal Mesh. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	USkeletalMesh* GetComponentMeshSkeletalMesh(const FName& ComponentName) const;
	
	/** Return true if a skeletal mesh has been generated for any component of this instance. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	bool HasAnySkeletalMesh() const;
	
	/** Return true if the instance has any parameters. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	bool HasAnyParameters() const;
	
	/** Set random values to the parameters. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetRandomValues();

	/** Set random values to the parameters using a stream. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetRandomValuesFromStream(const FRandomStream& InStream);

	/** Sets a parameter to its default value */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetDefaultValue(const FString& ParamName);

	/** Set all parameters to their default value. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetDefaultValues();
	
	/** Returns the AssetUserData that was gathered from all the constituent mesh parts during the last update. 
	  * It requires that the CustomizableObject had the bEnableAssetUserDataMerge set to true during compilation. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	TSet<UAssetUserData*> GetMergedAssetUserData(int32 ComponentIndex) const;
	
	/** Return true if the instance is not locked and if it's compiled. */
	bool CanUpdateInstance() const;

	/** Generate the instance with the current parameters and update all the components Skeletal Meshes asynchronously. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void UpdateSkeletalMeshAsync(bool bIgnoreCloseDist = false, bool bForceHighPriority = false);
		
	/** Generate the instance with the current parameters and update all the components Skeletal Meshes asynchronously.
	  * Callback will be called once the update finishes, even if it fails. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void UpdateSkeletalMeshAsyncResult(FInstanceUpdateDelegate Callback, bool bIgnoreCloseDist = false, bool bForceHighPriority = false);

	void UpdateSkeletalMeshAsyncResult(FInstanceUpdateNativeDelegate Callback, bool bIgnoreCloseDist = false, bool bForceHighPriority = false);

	/** Clones the instance creating a new identical transient instance. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	UCustomizableObjectInstance* Clone();

	/** Clones the instance creating a new identical static instance with the given Outer. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	UCustomizableObjectInstance* CloneStatic(UObject* Outer);

	/** Copy parameters from the given Instance. */
	void CopyParametersFromInstance(UCustomizableObjectInstance* Instance);

	/** Immediately destroy the Mutable Core Live Update Instance attached to this (if exists). */
	void DestroyLiveUpdateInstance();

	/** Return true if changing the parameter would affect the Instance given its current generation. */
	bool IsParameterRelevant(int32 ParameterIndex) const;

	/** Return true if the given parameter has any effect in the current object state, and considering the current values of the other parameters. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	bool IsParameterRelevant(const FString& ParamName) const;

	/** Return true if the parameter has changed but the Instance has not yet been updated. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	bool IsParameterDirty(const FString& ParamName, int32 RangeIndex = -1) const;
	
	/** For multidimensional parameters, return the number of dimensions that the given projector parameter supports. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	int32 GetProjectorValueRange(const FString& ParamName) const;

	/** For multidimensional parameters, return the number of dimensions that the given int parameter supports. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	int32 GetIntValueRange(const FString& ParamName) const;

	/** For multidimensional parameters, return the number of dimensions that the given float parameter supports. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	int32 GetFloatValueRange(const FString& ParamName) const;

	/** For multidimensional parameters, return the number of dimensions that the given texture parameter supports. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	int32 GetTextureValueRange(const FString& ParamName) const;

	/** Return the name of the option currently set in the given parameter. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	const FString& GetIntParameterSelectedOption(const FString& ParamName, int32 RangeIndex = -1) const;

	/** Set the currently selected option value for the given parameter, by parameter index and option name. */
	void SetIntParameterSelectedOption(int32 IntParamIndex, const FString& SelectedOption, int32 RangeIndex = -1);

	/** Set the currently selected option value for the given parameter, by parameter name and option name. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetIntParameterSelectedOption(const FString& ParamName, const FString& SelectedOptionName, int32 RangeIndex = -1);

	/** Gets the value of a float parameter with name "FloatParamName". */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	float GetFloatParameterSelectedOption(const FString& FloatParamName, int32 RangeIndex = -1) const;

	/** Sets the float value "FloatValue" of a float parameter with index "FloatParamIndex". */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetFloatParameterSelectedOption(const FString& FloatParamName, float FloatValue, int32 RangeIndex = -1);
	
	/** Gets the value of a texture parameter with name "TextureParamName". */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	FName GetTextureParameterSelectedOption(const FString& TextureParamName, int32 RangeIndex = -1) const;

	/** Sets the texture value "TextureValue" of a texture parameter with index "TextureParamIndex". */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetTextureParameterSelectedOption(const FString& TextureParamName, const FString& TextureValue, int32 RangeIndex = -1);

	/** Gets the value of a color parameter with name "ColorParamName". */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	FLinearColor GetColorParameterSelectedOption(const FString& ColorParamName) const;

	/** Sets the color value "ColorValue" of a color parameter with index "ColorParamIndex". */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetColorParameterSelectedOption(const FString& ColorParamName, const FLinearColor& ColorValue);

	/** Gets the bool value "BoolValue" of a bool parameter with name "BoolParamName". */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	bool GetBoolParameterSelectedOption(const FString& BoolParamName) const;

	/** Sets the bool value "BoolValue" of a bool parameter with name "BoolParamName". */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetBoolParameterSelectedOption(const FString& BoolParamName, bool BoolValue);

	/** Sets the vector value "VectorValue" of a vector parameter with index "VectorParamIndex". */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetVectorParameterSelectedOption(const FString& VectorParamName, const FLinearColor& VectorValue);

	/** Gets the value of a transform parameter with name "TransformParamName". */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	FTransform GetTransformParameterSelectedOption(const FString& TransformParamName) const;

	/** Sets the transform value "TransformValue" of a transform parameter with name "TransformParamName". */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetTransformParameterSelectedOption(const FString& TransformParamName, const FTransform& TransformValue);
	
	
	/** Sets the projector values of a projector parameter with index "ProjectorParamIndex". */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetProjectorValue(const FString& ProjectorParamName,
		const FVector& OutPos, const FVector& OutDirection, const FVector& OutUp, const FVector& OutScale,
		float OutAngle,
		int32 RangeIndex = -1);

	/** Set only the projector position keeping the rest of values. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetProjectorPosition(const FString& ProjectorParamName, const FVector& Pos, int32 RangeIndex = -1);
	
	/** Set only the projector direction vector keeping the rest of values. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetProjectorDirection(const FString& ProjectorParamName, const FVector& Direction, int32 RangeIndex = -1);
	
	/** Set only the projector up vector keeping the rest of values. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetProjectorUp(const FString& ProjectorParamName, const FVector& Up, int32 RangeIndex = -1);

	/** Set only the projector scale keeping the rest of values. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetProjectorScale(const FString& ProjectorParamName, const FVector& Scale, int32 RangeIndex = -1);

	/** Set only the cylindrical projector angle keeping the rest of values. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetProjectorAngle(const FString& ProjectorParamName, float Angle, int32 RangeIndex = -1);
	
	/** Get the projector values of a projector parameter with index "ProjectorParamIndex". */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void GetProjectorValue(const FString& ProjectorParamName,
		FVector& OutPos, FVector& OutDirection, FVector& OutUp, FVector& OutScale,
		float& OutAngle, ECustomizableObjectProjectorType& OutType,
		int32 RangeIndex = -1) const;

	void GetProjectorValueF(const FString& ProjectorParamName,
		FVector3f& Pos, FVector3f& Direction, FVector3f& Up, FVector3f& Scale,
		float& Angle, ECustomizableObjectProjectorType& Type,
		int32 RangeIndex = -1) const;

	/** Get the current projector position for the parameter with the given name. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	FVector GetProjectorPosition(const FString & ParamName, int32 RangeIndex = -1) const;

	/** Get the current projector direction vector for the parameter with the given name. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	FVector GetProjectorDirection(const FString & ParamName, int32 RangeIndex = -1) const;

	/** Get the current projector up vector for the parameter with the given name. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	FVector GetProjectorUp(const FString & ParamName, int32 RangeIndex = -1) const;

	/** Get the current projector scale for the parameter with the given name. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	FVector GetProjectorScale(const FString & ParamName, int32 RangeIndex = -1) const;

	/** Get the current cylindrical projector angle for the parameter with the given name. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	float GetProjectorAngle(const FString& ParamName, int32 RangeIndex = -1) const;

	/** Get the current projector type for the parameter with the given name. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	ECustomizableObjectProjectorType GetProjectorParameterType(const FString& ParamName, int32 RangeIndex = -1) const;

	/** Get the current projector for the parameter with the given name. */
	FCustomizableObjectProjector GetProjector(const FString& ParamName, int32 RangeIndex) const;
	
	/** Finds in IntParameters a parameter with name ParamName, returns the index if found, -1 otherwise. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	int32 FindIntParameterNameIndex(const FString& ParamName) const;

	/** Finds in FloatParameters a parameter with name ParamName, returns the index if found, -1 otherwise. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	int32 FindFloatParameterNameIndex(const FString& ParamName) const;

	/** Finds in BoolParameters a parameter with name ParamName, returns the index if found, -1 otherwise. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	int32 FindBoolParameterNameIndex(const FString& ParamName) const;

	/** Finds in VectorParameters a parameter with name ParamName, returns the index if found, -1 otherwise. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	int32 FindVectorParameterNameIndex(const FString& ParamName) const;

	/** Finds in ProjectorParameters a parameter with name ParamName, returns the index if found, -1 otherwise. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	int32 FindProjectorParameterNameIndex(const FString& ParamName) const;

	/** Increases the range of values of the integer with ParamName and returns the index of the new integer value, -1 otherwise.
	  * The added value is initialized with the first integer option and is the last one of the range. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	int32 AddValueToIntRange(const FString& ParamName);

	/** Increases the range of values of the float with ParamName, returns the index of the new float value, -1 otherwise.
	  * The added value is initialized with 0.5f and is the last one of the range. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	int32 AddValueToFloatRange(const FString& ParamName);

	/** Increases the range of values of the projector with ParamName, returns the index of the new projector value, -1 otherwise.
	  * The added value is initialized with the default projector as set up in the editor and is the last one of the range. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	int32 AddValueToProjectorRange(const FString& ParamName);

	/** Remove the RangeIndex element of the integer range of values from the parameter ParamName. If RangeValue is -1 removes the last of the integer range of values.
		Returns the index of the last valid integer, -1 if no values left. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	int32 RemoveValueFromIntRange(const FString& ParamName, int32 RangeIndex = -1);

	/** Remove the RangeIndex element of the float range of values from the parameter ParamName. If RangeValue is -1 removes the last of the float range of values.
		Returns the index of the last valid float, -1 if no values left. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	int32 RemoveValueFromFloatRange(const FString& ParamName, int32 RangeIndex = -1);

	/** Remove the RangeIndex element of the projector range of values from the parameter ParamName. If RangeValue is -1 removes the last of the projector range of values.
		Returns the index of the last valid projector, -1 if no values left. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	int32 RemoveValueFromProjectorRange(const FString& ParamName, int32 RangeIndex = -1);

	// ------------------------------------------------------------
	// Multilayer Projectors
	// ------------------------------------------------------------
	
	// Layers

	/** See FMultilayerProjector::NumLayers. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	int32 MultilayerProjectorNumLayers(const FName& ProjectorParamName) const;

	/** See FMultilayerProjector::CreateLayer. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void MultilayerProjectorCreateLayer(const FName& ProjectorParamName, int32 Index);

	/** See FMultilayerProjector::RemoveLayerAt. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void MultilayerProjectorRemoveLayerAt(const FName& ProjectorParamName, int32 Index);

	/** See FMultilayerProjector::GetLayer. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	FMultilayerProjectorLayer MultilayerProjectorGetLayer(const FName& ProjectorParamName, int32 Index) const;

	/** See FMultilayerProjector::UpdateLayer. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void MultilayerProjectorUpdateLayer(const FName& ProjectorParamName, int32 Index, const FMultilayerProjectorLayer& Layer);
	
	// ------------------------------------------------------------
	
	/** Returns the animation BP for the parameter component and slot, gathered from all the meshes that compose this instance. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	TSubclassOf<UAnimInstance> GetAnimBP(int32 ComponentIndex, const FName& Slot) const;

	/** Return the list of tags for this instance. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	const FGameplayTagContainer& GetAnimationGameplayTags() const;
	
	/** Execute a delegate for each animation instance involved in this customizable object instance. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void ForEachAnimInstance(int32 ComponentIndex, FEachComponentAnimInstanceClassDelegate Delegate) const;

	void ForEachAnimInstance(int32 ComponentIndex, FEachComponentAnimInstanceClassNativeDelegate Delegate) const;

	/** Check if the given UAnimInstance class requires to be fixed up. */
	bool AnimInstanceNeedsFixup(TSubclassOf<UAnimInstance> AnimInstance) const;

	/** Fix the given UAnimInstance instance. */
	void AnimInstanceFixup(UAnimInstance* AnimInstance) const;

	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	FInstancedStruct GetExtensionInstanceData(const UCustomizableObjectExtension* Extension) const;
	
	/** See FCustomizableObjectInstanceDescriptor::SaveDescriptor. */
	void SaveDescriptor(FArchive &CustomizableObjectDescriptor, bool bUseCompactDescriptor);

	/** See FCustomizableObjectInstanceDescriptor::LoadDescriptor. */
	void LoadDescriptor(FArchive &CustomizableObjectDescriptor);

	/** Enable physics asset replacement so that generated skeletal meshes have the merged physics assets of their skeletal mesh parts and reference mesh. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void SetReplacePhysicsAssets(bool bReplaceEnabled);

	/** Enables the reuse of all possible textures when the instance is updated without any changes in geometry or state (the first update after creation doesn't reuse any)
	  * It will only work if the textures aren't compressed, so set the instance to a Mutable state with texture compression disabled
	  * WARNING! If texture reuse is enabled, do NOT keep external references to the textures of the instance. The instance owns the textures. */
	void SetReuseInstanceTextures(bool bTextureReuseEnabled);
	
	/** If enabled, low-priority textures will generate resident mipmaps too. */
	void SetForceGenerateResidentMips(bool bForceGenerateResidentMips);

	const TArray<TObjectPtr<UMaterialInterface>>* GetOverrideMaterials(int32 ComponentIndex) const;
	
	// The following methods should only be used in an LOD management class
	void SetIsBeingUsedByComponentInPlay(bool bIsUsedByComponent);
	bool GetIsBeingUsedByComponentInPlay() const;
	void SetIsDiscardedBecauseOfTooManyInstances(bool bIsDiscarded);
	bool GetIsDiscardedBecauseOfTooManyInstances() const;
	void SetIsPlayerOrNearIt(bool NewValue);
	float GetMinSquareDistToPlayer() const;
	void SetMinSquareDistToPlayer(float NewValue);

	/** Return the number of components that are defined in the CO, unrelated to this instance parameters. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	int32 GetNumComponents() const;

	/** Return the min LOD that will be used in the next Instance update. */
	int32 GetMinLODToLoad() const;
	
	/** Return the min LOD this Instance is using. */
	int32 GetCurrentMinLOD() const;

	/** Sets an array of LODs to generate per component. Mutable will generate those plus the currently generated LODs (if any).
	  * Requires mutable.EnableOnlyGenerateRequestedLODs and CurrentInstanceLODManagement->IsOnlyGenerateRequestedLODLevelsEnabled() to be true.
	  * @param InMinLOD - MinLOD to generate.
	  * @param InMaxLOD - MaxLOD to generate - DEPRECATED.
	  * @param InRequestedLODsPerComponent - Array with bitmasks of requested LODs per component with range from [0 .. CO->GetComponentCount()].
	  * @param InOutRequestedUpdates - Map from Instance to Update data that stores a request for the Instance to be updated, which will be either processed or discarded by priority (to be rerequested the next tick) */
	void SetRequestedLODs(int32 InMinLOD, int32 InMaxLOD, const TArray<uint16>& InRequestedLODsPerComponent, FMutableInstanceUpdateMap& InOutRequestedUpdates);

	const TArray<uint16>& GetRequestedLODsPerComponent() const;

#if WITH_EDITOR
	/**
	 * Performs the baking of the instance resources in an async fashion. Bind yourself to the callback present in InBakingConfiguration to get notified in case it fails
	 * @param InBakingConfiguration The configuration to be using for the baking operation
	 */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstanceBaking)
	void Bake(const FBakingConfiguration& InBakingConfiguration);
#endif
	
	UCustomizableInstancePrivate* GetPrivate() const;

private:
	UPROPERTY()
	FCustomizableObjectInstanceDescriptor Descriptor;

	UPROPERTY()
	TObjectPtr<UCustomizableInstancePrivate> PrivateData;

public:
#if WITH_EDITORONLY_DATA
	/** Textures which can used as values in Texture Parameters. */
	UPROPERTY(EditAnywhere, Category = TextureParameter)
	TArray<TObjectPtr<UTexture2D>> TextureParameterDeclarations;
#endif

private:
	// Deprecated properties	
	UPROPERTY()
	TObjectPtr<UCustomizableObject> CustomizableObject_DEPRECATED;
	
	UPROPERTY()
	TArray<FCustomizableObjectBoolParameterValue> BoolParameters_DEPRECATED;

	UPROPERTY()
	TArray<FCustomizableObjectIntParameterValue> IntParameters_DEPRECATED;

	UPROPERTY()
	TArray<FCustomizableObjectFloatParameterValue> FloatParameters_DEPRECATED;

	UPROPERTY()																																						
	TArray<FCustomizableObjectTextureParameterValue> TextureParameters_DEPRECATED;

	UPROPERTY()
	TArray<FCustomizableObjectVectorParameterValue> VectorParameters_DEPRECATED;

	UPROPERTY()
	TArray<FCustomizableObjectProjectorParameterValue> ProjectorParameters_DEPRECATED;
	
	bool bBuildParameterRelevancy_DEPRECATED = false;
};


#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "Async/TaskGraphInterfaces.h"
#include "MuCO/CustomizableObjectParameterTypeDefinitions.h"
#endif
