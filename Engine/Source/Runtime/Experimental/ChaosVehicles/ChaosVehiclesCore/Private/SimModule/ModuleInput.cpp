// Copyright Epic Games, Inc. All Rights Reserved.

#include "SimModule/ModuleInput.h"

TArray<struct FModuleInputSetup>* FScopedModuleInputInitializer::InitSetupData = nullptr;

DEFINE_LOG_CATEGORY(LogModularInput);

float FModuleInputValue::GetMagnitudeSq() const
{
	switch (GetValueType())
	{
	case EModuleInputValueType::MBoolean:
	case EModuleInputValueType::MAxis1D:
		return Value.X * Value.X;
	case EModuleInputValueType::MAxis2D:
		return Value.SizeSquared2D();
	case EModuleInputValueType::MAxis3D:
		return Value.SizeSquared();
	}

	checkf(false, TEXT("Unsupported value type for module input value!"));
	return 0.f;
}

float FModuleInputValue::GetMagnitude() const
{
	switch (GetValueType())
	{
	case EModuleInputValueType::MBoolean:
	case EModuleInputValueType::MAxis1D:
		return Value.X;
	case EModuleInputValueType::MAxis2D:
		return Value.Size2D();
	case EModuleInputValueType::MAxis3D:
		return Value.Size();
	}

	checkf(false, TEXT("Unsupported value type for module input value!"));
	return 0.f;
}

void FModuleInputValue::SetMagnitude(float NewSize)
{
	switch (GetValueType())
	{
	case EModuleInputValueType::MBoolean:
	case EModuleInputValueType::MAxis1D:
		Value.X = NewSize;
		break;
	case EModuleInputValueType::MAxis2D:
		Value.GetSafeNormal2D()* NewSize;
		break;
	case EModuleInputValueType::MAxis3D:
		Value.GetSafeNormal()* NewSize;
		break;
	default:
		checkf(false, TEXT("Unsupported value type for module input value!"));
		break;
	}
}

void FModuleInputValue::Serialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	// #TODO: send only value changes/deltas
	Ar << ValueType;

	switch (GetValueType())
	{
	case EModuleInputValueType::MBoolean:
		{
			uint8 State = (Value.X != 0);
			Ar << State;
			if (Ar.IsLoading())
			{
				Value.X = State ? 1.0f : 0.0f;
			}
		}
		break;
	case EModuleInputValueType::MAxis1D:
		ModularQuantize::SerializeFixedFloat<1, 16>(Value.X, Ar);
		break;
	case EModuleInputValueType::MAxis2D:
		ModularQuantize::SerializeFixedFloat<1, 16>(Value.X, Ar);
		ModularQuantize::SerializeFixedFloat<1, 16>(Value.Y, Ar);
		break;
	case EModuleInputValueType::MAxis3D:
		Value.NetSerialize(Ar, Map, bOutSuccess);
		break;
	default:
		checkf(false, TEXT("Unsupported value type for module input value!"));
		break;
	}
	bOutSuccess = true;
}

void FModuleInputValue::Merge(const FModuleInputValue& From)
{
	switch (GetValueType())
	{
	case EModuleInputValueType::MBoolean:
		// ensure we capture edges of digitial inputs by taking largest absolute value
		if (FMath::Abs(From.Value.X) >= FMath::Abs(Value.X))
		{
			Value.X = From.Value.X;
		}
		break;
	case EModuleInputValueType::MAxis1D:
	case EModuleInputValueType::MAxis2D:
	case EModuleInputValueType::MAxis3D:
		// use the last known value for analog inputs, or should we take the highest value of the pair here also?
		Value = From.Value;			
		break;
	default:
		checkf(false, TEXT("Unsupported value type for module input value!"));
		break;
	}

}

FString FModuleInputValue::ToString() const
{
	switch (GetValueType())
	{
	case EModuleInputValueType::MBoolean:
		return FString(IsNonZero() ? TEXT("true") : TEXT("false"));
	case EModuleInputValueType::MAxis1D:
		return FString::Printf(TEXT("%3.3f"), Value.X);
	case EModuleInputValueType::MAxis2D:
		return FString::Printf(TEXT("X=%3.3f Y=%3.3f"), Value.X, Value.Y);
	case EModuleInputValueType::MAxis3D:
		return FString::Printf(TEXT("X=%3.3f Y=%3.3f Z=%3.3f"), Value.X, Value.Y, Value.Z);
	}

	checkf(false, TEXT("Unsupported value type for module input value!"));
	return FString{};
}



FModuleInputValue UDefaultModularVehicleInputModifier::InterpInputValue(float DeltaTime, const FModuleInputValue& CurrentValue, const FModuleInputValue& NewValue) const
{
	const FModuleInputValue DeltaValue = NewValue - CurrentValue;

	// We are "rising" when DeltaValue has the same sign as CurrentValue (i.e. delta causes an absolute magnitude gain)
	// OR we were at 0 before, and our delta is no longer 0.
	const bool bRising = ((DeltaValue.GetMagnitude() > 0.0f) == (CurrentValue.GetMagnitude() > 0.0f)) ||
		((DeltaValue.GetMagnitude() != 0.f) && (CurrentValue.GetMagnitude() == 0.f));

	const float MaxMagnitude = DeltaTime * (bRising ? RiseRate : FallRate);

	const FModuleInputValue ClampedDeltaValue = FModuleInputValue::Clamp(DeltaValue, -MaxMagnitude, MaxMagnitude);

	return CurrentValue + ClampedDeltaValue;
}

float UDefaultModularVehicleInputModifier::CalcControlFunction(float InputValue)
{
	// #TODO: Reinstate ?
	check(false);
	return 0.0f;
	// user defined curve

	//// else use option from drop down list
	//switch (InputCurveFunction)
	//{
	//case EFunctionType::CustomCurve:
	//{
	//	if (UserCurve.GetRichCurveConst() && !UserCurve.GetRichCurveConst()->IsEmpty())
	//	{
	//		float Output = FMath::Clamp(UserCurve.GetRichCurveConst()->Eval(FMath::Abs(InputValue)), 0.0f, 1.0f);
	//		return (InputValue < 0.f) ? -Output : Output;
	//	}
	//	else
	//	{
	//		return InputValue;
	//	}
	//}
	//break;
	//case EFunctionType::SquaredFunction:
	//{
	//	return (InputValue < 0.f) ? -InputValue * InputValue : InputValue * InputValue;
	//}
	//break;

	//case EFunctionType::LinearFunction:
	//default:
	//{
	//	return InputValue;
	//}
	//break;

	//}

}


void FModuleInputContainer::Initialize(TArray<FModuleInputSetup>& SetupData, FInputNameMap& NameMapOut)
{
	NameMapOut.Reset();
	InputValues.Reset();

	for (FModuleInputSetup& Setup : SetupData)
	{
		int Index = AddInput(Setup.Type, Setup.InputModifierClass);
		NameMapOut.Add(Setup.Name, Index);
	}
}

void FModuleInputContainer::ZeroValues()
{
	for (int I = 0; I < InputValues.Num(); I++)
	{
		InputValues[I].Reset();
	}
}

void FModuleInputContainer::Serialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bOutSuccess = true;

	uint32 Number = InputValues.Num();
	Ar << Number;
	if (Ar.IsLoading())
	{
		InputValues.Reset(Number);
		for (uint32 I = 0; I < Number; I++)
		{
			FModuleInputValue Value;
			Value.Serialize(Ar, Map, bOutSuccess);
			InputValues.EmplaceAt(I, Value);
		}
	}
	else
	{
		for (uint32 I = 0; I < Number; I++)
		{
			InputValues[I].Serialize(Ar, Map, bOutSuccess);
		}
	}

	return;
}

int FModuleInputContainer::AddInput(EModuleInputValueType Type, TSubclassOf<UDefaultModularVehicleInputModifier>& InputModifierClass)
{
	InputValues.Add(FModuleInputValue(Type, FVector::ZeroVector));
	return InputValues.Num() - 1;
}

void FModuleInputContainer::RemoveAllInputs()
{
	InputValues.Reset();
}

void FModuleInputContainer::Lerp(const FModuleInputContainer& Min, const FModuleInputContainer& Max, float Alpha)
{
	for (int I = 0; I < FMath::Min3(InputValues.Num(), Min.InputValues.Num(), Max.InputValues.Num()); I++)
	{
		InputValues[I].Lerp(Min.InputValues[I], Max.InputValues[I], Alpha);
	}
}

void FModuleInputContainer::Merge(const FModuleInputContainer& From)
{
	int Num = FMath::Min(InputValues.Num(), From.InputValues.Num());
	for (int I = 0; I < Num; I++)
	{
		InputValues[I].Merge(From.InputValues[I]);
	}
}


void FInputInterface::SetValue(const FName& InName, const FModuleInputValue& InValue)
{
	if (ValueContainer.GetNumInputs() > 0)
	{
		if (const int* Index = NameMap.Find(InName))
		{
			ValueContainer.SetValueAtIndex(*Index, InValue);
		}
		else
		{
			UE_LOG(LogModularInput, Warning, TEXT("Trying to set the value of an undefined control input %s"), *InName.ToString());
		}
	}
}

void FInputInterface::MergeValue(const FName& InName, const FModuleInputValue& InValue)
{
	if (ValueContainer.GetNumInputs() > 0)
	{
		if (const int* Index = NameMap.Find(InName))
		{
			ValueContainer.MergeValueAtIndex(*Index, InValue);
		}
		else
		{
			UE_LOG(LogModularInput, Warning, TEXT("Trying to set the value of an undefined control input %s"), *InName.ToString());
		}
	}
}


FModuleInputValue FInputInterface::GetValue(const FName& InName) const
{
	if (ValueContainer.GetNumInputs() > 0)
	{
		if (const int* Index = NameMap.Find(InName))
		{
			return ValueContainer.GetValueAtIndex(*Index);
		}
		else
		{
			UE_LOG(LogModularInput, Warning, TEXT("Trying to get the value of an undefined control input %s"), *InName.ToString());
		}
	}

	return FModuleInputValue(EModuleInputValueType::MBoolean, FVector::ZeroVector);
}

float FInputInterface::GetMagnitude(const FName& InName) const
{
	if (ValueContainer.GetNumInputs() > 0)
	{
		if (const int* Index = NameMap.Find(InName))
		{
			return ValueContainer.GetValueAtIndex(*Index).GetMagnitude();
		}
	}

	return 0.0f;
}

bool FInputInterface::InputsNonZero() const
{
	for (int I = 0; I < ValueContainer.GetNumInputs(); I++)
	{
		if (ValueContainer.GetValueAtIndex(I).IsNonZero())
		{
			return true;
		}
	}

	return false;
}