// Copyright Epic Games, Inc. All Rights Reserved.

#include "RedirectionSummary.h"

#if WITH_EDITOR

#include "Misc/ScopeRWLock.h"

FRedirectionSummary::FRedirectionSummary(FRedirectionSummary&& Other)
{
	*this = MoveTemp(Other);
}

FRedirectionSummary& FRedirectionSummary::operator=(FRedirectionSummary&& Other)
{
	if (&Other == this)
	{
		return *this;
	}

	// Technical correctness for holding multiple critical sections: enter the locks in the same order
	// from all possible callsites, in this case, from two calls to operator=.
	TOptional<FWriteScopeLock> ScopeA;
	TOptional<FWriteScopeLock> ScopeB;
	if (&Other < this)
	{
		ScopeA.Emplace(Other.Lock);
		ScopeB.Emplace(Lock);
	}
	else
	{
		ScopeA.Emplace(Lock);
		ScopeB.Emplace(Other.Lock);
	}

	RedirectsForPackage = MoveTemp(Other.RedirectsForPackage);
	GlobalRedirects = MoveTemp(Other.GlobalRedirects);
	Other.RedirectsForPackage.Empty();
	Other.GlobalRedirects.Empty();

	return *this;
}

void FRedirectionSummary::Add(const FCoreRedirect& CoreRedirect, bool bIsWildcardMatch)
{
	FWriteScopeLock ScopeLock(Lock);

	if (bIsWildcardMatch || CoreRedirect.OldName.PackageName.IsNone())
	{
		GlobalRedirects.Add(FCoreRedirect(CoreRedirect));
	}
	else
	{
		for (FName PackageName : GetAffectedPackages(CoreRedirect))
		{
			RedirectsForPackage.FindOrAdd(PackageName).Add(FCoreRedirect(CoreRedirect));
		}
	}
}

void FRedirectionSummary::Remove(const FCoreRedirect& CoreRedirect, bool bIsWildcardMatch)
{
	FWriteScopeLock ScopeLock(Lock);

	if (bIsWildcardMatch || CoreRedirect.OldName.PackageName.IsNone())
	{
		GlobalRedirects.Remove(CoreRedirect);
	}
	else
	{
		for (FName PackageName : GetAffectedPackages(CoreRedirect))
		{
			FRedirectContainer* Container = RedirectsForPackage.Find(PackageName);
			if (Container)
			{
				Container->Remove(CoreRedirect);
				if (Container->IsEmpty())
				{
					RedirectsForPackage.Remove(PackageName);
				}
			}
		}
	}
}

void FRedirectionSummary::AppendHashAffectingPackages(FBlake3& Hasher, TConstArrayView<FName> PackageNames)
{
	if (PackageNames.IsEmpty())
	{
		return;
	}

	int32 Index = -1;
	const FName* PackageNamesData = PackageNames.GetData();
	int32 PackageNamesNum = PackageNames.Num();
	bool bNeedWriteLock = false;

	{
		FReadScopeLock ScopeLock(Lock);
		if (GlobalRedirects.TryAppendHashInReadLock(Hasher))
		{
			Index = 0;
		}
		else
		{
			bNeedWriteLock = true;
		}
		while (!bNeedWriteLock && Index < PackageNamesNum)
		{
			FRedirectContainer* Container = RedirectsForPackage.Find(PackageNamesData[Index]);
			if (!Container || Container->TryAppendHashInReadLock(Hasher))
			{
				++Index;
			}
			else
			{
				bNeedWriteLock = true;
			}
		}
	}
	if (!bNeedWriteLock)
	{
		return;
	}

	FWriteScopeLock WriteLock(Lock);
	if (Index < 0)
	{
		GlobalRedirects.AppendHashInWriteLock(Hasher);
		Index = 0;
	}
	while (Index < PackageNamesNum)
	{
		FRedirectContainer* Container = RedirectsForPackage.Find(PackageNamesData[Index]);
		if (Container)
		{
			Container->AppendHashInWriteLock(Hasher);
		}
		++Index;
	}
}

void FRedirectionSummary::AppendHashGlobal(FBlake3& Hasher)
{
	{
		FReadScopeLock ScopeLock(Lock);
		if (GlobalRedirects.TryAppendHashInReadLock(Hasher))
		{
			return;
		}
	}
	FWriteScopeLock WriteLock(Lock);
	GlobalRedirects.AppendHashInWriteLock(Hasher);
}

void FRedirectionSummary::FRedirectContainer::Add(FCoreRedirect&& Redirect)
{
	Redirects.Add(MoveTemp(Redirect));
	bHashDirty = true;
}

void FRedirectionSummary::FRedirectContainer::Remove(const FCoreRedirect& Redirect)
{
	Redirects.Remove(Redirect);
	bHashDirty = true;
}

bool FRedirectionSummary::FRedirectContainer::IsEmpty() const
{
	return Redirects.IsEmpty();
}

void FRedirectionSummary::FRedirectContainer::Empty()
{
	Redirects.Empty();
	Hash = FBlake3Hash();
	bHashDirty = false;
}

bool FRedirectionSummary::FRedirectContainer::TryAppendHashInReadLock(FBlake3& Hasher) const
{
	if (bHashDirty)
	{
		return false;
	}
	AppendHashWithoutDirtyCheck(Hasher);
	return true;
}

void FRedirectionSummary::FRedirectContainer::AppendHashInWriteLock(FBlake3& Hasher)
{
	if (bHashDirty)
	{
		CalculateHash();
		bHashDirty = false;
	}
	AppendHashWithoutDirtyCheck(Hasher);
}

void FRedirectionSummary::FRedirectContainer::CalculateHash()
{
	FBlake3 Hasher;
	for (const TPair<FCoreRedirect,bool>& Pair: Redirects)
	{
		const FCoreRedirect& Redirect = Pair.Key;
		Redirect.AppendHash(Hasher);
	}
	Hash = Hasher.Finalize();
}

void FRedirectionSummary::FRedirectContainer::AppendHashWithoutDirtyCheck(FBlake3& Hasher) const
{
	Hasher.Update(&Hash.GetBytes(), sizeof(Hash.GetBytes()));
}

TArray<FName, TInlineAllocator<2>> FRedirectionSummary::GetAffectedPackages(const FCoreRedirect& Redirect)
{
	// Note on why we need the redirects that redirect both from and to the package:
	// When a redirector from X to Y changes to redirect from X to Z, package A that references X will now need to
	// write Z instead of Y into its saved imports.
	// When a redirector from X to Y changes to redirect from W to Y, or is deleted, package A that references X will
	// now need to write X instead of Y into its saved imports.
	TArray<FName, TInlineAllocator<2>> Result;
	if (!Redirect.OldName.PackageName.IsNone())
	{
		Result.Add(Redirect.OldName.PackageName);
	}
	if (!Redirect.NewName.PackageName.IsNone() && Redirect.NewName.PackageName != Redirect.OldName.PackageName)
	{
		Result.Add(Redirect.NewName.PackageName);
	}
	return Result;
}

bool FRedirectionSummary::FCompareRedirect::operator()(const FCoreRedirect& A, const FCoreRedirect& B)
{
	return A.Compare(B) < 0;
}

#endif