// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"
#include "VerseVM/VVMVerseClass.h"

class FString;
class UPackage;
class UStruct;
class UEnum;
struct FTopLevelAssetPath;

namespace uLang
{
class CTypeBase;
class CScope;
} // namespace uLang

namespace Verse
{
struct FAllocationContext;
struct VClass;
struct VPackage;
struct VPropertyType;
struct VTupleType;
enum class EPackageStage : uint8;
class CSymbolToResult;

// This interface must be implemented if Verse needs to create UObject instances.
class IEngineEnvironment
{
public:
	// Bind a VNI structure
	virtual void TryBindVniStruct(UStruct* Struct) = 0;

	// Bind a VNI enumeration
	virtual void TryBindVniEnum(UEnum* Enum) = 0;

	// Add persistent vars
	virtual void AddPersistentVars(UObject* Object, const TArray<FVersePersistentVar>& Vars) = 0;

	// Add session vars
	virtual void AddSessionVars(UObject* Object, const TArray<FVerseSessionVar>& Vars) = 0;

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	// Collect property information during code generation.
	virtual VPropertyType* CollectPropertyInfo(FAllocationContext Context, CSymbolToResult* Environment, const uLang::CTypeBase* Type, VPackage* Scope) = 0;

	// Bind a native module, class, or struct.
	virtual void TryBindNativeAsset(FAllocationContext Context, const FTopLevelAssetPath& Path) = 0;

	// Given a UPackage name, adjust the name when the package stage is either DEAD or TEMP.
	virtual const TCHAR* AdornPackageName(const TCHAR* PackageName, EPackageStage Stage, FString& ScratchSpace) = 0;

	// Create a new UPackage with the given package name
	virtual UPackage* CreateUPackage(FAllocationContext Context, const TCHAR* PackageName) = 0;

	// Create a new UClass/UScriptStruct from an existing VClass during native binding or for CVarUObjectProbability.
	virtual void CreateUStruct(FAllocationContext Context, VClass* Class, TWriteBarrier<VValue>& Result) = 0;

	// Create a new UScriptStruct for a given tuple type
	virtual void CreateUStruct(FAllocationContext Context, VTupleType* Tuple, VPackage* Scope, TWriteBarrier<VValue>& Result) = 0;
#endif // WITH_VERSE_VM
};
} // namespace Verse
