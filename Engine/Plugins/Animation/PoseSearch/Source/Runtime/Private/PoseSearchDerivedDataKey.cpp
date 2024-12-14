// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "PoseSearch/PoseSearchDerivedDataKey.h"
#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "AnimationModifier.h"
#include "Animation/AnimCompress.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/StreamableRenderAsset.h"
#include "Factories/FbxAnimSequenceImportData.h"
#include "Components/SkeletalMeshComponent.h"
#include "PoseSearch/PoseSearchDefines.h"
#include "UObject/DevObjectVersion.h"

namespace UE::PoseSearch
{

// log properties and UObjects names
#ifndef UE_POSE_SEARCH_DERIVED_DATA_LOGGING
	#define UE_POSE_SEARCH_DERIVED_DATA_LOGGING 0
#endif

// log properties data
#ifndef UE_POSE_SEARCH_DERIVED_DATA_LOGGING_VERBOSE
	#define UE_POSE_SEARCH_DERIVED_DATA_LOGGING_VERBOSE 0
#endif

FKeyBuilder::FKeyBuilder()
{
	ArIgnoreOuterRef = true;

	// Set FDerivedDataKeyBuilder to be a saving archive instead of a reference collector.
	// Reference collection causes FSoftObjectPtrs to be serialized by their weak pointer,
	// which doesn't give a stable hash.  Serializing these to a saving archive will
	// use a string reference instead, which is a more meaningful hash value.
	SetIsSaving(true);
}

FKeyBuilder::FKeyBuilder(const UObject* Object, bool bUseDataVer, bool bPerformConditionalPostLoadIfRequired, FPartialKeyHashes* InPartialKeyHashes, EDebugPartialKeyHashesMode InDebugPartialKeyHashesMode)
: FKeyBuilder()
{
	check(Object);

	// preallocating a reasonable amount of memory to avoid multiple reallocations
	ObjectsToSerialize.Reserve(256);
	ObjectBeingSerializedDependencies.Reserve(256);
	LocalPartialKeyHashes.Reserve(1024);

	bPerformConditionalPostLoad = bPerformConditionalPostLoadIfRequired;
	PartialKeyHashes = InPartialKeyHashes;
	DebugPartialKeyHashesMode = InDebugPartialKeyHashesMode;

	if (bUseDataVer)
	{
		// used to invalidate the key without having to change POSESEARCHDB_DERIVEDDATA_VER all the times
		int32 POSESEARCHDB_DERIVEDDATA_VER_SMALL = 2656;
		FGuid VersionGuid = FDevSystemGuids::GetSystemGuid(FDevSystemGuids::Get().POSESEARCHDB_DERIVEDDATA_VER);
		FString AnimationCompressionVersionString = UE::Anim::Compression::AnimationCompressionVersionString;

		*this << VersionGuid;
		*this << AnimationCompressionVersionString;
		*this << POSESEARCHDB_DERIVEDDATA_VER_SMALL;
	}

	// FKeyBuilder is a saving only archiver, and since it doesn't modify the input Object it's safe to do a const_cast 
	UObject* NonConstObject = const_cast<UObject*>(Object);
	*this << NonConstObject;

	while (!ObjectsToSerialize.IsEmpty() && !bAnyAssetNotReady)
	{
		SerializeObjectInternal(ObjectsToSerialize.Pop(EAllowShrinking::No));
	}
}

FKeyBuilder::FKeyBuilder(const UObject* Object, bool bUseDataVer, bool bPerformConditionalPostLoadIfRequired)
	: FKeyBuilder(Object, bUseDataVer, bPerformConditionalPostLoadIfRequired, nullptr, FKeyBuilder::EDebugPartialKeyHashesMode::DoNotUse)
{
}

void FKeyBuilder::Seek(int64 InPos)
{
	checkf(InPos == Tell(), TEXT("A hash cannot be computed when serialization relies on seeking."));
	FArchiveUObject::Seek(InPos);
}

bool FKeyBuilder::ShouldSkipProperty(const FProperty* InProperty) const
{
	if (InProperty == nullptr)
	{
		return false;
	}

	if (Super::ShouldSkipProperty(InProperty))
	{
		#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING
		UE_LOG(LogPoseSearch, Log, TEXT("  x %s (ShouldSkipProperty)"), *InProperty->GetFullName());
		#endif
		return true;
	}

	if (InProperty->HasAllPropertyFlags(CPF_Transient))
	{
		#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING
		UE_LOG(LogPoseSearch, Log, TEXT("  x %s (Transient)"), *InProperty->GetFullName());
		#endif
		return true;
	}

	if (InProperty->HasMetaData(ExcludeFromHashName))
	{
		#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING
		UE_LOG(LogPoseSearch, Log, TEXT("  x %s (ExcludeFromHash)"), *InProperty->GetFullName());
		#endif
		return true;
	}
	
	if (InProperty->HasMetaData(IgnoreForMemberInitializationTestName))
	{
		#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING
		UE_LOG(LogPoseSearch, Log, TEXT("  x %s (IgnoreForMemberInitializationTest)"), *InProperty->GetFullName());
		#endif
		return true;
	}

	check(!InProperty->HasMetaData(NeverInHashName));

	#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING
	UE_LOG(LogPoseSearch, Log, TEXT("  - %s"), *InProperty->GetFullName());
	#endif

	return false;
}

void FKeyBuilder::Serialize(void* Data, int64 Length)
{
	const uint8* HasherData = reinterpret_cast<uint8*>(Data);

	#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING_VERBOSE
	FString RawBytesString = BytesToString(HasherData, Length);
	UE_LOG(LogPoseSearch, Log, TEXT("  > %s"), *RawBytesString);
	#endif

	Hasher.Update(HasherData, Length);
}

FArchive& FKeyBuilder::operator<<(FName& Name)
{
	// Don't include the name of the object being serialized, since that isn't technically part of the object's state
	if (!ObjectBeingSerialized || (Name != ObjectBeingSerialized->GetFName()))
	{
		// we cannot use GetTypeHash(Name) since it's bound to be non deterministic between editor restarts, so we convert the name into an FString and let the Serialize(void* Data, int64 Length) deal with it
		FString NameString = Name.ToString();
		*this << NameString;
	}
	return *this;
}

FArchive& FKeyBuilder::TryAddDependency(UObject* Object, bool bAddToPartialKeyHashes)
{
	if (Object->HasAnyFlags(RF_NeedPostLoad))
	{
		if (bPerformConditionalPostLoad)
		{
			Object->ConditionalPostLoad();
		}
		else
		{
			bAnyAssetNotReady = true;
			return *this;
		}
	}

	// @todo: should we WaitOnExistingCompression?
	//if (UAnimSequence* AnimSequence = Cast<UAnimSequence>(Object))
	//{
	//	AnimSequence->WaitOnExistingCompression();
	//}

	// collecting ALL the dependencies of the object being serialized, so we can then cache it in PartialKeyHashes
	ObjectBeingSerializedDependencies.Add(Object);

	bool bAlreadyProcessed = false;
	Dependencies.Add(Object, &bAlreadyProcessed);

	// If we haven't already serialized this object
	if (bAlreadyProcessed)
	{
#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING
		UE_LOG(LogPoseSearch, Log, TEXT("AlreadyProcessed '%s' (%s)"), *Object->GetName(), *Object->GetClass()->GetName());
#endif
		return *this;
	}

	ObjectsToSerialize.Add(Object);
	return *this;
}

FArchive& FKeyBuilder::operator<<(class UObject*& Object)
{
	if (!Object)
	{
		return *this;
	}

	if (Object->HasAnyFlags(RF_Transient))
	{
#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING
		UE_LOG(LogPoseSearch, Log, TEXT("Transient '%s' (%s)"), *Object->GetName(), *Object->GetClass()->GetName());
#endif
		return *this;
	}

	if (IsExcludedType(Object))
	{
#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING
		UE_LOG(LogPoseSearch, Log, TEXT("Excluded '%s' (%s)"), *Object->GetName(), *Object->GetClass()->GetName());
#endif
		return *this;
	}

	return TryAddDependency(Object, true);
}

void FKeyBuilder::SerializeObjectInternal(UObject* Object)
{
	Hasher.Reset();

	check(!bAnyAssetNotReady && Object);

	// test to validate the PartialKeyHashes
	bool bIsValidTestPartialKeyHash = false;
	FPartialKeyHashes::FEntry TestEntry;

	// adding the LocalCachedHash here to keep the order consistent with Dependencies
	FLocalPartialKeyHash& LocalCachedHash = LocalPartialKeyHashes.AddDefaulted_GetRef();

	if (PartialKeyHashes)
	{
		if (DebugPartialKeyHashesMode == EDebugPartialKeyHashesMode::Validate)
		{
			if (const FPartialKeyHashes::FEntry* Entry = PartialKeyHashes->Find(Object))
			{
				bIsValidTestPartialKeyHash = true;
				TestEntry = *Entry;
			}
		}
		else if (DebugPartialKeyHashesMode == EDebugPartialKeyHashesMode::Use)
		{
			if (const FPartialKeyHashes::FEntry* Entry = PartialKeyHashes->Find(Object))
			{
				for (const TWeakObjectPtr<>& DependencyPtr : Entry->Dependencies)
				{
					if (UObject* Dependency = DependencyPtr.Get())
					{
						TryAddDependency(Dependency, false);
					}
				}

				LocalCachedHash.Object = Object;
				LocalCachedHash.Hash = Entry->Hash;
				return;
			}
		}
	}

	// making sure we don't call Object->Serialize recursively!
	check(ObjectBeingSerialized == nullptr);
	ObjectBeingSerialized = Object;

	ObjectBeingSerializedDependencies.Reset();

	if (IsAddNameOnlyType(Object))
	{
		// for specific types we only add their names to the hash
#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING
		UE_LOG(LogPoseSearch, Log, TEXT("AddingNameOnly '%s' (%s)"), *Object->GetName(), *Object->GetClass()->GetName());
#endif
		FString ObjectName = GetFullNameSafe(Object);
		*this << ObjectName;
	}
	else
	{
#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING
		UE_LOG(LogPoseSearch, Log, TEXT("Begin '%s' (%s)"), *Object->GetName(), *Object->GetClass()->GetName());
#endif
		Object->Serialize(*this);

#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING
		UE_LOG(LogPoseSearch, Log, TEXT("End '%s' (%s)"), *Object->GetName(), *Object->GetClass()->GetName());
#endif
	}

	// making sure we don't call Object->Serialize recursively!
	check(ObjectBeingSerialized == Object);
	ObjectBeingSerialized = nullptr;

	if (!bAnyAssetNotReady)
	{
		LocalCachedHash.Object = Object;
		LocalCachedHash.Hash = Hasher.Finalize();

		if (DebugPartialKeyHashesMode != EDebugPartialKeyHashesMode::DoNotUse && PartialKeyHashes)
		{
			PartialKeyHashes->Add(LocalCachedHash.Object, LocalCachedHash.Hash, ObjectBeingSerializedDependencies);

			if (bIsValidTestPartialKeyHash)
			{
				check(TestEntry.CheckDependencies(ObjectBeingSerializedDependencies));
				check(LocalCachedHash.Hash == TestEntry.Hash);
			}
		}
	}
}

FString FKeyBuilder::GetArchiveName() const
{
	return TEXT("FDerivedDataKeyBuilder");
}
	
bool FKeyBuilder::AnyAssetNotReady() const
{
	return bAnyAssetNotReady;
}

FIoHash FKeyBuilder::Finalize() const
{
	check(!bAnyAssetNotReady); // otherwise key can be non deterministic

	HashBuilderType FinalizeHasher;

	for (const FLocalPartialKeyHash& LocalCachedHash : LocalPartialKeyHashes)
	{
		const HashDigestType::ByteArray& LocalHashData = LocalCachedHash.Hash.GetBytes();
		FinalizeHasher.Update(LocalHashData, sizeof(HashDigestType::ByteArray));
	}

	// Stores a BLAKE3-160 hash, taken from the first 20 bytes of a BLAKE3-256 hash
	return FIoHash(FinalizeHasher.Finalize());
}

const TSet<const UObject*>& FKeyBuilder::GetDependencies() const
{
	return Dependencies;
}

// to keep the key generation lightweight, we don't hash these types
bool FKeyBuilder::IsExcludedType(class UObject* Object)
{
	return
		nullptr != Cast<UAnimationModifier>(Object);
}

// to keep the key generation lightweight, we hash only the full names for these types. Object(s) will be added to Dependencies
bool FKeyBuilder::IsAddNameOnlyType(class UObject* Object)
{
	return
		nullptr != Cast<IAnimationDataModel>(Object) ||
		nullptr != Cast<UActorComponent>(Object) ||
		nullptr != Cast<UAnimBoneCompressionSettings>(Object) ||
		nullptr != Cast<UAnimCurveCompressionSettings>(Object) ||
		nullptr != Cast<UAssetImportData>(Object) ||
		nullptr != Cast<UFunction>(Object) ||
		nullptr != Cast<USkeletalMesh>(Object) ||
		nullptr != Cast<UStreamableRenderAsset>(Object);
}

bool FKeyBuilder::ValidateAgainst(const FKeyBuilder& Other) const
{
	if (bAnyAssetNotReady != Other.bAnyAssetNotReady)
	{
		return false;
	}

	if (Dependencies.Num() != Other.Dependencies.Num())
	{
		return false;
	}

	for (TSet<const UObject*>::TConstIterator Iter = Dependencies.CreateConstIterator(); Iter; ++Iter)
	{
		if (!Other.Dependencies.Contains(*Iter))
		{
			return false;
		}
	}

	if (ObjectsToSerialize.Num() != Other.ObjectsToSerialize.Num())
	{
		return false;
	}
	
	for (int32 Index = 0; Index < ObjectsToSerialize.Num(); ++Index)
	{
		if (ObjectsToSerialize[Index] != Other.ObjectsToSerialize[Index])
		{
			return false;
		}
	}

	if (LocalPartialKeyHashes.Num() != Other.LocalPartialKeyHashes.Num())
	{
		return false;
	}
	
	for (int32 Index = 0; Index < LocalPartialKeyHashes.Num(); ++Index)
	{
		if (LocalPartialKeyHashes[Index].Hash != Other.LocalPartialKeyHashes[Index].Hash)
		{
			return false;
		}

		if (LocalPartialKeyHashes[Index].Object != Other.LocalPartialKeyHashes[Index].Object)
		{
			return false;
		}
	}

	return true;
}

} // namespace UE::PoseSearch	

#endif // WITH_EDITOR