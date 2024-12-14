// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/TVariant.h"
#include "UObject/Object.h"
#include "Math/MathFwd.h"
#include <string>

class UObject;

template<typename TType>
struct TDataflowPolicyTypeName
{
};

#define UE_DATAFLOW_POLICY_DECLARE_TYPENAME(TType) \
template<> \
struct TDataflowPolicyTypeName<TType> \
{  \
	FORCEINLINE static const TCHAR* GetName() \
	{ \
		return TEXT(#TType); \
	} \
};

UE_DATAFLOW_POLICY_DECLARE_TYPENAME(bool)
UE_DATAFLOW_POLICY_DECLARE_TYPENAME(uint8)
UE_DATAFLOW_POLICY_DECLARE_TYPENAME(uint16)
UE_DATAFLOW_POLICY_DECLARE_TYPENAME(uint32)
UE_DATAFLOW_POLICY_DECLARE_TYPENAME(uint64)
UE_DATAFLOW_POLICY_DECLARE_TYPENAME(int8)
UE_DATAFLOW_POLICY_DECLARE_TYPENAME(int16)
UE_DATAFLOW_POLICY_DECLARE_TYPENAME(int32)
UE_DATAFLOW_POLICY_DECLARE_TYPENAME(int64)
UE_DATAFLOW_POLICY_DECLARE_TYPENAME(float)
UE_DATAFLOW_POLICY_DECLARE_TYPENAME(double)
UE_DATAFLOW_POLICY_DECLARE_TYPENAME(FName)
UE_DATAFLOW_POLICY_DECLARE_TYPENAME(FString)
UE_DATAFLOW_POLICY_DECLARE_TYPENAME(TObjectPtr<UObject>)
UE_DATAFLOW_POLICY_DECLARE_TYPENAME(FVector2D)
UE_DATAFLOW_POLICY_DECLARE_TYPENAME(FVector)
UE_DATAFLOW_POLICY_DECLARE_TYPENAME(FVector4)
UE_DATAFLOW_POLICY_DECLARE_TYPENAME(FVector2f)
UE_DATAFLOW_POLICY_DECLARE_TYPENAME(FVector3f)
UE_DATAFLOW_POLICY_DECLARE_TYPENAME(FVector4f)

#undef UE_DATAFLOW_POLICY_DECLARE_TYPENAME

struct IDataflowTypePolicy
{
	virtual bool SupportsType(FName Type) const = 0;
};

struct FDataflowAllTypesPolicy : public IDataflowTypePolicy
{
	virtual bool SupportsType(FName InType) const override
	{
		return true;
	}

	static IDataflowTypePolicy* GetInterface()
	{
		static FDataflowAllTypesPolicy Instance;
		return &Instance;
	}
};

template <typename T>
struct TDataflowSingleTypePolicy : public IDataflowTypePolicy
{
	using FType = T;

	virtual bool SupportsType(FName InType) const override
	{
		return SupportsTypeStatic(InType);
	}

	static bool SupportsTypeStatic(FName InType)
	{
		return (InType == TypeName);
	}

	template <typename TVisitor>
	static bool VisitPolicyByType(FName RequestedType, TVisitor Visitor)
	{
		if (RequestedType == TypeName)
		{
			TDataflowSingleTypePolicy<T> SingleTypePolicy;
			Visitor(SingleTypePolicy);
			return true;
		}
		return false;
	}

	static IDataflowTypePolicy* GetInterface()
	{
		static TDataflowSingleTypePolicy Instance;
		return &Instance;
	}

	inline static const FName TypeName = FName(TDataflowPolicyTypeName<T>::GetName());
};

template <typename... TTypes>
struct TDataflowMultiTypePolicy;

template <>
struct TDataflowMultiTypePolicy<>: public IDataflowTypePolicy
{
	virtual bool SupportsType(FName InType) const override
	{
		return false;
	}

	static bool SupportsTypeStatic(FName InType)
	{
		return false;
	}

	template <typename TVisitor>
	static bool VisitPolicyByType(FName RequestedType, TVisitor Visitor)
	{
		return false;
	}
};

template <typename T, typename... TTypes>
struct TDataflowMultiTypePolicy<T, TTypes...>: public TDataflowMultiTypePolicy<TTypes...>
{
	using Super = TDataflowMultiTypePolicy<TTypes...>;

	virtual bool SupportsType(FName InType) const override
	{
		return SupportsTypeStatic(InType);
	}

	static bool SupportsTypeStatic(FName InType)
	{
		return TDataflowSingleTypePolicy<T>::SupportsTypeStatic(InType)
			|| Super::SupportsTypeStatic(InType);
	}

	template <typename TVisitor>
	static bool VisitPolicyByType(FName RequestedType, TVisitor Visitor)
	{
		if (TDataflowSingleTypePolicy<T>::VisitPolicyByType(RequestedType, Visitor))
		{
			return true;
		}
		return Super::VisitPolicyByType(RequestedType, Visitor);
	}

	static IDataflowTypePolicy* GetInterface()
	{
		static TDataflowMultiTypePolicy<T, TTypes...> Instance;
		return &Instance;
	}
};

struct FDataflowNumericTypePolicy : 
	public TDataflowMultiTypePolicy<double, float, int64, uint64, int32, uint32, int16, uint16, int8, uint8>
{
};

struct FDataflowVectorTypePolicy:
	public TDataflowMultiTypePolicy<FVector2D, FVector, FVector4, FVector2f, FVector3f, FVector4f>
{
};


struct FDataflowStringTypePolicy :
	public TDataflowMultiTypePolicy<FString, FName>
{
};

/**
* string comvertible types
* - FString / Fname
* - Numeric types ( see FDataflowNumericTypePolicy )
* - Vector types ( see FDataflowVectorTypePolicy )
* - bool
*/
struct FDataflowStringConvertibleTypePolicy : IDataflowTypePolicy
{
	virtual bool SupportsType(FName InType) const override
	{
		return SupportsTypeStatic(InType);
	}

	static bool SupportsTypeStatic(FName InType)
	{
		return FDataflowStringTypePolicy::SupportsTypeStatic(InType)
			|| FDataflowNumericTypePolicy::SupportsTypeStatic(InType)
			|| FDataflowVectorTypePolicy::SupportsTypeStatic(InType)
			|| TDataflowSingleTypePolicy<bool>::SupportsTypeStatic(InType)
			;
	}

	template <typename TVisitor>
	static bool VisitPolicyByType(FName RequestedType, TVisitor Visitor)
	{
		return FDataflowStringTypePolicy::VisitPolicyByType(RequestedType, Visitor)
			|| FDataflowNumericTypePolicy::VisitPolicyByType(RequestedType, Visitor)
			|| FDataflowVectorTypePolicy::VisitPolicyByType(RequestedType, Visitor)
			|| TDataflowSingleTypePolicy<bool>::VisitPolicyByType(RequestedType, Visitor)
			;
	}

	static IDataflowTypePolicy* GetInterface()
	{
		static FDataflowStringConvertibleTypePolicy Instance;
		return &Instance;
	}
};

struct FDataflowUObjectConvertibleTypePolicy : IDataflowTypePolicy
{
	virtual bool SupportsType(FName InType) const override
	{
		return SupportsTypeStatic(InType);
	}

	static bool SupportsTypeStatic(FName InType)
	{
		FString InnerTypeStr;
		if (GetObjectPtrInnerType(InType.ToString(), InnerTypeStr))
		{
			if (StaticFindFirstObject(UObject::StaticClass(), *InnerTypeStr, EFindFirstObjectOptions::NativeFirst))
			{
				return true;
			}
		}
		// not a proper object pointer 
		return false;
	}

	template <typename TVisitor>
	static bool VisitPolicyByType(FName RequestedType, TVisitor Visitor)
	{
		if (SupportsTypeStatic(RequestedType))
		{
			TDataflowSingleTypePolicy<TObjectPtr<UObject>> SingleTypePolicy;
			Visitor(SingleTypePolicy);
			return true;
		}
		return false;
	}

	static IDataflowTypePolicy* GetInterface()
	{
		static FDataflowUObjectConvertibleTypePolicy Instance;
		return &Instance;
	}

	// returns true if the type was a TObjectPtr and the inner type was properly extracted
	static bool GetObjectPtrInnerType(const FString& InTypeStr, FString& InnerType)
	{
		static constexpr const TCHAR* ObjectPtrPrefix = TEXT("TObjectPtr<U");
		static constexpr size_t ObjectPtrPrefixLen = std::char_traits<TCHAR>::length(ObjectPtrPrefix);
		if (InTypeStr.StartsWith(ObjectPtrPrefix))
		{
			InnerType = InTypeStr
				.RightChop(ObjectPtrPrefixLen) // remove the TObjectPtr< type
				.LeftChop(1) // remove the last ">"
				.TrimStartAndEnd();
				return true;
		}
		return false;
	}
};

// type Converters

template <typename T>
struct FDataflowConverter
{
	template <typename TFromType>
	static void From(const TFromType& From, T& To) { To = From; }

	template <typename TToType>
	static void To(const T& From, TToType& To) { To = From; }
};

template<typename T>
concept HasToStringMethod =
	requires(T t) {
		static_cast<FString>(t.ToString());
};


template<typename T>
concept HasInitFromStringMethod =
	requires(T t, const FString& s ) {
		static_cast<bool>(t.InitFromString(s));
};

template <>
struct FDataflowConverter<FString>
{
	template <typename TFromType>
	static void From(const TFromType& From, FString& To)
	{
		if constexpr (std::is_same_v<TFromType, FName>)
		{
			To = From.ToString();
		}
		else if constexpr (std::is_same_v<TFromType, bool>)
		{
			To = FString((From == true) ? "True" : "False");
		}
		else if constexpr (std::is_convertible_v<TFromType, double>)
		{
			To = FString::SanitizeFloat(double(From), 0);
		}
		else if constexpr (HasToStringMethod<TFromType>)
		{
			To = From.ToString();
		}
		else
		{
			To = From;
		}
	}

	template <typename TToType>
	static void To(const FString& From, TToType& To)
	{
		if constexpr (std::is_same_v<TToType, FName>)
		{
			To = FName(From);
		}
		else if constexpr (std::is_same_v<TToType, bool>)
		{
			To = From.ToBool();
		}
		else if constexpr (std::is_convertible_v<double, TToType>)
		{
			double Result = {0};
			LexTryParseString(Result, *From);
			To = Result;
		}
		else if constexpr (HasInitFromStringMethod<TToType>)
		{
			To.InitFromString(From);
		}
		else
		{
			To = From;
		}
	}
};

template <>
struct FDataflowConverter<FVector4>
{
	template <typename TFromType>
	static void From(const TFromType& From, FVector4& To)
	{
		if constexpr (std::is_same_v<TFromType, FVector2D> || std::is_same_v<TFromType, FVector2f>)
		{
			To = FVector4{ (double)From.X, (double)From.Y, 0, 0};
		}
		else if constexpr (std::is_same_v<TFromType, FVector> || std::is_same_v<TFromType, FVector3f>)
		{
			To = FVector4{ (double)From.X, (double)From.Y, (double)From.Z, 0 };
		}
		else if constexpr (std::is_same_v<TFromType, FVector4f>)
		{
			To = FVector4{ (double)From.X, (double)From.Y, (double)From.Z, (double)From.W };
		}
		else
		{
			To = From;
		}
	}

	template <typename TToType>
	static void To(const FVector4& From, TToType& To)
	{
		if constexpr (std::is_same_v<TToType, FVector2D>)
		{
			To = FVector2D{ From.X, From.Y };
		}
		else if constexpr (std::is_same_v<TToType, FVector2f>)
		{
			To = FVector2f{ (float)From.X, (float)From.Y };
		}
		else if constexpr (std::is_same_v<TToType, FVector>)
		{
			To = FVector{ From.X, From.Y, From.Z };
		}
		else if constexpr (std::is_same_v<TToType, FVector3f>)
		{
			To = FVector3f{ (float)From.X, (float)From.Y, (float)From.Z };
		}
		else if constexpr (std::is_same_v<TToType, FVector4f>)
		{
			To = FVector4f{ (float)From.X, (float)From.Y, (float)From.Z, (float)From.W };
		}
		else
		{
			To = From;
		}
	}
};