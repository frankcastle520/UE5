// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/UnrealString.h"
#include "MaterialShared.h"
#include "RHIFeatureLevel.h"
#include "RHIShaderPlatform.h"
#include "SceneTypes.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryWriter.h"

class FObjectPreSaveContext;
struct FAppendToClassSchemaContext;
struct FMaterialResourceForCooking;

namespace UE::MaterialInterface::Private
{

/** Record the instance-specific cook dependencies (config, hlsl) from the resources for a UMaterialInterface. */
void RecordMaterialDependenciesForCook(FObjectPreSaveContext ObjectSaveContext,
	TConstArrayView<FMaterialResourceForCooking> Resources);

/** Record the cook dependencies that apply to every UMaterialInterface. */
void HashMaterialStaticClassDependenciesForCook(FAppendToClassSchemaContext& Context);

/**
 * A copy of the fields saved from an FMaterialResourceForCooking that includes only the data necessary for
 * calculating non-UObject-based material dependencies. This struct is marshalled to CompactBinary and stored in cook
 * metadata and read at the beginning of incremental cooks to check whether the material's dependencies have changed.
 */
struct FRecordedMaterialResourceForCooking
{
	FMaterialShaderMapId ShaderMapId;
	FMaterialShaderParameters ShaderParameters;
	EShaderPlatform ShaderPlatform;
	/**
	 * We have to handle ExpressionIncludes separately rather than using ShaderMapId.ExpressionIncludesHash,
	 * because we need to record the filename of each expression include to recalculate it.
	 */
	TArray<FString> ExpressionIncludes;

	void Save(FCbWriter& Writer) const;
	bool TryLoad(FCbFieldView Field);
};

} // namespace UE::MaterialInterface::Private

///////////////////////////////////////////////////////
// CompactBinary interface for FRecordedMaterialResourceForCooking
///////////////////////////////////////////////////////

inline FCbWriter& operator<<(FCbWriter& Writer,
	const UE::MaterialInterface::Private::FRecordedMaterialResourceForCooking& Value)
{
	Value.Save(Writer);
	return Writer;
}
bool LoadFromCompactBinary(FCbFieldView Field,
	UE::MaterialInterface::Private::FRecordedMaterialResourceForCooking& OutValue);

///////////////////////////////////////////////////////
// Implementation details for UE_DEFINEINLINE_COMPACTBINARY_ENUM_INT
///////////////////////////////////////////////////////

template <typename IntType>
inline IntType GetViewFieldAsInteger(FCbFieldView FieldView, IntType DefaultValue)
{
	static_assert(sizeof(IntType) != 1, "Not implemented");
}
template <>
inline uint8 GetViewFieldAsInteger(FCbFieldView FieldView, uint8 DefaultValue)
{
	return FieldView.AsUInt8(DefaultValue);
}
template <>
inline uint16 GetViewFieldAsInteger(FCbFieldView FieldView, uint16 DefaultValue)
{
	return FieldView.AsUInt16(DefaultValue);
}

/**
 * Declare and Define inline functions for LoadFromCompactBinary and operator<< for an enum, by converting
 * the enum to an integer.
 */
#define UE_DEFINEINLINE_COMPACTBINARY_ENUM_INT(EnumType, IntType, NumValues, InvalidValue)				\
	inline bool LoadFromCompactBinary(FCbFieldView Field, EnumType& OutValue)							\
	{																									\
		IntType IntValue = GetViewFieldAsInteger<IntType>(Field, static_cast<IntType>(InvalidValue));	\
		if (Field.HasError() || IntValue >= static_cast<IntType>(NumValues))							\
		{																								\
			OutValue = static_cast<EnumType>(InvalidValue);												\
			return false;																				\
		}																								\
		OutValue = static_cast<EnumType>(IntValue);														\
		return true;																					\
	}																									\
	inline FCbWriter& operator<<(FCbWriter& Writer, EnumType Value)										\
	{																									\
		Writer << static_cast<IntType>(Value);															\
		return Writer;																					\
	}

///////////////////////////////////////////////////////
// CompactBinary functions for enums used by FRecordedMaterialResourceForCooking
///////////////////////////////////////////////////////

UE_DEFINEINLINE_COMPACTBINARY_ENUM_INT(EShaderPlatform, uint16, SP_NumPlatforms, SP_NumPlatforms);
UE_DEFINEINLINE_COMPACTBINARY_ENUM_INT(EMaterialQualityLevel::Type, uint8,
	EMaterialQualityLevel::Num, EMaterialQualityLevel::Low);
UE_DEFINEINLINE_COMPACTBINARY_ENUM_INT(ERHIFeatureLevel::Type, uint8,
	ERHIFeatureLevel::Num, ERHIFeatureLevel::ES3_1);


#endif // WITH_EDITOR
