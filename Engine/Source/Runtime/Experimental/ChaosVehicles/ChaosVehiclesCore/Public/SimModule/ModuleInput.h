// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
//#include "Engine/NetSerialization.h"
#include "Templates/SubclassOf.h"
#include "ModuleInput.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogModularInput, Log, All);


UENUM(BlueprintType)
enum class EModuleInputValueType : uint8
{
	// Value types in increasing size order (used for type promotion)

	MBoolean	UMETA(DisplayName = "Digital (bool)"),
	MAxis1D		UMETA(DisplayName = "Axis1D (float)"),
	MAxis2D		UMETA(DisplayName = "Axis2D (Vector2D)"),
	MAxis3D		UMETA(DisplayName = "Axis3D (Vector)"),
};

/** Input Options */
UENUM(BlueprintType)
enum class EFunctionType : uint8
{
	LinearFunction = 0,
	SquaredFunction,
	CustomCurve
};

namespace ModularQuantize
{
	template<int32 MaxValue, uint32 NumBits>
	struct TCompressedFloatDetails
	{
		// NumBits = 8:
		static constexpr int32 MaxBitValue = (1 << (NumBits - 1)) - 1;  //   0111 1111 - Max abs value we will serialize
		static constexpr int32 Bias = (1 << (NumBits - 1));             //   1000 0000 - Bias to pivot around (in order to support signed values)
		static constexpr int32 SerIntMax = (1 << (NumBits - 0));        // 1 0000 0000 - What we pass into SerializeInt
		static constexpr int32 MaxDelta = (1 << (NumBits - 0)) - 1;     //   1111 1111 - Max delta is
	};

	template<int32 MaxValue, uint32 NumBits, typename T UE_REQUIRES(std::is_floating_point_v<T>&& NumBits < 32)>
	bool ToCompressedFloat(const T InValue, uint32& OutCompressedFloat)
	{
		using Details = ModularQuantize::TCompressedFloatDetails<MaxValue, NumBits>;

		bool clamp = false;
		int64 ScaledValue;
		if (MaxValue > Details::MaxBitValue)
		{
			// We have to scale this down
			const T Scale = T(Details::MaxBitValue) / MaxValue;
			ScaledValue = FMath::TruncToInt(Scale * InValue);
		}
		else
		{
			// We will scale up to get extra precision. But keep is a whole number preserve whole values
			constexpr int32 Scale = Details::MaxBitValue / MaxValue;
			ScaledValue = FMath::RoundToInt(Scale * InValue);
		}

		uint32 Delta = static_cast<uint32>(ScaledValue + Details::Bias);

		if (Delta > Details::MaxDelta)
		{
			clamp = true;
			Delta = static_cast<int32>(Delta) > 0 ? Details::MaxDelta : 0;
		}

		OutCompressedFloat = Delta;

		return !clamp;
	}

	template<int32 MaxValue, uint32 NumBits, typename T UE_REQUIRES(std::is_floating_point_v<T>&& NumBits < 32)>
	bool FromCompressedFloat(const uint32 InCompressed, T& OutValue )
	{
		using Details = ModularQuantize::TCompressedFloatDetails<MaxValue, NumBits>;

		uint32 Delta = InCompressed;
		T UnscaledValue = static_cast<T>(static_cast<int32>(Delta) - Details::Bias);

		if constexpr (MaxValue > Details::MaxBitValue)
		{
			// We have to scale down, scale needs to be a float:
			constexpr T InvScale = MaxValue / (T)Details::MaxBitValue;
			OutValue = UnscaledValue * InvScale;
		}
		else
		{
			constexpr int32 Scale = Details::MaxBitValue / MaxValue;
			constexpr T InvScale = T(1) / (T)Scale;

			OutValue = UnscaledValue * InvScale;
		}

		return true;
	}

	template<int32 MaxValue, uint32 NumBits, typename T UE_REQUIRES(std::is_floating_point_v<T>&& NumBits < 32)>
	bool WriteCompressedFloat(const T Value, FArchive& Ar)
	{
		using Details = ModularQuantize::TCompressedFloatDetails<MaxValue, NumBits>;

		uint32 CompressedValue;
		bool clamp = ModularQuantize::ToCompressedFloat<MaxValue, NumBits>(Value, CompressedValue);

		Ar.SerializeInt(CompressedValue, Details::SerIntMax);

		return !clamp;
	}

	template<int32 MaxValue, uint32 NumBits, typename T UE_REQUIRES(std::is_floating_point_v<T>&& NumBits < 32)>
	bool ReadCompressedFloat(T& Value, FArchive& Ar)
	{
		using Details = ModularQuantize::TCompressedFloatDetails<MaxValue, NumBits>;

		uint32 CompressedValue;
		Ar.SerializeInt(CompressedValue, Details::SerIntMax);

		ModularQuantize::FromCompressedFloat<MaxValue, NumBits>(CompressedValue, Value);

		return true;
	}

	// Required because we seialize quantized vector in seperate parts depending on input type
	template<int32 MaxValue, uint32 NumBits>
	bool SerializeFixedFloat(double& InOutValue, FArchive& Ar)
	{
		if (Ar.IsSaving())
		{
			bool success = true;
			success &= ModularQuantize::WriteCompressedFloat<MaxValue, NumBits>(InOutValue, Ar);
			return success;
		}

		ModularQuantize::ReadCompressedFloat<MaxValue, NumBits>(InOutValue, Ar);
		return true;
	}

	template<int32 MaxValue, uint32 NumBits, typename T UE_REQUIRES(std::is_floating_point_v<T>&& NumBits < 32)>
	void QuantizeValue(T& Value)
	{
		uint32 CompressedValue = 0;
		ModularQuantize::ToCompressedFloat<MaxValue, NumBits>(Value, CompressedValue);
		ModularQuantize::FromCompressedFloat<MaxValue, NumBits>(CompressedValue, Value);
	}

}

USTRUCT(BlueprintType)
struct CHAOSVEHICLESCORE_API FModuleInputValue
{
	GENERATED_BODY()

public:
	using MAxis1D = double; // #TODO: Axis1D (float) double or float? FVector2D and FVector are double
	using MAxis2D = FVector2D;
	using MAxis3D = FVector;

	// Support all relevant default constructors (FModuleInputValue isn't movable)
	FModuleInputValue() = default;
	FModuleInputValue(const FModuleInputValue&) = default;
	FModuleInputValue& operator= (const FModuleInputValue&) = default;

	// Specialized constructors for supported types
	// Converting a value to a different type (e.g. Val = FVector(1, 1, 1); Val = true;) zeroes out any unused components to ensure getters continue to function correctly.
	explicit FModuleInputValue(bool bInValue) : Value(bInValue ? 1.f : 0.f, 0.f, 0.f), ValueType(EModuleInputValueType::MBoolean) {}
	FModuleInputValue(MAxis1D InValue) : Value(InValue, 0.f, 0.f), ValueType(EModuleInputValueType::MAxis1D) {}
	FModuleInputValue(MAxis2D InValue) : Value(InValue.X, InValue.Y, 0.f), ValueType(EModuleInputValueType::MAxis2D) {}
	FModuleInputValue(MAxis3D InValue) : Value(InValue), ValueType(EModuleInputValueType::MAxis3D) {}

	FModuleInputValue ReturnQuantized() const
	{
		FModuleInputValue OutValue = Value;

		switch (ValueType)
		{
			case EModuleInputValueType::MBoolean:
			case EModuleInputValueType::MAxis1D:
				ModularQuantize::QuantizeValue<1, 16>(OutValue.Value.X);
			break;

			case EModuleInputValueType::MAxis2D:
				ModularQuantize::QuantizeValue<1, 16>(OutValue.Value.X);
				ModularQuantize::QuantizeValue<1, 16>(OutValue.Value.Y);
			break;

			case EModuleInputValueType::MAxis3D:
				ModularQuantize::QuantizeValue<1, 16>(OutValue.Value.X);
				ModularQuantize::QuantizeValue<1, 16>(OutValue.Value.Y);
				ModularQuantize::QuantizeValue<1, 16>(OutValue.Value.Z);
			break;

			default:
				checkf(false, TEXT("Unsupported value type for module input value!"));
				break;

		}

		//UE_LOG(LogTemp, Warning, TEXT("Original %f vs Quantized %f"), Value.X, OutValue.Value.X);

		return OutValue;
	}

	// Build a specific type with an arbitrary Axis3D value
	FModuleInputValue(EModuleInputValueType InValueType, MAxis3D InValue) : Value(InValue), ValueType(InValueType)
	{
		// Clear out value components to match type
		switch (ValueType)
		{
		case EModuleInputValueType::MBoolean:
		case EModuleInputValueType::MAxis1D:
			Value.Y = 0.f;
			//[[fallthrough]];
		case EModuleInputValueType::MAxis2D:
			Value.Z = 0.f;
			//[[fallthrough]];
		case EModuleInputValueType::MAxis3D:
		default:
			return;
		}
	}

	// Resets Value without affecting ValueType
	void Reset()
	{
		Value = FVector::ZeroVector;
	}

	FModuleInputValue& operator+=(const FModuleInputValue& Rhs)
	{
		Value += Rhs.Value;
		// Promote value type to largest number of bits.
		ValueType = FMath::Max(ValueType, Rhs.ValueType);
		return *this;
	}

	friend FModuleInputValue operator+(const FModuleInputValue& Lhs, const FModuleInputValue& Rhs)
	{
		FModuleInputValue Result(Lhs);
		Result += Rhs;
		return Result;
	}

	FModuleInputValue& operator-=(const FModuleInputValue& Rhs)
	{
		Value -= Rhs.Value;
		// Promote value type to largest number of bits.
		ValueType = FMath::Max(ValueType, Rhs.ValueType);
		return *this;
	}

	friend FModuleInputValue operator-(const FModuleInputValue& Lhs, const FModuleInputValue& Rhs)
	{
		FModuleInputValue Result(Lhs);
		Result -= Rhs;
		return Result;
	}

	// Scalar operators
	FModuleInputValue& operator*=(float Scalar)
	{
		Value *= Scalar;
		return *this;
	}

	friend FModuleInputValue operator*(const FModuleInputValue& Lhs, const float Rhs)
	{
		FModuleInputValue Result(Lhs);
		Result *= Rhs;
		return Result;
	}

	static FModuleInputValue Clamp(const FModuleInputValue& InValue, const float InMin, const float InMax)
	{
		FModuleInputValue OutValue = InValue;
		float Mag = InValue.GetMagnitude();

		if (Mag < InMin)
		{
			OutValue.SetMagnitude(InMin);
		}
		else if (Mag > InMax)
		{
			OutValue.SetMagnitude(InMax);
		}

		return OutValue;
	}



	template<typename T>
	inline T Get() const { static_assert(sizeof(T) == 0, "Unsupported conversion for type"); }

	// Read only index based value accessor, doesn't care about type. Expect 0 when accessing unused components.
	float operator[](int32 Index) const { return Value[Index]; }

	bool IsNonZero(float Tolerance = KINDA_SMALL_NUMBER) const { return Value.SizeSquared() >= Tolerance * Tolerance; }

	// In-place type conversion
	FModuleInputValue& ConvertToType(EModuleInputValueType Type)
	{
		if (ValueType != Type)
		{
			*this = FModuleInputValue(Type, Value);
		}
		return *this;
	}
	FModuleInputValue& ConvertToType(const FModuleInputValue& Other) { return ConvertToType(Other.GetValueType()); }

	EModuleInputValueType GetValueType() const { return ValueType; }

	float GetMagnitudeSq() const;
	float GetMagnitude() const;

	// Serialize values
	void Serialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

	void Lerp(const FModuleInputValue& Min, const FModuleInputValue& Max, float Alpha)
	{
		Value = FMath::Lerp(Min.Value, Max.Value, Alpha);
	}

	void Merge(const FModuleInputValue& From);

	// Type sensitive debug stringify
	FString ToString() const;

protected:
	void SetMagnitude(float NewSize);

	UPROPERTY()
	FVector Value = FVector::ZeroVector;

	UPROPERTY()
	EModuleInputValueType ValueType = EModuleInputValueType::MBoolean;
};

// Supported getter specializations
template<>
inline bool FModuleInputValue::Get() const
{
	// True if any component is non-zero
	return IsNonZero();
}

template<>
inline FModuleInputValue::MAxis1D FModuleInputValue::Get() const
{
	return Value.X;
}

template<>
inline FModuleInputValue::MAxis2D FModuleInputValue::Get() const
{
	return MAxis2D(Value.X, Value.Y);
}

template<>
inline FModuleInputValue::MAxis3D FModuleInputValue::Get() const
{
	return Value;
}


class CHAOSVEHICLESCORE_API FModuleInputConversion
{
public:

	static bool ToBool(FModuleInputValue& InValue)
	{
		return InValue.Get<bool>();
	}

	static float ToAxis1D(FModuleInputValue& InValue)
	{
		return static_cast<float>(InValue.Get<FModuleInputValue::MAxis1D>());
	}

	static FVector2D ToAxis2D(FModuleInputValue& InValue)
	{
		return InValue.Get<FModuleInputValue::MAxis2D>();
	}

	static FVector ToAxis3D(FModuleInputValue& InValue)
	{
		return InValue.Get<FModuleInputValue::MAxis3D>();
	}

	static FString ToString(FModuleInputValue& ActionValue)
	{
		return ActionValue.ToString();
	}
};


UCLASS(BlueprintType, Blueprintable)
class CHAOSVEHICLESCORE_API UDefaultModularVehicleInputModifier : public UObject
{
	GENERATED_BODY()

public:
	UDefaultModularVehicleInputModifier(const class FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
		, RiseRate(5.0f), FallRate(5.0f), InputCurveFunction(EFunctionType::LinearFunction) { }

	virtual ~UDefaultModularVehicleInputModifier()
	{
	}

	/**
		* Rate at which the input value rises
		*/
	UPROPERTY(EditAnywhere, Category = VehicleInputRate)
	float RiseRate;

	/**
	 * Rate at which the input value falls
	 */
	UPROPERTY(EditAnywhere, Category = VehicleInputRate)
	float FallRate;

	/**
	 * Controller input curve, various predefined options, linear, squared, or user can specify a custom curve function
	 */
	UPROPERTY(EditAnywhere, Category = VehicleInputRate)
	EFunctionType InputCurveFunction;

	/**
	 * Controller input curve - should be a normalized float curve, i.e. time from 0 to 1 and values between 0 and 1
	 * This curve is only sued if the InputCurveFunction above is set to CustomCurve
	 */
	//UPROPERTY(EditAnywhere, Category = VehicleInputRate) #TODO: Reinstate?
	//FRuntimeFloatCurve UserCurve;

	/** Change an output value using max rise and fall rates */
	virtual FModuleInputValue InterpInputValue(float DeltaTime, const FModuleInputValue& CurrentValue, const FModuleInputValue& NewValue) const;
	virtual float CalcControlFunction(float InputValue);
};


USTRUCT(BlueprintType)
struct CHAOSVEHICLESCORE_API FModuleInputSetup
{
	GENERATED_BODY()

	FModuleInputSetup()
	{
		Type = EModuleInputValueType::MBoolean;
	}

	FModuleInputSetup(const FName& InName, const EModuleInputValueType& InType)
		: Name(InName)
		, Type(InType)
	{
	}

	bool operator==(const FModuleInputSetup& Rhs) const
	{
		return (Name == Rhs.Name);
	}

	UPROPERTY(EditAnywhere, Category = VehicleInput)
	FName Name;

	UPROPERTY(EditAnywhere, Category = VehicleInput)
	EModuleInputValueType Type;

	UPROPERTY(EditAnywhere, Category = VehicleInput)
	TSubclassOf<UDefaultModularVehicleInputModifier> InputModifierClass;
};

class CHAOSVEHICLESCORE_API FScopedModuleInputInitializer
{
public:
	FScopedModuleInputInitializer(TArray<struct FModuleInputSetup>& InSetupData)
	{
		InitSetupData = &InSetupData;
	}

	~FScopedModuleInputInitializer()
	{
		InitSetupData = nullptr;
	}

	static bool HasSetup() { return InitSetupData != nullptr; }
	static TArray<struct FModuleInputSetup>* GetSetup() { return InitSetupData; }
	
private:

	static TArray<struct FModuleInputSetup>* InitSetupData;
};


USTRUCT()
struct CHAOSVEHICLESCORE_API FModuleInputContainer
{
	GENERATED_BODY()

public:
	using FInputNameMap = TMap<FName, int>;
	using FInputValues = TArray<FModuleInputValue>;

	FModuleInputContainer()
	{
		if (FScopedModuleInputInitializer::HasSetup())
		{
			FInputNameMap NameMapOut;
			Initialize(*FScopedModuleInputInitializer::GetSetup(), NameMapOut);
		}
	}

	int GetNumInputs() const { return InputValues.Num(); }
	FModuleInputValue GetValueAtIndex(int Index) const { return InputValues[Index]; }
	void SetValueAtIndex(int Index, const FModuleInputValue& InValue) { InputValues[Index] = InValue.ReturnQuantized(); }
	void MergeValueAtIndex(int Index, const FModuleInputValue& InValue) { InputValues[Index].Merge(InValue.ReturnQuantized()); }

	FModuleInputContainer& operator=(const FModuleInputContainer& Other)
	{
		if (&Other != this)
		{
			InputValues.Reset();
			// perform a deep copy
			if (Other.InputValues.Num())
			{
				InputValues = Other.InputValues;
			}
		}
		return *this;
	}

	void Initialize(TArray<FModuleInputSetup>& SetupData, FInputNameMap& NameMapOut);

	void ZeroValues();

	void Serialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

	int AddInput(EModuleInputValueType Type, TSubclassOf<UDefaultModularVehicleInputModifier>& InputModifierClass);

	void RemoveAllInputs();

	void Lerp(const FModuleInputContainer& Min, const FModuleInputContainer& Max, float Alpha);

	void Merge(const FModuleInputContainer& From);

private:
	UPROPERTY()
	TArray<FModuleInputValue> InputValues;

};


class CHAOSVEHICLESCORE_API FInputInterface
{
public:

	using FInputNameMap = TMap<FName, int>;

	FInputInterface(const FInputNameMap& InNameMap, FModuleInputContainer& InValueContainer)
		: NameMap(InNameMap)
		, ValueContainer(InValueContainer)
	{
	}

	void SetValue(const FName& InName, const FModuleInputValue& InValue);
	void MergeValue(const FName& InName, const FModuleInputValue& InValue);
	FModuleInputValue GetValue(const FName& InName) const;
	float GetMagnitude(const FName& InName) const;
	bool InputsNonZero() const;

	const FInputNameMap& NameMap;			// per vehicle
	FModuleInputContainer& ValueContainer;	// per vehicle instance
};

UCLASS(Abstract, BlueprintType, Blueprintable, EditInlineNew)
class CHAOSVEHICLESCORE_API UVehicleInputProducerBase : public UObject
{
	GENERATED_BODY()
	
public:
	using FInputNameMap = TMap<FName, int>;

	/* initialize the input buffer container(s) */
	virtual void InitializeContainer(TArray<FModuleInputSetup>& SetupData, FInputNameMap& NameMapOut) {}

	/** capture input at game thread frequency */
	virtual void BufferInput(const FInputNameMap& InNameMap, const FName InName, const FModuleInputValue& InValue) {}

	/** produce input for PT simulation at PT frequency */
	virtual void ProduceInput(int32 PhysicsStep, int32 NumSteps, const FInputNameMap& InNameMap, FModuleInputContainer& InOutContainer) {}
};

