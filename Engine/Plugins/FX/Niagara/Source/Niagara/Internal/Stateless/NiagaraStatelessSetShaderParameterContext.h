// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraStatelessCommon.h"
#include "ShaderParameterStruct.h"

class FNiagaraStatelessSetShaderParameterContext
{
public:
	UE_NONCOPYABLE(FNiagaraStatelessSetShaderParameterContext);

	explicit FNiagaraStatelessSetShaderParameterContext(const FNiagaraStatelessSpaceTransforms& InSpaceTransforms, TConstArrayView<uint8> InRendererParameterData, TConstArrayView<uint8> InBuiltData, const FShaderParametersMetadata* InShaderParametersMetadata, uint8* InShaderParameters)
		: SpaceTransforms(InSpaceTransforms)
		, RendererParameterData(InRendererParameterData)
		, BuiltData(InBuiltData)
		, ShaderParametersBase(InShaderParameters)
		, ParameterOffset(0)
		, ShaderParametersMetadata(InShaderParametersMetadata)
	{
	}

	const FNiagaraStatelessSpaceTransforms& GetSpaceTransforms() const { return SpaceTransforms; }

	template<typename T>
	T* GetParameterNestedStruct() const
	{
		const uint32 StructOffset = Align(ParameterOffset, TShaderParameterStructTypeInfo<T>::Alignment);
	#if DO_CHECK
		ValidateIncludeStructType(StructOffset, TShaderParameterStructTypeInfo<T>::GetStructMetadata());
	#endif
		ParameterOffset = StructOffset + TShaderParameterStructTypeInfo<T>::GetStructMetadata()->GetSize();
		return reinterpret_cast<T*>(ShaderParametersBase + StructOffset);
	}

	template<typename T>
	const T* ReadBuiltData() const
	{
		const int32 Offset = Align(BuiltDataOffset, alignof(T));
		BuiltDataOffset = Offset + sizeof(T);
		check(BuiltDataOffset <= BuiltData.Num());
		return reinterpret_cast<const T*>(BuiltData.GetData() + Offset);
	}

	template<typename T>
	void GetRendererParameterValue(T& OutValue, int32 Offset, const T& DefaultValue) const
	{
		if (Offset != INDEX_NONE)
		{
			Offset *= sizeof(uint32);
			check(Offset >= 0 && Offset + sizeof(T) <= RendererParameterData.Num());
			FMemory::Memcpy(&OutValue, RendererParameterData.GetData() + Offset, sizeof(T));
		}
		else
		{
			OutValue = DefaultValue;
		}
	}

	void ConvertRangeToScaleBias(const FNiagaraStatelessRangeFloat& Range, float& OutScale, float& OutBias) const { OutScale = Range.GetScale(); GetRendererParameterValue(OutBias, Range.ParameterOffset, Range.Min); }
	void ConvertRangeToScaleBias(const FNiagaraStatelessRangeVector2& Range, FVector2f& OutScale, FVector2f& OutBias) const { OutScale = Range.GetScale(); GetRendererParameterValue(OutBias, Range.ParameterOffset, Range.Min); }
	void ConvertRangeToScaleBias(const FNiagaraStatelessRangeVector3& Range, FVector3f& OutScale, FVector3f& OutBias) const { OutScale = Range.GetScale(); GetRendererParameterValue(OutBias, Range.ParameterOffset, Range.Min); }
	void ConvertRangeToScaleBias(const FNiagaraStatelessRangeColor& Range, FLinearColor& OutScale, FLinearColor& OutBias) const { OutScale = Range.GetScale(); GetRendererParameterValue(OutBias, Range.ParameterOffset, Range.Min); }

	float ConvertRangeToValue(const FNiagaraStatelessRangeFloat& Range) const { float OutValue; GetRendererParameterValue(OutValue, Range.ParameterOffset, Range.Min); return OutValue; }
	FVector2f ConvertRangeToValue(const FNiagaraStatelessRangeVector2& Range) const { FVector2f OutValue; GetRendererParameterValue(OutValue, Range.ParameterOffset, Range.Min); return OutValue; }
	FVector3f ConvertRangeToValue(const FNiagaraStatelessRangeVector3& Range) const { FVector3f OutValue; GetRendererParameterValue(OutValue, Range.ParameterOffset, Range.Min); return OutValue; }
	FLinearColor ConvertRangeToValue(const FNiagaraStatelessRangeColor& Range) const { FLinearColor OutValue; GetRendererParameterValue(OutValue, Range.ParameterOffset, Range.Min); return OutValue; }

protected:
#if DO_CHECK
	void ValidateIncludeStructType(uint32 StructOffset, const FShaderParametersMetadata* StructMetaData) const;
#endif

private:
	const FNiagaraStatelessSpaceTransforms&	SpaceTransforms;
	TConstArrayView<uint8>					RendererParameterData;
	TConstArrayView<uint8>					BuiltData;
	mutable int32							BuiltDataOffset = 0;
	uint8*									ShaderParametersBase = nullptr;
	mutable uint32							ParameterOffset = 0;
	const FShaderParametersMetadata*		ShaderParametersMetadata = nullptr;
};
