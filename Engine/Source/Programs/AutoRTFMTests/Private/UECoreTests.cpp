// Copyright Epic Games, Inc. All Rights Reserved.

#include "Catch2Includes.h"
#include "AutoRTFM/AutoRTFM.h"
#include "Delegates/IDelegateInstance.h"
#include "HAL/MallocLeakDetection.h"
#include "HAL/ThreadSingleton.h"
#include "Internationalization/TextCache.h"
#include "Internationalization/TextFormatter.h"
#include "Internationalization/TextHistory.h"
#include "Serialization/CustomVersion.h"
#include "UObject/NameTypes.h"
#include "UObject/UObjectArray.h"
#include "MyAutoRTFMTestObject.h"
#include "Misc/TransactionallySafeScopeLock.h"
#include "Misc/TransactionallySafeRWScopeLock.h"
#include "Containers/Queue.h"
#include "Misc/ConfigCacheIni.h"

TEST_CASE("UECore.FDelegateHandle")
{
	FDelegateHandle Handle;

	SECTION("With Abort")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
		{
			Handle = FDelegateHandle(FDelegateHandle::GenerateNewHandle);
			AutoRTFM::AbortTransaction();
		});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
		REQUIRE(!Handle.IsValid());
	}

	REQUIRE(!Handle.IsValid());

	SECTION("With Commit")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
		{
			Handle = FDelegateHandle(FDelegateHandle::GenerateNewHandle);
		});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
		REQUIRE(Handle.IsValid());
	}
}

TEST_CASE("UECore.TThreadSingleton")
{
	struct MyStruct : TThreadSingleton<MyStruct>
	{
		int I;
		float F;
	};

	SECTION("TryGet First Time")
	{
		REQUIRE(nullptr == TThreadSingleton<MyStruct>::TryGet());

		// Set to something that isn't nullptr because TryGet will return that!
		MyStruct* Singleton;
		uintptr_t Data = 0x12345678abcdef00;
		memcpy(&Singleton, &Data, sizeof(Singleton));

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
		{
			Singleton = TThreadSingleton<MyStruct>::TryGet();
		});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
		REQUIRE(nullptr == Singleton);
	}

	SECTION("Get")
	{
		MALLOCLEAK_IGNORE_SCOPE(); // TThreadSingleton will appear as a leak.

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
			{
				TThreadSingleton<MyStruct>::Get().I = 42;
				TThreadSingleton<MyStruct>::Get().F = 42.0f;
				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		// The singleton *will remain* initialized though, even though we got it in
		// a transaction, because we have to do the singleton creation in the open.
		//
		// commenting out due to changes to this singleton structure under the hood, remove if no longer needed!
		// REQUIRE(nullptr != TThreadSingleton<MyStruct>::TryGet());

		// But any *changes* to the singleton data will be rolled back.
		REQUIRE(0 == TThreadSingleton<MyStruct>::Get().I);
		REQUIRE(0.0f == TThreadSingleton<MyStruct>::Get().F);

		Result = AutoRTFM::Transact([&]()
			{
				TThreadSingleton<MyStruct>::Get().I = 42;
				TThreadSingleton<MyStruct>::Get().F = 42.0f;
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);

		REQUIRE(42 == TThreadSingleton<MyStruct>::Get().I);
		REQUIRE(42.0f == TThreadSingleton<MyStruct>::Get().F);
	}

	SECTION("TryGet Second Time")
	{
		REQUIRE(nullptr != TThreadSingleton<MyStruct>::TryGet());

		MyStruct* Singleton = nullptr;

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
			{
				Singleton = TThreadSingleton<MyStruct>::TryGet();
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
		REQUIRE(nullptr != Singleton);
	}
}

TEST_CASE("UECore.FTextHistory")
{
	struct MyTextHistory final : FTextHistory_Base
	{
		// Need this to always return true so we hit the fun transactional bits!
		bool CanUpdateDisplayString() override
		{
			return true;
		}

		MyTextHistory(const FTextId& InTextId, FString&& InSourceString) : FTextHistory_Base(InTextId, MoveTemp(InSourceString)) {}
	};

	FTextKey Namespace("NAMESPACE");
	FTextKey Key("KEY");
	FTextId TextId(Namespace, Key);
	FString String("WOWWEE");

	MyTextHistory History(TextId, MoveTemp(String));

	SECTION("With Abort")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
			{
				History.UpdateDisplayStringIfOutOfDate();
				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	}

	SECTION("With Commit")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
			{
				History.UpdateDisplayStringIfOutOfDate();
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}
}

TEST_CASE("UECore.FCustomVersionContainer")
{
	FCustomVersionContainer Container;
	FGuid Guid(42, 42, 42, 42);

	FCustomVersionRegistration Register(Guid, 0, TEXT("WOWWEE"));

	REQUIRE(nullptr == Container.GetVersion(Guid));

	SECTION("With Abort")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
			{
				// The first time the version will be new.
				Container.SetVersionUsingRegistry(Guid);

				// The second time we should hit the cache the first one created.
				Container.SetVersionUsingRegistry(Guid);
				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
		REQUIRE(nullptr == Container.GetVersion(Guid));
	}

	SECTION("With Commit")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
			{
				// The first time the version will be new.
				Container.SetVersionUsingRegistry(Guid);

				// The second time we should hit the cache the first one created.
				Container.SetVersionUsingRegistry(Guid);
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
		REQUIRE(nullptr != Container.GetVersion(Guid));
	}
}

TEST_CASE("UECore.FName")
{
	SECTION("EName Constructor")
	{
		FName Name;

		SECTION("With Abort")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Name = FName(EName::Timer);
					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
			REQUIRE(Name.IsNone());
		}

		SECTION("With Commit")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Name = FName(EName::Timer);
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
			REQUIRE(EName::Timer == *Name.ToEName());
		}
	}

	SECTION("String Constructor")
	{
		FName Name;

		SECTION("With Abort")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Name = FName(TEXT("WOWWEE"), 42);
					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
			REQUIRE(Name.IsNone());
		}

		SECTION("Check FName was cached")
		{
			bool bWasCached = false;

			for (const FNameEntry* const Entry : FName::DebugDump())
			{
				// Even though we aborted the transaction above, the actual backing data store of
				// the FName system that deduplicates names will contain our name (the nature of
				// the global shared caching infrastructure means we cannot just throw away the
				// FName in the shared cache because it *could* have also been requested in the
				// open and we'd be stomping on that legit use of it!).
				if (0 != Entry->GetNameLength() && (TEXT("WOWWEE") == Entry->GetPlainNameString()))
				{
					bWasCached = true;
				}
			}

			REQUIRE(bWasCached);
		}

		SECTION("With Commit")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Name = FName(TEXT("WOWWEE"), 42);
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
			REQUIRE(TEXT("WOWWEE") == Name.GetPlainNameString());
			REQUIRE(42 == Name.GetNumber());
		}
	}
}

TEST_CASE("UECore.STATIC_FUNCTION_FNAME")
{
	FName Name;

	SECTION("With Abort")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
			{
				Name = STATIC_FUNCTION_FNAME(TEXT("WOWWEE"));
				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
		REQUIRE(Name.IsNone());
	}

	SECTION("With Commit")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
			{
				Name = STATIC_FUNCTION_FNAME(TEXT("WOWWEE"));
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}
}

TEST_CASE("UECore.TIntrusiveReferenceController")
{
	SECTION("AddSharedReference")
	{
		SharedPointerInternals::TIntrusiveReferenceController<int, ESPMode::ThreadSafe> Controller(42);

		SECTION("With Abort")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Controller.AddSharedReference();
					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
			REQUIRE(1 == Controller.GetSharedReferenceCount());
		}

		SECTION("With Commit")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Controller.AddSharedReference();
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
			REQUIRE(2 == Controller.GetSharedReferenceCount());
		}
	}

	SECTION("AddWeakReference")
	{
		SharedPointerInternals::TIntrusiveReferenceController<int, ESPMode::ThreadSafe> Controller(42);

		SECTION("With Abort")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Controller.AddWeakReference();
					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
			REQUIRE(1 == Controller.WeakReferenceCount);
		}

		SECTION("With Commit")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Controller.AddWeakReference();
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
			REQUIRE(2 == Controller.WeakReferenceCount);
		}
	}

	SECTION("ConditionallyAddSharedReference")
	{
		SECTION("With Shared Reference Non Zero")
		{
			SharedPointerInternals::TIntrusiveReferenceController<int, ESPMode::ThreadSafe> Controller(42);

			SECTION("With Abort")
			{
				AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
					{
						Controller.ConditionallyAddSharedReference();
						AutoRTFM::AbortTransaction();
					});

				REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
				REQUIRE(1 == Controller.GetSharedReferenceCount());
			}

			SECTION("With Commit")
			{
				AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
					{
						Controller.ConditionallyAddSharedReference();
					});

				REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
				REQUIRE(2 == Controller.GetSharedReferenceCount());
			}
		}

		SECTION("With Shared Reference Zero")
		{
			SharedPointerInternals::TIntrusiveReferenceController<int, ESPMode::ThreadSafe> Controller(42);

			// This test relies on us having a weak reference but no strong references to the object.
			Controller.AddWeakReference();
			Controller.ReleaseSharedReference();
			REQUIRE(0 == Controller.GetSharedReferenceCount());

			SECTION("With Abort")
			{
				AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
					{
						Controller.ConditionallyAddSharedReference();
						AutoRTFM::AbortTransaction();
					});

				REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
				REQUIRE(0 == Controller.GetSharedReferenceCount());
			}

			SECTION("With Commit")
			{
				AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
					{
						Controller.ConditionallyAddSharedReference();
					});

				REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
				REQUIRE(0 == Controller.GetSharedReferenceCount());
			}
		}
	}

	SECTION("GetSharedReferenceCount")
	{
		SharedPointerInternals::TIntrusiveReferenceController<int, ESPMode::ThreadSafe> Controller(42);

		SECTION("With Abort")
		{
			int32 Count = 0;

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Count = Controller.GetSharedReferenceCount();
					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
			REQUIRE(0 == Count);
		}

		SECTION("With Commit")
		{
			int32 Count = 0;

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Count = Controller.GetSharedReferenceCount();
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
			REQUIRE(1 == Count);
		}
	}

	SECTION("IsUnique")
	{
		SharedPointerInternals::TIntrusiveReferenceController<int, ESPMode::ThreadSafe> Controller(42);

		SECTION("True")
		{
			bool Unique = false;

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Unique = Controller.IsUnique();
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
			REQUIRE(Unique);
		}

		SECTION("False")
		{
			// Add a count to make us not unique.
			Controller.AddSharedReference();

			bool Unique = true;

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Unique = Controller.IsUnique();
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
			REQUIRE(!Unique);
		}
	}

	SECTION("ReleaseSharedReference")
	{
		SharedPointerInternals::TIntrusiveReferenceController<int, ESPMode::ThreadSafe> Controller(42);

		// We don't want the add weak reference deleter to trigger in this test so add another to its count.
		Controller.AddWeakReference();

		SECTION("With Abort")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Controller.ReleaseSharedReference();
					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
			REQUIRE(1 == Controller.GetSharedReferenceCount());
		}

		SECTION("With Commit")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Controller.ReleaseSharedReference();
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
		}
	}

	SECTION("ReleaseWeakReference")
	{
		auto* Controller = new SharedPointerInternals::TIntrusiveReferenceController<int, ESPMode::ThreadSafe>(42);

		SECTION("With Abort")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Controller->ReleaseWeakReference();
					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
			REQUIRE(1 == Controller->WeakReferenceCount);
			delete Controller;
		}

		SECTION("With Commit")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Controller->ReleaseWeakReference();
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
		}
	}

	SECTION("GetObjectPtr")
	{
		SharedPointerInternals::TIntrusiveReferenceController<int, ESPMode::ThreadSafe> Controller(42);

		SECTION("With Abort")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					*Controller.GetObjectPtr() = 13;
					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
			REQUIRE(42 == *Controller.GetObjectPtr());
		}

		SECTION("With Commit")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					*Controller.GetObjectPtr() = 13;
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
			REQUIRE(13 == *Controller.GetObjectPtr());
		}
	}
}

TEST_CASE("UECore.FText")
{
	FText Text;
	REQUIRE(Text.IsEmpty());

	SECTION("With Abort")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				Text = FText::FromString(FString(TEXT("Sheesh")));
				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
		REQUIRE(Text.IsEmpty());
	}

	SECTION("With Commit")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				Text = FText::FromString(FString(TEXT("Sheesh")));
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
		REQUIRE(!Text.IsEmpty());
		REQUIRE(Text.ToString() == TEXT("Sheesh"));
	}
}

TEST_CASE("UECore.FTextCache")
{
	// FTextCache is a singleton. Grab its reference.
	FTextCache& Cache = FTextCache::Get();

	// Use a fixed cache key for the tests below.
	const FTextId Key{TEXT("NAMESPACE"), TEXT("KEY")};

	// As FTextCache does not supply any way to query what's held in the cache,
	// the best we can do here is to call FindOrCache() and check the returned
	// FText strings are as expected.
	auto CheckCacheHealthy = [&]
	{
		FText LookupA = Cache.FindOrCache(TEXT("VALUE"), Key);
		REQUIRE(LookupA.ToString() == TEXT("VALUE"));
		FText LookupB = Cache.FindOrCache(TEXT("REPLACEMENT"), Key);
		REQUIRE(LookupB.ToString() == TEXT("REPLACEMENT"));
		Cache.RemoveCache(Key);
	};

	SECTION("FindOrCache() Add new")
	{
		SECTION("With Abort")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Cache.FindOrCache(TEXT("VALUE"), Key);
					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

			CheckCacheHealthy();
		}

		SECTION("With Commit")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Cache.FindOrCache(TEXT("VALUE"), Key);
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);

			CheckCacheHealthy();
		}
	}

	SECTION("FindOrCache() Replace with same value")
	{
		SECTION("With Abort")
		{
			// Add an entry to the cache before the transaction
			Cache.FindOrCache(TEXT("VALUE"), Key);

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Cache.FindOrCache(TEXT("REPLACEMENT"), Key);
					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

			CheckCacheHealthy();
		}

		SECTION("With Commit")
		{
			// Add an entry to the cache before the transaction
			Cache.FindOrCache(TEXT("VALUE"), Key);

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Cache.FindOrCache(TEXT("VALUE"), Key);
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);

			CheckCacheHealthy();
		}
	}

	SECTION("FindOrCache() Replace with different value")
	{
		SECTION("With Abort")
		{
			// Add an entry to the cache before the transaction
			Cache.FindOrCache(TEXT("ORIGINAL"), Key);

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Cache.FindOrCache(TEXT("REPLACEMENT"), Key);
					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

			CheckCacheHealthy();
		}

		SECTION("With Commit")
		{
			// Add an entry to the cache before the transaction
			Cache.FindOrCache(TEXT("ORIGINAL"), Key);

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Cache.FindOrCache(TEXT("REPLACEMENT"), Key);
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);

			CheckCacheHealthy();
		}
	}

	static constexpr bool bSupportsTransactionalRemoveCache = false; // #jira SOL-6743
	if (!bSupportsTransactionalRemoveCache)
	{
		return;
	}

	SECTION("RemoveCache()")
	{
		SECTION("With Abort")
		{
			// Add an entry to the cache before the transaction
			Cache.FindOrCache(TEXT("VALUE"), Key);

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Cache.RemoveCache(Key);
					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

			CheckCacheHealthy();
		}

		SECTION("With Commit")
		{
			// Add an entry to the cache before the transaction
			Cache.FindOrCache(TEXT("VALUE"), Key);

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
				{
					Cache.RemoveCache(Key);
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);

			CheckCacheHealthy();
		}
	}


	SECTION("Mixed Closed & Open")
	{
		SECTION("Closed: FindOrCache() Open: RemoveCache()")
		{
			SECTION("With Abort")
			{
				AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
					{
						Cache.FindOrCache(TEXT("VALUE"), Key);
						AutoRTFM::Open([&]{ Cache.RemoveCache(Key); });
						AutoRTFM::AbortTransaction();
					});

				REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

				CheckCacheHealthy();
			}

			SECTION("With Commit")
			{
				AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]()
					{
						Cache.FindOrCache(TEXT("VALUE"), Key);
						AutoRTFM::Open([&]{ Cache.RemoveCache(Key); });
					});

				REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);

				CheckCacheHealthy();
			}
		}
	}
}

TEST_CASE("UECore.FUObjectItem")
{
	SECTION("CreateStatID First In Open")
	{
		FUObjectItem Item;
		Item.Object = NewObject<UMyAutoRTFMTestObject>();
		Item.CreateStatID();

		PROFILER_CHAR* const StatIDStringStorage = Item.StatIDStringStorage;

		// If we abort then we won't change anything.
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				Item.CreateStatID();
				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
		REQUIRE(StatIDStringStorage == Item.StatIDStringStorage);

		// But also if we commit we likewise won't change anything because
		// the string storage was already created before the transaction
		// began.
		Result = AutoRTFM::Transact([&]
			{
				Item.CreateStatID();
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
		REQUIRE(StatIDStringStorage == Item.StatIDStringStorage);
	}

	SECTION("CreateStatID First In Closed")
	{
		FUObjectItem Item;
		Item.Object = NewObject<UMyAutoRTFMTestObject>();
		REQUIRE(nullptr == Item.StatIDStringStorage);
		REQUIRE(!Item.StatID.IsValidStat());

		// If we abort then we won't change anything.
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				Item.CreateStatID();
				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
		REQUIRE(nullptr == Item.StatIDStringStorage);
		REQUIRE(!Item.StatID.IsValidStat());

		// If we commit though we'll create the stat ID.
		Result = AutoRTFM::Transact([&]
			{
				Item.CreateStatID();
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
		REQUIRE(nullptr != Item.StatIDStringStorage);
		REQUIRE(Item.StatID.IsValidStat());
	}

	SECTION("CreateStatID On In-Transaction Object")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				FUObjectItem Item;
				Item.Object = NewObject<UMyAutoRTFMTestObject>();
				Item.CreateStatID();

				AutoRTFM::Open([&]
					{
						REQUIRE(nullptr != Item.StatIDStringStorage);
						REQUIRE(Item.StatID.IsValidStat());
					});

				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		Result = AutoRTFM::Transact([&]
			{
				FUObjectItem Item;
				Item.Object = NewObject<UMyAutoRTFMTestObject>();
				Item.CreateStatID();

				AutoRTFM::Open([&]
					{
						REQUIRE(nullptr != Item.StatIDStringStorage);
						REQUIRE(Item.StatID.IsValidStat());
					});
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}

	SECTION("CreateStatID In Closed Then Again In Open")
	{
		{
			FUObjectItem Item;
			Item.Object = NewObject<UMyAutoRTFMTestObject>();
			REQUIRE(nullptr == Item.StatIDStringStorage);
			REQUIRE(!Item.StatID.IsValidStat());

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
				{
					Item.CreateStatID();

					AutoRTFM::Open([&]
						{
							REQUIRE(nullptr != Item.StatIDStringStorage);
							REQUIRE(Item.StatID.IsValidStat());

							PROFILER_CHAR* const StatIDStringStorage = Item.StatIDStringStorage;

							Item.CreateStatID();

							REQUIRE(StatIDStringStorage == Item.StatIDStringStorage);
							REQUIRE(Item.StatID.IsValidStat());
						});

					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
			REQUIRE(nullptr == Item.StatIDStringStorage);
			REQUIRE(!Item.StatID.IsValidStat());
		}

		{
			FUObjectItem Item;
			Item.Object = NewObject<UMyAutoRTFMTestObject>();
			REQUIRE(nullptr == Item.StatIDStringStorage);
			REQUIRE(!Item.StatID.IsValidStat());

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
				{
					Item.CreateStatID();

					AutoRTFM::Open([&]
						{
							REQUIRE(nullptr != Item.StatIDStringStorage);
							REQUIRE(Item.StatID.IsValidStat());

							PROFILER_CHAR* const StatIDStringStorage = Item.StatIDStringStorage;

							Item.CreateStatID();

							REQUIRE(StatIDStringStorage == Item.StatIDStringStorage);
							REQUIRE(Item.StatID.IsValidStat());
						});
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
			REQUIRE(nullptr != Item.StatIDStringStorage);
			REQUIRE(Item.StatID.IsValidStat());
		}
	}
}

TEST_CASE("UECore.TransactionallySafeScopeLock")
{
	SECTION("Outside Transaction")
	{
		FTransactionallySafeCriticalSection CriticalSection;

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeScopeLock Lock(&CriticalSection);
				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeScopeLock Lock(&CriticalSection);
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}

	SECTION("Inside Transaction")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeCriticalSection CriticalSection;
				FTransactionallySafeScopeLock Lock(&CriticalSection);
				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeCriticalSection CriticalSection;
				FTransactionallySafeScopeLock Lock(&CriticalSection);
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}

	SECTION("Inside Transaction Used In Nested Transaction")
	{
		AutoRTFM::ETransactionResult InnerResult = AutoRTFM::ETransactionResult::Committed;

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeCriticalSection CriticalSection;

				InnerResult = AutoRTFM::Transact([&]
					{
						FTransactionallySafeScopeLock Lock(&CriticalSection);
						AutoRTFM::CascadingAbortTransaction();
					});
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByCascade == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeCriticalSection CriticalSection;

				InnerResult = AutoRTFM::Transact([&]
					{
						FTransactionallySafeScopeLock Lock(&CriticalSection);
						AutoRTFM::AbortTransaction();
					});
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == InnerResult);
		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeCriticalSection CriticalSection;

				InnerResult = AutoRTFM::Transact([&]
					{
						FTransactionallySafeScopeLock Lock(&CriticalSection);
					});

				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeCriticalSection CriticalSection;

				InnerResult = AutoRTFM::Transact([&]
					{
						FTransactionallySafeScopeLock Lock(&CriticalSection);
					});
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == InnerResult);
		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}
}

TEST_CASE("UECore.TransactionallySafeRWScopeLock")
{
	SECTION("Outside Transaction With Read Lock")
	{
		FTransactionallySafeRWLock RWLock;

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWScopeLock Lock(RWLock, FRWScopeLockType::SLT_ReadOnly);
				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWScopeLock Lock(RWLock, FRWScopeLockType::SLT_ReadOnly);
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}

	SECTION("Inside Transaction With Read Lock")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;
				FTransactionallySafeRWScopeLock Lock(RWLock, FRWScopeLockType::SLT_ReadOnly);
				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;
				FTransactionallySafeRWScopeLock Lock(RWLock, FRWScopeLockType::SLT_ReadOnly);
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}

	SECTION("Inside Transaction Used In Nested Transaction With Read Lock")
	{
		AutoRTFM::ETransactionResult InnerResult = AutoRTFM::ETransactionResult::Committed;

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;

				InnerResult = AutoRTFM::Transact([&]
					{
						FTransactionallySafeRWScopeLock Lock(RWLock, FRWScopeLockType::SLT_ReadOnly);
						AutoRTFM::CascadingAbortTransaction();
					});
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByCascade == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;

				InnerResult = AutoRTFM::Transact([&]
					{
						FTransactionallySafeRWScopeLock Lock(RWLock, FRWScopeLockType::SLT_ReadOnly);
						AutoRTFM::AbortTransaction();
					});
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == InnerResult);
		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;

				InnerResult = AutoRTFM::Transact([&]
					{
						FTransactionallySafeRWScopeLock Lock(RWLock, FRWScopeLockType::SLT_ReadOnly);
					});

				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;

				InnerResult = AutoRTFM::Transact([&]
					{
						FTransactionallySafeRWScopeLock Lock(RWLock, FRWScopeLockType::SLT_ReadOnly);
					});
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == InnerResult);
		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}

	SECTION("Outside Transaction With Write Lock")
	{
		FTransactionallySafeRWLock RWLock;

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWScopeLock Lock(RWLock, FRWScopeLockType::SLT_Write);
				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWScopeLock Lock(RWLock, FRWScopeLockType::SLT_Write);
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}

	SECTION("Inside Transaction With Write Lock")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;
				FTransactionallySafeRWScopeLock Lock(RWLock, FRWScopeLockType::SLT_Write);
				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;
				FTransactionallySafeRWScopeLock Lock(RWLock, FRWScopeLockType::SLT_Write);
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}

	SECTION("Inside Transaction Used In Nested Transaction With Write Lock")
	{
		AutoRTFM::ETransactionResult InnerResult = AutoRTFM::ETransactionResult::Committed;

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;

				InnerResult = AutoRTFM::Transact([&]
					{
						FTransactionallySafeRWScopeLock Lock(RWLock, FRWScopeLockType::SLT_Write);
						AutoRTFM::CascadingAbortTransaction();
					});
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByCascade == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;

				InnerResult = AutoRTFM::Transact([&]
					{
						FTransactionallySafeRWScopeLock Lock(RWLock, FRWScopeLockType::SLT_Write);
						AutoRTFM::AbortTransaction();
					});
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == InnerResult);
		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;

				InnerResult = AutoRTFM::Transact([&]
					{
						FTransactionallySafeRWScopeLock Lock(RWLock, FRWScopeLockType::SLT_Write);
					});

				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;

				InnerResult = AutoRTFM::Transact([&]
					{
						FTransactionallySafeRWScopeLock Lock(RWLock, FRWScopeLockType::SLT_Write);
					});
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == InnerResult);
		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}
}

TEST_CASE("UECore.TransactionallySafeReadScopeLock")
{
	SECTION("Outside Transaction")
	{
		FTransactionallySafeRWLock RWLock;

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeReadScopeLock Lock(RWLock);
				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeReadScopeLock Lock(RWLock);
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}

	SECTION("Inside Transaction")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;
				FTransactionallySafeReadScopeLock Lock(RWLock);
				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;
				FTransactionallySafeReadScopeLock Lock(RWLock);
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}

	SECTION("Inside Transaction Used In Nested Transaction")
	{
		AutoRTFM::ETransactionResult InnerResult = AutoRTFM::ETransactionResult::Committed;

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;

				InnerResult = AutoRTFM::Transact([&]
					{
						FTransactionallySafeReadScopeLock Lock(RWLock);
						AutoRTFM::CascadingAbortTransaction();
					});
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByCascade == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;

				InnerResult = AutoRTFM::Transact([&]
					{
						FTransactionallySafeReadScopeLock Lock(RWLock);
						AutoRTFM::AbortTransaction();
					});
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == InnerResult);
		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;

				InnerResult = AutoRTFM::Transact([&]
					{
						FTransactionallySafeReadScopeLock Lock(RWLock);
					});

				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;

				InnerResult = AutoRTFM::Transact([&]
					{
						FTransactionallySafeReadScopeLock Lock(RWLock);
					});
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == InnerResult);
		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}
}

TEST_CASE("UECore.TransactionallySafeWriteScopeLock")
{
	SECTION("Outside Transaction")
	{
		FTransactionallySafeRWLock RWLock;

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeWriteScopeLock Lock(RWLock);
				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeWriteScopeLock Lock(RWLock);
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}

	SECTION("Inside Transaction")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;
				FTransactionallySafeWriteScopeLock Lock(RWLock);
				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;
				FTransactionallySafeWriteScopeLock Lock(RWLock);
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}

	SECTION("Inside Transaction Used In Nested Transaction")
	{
		AutoRTFM::ETransactionResult InnerResult = AutoRTFM::ETransactionResult::Committed;

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;

				InnerResult = AutoRTFM::Transact([&]
					{
						FTransactionallySafeWriteScopeLock Lock(RWLock);
						AutoRTFM::CascadingAbortTransaction();
					});
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByCascade == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;

				InnerResult = AutoRTFM::Transact([&]
					{
						FTransactionallySafeWriteScopeLock Lock(RWLock);
						AutoRTFM::AbortTransaction();
					});
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == InnerResult);
		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;

				InnerResult = AutoRTFM::Transact([&]
					{
						FTransactionallySafeWriteScopeLock Lock(RWLock);
					});

				AutoRTFM::AbortTransaction();
			});

		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		Result = AutoRTFM::Transact([&]
			{
				FTransactionallySafeRWLock RWLock;

				InnerResult = AutoRTFM::Transact([&]
					{
						FTransactionallySafeWriteScopeLock Lock(RWLock);
					});
			});

		REQUIRE(AutoRTFM::ETransactionResult::Committed == InnerResult);
		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}
}

TEST_CASE("UECore.FTextFormatPatternDefinition")
{
	FTextFormatPatternDefinitionConstPtr Ptr;

	REQUIRE(!Ptr.IsValid());

	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Ptr = FTextFormatPatternDefinition::GetDefault().ToSharedPtr();
			AutoRTFM::AbortTransaction();
		});

	REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	REQUIRE(!Ptr.IsValid());

	Result = AutoRTFM::Transact([&]
		{
			Ptr = FTextFormatPatternDefinition::GetDefault().ToSharedPtr();
		});

	REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	REQUIRE(Ptr.IsValid());
}

TEST_CASE("UECore.FString")
{
	SECTION("Printf")
	{
		FString String;

		AutoRTFM::Commit([&]
			{
				String = FString::Printf(TEXT("Foo '%s' Bar"), TEXT("Stuff"));
			});

		REQUIRE(String == "Foo 'Stuff' BAR");
	}

	SECTION("Returned From Open")
	{
		SECTION("Copied New")
		{
			FString String;

			AutoRTFM::Commit([&]
				{
					String = AutoRTFM::Open([&]
						{
							return TEXT("WOW");
						});
				});

			REQUIRE(String == "WOW");
		}

		SECTION("Copied Old")
		{
			FString Other = TEXT("WOW");
			FString String;

			AutoRTFM::Commit([&]
				{
					String = AutoRTFM::Open([&]
						{
							return Other;
						});
				});

			REQUIRE(Other == "WOW");
			REQUIRE(String == "WOW");
		}
	}
}

TEST_CASE("UECore.TQueue")
{
	SECTION("SingleThreaded")
	{
		SECTION("Constructor")
		{
			AutoRTFM::Commit([&]
				{
					TQueue<int, EQueueMode::SingleThreaded> Queue;

					AutoRTFM::Open([&]
						{
							REQUIRE(nullptr == Queue.Peek());
						});
				});
		}

		SECTION("Dequeue")
		{
			TQueue<int, EQueueMode::SingleThreaded> Queue;
			REQUIRE(Queue.Enqueue(42));
			REQUIRE(!Queue.IsEmpty());

			int Value = 0;
			bool bSucceeded = false;

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
				{
					bSucceeded = Queue.Dequeue(Value);
					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
			REQUIRE(!bSucceeded);
			REQUIRE(0 == Value);
			REQUIRE(42 == *Queue.Peek());

			Result = AutoRTFM::Transact([&]
				{
					bSucceeded = Queue.Dequeue(Value);
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
			REQUIRE(bSucceeded);
			REQUIRE(42 == Value);
			REQUIRE(Queue.IsEmpty());
		}

		SECTION("Empty")
		{
			TQueue<int, EQueueMode::SingleThreaded> Queue;
			REQUIRE(Queue.Enqueue(42));
			REQUIRE(!Queue.IsEmpty());

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
				{
					Queue.Empty();
					AutoRTFM::Open([&] { REQUIRE(Queue.IsEmpty()); });
					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
			REQUIRE(42 == *Queue.Peek());

			Result = AutoRTFM::Transact([&]
				{
					Queue.Empty();
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
			REQUIRE(Queue.IsEmpty());
		}

		SECTION("Enqueue")
		{
			TQueue<int, EQueueMode::SingleThreaded> Queue;

			bool bSucceeded = false;

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
				{
					bSucceeded = Queue.Enqueue(42);
					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
			REQUIRE(Queue.IsEmpty());
			REQUIRE(!bSucceeded);

			Result = AutoRTFM::Transact([&]
				{
					bSucceeded = Queue.Enqueue(42);
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
			REQUIRE(42 == *Queue.Peek());
			REQUIRE(bSucceeded);
		}

		SECTION("IsEmpty")
		{
			TQueue<int, EQueueMode::SingleThreaded> Queue;
			REQUIRE(Queue.IsEmpty());

			bool bIsEmpty = false;

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
				{
					bIsEmpty = Queue.IsEmpty();
					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
			REQUIRE(!bIsEmpty);
			
			Result = AutoRTFM::Transact([&]
				{
					bIsEmpty = Queue.IsEmpty();
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
			REQUIRE(bIsEmpty);

			Queue.Enqueue(42);
			REQUIRE(!Queue.IsEmpty());

			Result = AutoRTFM::Transact([&]
				{
					bIsEmpty = Queue.IsEmpty();
					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
			REQUIRE(bIsEmpty);

			Result = AutoRTFM::Transact([&]
				{
					bIsEmpty = Queue.IsEmpty();
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
			REQUIRE(!bIsEmpty);
		}

		SECTION("Peek")
		{
			TQueue<int, EQueueMode::SingleThreaded> Queue;
			REQUIRE(Queue.Enqueue(42));

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
				{
					*Queue.Peek() = 13;
					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
			REQUIRE(42 == *Queue.Peek());

			Result = AutoRTFM::Transact([&]
				{
					*Queue.Peek() = 13;
				});

			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
			REQUIRE(13 == *Queue.Peek());
		}

		SECTION("Pop")
		{
			SECTION("Empty")
			{
				TQueue<int, EQueueMode::SingleThreaded> Queue;

				bool bSucceeded = true;

				AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
					{
						bSucceeded = Queue.Pop();
						AutoRTFM::AbortTransaction();
					});

				REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
				REQUIRE(bSucceeded);

				Result = AutoRTFM::Transact([&]
					{
						bSucceeded = Queue.Pop();
					});

				REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
				REQUIRE(!bSucceeded);
			}

			SECTION("Non Empty")
			{
				TQueue<int, EQueueMode::SingleThreaded> Queue;
				REQUIRE(Queue.Enqueue(42));

				bool bSucceeded = false;

				AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
					{
						bSucceeded = Queue.Pop();
						AutoRTFM::AbortTransaction();
					});

				REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
				REQUIRE(!bSucceeded);
				REQUIRE(!Queue.IsEmpty());

				Result = AutoRTFM::Transact([&]
					{
						bSucceeded = Queue.Pop();
					});

				REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
				REQUIRE(bSucceeded);
				REQUIRE(Queue.IsEmpty());
			}
		}
	}
}

TEST_CASE("UECore.FConfigFile")
{
	SECTION("Empty")
	{
		FConfigFile Config;

		Config.FindOrAddConfigSection(TEXT("WOW"));

		REQUIRE(!Config.IsEmpty());
	
		AutoRTFM::Commit([&]
			{
				Config.Empty();
			});
		
		REQUIRE(Config.IsEmpty());
	}
}
