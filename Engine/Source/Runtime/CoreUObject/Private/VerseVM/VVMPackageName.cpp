// Copyright Epic Games, Inc. All Rights Reserved.

#include "VerseVM/VVMPackageName.h"
#include "UObject/Object.h"
#include "VerseVM/VVMPackageTypes.h"

namespace Verse
{

FString FPackageName::GetVersePackageNameForVni(const TCHAR* MountPointName, const TCHAR* CppModuleName)
{
	return *Verse::Names::GetVersePackageNameForVni(FStringView(MountPointName), FStringView(CppModuleName));
}

FString FPackageName::GetVersePackageNameForContent(const TCHAR* MountPointName)
{
	return *Verse::Names::GetVersePackageNameForContent(FStringView(MountPointName));
}

FString FPackageName::GetVersePackageNameForPublishedContent(const TCHAR* MountPointName)
{
	return *Verse::Names::GetVersePackageNameForPublishedContent(FStringView(MountPointName));
}

FString FPackageName::GetVersePackageNameForAssets(const TCHAR* MountPointName)
{
	return *Verse::Names::GetVersePackageNameForAssets(FStringView(MountPointName));
}

FString FPackageName::GetVersePackageDirForContent(const TCHAR* MountPointName)
{
	return *Verse::Names::GetVersePackageDirForContent(FStringView(MountPointName));
}

FString FPackageName::GetVersePackageDirForAssets(const TCHAR* MountPointName)
{
	return *Verse::Names::GetVersePackageDirForAssets(FStringView(MountPointName));
}

FString FPackageName::GetUClassPackagePathForVni(const TCHAR* MountPointName, const TCHAR* CppModuleName)
{
	return *Verse::Names::GetUClassPackagePathForVni(FStringView(MountPointName), FStringView(CppModuleName));
}

FString FPackageName::GetUClassPackagePathForContent(const TCHAR* MountPointName, const TCHAR* QualifiedClassName)
{
	return *Verse::Names::GetUClassPackagePathForContent(FStringView(MountPointName), FStringView(QualifiedClassName));
}

FString FPackageName::GetUClassPackagePathForAssets(const TCHAR* MountPointName, const TCHAR* QualifiedClassName)
{
	return *Verse::Names::GetUClassPackagePathForAssets(FStringView(MountPointName), FStringView(QualifiedClassName));
}

FString FPackageName::GetUClassPackagePath(const TCHAR* VersePackageName, const TCHAR* QualifiedClassName, EVersePackageType* OutPackageType /* = nullptr */)
{
	return *Verse::Names::GetUClassPackagePath(FStringView(VersePackageName), FStringView(QualifiedClassName), OutPackageType);
}

FName FPackageName::GetVersePackageNameFromUClassPackagePath(FName UClassPackagePath, EVersePackageType* OutPackageType /* = nullptr */)
{
	FString Path = UClassPackagePath.ToString();
	const TCHAR* Ch = *Path;

	auto ParsePart = [&Ch]() {
		if (*Ch != '/')
		{
			return FString();
		}
		const TCHAR* Begin = ++Ch;
		while (*Ch && *Ch != '/')
		{
			++Ch;
		}
		return FString::ConstructFromPtrSize(Begin, int32(Ch - Begin));
	};

	FString ParsedMountPointName = ParsePart();
	FString ParsedVerseSubPath = ParsePart();
	FString ParsedVniSubPath = ParsePart();
	FString ParsedCppModuleName = ParsePart();

	if (ParsedMountPointName.Len() == 0 || ParsedVerseSubPath != Verse::Names::GetVerseSubPath<TCHAR>())
	{
		return NAME_None;
	}

	// Is this a VNI package?
	if (ParsedVniSubPath == Verse::Names::GetVniSubPath<TCHAR>() && ParsedCppModuleName.Len() > 0)
	{
		// Yes, all VNI classes are combined in a single UPackage with the name of the UBT module
		if (OutPackageType)
		{
			*OutPackageType = EVersePackageType::VNI;
		}
		return FName(ParsedMountPointName / ParsedCppModuleName);
	}

	// Is this an assets package?
	if (ParsedVniSubPath == Verse::Names::GetAssetsSubPath<TCHAR>() && ParsedCppModuleName.Len() > 0)
	{
		// Yes, each class is stored in its own UPackage
		if (OutPackageType)
		{
			*OutPackageType = EVersePackageType::Assets;
		}
		return FName(ParsedMountPointName / Verse::Names::GetAssetsSubPathForPackageName<TCHAR>());
	}

	// Is this a content package?
	if (ParsedVniSubPath.Len() > 0 && ParsedCppModuleName.Len() == 0)
	{
		// Yes, each class is stored in its own UPackage
		if (OutPackageType)
		{
			*OutPackageType = EVersePackageType::Content;
		}
		return FName(ParsedMountPointName);
	}

	return NAME_None;
}

FString FPackageName::GetMountPointName(const TCHAR* VersePackageName)
{
	const TCHAR* Slash = FCString::Strchr(VersePackageName, '/');
	return Slash ? FString::ConstructFromPtrSize(VersePackageName, int32(Slash - VersePackageName)) : FString(VersePackageName);
}

FName FPackageName::GetCppModuleName(const TCHAR* VersePackageName)
{
	const TCHAR* Slash = FCString::Strchr(VersePackageName, '/');
	return Slash ? FName(Slash + 1) : FName();
}

namespace Private
{
template <typename CharType>
EVersePackageType GetPackageType(const CharType* VersePackageName)
{
	// Is this a VNI or assets package?
	const CharType* Slash = TCString<CharType>::Strchr(VersePackageName, (CharType)'/');
	if (Slash)
	{
		// Assets or VNI?
		if (TCString<CharType>::Strcmp(Slash + 1, Verse::Names::GetAssetsSubPathForPackageName<CharType>()) == 0)
		{
			return EVersePackageType::Assets;
		}

		return EVersePackageType::VNI;
	}

	if (TStringView<CharType>(VersePackageName).EndsWith(Verse::Names::GetPublishedPackageNameSuffix<CharType>()))
	{
		return EVersePackageType::PublishedContent;
	}

	// No, each class is stored in its own UPackage
	return EVersePackageType::Content;
}
} // namespace Private

EVersePackageType FPackageName::GetPackageType(const TCHAR* VersePackageName)
{
	return Private::GetPackageType(VersePackageName);
}

EVersePackageType FPackageName::GetPackageType(const UTF8CHAR* VersePackageName)
{
	return Private::GetPackageType(VersePackageName);
}

FString FPackageName::GetTaskUClassName(const TCHAR* OwnerScopeName, const TCHAR* DecoratedAndMangledFunctionName)
{
	//! Must match NativeInterfaceWriter.cpp in GetTaskUClassName()
	return FString::Printf(TEXT("%s%s$%s"), TaskUClassPrefix, OwnerScopeName, DecoratedAndMangledFunctionName);
}

FString FPackageName::GetTaskUClassName(const UObject& OwnerScope, const TCHAR* DecoratedAndMangledFunctionName)
{
	return GetTaskUClassName(*OwnerScope.GetName(), DecoratedAndMangledFunctionName);
}

bool FPackageName::PackageRequiresInternalAPI(const char* Name, const EVersePackageScope VerseScope)
{
	return VerseScope == EVersePackageScope::InternalUser && GetPackageType(reinterpret_cast<const UTF8CHAR*>(Name)) != EVersePackageType::Assets;
}

} // namespace Verse
