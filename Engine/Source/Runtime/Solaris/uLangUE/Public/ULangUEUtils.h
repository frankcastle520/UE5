// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Set.h" // for BaseKeyFuncs
#include "Containers/StringConv.h"
#include "Containers/StringView.h"
#include "Containers/Utf8String.h"
#include "AutoRTFM/AutoRTFM.h"

#include "uLang/Common/Text/UTF8String.h"
#include "uLang/Common/Text/Symbol.h"
#include "uLang/Common/Text/TextRange.h"

class FULangConversionUtils
{
public:

	//=== String conversions from UE to uLang ===
	
	static FORCEINLINE uLang::CUTF8String FStringToULangStr(const FString& String)
	{
		// Use the known byte length so that the scan for null terminator is not needed with each conversion
		FTCHARToUTF8 UFT8String(*String, String.Len());
		const uLang::UTF8Char* UTF8CStr = (const uLang::UTF8Char*)UFT8String.Get();
		return uLang::CUTF8String(uLang::CUTF8StringView(UTF8CStr, UTF8CStr + UFT8String.Length()));
	}

	static FORCEINLINE uLang::CUTF8String FUtf8StringToULangStr(const FUtf8String& String)
	{
		return FUtf8StringViewToULangString(FUtf8StringView(*String, String.Len()));
	}

	static FORCEINLINE uLang::CUTF8String TCharToULangStr(const TCHAR* Text)
	{
		FTCHARToUTF8 UFT8String(Text);
		const uLang::UTF8Char* UTF8CStr = (const uLang::UTF8Char*)UFT8String.Get();
		return uLang::CUTF8String(uLang::CUTF8StringView(UTF8CStr, UTF8CStr + UFT8String.Length()));
	}

	static FORCEINLINE uLang::CUTF8String FNameToULangStr(const FName& NameId)
	{
		return FUtf8StringViewToULangString(WriteToUtf8String<FName::StringBufferSize>(NameId).ToView());
	}

	static FORCEINLINE uLang::CSymbol FNameToULangSymbol(const FName& NameId, uLang::CSymbolTable& uLangSymTable)
	{
		return uLangSymTable.AddChecked(FUtf8StringViewToULangStringView(WriteToUtf8String<FName::StringBufferSize>(NameId).ToView()));
	}

	static FORCEINLINE uLang::CSymbol TCharToULangSymbol(const TCHAR* Text, uLang::CSymbolTable& uLangSymTable)
	{
		FTCHARToUTF8 UFT8String(Text);
		return uLangSymTable.AddChecked(UFT8String.Get());
	}

	static FORCEINLINE uLang::CSymbol FStringToULangSymbol(const FString& String, uLang::CSymbolTable& uLangSymTable)
	{
		FTCHARToUTF8 UFT8String(*String, String.Len());
		const uLang::UTF8Char* UTF8Start = (const uLang::UTF8Char*)UFT8String.Get();
		uLang::CUTF8StringView StrView(UTF8Start, UTF8Start + UFT8String.Length());

		return uLangSymTable.AddChecked(StrView);
	}

	static FORCEINLINE uLang::CUTF8StringView FUtf8StringViewToULangStringView(const FUtf8StringView& StringView)
	{
		return uLang::CUTF8StringView((const uLang::UTF8Char*)StringView.GetData(), (const uLang::UTF8Char*)StringView.GetData() + StringView.Len());
	}

	static FORCEINLINE uLang::CUTF8String FUtf8StringViewToULangString(const FUtf8StringView& StringView)
	{
		return FUtf8StringViewToULangStringView(StringView);
	}

	//=== String conversions from uLang to UE ===

	static FORCEINLINE FString ULangStrToFString(const uLang::CUTF8String& ULangString)
	{
		return UTF8_TO_TCHAR(ULangString.AsUTF8());
	}

	static FORCEINLINE FUtf8String ULangStrToFUtf8String(const uLang::CUTF8String& ULangString)
	{
		return FUtf8String::ConstructFromPtrSize(reinterpret_cast<const UTF8CHAR*>(ULangString.AsUTF8()), ULangString.ByteLen());
	}

	template <typename CharType>
	static TString<CharType> ULangStrToTString(const uLang::CUTF8String& ULangString);

	template <>
	FORCEINLINE TString<TCHAR> ULangStrToTString<TCHAR>(const uLang::CUTF8String& ULangString)
	{
		return ULangStrToFString(ULangString);
	}

	template <>
	FORCEINLINE TString<UTF8CHAR> ULangStrToTString<UTF8CHAR>(const uLang::CUTF8String& ULangString)
	{
		return ULangStrToFUtf8String(ULangString);
	}

	static FORCEINLINE FName ULangStrToFName(const uLang::CUTF8String& ULangString)
	{
		FName Result;
		UE_AUTORTFM_OPEN{ Result = FName(UTF8_TO_TCHAR(ULangString.AsUTF8())); };
		return Result;
	}

	static FORCEINLINE FUtf8StringView ULangStringViewToFUtf8StringView(const uLang::CUTF8StringView& ULangStringView)
	{
		return FUtf8StringView((const UTF8CHAR*)ULangStringView.Data(), ULangStringView.ByteLen());
	}

	static FORCEINLINE FUtf8StringView ULangStrToFUtf8StringView(const uLang::CUTF8String& ULangString)
	{
		return ULangStringViewToFUtf8StringView(ULangString.ToStringView());
	}

	//=== Miscellaneous ===

	/** Converts a UE-style method name (UpperCamelCase) to uLang method name style (snake_case) */
	static FORCEINLINE FString UeToULangFunctionName(const FString& UEFunctionName)
	{
		// Keeping UeToULangFunctionName(), UeToULangDataName() and UeToULangLocalVarName() distinct in case they ever differ.
		return UEFunctionName;
	}

	/** Converts a UE-style data member name (UpperCamelCase) to uLang data member name style (snake_case) */
	static FORCEINLINE FString UeToULangDataName(const FString& UEDataName)
	{
		// Keeping UeToULangFunctionName(), UeToULangDataName() and UeToULangLocalVarName() distinct in case they ever differ.
		return UEDataName;
	}
	/** Converts a UE-style local temporary variable / parameter name (UpperCamelCase) to uLang local temporary variable / parameter name style (snake_case) */
	static FORCEINLINE FString UeToULangLocalVarName(const FString& UELocalVarName)
	{
		// Keeping UeToULangFunctionName(), UeToULangDataName() and UeToULangLocalVarName() distinct in case they ever differ.
		return UELocalVarName;
	}
};

template<typename ValueType>
struct TuLangSymbolKeyFuncs : BaseKeyFuncs<TPair<uLang::CSymbol, ValueType>, uLang::CSymbol>
{
	using Super = BaseKeyFuncs<TPair<uLang::CSymbol, ValueType>, uLang::CSymbol>;

	static FORCEINLINE const uLang::CSymbol& GetSetKey(typename Super::ElementInitType Element)
	{
		return Element.Key;
	}
	static FORCEINLINE bool Matches(const uLang::CSymbol& A, const uLang::CSymbol& B)
	{
		return (A == B);
	}
	static FORCEINLINE uint32 GetKeyHash(const uLang::CSymbol& Key)
	{
		return GetTypeHash(Key.GetId());
	}
};

namespace uLang
{

/**
 * Helper function so STextPosition can be used in a TMap
 */
FORCEINLINE uint32 GetTypeHash(const STextPosition& TextPosition)
{
	return ::GetTypeHash(TextPosition._Row) ^ ::GetTypeHash(TextPosition._Column);
}

}
