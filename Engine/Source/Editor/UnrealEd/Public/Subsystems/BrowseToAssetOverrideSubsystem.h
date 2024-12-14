// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "Delegates/Delegate.h"
#include "EditorSubsystem.h"

#include "BrowseToAssetOverrideSubsystem.generated.h"

using FBrowseToAssetOverrideDelegate = TDelegate<FName(const UObject*)>;

UCLASS(MinimalAPI)
class UBrowseToAssetOverrideSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	static UNREALED_API UBrowseToAssetOverrideSubsystem* Get();

	/**
	 * Given an object, see if it has a "browse to asset" package name override.
	 * @return The package name of override, or None if there is no override.
	 */
	UNREALED_API FName GetBrowseToAssetOverride(const UObject* Object);

	/**
	 * Register a per-class override for the "browse to asset" resolution.
	 * @note Callback should return a package name, or None if there is no override.
	 */
	UNREALED_API void RegisterBrowseToAssetOverrideForClass(const FTopLevelAssetPath& Class, FBrowseToAssetOverrideDelegate&& Callback);

	template <typename ClassType>
	void RegisterBrowseToAssetOverrideForClass(FBrowseToAssetOverrideDelegate&& Callback)
	{
		RegisterBrowseToAssetOverrideForClass(ClassType::StaticClass()->GetClassPathName(), MoveTemp(Callback));
	}
	
	/**
	 * Unregister a per-class override for the "browse to asset" resolution.
	 */
	UNREALED_API void UnregisterBrowseToAssetOverrideForClass(const FTopLevelAssetPath& Class);

	template <typename ClassType>
	void UnregisterBrowseToAssetOverrideForClass()
	{
		UnregisterBrowseToAssetOverrideForClass(ClassType::StaticClass()->GetClassPathName());
	}
	
private:
	TMap<FTopLevelAssetPath, FBrowseToAssetOverrideDelegate> PerClassOverrides;
};
