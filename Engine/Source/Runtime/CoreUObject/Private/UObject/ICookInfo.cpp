// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/ICookInfo.h"

#if WITH_EDITOR


namespace UE::Cook
{

const TCHAR* LexToString(EInstigator Value)
{
	switch (Value)
	{
#define EINSTIGATOR_VALUE_CALLBACK(Name, bAllowUnparameterized) case EInstigator::Name: return TEXT(#Name);
		EINSTIGATOR_VALUES(EINSTIGATOR_VALUE_CALLBACK)
#undef EINSTIGATOR_VALUE_CALLBACK
	default: return TEXT("OutOfRangeCategory");
	}
}

FString FInstigator::ToString() const
{
	TStringBuilder<256> Result;
	Result << LexToString(Category);
	if (!Referencer.IsNone())
	{
		Result << TEXT(": ") << Referencer;
	}
	else
	{
		bool bCategoryAllowsUnparameterized = false;
		switch (Category)
		{
#define EINSTIGATOR_VALUE_CALLBACK(Name, bAllowUnparameterized) case EInstigator::Name: bCategoryAllowsUnparameterized = bAllowUnparameterized; break;
			EINSTIGATOR_VALUES(EINSTIGATOR_VALUE_CALLBACK)
#undef EINSTIGATOR_VALUE_CALLBACK
		default: break;
		}
		if (!bCategoryAllowsUnparameterized)
		{
			Result << TEXT(": <NoReferencer>");
		}
	}
	return FString(Result);
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS;
FCookInfoEvent FDelegates::CookByTheBookStarted;
FCookInfoEvent FDelegates::CookByTheBookFinished;
PRAGMA_ENABLE_DEPRECATION_WARNINGS;
FCookInfoEvent FDelegates::CookStarted;
FCookInfoEvent FDelegates::CookFinished;
FValidateSourcePackage FDelegates::ValidateSourcePackage;

const TCHAR* GetReferencedSetFilename()
{
	return TEXT("ReferencedSet.txt");
}

}

static thread_local ECookLoadType GCookLoadType = ECookLoadType::Unexpected;

FCookLoadScope::FCookLoadScope(ECookLoadType ScopeType)
	: PreviousScope(GCookLoadType)
{
	GCookLoadType = ScopeType;
}

FCookLoadScope::~FCookLoadScope()
{
	GCookLoadType = PreviousScope;
}

ECookLoadType FCookLoadScope::GetCurrentValue()
{
	return GCookLoadType;
}

#endif
