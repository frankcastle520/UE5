// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/CoreRedirects.h"

#include "UObject/Linker.h"
#include "UObject/Package.h"
#include "UObject/PropertyHelper.h"
#include "UObject/TopLevelAssetPath.h"
#include "UObject/UnrealType.h"

#include "Algo/Compare.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Hash/Blake3.h"
#include "Logging/StructuredLog.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeRWLock.h"
#include "RedirectionSummary.h"
#include "Serialization/DeferredMessageLog.h"
#include "Templates/Casts.h"

#include "AutoRTFM/AutoRTFM.h"

#if WITH_EDITOR
#include "Misc/RedirectCollector.h"
#endif

DEFINE_LOG_CATEGORY(LogCoreRedirects);

#if !defined UE_WITH_CORE_REDIRECTS
#	define UE_WITH_CORE_REDIRECTS 1
#endif

#define COREREDIRECT_STATS 0
#if COREREDIRECT_STATS
static int sSubstringLookups = 0;
static int sSubstringPredictHit = 0;
static int sSubstringPredictMiss = 0;
FAutoConsoleCommand StaticLoadTrackerDump(TEXT("CoreRedirects.SubstringPrediction.StatsDump"), TEXT(""), FConsoleCommandDelegate::CreateLambda([]()
	{
		UE_LOG(LogCoreRedirects, Log, TEXT(
			"==========================================\n"
			"Dumping Substring Stats\n\n"
			"Lookups:\t\t\t\t%d\n"
			"Prediction Hit:\t\t%d\n"
			"Prediction Miss:\t\t%d\n"
			"=========================================="
		), sSubstringLookups, sSubstringPredictHit, sSubstringPredictMiss);
	}));

#define COREREDIRECT_STATS_UPDATE_PREDICTION_LOOKUP() do{ sSubstringLookups++; }while(false)
#define COREREDIRECT_STATS_UPDATE_PREDICTION_RESULT(bFound) do{ if(bFound){ sSubstringPredictHit++; } else { sSubstringPredictMiss++; }}while(false)
#else
#define COREREDIRECT_STATS_UPDATE_PREDICTION_LOOKUP()
#define COREREDIRECT_STATS_UPDATE_PREDICTION_RESULT(bFound)
#endif

/* 
* Small helper class to deal with the string representation of FNames while trying to minimize copies 
* without relying on internal details of FName. Due to the sheer volume of CoreRedirects, it's 
* significant to avoid the copies where possible. We maintain UTF8 encoding for FNames when dealing with Wildcard
* redirects since UTF8 overlays ASCII allowing pure ASCII strings to be compact, greatly improving PredictMatch efficiency.
* We also need a common encoding for how we store substring redirects and how we query them to ensure we do not generate
* false negatives in our fuzzy searchs using PredictMatch; UTF8 will allow us to still predict non-ASCII substrings.
*/
namespace UE::CoreRedirects::Private
{
	struct FNameUtf8String
	{
		FNameUtf8String() : UTF8() {}
		~FNameUtf8String() {}
		FNameUtf8String(FName Name)
			: UTF8()
		{
			// FName will append "None" when FName.IsNone. Instead treat empty string as None
			if (!Name.IsNone())
			{
				Name.AppendString(UTF8);
			}
		}

		operator FUtf8StringView() const
		{ 
			return UTF8; 
		}

		bool IsNone() const
		{
			return Len() == 0;
		}

		const UTF8CHAR* GetData() const
		{
			return UTF8.GetData();
		}

		int32 Len() const
		{
			return UTF8.Len();
		}

		int32 ByteLength() const
		{
			return UTF8.Len() * sizeof(UTF8CHAR);
		}

		bool StartsWith(const FUtf8StringView InPrefix, ESearchCase::Type InSearchCase) const
		{
			return UTF8.ToView().StartsWith(InPrefix, InSearchCase);
		}

		bool EndsWith(const FUtf8StringView InSuffix, ESearchCase::Type InSearchCase) const
		{
			return UTF8.ToView().EndsWith(InSuffix, InSearchCase);
		}

		bool Contains(const FUtf8StringView InSubstring, ESearchCase::Type InSearchCase) const
		{
			return (INDEX_NONE != UE::String::FindFirst(UTF8, InSubstring, InSearchCase));
		}

		void ReplaceAt(int32 Pos, int32 RemoveLen, FNameUtf8String& Str)
		{
			UTF8.ReplaceAt(Pos, RemoveLen, Str);
		}

		void RightChopInline(int Len)
		{
			UTF8.RemoveAt(0, Len);
		}

		TUtf8StringBuilder<FName::StringBufferSize> UTF8;
	};

	struct FCoreRedirectObjectUtf8Name
	{
		FCoreRedirectObjectUtf8Name(const FCoreRedirectObjectName& InName)
			: ObjectName(InName.ObjectName)
			, OuterName(InName.OuterName)
			, PackageName(InName.PackageName)
		{
		}

		FNameUtf8String ObjectName;
		FNameUtf8String OuterName;
		FNameUtf8String PackageName;
	};

	struct SubstringMatcher
	{
		static bool Matches(const FName LHS, const FNameUtf8String& RHSString, bool bPartialLHS, bool bPartialRHS)
		{
			const bool bLHSIsNone = LHS.IsNone();
			const bool bRHSIsNone = RHSString.IsNone();
			if (bLHSIsNone || bRHSIsNone)
			{
				return bLHSIsNone == bRHSIsNone || ((!bLHSIsNone || bPartialLHS) && (!bRHSIsNone || bPartialRHS));
			}

			FNameUtf8String LHSString(LHS);
			return RHSString.Contains(LHSString, ESearchCase::IgnoreCase);
		}
	};

	struct PrefixMatcher
	{
		static bool Matches(const FName LHS, const FNameUtf8String& RHSString, bool bPartialLHS, bool bPartialRHS)
		{
			const bool bLHSIsNone = LHS.IsNone();
			const bool bRHSIsNone = RHSString.IsNone();
			if (bLHSIsNone || bRHSIsNone)
			{
				return bLHSIsNone == bRHSIsNone || ((!bLHSIsNone || bPartialLHS) && (!bRHSIsNone || bPartialRHS));
			}

			FNameUtf8String LHSString(LHS);
			return RHSString.StartsWith(LHSString, ESearchCase::IgnoreCase);
		}
	};

	struct SuffixMatcher
	{
		static bool Matches(const FName LHS, const FNameUtf8String& RHSString, bool bPartialLHS, bool bPartialRHS)
		{
			const bool bLHSIsNone = LHS.IsNone();
			const bool bRHSIsNone = RHSString.IsNone();
			if (bLHSIsNone || bRHSIsNone)
			{
				return bLHSIsNone == bRHSIsNone || ((!bLHSIsNone || bPartialLHS) && (!bRHSIsNone || bPartialRHS));
			}

			FNameUtf8String LHSString(LHS);
			return RHSString.EndsWith(LHSString, ESearchCase::IgnoreCase);
		}
	};

	template <typename Matcher>
	static bool MatchWildcardRedirect(const FCoreRedirectObjectName& InRedirect, 
		const FCoreRedirectObjectUtf8Name& InUtf8Name, bool bPartialLHS, bool bPartialRHS)
	{
		if (!Matcher::Matches(InRedirect.ObjectName, InUtf8Name.ObjectName, bPartialLHS, bPartialRHS))
		{
			return false;
		}
		if (!Matcher::Matches(InRedirect.OuterName, InUtf8Name.OuterName, bPartialLHS, bPartialRHS))
		{
			return false;
		}
		if (!Matcher::Matches(InRedirect.PackageName, InUtf8Name.PackageName, bPartialLHS, bPartialRHS))
		{
			return false;
		}
		return true;
	}

	static bool MatchSubstring(const FCoreRedirectObjectName& InSubstringRedirect, const FCoreRedirectObjectUtf8Name& InUtf8Name,
		bool bPartialLHS, bool bPartialRHS)
	{
		return MatchWildcardRedirect<SubstringMatcher>(InSubstringRedirect, InUtf8Name, bPartialLHS, bPartialRHS);
	}

	static bool MatchPrefix(const FCoreRedirectObjectName& InPrefixRedirect, const FCoreRedirectObjectUtf8Name& InUtf8Name,
		bool bPartialLHS, bool bPartialRHS)
	{
		return MatchWildcardRedirect<PrefixMatcher>(InPrefixRedirect, InUtf8Name, bPartialLHS, bPartialRHS);
	}

	static bool MatchSuffix(const FCoreRedirectObjectName& InSuffixRedirect, const FCoreRedirectObjectUtf8Name& InUtf8Name,
		bool bPartialLHS, bool bPartialRHS)
	{
		return MatchWildcardRedirect<SuffixMatcher>(InSuffixRedirect, InUtf8Name, bPartialLHS, bPartialRHS);
	}


	bool RunAssetRedirectTests()
	{
		bool bSuccess = true;

		// Test
		TMap<FSoftObjectPath, FSoftObjectPath> Redirects;

		struct FTestDefinition
		{
			const TCHAR* Origin = nullptr;
			const TCHAR* Destination = nullptr;
			const TCHAR* TestDescription = nullptr;
			ECoreRedirectFlags RedirectFlags = ECoreRedirectFlags::None;
			bool ExpectTrue = true;
		};
		TArray<FTestDefinition> Tests;

		// Simple single redirection BasicName --> BasicNewName
		{
			FTestDefinition& BasicTest = Tests.AddDefaulted_GetRef();
			BasicTest.Origin = TEXT("/Game/BasicName.BasicName");
			BasicTest.Destination = TEXT("/Game/BasicNewName.BasicNewName");
			BasicTest.TestDescription = TEXT("basic asset redirect with Type_Object");
			BasicTest.RedirectFlags = ECoreRedirectFlags::Type_Object;
			BasicTest.ExpectTrue = true;

			{
				FSoftObjectPath SourcePath(BasicTest.Origin);
				FSoftObjectPath DestPath(BasicTest.Destination);
				Redirects.Add(SourcePath, DestPath);
			}
		}

		// Multi-asset package redirections. bp_orig_name + _C + Default --> bp_new_name + _C + Default
		{
			FTestDefinition& DefaultObjectTest = Tests.AddDefaulted_GetRef();
			DefaultObjectTest.Origin = TEXT("/Plugin/bp_orig_name.Default__bp_orig_name_C");
			DefaultObjectTest.Destination = TEXT("/Plugin/bp_new_name.Default__bp_new_name_C");
			DefaultObjectTest.TestDescription = TEXT("default object asset redirect with Type_Object");
			DefaultObjectTest.RedirectFlags = ECoreRedirectFlags::Type_Object;
			DefaultObjectTest.ExpectTrue = true;

			FTestDefinition& BPGCTest = Tests.AddDefaulted_GetRef();
			BPGCTest.Origin = TEXT("/Plugin/bp_orig_name.bp_orig_name_C");
			BPGCTest.Destination = TEXT("/Plugin/bp_new_name.bp_new_name_C");
			BPGCTest.TestDescription = TEXT("default BPGC asset redirect with Type_Class");
			BPGCTest.RedirectFlags = ECoreRedirectFlags::Type_Class;
			BPGCTest.ExpectTrue = true;

			FTestDefinition& InstanceOnlyTest = Tests.AddDefaulted_GetRef();
			InstanceOnlyTest.Origin = BPGCTest.Origin;
			InstanceOnlyTest.Destination = BPGCTest.Destination;
			InstanceOnlyTest.RedirectFlags = ECoreRedirectFlags::Type_Class | ECoreRedirectFlags::Category_InstanceOnly;
			InstanceOnlyTest.TestDescription = TEXT("category instance only");
			InstanceOnlyTest.ExpectTrue = false;

			{
				FSoftObjectPath SourcePath(TEXT("/Plugin/bp_orig_name.Default__bp_orig_name_C"));
				FSoftObjectPath DestPath(TEXT("/Plugin/bp_new_name.Default__bp_new_name_C"));
				Redirects.Add(SourcePath, DestPath);
			}

			{
				FSoftObjectPath SourcePath(TEXT("/Plugin/bp_orig_name.bp_orig_name_C"));
				FSoftObjectPath DestPath(TEXT("/Plugin/bp_new_name.bp_new_name_C"));
				Redirects.Add(SourcePath, DestPath);
			}

			{
				FSoftObjectPath SourcePath(TEXT("/Plugin/bp_orig_name.bp_orig_name"));
				FSoftObjectPath DestPath(TEXT("/Plugin/bp_new_name.bp_new_name"));
				Redirects.Add(SourcePath, DestPath);
			}
		}

		FCoreRedirects::AddAssetRedirects(Redirects);

		if (!FCoreRedirects::ValidateAssetRedirects())
		{
			bSuccess = false;
			UE_LOG(LogCoreRedirects, Error, TEXT("Failed asset redirect validation."));
		}

		for (auto& Test : Tests)
		{
			FCoreRedirectObjectName OldName(Test.Origin);
			FCoreRedirectObjectName NewName = FCoreRedirects::GetRedirectedName(Test.RedirectFlags, OldName);
			if (NewName.ToString().Equals(Test.Destination) != Test.ExpectTrue)
			{
				bSuccess = false;
				UE_LOG(LogCoreRedirects, Error, TEXT("Failed %s. Source = %s was unexpectedly redirected to %s"),
					Test.TestDescription, Test.Origin, *NewName.ToString());
			}
			if (Test.ExpectTrue)
			{
				TArray<FCoreRedirectObjectName> OldNames;
				if (!FCoreRedirects::FindPreviousNames(Test.RedirectFlags, NewName, OldNames))
				{
					bSuccess = false;
					UE_LOG(LogCoreRedirects, Error, TEXT("Failed to FindPreviousNames for %s"), Test.Destination);
				}
				else
				{
					bool bContainsName = false;
					for (const FCoreRedirectObjectName& ReverseOldName : OldNames)
					{
						if (ReverseOldName.ToString().Compare(Test.Origin))
						{
							bContainsName = true;
							break;
						}
					}
					if (!bContainsName)
					{
						bSuccess = false;
						UE_LOG(LogCoreRedirects, Error, TEXT("Failed to find expected previous name for %s"), Test.Destination);
					}
				}
			}
		}

		// Remove all redirects temporarily and verify no test finds a redirection
		FCoreRedirects::RemoveAllAssetRedirects();
		for (auto& Test : Tests)
		{
			FCoreRedirectObjectName OldName(Test.Origin);
			FCoreRedirectObjectName NewName = FCoreRedirects::GetRedirectedName(Test.RedirectFlags, OldName);
			if (!NewName.ToString().Equals(Test.Origin))
			{
				bSuccess = false;
				UE_LOG(LogCoreRedirects, Error, TEXT("Found unexpected redirect from %s to %s"),
					Test.Origin, *NewName.ToString());
			}
		}

#if WITH_EDITOR
		// ensure that it's safe to add any redirector in GRedirectCollector
		{
			TDynamicUniqueLock<FCriticalSection> ScopeLock(GRedirectCollector.AcquireLock());
			const TMap<FSoftObjectPath, FSoftObjectPath>& RedirectMap = GRedirectCollector.GetObjectPathRedirectionMapUnderLock(ScopeLock);
			TArray<const FCoreRedirect> RedirectList;
			for (const TPair<FSoftObjectPath, FSoftObjectPath>& Redirect : RedirectMap)
			{
				RedirectList.Emplace(ECoreRedirectFlags::Type_Asset, Redirect.Key.ToString(), Redirect.Value.ToString());
			}

			FCoreRedirects::AddRedirectList(RedirectList, TEXT("GRedirectCollector"));
			if (!FCoreRedirects::ValidateAssetRedirects())
			{
				bSuccess = false;
				UE_LOG(LogCoreRedirects, Error, TEXT("Failed asset redirect validation."));
			}
		}
		FCoreRedirects::RemoveAllAssetRedirects();
#endif

		// Ensure validation failure if chains exist
		{
			TMap<FSoftObjectPath, FSoftObjectPath> ChainRedirects;

			{
				FSoftObjectPath SourcePath(TEXT("/Game/Chain_FirstName.Chain_FirstName"));
				FSoftObjectPath DestPath(TEXT("/Game/Chain_SecondName.Chain_SecondName"));
				ChainRedirects.Add(SourcePath, DestPath);
			}

			{
				FSoftObjectPath SourcePath(TEXT("/Game/Chain_SecondName.Chain_SecondName"));
				FSoftObjectPath DestPath(TEXT("/Game/Chain_ThirdName.Chain_ThirdName"));
				ChainRedirects.Add(SourcePath, DestPath);
			}

			{
				FSoftObjectPath SourcePath(TEXT("/Game/Chain_ThirdName.Chain_ThirdName"));
				FSoftObjectPath DestPath(TEXT("/Game/Chain_FourthName.Chain_FourthName"));
				ChainRedirects.Add(SourcePath, DestPath);
			}

			FCoreRedirects::AddAssetRedirects(ChainRedirects);
			if (FCoreRedirects::ValidateAssetRedirects())
			{
				bSuccess = false;
				UE_LOG(LogCoreRedirects, Error, TEXT("Failed to detect erroneous chained redirect in ValidateAssetRedirects()"));
			}
			FCoreRedirects::RemoveAllAssetRedirects();
		}

		// Re-add the redirects so that they are in place for subsequent tests. This can help
		// detect unexpected interactions between asset redirects and legacy types of redirects
		FCoreRedirects::AddAssetRedirects(Redirects);

		return bSuccess;
	}
}

#if WITH_EDITOR
FRedirectionSummary GRedirectionSummary;
#endif

FCoreRedirectObjectName::FCoreRedirectObjectName(const FTopLevelAssetPath& TopLevelAssetPath)
	: FCoreRedirectObjectName(TopLevelAssetPath.GetAssetName(), NAME_None, TopLevelAssetPath.GetPackageName())
{
}

FCoreRedirectObjectName::FCoreRedirectObjectName(const FSoftObjectPath& SoftObjectPath)
{
	if (!SoftObjectPath.IsSubobject())
	{
		PackageName = SoftObjectPath.GetLongPackageFName();
		ObjectName = SoftObjectPath.GetAssetFName();
	}
	else
	{
		if (!ExpandNames(SoftObjectPath.ToString(), ObjectName, OuterName, PackageName))
		{
			Reset();
		}
	}
}

FCoreRedirectObjectName::FCoreRedirectObjectName(const FString& InString)
{
	if (!ExpandNames(InString, ObjectName, OuterName, PackageName))
	{
		Reset();
	}
}

FCoreRedirectObjectName::FCoreRedirectObjectName(const class UObject* Object)
{
	// This is more efficient than going to path string and back to FNames
	if (Object)
	{	
		UObject* Outer = Object->GetOuter();

		if (!Outer)
		{
			PackageName = Object->GetFName();
			// This is a package
		}
		else
		{
			FString OuterString;
			ObjectName = Object->GetFName();

			// Follow outer chain,
			while (Outer)
			{
				UObject* NextOuter = Outer->GetOuter();
				if (!NextOuter)
				{
					OuterName = FName(*OuterString);
					PackageName = Outer->GetFName();
					break;
				}
				if (!OuterString.IsEmpty())
				{
					OuterString.AppendChar(TEXT('.'));
				}
				OuterString.Append(Outer->GetName());

				Outer = NextOuter;
			}
		}
	}
}

FString FCoreRedirectObjectName::ToString() const
{
	return CombineNames(ObjectName, OuterName, PackageName);
}

void FCoreRedirectObjectName::Reset()
{
	ObjectName = OuterName = PackageName = NAME_None;
}

void FCoreRedirects::FWildcardData::Add(const FCoreRedirect& Redirect)
{
	if (Redirect.IsSubstringMatch())
	{
		Substrings.Add(Redirect);
		AddPredictionWords(Redirect);
	}
	else if (Redirect.IsPrefixMatch())
	{
		Prefixes.Add(Redirect);
	}
	else
	{
		check(Redirect.IsSuffixMatch());
		Suffixes.Add(Redirect);
	}
}

void FCoreRedirects::FWildcardData::AddPredictionWords(const FCoreRedirect& Redirect)
{
	using namespace UE::CoreRedirects::Private;

	const FName Names[3] = { Redirect.OldName.ObjectName, Redirect.OldName.OuterName, Redirect.OldName.PackageName };
	for (const FName& Name : Names)
	{
		if (Name.IsNone())
		{
			continue;
		}

		FNameUtf8String NameView(Name);

		// Since we only predict based on a small window of characters (8), 
		// try to use characters that will be more unique by removing common prefixes.
		// Note, this does not affect the actual substring matching, only
		// our prediction and more specifically our false positive rate.
		FUtf8StringView CommonPrefixes[] = 
		{ "/", "Script/", "Temp/", "Extra/", "Memory/", "Config/", "Game/", "Engine/", "Transient/", "Niagara/"};

		for (FUtf8StringView Prefix : CommonPrefixes)
		{
			// Only remove the prefix if doing so won't leave the string empty.
			// To consider: To keep entropy high, we might want to consider only performing the 
			// removal if we leave 'n' number of characers for our prediction.
			if (NameView.Len() > Prefix.Len() && NameView.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				NameView.RightChopInline(Prefix.Len());
			}
		}

		// Predict match doesn't interpret character data, however in order to avoid false negatives
		// we need to ensure all PredictionWords are added in the same encoding we will be using
		// for comparisons in MatchSubstringApproximate
		PredictMatch.AddPredictionWord((uint8*) NameView.GetData(), (uint16) NameView.ByteLength());
	}
}

bool FCoreRedirects::FWildcardData::MatchSubstringApproximate(const UE::CoreRedirects::Private::FCoreRedirectObjectUtf8Name& InUtf8Name) const
{
	using namespace UE::CoreRedirects::Private;

	const FNameUtf8String* Names[3] = { &InUtf8Name.ObjectName, &InUtf8Name.OuterName, &InUtf8Name.PackageName };
	for (const FNameUtf8String* Name : Names)
	{
		if (Name->IsNone())
		{
			continue;
		}

		if (PredictMatch.MatchApproximate((const uint8*) Name->GetData(), (uint16) Name->ByteLength()))
		{
			return true;
		}
	}
	return false;
}

void FCoreRedirects::FWildcardData::Rebuild()
{
	PredictMatch.Reset();

	for (const FCoreRedirect& Redirect : Substrings)
	{
		AddPredictionWords(Redirect);
	}
}

bool FCoreRedirects::FWildcardData::Matches(ECoreRedirectFlags InFlags, const FCoreRedirectObjectName& InName, 
	ECoreRedirectMatchFlags InMatchFlags, TArray<const FCoreRedirect*>& OutFoundRedirects) const
{
	using namespace UE::CoreRedirects::Private;

	bool bFound = false;
	const bool bPartialRHS = EnumHasAnyFlags(InMatchFlags, ECoreRedirectMatchFlags::AllowPartialMatch);

	// Substring implies prefix and suffix matches. For wildcard matches we must search all three
	// types as each can be defined distinctly
	const bool bSubstringMatch = EnumHasAllFlags(InFlags, ECoreRedirectFlags::Option_MatchSubstring);
	const bool bPrefixMatch = EnumHasAllFlags(InFlags, ECoreRedirectFlags::Option_MatchPrefix);
	const bool bSuffixMatch = EnumHasAllFlags(InFlags, ECoreRedirectFlags::Option_MatchSuffix);

	// Creating the string form of InName is expensive so early out if we know we can
	if ((!bSubstringMatch && !bPrefixMatch && !bSuffixMatch) ||
		(Prefixes.IsEmpty() && Suffixes.IsEmpty() && Substrings.IsEmpty()))
	{
		return false;
	}

	const FCoreRedirectObjectUtf8Name ObjectUtf8Name(InName);
	if (bPrefixMatch)
	{
		for (const FCoreRedirect& CheckRedirect : Prefixes)
		{
			if (MatchPrefix(CheckRedirect.OldName, ObjectUtf8Name, true /*bPartialLHS*/, bPartialRHS))
			{
				bFound = true;
				OutFoundRedirects.Add(&CheckRedirect);
			}
		}
	}

	if (bSuffixMatch)
	{
		for (const FCoreRedirect& CheckRedirect : Suffixes)
		{
			if (MatchSuffix(CheckRedirect.OldName, ObjectUtf8Name, true /*bPartialLHS*/, bPartialRHS))
			{
				bFound = true;
				OutFoundRedirects.Add(&CheckRedirect);
			}
		}
	}
	
	if (bSubstringMatch && !Substrings.IsEmpty())
	{
		COREREDIRECT_STATS_UPDATE_PREDICTION_LOOKUP();
		// Perform a fuzzy match against all known substrings. We may have false positives
		// so we must still perform a stronger check on a fuzzy match. However we can eliminate
		// the common failure case quickly with a fast fuzzy match.
		if (MatchSubstringApproximate(ObjectUtf8Name))
		{
			// We found a possible substring match, so now do a slow match to find if we really match
			for (const FCoreRedirect& CheckRedirect : Substrings)
			{
				if (MatchSubstring(CheckRedirect.OldName, ObjectUtf8Name, true /*bPartialLHS*/, bPartialRHS))
				{
					bFound = true;
					OutFoundRedirects.Add(&CheckRedirect);
				}
			}
			COREREDIRECT_STATS_UPDATE_PREDICTION_RESULT(bFound);
		}
	}

	return bFound;
}

bool FCoreRedirectObjectName::Matches(const FCoreRedirectObjectName& Other, EMatchFlags MatchFlags) const
{
	using namespace UE::CoreRedirects::Private;

	bool bPartialLHS = !EnumHasAnyFlags(MatchFlags, EMatchFlags::DisallowPartialLHSMatch);
	bool bPartialRHS = EnumHasAnyFlags(MatchFlags, EMatchFlags::AllowPartialRHSMatch);
	bool bSubstring = EnumHasAllFlags(MatchFlags, EMatchFlags::CheckSubString);
	bool bPrefix = EnumHasAllFlags(MatchFlags, EMatchFlags::CheckPrefix);
	bool bSuffix = EnumHasAllFlags(MatchFlags, EMatchFlags::CheckSuffix);

	// Substring implies prefix and suffix so we must check it first
	if (bSubstring)
	{
		return MatchSubstring(*this, Other, bPartialLHS, bPartialRHS);
	}
	else if(bPrefix)
	{
		return MatchPrefix(*this, Other, bPartialLHS, bPartialRHS);
	}
	else if (bSuffix)
	{
		return MatchSuffix(*this, Other, bPartialLHS, bPartialRHS);
	}

	auto FieldMatches = [bPartialLHS, bPartialRHS, bSubstring, bPrefix, bSuffix](FName LHS, FName RHS)
	{
		if (LHS == RHS)
			return true;

		const bool bLHSIsNone = LHS.IsNone();
		const bool bRHSIsNone = RHS.IsNone();
		if (bLHSIsNone || bRHSIsNone)
		{
			return (!bLHSIsNone || bPartialLHS) && (!bRHSIsNone || bPartialRHS);
		}

		return false;
	};

	if (!FieldMatches(ObjectName, Other.ObjectName))
	{
		return false;
	}
	if (!FieldMatches(OuterName, Other.OuterName))
	{
		return false;
	}
	if (!FieldMatches(PackageName, Other.PackageName))
	{
		return false;
	}
	return true;
}

int32 FCoreRedirectObjectName::MatchScore(const FCoreRedirectObjectName& Other) const
{
	int32 MatchScore = 1;

	if (ObjectName != NAME_None)
	{
		if (ObjectName == Other.ObjectName)
		{
			// Object name most important
			MatchScore += 8;
		}
		else
		{
			return 0;
		}
	}

	if (OuterName != NAME_None)
	{
		if (OuterName == Other.OuterName)
		{
			MatchScore += 4;
		}
		else
		{
			return 0;
		}
	}

	if (PackageName != NAME_None)
	{
		if (PackageName == Other.PackageName)
		{
			MatchScore += 2;
		}
		else
		{
			return 0;
		}
	}

	return MatchScore;
}

void FCoreRedirectObjectName::UnionFieldsInline(const FCoreRedirectObjectName& Other)
{
	if (ObjectName.IsNone())
	{
		ObjectName = Other.ObjectName;
	}
	if (OuterName.IsNone())
	{
		OuterName = Other.OuterName;
	}
	if (PackageName.IsNone())
	{
		PackageName = Other.PackageName;
	}
}

FName FCoreRedirectObjectName::GetSearchKey(ECoreRedirectFlags Type) const
{
	if (EnumHasAnyFlags(Type, ECoreRedirectFlags::Type_Package | ECoreRedirectFlags::Type_Asset))
	{
		return PackageName;
	}

	return ObjectName;
}

bool FCoreRedirectObjectName::HasValidCharacters(ECoreRedirectFlags Type) const
{
	static FString InvalidRedirectCharacters = TEXT("\"',|&!~\n\r\t@#(){}[]=;^%$`");
	static FString InvalidObjectRedirectCharacters = TEXT(".\n\r\t"); // Object and field names in Blueprints are very permissive...

	return ObjectName.IsValidXName(
		EnumHasAnyFlags(Type, ECoreRedirectFlags::Type_Object | ECoreRedirectFlags::Type_Property| ECoreRedirectFlags::Type_Function)
		? InvalidObjectRedirectCharacters : InvalidRedirectCharacters)
		&& OuterName.IsValidXName(InvalidRedirectCharacters) && PackageName.IsValidXName(InvalidRedirectCharacters);
}

bool FCoreRedirectObjectName::ExpandNames(const FStringView InStringView, FName& OutName, FName& OutOuter, FName &OutPackage)
{
	FStringView FullStringView = InStringView.TrimStartAndEnd();
	FullStringView.TrimStartAndEndInline();

	// Parse (/path.)?(outerchain.)?(name) where path and outerchain are optional
	// We also need to support (/path.)?(singleouter:)?(name) because the second delimiter in a chain is : for historical reasons
	int32 SlashIndex = INDEX_NONE;
	int32 FirstPeriodIndex = INDEX_NONE;
	int32 LastPeriodIndex = INDEX_NONE;
	int32 FirstColonIndex = INDEX_NONE;
	int32 LastColonIndex = INDEX_NONE;
	FullStringView.FindChar(TEXT('/'), SlashIndex);
	FullStringView.FindChar(TEXT('.'), FirstPeriodIndex);
	FullStringView.FindChar(TEXT(':'), FirstColonIndex);

	if (FirstColonIndex != INDEX_NONE && (FirstPeriodIndex == INDEX_NONE || FirstColonIndex < FirstPeriodIndex))
	{
		// If : is before . treat it as the first period
		FirstPeriodIndex = FirstColonIndex;
	}

	if (FirstPeriodIndex == INDEX_NONE)
	{
		// If start with /, fill in package name, otherwise name
		if (SlashIndex != INDEX_NONE)
		{
			OutPackage = FName(FullStringView);
		}
		else
		{
			OutName = FName(FullStringView);
		}
		return true;
	}

	FullStringView.FindLastChar(TEXT('.'), LastPeriodIndex);
	FullStringView.FindLastChar(TEXT(':'), LastColonIndex);

	if (LastColonIndex != INDEX_NONE && (LastPeriodIndex == INDEX_NONE || LastColonIndex > LastPeriodIndex))
	{
		// If : is after . treat it as the last period
		LastPeriodIndex = LastColonIndex;
	}

	if (SlashIndex == INDEX_NONE)
	{
		// No /, so start from beginning. There must be an outer if we got this far
		OutOuter = FName(FullStringView.Mid(0, LastPeriodIndex));
	}
	else
	{
		OutPackage = FName(FullStringView.Left(FirstPeriodIndex));
		if (FirstPeriodIndex != LastPeriodIndex)
		{
			// Extract Outer between periods
			OutOuter = FName(FullStringView.Mid(FirstPeriodIndex + 1, LastPeriodIndex - FirstPeriodIndex - 1));
		}
	}

	OutName = FName(FullStringView.Mid(LastPeriodIndex + 1));

	return true;
}

FString FCoreRedirectObjectName::CombineNames(FName NewName, FName NewOuter, FName NewPackage)
{
	TArray<FString> CombineStrings;

	if (NewOuter != NAME_None)
	{
		// If Outer is simple, need to use : instead of . because : is used for second delimiter only
		FString OuterString = NewOuter.ToString();
		int32 DelimIndex = INDEX_NONE;

		if (OuterString.FindChar(TEXT('.'), DelimIndex) || OuterString.FindChar(TEXT(':'), DelimIndex))
		{
			if (NewPackage != NAME_None)
			{
				return FString::Printf(TEXT("%s.%s.%s"), *NewPackage.ToString(), *NewOuter.ToString(), *NewName.ToString());
			}
			else
			{
				return FString::Printf(TEXT("%s.%s"), *NewOuter.ToString(), *NewName.ToString());
			}
		}
		else
		{
			if (NewPackage != NAME_None)
			{
				return FString::Printf(TEXT("%s.%s:%s"), *NewPackage.ToString(), *NewOuter.ToString(), *NewName.ToString());
			}
			else
			{
				return FString::Printf(TEXT("%s:%s"), *NewOuter.ToString(), *NewName.ToString());
			}
		}
	}
	else if (NewPackage != NAME_None)
	{
		if (NewName != NAME_None)
		{
			return FString::Printf(TEXT("%s.%s"), *NewPackage.ToString(), *NewName.ToString());
		}
		return NewPackage.ToString();
	}
	return NewName.ToString();
}

void FCoreRedirect::NormalizeNewName()
{
	// Fill in NewName as needed
	if (NewName.ObjectName == NAME_None)
	{
		NewName.ObjectName = OldName.ObjectName;
	}
	if (NewName.OuterName == NAME_None)
	{
		NewName.OuterName = OldName.OuterName;
	}
	if (NewName.PackageName == NAME_None)
	{
		NewName.PackageName = OldName.PackageName;
	}
}

const TCHAR* FCoreRedirect::ParseValueChanges(const TCHAR* Buffer)
{
	// If we export an empty array we export an empty string, so ensure that if we're passed an empty string
	// we interpret it as an empty array.
	if (*Buffer++ != TCHAR('('))
	{
		return nullptr;
	}

	SkipWhitespace(Buffer);
	if (*Buffer == TCHAR(')'))
	{
		return Buffer + 1;
	}

	for (;;)
	{
		SkipWhitespace(Buffer);
		if (*Buffer++ != TCHAR('('))
		{
			return nullptr;
		}

		// Parse the key and value
		FString KeyString, ValueString;
		Buffer = FPropertyHelpers::ReadToken(Buffer, KeyString, true);
		if (!Buffer)
		{
			return nullptr;
		}

		SkipWhitespace(Buffer);
		if (*Buffer++ != TCHAR(','))
		{
			return nullptr;
		}

		// Parse the value
		SkipWhitespace(Buffer);
		Buffer = FPropertyHelpers::ReadToken(Buffer, ValueString, true);
		if (!Buffer)
		{
			return nullptr;
		}

		SkipWhitespace(Buffer);
		if (*Buffer++ != TCHAR(')'))
		{
			return nullptr;
		}

		ValueChanges.Add(KeyString, ValueString);

		switch (*Buffer++)
		{
		case TCHAR(')'):
			return Buffer;

		case TCHAR(','):
			break;

		default:
			return nullptr;
		}
	}
}

static bool CheckRedirectFlagsMatch(ECoreRedirectFlags FlagsA, ECoreRedirectFlags FlagsB)
{
	// For type, check it includes the matching type
	const bool bTypesOverlap = !!((FlagsA & FlagsB) & ECoreRedirectFlags::Type_AllMask);

	// For category, the bits must be an exact match
	const bool bCategoriesMatch = (FlagsA & ECoreRedirectFlags::Category_AllMask) == (FlagsB & ECoreRedirectFlags::Category_AllMask);

	// Options are not considered in this function; Flags will match at this point regardless of their bits in Options_AllMask
	return bTypesOverlap && bCategoriesMatch;
}

bool FCoreRedirect::Matches(ECoreRedirectFlags InFlags, const FCoreRedirectObjectName& InName,
	ECoreRedirectMatchFlags MatchFlags) const
{
	// Check flags for Type/Category match
	if (!CheckRedirectFlagsMatch(InFlags, RedirectFlags))
	{
		return false;
	}

	if (EnumHasAnyFlags(InFlags, ECoreRedirectFlags::Type_Asset))
	{
		// Type_Asset matches should always be exact. They will either exactly match a package entry or an object entry
		MatchFlags |= ECoreRedirectMatchFlags::DisallowPartialLHSMatch;
	}

	return Matches(InName, MatchFlags);
}

bool FCoreRedirect::Matches(const FCoreRedirectObjectName& InName, ECoreRedirectMatchFlags MatchFlags) const
{
	FCoreRedirectObjectName::EMatchFlags NameMatchFlags = FCoreRedirectObjectName::EMatchFlags::None;

	if (EnumHasAllFlags(MatchFlags, ECoreRedirectMatchFlags::AllowPartialMatch))
	{
		NameMatchFlags |= FCoreRedirectObjectName::EMatchFlags::AllowPartialRHSMatch;
	}

	if (EnumHasAllFlags(MatchFlags, ECoreRedirectMatchFlags::DisallowPartialLHSMatch))
	{
		NameMatchFlags |= FCoreRedirectObjectName::EMatchFlags::DisallowPartialLHSMatch;
	}
	if (IsSubstringMatch())
	{
		NameMatchFlags |= FCoreRedirectObjectName::EMatchFlags::CheckSubString;
	}
	else if (IsPrefixMatch())
	{
		NameMatchFlags |= FCoreRedirectObjectName::EMatchFlags::CheckPrefix;
	}
	else if (IsSuffixMatch())
	{
		NameMatchFlags |= FCoreRedirectObjectName::EMatchFlags::CheckSuffix;
	}

	return OldName.Matches(InName, NameMatchFlags);
}

bool FCoreRedirect::HasValueChanges() const
{
	return ValueChanges.Num() > 0;
}

bool FCoreRedirect::IsSubstringMatch() const
{
	return EnumHasAllFlags(RedirectFlags, ECoreRedirectFlags::Option_MatchSubstring);
}

FCoreRedirectObjectName FCoreRedirect::RedirectName(const FCoreRedirectObjectName& OldObjectName, bool bIsKnownToMatch) const
{
	using namespace UE::CoreRedirects::Private;

	const bool bIsSubstringMatch = IsSubstringMatch();
	const bool bIsPrefixMatch = IsPrefixMatch();
	const bool bIsSuffixMatch = IsSuffixMatch();

	auto ConvertNames = [bIsSubstringMatch, bIsPrefixMatch, bIsSuffixMatch, bIsKnownToMatch](FName CurrentName,
		FName RedirectOldName, FName RedirectNewName)
		{
			if ((RedirectOldName != RedirectNewName) && !CurrentName.IsNone())
			{
				if (RedirectOldName.IsNone())
				{
					return RedirectNewName;
				}

				if (bIsSubstringMatch)
				{
					FNameUtf8String OutName(CurrentName);
					FNameUtf8String Substring(RedirectOldName);
					FNameUtf8String ReplacementName(RedirectNewName);
					int ReplacePos = UE::String::FindFirst(OutName, Substring, ESearchCase::IgnoreCase);

					if (!bIsKnownToMatch && ReplacePos == INDEX_NONE)
					{
						return CurrentName;
					}
					check(ReplacePos != INDEX_NONE);

					OutName.ReplaceAt(ReplacePos, Substring.Len(), ReplacementName);
					return FName(OutName);
				}
				else if (bIsPrefixMatch)
				{
					FNameUtf8String OutName(CurrentName);
					FNameUtf8String Prefix(RedirectOldName);
					FNameUtf8String ReplacementName(RedirectNewName);

					if (!bIsKnownToMatch && !OutName.StartsWith(Prefix, ESearchCase::IgnoreCase))
					{
						return CurrentName;
					}
					// bKnownToMatch implies FPrefixMatcher::Matches(CurrentName, RedirectOldName), 
					// so OutName.StartsWith(Prefix) is true, so we skip the stringcompare
					check(OutName.Len() >= Prefix.Len());

					OutName.ReplaceAt(0, Prefix.Len(), ReplacementName);
					return FName(OutName);
				}
				else if (bIsSuffixMatch)
				{
					FNameUtf8String OutName(CurrentName);
					FNameUtf8String Suffix(RedirectOldName);
					FNameUtf8String ReplacementName(RedirectNewName);

					if (!bIsKnownToMatch && !OutName.EndsWith(Suffix, ESearchCase::IgnoreCase))
					{
						return CurrentName;
					}
					// bKnownToMatch implies FSuffixMatcher::Matches(CurrentName, RedirectOldName), 
					// so OutName.EndsWith(Suffix) is true, so we skip the stringcompare
					check(OutName.Len() >= Suffix.Len());

					OutName.ReplaceAt(OutName.Len() - Suffix.Len(), Suffix.Len(), ReplacementName);
					return FName(OutName);
				}
				else
				{
					return RedirectNewName;
				}
			}
			return CurrentName;
		};

	// Convert names that are different and non empty
	FCoreRedirectObjectName ModifyName(OldObjectName);
	ModifyName.ObjectName = ConvertNames(OldObjectName.ObjectName, OldName.ObjectName, NewName.ObjectName);

	if (OldName.OuterName == NewName.OuterName)
	{
		// If package name and object name are specified, overwrite outer also since it was set to null explicitly
		if (OldName.OuterName.IsNone() && !NewName.PackageName.IsNone() && !NewName.ObjectName.IsNone() && !ModifyName.OuterName.IsNone())
		{
			ModifyName.OuterName = NewName.OuterName;
		}
	}
	else
	{
		ModifyName.OuterName = ConvertNames(OldObjectName.OuterName, OldName.OuterName, NewName.OuterName);
	}
	
	ModifyName.PackageName = ConvertNames(OldObjectName.PackageName, OldName.PackageName, NewName.PackageName);

	return ModifyName;
}

FCoreRedirectObjectName FCoreRedirect::RedirectName(const FCoreRedirectObjectName& OldObjectName) const
{
	return RedirectName(OldObjectName, false /* bIsKnownToMatch */);
}

bool FCoreRedirect::IdenticalMatchRules(const FCoreRedirect& Other) const
{
	// All types now use the full path
	return RedirectFlags == Other.RedirectFlags && OldName == Other.OldName;
}

void FCoreRedirect::AppendHash(FBlake3& Hasher) const
{
	Hasher.Update(&RedirectFlags, sizeof(RedirectFlags));
	OldName.AppendHash(Hasher);
	NewName.AppendHash(Hasher);
	OverrideClassName.AppendHash(Hasher);
	TArray<TPair<FString, FString>> ValueArray = ValueChanges.Array();
	Algo::Sort(ValueArray);
	for (const TPair<FString, FString>& Pair : ValueArray)
	{
		Hasher.Update(*Pair.Key, Pair.Key.Len() * sizeof((*Pair.Key)[0]));
		Hasher.Update(*Pair.Value, Pair.Value.Len() * sizeof((*Pair.Value)[0]));
	}
}

int FCoreRedirect::Compare(const FCoreRedirect& Other) const
{
	if (RedirectFlags != Other.RedirectFlags)
	{
		return RedirectFlags < Other.RedirectFlags ? -1 : 1;
	}
	int Compare = OldName.Compare(Other.OldName);
	if (Compare != 0)
	{
		return Compare;
	}
	Compare = NewName.Compare(Other.NewName);
	if (Compare != 0)
	{
		return Compare;
	}
	Compare = OverrideClassName.Compare(Other.OverrideClassName);
	if (Compare != 0)
	{
		return Compare;
	}
	Compare = Algo::CompareMap(ValueChanges, Other.ValueChanges);
	if (Compare != 0)
	{
		return Compare;
	}

	return 0;
}

void FCoreRedirectObjectName::AppendHash(FBlake3& Hasher) const
{
	FNameBuilder NameStr;
	int32 Marker = 0xabacadab;
	if (!PackageName.IsNone())
	{
		NameStr << PackageName;
		Hasher.Update(NameStr.GetData(), NameStr.Len() * sizeof(NameStr.GetData()[0]));
	}
	Hasher.Update(&Marker, sizeof(Marker));
	if (!OuterName.IsNone())
	{
		NameStr.Reset();
		NameStr << OuterName;
		Hasher.Update(NameStr.GetData(), NameStr.Len() * sizeof(NameStr.GetData()[0]));
	}
	Hasher.Update(&Marker, sizeof(Marker));
	if (!ObjectName.IsNone())
	{
		NameStr.Reset();
		NameStr << ObjectName;
		Hasher.Update(NameStr.GetData(), NameStr.Len() * sizeof(NameStr.GetData()[0]));
	}
	Hasher.Update(&Marker, sizeof(Marker));
}

int FCoreRedirectObjectName::Compare(const FCoreRedirectObjectName& Other) const
{
	if (PackageName != Other.PackageName)
	{
		return PackageName.Compare(Other.PackageName);
	}
	if (OuterName != Other.OuterName)
	{
		return OuterName.Compare(Other.OuterName);
	}
	if (ObjectName != Other.ObjectName)
	{
		return ObjectName.Compare(Other.ObjectName);
	}
	return 0;
}

std::atomic<bool> FCoreRedirects::bInitialized = false;
std::atomic<bool> FCoreRedirects::bInDebugMode = false;
bool FCoreRedirects::bValidatedOnce = false;

TMap<FName, ECoreRedirectFlags> FCoreRedirects::ConfigKeyMap;
FCoreRedirects::FRedirectTypeMap FCoreRedirects::RedirectTypeMap;
FCoreRedirects::FRWLockWithExclusiveRecursion FCoreRedirects::RWLock;

namespace UE::CoreRedirects::Private
{
class FRWWithExclusiveRecursionScopeLockForRead
{
public:
	explicit FRWWithExclusiveRecursionScopeLockForRead(FCoreRedirects::FRWLockWithExclusiveRecursion& InLock)
	: InternalLock(InLock)
	, NeedsUnlock(true)
	{
		UE_AUTORTFM_OPEN { InternalLock.ReadLock(); };
		AutoRTFM::PushOnAbortHandler(this, [this] { this->InternalLock.ReadUnlock(); });
	}

	~FRWWithExclusiveRecursionScopeLockForRead()
	{
		if (NeedsUnlock)
		{
			AutoRTFM::PopOnAbortHandler(this);
			UE_AUTORTFM_OPEN { InternalLock.ReadUnlock(); };
		}
	}
protected:

	enum class EInitFlag
	{
		InitFlag
	};

	FRWWithExclusiveRecursionScopeLockForRead(FCoreRedirects::FRWLockWithExclusiveRecursion& InLock, EInitFlag Unused)
	: InternalLock(InLock)
	, NeedsUnlock(false)
	{}

	FCoreRedirects::FRWLockWithExclusiveRecursion& InternalLock;
	bool NeedsUnlock;
};

class FRWWithExclusiveRecursionScopeLockForWrite :  public FRWWithExclusiveRecursionScopeLockForRead
{
public:
	explicit FRWWithExclusiveRecursionScopeLockForWrite(FCoreRedirects::FRWLockWithExclusiveRecursion& InLock)
	: FRWWithExclusiveRecursionScopeLockForRead(InLock, EInitFlag::InitFlag)
	{
		InternalLock.WriteLock();
	}

	~FRWWithExclusiveRecursionScopeLockForWrite()
	{
		InternalLock.WriteUnlock();
	}
};

}

FCoreRedirects::FRedirectNameMap& FCoreRedirects::FRedirectTypeMap::FindOrAdd(ECoreRedirectFlags Key)
{
	FRedirectNameMap*& NameMap = Map.FindOrAdd(Key);
	if (NameMap)
	{
		return *NameMap;
	}

	TPair<ECoreRedirectFlags, FRedirectNameMap>* OldData = FastIterable.GetData();
	TPair<ECoreRedirectFlags, FRedirectNameMap>& NewPair = FastIterable.Emplace_GetRef(Key, FRedirectNameMap());

	// Check to ensure FastIterable did not reallocate after emplacement
	if (FastIterable.GetData() == OldData)
	{
		NameMap = &NewPair.Value;
	}
	else
	{
		Map.Reset();
		for (TPair<ECoreRedirectFlags, FRedirectNameMap>& Pair : FastIterable)
		{
			Map.Add(Pair.Key, &Pair.Value);
		}
	}

	if (EnumHasAnyFlags(Key, ECoreRedirectFlags::Option_MatchWildcardMask))
	{
		NewPair.Value.Wildcards.Reset(new FWildcardData());
	}
	
	return NewPair.Value;
}

FCoreRedirects::FRedirectNameMap* FCoreRedirects::FRedirectTypeMap::Find(ECoreRedirectFlags Key)
{
	return Map.FindRef(Key);
}

void FCoreRedirects::FRedirectTypeMap::Empty()
{
	Map.Empty();
	FastIterable.Empty();
}

void FCoreRedirects::Initialize()
{
	ensureMsgf(IsInGameThread(), TEXT("FCoreRedirects can only be initialized on the game thread."));
	if (bInitialized.load(std::memory_order_relaxed))
	{
		return;
	}
	
	{
		FCoreRedirectorScopeLockForWrite ScopeLock(RWLock);

#if !UE_BUILD_SHIPPING
		if (FParse::Param(FCommandLine::Get(), TEXT("FullDebugCoreRedirects")))
		{
			// Enable debug mode and set to maximum verbosity
			bInDebugMode.store(true, std::memory_order_relaxed);
			UE_SET_LOG_VERBOSITY(LogCoreRedirects, VeryVerbose);
			FCoreDelegates::OnFEngineLoopInitComplete.AddStatic(ValidateAllRedirects);
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("DebugCoreRedirects")))
		{
			// Enable debug mode and increase log levels but don't show every message
			bInDebugMode.store(true, std::memory_order_relaxed);
			UE_SET_LOG_VERBOSITY(LogCoreRedirects, Verbose);
			FCoreDelegates::OnFEngineLoopInitComplete.AddStatic(ValidateAllRedirects);
		}
#endif

		// Setup map
		ConfigKeyMap.Add(TEXT("ObjectRedirects"), ECoreRedirectFlags::Type_Object);
		ConfigKeyMap.Add(TEXT("ClassRedirects"), ECoreRedirectFlags::Type_Class);
		ConfigKeyMap.Add(TEXT("StructRedirects"), ECoreRedirectFlags::Type_Struct);
		ConfigKeyMap.Add(TEXT("EnumRedirects"), ECoreRedirectFlags::Type_Enum);
		ConfigKeyMap.Add(TEXT("FunctionRedirects"), ECoreRedirectFlags::Type_Function);
		ConfigKeyMap.Add(TEXT("PropertyRedirects"), ECoreRedirectFlags::Type_Property);
		ConfigKeyMap.Add(TEXT("PackageRedirects"), ECoreRedirectFlags::Type_Package);
		ConfigKeyMap.Add(TEXT("AssetRedirects"), ECoreRedirectFlags::Type_Asset);

		RegisterNativeRedirectsUnderWriteLock(ScopeLock);

		// Prepopulate RedirectTypeMap entries that some threads write to after the engine goes multi-threaded.
		// Most RedirectTypeMap entries are written to only from InitUObject's call to ReadRedirectsFromIni, and at that point the Engine is single-threaded.
		// Known missing packages and plugin loads can add entries to existing lists but will not add brand new types.
		// Taking advantage of this, we treat the list of Key/Value pairs of RedirectTypeMap as immutable and read from it without synchronization.
		// Note that the values for those written-during-multithreading entries need to be synchronized; it is only the list of Key/Value pairs that is immutable.
		RedirectTypeMap.FindOrAdd(ECoreRedirectFlags::Type_Package | ECoreRedirectFlags::Category_Removed | ECoreRedirectFlags::Option_MissingLoad);

		bInitialized.store(true, std::memory_order_release);
	}
	// Enable to run startup tests
	// RunTests();
}

bool FCoreRedirects::RedirectNameAndValues(ECoreRedirectFlags Type, const FCoreRedirectObjectName& OldObjectName,
	FCoreRedirectObjectName& NewObjectName, const FCoreRedirect** FoundValueRedirect, ECoreRedirectMatchFlags MatchFlags)
{
	FCoreRedirectorScopeLockForRead ScopeLock(RWLock);
	return RedirectNameAndValuesUnderReadLock(Type, OldObjectName, NewObjectName, FoundValueRedirect, MatchFlags, ScopeLock);
}

bool FCoreRedirects::RedirectNameAndValuesUnderReadLock(ECoreRedirectFlags Type, const FCoreRedirectObjectName& OldObjectName,
	FCoreRedirectObjectName& NewObjectName, const FCoreRedirect** FoundValueRedirect, ECoreRedirectMatchFlags MatchFlags,
	const FCoreRedirectorScopeLockForRead& HeldLock)
{
	auto ProcessRedirect = [&FoundValueRedirect, OldObjectName](const FCoreRedirect* Redirect, const FCoreRedirectObjectName& NewObjectName)
	{
		if (FoundValueRedirect && (Redirect->HasValueChanges() || Redirect->OverrideClassName.IsValid()))
		{
			if (*FoundValueRedirect)
			{
				if ((*FoundValueRedirect)->ValueChanges.OrderIndependentCompareEqual(Redirect->ValueChanges) == false)
				{
					UE_LOG(LogCoreRedirects, Error, TEXT("RedirectNameAndValues(%s) found multiple conflicting value redirects, %s and %s!"),
						*OldObjectName.ToString(), *(*FoundValueRedirect)->OldName.ToString(), *Redirect->OldName.ToString());
				}
			}
			else
			{
				// Set value redirects for processing outside
				*FoundValueRedirect = Redirect;
			}
		}

		return Redirect->RedirectName(NewObjectName, true /* bIsKnownToMatch */);
	};

	NewObjectName = OldObjectName;
	TArray<const FCoreRedirect*> FoundRedirects;
	if (GetMatchingRedirectsUnderReadLock(Type, OldObjectName, FoundRedirects, MatchFlags, HeldLock))
	{
		if (FoundRedirects.Num() > 1)
		{
			// Sort them based on match
			FoundRedirects.Sort([&OldObjectName](const FCoreRedirect& A, const FCoreRedirect& B) 
				{ return A.OldName.MatchScore(OldObjectName) > B.OldName.MatchScore(OldObjectName); });
			NewObjectName = ProcessRedirect(FoundRedirects[0], NewObjectName);

			// Apply in order
			for (int32 i = 1; i < FoundRedirects.Num(); i++)
			{
				const FCoreRedirect* Redirect = FoundRedirects[i];

				// Only apply if name match is still valid, if it already renamed part of it it may not apply any more. 
				// Don't want to check flags as those were checked in the gather step
				if (Redirect->Matches(NewObjectName, MatchFlags))
				{
					NewObjectName = ProcessRedirect(Redirect, NewObjectName);
				}
			}
		}
		else
		{
			const FCoreRedirect* Redirect = FoundRedirects[0];
			NewObjectName = ProcessRedirect(Redirect, NewObjectName);
		}
	}

	const bool bDidRedirect = NewObjectName != OldObjectName;
	UE_CLOG(IsInDebugMode() && bDidRedirect, LogCoreRedirects, Verbose,
		TEXT("RedirectNameAndValues(%s) replaced by %s"), *OldObjectName.ToString(), *NewObjectName.ToString());
	return bDidRedirect;
}

bool FCoreRedirects::ValidateAssetRedirectsUnderReadLock(const FCoreRedirectorScopeLockForRead& HeldLock)
{
	bool bValidationSucceeded = true;
	FRedirectNameMap* RedirectNameMap = RedirectTypeMap.Find(ECoreRedirectFlags::Type_Asset);
	if (RedirectNameMap != nullptr)
	{
		for (const TPair<FName, TArray<FCoreRedirect>>& Pair : RedirectNameMap->RedirectMap)
		{
			// Pairs are package --> redirects because the search key for Type_Asset is the package name
			for (const FCoreRedirect& Redirect : Pair.Value)
			{
				const FCoreRedirectObjectName& SearchName = Redirect.NewName;

				TArray<const FCoreRedirect*> MatchingRedirects;
				GetMatchingRedirects(ECoreRedirectFlags::Type_Asset, SearchName, MatchingRedirects);
				for (const FCoreRedirect* MatchingRedirect : MatchingRedirects)
				{
					UE_LOG(LogCoreRedirects, Warning, TEXT("Found redirect from existing redirect. Chained redirects will not be followed. %s --> %s --> %s"),
						*Redirect.OldName.ToString(), *SearchName.ToString(), *MatchingRedirect->NewName.ToString());
					bValidationSucceeded = false;
				}
			}
		}
	}

	return bValidationSucceeded;
}

FCoreRedirectObjectName FCoreRedirects::GetRedirectedName(ECoreRedirectFlags Type,
	const FCoreRedirectObjectName& OldObjectName, ECoreRedirectMatchFlags MatchFlags)
{
	FCoreRedirectorScopeLockForRead ScopeLock(RWLock);
	FCoreRedirectObjectName NewObjectName;

	RedirectNameAndValuesUnderReadLock(Type, OldObjectName, NewObjectName, nullptr, MatchFlags, ScopeLock);

	return NewObjectName;
}

const TMap<FString, FString>* FCoreRedirects::GetValueRedirects(ECoreRedirectFlags Type,
	const FCoreRedirectObjectName& OldObjectName, ECoreRedirectMatchFlags MatchFlags)
{
	FCoreRedirectorScopeLockForRead ScopeLock(RWLock);
	FCoreRedirectObjectName NewObjectName;
	const FCoreRedirect* FoundRedirect = nullptr;

	RedirectNameAndValuesUnderReadLock(Type, OldObjectName, NewObjectName, &FoundRedirect, MatchFlags, ScopeLock);

	if (FoundRedirect && FoundRedirect->ValueChanges.Num() > 0)
	{
		UE_CLOG(IsInDebugMode() , LogCoreRedirects, VeryVerbose, TEXT("GetValueRedirects found %d matches for %s"),
			FoundRedirect->ValueChanges.Num(), *OldObjectName.ToString());

		return &FoundRedirect->ValueChanges;
	}

	return nullptr;
}

bool FCoreRedirects::GetMatchingRedirects(ECoreRedirectFlags SearchFlags, const FCoreRedirectObjectName& OldObjectName,
	TArray<const FCoreRedirect*>& FoundRedirects, ECoreRedirectMatchFlags MatchFlags)
{
	FCoreRedirectorScopeLockForRead ScopeLock(RWLock);
	return GetMatchingRedirectsUnderReadLock(SearchFlags, OldObjectName, FoundRedirects, MatchFlags, ScopeLock);
}

bool FCoreRedirects::GetMatchingRedirectsUnderReadLock(ECoreRedirectFlags SearchFlags, const FCoreRedirectObjectName& OldObjectName,
	TArray<const FCoreRedirect*>& FoundRedirects, ECoreRedirectMatchFlags MatchFlags, const FCoreRedirectorScopeLockForRead& HeldLock)
{
	// Look for all redirects that match the given names and flags
	bool bFound = false;

	// We always search Type_Asset as well as whatever is requested
	// That is because asset redirectors can redirect packages (implicitly) and any UObject type (explicitly)
	SearchFlags |= ECoreRedirectFlags::Type_Asset;

	// If we're not explicitly searching for packages, and not looking for removed things, and not searching for partial matches
	// based on ObjectName only, add the implicit (Type=Package,Category=None) redirects
	const bool bSearchPackageRedirects = !(SearchFlags & ECoreRedirectFlags::Type_Package) &&
		!(SearchFlags & ECoreRedirectFlags::Category_Removed) &&
		(!(MatchFlags & ECoreRedirectMatchFlags::AllowPartialMatch) || !OldObjectName.PackageName.IsNone());

	// Determine list of maps to look over, need to handle being passed multiple types in a bit mask
	for (const TPair<ECoreRedirectFlags, FRedirectNameMap>& Pair : RedirectTypeMap)
	{
		ECoreRedirectFlags PairFlags = Pair.Key;

		// We need to check all maps that match the search or package flags
		if (CheckRedirectFlagsMatch(PairFlags, SearchFlags) || 
			(bSearchPackageRedirects && CheckRedirectFlagsMatch(PairFlags, ECoreRedirectFlags::Type_Package)))
		{
			if (EnumHasAnyFlags(PairFlags, ECoreRedirectFlags::Option_MatchWildcardMask))
			{
				const FWildcardData* Wildcards = Pair.Value.Wildcards.Get();
				check(Wildcards);
				bFound |= Wildcards->Matches(PairFlags, OldObjectName, MatchFlags, FoundRedirects);
			}
			else
			{
				const TArray<FCoreRedirect>* RedirectsForName = Pair.Value.RedirectMap.Find(OldObjectName.GetSearchKey(PairFlags));
				if (RedirectsForName)
				{
					for (const FCoreRedirect& CheckRedirect : *RedirectsForName)
					{
						if (CheckRedirect.Matches(PairFlags, OldObjectName, MatchFlags))
						{
							bFound = true;
							FoundRedirects.Add(&CheckRedirect);
						}
					}
				}
			}
		}
	}

	return bFound;
}

bool FCoreRedirects::FindPreviousNames(ECoreRedirectFlags SearchFlags, const FCoreRedirectObjectName& NewObjectName, TArray<FCoreRedirectObjectName>& PreviousNames)
{
	FCoreRedirectorScopeLockForRead ScopeLock(RWLock);

	// Look for reverse direction redirects
	bool bFound = false;

	// If we're not explicitly searching for packages or looking for removed things, add the implicit (Type=Package,Category=None) redirects
	const bool bSearchPackageRedirects = !(SearchFlags & ECoreRedirectFlags::Type_Package) && !(SearchFlags & ECoreRedirectFlags::Category_Removed);

	// We always search Type_Asset as well as whatever is requested
	// That is because asset redirectors can redirect packages (implicitly) and any UObject type (explicitly)
	SearchFlags |= ECoreRedirectFlags::Type_Asset;

	auto TryReverseRedirect = [](const FCoreRedirect& Redirect, const FCoreRedirectObjectName& NewObjectName, TArray<FCoreRedirectObjectName>& PreviousNames)
		{
			FCoreRedirect ReverseRedirect = FCoreRedirect(Redirect);
			ReverseRedirect.OldName = Redirect.NewName;
			ReverseRedirect.NewName = Redirect.OldName;

			FCoreRedirectObjectName OldName = ReverseRedirect.RedirectName(NewObjectName, true /* bIsKnownToMatch */);

			if (OldName != NewObjectName)
			{
				PreviousNames.AddUnique(OldName);
				return true;
			}
			return false;
		};

	// Determine list of maps to look over, need to handle being passed multiple Flags in a bit mask
	for (const TPair<ECoreRedirectFlags, FRedirectNameMap>& Pair : RedirectTypeMap)
	{
		ECoreRedirectFlags PairFlags = Pair.Key;

		// We need to check all maps that match the search or package flags
		if (CheckRedirectFlagsMatch(PairFlags, SearchFlags) || (bSearchPackageRedirects && CheckRedirectFlagsMatch(PairFlags, ECoreRedirectFlags::Type_Package)))
		{
			if (EnumHasAnyFlags(PairFlags, ECoreRedirectFlags::Option_MatchWildcardMask))
			{
				const FWildcardData* Wildcards = Pair.Value.Wildcards.Get();
				check(Wildcards);

				FCoreRedirectObjectName::EMatchFlags MatchFlags = FCoreRedirectObjectName::EMatchFlags::None;
				const TArray<FCoreRedirect>* WildcardRedirects = nullptr;

				if (EnumHasAllFlags(PairFlags, ECoreRedirectFlags::Option_MatchSubstring))
				{
					WildcardRedirects = &Wildcards->Substrings;
					MatchFlags |= FCoreRedirectObjectName::EMatchFlags::CheckSubString;
				}
				else if (EnumHasAllFlags(PairFlags, ECoreRedirectFlags::Option_MatchPrefix))
				{
					WildcardRedirects = &Wildcards->Prefixes;
					MatchFlags |= FCoreRedirectObjectName::EMatchFlags::CheckPrefix;
				}
				else if (EnumHasAllFlags(PairFlags, ECoreRedirectFlags::Option_MatchSuffix))
				{
					WildcardRedirects = &Wildcards->Suffixes;
					MatchFlags |= FCoreRedirectObjectName::EMatchFlags::CheckSuffix;
				}
				check(WildcardRedirects);

				for (const FCoreRedirect& Redirect : *WildcardRedirects)
				{
					if (Redirect.NewName.Matches(NewObjectName, MatchFlags))
					{
						bFound |= TryReverseRedirect(Redirect, NewObjectName, PreviousNames);
					}
				}
			}
			else
			{
				FCoreRedirectObjectName::EMatchFlags MatchFlags = 
					EnumHasAnyFlags(PairFlags, ECoreRedirectFlags::Type_Asset) 
						? FCoreRedirectObjectName::EMatchFlags::AllowPartialRHSMatch : FCoreRedirectObjectName::EMatchFlags::None;
				for (const TPair<FName, TArray<FCoreRedirect>>& RedirectPair : Pair.Value.RedirectMap)
				{
					for (const FCoreRedirect& Redirect : RedirectPair.Value)
					{
						if (Redirect.NewName.Matches(NewObjectName, MatchFlags))
						{
							bFound |= TryReverseRedirect(Redirect, NewObjectName, PreviousNames);
						}
					}
				}
			}
		}
	}

	UE_CLOG(bFound && IsInDebugMode(), LogCoreRedirects, VeryVerbose, TEXT("FindPreviousNames found %d previous names for %s"), PreviousNames.Num(), *NewObjectName.ToString());

	return bFound;
}

bool FCoreRedirects::IsKnownMissing(ECoreRedirectFlags Type, const FCoreRedirectObjectName& ObjectName)
{
	TArray<const FCoreRedirect*> FoundRedirects;

	FCoreRedirectorScopeLockForRead ScopeLock(RWLock);
	return FCoreRedirects::GetMatchingRedirectsUnderReadLock(Type | ECoreRedirectFlags::Category_Removed, ObjectName, FoundRedirects, ECoreRedirectMatchFlags::None, ScopeLock);
}

bool FCoreRedirects::AddKnownMissing(ECoreRedirectFlags Type, const FCoreRedirectObjectName& ObjectName, ECoreRedirectFlags Channel)
{
	Initialize();

	check((Channel & ~ECoreRedirectFlags::Option_MissingLoad) == ECoreRedirectFlags::None);
	FCoreRedirect NewRedirect(Type | ECoreRedirectFlags::Category_Removed | Channel, ObjectName, FCoreRedirectObjectName());

	return AddRedirectList(TArrayView<const FCoreRedirect>(&NewRedirect, 1), TEXT("AddKnownMissing"));
}

bool FCoreRedirects::RemoveKnownMissing(ECoreRedirectFlags Type, const FCoreRedirectObjectName& ObjectName, ECoreRedirectFlags Channel)
{
	check((Channel & ~ECoreRedirectFlags::Option_MissingLoad) == ECoreRedirectFlags::None);
	FCoreRedirect RedirectToRemove(Type | ECoreRedirectFlags::Category_Removed | Channel, ObjectName, FCoreRedirectObjectName());

	return RemoveRedirectList(TArrayView<const FCoreRedirect>(&RedirectToRemove, 1), TEXT("RemoveKnownMissing"));
}

void FCoreRedirects::ClearKnownMissing(ECoreRedirectFlags Type, ECoreRedirectFlags Channel)
{
	check((Channel & ~ECoreRedirectFlags::Option_MissingLoad) == ECoreRedirectFlags::None);
	ECoreRedirectFlags RedirectFlags = Type | ECoreRedirectFlags::Category_Removed | Channel;

	FCoreRedirectorScopeLockForWrite ScopeLock(RWLock);
	FRedirectNameMap* RedirectNameMap = RedirectTypeMap.Find(RedirectFlags);
	if (RedirectNameMap)
	{
		RedirectNameMap->RedirectMap.Empty();
	}
}

#if WITH_EDITOR
void FCoreRedirects::AppendHashOfRedirectsAffectingPackages(FBlake3& Hasher, TConstArrayView<FName> PackageNames)
{
	GRedirectionSummary.AppendHashAffectingPackages(Hasher, PackageNames);
}

void FCoreRedirects::AppendHashOfGlobalRedirects(FBlake3& Hasher)
{
	GRedirectionSummary.AppendHashGlobal(Hasher);
}

void FCoreRedirects::RecordAddedObjectRedirector(const FSoftObjectPath& Source, const FSoftObjectPath& Dest)
{
	FCoreRedirect ConvertedToCoreRedirect(ECoreRedirectFlags::Type_Object,
		FCoreRedirectObjectName(Source), FCoreRedirectObjectName(Dest));
	GRedirectionSummary.Add(ConvertedToCoreRedirect, false /* bIsWildcardMatch */);
}

void FCoreRedirects::RecordRemovedObjectRedirector(const FSoftObjectPath& Source, const FSoftObjectPath& Dest)
{
	FCoreRedirect ConvertedToCoreRedirect(ECoreRedirectFlags::Type_Object,
		FCoreRedirectObjectName(Source), FCoreRedirectObjectName(Dest));
	GRedirectionSummary.Remove(ConvertedToCoreRedirect, false /* bIsWildcardMatch */);
}

#endif

bool FCoreRedirects::RunTests()
{
	bool bSuccess = true;

	FRedirectTypeMap BackupMap = MoveTemp(RedirectTypeMap);
	RedirectTypeMap.Empty();
#if WITH_EDITOR
	FRedirectionSummary BackupSummary = MoveTemp(GRedirectionSummary);
	GRedirectionSummary = FRedirectionSummary();
#endif

	UE_LOG(LogCoreRedirects, Log, TEXT("Running FCoreRedirect Tests"));

	TArray<FCoreRedirect> NewRedirects;

	NewRedirects.Emplace(ECoreRedirectFlags::Type_Property, TEXT("Property"), TEXT("Property2"));
	NewRedirects.Emplace(ECoreRedirectFlags::Type_Property, TEXT("Class.Property"), TEXT("Property3"));
	NewRedirects.Emplace(ECoreRedirectFlags::Type_Property, TEXT("/Game/PackageSpecific.Class.Property"), TEXT("Property4"));
	NewRedirects.Emplace(ECoreRedirectFlags::Type_Property, TEXT("/Game/Package.Class.OtherProperty"), TEXT("/Game/Package.Class.OtherProperty2"));
	NewRedirects.Emplace(ECoreRedirectFlags::Type_Class, TEXT("Class"), TEXT("Class2"));
	NewRedirects.Emplace(ECoreRedirectFlags::Type_Class, TEXT("/Game/Package.Class"), TEXT("Class3"));
	NewRedirects.Emplace(ECoreRedirectFlags::Type_Class | ECoreRedirectFlags::Category_InstanceOnly, TEXT("/Game/Package.Class"), TEXT("/Game/Package.ClassInstance"));
	NewRedirects.Emplace(ECoreRedirectFlags::Type_Package, TEXT("/Game/Package"), TEXT("/Game/Package2"));
	NewRedirects.Emplace(ECoreRedirectFlags::Type_Package | ECoreRedirectFlags::Option_MatchSubstring, TEXT("/oldgame"), TEXT("/newgame"));
	NewRedirects.Emplace(ECoreRedirectFlags::Type_Package | ECoreRedirectFlags::Option_MatchSubstring, TEXT("/古いゲーム"), TEXT("/新しいゲーム"));
	NewRedirects.Emplace(ECoreRedirectFlags::Type_Package | ECoreRedirectFlags::Option_MatchSubstring, TEXT("/混合部分文字列"), TEXT("/mixed_substring"));
	NewRedirects.Emplace(ECoreRedirectFlags::Type_Package | ECoreRedirectFlags::Option_MatchPrefix, TEXT("/oldprefix"), TEXT("/newprefix"));
	NewRedirects.Emplace(ECoreRedirectFlags::Type_Package | ECoreRedirectFlags::Option_MatchPrefix, TEXT("/古いプレフィックス"), TEXT("/新しいプレフィックス"));
	NewRedirects.Emplace(ECoreRedirectFlags::Type_Package | ECoreRedirectFlags::Option_MatchPrefix, TEXT("/混合接頭辞"), TEXT("/mixed_prefix"));
	NewRedirects.Emplace(ECoreRedirectFlags::Type_Package | ECoreRedirectFlags::Option_MatchSuffix, TEXT("/oldsuffix"), TEXT("/newsuffix"));
	NewRedirects.Emplace(ECoreRedirectFlags::Type_Package | ECoreRedirectFlags::Option_MatchSuffix, TEXT("/古い接尾辞"), TEXT("/新しい接尾辞"));
	NewRedirects.Emplace(ECoreRedirectFlags::Type_Package | ECoreRedirectFlags::Option_MatchSuffix, TEXT("/混合接尾辞"), TEXT("/mixed_suffix"));
	NewRedirects.Emplace(ECoreRedirectFlags::Type_Package | ECoreRedirectFlags::Category_Removed, TEXT("/Game/RemovedPackage"), TEXT("/Game/RemovedPackage"));
	NewRedirects.Emplace(ECoreRedirectFlags::Type_Package | ECoreRedirectFlags::Category_Removed | ECoreRedirectFlags::Option_MissingLoad, TEXT("/Game/MissingLoadPackage"), TEXT("/Game/MissingLoadPackage"));

	AddRedirectList(NewRedirects, TEXT("RunTests"));

	// Run the asset tests first so that their entries are present when we run the other tests
	// That way we can confirm that having asset type redirects doesn't break existing tests
	bSuccess = bSuccess && UE::CoreRedirects::Private::RunAssetRedirectTests();

	struct FRedirectTest
	{
		FString OldName;
		FString NewName;
		ECoreRedirectFlags Type;

		FRedirectTest(const FString& InOldName, const FString& InNewName, ECoreRedirectFlags InType)
			: OldName(InOldName), NewName(InNewName), Type(InType)
		{}
	};

	TArray<FRedirectTest> Tests;

	// Package-specific property rename and package rename apply
	Tests.Emplace(TEXT("/Game/PackageSpecific.Class:Property"), TEXT("/Game/PackageSpecific.Class:Property4"), ECoreRedirectFlags::Type_Property);
	// Verify . works as well
	Tests.Emplace(TEXT("/Game/PackageSpecific.Class.Property"), TEXT("/Game/PackageSpecific.Class:Property4"), ECoreRedirectFlags::Type_Property);
	// Wrong type, no replacement
	Tests.Emplace(TEXT("/Game/PackageSpecific.Class:Property"), TEXT("/Game/PackageSpecific.Class:Property"), ECoreRedirectFlags::Type_Function);
	// Class-specific property rename and package rename apply
	Tests.Emplace(TEXT("/Game/Package.Class:Property"), TEXT("/Game/Package2.Class:Property3"), ECoreRedirectFlags::Type_Property);
	// Package-Specific class rename applies
	Tests.Emplace(TEXT("/Game/Package.Class"), TEXT("/Game/Package2.Class3"), ECoreRedirectFlags::Type_Class);
	// Generic class rename applies
	Tests.Emplace(TEXT("/Game/PackageOther.Class"), TEXT("/Game/PackageOther.Class2"), ECoreRedirectFlags::Type_Class);
	// Check instance option
	Tests.Emplace(TEXT("/Game/Package.Class"), TEXT("/Game/Package2.ClassInstance"), ECoreRedirectFlags::Type_Class | ECoreRedirectFlags::Category_InstanceOnly);

	// String manipulation tests. While we use TCHAR for input, FNames will coerce storage to ASCII where possible 
	// so we validate we can support truly wide strings as well as mixed string manipulation operations.

	// Substring tests
	Tests.Emplace(TEXT("/oldgame/Package.DefaultClass"), TEXT("/newgame/Package.DefaultClass"), ECoreRedirectFlags::Type_Package);
	Tests.Emplace(TEXT("/古いゲーム/Package.DefaultClass"), TEXT("/新しいゲーム/Package.DefaultClass"), ECoreRedirectFlags::Type_Package);
	Tests.Emplace(TEXT("/混合部分文字列/Package.DefaultClass"), TEXT("/mixed_substring/Package.DefaultClass"), ECoreRedirectFlags::Type_Package);
	// Prefix tests
	Tests.Emplace(TEXT("/oldprefix_SomeGame/Package.DefaultClass"), TEXT("/newprefix_SomeGame/Package.DefaultClass"), ECoreRedirectFlags::Type_Package);
	Tests.Emplace(TEXT("/古いプレフィックス_SomeGame/Package.DefaultClass"), TEXT("/新しいプレフィックス_SomeGame/Package.DefaultClass"), ECoreRedirectFlags::Type_Package);
	Tests.Emplace(TEXT("/混合接頭辞_SomeGame/Package.DefaultClass"), TEXT("/mixed_prefix_SomeGame/Package.DefaultClass"), ECoreRedirectFlags::Type_Package);
	// Suffix tests
	Tests.Emplace(TEXT("/Game/Package/oldsuffix"), TEXT("/Game/Package/newsuffix"), ECoreRedirectFlags::Type_Package);
	Tests.Emplace(TEXT("/Game/Package/古い接尾辞"), TEXT("/Game/Package/新しい接尾辞"), ECoreRedirectFlags::Type_Package);
	Tests.Emplace(TEXT("/Game/Package/混合接尾辞"), TEXT("/Game/Package/mixed_suffix"), ECoreRedirectFlags::Type_Package);

	for (FRedirectTest& Test : Tests)
	{
		FCoreRedirectObjectName OldName = FCoreRedirectObjectName(Test.OldName);
		FCoreRedirectObjectName NewName = GetRedirectedName(Test.Type, OldName);

		if (NewName.ToString() != Test.NewName)
		{
			bSuccess = false;
			UE_LOG(LogCoreRedirects, Error, TEXT("FCoreRedirect Test Failed: %s to %s, should be %s!"), *OldName.ToString(), *NewName.ToString(), *Test.NewName);
		}
	}

	// Check reverse lookup
	TArray<FCoreRedirectObjectName> OldNames;

	FindPreviousNames(ECoreRedirectFlags::Type_Class, FCoreRedirectObjectName(TEXT("/Game/PackageOther.Class2")), OldNames);
	if (OldNames.Num() != 1 || OldNames[0].ToString() != TEXT("/Game/PackageOther.Class"))
	{
		bSuccess = false;
		UE_LOG(LogCoreRedirects, Error, TEXT("FCoreRedirect Test Failed: ReverseLookup (direct matching)!"));
	}
	OldNames.Empty();

	// Check reverse lookup - substring
	FindPreviousNames(ECoreRedirectFlags::Type_Package, FCoreRedirectObjectName(TEXT("/newgame/TestPackage")), OldNames);
	if (OldNames.Num() != 1 || OldNames[0].ToString() != TEXT("/oldgame/TestPackage"))
	{
		bSuccess = false;
		UE_LOG(LogCoreRedirects, Error, TEXT("FCoreRedirect Test Failed: ReverseLookup (substring matching)!"));
	}
	OldNames.Empty();

	// Check reverse lookup - prefix
	FindPreviousNames(ECoreRedirectFlags::Type_Package, FCoreRedirectObjectName(TEXT("/newprefix_SomeGame/TestPackage")), OldNames);
	if (OldNames.Num() != 1 || OldNames[0].ToString() != TEXT("/oldprefix_SomeGame/TestPackage"))
	{
		bSuccess = false;
		UE_LOG(LogCoreRedirects, Error, TEXT("FCoreRedirect Test Failed: ReverseLookup (prefix matching)!"));
	}
	OldNames.Empty();

	// Check reverse lookup - suffix
	FindPreviousNames(ECoreRedirectFlags::Type_Package, FCoreRedirectObjectName(TEXT("/TestGame/newsuffix")), OldNames);
	if (OldNames.Num() != 1 || OldNames[0].ToString() != TEXT("/TestGame/oldsuffix"))
	{
		bSuccess = false;
		UE_LOG(LogCoreRedirects, Error, TEXT("FCoreRedirect Test Failed: ReverseLookup (suffix matching)!"));
	}
	OldNames.Empty();

	// Check removed
	if (!IsKnownMissing(ECoreRedirectFlags::Type_Package, FCoreRedirectObjectName(TEXT("/Game/RemovedPackage"))))
	{
		bSuccess = false;
		UE_LOG(LogCoreRedirects, Error, TEXT("FCoreRedirect Test Failed: /Game/RemovedPackage should be removed!"));
	}

	if (!IsKnownMissing(ECoreRedirectFlags::Type_Package, FCoreRedirectObjectName(TEXT("/Game/MissingLoadPackage"))))
	{
		bSuccess = false;
		UE_LOG(LogCoreRedirects, Error, TEXT("FCoreRedirect Test Failed: /Game/MissingLoadPackage should be removed!"));
	}

	if (IsKnownMissing(ECoreRedirectFlags::Type_Package, FCoreRedirectObjectName(TEXT("/Game/NotRemovedPackage"))))
	{
		bSuccess = false;
		UE_LOG(LogCoreRedirects, Error, TEXT("FCoreRedirect Test Failed: /Game/NotRemovedPackage should be removed!"));
	}

	AddKnownMissing(ECoreRedirectFlags::Type_Package, FCoreRedirectObjectName(TEXT("/Game/NotRemovedMissingLoad")), ECoreRedirectFlags::Option_MissingLoad);

	if (!IsKnownMissing(ECoreRedirectFlags::Type_Package, FCoreRedirectObjectName(TEXT("/Game/NotRemovedMissingLoad"))))
	{
		bSuccess = false;
		UE_LOG(LogCoreRedirects, Error, TEXT("FCoreRedirect Test Failed: /Game/NotRemovedMissingLoad should be removed now!"));
	}

	RemoveKnownMissing(ECoreRedirectFlags::Type_Package, FCoreRedirectObjectName(TEXT("/Game/NotRemovedMissingLoad")), ECoreRedirectFlags::None);

	if (!IsKnownMissing(ECoreRedirectFlags::Type_Package, FCoreRedirectObjectName(TEXT("/Game/NotRemovedMissingLoad"))))
	{
		bSuccess = false;
		UE_LOG(LogCoreRedirects, Error, TEXT("FCoreRedirect Test Failed: RemoveKnownMissing of /Game/NotRemovedMissingLoad but with bIsMissingLoad=false should not have removed the redirect!"));
	}

	RemoveKnownMissing(ECoreRedirectFlags::Type_Package, FCoreRedirectObjectName(TEXT("/Game/NotRemovedMissingLoad")), ECoreRedirectFlags::Option_MissingLoad);

	if (IsKnownMissing(ECoreRedirectFlags::Type_Package, FCoreRedirectObjectName(TEXT("/Game/NotRemovedMissingLoad"))))
	{
		bSuccess = false;
		UE_LOG(LogCoreRedirects, Error, TEXT("FCoreRedirect Test Failed: /Game/NotRemovedMissingLoad should no longer be removed!"));
	}

	AddKnownMissing(ECoreRedirectFlags::Type_Package, FCoreRedirectObjectName(TEXT("/Game/NotRemovedPackage")), ECoreRedirectFlags::None);

	if (!IsKnownMissing(ECoreRedirectFlags::Type_Package, FCoreRedirectObjectName(TEXT("/Game/NotRemovedPackage"))))
	{
		bSuccess = false;
		UE_LOG(LogCoreRedirects, Error, TEXT("FCoreRedirect Test Failed: /Game/NotRemovedPackage should be removed now!"));
	}

	RemoveKnownMissing(ECoreRedirectFlags::Type_Package, FCoreRedirectObjectName(TEXT("/Game/NotRemovedPackage")), ECoreRedirectFlags::Option_MissingLoad);

	if (!IsKnownMissing(ECoreRedirectFlags::Type_Package, FCoreRedirectObjectName(TEXT("/Game/NotRemovedPackage"))))
	{
		bSuccess = false;
		UE_LOG(LogCoreRedirects, Error, TEXT("FCoreRedirect Test Failed: RemoveKnownMissing of /Game/NotRemovedPackage but with bIsMissingLoad=true should not have removed the redirect!"));
	}

	RemoveKnownMissing(ECoreRedirectFlags::Type_Package, FCoreRedirectObjectName(TEXT("/Game/NotRemovedPackage")), ECoreRedirectFlags::None);

	if (IsKnownMissing(ECoreRedirectFlags::Type_Package, FCoreRedirectObjectName(TEXT("/Game/NotRemovedPackage"))))
	{
		bSuccess = false;
		UE_LOG(LogCoreRedirects, Error, TEXT("FCoreRedirect Test Failed: /Game/NotRemovedPackage should no longer be removed!"));
	}

	// Restore old state
	RedirectTypeMap = MoveTemp(BackupMap);
#if WITH_EDITOR
	GRedirectionSummary = MoveTemp(BackupSummary);
#endif

	UE_LOG(LogCoreRedirects, Log, TEXT("FCoreRedirect Test %s!"), (bSuccess ? TEXT("Passed") : TEXT("Failed")));
	return bSuccess;
}

void FCoreRedirects::AddAssetRedirects(const TMap<FSoftObjectPath, FSoftObjectPath>& InRedirects)
{
	if (InRedirects.Num() > 0)
	{
		FCoreRedirectorScopeLockForWrite ScopeLock(RWLock);
		FRedirectNameMap* ExistingMap = &RedirectTypeMap.FindOrAdd(ECoreRedirectFlags::Type_Asset);
		int32 NumAdded = 0;
		int32 NumSkipped = 0;
		for (const TPair<FSoftObjectPath, FSoftObjectPath>& Pair : InRedirects)
		{
			// Asset redirects are, by definition, not package redirects
			if (Pair.Key.GetLongPackageFName().IsNone() || Pair.Value.GetAssetFName().IsNone())
			{
				UE_LOG(LogCoreRedirects, Warning, TEXT("Attempted to register asset redirector that was missing a package or object name. Redirector was from %s to %s"),
					*Pair.Key.ToString(), *Pair.Value.ToString());
				NumSkipped++;
				continue;
			}

			// Asset redirects use the package as the lookup key but contain multiple redirects underneath
			// Conceptually, for each object redirector we add two core redirects: one for the package and one for the object itself
			// In practice, we can just add the package redirect the first time

			FCoreRedirect ObjectRedirector(ECoreRedirectFlags::Type_Asset, Pair.Key.ToString(), Pair.Value.ToString());
			TArray<FCoreRedirect>& ExistingRedirects = ExistingMap->RedirectMap.FindOrAdd(ObjectRedirector.GetSearchKey());

			if (ExistingRedirects.Num() == 0)
			{
				// This is a new redirector. Add two entries, one for the package first
				FCoreRedirect PackageRedirect(ECoreRedirectFlags::Type_Asset, 
					Pair.Key.GetLongPackageName(), Pair.Value.GetLongPackageName());

				ExistingRedirects.Add(PackageRedirect);
			}

			// Check that, among the existing redirects, this one won't be a duplicate. Don't bother checking the package entry
			bool bShouldAddRedirect = true;
			for (int32 RedirectIndex = 1; RedirectIndex < ExistingRedirects.Num(); RedirectIndex++)
			{
				if (Pair.Key.GetLongPackageFName() == ExistingRedirects[RedirectIndex].OldName.PackageName
					&& Pair.Key.GetAssetName() == ExistingRedirects[RedirectIndex].OldName.ObjectName)
				{
					const FCoreRedirectObjectName& ExistingTargetName = ExistingRedirects[0].NewName;

					UE_LOGFMT(LogCoreRedirects, Error, "Skipping new redirect target '{target}' due to existing map from '{source}' to '{dest}'",
						Pair.Value.ToString(), Pair.Key.ToString(), ExistingTargetName.ToString());

					bShouldAddRedirect = false;
					NumSkipped++;
					break;
				}
			}

			if (bShouldAddRedirect)
			{
				ExistingRedirects.Add(ObjectRedirector);
				NumAdded++;
			}
		}

		UE_LOG(LogCoreRedirects, Display, 
			TEXT("Object redirects provided to FCoreRedirects: %d. Redirects add: %d. Redirects skipped: %d"),
			InRedirects.Num(), NumAdded, NumSkipped);

		if (IsInDebugMode())
		{
			ValidateAssetRedirects();
		}
	}
}

void FCoreRedirects::RemoveAllAssetRedirects()
{
	FCoreRedirectorScopeLockForWrite ScopeLock(RWLock);
	FRedirectNameMap* ExistingMap = &RedirectTypeMap.FindOrAdd(ECoreRedirectFlags::Type_Asset);
	ExistingMap->RedirectMap.Reset();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoreRedirectTest, "System.Core.Misc.CoreRedirects", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FCoreRedirectTest::RunTest(const FString& Parameters)
{
	return FCoreRedirects::RunTests();
}

bool FCoreRedirects::ReadRedirectsFromIni(const FString& IniName)
{
	FCoreRedirectorScopeLockForWrite ScopeLock(RWLock);
	Initialize();

	if (GConfig)
	{
		const TCHAR* RedirectSectionName = TEXT("CoreRedirects");
		const FConfigSection* RedirectSection = GConfig->GetSection(RedirectSectionName, false, IniName);
		if (RedirectSection)
		{
			TArray<FCoreRedirect> NewRedirects;

			for (FConfigSection::TConstIterator It(*RedirectSection); It; ++It)
			{
				FString OldName, NewName, OverrideClassName;

				bool bInstanceOnly = false;
				bool bRemoved = false;
				bool bMatchSubstring = false;
				bool bMatchWildcard = false;

				const FString& ValueString = It.Value().GetValue();

				FParse::Bool(*ValueString, TEXT("InstanceOnly="), bInstanceOnly);
				FParse::Bool(*ValueString, TEXT("Removed="), bRemoved);
				FParse::Bool(*ValueString, TEXT("MatchSubstring="), bMatchSubstring);
				FParse::Bool(*ValueString, TEXT("MatchWildcard="), bMatchWildcard);

				FParse::Value(*ValueString, TEXT("OldName="), OldName);
				FParse::Value(*ValueString, TEXT("NewName="), NewName);

				FParse::Value(*ValueString, TEXT("OverrideClassName="), OverrideClassName);

				ECoreRedirectFlags* FlagPtr = ConfigKeyMap.Find(It.Key());

				// If valid type
				if (FlagPtr)
				{
					ECoreRedirectFlags NewFlags = *FlagPtr;

					if (bInstanceOnly)
					{
						NewFlags |= ECoreRedirectFlags::Category_InstanceOnly;
					}

					if (bRemoved)
					{
						NewFlags |= ECoreRedirectFlags::Category_Removed;
					}

					if (bMatchWildcard || bMatchSubstring)
					{
						UE_CLOG(bMatchSubstring, LogCoreRedirects, Warning, TEXT("ReadRedirectsFromIni(%s) 'MatchSubstring=' is deprecated. "
							"Please prefer `MatchWildcard=' instead for redirect %s.\n\t"
							"For more information refer to the documentation in Engine/Config/BaseEngine.ini."), *IniName, *ValueString);

						constexpr FStringView WildcardMarker(TEXTVIEW("..."));
						bool bMatchPrefix = OldName.EndsWith(WildcardMarker, ESearchCase::CaseSensitive);
						bool bMatchSuffix = OldName.StartsWith(WildcardMarker, ESearchCase::CaseSensitive);

						bMatchSubstring = bMatchSubstring || (bMatchPrefix && bMatchSuffix);

						// Count how many '...' there are to ensure the OldName is not malformed
						int WildcardCount = 0;
						int StartPos = 0;
						FStringView OldNameView(OldName);
						while ((StartPos = OldNameView.Find(WildcardMarker, StartPos)) != INDEX_NONE)
						{
							StartPos += WildcardMarker.Len();
							WildcardCount++;
						}

						if ((!bMatchPrefix && !bMatchSuffix && !bMatchSubstring)		  // No wildcards found and not handling MatchSubstring (no '...')
							|| (WildcardCount > ((int)bMatchPrefix + (int)bMatchSuffix))) // Ensure we don't have more wildcards than necessary
						{
							UE_LOG(LogCoreRedirects, Error, TEXT("ReadRedirectsFromIni(%s) failed to parse OldName for wildcard redirect %s! "
								"OldName must be of the form 'PrefixName...', '...SubstringName...' or '...SuffixName'. For more information refer to the documentation in Engine/Config/BaseEngine.ini."), *IniName, *ValueString);
							continue;
						}

						if (bMatchPrefix)
						{
							NewFlags |= ECoreRedirectFlags::Option_MatchPrefix;
							OldName.LeftChopInline(WildcardMarker.Len(), EAllowShrinking::No);
						}

						if (bMatchSuffix)
						{
							NewFlags |= ECoreRedirectFlags::Option_MatchSuffix;
							OldName.RightChopInline(WildcardMarker.Len(), EAllowShrinking::No);
						}

						// UE-209583 - Convert prefix and suffix matches to substring matches since prefix/suffix is currently unoptimized
						if (bMatchPrefix || bMatchSuffix)
						{
							bMatchSubstring = true;
						}

						NewFlags |= bMatchSubstring ? ECoreRedirectFlags::Option_MatchSubstring : ECoreRedirectFlags::None;
					}

					FCoreRedirect* Redirect = &NewRedirects.Emplace_GetRef(NewFlags, FCoreRedirectObjectName(OldName), FCoreRedirectObjectName(NewName));

					if (!OverrideClassName.IsEmpty())
					{
						Redirect->OverrideClassName = FCoreRedirectObjectName(OverrideClassName);
					}

					int32 ValueChangesIndex = ValueString.Find(TEXT("ValueChanges="));
					if (ValueChangesIndex != INDEX_NONE)
					{
						// Look for first (
						ValueChangesIndex = ValueString.Find(TEXT("("), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueChangesIndex);

						FString ValueChangesString = ValueString.Mid(ValueChangesIndex);
						const TCHAR* Buffer = Redirect->ParseValueChanges(GetData(ValueChangesString));

						if (!Buffer)
						{
							UE_LOG(LogCoreRedirects, Error, TEXT("ReadRedirectsFromIni(%s) failed to parse ValueChanges for redirect %s!"), *IniName, *ValueString);

							// Remove added redirect
							Redirect = nullptr;
							NewRedirects.RemoveAt(NewRedirects.Num() - 1);
						}
					}
				}
				else
				{
					UE_LOG(LogCoreRedirects, Error, TEXT("ReadRedirectsFromIni(%s) failed to parse type for redirect %s!"), *IniName, *ValueString);
				}
			}

			// We no longer need the redirect config data in memory so remove it entirely
			GConfig->RemoveSectionFromBranch(RedirectSectionName, *IniName);

			return AddRedirectList(NewRedirects, IniName);
		}
		else
		{
			UE_LOG(LogCoreRedirects, Verbose, TEXT("ReadRedirectsFromIni(%s) did not find any redirects"), *IniName);
		}
	}
	else
	{
		UE_LOG(LogCoreRedirects, Warning, TEXT(" **** CORE REDIRECTS UNABLE TO INITIALIZE! **** "));
	}
	return false;
}

bool FCoreRedirects::AddRedirectList(TArrayView<const FCoreRedirect> Redirects, const FString& SourceString)
{
	FCoreRedirectorScopeLockForWrite ScopeLock(RWLock);
	Initialize();
	return AddRedirectListUnderWriteLock(Redirects, SourceString, ScopeLock);
}

bool FCoreRedirects::AddRedirectListUnderWriteLock(TArrayView<const FCoreRedirect> Redirects, const FString & SourceString, 
	const FCoreRedirectorScopeLockForWrite& HeldLock)
{
	UE_LOG(LogCoreRedirects, Verbose, TEXT("AddRedirect(%s) adding %d redirects"), *SourceString, Redirects.Num());

	if (IsInDebugMode() && bValidatedOnce)
	{
		// Validate on apply because we finished our initial validation pass
		ValidateRedirectList(Redirects, SourceString);
	}

	bool bAddedAny = false;
	for (const FCoreRedirect& NewRedirect : Redirects)
	{
		if (!NewRedirect.OldName.IsValid() || !NewRedirect.NewName.IsValid())
		{
			UE_LOG(LogCoreRedirects, Error, TEXT("AddRedirect(%s) failed to add redirect from %s to %s with empty name!"), *SourceString, *NewRedirect.OldName.ToString(), *NewRedirect.NewName.ToString());
			continue;
		}

		// Type_Asset redirects derive from UObjectRedirector instances on disk.
		// Therefore we can assume that the names in it are valid (because they exist)
		// This is as opposed to redirects that are manually created either in code or in 
		// ini files and which should be subject to additional validation.
		// Because the validation in HasValidCharacters does not perfectly align with other systems,
		// there can exist assets which are valid, are referenced by UObjectRedirector assets, but which
		// fail the check.
		if (!EnumHasAnyFlags(NewRedirect.RedirectFlags, ECoreRedirectFlags::Type_Asset))
		{
			if ((!NewRedirect.OldName.HasValidCharacters(NewRedirect.RedirectFlags) && !FPackageName::IsVersePackage(NewRedirect.OldName.PackageName.ToString()))
				|| (!NewRedirect.NewName.HasValidCharacters(NewRedirect.RedirectFlags) && !FPackageName::IsVersePackage(NewRedirect.NewName.PackageName.ToString())))
			{
				UE_LOG(LogCoreRedirects, Error, TEXT("AddRedirect(%s) failed to add redirect from %s to %s with invalid characters!"), *SourceString, *NewRedirect.OldName.ToString(), *NewRedirect.NewName.ToString());
				continue;
			}
		}

		if (NewRedirect.IsWildcardMatch())
		{
			UE_LOG(LogCoreRedirects, Log, TEXT("AddRedirect(%s) has wildcard redirect %s, these are very slow and should be resolved as soon as possible! Please refer to the documentation in Engine/Config/BaseEngine.ini."), *SourceString, *NewRedirect.OldName.ToString());
		}
		
		if (AddSingleRedirectUnderWriteLock(NewRedirect, SourceString, HeldLock))
		{
			bAddedAny = true;

			// If value redirect, add a value redirect from NewName->NewName as well, this will merge with existing ones as needed
			if (NewRedirect.OldName != NewRedirect.NewName && NewRedirect.HasValueChanges())
			{
				FCoreRedirect ValueRedirect = NewRedirect;
				ValueRedirect.OldName = ValueRedirect.NewName;

				AddSingleRedirectUnderWriteLock(ValueRedirect, SourceString, HeldLock);
			}
		}
	}

	return bAddedAny;
}

bool FCoreRedirects::AddSingleRedirectUnderWriteLock(const FCoreRedirect& NewRedirect, const FString& SourceString, 
	const FCoreRedirectorScopeLockForWrite& HeldLock)
{
	const bool bIsWildcardMatch = NewRedirect.IsWildcardMatch();
	FRedirectNameMap* ExistingNameMap;

	ExistingNameMap = &RedirectTypeMap.FindOrAdd(NewRedirect.RedirectFlags);

	TArray<FCoreRedirect>* ExistingRedirects = nullptr;
	if (bIsWildcardMatch)
	{
		if (NewRedirect.IsSubstringMatch())
		{
			ExistingRedirects = &ExistingNameMap->Wildcards->Substrings;
		}
		else if (NewRedirect.IsPrefixMatch())
		{
			ExistingRedirects = &ExistingNameMap->Wildcards->Prefixes;
		}
		else if (NewRedirect.IsSuffixMatch())
		{
			ExistingRedirects = &ExistingNameMap->Wildcards->Suffixes;
		}
		check(ExistingRedirects);
	}
	else
	{
		ExistingRedirects = &ExistingNameMap->RedirectMap.FindOrAdd(NewRedirect.GetSearchKey());
	}

	// Check for duplicate
	bool bFoundDuplicate = false;
	for (FCoreRedirect& ExistingRedirect : *ExistingRedirects)
	{
		if (ExistingRedirect.IdenticalMatchRules(NewRedirect))
		{
			bFoundDuplicate = true;
			bool bIsSameNewName = ExistingRedirect.NewName == NewRedirect.NewName;
			bool bOneIsPartialOtherIsFull = false;
			if (!bIsSameNewName &&
				ExistingRedirect.OldName.Matches(NewRedirect.OldName, FCoreRedirectObjectName::EMatchFlags::AllowPartialRHSMatch) &&
				ExistingRedirect.NewName.Matches(NewRedirect.NewName, FCoreRedirectObjectName::EMatchFlags::AllowPartialRHSMatch))
			{
				bIsSameNewName = true;
				bOneIsPartialOtherIsFull = true;
			}

			if (bIsSameNewName)
			{
				// Merge fields from the two duplicate redirects
				bool bBothHaveValueChanges = ExistingRedirect.HasValueChanges() && NewRedirect.HasValueChanges();
				ExistingRedirect.OldName.UnionFieldsInline(NewRedirect.OldName);
				ExistingRedirect.NewName.UnionFieldsInline(NewRedirect.NewName);
				ExistingRedirect.ValueChanges.Append(NewRedirect.ValueChanges);
				if (bBothHaveValueChanges)
				{
					// No warning when there are values changes to merge
					UE_LOG(LogCoreRedirects, Verbose, TEXT("AddRedirect(%s) merging value redirects for %s"), *SourceString, *ExistingRedirect.NewName.ToString());
				}
				else if (bOneIsPartialOtherIsFull)
				{
					UE_LOG(LogCoreRedirects, Warning, TEXT("AddRedirect(%s) found duplicate redirects for %s to %s, one a FullPath and the other ObjectName-only.")
						TEXT(" This used to be required for StructRedirects but now you should remove the ObjectName-only redirect and keep the FullPath."),
						*SourceString, *ExistingRedirect.OldName.ToString(), *ExistingRedirect.NewName.ToString());
				}
				else
				{
					UE_LOG(LogCoreRedirects, Verbose, TEXT("AddRedirect(%s) ignoring duplicate redirects for %s to %s"), *SourceString, *ExistingRedirect.OldName.ToString(), *ExistingRedirect.NewName.ToString());
				}
			}
			else
			{
				UE_LOG(LogCoreRedirects, Error, TEXT("AddRedirect(%s) found conflicting redirects for %s! Old: %s, New: %s"), *SourceString, *ExistingRedirect.OldName.ToString(), *ExistingRedirect.NewName.ToString(), *NewRedirect.NewName.ToString());
			}
			break;
		}
	}

	if (bFoundDuplicate)
	{
		return false;
	}

	if (bIsWildcardMatch)
	{
		ExistingNameMap->Wildcards->Add(NewRedirect);
	}
	else
	{
		ExistingRedirects->Add(NewRedirect);
	}
#if WITH_EDITOR
	GRedirectionSummary.Add(NewRedirect, bIsWildcardMatch);
#endif

	return true;
}

bool FCoreRedirects::RemoveRedirectList(TArrayView<const FCoreRedirect> Redirects, const FString& SourceString)
{
	FCoreRedirectorScopeLockForWrite ScopeLock(RWLock);
	UE_LOG(LogCoreRedirects, Verbose, TEXT("RemoveRedirect(%s) Removing %d redirects"), *SourceString, Redirects.Num());

	bool bRemovedAny = false;
	for (const FCoreRedirect& RedirectToRemove : Redirects)
	{
		if (!RedirectToRemove.OldName.IsValid() || !RedirectToRemove.NewName.IsValid())
		{
			UE_LOG(LogCoreRedirects, Error, TEXT("RemoveRedirect(%s) failed to remove redirect from %s to %s with empty name!"), *SourceString, *RedirectToRemove.OldName.ToString(), *RedirectToRemove.NewName.ToString());
			continue;
		}

		if (RedirectToRemove.HasValueChanges())
		{
			UE_LOG(LogCoreRedirects, Error, TEXT("RemoveRedirect(%s) failed to remove redirect from %s to %s as it contains value changes!"), *SourceString, *RedirectToRemove.OldName.ToString(), *RedirectToRemove.NewName.ToString());
			continue;
		}

		if (!RedirectToRemove.OldName.HasValidCharacters(RedirectToRemove.RedirectFlags) || !RedirectToRemove.NewName.HasValidCharacters(RedirectToRemove.RedirectFlags))
		{
			UE_LOG(LogCoreRedirects, Error, TEXT("RemoveRedirect(%s) failed to remove redirect from %s to %s with invalid characters!"), *SourceString, *RedirectToRemove.OldName.ToString(), *RedirectToRemove.NewName.ToString());
			continue;
		}

		if (RedirectToRemove.NewName.PackageName != RedirectToRemove.OldName.PackageName && RedirectToRemove.OldName.OuterName != NAME_None)
		{
			UE_LOG(LogCoreRedirects, Error, TEXT("RemoveRedirect(%s) failed to remove redirect, it's not valid to modify package from %s to %s while specifying outer!"), *SourceString, *RedirectToRemove.OldName.ToString(), *RedirectToRemove.NewName.ToString());
			continue;
		}

		if (RedirectToRemove.IsWildcardMatch())
		{
			UE_LOG(LogCoreRedirects, Log, TEXT("RemoveRedirect(%s) has wildcard redirect %s, these are very slow and should be resolved as soon as possible! Please refer to the documentation in Engine/Config/BaseEngine.ini."), *SourceString, *RedirectToRemove.OldName.ToString());
		}

		bRemovedAny |= RemoveSingleRedirectUnderWriteLock(RedirectToRemove, SourceString, ScopeLock);
	}

	return bRemovedAny;
}

bool FCoreRedirects::IsInitialized()
{
	// Use an atomic variable rather than take a read lock because we might already be under a read lock
	return bInitialized.load(std::memory_order_acquire);
}

bool FCoreRedirects::IsInDebugMode()
{
	// Use an atomic variable rather than take a read lock because we might already be under a read lock
	return bInDebugMode.load(std::memory_order_relaxed);
}

bool FCoreRedirects::RemoveSingleRedirectUnderWriteLock(const FCoreRedirect& RedirectToRemove, const FString& SourceString,
	const FCoreRedirectorScopeLockForWrite& HeldLock)
{
	const bool bIsWildcardMatch = RedirectToRemove.IsWildcardMatch();
	FRedirectNameMap* ExistingNameMap = RedirectTypeMap.Find(RedirectToRemove.RedirectFlags);

	if (!ExistingNameMap)
	{
		return false;
	}

	TArray<FCoreRedirect>* ExistingRedirects = nullptr;
	if (bIsWildcardMatch)
	{
		if (RedirectToRemove.IsSubstringMatch())
		{
			ExistingRedirects = &ExistingNameMap->Wildcards->Substrings;
		}
		else if (RedirectToRemove.IsPrefixMatch())
		{
			ExistingRedirects = &ExistingNameMap->Wildcards->Prefixes;
		}
		else if (RedirectToRemove.IsSuffixMatch())
		{
			ExistingRedirects = &ExistingNameMap->Wildcards->Suffixes;
		}
	}
	else
	{
		ExistingRedirects = ExistingNameMap->RedirectMap.Find(RedirectToRemove.GetSearchKey());
	}

	if (!ExistingRedirects)
	{
		return false;
	}

	bool bRemovedRedirect = false;
	for (int32 ExistingRedirectIndex = 0; ExistingRedirectIndex < ExistingRedirects->Num(); ++ExistingRedirectIndex)
	{
		FCoreRedirect& ExistingRedirect = (*ExistingRedirects)[ExistingRedirectIndex];

		if (ExistingRedirect.IdenticalMatchRules(RedirectToRemove))
		{
			if (ExistingRedirect.NewName != RedirectToRemove.NewName)
			{
				// This isn't the redirect we were looking for... move on in case there's another match for our old name
				continue;
			}

			bRemovedRedirect = true;
			ExistingRedirects->RemoveAt(ExistingRedirectIndex);
			break;
		}
	}

	if (bRemovedRedirect)
	{
		if (bIsWildcardMatch)
		{
			// We removed a substring redirect so we need to regenerate our prediction 
			// tables to avoid unnecessary false positives
			ExistingNameMap->Wildcards->Rebuild();
		}
#if WITH_EDITOR
		GRedirectionSummary.Remove(RedirectToRemove, bIsWildcardMatch);
#endif
	}

	return bRemovedRedirect;
}

void FCoreRedirects::ValidateRedirectList(TArrayView<const FCoreRedirect> Redirects, const FString& SourceString)
{
	for (const FCoreRedirect& Redirect : Redirects)
	{
		if (Redirect.NewName.IsValid())
		{
			// If the new package is loaded but the new name isn't, this is very likely a bug
			// If the new package isn't loaded the redirect can't be validated either way, so report it for manual follow up
			UObject* NewPackage = FindObjectFast<UPackage>(nullptr, Redirect.NewName.PackageName);
			FString NewPath = Redirect.NewName.ToString();
			FString OldPath = Redirect.OldName.ToString();

			if (CheckRedirectFlagsMatch(Redirect.RedirectFlags, ECoreRedirectFlags::Type_Class))
			{
				if (Redirect.NewName.PackageName.IsNone())
				{
					UE_LOG(LogCoreRedirects, Warning, TEXT("ValidateRedirect(%s) has missing package for Class redirect from %s to %s!"), *SourceString, *OldPath, *NewPath);
				}
				else
				{
					const UClass* FindClass = FindObject<const UClass>(FTopLevelAssetPath(NewPath));
					if (!FindClass)
					{
						if (NewPackage)
						{
							UE_LOG(LogCoreRedirects, Error, TEXT("ValidateRedirect(%s) failed to find destination Class for redirect from %s to %s with loaded package!"), *SourceString, *OldPath, *NewPath);
						}
						else
						{
							UE_LOG(LogCoreRedirects, Log, TEXT("ValidateRedirect(%s) can't validate destination Class for redirect from %s to %s with unloaded package"), *SourceString, *OldPath, *NewPath);
						}
					}
				}
			}

			if (CheckRedirectFlagsMatch(Redirect.RedirectFlags, ECoreRedirectFlags::Type_Struct))
			{
				if (Redirect.NewName.PackageName.IsNone())
				{
					UE_LOG(LogCoreRedirects, Warning, TEXT("ValidateRedirect(%s) has missing package for Struct redirect from %s to %s!"), *SourceString, *OldPath, *NewPath);
				}
				else
				{
					const UScriptStruct* FindStruct = FindObject<const UScriptStruct>(FTopLevelAssetPath(NewPath));
					if (!FindStruct)
					{
						if (NewPackage)
						{
							UE_LOG(LogCoreRedirects, Error, TEXT("ValidateRedirect(%s) failed to find destination Struct for redirect from %s to %s with loaded package!"), *SourceString, *OldPath, *NewPath);
						}
						else
						{
							UE_LOG(LogCoreRedirects, Log, TEXT("ValidateRedirect(%s) can't validate destination Struct for redirect from %s to %s with unloaded package"), *SourceString, *OldPath, *NewPath);
						}
					}
				}
			}

			if (CheckRedirectFlagsMatch(Redirect.RedirectFlags, ECoreRedirectFlags::Type_Enum))
			{
				if (Redirect.NewName.PackageName.IsNone())
				{
					// If the name is the same that's fine and this is just for value redirects
					if (Redirect.NewName != Redirect.OldName)
					{
						UE_LOG(LogCoreRedirects, Warning, TEXT("ValidateRedirect(%s) has missing package for Enum redirect from %s to %s!"), *SourceString, *OldPath, *NewPath);
					}
				}
				else
				{
					const UEnum* FindEnum = FindObject<const UEnum>(FTopLevelAssetPath(NewPath));
					if (!FindEnum)
					{
						if (NewPackage)
						{
							UE_LOG(LogCoreRedirects, Error, TEXT("ValidateRedirect(%s) failed to find destination Enum for redirect from %s to %s with loaded package!"), *SourceString, *OldPath, *NewPath);
						}
						else
						{
							UE_LOG(LogCoreRedirects, Log, TEXT("ValidateRedirect(%s) can't validate destination Enum for redirect from %s to %s with unloaded package"), *SourceString, *OldPath, *NewPath);
						}
					}
				}
			}
		}
	}
}

void FCoreRedirects::ValidateAllRedirects()
{
	FCoreRedirectorScopeLockForRead ReadLock(RWLock);
	bValidatedOnce = true;

	// Validate all existing redirects

	for (const TPair<ECoreRedirectFlags, FRedirectNameMap>& Pair : RedirectTypeMap)
	{
		ECoreRedirectFlags PairFlags = Pair.Key;
		FString ListName = FString::Printf(TEXT("Type %d"), (int32)PairFlags);

		for (const TPair<FName, TArray<FCoreRedirect> >& ArrayPair : Pair.Value.RedirectMap)
		{
			ValidateRedirectList(ArrayPair.Value, ListName);
		}
	}

	ValidateAssetRedirectsUnderReadLock(ReadLock);
}

bool FCoreRedirects::ValidateAssetRedirects()
{
	FCoreRedirectorScopeLockForRead ReadLock(RWLock);
	return ValidateAssetRedirectsUnderReadLock(ReadLock);
}

const TMap<FName, ECoreRedirectFlags>& FCoreRedirects::GetConfigKeyMap()
{
	// The config key map is only written to during initialization. That allows us to provide unguarded access after initialization
	ensureMsgf(IsInitialized(), TEXT("It is not legal to read the config key map until after FCoreRedirects has been initialized."));
	return ConfigKeyMap;
}

ECoreRedirectFlags FCoreRedirects::GetFlagsForTypeName(FName PackageName, FName TypeName)
{
	if (PackageName == GLongCoreUObjectPackageName)
	{
		if (TypeName == NAME_Class)
		{
			return ECoreRedirectFlags::Type_Class;
		}
		else if (TypeName == NAME_ScriptStruct)
		{
			return ECoreRedirectFlags::Type_Struct;
		}
		else if (TypeName == NAME_Enum)
		{
			return ECoreRedirectFlags::Type_Enum;
		}
		else if (TypeName == NAME_Package)
		{
			return ECoreRedirectFlags::Type_Package;
		}
		else if (TypeName == NAME_Function)
		{
			return ECoreRedirectFlags::Type_Function;
		}

		// If ending with property, it's a property
		if (TypeName.ToString().EndsWith(TEXT("Property")))
		{
			return ECoreRedirectFlags::Type_Property;
		}
	}

	// If ending with GeneratedClass this has to be a class subclass, some of these are in engine or plugins
	if (TypeName.ToString().EndsWith(TEXT("GeneratedClass")))
	{
		return ECoreRedirectFlags::Type_Class;
	}

	if (TypeName == NAME_UserDefinedEnum)
	{
		return ECoreRedirectFlags::Type_Enum;
	}

	return ECoreRedirectFlags::Type_Object;
}

ECoreRedirectFlags FCoreRedirects::GetFlagsForTypeClass(UClass *TypeClass)
{
	// Use Name version for consistency, if we can't figure it out from just the name it isn't safe
	return GetFlagsForTypeName(TypeClass->GetOutermost()->GetFName(), TypeClass->GetFName());
}

// We want to only load these redirects in editor builds, but Matinee needs them at runtime still 

#if UE_WITH_CORE_REDIRECTS

UE_DISABLE_OPTIMIZATION_SHIP

// The compiler doesn't like having a massive string table in a single function so split it up
#define CLASS_REDIRECT(OldName, NewName) Redirects.Emplace_GetRef(ECoreRedirectFlags::Type_Class, TEXT(OldName), TEXT(NewName))
#define CLASS_REDIRECT_INSTANCES(OldName, NewName) Redirects.Emplace_GetRef(ECoreRedirectFlags::Type_Class | ECoreRedirectFlags::Category_InstanceOnly, TEXT(OldName), TEXT(NewName))
#define STRUCT_REDIRECT(OldName, NewName) Redirects.Emplace_GetRef(ECoreRedirectFlags::Type_Struct, TEXT(OldName), TEXT(NewName))
#define ENUM_REDIRECT(OldName, NewName) Redirects.Emplace_GetRef(ECoreRedirectFlags::Type_Enum, TEXT(OldName), TEXT(NewName))
#define PROPERTY_REDIRECT(OldName, NewName) Redirects.Emplace_GetRef(ECoreRedirectFlags::Type_Property, TEXT(OldName), TEXT(NewName))
#define FUNCTION_REDIRECT(OldName, NewName) Redirects.Emplace_GetRef(ECoreRedirectFlags::Type_Function, TEXT(OldName), TEXT(NewName))
#define PACKAGE_REDIRECT(OldName, NewName) Redirects.Emplace_GetRef(ECoreRedirectFlags::Type_Package, TEXT(OldName), TEXT(NewName))

static void RegisterNativeRedirects40(TArray<FCoreRedirect>& Redirects)
{
	// CLASS_REDIRECT("AnimTreeInstance", "/Script/Engine.AnimInstance"); // Leaving some of these disabled as they can cause issues with the reverse class lookup in asset registry scans 
	CLASS_REDIRECT("AnimationCompressionAlgorithm", "/Script/Engine.AnimCompress");
	CLASS_REDIRECT("AnimationCompressionAlgorithm_BitwiseCompressOnly", "/Script/Engine.AnimCompress_BitwiseCompressOnly");
	CLASS_REDIRECT("AnimationCompressionAlgorithm_LeastDestructive", "/Script/Engine.AnimCompress_LeastDestructive");
	CLASS_REDIRECT("AnimationCompressionAlgorithm_PerTrackCompression", "/Script/Engine.AnimCompress_PerTrackCompression");
	CLASS_REDIRECT("AnimationCompressionAlgorithm_RemoveEverySecondKey", "/Script/Engine.AnimCompress_RemoveEverySecondKey");
	CLASS_REDIRECT("AnimationCompressionAlgorithm_RemoveLinearKeys", "/Script/Engine.AnimCompress_RemoveLinearKeys");
	CLASS_REDIRECT("AnimationCompressionAlgorithm_RemoveTrivialKeys", "/Script/Engine.AnimCompress_RemoveTrivialKeys");
	// CLASS_REDIRECT("BlueprintActorBase", "/Script/Engine.Actor");
	CLASS_REDIRECT("DefaultPawnMovement", "/Script/Engine.FloatingPawnMovement");
	CLASS_REDIRECT("DirectionalLightMovable", "/Script/Engine.DirectionalLight");
	CLASS_REDIRECT("DirectionalLightStatic", "/Script/Engine.DirectionalLight");
	CLASS_REDIRECT("DirectionalLightStationary", "/Script/Engine.DirectionalLight");
	CLASS_REDIRECT("DynamicBlockingVolume", "/Script/Engine.BlockingVolume");
	CLASS_REDIRECT("DynamicPhysicsVolume", "/Script/Engine.PhysicsVolume");
	CLASS_REDIRECT("DynamicTriggerVolume", "/Script/Engine.TriggerVolume");
	// CLASS_REDIRECT("GameInfo", "/Script/Engine.GameMode");
	// CLASS_REDIRECT("GameReplicationInfo", "/Script/Engine.GameState");
	CLASS_REDIRECT("InterpActor", "/Script/Engine.StaticMeshActor");
	CLASS_REDIRECT("K2Node_CallSuperFunction", "/Script/BlueprintGraph.K2Node_CallParentFunction");
	CLASS_REDIRECT("MaterialSpriteComponent", "/Script/Engine.MaterialBillboardComponent");
	CLASS_REDIRECT("MovementComp_Character", "/Script/Engine.CharacterMovementComponent");
	CLASS_REDIRECT("MovementComp_Projectile", "/Script/Engine.ProjectileMovementComponent");
	CLASS_REDIRECT("MovementComp_Rotating", "/Script/Engine.RotatingMovementComponent");
	CLASS_REDIRECT("NavAreaDefault", "/Script/NavigationSystem.NavArea_Default");
	CLASS_REDIRECT("NavAreaDefinition", "/Script/NavigationSystem.NavArea");
	CLASS_REDIRECT("NavAreaNull", "/Script/NavigationSystem.NavArea_Null");
	CLASS_REDIRECT("PhysicsActor", "/Script/Engine.StaticMeshActor");
	CLASS_REDIRECT("PhysicsBSJointActor", "/Script/Engine.PhysicsConstraintActor");
	CLASS_REDIRECT("PhysicsHingeActor", "/Script/Engine.PhysicsConstraintActor");
	CLASS_REDIRECT("PhysicsPrismaticActor", "/Script/Engine.PhysicsConstraintActor");
	// CLASS_REDIRECT("PlayerCamera", "/Script/Engine.PlayerCameraManager");
	// CLASS_REDIRECT("PlayerReplicationInfo", "/Script/Engine.PlayerState");
	CLASS_REDIRECT("PointLightMovable", "/Script/Engine.PointLight");
	CLASS_REDIRECT("PointLightStatic", "/Script/Engine.PointLight");
	CLASS_REDIRECT("PointLightStationary", "/Script/Engine.PointLight");
	CLASS_REDIRECT("RB_BSJointSetup", "/Script/Engine.PhysicsConstraintTemplate");
	CLASS_REDIRECT("RB_BodySetup", "/Script/Engine.BodySetup");
	CLASS_REDIRECT("RB_ConstraintActor", "/Script/Engine.PhysicsConstraintActor");
	CLASS_REDIRECT("RB_ConstraintComponent", "/Script/Engine.PhysicsConstraintComponent");
	CLASS_REDIRECT("RB_ConstraintSetup", "/Script/Engine.PhysicsConstraintTemplate");
	CLASS_REDIRECT("RB_Handle", "/Script/Engine.PhysicsHandleComponent");
	CLASS_REDIRECT("RB_HingeSetup", "/Script/Engine.PhysicsConstraintTemplate");
	CLASS_REDIRECT("RB_PrismaticSetup", "/Script/Engine.PhysicsConstraintTemplate");
	CLASS_REDIRECT("RB_RadialForceComponent", "/Script/Engine.RadialForceComponent");
	CLASS_REDIRECT("RB_SkelJointSetup", "/Script/Engine.PhysicsConstraintTemplate");
	CLASS_REDIRECT("RB_Thruster", "/Script/Engine.PhysicsThruster");
	CLASS_REDIRECT("RB_ThrusterComponent", "/Script/Engine.PhysicsThrusterComponent");
	CLASS_REDIRECT("SensingComponent", "/Script/AIModule.PawnSensingComponent");
	CLASS_REDIRECT("SingleAnimSkeletalActor", "/Script/Engine.SkeletalMeshActor");
	CLASS_REDIRECT("SingleAnimSkeletalComponent", "/Script/Engine.SkeletalMeshComponent");
	CLASS_REDIRECT("SkeletalMeshReplicatedComponent", "/Script/Engine.SkeletalMeshComponent");
	CLASS_REDIRECT("SkeletalPhysicsActor", "/Script/Engine.SkeletalMeshActor");
	CLASS_REDIRECT("SoundMode", "/Script/Engine.SoundMix");
	CLASS_REDIRECT("SpotLightMovable", "/Script/Engine.SpotLight");
	CLASS_REDIRECT("SpotLightStatic", "/Script/Engine.SpotLight");
	CLASS_REDIRECT("SpotLightStationary", "/Script/Engine.SpotLight");
	CLASS_REDIRECT("SpriteComponent", "/Script/Engine.BillboardComponent");
	CLASS_REDIRECT("StaticMeshReplicatedComponent", "/Script/Engine.StaticMeshComponent");
	CLASS_REDIRECT("VimBlueprint", "/Script/Engine.AnimBlueprint");
	CLASS_REDIRECT("VimGeneratedClass", "/Script/Engine.AnimBlueprintGeneratedClass");
	CLASS_REDIRECT("VimInstance", "/Script/Engine.AnimInstance");
	CLASS_REDIRECT("WorldInfo", "/Script/Engine.WorldSettings");
	CLASS_REDIRECT_INSTANCES("NavAreaMeta", "/Script/NavigationSystem.NavArea_Default");

	STRUCT_REDIRECT("VimDebugData", "/Script/Engine.AnimBlueprintDebugData");

	FUNCTION_REDIRECT("Actor.GetController", "Pawn.GetController");
	FUNCTION_REDIRECT("Actor.GetTouchingActors", "Actor.GetOverlappingActors");
	PROPERTY_REDIRECT("Actor.GetOverlappingActors.OutTouchingActors", "OverlappingActors");
	FUNCTION_REDIRECT("Actor.GetTouchingComponents", "Actor.GetOverlappingComponents");
	PROPERTY_REDIRECT("Actor.GetOverlappingComponents.TouchingComponents", "OverlappingComponents");
	FUNCTION_REDIRECT("Actor.HasTag", "Actor.ActorHasTag");
	FUNCTION_REDIRECT("Actor.ReceiveActorTouch", "Actor.ReceiveActorBeginOverlap");
	PROPERTY_REDIRECT("Actor.ReceiveActorBeginOverlap.Other", "OtherActor");
	FUNCTION_REDIRECT("Actor.ReceiveActorUntouch", "Actor.ReceiveActorEndOverlap");
	PROPERTY_REDIRECT("Actor.ReceiveActorEndOverlap.Other", "OtherActor");
	PROPERTY_REDIRECT("Actor.ReceiveHit.NormalForce", "NormalImpulse");
	FUNCTION_REDIRECT("Actor.SetActorHidden", "Actor.SetActorHiddenInGame");
	PROPERTY_REDIRECT("Actor.LifeSpan", "Actor.InitialLifeSpan");
	PROPERTY_REDIRECT("Actor.OnActorTouch", "OnActorBeginOverlap");
	PROPERTY_REDIRECT("Actor.OnActorUnTouch", "OnActorEndOverlap");

	FUNCTION_REDIRECT("AnimInstance.GetSequencePlayerLength", "GetAnimAssetPlayerLength");
	FUNCTION_REDIRECT("AnimInstance.GetSequencePlayerTimeFraction", "GetAnimAssetPlayerTimeFraction");
	FUNCTION_REDIRECT("AnimInstance.GetSequencePlayerTimeFromEnd", "GetAnimAssetPlayerTimeFromEnd");
	FUNCTION_REDIRECT("AnimInstance.GetSequencePlayerTimeFromEndFraction", "GetAnimAssetPlayerTimeFromEndFraction");
	FUNCTION_REDIRECT("AnimInstance.KismetInitializeAnimation", "AnimInstance.BlueprintInitializeAnimation");
	FUNCTION_REDIRECT("AnimInstance.KismetUpdateAnimation", "AnimInstance.BlueprintUpdateAnimation");
	PROPERTY_REDIRECT("AnimInstance.GetAnimAssetPlayerLength.Sequence", "AnimAsset");
	PROPERTY_REDIRECT("AnimInstance.GetAnimAssetPlayerTimeFraction.Sequence", "AnimAsset");
	PROPERTY_REDIRECT("AnimInstance.GetAnimAssetPlayerTimeFromEnd.Sequence", "AnimAsset");
	PROPERTY_REDIRECT("AnimInstance.GetAnimAssetPlayerTimeFromEndFraction.Sequence", "AnimAsset");
	PROPERTY_REDIRECT("AnimInstance.VimVertexAnims", "AnimInstance.VertexAnims");

	FUNCTION_REDIRECT("GameplayStatics.ClearSoundMode", "GameplayStatics.ClearSoundMixModifiers");
	FUNCTION_REDIRECT("GameplayStatics.GetGameInfo", "GetGameMode");
	FUNCTION_REDIRECT("GameplayStatics.GetGameReplicationInfo", "GetGameState");
	FUNCTION_REDIRECT("GameplayStatics.GetPlayerCamera", "GameplayStatics.GetPlayerCameraManager");
	FUNCTION_REDIRECT("GameplayStatics.K2_SetSoundMode", "GameplayStatics.SetBaseSoundMix");
	FUNCTION_REDIRECT("GameplayStatics.PopSoundMixModifier.InSoundMode", "InSoundMixModifier");
	FUNCTION_REDIRECT("GameplayStatics.PopSoundMode", "GameplayStatics.PopSoundMixModifier");
	FUNCTION_REDIRECT("GameplayStatics.PushSoundMixModifier.InSoundMode", "InSoundMixModifier");
	FUNCTION_REDIRECT("GameplayStatics.PushSoundMode", "GameplayStatics.PushSoundMixModifier");
	FUNCTION_REDIRECT("GameplayStatics.SetBaseSoundMix.InSoundMode", "InSoundMix");
	FUNCTION_REDIRECT("GameplayStatics.SetTimeDilation", "GameplayStatics.SetGlobalTimeDilation");

	FUNCTION_REDIRECT("KismetMaterialLibrary.CreateMaterialInstanceDynamic", "KismetMaterialLibrary.CreateDynamicMaterialInstance");
	FUNCTION_REDIRECT("KismetMaterialParameterCollectionLibrary.GetScalarParameterValue", "KismetMaterialLibrary.GetScalarParameterValue");
	FUNCTION_REDIRECT("KismetMaterialParameterCollectionLibrary.GetVectorParameterValue", "KismetMaterialLibrary.GetVectorParameterValue");
	FUNCTION_REDIRECT("KismetMaterialParameterCollectionLibrary.SetScalarParameterValue", "KismetMaterialLibrary.SetScalarParameterValue");
	FUNCTION_REDIRECT("KismetMaterialParameterCollectionLibrary.SetVectorParameterValue", "KismetMaterialLibrary.SetVectorParameterValue");

	FUNCTION_REDIRECT("KismetMathLibrary.BreakTransform.Translation", "Location");
	FUNCTION_REDIRECT("KismetMathLibrary.Conv_VectorToTransform.InTranslation", "InLocation");
	FUNCTION_REDIRECT("KismetMathLibrary.FRand", "RandomFloat");
	FUNCTION_REDIRECT("KismetMathLibrary.FRandFromStream", "RandomFloatFromStream");
	FUNCTION_REDIRECT("KismetMathLibrary.FRandRange", "RandomFloatInRange");
	FUNCTION_REDIRECT("KismetMathLibrary.FRandRangeFromStream", "RandomFloatInRangeFromStream");
	FUNCTION_REDIRECT("KismetMathLibrary.InverseTransformPosition", "KismetMathLibrary.InverseTransformLocation");
	PROPERTY_REDIRECT("KismetMathLibrary.InverseTransformLocation.Position", "Location");
	PROPERTY_REDIRECT("KismetMathLibrary.MakeTransform.Translation", "Location");
	FUNCTION_REDIRECT("KismetMathLibrary.Rand", "RandomInteger");
	FUNCTION_REDIRECT("KismetMathLibrary.RandBool", "RandomBool");
	FUNCTION_REDIRECT("KismetMathLibrary.RandBoolFromStream", "RandomBoolFromStream");
	FUNCTION_REDIRECT("KismetMathLibrary.RandFromStream", "RandomIntegerFromStream");
	FUNCTION_REDIRECT("KismetMathLibrary.RandRange", "RandomIntegerInRange");
	FUNCTION_REDIRECT("KismetMathLibrary.RandRangeFromStream", "RandomIntegerInRangeFromStream");
	FUNCTION_REDIRECT("KismetMathLibrary.RotRand", "RandomRotator");
	FUNCTION_REDIRECT("KismetMathLibrary.RotRandFromStream", "RandomRotatorFromStream");
	FUNCTION_REDIRECT("KismetMathLibrary.TransformPosition", "KismetMathLibrary.TransformLocation");
	PROPERTY_REDIRECT("KismetMathLibrary.TransformLocation.Position", "Location");
	FUNCTION_REDIRECT("KismetMathLibrary.VRand", "RandomUnitVector");
	FUNCTION_REDIRECT("KismetMathLibrary.VRandFromStream", "RandomUnitVectorFromStream");

	PROPERTY_REDIRECT("KismetSystemLibrary.CapsuleTraceMultiForObjects.ObjectsToTrace", "ObjectTypes");
	PROPERTY_REDIRECT("KismetSystemLibrary.CapsuleTraceSingleForObjects.ObjectsToTrace", "ObjectTypes");
	PROPERTY_REDIRECT("KismetSystemLibrary.LineTraceMultiForObjects.ObjectsToTrace", "ObjectTypes");
	PROPERTY_REDIRECT("KismetSystemLibrary.LineTraceSingleForObjects.ObjectsToTrace", "ObjectTypes");
	PROPERTY_REDIRECT("KismetSystemLibrary.PrintKismetWarning", "PrintWarning");
	PROPERTY_REDIRECT("KismetSystemLibrary.SphereTraceMultiForObjects.ObjectsToTrace", "ObjectTypes");
	PROPERTY_REDIRECT("KismetSystemLibrary.SphereTraceSingleForObjects.ObjectsToTrace", "ObjectTypes");

	FUNCTION_REDIRECT("AIController.ClearFocus", "AIController.K2_ClearFocus");
	FUNCTION_REDIRECT("AIController.SetFocalPoint", "AIController.K2_SetFocalPoint");
	FUNCTION_REDIRECT("AIController.SetFocus", "AIController.K2_SetFocus");
	FUNCTION_REDIRECT("ArrowComponent.SetArrowColor_New", "ArrowComponent.SetArrowColor");
	FUNCTION_REDIRECT("Character.Launch", "Character.LaunchCharacter");
	FUNCTION_REDIRECT("Controller.K2_GetActorRotation", "Controller.GetControlRotation");
	FUNCTION_REDIRECT("DecalActor.CreateMIDForDecal", "DecalActor.CreateDynamicMaterialInstance");
	FUNCTION_REDIRECT("DecalComponent.CreateMIDForDecal", "DecalComponent.CreateDynamicMaterialInstance");
	PROPERTY_REDIRECT("HUD.AddHitBox.InPos", "Position");
	PROPERTY_REDIRECT("HUD.AddHitBox.InPriority", "Priority");
	PROPERTY_REDIRECT("HUD.AddHitBox.InSize", "Size");
	PROPERTY_REDIRECT("HUD.AddHitBox.bInConsumesInput", "bConsumesInput");
	FUNCTION_REDIRECT("LevelScriptActor.BeginGame", "Actor.ReceiveBeginPlay");
	FUNCTION_REDIRECT("LevelScriptActor.LoadStreamLevel", "GameplayStatics.LoadStreamLevel");
	FUNCTION_REDIRECT("LevelScriptActor.OpenLevel", "GameplayStatics.OpenLevel");
	FUNCTION_REDIRECT("LevelScriptActor.UnloadStreamLevel", "GameplayStatics.UnloadStreamLevel");
	FUNCTION_REDIRECT("MovementComponent.ConstrainPositionToPlane", "MovementComponent.ConstrainLocationToPlane");
	PROPERTY_REDIRECT("MovementComponent.ConstrainLocationToPlane.Position", "Location");
	FUNCTION_REDIRECT("PlayerCameraManager.KismetUpdateCamera", "BlueprintUpdateCamera");
	FUNCTION_REDIRECT("PlayerController.AddLookUpInput", "PlayerController.AddPitchInput");
	FUNCTION_REDIRECT("PlayerController.AddTurnInput", "PlayerController.AddYawInput");
	PROPERTY_REDIRECT("PlayerController.DeprojectMousePositionToWorld.Direction", "WorldDirection");
	PROPERTY_REDIRECT("PlayerController.DeprojectMousePositionToWorld.WorldPosition", "WorldLocation");
	FUNCTION_REDIRECT("PrimitiveComponent.AddForceAtPosition", "PrimitiveComponent.AddForceAtLocation");
	PROPERTY_REDIRECT("PrimitiveComponent.AddForceAtLocation.Position", "Location");
	FUNCTION_REDIRECT("PrimitiveComponent.AddImpulseAtPosition", "PrimitiveComponent.AddImpulseAtLocation");
	PROPERTY_REDIRECT("PrimitiveComponent.AddImpulseAtLocation.Position", "Location");
	FUNCTION_REDIRECT("PrimitiveComponent.CreateAndSetMaterialInstanceDynamic", "PrimitiveComponent.CreateDynamicMaterialInstance");
	FUNCTION_REDIRECT("PrimitiveComponent.CreateAndSetMaterialInstanceDynamicFromMaterial", "PrimitiveComponent.CreateDynamicMaterialInstance");
	PROPERTY_REDIRECT("PrimitiveComponent.CreateDynamicMaterialInstance.Parent", "SourceMaterial");
	FUNCTION_REDIRECT("PrimitiveComponent.GetRBAngularVelocity", "GetPhysicsAngularVelocity");
	FUNCTION_REDIRECT("PrimitiveComponent.GetRBLinearVelocity", "GetPhysicsLinearVelocity");
	FUNCTION_REDIRECT("PrimitiveComponent.GetTouchingActors", "PrimitiveComponent.GetOverlappingActors");
	PROPERTY_REDIRECT("PrimitiveComponent.GetOverlappingActors.TouchingActors", "OverlappingActors");
	FUNCTION_REDIRECT("PrimitiveComponent.GetTouchingComponents", "PrimitiveComponent.GetOverlappingComponents");
	PROPERTY_REDIRECT("PrimitiveComponent.GetOverlappingComponents.TouchingComponents", "OverlappingComponents");
	FUNCTION_REDIRECT("PrimitiveComponent.KismetTraceComponent", "PrimitiveComponent.K2_LineTraceComponent");
	FUNCTION_REDIRECT("PrimitiveComponent.SetAllRBLinearVelocity", "SetAllPhysicsLinearVelocity");
	FUNCTION_REDIRECT("PrimitiveComponent.SetMovementChannel", "PrimitiveComponent.SetCollisionObjectType");
	FUNCTION_REDIRECT("PrimitiveComponent.SetRBAngularVelocity", "SetPhysicsAngularVelocity");
	FUNCTION_REDIRECT("PrimitiveComponent.SetRBLinearVelocity", "SetPhysicsLinearVelocity");
	FUNCTION_REDIRECT("ProjectileMovementComponent.StopMovement", "ProjectileMovementComponent.StopSimulating");
	FUNCTION_REDIRECT("SceneComponent.GetComponentToWorld", "K2_GetComponentToWorld");
	FUNCTION_REDIRECT("SingleAnimSkeletalComponent.GetPlayRate", "SkeletalMeshComponent.GetPlayRate");
	FUNCTION_REDIRECT("SingleAnimSkeletalComponent.GetPosition", "SkeletalMeshComponent.GetPosition");
	FUNCTION_REDIRECT("SingleAnimSkeletalComponent.IsPlaying", "SkeletalMeshComponent.IsPlaying");
	FUNCTION_REDIRECT("SingleAnimSkeletalComponent.Play", "SkeletalMeshComponent.Play");
	FUNCTION_REDIRECT("SingleAnimSkeletalComponent.SetAnim", "SkeletalMeshComponent.SetAnimation");
	FUNCTION_REDIRECT("SingleAnimSkeletalComponent.SetPlayRate", "SkeletalMeshComponent.SetPlayRate");
	FUNCTION_REDIRECT("SingleAnimSkeletalComponent.SetPosition", "SkeletalMeshComponent.SetPosition");
	FUNCTION_REDIRECT("SingleAnimSkeletalComponent.Stop", "SkeletalMeshComponent.Stop");
	FUNCTION_REDIRECT("SkinnedMeshComponent.MatchRefBone", "SkinnedMeshComponent.GetBoneIndex");

	PROPERTY_REDIRECT("AnimNotifyEvent.Time", "AnimNotifyEvent.DisplayTime");
	PROPERTY_REDIRECT("AnimSequence.BasePose", "AnimSequence.RetargetSource");
	PROPERTY_REDIRECT("AudioComponent.PitchMultiplierMax", "AudioComponent.PitchModulationMax");
	PROPERTY_REDIRECT("AudioComponent.PitchMultiplierMin", "AudioComponent.PitchModulationMin");
	PROPERTY_REDIRECT("AudioComponent.VolumeMultiplierMax", "AudioComponent.VolumeModulationMax");
	PROPERTY_REDIRECT("AudioComponent.VolumeMultiplierMin", "AudioComponent.VolumeModulationMin");
	PROPERTY_REDIRECT("BodyInstance.MovementChannel", "BodyInstance.ObjectType");
	PROPERTY_REDIRECT("BranchingPoint.Time", "BranchingPoint.DisplayTime");
	PROPERTY_REDIRECT("CapsuleComponent.CapsuleHeight", "CapsuleComponent.CapsuleHalfHeight");
	PROPERTY_REDIRECT("CharacterMovementComponent.AccelRate", "CharacterMovementComponent.MaxAcceleration");
	PROPERTY_REDIRECT("CharacterMovementComponent.BrakingDeceleration", "CharacterMovementComponent.BrakingDecelerationWalking");
	PROPERTY_REDIRECT("CharacterMovementComponent.CrouchHeight", "CharacterMovementComponent.CrouchedHalfHeight");
	PROPERTY_REDIRECT("CollisionResponseContainer.Dynamic", "CollisionResponseContainer.WorldDynamic");
	PROPERTY_REDIRECT("CollisionResponseContainer.RigidBody", "CollisionResponseContainer.PhysicsBody");
	PROPERTY_REDIRECT("CollisionResponseContainer.Static", "CollisionResponseContainer.WorldStatic");
	PROPERTY_REDIRECT("Controller.PlayerReplicationInfo", "Controller.PlayerState");
	PROPERTY_REDIRECT("DefaultPawn.DefaultPawnMovement", "DefaultPawn.MovementComponent");
	PROPERTY_REDIRECT("DirectionalLightComponent.MovableWholeSceneDynamicShadowRadius", "DirectionalLightComponent.DynamicShadowDistanceMovableLight");
	PROPERTY_REDIRECT("DirectionalLightComponent.StationaryWholeSceneDynamicShadowRadius", "DirectionalLightComponent.DynamicShadowDistanceStationaryLight");
	PROPERTY_REDIRECT("FloatingPawnMovement.AccelRate", "FloatingPawnMovement.Acceleration");
	PROPERTY_REDIRECT("FloatingPawnMovement.DecelRate", "FloatingPawnMovement.Deceleration");
	PROPERTY_REDIRECT("GameMode.GameReplicationInfoClass", "GameMode.GameStateClass");
	PROPERTY_REDIRECT("GameMode.PlayerReplicationInfoClass", "GameMode.PlayerStateClass");
	PROPERTY_REDIRECT("GameState.GameClass", "GameState.GameModeClass");
	PROPERTY_REDIRECT("K2Node_TransitionRuleGetter.AssociatedSequencePlayerNode", "K2Node_TransitionRuleGetter.AssociatedAnimAssetPlayerNode");
	PROPERTY_REDIRECT("LightComponent.InverseSquaredFalloff", "PointLightComponent.bUseInverseSquaredFalloff");
	PROPERTY_REDIRECT("LightComponentBase.Brightness", "LightComponentBase.Intensity");
	PROPERTY_REDIRECT("Material.RefractionBias", "Material.RefractionDepthBias");
	PROPERTY_REDIRECT("MaterialEditorInstanceConstant.RefractionBias", "MaterialEditorInstanceConstant.RefractionDepthBias");
	PROPERTY_REDIRECT("NavLinkProxy.NavLinks", "NavLinkProxy.PointLinks");
	PROPERTY_REDIRECT("NavLinkProxy.NavSegmentLinks", "NavLinkProxy.SegmentLinks");
	PROPERTY_REDIRECT("Pawn.ControllerClass", "Pawn.AIControllerClass");
	PROPERTY_REDIRECT("Pawn.PlayerReplicationInfo", "Pawn.PlayerState");
	PROPERTY_REDIRECT("PawnSensingComponent.SightCounterInterval", "PawnSensingComponent.SensingInterval");
	PROPERTY_REDIRECT("PawnSensingComponent.bWantsSeePlayerNotify", "PawnSensingComponent.bSeePawns");
	PROPERTY_REDIRECT("PlayerController.LookRightScale", "PlayerController.InputYawScale");
	PROPERTY_REDIRECT("PlayerController.LookUpScale", "PlayerController.InputPitchScale");
	PROPERTY_REDIRECT("PlayerController.InputYawScale", "PlayerController.InputYawScale_DEPRECATED");
	PROPERTY_REDIRECT("PlayerController.InputPitchScale", "PlayerController.InputPitchScale_DEPRECATED");
	PROPERTY_REDIRECT("PlayerController.InputRollScale", "PlayerController.InputRollScale_DEPRECATED");
	PROPERTY_REDIRECT("PlayerController.PlayerCamera", "PlayerController.PlayerCameraManager");
	PROPERTY_REDIRECT("PlayerController.PlayerCameraClass", "PlayerController.PlayerCameraManagerClass");
	PROPERTY_REDIRECT("PointLightComponent.Radius", "PointLightComponent.AttenuationRadius");
	PROPERTY_REDIRECT("PostProcessSettings.ExposureOffset", "PostProcessSettings.AutoExposureBias");
	PROPERTY_REDIRECT("PostProcessSettings.EyeAdaptationHighPercent", "PostProcessSettings.AutoExposureHighPercent");
	PROPERTY_REDIRECT("PostProcessSettings.EyeAdaptationLowPercent", "PostProcessSettings.AutoExposureLowPercent");
	PROPERTY_REDIRECT("PostProcessSettings.EyeAdaptationMaxBrightness", "PostProcessSettings.AutoExposureMaxBrightness");
	PROPERTY_REDIRECT("PostProcessSettings.EyeAdaptationMinBrightness", "PostProcessSettings.AutoExposureMinBrightness");
	PROPERTY_REDIRECT("PostProcessSettings.EyeAdaptionSpeedDown", "PostProcessSettings.AutoExposureSpeedDown");
	PROPERTY_REDIRECT("PostProcessSettings.EyeAdaptionSpeedUp", "PostProcessSettings.AutoExposureSpeedUp");
	PROPERTY_REDIRECT("PostProcessSettings.bOverride_ExposureOffset", "PostProcessSettings.bOverride_AutoExposureBias");
	PROPERTY_REDIRECT("PostProcessSettings.bOverride_EyeAdaptationHighPercent", "PostProcessSettings.bOverride_AutoExposureHighPercent");
	PROPERTY_REDIRECT("PostProcessSettings.bOverride_EyeAdaptationLowPercent", "PostProcessSettings.bOverride_AutoExposureLowPercent");
	PROPERTY_REDIRECT("PostProcessSettings.bOverride_EyeAdaptationMaxBrightness", "PostProcessSettings.bOverride_AutoExposureMaxBrightness");
	PROPERTY_REDIRECT("PostProcessSettings.bOverride_EyeAdaptationMinBrightness", "PostProcessSettings.bOverride_AutoExposureMinBrightness");
	PROPERTY_REDIRECT("PostProcessSettings.bOverride_EyeAdaptionSpeedDown", "PostProcessSettings.bOverride_AutoExposureSpeedDown");
	PROPERTY_REDIRECT("PostProcessSettings.bOverride_EyeAdaptionSpeedUp", "PostProcessSettings.bOverride_AutoExposureSpeedUp");
	PROPERTY_REDIRECT("SceneComponent.ModifyFrequency", "SceneComponent.Mobility");
	PROPERTY_REDIRECT("SceneComponent.RelativeTranslation", "SceneComponent.RelativeLocation");
	PROPERTY_REDIRECT("SceneComponent.bAbsoluteTranslation", "SceneComponent.bAbsoluteLocation");
	PROPERTY_REDIRECT("SceneComponent.bComputeBoundsOnceDuringCook", "SceneComponent.bComputeBoundsOnceForGame");
	PROPERTY_REDIRECT("SkeletalMeshComponent.AnimationBlueprint", "SkeletalMeshComponent.AnimBlueprintGeneratedClass");
	PROPERTY_REDIRECT("SlateBrush.TextureName", "SlateBrush.ResourceName");
	PROPERTY_REDIRECT("SlateBrush.TextureObject", "SlateBrush.ResourceObject");
	PROPERTY_REDIRECT("WorldSettings.DefaultGameType", "WorldSettings.DefaultGameMode");

	FCoreRedirect& PointLightComponent = CLASS_REDIRECT("PointLightComponent", "/Script/Engine.PointLightComponent");
	PointLightComponent.ValueChanges.Add(TEXT("PointLightComponent0"), TEXT("LightComponent0"));

	FCoreRedirect& DirectionalLightComponent = CLASS_REDIRECT("DirectionalLightComponent", "/Script/Engine.DirectionalLightComponent");
	DirectionalLightComponent.ValueChanges.Add(TEXT("DirectionalLightComponent0"), TEXT("LightComponent0"));

	FCoreRedirect& SpotLightComponent = CLASS_REDIRECT("SpotLightComponent", "/Script/Engine.SpotLightComponent");
	SpotLightComponent.ValueChanges.Add(TEXT("SpotLightComponent0"), TEXT("LightComponent0"));

	FCoreRedirect& ETransitionGetterType = ENUM_REDIRECT("ETransitionGetterType", "/Script/AnimGraph.ETransitionGetter");
	ETransitionGetterType.ValueChanges.Add(TEXT("TGT_ArbitraryState_GetBlendWeight"), TEXT("ETransitionGetter::ArbitraryState_GetBlendWeight"));
	ETransitionGetterType.ValueChanges.Add(TEXT("TGT_CurrentState_ElapsedTime"), TEXT("ETransitionGetter::CurrentState_ElapsedTime"));
	ETransitionGetterType.ValueChanges.Add(TEXT("TGT_CurrentState_GetBlendWeight"), TEXT("ETransitionGetter::CurrentState_GetBlendWeight"));
	ETransitionGetterType.ValueChanges.Add(TEXT("TGT_CurrentTransitionDuration"), TEXT("ETransitionGetter::CurrentTransitionDuration"));
	ETransitionGetterType.ValueChanges.Add(TEXT("TGT_SequencePlayer_GetCurrentTime"), TEXT("ETransitionGetter::AnimationAsset_GetCurrentTime"));
	ETransitionGetterType.ValueChanges.Add(TEXT("TGT_SequencePlayer_GetCurrentTimeFraction"), TEXT("ETransitionGetter::AnimationAsset_GetCurrentTimeFraction"));
	ETransitionGetterType.ValueChanges.Add(TEXT("TGT_SequencePlayer_GetLength"), TEXT("ETransitionGetter::AnimationAsset_GetLength"));
	ETransitionGetterType.ValueChanges.Add(TEXT("TGT_SequencePlayer_GetTimeFromEnd"), TEXT("ETransitionGetter::AnimationAsset_GetTimeFromEnd"));
	ETransitionGetterType.ValueChanges.Add(TEXT("TGT_SequencePlayer_GetTimeFromEndFraction"), TEXT("ETransitionGetter::AnimationAsset_GetTimeFromEndFraction"));

	FCoreRedirect& EModifyFrequency = ENUM_REDIRECT("EModifyFrequency", "/Script/Engine.EComponentMobility");
	EModifyFrequency.ValueChanges.Add(TEXT("MF_Dynamic"), TEXT("EComponentMobility::Movable"));
	EModifyFrequency.ValueChanges.Add(TEXT("MF_OccasionallyModified"), TEXT("EComponentMobility::Stationary"));
	EModifyFrequency.ValueChanges.Add(TEXT("MF_Static"), TEXT("EComponentMobility::Static"));

	FCoreRedirect& EAttachLocationType = ENUM_REDIRECT("EAttachLocationType", "/Script/Engine.EAttachLocation");
	EAttachLocationType.ValueChanges.Add(TEXT("EAttachLocationType_AbsoluteWorld"), TEXT("EAttachLocation::KeepWorldPosition"));
	EAttachLocationType.ValueChanges.Add(TEXT("EAttachLocationType_RelativeOffset"), TEXT("EAttachLocation::KeepRelativeOffset"));
	EAttachLocationType.ValueChanges.Add(TEXT("EAttachLocationType_SnapTo"), TEXT("EAttachLocation::SnapToTarget"));

	FCoreRedirect& EAxis = ENUM_REDIRECT("EAxis", "/Script/CoreUObject.EAxis");
	EAxis.ValueChanges.Add(TEXT("AXIS_BLANK"), TEXT("EAxis::None"));
	EAxis.ValueChanges.Add(TEXT("AXIS_NONE"), TEXT("EAxis::None"));
	EAxis.ValueChanges.Add(TEXT("AXIS_X"), TEXT("EAxis::X"));
	EAxis.ValueChanges.Add(TEXT("AXIS_Y"), TEXT("EAxis::Y"));
	EAxis.ValueChanges.Add(TEXT("AXIS_Z"), TEXT("EAxis::Z"));

	FCoreRedirect& EMaxConcurrentResolutionRule = ENUM_REDIRECT("EMaxConcurrentResolutionRule", "/Script/Engine.EMaxConcurrentResolutionRule");
	EMaxConcurrentResolutionRule.ValueChanges.Add(TEXT("EMaxConcurrentResolutionRule::StopFarthest"), TEXT("EMaxConcurrentResolutionRule::StopFarthestThenPreventNew"));

	FCoreRedirect& EParticleEventType = ENUM_REDIRECT("EParticleEventType", "/Script/Engine.EParticleEventType");
	EParticleEventType.ValueChanges.Add(TEXT("EPET_Kismet"), TEXT("EPET_Blueprint"));

	FCoreRedirect& ETranslucencyLightingMode = ENUM_REDIRECT("ETranslucencyLightingMode", "/Script/Engine.ETranslucencyLightingMode");
	ETranslucencyLightingMode.ValueChanges.Add(TEXT("TLM_PerPixel"), TEXT("TLM_VolumetricDirectional"));
	ETranslucencyLightingMode.ValueChanges.Add(TEXT("TLM_PerPixelNonDirectional"), TEXT("TLM_VolumetricNonDirectional"));
}

static void RegisterNativeRedirects46(TArray<FCoreRedirect>& Redirects)
{
	// 4.1-4.4 

	CLASS_REDIRECT("K2Node_CastToInterface", "/Script/BlueprintGraph.K2Node_DynamicCast");
	CLASS_REDIRECT("K2Node_MathExpression", "/Script/BlueprintGraph.K2Node_MathExpression");
	CLASS_REDIRECT("EmitterSpawnable", "/Script/Engine.Emitter");
	CLASS_REDIRECT("SlateWidgetStyleAsset", "/Script/SlateCore.SlateWidgetStyleAsset");
	CLASS_REDIRECT("SlateWidgetStyleContainerBase", "/Script/SlateCore.SlateWidgetStyleContainerBase");
	CLASS_REDIRECT("SmartNavLinkComponent", "/Script/NavigationSystem.NavLinkCustomComponent");
	CLASS_REDIRECT("WidgetBlueprint", "/Script/UMGEditor.WidgetBlueprint");

	PROPERTY_REDIRECT("AnimNotify.Received_Notify.AnimSeq", "Animation");
	PROPERTY_REDIRECT("AnimNotifyState.Received_NotifyBegin.AnimSeq", "Animation");
	PROPERTY_REDIRECT("AnimNotifyState.Received_NotifyEnd.AnimSeq", "Animation");
	PROPERTY_REDIRECT("AnimNotifyState.Received_NotifyTick.AnimSeq", "Animation");
	FUNCTION_REDIRECT("Character.IsJumping", "Character.IsJumpProvidingForce");
	PROPERTY_REDIRECT("CharacterMovementComponent.AddImpulse.InMomentum", "Impulse");
	PROPERTY_REDIRECT("CharacterMovementComponent.AddImpulse.bMassIndependent", "bVelocityChange");
	FUNCTION_REDIRECT("CharacterMovementComponent.AddMomentum", "CharacterMovementComponent.AddImpulse");
	FUNCTION_REDIRECT("Controller.GetControlledPawn", "Controller.K2_GetPawn");
	FUNCTION_REDIRECT("DefaultPawn.LookUp", "Pawn.AddControllerPitchInput");
	FUNCTION_REDIRECT("DefaultPawn.Turn", "Pawn.AddControllerYawInput");
	FUNCTION_REDIRECT("KismetSystemLibrary.EXPERIMENTAL_ShowGameCenterLeaderboard", "KismetSystemLibrary.ShowPlatformSpecificLeaderboardScreen");
	FUNCTION_REDIRECT("MovementComponent.GetMaxSpeedModifier", "MovementComponent.K2_GetMaxSpeedModifier");
	FUNCTION_REDIRECT("MovementComponent.GetModifiedMaxSpeed", "MovementComponent.K2_GetModifiedMaxSpeed");
	FUNCTION_REDIRECT("Pawn.AddLookUpInput", "Pawn.AddControllerPitchInput");
	FUNCTION_REDIRECT("Pawn.AddPitchInput", "Pawn.AddControllerPitchInput");
	FUNCTION_REDIRECT("Pawn.AddRollInput", "Pawn.AddControllerRollInput");
	FUNCTION_REDIRECT("Pawn.AddTurnInput", "Pawn.AddControllerYawInput");
	FUNCTION_REDIRECT("Pawn.AddYawInput", "Pawn.AddControllerYawInput");
	FUNCTION_REDIRECT("PawnMovementComponent.StopActiveMovement", "NavMovementComponent.StopActiveMovement");
	FUNCTION_REDIRECT("PointLightComponent.SetRadius", "PointLightComponent.SetAttenuationRadius");
	FUNCTION_REDIRECT("SkeletalMeshComponent.SetAnimBlueprint", "SkeletalMeshComponent.SetAnimInstanceClass");
	FUNCTION_REDIRECT("SkeletalMeshComponent.SetAnimClass", "SkeletalMeshComponent.SetAnimInstanceClass");
	PROPERTY_REDIRECT("SkeletalMeshComponent.SetAnimInstanceClass.NewBlueprint", "NewClass");

	PROPERTY_REDIRECT("StringClassReference.ClassName", "StringClassReference.AssetLongPathname");
	PROPERTY_REDIRECT("Material.LightingModel", "Material.ShadingModel");
	PROPERTY_REDIRECT("MaterialInstanceBasePropertyOverrides.LightingModel", "MaterialInstanceBasePropertyOverrides.ShadingModel");
	PROPERTY_REDIRECT("MaterialInstanceBasePropertyOverrides.bOverride_LightingModel", "MaterialInstanceBasePropertyOverrides.bOverride_ShadingModel");
	PROPERTY_REDIRECT("PassiveSoundMixModifier.VolumeThreshold", "PassiveSoundMixModifier.MinVolumeThreshold");
	PROPERTY_REDIRECT("PrimitiveComponent.CanBeCharacterBase", "PrimitiveComponent.CanCharacterStepUpOn");
	PROPERTY_REDIRECT("SkeletalMeshLODInfo.DisplayFactor", "SkeletalMeshLODInfo.ScreenSize");
	PROPERTY_REDIRECT("SplineMeshComponent.SplineXDir", "SplineMeshComponent.SplineUpDir");
	PROPERTY_REDIRECT("TextureFactory.LightingModel", "TextureFactory.ShadingModel");

	FCoreRedirect& EKinematicBonesUpdateToPhysics = ENUM_REDIRECT("EKinematicBonesUpdateToPhysics", "/Script/Engine.EKinematicBonesUpdateToPhysics");
	EKinematicBonesUpdateToPhysics.ValueChanges.Add(TEXT("EKinematicBonesUpdateToPhysics::SkipFixedAndSimulatingBones"), TEXT("EKinematicBonesUpdateToPhysics::SkipAllBones"));

	FCoreRedirect& EMaterialLightingModel = ENUM_REDIRECT("EMaterialLightingModel", "/Script/Engine.EMaterialShadingModel");
	EMaterialLightingModel.ValueChanges.Add(TEXT("MLM_DefaultLit"), TEXT("MSM_DefaultLit"));
	EMaterialLightingModel.ValueChanges.Add(TEXT("MLM_PreintegratedSkin"), TEXT("MSM_PreintegratedSkin"));
	EMaterialLightingModel.ValueChanges.Add(TEXT("MLM_Subsurface"), TEXT("MSM_Subsurface"));
	EMaterialLightingModel.ValueChanges.Add(TEXT("MLM_Unlit"), TEXT("MSM_Unlit"));

	FCoreRedirect& ESmartNavLinkDir = ENUM_REDIRECT("ESmartNavLinkDir", "/Script/Engine.ENavLinkDirection");
	ESmartNavLinkDir.ValueChanges.Add(TEXT("ESmartNavLinkDir::BothWays"), TEXT("ENavLinkDirection::BothWays"));
	ESmartNavLinkDir.ValueChanges.Add(TEXT("ESmartNavLinkDir::OneWay"), TEXT("ENavLinkDirection::LeftToRight"));

	// 4.5

	CLASS_REDIRECT("AIController", "/Script/AIModule.AIController");
	CLASS_REDIRECT("AIResourceInterface", "/Script/AIModule.AIResourceInterface");
	CLASS_REDIRECT("AISystem", "/Script/AIModule.AISystem");
	CLASS_REDIRECT("BTAuxiliaryNode", "/Script/AIModule.BTAuxiliaryNode");
	CLASS_REDIRECT("BTCompositeNode", "/Script/AIModule.BTCompositeNode");
	CLASS_REDIRECT("BTComposite_Selector", "/Script/AIModule.BTComposite_Selector");
	CLASS_REDIRECT("BTComposite_Sequence", "/Script/AIModule.BTComposite_Sequence");
	CLASS_REDIRECT("BTComposite_SimpleParallel", "/Script/AIModule.BTComposite_SimpleParallel");
	CLASS_REDIRECT("BTDecorator", "/Script/AIModule.BTDecorator");
	CLASS_REDIRECT("BTDecorator_Blackboard", "/Script/AIModule.BTDecorator_Blackboard");
	CLASS_REDIRECT("BTDecorator_BlackboardBase", "/Script/AIModule.BTDecorator_BlackboardBase");
	CLASS_REDIRECT("BTDecorator_BlueprintBase", "/Script/AIModule.BTDecorator_BlueprintBase");
	CLASS_REDIRECT("BTDecorator_CompareBBEntries", "/Script/AIModule.BTDecorator_CompareBBEntries");
	CLASS_REDIRECT("BTDecorator_ConeCheck", "/Script/AIModule.BTDecorator_ConeCheck");
	CLASS_REDIRECT("BTDecorator_Cooldown", "/Script/AIModule.BTDecorator_Cooldown");
	CLASS_REDIRECT("BTDecorator_DoesPathExist", "/Script/AIModule.BTDecorator_DoesPathExist");
	CLASS_REDIRECT("BTDecorator_ForceSuccess", "/Script/AIModule.BTDecorator_ForceSuccess");
	CLASS_REDIRECT("BTDecorator_KeepInCone", "/Script/AIModule.BTDecorator_KeepInCone");
	CLASS_REDIRECT("BTDecorator_Loop", "/Script/AIModule.BTDecorator_Loop");
	CLASS_REDIRECT("BTDecorator_Optional", "/Script/AIModule.BTDecorator_ForceSuccess");
	CLASS_REDIRECT("BTDecorator_ReachedMoveGoal", "/Script/AIModule.BTDecorator_ReachedMoveGoal");
	CLASS_REDIRECT("BTDecorator_TimeLimit", "/Script/AIModule.BTDecorator_TimeLimit");
	CLASS_REDIRECT("BTFunctionLibrary", "/Script/AIModule.BTFunctionLibrary");
	CLASS_REDIRECT("BTNode", "/Script/AIModule.BTNode");
	CLASS_REDIRECT("BTService", "/Script/AIModule.BTService");
	CLASS_REDIRECT("BTService_BlackboardBase", "/Script/AIModule.BTService_BlackboardBase");
	CLASS_REDIRECT("BTService_BlueprintBase", "/Script/AIModule.BTService_BlueprintBase");
	CLASS_REDIRECT("BTService_DefaultFocus", "/Script/AIModule.BTService_DefaultFocus");
	CLASS_REDIRECT("BTTaskNode", "/Script/AIModule.BTTaskNode");
	CLASS_REDIRECT("BTTask_BlackboardBase", "/Script/AIModule.BTTask_BlackboardBase");
	CLASS_REDIRECT("BTTask_BlueprintBase", "/Script/AIModule.BTTask_BlueprintBase");
	CLASS_REDIRECT("BTTask_MakeNoise", "/Script/AIModule.BTTask_MakeNoise");
	CLASS_REDIRECT("BTTask_MoveDirectlyToward", "/Script/AIModule.BTTask_MoveDirectlyToward");
	CLASS_REDIRECT("BTTask_MoveTo", "/Script/AIModule.BTTask_MoveTo");
	CLASS_REDIRECT("BTTask_PlaySound", "/Script/AIModule.BTTask_PlaySound");
	CLASS_REDIRECT("BTTask_RunBehavior", "/Script/AIModule.BTTask_RunBehavior");
	CLASS_REDIRECT("BTTask_RunEQSQuery", "/Script/AIModule.BTTask_RunEQSQuery");
	CLASS_REDIRECT("BTTask_Wait", "/Script/AIModule.BTTask_Wait");
	CLASS_REDIRECT("BehaviorTree", "/Script/AIModule.BehaviorTree");
	CLASS_REDIRECT("BehaviorTreeComponent", "/Script/AIModule.BehaviorTreeComponent");
	CLASS_REDIRECT("BehaviorTreeManager", "/Script/AIModule.BehaviorTreeManager");
	CLASS_REDIRECT("BehaviorTreeTypes", "/Script/AIModule.BehaviorTreeTypes");
	CLASS_REDIRECT("BlackboardComponent", "/Script/AIModule.BlackboardComponent");
	CLASS_REDIRECT("BlackboardData", "/Script/AIModule.BlackboardData");
	CLASS_REDIRECT("BlackboardKeyType", "/Script/AIModule.BlackboardKeyType");
	CLASS_REDIRECT("BlackboardKeyType_Bool", "/Script/AIModule.BlackboardKeyType_Bool");
	CLASS_REDIRECT("BlackboardKeyType_Class", "/Script/AIModule.BlackboardKeyType_Class");
	CLASS_REDIRECT("BlackboardKeyType_Enum", "/Script/AIModule.BlackboardKeyType_Enum");
	CLASS_REDIRECT("BlackboardKeyType_Float", "/Script/AIModule.BlackboardKeyType_Float");
	CLASS_REDIRECT("BlackboardKeyType_Int", "/Script/AIModule.BlackboardKeyType_Int");
	CLASS_REDIRECT("BlackboardKeyType_Name", "/Script/AIModule.BlackboardKeyType_Name");
	CLASS_REDIRECT("BlackboardKeyType_NativeEnum", "/Script/AIModule.BlackboardKeyType_NativeEnum");
	CLASS_REDIRECT("BlackboardKeyType_Object", "/Script/AIModule.BlackboardKeyType_Object");
	CLASS_REDIRECT("BlackboardKeyType_String", "/Script/AIModule.BlackboardKeyType_String");
	CLASS_REDIRECT("BlackboardKeyType_Vector", "/Script/AIModule.BlackboardKeyType_Vector");
	CLASS_REDIRECT("BrainComponent", "/Script/AIModule.BrainComponent");
	CLASS_REDIRECT("CrowdAgentInterface", "/Script/AIModule.CrowdAgentInterface");
	CLASS_REDIRECT("CrowdFollowingComponent", "/Script/AIModule.CrowdFollowingComponent");
	CLASS_REDIRECT("CrowdManager", "/Script/AIModule.CrowdManager");
	CLASS_REDIRECT("EQSQueryResultSourceInterface", "/Script/AIModule.EQSQueryResultSourceInterface");
	CLASS_REDIRECT("EQSRenderingComponent", "/Script/AIModule.EQSRenderingComponent");
	CLASS_REDIRECT("EQSTestingPawn", "/Script/AIModule.EQSTestingPawn");
	CLASS_REDIRECT("EnvQuery", "/Script/AIModule.EnvQuery");
	CLASS_REDIRECT("EnvQueryContext", "/Script/AIModule.EnvQueryContext");
	CLASS_REDIRECT("EnvQueryContext_BlueprintBase", "/Script/AIModule.EnvQueryContext_BlueprintBase");
	CLASS_REDIRECT("EnvQueryContext_Item", "/Script/AIModule.EnvQueryContext_Item");
	CLASS_REDIRECT("EnvQueryContext_Querier", "/Script/AIModule.EnvQueryContext_Querier");
	CLASS_REDIRECT("EnvQueryGenerator", "/Script/AIModule.EnvQueryGenerator");
	CLASS_REDIRECT("EnvQueryGenerator_Composite", "/Script/AIModule.EnvQueryGenerator_Composite");
	CLASS_REDIRECT("EnvQueryGenerator_OnCircle", "/Script/AIModule.EnvQueryGenerator_OnCircle");
	CLASS_REDIRECT("EnvQueryGenerator_PathingGrid", "/Script/AIModule.EnvQueryGenerator_PathingGrid");
	CLASS_REDIRECT("EnvQueryGenerator_ProjectedPoints", "/Script/AIModule.EnvQueryGenerator_ProjectedPoints");
	CLASS_REDIRECT("EnvQueryGenerator_SimpleGrid", "/Script/AIModule.EnvQueryGenerator_SimpleGrid");
	CLASS_REDIRECT("EnvQueryItemType", "/Script/AIModule.EnvQueryItemType");
	CLASS_REDIRECT("EnvQueryItemType_Actor", "/Script/AIModule.EnvQueryItemType_Actor");
	CLASS_REDIRECT("EnvQueryItemType_ActorBase", "/Script/AIModule.EnvQueryItemType_ActorBase");
	CLASS_REDIRECT("EnvQueryItemType_Direction", "/Script/AIModule.EnvQueryItemType_Direction");
	CLASS_REDIRECT("EnvQueryItemType_Point", "/Script/AIModule.EnvQueryItemType_Point");
	CLASS_REDIRECT("EnvQueryItemType_VectorBase", "/Script/AIModule.EnvQueryItemType_VectorBase");
	CLASS_REDIRECT("EnvQueryManager", "/Script/AIModule.EnvQueryManager");
	CLASS_REDIRECT("EnvQueryOption", "/Script/AIModule.EnvQueryOption");
	CLASS_REDIRECT("EnvQueryTest", "/Script/AIModule.EnvQueryTest");
	CLASS_REDIRECT("EnvQueryTest_Distance", "/Script/AIModule.EnvQueryTest_Distance");
	CLASS_REDIRECT("EnvQueryTest_Dot", "/Script/AIModule.EnvQueryTest_Dot");
	CLASS_REDIRECT("EnvQueryTest_Pathfinding", "/Script/AIModule.EnvQueryTest_Pathfinding");
	CLASS_REDIRECT("EnvQueryTest_Trace", "/Script/AIModule.EnvQueryTest_Trace");
	CLASS_REDIRECT("EnvQueryTypes", "/Script/AIModule.EnvQueryTypes");
	CLASS_REDIRECT("KismetAIAsyncTaskProxy", "/Script/AIModule.AIAsyncTaskBlueprintProxy");
	CLASS_REDIRECT("KismetAIHelperLibrary", "/Script/AIModule.AIBlueprintHelperLibrary");
	CLASS_REDIRECT("PathFollowingComponent", "/Script/AIModule.PathFollowingComponent");
	CLASS_REDIRECT("PawnSensingComponent", "/Script/AIModule.PawnSensingComponent");

	STRUCT_REDIRECT("SReply", "/Script/UMG.EventReply");

	PROPERTY_REDIRECT("Actor.AddTickPrerequisiteActor.DependentActor", "PrerequisiteActor");
	FUNCTION_REDIRECT("Actor.AttachRootComponentTo", "Actor.K2_AttachRootComponentTo");
	FUNCTION_REDIRECT("Actor.AttachRootComponentToActor", "Actor.K2_AttachRootComponentToActor");
	FUNCTION_REDIRECT("Actor.SetTickPrerequisite", "Actor.AddTickPrerequisiteActor");
	PROPERTY_REDIRECT("BTTask_MoveDirectlyToward.bForceMoveToLocation", "bDisablePathUpdateOnGoalLocationChange");
	PROPERTY_REDIRECT("KismetSystemLibrary.DrawDebugPlane.Loc", "Location");
	PROPERTY_REDIRECT("KismetSystemLibrary.DrawDebugPlane.P", "PlaneCoordinates");
	FUNCTION_REDIRECT("KismetSystemLibrary.EXPERIMENTAL_CloseAdBanner", "KismetSystemLibrary.ForceCloseAdBanner");
	FUNCTION_REDIRECT("KismetSystemLibrary.EXPERIMENTAL_HideAdBanner", "KismetSystemLibrary.HideAdBanner");
	FUNCTION_REDIRECT("KismetSystemLibrary.EXPERIMENTAL_ShowAdBanner", "KismetSystemLibrary.ShowAdBanner");
	FUNCTION_REDIRECT("LightComponent.SetBrightness", "LightComponent.SetIntensity");
	FUNCTION_REDIRECT("NavigationPath.GetPathLenght", "NavigationPath.GetPathLength");
	FUNCTION_REDIRECT("Pawn.GetMovementInputVector", "Pawn.K2_GetMovementInputVector");
	FUNCTION_REDIRECT("PawnMovementComponent.GetInputVector", "PawnMovementComponent.GetPendingInputVector");
	FUNCTION_REDIRECT("SceneComponent.AttachTo", "SceneComponent.K2_AttachTo");
	FUNCTION_REDIRECT("SkyLightComponent.SetBrightness", "SkyLightComponent.SetIntensity");

	// 4.6

	CLASS_REDIRECT("ControlPointMeshComponent", "/Script/Landscape.ControlPointMeshComponent");
	CLASS_REDIRECT("Landscape", "/Script/Landscape.Landscape");
	CLASS_REDIRECT("LandscapeComponent", "/Script/Landscape.LandscapeComponent");
	CLASS_REDIRECT("LandscapeGizmoActiveActor", "/Script/Landscape.LandscapeGizmoActiveActor");
	CLASS_REDIRECT("LandscapeGizmoActor", "/Script/Landscape.LandscapeGizmoActor");
	CLASS_REDIRECT("LandscapeGizmoRenderComponent", "/Script/Landscape.LandscapeGizmoRenderComponent");
	CLASS_REDIRECT("LandscapeHeightfieldCollisionComponent", "/Script/Landscape.LandscapeHeightfieldCollisionComponent");
	CLASS_REDIRECT("LandscapeInfo", "/Script/Landscape.LandscapeInfo");
	CLASS_REDIRECT("LandscapeInfoMap", "/Script/Landscape.LandscapeInfoMap");
	CLASS_REDIRECT("LandscapeLayerInfoObject", "/Script/Landscape.LandscapeLayerInfoObject");
	CLASS_REDIRECT("LandscapeMaterialInstanceConstant", "/Script/Landscape.LandscapeMaterialInstanceConstant");
	CLASS_REDIRECT("LandscapeMeshCollisionComponent", "/Script/Landscape.LandscapeMeshCollisionComponent");
	CLASS_REDIRECT("LandscapeProxy", "/Script/Landscape.LandscapeProxy");
	CLASS_REDIRECT("LandscapeSplineControlPoint", "/Script/Landscape.LandscapeSplineControlPoint");
	CLASS_REDIRECT("LandscapeSplineSegment", "/Script/Landscape.LandscapeSplineSegment");
	CLASS_REDIRECT("LandscapeSplinesComponent", "/Script/Landscape.LandscapeSplinesComponent");
	CLASS_REDIRECT("MaterialExpressionLandscapeLayerBlend", "/Script/Landscape.MaterialExpressionLandscapeLayerBlend");
	CLASS_REDIRECT("MaterialExpressionLandscapeLayerCoords", "/Script/Landscape.MaterialExpressionLandscapeLayerCoords");
	CLASS_REDIRECT("MaterialExpressionLandscapeLayerSwitch", "/Script/Landscape.MaterialExpressionLandscapeLayerSwitch");
	CLASS_REDIRECT("MaterialExpressionLandscapeLayerWeight", "/Script/Landscape.MaterialExpressionLandscapeLayerWeight");
	CLASS_REDIRECT("MaterialExpressionLandscapeVisibilityMask", "/Script/Landscape.MaterialExpressionLandscapeVisibilityMask");
	CLASS_REDIRECT("MaterialExpressionTerrainLayerCoords", "/Script/Landscape.MaterialExpressionLandscapeLayerCoords");
	CLASS_REDIRECT("MaterialExpressionTerrainLayerSwitch", "/Script/Landscape.MaterialExpressionLandscapeLayerSwitch");
	CLASS_REDIRECT("MaterialExpressionTerrainLayerWeight", "/Script/Landscape.MaterialExpressionLandscapeLayerWeight");
	CLASS_REDIRECT("ReverbVolume", "/Script/Engine.AudioVolume");
	CLASS_REDIRECT("ReverbVolumeToggleable", "/Script/Engine.AudioVolume");

	STRUCT_REDIRECT("KeyboardEvent", "/Script/SlateCore.KeyEvent");
	STRUCT_REDIRECT("KeyboardFocusEvent", "/Script/SlateCore.FocusEvent");

	FUNCTION_REDIRECT("Actor.AddActorLocalOffset", "Actor.K2_AddActorLocalOffset");
	FUNCTION_REDIRECT("Actor.AddActorLocalRotation", "Actor.K2_AddActorLocalRotation");
	FUNCTION_REDIRECT("Actor.AddActorLocalTransform", "Actor.K2_AddActorLocalTransform");
	FUNCTION_REDIRECT("Actor.AddActorLocalTranslation", "Actor.K2_AddActorLocalOffset");
	PROPERTY_REDIRECT("Actor.K2_AddActorLocalOffset.DeltaTranslation", "DeltaLocation");
	FUNCTION_REDIRECT("Actor.AddActorWorldOffset", "Actor.K2_AddActorWorldOffset");
	FUNCTION_REDIRECT("Actor.AddActorWorldRotation", "Actor.K2_AddActorWorldRotation");
	FUNCTION_REDIRECT("Actor.AddActorWorldTransform", "Actor.K2_AddActorWorldTransform");
	FUNCTION_REDIRECT("Actor.SetActorLocation", "Actor.K2_SetActorLocation");
	FUNCTION_REDIRECT("Actor.SetActorLocationAndRotation", "Actor.K2_SetActorLocationAndRotation");
	FUNCTION_REDIRECT("Actor.SetActorRelativeLocation", "Actor.K2_SetActorRelativeLocation");
	PROPERTY_REDIRECT("Actor.K2_SetActorRelativeLocation.NewRelativeTranslation", "NewRelativeLocation");
	FUNCTION_REDIRECT("Actor.SetActorRelativeRotation", "Actor.K2_SetActorRelativeRotation");
	FUNCTION_REDIRECT("Actor.SetActorRelativeTransform", "Actor.K2_SetActorRelativeTransform");
	FUNCTION_REDIRECT("Actor.SetActorRelativeTranslation", "Actor.K2_SetActorRelativeLocation");
	FUNCTION_REDIRECT("Actor.SetActorTransform", "Actor.K2_SetActorTransform");
	FUNCTION_REDIRECT("BTFunctionLibrary.GetBlackboard", "BTFunctionLibrary.GetOwnersBlackboard");
	FUNCTION_REDIRECT("KismetMathLibrary.NearlyEqual_RotatorRotator", "EqualEqual_RotatorRotator");
	FUNCTION_REDIRECT("KismetMathLibrary.NearlyEqual_VectorVector", "EqualEqual_VectorVector");
	FUNCTION_REDIRECT("KismetMathLibrary.ProjectOnTo", "ProjectVectorOnToVector");
	PROPERTY_REDIRECT("KismetMathLibrary.ProjectVectorOnToVector.X", "V");
	PROPERTY_REDIRECT("KismetMathLibrary.ProjectVectorOnToVector.Y", "Target");
	PROPERTY_REDIRECT("LightComponent.SetIntensity.NewBrightness", "NewIntensity");
	FUNCTION_REDIRECT("SceneComponent.AddLocalOffset", "SceneComponent.K2_AddLocalOffset");
	FUNCTION_REDIRECT("SceneComponent.AddLocalRotation", "SceneComponent.K2_AddLocalRotation");
	FUNCTION_REDIRECT("SceneComponent.AddLocalTransform", "SceneComponent.K2_AddLocalTransform");
	FUNCTION_REDIRECT("SceneComponent.AddLocalTranslation", "SceneComponent.K2_AddLocalOffset");
	PROPERTY_REDIRECT("SceneComponent.K2_AddLocalOffset.DeltaTranslation", "DeltaLocation");
	FUNCTION_REDIRECT("SceneComponent.AddRelativeLocation", "SceneComponent.K2_AddRelativeLocation");
	PROPERTY_REDIRECT("SceneComponent.K2_AddRelativeLocation.DeltaTranslation", "DeltaLocation");
	FUNCTION_REDIRECT("SceneComponent.AddRelativeRotation", "SceneComponent.K2_AddRelativeRotation");
	FUNCTION_REDIRECT("SceneComponent.AddRelativeTranslation", "SceneComponent.K2_AddRelativeLocation");
	FUNCTION_REDIRECT("SceneComponent.AddWorldOffset", "SceneComponent.K2_AddWorldOffset");
	FUNCTION_REDIRECT("SceneComponent.AddWorldRotation", "SceneComponent.K2_AddWorldRotation");
	FUNCTION_REDIRECT("SceneComponent.AddWorldTransform", "SceneComponent.K2_AddWorldTransform");
	FUNCTION_REDIRECT("SceneComponent.SetRelativeLocation", "SceneComponent.K2_SetRelativeLocation");
	PROPERTY_REDIRECT("SceneComponent.K2_SetRelativeLocation.NewTranslation", "NewLocation");
	FUNCTION_REDIRECT("SceneComponent.SetRelativeRotation", "SceneComponent.K2_SetRelativeRotation");
	FUNCTION_REDIRECT("SceneComponent.SetRelativeTransform", "SceneComponent.K2_SetRelativeTransform");
	FUNCTION_REDIRECT("SceneComponent.SetRelativeTranslation", "SceneComponent.K2_SetRelativeLocation");
	FUNCTION_REDIRECT("SceneComponent.SetWorldLocation", "SceneComponent.K2_SetWorldLocation");
	PROPERTY_REDIRECT("SceneComponent.K2_SetWorldLocation.NewTranslation", "NewLocation");
	FUNCTION_REDIRECT("SceneComponent.SetWorldRotation", "SceneComponent.K2_SetWorldRotation");
	FUNCTION_REDIRECT("SceneComponent.SetWorldTransform", "SceneComponent.K2_SetWorldTransform");
	FUNCTION_REDIRECT("SceneComponent.SetWorldTranslation", "SceneComponent.K2_SetWorldLocation");
	PROPERTY_REDIRECT("SkyLightComponent.SetIntensity.NewBrightness", "NewIntensity");
}

static void RegisterNativeRedirects49(TArray<FCoreRedirect>& Redirects)
{
	// 4.7

	CLASS_REDIRECT("EdGraphNode_Comment", "/Script/UnrealEd.EdGraphNode_Comment");
	CLASS_REDIRECT("K2Node_Comment", "/Script/UnrealEd.EdGraphNode_Comment");
	CLASS_REDIRECT("VimBlueprintFactory", "/Script/UnrealEd.AnimBlueprintFactory");

	FUNCTION_REDIRECT("Actor.SetTickEnabled", "Actor.SetActorTickEnabled");
	PROPERTY_REDIRECT("UserWidget.OnKeyboardFocusLost.InKeyboardFocusEvent", "InFocusEvent");
	PROPERTY_REDIRECT("UserWidget.OnControllerAnalogValueChanged.ControllerEvent", "InAnalogInputEvent");
	PROPERTY_REDIRECT("UserWidget.OnControllerButtonPressed.ControllerEvent", "InKeyEvent");
	PROPERTY_REDIRECT("UserWidget.OnControllerButtonReleased.ControllerEvent", "InKeyEvent");
	PROPERTY_REDIRECT("UserWidget.OnKeyDown.InKeyboardEvent", "InKeyEvent");
	PROPERTY_REDIRECT("UserWidget.OnKeyUp.InKeyboardEvent", "InKeyEvent");
	PROPERTY_REDIRECT("UserWidget.OnKeyboardFocusReceived.InKeyboardFocusEvent", "InFocusEvent");
	PROPERTY_REDIRECT("UserWidget.OnPreviewKeyDown.InKeyboardEvent", "InKeyEvent");
	
	PROPERTY_REDIRECT("MeshComponent.Materials", "MeshComponent.OverrideMaterials");
	PROPERTY_REDIRECT("Pawn.AutoPossess", "Pawn.AutoPossessPlayer");

	FCoreRedirect& ECollisionChannel = ENUM_REDIRECT("ECollisionChannel", "/Script/Engine.ECollisionChannel");
	ECollisionChannel.ValueChanges.Add(TEXT("ECC_Default"), TEXT("ECC_Visibility"));
	ECollisionChannel.ValueChanges.Add(TEXT("ECC_Dynamic"), TEXT("ECC_WorldDynamic"));
	ECollisionChannel.ValueChanges.Add(TEXT("ECC_OverlapAll"), TEXT("ECC_OverlapAll_Deprecated"));
	ECollisionChannel.ValueChanges.Add(TEXT("ECC_OverlapAllDynamic"), TEXT("ECC_OverlapAll_Deprecated"));
	ECollisionChannel.ValueChanges.Add(TEXT("ECC_OverlapAllDynamic_Deprecated"), TEXT("ECC_OverlapAll_Deprecated"));
	ECollisionChannel.ValueChanges.Add(TEXT("ECC_OverlapAllStatic"), TEXT("ECC_OverlapAll_Deprecated"));
	ECollisionChannel.ValueChanges.Add(TEXT("ECC_OverlapAllStatic_Deprecated"), TEXT("ECC_OverlapAll_Deprecated"));
	ECollisionChannel.ValueChanges.Add(TEXT("ECC_PawnMovement"), TEXT("ECC_Pawn"));
	ECollisionChannel.ValueChanges.Add(TEXT("ECC_RigidBody"), TEXT("ECC_PhysicsBody"));
	ECollisionChannel.ValueChanges.Add(TEXT("ECC_RigidBodyInteractable"), TEXT("ECC_PhysicsBody"));
	ECollisionChannel.ValueChanges.Add(TEXT("ECC_TouchAll"), TEXT("ECC_OverlapAll_Deprecated"));
	ECollisionChannel.ValueChanges.Add(TEXT("ECC_TouchAllDynamic"), TEXT("ECC_OverlapAll_Deprecated"));
	ECollisionChannel.ValueChanges.Add(TEXT("ECC_TouchAllStatic"), TEXT("ECC_OverlapAll_Deprecated"));
	ECollisionChannel.ValueChanges.Add(TEXT("ECC_VehicleMovement"), TEXT("ECC_Vehicle"));
	ECollisionChannel.ValueChanges.Add(TEXT("ECC_WorldTrace"), TEXT("ECC_WorldStatic"));

	// 4.8

	CLASS_REDIRECT("EditorGameAgnosticSettings", "/Script/UnrealEd.EditorSettings");
	CLASS_REDIRECT("FoliageType", "/Script/Foliage.FoliageType");
	CLASS_REDIRECT("FoliageType_InstancedStaticMesh", "/Script/Foliage.FoliageType_InstancedStaticMesh");
	CLASS_REDIRECT("InstancedFoliageActor", "/Script/Foliage.InstancedFoliageActor");
	CLASS_REDIRECT("InstancedFoliageSettings", "/Script/Foliage.FoliageType_InstancedStaticMesh");
	CLASS_REDIRECT("InteractiveFoliageComponent", "/Script/Foliage.InteractiveFoliageComponent");
	CLASS_REDIRECT("ProceduralFoliage", "/Script/Foliage.ProceduralFoliageSpawner");
	CLASS_REDIRECT("ProceduralFoliageActor", "/Script/Foliage.ProceduralFoliageVolume");
	
	STRUCT_REDIRECT("ProceduralFoliageTypeData", "/Script/Foliage.FoliageTypeObject");

	FCoreRedirect& EComponentCreationMethod = ENUM_REDIRECT("EComponentCreationMethod", "/Script/Engine.EComponentCreationMethod");
	EComponentCreationMethod.ValueChanges.Add(TEXT("EComponentCreationMethod::ConstructionScript"), TEXT("EComponentCreationMethod::SimpleConstructionScript"));

	FCoreRedirect& EConstraintTransform = ENUM_REDIRECT("EConstraintTransform", "/Script/Engine.EConstraintTransform");
	EConstraintTransform.ValueChanges.Add(TEXT("EConstraintTransform::Absoluate"), TEXT("EConstraintTransform::Absolute"));

	FCoreRedirect& ELockedAxis = ENUM_REDIRECT("ELockedAxis", "/Script/Engine.EDOFMode");
	ELockedAxis.ValueChanges.Add(TEXT("Custom"), TEXT("EDOFMode::CustomPlane"));
	ELockedAxis.ValueChanges.Add(TEXT("X"), TEXT("EDOFMode::YZPlane"));
	ELockedAxis.ValueChanges.Add(TEXT("Y"), TEXT("EDOFMode::XZPlane"));
	ELockedAxis.ValueChanges.Add(TEXT("Z"), TEXT("EDOFMode::XYPlane"));

	FCoreRedirect& EEndPlayReason = ENUM_REDIRECT("EEndPlayReason", "/Script/Engine.EEndPlayReason");
	EEndPlayReason.ValueChanges.Add(TEXT("EEndPlayReason::ActorDestroyed"), TEXT("EEndPlayReason::Destroyed"));

	FUNCTION_REDIRECT("ActorComponent.ReceiveInitializeComponent", "ActorComponent.ReceiveBeginPlay");
	FUNCTION_REDIRECT("ActorComponent.ReceiveUninitializeComponent", "ActorComponent.ReceiveEndPlay");

	PROPERTY_REDIRECT("CameraComponent.bUseControllerViewRotation", "CameraComponent.bUsePawnControlRotation");
	PROPERTY_REDIRECT("CameraComponent.bUsePawnViewRotation", "CameraComponent.bUsePawnControlRotation");
	PROPERTY_REDIRECT("CharacterMovementComponent.AirSpeed", "CharacterMovementComponent.MaxFlySpeed");
	PROPERTY_REDIRECT("CharacterMovementComponent.CrouchedSpeedPercent", "CharacterMovementComponent.CrouchedSpeedMultiplier");
	PROPERTY_REDIRECT("CharacterMovementComponent.GroundSpeed", "CharacterMovementComponent.MaxWalkSpeed");
	PROPERTY_REDIRECT("CharacterMovementComponent.JumpZ", "CharacterMovementComponent.JumpZVelocity");
	PROPERTY_REDIRECT("CharacterMovementComponent.WaterSpeed", "CharacterMovementComponent.MaxSwimSpeed");
	PROPERTY_REDIRECT("CharacterMovementComponent.bCrouchMovesCharacterDown", "CharacterMovementComponent.bCrouchMaintainsBaseLocation");
	PROPERTY_REDIRECT("CharacterMovementComponent.bOrientToMovement", "CharacterMovementComponent.bOrientRotationToMovement");
	PROPERTY_REDIRECT("FunctionalTest.GetAdditionalTestFinishedMessage", "FunctionalTest.OnAdditionalTestFinishedMessageRequest");
	PROPERTY_REDIRECT("FunctionalTest.WantsToRunAgain", "FunctionalTest.OnWantsReRunCheck");
	PROPERTY_REDIRECT("ProjectileMovementComponent.Speed", "ProjectileMovementComponent.InitialSpeed");
	PROPERTY_REDIRECT("SpringArmComponent.bUseControllerViewRotation", "SpringArmComponent.bUsePawnControlRotation");
	PROPERTY_REDIRECT("SpringArmComponent.bUsePawnViewRotation", "SpringArmComponent.bUsePawnControlRotation");
	PROPERTY_REDIRECT("BodyInstance.CustomLockedAxis", "BodyInstance.CustomDOFPlaneNormal");
	PROPERTY_REDIRECT("BodyInstance.LockedAxisMode", "BodyInstance.DOFMode");
	PROPERTY_REDIRECT("CharacterMovementComponent.NavMeshProjectionCapsuleHeightScaleDown", "CharacterMovementComponent.NavMeshProjectionHeightScaleDown");
	PROPERTY_REDIRECT("CharacterMovementComponent.NavMeshProjectionCapsuleHeightScaleUp", "CharacterMovementComponent.NavMeshProjectionHeightScaleUp");
	PROPERTY_REDIRECT("LandscapeSplineControlPoint.MeshComponent", "LandscapeSplineControlPoint.LocalMeshComponent");
	PROPERTY_REDIRECT("LandscapeSplineSegment.MeshComponents", "LandscapeSplineSegment.LocalMeshComponents");
	PROPERTY_REDIRECT("ProceduralFoliageComponent.Overlap", "ProceduralFoliageComponent.TileOverlap");
	PROPERTY_REDIRECT("ProceduralFoliageComponent.ProceduralFoliage", "ProceduralFoliageComponent.FoliageSpawner");
	PROPERTY_REDIRECT("ProceduralFoliageSpawner.Types", "ProceduralFoliageSpawner.FoliageTypes");
	PROPERTY_REDIRECT("SpriteGeometryCollection.Polygons", "SpriteGeometryCollection.Shapes");

	// 4.9

	CLASS_REDIRECT("EditorUserSettings", "/Script/UnrealEd.EditorPerProjectUserSettings");	
	CLASS_REDIRECT("MovieScene", "/Script/MovieScene.MovieScene");
	CLASS_REDIRECT("MovieScene3DTransformSection", "/Script/MovieSceneTracks.MovieScene3DTransformSection");
	CLASS_REDIRECT("MovieScene3DTransformTrack", "/Script/MovieSceneTracks.MovieScene3DTransformTrack");
	CLASS_REDIRECT("MovieSceneAudioSection", "/Script/MovieSceneTracks.MovieSceneAudioSection");
	CLASS_REDIRECT("MovieSceneAudioTrack", "/Script/MovieSceneTracks.MovieSceneAudioTrack");
	CLASS_REDIRECT("MovieSceneBoolTrack", "/Script/MovieSceneTracks.MovieSceneBoolTrack");
	CLASS_REDIRECT("MovieSceneByteSection", "/Script/MovieSceneTracks.MovieSceneByteSection");
	CLASS_REDIRECT("MovieSceneByteTrack", "/Script/MovieSceneTracks.MovieSceneByteTrack");
	CLASS_REDIRECT("MovieSceneColorSection", "/Script/MovieSceneTracks.MovieSceneColorSection");
	CLASS_REDIRECT("MovieSceneColorTrack", "/Script/MovieSceneTracks.MovieSceneColorTrack");
	CLASS_REDIRECT("MovieSceneFloatSection", "/Script/MovieSceneTracks.MovieSceneFloatSection");
	CLASS_REDIRECT("MovieSceneFloatTrack", "/Script/MovieSceneTracks.MovieSceneFloatTrack");
	CLASS_REDIRECT("MovieSceneParticleSection", "/Script/MovieSceneTracks.MovieSceneParticleSection");
	CLASS_REDIRECT("MovieSceneParticleTrack", "/Script/MovieSceneTracks.MovieSceneParticleTrack");
	CLASS_REDIRECT("MovieScenePropertyTrack", "/Script/MovieSceneTracks.MovieScenePropertyTrack");
	CLASS_REDIRECT("MovieSceneSection", "/Script/MovieScene.MovieSceneSection");
	CLASS_REDIRECT("MovieSceneTrack", "/Script/MovieScene.MovieSceneTrack");

	PACKAGE_REDIRECT("/Script/MovieSceneCore", "/Script/MovieScene");
	PACKAGE_REDIRECT("/Script/MovieSceneCoreTypes", "/Script/MovieSceneTracks");

	STRUCT_REDIRECT("Anchors", "/Script/Slate.Anchors");
	STRUCT_REDIRECT("AnimNode_BoneDrivenController", "/Script/AnimGraphRuntime.AnimNode_BoneDrivenController");
	STRUCT_REDIRECT("AnimNode_CopyBone", "/Script/AnimGraphRuntime.AnimNode_CopyBone");
	STRUCT_REDIRECT("AnimNode_HandIKRetargeting", "/Script/AnimGraphRuntime.AnimNode_HandIKRetargeting");
	STRUCT_REDIRECT("AnimNode_LookAt", "/Script/AnimGraphRuntime.AnimNode_LookAt");
	STRUCT_REDIRECT("AnimNode_ModifyBone", "/Script/AnimGraphRuntime.AnimNode_ModifyBone");
	STRUCT_REDIRECT("AnimNode_RotationMultiplier", "/Script/AnimGraphRuntime.AnimNode_RotationMultiplier");
	STRUCT_REDIRECT("AnimNode_SkeletalControlBase", "/Script/AnimGraphRuntime.AnimNode_SkeletalControlBase");
	STRUCT_REDIRECT("AnimNode_SpringBone", "/Script/AnimGraphRuntime.AnimNode_SpringBone");
	STRUCT_REDIRECT("AnimNode_Trail", "/Script/AnimGraphRuntime.AnimNode_Trail");
	STRUCT_REDIRECT("AnimNode_TwoBoneIK", "/Script/AnimGraphRuntime.AnimNode_TwoBoneIK");
	STRUCT_REDIRECT("MovieSceneEditorData", "/Script/MovieScene.MovieSceneEditorData");
	STRUCT_REDIRECT("MovieSceneObjectBinding", "/Script/MovieScene.MovieSceneBinding");
	STRUCT_REDIRECT("MovieScenePossessable", "/Script/MovieScene.MovieScenePossessable");
	STRUCT_REDIRECT("MovieSceneSpawnable", "/Script/MovieScene.MovieSceneSpawnable");
	STRUCT_REDIRECT("SpritePolygon", "/Script/Paper2D.SpriteGeometryShape");
	STRUCT_REDIRECT("SpritePolygonCollection", "/Script/Paper2D.SpriteGeometryCollection");

	FUNCTION_REDIRECT("GameplayStatics.PlayDialogueAttached", "GameplayStatics.SpawnDialogueAttached");
	FUNCTION_REDIRECT("GameplayStatics.PlaySoundAttached", "GameplayStatics.SpawnSoundAttached");
	FUNCTION_REDIRECT("KismetMathLibrary.BreakRot", "KismetMathLibrary.BreakRotator");
	FUNCTION_REDIRECT("KismetMathLibrary.MakeRot", "KismetMathLibrary.MakeRotator");
	FUNCTION_REDIRECT("KismetMathLibrary.MapRange", "KismetMathLibrary.MapRangeUnclamped");
	FUNCTION_REDIRECT("PrimitiveComponent.GetMoveIgnoreActors", "PrimitiveComponent.CopyArrayOfMoveIgnoreActors");
	FUNCTION_REDIRECT("SplineComponent.GetNumSplinePoints", "SplineComponent.GetNumberOfSplinePoints");
	FUNCTION_REDIRECT("VerticalBox.AddChildVerticalBox", "VerticalBox.AddChildToVerticalBox");
	
	PROPERTY_REDIRECT("ComponentKey.VariableGuid", "ComponentKey.AssociatedGuid");
	PROPERTY_REDIRECT("ComponentKey.VariableName", "ComponentKey.SCSVariableName");
	PROPERTY_REDIRECT("FoliageType.InitialMaxAge", "FoliageType.MaxInitialAge");
	PROPERTY_REDIRECT("FoliageType.bGrowsInShade", "FoliageType.bSpawnsInShade");
	PROPERTY_REDIRECT("MemberReference.MemberParentClass", "MemberReference.MemberParent");
	PROPERTY_REDIRECT("SimpleMemberReference.MemberParentClass", "SimpleMemberReference.MemberParent");
	PROPERTY_REDIRECT("SoundNodeModPlayer.SoundMod", "SoundNodeModPlayer.SoundModAssetPtr");
	PROPERTY_REDIRECT("SoundNodeWavePlayer.SoundWave", "SoundNodeWavePlayer.SoundWaveAssetPtr");

	ENUM_REDIRECT("ECheckBoxState", "/Script/SlateCore.ECheckBoxState");
	ENUM_REDIRECT("ESlateCheckBoxState", "/Script/SlateCore.ECheckBoxState");
	ENUM_REDIRECT("EAxisOption", "/Script/Engine.EAxisOption");
	ENUM_REDIRECT("EBoneAxis", "/Script/Engine.EBoneAxis");
	ENUM_REDIRECT("EBoneModificationMode", "/Script/AnimGraphRuntime.EBoneModificationMode");
	ENUM_REDIRECT("EComponentType", "/Script/Engine.EComponentType");
	ENUM_REDIRECT("EInterpolationBlend", "/Script/AnimGraphRuntime.EInterpolationBlend");
}

UE_ENABLE_OPTIMIZATION_SHIP

void FCoreRedirects::RegisterNativeRedirectsUnderWriteLock(const FCoreRedirectorScopeLockForWrite& HeldLock)
{
	// Registering redirects here instead of in baseengine.ini is faster to parse and can clean up the ini, but is not required
	TArray<FCoreRedirect> Redirects;

	RegisterNativeRedirects40(Redirects);
	RegisterNativeRedirects46(Redirects);
	RegisterNativeRedirects49(Redirects);

	// 4.10 and later are in baseengine.ini

	AddRedirectListUnderWriteLock(Redirects, TEXT("RegisterNativeRedirects"), HeldLock);
}
#else
void FCoreRedirects::RegisterNativeRedirectsUnderWriteLock(const FCoreRedirectorScopeLock& HeldLock)
{
}
#endif // UE_WITH_CORE_REDIRECTS

void FCoreRedirects::FRWLockWithExclusiveRecursion::ReadLock()
{
	int32 InitialLockOwner = WriteLockOwnerThreadId.load(std::memory_order_relaxed);
	
	// Avoid calling GetCurrentThreadId in the common case where we start out unlocked
	// If we are unowned at the start, then there are three cases to consider:
	// 1. We are unlocked. In that case we'll pass the InitialLockOwner check and call ReadLock()
	// 2. We are already in a ReadLock() state. Taking the read lock again will cause us to deadlock. 
	// That is part of the contract for this lock so it's okay.
	// 3. We are write locked, in which case we'll enter the first block 
	if (InitialLockOwner != 0)
	{
		// If we are initially write locked, there are two possibilities:
		// 1. This thread owns the write lock. In that case, it's safe to just bump the recursion count
		// 2. Another thread owns the write lock, in which case we want to block with a ReadLock()
		if (InitialLockOwner == FPlatformTLS::GetCurrentThreadId())
		{
			// Allow recursive read locking by bumping the recursion count
			RecursionCount++;
		}
		else
		{
			InternalLock.ReadLock();
		}
	}
	else
	{
		// We may or may not still be locked at this point if another thread took a WriteLock
		// but it doesn't matter because in either case we want to take a ReadLock and potentially wait
		InternalLock.ReadLock();
	}
}

void FCoreRedirects::FRWLockWithExclusiveRecursion::WriteLock()
{
	uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
	if (WriteLockOwnerThreadId.load(std::memory_order_relaxed) != CurrentThreadId)
	{
		InternalLock.WriteLock();
		WriteLockOwnerThreadId = CurrentThreadId;
	}
	RecursionCount++;
}

void FCoreRedirects::FRWLockWithExclusiveRecursion::WriteUnlock()
{
	ensure(WriteLockOwnerThreadId.load(std::memory_order_relaxed) == FPlatformTLS::GetCurrentThreadId());
	ensure(RecursionCount > 0);
	RecursionCount--;
	if (RecursionCount == 0)
	{
		WriteLockOwnerThreadId.store(0, std::memory_order_relaxed);
		InternalLock.WriteUnlock();
	}
}

void FCoreRedirects::FRWLockWithExclusiveRecursion::ReadUnlock()
{
	if (RecursionCount > 0)
	{
		ensureMsgf(WriteLockOwnerThreadId.load(std::memory_order_relaxed) == FPlatformTLS::GetCurrentThreadId(), 
			TEXT("Called ReadUnlock() on a lock held exclusively by another thread."));
		RecursionCount--;
	}
	else
	{
		InternalLock.ReadUnlock();
	}
}
