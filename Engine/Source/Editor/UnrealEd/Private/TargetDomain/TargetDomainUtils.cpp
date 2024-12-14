// Copyright Epic Games, Inc. All Rights Reserved.

#include "TargetDomain/TargetDomainUtils.h"

#include "Algo/BinarySearch.h"
#include "Algo/IsSorted.h"
#include "Algo/Sort.h"
#include "Algo/Unique.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Containers/Set.h"
#include "Containers/SharedString.h"
#include "Containers/UnrealString.h"
#include "Cooker/CookConfigAccessTracker.h"
#include "Cooker/CookDependency.h"
#include "Cooker/PackageBuildDependencyTracker.h"
#include "CookOnTheSide/CookLog.h"
#include "CookPackageSplitter.h"
#include "DerivedDataBuildDefinition.h"
#include "DerivedDataBuildKey.h"
#include "EditorDomain/EditorDomain.h"
#include "EditorDomain/EditorDomainUtils.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "Hash/Blake3.h"
#include "IO/IoDispatcher.h"
#include "IO/IoHash.h"
#include "Misc/App.h"
#include "Misc/Guid.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Serialization/PackageWriter.h"
#include "UObject/CoreRedirects.h"
#include "ZenStoreHttpClient.h"

namespace UE::TargetDomain
{

// Change TargetDomainVersion to a new guid when all TargetDomain keys in an incremental cook need to be invalidated
FGuid TargetDomainVersion(TEXT("C9B0281696234067A3A888CEEAAA50F9"));

// Bump CookDependenciesVersion version when the serialization of CookDependencies has changed and we want to add
// backwards compatibility rather than invalidating everything.
constexpr uint32 CookDependenciesVersion = 0x00000002;

static const FUtf8StringView CookDependenciesAttachmentKey = UTF8TEXTVIEW("CookDependencies");
static const FUtf8StringView BuildDefinitionsAttachmentKey = UTF8TEXTVIEW("BuildDefinitionsAttachmentKey");
/**
 * Reads / writes an oplog for EditorDomain BuildDefinitionLists.
 * TODO: Reduce duplication between this class and FZenStoreWriter
 */
class FEditorDomainOplog
{
public:
	FEditorDomainOplog();

	bool IsValid() const;
	void CommitPackage(FName PackageName, TArrayView<IPackageWriter::FCommitAttachmentInfo> Attachments);
	FCbObject GetOplogAttachment(FName PackageName, FUtf8StringView AttachmentKey);

private:
	struct FOplogEntry
	{
		struct FAttachment
		{
			const UTF8CHAR* Key;
			FIoHash Hash;
		};

		TArray<FAttachment> Attachments;
	};

	void InitializeRead();
	
	FCbAttachment CreateAttachment(FSharedBuffer AttachmentData);
	FCbAttachment CreateAttachment(FCbObject AttachmentData)
	{
		return CreateAttachment(AttachmentData.GetBuffer().ToShared());
	}

	static void StaticInit();
	static bool IsReservedOplogKey(FUtf8StringView Key);

	UE::FZenStoreHttpClient HttpClient;
	FCriticalSection Lock;
	TMap<FName, FOplogEntry> Entries;
	bool bConnectSuccessful = false;
	bool bInitializedRead = false;

	static TArray<const UTF8CHAR*> ReservedOplogKeys;
};
TUniquePtr<FEditorDomainOplog> GEditorDomainOplog;

// Constructor/Destructor defined here in cpp rather than header so we can 
// avoid needing the definition of FCookDependency in the header; it is needed
// for construct/destruct of TArray<FCookDependency>.
FCookDependencies::FCookDependencies() = default;
FCookDependencies::~FCookDependencies() = default;
FCookDependencies::FCookDependencies(const FCookDependencies&) = default;
FCookDependencies::FCookDependencies(FCookDependencies&&) = default;
FCookDependencies& FCookDependencies::operator=(const FCookDependencies&) = default;
FCookDependencies& FCookDependencies::operator=(FCookDependencies&&) = default;


bool FCookDependencies::IsValid() const
{
	return bValid;
}

bool FCookDependencies::HasKeyMatch(const FAssetPackageData* OverrideAssetPackageData)
{
	if (!bValid)
	{
		return false;
	}
	if (StoredKey.IsZero())
	{
		return false;
	}
	if (CurrentKey.IsZero())
	{
		if (!TryCalculateCurrentKey(OverrideAssetPackageData))
		{
			return false;
		}
	}
	return CurrentKey == StoredKey;
}

bool FCookDependencies::TryCalculateCurrentKey(const FAssetPackageData* OverrideAssetPackageData,
	FString* OutErrorMessage)
{
	IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
	if (PackageName.IsNone())
	{
		if (OutErrorMessage) *OutErrorMessage = TEXT("PackageName is not set.");
		return false;
	}
	if (!AssetRegistry)
	{
		if (OutErrorMessage) *OutErrorMessage = TEXT("AssetRegistry is unavailable.");
		return false;
	}
	FEditorDomain* EditorDomain = FEditorDomain::Get();
	if (!EditorDomain)
	{
		if (OutErrorMessage) *OutErrorMessage = TEXT("EditorDomain is unavailable.");
		return false;
	}
	FBlake3 KeyBuilder;
	KeyBuilder.Update(&TargetDomainVersion, sizeof(TargetDomainVersion));

	UE::EditorDomain::FPackageDigest PackageDigest = OverrideAssetPackageData ?
		UE::EditorDomain::CalculatePackageDigest(*OverrideAssetPackageData, PackageName) :
		EditorDomain->GetPackageDigest(PackageName);
	if (!PackageDigest.IsSuccessful())
	{
		if (OutErrorMessage) *OutErrorMessage = PackageDigest.GetStatusString();
		return false;
	}
	KeyBuilder.Update(&PackageDigest.Hash, sizeof(PackageDigest.Hash));

	if (!ClassDependencies.IsEmpty())
	{
		TArray<FTopLevelAssetPath, TInlineAllocator<1>> ClassPaths;
		ClassPaths.Reserve(ClassDependencies.Num());
		for (const FString& ClassPathStr : ClassDependencies)
		{
			FTopLevelAssetPath& ClassPath = ClassPaths.Emplace_GetRef(ClassPathStr);
			if (!ClassPath.IsValid())
			{
				if (OutErrorMessage)
				{
					*OutErrorMessage = FString::Printf(
						TEXT("ClassDependency failed: %s is not a valid TopLevelAssetPath."), *ClassPathStr);
				}
				return false;
			}
		}
		FString AppendDigestError;
		if (!UE::EditorDomain::TryAppendClassDigests(KeyBuilder, ClassPaths, &AppendDigestError))
		{
			if (OutErrorMessage)
			{
				*OutErrorMessage = FString::Printf(
					TEXT("ClassDependency failed: %s"), *AppendDigestError);
			}
			return false;
		}
	}

	for (FName PackageDependency : BuildPackageDependencies)
	{
		PackageDigest = EditorDomain->GetPackageDigest(PackageDependency);
		if (!PackageDigest.IsSuccessful())
		{
			if (OutErrorMessage)
			{
				*OutErrorMessage = FString::Printf(TEXT("Could not create PackageDigest for %s: %s"),
					*PackageDependency.ToString(), *PackageDigest.GetStatusString());
			}
			return false;
		}
		KeyBuilder.Update(&PackageDigest.Hash, sizeof(PackageDigest.Hash));
	}

	FCoreRedirects::AppendHashOfRedirectsAffectingPackages(KeyBuilder, RuntimePackageDependencies);
	FCoreRedirects::AppendHashOfRedirectsAffectingPackages(KeyBuilder, ScriptPackageDependencies);

	if (!ConfigDependencies.IsEmpty())
	{
#if UE_WITH_CONFIG_TRACKING
		using namespace UE::ConfigAccessTracking;
		FCookConfigAccessTracker& ConfigTracker = FCookConfigAccessTracker::Get();
#endif
		for (const FString& ConfigDependency : ConfigDependencies)
		{
			FString Value;
#if UE_WITH_CONFIG_TRACKING
			Value = ConfigTracker.GetValue(ConfigDependency);
#endif
			uint8 Marker = 0;
			KeyBuilder.Update(&Marker, sizeof(Marker));
			if (!Value.IsEmpty())
			{
				KeyBuilder.Update(*Value, Value.Len() * sizeof(Value[0]));
			}
		}
	}

	if (!CookDependencies.IsEmpty())
	{
		bool bError = false;
		UE::Cook::FCookDependencyContext Context(&KeyBuilder, [&bError, OutErrorMessage](FString&& ErrorMessage)
			{
				if (OutErrorMessage)
				{
					if (bError)
					{
						*OutErrorMessage += TEXT("\n");
						*OutErrorMessage += ErrorMessage;
					}
					else
					{
						*OutErrorMessage = MoveTemp(ErrorMessage);
					}
				}
				bError = true;
			}, PackageName);

		for (UE::Cook::FCookDependency& CookDependency : CookDependencies)
		{
			CookDependency.UpdateHash(Context);
		}
		if (bError)
		{
			return false;
		}
	}

	if (OutErrorMessage) OutErrorMessage->Reset();
	CurrentKey = KeyBuilder.Finalize();
	return true;
}

void FCookDependencies::Reset()
{
	BuildPackageDependencies.Reset();
	ConfigDependencies.Reset();
	RuntimePackageDependencies.Reset();
	ScriptPackageDependencies.Reset();
	PackageName = FName();
	StoredKey = FIoHash::Zero;
	CurrentKey = FIoHash::Zero;
	bValid = false;
}

void FCookDependencies::Empty()
{
	Reset();
	BuildPackageDependencies.Empty();
	ConfigDependencies.Empty();
	RuntimePackageDependencies.Empty();
}

enum class EPackageMountPoint
{
	Transient,
	Script,
	Content,
	GeneratedContent,
};
EPackageMountPoint GetPackageMountPoint(FName PackageName, FName TransientPackageName)
{
	if (PackageName == TransientPackageName)
	{
		return EPackageMountPoint::Transient;
	}
	TStringBuilder<256> StringBuffer;
	PackageName.ToString(StringBuffer);
	if (FPackageName::IsMemoryPackage(StringBuffer))
	{
		return EPackageMountPoint::Transient;
	}
	if (FPackageName::IsScriptPackage(StringBuffer))
	{
		return EPackageMountPoint::Script;
	}
	if (ICookPackageSplitter::IsUnderGeneratedPackageSubPath(StringBuffer))
	{
		return EPackageMountPoint::GeneratedContent;
	}
	return EPackageMountPoint::Content;
}

FCookDependencies FCookDependencies::Collect(UPackage* Package, const ITargetPlatform* TargetPlatform,
	FSavePackageResultStruct* SaveResult, const FGeneratedPackageResultStruct* GeneratedResult, 
	TArray<FName>&& RuntimeDependencies, FString* OutErrorMessage)
{
	if (!Package)
	{
		if (OutErrorMessage) *OutErrorMessage = TEXT("Invalid null package.");
		return FCookDependencies();
	}
	IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
	if (!AssetRegistry)
	{
		if (OutErrorMessage) *OutErrorMessage = TEXT("AssetRegistry is unavailable.");
		return FCookDependencies();
	}
	FEditorDomain* EditorDomain = FEditorDomain::Get();
	if (!EditorDomain)
	{
		if (OutErrorMessage) *OutErrorMessage = TEXT("EditorDomain is unavailable.");
		return FCookDependencies();
	}

	FCookDependencies Result;
	Result.PackageName = Package->GetFName();
	TSet<FName> BuildDependenciesSet;

	TArray<FName> AssetDependencies;
	if (!GeneratedResult)
	{
		AssetRegistry->GetDependencies(Result.PackageName, AssetDependencies,
			UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Game);
		RuntimeDependencies.Append(MoveTemp(AssetDependencies));
	}
	else
	{
		// GeneratedResult->PackageDependencies are already incorporated into the PackageHash of the generated
		// package by FCookGenerationInfo::CreatePackageHash, so we do not need to add them into the BuildDependencies
	}

	FPackageBuildDependencyTracker& Tracker = FPackageBuildDependencyTracker::Get();

#if UE_WITH_PACKAGE_ACCESS_TRACKING
	if (Tracker.IsEnabled())
	{
		TArray<FBuildDependencyAccessData> AccessDatas = Tracker.GetAccessDatas(Result.PackageName);

		BuildDependenciesSet.Reserve(AccessDatas.Num());
		for (FBuildDependencyAccessData& AccessData : AccessDatas)
		{
			if (AccessData.TargetPlatform == TargetPlatform || AccessData.TargetPlatform == nullptr)
			{
				BuildDependenciesSet.Add(AccessData.ReferencedPackage);
			}
		}
	}
	else
#endif
	{
		// Defensively treat all asset dependencies as both build and runtime dependencies
		BuildDependenciesSet.Append(AssetDependencies);
	}

#if UE_WITH_CONFIG_TRACKING
	{
		using namespace UE::ConfigAccessTracking;
		FCookConfigAccessTracker& ConfigTracker = FCookConfigAccessTracker::Get();
		if (ConfigTracker.IsEnabled())
		{
			TArray<FConfigAccessData> ConfigKeys = ConfigTracker.GetPackageRecords(Result.PackageName, TargetPlatform);
			Result.ConfigDependencies.Reserve(ConfigKeys.Num());
			for (const FConfigAccessData& ConfigKey : ConfigKeys)
			{
				Result.ConfigDependencies.Add(ConfigKey.FullPathToString());
			}
		}
	}
#endif
	if (SaveResult)
	{
		// Convert some FCookDependency types into our specially-handled dependencies, and put the more
		// generic ones into this->CookDependencies
		Result.CookDependencies.Reserve(SaveResult->CookDependencies.Num());

		// Additional dependencies can be added from SettingsObject dependencies. Add these included dependencies
		// to a followup list that we process in the second round, if they exist. Handle detecting cycles in the
		// included dependencies; cycle detection is currently simple: nested includes are not yet required
		// so give an error if we ever make it to a third round.
		TArray<UE::Cook::FCookDependency> NextIncludedDependenciesBuffer[2];
		TArray<UE::Cook::FCookDependency>* RemainingDependencies = &SaveResult->CookDependencies;
		int32 IncludedDependenciesRound = 0;
		constexpr int32 MaxNumberOfIncludedDependenciesRounds = 2;
		while (!RemainingDependencies->IsEmpty())
		{
			TArray<UE::Cook::FCookDependency>* NextIncludedDependencies =
				&NextIncludedDependenciesBuffer[IncludedDependenciesRound % UE_ARRAY_COUNT(NextIncludedDependenciesBuffer)];
			if (IncludedDependenciesRound++ > 0)
			{
				NextIncludedDependencies->Reset();
				if (IncludedDependenciesRound > MaxNumberOfIncludedDependenciesRounds)
				{
					if (OutErrorMessage)
					{
						*OutErrorMessage = TEXT("More rounds than expected when handling included dependencies.");
					}
					return FCookDependencies();
				}
			}

			for (UE::Cook::FCookDependency& CookDependency : *RemainingDependencies)
			{
				switch (CookDependency.GetType())
				{
				case UE::Cook::ECookDependency::TransitiveBuild:
				{
					// Build dependencies from a package to itself have a performance cost and serve no
					// purpose, so remove them. They can occur in some systems that naively add a build dependency
					// from one object to another without checking whether the second object is in the same package.
					FName DependencyPackageName = CookDependency.GetPackageName();
					if (DependencyPackageName == Result.PackageName)
					{
						continue;
					}
					if (CookDependency.IsAlsoAddRuntimeDependency())
					{
						RuntimeDependencies.Add(DependencyPackageName);
					}
					Result.TransitiveBuildDependencies.Add(MoveTemp(CookDependency));
					break;
				}
				case UE::Cook::ECookDependency::Package:
					BuildDependenciesSet.Add(CookDependency.GetPackageName());
					break;
				case UE::Cook::ECookDependency::Config:
					Result.ConfigDependencies.Add(FString(CookDependency.GetConfigPath()));
					break;
				case UE::Cook::ECookDependency::SettingsObject:
				{
					const UObject* SettingsObject = CookDependency.GetSettingsObject();
					if (SettingsObject)
					{
						// We rely on the object to be rooted because we use its pointer as a key for the lifetime of
						// the cook process, so it being garbage collected and something else allocated on the same
						// pointer would break our key. IsRooted should have been validated by
						// FCookDependency::SettingsObject.
						check(SettingsObject->IsRooted());
						FCookDependencyGroups::FRecordedDependencies& IncludeDependencies =
							FCookDependencyGroups::Get().FindOrCreate(reinterpret_cast<UPTRINT>(SettingsObject));
						if (!IncludeDependencies.bInitialized)
						{
							IncludeDependencies.Dependencies = CollectSettingsObject(SettingsObject,
								&IncludeDependencies.ErrorMessage);
							IncludeDependencies.bInitialized = true;
						}
						if (!IncludeDependencies.Dependencies.IsValid())
						{
							if (OutErrorMessage)
							{
								*OutErrorMessage = FString::Printf(
									TEXT("Dependencies for SettingsObject %s are unavailable: %s."),
									*SettingsObject->GetPathName(), *IncludeDependencies.ErrorMessage);
							}
							return FCookDependencies();
						}

						for (const UE::Cook::FCookDependency& IncludeDependency :
							IncludeDependencies.Dependencies.GetCookDependencies())
						{
							NextIncludedDependencies->Add(IncludeDependency);
						}
					}
					break;
				}
				case UE::Cook::ECookDependency::NativeClass:
					Result.ClassDependencies.Add(FString(CookDependency.GetClassPath()));
					break;
				default:
					Result.CookDependencies.Add(MoveTemp(CookDependency));
					break;
				}
			}
			RemainingDependencies = NextIncludedDependencies;
		}
	}

	Algo::Sort(Result.CookDependencies);
	Result.CookDependencies.SetNum(Algo::Unique(Result.CookDependencies));
	Algo::Sort(Result.TransitiveBuildDependencies);
	Result.TransitiveBuildDependencies.SetNum(Algo::Unique(Result.TransitiveBuildDependencies));
	Algo::Sort(Result.ConfigDependencies);
	Result.ConfigDependencies.SetNum(Algo::Unique(Result.ConfigDependencies));
	Algo::Sort(Result.ClassDependencies);
	Result.ClassDependencies.SetNum(Algo::Unique(Result.ClassDependencies));

	FName TransientPackageName = GetTransientPackage()->GetFName();
	Result.BuildPackageDependencies = BuildDependenciesSet.Array();
	Result.BuildPackageDependencies.RemoveAllSwap([TransientPackageName](FName InPackageName)
		{
			return GetPackageMountPoint(InPackageName, TransientPackageName) != EPackageMountPoint::Content;
		}, EAllowShrinking::Yes);
	Result.BuildPackageDependencies.Sort(FNameLexicalLess());

	for (TArray<FName>::TIterator Iter(RuntimeDependencies); Iter; ++Iter)
	{
		FName PackageName = *Iter;
		EPackageMountPoint MountPoint = GetPackageMountPoint(PackageName, TransientPackageName);
		switch (MountPoint)
		{
		case EPackageMountPoint::GeneratedContent:
		case EPackageMountPoint::Content:
			Result.RuntimePackageDependencies.Add(PackageName);
			break;
		case EPackageMountPoint::Script:
			Result.ScriptPackageDependencies.Add(PackageName);
			break;
		default:
			break;
		}
	}
	Result.RuntimePackageDependencies.Sort(FNameLexicalLess());
	Result.RuntimePackageDependencies.SetNum(Algo::Unique(Result.RuntimePackageDependencies), EAllowShrinking::Yes);
	Result.ScriptPackageDependencies.Sort(FNameLexicalLess());
	Result.ScriptPackageDependencies.SetNum(Algo::Unique(Result.ScriptPackageDependencies), EAllowShrinking::Yes);

	const FAssetPackageData* AssetPackageData = GeneratedResult ? &GeneratedResult->AssetPackageData : nullptr;
	if (!Result.TryCalculateCurrentKey(AssetPackageData, OutErrorMessage))
	{
		return FCookDependencies();
	}
	Result.StoredKey = Result.CurrentKey;
	Result.bValid = true;

	if (OutErrorMessage) OutErrorMessage->Reset();
	return Result;
}

FCookDependencies FCookDependencies::CollectSettingsObject(const UObject* Object, FString* OutErrorMessage)
{
	if (!Object)
	{
		if (OutErrorMessage)
		{
			*OutErrorMessage = TEXT("Invalid null Object.");
		}
		return FCookDependencies();
	}

	UClass* Class = Object->GetClass();
	if (!Class->HasAnyClassFlags(CLASS_Config | CLASS_PerObjectConfig))
	{
		if (OutErrorMessage)
		{
			*OutErrorMessage = FString::Printf(TEXT("Class %s is not a config class."), *Class->GetPathName());
		}
		return FCookDependencies();
	}
	if (!Class->HasAnyClassFlags(CLASS_PerObjectConfig) && Object != Class->GetDefaultObject())
	{
		if (OutErrorMessage)
		{
			*OutErrorMessage = FString::Printf(TEXT("Class %s is not a per-object-config class."),
				*Class->GetPathName());
		}
		return FCookDependencies();
	}

	FCookDependencies Result;
	TArray<UE::ConfigAccessTracking::FConfigAccessData> ConfigDatas;
	const_cast<UObject*>(Object)->LoadConfig(nullptr /* ConfigClass */, nullptr /* Filename */,
		UE::LCPF_None /* PropagationFlags */, nullptr /* PropertyToLoad */, &ConfigDatas);
	Result.CookDependencies.Reserve(ConfigDatas.Num() + 1);
	for (const UE::ConfigAccessTracking::FConfigAccessData& ConfigData : ConfigDatas)
	{
		Result.CookDependencies.Add(UE::Cook::FCookDependency::Config(ConfigData));
	}

	// In addition to adding the config dependencies, add a dependency on the class schema. If the current class has
	// config fields A,B,C, we add dependencies on those config values. But if the class header is modified to have
	// additional config field D then we need to rebuild packages that depend on it to record the new dependency on D.
	UClass* NativeClass = Class;
	while (NativeClass && !NativeClass->IsNative())
	{
		NativeClass = NativeClass->GetSuperClass();
	}
	if (NativeClass)
	{
		Result.CookDependencies.Add(UE::Cook::FCookDependency::NativeClass(NativeClass));
	}

	Algo::Sort(Result.CookDependencies);
	Result.CookDependencies.SetNum(Algo::Unique(Result.CookDependencies));
	Result.bValid = true;
	return Result;
}

}

bool LoadFromCompactBinary(FCbObjectView ObjectView, UE::TargetDomain::FCookDependencies& Dependencies)
{
	using namespace UE::TargetDomain;

	Dependencies.Reset();
	int32 Version = -1;

	for (FCbFieldViewIterator FieldView(ObjectView.CreateViewIterator()); FieldView; )
	{
		const FCbFieldViewIterator Last = FieldView;
		if (FieldView.GetName().Equals(UTF8TEXTVIEW("Version")))
		{
			Version = FieldView.AsInt32();
			if ((FieldView++).HasError() || Version != CookDependenciesVersion)
			{
				return false;
			}
		}
		if (FieldView.GetName().Equals(UTF8TEXTVIEW("StoredKey")))
		{
			if (!LoadFromCompactBinary(FieldView++, Dependencies.StoredKey))
			{
				return false;
			}
		}
		if (FieldView.GetName().Equals(UTF8TEXTVIEW("BuildPackageDependencies")))
		{
			if (!LoadFromCompactBinary(FieldView++, Dependencies.BuildPackageDependencies))
			{
				return false;
			}
		}
		if (FieldView.GetName().Equals(UTF8TEXTVIEW("ConfigDependencies")))
		{
			if (!LoadFromCompactBinary(FieldView++, Dependencies.ConfigDependencies))
			{
				return false;
			}
		}
		if (FieldView.GetName().Equals(UTF8TEXTVIEW("RuntimePackageDependencies")))
		{
			if (!LoadFromCompactBinary(FieldView++, Dependencies.RuntimePackageDependencies))
			{
				return false;
			}
		}
		if (FieldView.GetName().Equals(UTF8TEXTVIEW("ScriptPackageDependencies")))
		{
			if (!LoadFromCompactBinary(FieldView++, Dependencies.ScriptPackageDependencies))
			{
				return false;
			}
		}
		if (FieldView.GetName().Equals(UTF8TEXTVIEW("CookDependencies")))
		{
			if (!LoadFromCompactBinary(FieldView++, Dependencies.CookDependencies))
			{
				return false;
			}
		}
		if (FieldView.GetName().Equals(UTF8TEXTVIEW("TransitiveBuildDependencies")))
		{
			if (!LoadFromCompactBinary(FieldView++, Dependencies.TransitiveBuildDependencies))
			{
				return false;
			}
		}
		if (FieldView.GetName().Equals(UTF8TEXTVIEW("ClassDependencies")))
		{
			if (!LoadFromCompactBinary(FieldView++, Dependencies.ClassDependencies))
			{
				return false;
			}
		}
		if (FieldView == Last)
		{
			++FieldView;
		}
	}
	if (Version == -1)
	{
		return false;
	}
	Dependencies.bValid = true;
	return true;
}

FCbWriter& operator<<(FCbWriter& Writer, const UE::TargetDomain::FCookDependencies& CookDependencies)
{
	using namespace UE::TargetDomain;

	Writer.BeginObject();
	Writer << "Version" << CookDependenciesVersion;
	Writer << "StoredKey" << CookDependencies.StoredKey;
	if (!CookDependencies.BuildPackageDependencies.IsEmpty())
	{
		Writer << "BuildPackageDependencies" << CookDependencies.BuildPackageDependencies;
	}
	if (!CookDependencies.ConfigDependencies.IsEmpty())
	{
		Writer << "ConfigDependencies" << CookDependencies.ConfigDependencies;
	}
	if (!CookDependencies.RuntimePackageDependencies.IsEmpty())
	{
		Writer << "RuntimePackageDependencies" << CookDependencies.RuntimePackageDependencies;
	}
	if (!CookDependencies.ScriptPackageDependencies.IsEmpty())
	{
		Writer << "ScriptPackageDependencies" << CookDependencies.ScriptPackageDependencies;
	}
	if (!CookDependencies.CookDependencies.IsEmpty())
	{
		Writer << "CookDependencies" << CookDependencies.CookDependencies;
	}
	if (!CookDependencies.TransitiveBuildDependencies.IsEmpty())
	{
		Writer << "TransitiveBuildDependencies" << CookDependencies.TransitiveBuildDependencies;
	}
	if (!CookDependencies.ClassDependencies.IsEmpty())
	{
		Writer << "ClassDependencies" << CookDependencies.ClassDependencies;
	}

	Writer.EndObject();
	return Writer;
}

namespace UE::TargetDomain
{

FCookDependencyGroups& FCookDependencyGroups::Get()
{
	static FCookDependencyGroups Singleton;
	return Singleton;
}

FCookDependencyGroups::FRecordedDependencies& FCookDependencyGroups::FindOrCreate(UPTRINT Key)
{
	return Groups.FindOrAdd(Key);
}

FBuildDefinitionList FBuildDefinitionList::Collect(UPackage* Package, const ITargetPlatform* TargetPlatform,
	FString* OutErrorMessage)
{
	using namespace UE::DerivedData;

	FBuildDefinitionList Result;

	// TODO_BuildDefinitionList: Calculate and store BuildDefinitionList on the PackageData, or collect it here from some other source.
	if (Result.Definitions.IsEmpty())
	{
		if (OutErrorMessage) *OutErrorMessage = TEXT("Not yet implemented");
		return FBuildDefinitionList();
	}

	TArray<FBuildDefinition>& Defs = Result.Definitions;
	Algo::Sort(Defs, [](const FBuildDefinition& A, const FBuildDefinition& B)
		{
			return A.GetKey().Hash < B.GetKey().Hash;
		});

	if (OutErrorMessage) OutErrorMessage->Reset();
	return Result;
}

void FBuildDefinitionList::Reset()
{
	Definitions.Reset();
}

void FBuildDefinitionList::Empty()
{
	Definitions.Empty();
}

}

bool LoadFromCompactBinary(FCbObject&& Object, UE::TargetDomain::FBuildDefinitionList& Definitions)
{
	using namespace UE::DerivedData;

	FCbField DefinitionsField = Object["BuildDefinitions"];
	FCbArray DefinitionsArrayField = DefinitionsField.AsArray();
	if (DefinitionsField.HasError())
	{
		return false;
	}
	TArray<FBuildDefinition>& Defs = Definitions.Definitions;
	Defs.Empty(DefinitionsArrayField.Num());
	for (FCbField& BuildDefinitionObj : DefinitionsArrayField)
	{
		FOptionalBuildDefinition BuildDefinition = FBuildDefinition::Load(TEXTVIEW("TargetDomainBuildDefinitionList"),
			BuildDefinitionObj.AsObject());
		if (!BuildDefinition)
		{
			Defs.Empty();
			return false;
		}
		Defs.Add(MoveTemp(BuildDefinition).Get());
	}

	return true;
}

FCbWriter& operator<<(FCbWriter& Writer, const UE::TargetDomain::FBuildDefinitionList& Definitions)
{
	using namespace UE::DerivedData;

	Writer.BeginObject();
	Writer.BeginArray("BuildDefinitions");
	for (const FBuildDefinition& BuildDefinition : Definitions.Definitions)
	{
		BuildDefinition.Save(Writer);
	}
	Writer.EndArray();
	return Writer;
}

namespace UE::TargetDomain
{

void FCookAttachments::Reset()
{
	Dependencies.Reset();
	BuildDefinitions.Reset();
}

void FCookAttachments::Empty()
{
	Dependencies.Empty();
	BuildDefinitions.Empty();
}

bool TryCollectAndStoreCookDependencies(UPackage* Package, const ITargetPlatform* TargetPlatform,
	FSavePackageResultStruct* SaveResult, const FGeneratedPackageResultStruct* GeneratedResult,
	TArray<FName>&& RuntimeDependencies, IPackageWriter::FCommitAttachmentInfo& OutResult)
{
	FString ErrorMessage;
	FCookDependencies CookDependencies = FCookDependencies::Collect(Package, TargetPlatform, SaveResult,
		GeneratedResult, MoveTemp(RuntimeDependencies), &ErrorMessage);
	if (!CookDependencies.IsValid())
	{
		// IterativeTODO: This error occurs due to dependencies on _Verse and on some transient packages.
#if 0
		UE_LOG(LogCook, Error, TEXT("Could not collect CookDependencies for package '%s': %s"),
			*Package->GetName(), *ErrorMessage);
#endif
		OutResult.Value = FCbObject();
		return false;
	}

	FCbWriter Writer;
	Writer << CookDependencies;
	OutResult.Key = CookDependenciesAttachmentKey;
	OutResult.Value = Writer.Save().AsObject();
	return true;
}

bool TryCollectAndStoreBuildDefinitionList(UPackage* Package, const ITargetPlatform* TargetPlatform,
	IPackageWriter::FCommitAttachmentInfo& OutResult)
{
	FBuildDefinitionList Definitions = FBuildDefinitionList::Collect(Package, TargetPlatform);
	if (Definitions.Definitions.IsEmpty())
	{
		OutResult.Value = FCbObject();
		return false;
	}

	FCbWriter Writer;
	Writer << Definitions;
	OutResult.Key = BuildDefinitionsAttachmentKey;
	OutResult.Value = Writer.Save().AsObject();
	return true;
}

void FCookAttachments::Fetch(TArrayView<FName> PackageNames, const ITargetPlatform* TargetPlatform,
	ICookedPackageWriter* PackageWriter,
	TUniqueFunction<void(FName PackageName, FCookAttachments&& Result)>&& Callback)
{
	for (FName PackageName : PackageNames)
	{
		FCbObject DependenciesObj;
		FCbObject BuildDefinitionsObj;
		if (TargetPlatform)
		{
			check(PackageWriter);
			DependenciesObj = PackageWriter->GetOplogAttachment(PackageName, CookDependenciesAttachmentKey);
			BuildDefinitionsObj = PackageWriter->GetOplogAttachment(PackageName, CookDependenciesAttachmentKey);
		}
		else
		{
			if (!GEditorDomainOplog)
			{
				Callback(PackageName, FCookAttachments());
				continue;
			}
			DependenciesObj = GEditorDomainOplog->GetOplogAttachment(PackageName, CookDependenciesAttachmentKey);
			BuildDefinitionsObj = GEditorDomainOplog->GetOplogAttachment(PackageName, BuildDefinitionsAttachmentKey);
		}

		FCookAttachments Result;
		if (LoadFromCompactBinary(DependenciesObj, Result.Dependencies))
		{
			Result.Dependencies.PackageName = PackageName;
		}
		LoadFromCompactBinary(MoveTemp(BuildDefinitionsObj), Result.BuildDefinitions);

		Callback(PackageName, MoveTemp(Result));
	}
}

bool IsIterativeEnabled(FName PackageName, bool bAllowAllClasses, const FAssetPackageData* OverrideAssetPackageData)
{
	IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
	if (!AssetRegistry)
	{
		return false;
	}
	TOptional<FAssetPackageData> PackageDataOpt;
	if (!OverrideAssetPackageData)
	{
		PackageDataOpt = AssetRegistry->GetAssetPackageDataCopy(PackageName);
		if (!PackageDataOpt)
		{
			return false;
		}
		OverrideAssetPackageData = PackageDataOpt.GetPtrOrNull();
	}
	const FAssetPackageData& PackageData = *OverrideAssetPackageData;

	if (!bAllowAllClasses)
	{
		auto LogInvalidDueTo = [](FName PackageName, FName ClassPath)
			{
				UE_LOG(LogEditorDomain, Verbose, TEXT("NonIterative Package %s due to %s"), *PackageName.ToString(), *ClassPath.ToString());
			};

		UE::EditorDomain::FClassDigestMap& ClassDigests = UE::EditorDomain::GetClassDigests();
		FReadScopeLock ClassDigestsScopeLock(ClassDigests.Lock);
		for (FName ClassName : PackageData.ImportedClasses)
		{
			FTopLevelAssetPath ClassPath(WriteToString<256>(ClassName).ToView());
			UE::EditorDomain::FClassDigestData* ExistingData = nullptr;
			if (ClassPath.IsValid())
			{
				ExistingData = ClassDigests.Map.Find(ClassPath);
			}
			if (!ExistingData)
			{
				// !ExistingData -> !allowed, because caller has already called CalculatePackageDigest, so all
				// existing classes in the package have been added to ClassDigests.
				LogInvalidDueTo(PackageName, ClassName);
				return false;
			}
			if (!ExistingData->bNative)
			{
				// TODO: We need to add a way to mark non-native classes (there can be many of them) as allowed or denied.
				// Currently we are allowing them all, so long as their closest native is allowed. But this is not completely
				// safe to do, because non-native classes can add constructionevents that e.g. use the Random function.
				ExistingData = ClassDigests.Map.Find(ExistingData->ClosestNative);
				if (!ExistingData)
				{
					LogInvalidDueTo(PackageName, ClassName);
					return false;
				}
			}
			if (!ExistingData->bTargetIterativeEnabled)
			{
				LogInvalidDueTo(PackageName, ClassName);
				return false;
			}
		}
	}
	return true;
}

TArray<const UTF8CHAR*> FEditorDomainOplog::ReservedOplogKeys;

FEditorDomainOplog::FEditorDomainOplog()
#if UE_WITH_ZEN
: HttpClient(TEXT("localhost"), UE::Zen::FZenServiceInstance::GetAutoLaunchedPort() > 0 ? UE::Zen::FZenServiceInstance::GetAutoLaunchedPort() : 8558)
#else
: HttpClient(TEXT("localhost"), 8558)
#endif
{
	StaticInit();

	FString ProjectId = FApp::GetZenStoreProjectId();
	FString OplogId = TEXT("EditorDomain");

	FString RootDir = FPaths::RootDir();
	FString EngineDir = FPaths::EngineDir();
	FPaths::NormalizeDirectoryName(EngineDir);
	FString ProjectDir = FPaths::ProjectDir();
	FPaths::NormalizeDirectoryName(ProjectDir);
	FString ProjectPath = FPaths::GetProjectFilePath();
	FPaths::NormalizeFilename(ProjectPath);

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	FString AbsServerRoot = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(*RootDir);
	FString AbsEngineDir = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(*EngineDir);
	FString AbsProjectDir = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(*ProjectDir);
	FString ProjectFilePath = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(*ProjectPath);

#if UE_WITH_ZEN
	if (UE::Zen::IsDefaultServicePresent())
	{
		bool IsLocalConnection = HttpClient.GetZenServiceInstance().IsServiceRunningLocally();
		HttpClient.TryCreateProject(ProjectId, FStringView(), OplogId, AbsServerRoot, AbsEngineDir, AbsProjectDir, IsLocalConnection ? ProjectFilePath : FStringView());
		HttpClient.TryCreateOplog(ProjectId, OplogId, TEXT("") /*InOplogMarkerFile*/, false /* bFullBuild */);
	}
#endif
}

void FEditorDomainOplog::InitializeRead()
{
	if (bInitializedRead)
	{
		return;
	}
	UE_LOG(LogEditorDomain, Display, TEXT("Fetching EditorDomain oplog..."));

	TFuture<FIoStatus> FutureOplogStatus = HttpClient.GetOplog().Next([this](TIoStatusOr<FCbObject> OplogStatus)
		{
			if (!OplogStatus.IsOk())
			{
				return OplogStatus.Status();
			}

			FCbObject Oplog = OplogStatus.ConsumeValueOrDie();

			for (FCbField& EntryObject : Oplog["entries"])
			{
				FUtf8StringView PackageName = EntryObject["key"].AsString();
				if (PackageName.IsEmpty())
				{
					continue;
				}
				FName PackageFName(PackageName);
				FOplogEntry& Entry = Entries.FindOrAdd(PackageFName);
				Entry.Attachments.Empty();

				for (FCbFieldView Field : EntryObject)
				{
					FUtf8StringView FieldName = Field.GetName();
					if (IsReservedOplogKey(FieldName))
					{
						continue;
					}
					if (Field.IsHash())
					{
						const UTF8CHAR* AttachmentId = UE::FZenStoreHttpClient::FindOrAddAttachmentId(FieldName);
						Entry.Attachments.Add({ AttachmentId, Field.AsHash() });
					}
				}
				Entry.Attachments.Shrink();
				check(Algo::IsSorted(Entry.Attachments, [](const FOplogEntry::FAttachment& A, const FOplogEntry::FAttachment& B)
					{
						return FUtf8StringView(A.Key).Compare(FUtf8StringView(B.Key), ESearchCase::IgnoreCase) < 0;
					}));
			}

			return FIoStatus::Ok;
		});
	FutureOplogStatus.Get();
	bInitializedRead = true;
}

FCbAttachment FEditorDomainOplog::CreateAttachment(FSharedBuffer AttachmentData)
{
	FCompressedBuffer CompressedBuffer = FCompressedBuffer::Compress(AttachmentData);
	check(!CompressedBuffer.IsNull());
	return FCbAttachment(CompressedBuffer);
}

void FEditorDomainOplog::StaticInit()
{
	if (ReservedOplogKeys.Num() > 0)
	{
		return;
	}

	ReservedOplogKeys.Append({ UTF8TEXT("key") });
	Algo::Sort(ReservedOplogKeys, [](const UTF8CHAR* A, const UTF8CHAR* B)
		{
			return FUtf8StringView(A).Compare(FUtf8StringView(B), ESearchCase::IgnoreCase) < 0;
		});;
}

bool FEditorDomainOplog::IsReservedOplogKey(FUtf8StringView Key)
{
	int32 Index = Algo::LowerBound(ReservedOplogKeys, Key,
		[](const UTF8CHAR* Existing, FUtf8StringView Key)
		{
			return FUtf8StringView(Existing).Compare(Key, ESearchCase::IgnoreCase) < 0;
		});
	return Index != ReservedOplogKeys.Num() &&
		FUtf8StringView(ReservedOplogKeys[Index]).Equals(Key, ESearchCase::IgnoreCase);
}

bool FEditorDomainOplog::IsValid() const
{
	return HttpClient.IsConnected();
}

void FEditorDomainOplog::CommitPackage(FName PackageName, TArrayView<IPackageWriter::FCommitAttachmentInfo> Attachments)
{
	FScopeLock ScopeLock(&Lock);

	FCbPackage Pkg;

	TArray<FCbAttachment, TInlineAllocator<2>> CbAttachments;
	int32 NumAttachments = Attachments.Num();
	FOplogEntry& Entry = Entries.FindOrAdd(PackageName);
	Entry.Attachments.Empty(NumAttachments);
	if (NumAttachments)
	{
		TArray<const IPackageWriter::FCommitAttachmentInfo*, TInlineAllocator<2>> SortedAttachments;
		SortedAttachments.Reserve(NumAttachments);
		for (const IPackageWriter::FCommitAttachmentInfo& AttachmentInfo : Attachments)
		{
			SortedAttachments.Add(&AttachmentInfo);
		}
		SortedAttachments.Sort([](const IPackageWriter::FCommitAttachmentInfo& A, const IPackageWriter::FCommitAttachmentInfo& B)
			{
				return A.Key.Compare(B.Key, ESearchCase::IgnoreCase) < 0;
			});
		CbAttachments.Reserve(NumAttachments);
		for (const IPackageWriter::FCommitAttachmentInfo* AttachmentInfo : SortedAttachments)
		{
			const FCbAttachment& CbAttachment = CbAttachments.Add_GetRef(CreateAttachment(AttachmentInfo->Value));
			check(!IsReservedOplogKey(AttachmentInfo->Key));
			Pkg.AddAttachment(CbAttachment);
			Entry.Attachments.Add(FOplogEntry::FAttachment{
				UE::FZenStoreHttpClient::FindOrAddAttachmentId(AttachmentInfo->Key), CbAttachment.GetHash() });
		}
	}

	FCbWriter PackageObj;
	FString PackageNameKey = PackageName.ToString();
	PackageNameKey.ToLowerInline();
	PackageObj.BeginObject();
	PackageObj << "key" << PackageNameKey;
	for (int32 Index = 0; Index < NumAttachments; ++Index)
	{
		FCbAttachment& CbAttachment = CbAttachments[Index];
		FOplogEntry::FAttachment& EntryAttachment = Entry.Attachments[Index];
		PackageObj << EntryAttachment.Key << CbAttachment;
	}
	PackageObj.EndObject();

	FCbObject Obj = PackageObj.Save().AsObject();
	Pkg.SetObject(Obj);
	HttpClient.AppendOp(Pkg);
}

// Note that this is destructive - we yank out the buffer memory from the 
// IoBuffer into the FSharedBuffer
FSharedBuffer IoBufferToSharedBuffer(FIoBuffer& InBuffer)
{
	InBuffer.EnsureOwned();
	const uint64 DataSize = InBuffer.DataSize();
	uint8* DataPtr = InBuffer.Release().ValueOrDie();
	return FSharedBuffer{ FSharedBuffer::TakeOwnership(DataPtr, DataSize, FMemory::Free) };
};

FCbObject FEditorDomainOplog::GetOplogAttachment(FName PackageName, FUtf8StringView AttachmentKey)
{
	FScopeLock ScopeLock(&Lock);
	InitializeRead();

	FOplogEntry* Entry = Entries.Find(PackageName);
	if (!Entry)
	{
		return FCbObject();
	}

	const UTF8CHAR* AttachmentId = UE::FZenStoreHttpClient::FindAttachmentId(AttachmentKey);
	if (!AttachmentId)
	{
		return FCbObject();
	}
	FUtf8StringView AttachmentIdView(AttachmentId);

	int32 AttachmentIndex = Algo::LowerBound(Entry->Attachments, AttachmentIdView,
		[](const FOplogEntry::FAttachment& Existing, FUtf8StringView AttachmentIdView)
		{
			return FUtf8StringView(Existing.Key).Compare(AttachmentIdView, ESearchCase::IgnoreCase) < 0;
		});
	if (AttachmentIndex == Entry->Attachments.Num())
	{
		return FCbObject();
	}
	const FOplogEntry::FAttachment& Existing = Entry->Attachments[AttachmentIndex];
	if (!FUtf8StringView(Existing.Key).Equals(AttachmentIdView, ESearchCase::IgnoreCase))
	{
		return FCbObject();
	}
	TIoStatusOr<FIoBuffer> BufferResult = HttpClient.ReadOpLogAttachment(WriteToString<48>(Existing.Hash));
	if (!BufferResult.IsOk())
	{
		return FCbObject();
	}
	FIoBuffer Buffer = BufferResult.ValueOrDie();
	if (Buffer.DataSize() == 0)
	{
		return FCbObject();
	}

	FSharedBuffer SharedBuffer = IoBufferToSharedBuffer(Buffer);
	return FCbObject(SharedBuffer);
}

void CommitEditorDomainCookAttachments(FName PackageName, TArrayView<IPackageWriter::FCommitAttachmentInfo> Attachments)
{
	if (!GEditorDomainOplog)
	{
		return;
	}
	GEditorDomainOplog->CommitPackage(PackageName, Attachments);
}

void CookInitialize()
{
	bool bCookAttachmentsEnabled = true;
	GConfig->GetBool(TEXT("EditorDomain"), TEXT("CookAttachmentsEnabled"), bCookAttachmentsEnabled, GEditorIni);
	if (bCookAttachmentsEnabled)
	{
		GEditorDomainOplog = MakeUnique<FEditorDomainOplog>();
		if (!GEditorDomainOplog->IsValid())
		{
			UE_LOG(LogEditorDomain, Display, TEXT("Failed to connect to ZenServer; EditorDomain oplog is unavailable."));
			GEditorDomainOplog.Reset();
		}
	}
}


} // namespace UE::TargetDomain
