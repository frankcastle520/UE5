// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "Elements/Common/TypedElementCommonTypes.h"
#include "Elements/Common/TypedElementHandles.h"
#include "Elements/Common/TypedElementQueryTypes.h"
#include "HAL/Platform.h"
#include "IO/PackageId.h"
#include "Misc/PackagePath.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementPackageColumns.generated.h"

/**
 * A package reference column that has not yet been resolved to reference a package.
 */
USTRUCT(meta = (DisplayName = "Unresolved package path reference"))
struct FTypedElementPackageUnresolvedReference final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	FString PathOnDisk;
	
	UE::Editor::DataStorage::IndexHash Index;
};

/**
 * Column that references a row in the table that provides package and source control information.
 */
USTRUCT(meta = (DisplayName = "Package path reference"))
struct FTypedElementPackageReference final : public FEditorDataStorageColumn
{
	GENERATED_BODY()
	
	UE::Editor::DataStorage::RowHandle Row;
};

/**
 * Tag that indicates some related package information has been modified.
 */
USTRUCT(meta = (DisplayName = "Package information has been updated"))
struct FTypedElementPackageUpdatedTag final : public FEditorDataStorageTag
{
	GENERATED_BODY()
};

/**
 * Column that stores the path of a package.
 */
USTRUCT(meta = (DisplayName = "Package path"))
struct FTypedElementPackagePathColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	UPROPERTY(meta = (Searchable))
	FString Path;
};

inline uint32 GetTypeHash(const FTypedElementPackagePathColumn& InStruct)
{
	return GetTypeHash(InStruct.Path);
}

/**
 * Column that stores the full loading path to a package.
 */
USTRUCT(meta = (DisplayName = "Package loaded path"))
struct FTypedElementPackageLoadedPathColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	FPackagePath LoadedPath;
};
