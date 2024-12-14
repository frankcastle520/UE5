// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeRDGConstant.h"

#include "Helper/NNERuntimeRDGLogHelper.h"
#include "Helper/NNERuntimeRDGOperatorHelper.h"
#include "NNEAttributeTensor.h"
#include "NNEHlslShadersLog.h"
#include "NNERuntimeRDGHlslHelper.h"
#include "NNETensor.h"
#include "NNETypes.h"

namespace UE::NNERuntimeRDG::Private::Hlsl
{
	/**
	 * Constant operator implementation
	 */
	class FConstant : public FOperatorHlsl
	{
	public:

		FConstant() {}
		virtual ~FConstant() = default;

	private:

		FNNEAttributeValue Attribute;


	public:

		virtual int PrepareOutputs(TConstArrayView<NNE::Internal::FTensorRef> InputTensors, TArrayView<NNE::Internal::FTensorRef> OutputTensors) override
		{
			using NNE::FTensorShape;
			using NNE::Internal::FTensor;
			using NNE::Internal::FAttributeTensor;

			check(InputTensors.Num() == 0);
			check(OutputTensors.Num() == 1);

			FTensor& Output = *OutputTensors[0];

			switch (Attribute.GetType())
			{
				case ENNEAttributeDataType::Float:
				{
					if (Output.GetDataType() != ENNETensorDataType::Float)
					{
						UE_LOG(LogNNERuntimeRDGHlsl, Warning, TEXT("Constant: Output data type %s does not match constant type of float"), 
							   					*LogHelper::GetTensorDataTypeName(Output.GetDataType()));
						return -1;
					}
					const float Value = Attribute.GetValue<float>();
					const TArray<uint32, TInlineAllocator<FTensorShape::MaxRank>> Shape;
					const TConstArrayView<float> Data = MakeArrayView(&Value, 1);
					Output.SetShape(FTensorShape::Make(Shape));
					Output.SetPreparedData(Data);
				} break;
				case ENNEAttributeDataType::FloatArray:
				{
					if (Output.GetDataType() != ENNETensorDataType::Float)
					{
						UE_LOG(LogNNERuntimeRDGHlsl, Warning, TEXT("Constant: Output data type %s does not match constant type of float"), 
							   					*LogHelper::GetTensorDataTypeName(Output.GetDataType()));
						return -1;
					}
					const TArray<float> Values = Attribute.GetValue<TArray<float>>();
					TArray<uint32, TInlineAllocator<FTensorShape::MaxRank>> Shape;
					Shape.Add(Values.Num());
					Output.SetShape(FTensorShape::Make(Shape));
					Output.SetPreparedData(MakeArrayView(Values));
				} break;
				case ENNEAttributeDataType::Tensor:
				{
					const FAttributeTensor AttributeTensor = Attribute.GetValue<FAttributeTensor>();
					if (Output.GetDataType() != AttributeTensor.GetDataType())
					{
						UE_LOG(LogNNERuntimeRDGHlsl, Warning, TEXT("Constant: Output data type %s does not match constant tensor data type %s"), 
							   					*LogHelper::GetTensorDataTypeName(Output.GetDataType()), 
												*LogHelper::GetTensorDataTypeName(AttributeTensor.GetDataType()));
						return -1;
					}
					AttributeTensor.FillFTensorWithShapeAndData(Output);
				} break;
				default:
					check(false);
					break;
			}
			check(OutputTensors[0]->IsConstant());
			return 0;
		};

		virtual bool Initialize(TConstArrayView<NNE::FTensorDesc> InputTensorDescs, TConstArrayView<NNE::FTensorDesc> OutputTensorDescs, const NNE::FAttributeMap& Attributes) override
		{
			check(InputTensorDescs.Num() == 0);
			check(OutputTensorDescs.Num() == 1);
			check(Attributes.Num() == 1);

			Attribute = Attributes.GetAttributeValue(0);

			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TConstArrayView<FTensorRDGRef> InputTensors, TConstArrayView<FTensorRDGRef> OutputTensors) override
		{
			checkf(false, TEXT("Dispatch should never be called, since we have a constant output"));
		}
	};

	bool ValidateConstantOperator(const NNE::FAttributeMap& AttributeMap, TConstArrayView<ENNETensorDataType> InputTypes, TConstArrayView<NNE::FSymbolicTensorShape> InputShapes)
	{
		bool bIsValid = true;

		FAttributeValidator AttributeValidator;
		AttributeValidator.AddOptional(TEXT("value"), ENNEAttributeDataType::Tensor);
		AttributeValidator.AddOptional(TEXT("value_float"), ENNEAttributeDataType::Float);
		AttributeValidator.AddOptional(TEXT("value_floats"), ENNEAttributeDataType::FloatArray);
		if (!AttributeValidator.Validate(AttributeMap))
		{
			return false;
		}

		if (AttributeMap.Num() != 1)
		{
			UE_LOG(LogNNERuntimeRDGHlsl, Warning, TEXT("Constant: Operator requires exacly one attribute, but '%d' attributes found."), AttributeMap.Num());
			return false;
		}

		FInputValidator InputValidator;
		bIsValid &= InputValidator.Validate(InputTypes);

		return bIsValid;
	}

	FOperatorHlsl* CreateConstantOperator()
	{
		return new FConstant();
	}

	bool RegisterConstantOperator(FOperatorRegistryHlsl& Registry)
	{
		// Note: support of a particular version is partial with respect to tensor data types (only the most typical ones are usually supported).
		#define OP(Version) \
		Registry.OpAdd({{TEXT("Constant"), TEXT("Onnx")}, Version}, CreateConstantOperator, ValidateConstantOperator);

		OP(9)
		OP(11)
		OP(12)
		OP(13)
		OP(19)
		OP(21)

		#undef OP
		return true;
	}

} // UE::NNERuntimeRDG::Private::Hlsl
