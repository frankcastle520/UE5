// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"
#include "Containers/Utf8String.h"
#include "CoreMinimal.h"
#include "VerseVM/VVMPackageTypes.h"

namespace Verse::Names
{
static constexpr int32 DefaultNameLength = 64;

//--------------------------------------------------------------------------------------------------------------------
// Private helper methods that should not be used outside of Verse/Solaris code
//--------------------------------------------------------------------------------------------------------------------

namespace Private
{

//--------------------------------------------------------------------------------------------------------------------
// Name mangling to make a cased name a cassless name
//--------------------------------------------------------------------------------------------------------------------

// The following method are intended to take a case sensitive name (which maybe already adorned with package information)
// and convert it into a case insensitive name.
COREUOBJECT_API FString MangleCasedName(FStringView Name, bool* bOutNameWasMangled = nullptr);
COREUOBJECT_API FString UnmangleCasedName(const FName MaybeMangledName, bool* bOutNameWasMangled = nullptr);

//--------------------------------------------------------------------------------------------------------------------
// Encoding and decoding
//--------------------------------------------------------------------------------------------------------------------

// Encode and decode a verse names.  This is currently only used to encode functions.
// The method takes characters that could be considered invalid for UE names and makes them valid
COREUOBJECT_API FUtf8String EncodeName(FUtf8StringView Path);
COREUOBJECT_API FString EncodeName(FStringView Path);
COREUOBJECT_API FUtf8String DecodeName(FUtf8StringView Path);
COREUOBJECT_API FString DecodeName(FStringView Path);
} // namespace Private

//--------------------------------------------------------------------------------------------------------------------
// String constants
//--------------------------------------------------------------------------------------------------------------------

// Create Get methods to return the given constant strings
#define UE_MAKE_CONSTANT_STRING_METHODS(Name, Text)   \
	template <typename CharType>                      \
	const CharType* Get##Name();                      \
	template <>                                       \
	FORCEINLINE const UTF8CHAR* Get##Name<UTF8CHAR>() \
	{                                                 \
		return UTF8TEXT(Text);                        \
	}                                                 \
	template <>                                       \
	FORCEINLINE const TCHAR* Get##Name<TCHAR>()       \
	{                                                 \
		return TEXT(Text);                            \
	}

UE_MAKE_CONSTANT_STRING_METHODS(VerseSubPath, "_Verse")
UE_MAKE_CONSTANT_STRING_METHODS(VniSubPath, "VNI")
UE_MAKE_CONSTANT_STRING_METHODS(AssetsSubPath, "Assets")
UE_MAKE_CONSTANT_STRING_METHODS(AssetsSubPathForPackageName, "Assets")
UE_MAKE_CONSTANT_STRING_METHODS(PublishedPackageNameSuffix, "-Published")

#undef UE_MAKE_CONSTANT_STRING_METHODS

//--------------------------------------------------------------------------------------------------------------------
// UE Package names for Verse
//--------------------------------------------------------------------------------------------------------------------

// The following methods assist in generating the UE package names
COREUOBJECT_API TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetDecoratedName(FUtf8StringView Path, FUtf8StringView Module, FUtf8StringView Name);
COREUOBJECT_API TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetDecoratedName(FStringView Path, FStringView Module, FStringView Name);

COREUOBJECT_API TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetDecoratedName(FUtf8StringView Path, FUtf8StringView Name);
COREUOBJECT_API TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetDecoratedName(FStringView Path, FStringView Name);

COREUOBJECT_API TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetVersePackageNameForVni(FUtf8StringView MountPointName, FUtf8StringView CppModuleName);
COREUOBJECT_API TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetVersePackageNameForVni(FStringView MountPointName, FStringView CppModuleName);

COREUOBJECT_API TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetVersePackageNameForContent(FUtf8StringView MountPointName);
COREUOBJECT_API TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetVersePackageNameForContent(FStringView MountPointName);

COREUOBJECT_API TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetVersePackageNameForPublishedContent(FUtf8StringView MountPointName);
COREUOBJECT_API TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetVersePackageNameForPublishedContent(FStringView MountPointName);

COREUOBJECT_API TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetVersePackageNameForAssets(FUtf8StringView MountPointName);
COREUOBJECT_API TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetVersePackageNameForAssets(FStringView MountPointName);

COREUOBJECT_API TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetVersePackageDirForContent(FUtf8StringView MountPointName);
COREUOBJECT_API TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetVersePackageDirForContent(FStringView MountPointName);

COREUOBJECT_API TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetVersePackageDirForAssets(FUtf8StringView MountPointName);
COREUOBJECT_API TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetVersePackageDirForAssets(FStringView MountPointName);

COREUOBJECT_API TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetUClassPackagePathForVni(FUtf8StringView MountPointName, FUtf8StringView CppModuleName);
COREUOBJECT_API TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetUClassPackagePathForVni(FStringView MountPointName, FStringView CppModuleName);

COREUOBJECT_API TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetUClassPackagePathForContent(FUtf8StringView MountPointName, FUtf8StringView QualifiedClassName);
COREUOBJECT_API TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetUClassPackagePathForContent(FStringView MountPointName, FStringView QualifiedClassName);

COREUOBJECT_API TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetUClassPackagePathForAssets(FUtf8StringView MountPointName, FUtf8StringView QualifiedClassName);
COREUOBJECT_API TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetUClassPackagePathForAssets(FStringView MountPointName, FStringView QualifiedClassName);

COREUOBJECT_API TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetUClassPackagePath(FUtf8StringView VersePackageName, FUtf8StringView QualifiedClassName, EVersePackageType* OutPackageType = nullptr);
COREUOBJECT_API TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetUClassPackagePath(FStringView VersePackageName, FStringView QualifiedClassName, EVersePackageType* OutPackageType = nullptr);

//--------------------------------------------------------------------------------------------------------------------
// Verse path helper methods
//--------------------------------------------------------------------------------------------------------------------

// Test to see if the given path is a full Verse path (begins with open parenthesis)
inline bool IsFullPath(FUtf8StringView Name)
{
	return Name.Len() > 0 && Name[0] == UTF8CHAR('(');
}
inline bool IsFullPath(FStringView Name)
{
	return Name.Len() > 0 && Name[0] == TCHAR('(');
}

//--------------------------------------------------------------------------------------------------------------------
// Property name conversions
//
// NOTE: VVMULangNames.h contains helper methods specific to uLang types
//--------------------------------------------------------------------------------------------------------------------

// Convert a Verse property name to a UE name as a string
// If bWasVerseName is true, then the name needed to be modified to be used as a UE name
COREUOBJECT_API FString VersePropToUEName(FStringView VerseName, bool* bWasVerseName = nullptr);

// Convert a Verse property name to a UE name as an FName.  If the resulting name is too long, the engine will check.
// If bWasVerseName is true, then the name needed to be modified to be used as a UE name
COREUOBJECT_API FName VersePropToUEFName(FStringView VerseName, bool* bWasVerseName = nullptr);

// Convert a UE property name to the original Verse name.
// WARNING: The resulting string is case sensitive and should NEVER be converted to an FName
// If bIsVerseName is true, then the UE name was originally a verse name.
COREUOBJECT_API FString UEPropToVerseName(FStringView UEName, bool* bIsVerseName = nullptr);
COREUOBJECT_API FString UEPropToVerseName(FName UEName, bool* bIsVerseName = nullptr);

// WARNING: This version is commonly used to signal that the code is depending on the verse name
// being stored in an FName which is not valid.
COREUOBJECT_API FName UEPropToVerseFName(FStringView UEName, bool* bIsVerseName = nullptr);
COREUOBJECT_API FName UEPropToVerseFName(FName UEName, bool* bIsVerseName = nullptr);

//--------------------------------------------------------------------------------------------------------------------
// Function name conversions
//
// NOTE: VVMULangNames.h contains helper methods specific to uLang types
//--------------------------------------------------------------------------------------------------------------------

// Convert a Verse function name to a UE name as a string
COREUOBJECT_API FString VerseFuncToUEName(FStringView VerseName);

// Convert a Verse function name to a UE name as an FName.  If the resulting name is too long, the engine will check.
COREUOBJECT_API FName VerseFuncToUEFName(FStringView VerseName);

// Convert a UE function name to the original Verse name.
// WARNING: The resulting string is case sensitive and should NEVER be converted to an FName
// If bIsVerseName is true, then the UE name was originally a verse name.
COREUOBJECT_API FString UEFuncToVerseName(FStringView UEName);
COREUOBJECT_API FString UEFuncToVerseName(FName UEName);

} // namespace Verse::Names
