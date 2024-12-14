// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "CoreMinimal.h"
#include "HAL/Platform.h"
#include "Misc/CString.h"
#include "Serialization/JsonTypes.h"
#include "Templates/SharedPointer.h"

class FJsonObject;

/**
 * A Json Value is a structure that can be any of the Json Types.
 * It should never be used on its, only its derived types should be used.
 */
class FJsonValue
{
public:

	/** Returns this value as a double, logging an error and returning zero if this is not an Json Number */
	JSON_API double AsNumber() const;

	/** Returns this value as a string, logging an error and returning an empty string if not possible */
	JSON_API FString AsString() const;

	/** Returns this value as a boolean, logging an error and returning false if not possible */
	JSON_API bool AsBool() const;

	/** Returns this value as an array, logging an error and returning an empty array reference if not possible */
	JSON_API const TArray< TSharedPtr<FJsonValue> >& AsArray() const;

	/** Returns this value as an object, throwing an error if this is not an Json Object */
	JSON_API virtual const TSharedPtr<FJsonObject>& AsObject() const;

	/** Tries to convert this value to a number, returning false if not possible */
	virtual bool TryGetNumber(double& OutNumber) const { return false; }

	/** Tries to convert this value to a number, returning false if not possible */
	JSON_API virtual bool TryGetNumber(float& OutNumber) const;

	/** Tries to convert this value to a number, returning false if not possible */
	JSON_API virtual bool TryGetNumber(int8& OutNumber) const;

	/** Tries to convert this value to a number, returning false if not possible */
	JSON_API virtual bool TryGetNumber(int16& OutNumber) const;

	/** Tries to convert this value to a number, returning false if not possible */
	JSON_API virtual bool TryGetNumber(int32& OutNumber) const;

	/** Tries to convert this value to a number, returning false if not possible */
	JSON_API virtual bool TryGetNumber(int64& OutNumber) const;

	/** Tries to convert this value to a number, returning false if not possible */
	JSON_API virtual bool TryGetNumber(uint8& OutNumber) const;

	/** Tries to convert this value to a number, returning false if not possible */
	JSON_API virtual bool TryGetNumber(uint16& OutNumber) const;

	/** Tries to convert this value to a number, returning false if not possible */
	JSON_API virtual bool TryGetNumber(uint32& OutNumber) const;

	/** Tries to convert this value to a number, returning false if not possible */
	JSON_API virtual bool TryGetNumber(uint64& OutNumber) const;

	/** Tries to convert this value to a string, returning false if not possible */
	virtual bool TryGetString(FString& OutString) const { return false; }

	/** Tries to convert this value to a bool, returning false if not possible */
	virtual bool TryGetBool(bool& OutBool) const { return false; }

	/** Tries to convert this value to an array, returning false if not possible */
	virtual bool TryGetArray(const TArray< TSharedPtr<FJsonValue> >*& OutArray) const { return false; }
	
	/** Tries to convert this value to an array, returning false if not possible */
	virtual bool TryGetArray(TArray< TSharedPtr<FJsonValue> >*& OutArray) { return false; }

	/** Tries to convert this value to an object, returning false if not possible */
	virtual bool TryGetObject(const TSharedPtr<FJsonObject>*& Object) const { return false; }

	/** Tries to convert this value to an object, returning false if not possible */
	virtual bool TryGetObject(TSharedPtr<FJsonObject>*& Object) { return false; }

	/** Returns whether or not a caller should prefer a string representation of the value, rather than the natural JSON type */
	virtual bool PreferStringRepresentation() const { return false; }

	/** Returns true if this value is a 'null' */
	bool IsNull() const { return Type == EJson::Null || Type == EJson::None; }

	/** Get a field of the same type as the argument */
	void AsArgumentType(double                          & Value) { Value = AsNumber(); }
	void AsArgumentType(FString                         & Value) { Value = AsString(); }
	void AsArgumentType(bool                            & Value) { Value = AsBool  (); }
	void AsArgumentType(TArray< TSharedPtr<FJsonValue> >& Value) { Value = AsArray (); }
	void AsArgumentType(TSharedPtr<FJsonObject>         & Value) { Value = AsObject(); }

	/**
	 * Returns the memory footprint for this object in Bytes, including sizeof(*this) and allocated memory.
	 * All children should implement this so their memory layout is properly accounted for
	 */
	JSON_API virtual SIZE_T GetMemoryFootprint() const { return sizeof(*this); }

	EJson Type;

	static JSON_API TSharedPtr<FJsonValue> Duplicate(const TSharedPtr<const FJsonValue>& Src);
	static JSON_API TSharedPtr<FJsonValue> Duplicate(const TSharedPtr<FJsonValue>& Src);

	static JSON_API bool CompareEqual(const FJsonValue& Lhs, const FJsonValue& Rhs);

protected:

	FJsonValue() : Type(EJson::None) {}
	virtual ~FJsonValue() {}

	virtual FString GetType() const = 0;

	JSON_API void ErrorMessage(const FString& InType) const;

	friend inline bool operator==(const FJsonValue& Lhs, const FJsonValue& Rhs)
	{
		return FJsonValue::CompareEqual(Lhs, Rhs);
	}

	friend inline bool operator!=(const FJsonValue& Lhs, const FJsonValue& Rhs)
	{
		return !FJsonValue::CompareEqual(Lhs, Rhs);
	}
};


/** A Json String Value. */
class FJsonValueString : public FJsonValue
{
public:
	FJsonValueString(const FString& InString) : Value(InString) {Type = EJson::String;}
	FJsonValueString(FString&& InString) : Value(MoveTemp(InString)) {Type = EJson::String;}

	virtual bool TryGetString(FString& OutString) const override	{ OutString = Value; return true; }
	virtual bool TryGetNumber(double& OutDouble) const override		{ if (Value.IsNumeric()) { OutDouble = FCString::Atod(*Value); return true; } else { return false; } }
	virtual bool TryGetNumber(int32& OutValue) const override		{ LexFromString(OutValue, *Value); return true; }
	virtual bool TryGetNumber(uint32& OutValue) const override		{ LexFromString(OutValue, *Value); return true; }
	virtual bool TryGetNumber(int64& OutValue) const override		{ LexFromString(OutValue, *Value); return true; }
	virtual bool TryGetNumber(uint64& OutValue) const override		{ LexFromString(OutValue, *Value); return true; }
	virtual bool TryGetBool(bool& OutBool) const override			{ OutBool = Value.ToBool(); return true; }

	// Way to check if string value is empty without copying the string 
	bool IsEmpty() const { return Value.IsEmpty(); }

	virtual SIZE_T GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize(); }

protected:
	FString Value;

	virtual FString GetType() const override {return TEXT("String");}
	/** Helper to calculate allocated size of the Value string */
	SIZE_T GetAllocatedSize() const { return Value.GetAllocatedSize(); }
};


/** A Json Number Value. */
class FJsonValueNumber : public FJsonValue
{
public:
	FJsonValueNumber(double InNumber) : Value(InNumber) {Type = EJson::Number;}
	virtual bool TryGetNumber(double& OutNumber) const override		{ OutNumber = Value; return true; }
	virtual bool TryGetBool(bool& OutBool) const override			{ OutBool = (Value != 0.0); return true; }
	virtual bool TryGetString(FString& OutString) const override	{ OutString = FString::SanitizeFloat(Value, 0); return true; }
	virtual SIZE_T GetMemoryFootprint() const override { return sizeof(*this); }

protected:

	double Value;

	virtual FString GetType() const override {return TEXT("Number");}
};


/** A Json Number Value, stored internally as a string so as not to lose precision */
class FJsonValueNumberString : public FJsonValue
{
public:
	FJsonValueNumberString(const FString& InString) : Value(InString) { Type = EJson::Number; }
	FJsonValueNumberString(FString&& InString) : Value(MoveTemp(InString)) { Type = EJson::Number; }

	virtual bool TryGetString(FString& OutString) const override { OutString = Value; return true; }
	virtual bool TryGetNumber(double& OutDouble) const override { return LexTryParseString(OutDouble, *Value); }
	virtual bool TryGetNumber(float &OutDouble) const override { return LexTryParseString(OutDouble, *Value); }
	virtual bool TryGetNumber(int8& OutValue) const override { return LexTryParseString(OutValue, *Value); }
	virtual bool TryGetNumber(int16& OutValue) const override { return LexTryParseString(OutValue, *Value); }
	virtual bool TryGetNumber(int32& OutValue) const override { return LexTryParseString(OutValue, *Value); }
	virtual bool TryGetNumber(int64& OutValue) const override { return LexTryParseString(OutValue, *Value); }
	virtual bool TryGetNumber(uint8& OutValue) const override { return LexTryParseString(OutValue, *Value); }
	virtual bool TryGetNumber(uint16& OutValue) const override { return LexTryParseString(OutValue, *Value); }
	virtual bool TryGetNumber(uint32& OutValue) const override { return LexTryParseString(OutValue, *Value); }
	virtual bool TryGetNumber(uint64& OutValue) const override { return LexTryParseString(OutValue, *Value); }
	virtual bool TryGetBool(bool& OutBool) const override { OutBool = Value.ToBool(); return true; }
	virtual bool PreferStringRepresentation() const override { return true; }
	virtual SIZE_T GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize(); }

protected:
	FString Value;

	virtual FString GetType() const override { return TEXT("NumberString"); }
	/** Helper to calculate allocated size of the Value string */
	SIZE_T GetAllocatedSize() const { return Value.GetAllocatedSize(); }
};


/** A Json Boolean Value. */
class FJsonValueBoolean : public FJsonValue
{
public:
	FJsonValueBoolean(bool InBool) : Value(InBool) {Type = EJson::Boolean;}
	virtual bool TryGetNumber(double& OutNumber) const override		{ OutNumber = Value ? 1 : 0; return true; }
	virtual bool TryGetBool(bool& OutBool) const override			{ OutBool = Value; return true; }
	virtual bool TryGetString(FString& OutString) const override	{ OutString = Value ? TEXT("true") : TEXT("false"); return true; }
	virtual SIZE_T GetMemoryFootprint() const override { return sizeof(*this); }

protected:
	bool Value;

	virtual FString GetType() const override {return TEXT("Boolean");}
};


/** A Json Array Value. */
class FJsonValueArray : public FJsonValue
{
public:
	FJsonValueArray(const TArray< TSharedPtr<FJsonValue> >& InArray) : Value(InArray) {Type = EJson::Array;}
	FJsonValueArray(TArray< TSharedPtr<FJsonValue> >&& InArray) : Value(MoveTemp(InArray)) {Type = EJson::Array;}
	virtual bool TryGetArray(const TArray< TSharedPtr<FJsonValue> >*& OutArray) const override	{ OutArray = &Value; return true; }
	virtual bool TryGetArray(TArray< TSharedPtr<FJsonValue> >*& OutArray) override				{ OutArray = &Value; return true; }
	virtual SIZE_T GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize(); }

protected:
	TArray< TSharedPtr<FJsonValue> > Value;

	virtual FString GetType() const override {return TEXT("Array");}
	/** Helper to calculate allocated size of the Value array and its contents */
	SIZE_T GetAllocatedSize() const 
	{
		SIZE_T SizeBytes = 0;
		SizeBytes += Value.GetAllocatedSize();
		for (const TSharedPtr<FJsonValue>& Element : Value)
		{
			SizeBytes += Element.IsValid() ? Element->GetMemoryFootprint() : 0;
		}
		return SizeBytes; 
	}
};


/** A Json Object Value. */
class FJsonValueObject : public FJsonValue
{
public:
	FJsonValueObject(TSharedPtr<FJsonObject> InObject) : Value(MoveTemp(InObject)) {Type = EJson::Object;}
	virtual bool TryGetObject(const TSharedPtr<FJsonObject>*& OutObject) const override	{ OutObject = &Value; return true; }
	virtual bool TryGetObject(TSharedPtr<FJsonObject>*& OutObject) override				{ OutObject = &Value; return true; }
	virtual SIZE_T GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize(); }
protected:
	TSharedPtr<FJsonObject> Value;

	virtual FString GetType() const override {return TEXT("Object");}
	/** Helper to calculate allocated size of the Value object and its contents */
	JSON_API SIZE_T GetAllocatedSize() const;
};


/** A Json Null Value. */
class FJsonValueNull : public FJsonValue
{
public:
	FJsonValueNull() {Type = EJson::Null;}
	virtual SIZE_T GetMemoryFootprint() const override { return sizeof(*this); }

protected:
	virtual FString GetType() const override {return TEXT("Null");}
};

namespace UE::Json
{

template<typename T, typename = typename std::enable_if<!std::is_same_v<T, FJsonValue>>>
static JsonSimpleValueVariant ToSimpleJsonVariant(const T& InSimpleValue)
{
	using InSimpleValueType = std::decay_t<decltype(InSimpleValue)>;
	if constexpr (std::is_same_v<InSimpleValueType, bool> || std::is_same_v<InSimpleValueType, FString>)
	{
		return JsonSimpleValueVariant(TInPlaceType<T>(), InSimpleValue);
	}
	else
	{
		return JsonSimpleValueVariant(TInPlaceType<JsonNumberValueVariants>(), JsonNumberValueVariants(TInPlaceType<T>(), InSimpleValue));
	}
}

static JsonSimpleValueVariant ToSimpleJsonVariant(const FJsonValue& InJsonValue)
{
	if (!InJsonValue.PreferStringRepresentation())
	{
		if (InJsonValue.Type == EJson::Boolean)
		{
			return JsonSimpleValueVariant(TInPlaceType<bool>(), InJsonValue.AsBool());
		}
		else if (InJsonValue.Type == EJson::Number)
		{
			const double JsonNumber_v = InJsonValue.AsNumber();

			/* If the Json Number Value requires a decimal point, then we read in the value as a double, otherwise, we read it in as an int */
			if (FString::SanitizeFloat(JsonNumber_v, 0).Contains(TEXT(".")))
			{
				return ToSimpleJsonVariant(JsonNumber_v);
			}
			else
			{
				return ToSimpleJsonVariant(FMath::RoundToInt64(JsonNumber_v));
			}
		}
	}

	return JsonSimpleValueVariant(TInPlaceType<FString>(), InJsonValue.AsString());
}

} // namespace UE::Json

/* Global operators */

static bool operator==(const JsonNumberValueVariants& Lhs, const FString& Rhs)
{
	return Rhs.IsNumeric() && ::Visit([Rhs](const auto& StoredNumber)
		{
			using StoredNumberType = std::decay_t<decltype(StoredNumber)>;
			if constexpr (std::is_same_v<StoredNumberType, float> || std::is_same_v<StoredNumberType, double>)
			{
				return FString::SanitizeFloat(StoredNumber, 0) == Rhs;
			}
			else
			{
				return StoredNumber == FCString::Atoi64(*Rhs);
			}
		}, Lhs);
}

static bool operator!=(const JsonNumberValueVariants& Lhs, const FString& Rhs)
{
	return !(Lhs == Rhs);
}

static bool operator==(const FString& Lhs, const JsonNumberValueVariants& Rhs)
{
	return Rhs == Lhs;
}

static bool operator!=(const FString& Lhs, const JsonNumberValueVariants& Rhs)
{
	return !(Lhs == Rhs);
}

////////////////////////////////////////////////////////////

static FString ToString(const JsonNumberValueVariants& InNumberVariant)
{
	return ::Visit([](auto& StoredNumber)
		{
			using StoredNumberType = std::decay_t<decltype(StoredNumber)>;
			if constexpr (std::is_same_v<StoredNumberType, float> || std::is_same_v<StoredNumberType, double>)
			{
				return FString::SanitizeFloat(StoredNumber, 0);
			}
			else
			{
				return FString::Printf(TEXT("%lld"), static_cast<int64>(StoredNumber));
			}
		}, InNumberVariant);
}

static bool operator==(const JsonNumberValueVariants& Lhs, const JsonNumberValueVariants& Rhs)
{
	const bool bLhsIsFloat = Lhs.IsType<float>() || Lhs.IsType<double>();
	const bool bRhsIsFloat = Rhs.IsType<float>() || Rhs.IsType<double>();
	if (bLhsIsFloat || bRhsIsFloat)
	{
		return ToString(Lhs) == ToString(Rhs);
	}
	else
	{
		auto CastToInt64Functor = [](auto& StoredNumber)
			{
				return static_cast<int64>(StoredNumber);
			};

		const int64 LhsValue = ::Visit(CastToInt64Functor, Lhs);
		const int64 RhsValue = ::Visit(CastToInt64Functor, Rhs);

		return LhsValue == RhsValue;
	}
}

static bool operator!=(const JsonNumberValueVariants& Lhs, const JsonNumberValueVariants& Rhs)
{
	return !(Lhs == Rhs);
}

////////////////////////////////////////////////////////////

static bool operator==(const JsonSimpleValueVariant& Lhs, const JsonSimpleValueVariant& Rhs)
{
	if (Lhs.IsType<bool>())
	{
		if (Rhs.IsType<bool>())
		{
			return Lhs.Get<bool>() == Rhs.Get<bool>();
		}
		else if (Rhs.IsType<FString>())
		{
			if (Lhs.Get<bool>())
			{
				return Rhs.Get<FString>().Equals(TEXT("true"), ESearchCase::IgnoreCase) ||
					Rhs.Get<FString>().Equals(TEXT("1"), ESearchCase::IgnoreCase);
			}
			else
			{
				return Rhs.Get<FString>().Equals(TEXT("false"), ESearchCase::IgnoreCase) ||
					Rhs.Get<FString>().Equals(TEXT("0"), ESearchCase::IgnoreCase);
			}
		}
		else // RhsType.IsType<JsonNumberValueVariants>()
		{
			return ::Visit([&Lhs](const auto& RhsStoredNumber)
				{
					using RhsStoredNumberType = std::decay_t<decltype(RhsStoredNumber)>;
					if constexpr (std::is_same_v<RhsStoredNumberType, float> || std::is_same_v<RhsStoredNumberType, double>)
					{
						if (!FString::SanitizeFloat(RhsStoredNumber, 0).Contains(TEXT(".")))
						{
							const int64 RhsStoredNumberAsInt = FMath::RoundToInt64(RhsStoredNumber);
							if (Lhs.Get<bool>())
							{
								return RhsStoredNumberAsInt == 1;
							}
							else
							{
								return RhsStoredNumberAsInt == 0;
							}
						}
						else
						{
							return false;
						}
					}
					else
					{
						if (Lhs.Get<bool>())
						{
							return RhsStoredNumber == 1;
						}
						else
						{
							return RhsStoredNumber == 0;
						}
					}
				}, Rhs.Get<JsonNumberValueVariants>());
		}
	}
	else if (Lhs.IsType<JsonNumberValueVariants>())
	{
		if (Rhs.IsType<JsonNumberValueVariants>())
		{
			return Lhs.Get<JsonNumberValueVariants>() == Rhs.Get<JsonNumberValueVariants>();
		}
		else // RhsType.IsType<bool>() || RhsType.IsType<FString>()
		{
			// Swapping args to avoid code duplication
			return Rhs == Lhs;
		}
	}
	else // Lhs.IsType<FString>()
	{
		if (Rhs.IsType<FString>())
		{
			return Lhs.Get<FString>().Equals(Rhs.Get<FString>(), ESearchCase::CaseSensitive);
		}
		else if (Rhs.IsType<bool>())
		{
			// Swapping args to avoid code duplication
			return Rhs == Lhs;
		}
		else // RhsType.IsType<JsonNumberValueVariants>()
		{
			return Lhs.Get<FString>() == Rhs.Get<JsonNumberValueVariants>();
		}
	}
}

static bool operator!=(const JsonSimpleValueVariant& Lhs, const JsonSimpleValueVariant& Rhs)
{
	return !(Lhs == Rhs);
}