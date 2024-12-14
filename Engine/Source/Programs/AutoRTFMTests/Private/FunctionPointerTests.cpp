// Copyright Epic Games, Inc. All Rights Reserved.

#include "Catch2Includes.h"
#include "Templates/Function.h"

#include <AutoRTFM/AutoRTFM.h>
#include <cmath>

namespace
{
	int TestCFunction()
	{
		if (AutoRTFM::IsClosed())
		{
			return 42;
		}
		else
		{
			AutoRTFM::AbortTransaction();
		}

		return 43;
	}

	using CStyleType = int (*)();
	using CosfType = float (*)(float);

	UE_DISABLE_OPTIMIZATION_SHIP
	CStyleType GetTestCFunction()
	{
		return &TestCFunction;
	}

	CosfType GetCosfFunction()
	{
		return &std::cosf;
	}
	UE_ENABLE_OPTIMIZATION_SHIP
}

TEST_CASE("FunctionPointer.CStyle")
{
	int Result = 0;
	AutoRTFM::Commit([&]
		{
			CStyleType CStyle = GetTestCFunction();
			Result = CStyle();
		});

	REQUIRE(42 == Result);
}

TEST_CASE("FunctionPointer.StandardLibrary")
{
	SECTION("Created inside transaction")
	{
		float Result = 0;
		AutoRTFM::Commit([&]
			{
				CosfType fn = GetCosfFunction();
				Result = fn(0.0f);
			});

		REQUIRE(Result == 1.0f);
	}

	SECTION("Created outside transaction")
	{
		int Result = 0;
		CosfType fn = GetCosfFunction();

		AutoRTFM::Commit([&]
			{
				Result = fn(0.0f);
			});

		REQUIRE(Result == 1.0f);
	}
}

TEST_CASE("FunctionPointer.TFunction")
{
	SECTION("Created inside transaction")
	{
		int Result = 0;
		AutoRTFM::Commit([&]
			{
				TFunction<void()> MyFunc = [&Result]() -> void
					{
						Result = 42;
					};

				if (MyFunc)
				{
					MyFunc();
				}

				MyFunc.CheckCallable();

				MyFunc.Reset();
			});

		REQUIRE(42 == Result);
	}

	SECTION("Created outside transaction")
	{
		int Result = 0;
		TFunction<void()> MyFunc = [&Result]() -> void
			{
				Result = 42;
			};

		AutoRTFM::Commit([&]
			{
				if (MyFunc)
				{
					MyFunc();
				}

				MyFunc.CheckCallable();

				MyFunc.Reset();
			});

		REQUIRE(42 == Result);
	}
}

TEST_CASE("FunctionPointer.TUniqueFunction")
{
	SECTION("Created inside transaction")
	{
		int Result = 0;
		AutoRTFM::Commit([&]
			{
				TUniqueFunction<void()> MyFunc = [&Result]() -> void
					{
						Result = 42;
					};

				if (MyFunc)
				{
					MyFunc();
				}

				MyFunc.CheckCallable();

				MyFunc.Reset();
			});

		REQUIRE(42 == Result);
	}

	SECTION("Created outside transaction")
	{
		int Result = 0;
		TUniqueFunction<void()> MyFunc = [&Result]() -> void
			{
				Result = 42;
			};

		AutoRTFM::Commit([&]
			{
				if (MyFunc)
				{
					MyFunc();
				}

				MyFunc.CheckCallable();

				MyFunc.Reset();
			});

		REQUIRE(42 == Result);
	}
}
