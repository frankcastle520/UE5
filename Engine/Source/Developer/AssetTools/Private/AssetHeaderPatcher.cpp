// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetHeaderPatcher.h"

#include "Algo/Copy.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/PackageReader.h"
#include "Containers/ContainersFwd.h"
#include "Internationalization/GatherableTextData.h"
#include "Misc/Base64.h"
#include "Misc/EnumerateRange.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Serialization/LargeMemoryReader.h"
#include "UObject/CoreRedirects.h"
#include "UObject/Linker.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectResource.h"
#include "UObject/Package.h"
#include "UObject/PackageFileSummary.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionActorDescUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogAssetHeaderPatcher, Log, All);

namespace
{
	// If working on header patching, this is very helpful for dumping what is patched and reviewing the files in a folder comparison of your favourite diff program.
	FString DumpOutputDirectory;
	static FAutoConsoleVariableRef CVarDumpOutputDirectory(
		TEXT("AssetHeaderPatcher.DebugDumpDir"),
		DumpOutputDirectory,
		TEXT("'Before'/'After' text representations of each package processed during patching will be written out to the provided absolute filesystem path. Useful for comparing what was patched.")
	);

	// Tag 'Key' names that are generally large blobs of data that can't/shouldn't be patched
	const TCHAR* TagsToIgnore[] =
	{
		TEXT("FiBData")
	};

	const FStringView InvalidObjectPathCharacters(INVALID_OBJECTPATH_CHARACTERS);

	bool SplitLongPackageName(FStringView LongPackageName, FStringView& PackageRoot, FStringView& PackagePath, FStringView& PackageName)
	{
		if (LongPackageName.IsEmpty() || LongPackageName[0] != TEXT('/'))
		{
			return false;
		}

		PackageRoot = FStringView(LongPackageName.GetData() + 1); // + 1 to skip the leading '/'
		int32 SeparatorPos;
		if (!PackageRoot.FindChar(TEXT('/'), SeparatorPos))
		{
			return false;
		}
		PackageRoot.LeftInline(SeparatorPos);

		const int32 PackagePathOffset = PackageRoot.Len() + 2; // + 2 for the leading and trailing '/'
		if (LongPackageName.Len() < PackagePathOffset || !LongPackageName.FindLastChar(TEXT('/'), SeparatorPos))
		{
			return false;
		}

		// May be empty. If the PackageName is off the root there is no PackagePath
		const int32 PackagePathLen = SeparatorPos - (PackagePathOffset - 1);
		check(PackagePathLen >= 0);
		PackagePath = FStringView(LongPackageName.GetData() + PackagePathOffset, PackagePathLen - !!PackagePathLen);

		const int32 PackageNameOffset = PackagePathOffset + PackagePath.Len() + !PackagePath.IsEmpty();
		PackageName = FStringView(LongPackageName.GetData() + PackageNameOffset, LongPackageName.Len() - PackageNameOffset);

		return true;
	}

	FStringView Find(const TMap<FString, FString>& Table, FStringView Needle)
	{
		uint32 NeedleHash = TMap<FString, FString>::KeyFuncsType::GetKeyHash<FStringView>(Needle);
		const FString* MaybeNewItem = Table.FindByHash<FStringView>(NeedleHash, Needle);
		if (MaybeNewItem)
		{
			return *MaybeNewItem;
		}

		return {};
	}
}

FString LexToString(FAssetHeaderPatcher::EResult InResult)
{
	switch (InResult)
	{
		case FAssetHeaderPatcher::EResult::NotStarted: return TEXT("Not Started");
		case FAssetHeaderPatcher::EResult::Cancelled: return TEXT("Cancelled");
		case FAssetHeaderPatcher::EResult::InProgress: return TEXT("In Progress");
		case FAssetHeaderPatcher::EResult::Success: return TEXT("Success");
		case FAssetHeaderPatcher::EResult::ErrorFailedToLoadSourceAsset: return TEXT("Failed to load source asset");
		case FAssetHeaderPatcher::EResult::ErrorFailedToDeserializeSourceAsset: return TEXT("Failed to deserialize source asset");
		case FAssetHeaderPatcher::EResult::ErrorUnexpectedSectionOrder: return TEXT("Unexpected section order");
		case FAssetHeaderPatcher::EResult::ErrorBadOffset: return TEXT("Bad offset");
		case FAssetHeaderPatcher::EResult::ErrorUnkownSection: return TEXT("Unknown section");
		case FAssetHeaderPatcher::EResult::ErrorFailedToOpenDestinationFile: return TEXT("Failed to open destination file");
		case FAssetHeaderPatcher::EResult::ErrorFailedToWriteToDestinationFile: return TEXT("Failed to write to destination file");
		case FAssetHeaderPatcher::EResult::ErrorEmptyRequireSection: return TEXT("Empty required section");
		default: return TEXT("Unknown");
	}
}

FAssetHeaderPatcher::FContext::FContext(const TMap<FString, FString>& SourceAndDestPackages, const bool bInGatherDependentPackages)
	: PackagePathRenameMap(SourceAndDestPackages)
{
	AddVerseMounts();

	if (bInGatherDependentPackages)
	{
		GatherDependentPackages();
	}

	GenerateFilePathsFromPackagePaths();
	GenerateAdditionalRemappings();
}

FAssetHeaderPatcher::FContext::FContext(const FString& InSrcRoot, const FString& InDstRoot, const FString& InSrcBaseDir, const TMap<FString, FString>& InSrcAndDstFilePaths, const TMap<FString, FString>& InMountPointReplacements)
	: FilePathRenameMap(InSrcAndDstFilePaths)
	, StringMountReplacements(InMountPointReplacements)
{
	AddVerseMounts();
	GeneratePackagePathsFromFilePaths(InSrcRoot, InDstRoot, InSrcBaseDir);
	GenerateAdditionalRemappings();
}

void FAssetHeaderPatcher::FContext::AddVerseMounts()
{
	// Todo: Expose this so callers provide this data
	VerseMountPoints.Add("localhost");
}

void FAssetHeaderPatcher::FContext::GenerateFilePathsFromPackagePaths()
{
	FilePathRenameMap.Reserve(PackagePathRenameMap.Num());

	// Construct all source and destination filenames from our package map
	for (const TTuple<FString, FString>& Package : PackagePathRenameMap)
	{
		const FString& PackageName = Package.Key;
		const FString& DestPackage = Package.Value;
		FString SrcFilename;

		// To consider: Allow the caller to provide their own file filter
		if (FPackageName::IsVersePackage(PackageName))
		{
			// Verse packages are not header patchable.
			// They are also not Packages as far as DoesPackageExist tells me.
			// But they are real files that in template copying have already been done, so we dont want a warning message.
			continue;
		}

		if (FPackageName::DoesPackageExist(PackageName, &SrcFilename))
		{
			FString DestFilename = FPackageName::LongPackageNameToFilename(DestPackage, FString(FPathViews::GetExtension(SrcFilename, true)));
			FilePathRenameMap.Add({ MoveTemp(SrcFilename), MoveTemp(DestFilename) });
		}
		else
		{
			UE_LOG(LogAssetHeaderPatcher, Warning, TEXT("{%s} package does not exist, and will not be patched."), *PackageName);
		}
	}
}

void FAssetHeaderPatcher::FContext::GeneratePackagePathsFromFilePaths(const FString& InSrcRoot, const FString& InDstRoot, const FString& InSrcBaseDir)
{
	const FString SourceContentPath = FPaths::Combine(InSrcBaseDir, TEXT("Content"));
	for (const TTuple<FString, FString>& SourceAndDest : FilePathRenameMap)
	{
		const FString& SrcFileName = SourceAndDest.Key;

		if (FPaths::IsUnderDirectory(SrcFileName, SourceContentPath))
		{
			if (FStringView RelativePkgPath; FPathViews::TryMakeChildPathRelativeTo(SrcFileName, SourceContentPath, RelativePkgPath))
			{
				RelativePkgPath = FPathViews::GetBaseFilenameWithPath(RelativePkgPath); // chop the extension
				if (RelativePkgPath.Len() > 0 && !RelativePkgPath.EndsWith(TEXT("/")))
				{
					PackagePathRenameMap.Add(FPaths::Combine(TEXT("/"), InSrcRoot, RelativePkgPath),
						FPaths::Combine(TEXT("/"), InDstRoot, RelativePkgPath));
				}
			}
		}
	}
}

void FAssetHeaderPatcher::FContext::GatherDependentPackages()
{
	// Paths under the __External root drop the package root, so create mappings, per plugin, 
	// we can leverage when handling those cases where the package path may have been remapped
	TMap<FString, TMap<FString, FString>> PluginExternalMappings;
	for (const TPair<FString, FString>& SrcDstPair : PackagePathRenameMap)
	{
		const FString& Src = SrcDstPair.Key;
		const FString& Dst = SrcDstPair.Value;

		FStringView SrcPackageRoot;
		FStringView SrcPackagePath;
		FStringView SrcPackageName;
		SplitLongPackageName(Src, SrcPackageRoot, SrcPackagePath, SrcPackageName);

		FStringView DstPackageRoot;
		FStringView DstPackagePath;
		FStringView DstPackageName;
		SplitLongPackageName(Dst, DstPackageRoot, DstPackagePath, DstPackageName);

		TMap<FString, FString>& ExternalMappings = PluginExternalMappings.FindOrAddByHash(GetTypeHash(SrcPackageRoot), FString(SrcPackageRoot));
		FStringView SrcPath = SrcPackagePath.IsEmpty() ? SrcPackageName : SrcPackagePath;
		FStringView DstPath = DstPackagePath.IsEmpty() ? DstPackageName : DstPackagePath;
		ExternalMappings.Add(FString(SrcPath), FString(DstPath));

		// if there is a path
		if (!SrcPackagePath.IsEmpty())
		{
			// add the local path/asset for the case of maps (which we cannot tell at this point)
			ExternalMappings.Add(FString(SrcPath.GetData()), FString(DstPath.GetData()));
		}
	}

	TMap<FString, FString> Result;
	IAssetRegistry& Registry = *IAssetRegistry::Get();

	TArray< TTuple<FString, FString> > ToProcess;
	Algo::Copy(PackagePathRenameMap, ToProcess);

	TStringBuilder<NAME_SIZE> SrcDependencyBuilder;
	while (ToProcess.Num())
	{
		TTuple<FString, FString> Package = ToProcess.Pop();

		if (Result.Contains(Package.Key))
		{
			continue;
		}

		// Become a patching name even if it doesn't have a file.
		Result.Add({ Package.Key, Package.Value });

		TArray<FName> Dependencies;
		if (!Registry.GetDependencies(FName(*Package.Key), Dependencies))
		{
			continue;
		}

		FStringView SrcPackageRoot = FPackageName::SplitPackageNameRoot(Package.Key, nullptr);
		FStringView DstPackageRoot = FPackageName::SplitPackageNameRoot(Package.Value, nullptr);
		for (const FName Dependency : Dependencies)
		{
			Dependency.ToString(SrcDependencyBuilder);
			FStringView SrcDependency = SrcDependencyBuilder.ToView();

			if (PackagePathRenameMap.FindByHash(GetTypeHash(SrcDependency), SrcDependency))
			{
				// We already handled this mapping
				continue;
			}

			FStringView SrcDependencyPackageRoot;
			FStringView SrcDependencyPackagePath;
			FStringView SrcDependencyPackageName;
			SplitLongPackageName(SrcDependency, SrcDependencyPackageRoot, SrcDependencyPackagePath, SrcDependencyPackageName);
			check(!SrcDependencyPackageRoot.IsEmpty());

			// Only consider dependency paths that are for the same package as our src->dst mapping
			// If the src mapping doesn't begin with a '/' the package name will be empty, since the path isn't a package path
			if (SrcDependencyPackageRoot != SrcPackageRoot)
			{
				continue;
			}

			TStringBuilder<NAME_SIZE> DstDependencyString;

			// Special handling for external references. The __External[Actors__|Objects__] directory is always under the package root, may contain an
			// arbitrary amount of subdirs but then ends with two hash subdirs. The path between the __External[Actors__|Objects__] and the two hash dirs
			// may need remapping so we look at our external mappings to do so.
			bool bHasExternalActorDir = SrcDependencyPackagePath.StartsWith(FPackagePath::GetExternalActorsFolderName());
			bool bHasExternalObjectsDir = !bHasExternalActorDir && SrcDependencyPackagePath.StartsWith(FPackagePath::GetExternalObjectsFolderName());
			if (bHasExternalActorDir || bHasExternalObjectsDir)
			{
				int32 RightPartStartPos;
				if (!SrcDependencyPackagePath.FindChar(TEXT('/'), RightPartStartPos))
				{
					// This is a path to only the special directory, skip it no remapping is needed
					continue;
				}
				RightPartStartPos++; // Skip past the '/'

				// Find the start of the two hash dirs
				// e.g. __ExternalActors__/path/of/interest/A/A9, we only want 'path/of/interest'
				FStringView ExternalPackagePath(SrcDependencyPackagePath.GetData() + RightPartStartPos, SrcDependencyPackagePath.Len() - RightPartStartPos);
				int32 HashDirStartPos = 0;
				int32 NumHashDirsToStrip = 2;
				while (NumHashDirsToStrip--)
				{
					if (ExternalPackagePath.FindLastChar(TEXT('/'), HashDirStartPos))
					{
						ExternalPackagePath.LeftChopInline(ExternalPackagePath.Len() - HashDirStartPos);
					}
				}

				// Our __External[Actors|Objects]__ path is malformed
				if (HashDirStartPos == INDEX_NONE)
				{
					continue;
				}

				const int32 HashPathOffset = RightPartStartPos + HashDirStartPos;
				FStringView HashPath(SrcDependencyPackagePath.GetData() + HashPathOffset, SrcDependencyPackagePath.Len() - HashPathOffset);
				const TMap<FString, FString>* ExternalMappings = PluginExternalMappings.FindByHash(GetTypeHash(SrcPackageRoot), SrcPackageRoot);
				if (!ExternalMappings)
				{
					// We have no mapping for this dependency's external actors/objects
					continue;
				}
				const FString* DstExternalPackagePath = ExternalMappings->FindByHash(GetTypeHash(ExternalPackagePath), ExternalPackagePath);

				DstDependencyString.AppendChar(TEXT('/'));
				DstDependencyString.Append(DstPackageRoot);
				DstDependencyString.AppendChar(TEXT('/'));
				DstDependencyString.Append(bHasExternalActorDir ? FPackagePath::GetExternalActorsFolderName() : FPackagePath::GetExternalObjectsFolderName());
				DstDependencyString.AppendChar(TEXT('/'));
				DstDependencyString.Append(DstExternalPackagePath ? *DstExternalPackagePath : ExternalPackagePath);
				DstDependencyString.Append(HashPath); // HashPath already contains the leading '/'
				DstDependencyString.AppendChar(TEXT('/'));
				DstDependencyString.Append(SrcDependencyPackageName);
			}
			else
			{
				// We aren't handling a special directory so replace the package root
				DstDependencyString.AppendChar(TEXT('/'));
				DstDependencyString.Append(DstPackageRoot);
				DstDependencyString.AppendChar(TEXT('/'));

				if (!SrcDependencyPackagePath.IsEmpty())
				{
					DstDependencyString.Append(SrcDependencyPackagePath);
					DstDependencyString.AppendChar(TEXT('/'));
				}

				DstDependencyString.Append(SrcDependencyPackageName);
			}

			// If a dep start with the package name, then we are going to copy the asset.
			// but we need to recurse on this asset as it may have sub dependencies we don't know of yet.
			ToProcess.Add({ FString(SrcDependency), DstDependencyString.ToString() });
		}
	}

	PackagePathRenameMap = MoveTemp(Result);
}

void FAssetHeaderPatcher::FContext::GenerateAdditionalRemappings()
{
	TArray<FCoreRedirect> ExternalObjectRedirects;
	TStringBuilder<24> ExternalActorsFolderBuilder;
	ExternalActorsFolderBuilder << FPackagePath::GetExternalActorsFolderName() << TEXT("/");
	const FStringView ExternalActorsFolder = ExternalActorsFolderBuilder.ToView();

	TStringBuilder<24> ExternalObjectsFolderBuilder;
	ExternalObjectsFolderBuilder << FPackagePath::GetExternalObjectsFolderName() << TEXT("/");
	const FStringView ExternalObjectsFolder = ExternalObjectsFolderBuilder.ToView();

	TStringBuilder<NAME_SIZE> SrcNameBuilder;
	TStringBuilder<NAME_SIZE> DstNameBuilder;
	for (const TTuple<FString, FString>& Package : PackagePathRenameMap)
	{
		const FString& SrcNameString = Package.Key;
		const FString& DstNameString = Package.Value;

		bool bIsExternalObjectOrActor = false;
		FStringView SrcPackageName;
		{
			FStringView SrcPackageRoot;
			FStringView SrcPackagePath;
			if (!ensure(SplitLongPackageName(SrcNameString, SrcPackageRoot, SrcPackagePath, SrcPackageName))
				|| SrcPackagePath.StartsWith(ExternalActorsFolder)
				|| SrcPackagePath.StartsWith(ExternalObjectsFolder))
			{
				bIsExternalObjectOrActor = true;
			}
		}

		// /Path/To/Package mapping
		{
			FCoreRedirect PackageRedirect(ECoreRedirectFlags::Type_Package,
				FCoreRedirectObjectName(SrcNameString),
				FCoreRedirectObjectName(DstNameString));

			if (bIsExternalObjectOrActor)
			{
				// The other mappings below don't apply to ExternalActors or ExternalObjects so we skip them
				// now that we have a PackagePath mapping for them
				ExternalObjectRedirects.Emplace(MoveTemp(PackageRedirect));
				continue;
			}
			else
			{
				Redirects.Emplace(MoveTemp(PackageRedirect));
			}
		}

		FStringView DstPackageName = FPathViews::GetBaseFilename(DstNameString);

		// Path.ObjectName mapping
		{
			SrcNameBuilder.Reset();
			SrcNameBuilder.Append(SrcNameString);
			SrcNameBuilder.AppendChar(TEXT('.'));
			SrcNameBuilder.Append(SrcPackageName);

			DstNameBuilder.Reset();
			DstNameBuilder.Append(DstNameString);
			DstNameBuilder.AppendChar(TEXT('.'));
			DstNameBuilder.Append(DstPackageName);

			FCoreRedirect PackageObjectRedirect(ECoreRedirectFlags::Type_Package | ECoreRedirectFlags::Type_Object,
				FCoreRedirectObjectName(SrcNameBuilder.ToString()),
				FCoreRedirectObjectName(DstNameBuilder.ToString()));
			Redirects.Emplace(MoveTemp(PackageObjectRedirect));
		}

		// MaterialFunctionInterface "EditorOnlyData"
		{
			SrcNameBuilder.Reset();
			SrcNameBuilder.Append(SrcNameString);
			SrcNameBuilder.AppendChar(TEXT('.'));
			SrcNameBuilder.Append(SrcPackageName);
			SrcNameBuilder.Append(TEXT("EditorOnlyData"));

			DstNameBuilder.Reset();
			DstNameBuilder.Append(DstNameString);
			DstNameBuilder.AppendChar(TEXT('.'));
			DstNameBuilder.Append(DstPackageName);
			DstNameBuilder.Append(TEXT("EditorOnlyData"));

			FCoreRedirect BlueprintClassRedirect(ECoreRedirectFlags::Type_Class | ECoreRedirectFlags::Type_Package,
				FCoreRedirectObjectName(SrcNameBuilder.ToString()),
				FCoreRedirectObjectName(DstNameBuilder.ToString()));
			Redirects.Emplace(MoveTemp(BlueprintClassRedirect));
		}

		// Compiled Blueprint class names
		{
			SrcNameBuilder.Reset();
			SrcNameBuilder.Append(SrcNameString);
			SrcNameBuilder.AppendChar(TEXT('.'));
			SrcNameBuilder.Append(SrcPackageName);
			SrcNameBuilder.Append(TEXT("_C"));

			DstNameBuilder.Reset();
			DstNameBuilder.Append(DstNameString);
			DstNameBuilder.AppendChar(TEXT('.'));
			DstNameBuilder.Append(DstPackageName);
			DstNameBuilder.Append(TEXT("_C"));

			FCoreRedirect BlueprintClassRedirect(ECoreRedirectFlags::Type_Class | ECoreRedirectFlags::Type_Package,
				FCoreRedirectObjectName(SrcNameBuilder.ToString()),
				FCoreRedirectObjectName(DstNameBuilder.ToString()));
			Redirects.Emplace(MoveTemp(BlueprintClassRedirect));
		}

		// Blueprint generated class default object
		{
			SrcNameBuilder.Reset();
			SrcNameBuilder.Append(SrcNameString);
			SrcNameBuilder.AppendChar(TEXT('.'));
			SrcNameBuilder.Append(DEFAULT_OBJECT_PREFIX);
			SrcNameBuilder.Append(SrcPackageName);
			SrcNameBuilder.Append(TEXT("_C"));

			DstNameBuilder.Reset();
			DstNameBuilder.Append(DstNameString);
			DstNameBuilder.AppendChar(TEXT('.'));
			DstNameBuilder.Append(DEFAULT_OBJECT_PREFIX);
			DstNameBuilder.Append(DstPackageName);
			DstNameBuilder.Append(TEXT("_C"));

			FCoreRedirect DefaultBlueprintClassRedirect(ECoreRedirectFlags::Type_Class | ECoreRedirectFlags::Type_Package,
				FCoreRedirectObjectName(SrcNameBuilder.ToString()),
				FCoreRedirectObjectName(DstNameBuilder.ToString()));
			Redirects.Emplace(MoveTemp(DefaultBlueprintClassRedirect));
		}
	}

	// For best-effort string matches. Intentionally excluding external objects as AssetRegistry Tag data
	// can't refer to these paths in a manner that we can't deduce from the redirects themselves
	for (auto& Redirect : Redirects)
	{
		const FCoreRedirectObjectName& SrcName = Redirect.OldName;
		const FCoreRedirectObjectName& DstName = Redirect.NewName;

		StringReplacements.Add(SrcName.ObjectName.ToString(), DstName.ObjectName.ToString());
		StringReplacements.Add(SrcName.PackageName.ToString(), DstName.PackageName.ToString());
		StringReplacements.Add(SrcName.ToString(), DstName.ToString());

		// Tag data can contain VersePaths which are like Top-Level Asset Paths
		// but with a mountpoint prefix and only '/' delimiters
		for (FString& VerseMount : VerseMountPoints)
		{
			SrcNameBuilder.Reset();
			SrcNameBuilder.AppendChar(TEXT('/'));
			SrcNameBuilder.Append(VerseMount);
			SrcName.PackageName.AppendString(SrcNameBuilder);
			SrcNameBuilder.AppendChar(TEXT('/'));
			SrcName.ObjectName.AppendString(SrcNameBuilder);

			DstNameBuilder.Reset();
			DstNameBuilder.AppendChar(TEXT('/'));
			DstNameBuilder.Append(VerseMount);
			DstName.PackageName.AppendString(DstNameBuilder);
			DstNameBuilder.AppendChar(TEXT('/'));
			DstName.ObjectName.AppendString(DstNameBuilder);
			StringReplacements.Add(SrcNameBuilder.ToString(), DstNameBuilder.ToString());
		}
	}

	// Now that we have generated the string matches above, add the external redirects
	Redirects.Append(ExternalObjectRedirects);

	// Add prefix redirects for any mountpoint replacements
	TMap<FString, FString> FormattedStringMountReplacements;
	FormattedStringMountReplacements.Reserve(StringMountReplacements.Num());
	for (const auto& MountPointPair : StringMountReplacements)
	{
		const FString& SrcMountPoint = MountPointPair.Key;
		const FString& DstMountPoint = MountPointPair.Value;

		SrcNameBuilder.Reset();
		SrcNameBuilder.AppendChar(TEXT('/'));
		SrcNameBuilder.Append(SrcMountPoint);
		SrcNameBuilder.AppendChar(TEXT('/'));

		DstNameBuilder.Reset();
		DstNameBuilder.AppendChar(TEXT('/'));
		DstNameBuilder.Append(DstMountPoint);
		DstNameBuilder.AppendChar(TEXT('/'));

		FCoreRedirect MountRedirect(ECoreRedirectFlags::Type_Package | ECoreRedirectFlags::Option_MatchPrefix,
			FCoreRedirectObjectName(SrcNameBuilder.ToString()),
			FCoreRedirectObjectName(DstNameBuilder.ToString()));
		Redirects.Emplace(MoveTemp(MountRedirect));

		// Store off the actual mount path prefix to make patching easier later
		FormattedStringMountReplacements.Add(SrcNameBuilder.ToString(), DstNameBuilder.ToString());
	}
	StringMountReplacements = MoveTemp(FormattedStringMountReplacements);
}

// To override writing of FName's to ensure they have been patched
class FNamePatchingWriter final : public FArchiveProxy
{
public:
	FNamePatchingWriter(FArchive& InAr, const TMap<FNameEntryId, int32>& InNameToIndexMap)
		: FArchiveProxy(InAr)
		, NameToIndexMap(InNameToIndexMap)
	{
	}

	virtual ~FNamePatchingWriter() { }

	virtual FArchive& operator<<(FName& Name) override
	{
		FNameEntryId EntryId = Name.GetDisplayIndex();
		const int32* MaybeIndex = NameToIndexMap.Find(EntryId);

		if (MaybeIndex == nullptr)
		{
			ErrorMessage += FString::Printf(TEXT("Cannot serialize FName %s because it is not in the name table for %s\n"), *Name.ToString(), *GetArchiveName());
			SetCriticalError();
			return *this;
		}

		int32 Index = *MaybeIndex;
		int32 Number = Name.GetNumber();

		FArchive& Ar = *this;
		Ar << Index;
		Ar << Number;

		return *this;
	}

	const FString& GetErrorMessage() const
	{
		return ErrorMessage;
	}

private:
	const TMap<FNameEntryId, int32>& NameToIndexMap;
	FString ErrorMessage;
};

enum class EPatchedSection
{
	Summary,
	NameTable,
	SoftPathTable,
	GatherableTextDataTable,
	SearchableNamesMap,
	ImportTable,
	ExportTable,
	SoftPackageReferencesTable,
	ThumbnailTable,
	AssetRegistryData
};

struct FSectionData
{
	EPatchedSection Section = EPatchedSection::Summary;
	int64 Offset = 0;
	int64 Size = 0;
	bool bRequired = false;
};

enum class ESummaryOffset
{
	NameTable,
	SoftObjectPathList,
	GatherableTextDataTable,
	ImportTable,
	ExportTable,
	DependsTable,
	SoftPackageReferenceList,
	SearchableNamesMap,
	ThumbnailTable,
	AssetRegistryData,
	WorldTileInfoData,
	PreloadDependency, // Should not be present - only for cooked data
	BulkData,
	PayloadToc
};

// To override MemoryReaders FName method
class FReadFNameAs2IntFromMemoryReader final : public FLargeMemoryReader
{
public:
	FReadFNameAs2IntFromMemoryReader(TArray<FName>& InNameTable, const uint8* InData, const int64 Num, ELargeMemoryReaderFlags InFlags = ELargeMemoryReaderFlags::None, const FName InArchiveName = NAME_None)
		: FLargeMemoryReader(InData, Num, InFlags, InArchiveName)
		, NameTable(InNameTable)
	{
	}

	// FLargeMemoryReader falls back to FMemoryArchive's imp of this method.
	// which uses strings as the format for FName.
	// We need the 2xint32 version when decoding the current file formats. 
	virtual FArchive& operator<<(FName& OutName) override
	{
		int32 NameIndex;
		int32 Number;
		FArchive& Ar = *this;
		Ar << NameIndex;
		Ar << Number;

		if (NameTable.IsValidIndex(NameIndex))
		{
			FNameEntryId MappedName = NameTable[NameIndex].GetDisplayIndex();
			OutName = FName::CreateFromDisplayId(MappedName, Number);
		}
		else
		{
			OutName = FName();
			SetCriticalError();
		}

		return *this;
	}

	virtual FString GetArchiveName() const override
	{
		return TEXT("FReadFNameAs2IntFromMemoryReader");
	}
private:
	TArray<FName>& NameTable;
};

struct FSummaryOffsetMeta
{
	// NOTE: The offsets in Summary get to a max of 312 bytes.
	// So we could drop this to a uint16 but that is probably overkill at this point.
	uint32 Offset : 31;
	uint32 bIs64Bit : 1;

	int64 Value(FPackageFileSummary& Summary) const
	{
		intptr_t Ptr = reinterpret_cast<intptr_t>(&Summary) + Offset;
		if (bIs64Bit)
		{
			return *reinterpret_cast<int64*>(Ptr);
		}
		else
		{
			return *reinterpret_cast<int32*>(Ptr);
		}
	}

	void PatchOffsetValue(FPackageFileSummary& Summary, int64 Value) const
	{
		intptr_t Ptr = reinterpret_cast<intptr_t>(&Summary) + Offset;
		if (bIs64Bit)
		{
			int64& Dst = *reinterpret_cast<int64*>(Ptr);
			Dst += Value;
		}
		else
		{
			int32& Dst = *reinterpret_cast<int32*>(Ptr);
			*reinterpret_cast<int32*>(Ptr) = IntCastChecked<int32>((int64)Dst + Value);
		}
	}
};

void PatchSummaryOffsets(FPackageFileSummary& Dst, int64 OffsetFrom, int64 OffsetDelta)
{
	if (!OffsetDelta)
	{
		return;
	}

	constexpr FSummaryOffsetMeta OffsetTable[] = {

#define UE_POPULATE_OFFSET_INFO(NAME)					\
			(uint32)STRUCT_OFFSET(FPackageFileSummary, NAME),	\
			std::is_same_v<decltype(((FPackageFileSummary*)0)->NAME), int64>

				{ UE_POPULATE_OFFSET_INFO(NameOffset) },
				{ UE_POPULATE_OFFSET_INFO(SoftObjectPathsOffset) },
				{ UE_POPULATE_OFFSET_INFO(GatherableTextDataOffset) },
				{ UE_POPULATE_OFFSET_INFO(ImportOffset) },
				{ UE_POPULATE_OFFSET_INFO(ExportOffset) },
				{ UE_POPULATE_OFFSET_INFO(DependsOffset) },
				{ UE_POPULATE_OFFSET_INFO(SoftPackageReferencesOffset) },
				{ UE_POPULATE_OFFSET_INFO(SearchableNamesOffset) },
				{ UE_POPULATE_OFFSET_INFO(ThumbnailTableOffset) },
				{ UE_POPULATE_OFFSET_INFO(AssetRegistryDataOffset) },
				{ UE_POPULATE_OFFSET_INFO(BulkDataStartOffset) },
				{ UE_POPULATE_OFFSET_INFO(WorldTileInfoDataOffset) },
				{ UE_POPULATE_OFFSET_INFO(PreloadDependencyOffset) },
				{ UE_POPULATE_OFFSET_INFO(PayloadTocOffset) },

	#undef UE_POPULATE_OFFSET_INFO
	};

	for (const FSummaryOffsetMeta& OffsetData : OffsetTable)
	{
		if (OffsetData.Value(Dst) > OffsetFrom)
		{
			OffsetData.PatchOffsetValue(Dst, OffsetDelta);
		}
	}
};

FAssetDataTagMap MakeTagMap(const TArray<UE::AssetRegistry::FDeserializeTagData>& TagData)
{
	FAssetDataTagMap Out;
	Out.Reserve(TagData.Num());
	for (const UE::AssetRegistry::FDeserializeTagData& Tag : TagData)
	{
		if (!Tag.Key.IsEmpty() && !Tag.Value.IsEmpty())
		{
			Out.Add(*Tag.Key, Tag.Value);
		}
	}

	return Out;
}

// The information we need in the task to do patching.
class FAssetHeaderPatcherInner
{
public:
	using EResult = FAssetHeaderPatcher::EResult;

	struct FThumbnailEntry
	{
		FString ObjectShortClassName;
		FString ObjectPathWithoutPackageName;
		int32 FileOffset = 0;
		int32 Delta = 0;
	};

	FAssetHeaderPatcherInner(const FString& InSrcAsset, const FString& InDstAsset, const TMap<FString, FString>& InStringReplacements, const TMap<FString, FString>& InStringMountPointReplacements, FArchive* InDstArchive = nullptr)
		: SrcAsset(InSrcAsset)
		, DstAsset(InDstAsset)
		, StringReplacements(InStringReplacements)
		, StringMountPointReplacements(InStringMountPointReplacements)
		, DstArchive(InDstArchive)
		, bPatchPrimaryAssetTag(false)
		, bIsNonOneFilePerActorPackage(false)
	{
		for (auto TagToIgnore : TagsToIgnore)
		{
			IgnoredTags.Add(TagToIgnore);
		}
	}

	bool DoPatch(FString& InOutString);
	bool DoPatch(FName& InOutName);
	bool DoPatch(FObjectResource& InOutResource, bool bIsExport, FName& OutPatchedObjectName);
	bool DoPatch(FSoftObjectPath& InOutSoft);
	bool DoPatch(FTopLevelAssetPath& InOutPath);
	bool DoPatch(FGatherableTextData& InOutGatherablerTextData);
	bool DoPatch(FThumbnailEntry& InOutThumbnailEntry);
	bool RemapFName(FName SrcName, FName DstName);
	bool ShouldReplaceMountPoint(const FStringView InPath, FStringView& OutSrcMountPoint, FStringView& OutDstMountPoint);
	void PatchNameTable();

	FAssetHeaderPatcher::EResult PatchHeader();
	FAssetHeaderPatcher::EResult PatchHeader_Deserialize();
	void PatchHeader_PatchSections();
	FAssetHeaderPatcher::EResult PatchHeader_WriteDestinationFile();
	void DumpState(FStringView InDir);

	TSet<FString> IgnoredTags;

	const FString& SrcAsset;
	const FString& DstAsset;
	const TMap<FString, FString>& StringReplacements;
	const TMap<FString, FString>& StringMountPointReplacements;
	FArchive* DstArchive = nullptr;
	TUniquePtr<FArchive> DstArchiveOwner;

	TArray64<uint8> SrcBuffer;

	struct FHeaderInformation
	{
		int64 SummarySize = -1;
		int64 NameTableSize = -1;
		int64 SoftObjectPathListSize = -1;
		int64 GatherableTextDataSize = -1;
		int64 ImportTableSize = -1;
		int64 ExportTableSize = -1;
		int64 SoftPackageReferencesListSize = -1;
		int64 ThumbnailTableSize = -1;
		int64 SearchableNamesMapSize = -1;
		int64 AssetRegistryDataSize = -1;
		int64 PackageTrailerSize = -1;
	};

	FHeaderInformation HeaderInformation;
	FPackageFileSummary Summary;
	FName OriginalPackagePath;					 // e.g. "/MountName/TopLevelPackageName"
	FName OriginalNonOneFilePerActorPackagePath; // e.g. "/MountName/MountName"
	FString OriginalPrimaryAssetName;			 // e.g. "MountName"
	bool bPatchPrimaryAssetTag;
	bool bIsNonOneFilePerActorPackage;

	// NameTable Members
	TArray<FName> NameTable;
	TMap<FNameEntryId, int32> NameToIndexMap;
	TMap<FNameEntryId, FNameEntryId> RenameMap;
	TSet<FNameEntryId> AddedNames;

	TArray<FSoftObjectPath> SoftObjectPathTable;
	TArray<FGatherableTextData> GatherableTextDataTable;
	TArray<FObjectImport> ImportTable;
	TArray<FObjectExport> ExportTable;
	TArray<FName> SoftPackageReferencesTable;
	TMap<FPackageIndex, TArray<FName>> SearchableNamesMap;
	TArray<FThumbnailEntry> ThumbnailTable;

	// Asset registry data information
	struct FAssetRegistryObjectData
	{
		UE::AssetRegistry::FDeserializeObjectPackageData ObjectData;
		TArray<UE::AssetRegistry::FDeserializeTagData> TagData;
	};

	struct FAssetRegistryData
	{
		int64 SectionSize = -1;
		UE::AssetRegistry::FDeserializePackageData PkgData;
		TArray<FAssetRegistryObjectData> ObjectData;
	};
	FAssetRegistryData AssetRegistryData;
};

FAssetHeaderPatcher::EResult FAssetHeaderPatcher::DoPatch(const FString& InSrcAsset, const FString& InDstAsset, const FContext& InContext)
{
	FAssetHeaderPatcherInner Inner(InSrcAsset, InDstAsset, InContext.StringReplacements, InContext.StringMountReplacements);

	if (!FFileHelper::LoadFileToArray(Inner.SrcBuffer, *Inner.SrcAsset))
	{
		UE_LOG(LogAssetHeaderPatcher, Error, TEXT("Failed to load %s"), *Inner.SrcAsset);
		return FAssetHeaderPatcherInner::EResult::ErrorFailedToLoadSourceAsset;
	}
	else
	{
		return Inner.PatchHeader();
	}
}

void FAssetHeaderPatcher::Reset()
{
	ErroredFiles.Empty();
	PatchedFiles.Empty();

	PatchingTask = UE::Tasks::FTask();
	Status = EResult::NotStarted;
	bCancelled = false;
}
void FAssetHeaderPatcher::SetContext(FContext InContext)
{
	checkf(!IsPatching(), TEXT("Cannot set the patcher context while patching"));
	Context = InContext;
	Reset();
}

UE::Tasks::FTask FAssetHeaderPatcher::PatchAsync(int32* InOutNumFilesToPatch, int32* InOutNumFilesPatched)
{
	return PatchAsync(InOutNumFilesToPatch, InOutNumFilesPatched, FAssetHeaderPatcherCompletionDelegate(), FAssetHeaderPatcherCompletionDelegate());
}

UE::Tasks::FTask FAssetHeaderPatcher::PatchAsync(int32* InOutNumFilesToPatch, int32* InOutNumFilesPatched, FAssetHeaderPatcherCompletionDelegate InOnSuccess, FAssetHeaderPatcherCompletionDelegate InOnError)
{
	PatchedFiles = Context.FilePathRenameMap;
	if (InOutNumFilesToPatch)
	{
		*InOutNumFilesToPatch = PatchedFiles.Num();
	}

	// Before we start patching we need to apply any patching redirects that exist
	FCoreRedirects::AddRedirectList(Context.Redirects, TEXT("Asset Header Patcher"));

	// Spawn tasks (Scatter)
	UE::Tasks::FTask PatchAssetsCleanupTask;
	TArray<UE::Tasks::FTask> PatchAssetTasks;

	// Note we are scheduling and launching tasks one at a time rather than preparing all jobs and launching all at once.
	// While this means more overhead scheduling, it means that we won't have many tasks all hit the filesystem at the same time
	// attempting to read and (more importantly) write to disk at the exact same time.
	constexpr bool bSingleThreaded = false; // Useful for debugging
	for (const TTuple<FString, FString>& Filename : PatchedFiles)
	{
		auto DoPatchFn = [this, 
			SrcFilename = Filename.Key,
			DstFilename = Filename.Value,
			NumPatched = InOutNumFilesPatched, 
			OnSuccess = InOnSuccess,
			OnError = InOnError]()
			{
				// Even if we are cancelled, increment our progress
				if (NumPatched)
				{
					// We don't support C++20 in all modules and platforms yet and avoid using atomic_ref as a result
					FPlatformAtomics::InterlockedAdd((volatile int32*)NumPatched, 1);
				}

				if (bCancelled)
				{
					return;
				}

				FAssetHeaderPatcher::EResult Result = FAssetHeaderPatcher::DoPatch(SrcFilename, DstFilename, Context);
				if (Result != FAssetHeaderPatcher::EResult::Success)
				{
					FScopeLock Lock(&ErroredFilesLock);
					// Don't lose our cancelled state, even when there are errors
					if (Status != EResult::Cancelled)
					{
						Status = Result;
					}
					ErroredFiles.Add(SrcFilename, Result);

					OnError.ExecuteIfBound(SrcFilename, DstFilename);
				}
				else
				{
					OnSuccess.ExecuteIfBound(SrcFilename, DstFilename);
				}
			};

		if constexpr (bSingleThreaded)
		{
			DoPatchFn();
		}
		else
		{
			PatchAssetTasks.Add(UE::Tasks::Launch(UE_SOURCE_LOCATION, MoveTemp(DoPatchFn)));
		}
	}

	// Once all tasks have completed, remove the redirects before we declare Patching complete
	UE::Tasks::FTask PatcherCleanupTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [this]()
		{
			FCoreRedirects::RemoveRedirectList(Context.Redirects, TEXT("Asset Header Patcher"));

			if (Status != EResult::Cancelled && ErroredFiles.IsEmpty())
			{
				Status = EResult::Success;
			}

			{
				FScopeLock Lock(&ErroredFilesLock);
				for (auto& ErroredFile : ErroredFiles)
				{
					PatchedFiles.Remove(ErroredFile.Key);
				}
			}

		}, UE::Tasks::Prerequisites(PatchAssetTasks));

	Status = EResult::InProgress;

	return PatcherCleanupTask;
}

FAssetHeaderPatcher::EResult FAssetHeaderPatcherInner::PatchHeader()
{
	FAssetHeaderPatcher::EResult Result = PatchHeader_Deserialize();
	if (Result != EResult::Success)
	{
		return Result;
	}
	
	if (DumpOutputDirectory.IsEmpty())
	{
		PatchHeader_PatchSections();
	}
	else
	{
		FString BaseDir = DumpOutputDirectory;
		FPaths::NormalizeDirectoryName(BaseDir);

		FString BeforeDir = BaseDir / FString(TEXT("Before"));
		FPaths::RemoveDuplicateSlashes(BeforeDir);
		DumpState(BeforeDir);

		PatchHeader_PatchSections();

		FString AfterDir = BaseDir / FString(TEXT("After"));
		FPaths::RemoveDuplicateSlashes(AfterDir);
		DumpState(AfterDir);
	}

	return PatchHeader_WriteDestinationFile();
}

FAssetHeaderPatcher::EResult FAssetHeaderPatcherInner::PatchHeader_Deserialize()
{
	FReadFNameAs2IntFromMemoryReader MemAr(NameTable, SrcBuffer.GetData(), SrcBuffer.Num());

	MemAr << Summary;
	HeaderInformation.SummarySize = MemAr.Tell();

	// Summary.PackageName isn't always serialized. In such cases, determine the package name from the file name
	if (Summary.PackageName.IsEmpty() || Summary.PackageName.Equals(TEXT("None")))
	{
		// e.g. "../../Some/Long/Path/MyPlugin/Plugins/MyPackage/Content/TopLevelAssetName.uasset"
		TStringView Path(SrcAsset);

		static const TStringView ContentDir(TEXT("/Content/"));
		int32 Pos = Path.Find(ContentDir, ESearchCase::IgnoreCase);
		if (Pos <= 0)
		{
			UE_LOG(LogAssetHeaderPatcher, Error, TEXT("Cannot patch '%s': Package header is missing a 'PackageName' string, nor could a PackageName be deduced."), *SrcAsset);
			return FAssetHeaderPatcher::EResult::ErrorEmptyRequireSection;
		}
		
		int32 MountNamePos;
		TStringView LeftPath(Path.GetData(), Pos);
		if (!LeftPath.FindLastChar(TEXT('/'), MountNamePos))
		{
			UE_LOG(LogAssetHeaderPatcher, Error, TEXT("Cannot patch '%s': Package header is missing a 'PackageName' string, nor could a PackageName be deduced."), *SrcAsset);
			return FAssetHeaderPatcher::EResult::ErrorEmptyRequireSection;
		}

		int32 ExtensionPos;
		TStringView RightPath(Path.GetData() + Pos + ContentDir.Len());
		if (!RightPath.FindLastChar(TEXT('.'), ExtensionPos))
		{
			UE_LOG(LogAssetHeaderPatcher, Error, TEXT("Cannot patch '%s': Package header is missing a 'PackageName' string, nor could a PackageName be deduced."), *SrcAsset);
			return FAssetHeaderPatcher::EResult::ErrorEmptyRequireSection;
		}

		TStringView MountName(LeftPath.GetData() + MountNamePos, (Pos - MountNamePos) + 1); // + 1 so we can include the '/' from "/Content"
		TStringView AssetPath(RightPath.GetData(), ExtensionPos);
		Summary.PackageName.Empty(MountName.Len() + AssetPath.Len());
		Summary.PackageName.Append(MountName);
		Summary.PackageName.Append(AssetPath);
	}

	// Store the original name as an FName as it will be used when
	// patching paths for other objects in the package
	{
		OriginalPackagePath = FName(Summary.PackageName, NAME_NO_NUMBER_INTERNAL);

		// Some ObjectPaths have an implied package, however when it comes to 
		// non-One File Per Actor packages, the implied package is the map package
		// so we determine which package we are and cache the map name in case we need it
		{
			bIsNonOneFilePerActorPackage = false;
			TStringBuilder<256> PathBuilder;
			PathBuilder.AppendChar(TEXT('/'));
			PathBuilder.Append(FPackagePath::GetExternalActorsFolderName());
			PathBuilder.AppendChar(TEXT('/'));
			if (Summary.PackageName.Contains(PathBuilder))
			{
				bIsNonOneFilePerActorPackage = true;
			}
			else
			{
				PathBuilder.Reset();
				PathBuilder.AppendChar(TEXT('/'));
				PathBuilder.Append(FPackagePath::GetExternalObjectsFolderName());
				PathBuilder.AppendChar(TEXT('/'));
				bIsNonOneFilePerActorPackage = Summary.PackageName.Contains(PathBuilder);
			}

			int32 SlashPos = INDEX_NONE;
			FStringView PackageRoot(Summary.PackageName);
			if (!PackageRoot.FindChar(TEXT('/'), SlashPos) || SlashPos != 0)
			{
				UE_LOG(LogAssetHeaderPatcher, Error, TEXT("Cannot patch '%s': PackageName is malformed."), *SrcAsset);
				return FAssetHeaderPatcher::EResult::ErrorFailedToDeserializeSourceAsset;
			}

			PackageRoot.RightChopInline(1); // Drop the first slash
			if (!PackageRoot.FindChar(TEXT('/'), SlashPos))
			{
				UE_LOG(LogAssetHeaderPatcher, Error, TEXT("Cannot patch '%s': PackageName is malformed."), *SrcAsset);
				return FAssetHeaderPatcher::EResult::ErrorFailedToDeserializeSourceAsset;
			}

			PathBuilder.Reset();
			PathBuilder.AppendChar(TEXT('/'));
			PathBuilder.Append(PackageRoot.GetData(), SlashPos);
			PathBuilder.AppendChar(TEXT('/'));
			PathBuilder.Append(PackageRoot.GetData(), SlashPos);
			OriginalNonOneFilePerActorPackagePath = FName(PathBuilder);

			// While here set the OriginalPrimaryAssetName which is used in AssetRegistry Tag lookups for GameFeatureData
			bPatchPrimaryAssetTag = FPathViews::GetBaseFilename(Summary.PackageName) == TEXT("GameFeatureData");
			OriginalPrimaryAssetName.Empty();
			OriginalPrimaryAssetName.Append(PackageRoot.GetData(), SlashPos);
		}
	}

	// set version numbers so components branch correctly
	MemAr.SetUEVer(Summary.GetFileVersionUE());
	MemAr.SetLicenseeUEVer(Summary.GetFileVersionLicenseeUE());
	MemAr.SetEngineVer(Summary.SavedByEngineVersion);
	MemAr.SetCustomVersions(Summary.GetCustomVersionContainer());
	if (Summary.GetPackageFlags() & PKG_FilterEditorOnly)
	{
		MemAr.SetFilterEditorOnly(true);
	}

	if (Summary.DataResourceOffset > 0)
	{
		// Should only be set in cooked data. If that changes, we need to add code to patch it
		UE_LOG(LogAssetHeaderPatcher, Error, TEXT("Asset %s has an unexpected DataResourceOffset"), *SrcAsset);
		return EResult::ErrorUnexpectedSectionOrder;
	}

	if (Summary.NameCount > 0)
	{
		MemAr.Seek(Summary.NameOffset);
		NameTable.Reserve(Summary.NameCount);
		for (int32 NameMapIdx = 0; NameMapIdx < Summary.NameCount; ++NameMapIdx)
		{
			FNameEntrySerialized NameEntry(ENAME_LinkerConstructor);
			MemAr << NameEntry;
			NameTable.Add(FName(NameEntry));
		}

		HeaderInformation.NameTableSize = MemAr.Tell() - HeaderInformation.SummarySize;

		// Initialize a mapping for Name to index in NameTable as we will use
		// this for patching in new names and to determine if multiple FNames share the same 
		// value but might not after patching (i.e. their use of the name differs based on context, and
		// post-patching the FNames in those contexts no longer match.
		NameToIndexMap.Empty(NameTable.Num());
		RenameMap.Reserve(NameTable.Num());
		AddedNames.Empty();
		for (int32 i = 0; i < NameTable.Num(); ++i)
		{
			NameToIndexMap.Add(NameTable[i].GetDisplayIndex(), i);
		}
	}

	if (Summary.SoftObjectPathsCount > 0)
	{
		MemAr.Seek(Summary.SoftObjectPathsOffset);
		SoftObjectPathTable.Reserve(Summary.SoftObjectPathsCount);
		for (int32 Idx = 0; Idx < Summary.SoftObjectPathsCount; ++Idx)
		{
			FSoftObjectPath& PathRef = SoftObjectPathTable.AddDefaulted_GetRef();
			PathRef.SerializePath(MemAr);
		}
		HeaderInformation.SoftObjectPathListSize = MemAr.Tell() - Summary.SoftObjectPathsOffset;
	}
	else if(Summary.GetFileVersionUE() >= EUnrealEngineObjectUE5Version::ADD_SOFTOBJECTPATH_LIST)
	{
		HeaderInformation.SoftObjectPathListSize = 0;
	}
	else
	{
		UE_LOG(LogAssetHeaderPatcher, Error, TEXT("Asset '%s' is too old to be used with AssetHeaderPatching. Please resave the file before trying to patch again."), *SrcAsset);
		return EResult::ErrorUnkownSection;
	}

	if (Summary.GatherableTextDataCount > 0)
	{
		MemAr.Seek(Summary.GatherableTextDataOffset);
		GatherableTextDataTable.Reserve(Summary.GatherableTextDataCount);
		for (int32 GatherableTextDataIndex = 0; GatherableTextDataIndex < Summary.GatherableTextDataCount; ++GatherableTextDataIndex)
		{
			FGatherableTextData& GatherableTextData = GatherableTextDataTable.Emplace_GetRef();
			MemAr << GatherableTextData;
		}

		HeaderInformation.GatherableTextDataSize = MemAr.Tell() - Summary.GatherableTextDataOffset;
	}
	else
	{
		HeaderInformation.GatherableTextDataSize = 0;
	}

#define UE_CHECK_AND_SET_ERROR_AND_RETURN(EXP)	\
	do											\
	{											\
		if (EXP)								\
		{										\
			UE_LOG(LogAssetHeaderPatcher, Log, TEXT("Asset %s fails %s"), *SrcAsset, TEXT(#EXP));	\
			return EResult::ErrorBadOffset;		\
		}										\
	}											\
	while(0)

	if (Summary.ImportCount > 0)
	{
		UE_CHECK_AND_SET_ERROR_AND_RETURN(Summary.ImportOffset >= Summary.TotalHeaderSize);
		UE_CHECK_AND_SET_ERROR_AND_RETURN(Summary.ImportOffset < 0);

		MemAr.Seek(Summary.ImportOffset);
		ImportTable.Reserve(Summary.ImportCount);
		for (int32 ImportIndex = 0; ImportIndex < Summary.ImportCount; ++ImportIndex)
		{
			FObjectImport& Import = ImportTable.Emplace_GetRef();
			MemAr << Import;
		}

		HeaderInformation.ImportTableSize = MemAr.Tell() - Summary.ImportOffset;
	}
	else
	{
		HeaderInformation.ImportTableSize = 0;
	}

	if (Summary.ExportCount > 0)
	{
		UE_CHECK_AND_SET_ERROR_AND_RETURN(Summary.ExportOffset >= Summary.TotalHeaderSize);
		UE_CHECK_AND_SET_ERROR_AND_RETURN(Summary.ExportOffset < 0);

		MemAr.Seek(Summary.ExportOffset);
		ExportTable.Reserve(Summary.ExportCount);
		for (int32 ExportIndex = 0; ExportIndex < Summary.ExportCount; ++ExportIndex)
		{
			FObjectExport& Export = ExportTable.Emplace_GetRef();
			MemAr << Export;
		}

		HeaderInformation.ExportTableSize = MemAr.Tell() - Summary.ExportOffset;
	}
	else
	{
		HeaderInformation.ExportTableSize = 0;
	}

#undef UE_CHECK_AND_SET_ERROR_AND_RETURN

	if (Summary.SoftPackageReferencesCount)
	{
		MemAr.Seek(Summary.SoftPackageReferencesOffset);
		SoftPackageReferencesTable.Reserve(Summary.SoftPackageReferencesCount);
		for (int32 Idx = 0; Idx < Summary.SoftPackageReferencesCount; ++Idx)
		{
			FName& Reference = SoftPackageReferencesTable.Emplace_GetRef();
			MemAr << Reference;
		}

		HeaderInformation.SoftPackageReferencesListSize = MemAr.Tell() - Summary.SoftPackageReferencesOffset;
	}
	else
	{
		HeaderInformation.SoftPackageReferencesListSize = 0;
	}

	if (Summary.SearchableNamesOffset)
	{
		MemAr.Seek(Summary.SearchableNamesOffset);
		FLinkerTables LinkerTables;
		LinkerTables.SerializeSearchableNamesMap(MemAr);
		SearchableNamesMap = MoveTemp(LinkerTables.SearchableNamesMap);

		HeaderInformation.SearchableNamesMapSize = MemAr.Tell() - Summary.SearchableNamesOffset;
	}

	if (Summary.ThumbnailTableOffset)
	{
		MemAr.Seek(Summary.ThumbnailTableOffset);

		int32 ThumbnailCount = 0;
		MemAr << ThumbnailCount;

		ThumbnailTable.Reserve(ThumbnailCount);
		for (int32 Index = 0; Index < ThumbnailCount; ++Index)
		{
			FThumbnailEntry& Entry = ThumbnailTable.Emplace_GetRef();
			MemAr << Entry.ObjectShortClassName;
			MemAr << Entry.ObjectPathWithoutPackageName;
			MemAr << Entry.FileOffset;
		}

		HeaderInformation.ThumbnailTableSize = MemAr.Tell() - Summary.ThumbnailTableOffset;
	}

	// Load AR data
	if (Summary.AssetRegistryDataOffset)
	{
		MemAr.Seek(Summary.AssetRegistryDataOffset);

		UE::AssetRegistry::EReadPackageDataMainErrorCode ErrorCode;
		if (!AssetRegistryData.PkgData.DoSerialize(MemAr, Summary, ErrorCode))
		{
			UE_LOG(LogAssetHeaderPatcher, Error, TEXT("Failed to deserialize asset registry data for %s"), *SrcAsset);
			return EResult::ErrorFailedToDeserializeSourceAsset;
		}

		AssetRegistryData.ObjectData.Reserve(AssetRegistryData.PkgData.ObjectCount);
		for (int32 i = 0; i < AssetRegistryData.PkgData.ObjectCount; ++i)
		{
			FAssetRegistryObjectData& ObjData = AssetRegistryData.ObjectData.Emplace_GetRef();
			if (!ObjData.ObjectData.DoSerialize(MemAr, ErrorCode))
			{
				UE_LOG(LogAssetHeaderPatcher, Error, TEXT("Failed to deserialize asset registry data for %s"), *SrcAsset);
				return EResult::ErrorFailedToDeserializeSourceAsset;
			}

			ObjData.TagData.Reserve(ObjData.ObjectData.TagCount);
			for (int32 j = 0; j < ObjData.ObjectData.TagCount; ++j)
			{
				if (!ObjData.TagData.Emplace_GetRef().DoSerialize(MemAr, ErrorCode))
				{
					UE_LOG(LogAssetHeaderPatcher, Error, TEXT("Failed to deserialize asset registry data for %s"), *SrcAsset);
					return EResult::ErrorFailedToDeserializeSourceAsset;
				}
			}
		}

		AssetRegistryData.SectionSize = MemAr.Tell() - Summary.AssetRegistryDataOffset;
	}

	return EResult::Success;
}

bool FAssetHeaderPatcherInner::ShouldReplaceMountPoint(const FStringView InPath, FStringView& OutSrcMountPoint, FStringView& OutDstMountPoint)
{
	for (auto& MountPair : StringMountPointReplacements)
	{
		const FStringView SrcMount(MountPair.Key);
		const FStringView DstMount(MountPair.Value);

		if (InPath.StartsWith(SrcMount))
		{
			OutSrcMountPoint = SrcMount;
			OutDstMountPoint = DstMount;
			return true;
		}
	}
	return false;
}

// Note, like DoPatch(FName&) we should strive to remove this method in favour of one that understands
// the context for which this string belongs to. Patching it based on search and replace, is going to be 
// error-prone and should be avoided.
bool FAssetHeaderPatcherInner::DoPatch(FString& InOutString)
{
	// Attempt a direct replacement
	{
		// Find a Path, change a Path.
		FStringView MaybeReplacement = Find(StringReplacements, InOutString);
		if (!MaybeReplacement.IsEmpty())
		{
			InOutString = MaybeReplacement;
			return true;
		}
	}

	// Direct replacement failed so now try substring replacements

	bool bDidPatch = false;
	TStringBuilder<NAME_SIZE> DstStringBuilder;
	{
		// Patch Object paths with sub-object (not-necessarily quoted)
		// Path occurs to the left of a ":"
		int32 ColonPos;
		FStringView PathView(InOutString);
		while (PathView.FindChar(SUBOBJECT_DELIMITER_CHAR, ColonPos))
		{
			if ((ColonPos + 1) < PathView.Len() && PathView[ColonPos + 1] == SUBOBJECT_DELIMITER_CHAR)
			{
				// "::" is not a path delim
				PathView.RightChopInline(ColonPos + 1);
				continue;
			}

			// Presumably we have found the start of a path's sub-object path. Create a new 
			// view for our possible ObjectPath and walk backwards confirming we are in a path
			// otherwise start over at the next ':'
			FStringView ObjectPathView(PathView.GetData(), ColonPos);

			int32 OuterDelimiterPos;
			if (!ObjectPathView.FindLastChar(TEXT('.'), OuterDelimiterPos))
			{
				// A ':' but '.' before it is not an object path
				PathView.RightChopInline(ColonPos + 1);
				continue;
			}

			int32 LastPathDelimiterPos = INDEX_NONE;
			int32 Index = OuterDelimiterPos;
			while (--Index >= 0)
			{
				if (ObjectPathView[Index] == TEXT('/'))
				{
					LastPathDelimiterPos = Index;
				}
				else
				{
					// Confirm we are still in a path
					int32 PosInvalidChar = 0;
					if (InvalidObjectPathCharacters.FindChar(ObjectPathView[Index], PosInvalidChar))
					{
						break;
					}
				}
			}

			if (LastPathDelimiterPos < 0)
			{
				// No '/' means we aren't in a path
				PathView.RightChopInline(ColonPos + 1);
				continue;
			}

			FStringView SrcMountPoint;
			FStringView DstMountPoint;
			FStringView ObjectPath(PathView.GetData() + LastPathDelimiterPos, ColonPos - LastPathDelimiterPos);
			FStringView MaybeReplacement = Find(StringReplacements, ObjectPath);
			if (!MaybeReplacement.IsEmpty())
			{
				FStringView LeftPart(*InOutString, int32(PathView.GetData() - *InOutString) + LastPathDelimiterPos);
				FStringView RightPart(PathView.GetData() + ColonPos);

				DstStringBuilder.Reset();
				DstStringBuilder.Append(LeftPart);
				DstStringBuilder.Append(MaybeReplacement);
				DstStringBuilder.Append(RightPart);

				InOutString = DstStringBuilder.ToString();
				bDidPatch = true;

				// Keep searching until the path is depleted since there might be more than one path to replace
				PathView = FStringView(*InOutString + LeftPart.Len() + MaybeReplacement.Len() + 1);
			}
			else if (ShouldReplaceMountPoint(ObjectPath, SrcMountPoint, DstMountPoint))
			{
				FStringView LeftPart(*InOutString, int32(PathView.GetData() - *InOutString) + LastPathDelimiterPos);
				FStringView RightPart(PathView.GetData() + LastPathDelimiterPos + SrcMountPoint.Len());

				DstStringBuilder.Reset();
				DstStringBuilder.Append(LeftPart);
				DstStringBuilder.Append(DstMountPoint);
				DstStringBuilder.Append(RightPart);

				InOutString = DstStringBuilder.ToString();
				bDidPatch = true;

				// Keep searching until the path is depleted since there might be more than one path to replace
				// Skip to the colon since we know we didn't have any matches within the quotes beyond the mount
				PathView = FStringView(*InOutString + ColonPos + 1);
			}
			else
			{
				// No match but keep searching as there may be more than one ':'
				PathView.RightChopInline(ColonPos + 1);
			}
		}
	}

	{
		// Patch quoted paths.
		// Path occurs to the right of the first "'" or """ 
		auto PatchQuotedPath = [this, &DstStringBuilder](FString& StringToPatch, FStringView Quote)
			{
				int32 FirstQuotePos = INDEX_NONE;
				bool bFoundReplacement = false;
				FStringView PathView(StringToPatch);
				while ((FirstQuotePos = PathView.Find(Quote, 0, ESearchCase::CaseSensitive)) != INDEX_NONE)
				{
					int32 SecondQuotePos = PathView.Find(Quote, FirstQuotePos + 1, ESearchCase::CaseSensitive);
					if (SecondQuotePos == INDEX_NONE)
					{
						// If there isn't a second quote we're done
						break;
					}

					FStringView SrcMountPoint;
					FStringView DstMountPoint;
					FStringView StrippedQuotedPath = FStringView(PathView.GetData() + FirstQuotePos + 1, SecondQuotePos - FirstQuotePos - 1); // +1 and -1 are to skip the quotes
					FStringView MaybeReplacement = Find(StringReplacements, StrippedQuotedPath);
					if (!MaybeReplacement.IsEmpty())
					{
						FStringView LeftPart(*StringToPatch, int32(PathView.GetData() - *StringToPatch) + FirstQuotePos + 1); // +1 to ensure we include the quote
						FStringView RightPart(PathView.GetData() + SecondQuotePos);

						DstStringBuilder.Reset();
						DstStringBuilder.Append(LeftPart);
						DstStringBuilder.Append(MaybeReplacement);
						DstStringBuilder.Append(RightPart);

						StringToPatch = DstStringBuilder.ToString();
						bFoundReplacement = true;

						// Keep searching until the path is depleted since there might be more than one path to replace
						PathView = FStringView(*StringToPatch + LeftPart.Len() + MaybeReplacement.Len() + 1);
					}
					else if (ShouldReplaceMountPoint(StrippedQuotedPath, SrcMountPoint, DstMountPoint))
					{
						FStringView LeftPart(*StringToPatch, int32(PathView.GetData() - *StringToPatch) + FirstQuotePos + 1); // +1 to ensure we include the quote
						FStringView RightPart(PathView.GetData() + FirstQuotePos + SrcMountPoint.Len() + 1); // +1 to ensure we skip the first quote

						DstStringBuilder.Reset();
						DstStringBuilder.Append(LeftPart);
						DstStringBuilder.Append(DstMountPoint);
						DstStringBuilder.Append(RightPart);

						StringToPatch = DstStringBuilder.ToString();
						bFoundReplacement = true;

						// Keep searching until the path is depleted since there might be more than one path to replace
						// Skip to the end quote since we know we didn't have any matches within the quotes beyond the mount
						PathView = FStringView(*StringToPatch + SecondQuotePos + 1);
					}
					else
					{
						// No match but keep searching as there may be more than one quoted path
						PathView.RightChopInline(SecondQuotePos + 1);
					}
				}
				return bFoundReplacement;
			};
		bDidPatch |= PatchQuotedPath(InOutString, TEXT("'"));
		bDidPatch |= PatchQuotedPath(InOutString, TEXT("\""));
	}

	return bDidPatch;
}

bool FAssetHeaderPatcherInner::RemapFName(FName SrcName, FName DstName)
{
	// NameTable entries only care about the comparison form (no number) so 
	// only consider that for remapping purposes
	FNameEntryId SrcComparisonId = SrcName.GetDisplayIndex();
	FNameEntryId DstComparisonId = DstName.GetDisplayIndex();
	if (SrcComparisonId == DstComparisonId)
	{
		return false;
	}
	checkf(DstName != NAME_None, TEXT("There should never be a None FName in the NameTable"));

	FNameEntryId* RemappedFName = RenameMap.Find(SrcComparisonId);
	if (RemappedFName)
	{
		// We already have a mapping. That is fine; we might have used the same FName in more than one place.
		// However, we need to be certain we are renaming the name to the same new name. If not, this means
		// the originals names overlapped but in the patched case they don't (e.g. A class FName may have matched a Package
		// name, but after patching it's possible _only_ the Package name has changed. In such a case we don't want to rename the 
		// class name inadvertently by patching the shared NameTable entry. If we have a mismatch with the new patched 
		// name, record the new name and we will append it to the NameTable later.
		if (*RemappedFName != DstComparisonId)
		{
			AddedNames.Add(DstComparisonId);
		}
	}
	else
	{
		RenameMap.Add(SrcComparisonId, DstComparisonId);
	}

	return true;
}

bool FAssetHeaderPatcherInner::DoPatch(FName& InOutName)
{
	// If we are given an FName to patch we have no real context as to what that FName is
	// so we conservatively assume it is a package path and attempt to patch that only
	FCoreRedirectObjectName SrcPackageName(NAME_None, NAME_None, InOutName);
	FCoreRedirectObjectName DstPackageName = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Package, SrcPackageName);
	if (RemapFName(SrcPackageName.PackageName, DstPackageName.PackageName))
	{
		InOutName = DstPackageName.PackageName;
		return true;
	}
	return false;
}

void FAssetHeaderPatcherInner::PatchNameTable()
{
	// Note, no number is assigned when replacing FNames as the NameTable only tracks unnumbered names

	// Update the NameTable with the known patched values and add our new patched names to the NameToIndex
	// map so we can validate that we always have a FName mapping to an entry in the name table when writing
	for (auto& Pair : RenameMap)
	{
		FNameEntryId SrcName = Pair.Key;
		FNameEntryId DstName = Pair.Value;
		int32* pSrcIndex = NameToIndexMap.Find(SrcName);
		checkf(pSrcIndex && *pSrcIndex < NameTable.Num(), TEXT("An FName remapping was done for a name (%s) not in the NameTable."), *FName::CreateFromDisplayId(DstName, NAME_NO_NUMBER_INTERNAL).ToString());
		int32 SrcIndex = *pSrcIndex;

		NameTable[SrcIndex] = FName::CreateFromDisplayId(DstName, NAME_NO_NUMBER_INTERNAL);
		NameToIndexMap.Add(DstName, SrcIndex);
	}

	for (FNameEntryId NewName : AddedNames)
	{
		FName NewFName = FName::CreateFromDisplayId(NewName, NAME_NO_NUMBER_INTERNAL);
		int32 NameTableIndex = NameTable.Num();

		NameTable.Add(NewFName);
		NameToIndexMap.Add(NewFName.GetDisplayIndex(), NameTableIndex);
	}

	Summary.NameCount = NameTable.Num();
}

bool FAssetHeaderPatcherInner::DoPatch(FSoftObjectPath& InOutSoft)
{
	FTopLevelAssetPath InOutTopLevelAssetPath = InOutSoft.GetAssetPath();
	if (!DoPatch(InOutTopLevelAssetPath))
	{
		return false;
	}

	InOutSoft.SetPath(InOutTopLevelAssetPath, InOutSoft.GetSubPathString());
	return true;
}

bool FAssetHeaderPatcherInner::DoPatch(FObjectResource& InOutResource, bool bIsExport, FName& OutPatchedObjectName)
{
	bool bOutermostIsExport = bIsExport;
	FPackageIndex OuterIndex = InOutResource.OuterIndex;
	TArray<FName, TInlineAllocator<8>> OuterStack;
	while (!OuterIndex.IsNull())
	{
		const FObjectResource* OuterResource;
		if (OuterIndex.IsImport())
		{
			bOutermostIsExport = false;
			OuterResource = &ImportTable[OuterIndex.ToImport()];
		}
		else
		{
			bOutermostIsExport = true;
			OuterResource = &ExportTable[OuterIndex.ToExport()];
		}

		OuterStack.Push(OuterResource->ObjectName);
		OuterIndex = OuterResource->OuterIndex;
	}

	FName SrcObjectName;
	FName SrcOuterName;
	FName SrcPackageName;
	bool bRemapByPackageName = false;
	if (OuterStack.Num() == 0)
	{
		if (bOutermostIsExport)
		{
			SrcPackageName = OriginalPackagePath;		// /Package/Package
			SrcOuterName = NAME_None;
			SrcObjectName = InOutResource.ObjectName;	// MyObject
		}
		else
		{
			// The ObjectName is a package
			SrcPackageName = InOutResource.ObjectName;	// /Package/Package
			SrcOuterName = NAME_None;
			SrcObjectName = NAME_None;
			bRemapByPackageName = true;
		}
	}
	else
	{
		SrcPackageName = bOutermostIsExport ? OriginalPackagePath : OuterStack.Pop();

		TStringBuilder<NAME_SIZE> OuterString;
		while (!OuterStack.IsEmpty())
		{
			FName Outer = OuterStack.Pop();
			Outer.ToString(OuterString);
			OuterString.AppendChar(TEXT('.'));
		}
		if (OuterString.Len())
		{
			OuterString.RemoveSuffix(1);
		}
		SrcOuterName = FName(OuterString);
		SrcObjectName = InOutResource.ObjectName;
	}
	
	const FCoreRedirectObjectName SrcObjectPath(SrcObjectName, SrcOuterName, SrcPackageName);
	const FCoreRedirectObjectName DstObjectPath = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_AllMask, SrcObjectPath);

	bool bPatched = false;
	if (!bRemapByPackageName)
	{
		bPatched = RemapFName(SrcObjectPath.ObjectName, DstObjectPath.ObjectName);
		OutPatchedObjectName = DstObjectPath.ObjectName;
	}
	else
	{
		bPatched = RemapFName(SrcObjectPath.PackageName, DstObjectPath.PackageName);
		OutPatchedObjectName = DstObjectPath.PackageName;
	}

#if WITH_EDITORONLY_DATA
	InOutResource.OldClassName = NAME_None;
#endif

	return bPatched;
}

bool FAssetHeaderPatcherInner::DoPatch(FTopLevelAssetPath& InOutPath)
{
	const FCoreRedirectObjectName SrcTopLevelAssetPath = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_AllMask, InOutPath);
	const FTopLevelAssetPath DstTopLevelAssetPath(SrcTopLevelAssetPath.ToString());

	bool bPatched = RemapFName(InOutPath.GetAssetName(), DstTopLevelAssetPath.GetAssetName());
	bPatched |= RemapFName(InOutPath.GetPackageName(), DstTopLevelAssetPath.GetPackageName());

	if (bPatched)
	{
		InOutPath = DstTopLevelAssetPath;
	}

	return bPatched;
}

bool FAssetHeaderPatcherInner::DoPatch(FGatherableTextData& InOutGatherableTextData)
{
	// There are various fields in FGatherableTextData however only one pertains to 
	// asset paths and types, SourceSiteContexts.SiteDescription. The rest are contextual
	// key-value pairs of text which are not references to assets/types and thus do not need patching
	// (at least we can't understand the context a priori to know if specialized code
	// may try to load from these strings)

	bool bDidPatch = false;
	for (FTextSourceSiteContext& SourceSiteContext : InOutGatherableTextData.SourceSiteContexts)
	{
		FStringView ClassName;
		FStringView PackagePath;
		FStringView ObjectName;
		FStringView SubObjectName;
		FPackageName::SplitFullObjectPath(SourceSiteContext.SiteDescription, ClassName, PackagePath, ObjectName, SubObjectName, true /*bDetectClassName*/);

		// Todo to use StringView logic above to reduce string copies
		FSoftObjectPath SiteDescriptionPath(SourceSiteContext.SiteDescription);
		if (!SiteDescriptionPath.IsValid())
		{
			continue;
		}

		FTopLevelAssetPath TopLevelAssetPath = SiteDescriptionPath.GetAssetPath();
		const FCoreRedirectObjectName RedirectedTopLevelAssetPath = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_AllMask, TopLevelAssetPath);
		const FTopLevelAssetPath PatchedTopLevelAssetPath(RedirectedTopLevelAssetPath.ToString());
		if (TopLevelAssetPath == PatchedTopLevelAssetPath)
		{
			continue;
		}
		bDidPatch = true;
		SiteDescriptionPath.SetPath(PatchedTopLevelAssetPath, SiteDescriptionPath.GetSubPathString());
		SourceSiteContext.SiteDescription = SiteDescriptionPath.ToString();
	}
	
	return bDidPatch;
}

bool FAssetHeaderPatcherInner::DoPatch(FThumbnailEntry& InThumbnailEntry)
{
	// These objects can potentially be paths to sub-objects. For renaming purposes we 
	// want to drop the sub-object path and grab the AssetName
	FStringView SrcObjectPathWithoutPackageName(InThumbnailEntry.ObjectPathWithoutPackageName);
	int32 ColonPos = INDEX_NONE;
	if (SrcObjectPathWithoutPackageName.FindChar(TEXT(':'), ColonPos))
	{
		SrcObjectPathWithoutPackageName.LeftChopInline(SrcObjectPathWithoutPackageName.Len() - ColonPos);
	}

	FName PackageFName = OriginalPackagePath;
	if (bIsNonOneFilePerActorPackage)
	{
		PackageFName = OriginalNonOneFilePerActorPackagePath;
	}

	const FCoreRedirectObjectName SrcTopLevelAssetName(FName(SrcObjectPathWithoutPackageName), NAME_None, PackageFName);
	const FCoreRedirectObjectName RedirectedTopLevelAssetName = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Object, SrcTopLevelAssetName);
	bool bPatched = RemapFName(SrcTopLevelAssetName.ObjectName, RedirectedTopLevelAssetName.ObjectName);

	const FCoreRedirectObjectName SrcClassName(FName(InThumbnailEntry.ObjectShortClassName), NAME_None, NAME_None);
	const FCoreRedirectObjectName RedirectedClassName = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class, SrcClassName);
	bPatched |= RemapFName(SrcClassName.ObjectName, RedirectedClassName.ObjectName);

	if (bPatched)
	{
		// Since we patched, we will cause the inline string name to affect the thumbnail offsets. Calculate the size change
		// here so we can use it during writing where we will fix up the offsets.
		int32 Delta = -(InThumbnailEntry.ObjectShortClassName.Len() + InThumbnailEntry.ObjectPathWithoutPackageName.Len());

		InThumbnailEntry.ObjectShortClassName = RedirectedClassName.ObjectName.ToString();
		InThumbnailEntry.ObjectPathWithoutPackageName = RedirectedTopLevelAssetName.ObjectName.ToString();
		Delta += InThumbnailEntry.ObjectShortClassName.Len() + InThumbnailEntry.ObjectPathWithoutPackageName.Len();

		InThumbnailEntry.Delta = Delta;
	}

	return bPatched;
}

void FAssetHeaderPatcherInner::PatchHeader_PatchSections()
{
	// Package Summary
	{
		const FCoreRedirectObjectName DstPackageName = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Package,
			FCoreRedirectObjectName(NAME_None, NAME_None, OriginalPackagePath));

		// This is a string, so we do not want to Remap the patched name unless it's a non-OFPA
		// package, in which case there will be an FName entry for this path
		Summary.PackageName = DstPackageName.PackageName.ToString();
		
		// It seems that non-OFPA packages tend to have the package name in the nametable, 
		// however it isn't a guarantee, so we confirm the name is there before remapping and
		// extend this special case of NameTable patching to all packages, OFPA or not.
		if (NameToIndexMap.Find(OriginalPackagePath.GetDisplayIndex()))
		{
			RemapFName(OriginalPackagePath, DstPackageName.PackageName);
		}
	}

	// Patching of the FObjectResource ObjectNames is deferred since when patching we need to 
	// walk the original names to determine if they need patching in the first place
	TMap<int32, FName> PatchedExportObjectNames;
	TMap<int32, FName> PatchedImportObjectNames;

	// Export Table
	{
		PatchedExportObjectNames.Reserve(ExportTable.Num());
		for (int32 i = 0; i < ExportTable.Num(); ++i)
		{
			FObjectExport& Export = ExportTable[i];
			FName PatchedObjectName;
			if (DoPatch(Export, true, PatchedObjectName))
			{
				PatchedExportObjectNames.Add(i, PatchedObjectName);
			}
		}
	}

	// Import table
	{
		PatchedImportObjectNames.Reserve(ImportTable.Num());
		for (int32 i = 0; i < ImportTable.Num(); ++i)
		{
			FObjectImport& Import = ImportTable[i];
			FName PatchedObjectName;
			if (DoPatch(Import, false, PatchedObjectName))
			{
				PatchedImportObjectNames.Add(i, PatchedObjectName);
			}

			const FCoreRedirectObjectName SrcClass(Import.ClassName, NAME_None, Import.ClassPackage);
			const FCoreRedirectObjectName DstClass = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class | ECoreRedirectFlags::Type_Package, SrcClass);

			if (RemapFName(SrcClass.ObjectName, DstClass.ObjectName))
			{
				Import.ClassName = DstClass.ObjectName;
			}

			if (RemapFName(SrcClass.PackageName, DstClass.PackageName))
			{
				Import.ClassPackage = DstClass.PackageName;
			}

#if WITH_EDITORONLY_DATA
			const FCoreRedirectObjectName SrcPackage(NAME_None, NAME_None, Import.PackageName);
			const FCoreRedirectObjectName DstPackage = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Package, SrcPackage);

			if (RemapFName(SrcPackage.PackageName, DstPackage.PackageName))
			{
				Import.PackageName = DstPackage.PackageName;
			}
#endif
		}
	}

	// Finish the FObjectResource patching that was deferred above for the ExportTable and ImportTable
	{
		for (auto& Pair : PatchedExportObjectNames)
		{
			int32 Index = Pair.Key;
			FName PatchedName = Pair.Value;
			ExportTable[Index].ObjectName = PatchedName;
		}

		for (auto& Pair : PatchedImportObjectNames)
		{
			int32 Index = Pair.Key;
			FName PatchedName = Pair.Value;
			ImportTable[Index].ObjectName = PatchedName;
		}
	}

	// Soft paths
	for (FSoftObjectPath& SoftObjectPath : SoftObjectPathTable)
	{
		DoPatch(SoftObjectPath);
	}

	// GatherableTextData table
	for (FGatherableTextData& GatherableTextData : GatherableTextDataTable)
	{
		DoPatch(GatherableTextData);
	}

	// Soft Package Reference's
	for (FName& Reference : SoftPackageReferencesTable)
	{
		FCoreRedirectObjectName SrcPackagePath(NAME_None, NAME_None, Reference);
		FCoreRedirectObjectName DstPackageName = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Package, SrcPackagePath);
		if (RemapFName(SrcPackagePath.PackageName, DstPackageName.PackageName))
		{
			Reference = DstPackageName.PackageName;
		}
	}

	// SearchableNamesMap
	for (auto& Pair : SearchableNamesMap)
	{
		TArray<FName>& Names = Pair.Value;
		for (FName& Name : Names)
		{
			DoPatch(Name);
		}
	}

	// Thumbnail Table
	for (FThumbnailEntry& ThumbnailEntry : ThumbnailTable)
	{
		DoPatch(ThumbnailEntry);
	}

	// Asset Register Data
	for (FAssetRegistryObjectData& ObjData : AssetRegistryData.ObjectData)
	{
		// ObjectPath is a toss-up. 
		// Sometimes it's a FTopLevelAssetPath with an implied PackageName (this package's name) and AssetName.
		// Sometimes it's a full FSoftPath (e.g. when dealing with ExternalObjects)
		FSoftObjectPath SrcObjectPath(ObjData.ObjectData.ObjectPath);
		{
			if (SrcObjectPath.IsValid())
			{
				FSoftObjectPath SrdDstObjectPath = SrcObjectPath;

				if (DoPatch(SrdDstObjectPath))
				{
					ObjData.ObjectData.ObjectPath = SrdDstObjectPath.ToString();
				}
			}
			else
			{
				FTopLevelAssetPath SrcDstTopLevelAssetPath(OriginalPackagePath, FName(ObjData.ObjectData.ObjectPath));
				SrcObjectPath.SetPath(SrcDstTopLevelAssetPath, SrcObjectPath.GetSubPathString());

				if (DoPatch(SrcDstTopLevelAssetPath))
				{
					ObjData.ObjectData.ObjectPath = SrcDstTopLevelAssetPath.GetAssetName().ToString();
				}
			}
		}

		// ObjData.ObjectData.ObjectClassName is a FTopLevelAssetPath stored as a string
		FTopLevelAssetPath SrcObjectClassName(ObjData.ObjectData.ObjectClassName);
		{
			FTopLevelAssetPath SrcDstObjectClassName = SrcObjectClassName;

			if (DoPatch(SrcDstObjectClassName))
			{
				ObjData.ObjectData.ObjectClassName = SrcDstObjectClassName.ToString();
			}
		}

		for (UE::AssetRegistry::FDeserializeTagData& TagData : ObjData.TagData)
		{
			if (IgnoredTags.Contains(TagData.Key))
			{
				continue;
			}

			// WorldPartitionActor metadata is special. It's an encoded string blob which needs
			// handling internally, so we make use of a custom patcher to let us intercept
			// various elements that might need patching.
			if (TagData.Key == FWorldPartitionActorDescUtils::ActorMetaDataTagName())
			{
				const FString LongPackageName(SrcAsset);
				const FString ObjectPath(ObjData.ObjectData.ObjectPath);
				const FTopLevelAssetPath AssetClassPathName(ObjData.ObjectData.ObjectClassName);
				const FAssetDataTagMap Tags(MakeTagMap(ObjData.TagData));
				const FAssetData AssetData(LongPackageName, ObjectPath, AssetClassPathName, Tags);

				struct FWorldPartitionAssetDataPatcherInner : FWorldPartitionAssetDataPatcher
				{
					FWorldPartitionAssetDataPatcherInner(FAssetHeaderPatcherInner* InInner) : Inner(InInner) {}
					virtual bool DoPatch(FString& InOutString) override 
					{ 
						return Inner->DoPatch(InOutString); 
					}
					virtual bool DoPatch(FName& InOutName) override 
					{ 
						// FNames are actually strings inside WorldPartitionActor metadata, and since a lone
						// FName has no context for how to patch it, convert it to a string to perform a 
						// best-effort search.
						FString NameString;
						InOutName.ToString(NameString);
						if (Inner->DoPatch(NameString))
						{
							InOutName = FName(NameString);
							return true;
						}
						return false;
					}
					virtual bool DoPatch(FSoftObjectPath& InOutSoft) override 
					{
						return Inner->DoPatch(InOutSoft);
					}
					virtual bool DoPatch(FTopLevelAssetPath& InOutPath) override 
					{ 
						return Inner->DoPatch(InOutPath);
					}
					FAssetHeaderPatcherInner* Inner;
				};

				FString PatchedAssetData;
				FWorldPartitionAssetDataPatcherInner Patcher(this);
				if (FWorldPartitionActorDescUtils::GetPatchedAssetDataFromAssetData(AssetData, PatchedAssetData, &Patcher))
				{
					TagData.Value = PatchedAssetData;
				}
			}
			// Special case for common Tag
			else if (bPatchPrimaryAssetTag && TagData.Key == TEXT("PrimaryAssetName"))
			{
				if (TagData.Value == OriginalPrimaryAssetName)
				{
					const FCoreRedirectObjectName DstPackageName = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Package,
						FCoreRedirectObjectName(NAME_None, NAME_None, OriginalPackagePath));

					TStringBuilder<256> Builder;
					DstPackageName.PackageName.ToString(Builder);
					FStringView PrimaryAssetView = Builder.ToView();
					ensure(PrimaryAssetView.Len() && PrimaryAssetView[0] == TEXT('/'));
					PrimaryAssetView.RemovePrefix(1);

					int32 SlashPos = INDEX_NONE;
					if (PrimaryAssetView.FindChar(TEXT('/'), SlashPos))
					{
						TagData.Value.Empty();
						TagData.Value.Append(PrimaryAssetView.GetData(), SlashPos);
					}
				}
			}
			else
			{
				DoPatch(TagData.Value);
			}
		}
	}

	// Do nametable patching last since we want to ensure we have determined all the remappings necessary
	PatchNameTable();
}

FAssetHeaderPatcher::EResult FAssetHeaderPatcherInner::PatchHeader_WriteDestinationFile()
{
	// Serialize modified sections and reconstruct the file	
	// Original offsets and sizes of any sections that will be patched
	//	  Tag											Offset									Size												bRequired
	const FSectionData SourceSections[] = {
		{ EPatchedSection::Summary,						0,										HeaderInformation.SummarySize,						true	},
		{ EPatchedSection::NameTable,					Summary.NameOffset,						HeaderInformation.NameTableSize,					true	},
		{ EPatchedSection::SoftPathTable,				Summary.SoftObjectPathsOffset,			HeaderInformation.SoftObjectPathListSize,			false	},
		{ EPatchedSection::GatherableTextDataTable,		Summary.GatherableTextDataOffset,		HeaderInformation.GatherableTextDataSize,			false	},
		{ EPatchedSection::ImportTable,					Summary.ImportOffset,					HeaderInformation.ImportTableSize,					true	},
		{ EPatchedSection::ExportTable,					Summary.ExportOffset,					HeaderInformation.ExportTableSize,					true	},
		{ EPatchedSection::SoftPackageReferencesTable,	Summary.SoftPackageReferencesOffset,	HeaderInformation.SoftPackageReferencesListSize,	false	},
		{ EPatchedSection::SearchableNamesMap,			Summary.SearchableNamesOffset,			HeaderInformation.SearchableNamesMapSize,			false	},
		{ EPatchedSection::ThumbnailTable,				Summary.ThumbnailTableOffset,			HeaderInformation.ThumbnailTableSize,				false	},
		{ EPatchedSection::AssetRegistryData,			Summary.AssetRegistryDataOffset,		AssetRegistryData.SectionSize,						true	},
	};

	const int32 SourceTotalHeaderSize = Summary.TotalHeaderSize;

	// Ensure the sections are in the expected order.
	for (int32 SectionIdx = 1; SectionIdx < UE_ARRAY_COUNT(SourceSections); ++SectionIdx)
	{
		const FSectionData& SourceSection = SourceSections[SectionIdx];
		const FSectionData& PrevSection = SourceSections[SectionIdx - 1];

		// Verify sections are ordered as expected
		if (SourceSection.Offset < 0 || (SourceSection.bRequired && (SourceSection.Offset < PrevSection.Offset)))
		{
			UE_LOG(LogAssetHeaderPatcher, Error, TEXT("Unexpected section order for %s (%d %zd < %zd) "), *SrcAsset, SectionIdx, SourceSection.Offset, PrevSection.Offset);
			return EResult::ErrorUnexpectedSectionOrder;
		}
	}

	// Ensure the required sections have data
	for (const FSectionData& SourceSection : SourceSections)
	{
		// skip processing empty non required chunks.
		if (SourceSection.bRequired && SourceSection.Size <= 0)
		{
			UE_LOG(LogAssetHeaderPatcher, Error, TEXT("Unexpected section order for %s"), *SrcAsset);
			return EResult::ErrorEmptyRequireSection;
		}
	}

	// Create the destination file if not open already
	if (!DstArchive)
	{
		TUniquePtr<FArchive> FileWriter(IFileManager::Get().CreateFileWriter(*DstAsset, FILEWRITE_EvenIfReadOnly));
		if (!FileWriter)
		{
			UE_LOG(LogAssetHeaderPatcher, Error, TEXT("Failed to open %s for write"), *DstAsset);
			return EResult::ErrorFailedToOpenDestinationFile;
		}
		DstArchiveOwner = MoveTemp(FileWriter);
		DstArchive = DstArchiveOwner.Get();
	}

	FNamePatchingWriter Writer(*DstArchive, NameToIndexMap);

	// set version numbers so components branch correctly
	Writer.SetUEVer(Summary.GetFileVersionUE());
	Writer.SetLicenseeUEVer(Summary.GetFileVersionLicenseeUE());
	Writer.SetEngineVer(Summary.SavedByEngineVersion);
	Writer.SetCustomVersions(Summary.GetCustomVersionContainer());
	if (Summary.GetPackageFlags() & PKG_FilterEditorOnly)
	{
		Writer.SetFilterEditorOnly(true);
	}

	int64 LastSectionEndedAt = 0;

	for (int32 SectionIdx = 0; SectionIdx < UE_ARRAY_COUNT(SourceSections); ++SectionIdx)
	{
		const FSectionData& SourceSection = SourceSections[SectionIdx];

		// skip processing empty non required chunks.
		if (!SourceSection.bRequired && SourceSection.Size <= 0)
		{
			continue;
		}

		// copy the blob from the end of the last section, to the start of this one
		if (LastSectionEndedAt)
		{
			int64 SizeToCopy = SourceSection.Offset - LastSectionEndedAt;
			checkf(SizeToCopy >= 0, TEXT("Section %d of %s\n%zd -> %zd %zd"), SectionIdx, *SrcAsset, SourceSection.Offset, LastSectionEndedAt, SizeToCopy);
			Writer.Serialize(SrcBuffer.GetData() + LastSectionEndedAt, SizeToCopy);
		}
		LastSectionEndedAt = SourceSection.Offset + SourceSection.Size;

		// Serialize the current patched section and patch summary offsets
		switch (SourceSection.Section)
		{
		case EPatchedSection::Summary:
		{
			// We will write the Summary twice.
			// The first is so we get its new size (if the name was changed in patching)
			// The second is done after the loop, to patch up all the offsets.
			check(Writer.Tell() == 0);
			Writer << Summary;
			const int64 SummarySize = Writer.Tell();
			const int64 Delta = SummarySize - SourceSection.Size;
			PatchSummaryOffsets(Summary, 0, Delta);
			Summary.TotalHeaderSize += (int32)Delta;

			break;
		}

		case EPatchedSection::NameTable:
		{
			const int64 NameTableStartOffset = Writer.Tell();
			for (FName& Name : NameTable)
			{
				const FNameEntry* Entry = FName::GetEntry(Name.GetDisplayIndex());
				check(Entry);
				Entry->Write(Writer);
			}
			checkf(!Writer.IsCriticalError(), TEXT("Issue writing %s"), *Writer.GetErrorMessage());

			const int64 NameTableSize = Writer.Tell() - NameTableStartOffset;
			const int64 Delta = NameTableSize - SourceSection.Size;
			PatchSummaryOffsets(Summary, NameTableStartOffset, Delta);
			Summary.TotalHeaderSize += (int32)Delta;
			check(Summary.NameCount == NameTable.Num());
			check(Summary.NameOffset == NameTableStartOffset);

			break;
		}

		case EPatchedSection::SoftPathTable:
		{
			const int64 TableStartOffset = Writer.Tell();
			for (FSoftObjectPath& PathRef : SoftObjectPathTable)
			{
				PathRef.SerializePath(Writer);
			}
			checkf(!Writer.IsCriticalError(), TEXT("Issue writing %s"), *Writer.GetErrorMessage());

			const int64 TableSize = Writer.Tell() - TableStartOffset;
			const int64 Delta = TableSize - SourceSection.Size;
			checkf(Delta == 0, TEXT("Delta should be Zero. is %d"), (int)Delta);
			check(Summary.SoftObjectPathsCount == SoftObjectPathTable.Num());
			check(Summary.SoftObjectPathsOffset == TableStartOffset);

			break;
		}

		case EPatchedSection::GatherableTextDataTable:
		{
			const int64 GatherableTableStartOffset = Writer.Tell();
			for (FGatherableTextData& GatherableTextData : GatherableTextDataTable)
			{
				Writer << GatherableTextData;
			}
			checkf(!Writer.IsCriticalError(), TEXT("Issue writing %s"), *Writer.GetErrorMessage());

			const int64 TableSize = Writer.Tell() - GatherableTableStartOffset;
			const int64 Delta = TableSize - SourceSection.Size;
			PatchSummaryOffsets(Summary, GatherableTableStartOffset, Delta);
			Summary.TotalHeaderSize += (int32)Delta;
			check(Summary.GatherableTextDataCount == GatherableTextDataTable.Num());
			check(Summary.GatherableTextDataOffset == GatherableTableStartOffset);

			break;
		}

		case EPatchedSection::SearchableNamesMap:
		{
			const int64 TableStartOffset = Writer.Tell();
			
			FLinkerTables LinkerTables;
			LinkerTables.SearchableNamesMap = SearchableNamesMap;
			LinkerTables.SerializeSearchableNamesMap(Writer);

			checkf(!Writer.IsCriticalError(), TEXT("Issue writing %s"), *Writer.GetErrorMessage());

			const int64 TableSize = Writer.Tell() - TableStartOffset;
			const int64 Delta = TableSize - SourceSection.Size;
			checkf(Delta == 0, TEXT("Delta should be Zero. is %d"), (int)Delta);
			check(Summary.SearchableNamesOffset == TableStartOffset);

			break;
		}

		case EPatchedSection::ImportTable:
		{
			const int64 ImportTableStartOffset = Writer.Tell();
			for (FObjectImport& Import : ImportTable)
			{
				Writer << Import;
			}
			checkf(!Writer.IsCriticalError(), TEXT("Issue writing %s"), *Writer.GetErrorMessage());

			const int64 ImportTableSize = Writer.Tell() - ImportTableStartOffset;
			const int64 Delta = ImportTableSize - SourceSection.Size;
			check(Delta == 0);
			checkf(ImportTableSize == SourceSection.Size, TEXT("%d == %d"), (int)ImportTableSize, (int)SourceSection.Size); // We only patch export table offsets, we should not be patching size
			checkf(Summary.ImportCount == ImportTable.Num(), TEXT("%d == %d"), Summary.ImportCount, ImportTable.Num());
			checkf(Summary.ImportOffset == ImportTableStartOffset, TEXT("%d == %d"), Summary.ImportOffset, ImportTableStartOffset);

			break;
		}

		case EPatchedSection::ExportTable:
		{
			// The export table offsets aren't correct yet.
			// Once we know them, we will seek back and write it a second time.
			const int64 ExportTableStartOffset = Writer.Tell();
			for (FObjectExport& Export : ExportTable)
			{
				Writer << Export;
			}
			checkf(!Writer.IsCriticalError(), TEXT("Issue writing %s"), *Writer.GetErrorMessage());

			const int64 ExportTableSize = Writer.Tell() - ExportTableStartOffset;
			const int64 Delta = ExportTableSize - SourceSection.Size;
			check(Delta == 0);
			checkf(ExportTableSize == SourceSection.Size, TEXT("%d == %d"), (int)ExportTableSize, (int)SourceSection.Size); // We only patch export table offsets, we should not be patching size
			checkf(Summary.ExportCount == ExportTable.Num(), TEXT("%d == %d"), Summary.ExportCount, ExportTable.Num());
			checkf(Summary.ExportOffset == ExportTableStartOffset, TEXT("%d == %d"), Summary.ExportOffset, ExportTableStartOffset);

			break;
		}

		case EPatchedSection::SoftPackageReferencesTable:
		{
			const int64 TableStartOffset = Writer.Tell();
			for (FName& Reference : SoftPackageReferencesTable)
			{
				Writer << Reference;
			}
			checkf(!Writer.IsCriticalError(), TEXT("Issue writing %s"), *Writer.GetErrorMessage());

			const int64 TableSize = Writer.Tell() - TableStartOffset;
			const int64 Delta = TableSize - SourceSection.Size;
			checkf(Delta == 0, TEXT("Delta should be Zero. is %d"), (int)Delta);
			check(Summary.SoftPackageReferencesCount == SoftPackageReferencesTable.Num());
			check(Summary.SoftPackageReferencesOffset == TableStartOffset);

			break;
		}

		case EPatchedSection::ThumbnailTable:
		{
			const int64 ThumbnailTableStartOffset = Writer.Tell();
			const int64 ThumbnailTableDeltaOffset = ThumbnailTableStartOffset - SourceSection.Offset;
			int32 ThumbnailCount = ThumbnailTable.Num();
			Writer << ThumbnailCount;
			int32 AccumulatedDelta = 0;
			for (FThumbnailEntry& Entry : ThumbnailTable)
			{
				AccumulatedDelta += Entry.Delta;
				Writer << Entry.ObjectShortClassName;
				Writer << Entry.ObjectPathWithoutPackageName;
				Entry.FileOffset += (int32)ThumbnailTableDeltaOffset + AccumulatedDelta;
				Writer << Entry.FileOffset;
			}
			checkf(!Writer.IsCriticalError(), TEXT("Issue writing %s"), *Writer.GetErrorMessage());

			const int64 ThumbnailTableSize = Writer.Tell() - ThumbnailTableStartOffset;
			const int64 Delta = ThumbnailTableSize - SourceSection.Size;
			PatchSummaryOffsets(Summary, ThumbnailTableStartOffset, Delta);
			Summary.TotalHeaderSize += (int32)Delta;
			checkf(ThumbnailTableStartOffset == Summary.ThumbnailTableOffset, TEXT("%zd == %zd"), ThumbnailTableStartOffset, Summary.ThumbnailTableOffset);

			break;
		}

		case EPatchedSection::AssetRegistryData:
		{
			const int64 AssetRegistryDataStartOffset = Writer.Tell();
			checkf(AssetRegistryDataStartOffset == Summary.AssetRegistryDataOffset, TEXT("%zd == %zd"), AssetRegistryDataStartOffset, Summary.AssetRegistryDataOffset);

			// Manually write this back out, there isn't a nicely factored function to call for this
			if (AssetRegistryData.PkgData.DependencyDataOffset != INDEX_NONE)
			{
				Writer << AssetRegistryData.PkgData.DependencyDataOffset;
			}
			Writer << AssetRegistryData.PkgData.ObjectCount;

			check(AssetRegistryData.PkgData.ObjectCount == AssetRegistryData.ObjectData.Num());
			for (FAssetRegistryObjectData& ObjData : AssetRegistryData.ObjectData)
			{
				Writer << ObjData.ObjectData.ObjectPath;
				Writer << ObjData.ObjectData.ObjectClassName;
				Writer << ObjData.ObjectData.TagCount;

				check(ObjData.ObjectData.TagCount == ObjData.TagData.Num());
				for (UE::AssetRegistry::FDeserializeTagData& TagData : ObjData.TagData)
				{
					Writer << TagData.Key;
					Writer << TagData.Value;
				}
			}
			checkf(!Writer.IsCriticalError(), TEXT("Issue writing %s"), *Writer.GetErrorMessage());

			const int64 AssetRegistryDataSize = Writer.Tell() - AssetRegistryDataStartOffset;
			const int64 Delta = AssetRegistryDataSize - SourceSection.Size;
			PatchSummaryOffsets(Summary, AssetRegistryDataStartOffset, Delta);
			Summary.TotalHeaderSize += (int32)Delta;

			if (AssetRegistryData.PkgData.DependencyDataOffset != INDEX_NONE)
			{
				// DependencyDataOffset is not relative but points to just after the rest of the AR data
				// We will seek back and write this later
				const int64 DependencyDataDelta = AssetRegistryDataStartOffset - SourceSection.Offset + Delta;
				AssetRegistryData.PkgData.DependencyDataOffset += DependencyDataDelta;
			}

			break;
		}

		default:
			UE_LOG(LogAssetHeaderPatcher, Error, TEXT("Unexpected section for %s"), *SrcAsset);
			return EResult::ErrorUnkownSection;
		}

		if (Writer.IsError())
		{
			UE_LOG(LogAssetHeaderPatcher, Error, TEXT("Failed to write to %s"), *DstAsset);
			return EResult::ErrorFailedToWriteToDestinationFile;
		}
	}

	{   // copy the last blob
		int64 SizeToCopy = SrcBuffer.Num() - LastSectionEndedAt;
		checkf(SizeToCopy >= 0, TEXT("Section last of %s\n%zd -> %zd %zd"), *SrcAsset, SrcBuffer.Num(), LastSectionEndedAt, SizeToCopy);
		Writer.Serialize(SrcBuffer.GetData() + LastSectionEndedAt, SizeToCopy);
	}

	if (Writer.IsError())
	{
		UE_LOG(LogAssetHeaderPatcher, Error, TEXT("Failed to write to %s"), *DstAsset);
		return EResult::ErrorFailedToWriteToDestinationFile;
	}

	// Re-write summary with patched offsets
	Writer.Seek(0);
	Writer << Summary;

	{
		// Re-write export table with patched offsets
		// Patch Export table offsets now that we have patched all the header sections
		Writer.Seek(Summary.ExportOffset);
		const int64 ExportOffsetDelta = static_cast<int64>(Summary.TotalHeaderSize) - SourceTotalHeaderSize;
		for (FObjectExport& Export : ExportTable)
		{
			Export.SerialOffset += ExportOffsetDelta;
			Writer << Export;
		}
	}

	if (Writer.IsError())
	{
		UE_LOG(LogAssetHeaderPatcher, Error, TEXT("Failed to write to %s"), *DstAsset);
		return EResult::ErrorFailedToWriteToDestinationFile;
	}

	if (AssetRegistryData.PkgData.DependencyDataOffset != INDEX_NONE)
	{
		// Re-write asset registry dependency data offset
		Writer.Seek(Summary.AssetRegistryDataOffset);
		Writer << AssetRegistryData.PkgData.DependencyDataOffset;

		if (Writer.IsError())
		{
			UE_LOG(LogAssetHeaderPatcher, Error, TEXT("Failed to write to %s"), *DstAsset);
			return EResult::ErrorFailedToWriteToDestinationFile;
		}
	}

	return EResult::Success;
}

void FAssetHeaderPatcherInner::DumpState(FStringView OutputDirectory)
{
	TStringBuilder<1024> Builder;
	auto GetDebugFNameString = [this](FName Name)
		{
			int32* Index = NameToIndexMap.Find(Name.GetDisplayIndex());
			if (Index)
			{
				return FString::Printf(TEXT("%s (nametable index: %d, fname:{'%s', %d})"), *Name.ToString(), *Index, *Name.GetPlainNameString(), Name.GetNumber());
			}
			else
			{
				return FString(TEXT("None (nametable index: -1, fname {'None', 0})"));
			}
		};

	Builder.Append(TEXT("{\n"));

	Builder.Append(TEXT("\t\"Summary\":{ "));
	{
		Builder.Append(TEXT("\n\t\t\"PackageName\": \""));
		Builder.Append(Summary.PackageName);
		Builder.Append(TEXT("\""));
	}
	Builder.Append(TEXT("\n\t},\n"));

	Builder.Append(TEXT("\t\"NameTable\":[ "));
	for (const FName& Name : NameTable)
	{
		Builder.Append(TEXT("\n\t\t\""));
		Builder.Append(GetDebugFNameString(Name));
		Builder.Append(TEXT("\","));
	}
	Builder.RemoveSuffix(1);
	Builder.Append(TEXT("\n\t],\n"));

	Builder.Append(TEXT("\t\"ExportTable\":[ "));
	for (const auto& Export : ExportTable)
	{
		Builder.Append(TEXT("\n\t\t{\n"));

		Builder.Append(TEXT("\t\t\t\"ObjectName\": \""));
		Builder.Append(GetDebugFNameString(Export.ObjectName));
		Builder.Append(TEXT("\""));

#if WITH_EDITORONLY_DATA
		Builder.Append(TEXT(",\n"));
		Builder.Append(TEXT("\t\t\t\"OldClassName\": \""));
		Builder.Append(GetDebugFNameString(Export.OldClassName));
		Builder.Append(TEXT("\""));
#endif
		Builder.Append(TEXT("\n\t\t},"));
	}
	Builder.RemoveSuffix(1);
	Builder.Append(TEXT("\n\t],\n"));

	Builder.Append(TEXT("\t\"ImportTable\":[ "));
	for (const auto& Import : ImportTable)
	{
		Builder.Append(TEXT("\n\t\t{\n"));

		Builder.Append(TEXT("\t\t\t\"ObjectName\": \""));
		Builder.Append(GetDebugFNameString(Import.ObjectName));
		Builder.Append(TEXT("\",\n"));

#if WITH_EDITORONLY_DATA
		Builder.Append(TEXT("\t\t\t\"OldClassName\": \""));
		Builder.Append(GetDebugFNameString(Import.OldClassName));
		Builder.Append(TEXT("\",\n"));
#endif

		Builder.Append(TEXT("\t\t\t\"ClassPackage\": \""));
		Builder.Append(GetDebugFNameString(Import.ClassPackage));
		Builder.Append(TEXT("\",\n"));

		Builder.Append(TEXT("\t\t\t\"ClassName\": \""));
		Builder.Append(GetDebugFNameString(Import.ClassName));
		Builder.Append(TEXT("\""));

#if WITH_EDITORONLY_DATA
		Builder.Append(TEXT(",\n\t\t\t\"PackageName\": \""));
		Builder.Append(GetDebugFNameString(Import.PackageName));
		Builder.Append(TEXT("\""));
#endif
		Builder.Append(TEXT("\n\t\t},"));

	}
	Builder.RemoveSuffix(1);
	Builder.Append(TEXT("\n\t],\n"));

	Builder.Append(TEXT("\t\"SoftObjectPathTable\":[ "));
	for (const FSoftObjectPath& SoftObjectPath : SoftObjectPathTable)
	{
		Builder.Append(TEXT("\n\t\t{\n"));

		FTopLevelAssetPath TLAP = SoftObjectPath.GetAssetPath();
		FString Subpath = SoftObjectPath.GetSubPathString();

		Builder.Append(TEXT("\t\t\t\"AssetPath\": {\n\""));
		Builder.Append(TEXT("\t\t\t\t\"PackageName\": \""));
		Builder.Append(GetDebugFNameString(TLAP.GetPackageName()));
		Builder.Append(TEXT("\",\n"));
		Builder.Append(TEXT("\t\t\t\t\"AssetName\": \""));
		Builder.Append(GetDebugFNameString(TLAP.GetAssetName()));
		Builder.Append(TEXT("\"\n"));
		Builder.Append(TEXT("\t\t\t},\n"));

		Builder.Append(TEXT("\t\t\t\"Subpath (string)\": \""));
		Builder.Append(Subpath);
		Builder.Append(TEXT("\""));

		Builder.Append(TEXT("\n\t\t},"));

	}
	Builder.RemoveSuffix(1);
	Builder.Append(TEXT("\n\t],\n"));

	Builder.Append(TEXT("\t\"SoftPackageReferencesTable\":[ "));
	for (const FName SoftPackageRef : SoftPackageReferencesTable)
	{
		Builder.Append(TEXT("\n\t\t\""));
		Builder.Append(GetDebugFNameString(SoftPackageRef));
		Builder.Append(TEXT("\","));
	}
	Builder.RemoveSuffix(1);
	Builder.Append(TEXT("\n\t],\n"));

	Builder.Append(TEXT("\t\"GatherableTextDataTable\":[ "));
	for (const FGatherableTextData& GatherableTextData : GatherableTextDataTable)
	{
		Builder.Append(TEXT("\n\t\t{\n"));
		Builder.Append(TEXT("\t\t\t\"SourceSiteContexts.SiteDescription (string)\": ["));
		for (auto& SiteContext : GatherableTextData.SourceSiteContexts)
		{
			Builder.Append(TEXT("\n\t\t\t\t\""));
			Builder.Append(SiteContext.SiteDescription);
			Builder.Append(TEXT("\","));
		}
		Builder.RemoveSuffix(1);
		Builder.Append(TEXT("\n\t\t\t]"));
		Builder.Append(TEXT("\n\t\t},"));
	}
	Builder.RemoveSuffix(1);
	Builder.Append(TEXT("\n\t],\n"));

	Builder.Append(TEXT("\t\"ThumbnailTable\":[ "));
	for (const FThumbnailEntry& ThumbnailEntry : ThumbnailTable)
	{
		Builder.Append(TEXT("\n\t\t{\n"));

		Builder.Append(TEXT("\t\t\t\"ObjectPathWithoutPackageName (string)\": \""));
		Builder.Append(ThumbnailEntry.ObjectPathWithoutPackageName);
		Builder.Append(TEXT("\",\n"));

		Builder.Append(TEXT("\t\t\t\"ObjectShortClassName (string)\": \""));
		Builder.Append(ThumbnailEntry.ObjectShortClassName);
		Builder.Append(TEXT("\""));

		Builder.Append(TEXT("\n\t\t},"));
	}
	Builder.RemoveSuffix(1);
	Builder.Append(TEXT("\n\t],\n"));

	Builder.Append(TEXT("\t\"AssetRegistryData\":[ "));
	for (const FAssetRegistryObjectData& ObjData : AssetRegistryData.ObjectData)
	{
		Builder.Append(TEXT("\n\t\t{\n"));

		Builder.Append(TEXT("\t\t\t\"ObjectData\": {\n"));
		Builder.Append(TEXT("\t\t\t\t\"ObjectPath (string)\": \""));
		Builder.Append(ObjData.ObjectData.ObjectPath);
		Builder.Append(TEXT("\",\n"));
		Builder.Append(TEXT("\t\t\t\t\"ObjectClassName (string)\": \""));
		Builder.Append(ObjData.ObjectData.ObjectClassName);
		Builder.Append(TEXT("\"\n"));
		Builder.Append(TEXT("\t\t\t},\n"));

		Builder.Append(TEXT("\t\t\t\"TagData\": [\n"));
		for (const auto& TagData : ObjData.TagData)
		{
			FString Value = TagData.Value;
			bool bNeedDecode = TagData.Key == FWorldPartitionActorDescUtils::ActorMetaDataTagName();
			if (bNeedDecode)
			{
				const FString LongPackageName(SrcAsset);
				const FString ObjectPath(ObjData.ObjectData.ObjectPath);
				const FTopLevelAssetPath AssetClassPathName(ObjData.ObjectData.ObjectClassName);
				const FAssetDataTagMap Tags(MakeTagMap(ObjData.TagData));
				const FAssetData AssetData(LongPackageName, ObjectPath, AssetClassPathName, Tags);

				struct FWorldPartitionAssetDataPrinter : FWorldPartitionAssetDataPatcher
				{
					FWorldPartitionAssetDataPrinter(int32 InIndentDepth)
						: IndentDepth(InIndentDepth) 
					{
					}

					virtual bool DoPatch(FString& InOutString) override
					{
						Builder.Append(TEXT("\n"));
						Indent();
						Builder.Append(TEXT("string=\""));
						Builder.Append(InOutString);
						Builder.Append(TEXT("\""));
						return false;
					}
					virtual bool DoPatch(FName& InOutName) override
					{
						Builder.Append(TEXT("\n"));
						Indent();
						Builder.Append(TEXT("FName=\""));
						Builder.Append(InOutName.ToString());
						Builder.Append(TEXT("\""));
						return false;
					}
					virtual bool DoPatch(FSoftObjectPath& InOutSoft) override
					{
						Builder.Append(TEXT("\n"));
						Indent();
						Builder.Append(TEXT("FSoftObjectPath="));
						FTopLevelAssetPath TLAP = InOutSoft.GetAssetPath();
						Builder.Append(TEXT("{{PackageName=\""));
						Builder.Append(TLAP.GetPackageName().ToString());
						Builder.Append(TEXT("\", AssetName=\""));
						Builder.Append(TLAP.GetAssetName().ToString());
						Builder.Append(TEXT("\"}, SubPath (string)=\""));
						Builder.Append(InOutSoft.GetSubPathString());
						Builder.Append(TEXT("\"}"));
						return false;
					}
					virtual bool DoPatch(FTopLevelAssetPath& InOutPath) override
					{
						Builder.Append(TEXT("\n"));
						Indent();
						Builder.Append(TEXT("FTopLevelAssetPath="));
						Builder.Append(TEXT("{PackageName=\""));
						Builder.Append(InOutPath.GetPackageName().ToString());
						Builder.Append(TEXT("\", AssetName=\""));
						Builder.Append(InOutPath.GetAssetName().ToString());
						Builder.Append(TEXT("\"}"));
						return false;
					}
					void Indent()
					{
						for (int32 i = 0; i < IndentDepth; ++i)
						{
							Builder.Append(TEXT("\t"));
						}
					}
					const TCHAR* ToString() const
					{
						return Builder.ToString();
					}
					int32 IndentDepth;
					TStringBuilder<1024> Builder;
				};

				FString PatchedAssetData;
				FWorldPartitionAssetDataPrinter Patcher(5);
				FWorldPartitionActorDescUtils::GetPatchedAssetDataFromAssetData(AssetData, PatchedAssetData, &Patcher);
				Value = Patcher.ToString();
			}

			Builder.Append(TEXT("\n\t\t\t\t{\n"));

			Builder.Append(TEXT("\t\t\t\t\t\"Key (string)\": \""));
			Builder.Append(TagData.Key);
			Builder.Append(TEXT("\",\n"));
			Builder.Append(TEXT("\t\t\t\t\t\"Value"));
			if (bNeedDecode)
			{
				Builder.Append(TEXT(" (decoded string)"));
			}
			else
			{
				Builder.Append(TEXT("(string)"));
			}
			Builder.Append(TEXT("\": \""));
			Builder.Append(Value);
			Builder.Append(TEXT("\"\n"));

			Builder.Append(TEXT("\t\t\t\t},"));
		}
		Builder.RemoveSuffix(1);
		Builder.Append(TEXT("\n\t\t\t]\n"));

		Builder.Append(TEXT("\n\t\t},"));
	}
	Builder.RemoveSuffix(1);
	Builder.Append(TEXT("\n\t]\n"));

	Builder.Append(TEXT("}"));


	// Write to disk
	TStringBuilder<256> OutPath;
	OutPath.Append(OutputDirectory);
	FString SubPath = SrcAsset;
	FPaths::CollapseRelativeDirectories(SubPath);
	if (SubPath.StartsWith(TEXT("../")))
	{
		int32 Pos = SubPath.Find(TEXT("../"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (Pos >= 0)
		{
			SubPath.RightChopInline(Pos + 3);
		}
	}
	else if (SubPath.Len() > 2 && SubPath[1] == TEXT(':'))
	{
		SubPath.RightChopInline(2); // Drop the drive
	}
	OutPath = FPaths::Combine(OutPath, SubPath);
	OutPath.Append(TEXT(".txt"));
	FFileHelper::SaveStringToFile(Builder.ToString(), *OutPath);
}

///////////////////////////////////////////////////////////////////////////

#if WITH_TESTS

#include "Tests/TestHarnessAdapter.h"

TEST_CASE_NAMED(FAssetHeaderPatcherTests, "AssetHeaderPatcher", "[AssetHeaderPatcher][EngineFilter]")
{
// Useful when iterating so you halt if something fails while a debugger is attached
#if 0
#undef CHECK
#undef CHECK_EQUALS
#undef CHECK_NOT_EQUALS
#define CHECK(...) if (!(__VA_ARGS__)) { UE_DEBUG_BREAK(); FAutomationTestFramework::Get().GetCurrentTest()->AddError(TEXT("Condition failed")); }
#define CHECK_EQUALS(What, X, Y) if(!FAutomationTestFramework::Get().GetCurrentTest()->TestEqual(What, X, Y)) { UE_DEBUG_BREAK(); };
#define CHECK_NOT_EQUALS(What, X, Y) if(!FAutomationTestFramework::Get().GetCurrentTest()->TestNotEqual(What, X, Y)) { UE_DEBUG_BREAK(); };
#endif

	struct FTestPatcherContext : FAssetHeaderPatcher::FContext
	{
		FTestPatcherContext(TMap<FString, FString> PackageRenameMap, bool bGatherDependentPackages = true) : FAssetHeaderPatcher::FContext(PackageRenameMap, bGatherDependentPackages) {}
		const TMap<FString, FString>& GetStringReplacements()
		{
			return StringReplacements;
		}

		void GenerateRemappings()
		{
			GenerateAdditionalRemappings();
		}

		const TArray<FCoreRedirect>& GetRedirects()
		{
			return Redirects;
		}

		const TArray<FString>& GetVerseMountPoints()
		{
			return VerseMountPoints;
		}
	};

	// To avoid having to deal with serialization, we mock some data and inject it directly
	// into the patcher as if done via serialization
	const FString DummySrcDstAsset			= TEXT("/SrcMount/SomePath/SrcPackage");
	const TCHAR*  SrcPackagePath			= TEXT("/SrcMount/SomePath/SrcPackage");
	const TCHAR*  DstPackagePath			= TEXT("/DstMount/SomePath/DstPackage");
	const TCHAR*  SrcPackageObjectPath		= TEXT("/SrcMount/SomePath/SrcPackage.SrcPackage");
	const TCHAR*  DstPackageObjectPath		= TEXT("/DstMount/SomePath/DstPackage.DstPackage");
	const TCHAR*  SrcMountName				= TEXT("/SrcSpecialMount/");
	const TCHAR*  DstMountName				= TEXT("/DstSpecialMount/");
	const FName   SrcPackagePathFName(SrcPackagePath);
	const FName   DstPackagePathFName(DstPackagePath);
	const FName   SrcAssetFName(TEXT("SrcPackage"));
	const FName   DstAssetFName(TEXT("DstPackage"));
	const FName   SrcExportObjectFName = SrcAssetFName;
	const FName   DstExportObjectFName = DstAssetFName;
	const FName   DummyImportPackagePathFName(TEXT("/DummyMount/DummyPackage"));


	TMap<FString, FString> MountPointReplacementMap =
	{
		{ SrcMountName, DstMountName },
	};

	TMap<FString, FString> PackageRenameMap =
	{
		{ SrcPackagePath, DstPackagePath },
	};

	FTestPatcherContext Context(PackageRenameMap, false /*bGatherDependentPackages*/);
	const TMap<FString, FString>& StringReplacements = Context.GetStringReplacements();
	CHECK(StringReplacements.Num() > PackageRenameMap.Num()); // Ensure we generated more mappings off of the PackageRenameMap
	CHECK(FCoreRedirects::AddRedirectList(Context.GetRedirects(), TEXT("Asset Header Patcher Tests")));

	FAssetHeaderPatcherInner Patcher(DummySrcDstAsset, DummySrcDstAsset, StringReplacements, MountPointReplacementMap);

	int32 OriginalNameTableCount = 0;
	auto ResetPatcher = [&Patcher, &OriginalNameTableCount, SrcPackagePathFName, SrcAssetFName, SrcExportObjectFName, DummyImportPackagePathFName]()
		{
			// Reset NameTable
			Patcher.NameTable.Empty();
			Patcher.NameToIndexMap.Empty();
			Patcher.RenameMap.Empty();
			Patcher.AddedNames.Empty();
			Patcher.ExportTable.Empty();

			// Repopulate it with our test data normally set through deserialization
			// NameTable
			Patcher.NameToIndexMap.Add(SrcPackagePathFName.GetDisplayIndex(), Patcher.NameTable.Num());
			Patcher.NameTable.Add(SrcPackagePathFName);
			Patcher.NameToIndexMap.Add(SrcAssetFName.GetDisplayIndex(), Patcher.NameTable.Num());
			Patcher.NameTable.Add(SrcAssetFName);
			Patcher.NameToIndexMap.Add(FName(NAME_None).GetDisplayIndex(), Patcher.NameTable.Num());
			Patcher.NameTable.Add(FName(NAME_None));
			Patcher.NameToIndexMap.Add(FName(DummyImportPackagePathFName).GetDisplayIndex(), Patcher.NameTable.Num());
			Patcher.NameTable.Add(FName(DummyImportPackagePathFName));

			FObjectImport DummyImport;
			DummyImport.ObjectName = Patcher.NameTable[Patcher.NameToIndexMap[DummyImportPackagePathFName.GetDisplayIndex()]];
			DummyImport.OldClassName = DummyImportPackagePathFName; // Set to something other than NAME_None
			DummyImport.OuterIndex = FPackageIndex();
			Patcher.ImportTable.Add(DummyImport);

			// Export Table
			FObjectExport SrcPackageExport;
			SrcPackageExport.ObjectName = Patcher.NameTable[Patcher.NameToIndexMap[SrcExportObjectFName.GetDisplayIndex()]];
			SrcPackageExport.OldClassName = SrcExportObjectFName; // Set to something other than NAME_None
			SrcPackageExport.OuterIndex = FPackageIndex(); // This package is the outer
			Patcher.ExportTable.Add(SrcPackageExport);

			FObjectExport DummyExport;
			DummyExport.ObjectName = Patcher.NameTable[Patcher.NameToIndexMap[SrcExportObjectFName.GetDisplayIndex()]]; // Same name as SrcPackageExport
			DummyExport.OldClassName = SrcExportObjectFName; // Set to something other than NAME_None
			DummyExport.OuterIndex = FPackageIndex::FromImport(0); // DummyImport is our outer
			Patcher.ExportTable.Add(DummyExport);

			// Summary
			Patcher.Summary.NameCount = Patcher.NameTable.Num();
			Patcher.OriginalPackagePath = SrcPackagePathFName;
			OriginalNameTableCount = Patcher.NameTable.Num();
		};

	SECTION("FContext Additional Remappings")
	{
		{
			FString Actual(TEXT(R"(/SrcMount/SomePath/SrcPackage)"));
			const FString Expected(TEXT(R"(/DstMount/SomePath/DstPackage)"));
			CHECK(Patcher.DoPatch(Actual));
			CHECK_EQUALS(TEXT("Patch string with direct match"), Actual, Expected);
		}

		{
			FString Actual(TEXT(R"(/SrcMount/SomePath/SrcPackage.SrcPackage)"));
			const FString Expected(TEXT(R"(/DstMount/SomePath/DstPackage.DstPackage)"));
			CHECK(Patcher.DoPatch(Actual));
			CHECK_EQUALS(TEXT("Generated Top-Level Asset mapping"), Actual, Expected);
		}

		{
			FString Actual(TEXT(R"(/SrcMount/SomePath/SrcPackage.SrcPackage_C)"));
			const FString Expected(TEXT(R"(/DstMount/SomePath/DstPackage.DstPackage_C)"));
			CHECK(Patcher.DoPatch(Actual));
			CHECK_EQUALS(TEXT("Generated Blueprint Generated Class mapping"), Actual, Expected);
		}

		{
			FString Actual(TEXT(R"(/SrcMount/SomePath/SrcPackage.Default__SrcPackage_C)"));
			const FString Expected(TEXT(R"(/DstMount/SomePath/DstPackage.Default__DstPackage_C)"));
			CHECK(Patcher.DoPatch(Actual));
			CHECK_EQUALS(TEXT("Generated Blueprint Generated Class Default Object mapping"), Actual, Expected);
		}

		{
			FString Actual(TEXT(R"(/SrcMount/SomePath/SrcPackage.SrcPackageEditorOnlyData)"));
			const FString Expected(TEXT(R"(/DstMount/SomePath/DstPackage.DstPackageEditorOnlyData)"));
			CHECK(Patcher.DoPatch(Actual));
			CHECK_EQUALS(TEXT("Generated MaterialFunctionInterface Editor Only Data mapping"), Actual, Expected);
		}

		SECTION("Verse Mountpoints")
		{
			for (const FString& VerseMount : Context.GetVerseMountPoints())
			{
				// We only generate verse paths for objects, so this package path will not have a mapping
				{
					FString Actual = FString::Printf(TEXT(R"(/%s/SrcMount/SomePath/SrcPackage)"), *VerseMount);
					const FString Expected = FString::Printf(TEXT(R"(/%s/DstMount/SomePath/DstPackage)"), *VerseMount);
					CHECK(!Patcher.DoPatch(Actual));
					CHECK_NOT_EQUALS(TEXT("Patch string with direct match"), Actual, Expected);
				}

				{
					FString Actual = FString::Printf(TEXT(R"(/%s/SrcMount/SomePath/SrcPackage/SrcPackage)"), *VerseMount);
					const FString Expected = FString::Printf(TEXT(R"(/%s/DstMount/SomePath/DstPackage/DstPackage)"), *VerseMount);
					CHECK(Patcher.DoPatch(Actual));
					CHECK_EQUALS(TEXT("Patch string with direct match"), Actual, Expected);
				}

				{
					FString Actual = FString::Printf(TEXT(R"(/%s/SrcMount/SomePath/SrcPackage/SrcPackage)"), *VerseMount);
					const FString Expected = FString::Printf(TEXT(R"(/%s/DstMount/SomePath/DstPackage/DstPackage)"), *VerseMount);
					CHECK(Patcher.DoPatch(Actual));
					CHECK_EQUALS(TEXT("Generated Top-Level Asset mapping"), Actual, Expected);
				}

				{
					FString Actual = FString::Printf(TEXT(R"(/%s/SrcMount/SomePath/SrcPackage/SrcPackage_C)"), *VerseMount);
					const FString Expected = FString::Printf(TEXT(R"(/%s/DstMount/SomePath/DstPackage/DstPackage_C)"), *VerseMount);
					CHECK(Patcher.DoPatch(Actual));
					CHECK_EQUALS(TEXT("Generated Blueprint Generated Class mapping"), Actual, Expected);
				}

				{
					FString Actual = FString::Printf(TEXT(R"(/%s/SrcMount/SomePath/SrcPackage/Default__SrcPackage_C)"), *VerseMount);
					const FString Expected = FString::Printf(TEXT(R"(/%s/DstMount/SomePath/DstPackage/Default__DstPackage_C)"), *VerseMount);
					CHECK(Patcher.DoPatch(Actual));
					CHECK_EQUALS(TEXT("Generated Blueprint Generated Class Default Object mapping"), Actual, Expected);
				}

				{
					FString Actual = FString::Printf(TEXT(R"(/%s/SrcMount/SomePath/SrcPackage/SrcPackageEditorOnlyData)"), *VerseMount);
					const FString Expected = FString::Printf(TEXT(R"(/%s/DstMount/SomePath/DstPackage/DstPackageEditorOnlyData)"), *VerseMount);
					CHECK(Patcher.DoPatch(Actual));
					CHECK_EQUALS(TEXT("Generated MaterialFunctionInterface Editor Only Data mapping"), Actual, Expected);
				}
			}
		}
	}

	SECTION("DoPatch(FString)")
	{
		SECTION("Direct match")
		{
			{
				FString Actual(TEXT(R"(/SrcMount/SomePath/SrcPackage)"));
				const FString Expected(TEXT(R"(/DstMount/SomePath/DstPackage)"));
				CHECK(Patcher.DoPatch(Actual));
				CHECK_EQUALS(TEXT("Patch string with direct match"), Actual, Expected);
			}

			{
				FString Actual(TEXT(R"(/SrcMount/SomePath/SrcPackage2)"));
				const FString Expected = Actual; // Must be a copy
				CHECK(!Patcher.DoPatch(Actual));
				CHECK_EQUALS(TEXT("Patch string with no direct match"), Actual, Expected);
			}
		}

		SECTION("Sub-Object Paths")
		{
			{
				FString Actual(TEXT(R"(/SrcMount/SomePath/SrcPackage.SrcPackage:AnOuter.To.A.SubObject)"));
				const FString Expected(TEXT(R"(/DstMount/SomePath/DstPackage.DstPackage:AnOuter.To.A.SubObject)"));
				CHECK(Patcher.DoPatch(Actual));
				CHECK_EQUALS(TEXT("Patch sub-object path"), Actual, Expected);
			}

			// Worth adding support for in the future, but at the moment we cannot patch various parts of unquoted 
			// sub-object paths (that are specifically strings in the header, FNames are fine). In this case we 
			// can't patch the package path because the top-level asset (UnmappedObject) has no mapping for patching
			{
				FString Actual(TEXT(R"(/SrcMount/SomePath/SrcPackage.UnmappedObject:AnOuter.To.A.SubObject)"));
				const FString Expected(TEXT(R"(/DstMount/SomePath/DstPackage.UnmappedObject:AnOuter.To.A.SubObject)"));
				CHECK(!Patcher.DoPatch(Actual));
				CHECK_NOT_EQUALS(TEXT("Can't patch sub-object paths, for "), Actual, Expected);
			}
		}

		SECTION("Quoted match")
		{
			SECTION("Single Quote")
			{
				{
					FString Actual(TEXT(R"('/SrcMount/SomePath/SrcPackage')"));
					const FString Expected(TEXT(R"('/DstMount/SomePath/DstPackage')"));
					CHECK(Patcher.DoPatch(Actual));
					CHECK_EQUALS(TEXT("Patch package path with quotes"), Actual, Expected);
				}

				{
					FString Actual(TEXT(R"('/SrcMount/SomePath/SrcPackage.SrcPackage')"));
					const FString Expected(TEXT(R"('/DstMount/SomePath/DstPackage.DstPackage')"));
					CHECK(Patcher.DoPatch(Actual));
					CHECK_EQUALS(TEXT("Patch object path with quotes"), Actual, Expected);
				}

				{
					FString Actual(TEXT(R"('/SrcMount/SomePath/SrcPackage.SrcPackage_C')"));
					const FString Expected(TEXT(R"('/DstMount/SomePath/DstPackage.DstPackage_C')"));
					CHECK(Patcher.DoPatch(Actual));
					CHECK_EQUALS(TEXT("Patch blueprint generated class with quotes"), Actual, Expected);
				}

				{
					FString Actual(TEXT(R"('/SrcMount/SomePath/SrcPackage.Default__SrcPackage_C')"));
					const FString Expected(TEXT(R"('/DstMount/SomePath/DstPackage.Default__DstPackage_C')"));
					CHECK(Patcher.DoPatch(Actual));
					CHECK_EQUALS(TEXT("Patch default blueprint generated class object path with quotes"), Actual, Expected);
				}
			}

			SECTION("Double Quote")
			{
				{
					FString Actual(TEXT(R"("/SrcMount/SomePath/SrcPackage")"));
					const FString Expected(TEXT(R"("/DstMount/SomePath/DstPackage")"));
					CHECK(Patcher.DoPatch(Actual));
					CHECK_EQUALS(TEXT("Patch package path with quotes"), Actual, Expected);
				}

				{
					FString Actual(TEXT(R"("/SrcMount/SomePath/SrcPackage.SrcPackage")"));
					const FString Expected(TEXT(R"("/DstMount/SomePath/DstPackage.DstPackage")"));
					CHECK(Patcher.DoPatch(Actual));
					CHECK_EQUALS(TEXT("Patch object path with quotes"), Actual, Expected);
				}

				{
					FString Actual(TEXT(R"("/SrcMount/SomePath/SrcPackage.SrcPackage_C")"));
					const FString Expected(TEXT(R"("/DstMount/SomePath/DstPackage.DstPackage_C")"));
					CHECK(Patcher.DoPatch(Actual));
					CHECK_EQUALS(TEXT("Patch blueprint generated class with quotes"), Actual, Expected);
				}

				{
					FString Actual(TEXT(R"("/SrcMount/SomePath/SrcPackage.Default__SrcPackage_C")"));
					const FString Expected(TEXT(R"("/DstMount/SomePath/DstPackage.Default__DstPackage_C")"));
					CHECK(Patcher.DoPatch(Actual));
					CHECK_EQUALS(TEXT("Patch default blueprint generated class object path with quotes"), Actual, Expected);
				}
			}

			SECTION("Substring match")
			{
				{
					FString Actual(
						TEXT(R"(((ReferenceNodePath="/SrcMount/SomePath/SrcPackage.SrcPackage:RigVMModel.Setup Arm",)")
						TEXT(R"(((Package="/SrcMount/SomePath/SrcPackage",)")
						TEXT(R"(HostObject="/SrcMount/SomePath/SrcPackage.SrcPackage_C")))"));
					FString Expected(
						TEXT(R"(((ReferenceNodePath="/DstMount/SomePath/DstPackage.DstPackage:RigVMModel.Setup Arm",)")
						TEXT(R"(((Package="/DstMount/SomePath/DstPackage",)")
						TEXT(R"(HostObject="/DstMount/SomePath/DstPackage.DstPackage_C")))"));
					CHECK(Patcher.DoPatch(Actual));
					CHECK_EQUALS(TEXT("Patch substring with quoted package, object and sub-object paths"), Actual, Expected);
				}
			}
		}

		SECTION("Mountpoint match")
		{
			/*
				We currently don't support mount point replacement _for strings_ that don't
				provide some kind of delimiter for us to scan for. As such package paths
				and top-level asset paths are not supported unless they are quoted. Sub-object
				paths are supported.
			*/
			/*
			{
				FString Actual(TEXT(R"(/SrcSpecialMount/SomePath/SomePackage)"));
				const FString Expected(TEXT(R"(/DstSpecialMount/SomePath/SomePackage)"));
				CHECK(Patcher.DoPatch(Actual));
				CHECK_EQUALS(TEXT("Patch package path replaces only mount"), Actual, Expected);
			}

			{
				FString Actual(TEXT(R"(/SrcSpecialMount/SomePath/SomePackage.SomePackage)"));
				const FString Expected(TEXT(R"(/DstSpecialMount/SomePath/SomePackage.SomePackage)"));
				CHECK(Patcher.DoPatch(Actual));
				CHECK_EQUALS(TEXT("Patch package object path replaces only mount"), Actual, Expected);
			}
			*/

			{
				FString Actual(TEXT(R"(/SrcSpecialMount/SomePath/SomePackage.TopLevel:SubObject1.SubObject2)"));
				const FString Expected(TEXT(R"(/DstSpecialMount/SomePath/SomePackage.TopLevel:SubObject1.SubObject2)"));
				CHECK(Patcher.DoPatch(Actual));
				CHECK_EQUALS(TEXT("Patch package sub-object path replaces only mount"), Actual, Expected);
			}

			{
				FString Actual(TEXT(R"("/SrcSpecialMount/SomePath/SomePackage")"));
				const FString Expected(TEXT(R"("/DstSpecialMount/SomePath/SomePackage")"));
				CHECK(Patcher.DoPatch(Actual));
				CHECK_EQUALS(TEXT("Patch double quoted path replaces only mount"), Actual, Expected);
			}

			{
				FString Actual(TEXT(R"('/SrcSpecialMount/SomePath/SomePackage')"));
				const FString Expected(TEXT(R"('/DstSpecialMount/SomePath/SomePackage')"));
				CHECK(Patcher.DoPatch(Actual));
				CHECK_EQUALS(TEXT("Patch single quoted path replaces only mount"), Actual, Expected);
			}

			{
				FString Actual(TEXT(R"(SomePrefix="/SrcSpecialMount/SomePath/SomePackage")"));
				const FString Expected(TEXT(R"(SomePrefix="/DstSpecialMount/SomePath/SomePackage")"));
				CHECK(Patcher.DoPatch(Actual));
				CHECK_EQUALS(TEXT("Substring patch replaces only mount when double quoted"), Actual, Expected);
			}

			{
				FString Actual(TEXT(R"(SomePrefix='/SrcSpecialMount/SomePath/SomePackage')"));
				const FString Expected(TEXT(R"(SomePrefix='/DstSpecialMount/SomePath/SomePackage')"));
				CHECK(Patcher.DoPatch(Actual));
				CHECK_EQUALS(TEXT("Substring patch replaces only mount when single quoted"), Actual, Expected);
			}
		}
	}

	SECTION("DoPatch(FSoftObjectPath")
	{
		{
			ResetPatcher();

			FSoftObjectPath Actual(TEXT("/SrcMount/SomePath/SrcPackage.SrcPackage"));
			FSoftObjectPath Expected(TEXT("/DstMount/SomePath/DstPackage.DstPackage"));
			CHECK(Patcher.DoPatch(Actual));
			CHECK_EQUALS(TEXT("SoftObjectPath patching"), Actual, Expected);
			CHECK_EQUALS(TEXT("SoftObject patching doesn't implicitly update the NameTable"), Patcher.NameTable[0], SrcPackagePathFName);
			CHECK_EQUALS(TEXT("SoftObject patching doesn't implicitly update the PackageFileSummary"), Patcher.Summary.NameCount, OriginalNameTableCount);
			Patcher.PatchNameTable();
			CHECK_EQUALS(TEXT("SoftObject patching updates NameTable entry"), Patcher.NameTable[0], DstPackagePathFName);
			CHECK_EQUALS(TEXT("SoftObject patching doesn't implicitly update the PackageFileSummary"), Patcher.Summary.NameCount, OriginalNameTableCount);
		}

		{
			ResetPatcher();

			FSoftObjectPath Actual(TEXT("/SrcMount/SomePath/SrcPackage.SrcPackage:Some.SrcPackage.Subobject"));
			// Note we do not replace the sub-object "SrcPackage" despite it matching the original package and object name
			FSoftObjectPath Expected(TEXT("/DstMount/SomePath/DstPackage.DstPackage:Some.SrcPackage.Subobject"));
			CHECK(Patcher.DoPatch(Actual));
			CHECK_EQUALS(TEXT("SoftObjectPath with sub-object path patching"), Actual, Expected);
			CHECK_EQUALS(TEXT("SoftObject patching doesn't implicitly update the NameTable"), Patcher.NameTable[0], SrcPackagePathFName);
			CHECK_EQUALS(TEXT("SoftObject patching doesn't implicitly update the PackageFileSummary"), Patcher.Summary.NameCount, OriginalNameTableCount);
			Patcher.PatchNameTable();
			CHECK_EQUALS(TEXT("SoftObject patching updates NameTable entry"), Patcher.NameTable[0], DstPackagePathFName);
			CHECK_EQUALS(TEXT("SoftObject patching doesn't implicitly update the PackageFileSummary"), Patcher.Summary.NameCount, OriginalNameTableCount);
		}
	}

	SECTION("DoPatch(FTopLevelAssetPath")
	{
		{
			ResetPatcher();

			FTopLevelAssetPath Actual(SrcPackagePath, SrcAssetFName);
			FTopLevelAssetPath Expected(DstPackagePath, DstAssetFName);
			CHECK(Patcher.DoPatch(Actual));
			CHECK_EQUALS(TEXT("TopLevelAssetPatch(FName,FName) patching"), Actual, Expected);
			CHECK_EQUALS(TEXT("TopLevelAssetPatch(FName,FName) patching doesn't implicitly update the NameTable"), Patcher.NameTable[0], SrcPackagePathFName);
			CHECK_EQUALS(TEXT("TopLevelAssetPatch(FName,FName) patching doesn't implicitly update the PackageFileSummary"), Patcher.Summary.NameCount, OriginalNameTableCount);
			Patcher.PatchNameTable();
			CHECK_EQUALS(TEXT("TopLevelAssetPatch(FName,FName) patching updates NameTable entry"), Patcher.NameTable[0], DstPackagePathFName);
			CHECK_EQUALS(TEXT("TopLevelAssetPatch(FName,FName) patching doesn't implicitly update the PackageFileSummary"), Patcher.Summary.NameCount, OriginalNameTableCount);
		}

		{
			ResetPatcher();

			FTopLevelAssetPath Actual(SrcPackageObjectPath);
			FTopLevelAssetPath Expected(DstPackageObjectPath);
			CHECK(Patcher.DoPatch(Actual));
			CHECK_EQUALS(TEXT("TopLevelAssetPatch(string) patching"), Actual, Expected);
			CHECK_EQUALS(TEXT("TopLevelAssetPatch(string) patching doesn't implicitly update the NameTable"), Patcher.NameTable[0], SrcPackagePathFName);
			CHECK_EQUALS(TEXT("TopLevelAssetPatch(string) patching doesn't implicitly update the PackageFileSummary"), Patcher.Summary.NameCount, OriginalNameTableCount);
			Patcher.PatchNameTable();
			CHECK_EQUALS(TEXT("TopLevelAssetPatch(string) patching updates NameTable entry"), Patcher.NameTable[0], DstPackagePathFName);
			CHECK_EQUALS(TEXT("TopLevelAssetPatch(string) patching doesn't implicitly update the PackageFileSummary"), Patcher.Summary.NameCount, OriginalNameTableCount);
		}
	}

	SECTION("DoPatch(FGatherableTextData")
	{
		{
			ResetPatcher();

			FGatherableTextData Actual;
			Actual.NamespaceName = SrcPackagePath;
			Actual.SourceData.SourceString = SrcPackagePath;
			FTextSourceSiteContext SrcSiteContext;
			SrcSiteContext.KeyName = SrcPackagePath;
			SrcSiteContext.SiteDescription = SrcPackagePath;
			Actual.SourceSiteContexts.Add(SrcSiteContext);

			FGatherableTextData Expected = Actual;
			Expected.SourceSiteContexts = TArray<FTextSourceSiteContext>();
			FTextSourceSiteContext DstSiteContext;
			DstSiteContext.KeyName = SrcPackagePath;
			DstSiteContext.SiteDescription = DstPackagePath;
			Expected.SourceSiteContexts.Add(DstSiteContext);

			CHECK(Patcher.DoPatch(Actual));
			CHECK_EQUALS(TEXT("FGatherableTextData patching doesn't update NamespaceName"), Actual.NamespaceName, Expected.NamespaceName);
			CHECK_EQUALS(TEXT("FGatherableTextData patching doesn't update SourceData.SourceString"), Actual.SourceData.SourceString, Expected.SourceData.SourceString);
			CHECK_EQUALS(TEXT("FGatherableTextData patching doesn't update SourceSiteContexts[].KeyName"), Actual.SourceSiteContexts[0].KeyName, Expected.SourceSiteContexts[0].KeyName);
			CHECK_EQUALS(TEXT("FGatherableTextData patching does update SourceData.SourceString[].SiteDescription"), Actual.SourceSiteContexts[0].SiteDescription, Expected.SourceSiteContexts[0].SiteDescription);
			CHECK_EQUALS(TEXT("FGatherableTextData patching doesn't implicitly update the PackageFileSummary"), Patcher.Summary.NameCount, OriginalNameTableCount);
			CHECK_EQUALS(TEXT("FGatherableTextData patching doesn't implicitly update the NameTable"), Patcher.NameTable[0], SrcPackagePathFName);
			CHECK_EQUALS(TEXT("FGatherableTextData patching doesn't implicitly update the NameTable"), Patcher.NameTable[1], SrcAssetFName);
			Patcher.PatchNameTable();
			// FGatherableTexData doesn't contain FNames so we shouldn't have updated the NameTable at all
			CHECK_EQUALS(TEXT("FGatherableTextData patching doesn't implicitly update the PackageFileSummary"), Patcher.Summary.NameCount, OriginalNameTableCount);
			CHECK_EQUALS(TEXT("FGatherableTextData patching updates NameTable entry"), Patcher.NameTable[0], SrcPackagePathFName);
			CHECK_EQUALS(TEXT("FGatherableTextData patching doesn't implicitly update the NameTable"), Patcher.NameTable[1], SrcAssetFName);
		}
	}

	SECTION("DoPatch(FObjectResource)")
	{
		{
			ResetPatcher();

			CHECK(Patcher.ExportTable.Num() > 0);
			FObjectResource ExportResource = Patcher.ExportTable[0];
			CHECK(ExportResource.OldClassName != NAME_None);

			FName Actual = ExportResource.ObjectName;
			const FName Expected = DstExportObjectFName;
			CHECK(Actual == SrcExportObjectFName);
			CHECK(Patcher.NameTable.Contains(Actual));

			CHECK(Patcher.DoPatch(ExportResource, true, Actual));
			CHECK(ExportResource.OldClassName == NAME_None);
			CHECK(Actual == Expected);
		}

		// We are looking at an Export object that has the same name as an export that we will patch
		// however, in this instance the outers are different. This export should not be patched as a 
		// result since we do not have a remapping for it's full object path
		{
			ResetPatcher();

			CHECK(Patcher.ExportTable.Num() > 1);
			FObjectResource ExportResource = Patcher.ExportTable[1];
			CHECK(ExportResource.OldClassName != NAME_None);
			CHECK(ExportResource.OuterIndex.IsImport());

			FName Actual = ExportResource.ObjectName;
			const FName Expected = Actual;
			CHECK(Actual == SrcExportObjectFName);
			CHECK(Patcher.NameTable.Contains(Actual));

			CHECK(!Patcher.DoPatch(ExportResource, true, Actual));
			CHECK(ExportResource.OldClassName == NAME_None); // We always clear this
			CHECK(Actual == Expected);
		}
	}

	CHECK(FCoreRedirects::RemoveRedirectList(Context.GetRedirects(), TEXT("Asset Header Patcher Tests")));
}

#endif // WITH_TESTS
