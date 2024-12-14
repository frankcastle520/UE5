// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/Map.h"
#include "Containers/SortedMap.h"
#include "HAL/CriticalSection.h"
#include "Hash/Blake3.h"
#include "UObject/CoreRedirects.h"
#include "UObject/NameTypes.h"

/**
 * Container for FCoreRedirects that can affect a package. Used by class FCoreRedirects to implement
 * AppendHashOfRedirectsAffectingPackages.
 */
class FRedirectionSummary
{
public:
	FRedirectionSummary() = default;
	FRedirectionSummary(FRedirectionSummary&& Other);
	FRedirectionSummary& operator=(FRedirectionSummary&& Other);

	void Add(const FCoreRedirect& CoreRedirect, bool bIsWildcardMatch);
	void Remove(const FCoreRedirect& CoreRedirect, bool bIsWildcardMatch);
	void AppendHashAffectingPackages(FBlake3& Hasher, TConstArrayView<FName> PackageNames);
	void AppendHashGlobal(FBlake3& Hasher);

private:
	struct FCompareRedirect
	{
		bool operator()(const FCoreRedirect& A, const FCoreRedirect& B);
	};
	struct FRedirectContainer
	{
	public:
		void Add(FCoreRedirect&& Redirect);
		void Remove(const FCoreRedirect& Redirect);
		bool IsEmpty() const;
		void Empty();
		bool TryAppendHashInReadLock(FBlake3& Hasher) const;
		void AppendHashInWriteLock(FBlake3& Hasher);
	private:
		void CalculateHash();
		void AppendHashWithoutDirtyCheck(FBlake3& Hasher) const;
	private:
		TSortedMap<FCoreRedirect, bool, FDefaultAllocator, FCompareRedirect> Redirects;
		FBlake3Hash Hash;
		bool bHashDirty = false;
	};

private:
	static TArray<FName, TInlineAllocator<2>> GetAffectedPackages(const FCoreRedirect& Redirect);

private:
	TMap<FName, FRedirectContainer> RedirectsForPackage;
	FRedirectContainer GlobalRedirects;
	/**
	 * CoreRedirects are written when the engine is single threaded, but we do not write the hashes for the
	 * global and per-package containers until they are requested on demand. The requests on demand can
	 * occur on multiple threads, so we need a lock to ensure that the first thread that requests them and finds
	 * them dirty does not cause a data race with the second thread requests them.
	 */
	FRWLock Lock;
};

#endif