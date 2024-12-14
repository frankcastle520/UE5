// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "NNETensor.h"
#include "NNETypes.h"
#include "Serialization/Archive.h"

namespace UE::NNE::Internal
{
	class FAttributeTensor
	{
	protected:
		ENNETensorDataType	DataType;
		TArray<uint32, TInlineAllocator<FTensorShape::MaxRank>> Shape;
		TArray<uint8>		Data;

	public:

		ENNETensorDataType GetDataType() const
		{
			return DataType;
		}

		void FillFTensorWithShapeAndData(FTensor& Tensor) const
		{
			Tensor.SetShape(FTensorShape::Make(Shape));
			Tensor.SetPreparedData(MakeConstArrayView(Data));
		}

		static FAttributeTensor Make(const FTensorShape& Shape,
									 ENNETensorDataType DataType, 
									 TConstArrayView<uint8> Data)
		{
			uint64 Volume = Shape.Volume();
			check(Volume <= TNumericLimits<uint32>::Max());
			check(Data.NumBytes() == GetTensorDataTypeSizeInBytes(DataType) * Volume);

			FAttributeTensor Tensor;
			Tensor.DataType = DataType;
			Tensor.Shape.Reset();
			Tensor.Shape.Append(Shape.GetData());
			Tensor.Data.Reset();
			Tensor.Data.Append(Data);
			return Tensor;
		}

		friend FArchive& operator<<(FArchive& Ar, FAttributeTensor& Tensor)
		{
			Ar << Tensor.DataType;
			Ar << Tensor.Shape;
			Ar << Tensor.Data;
			return Ar;
		}
	};
} // namespace UE::NNE::Internal
