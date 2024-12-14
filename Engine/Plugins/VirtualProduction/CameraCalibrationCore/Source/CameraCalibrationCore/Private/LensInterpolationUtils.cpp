// Copyright Epic Games, Inc. All Rights Reserved.


#include "LensInterpolationUtils.h"

#include "LensData.h"
#include "Curves/CurveEvaluation.h"
#include "Curves/RichCurve.h"


//Property interpolation utils largely inspired from livelink interp code
namespace LensInterpolationUtils
{
	void InterpolateProperty(FProperty* Property, float InBlendWeight, const void* InFrameDataA, const void* InFrameDataB, void* OutFrameData);

	template<typename Type>
	void Interpolate(const FStructProperty* StructProperty, float InBlendWeight, const void* DataA, const void* DataB, void* DataResult)
	{
		const Type* ValuePtrA = StructProperty->ContainerPtrToValuePtr<Type>(DataA);
		const Type* ValuePtrB = StructProperty->ContainerPtrToValuePtr<Type>(DataB);
		Type* ValuePtrResult = StructProperty->ContainerPtrToValuePtr<Type>(DataResult);

		Type ValueResult = BlendValue(InBlendWeight, *ValuePtrA, *ValuePtrB);
		StructProperty->CopySingleValue(ValuePtrResult, &ValueResult);
	}

	void Interpolate(const UStruct* InStruct, float InBlendWeight, const void* InFrameDataA, const void* InFrameDataB, void* OutFrameData)
	{
		for (TFieldIterator<FProperty> Itt(InStruct); Itt; ++Itt)
		{
			FProperty* Property = *Itt;

			if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				//ArrayProps have an ArrayDim of 1 but just to be sure...
				for (int32 DimIndex = 0; DimIndex < ArrayProperty->ArrayDim; ++DimIndex)
				{
					const void* Data0 = ArrayProperty->ContainerPtrToValuePtr<const void>(InFrameDataA, DimIndex);
					const void* Data1 = ArrayProperty->ContainerPtrToValuePtr<const void>(InFrameDataB, DimIndex);
					void* DataResult = ArrayProperty->ContainerPtrToValuePtr<void>(OutFrameData, DimIndex);

					FScriptArrayHelper ArrayHelperA(ArrayProperty, Data0);
					FScriptArrayHelper ArrayHelperB(ArrayProperty, Data1);
					FScriptArrayHelper ArrayHelperResult(ArrayProperty, DataResult);

					const int32 MinValue = FMath::Min(ArrayHelperA.Num(), ArrayHelperB.Num());
					ArrayHelperResult.Resize(MinValue);

					for (int32 ArrayIndex = 0; ArrayIndex < MinValue; ++ArrayIndex)
					{
						InterpolateProperty(ArrayProperty->Inner, InBlendWeight, ArrayHelperA.GetRawPtr(ArrayIndex), ArrayHelperB.GetRawPtr(ArrayIndex), ArrayHelperResult.GetRawPtr(ArrayIndex));
					}
				}
			}
			else if (Property->ArrayDim > 1)
			{
				for (int32 DimIndex = 0; DimIndex < Property->ArrayDim; ++DimIndex)
				{
					const void* Data0 = Property->ContainerPtrToValuePtr<const void>(InFrameDataA, DimIndex);
					const void* Data1 = Property->ContainerPtrToValuePtr<const void>(InFrameDataB, DimIndex);
					void* DataResult = Property->ContainerPtrToValuePtr<void>(OutFrameData, DimIndex);

					InterpolateProperty(Property, InBlendWeight, Data0, Data1, DataResult);
				}
			}
			else
			{
				InterpolateProperty(Property, InBlendWeight, InFrameDataA, InFrameDataB, OutFrameData);
			}
		}
	}

	void InterpolateProperty(FProperty* Property, float InBlendWeight, const void* InDataA, const void* InDataB, void* OutData)
	{
		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct->GetFName() == NAME_Vector)
			{
				Interpolate<FVector>(StructProperty, InBlendWeight, InDataA, InDataB, OutData);
			}
			else if (StructProperty->Struct->GetFName() == NAME_Vector2D)
			{
				Interpolate<FVector2D>(StructProperty, InBlendWeight, InDataA, InDataB, OutData);
			}
			else if (StructProperty->Struct->GetFName() == NAME_Vector4)
			{
				Interpolate<FVector4>(StructProperty, InBlendWeight, InDataA, InDataB, OutData);
			}
			else if (StructProperty->Struct->GetFName() == NAME_Rotator)
			{
				Interpolate<FRotator>(StructProperty, InBlendWeight, InDataA, InDataB, OutData);
			}
			else if (StructProperty->Struct->GetFName() == NAME_Quat)
			{
				Interpolate<FQuat>(StructProperty, InBlendWeight, InDataA, InDataB, OutData);
			}
			else
			{
				const void* Data0 = StructProperty->ContainerPtrToValuePtr<const void>(InDataA);
				const void* Data1 = StructProperty->ContainerPtrToValuePtr<const void>(InDataB);
				void* DataResult = StructProperty->ContainerPtrToValuePtr<void>(OutData);
				Interpolate(StructProperty->Struct, InBlendWeight, Data0, Data1, DataResult);
			}
		}
		else if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			if (NumericProperty->IsFloatingPoint())
			{
				const void* Data0 = NumericProperty->ContainerPtrToValuePtr<const void>(InDataA);
				const double Value0 = NumericProperty->GetFloatingPointPropertyValue(Data0);
				const void* Data1 = NumericProperty->ContainerPtrToValuePtr<const void>(InDataB);
				const double Value1 = NumericProperty->GetFloatingPointPropertyValue(Data1);

				const double ValueResult = BlendValue(InBlendWeight, Value0, Value1);

				void* DataResult = NumericProperty->ContainerPtrToValuePtr<void>(OutData);
				NumericProperty->SetFloatingPointPropertyValue(DataResult, ValueResult);
			}
			else if (NumericProperty->IsInteger() && !NumericProperty->IsEnum())
			{
				const void* Data0 = NumericProperty->ContainerPtrToValuePtr<const void>(InDataA);
				const int64 Value0 = NumericProperty->GetSignedIntPropertyValue(Data0);
				const void* Data1 = NumericProperty->ContainerPtrToValuePtr<const void>(InDataB);
				const int64 Value1 = NumericProperty->GetSignedIntPropertyValue(Data1);

				const int64 ValueResult = BlendValue(InBlendWeight, Value0, Value1);

				void* DataResult = NumericProperty->ContainerPtrToValuePtr<void>(OutData);
				NumericProperty->SetIntPropertyValue(DataResult, ValueResult);
			}
		}
	}

	float GetBlendFactor(float InValue, float ValueA, float ValueB)
	{
		//Keep input in range
		InValue = FMath::Clamp(InValue, ValueA, ValueB);

		const float Divider = ValueB - ValueA;
		if (!FMath::IsNearlyZero(Divider))
		{
			return (InValue - ValueA) / Divider;
		}
		else
		{
			return 1.0f;
		}
	}

	float FTangentBezierCurve::Eval(float InX)
	{
		if (FMath::IsNearlyEqual(X0, X1))
		{
			return Y0;
		}

		constexpr float OneThird = 1.0f / 3.0f;

		const float DeltaX = X1 - X0;
		const float DeltaY = Y1 - Y0;
		const float Alpha = (InX - X0) / DeltaX;
		const float TangentScale = DeltaY / DeltaX;
		
		const float P0 = Y0;
		const float P3 = Y1;
		const float P1 = P0 + (Tangent0 * TangentScale * DeltaX * OneThird);
		const float P2 = P3 - (Tangent1 * TangentScale * DeltaX * OneThird);
		return UE::Curves::BezierInterp(P0, P1, P2, P3, Alpha);
	}

	float FCoonsPatch::Blend(const FVector2D& InPoint)
	{
		float Alpha = 0.0;
		if (!FMath::IsNearlyEqual(X0, X1))
		{
			Alpha = (InPoint.X - X0) / (X1 - X0);
		}
		
		float Beta = 0.0;
		if (!FMath::IsNearlyEqual(Y0, Y1))
		{
			Beta = (InPoint.Y - Y0) / (Y1 - Y0);
		}
		
		// In degenerate cases (such as only having 3 defined corners), the four curves' corners may not match.
		// Simply use the average of the two possible values in that case; while this doesn't match the strict
		// definition for a Coons patch, it allows the patch to give a somewhat usable value for such edge cases
		const float P00 = X0Curve.Eval(X0) + Y0Curve.Eval(Y0);
		const float P01 = X0Curve.Eval(X1) + Y1Curve.Eval(Y0);
		const float P10 = X1Curve.Eval(X0) + Y0Curve.Eval(Y1);
		const float P11 = X1Curve.Eval(X1) + Y1Curve.Eval(Y1);
			
		const float Lx = FMath::Lerp(X0Curve.Eval(InPoint.X), X1Curve.Eval(InPoint.X), Beta);
		const float Ly = FMath::Lerp(Y0Curve.Eval(InPoint.Y), Y1Curve.Eval(InPoint.Y), Alpha);
		const float B = FMath::BiLerp(P00, P01, P10, P11, Alpha, Beta);
		
		return Lx + Ly - 0.5 * B;
	}

	FTangentBezierCoonsPatch::FTangentBezierCoonsPatch(float X0, float X1, float Y0, float Y1, float XTangents[4], float YTangents[4])
		: Corners {
			FPatchCorner(X0, Y0, XTangents[0], YTangents[0], 0.0),
			FPatchCorner(X1, Y0, XTangents[1], YTangents[1], 0.0),
			FPatchCorner(X1, Y1, XTangents[2], YTangents[2], 0.0),
			FPatchCorner(X0, Y1, XTangents[3], YTangents[3], 0.0)}
	{ }

	float FTangentBezierCoonsPatch::Blend(const FVector2D& InPoint)
	{
		
		float Alpha = 0.0;
		if (!FMath::IsNearlyEqual(Corners[0].X, Corners[1].X))
		{
			Alpha = (InPoint.X - Corners[0].X) / (Corners[1].X - Corners[0].X);
		}
		
		float Beta = 0.0;
		if (!FMath::IsNearlyEqual(Corners[0].Y, Corners[3].Y))
		{
			Beta = (InPoint.Y - Corners[0].Y) / (Corners[3].Y - Corners[0].Y);
		}

		FTangentBezierCurve X0Curve = FTangentBezierCurve(Corners[0].X, Corners[1].X, Corners[0].Value, Corners[1].Value, Corners[0].TangentX, Corners[1].TangentX);
		FTangentBezierCurve X1Curve = FTangentBezierCurve(Corners[3].X, Corners[2].X, Corners[3].Value, Corners[2].Value, Corners[3].TangentX, Corners[2].TangentX);
		FTangentBezierCurve Y0Curve = FTangentBezierCurve(Corners[0].Y, Corners[3].Y, Corners[0].Value, Corners[3].Value, Corners[0].TangentY, Corners[3].TangentY);
		FTangentBezierCurve Y1Curve = FTangentBezierCurve(Corners[1].Y, Corners[2].Y, Corners[1].Value, Corners[2].Value, Corners[1].TangentY, Corners[2].TangentY);
	
		const float Lx = FMath::Lerp(X0Curve.Eval(InPoint.X), X1Curve.Eval(InPoint.X), Beta);
		const float Ly = FMath::Lerp(Y0Curve.Eval(InPoint.Y), Y1Curve.Eval(InPoint.Y), Alpha);
		const float B = FMath::BiLerp(Corners[0].Value, Corners[1].Value, Corners[3].Value, Corners[2].Value, Alpha, Beta);
		
		return Lx + Ly - B;
	}
}


