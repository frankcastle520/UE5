// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NNEAttributeDataType.h"
#include "NNEAttributeTensor.h"

template<typename T> struct TNNEAttributeValueTraits
{
	static constexpr ENNEAttributeDataType GetType()
	{
		static_assert(!sizeof(T), "Attribute value trait must be specialized for this type.");
		return ENNEAttributeDataType::None;
	}
};

// Attribute specializations
template<> struct TNNEAttributeValueTraits<float>
{
	static constexpr ENNEAttributeDataType GetType() { return ENNEAttributeDataType::Float; }
};

template<> struct TNNEAttributeValueTraits<int32>
{
	static constexpr ENNEAttributeDataType GetType() { return ENNEAttributeDataType::Int32; }
};

template<> struct TNNEAttributeValueTraits<FString>
{
	static constexpr ENNEAttributeDataType GetType() { return ENNEAttributeDataType::String; }
};

template<> struct TNNEAttributeValueTraits<UE::NNE::Internal::FAttributeTensor>
{
	static constexpr ENNEAttributeDataType GetType() { return ENNEAttributeDataType::Tensor; }
};

template<> struct TNNEAttributeValueTraits<TArray<int32>>
{
	static constexpr ENNEAttributeDataType GetType() { return ENNEAttributeDataType::Int32Array; }
};

template<> struct TNNEAttributeValueTraits<TArray<float>>
{
	static constexpr ENNEAttributeDataType GetType() { return ENNEAttributeDataType::FloatArray; }
};

template<> struct TNNEAttributeValueTraits<TArray<FString>>
{
	static constexpr ENNEAttributeDataType GetType() { return ENNEAttributeDataType::StringArray; }
};

template<> struct TNNEAttributeValueTraits<TArray<UE::NNE::Internal::FAttributeTensor>>
{
	static constexpr ENNEAttributeDataType GetType() { return ENNEAttributeDataType::TensorArray; }
};
