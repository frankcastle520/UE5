// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCO/MultilayerProjector.h"
#include "MuCO/CustomizableObjectParameterTypeDefinitions.h"
#include "Math/RandomStream.h"

#include "CustomizableObjectInstanceDescriptor.generated.h"

class UTexture2D;
enum class ECustomizableObjectProjectorType : uint8;

class FArchive;
class UCustomizableInstancePrivate;
class UCustomizableObject;
class UCustomizableObjectInstance;
class FDescriptorHash;
class FMutableUpdateCandidate;

typedef TMap<const UCustomizableObjectInstance*, FMutableUpdateCandidate> FMutableInstanceUpdateMap;

namespace mu
{
	class Parameters;
	
	template<typename Type>
	class Ptr;
}


/** Set of parameters + state that defines a CustomizableObjectInstance.
 *
 * This object has the same parameters + state interface as UCustomizableObjectInstance.
 * UCustomizableObjectInstance must share the same interface. Any public methods added here should also end up in the Instance. */
USTRUCT()
struct CUSTOMIZABLEOBJECT_API FCustomizableObjectInstanceDescriptor
{
	GENERATED_BODY()

	FCustomizableObjectInstanceDescriptor() = default;
	
	explicit FCustomizableObjectInstanceDescriptor(UCustomizableObject& Object);

	/** Serialize this object. 
	 *
	 * Backwards compatibility is not guaranteed.
 	 * Multilayer Projectors not supported.
	 *
  	 * @param bUseCompactDescriptor If true it assumes the compiled objects are the same on both ends of the serialisation */
	void SaveDescriptor(FArchive &Ar, bool bUseCompactDescriptor);

	/** Deserialize this object. Does not support Multilayer Projectors! */

	/** Deserialize this object.
     *
	 * Backwards compatibility is not guaranteed.
	 * Multilayer Projectors not supported */
	void LoadDescriptor(FArchive &Ar);

	// Could return nullptr in some rare situations, so check first
	UCustomizableObject* GetCustomizableObject() const;

	void SetCustomizableObject(UCustomizableObject* InCustomizableObject);
	
	bool GetBuildParameterRelevancy() const;
	
	void SetBuildParameterRelevancy(bool Value);
	
	/** Update all parameters to be up to date with the Mutable Core parameters. */
	void ReloadParameters();
    
	int32 GetMinLod() const;

	void SetMinLod(int32 InMinLOD);

	int32 GetMaxLod() const { return MAX_int32; }; // DEPRECATED

	void SetMaxLod(int32 InMaxLOD) {}; // DEPRECATED

	void SetRequestedLODLevels(const TArray<uint16>& InRequestedLODLevels);

	const TArray<uint16>& GetRequestedLODLevels() const;

	// ------------------------------------------------------------
	// Parameters
	// ------------------------------------------------------------

	const TArray<FCustomizableObjectBoolParameterValue>& GetBoolParameters() const;
	
	const TArray<FCustomizableObjectIntParameterValue>& GetIntParameters() const;

	const TArray<FCustomizableObjectFloatParameterValue>& GetFloatParameters() const;
	
	const TArray<FCustomizableObjectTextureParameterValue>& GetTextureParameters() const;

	const TArray<FCustomizableObjectVectorParameterValue>& GetVectorParameters() const;

	const TArray<FCustomizableObjectProjectorParameterValue>& GetProjectorParameters() const;
	
	const TArray<FCustomizableObjectTransformParameterValue>& GetTransformParameters() const;
	
	/** Return true if there are any parameters. */
	bool HasAnyParameters() const;

	/** Gets the value of the int parameter with name "ParamName". */
	const FString& GetIntParameterSelectedOption(const FString& ParamName, int32 RangeIndex = -1) const;

	/** Sets the selected option of an int parameter by the option's name. */
	void SetIntParameterSelectedOption(int32 IntParamIndex, const FString& SelectedOption, int32 RangeIndex = -1);

	/** Sets the selected option of an int parameter, by the option's name */
	void SetIntParameterSelectedOption(const FString& ParamName, const FString& SelectedOptionName, int32 RangeIndex = -1);

	/** Gets the value of a float parameter with name "FloatParamName". */
	float GetFloatParameterSelectedOption(const FString& FloatParamName, int32 RangeIndex = -1) const;

	/** Sets the float value "FloatValue" of a float parameter with index "FloatParamIndex". */
	void SetFloatParameterSelectedOption(const FString& FloatParamName, float FloatValue, int32 RangeIndex = -1);

	/** Gets the value of a texture parameter with name "TextureParamName". */
	FName GetTextureParameterSelectedOption(const FString& TextureParamName, int32 RangeIndex) const;

	/** Sets the texture value "TextureValue" of a texture parameter with index "TextureParamIndex". */
	void SetTextureParameterSelectedOption(const FString& TextureParamName, const FString& TextureValue, int32 RangeIndex);
	
	/** Gets the value of a color parameter with name "ColorParamName". */
	FLinearColor GetColorParameterSelectedOption(const FString& ColorParamName) const;

	/** Sets the color value "ColorValue" of a color parameter with index "ColorParamIndex". */
	void SetColorParameterSelectedOption(const FString& ColorParamName, const FLinearColor& ColorValue);

	/** Gets the value of a transform parameter with name "TransformParamName". */
	FTransform GetTransformParameterSelectedOption(const FString& TransformParamName) const;

	/** Sets the transform value "TransformValue" of a transform parameter with name "TransformParamName". */
	void SetTransformParameterSelectedOption(const FString& TransformParamName, const FTransform& TransformValue);

	/** Gets the value of the bool parameter with name "BoolParamName". */
	bool GetBoolParameterSelectedOption(const FString& BoolParamName) const;

	/** Sets the bool value "BoolValue" of a bool parameter with name "BoolParamName". */
	void SetBoolParameterSelectedOption(const FString& BoolParamName, bool BoolValue);

	/** Sets the vector value "VectorValue" of a bool parameter with index "VectorParamIndex". */
	void SetVectorParameterSelectedOption(const FString& VectorParamName, const FLinearColor& VectorValue);

	/** Sets the projector values of a projector parameter with index "ProjectorParamIndex". */
	void SetProjectorValue(const FString& ProjectorParamName,
		const FVector& Pos, const FVector& Direction, const FVector& Up, const FVector& Scale,
		float Angle,
		int32 RangeIndex = -1);

	/** Set only the projector position. */
	void SetProjectorPosition(const FString& ProjectorParamName, const FVector& Pos, int32 RangeIndex = -1);

	/** Set only the projector direction. */
	void SetProjectorDirection(const FString& ProjectorParamName, const FVector& Direction, int32 RangeIndex = -1);
	
	/** Set only the projector up vector. */
	void SetProjectorUp(const FString& ProjectorParamName, const FVector& Up, int32 RangeIndex = -1);

	/** Set only the projector scale. */
	void SetProjectorScale(const FString& ProjectorParamName, const FVector& Scale, int32 RangeIndex = -1);

	/** Set only the cylindrical projector angle. */
	void SetProjectorAngle(const FString& ProjectorParamName, float Angle, int32 RangeIndex = -1);
	
	/** Get the projector values of a projector parameter with index "ProjectorParamIndex". */
	void GetProjectorValue(const FString& ProjectorParamName,
		FVector& OutPos, FVector& OutDirection, FVector& OutUp, FVector& OutScale,
		float& OutAngle, ECustomizableObjectProjectorType& OutType,
		int32 RangeIndex = -1) const;

	/** Float version. See GetProjectorValue. */
	void GetProjectorValueF(const FString& ProjectorParamName,
		FVector3f& OutPos, FVector3f& OutDirection, FVector3f& OutUp, FVector3f& OutScale,
		float& OutAngle, ECustomizableObjectProjectorType& OutType,
		int32 RangeIndex = -1) const;

	/** Get the current projector position for the parameter with the given name. */
	FVector GetProjectorPosition(const FString & ParamName, int32 RangeIndex = -1) const;

	/** Get the current projector direction vector for the parameter with the given name. */
	FVector GetProjectorDirection(const FString & ParamName, int32 RangeIndex = -1) const;

	/** Get the current projector up vector for the parameter with the given name. */
	FVector GetProjectorUp(const FString & ParamName, int32 RangeIndex = -1) const;

	/** Get the current projector scale for the parameter with the given name. */
	FVector GetProjectorScale(const FString & ParamName, int32 RangeIndex = -1) const;

	/** Get the current cylindrical projector angle for the parameter with the given name. */
	float GetProjectorAngle(const FString& ParamName, int32 RangeIndex = -1) const;

	/** Get the current projector type for the parameter with the given name. */
	ECustomizableObjectProjectorType GetProjectorParameterType(const FString& ParamName, int32 RangeIndex = -1) const;

	/** Get the current projector for the parameter with the given name. */
	FCustomizableObjectProjector GetProjector(const FString& ParamName, int32 RangeIndex) const;
	
	/** Finds the parameter with name ParamName in the array of its type, returns the index if found, INDEX_NONE otherwise. */
	int32 FindTypedParameterIndex(const FString& ParamName, EMutableParameterType Type) const;

	// Parameter Ranges

	/** Gets the range of values of the projector with ParamName, returns -1 if the parameter does not exist. */
	int32 GetProjectorValueRange(const FString& ParamName) const;

	/** Gets the range of values of the int with ParamName, returns -1 if the parameter does not exist. */
	int32 GetIntValueRange(const FString& ParamName) const;

	/** Gets the range of values of the float with ParamName, returns -1 if the parameter does not exist. */
	int32 GetFloatValueRange(const FString& ParamName) const;

	/** Gets the range of values of the texture with ParamName, returns -1 if the parameter does not exist. */
	int32 GetTextureValueRange(const FString& ParamName) const;

	/** Increases the range of values of the integer with ParamName, returns the index of the new integer value, -1 otherwise.
	 * The added value is initialized with the first integer option and is the last one of the range. */
	int32 AddValueToIntRange(const FString& ParamName);

	/** Increases the range of values of the float with ParamName, returns the index of the new float value, -1 otherwise.
	 * The added value is initialized with 0.5f and is the last one of the range. */
	int32 AddValueToFloatRange(const FString& ParamName);

	/** Increases the range of values of the float with ParamName, returns the index of the new float value, -1 otherwise. 
	 * The added value is not initialized. */
	int32 AddValueToTextureRange(const FString& ParamName);

	/** Increases the range of values of the projector with ParamName, returns the index of the new projector value, -1 otherwise.
	 * The added value is initialized with the default projector as set up in the editor and is the last one of the range. */
	int32 AddValueToProjectorRange(const FString& ParamName);

	/** Remove the RangeIndex element of the integer range of values from the parameter ParamName. If Range index is -1, removes the last element.
		Returns the index of the last valid integer, -1 if no values left. */
	int32 RemoveValueFromIntRange(const FString& ParamName, int32 RangeIndex = -1);

	/** Remove the RangeIndex element of the float range of values from the parameter ParamName. If Range index is -1, removes the last element.
		Returns the index of the last valid float, -1 if no values left. */
	int32 RemoveValueFromFloatRange(const FString& ParamName, int32 RangeIndex = -1);

	/** Remove the last of the texture range of values from the parameter ParamName, returns the index of the last valid float, -1 if no values left. */
	int32 RemoveValueFromTextureRange(const FString& ParamName);

	/** Remove the RangeIndex element of the texture range of values from the parameter ParamName, returns the index of the last valid float, -1 if no values left. */
	int32 RemoveValueFromTextureRange(const FString& ParamName, int32 RangeIndex);

	/** Remove the RangeIndex element of the projector range of values from the parameter ParamName. If Range index is -1, removes the last element.
		Returns the index of the last valid projector, -1 if no values left.
	*/
	int32 RemoveValueFromProjectorRange(const FString& ParamName, int32 RangeIndex = -1);

	// ------------------------------------------------------------
   	// States
   	// ------------------------------------------------------------

	/** Get the current optimization state. */
	int32 GetState() const;

	/** Get the current optimization state. */	
	FString GetCurrentState() const;

	/** Set the current optimization state. */
	void SetState(int32 InState);

	/** Set the current optimization state. */
	void SetCurrentState(const FString& StateName);

	// ------------------------------------------------------------

	void SetRandomValues();
	
	void SetRandomValuesFromStream(const FRandomStream& InStream);

	void SetDefaultValue(int32 ParamIndex);
	void SetDefaultValues();
	
	// ------------------------------------------------------------
	// Multilayer Projectors
	// ------------------------------------------------------------

	/** @return true if ParamName belongs to a multilayer projector parameter. */
	bool IsMultilayerProjector(const FString& ParamName) const;

	// Layers
	/** @return number of layers of the projector with name ParamName,-1 if invalid or not found. */
	int32 NumProjectorLayers(const FName& ParamName) const;

	/** Creates a new layer for the multilayer projector with name ParamName. */
	void CreateLayer(const FName& ParamName, int32 Index);

	/** Removes the layer at Index from the multilayer projector with name ParamName. */
	void RemoveLayerAt(const FName& ParamName, int32 Index);

	/** @return copy of the layer at Index for the multilayer projector with name ParamName. */
	FMultilayerProjectorLayer GetLayer(const FName& ParamName, int32 Index) const;

	/** Updates the parameters of the layer at Index from the multilayer projector with name ParamName. */
	void UpdateLayer(const FName& ParamName, int32 Index, const FMultilayerProjectorLayer& Layer);

	/** Return a Mutable Core object containing all parameters. */
	mu::Ptr<mu::Parameters> GetParameters() const;

	FString ToString() const;
	
private:

	UPROPERTY()
	TObjectPtr<UCustomizableObject> CustomizableObject = nullptr;

	UPROPERTY()
	TArray<FCustomizableObjectBoolParameterValue> BoolParameters;

	UPROPERTY()
	TArray<FCustomizableObjectIntParameterValue> IntParameters;
	
	UPROPERTY()
	TArray<FCustomizableObjectFloatParameterValue> FloatParameters;
	
	UPROPERTY()
	TArray<FCustomizableObjectTextureParameterValue> TextureParameters;
	
	UPROPERTY()
	TArray<FCustomizableObjectVectorParameterValue> VectorParameters;
	
	UPROPERTY()
	TArray<FCustomizableObjectProjectorParameterValue> ProjectorParameters;

	UPROPERTY()
	TArray<FCustomizableObjectTransformParameterValue> TransformParameters;

	/** Mutable parameters optimization state. Transient UProperty to make it transactable. */
	UPROPERTY(Transient)
	int32 State = 0;
	
	/** If this is set to true, when updating the instance an additional step will be performed to calculate the list of instance parameters that are relevant for the current parameter values. */
	bool bBuildParameterRelevancy = false;

	/** These are the LODs Mutable can generate, they MUST NOT be used in an update (Mutable thread). */
	int32 MinLOD = 0;

	/** Array of RequestedLODs per component to generate, they MUST NOT be used in an update (Mutable thread). */
	TArray<uint16> RequestedLODLevels;
	
	// Friends
	friend FDescriptorHash;
	friend UCustomizableObjectInstance;
	friend UCustomizableInstancePrivate;
	friend FMultilayerProjector;
	friend FMutableUpdateCandidate;
};


#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectParameterTypeDefinitions.h"
#endif
