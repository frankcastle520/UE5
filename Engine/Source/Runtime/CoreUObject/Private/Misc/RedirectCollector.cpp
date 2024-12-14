// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/RedirectCollector.h"
#include "Algo/Transform.h"
#include "Misc/CoreDelegates.h"
#include "UObject/CoreRedirects.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "UObject/Package.h"
#include "Templates/Casts.h"
#include "UObject/UnrealType.h"
#include "UObject/ObjectRedirector.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/LinkerLoad.h"
#include "UObject/UObjectThreadContext.h"
#include "UObject/SoftObjectPath.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"

#if WITH_EDITOR

DEFINE_LOG_CATEGORY_STATIC(LogRedirectors, Log, All);

FAutoConsoleCommand CVarResolveAllSoftObjects(
	TEXT("RedirectCollector.ResolveAllSoftObjectPaths"),
	TEXT("Attempts to load / resolve all currently referenced Soft Object Paths"),
	FConsoleCommandDelegate::CreateLambda([]
	{
		GRedirectCollector.ResolveAllSoftObjectPaths();
	})
);

void RedirectCollectorDumpAllAssetRedirects();

FAutoConsoleCommand CVarRedirectCollectorDumpAllAssetRedirects(
	TEXT("redirectcollector.DumpAllAssetRedirects"),
	TEXT("Prints all tracked redirectors to the log."),
	FConsoleCommandDelegate::CreateStatic(&RedirectCollectorDumpAllAssetRedirects)
);

void RedirectCollectorDumpAllAssetRedirects()
{
	UE::TDynamicUniqueLock<FCriticalSection> ScopeLock(GRedirectCollector.AcquireLock());
	const TMap<FSoftObjectPath, FSoftObjectPath>& Redirects = GRedirectCollector.GetObjectPathRedirectionMapUnderLock(ScopeLock);
	FString FullyQualifiedFileName = FPaths::ProfilingDir() + FString::Printf(TEXT("AllRedirects (%s).csv"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	TUniquePtr<FArchive> OutputFile(IFileManager::Get().CreateFileWriter(*FullyQualifiedFileName));
	if (OutputFile.IsValid())
	{
		TAnsiStringBuilder<4096> StringBuilder;

		for (const TPair<FSoftObjectPath, FSoftObjectPath>& Pair : Redirects)
		{
			StringBuilder	<< WriteToAnsiString<256>(Pair.Key.ToString()).ToString() 
							<< TEXT(",") 
							<< WriteToAnsiString<256>(Pair.Value.ToString()).ToString()
							<< LINE_TERMINATOR;
		}
		OutputFile->Serialize(StringBuilder.GetData(), StringBuilder.Len());
	}
}

void FRedirectCollector::OnSoftObjectPathLoaded(const FSoftObjectPath& ObjectPath, FArchive* InArchive)
{
	if (ObjectPath.IsNull() || !GIsEditor)
	{
		// No need to track empty strings, or in standalone builds
		return;
	}

	FSoftObjectPathThreadContext& ThreadContext = FSoftObjectPathThreadContext::Get();

	FName PackageName, PropertyName;
	ESoftObjectPathCollectType CollectType = ESoftObjectPathCollectType::AlwaysCollect;
	ESoftObjectPathSerializeType SerializeType = ESoftObjectPathSerializeType::AlwaysSerialize;

	ThreadContext.GetSerializationOptions(PackageName, PropertyName, CollectType, SerializeType, InArchive);

	if (CollectType == ESoftObjectPathCollectType::NonPackage)
	{
		// Do not track
		return;
	}

	const bool bReferencedByEditorOnlyProperty = (CollectType == ESoftObjectPathCollectType::EditorOnlyCollect);
	FTopLevelAssetPath AssetPath = ObjectPath.GetAssetPath();

	FScopeLock ScopeLock(&CriticalSection);
	if (CollectType != ESoftObjectPathCollectType::NeverCollect)
	{
		// Add this reference to the soft object inclusion list for the cook's iterative traversal of the soft dependency graph
		FSoftObjectPathProperty SoftObjectPathProperty(FSoftObjectPath(AssetPath, {}), PropertyName, bReferencedByEditorOnlyProperty);
		SoftObjectPathMap.FindOrAdd(PackageName).Add(SoftObjectPathProperty);
	}

	if (ShouldTrackPackageReferenceTypes())
	{
		// Add the referenced package to the potential-exclusion list for the cook's up-front traversal of the soft dependency graph
		TStringBuilder<FName::StringBufferSize> ObjectPathString;
		ObjectPath.AppendString(ObjectPathString);
		FName ReferencedPackageName = FName(FPackageName::ObjectPathToPackageName(ObjectPathString));
		if (PackageName != ReferencedPackageName)
		{
			TMap<FName, ESoftObjectPathCollectType>& PackageReferences = PackageReferenceTypes.FindOrAdd(PackageName);
			ESoftObjectPathCollectType& ExistingCollectType = PackageReferences.FindOrAdd(ReferencedPackageName, ESoftObjectPathCollectType::NeverCollect);
			ExistingCollectType = FMath::Max(ExistingCollectType, CollectType);
		}
	}
}

void FRedirectCollector::CollectSavedSoftPackageReferences(FName ReferencingPackage, const TSet<FName>& PackageNames, bool bEditorOnlyReferences)
{
	TArray<FSoftObjectPathProperty, TInlineAllocator<4>> SoftObjectPathArray;
	Algo::Transform(PackageNames, SoftObjectPathArray, [ReferencingPackage, bEditorOnlyReferences](const FName& PackageName)
		{
			return FSoftObjectPathProperty(FSoftObjectPath::ConstructFromPackageAsset(PackageName, NAME_None), NAME_None, bEditorOnlyReferences);
		});

	FScopeLock ScopeLock(&CriticalSection);
	SoftObjectPathMap.FindOrAdd(ReferencingPackage).Append(SoftObjectPathArray);
}

void FRedirectCollector::ResolveAllSoftObjectPaths(FName FilterPackage)
{	
	auto LoadSoftObjectPathLambda = [this](const FSoftObjectPathProperty& SoftObjectPathProperty, const FName ReferencerPackageName)
	{
		FSoftObjectPath ToLoadPath = SoftObjectPathProperty.GetObjectPath();
		const FString ToLoad = ToLoadPath.ToString();

		if (ToLoad.Len() > 0 )
		{
			UE_LOG(LogRedirectors, Verbose, TEXT("Resolving Soft Object Path '%s'"), *ToLoad);
			UE_CLOG(SoftObjectPathProperty.GetPropertyName().ToString().Len(), LogRedirectors, Verbose, TEXT("    Referenced by '%s'"), *SoftObjectPathProperty.GetPropertyName().ToString());

			int32 DotIndex = ToLoad.Find(TEXT("."));
			FString PackageName = DotIndex != INDEX_NONE ? ToLoad.Left(DotIndex) : ToLoad;

			// If is known missing don't try
			if (FLinkerLoad::IsKnownMissingPackage(FName(*PackageName)))
			{
				return;
			}

			UObject *Loaded = LoadObject<UObject>(NULL, *ToLoad, NULL, SoftObjectPathProperty.GetReferencedByEditorOnlyProperty() ? LOAD_EditorOnly | LOAD_NoWarn : LOAD_NoWarn, NULL);

			if (Loaded)
			{
				FSoftObjectPath Dest(Loaded);
				UE_LOG(LogRedirectors, Verbose, TEXT("    Resolved to '%s'"), *Dest.ToString());
				if (Dest.ToString() != ToLoad)
				{
					AddObjectPathRedirectionInternal(ToLoadPath, Dest);
					FCoreRedirects::RecordAddedObjectRedirector(ToLoadPath, Dest);
				}
			}
			else
			{
				const FString Referencer = SoftObjectPathProperty.GetPropertyName().ToString().Len() ? SoftObjectPathProperty.GetPropertyName().ToString() : TEXT("Unknown");
				UE_LOG(LogRedirectors, Display, TEXT("Soft Object Path '%s' was not found when resolving paths! (Referencer '%s:%s')"), *ToLoad, *ReferencerPackageName.ToString(), *Referencer);
			}
		}
	};

	FScopeLock ScopeLock(&CriticalSection);

	FSoftObjectPathMap KeepSoftObjectPathMap;
	KeepSoftObjectPathMap.Reserve(SoftObjectPathMap.Num());
	while (SoftObjectPathMap.Num())
	{
		FSoftObjectPathMap LocalSoftObjectPathMap;
		Swap(SoftObjectPathMap, LocalSoftObjectPathMap);

		for (TPair<FName, FSoftObjectPathPropertySet>& CurrentPackage : LocalSoftObjectPathMap)
		{
			const FName& CurrentPackageName = CurrentPackage.Key;
			FSoftObjectPathPropertySet& SoftObjectPathProperties = CurrentPackage.Value;

			if ((FilterPackage != NAME_None) && // not using a filter
				(FilterPackage != CurrentPackageName) && // this is the package we are looking for
				(CurrentPackageName != NAME_None) // if we have an empty package name then process it straight away
				)
			{
				// If we have a valid filter and it doesn't match, skip processing of this package and keep it
				KeepSoftObjectPathMap.FindOrAdd(CurrentPackageName).Append(MoveTemp(SoftObjectPathProperties));
				continue;
			}

			// This will call LoadObject which may trigger OnSoftObjectPathLoaded and add new soft object paths to the SoftObjectPathMap
			for (const FSoftObjectPathProperty& SoftObjecPathProperty : SoftObjectPathProperties)
			{
				LoadSoftObjectPathLambda(SoftObjecPathProperty, CurrentPackageName);
			}
		}
	}
	PackageReferenceTypes.Empty();

	check(SoftObjectPathMap.Num() == 0);
	// Add any non processed packages back into the global map for the next time this is called
	Swap(SoftObjectPathMap, KeepSoftObjectPathMap);
	// we shouldn't have any references left if we decided to resolve them all
	check((SoftObjectPathMap.Num() == 0) || (FilterPackage != NAME_None));
}

void FRedirectCollector::ProcessSoftObjectPathPackageList(FName FilterPackage, bool bGetEditorOnly, TSet<FName>& OutReferencedPackages)
{
	TSet<FSoftObjectPathProperty> SoftObjectPathProperties;
	{
		FScopeLock ScopeLock(&CriticalSection);
		// always remove all data for the processed FilterPackage, in addition to processing it to populate OutReferencedPackages
		if (!SoftObjectPathMap.RemoveAndCopyValue(FilterPackage, SoftObjectPathProperties))
		{
			return;
		}
	}

	// potentially add soft object path package names to OutReferencedPackages
	OutReferencedPackages.Reserve(SoftObjectPathProperties.Num());
	for (const FSoftObjectPathProperty& SoftObjectPathProperty : SoftObjectPathProperties)
	{
		if (!SoftObjectPathProperty.GetReferencedByEditorOnlyProperty() || bGetEditorOnly)
		{
			FSoftObjectPath ToLoadPath = SoftObjectPathProperty.GetObjectPath();
			FString PackageNameString = FPackageName::ObjectPathToPackageName(ToLoadPath.ToString());
			OutReferencedPackages.Add(FName(*PackageNameString));
		}
	}
}

bool FRedirectCollector::RemoveAndCopySoftObjectPathExclusions(FName PackageName, TSet<FName>& OutExcludedReferences)
{
	OutExcludedReferences.Reset();
	FScopeLock ScopeLock(&CriticalSection);
	TMap<FName, ESoftObjectPathCollectType> PackageTypes;
	if (!PackageReferenceTypes.RemoveAndCopyValue(PackageName, PackageTypes))
	{
		return false;
	}

	for (TPair<FName, ESoftObjectPathCollectType>& Pair : PackageTypes)
	{
		if (Pair.Value < ESoftObjectPathCollectType::AlwaysCollect)
		{
			OutExcludedReferences.Add(Pair.Key);
		}
	}
	return OutExcludedReferences.Num() != 0;
}

void FRedirectCollector::OnStartupPackageLoadComplete()
{
	// When startup packages are done loading, we never track any more regardless whether we were before
	FScopeLock ScopeLock(&CriticalSection);
	TrackingReferenceTypesState = ETrackingReferenceTypesState::Disabled;
}

void FRedirectCollector::GetAllSourcePathsForTargetPath(const FSoftObjectPath& TargetPath, TArray<FSoftObjectPath>& OutSourcePaths) const
{
	FScopeLock ScopeLock(&CriticalSection);

	OutSourcePaths.Reset();
	if (const ObjectPathSourcesArray* Sources = ObjectPathRedirectionReverseMap.Find(TargetPath))
	{
		OutSourcePaths = *Sources;
	}
}

void FRedirectCollector::AddObjectPathRedirectionInternal(const FSoftObjectPath& Source, const FSoftObjectPath& Destination)
{
	if (FSoftObjectPath* ExistingDestination = ObjectPathRedirectionMap.Find(Source))
	{
		// We are replacing a redirect not adding one. 
		// That means we need to remove an old reverse lookup and add a new one
		if (ObjectPathSourcesArray* ReverseLookupArray = ObjectPathRedirectionReverseMap.Find(*ExistingDestination))
		{
			ReverseLookupArray->Remove(Source);
		}

		*ExistingDestination = Destination;
	}
	else
	{
		ObjectPathRedirectionMap.Add(Source, Destination);
	}

	ObjectPathSourcesArray& ReverseLookupArray = ObjectPathRedirectionReverseMap.FindOrAdd(Destination);
	ReverseLookupArray.AddUnique(Source);
}

bool FRedirectCollector::ShouldTrackPackageReferenceTypes()
{
	// Called from within CriticalSection
	if (TrackingReferenceTypesState == ETrackingReferenceTypesState::Uninitialized)
	{
		// OnStartupPackageLoadComplete has not been called yet. Turn tracking on/off depending on whether the
		// run mode needs it.
		TrackingReferenceTypesState = IsRunningCookCommandlet() ? ETrackingReferenceTypesState::Enabled : ETrackingReferenceTypesState::Disabled;
	}
	return TrackingReferenceTypesState == ETrackingReferenceTypesState::Enabled;
}

void FRedirectCollector::AddAssetPathRedirection(const FSoftObjectPath& OriginalPath, const FSoftObjectPath& RedirectedPath)
{
	FScopeLock ScopeLock(&CriticalSection);

	if (!ensureMsgf(!OriginalPath.IsNull(), TEXT("Cannot add redirect from Name_None!")))
	{
		return;
	}

	FSoftObjectPath FinalRedirection = GetAssetPathRedirection(RedirectedPath);
	if (FinalRedirection == OriginalPath)
	{
		// If RedirectedPath points back to OriginalPath, remove that to avoid a circular reference
		// This can happen when renaming assets in the editor but not actually dropping redirectors because it was new
		TryRemoveObjectPathRedirectionInternal(RedirectedPath);
	}

	// This replaces an existing mapping, can happen in the editor if things are renamed twice
	AddObjectPathRedirectionInternal(OriginalPath, RedirectedPath);
	FCoreRedirects::RecordAddedObjectRedirector(OriginalPath, RedirectedPath);
}

bool FRedirectCollector::TryRemoveObjectPathRedirectionInternal(const FSoftObjectPath& Source)
{
	FSoftObjectPath Destination;
	bool bRemoved = ObjectPathRedirectionMap.RemoveAndCopyValue(Source, Destination);
	if (bRemoved)
	{
		FCoreRedirects::RecordRemovedObjectRedirector(Source, Destination);
		if (ObjectPathSourcesArray* ReverseArray = ObjectPathRedirectionReverseMap.Find(Destination))
		{
			ReverseArray->RemoveSingleSwap(Source);
			if (ReverseArray->Num() == 0)
			{
				ObjectPathRedirectionReverseMap.Remove(Destination);
			}
		}
	}
	return bRemoved;
}

void FRedirectCollector::AddAssetPathRedirection(FName OriginalPath, FName RedirectedPath)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	AddAssetPathRedirection(FSoftObjectPath(OriginalPath), FSoftObjectPath(RedirectedPath));
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FRedirectCollector::RemoveAssetPathRedirection(const FSoftObjectPath& OriginalPath)
{
	FScopeLock ScopeLock(&CriticalSection);

	TryRemoveObjectPathRedirectionInternal(OriginalPath);
}

void FRedirectCollector::RemoveAssetPathRedirection(FName OriginalPath)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	RemoveAssetPathRedirection(FSoftObjectPath(OriginalPath));
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

FName FRedirectCollector::GetAssetPathRedirection(FName OriginalPath)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return FName(*GetAssetPathRedirection(FSoftObjectPath(OriginalPath)).ToString());
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

FSoftObjectPath FRedirectCollector::GetAssetPathRedirection(const FSoftObjectPath& OriginalPath)
{
	FScopeLock ScopeLock(&CriticalSection);
	TArray<FSoftObjectPath, TInlineAllocator<2>> SeenPaths;

	// We need to follow the redirect chain recursively
	FSoftObjectPath CurrentPath = OriginalPath;

	while (!CurrentPath.IsNull())
	{
		if (!ensureMsgf(!SeenPaths.Contains(CurrentPath), TEXT("Found circular redirect from %s to %s! Returning None instead"), *OriginalPath.ToString(), *CurrentPath.ToString()))
		{
			UE_LOG(LogRedirectors, Error, TEXT("Logging redirection chain: "));
			for (const FSoftObjectPath& Entry : SeenPaths)
			{
				UE_LOG(LogRedirectors, Error, TEXT(" %s"), *Entry.ToString());
			}
			return FSoftObjectPath();
		}
		SeenPaths.Add(CurrentPath);

		if (FSoftObjectPath* NewPath = ObjectPathRedirectionMap.Find(CurrentPath))
		{
			CurrentPath = *NewPath;
		}
		else if ((NewPath = ObjectPathRedirectionMap.Find(CurrentPath.GetWithoutSubPath())))
		{
			CurrentPath = FSoftObjectPath(NewPath->GetAssetPath(), CurrentPath.GetSubPathString());
		}
		else
		{
			break;
		}
	}

	if (CurrentPath != OriginalPath)
	{
		return CurrentPath;
	}
	return FSoftObjectPath();
}

FRedirectCollector GRedirectCollector;
#if WITH_AUTOMATION_WORKER

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRedirectCollectorReverseLookupTest, "System.Core.Misc.RedirectCollector.ReverseLookup", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FRedirectCollectorReverseLookupTest::RunTest(const FString& Parameters)
{
	FScopeLock ScopeLock(&GRedirectCollector.CriticalSection);
	bool bSuccess = true;

	// Validate that every forward redirect has a corresponding reverse redirect
	for (const TPair<FSoftObjectPath, FSoftObjectPath>& ForwardRedirect : GRedirectCollector.ObjectPathRedirectionMap)
	{
		bool bFoundReverseEntry = false;
		if (const FRedirectCollector::ObjectPathSourcesArray* Sources =
			GRedirectCollector.ObjectPathRedirectionReverseMap.Find(ForwardRedirect.Value))
		{
			if (Sources->Contains(ForwardRedirect.Key))
			{
				bFoundReverseEntry = true;
			}
		}

		if (!bFoundReverseEntry)
		{
			bSuccess = false;
			AddError(FString::Printf(TEXT("Failed to find matching reverse lookup for redirect %s --> %s"),
				*ForwardRedirect.Key.ToString(), *ForwardRedirect.Value.ToString()));
		}
	}

	// Validate that every reverse redirect has a corresponding forward redirect
	for (const TPair<FSoftObjectPath, FRedirectCollector::ObjectPathSourcesArray>& ReverseRedirectList : GRedirectCollector.ObjectPathRedirectionReverseMap)
	{
		for (const FSoftObjectPath& Source : ReverseRedirectList.Value)
		{
			bool bFoundForwardEntry = false;
			if (const FSoftObjectPath* Target = GRedirectCollector.ObjectPathRedirectionMap.Find(Source))
			{
				if (*Target == ReverseRedirectList.Key)
				{
					bFoundForwardEntry = true;
				}
			}

			if (!bFoundForwardEntry)
			{
				bSuccess = false;
				AddError(FString::Printf(TEXT("Failed to find matching reverse lookup for redirect %s --> %s"),
					*Source.ToString(), *ReverseRedirectList.Key.ToString()));
			}
		}
	}
	return bSuccess;
}
#endif // WITH_AUTOMATION_WORKER
#endif // WITH_EDITOR
