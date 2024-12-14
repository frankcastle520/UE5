// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnDemandPackageStoreBackend.h"

#include "Algo/Accumulate.h"
#include "Algo/Find.h"
#include "Async/Mutex.h"
#include "Async/UniqueLock.h"
#include "IO/IoContainerHeader.h"
#include "IO/IoStoreOnDemand.h"
#include "IO/PackageId.h"
#include "Internationalization/PackageLocalizationManager.h"

namespace UE::IoStore
{

///////////////////////////////////////////////////////////////////////////////
class FOnDemandPackageStoreBackend final
	: public IOnDemandPackageStoreBackend
{
	struct FContainer
	{
		FContainer(FString&& ContainerName, FSharedContainerHeader ContainerHeader)
			: Name(MoveTemp(ContainerName))
			, Header(MoveTemp(ContainerHeader))
		{ }

		FString					Name;
		FSharedContainerHeader	Header;
	};

	using FSharedBackendContext = TSharedPtr<const FPackageStoreBackendContext>;
	using FEntryMap				= TMap<FPackageId, const FFilePackageStoreEntry*>;
	using FUniqueContainer		= TUniquePtr<FContainer>;
	using FRedirect				= TTuple<FName, FPackageId>;
	using FLocalizedMap			= TMap<FPackageId, FName>;
	using FRedirectMap			= TMap<FPackageId, FRedirect>;

public:
						FOnDemandPackageStoreBackend();
						virtual ~FOnDemandPackageStoreBackend ();

	virtual FIoStatus	Mount(FString ContainerName, FSharedContainerHeader ContainerHeader) override;
	virtual FIoStatus	Unmount(const FString& ContainerName) override;
	virtual FIoStatus	UnmountAll() override;

	virtual void		OnMounted(TSharedRef<const FPackageStoreBackendContext> Context) override;
	virtual void 		BeginRead() override;
	virtual void 		EndRead() override;

	virtual bool		GetPackageRedirectInfo(
							FPackageId PackageId,
							FName& OutSourcePackageName,
							FPackageId& OutRedirectedToPackageId) override;

	virtual EPackageStoreEntryStatus GetPackageStoreEntry(
							FPackageId PackageId,
							FName PackageName,
							FPackageStoreEntry& OutPackageStoreEntry) override;

private:
	void				UpdateLookupTables();

	FSharedBackendContext		BackendContext;
	TArray<FUniqueContainer>	Containers;
	FEntryMap					EntryMap;
	FLocalizedMap				LocalizedMap;
	FRedirectMap				RedirectMap;
	UE::FMutex					Mutex;
	bool						bNeedsUpdate = false;
};

///////////////////////////////////////////////////////////////////////////////
FOnDemandPackageStoreBackend::FOnDemandPackageStoreBackend()
{
}

FOnDemandPackageStoreBackend::~FOnDemandPackageStoreBackend()
{
}

FIoStatus FOnDemandPackageStoreBackend::Mount(FString ContainerName, FSharedContainerHeader ContainerHeader)
{
	{
		UE::TUniqueLock Lock(Mutex);

		const FUniqueContainer* Existing =
			Algo::FindByPredicate(
				Containers,
				[&ContainerName](const FUniqueContainer& C) { return C->Name == ContainerName; });

		if (Existing != nullptr)
		{
			return EIoErrorCode::Ok;
		}

		Containers.Add(MakeUnique<FContainer>(MoveTemp(ContainerName), MoveTemp(ContainerHeader)));
		bNeedsUpdate = true;
	}

	if (BackendContext)
	{
		BackendContext->PendingEntriesAdded.Broadcast();
	}

	return EIoErrorCode::Ok;
}

FIoStatus FOnDemandPackageStoreBackend::Unmount(const FString& ContainerName)
{
	UE::TUniqueLock Lock(Mutex);
	const int32 NumRemoved = Containers.RemoveAll([&ContainerName](const TUniquePtr<FContainer>& C)
	{
		return C->Name == ContainerName;
	});
	check(NumRemoved <= 1);

	bNeedsUpdate = NumRemoved > 0;
	return NumRemoved == 0 ? EIoErrorCode::NotFound : EIoErrorCode::Ok;
}

FIoStatus FOnDemandPackageStoreBackend::UnmountAll()
{
	UE::TUniqueLock Lock(Mutex);
	Containers.Empty();
	bNeedsUpdate = true;
	return EIoErrorCode::Ok;
}

void FOnDemandPackageStoreBackend::OnMounted(TSharedRef<const FPackageStoreBackendContext> Context)
{
	BackendContext = Context;
}

void FOnDemandPackageStoreBackend::BeginRead()
{
	ensure(Mutex.TryLock());
	UpdateLookupTables();
}

void FOnDemandPackageStoreBackend::EndRead()
{
	Mutex.Unlock();
}

EPackageStoreEntryStatus FOnDemandPackageStoreBackend::GetPackageStoreEntry(
	FPackageId PackageId,
	FName PackageName,
	FPackageStoreEntry& OutPackageStoreEntry)
{
	if (const FFilePackageStoreEntry* Entry = EntryMap.FindRef(PackageId))
	{
		OutPackageStoreEntry.ImportedPackageIds =
			MakeArrayView(Entry->ImportedPackages.Data(), Entry->ImportedPackages.Num());
		OutPackageStoreEntry.ShaderMapHashes =
			MakeArrayView(Entry->ShaderMapHashes.Data(), Entry->ShaderMapHashes.Num());

		return EPackageStoreEntryStatus::Ok;
	}

	return EPackageStoreEntryStatus::Missing;
}

bool FOnDemandPackageStoreBackend::GetPackageRedirectInfo(
	FPackageId PackageId,
	FName& OutSourcePackageName,
	FPackageId& OutRedirectedToPackageId)
{
	if (const FRedirect* Redirect = RedirectMap.Find(PackageId))
	{
		OutSourcePackageName		= Redirect->Key;
		OutRedirectedToPackageId	= Redirect->Value;
		return true;
	}
	
	if (const FName* SourcePkgName = LocalizedMap.Find(PackageId))
	{
		const FName LocalizedPkgName = FPackageLocalizationManager::Get().FindLocalizedPackageName(*SourcePkgName);
		if (LocalizedPkgName.IsNone() == false)
		{
			const FPackageId LocalizedPkgId = FPackageId::FromName(LocalizedPkgName);
			if (EntryMap.Find(LocalizedPkgId))
			{
				OutSourcePackageName		= *SourcePkgName;
				OutRedirectedToPackageId	= LocalizedPkgId;
				return true;
			}
		}
	}

	return false;
}

void FOnDemandPackageStoreBackend::UpdateLookupTables()
{
	if (!bNeedsUpdate)
	{
		return;
	}

	bNeedsUpdate = false;
	check(Mutex.IsLocked());

	EntryMap.Empty();
	LocalizedMap.Empty();
	RedirectMap.Empty();

	const int32 PackageCount = Algo::TransformAccumulate(
		Containers,
		[](const FUniqueContainer& C) { return C->Header->PackageIds.Num(); },
		0);

	EntryMap.Reserve(PackageCount);

	for (const FUniqueContainer& Container : Containers)
	{
		const FIoContainerHeader& Hdr = *Container->Header;
		TConstArrayView<FFilePackageStoreEntry> Entries(
			reinterpret_cast<const FFilePackageStoreEntry*>(Hdr.StoreEntries.GetData()),
			Hdr.PackageIds.Num());

		int32 Idx = 0;
		for (const FFilePackageStoreEntry& Entry : Entries)
		{
			const FPackageId PkgId = Hdr.PackageIds[Idx++];
			EntryMap.Add(PkgId, &Entry);
		}

		for (const FIoContainerHeaderLocalizedPackage& Localized : Hdr.LocalizedPackages)
		{
			FName& SourcePackageName = LocalizedMap.FindOrAdd(Localized.SourcePackageId);
			if (SourcePackageName.IsNone())
			{
				FDisplayNameEntryId NameEntry = Hdr.RedirectsNameMap[Localized.SourcePackageName.GetIndex()];
				SourcePackageName = NameEntry.ToName(Localized.SourcePackageName.GetNumber());
			}
		}

		for (const FIoContainerHeaderPackageRedirect& Redirect : Hdr.PackageRedirects)
		{
			FRedirect& RedirectEntry = RedirectMap.FindOrAdd(Redirect.SourcePackageId);
			FName& SourcePackageName = RedirectEntry.Key;
			if (SourcePackageName.IsNone())
			{
				FDisplayNameEntryId NameEntry = Hdr.RedirectsNameMap[Redirect.SourcePackageName.GetIndex()];
				SourcePackageName = NameEntry.ToName(Redirect.SourcePackageName.GetNumber());
				RedirectEntry.Value = Redirect.TargetPackageId;
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
TSharedPtr<IOnDemandPackageStoreBackend> MakeOnDemandPackageStoreBackend()
{
	return MakeShareable<IOnDemandPackageStoreBackend>(new FOnDemandPackageStoreBackend());
}

} // namespace UE
