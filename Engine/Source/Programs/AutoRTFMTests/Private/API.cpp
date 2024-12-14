// Copyright Epic Games, Inc. All Rights Reserved.

#include "API.h"
#include "Catch2Includes.h"

#include <AutoRTFM/AutoRTFM.h>
#include <memory>
#include <thread>

TEST_CASE("API.autortfm_is_transactional")
{
    REQUIRE(false == autortfm_is_transactional());

    bool InTransaction = false;
    bool InOpenNest = false;

    AutoRTFM::Commit([&]
    {
        InTransaction = autortfm_is_transactional();

        AutoRTFM::Open([&]
        {
            InOpenNest = autortfm_is_transactional();
        });
    });

    REQUIRE(true == InTransaction);
    REQUIRE(true == InOpenNest);
}

TEST_CASE("API.autortfm_is_closed")
{
    REQUIRE(false == autortfm_is_closed());

    // Set to the opposite of what we expect at the end of function.
    bool InTransaction = false;
    bool InOpenNest = true;
    bool InClosedNestInOpenNest = false;

    AutoRTFM::Commit([&]
    {
        InTransaction = autortfm_is_closed();

        AutoRTFM::Open([&]
        {
            InOpenNest = autortfm_is_closed();

            REQUIRE(AutoRTFM::EContextStatus::OnTrack == AutoRTFM::Close([&]
            {
                InClosedNestInOpenNest = autortfm_is_closed();
            }));
        });
    });

    REQUIRE(true == InTransaction);
    REQUIRE(false == InOpenNest);
    REQUIRE(true == InClosedNestInOpenNest);
}

TEST_CASE("API.autortfm_abort_transaction")
{
    bool BeforeNest = false;
    bool InNest = false;
    bool AfterNest = false;
    AutoRTFM::ETransactionResult NestResult;

    AutoRTFM::Commit([&]
    {
        BeforeNest = true;

        NestResult = AutoRTFM::Transact([&]
        {
            // Because we are aborting this won't actually occur!
            InNest = true;

            autortfm_abort_transaction();
        });

        AfterNest = true;
    });

    REQUIRE(true == BeforeNest);
    REQUIRE(false == InNest);
    REQUIRE(true == AfterNest);
    REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == NestResult);
}

TEST_CASE("API.autortfm_abort_if_transactional")
{
    // Calling this outwith any transaction won't abort the program.
    autortfm_abort_if_transactional();

    bool BeforeNest = false;
    bool InNest = false;
    bool AfterNest = false;
    AutoRTFM::ETransactionResult NestResult;

    AutoRTFM::Commit([&]
    {
        BeforeNest = true;

        NestResult = AutoRTFM::Transact([&]
        {
            // Because we are aborting this won't actually occur!
            InNest = true;

            autortfm_abort_if_transactional();
        });

        AfterNest = true;
    });

    REQUIRE(true == BeforeNest);
    REQUIRE(false == InNest);
    REQUIRE(true == AfterNest);
    REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == NestResult);
}

TEST_CASE("API.autortfm_abort_if_closed")
{
    // Calling this outwith any transaction won't abort the program.
    autortfm_abort_if_closed();

    bool BeforeNest = false;
    bool InNest = false;
    bool AfterNest = false;

    REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == AutoRTFM::Transact([&]
    {
        BeforeNest = true;

        AutoRTFM::Open([&]
        {
            InNest = true;

            // This won't abort because we aren't closed!
            autortfm_abort_if_closed();
        });

        AfterNest = true;

        autortfm_abort_if_closed();
    }));

    REQUIRE(false == BeforeNest);
    REQUIRE(true == InNest);
    REQUIRE(false == AfterNest);
}

TEST_CASE("API.autortfm_open")
{
    int Answer = 6 * 9;

    // An open call outside a transaction succeeds.
    autortfm_open([](void* const Arg)
    {
        *static_cast<int* const>(Arg) = 42;
    }, &Answer);

    REQUIRE(42 == Answer);

    REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == AutoRTFM::Transact([&]
    {
        // An open call inside a transaction succeeds also.
        autortfm_open([](void* const Arg)
        {
            *static_cast<int* const>(Arg) *= 2;
        }, &Answer);

        AutoRTFM::AbortTransaction();
    }));

    REQUIRE(84 == Answer);
}

TEST_CASE("API.autortfm_register_open_function")
{
    autortfm_register_open_function(
        reinterpret_cast<void*>(NoAutoRTFM::DoSomethingC),
        reinterpret_cast<void*>(NoAutoRTFM::DoSomethingInTransactionC));

    int I = -42;

    AutoRTFM::Commit([&]
    {
        I = NoAutoRTFM::DoSomethingC(I);
    });

    REQUIRE(0 == I);
}

TEST_CASE("API.autortfm_on_commit")
{
    bool OuterTransaction = false;
    bool InnerTransaction = false;
    bool InnerTransactionWithAbort = false;
    bool InnerOpenNest = false;
    AutoRTFM::ETransactionResult NestResult;

    AutoRTFM::Commit([&]
    {
        autortfm_on_commit([](void* const Arg)
        {
            *static_cast<bool* const>(Arg) = true;
        }, &OuterTransaction);

        // This should only be modified on the commit!
        if (OuterTransaction)
        {
            AutoRTFM::AbortTransaction();
        }

        AutoRTFM::Commit([&]
        {
            autortfm_on_commit([](void* const Arg)
            {
                *static_cast<bool* const>(Arg) = true;
            }, &InnerTransaction);
        });

        // This should only be modified on the commit!
        if (InnerTransaction)
        {
            AutoRTFM::AbortTransaction();
        }

        NestResult = AutoRTFM::Transact([&]
        {
            autortfm_on_commit([](void* const Arg)
            {
                *static_cast<bool* const>(Arg) = true;
            }, &InnerTransactionWithAbort);

            AutoRTFM::AbortTransaction();
        });

        // This should never be modified because its transaction aborted!
        if (InnerTransactionWithAbort)
        {
            AutoRTFM::AbortTransaction();
        }

        AutoRTFM::Open([&]
        {
            autortfm_on_commit([](void* const Arg)
            {
                *static_cast<bool* const>(Arg) = true;
            }, &InnerOpenNest);

            // This should be modified immediately!
            if (!InnerOpenNest)
            {
                AutoRTFM::AbortTransaction();
            }
        });
    });

    REQUIRE(true == OuterTransaction);
    REQUIRE(true == InnerTransaction);
    REQUIRE(false == InnerTransactionWithAbort);
    REQUIRE(true == InnerOpenNest);
    REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == NestResult);
}

TEST_CASE("API.autortfm_on_abort")
{
	// Too hard to get this test working when retrying nested transactions so bail!
	if (AutoRTFM::ForTheRuntime::ShouldRetryNestedTransactionsToo())
	{
		return;
	}

    bool OuterTransaction = false;
    bool InnerTransaction = false;
    bool InnerTransactionWithAbort = false;
    bool InnerOpenNest = false;
    AutoRTFM::ETransactionResult NestResult = AutoRTFM::ETransactionResult::Committed;

    REQUIRE(AutoRTFM::ETransactionResult::Committed == AutoRTFM::Transact([&]
    {
		// If we are retrying transactions, need to reset the test state.
		AutoRTFM::OnAbort([&]
		{
			OuterTransaction = false;
			InnerTransaction = false;
			InnerTransactionWithAbort = false;
			InnerOpenNest = false;
			NestResult = AutoRTFM::ETransactionResult::Committed;
		});

        autortfm_on_abort([](void* const Arg)
        {
            *static_cast<bool* const>(Arg) = true;
        }, &OuterTransaction);

        // This should only be modified on the commit!
        if (OuterTransaction)
        {
            AutoRTFM::AbortTransaction();
        }

        AutoRTFM::Commit([&]
        {
            autortfm_on_abort([](void* const Arg)
            {
                *static_cast<bool* const>(Arg) = true;
            }, &InnerTransaction);
        });

        // This should only be modified on the commit!
        if (InnerTransaction)
        {
            AutoRTFM::AbortTransaction();
        }

        NestResult = AutoRTFM::Transact([&]
        {
            autortfm_on_abort([](void* const Arg)
            {
                *static_cast<bool* const>(Arg) = true;
            }, &InnerTransactionWithAbort);

            AutoRTFM::AbortTransaction();
        });

        // OnAbort runs eagerly on inner abort
        if (!InnerTransactionWithAbort)
        {
            AutoRTFM::AbortTransaction();
        }

        AutoRTFM::Open([&]
        {
            autortfm_on_abort([](void* const Arg)
            {
                *static_cast<bool* const>(Arg) = true;
            }, &InnerOpenNest);
        });

        // This should only be modified on the commit!
        if (InnerOpenNest)
        {
            AutoRTFM::AbortTransaction();
        }
    }));

    REQUIRE(false == OuterTransaction);
    REQUIRE(false == InnerTransaction);
    REQUIRE(true == InnerTransactionWithAbort);
    REQUIRE(false == InnerOpenNest);
    REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == NestResult);
}

TEST_CASE("API.autortfm_did_allocate")
{
    constexpr unsigned Size = 1024;
    unsigned BumpAllocator[Size];
    unsigned NextBump = 0;

    AutoRTFM::Commit([&]
    {
		// If we are retrying transactions, need to reset the test state.
		AutoRTFM::OnAbort([&]
		{
			NextBump = 0;
		});

        for (unsigned I = 0; I < Size; I++)
        {
            unsigned* Data;
            AutoRTFM::Open([&]
            {
                Data = reinterpret_cast<unsigned*>(autortfm_did_allocate(
                    &BumpAllocator[NextBump++],
                    sizeof(unsigned)));
            });

            *Data = I;
        }
    });

    for (unsigned I = 0; I < Size; I++)
    {
        REQUIRE(I == BumpAllocator[I]);
    }
}

TEST_CASE("API.autortfm_check_consistency_assuming_no_races")
{
    AutoRTFM::Commit([&]
    {
        autortfm_check_consistency_assuming_no_races();
    });
}

TEST_CASE("API.ETransactionResult")
{
    int Answer = 6 * 9;

    REQUIRE(AutoRTFM::ETransactionResult::Committed == AutoRTFM::Transact([&]
    {
        Answer = 42;
    }));

    REQUIRE(42 == Answer);

    REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == AutoRTFM::Transact([&]
    {
        Answer = 13;
        AutoRTFM::AbortTransaction();
    }));

    REQUIRE(42 == Answer);

    REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == AutoRTFM::Transact([&]
    {
        Answer = 13;
        AutoRTFM::AbortIfTransactional();
    }));
    
    REQUIRE(42 == Answer);
}

TEST_CASE("API.IsTransactional")
{
    REQUIRE(false == AutoRTFM::IsTransactional());

    bool InTransaction = false;
    bool InOpenNest = false;
	bool InAbort = true;
	bool InCommit = true;

    AutoRTFM::Commit([&]
    {
        InTransaction = AutoRTFM::IsTransactional();

        AutoRTFM::Open([&]
        {
            InOpenNest = AutoRTFM::IsTransactional();
        });

		AutoRTFM::Transact([&]
			{
				AutoRTFM::OnAbort([&]
					{
						InAbort = AutoRTFM::IsTransactional();
					});

				AutoRTFM::AbortTransaction();
			});

		AutoRTFM::OnCommit([&]
			{
				InCommit = AutoRTFM::IsTransactional();
			});
    });

    REQUIRE(true == InTransaction);
    REQUIRE(true == InOpenNest);
	REQUIRE(false == InAbort);
	REQUIRE(false == InCommit);
}

TEST_CASE("API.IsClosed")
{
    REQUIRE(false == AutoRTFM::IsClosed());

    // Set to the opposite of what we expect at the end of function.
    bool InTransaction = false;
    bool InOpenNest = true;
    bool InClosedNestInOpenNest = false;
	bool InAbort = true;
	bool InCommit = true;

    AutoRTFM::Commit([&]
    {
        InTransaction = AutoRTFM::IsClosed();

		AutoRTFM::Transact([&]
			{
				AutoRTFM::OnAbort([&]
					{
						InAbort = AutoRTFM::IsClosed();
					});

				AutoRTFM::AbortTransaction();
			});

		AutoRTFM::OnCommit([&]
			{
				InCommit = AutoRTFM::IsClosed();
			});

        AutoRTFM::Open([&]
        {
            InOpenNest = AutoRTFM::IsClosed();

            REQUIRE(AutoRTFM::EContextStatus::OnTrack == AutoRTFM::Close([&]
            {
                InClosedNestInOpenNest = AutoRTFM::IsClosed();
            }));
        });
    });

    REQUIRE(true == InTransaction);
    REQUIRE(false == InOpenNest);
    REQUIRE(true == InClosedNestInOpenNest);
	REQUIRE(false == InAbort);
	REQUIRE(false == InCommit);
}

TEST_CASE("API.IsCommittingOrAborting")
{
	REQUIRE(false == AutoRTFM::IsCommittingOrAborting());

	// Set to the opposite of what we expect at the end of function.
	bool InTransaction = true;
	bool InOpenNest = true;
	bool InClosedNestInOpenNest = true;
	bool InAbort = false;
	bool InCommit = false;

	AutoRTFM::Commit([&]
		{
			InTransaction = AutoRTFM::IsCommittingOrAborting();

			AutoRTFM::Transact([&]
				{
					AutoRTFM::OnAbort([&]
						{
							InAbort = AutoRTFM::IsCommittingOrAborting();
						});

					AutoRTFM::AbortTransaction();
				});

			AutoRTFM::OnCommit([&]
				{
					InCommit = AutoRTFM::IsCommittingOrAborting();
				});

			AutoRTFM::Open([&]
				{
					InOpenNest = AutoRTFM::IsCommittingOrAborting();

					REQUIRE(AutoRTFM::EContextStatus::OnTrack == AutoRTFM::Close([&]
						{
							InClosedNestInOpenNest = AutoRTFM::IsCommittingOrAborting();
						}));
				});
		});

	REQUIRE(false == InTransaction);
	REQUIRE(false == InOpenNest);
	REQUIRE(false == InClosedNestInOpenNest);
	REQUIRE(true == InAbort);
	REQUIRE(true == InCommit);
}

TEST_CASE("API.Transact")
{
    int Answer = 6 * 9;

    REQUIRE(AutoRTFM::ETransactionResult::Committed == AutoRTFM::Transact([&]
    {
        Answer = 42;
    }));

    REQUIRE(42 == Answer);
}

TEST_CASE("API.Commit")
{
    int Answer = 6 * 9;

    AutoRTFM::Commit([&]
    {
        Answer = 42;
    });

    REQUIRE(42 == Answer);
}

TEST_CASE("API.Abort")
{
    bool BeforeNest = false;
    bool InNest = false;
    bool AfterNest = false;
    AutoRTFM::ETransactionResult NestResult;

    AutoRTFM::Commit([&]
    {
        BeforeNest = true;

        NestResult = AutoRTFM::Transact([&]
        {
            // Because we are aborting this won't actually occur!
            InNest = true;

            AutoRTFM::AbortTransaction();
        });

        AfterNest = true;
    });

    REQUIRE(true == BeforeNest);
    REQUIRE(false == InNest);
    REQUIRE(true == AfterNest);
    REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == NestResult);
}

TEST_CASE("API.AbortIfTransactional")
{
    // Calling this outwith any transaction won't abort the program.
    AutoRTFM::AbortIfTransactional();

    bool BeforeNest = false;
    bool InNest = false;
    bool AfterNest = false;
    AutoRTFM::ETransactionResult NestResult;

    AutoRTFM::Commit([&]
    {
        BeforeNest = true;

        NestResult = AutoRTFM::Transact([&]
        {
            // Because we are aborting this won't actually occur!
            InNest = true;

            AutoRTFM::AbortIfTransactional();
        });

        AfterNest = true;
    });

    REQUIRE(true == BeforeNest);
    REQUIRE(false == InNest);
    REQUIRE(true == AfterNest);
    REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == NestResult);
}

TEST_CASE("API.AbortIfClosed")
{
    // Calling this outwith any transaction won't abort the program.
    AutoRTFM::AbortIfClosed();

    bool BeforeNest = false;
    bool InNest = false;
    bool AfterNest = false;

    REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == AutoRTFM::Transact([&]
    {
        BeforeNest = true;

        AutoRTFM::Open([&]
        {
            InNest = true;

            // This won't abort because we aren't closed!
            AutoRTFM::AbortIfClosed();
        });

        AfterNest = true;

        AutoRTFM::AbortIfClosed();
    }));

    REQUIRE(false == BeforeNest);
    REQUIRE(true == InNest);
    REQUIRE(false == AfterNest);
}

TEST_CASE("API.Open")
{
    int Answer = 6 * 9;

    // An open call outside a transaction succeeds.
    AutoRTFM::Open([&]
    {
        Answer = 42;
    });

    REQUIRE(42 == Answer);

    REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == AutoRTFM::Transact([&]
    {
        // An open call inside a transaction succeeds also.
        AutoRTFM::Open([&]
        {
            Answer *= 2;
        });

        AutoRTFM::AbortTransaction();
    }));

    REQUIRE(84 == Answer);
}

TEST_CASE("API.Close")
{
    bool InClosedNest = false;
    bool InOpenNest = false;
    bool InClosedNestInOpenNest = false;

    REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == AutoRTFM::Transact([&]
    {
        // A closed call inside a transaction does not abort.
        REQUIRE(AutoRTFM::EContextStatus::OnTrack == AutoRTFM::Close([&]
        {
            InClosedNest = true;
        }));

        AutoRTFM::Open([&]
        {
            // A closed call inside an open does not abort either.
			REQUIRE(AutoRTFM::EContextStatus::OnTrack == AutoRTFM::Close([&]
            {
                InClosedNestInOpenNest = true;
            }));

            InOpenNest = true;
        });

        AutoRTFM::AbortTransaction();
    }));

    REQUIRE(false == InClosedNest);
    REQUIRE(true == InOpenNest);
    REQUIRE(false == InClosedNestInOpenNest);
}

TEST_CASE("API.RegisterOpenFunction")
{
    AutoRTFM::ForTheRuntime::RegisterOpenFunction(
        reinterpret_cast<void*>(NoAutoRTFM::DoSomethingCpp),
        reinterpret_cast<void*>(NoAutoRTFM::DoSomethingInTransactionCpp));

    int I = -42;

    AutoRTFM::Commit([&]
    {
        I = NoAutoRTFM::DoSomethingCpp(I);
    });

    REQUIRE(0 == I);
}

TEST_CASE("API.OnCommit")
{
    bool OuterTransaction = false;
    bool InnerTransaction = false;
    bool InnerTransactionWithAbort = false;
    bool InnerOpenNest = false;
    AutoRTFM::ETransactionResult NestResult;

    REQUIRE(AutoRTFM::ETransactionResult::Committed == AutoRTFM::Transact([&]
    {
        AutoRTFM::OnCommit([&]
        {
            OuterTransaction = true;
        });

        // This should only be modified on the commit!
        if (OuterTransaction)
        {
            AutoRTFM::AbortTransaction();
        }

        AutoRTFM::Commit([&]
        {
            AutoRTFM::OnCommit([&]
            {
                InnerTransaction = true;
            });
        });

        // This should only be modified on the commit!
        if (InnerTransaction)
        {
			AutoRTFM::AbortTransaction();
        }

        NestResult = AutoRTFM::Transact([&]
        {
            AutoRTFM::OnCommit([&]
            {
                InnerTransactionWithAbort = true;
            });

		AutoRTFM::AbortTransaction();
        });

        // This should never be modified because its transaction aborted!
        if (InnerTransactionWithAbort)
        {
			AutoRTFM::AbortTransaction();
        }

        AutoRTFM::Open([&]
        {
            AutoRTFM::OnCommit([&]
            {
                InnerOpenNest = true;
            });

            // This should be modified immediately!
            if (!InnerOpenNest)
            {
				AutoRTFM::AbortTransaction();
            }
        });
    }));

    REQUIRE(true == OuterTransaction);
    REQUIRE(true == InnerTransaction);
    REQUIRE(false == InnerTransactionWithAbort);
    REQUIRE(true == InnerOpenNest);
    REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == NestResult);
}

TEST_CASE("API.OnAbort")
{
	// Too hard to get this test working when retrying nested transactions so bail!
	if (AutoRTFM::ForTheRuntime::ShouldRetryNestedTransactionsToo())
	{
		return;
	}

    bool OuterTransaction = false;
    bool InnerTransaction = false;
    bool InnerTransactionWithAbort = false;
    bool InnerOpenNest = false;
    AutoRTFM::ETransactionResult NestResult = AutoRTFM::ETransactionResult::Committed;

    REQUIRE(AutoRTFM::ETransactionResult::Committed == AutoRTFM::Transact([&]
    {
		// If we are retrying transactions, need to reset the test state.
		AutoRTFM::OnAbort([&]
		{
			OuterTransaction = false;
			InnerTransaction = false;
			InnerTransactionWithAbort = false;
			InnerOpenNest = false;
			NestResult = AutoRTFM::ETransactionResult::Committed;
		});

        AutoRTFM::OnAbort([&]
        {
            OuterTransaction = true;
        });

        // This should only be modified on the commit!
        if (OuterTransaction)
        {
			AutoRTFM::AbortTransaction();
        }

        AutoRTFM::Commit([&]
        {
            AutoRTFM::OnAbort([&]
            {
                InnerTransaction = true;
            });
        });

        // This should only be modified on the commit!
        if (InnerTransaction)
        {
			AutoRTFM::AbortTransaction();
        }

        NestResult = AutoRTFM::Transact([&]
        {
            AutoRTFM::OnAbort([&]
            {
                InnerTransactionWithAbort = true;
            });

			AutoRTFM::AbortTransaction();
        });

        // Inner OnAbort runs eagerly
        if (!InnerTransactionWithAbort)
        {
			AutoRTFM::AbortTransaction();
        }

        AutoRTFM::Open([&]
        {
            AutoRTFM::OnAbort([&]
            {
                InnerOpenNest = true;
            });
        });

        // This should only be modified on the commit!
        if (InnerOpenNest)
        {
			AutoRTFM::AbortTransaction();
        }
    }));

    REQUIRE(false == OuterTransaction);
    REQUIRE(false == InnerTransaction);
    REQUIRE(true == InnerTransactionWithAbort);
    REQUIRE(false == InnerOpenNest);
    REQUIRE(AutoRTFM::ETransactionResult::AbortedByRequest == NestResult);
}

TEST_CASE("API.DidAllocate")
{
    constexpr unsigned Size = 1024;
    unsigned BumpAllocator[Size];
    unsigned NextBump = 0;

    AutoRTFM::Commit([&]
    {
		// If we are retrying transactions, need to reset the test state.
		AutoRTFM::OnAbort([&]
		{
			NextBump = 0;
		});

        for (unsigned I = 0; I < Size; I++)
        {
            unsigned* Data;
            AutoRTFM::Open([&]
            {
                Data = reinterpret_cast<unsigned*>(AutoRTFM::DidAllocate(
                    &BumpAllocator[NextBump++],
                    sizeof(unsigned)));
            });

            *Data = I;
        }
    });

    for (unsigned I = 0; I < Size; I++)
    {
        REQUIRE(I == BumpAllocator[I]);
    }
}

TEST_CASE("API.CheckConsistencyAssumingNoRaces")
{
    AutoRTFM::Commit([&]
    {
        AutoRTFM::ForTheRuntime::CheckConsistencyAssumingNoRaces();
    });
}

TEST_CASE("API.IsOnCurrentTransactionStack")
{
	{
		int OnStackNotInTransaction = 1;
		REQUIRE(!AutoRTFM::IsOnCurrentTransactionStack(&OnStackNotInTransaction));

		int* OnHeapNotInTransaction = new int{2};
		REQUIRE(!AutoRTFM::IsOnCurrentTransactionStack(OnHeapNotInTransaction));
		delete OnHeapNotInTransaction;
	}

	AutoRTFM::Commit([&]
	{
		int OnStackInTransaction = 3;
		REQUIRE(AutoRTFM::IsOnCurrentTransactionStack(&OnStackInTransaction));

		int* OnHeapInTransaction = new int{4};
		REQUIRE(!AutoRTFM::IsOnCurrentTransactionStack(OnHeapInTransaction));
		delete OnHeapInTransaction;

		AutoRTFM::Commit([&]
		{
			// `OnStackInTransaction` is no longer in the innermost scope.
			REQUIRE(!AutoRTFM::IsOnCurrentTransactionStack(&OnStackInTransaction));

			int OnInnermostStackInTransaction = 5;
			REQUIRE(AutoRTFM::IsOnCurrentTransactionStack(&OnInnermostStackInTransaction));
		});
	});
}
