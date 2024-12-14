// Copyright Epic Games, Inc. All Rights Reserved.

#include "SaveContext.h"

#include "Algo/Find.h"
#include "Algo/Unique.h"
#include "Cooker/CookDependency.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/PackageWriter.h"
#include "UObject/UObjectGlobals.h"

TArray<ESaveRealm> FSaveContext::GetHarvestedRealmsToSave()
{
	TArray<ESaveRealm> HarvestedContextsToSave;
	if (IsCooking())
	{
		HarvestedContextsToSave.Add(ESaveRealm::Game);
		if (IsSaveOptional())
		{
			HarvestedContextsToSave.Add(ESaveRealm::Optional);
		}
	}
	else
	{
		HarvestedContextsToSave.Add(ESaveRealm::Editor);
	}
	return HarvestedContextsToSave;
}

bool FSaveContext::IsUnsaveable(TObjectPtr<UObject> InObject, bool bEmitWarning)
{
	if (!InObject)
	{
		return false;
	}
	UE::SavePackageUtilities::FObjectStatus& ObjectStatus = UpdateSaveableStatus(InObject);
	check(ObjectStatus.bSaveableStatusValid);

	if (bEmitWarning && ObjectStatus.SaveableStatus != ESaveableStatus::Success)
	{
		// if this is a class default object being exported, make sure it's not unsaveable for any reason,
		// as we need it to be saved to disk (unless it's associated with a transient generated class)
#if WITH_EDITORONLY_DATA
		ensureAlways(!ObjectStatus.bAttemptedExport || !InObject->HasAllFlags(RF_ClassDefaultObject) ||
			(InObject->GetClass()->ClassGeneratedBy != nullptr && InObject->GetClass()->HasAnyFlags(RF_Transient)));
#endif

		if (ObjectStatus.SaveableStatus == ESaveableStatus::OuterUnsaveable
			&& (ObjectStatus.SaveableStatusCulpritStatus == ESaveableStatus::AbstractClass
				|| ObjectStatus.SaveableStatusCulpritStatus == ESaveableStatus::DeprecatedClass
				|| ObjectStatus.SaveableStatusCulpritStatus == ESaveableStatus::NewerVersionExistsClass)
			&& InObject.GetPackage() == GetPackage())
		{
			check(ObjectStatus.SaveableStatusCulprit);
			UE_LOG(LogSavePackage, Warning, TEXT("%s has unsaveable outer %s (outer is %s), so it will not be saved."),
				*InObject.GetFullName(), *ObjectStatus.SaveableStatusCulprit->GetFullName(),
				LexToString(ObjectStatus.SaveableStatusCulpritStatus));
		}
	}

	return ObjectStatus.SaveableStatus != ESaveableStatus::Success;
}

UE::SavePackageUtilities::FObjectStatus& FSaveContext::UpdateSaveableStatus(TObjectPtr<UObject> InObject)
{
	UE::SavePackageUtilities::FObjectStatus* ObjectStatus = &ObjectStatusCache.FindOrAdd(InObject);
	if (ObjectStatus->bSaveableStatusValid)
	{
		return *ObjectStatus;
	}

	ObjectStatus->bSaveableStatusValid = true;
	ObjectStatus->SaveableStatus = ESaveableStatus::Success;

	ESaveableStatus StatusNoOuter = GetSaveableStatusNoOuter(InObject, *ObjectStatus);
	if (StatusNoOuter != ESaveableStatus::Success)
	{
		check(StatusNoOuter != ESaveableStatus::OuterUnsaveable &&
			StatusNoOuter != ESaveableStatus::ClassUnsaveable);
		ObjectStatus->SaveableStatus = StatusNoOuter;
		return *ObjectStatus;
	}

	if (!InObject.IsResolved())
	{
		// We do not test the saveability of the outer of unresolved objects because we cannot get their
		// outer without resolving them.
		return *ObjectStatus;
	}

	UObject* Outer = InObject->GetOuter();
	if (Outer)
	{
		UE::SavePackageUtilities::FObjectStatus& OuterStatus = UpdateSaveableStatus(Outer);
		// Calling UpdateSaveableStatus on the outer might have modified ObjectStatusCache, so
		// look up our pointer to ObjectStatus again.
		ObjectStatus = ObjectStatusCache.Find(InObject);
		check(ObjectStatus);

		ObjectStatus->bSaveableStatusValid = true;
		ObjectStatus->SaveableStatus = ESaveableStatus::Success;
		if (OuterStatus.SaveableStatus != ESaveableStatus::Success)
		{
			ObjectStatus->SaveableStatus = ESaveableStatus::OuterUnsaveable;
			if (OuterStatus.SaveableStatus == ESaveableStatus::OuterUnsaveable)
			{
				check(OuterStatus.SaveableStatusCulprit);
				check(OuterStatus.SaveableStatusCulpritStatus != ESaveableStatus::Success);
				ObjectStatus->SaveableStatusCulprit = OuterStatus.SaveableStatusCulprit;
				ObjectStatus->SaveableStatusCulpritStatus = OuterStatus.SaveableStatusCulpritStatus;
			}
			else
			{
				ObjectStatus->SaveableStatusCulprit = Outer;
				ObjectStatus->SaveableStatusCulpritStatus = OuterStatus.SaveableStatus;
			}
		}
	}

	return *ObjectStatus;
}

ESaveableStatus FSaveContext::GetSaveableStatusNoOuter(TObjectPtr<UObject> Obj,
	UE::SavePackageUtilities::FObjectStatus& ObjectStatus) const
{
	// pending kill objects are unsaveable
	if (Obj.IsResolved() && !IsValidChecked(Obj))
	{
		return ESaveableStatus::PendingKill;
	}

	// transient objects are unsaveable if non-native
	if (Obj.IsResolved() && !Obj->IsNative())
	{
		if (ObjectStatus.HasTransientFlag(Obj))
		{
			return ESaveableStatus::TransientFlag;
		}
		if (ObjectStatus.bSaveOverrideForcedTransient)
		{
			return ESaveableStatus::TransientOverride;
		}
	}

	UClass* Class = Obj.GetClass();
	// if the object class is abstract, has been marked as deprecated, there is a newer version that exist, or the class is marked transient, then the object is unsaveable
	// @note: Although object instances of a transient class should definitely be unsaveable, it results in discrepancies with the old save algorithm and currently load problems
	if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists /*| CLASS_Transient*/)
		&& !Obj->HasAnyFlags(RF_ClassDefaultObject))
	{
		// There used to be a check for reference if the class had the CLASS_HasInstancedReference,
		// but we don't need it because those references are outer-ed to the object being flagged as unsaveable, making them unsaveable as well without having to look for them
		return Class->HasAnyClassFlags(CLASS_Abstract) ? ESaveableStatus::AbstractClass :
			Class->HasAnyClassFlags(CLASS_Deprecated) ? ESaveableStatus::DeprecatedClass :
			ESaveableStatus::NewerVersionExistsClass;
	}

	return ESaveableStatus::Success;
}

bool FSaveContext::IsTransient(TObjectPtr<UObject> InObject)
{
	if (!InObject)
	{
		return false;
	}

	if (InObject->HasAnyFlags(RF_Transient))
	{
		return true;
	}

	UE::SavePackageUtilities::FObjectStatus& Status = GetCachedObjectStatus(InObject);
	if (Status.bSaveOverrideForcedTransient)
	{
		return true;
	}
	if (Status.bAttemptedExport && Status.bSaveableStatusValid && Status.SaveableStatus != ESaveableStatus::Success)
	{
		// Exports found to be unsaveable are treated the same as transient objects for all the calls to IsTransient
		// in SavePackage.
		return true;
	}

	return false;
}

FSavePackageResultStruct FSaveContext::GetFinalResult()
{
	if (Result != ESavePackageResult::Success)
	{
		return Result;
	}

	ESavePackageResult FinalResult = IsStubRequested() ? ESavePackageResult::GenerateStub : ESavePackageResult::Success;
	FSavePackageResultStruct ResultData(FinalResult, TotalPackageSizeUncompressed,
		SerializedPackageFlags, IsCompareLinker() ? MoveTemp(GetHarvestedRealm().Linker) : nullptr);

	ResultData.SavedAssets = MoveTemp(SavedAssets);
	UClass* PackageClass = UPackage::StaticClass();
	for (TObjectPtr<UObject> Import : GetImports())
	{
		if (Import.IsA(PackageClass))
		{
			ResultData.ImportPackages.Add(Import.GetFName());
		}
	}
	TSet<FName>& SoftPackageReferenceList = GetSoftPackageReferenceList();
	ResultData.SoftPackageReferences = SoftPackageReferenceList.Array();
#if WITH_EDITOR
	for (const FSoftObjectPath& RuntimeDependency : ObjectSaveContext.CookRuntimeDependencies)
	{
		FName PackageDependency = RuntimeDependency.GetLongPackageFName();
		if (!PackageDependency.IsNone())
		{
			ResultData.SoftPackageReferences.Add(PackageDependency);
		}
	}
	ResultData.CookDependencies = MoveTemp(ObjectSaveContext.CookBuildDependencies);
#endif

	return ResultData;
}

UE::SavePackageUtilities::EEditorOnlyObjectFlags FSaveContext::GetEditorOnlyObjectFlags() const
{
	using namespace UE::SavePackageUtilities;

	// If doing an editor save, HasNonEditorOnlyReferences=true overrides NotForClient, NotForServer,
	// and virtual IsEditorOnly and marks it as UsedInGame
	bool bApplyHasNonEditorOnlyReferences = GetTargetPlatform() == nullptr;
	return
		EEditorOnlyObjectFlags::CheckRecursive |
		(bApplyHasNonEditorOnlyReferences
			? EEditorOnlyObjectFlags::ApplyHasNonEditorOnlyReferences
			: EEditorOnlyObjectFlags::None);
}

namespace
{
	TArray<UClass*> AutomaticOptionalInclusionAssetTypeList;
}

void FSaveContext::SetupHarvestingRealms()
{
	// Create the different harvesting realms
	HarvestedRealms.AddDefaulted((uint32)ESaveRealm::RealmCount);

	// if cooking the default harvesting context is Game, otherwise it's the editor context
	CurrentHarvestingRealm = IsCooking() ? ESaveRealm::Game : ESaveRealm::Editor;

	// Generate the automatic optional context inclusion asset list
	static bool bAssetListGenerated = [](TArray<UClass*>& OutAssetList)
	{
		TArray<FString> AssetList;
		GConfig->GetArray(TEXT("CookSettings"), TEXT("AutomaticOptionalInclusionAssetType"), AssetList, GEditorIni);
		for (const FString& AssetType : AssetList)
		{
			if (UClass* AssetClass = FindObject<UClass>(nullptr, *AssetType, true))
			{
				OutAssetList.Add(AssetClass);
			}
			else
			{
				UE_LOG(LogSavePackage, Warning, TEXT("The asset type '%s' was not found while building the allowlist for automatic optional data inclusion list."), *AssetType);
			}
		}
		return true;
	}(AutomaticOptionalInclusionAssetTypeList);

	if (bAssetListGenerated && Asset)
	{
		// if the asset type itself is a class (ie. BP) use that to check for auto optional
		UClass* AssetType = Cast<UClass>(Asset);
		AssetType = AssetType ? AssetType : Asset->GetClass();
		bool bAllowedClass = Algo::FindByPredicate(AutomaticOptionalInclusionAssetTypeList, [AssetType](const UClass* InAssetClass)
			{
				return AssetType->IsChildOf(InAssetClass);
			}) != nullptr;
		bIsSaveAutoOptional = IsCooking() && IsSaveOptional() && bAllowedClass;
	}
}

EObjectMark FSaveContext::GetExcludedObjectMarksForGameRealm(const ITargetPlatform* TargetPlatform)
{
	if (TargetPlatform)
	{
		return UE::SavePackageUtilities::GetExcludedObjectMarksForTargetPlatform(TargetPlatform);
	}
	else
	{
		return static_cast<EObjectMark>(OBJECTMARK_NotForTargetPlatform | OBJECTMARK_EditorOnly);
	}
}

void FSaveContext::UpdateEditorRealmPackageBuildDependencies()
{
	using namespace UE::Cook;

	PackageBuildDependencies.Empty();

	// PackageBuildDependencies are only recorded for non-cooked packages
	if (IsCooking())
	{
		return;
	}

#if WITH_EDITOR
	PackageBuildDependencies.Reserve(ObjectSaveContext.CookBuildDependencies.Num());
	for (UE::Cook::FCookDependency& CookDependency : ObjectSaveContext.CookBuildDependencies)
	{
		FName PackageName = NAME_None;
		switch (CookDependency.GetType())
		{
		case ECookDependency::Package: [[fallthrough]];
		case ECookDependency::TransitiveBuild:
			PackageName = CookDependency.GetPackageName();
			break;
		default:
			break;
		}
		if (PackageName.IsNone())
		{
			continue;
		}
		PackageBuildDependencies.Add(PackageName);
	}
	PackageBuildDependencies.Sort(FNameLexicalLess());
	PackageBuildDependencies.SetNum(Algo::Unique(PackageBuildDependencies));

	FHarvestedRealm& HarvestedRealm = GetHarvestedRealm(ESaveRealm::Editor);
	TSet<FNameEntryId>& NamesReferencedFromPackageHeader = HarvestedRealm.GetNamesReferencedFromPackageHeader();
	for (FName PackageBuildDependency : PackageBuildDependencies)
	{
		NamesReferencedFromPackageHeader.Add(PackageBuildDependency.GetDisplayIndex());
	}
#endif
}

const TCHAR* LexToString(ESaveableStatus Status)
{
	static_assert(static_cast<int32>(ESaveableStatus::__Count) == 10);
	switch (Status)
	{
	case ESaveableStatus::Success: return TEXT("is saveable");
	case ESaveableStatus::PendingKill: return TEXT("is pendingkill");
	case ESaveableStatus::TransientFlag: return TEXT("is transient");
	case ESaveableStatus::TransientOverride: return TEXT("is Overriden as transient");
	case ESaveableStatus::AbstractClass: return TEXT("has a Class with CLASS_Abstract");
	case ESaveableStatus::DeprecatedClass: return TEXT("has a Class with CLASS_Deprecated");
	case ESaveableStatus::NewerVersionExistsClass: return TEXT("has a Class with CLASS_NewerVersionExists");
	case ESaveableStatus::OuterUnsaveable: return TEXT("has an unsaveable Outer");
	case ESaveableStatus::ClassUnsaveable: return TEXT("has an unsaveable Class");
	case ESaveableStatus::ExcludedByPlatform: return TEXT("is excluded by TargetPlatform");
	default: return TEXT("Unknown");
	}
}