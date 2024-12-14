// Copyright Epic Games, Inc. All Rights Reserved.

#include "String/CaseConversion.h"
#include "Misc/StringBuilder.h"

namespace UE::String::Private
{

template <typename CharType>
static inline void UpperCaseTo(TStringView<CharType> Input, TStringBuilderBase<CharType>& Output)
{
	const int32 Offset = Output.AddUninitialized(Input.Len());
	CharType* OutputIterator = Output.GetData() + Offset;
	for (CharType Char : Input)
	{
		*OutputIterator++ = TChar<CharType>::ToUpper(Char);
	}
}

template <typename CharType>
static inline void LowerCaseTo(TStringView<CharType> Input, TStringBuilderBase<CharType>& Output)
{
	const int32 Offset = Output.AddUninitialized(Input.Len());
	CharType* OutputIterator = Output.GetData() + Offset;
	for (CharType Char : Input)
	{
		*OutputIterator++ = TChar<CharType>::ToLower(Char);
	}
}

} // UE::String::Private

namespace UE::String
{

void UpperCaseTo(FAnsiStringView Input, FAnsiStringBuilderBase& Output)
{
	Private::UpperCaseTo(Input, Output);
}

void UpperCaseTo(FUtf8StringView Input, FUtf8StringBuilderBase& Output)
{
	Private::UpperCaseTo(Input, Output);
}

void UpperCaseTo(FWideStringView Input, FWideStringBuilderBase& Output)
{
	Private::UpperCaseTo(Input, Output);
}

void LowerCaseTo(FAnsiStringView Input, FAnsiStringBuilderBase& Output)
{
	Private::LowerCaseTo(Input, Output);
}

void LowerCaseTo(FUtf8StringView Input, FUtf8StringBuilderBase& Output)
{
	Private::LowerCaseTo(Input, Output);
}

void LowerCaseTo(FWideStringView Input, FWideStringBuilderBase& Output)
{
	Private::LowerCaseTo(Input, Output);
}

} // UE::String
