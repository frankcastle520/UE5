// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/MallocLeakDetection.h"

THIRD_PARTY_INCLUDES_START
#include "Catch2Includes.h"
THIRD_PARTY_INCLUDES_END

#include "MyAutoRTFMTestObject.h"
#include <AutoRTFM/AutoRTFM.h>
#include "UObject/GCObject.h"
#include "UObject/ReachabilityAnalysis.h"
#include "UObject/UObjectAnnotation.h"

TEST_CASE("UObject.NewObject")
{
	SECTION("Create")
	{
		UMyAutoRTFMTestObject* Object = nullptr;

		AutoRTFM::Commit([&]
			{
				Object = NewObject<UMyAutoRTFMTestObject>();
			});

		REQUIRE(nullptr != Object);
		REQUIRE(42 == Object->Value);
	}

	SECTION("Abort")
	{
		UMyAutoRTFMTestObject* Object = nullptr;

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == AutoRTFM::Transact([&]
			{
				Object = NewObject<UMyAutoRTFMTestObject>();
				AutoRTFM::AbortTransaction();
			}));

		REQUIRE(nullptr == Object);
	}
}

TEST_CASE("UObject.NewObjectWithOuter")
{
	SECTION("Create")
	{
		UMyAutoRTFMTestObject* Outer = NewObject<UMyAutoRTFMTestObject>();
		UMyAutoRTFMTestObject* Object = nullptr;

		AutoRTFM::Commit([&]
			{
				Object = NewObject<UMyAutoRTFMTestObject>(Outer);
			});

		REQUIRE(nullptr != Object);
		REQUIRE(42 == Object->Value);
		REQUIRE(Object->IsInOuter(Outer));
		REQUIRE(55 == Outer->Value);
	}

	SECTION("Abort")
	{
		UMyAutoRTFMTestObject* Outer = NewObject<UMyAutoRTFMTestObject>();
		UMyAutoRTFMTestObject* Object = nullptr;

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == AutoRTFM::Transact([&]
			{
				Object = NewObject<UMyAutoRTFMTestObject>(Outer);
				AutoRTFM::AbortTransaction();
			}));

		REQUIRE(nullptr == Object);
		REQUIRE(42 == Outer->Value);
	}
}

// This is a copy of the helper function in TestGarbageCollector.cpp.
int32 PerformGarbageCollectionWithIncrementalReachabilityAnalysis(TFunctionRef<bool(int32)> ReachabilityIterationCallback)
{
	int32 ReachabilityIterationIndex = 0;

	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, false);

	while (IsIncrementalReachabilityAnalysisPending())
	{
		if (ReachabilityIterationCallback(ReachabilityIterationIndex))
		{
			break;
		}

		// Re-check if incremental rachability is still pending because the callback above could've triggered GC which would complete all iterations
		if (IsIncrementalReachabilityAnalysisPending())
		{
			PerformIncrementalReachabilityAnalysis(GetReachabilityAnalysisTimeLimit());
			ReachabilityIterationIndex++;
		}
	}

	if (IsIncrementalPurgePending())
	{
		IncrementalPurgeGarbage(false);
	}
	check(IsIncrementalPurgePending() == false);

	return ReachabilityIterationIndex + 1;
}

TEST_CASE("UObject.MarkAsReachable")
{
	// We need incremental reachability to be on.
	SetIncrementalReachabilityAnalysisEnabled(true);

	// Cache the original time limit.
	const float Original = GetReachabilityAnalysisTimeLimit();

	// And we need a super small time limit s that reachability analysis will definitely have started.
	SetReachabilityAnalysisTimeLimit(FLT_MIN);
	
	// We need to be sure we've done the static GC initialization before we start doing a garbage
	// collection.
	FGCObject::StaticInit();

	UMyAutoRTFMTestObject* const Object = NewObject<UMyAutoRTFMTestObject>();

	// Somewhat ironically, garbage collection can leak memory.
	MALLOCLEAK_IGNORE_SCOPE();

	PerformGarbageCollectionWithIncrementalReachabilityAnalysis([Object](int32 index)
	{
		if (0 != index)
		{
			return true;
		}

		REQUIRE(AutoRTFM::ETransactionResult::Committed == AutoRTFM::Transact([&]
		{
			Object->MarkAsReachable();
		}));

		return false;
	});

	// Reset it back just incase another test required the original time limit.
	SetReachabilityAnalysisTimeLimit(Original);
}

TEST_CASE("UObject.TestAddAnnotation")
{
	struct FTestAnnotation
	{
		FTestAnnotation()
			: TestAnnotationNumber(32)
		{
		}

		int TestAnnotationNumber;

		bool IsDefault() const
		{
			return TestAnnotationNumber == 32;
		}
	};

	FUObjectAnnotationSparse<FTestAnnotation, true> GTestAnnotation;

	SECTION("Create")
	{
		UMyAutoRTFMTestObject* Outer = NewObject<UMyAutoRTFMTestObject>();
		UMyAutoRTFMTestObject* Object = nullptr;

		AutoRTFM::Commit([&]
			{
				Object = NewObject<UMyAutoRTFMTestObject>(Outer);

				FTestAnnotation Temp;
				Temp.TestAnnotationNumber = 70;

				GTestAnnotation.AddAnnotation(Object, Temp);
			});

		REQUIRE(nullptr != Object);
		REQUIRE(42 == Object->Value);
		REQUIRE(Object->IsInOuter(Outer));
		REQUIRE(55 == Outer->Value);
		REQUIRE(70 == GTestAnnotation.GetAnnotation(Object).TestAnnotationNumber);
	}

	SECTION("Abort")
	{
		UMyAutoRTFMTestObject* Outer = NewObject<UMyAutoRTFMTestObject>();
		UMyAutoRTFMTestObject* Object = nullptr;

		Object = NewObject<UMyAutoRTFMTestObject>(Outer);

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == AutoRTFM::Transact([&]
			{

				FTestAnnotation Temp;
				Temp.TestAnnotationNumber = 70;

				GTestAnnotation.AddAnnotation(Object, Temp);

				AutoRTFM::AbortTransaction();
			}));

		REQUIRE(32 == GTestAnnotation.GetAnnotation(Object).TestAnnotationNumber);
	}
}

struct FAnnotationObject
{
	UObject* Object = nullptr;

	FAnnotationObject() {}

	FAnnotationObject(UObject* InObject) : Object(InObject) {}

	bool IsDefault() { return !Object; }
};

template <> struct TIsPODType<FAnnotationObject> { enum { Value = true }; };

TEST_CASE("UObject.AnnotationMap")
{
	FUObjectAnnotationSparse<FAnnotationObject, false> AnnotationMap;

	UObject* Key = NewObject<UMyAutoRTFMTestObject>();

	AutoRTFM::Transact([&]
	{
		UObject* Value = NewObject<UMyAutoRTFMTestObject>();
		AnnotationMap.GetAnnotation(Key);
		AnnotationMap.AddAnnotation(Key, Value);
	});

	REQUIRE(!AnnotationMap.GetAnnotation(Key).IsDefault());
}

TEST_CASE("UObject.AtomicallySetFlags")
{
	UObject* const Object = NewObject<UMyAutoRTFMTestObject>();

	constexpr EObjectFlags OldFlags = EObjectFlags::RF_Public | EObjectFlags::RF_Transient;
	constexpr EObjectFlags FlagsToAdd = EObjectFlags::RF_Transient | EObjectFlags::RF_AllocatedInSharedPage;

	// We need to ensure we cover the case where we are adding a flag that is already there
	// and thus cannot just wipe that out if we abort!
	Object->AtomicallyClearFlags(FlagsToAdd);
	Object->AtomicallySetFlags(OldFlags);

	REQUIRE(Object->HasAllFlags(OldFlags) & !Object->HasAllFlags(FlagsToAdd));

	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Object->AtomicallySetFlags(FlagsToAdd);
			AutoRTFM::AbortTransaction();
		});

	REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	REQUIRE(Object->HasAllFlags(OldFlags) & !Object->HasAllFlags(FlagsToAdd));

	Result = AutoRTFM::Transact([&]
		{
			Object->AtomicallySetFlags(FlagsToAdd);
		});

	REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	REQUIRE(Object->HasAllFlags(OldFlags) & Object->HasAllFlags(FlagsToAdd));
}

TEST_CASE("UObject.AtomicallyClearFlags")
{
	UObject* const Object = NewObject<UMyAutoRTFMTestObject>();

	constexpr EObjectFlags OldFlags = EObjectFlags::RF_Public | EObjectFlags::RF_Transient;
	constexpr EObjectFlags FlagsToClear = EObjectFlags::RF_Transient | EObjectFlags::RF_AllocatedInSharedPage;

	// We need to ensure we cover the case where we are adding a flag that is already there
	// and thus cannot just wipe that out if we abort!
	Object->AtomicallyClearFlags(FlagsToClear);
	Object->AtomicallySetFlags(OldFlags);

	REQUIRE(Object->HasAllFlags(OldFlags) & !Object->HasAllFlags(FlagsToClear));

	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Object->AtomicallyClearFlags(FlagsToClear);
			AutoRTFM::AbortTransaction();
		});

	REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	REQUIRE(Object->HasAllFlags(OldFlags) & !Object->HasAllFlags(FlagsToClear));

	Result = AutoRTFM::Transact([&]
		{
			Object->AtomicallyClearFlags(FlagsToClear);
		});

	REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	REQUIRE(Object->HasAnyFlags(OldFlags) & !Object->HasAllFlags(FlagsToClear));
}
