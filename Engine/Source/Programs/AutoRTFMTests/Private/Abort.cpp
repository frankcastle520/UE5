// Copyright Epic Games, Inc. All Rights Reserved.

#include "Catch2Includes.h"
#include <AutoRTFM/AutoRTFM.h>
#include <map>
#include <vector>

TEST_CASE("Abort")
{
    int x = 42;
    std::vector<int> v;
    std::map<int, std::vector<int>> m;
    v.push_back(100);
    m[1].push_back(2);
    m[1].push_back(3);
    m[4].push_back(5);
    m[6].push_back(7);
    m[6].push_back(8);
    m[6].push_back(9);

	auto transaction = AutoRTFM::Transact([&]()
    {
		x = 5;
    	for (size_t n = 10; n--;)
    		v.push_back(2 * n);
    	m.clear();
    	m[10].push_back(11);
    	m[12].push_back(13);
    	m[12].push_back(14);
    	AutoRTFM::AbortTransaction();
	});

    REQUIRE(
        AutoRTFM::ETransactionResult::AbortedByRequest ==
		transaction);
    REQUIRE(x == 42);
    REQUIRE(v.size() == 1);
    REQUIRE(v[0] == 100);
    REQUIRE(m.size() == 3);
    REQUIRE(m[1].size() == 2);
    REQUIRE(m[1][0] == 2);
    REQUIRE(m[1][1] == 3);
    REQUIRE(m[4].size() == 1);
    REQUIRE(m[4][0] == 5);
    REQUIRE(m[6].size() == 3);
    REQUIRE(m[6][0] == 7);
    REQUIRE(m[6][1] == 8);
    REQUIRE(m[6][2] == 9);
}

TEST_CASE("Abort.NestedAbortOrder")
{
	AutoRTFM::ETransactionResult InnerResult;
	unsigned Orderer = 0;

	AutoRTFM::Commit([&]
	{
		// If we are retrying transactions, need to reset the test state.
		AutoRTFM::OnAbort([&]
			{
				Orderer = 0;
			});

		InnerResult = AutoRTFM::Transact([&]
			{
				AutoRTFM::OnAbort([&]
					{
						REQUIRE(1 == Orderer);
						Orderer += 1;
					});

				AutoRTFM::OnAbort([&]
					{
						REQUIRE(0 == Orderer);
						Orderer += 1;
					});

				AutoRTFM::AbortTransaction();
			});
	});

	REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == InnerResult);
	REQUIRE(2 == Orderer);
}

TEST_CASE("Abort.TransactionInOnCommit")
{
	AutoRTFM::ETransactionResult InnerResult;

	AutoRTFM::Commit([&]
	{
		AutoRTFM::OnCommit([&]
		{
			bool bDidSomething = false;

			InnerResult = AutoRTFM::Transact([&]
			{
				bDidSomething = true;
			});

			REQUIRE(false == bDidSomething);
		});
	});

	REQUIRE(AutoRTFM::ETransactionResult::AbortedByTransactInOnCommit == InnerResult);
}

TEST_CASE("Abort.TransactionInOnAbort")
{
	AutoRTFM::ETransactionResult Result;
	AutoRTFM::ETransactionResult InnerResult;

	Result = AutoRTFM::Transact([&]
	{
		AutoRTFM::OnAbort([&]
		{
			bool bDidSomething = false;

			InnerResult = AutoRTFM::Transact([&]
			{
				bDidSomething = true;
			});

			REQUIRE(false == bDidSomething);
		});

		AutoRTFM::AbortTransaction();
	});

	REQUIRE(AutoRTFM::ETransactionResult::AbortedByTransactInOnAbort == InnerResult);
	REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
}

TEST_CASE("Abort.Cascade")
{
	bool bTouched = false;

	const AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
	{
		bTouched = true;
		AutoRTFM::Transact([&]
		{
			AutoRTFM::CascadingAbortTransaction();
		});
	});

	REQUIRE(AutoRTFM::ETransactionResult::AbortedByCascade == Result);
	REQUIRE(false == bTouched);
}

TEST_CASE("Abort.CascadeThroughOpen")
{
	bool bTouched = false;

	const AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
	{
		bTouched = true;

		AutoRTFM::Open([&]
		{
			const AutoRTFM::EContextStatus Status = AutoRTFM::Close([&]
			{
				AutoRTFM::Transact([&]
				{
					AutoRTFM::CascadingAbortTransaction();
				});
			});

			REQUIRE(AutoRTFM::EContextStatus::AbortedByCascade == Status);
		});
	});

	REQUIRE(AutoRTFM::ETransactionResult::AbortedByCascade == Result);
	REQUIRE(false == bTouched);
}

TEST_CASE("Abort.CascadeThroughManualTransaction")
{
	bool bTouched = false;

	const AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
	{
		bTouched = true;

		AutoRTFM::Open([&]
		{
			REQUIRE(true == AutoRTFM::ForTheRuntime::StartTransaction());

			const AutoRTFM::EContextStatus Status = AutoRTFM::Close([&]
			{
				AutoRTFM::CascadingAbortTransaction();
			});

			REQUIRE(AutoRTFM::EContextStatus::AbortedByCascade == Status);

			// We need to clear the status ourselves.
			AutoRTFM::ForTheRuntime::ClearTransactionStatus();

			// Before manually starting the cascade again.
			AutoRTFM::CascadingAbortTransaction();
		});
	});

	REQUIRE(AutoRTFM::ETransactionResult::AbortedByCascade == Result);
	REQUIRE(false == bTouched);
}

inline void* UIntToPointer(uint64 Value)
{
	union
	{
		uint64 Int;
		void* Ptr;
	};

	Int = Value;
	return Ptr;
}

TEST_CASE("Abort.PushOnAbortHandler_NoAbort")
{
	int Value = 55;

	AutoRTFM::Commit([&]
	{
		Value = 66;
		AutoRTFM::PushOnAbortHandler(UIntToPointer(747), [&Value](){ Value = 77; });
	});

	REQUIRE(Value == 66);
}

TEST_CASE("Abort.PushOnAbortHandler_WithAbort")
{
	int Value = 55;

	const AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
	{
		Value = 66;
		AutoRTFM::PushOnAbortHandler(UIntToPointer(747), [&Value](){ Value = 77; });

		AutoRTFM::AbortTransaction();
	});

	REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	REQUIRE(Value == 77);
}

TEST_CASE("Abort.PushOnAbortHandler_WithPop_NoAbort")
{
	int Value = 55;

	AutoRTFM::Commit([&]
		{
			Value = 66;
			AutoRTFM::PushOnAbortHandler(UIntToPointer(747), [&Value]() { Value = 77; });
			Value = 88;

			AutoRTFM::PopOnAbortHandler(UIntToPointer(747));
		});

	REQUIRE(Value == 88);
}

TEST_CASE("Abort.PushOnAbortHandler_WithPopAll_NoAbort")
{
	int Value = 55;

	AutoRTFM::Commit([&]
	{
		Value = 66;
		AutoRTFM::PushOnAbortHandler(UIntToPointer(747), [&Value](){ Value = 77; });
		Value = 88;

		AutoRTFM::PopAllOnAbortHandlers(UIntToPointer(747));
	});

	REQUIRE(Value == 88);
}

TEST_CASE("Abort.PushOnAbortHandler_WithPop_WithAbort")
{
	int Value = 55;

	const AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
	{
		Value = 66;
		AutoRTFM::PushOnAbortHandler(UIntToPointer(747), [&Value](){ Value = 77; });
		Value = 88;

		AutoRTFM::PopOnAbortHandler(UIntToPointer(747));

		AutoRTFM::AbortTransaction();
	});

	REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	REQUIRE(Value == 55);
}

TEST_CASE("Abort.PushOnAbortHandler_WithPopAll_WithAbort")
{
	int Value = 55;

	const AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
	{
		Value = 66;
		AutoRTFM::PushOnAbortHandler(UIntToPointer(747), [&Value]() { Value = 77; });
		Value = 88;

		AutoRTFM::PopAllOnAbortHandlers(UIntToPointer(747));

		AutoRTFM::AbortTransaction();
	});

	REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	REQUIRE(Value == 55);
}

TEST_CASE("Abort.PushOnAbortHandler_Duplicates1")
{
	int Value = 55;

	const AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Value = 66;
			AutoRTFM::PushOnAbortHandler(UIntToPointer(747), [&Value]() { Value = 77; });
			AutoRTFM::PushOnAbortHandler(UIntToPointer(747), [&Value]() { Value = 88; });
			Value = 99;

			AutoRTFM::PopOnAbortHandler(UIntToPointer(747));

			AutoRTFM::AbortTransaction();
		});

	REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

	// The first push on abort will still go through.
	REQUIRE(Value == 77);
}

TEST_CASE("Abort.PushOnAbortHandler_PopAll_Duplicates")
{
	int Value = 55;

	const AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
	{
		Value = 66;
		AutoRTFM::PushOnAbortHandler(UIntToPointer(747), [&Value]() { Value = 77; });
		AutoRTFM::PushOnAbortHandler(UIntToPointer(747), [&Value]() { Value = 88; });
		Value = 99;

		AutoRTFM::PopAllOnAbortHandlers(UIntToPointer(747));

		AutoRTFM::AbortTransaction();
	});

	REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);

	// No abort handlers should execute.
	REQUIRE(Value == 55);
}

TEST_CASE("Abort.PushOnAbortHandler_Duplicates2")
{
	int Value = 55;

	const AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
	{
		Value = 66;
		AutoRTFM::PushOnAbortHandler(UIntToPointer(747), [&Value](){ Value += 12; });
		AutoRTFM::PushOnAbortHandler(UIntToPointer(747), [&Value](){ Value = 65; });
		Value = 99;

		AutoRTFM::AbortTransaction();
	});

	REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	REQUIRE(Value == 77);
}

TEST_CASE("Abort.PushOnAbortHandler_Order")
{
	SECTION("HandlerSandwich")
	{
		SECTION("WithoutPop")
		{
			int Value = 37;

			const AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
				{
					AutoRTFM::OnAbort([&Value] { REQUIRE(42 == Value); Value += 1; });
					AutoRTFM::PushOnAbortHandler(UIntToPointer(747), [&Value]() { REQUIRE(40 == Value); Value += 2; });
					AutoRTFM::OnAbort([&Value] { REQUIRE(37 == Value); Value += 3; });

					Value = 99;

					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
			REQUIRE(Value == 43);
		}

		SECTION("WithPop")
		{
			int Value = 37;

			const AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
				{
					AutoRTFM::OnAbort([&Value] { REQUIRE(40 == Value); Value += 1; });
					AutoRTFM::PushOnAbortHandler(UIntToPointer(747), [&Value]() { REQUIRE(false); });
					AutoRTFM::OnAbort([&Value] { REQUIRE(37 == Value); Value += 3; });

					AutoRTFM::PopOnAbortHandler(UIntToPointer(747));

					Value = 99;

					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
			REQUIRE(Value == 41);
		}
	}

	SECTION("HandlerInChild")
	{
		SECTION("WithoutPop")
		{
			int Value = 37;

			const AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
				{
					AutoRTFM::OnAbort([&Value]
						{
							REQUIRE(42 == Value);
							Value += 1;
						});

					// Make a child transaction.
					AutoRTFM::Commit([&]
						{
							AutoRTFM::PushOnAbortHandler(UIntToPointer(747), [&Value]()
								{
									// If we are retrying nested transactions too, we can't check that
									// the value was something specific before hand!
									if (!AutoRTFM::ForTheRuntime::ShouldRetryNestedTransactionsToo())
									{
										REQUIRE(40 == Value);
										Value += 2;
									}
									else
									{
										Value += 1;
									}
								});
						});

					AutoRTFM::OnAbort([&Value]
						{
							// If we are retrying nested transactions too, we've ran the on-abort in the
							// child transaction once, so our value will be larger.
							if (!AutoRTFM::ForTheRuntime::ShouldRetryNestedTransactionsToo())
							{
								REQUIRE(37 == Value);
							}
							else
							{
								REQUIRE(38 == Value);
							}

							Value += 3;
						});

					Value = 99;

					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
			REQUIRE(Value == 43);
		}

		SECTION("WithPop")
		{
			int Value = 37;

			const AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
				{
					AutoRTFM::OnAbort([&Value] { REQUIRE(40 == Value); Value += 1; });

					// Make a child transaction.
					AutoRTFM::Commit([&]
						{
							AutoRTFM::PushOnAbortHandler(UIntToPointer(747), [&Value]()
								{
									// Only if we are retrying on 
									REQUIRE(AutoRTFM::ForTheRuntime::ShouldRetryNestedTransactionsToo());
								});
						});

					AutoRTFM::OnAbort([&Value] { REQUIRE(37 == Value); Value += 3; });

					// Bit funky, but we can pop the child's push here!
					AutoRTFM::PopOnAbortHandler(UIntToPointer(747));

					Value = 99;

					AutoRTFM::AbortTransaction();
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
			REQUIRE(Value == 41);
		}

		SECTION("AbortInChild")
		{
			int Value = 99;

			AutoRTFM::ETransactionResult Result = AutoRTFM::ETransactionResult::Committed;
			AutoRTFM::Commit([&]
				{
					AutoRTFM::OnCommit([&Value] { REQUIRE(37 == Value); Value += 1; });

					// Make a child transaction.
					Result = AutoRTFM::Transact([&]
						{
							AutoRTFM::PushOnAbortHandler(UIntToPointer(747), [&Value]() { REQUIRE(99 == Value); Value += 2; });
							AutoRTFM::AbortTransaction();
						});

					AutoRTFM::Open([&]
					{
						REQUIRE(Value == 101);
					});

					AutoRTFM::OnCommit([&Value] { REQUIRE(38 == Value); Value += 3; });

					Value = 37;

					AutoRTFM::OnAbort([&Value] { Value = 99; });
				});

			REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
			REQUIRE(Value == 41);
		}
	}
}

TEST_CASE("Abort.OnAbortTiming")
{
	bool bOnAbortRan = false;
	int Memory = 666;
	AutoRTFM::Commit([&]
	{
		// If we are retrying transactions, need to reset the test state.
		AutoRTFM::OnAbort([&]
		{
			REQUIRE(bOnAbortRan);
			REQUIRE(Memory == 666);
			bOnAbortRan = false;
		});

		REQUIRE(bOnAbortRan == false);
		REQUIRE(Memory == 666);

		AutoRTFM::Transact([&]
		{
			Memory = 1234;
			REQUIRE(Memory == 1234);

			AutoRTFM::OnAbort([&]
			{
				REQUIRE(Memory == 666);
				bOnAbortRan = true;
			});

			AutoRTFM::AbortTransaction();
		});
	});
	REQUIRE(Memory == 666);
	REQUIRE(bOnAbortRan == true);
}

static void FnHasNoClosed()
{
	(void)fopen("fopen() is not supported in a closed transaction", "rb");
}

TEST_CASE("Abort.Language")
{
	bool bTouched = false;

	const AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			bTouched = true;
			FnHasNoClosed();
		});

	REQUIRE(AutoRTFM::ETransactionResult::AbortedByLanguage == Result);
	REQUIRE(false == bTouched);
}

TEST_CASE("Abort.LanguageThroughOpen")
{
	bool bTouched = false;

	const AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			bTouched = true;

			AutoRTFM::Open([&]
				{
					const AutoRTFM::EContextStatus Status = AutoRTFM::Close([&]
						{
							FnHasNoClosed();
						});

					REQUIRE(AutoRTFM::EContextStatus::AbortedByLanguage == Status);
				});
		});

	REQUIRE(AutoRTFM::ETransactionResult::AbortedByLanguage == Result);
	REQUIRE(false == bTouched);
}

// Test for SOL-5804
TEST_CASE("Abort.StackWriteToOuterOpen")
{
	std::string_view TestResult;
	AutoRTFM::EContextStatus CloseStatus = AutoRTFM::EContextStatus::Idle;
	bool WritesUndone = true;

	const AutoRTFM::ETransactionResult TransactionResult = AutoRTFM::Transact([&]
	{
		AutoRTFM::Open([&]
		{
			std::array<int, 64> Values{};

			CloseStatus = AutoRTFM::Close([&]
			{
				// On stack outside transaction.
				// Should be reverted as part of the abort.
				WritesUndone = false;

				// On stack inside transaction.
				// Writes should not be reverted as part of the abort.
				for (size_t I = 0; I < Values.size(); I++)
				{
					Values[I] = static_cast<int>(I * 10);
				}
			});
		});

		// If any of the variables on the stack within the Open() get written to
		// on abort, then it should change the values of this array.
		std::array<int, 64> StackGuard{};

		// The OnAbort handler should be called *after* the memory is reverted.
		AutoRTFM::OnAbort([&]
		{
			if (!WritesUndone)
			{
				TestResult = "OnAbort was called without first reverting memory";
			}
			else if (StackGuard != std::array<int, 64>{})
			{
				TestResult = "StackGuard was corrupted";
			}
			else
			{
				TestResult = "Success";
			}
		});

		// Do the abort!
		AutoRTFM::AbortTransaction();
	});

	REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == TransactionResult);
	REQUIRE(AutoRTFM::EContextStatus::OnTrack == CloseStatus);
	REQUIRE("Success" == TestResult);
}
