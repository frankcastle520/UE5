// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorScriptingUtils.h"
#include "Algo/Count.h"
#include "Algo/IndexOf.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "HAL/PlatformFile.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"


DEFINE_LOG_CATEGORY(LogEditorScripting);

namespace EditorScriptingUtils
{
	bool CheckIfInEditorAndPIE()
	{
		return IsInEditorAndNotPlaying();
	}

	bool IsInEditorAndNotPlaying()
	{
		if (!IsInGameThread())
		{
			UE_LOG(LogEditorScripting, Error, TEXT("You are not on the main thread."));
			return false;
		}
		if (!GIsEditor)
		{
			UE_LOG(LogEditorScripting, Error, TEXT("You are not in the Editor."));
			return false;
		}
		if (GEditor->PlayWorld || GIsPlayInEditorWorld)
		{
			UE_LOG(LogEditorScripting, Error, TEXT("The Editor is currently in a play mode."));
			return false;
		}
		return true;
	}

	bool IsPackageFlagsSupportedForAssetLibrary(uint32 PackageFlags)
	{
		return (PackageFlags & (PKG_ContainsMap | PKG_PlayInEditor | PKG_ContainsMapData)) == 0;
	}

	// Test for invalid characters
	bool IsAValidPath(const FString& Path, const TCHAR* InvalidChar, FString& OutFailureReason)
	{
		// Like !FName::IsValidGroupName(Path)), but with another list and no conversion to from FName
		// InvalidChar may be INVALID_OBJECTPATH_CHARACTERS or INVALID_LONGPACKAGE_CHARACTERS or ...
		const int32 StrLen = FCString::Strlen(InvalidChar);
		for (int32 Index = 0; Index < StrLen; ++Index)
		{
			int32 FoundIndex = 0;
			if (Path.FindChar(InvalidChar[Index], FoundIndex))
			{
				OutFailureReason = FString::Printf(TEXT("Can't convert the path %s because it contains invalid characters."), *Path);
				return false;
			}
		}

		if (Path.Len() > FPlatformMisc::GetMaxPathLength())
		{
			OutFailureReason = FString::Printf(TEXT("Can't convert the path because it is too long (%d characters). This may interfere with cooking for consoles. Unreal filenames should be no longer than %d characters. Full path value: %s"), Path.Len(), FPlatformMisc::GetMaxPathLength(), *Path);
			return false;
		}
		return true;
	}

	bool IsAValidPathForCreateNewAsset(const FString& ObjectPath, FString& OutFailureReason)
	{
		const FString ObjectName = FPackageName::ObjectPathToPathWithinPackage(ObjectPath);

		// Make sure the name is not already a class or otherwise invalid for saving
		FText FailureReason;
		if (!FFileHelper::IsFilenameValidForSaving(ObjectName, FailureReason))
		{
			OutFailureReason = FailureReason.ToString();
			return false;
		}

		// Make sure the new name only contains valid characters
		if (!FName::IsValidXName(ObjectName, INVALID_OBJECTNAME_CHARACTERS INVALID_LONGPACKAGE_CHARACTERS, &FailureReason))
		{
			OutFailureReason = FailureReason.ToString();
			return false;
		}

		// Make sure we are not creating an FName that is too large
		if (ObjectPath.Len() >= NAME_SIZE)
		{
			OutFailureReason = TEXT("This asset name is too long (") + FString::FromInt(ObjectPath.Len()) + TEXT(" characters), the maximum is ") + FString::FromInt(NAME_SIZE) + TEXT(". Please choose a shorter name.");
			return false;
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
		if (AssetData.IsValid())
		{
			OutFailureReason = TEXT("An asset already exists at this location.");
			return false;
		}

		return true;
	}

	/** Remove Class from "Class /Game/MyFolder/MyAsset" */
	FString RemoveFullName(const FString& AnyAssetPath, FString& OutFailureReason)
	{
		FString Result = AnyAssetPath.TrimStartAndEnd();
		int32 NumberOfSpace = Algo::Count(AnyAssetPath, TEXT(' '));

		if (NumberOfSpace == 0)
		{
			return MoveTemp(Result);
		}
		else if (NumberOfSpace > 1)
		{
			OutFailureReason = FString::Printf(TEXT("Can't convert path '%s' because there are too many spaces."), *AnyAssetPath);
			return FString();
		}
		else// if (NumberOfSpace == 1)
		{
			int32 FoundIndex = 0;
			AnyAssetPath.FindChar(TEXT(' '), FoundIndex);
			check(FoundIndex > INDEX_NONE && FoundIndex < AnyAssetPath.Len()); // because of TrimStartAndEnd

			// Confirm that it's a valid Class
			FString ClassName = AnyAssetPath.Left(FoundIndex);

			// Convert \ to /
			ClassName.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);

			// Test ClassName for invalid Char
			const int32 StrLen = FCString::Strlen(INVALID_OBJECTNAME_CHARACTERS);
			for (int32 Index = 0; Index < StrLen; ++Index)
			{
				int32 InvalidFoundIndex = 0;
				if (ClassName.FindChar(INVALID_OBJECTNAME_CHARACTERS[Index], InvalidFoundIndex))
				{
					OutFailureReason = FString::Printf(TEXT("Can't convert the path %s because it contains invalid characters (probably spaces)."), *AnyAssetPath);
					return FString();
				}
			}

			// Return the path without the Class name
			return AnyAssetPath.Mid(FoundIndex + 1);
		}
	}

	FString ConvertAnyPathToObjectPath(const FString& AnyAssetPath, FString& OutFailureReason)
	{
		// Convert "AssetClass'/Game/Folder/MyAsset.MyAsset'" -> "/Game/Folder/MyAsset.MyAsset"
		FString TextPath = FPackageName::ExportTextPathToObjectPath(AnyAssetPath);

		// Convert "AssetClass /Game/Folder/MyAsset.MyAsset" -> "/Game/Folder/MyAsset.MyAsset"
		TextPath = EditorScriptingUtils::RemoveFullName(TextPath, OutFailureReason);
		if (TextPath.IsEmpty())
		{
			return FString();
		}

		// Convert \ to /
		TextPath.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);
		FPaths::RemoveDuplicateSlashes(TextPath);

		// Extract the subobject path, if any
		FString SubObjectPath;
		if (int32 SubObjectDelimiterIdx = INDEX_NONE;
			TextPath.FindChar(SUBOBJECT_DELIMITER_CHAR, SubObjectDelimiterIdx))
		{
			SubObjectPath = TextPath.Mid(SubObjectDelimiterIdx + 1);
			TextPath.LeftInline(SubObjectDelimiterIdx);
		}

		// Extract and validate the object name
		FString ObjectName;
		if (int32 ObjectDelimiterIdx = INDEX_NONE;
			TextPath.FindChar(TEXT('.'), ObjectDelimiterIdx))
		{
			ObjectName = TextPath.Mid(ObjectDelimiterIdx + 1);
			TextPath.LeftInline(ObjectDelimiterIdx);
		}
		else
		{
			// Infer from the package name
			ObjectName = FPackageName::GetShortName(TextPath);
		}
		if (ObjectName.IsEmpty())
		{
			OutFailureReason = FString::Printf(TEXT("Can't convert the path '%s' because it doesn't contain an asset name."), *AnyAssetPath);
			return FString();
		}
		if (!EditorScriptingUtils::IsAValidPath(ObjectName, INVALID_OBJECTNAME_CHARACTERS, OutFailureReason))
		{
			return FString();
		}

		// TextPath should now be a valid package name, so verify that
		if (!EditorScriptingUtils::IsAValidPath(TextPath, INVALID_LONGPACKAGE_CHARACTERS, OutFailureReason))
		{
			return FString();
		}

		// Reject disallowed roots
		if (FPackageName::IsScriptPackage(TextPath))
		{
			OutFailureReason = FString::Printf(TEXT("Can't convert the path '%s' because it start with /Script/"), *AnyAssetPath);
			return FString();
		}
		if (FPackageName::IsMemoryPackage(TextPath))
		{
			OutFailureReason = FString::Printf(TEXT("Can't convert the path '%s' because it start with /Memory/"), *AnyAssetPath);
			return FString();
		}

		// Confirm that the path starts with a valid root
		if (!FPackageName::IsValidPath(TextPath))
		{
			OutFailureReason = FString::Printf(TEXT("Can't convert the path '%s' because it does not map to a root."), *AnyAssetPath);
			return FString();
		}

		// Rebuild the full object path
		TextPath += TEXT(".");
		TextPath += ObjectName;

		return TextPath;
	}

	FString ConvertAnyPathToLongPackagePath(const FString& AnyPath, FString& OutFailureReason)
	{
		// Convert "AssetClass'/Game/Folder/MyAsset.MyAsset'" -> "/Game/Folder/MyAsset.MyAsset"
		FString TextPath = FPackageName::ExportTextPathToObjectPath(AnyPath);

		// Convert "AssetClass /Game/Folder/MyAsset.MyAsset" -> "/Game/Folder/MyAsset.MyAsset"
		TextPath = EditorScriptingUtils::RemoveFullName(TextPath, OutFailureReason);
		if (TextPath.IsEmpty())
		{
			return FString();
		}

		// Convert \ to /
		TextPath.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);
		FPaths::RemoveDuplicateSlashes(TextPath);

		// Remove the object path, if any
		if (int32 ObjectDelimiterIdx = INDEX_NONE;
			TextPath.FindChar(TEXT('.'), ObjectDelimiterIdx))
		{
			TextPath.LeftInline(ObjectDelimiterIdx);
		}

		// TextPath should now be a valid package name, so verify that
		if (!EditorScriptingUtils::IsAValidPath(TextPath, INVALID_LONGPACKAGE_CHARACTERS, OutFailureReason))
		{
			return FString();
		}

		// Reject disallowed roots
		if (FPackageName::IsScriptPackage(TextPath))
		{
			OutFailureReason = FString::Printf(TEXT("Can't convert the path '%s' because it start with /Script/"), *AnyPath);
			return FString();
		}
		if (FPackageName::IsMemoryPackage(TextPath))
		{
			OutFailureReason = FString::Printf(TEXT("Can't convert the path '%s' because it start with /Memory/"), *AnyPath);
			return FString();
		}

		// Confirm that the path starts with a valid root
		if (!FPackageName::IsValidPath(TextPath))
		{
			OutFailureReason = FString::Printf(TEXT("Can't convert the path '%s' because it does not map to a root."), *AnyPath);
			return FString();
		}

		return TextPath;
	}

	FAssetData FindAssetDataFromAnyPath(const FString& AnyAssetPath, FString& OutFailureReason)
	{
		FString ObjectPath = ConvertAnyPathToObjectPath(AnyAssetPath, OutFailureReason);
		if (ObjectPath.IsEmpty())
		{
			return FAssetData();
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
		if (!AssetData.IsValid())
		{
			OutFailureReason = FString::Printf(TEXT("The AssetData '%s' could not be found in the Content Browser."), *ObjectPath);
			return FAssetData();
		}
		
		return AssetData;
	}

	bool IsAContentBrowserAsset(UObject* Object, FString& OutFailureReason)
	{
		if (!IsValid(Object))
		{
			OutFailureReason = TEXT("The Asset is not valid.");
			return false;
		}

		if (!ObjectTools::IsObjectBrowsable(Object))
		{
			OutFailureReason = FString::Printf(TEXT("The object %s is not an asset."), *Object->GetName());
			return false;
		}

		UPackage* NewPackage = Object->GetOutermost();

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(NewPackage->GetName())); // NOTE: This only works because of the in-memory-assets path, the package names is not correct for GetAssetByObjectPath
		if (!AssetData.IsValid())
		{
			OutFailureReason = FString::Printf(TEXT("The AssetData '%s' could not be found in the Content Browser."), *NewPackage->GetName());
			return false;
		}

		if (FEditorFileUtils::IsMapPackageAsset(AssetData.GetObjectPathString()))
		{
			OutFailureReason = FString::Printf(TEXT("The AssetData '%s' is not accessible because it is of type Map/Level."), *AssetData.GetObjectPathString());
			return false;
		}

		// check if it's a umap
		if (!IsPackageFlagsSupportedForAssetLibrary(AssetData.PackageFlags))
		{
			OutFailureReason = FString::Printf(TEXT("The AssetData '%s' is not accessible because it is of type Map/Level."), *NewPackage->GetName());
			return false;
		}

		return true;
	}

	bool GetAssetsInPath(const FString& LongPackagePath, bool bRecursive, TArray<FAssetData>& OutAssetDatas, TArray<FAssetData>& OutMapAssetDatas, FString& OutFailureReason)
	{
		OutAssetDatas.Reset();
		OutMapAssetDatas.Reset();

		// Ask the AssetRegistry if it's a folder
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		if (!AssetRegistryModule.Get().GetAssetsByPath(*LongPackagePath, OutAssetDatas, bRecursive))
		{	// GetAssetsByPath want this syntax /Game/MyFolder
			OutFailureReason = TEXT("The internal search input were not valid.");
			return false;
		}

		// Remove Map & PlayInEditor package
		for (int32 Index = OutAssetDatas.Num() - 1; Index >= 0; --Index)
		{
			if (FEditorFileUtils::IsMapPackageAsset(OutAssetDatas[Index].GetObjectPathString())
				|| !IsPackageFlagsSupportedForAssetLibrary(OutAssetDatas[Index].PackageFlags))
			{
				OutMapAssetDatas.Add(OutAssetDatas[Index]);
				OutAssetDatas.RemoveAtSwap(Index);
			}
		}

		return true;
	}

	bool GetAssetsInPath(const FString& LongPackagePath, bool bRecursive, TArray<UObject*>& OutAssets, TArray<FAssetData>& OutCouldNotLoadAssetData, TArray<FString>& OutFailureReasons)
	{
		OutAssets.Reset();
		OutCouldNotLoadAssetData.Reset();
		OutFailureReasons.Reset();

		TArray<FAssetData> AssetDatas;
		FString FailureReason;
		if (!GetAssetsInPath(LongPackagePath, bRecursive, AssetDatas, OutCouldNotLoadAssetData, FailureReason))
		{
			OutFailureReasons.Add(MoveTemp(FailureReason));
			return false;
		}

		for (const FAssetData& AssetData : AssetDatas)
		{
			FString LoadFailureReason;
			UObject* LoadedObject = LoadAsset(AssetData, false, LoadFailureReason);
			if (LoadedObject)
			{
				OutAssets.Add(LoadedObject);
			}
			else
			{
				OutFailureReasons.Add(MoveTemp(LoadFailureReason));
				OutCouldNotLoadAssetData.Add(AssetData);
			}
		}

		return true;
	}

	UObject* LoadAsset(const FAssetData& AssetData, bool bAllowMapAsset, FString& OutFailureReason)
	{
		if (!AssetData.IsValid())
		{
			return nullptr;
		}

		if (!bAllowMapAsset)
		{
			if (FEditorFileUtils::IsMapPackageAsset(AssetData.GetObjectPathString())
				|| !IsPackageFlagsSupportedForAssetLibrary(AssetData.PackageFlags))
			{
				OutFailureReason = FString::Printf(TEXT("The AssetData '%s' is not accessible because it is of type Map/Level."), *AssetData.GetObjectPathString());
				return nullptr;
			}
		}

		UObject* FoundObject = AssetData.GetAsset();
		if (!IsValid(FoundObject))
		{
			OutFailureReason = FString::Printf(TEXT("The asset '%s' exists but was not able to be loaded."), *AssetData.GetObjectPathString());
		}
		else if (!FoundObject->IsAsset())
		{
			OutFailureReason = FString::Printf(TEXT("'%s' is not a valid asset."), *AssetData.GetObjectPathString());
			FoundObject = nullptr;
		}
		return FoundObject;
	}


	bool DeleteEmptyDirectoryFromDisk(const FString& LongPackagePath)
	{
		struct FEmptyFolderVisitor : public IPlatformFile::FDirectoryVisitor
		{
			bool bIsEmpty;

			FEmptyFolderVisitor()
				: bIsEmpty(true)
			{
			}

			virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory) override
			{
				if (!bIsDirectory)
				{
					bIsEmpty = false;
					return false; // abort searching
				}

				return true; // continue searching
			}
		};

		FString PathToDeleteOnDisk;
		if (FPackageName::TryConvertLongPackageNameToFilename(LongPackagePath, PathToDeleteOnDisk))
		{
			// Look for files on disk in case the folder contains things not tracked by the asset registry
			FEmptyFolderVisitor EmptyFolderVisitor;
			IFileManager::Get().IterateDirectoryRecursively(*PathToDeleteOnDisk, EmptyFolderVisitor);

			if (EmptyFolderVisitor.bIsEmpty)
			{
				return IFileManager::Get().DeleteDirectory(*PathToDeleteOnDisk, false, true);
			}
		}

		return false;
	}
}
