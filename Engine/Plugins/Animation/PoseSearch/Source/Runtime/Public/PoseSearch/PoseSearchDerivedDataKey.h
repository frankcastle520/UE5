// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "Hash/Blake3.h"
#include "IO/IoHash.h"
#include "Serialization/ArchiveUObject.h"
#include "UObject/UnrealType.h"

namespace UE::PoseSearch
{

class FPartialKeyHashes;

class POSESEARCH_API FKeyBuilder : public FArchiveUObject
{
public:
	using Super = FArchiveUObject;
	using HashDigestType = FBlake3Hash;
	using HashBuilderType = FBlake3;

	inline static const FName ExcludeFromHashName = FName(TEXT("ExcludeFromHash"));
	inline static const FName NeverInHashName = FName(TEXT("NeverInHash"));
	inline static const FName IgnoreForMemberInitializationTestName = FName(TEXT("IgnoreForMemberInitializationTest"));
	
	FKeyBuilder();
	FKeyBuilder(const UObject* Object, bool bUseDataVer, bool bPerformConditionalPostLoadIfRequired);
	
	// Experimental, this feature might be removed without warning, not for production use
	enum EDebugPartialKeyHashesMode
	{
		Use,
		DoNotUse,
		Validate
	};

	// Experimental, this feature might be removed without warning, not for production use
	FKeyBuilder(const UObject* Object, bool bUseDataVer, bool bPerformConditionalPostLoadIfRequired, FPartialKeyHashes* InPartialKeyHashes, EDebugPartialKeyHashesMode InDebugPartialKeyHashesMode);

	// Experimental, this feature might be removed without warning, not for production use
	bool ValidateAgainst(const FKeyBuilder& Other) const;

	using Super::IsSaving;
	using Super::operator<<;

	// Begin FArchive Interface
	virtual void Seek(int64 InPos) override;
	virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
	virtual void Serialize(void* Data, int64 Length) override;
	virtual FArchive& operator<<(FName& Name) override;
	virtual FArchive& operator<<(class UObject*& Object) override;
	virtual FString GetArchiveName() const override;
	// End FArchive Interface
	
	bool AnyAssetNotReady() const;
	FIoHash Finalize() const;
	const TSet<const UObject*>& GetDependencies() const;

protected:
	// to keep the key generation lightweight, we don't hash these types
	static bool IsExcludedType(class UObject* Object);

	// to keep the key generation lightweight, we hash only the full names for these types. Object(s) will be added to Dependencies
	static bool IsAddNameOnlyType(class UObject* Object);

	HashBuilderType Hasher;

	// Set of objects that have already been serialized
	TSet<const UObject*> Dependencies;

	// Object currently being serialized
	UObject* ObjectBeingSerialized = nullptr;

	// true if some dependent assets are not ready (fully loaded)
	bool bAnyAssetNotReady = false;

	// if true ConditionalPostLoad will be performed on the dependant assets requiring it
	bool bPerformConditionalPostLoad = false;

private:
	// Experimental, this feature might be removed without warning, not for production use
	void SerializeObjectInternal(UObject* Object);

	// Experimental, this feature might be removed without warning, not for production use
	FArchive& TryAddDependency(UObject* Object, bool bAddToPartialKeyHashes);

	// Experimental, this feature might be removed without warning, not for production use
	TArray<UObject*> ObjectsToSerialize;
	
	// Experimental, this feature might be removed without warning, not for production use
	TArray<UObject*> ObjectBeingSerializedDependencies;

	// Experimental, this feature might be removed without warning, not for production use
	struct FLocalPartialKeyHash
	{
		UObject* Object = nullptr;
		HashDigestType Hash;
	};
	
	// Experimental, this feature might be removed without warning, not for production use
	TArray<FLocalPartialKeyHash> LocalPartialKeyHashes;
	
	// Experimental, this feature might be removed without warning, not for production use
	FPartialKeyHashes* PartialKeyHashes = nullptr;

	// Experimental, this feature might be removed without warning, not for production use
	EDebugPartialKeyHashesMode DebugPartialKeyHashesMode = EDebugPartialKeyHashesMode::Use;
};

// Experimental, this feature might be removed without warning, not for production use
class FPartialKeyHashes
{
public:
	struct FEntry
	{
		FKeyBuilder::HashDigestType Hash;
		TArray<TWeakObjectPtr<>> Dependencies;

		bool CheckDependencies(TArrayView<UObject*> OtherDependencies) const
		{
			#if DO_CHECK
			if (Dependencies.Num() != OtherDependencies.Num())
			{
				return false;
			}
			
			for (int32 DependencyIndex = 0; DependencyIndex < Dependencies.Num(); ++DependencyIndex)
			{
				if (!OtherDependencies[DependencyIndex])
				{
					return false;
				}

				// we could have lost a weak pointer here...
				if (Dependencies[DependencyIndex].IsValid())
				{
					if (Dependencies[DependencyIndex].Get() != OtherDependencies[DependencyIndex])
					{
						return false;
					}
				}
			}
			#endif // DO_CHECK

			return true;
		}

	};

	void Reset()
	{
		Entries.Reset();
	}

	void Remove(UObject* Object)
	{
		Entries.Remove(Object);
	}

	void Add(UObject* Object, const FKeyBuilder::HashDigestType& Hash, TArrayView<UObject*> Dependencies)
	{
		check(Object);

		check(!Hash.IsZero());

		if (FEntry* OldEntry = Entries.Find(Object))
		{
			check(OldEntry->Hash == Hash);
			check(OldEntry->CheckDependencies(Dependencies))
		}
		else
		{
			FEntry& NewEntry = Entries.Add(Object);
			NewEntry.Hash = Hash;
			NewEntry.Dependencies = Dependencies;
		}
	}

	const FEntry* Find(UObject* Object)
	{
		check(Object);
		if (const TPair<TWeakObjectPtr<>, FEntry>* Pair = Entries.FindPair(Object))
		{
			// making sure all the TWeakObjectPtr are still valid
			if (!Pair->Key.IsValid())
			{
				Entries.Remove(Object);
				return nullptr;
			}
			
			for (const TWeakObjectPtr<> Dependency : Pair->Value.Dependencies)
			{
				if (!Dependency.IsValid())
				{
					Entries.Remove(Object);
					return nullptr;
				}
			}

			return &Pair->Value;
		}

		return nullptr;
	}

private:
	struct FEntries : public TMap<TWeakObjectPtr<>, FEntry>
	{
		const TPair<TWeakObjectPtr<>, FEntry>* FindPair(TWeakObjectPtr<> Key) const
		{
			return Pairs.Find(Key);
		}
	};

	FEntries Entries;
};

} // namespace UE::PoseSearch

#endif // WITH_EDITOR