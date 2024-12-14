// Copyright Epic Games, Inc. All Rights Reserved.

#include "VerseVM/VVMNames.h"
#include "Containers/StringConv.h"
#include "Containers/Utf8String.h"
#include "Misc/StringBuilder.h"

namespace Verse::Names
{

namespace Private
{
// Reserved name prefixes which will not be mangled.
#define VERSE_MANGLED_PREFIX "__verse_0x"
const TCHAR* const InternalNames[] =
	{
		// Avoid recursive mangling
		TEXT(VERSE_MANGLED_PREFIX),

		// Generated names, no need to mangle
		TEXT("RetVal"),
		TEXT("_RetVal"),
		TEXT("$TEMP"),
		TEXT("_Self"),
};

bool ShouldMangleCasedName(FStringView Name)
{
	for (int32 i = 0; i < UE_ARRAY_COUNT(InternalNames); ++i)
	{
		if (Name.StartsWith(InternalNames[i]))
		{
			return false;
		}
	}
	return true;
}

FString MangleCasedName(FStringView Name, bool* bOutNameWasMangled /* = nullptr */)
{
	FString Result;
	const bool bNameWasMangled = Private::ShouldMangleCasedName(Name);
	if (bNameWasMangled)
	{
		Result = TEXT(VERSE_MANGLED_PREFIX);
		auto AnsiString = StringCast<ANSICHAR>(Name.GetData(), Name.Len());
		const uint32 Crc = FCrc::StrCrc32Len(AnsiString.Get(), AnsiString.Length());
		Result.Append(BytesToHex(reinterpret_cast<const uint8*>(&Crc), sizeof(Crc)));
		Result.Append(TEXT("_"));
		Result.Append(Name);
	}
	else
	{
		Result = Name;
	}

	if (bOutNameWasMangled)
	{
		*bOutNameWasMangled = bNameWasMangled;
	}
	return Result;
}

FString UnmangleCasedName(const FName MaybeMangledName, bool* bOutNameWasMangled /* = nullptr */)
{
	FString Result = MaybeMangledName.ToString();
	bool bNameWasMangled = Result.StartsWith(TEXT("__verse_0x"));
	if (bNameWasMangled)
	{
		// chop "__verse_0x" (10 char) + CRC (8 char) + "_" (1 char)
		Result = Result.RightChop(19);
	}

	if (bOutNameWasMangled)
	{
		*bOutNameWasMangled = bNameWasMangled;
	}
	return Result;
}

#undef VERSE_MANGLED_PREFIX

// NOTE: This method is a duplicate of uLang::CppMangling::Mangle
FUtf8String EncodeName(FUtf8StringView Path)
{
	TStringBuilderWithBuffer<UTF8CHAR, 128> Builder;

	bool bIsFirstChar = true;
	while (!Path.IsEmpty())
	{
		const UTF8CHAR Char = Path[0];
		Path.RightChopInline(1);

		if ((Char >= 'a' && Char <= 'z')
			|| (Char >= 'A' && Char <= 'Z')
			|| (Char >= '0' && Char <= '9' && !bIsFirstChar))
		{
			Builder.AppendChar(Char);
		}
		else if (Char == '[' && !Path.IsEmpty() && Path[0] == ']')
		{
			Path.RightChopInline(1);
			Builder.Append("_K");
		}
		else if (Char == '-' && !Path.IsEmpty() && Path[0] == '>')
		{
			Path.RightChopInline(1);
			Builder.Append("_T");
		}
		else if (Char == '_')
		{
			Builder.Append("__");
		}
		else if (Char == '(')
		{
			Builder.Append("_L");
		}
		else if (Char == ',')
		{
			Builder.Append("_M");
		}
		else if (Char == ':')
		{
			Builder.Append("_N");
		}
		else if (Char == '^')
		{
			Builder.Append("_P");
		}
		else if (Char == '?')
		{
			Builder.Append("_Q");
		}
		else if (Char == ')')
		{
			Builder.Append("_R");
		}
		else if (Char == '\'')
		{
			Builder.Append("_U");
		}
		else
		{
			Builder.Appendf(UTF8TEXT("_%.2x"), uint8_t(Char));
		}

		bIsFirstChar = false;
	}

	return FUtf8String(Builder.ToView());
}

FString EncodeName(FStringView Path)
{
	return FString(EncodeName(StrCast<UTF8CHAR>(Path.GetData(), Path.Len())));
}

// NOTE: This method is a duplicate of uLang::CppMangling::Demangle
FUtf8String DecodeName(FUtf8StringView Path)
{
	TStringBuilderWithBuffer<UTF8CHAR, 128> Builder;

	while (!Path.IsEmpty())
	{
		const UTF8CHAR Char = Path[0];
		if (Char != '_' || Path.Len() < 2)
		{
			Builder.AppendChar(Char);
			Path.RightChopInline(1);
		}
		else
		{
			// Handle escape codes prefixed by underscore.
			struct FEscapeCode
			{
				char Escaped;
				const char* Unescaped;
			};
			static const FEscapeCode EscapeCodes[] =
				{
					{'_',  "_"},
					{'K', "[]"},
					{'L',  "("},
					{'M',  ","},
					{'N',  ":"},
					{'P',  "^"},
					{'Q',  "?"},
					{'R',  ")"},
					{'T', "->"},
					{'U', "\'"},
            };
			bool bHandledEscapeCode = false;
			const UTF8CHAR Escaped = Path[1];
			for (const FEscapeCode& EscapeCode : EscapeCodes)
			{
				if (Escaped == (UTF8CHAR)EscapeCode.Escaped)
				{
					Builder.Append(EscapeCode.Unescaped);
					Path.RightChopInline(2);
					bHandledEscapeCode = true;
					break;
				}
			}
			if (!bHandledEscapeCode)
			{
				// Handle hexadecimal escapes.
				if (Path.Len() < 3)
				{
					Builder.Append(Path);
					Path.Reset();
				}
				else
				{
					auto ParseHexit = [](const UTF8CHAR Hexit) -> int {
						return (Hexit >= '0' && Hexit <= '9') ? (Hexit - '0')
							 : (Hexit >= 'a' && Hexit <= 'f') ? (Hexit - 'a' + 10)
							 : (Hexit >= 'A' && Hexit <= 'F') ? (Hexit - 'A' + 10)
															  : -1;
					};

					int Hexit1 = ParseHexit(Path[1]);
					int Hexit2 = ParseHexit(Path[2]);
					if (Hexit1 == -1 || Hexit2 == -1)
					{
						Builder.Append(Path.Left(3));
					}
					else
					{
						Builder.AppendChar(UTF8CHAR(Hexit1 * 16 + Hexit2));
					}

					Path.RightChopInline(3);
				}
			}
		}
	}
	return FUtf8String(Builder.ToView());
}

FString DecodeName(FStringView Path)
{
	return FString(DecodeName(StrCast<UTF8CHAR>(Path.GetData(), Path.Len())));
}

} // namespace Private

namespace Private
{
template <typename CharType>
FORCEINLINE_DEBUGGABLE TStringBuilderWithBuffer<CharType, DefaultNameLength> GetDecoratedName(TStringView<CharType> Path, TStringView<CharType> Module, TStringView<CharType> Name)
{
	if (!Module.IsEmpty())
	{
		return TStringBuilderWithBuffer<CharType, DefaultNameLength>(InPlace, '(', Path, '/', Module, ":)", Name);
	}
	else
	{
		return TStringBuilderWithBuffer<CharType, DefaultNameLength>(InPlace, '(', Path, ":)", Name);
	}
}

template <typename CharType>
FORCEINLINE_DEBUGGABLE TStringBuilderWithBuffer<CharType, DefaultNameLength> GetDecoratedName(TStringView<CharType> Path, TStringView<CharType> Name)
{
	return GetDecoratedName(Path, TStringView<CharType>(), Name);
}

template <typename CharType>
FORCEINLINE_DEBUGGABLE TStringBuilderWithBuffer<CharType, DefaultNameLength> GetVersePackageNameForVni(TStringView<CharType> MountPointName, TStringView<CharType> CppModuleName)
{
	return TStringBuilderWithBuffer<CharType, DefaultNameLength>(InPlace, MountPointName, '/', CppModuleName);
}

template <typename CharType>
FORCEINLINE_DEBUGGABLE TStringBuilderWithBuffer<CharType, DefaultNameLength> GetVersePackageNameForContent(TStringView<CharType> MountPointName)
{
	return TStringBuilderWithBuffer<CharType, DefaultNameLength>(InPlace, MountPointName);
}

template <typename CharType>
FORCEINLINE_DEBUGGABLE TStringBuilderWithBuffer<CharType, DefaultNameLength> GetVersePackageNameForPublishedContent(TStringView<CharType> MountPointName)
{
	return TStringBuilderWithBuffer<CharType, DefaultNameLength>(InPlace, MountPointName, GetPublishedPackageNameSuffix<CharType>());
}

template <typename CharType>
FORCEINLINE_DEBUGGABLE TStringBuilderWithBuffer<CharType, DefaultNameLength> GetVersePackageNameForAssets(TStringView<CharType> MountPointName)
{
	return TStringBuilderWithBuffer<CharType, DefaultNameLength>(InPlace, MountPointName, '/', GetAssetsSubPathForPackageName<CharType>());
}

template <typename CharType>
FORCEINLINE_DEBUGGABLE TStringBuilderWithBuffer<CharType, DefaultNameLength> GetVersePackageDirForContent(TStringView<CharType> MountPointName)
{
	return TStringBuilderWithBuffer<CharType, DefaultNameLength>(InPlace, '/', MountPointName, '/', GetVerseSubPath<CharType>(), '/');
}

template <typename CharType>
FORCEINLINE_DEBUGGABLE TStringBuilderWithBuffer<CharType, DefaultNameLength> GetVersePackageDirForAssets(TStringView<CharType> MountPointName)
{
	return TStringBuilderWithBuffer<CharType, DefaultNameLength>(InPlace, '/', MountPointName, '/', GetVerseSubPath<CharType>(), '/', GetAssetsSubPath<CharType>(), '/');
}

template <typename CharType>
FORCEINLINE_DEBUGGABLE TStringBuilderWithBuffer<CharType, DefaultNameLength> GetUClassPackagePathForVni(TStringView<CharType> MountPointName, TStringView<CharType> CppModuleName)
{
	return TStringBuilderWithBuffer<CharType, DefaultNameLength>(InPlace, '/', MountPointName, '/', GetVerseSubPath<CharType>(), '/', GetVniSubPath<CharType>(), '/', CppModuleName);
}

template <typename CharType>
FORCEINLINE_DEBUGGABLE TStringBuilderWithBuffer<CharType, DefaultNameLength> GetUClassPackagePathForContent(TStringView<CharType> MountPointName, TStringView<CharType> QualifiedClassName)
{
	return TStringBuilderWithBuffer<CharType, DefaultNameLength>(InPlace, '/', MountPointName, '/', GetVerseSubPath<CharType>(), '/', QualifiedClassName);
}

template <typename CharType>
FORCEINLINE_DEBUGGABLE TStringBuilderWithBuffer<CharType, DefaultNameLength> GetUClassPackagePathForAssets(TStringView<CharType> MountPointName, TStringView<CharType> QualifiedClassName)
{
	return TStringBuilderWithBuffer<CharType, DefaultNameLength>(InPlace, '/', MountPointName, '/', GetVerseSubPath<CharType>(), '/', GetAssetsSubPath<CharType>(), '/', QualifiedClassName);
}

template <typename CharType>
FORCEINLINE_DEBUGGABLE TStringBuilderWithBuffer<CharType, DefaultNameLength> GetUClassPackagePath(TStringView<CharType> VersePackageName, TStringView<CharType> QualifiedClassName, EVersePackageType* OutPackageType = nullptr)
{
	ensure(!QualifiedClassName.IsEmpty()); // Must not be the empty string

	// Ast package names are either
	// "<plugin_name>" for the content Verse package in a plugin, or
	// "<plugin_name>/<vni_module_name>" for VNI Verse packages inside plugins
	// "<plugin_name>/Assets" for reflected assets Verse packages inside plugins

	// Is this a VNI or assets package?
	const CharType* Slash = TCString<CharType>::Strchr(VersePackageName.GetData(), CharType('/'));
	if (Slash)
	{
		// Assets or VNI?
		if (TCString<CharType>::Strcmp(Slash + 1, GetAssetsSubPathForPackageName<CharType>()) == 0)
		{
			// Assets, each class is stored in its own UPackage
			if (OutPackageType)
			{
				*OutPackageType = EVersePackageType::Assets;
			}
			return GetUClassPackagePathForAssets(VersePackageName.Left(int32(Slash - VersePackageName.GetData())), QualifiedClassName);
		}

		// VNI: All VNI classes are combined in a single UPackage with the name of the UBT module
		if (OutPackageType)
		{
			*OutPackageType = EVersePackageType::VNI;
		}
		return GetUClassPackagePathForVni(VersePackageName.Left(int32(Slash - VersePackageName.GetData())), TStringView<CharType>(Slash + 1));
	}

	// No, each class is stored in its own UPackage
	if (OutPackageType)
	{
		*OutPackageType = EVersePackageType::Content;
	}
	TString<CharType> ContentClassName(QualifiedClassName);
	ContentClassName.ReplaceCharInline(CharType('.'), CharType('_'), ESearchCase::CaseSensitive);
	return GetUClassPackagePathForContent(VersePackageName, TStringView<CharType>(ContentClassName));
}

} // namespace Private

TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetDecoratedName(FUtf8StringView Path, FUtf8StringView Module, FUtf8StringView Name)
{
	return Private::GetDecoratedName(Path, Module, Name);
}

TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetDecoratedName(FStringView Path, FStringView Module, FStringView Name)
{
	return Private::GetDecoratedName(Path, Module, Name);
}

TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetDecoratedName(FUtf8StringView Path, FUtf8StringView Name)
{
	return Private::GetDecoratedName(Path, Name);
}

TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetDecoratedName(FStringView Path, FStringView Name)
{
	return Private::GetDecoratedName(Path, Name);
}

TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetVersePackageNameForVni(FUtf8StringView MountPointName, FUtf8StringView CppModuleName)
{
	return Private::GetVersePackageNameForVni(MountPointName, CppModuleName);
}

TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetVersePackageNameForVni(FStringView MountPointName, FStringView CppModuleName)
{
	return Private::GetVersePackageNameForVni(MountPointName, CppModuleName);
}

TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetVersePackageNameForContent(FUtf8StringView MountPointName)
{
	return Private::GetVersePackageNameForContent(MountPointName);
}

TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetVersePackageNameForContent(FStringView MountPointName)
{
	return Private::GetVersePackageNameForContent(MountPointName);
}

TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetVersePackageNameForPublishedContent(FUtf8StringView MountPointName)
{
	return Private::GetVersePackageNameForPublishedContent(MountPointName);
}

TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetVersePackageNameForPublishedContent(FStringView MountPointName)
{
	return Private::GetVersePackageNameForPublishedContent(MountPointName);
}

TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetVersePackageNameForAssets(FUtf8StringView MountPointName)
{
	return Private::GetVersePackageNameForAssets(MountPointName);
}

TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetVersePackageNameForAssets(FStringView MountPointName)
{
	return Private::GetVersePackageNameForAssets(MountPointName);
}

TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetVersePackageDirForContent(FUtf8StringView MountPointName)
{
	return Private::GetVersePackageDirForContent(MountPointName);
}

TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetVersePackageDirForContent(FStringView MountPointName)
{
	return Private::GetVersePackageDirForContent(MountPointName);
}

TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetVersePackageDirForAssets(FUtf8StringView MountPointName)
{
	return Private::GetVersePackageDirForAssets(MountPointName);
}

TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetVersePackageDirForAssets(FStringView MountPointName)
{
	return Private::GetVersePackageDirForAssets(MountPointName);
}

TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetUClassPackagePathForVni(FUtf8StringView MountPointName, FUtf8StringView CppModuleName)
{
	return Private::GetUClassPackagePathForVni(MountPointName, CppModuleName);
}

TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetUClassPackagePathForVni(FStringView MountPointName, FStringView CppModuleName)
{
	return Private::GetUClassPackagePathForVni(MountPointName, CppModuleName);
}

TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetUClassPackagePathForContent(FUtf8StringView MountPointName, FUtf8StringView QualifiedClassName)
{
	return Private::GetUClassPackagePathForContent(MountPointName, QualifiedClassName);
}

TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetUClassPackagePathForContent(FStringView MountPointName, FStringView QualifiedClassName)
{
	return Private::GetUClassPackagePathForContent(MountPointName, QualifiedClassName);
}

TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetUClassPackagePathForAssets(FUtf8StringView MountPointName, FUtf8StringView QualifiedClassName)
{
	return Private::GetUClassPackagePathForAssets(MountPointName, QualifiedClassName);
}

TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetUClassPackagePathForAssets(FStringView MountPointName, FStringView QualifiedClassName)
{
	return Private::GetUClassPackagePathForAssets(MountPointName, QualifiedClassName);
}

TStringBuilderWithBuffer<UTF8CHAR, DefaultNameLength> GetUClassPackagePath(FUtf8StringView VersePackageName, FUtf8StringView QualifiedClassName, EVersePackageType* OutPackageType /* = nullptr */)
{
	return Private::GetUClassPackagePath(VersePackageName, QualifiedClassName, OutPackageType);
}

TStringBuilderWithBuffer<TCHAR, DefaultNameLength> GetUClassPackagePath(FStringView VersePackageName, FStringView QualifiedClassName, EVersePackageType* OutPackageType /* = nullptr */)
{
	return Private::GetUClassPackagePath(VersePackageName, QualifiedClassName, OutPackageType);
}

FString VersePropToUEName(FStringView VerseName, bool* bWasVerseName /* = nullptr */)
{
	FString Result;
	bool bModified = false;

	// If we are creating a property based on
	if (IsFullPath(VerseName))
	{
		bModified = true;
		Result = Private::MangleCasedName(Private::EncodeName(VerseName));
	}
	else
	{
		Result = Private::MangleCasedName(VerseName, &bModified);
	}

	if (bWasVerseName != nullptr)
	{
		*bWasVerseName = bModified;
	}
	return Result;
}

FName VersePropToUEFName(FStringView VerseName, bool* bWasVerseName /* = nullptr */)
{
	return FName(VersePropToUEName(VerseName, bWasVerseName));
}

FString UEPropToVerseName(FStringView UEName, bool* bIsVerseName /* = nullptr */)
{

	// Strip any adornment for cased names
	bool bModified = false;
	if (UEName.StartsWith(TEXT("__verse_0x")))
	{
		bModified = true;
		// chop "__verse_0x" (10 char) + CRC (8 char) + "_" (1 char)
		UEName = UEName.RightChop(19);
	}

	if (bIsVerseName != nullptr)
	{
		*bIsVerseName = bModified;
	}
	return FString(UEName);
}

FString UEPropToVerseName(FName UEName, bool* bIsVerseName /* = nullptr */)
{
	return UEPropToVerseName(UEName.ToString(), bIsVerseName);
}

FName UEPropToVerseFName(FName UEName, bool* bIsVerseName /* = nullptr */)
{
	bool bScratchIsVerseName;
	FString VerseName = UEPropToVerseName(UEName, &bScratchIsVerseName);
	if (bIsVerseName != nullptr)
	{
		*bIsVerseName = bScratchIsVerseName;
	}
	return bScratchIsVerseName ? FName(VerseName) : UEName;
}

FName UEPropToVerseFName(FStringView UEName, bool* bIsVerseName /* = nullptr */)
{
	return FName(UEPropToVerseName(UEName, bIsVerseName));
}

FString VerseFuncToUEName(FStringView VerseName)
{
	return Private::EncodeName(VerseName);
}

FName VerseFuncToUEFName(FStringView VerseName)
{
	return FName(VerseFuncToUEName(VerseName));
}

FString UEFuncToVerseName(FStringView UEName)
{
	return Private::DecodeName(UEName);
}

FString UEFuncToVerseName(FName UEName)
{
	return UEFuncToVerseName(UEName.ToString());
}

} // namespace Verse::Names
