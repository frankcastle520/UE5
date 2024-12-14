// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VerseVM/VVMNames.h"

class UObject;

enum class EVersePackageScope : uint8;
enum class EVersePackageType : uint8;

namespace Verse
{

class COREUOBJECT_API FPackageName
{
public:
	// The following methods are being deprecated and should use Verse::Names methods found in VVMNames.h
	static FString GetVersePackageNameForVni(const TCHAR* MountPointName, const TCHAR* CppModuleName);
	static FString GetVersePackageNameForContent(const TCHAR* MountPointName);
	static FString GetVersePackageNameForPublishedContent(const TCHAR* MountPointName);
	static FString GetVersePackageNameForAssets(const TCHAR* MountPointName);
	static FString GetVersePackageDirForContent(const TCHAR* MountPointName);
	static FString GetVersePackageDirForAssets(const TCHAR* MountPointName);
	static FString GetUClassPackagePath(const TCHAR* VersePackageName, const TCHAR* QualifiedClassName, EVersePackageType* OutPackageType = nullptr);
	static FString GetUClassPackagePathForVni(const TCHAR* MountPointName, const TCHAR* CppModuleName);
	static FString GetUClassPackagePathForContent(const TCHAR* MountPointName, const TCHAR* QualifiedClassName);
	static FString GetUClassPackagePathForAssets(const TCHAR* MountPointName, const TCHAR* QualifiedClassName);

	// The following methods don't have a Verse::Names version yet
	static FName GetVersePackageNameFromUClassPackagePath(FName UClassPackagePath, EVersePackageType* OutPackageType = nullptr); // Reverse of what the above function does
	static FString GetMountPointName(const TCHAR* VersePackageName);
	static FName GetCppModuleName(const TCHAR* VersePackageName);
	static EVersePackageType GetPackageType(const TCHAR* VersePackageName);
	static EVersePackageType GetPackageType(const UTF8CHAR* VersePackageName);

	static FString GetTaskUClassName(const TCHAR* OwnerScopeName, const TCHAR* DecoratedAndMangledFunctionName);
	static FString GetTaskUClassName(const UObject& OwnerScope, const TCHAR* DecoratedAndMangledFunctionName);

	static bool PackageRequiresInternalAPI(const char* Name, const EVersePackageScope VerseScope);

	// Class name substitute for root module classes of a package
	static constexpr char const* const RootModuleClassName = "_Root"; // Keep in sync with RootModuleClassName in NativeInterfaceWriter.cpp

	// Prefix Constants
	static constexpr TCHAR const* const TaskUClassPrefix = TEXT("task_");
};

} // namespace Verse
