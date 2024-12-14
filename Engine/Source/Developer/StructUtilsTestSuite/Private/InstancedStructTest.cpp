// Copyright Epic Games, Inc. All Rights Reserved.

#include "AITestsCommon.h"
#include "StructUtils/SharedStruct.h"
#include "StructUtilsTestTypes.h"

#define LOCTEXT_NAMESPACE "StructUtilsTests"

UE_DISABLE_OPTIMIZATION_SHIP

namespace FInstancedStructTest
{
	struct FTest_InstancedStructCreate : FAITestBase
	{
		virtual bool InstantTest() override
		{
			constexpr float Val = 99.f;
			
			{
				FInstancedStruct InstancedStruct = FInstancedStruct::Make<FTestStructSimpleNonZeroDefault>();

				AITEST_EQUAL("FInstancedStruct default initialized from Make should have same value as default constructed", FTestStructSimpleNonZeroDefault(), InstancedStruct.Get<FTestStructSimpleNonZeroDefault>());
			}

			{
				FTestStructSimple Simple(Val);

				FInstancedStruct InstancedStruct = FInstancedStruct::Make(Simple);

				AITEST_EQUAL("FInstancedStruct initialized from Make should have value of FTestStructSimple its initiliazed from", Val, InstancedStruct.Get<FTestStructSimple>().Float);
			}

			{
				FTestStructSimple Simple(Val);
				FStructView StructView = FStructView::Make(Simple);
				FInstancedStruct InstancedStruct(StructView);

				AITEST_EQUAL("FInstancedStruct initialized from Make should have value of FStructView its initiliazed from", Val, InstancedStruct.Get<FTestStructSimple>().Float);
			}
			
			{
				FTestStructSimple Simple(Val);
				TConstStructView<FTestStructSimple> ConstStructView(Simple);
				TInstancedStruct<FTestStructSimple> InstancedStruct(ConstStructView);

				AITEST_EQUAL("TInstancedStruct initialized from Make should have value of TConstStructView its initiliazed from", Val, InstancedStruct.Get<FTestStructSimple>().Float);
			}

			{
				FTestStructSimple Simple(Val);

				FInstancedStruct InstancedStruct = FInstancedStruct::Make<FTestStructSimple>(Val);

				AITEST_EQUAL("FInstancedStruct initialized from Make should have value reflecting TArgs", Val, InstancedStruct.Get<FTestStructSimple>().Float);
			}

			{
				FTestStructSimple Simple(Val);

				FInstancedStruct InstancedStruct;
				InstancedStruct.InitializeAs<FTestStructSimple>(Val);

				AITEST_EQUAL("FInstancedStruct initialized from Make should have value reflecting TArgs", Val, InstancedStruct.Get<FTestStructSimple>().Float);
			}

			{
				FTestStructSimple Simple(Val);

				FInstancedStruct InstancedStruct;
				InstancedStruct.InitializeAs<FTestStructSimple>(Val);
				AITEST_EQUAL("FInstancedStruct initialized from InitializeAs should have value reflecting TArgs", Val, InstancedStruct.Get<FTestStructSimple>().Float);

				InstancedStruct.InitializeAs<FTestStructSimpleNonZeroDefault>();
				AITEST_EQUAL("FInstancedStruct initialized from InitializeAs should have same value as default constructed", FTestStructSimpleNonZeroDefault(), InstancedStruct.Get<FTestStructSimpleNonZeroDefault>());

				InstancedStruct.InitializeAs(nullptr);
				AITEST_FALSE("FInstancedStruct initialized from InitializeAs with empty struct should not be valid", InstancedStruct.IsValid());
			}

			return true;
		}
	};

	IMPLEMENT_AI_INSTANT_TEST(FTest_InstancedStructCreate, "System.StructUtils.InstancedStruct.Make");

	struct FTest_InstancedStructBasic : FAITestBase
	{
		virtual bool InstantTest() override
		{
			{
				FInstancedStruct InstancedStruct = FInstancedStruct::Make<FTestStructSimple>();
				FInstancedStruct InstancedStruct2(InstancedStruct);

				AITEST_EQUAL("InstancedStruct and InstancedStruct2 should be equal from copy construction", InstancedStruct, InstancedStruct2);
			}

			{
				FInstancedStruct InstancedStruct = FInstancedStruct::Make<FTestStructSimple>();
				FInstancedStruct InstancedStruct2;

				InstancedStruct2 = InstancedStruct;

				AITEST_EQUAL("FInstancedStruct and FInstancedStruct should be equal from copy assignment", InstancedStruct, InstancedStruct2);
			}

			{
				FInstancedStruct InstancedStruct;
				AITEST_FALSE("Default constructed FInstancedStruct should IsValid() == false", InstancedStruct.IsValid());
			}

			{
				FInstancedStruct InstancedStruct = FInstancedStruct::Make<FTestStructSimple>();
				AITEST_TRUE("FInstancedStruct created to a specific struct type should be IsValid()", InstancedStruct.IsValid());
			}

			return true;
		}
	};
	IMPLEMENT_AI_INSTANT_TEST(FTest_InstancedStructBasic, "System.StructUtils.InstancedStruct.Basic");

	struct FTest_InstancedStructCustomScriptStruct : FAITestBase
	{
		virtual bool InstantTest() override
		{
			// Create the TestObject before UScriptStruct, so that CustomStruct gets destroyed first.
			TWeakObjectPtr<UTestObjectWithInstanceStruct> TestObject = NewObject<UTestObjectWithInstanceStruct>();
			check(TestObject.IsValid());

			TWeakObjectPtr<UScriptStruct> CustomStruct = NewObject<UScriptStruct>();
			check(CustomStruct.IsValid());

			FIntProperty* IntProp = new FIntProperty(CustomStruct.Get(), FName("Int"), RF_Public);
			check(IntProp);
			CustomStruct->AddCppProperty(IntProp);

			FStrProperty* StrProp = new FStrProperty(CustomStruct.Get(), FName("String"), RF_Public);
			check(StrProp);
			CustomStruct->AddCppProperty(StrProp);

			CustomStruct->SetSuperStruct(nullptr);
			CustomStruct->Bind();
			CustomStruct->StaticLink(/*RelinkExistingProperties*/true);

			
			TestObject->Value.InitializeAs(CustomStruct.Get());
			AITEST_TRUE("FInstancedStruct created to a specific struct type should be IsValid()", TestObject->Value.IsValid());

			// CustomStruct and TestObject should both get collected.
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

			AITEST_FALSE("CustomStruct should not be valid", CustomStruct.IsValid());
			AITEST_FALSE("TestObject should not be valid", TestObject.IsValid());
			
			return true;
		}
	};
	IMPLEMENT_AI_INSTANT_TEST(FTest_InstancedStructCustomScriptStruct, "System.StructUtils.InstancedStruct.CustomScriptStruct");

}


UE_ENABLE_OPTIMIZATION_SHIP

#undef LOCTEXT_NAMESPACE
