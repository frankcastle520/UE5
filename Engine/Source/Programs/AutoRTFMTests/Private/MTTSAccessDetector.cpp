// Copyright Epic Games, Inc. All Rights Reserved.

#include "Catch2Includes.h"
#include "Misc/MTTransactionallySafeAccessDetector.h"
#include <AutoRTFM/AutoRTFM.h>

#if ENABLE_MT_DETECTOR

TEST_CASE("MTTransactionallySafeAccessDetector")
{
	FRWTransactionallySafeAccessDetector Detector;

	SECTION("AcquireWriteAccess, ReleaseWriteAccess")
	{
		Detector.AcquireWriteAccess();
		Detector.ReleaseWriteAccess();
	}

	SECTION("AcquireReadAccess, ReleaseReadAccess")
	{
		Detector.AcquireReadAccess();
		Detector.ReleaseReadAccess();
	}

	SECTION("AcquireReadAccess, AcquireReadAccess, ReleaseReadAccess, ReleaseReadAccess")
	{
		Detector.AcquireReadAccess();
		Detector.AcquireReadAccess();
		Detector.ReleaseReadAccess();
		Detector.ReleaseReadAccess();
	}

	SECTION("Transact(AcquireWriteAccess, ReleaseWriteAccess)")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Detector.AcquireWriteAccess();
			Detector.ReleaseWriteAccess();
		});
		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}

	SECTION("Transact(AcquireWriteAccess, ReleaseWriteAccess, Abort)")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Detector.AcquireWriteAccess();
			Detector.ReleaseWriteAccess();
			AutoRTFM::AbortTransaction();
		});
		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	}

	SECTION("Transact(AcquireWriteAccess), ReleaseWriteAccess")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Detector.AcquireWriteAccess();
		});
		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);

		Detector.ReleaseWriteAccess();
	}

	SECTION("Transact(AcquireWriteAccess, Abort)")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Detector.AcquireWriteAccess();
			AutoRTFM::AbortTransaction();
		});
		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	}

	SECTION("Transact(AcquireReadAccess, Abort)")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Detector.AcquireReadAccess();
			AutoRTFM::AbortTransaction();
		});
		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	}

	SECTION("AcquireReadAccess, Transact(AcquireReadAccess, Abort), ReleaseReadAccess")
	{
		Detector.AcquireReadAccess();

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Detector.AcquireReadAccess();
			AutoRTFM::AbortTransaction();
		});
		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		Detector.ReleaseReadAccess();
	}

	SECTION("AcquireWriteAccess, Transact(ReleaseWriteAccess)")
	{
		Detector.AcquireWriteAccess();

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Detector.ReleaseWriteAccess();
		});
		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}

	SECTION("AcquireReadAccess, Transact(ReleaseReadAccess)")
	{
		Detector.AcquireReadAccess();

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Detector.ReleaseReadAccess();
		});
		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}

	SECTION("AcquireWriteAccess, Transact(ReleaseWriteAccess, Abort), ReleaseWriteAccess")
	{
		Detector.AcquireWriteAccess();

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Detector.ReleaseWriteAccess();
			AutoRTFM::AbortTransaction();
		});
		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		Detector.ReleaseWriteAccess();
	}

	SECTION("Transact(AcquireReadAccess, AcquireReadAccess, ReleaseReadAccess, ReleaseReadAccess)")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Detector.AcquireReadAccess();
			Detector.AcquireReadAccess();
			Detector.ReleaseReadAccess();
			Detector.ReleaseReadAccess();
		});
		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}

	SECTION("Transact(AcquireReadAccess, AcquireReadAccess, ReleaseReadAccess, ReleaseReadAccess, Abort)")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Detector.AcquireReadAccess();
			Detector.AcquireReadAccess();
			Detector.ReleaseReadAccess();
			Detector.ReleaseReadAccess();
			AutoRTFM::AbortTransaction();
		});
		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	}
	
	SECTION("Transact(AcquireWriteAccess, ReleaseWriteAccess, AcquireReadAccess)")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Detector.AcquireWriteAccess();
			Detector.ReleaseWriteAccess();
			Detector.AcquireReadAccess();
		});
		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
		
		Detector.ReleaseReadAccess();
	}

	SECTION("Transact(AcquireWriteAccess, ReleaseWriteAccess, AcquireReadAccess, Abort)")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Detector.AcquireWriteAccess();
			Detector.ReleaseWriteAccess();
			Detector.AcquireReadAccess();
			AutoRTFM::AbortTransaction();
		});
		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	}

	SECTION("Transact(AcquireWriteAccess, ReleaseWriteAccess, AcquireReadAccess, ReleaseReadAccess)")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Detector.AcquireWriteAccess();
			Detector.ReleaseWriteAccess();
			Detector.AcquireReadAccess();
			Detector.ReleaseReadAccess();
		});
		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}

	SECTION("Transact(AcquireWriteAccess, ReleaseWriteAccess, AcquireReadAccess, ReleaseReadAccess, Abort)")
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Detector.AcquireWriteAccess();
			Detector.ReleaseWriteAccess();
			Detector.AcquireReadAccess();
			Detector.ReleaseReadAccess();
			AutoRTFM::AbortTransaction();
		});
		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	}

	SECTION("AcquireReadAccess, Transact(ReleaseReadAccess, AcquireWriteAccess), ReleaseWriteAccess")
	{
		Detector.AcquireReadAccess();

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Detector.ReleaseReadAccess();
			Detector.AcquireWriteAccess();
		});
		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);

		Detector.ReleaseWriteAccess();
	}

	SECTION("AcquireReadAccess, Transact(ReleaseReadAccess, AcquireWriteAccess, Abort), ReleaseReadAccess")
	{
		Detector.AcquireReadAccess();

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Detector.ReleaseReadAccess();
			Detector.AcquireWriteAccess();
			AutoRTFM::AbortTransaction();
		});
		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		Detector.ReleaseReadAccess();
	}

	SECTION("AcquireReadAccess, Transact(ReleaseReadAccess, AcquireWriteAccess, ReleaseWriteAccess)")
	{
		Detector.AcquireReadAccess();

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Detector.ReleaseReadAccess();
			Detector.AcquireWriteAccess();
			Detector.ReleaseWriteAccess();
		});
		REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	}

	SECTION("AcquireReadAccess, Transact(ReleaseReadAccess, AcquireWriteAccess, ReleaseWriteAccess, Abort), ReleaseReadAccess")
	{
		Detector.AcquireReadAccess();

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Detector.ReleaseReadAccess();
			Detector.AcquireWriteAccess();
			Detector.ReleaseWriteAccess();
			AutoRTFM::AbortTransaction();
		});
		REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

		Detector.ReleaseReadAccess();
	}

	{
		// These tests use AutoRTFM::Open() blocks to acquire / release locks.
		// Transaction retries will cause the lock counts to go out of sync, so
		// we can only test these with retries disabled retries.
		struct DisableRetriesScope
		{
			DisableRetriesScope() : OldState(AutoRTFM::ForTheRuntime::GetRetryTransaction())
			{
				AutoRTFM::ForTheRuntime::SetRetryTransaction(AutoRTFM::ForTheRuntime::NoRetry);
			}
			~DisableRetriesScope()
			{
				AutoRTFM::ForTheRuntime::SetRetryTransaction(OldState);
			}
			AutoRTFM::ForTheRuntime::EAutoRTFMRetryTransactionState OldState;
		};
		DisableRetriesScope DisableRetries;

		SECTION("Transact(AcquireReadAccess, Open(AcquireReadAccess, ReleaseReadAccess), ReleaseReadAccess)")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				Detector.AcquireReadAccess();
				AutoRTFM::Open([&]
				{
					Detector.AcquireReadAccess();
					Detector.ReleaseReadAccess();
				});
				Detector.ReleaseReadAccess();
			});
			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
		}

		SECTION("Transact(AcquireReadAccess, Open(AcquireReadAccess, ReleaseReadAccess), ReleaseReadAccess, Abort)")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				Detector.AcquireReadAccess();
				AutoRTFM::Open([&]
				{
					Detector.AcquireReadAccess();
					Detector.ReleaseReadAccess();
				});
				Detector.ReleaseReadAccess();
				AutoRTFM::AbortTransaction();
			});
			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
		}

		SECTION("Transact(AcquireReadAccess, Open(AcquireReadAccess)), ReleaseReadAccess, ReleaseReadAccess")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				Detector.AcquireReadAccess();
				AutoRTFM::Open([&]
				{
					Detector.AcquireReadAccess();
				});
			});
			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);

			Detector.ReleaseReadAccess();
			Detector.ReleaseReadAccess();
		}

		SECTION("Transact(Open(AcquireReadAccess), ReleaseReadAccess, AcquireWriteAccess, Abort), ReleaseReadAccess")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				AutoRTFM::Open([&]
				{
					Detector.AcquireReadAccess();
				});
				Detector.ReleaseReadAccess();
				Detector.AcquireWriteAccess();
				AutoRTFM::AbortTransaction();
			});
			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

			Detector.ReleaseReadAccess();
		}

		SECTION("Transact(AcquireReadAccess, Open(AcquireReadAccess), Abort), ReleaseReadAccess")
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				Detector.AcquireReadAccess();
				AutoRTFM::Open([&]
				{
					Detector.AcquireReadAccess();
				});
				AutoRTFM::AbortTransaction();
			});
			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

			Detector.ReleaseReadAccess();
		}

		SECTION("AcquireReadAccess, Transact(AcquireReadAccess, Open(ReleaseReadAccess), Abort)")
		{
			Detector.AcquireReadAccess();

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				Detector.AcquireReadAccess();
				AutoRTFM::Open([&]
				{
					Detector.ReleaseReadAccess();
				});
				AutoRTFM::AbortTransaction();
			});
			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
		}

		SECTION("AcquireWriteAccess, Transact(Open(ReleaseWriteAccess), AcquireReadAccess, ReleaseReadAccess)")
		{
			Detector.AcquireWriteAccess();

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				AutoRTFM::Open([&]
				{
					Detector.ReleaseWriteAccess();
				});
				Detector.AcquireReadAccess();
			});
			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);

			Detector.ReleaseReadAccess();
		}

		SECTION("AcquireWriteAccess, Transact(Open(ReleaseWriteAccess), AcquireReadAccess, Abort)")
		{
			Detector.AcquireWriteAccess();

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				AutoRTFM::Open([&]
				{
					Detector.ReleaseWriteAccess();
				});
				Detector.AcquireReadAccess();
				AutoRTFM::AbortTransaction();
			});
			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
		}

		SECTION("AcquireReadAccess, Transact(Open(ReleaseReadAccess), AcquireWriteAccess), ReleaseWriteAccess")
		{
			Detector.AcquireReadAccess();

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				AutoRTFM::Open([&]
				{
					Detector.ReleaseReadAccess();
				});
				Detector.AcquireWriteAccess();
			});
			REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
		
			Detector.ReleaseWriteAccess();
		}

		SECTION("AcquireReadAccess, Transact(Open(ReleaseReadAccess), AcquireWriteAccess, Abort)")
		{
			Detector.AcquireReadAccess();

			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				AutoRTFM::Open([&]
				{
					Detector.ReleaseReadAccess();
				});
				Detector.AcquireWriteAccess();
				AutoRTFM::AbortTransaction();
			});
			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
		}
	}
}

#endif // ENABLE_MT_DETECTOR
