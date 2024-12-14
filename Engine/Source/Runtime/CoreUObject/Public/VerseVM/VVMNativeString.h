// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Utf8String.h"

namespace Verse
{
class FNativeString;
}

template <>
struct TIsZeroConstructType<Verse::FNativeString>
{
	static constexpr bool Value = true;
};

template <>
struct TIsContiguousContainer<Verse::FNativeString>
{
	static constexpr bool Value = true;
};

namespace Verse
{
// Thin wrapper around FUtf8String that adapts it to Verse semantics.
class FNativeString
{
public:
	using ElementType = FUtf8String::ElementType;

private:
	FUtf8String String;

public:
	FNativeString() = default;
	FNativeString(FNativeString&&) = default;
	FNativeString(const FNativeString&) = default;
	FNativeString& operator=(FNativeString&&) = default;
	FNativeString& operator=(const FNativeString&) = default;

	FORCEINLINE FNativeString(const ANSICHAR* Str)
		: String(Str) {}

	FORCEINLINE FNativeString(FUtf8String&& InString)
		: String(MoveTemp(InString)) {}

	template <
		typename CharRangeType,
		typename CharRangeElementType = TElementType_T<CharRangeType>
			UE_REQUIRES(TIsContiguousContainer<CharRangeType>::Value&& TIsCharType_V<CharRangeElementType>)>
	FORCEINLINE explicit FNativeString(CharRangeType&& Range)
		: String(Forward<CharRangeType>(Range))
	{
	}

	template <
		typename CharRangeType,
		typename CharRangeElementType = TElementType_T<CharRangeType>
			UE_REQUIRES(TIsContiguousContainer<CharRangeType>::Value&& std::is_same_v<ElementType, CharRangeElementType>)>
	FORCEINLINE FNativeString& operator=(CharRangeType&& Range)
	{
		String = Forward<CharRangeType>(Range);
		return *this;
	}

	friend FORCEINLINE ElementType* GetData(FNativeString& InString) { return GetData(InString.String); }
	friend FORCEINLINE const ElementType* GetData(const FNativeString& InString) { return GetData(InString.String); }

	friend FORCEINLINE int32 GetNum(const FNativeString& InString) { return GetNum(InString.String); }

	FORCEINLINE ElementType& operator[](int32 Index) UE_LIFETIMEBOUND { return String[Index]; }
	FORCEINLINE const ElementType& operator[](int32 Index) const UE_LIFETIMEBOUND { return String[Index]; }

	[[nodiscard]] FORCEINLINE const ElementType* operator*() const UE_LIFETIMEBOUND { return *String; }

	[[nodiscard]] FORCEINLINE int Len() const { return String.Len(); }
	[[nodiscard]] FORCEINLINE bool IsEmpty() const { return String.IsEmpty(); }

	[[nodiscard]] FORCEINLINE friend bool operator==(const FNativeString& Lhs, const FNativeString& Rhs)
	{
		// Do not forward to FUtf8String::operator==, which is case-insensitive.
		return Lhs.Equals(Rhs);
	}

	[[nodiscard]] FORCEINLINE friend bool operator!=(const FNativeString& Lhs, const FNativeString& Rhs)
	{
		// Do not forward to FUtf8String::operator!=, which is case-insensitive.
		return !Lhs.Equals(Rhs);
	}

	[[nodiscard]] FORCEINLINE bool Equals(const FNativeString& Other) const
	{
		return String.Equals(Other.String, ESearchCase::CaseSensitive);
	}

	friend FORCEINLINE int32 GetTypeHash(const FNativeString& S)
	{
		// Do not forward to GetTypeHash(const FUtf8String&), which is case-insensitive.
		return FCrc::StrCrc32Len(GetData(S), GetNum(S));
	}

	void Reset(int32 NewReservedSize = 0) { String.Reset(NewReservedSize); }

	template <
		typename CharRangeType,
		typename CharRangeElementType = TElementType_T<CharRangeType>
			UE_REQUIRES(TIsContiguousContainer<CharRangeType>::Value&& TIsCharType_V<CharRangeElementType>)>
	FORCEINLINE FNativeString& operator+=(CharRangeType&& Str)
	{
		String += Forward<CharRangeType>(Str);
		return *this;
	}
	FORCEINLINE FNativeString& operator+=(const ANSICHAR* Str)
	{
		String += Str;
		return *this;
	}

	template <
		typename CharRangeType,
		typename CharRangeElementType = TElementType_T<CharRangeType>
			UE_REQUIRES(TIsContiguousContainer<CharRangeType>::Value&& std::is_same_v<ElementType, CharRangeElementType>)>
	FORCEINLINE friend FNativeString operator+(FNativeString&& Lhs, CharRangeType&& Rhs)
	{
		return MoveTemp(Lhs.String) + Forward<CharRangeType>(Rhs);
	}
	FORCEINLINE friend FNativeString operator+(FNativeString&& Lhs, const ANSICHAR* Rhs)
	{
		return MoveTemp(Lhs.String) + Rhs;
	}

	template <typename FmtType, typename... Types>
	[[nodiscard]] FORCEINLINE static FNativeString Printf(const FmtType& Fmt, Types... Args) { return FUtf8String::Printf(Fmt, Args...); }

	friend FORCEINLINE FArchive& operator<<(FArchive& Ar, FNativeString& S) { return Ar << S.String; }
};
} // namespace Verse
