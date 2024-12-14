// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExternalPackageHelper.h"

#if WITH_EDITOR

#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/ArchiveMD5.h"
#include "Misc/Paths.h"
#include "HAL/PlatformApplicationMisc.h"
#include "UObject/MetaData.h"
#include "GameFramework/Actor.h"

FExternalPackageHelper::FOnObjectPackagingModeChanged FExternalPackageHelper::OnObjectPackagingModeChanged;

EPackageFlags FExternalPackageHelper::GetDefaultExternalPackageFlags()
{
	return (PKG_EditorOnly | PKG_ContainsMapData | PKG_NewlyCreated);
}

FExternalPackageHelper::FRenameExternalObjectsHelperContext::FRenameExternalObjectsHelperContext(const UObject* SourceObject, ERenameFlags Flags)
{
	if (GIsEditor && ((Flags & REN_Test) == 0))
	{
		check(SourceObject);
		const UPackage* Package = SourceObject->GetPackage();
		check(Package);
		SourcePackage = Package;
		OldObject = SourceObject;
	}
}

FExternalPackageHelper::FRenameExternalObjectsHelperContext::~FRenameExternalObjectsHelperContext()
{	
	if (GIsEditor && OldObject && (OldObject->GetPackage() != SourcePackage))
	{		
		// Get external packages
		const TArray<UPackage*>& ExternalObjectPackages = OldObject->GetPackage()->GetExternalPackages();
		for (const UPackage* ExternalPackage : ExternalObjectPackages)
		{
			TArray<UObject*> DependantObjects;
			ForEachObjectWithPackage(ExternalPackage, [&DependantObjects](UObject* Object)
			{
				check(Object)
				if (!Cast<UMetaData>(Object))
				{
					DependantObjects.Add(Object);
				}
				return true;
			}, false);

			for (UObject* Object : DependantObjects)
            {
			    FExternalPackageHelper::SetPackagingMode(Object, nullptr, false);
			    FExternalPackageHelper::SetPackagingMode(Object, OldObject, true);
            }
		}
	}
}

void FExternalPackageHelper::DuplicateExternalPackages(const UObject* InObject, FObjectDuplicationParameters& InDuplicationParameters, EActorPackagingScheme ActorPackagingScheme /*= EActorPackagingScheme::Reduced*/)
{
	if (InDuplicationParameters.DuplicateMode != EDuplicateMode::PIE && InDuplicationParameters.bAssignExternalPackages)
	{
		const UPackage* SourcePackage = InObject->GetPackage();
		UPackage* DestinationPackage = InDuplicationParameters.DestOuter->GetPackage();
		
		FString ReplaceFrom = FPaths::GetBaseFilename(*SourcePackage->GetName());
		ReplaceFrom = FString::Printf(TEXT("%s.%s:"), *ReplaceFrom, *ReplaceFrom);

		FString ReplaceTo = FPaths::GetBaseFilename(*DestinationPackage->GetName());
		ReplaceTo = FString::Printf(TEXT("%s.%s:"), *ReplaceTo, *ReplaceTo);
			
		ForEachObjectWithOuter(InObject, [&ReplaceFrom, &ReplaceTo, &InDuplicationParameters, SourcePackage, DestinationPackage, ActorPackagingScheme](const UObject* Object)
		{
			if (UPackage* Package = Object ? Object->GetExternalPackage() : nullptr)
			{
				FString Path = Object->GetPathName();
				if (DestinationPackage != SourcePackage)
				{
					Path = Path.Replace(*ReplaceFrom, *ReplaceTo);
				}
				UPackage* DupPackage = Object->IsA<AActor>() ? ULevel::CreateActorPackage(DestinationPackage, ActorPackagingScheme, Path, Object) : FExternalPackageHelper::CreateExternalPackage(DestinationPackage, Path);
				DupPackage->MarkAsFullyLoaded();
				DupPackage->MarkPackageDirty();
				
				InDuplicationParameters.DuplicationSeed.Add(Package, DupPackage);
			}
		}, /*bIncludeNestedObjects*/ true);
	}
}

UPackage* FExternalPackageHelper::CreateExternalPackage(const UObject* InObjectOuter, const FString& InObjectPath, EPackageFlags InFlags, const UExternalDataLayerAsset* InExternalDataLayerAsset)
{
	const UPackage* OutermostPackage = InObjectOuter->IsA<UPackage>() ? CastChecked<UPackage>(InObjectOuter) : InObjectOuter->GetOutermostObject()->GetPackage();
	const FString RootPath = InExternalDataLayerAsset ? FExternalDataLayerHelper::GetExternalDataLayerLevelRootPath(InExternalDataLayerAsset, OutermostPackage->GetName()) : OutermostPackage->GetName();
	const FString ExternalObjectPackageName = FExternalPackageHelper::GetExternalPackageName(RootPath, InObjectPath);
	UPackage* Package = CreatePackage(*ExternalObjectPackageName);
	Package->SetPackageFlags(InFlags);
	return Package;
}

void FExternalPackageHelper::SetPackagingMode(UObject* InObject, const UObject* InObjectOuter, bool bInIsPackageExternal, bool bInShouldDirty, EPackageFlags InExternalPackageFlags)
{
	if (bInIsPackageExternal == InObject->IsPackageExternal())
	{
		return;
	}

	// Optionally mark the current object & package as dirty
	InObject->Modify(bInShouldDirty);

	if (bInIsPackageExternal)
	{
		const IDataLayerInstanceProvider* DataLayerInstanceProvider = InObject->GetImplementingOuter<IDataLayerInstanceProvider>();
		const UExternalDataLayerAsset* ExternalDataLayerAsset = DataLayerInstanceProvider ? DataLayerInstanceProvider->GetRootExternalDataLayerAsset() : nullptr;
		UPackage* NewObjectPackage = FExternalPackageHelper::CreateExternalPackage(InObjectOuter, InObject->GetPathName(), InExternalPackageFlags, ExternalDataLayerAsset);
		InObject->SetExternalPackage(NewObjectPackage);
	}
	else
	{
		UPackage* ObjectPackage = InObject->GetExternalPackage();
		// Detach the linker exports so it doesn't resolve to this object anymore
		ResetLinkerExports(ObjectPackage);
		InObject->SetExternalPackage(nullptr);
	}

	OnObjectPackagingModeChanged.Broadcast(InObject, bInIsPackageExternal);

	// Mark the new object package dirty
	InObject->MarkPackageDirty();
}

FString FExternalPackageHelper::GetExternalObjectsPath(const FString& InOuterPackageName, const FString& InPackageShortName)
{
	// Strip the temp prefix if found
	FString ExternalObjectsPath;

	auto TrySplitLongPackageName = [&InPackageShortName](const FString& InOuterPackageName, FString& OutExternalObjectsPath)
	{
		FString MountPoint, PackagePath, ShortName;
		if (FPackageName::SplitLongPackageName(InOuterPackageName, MountPoint, PackagePath, ShortName))
		{
			OutExternalObjectsPath = FString::Printf(TEXT("%s%s/%s%s"), *MountPoint, FPackagePath::GetExternalObjectsFolderName(), *PackagePath, InPackageShortName.IsEmpty() ? *ShortName : *InPackageShortName);
			return true;
		}
		return false;
	};

	// This exists only to support the Fortnite Foundation Outer streaming which prefix a valid package with /Temp (/Temp/Game/...)
	// Unsaved worlds also have a /Temp prefix but no other mount point in their paths and they should fallback to not stripping the prefix. (first call to SplitLongPackageName will fail and second will succeed)
	if (InOuterPackageName.StartsWith(TEXT("/Temp")))
	{
		FString BaseOuterPackageName = InOuterPackageName.Mid(5);
		if (TrySplitLongPackageName(BaseOuterPackageName, ExternalObjectsPath))
		{
			return ExternalObjectsPath;
		}
	}

	if (TrySplitLongPackageName(InOuterPackageName, ExternalObjectsPath))
	{
		return ExternalObjectsPath;
	}

	return FString();
}

FString FExternalPackageHelper::GetExternalObjectsPath(UPackage* InPackage, const FString& InPackageShortName /* = FString()*/, bool bTryUsingPackageLoadedPath /* = false*/)
{
	check(InPackage);
	if (bTryUsingPackageLoadedPath && !InPackage->GetLoadedPath().IsEmpty())
	{
		return FExternalPackageHelper::GetExternalObjectsPath(InPackage->GetLoadedPath().GetPackageName());
	}
	// We can't use the Package->FileName here because it might be a duplicated a package
	// We can't use the package short name directly in some cases either (PIE, instanced load) as it may contain pie prefix or not reflect the real object location
	return FExternalPackageHelper::GetExternalObjectsPath(InPackage->GetName(), InPackageShortName);
}

FString FExternalPackageHelper::GetExternalPackageName(const FString& InOuterPackageName, const FString& InObjectPath)
{
	// Convert the object path to lowercase to make sure we get the same hash for case insensitive file systems
	FString ObjectPath = InObjectPath.ToLower();

	FArchiveMD5 ArMD5;
	ArMD5 << ObjectPath;

	FGuid PackageGuid = ArMD5.GetGuidFromHash();
	check(PackageGuid.IsValid());

	FString GuidBase36 = PackageGuid.ToString(EGuidFormats::Base36Encoded);
	check(GuidBase36.Len());

	FString BaseDir = FExternalPackageHelper::GetExternalObjectsPath(InOuterPackageName);

	TStringBuilderWithBuffer<TCHAR, NAME_SIZE> ObjectPackageName;
	ObjectPackageName.Append(BaseDir);
	ObjectPackageName.Append(TEXT("/"));
	ObjectPackageName.Append(*GuidBase36, 1);
	ObjectPackageName.Append(TEXT("/"));
	ObjectPackageName.Append(*GuidBase36 + 1, 2);
	ObjectPackageName.Append(TEXT("/"));
	ObjectPackageName.Append(*GuidBase36 + 3);
	return ObjectPackageName.ToString();
}

FString FExternalPackageHelper::GetExternalObjectPackageInstanceName(const FString& OuterPackageName, const FString& ObjectPackageName)
{
	return FLinkerInstancingContext::GetInstancedPackageName(OuterPackageName, ObjectPackageName);
}

void FExternalPackageHelper::GetExternalSaveableObjects(UObject* InOuter, TArray<UObject*>& OutObjects, EGetExternalSaveableObjectsFlags InFlags)
{
	// Get external packages
	TSet<UPackage*> ExternalObjectPackages;
	ExternalObjectPackages.Append(InOuter->GetPackage()->GetExternalPackages());

	// Find assets for external packages
	for (UPackage* ExternalPackage : ExternalObjectPackages)
	{
		const bool bPassesDirtyCheck = !EnumHasAnyFlags(InFlags, EGetExternalSaveableObjectsFlags::CheckDirty) || ExternalPackage->IsDirty();
		if(bPassesDirtyCheck && FPackageName::IsValidLongPackageName(ExternalPackage->GetName()))
		{
			if(UObject* Asset = ExternalPackage->FindAssetInPackage())
			{
				OutObjects.Add(Asset);
			}
		}
	}
}

TArray<FString> FExternalPackageHelper::GetObjectsExternalPackageFilePath(const TArray<const UObject*>& InObjects)
{
	TArray<FString> PackageFilePaths;
	for (const UObject* Object : InObjects)
	{
		if (Object && Object->IsPackageExternal())
		{
			const FString LocalFullPath(Object->GetExternalPackage()->GetLoadedPath().GetLocalFullPath());
			if (!LocalFullPath.IsEmpty())
			{
				PackageFilePaths.Add(FPaths::ConvertRelativePathToFull(LocalFullPath));
			}
		}
	}
	return PackageFilePaths;
}

void FExternalPackageHelper::CopyObjectsExternalPackageFilePathToClipboard(const TArray<const UObject*>& InObjects)
{
	TArray<FString> PackageFilePaths = GetObjectsExternalPackageFilePath(InObjects);
	if (!PackageFilePaths.IsEmpty())
	{
		FString Result = FString::Join(PackageFilePaths, TEXT("\n"));
		check(Result.Len());
		FPlatformApplicationMisc::ClipboardCopy(*Result);
	}
}

void FExternalPackageHelper::GetSortedAssets(const FARFilter& Filter, TArray<FAssetData>& OutAssets)
{
	OutAssets.Reset();
	IAssetRegistry::GetChecked().GetAssets(Filter, OutAssets);
	OutAssets.Sort();
}


#endif
