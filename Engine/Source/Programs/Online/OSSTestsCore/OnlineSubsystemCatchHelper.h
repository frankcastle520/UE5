// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TestHarness.h"
#include "TestDriver.h"
#include "CoreMinimal.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"

#include <catch2/catch_test_case_info.hpp>
#include <catch2/interfaces/catch_interfaces_registry_hub.hpp>

class OnlineSubsystemTestBase : public Catch::ITestInvoker
{
public:
	void ConstructInternal(FString ServiceName);
	virtual ~OnlineSubsystemTestBase();

	/* Loads all necessary services for the current test run */
	static void LoadServiceModules();

	/* Unloads all necessary services for the current test run */
	static void UnloadServiceModules();

protected:

	OnlineSubsystemTestBase();
	
	FString GetSubsystem() const;

#if OSSTESTS_USEEXTERNAUTH
	TArray<FOnlineAccountCredentials> CustomCredentials(int32 LocalUserNum, int32 NumUsers) const;
#endif // OSSTESTS_USEEXTERNAUTH

	TArray<FOnlineAccountCredentials> GetIniCredentials(int32 NumUsers) const;

	TArray<FOnlineAccountCredentials> GetCredentials(int32 LocalUserNum, int32 NumUsers) const;

	FTestPipeline& GetLoginPipeline(uint32 NumUsersToLogin = 1) const;
	FTestPipeline& GetPipeline() const;

	/* Returns the ini login category name for the configured service */
	FString GetLoginCredentialCategory() const;

	void RunToCompletion() const;

	/* ITestInvoker */
	virtual void invoke() const override = 0;

private:
	FString Tags;
	FString Subsystem;
	// Catch's ITestInvoker is a const interface but we'll be changing stuff (emplacing steps into the driver, setting flags, etc.) so we make these mutable
	mutable FTestDriver Driver;
	mutable FTestPipeline Pipeline;
	mutable uint32 NumLocalUsers = -1;
	mutable uint32 NumUsersToLogout = -1;
};

typedef OnlineSubsystemTestBase* (*OnlineSubsystemTestConstructor)();

class OnlineSubsystemAutoReg
{
private:
	// TODO: This should poll some info from the tags and generate the list based on that
	TArray<FString> GetApplicableSubsystems();

public:
	struct FReportingSkippableTags
	{
		TArray<FString> MayFailTags;
		TArray<FString> ShouldFailTags;
		TArray<FString> DisableTestTags;
	};
		
	struct FApplicableServicesConfig
	{
		FString Tag;
		UE::Online::EOnlineServices ServicesType;
		TArray<FString> ModulesToLoad;
	};

	static TArray<FApplicableServicesConfig> GetApplicableServices();

	/*
	* Helper function that calls CheckAllTagsIsIn(const TArray<FString>&, const TArray<FString>&);
	*
	* @param  TestTags			The array of test tags we wish to test against.
	* @param  RawTagString		Comma seperated string of elemnets we wish to convert to an array.
	*
	* @return true if all elements of RawTagString prased as an comma sperated array is in TestTags.
	*/
	static bool CheckAllTagsIsIn(const TArray<FString>& TestTags, const FString& RawTagString);


	/**
	 * Checks if every element of InputTags is in TestTags.
	 *
	 * @param  TestTags       The array of test tags we wish to test against.
	 * @param  InputTags	  The array of tags we wish to see if all elements of are present in TestTags.
	 *
	 * @return  true if all elements of InputTags is in TestTags.
	 */
	static bool CheckAllTagsIsIn(const TArray<FString>& TestTags, const TArray<FString>& InputTags);

	static FString GenerateTags(const FString& ServiceName, const FReportingSkippableTags& SkippableTags, const TCHAR* InTag);
	static bool ShouldDisableTest(const FString& ServiceName, const FReportingSkippableTags& SkippableTags, const TCHAR* InTag);

	// This code is kept identical to Catch internals so that there is as little deviation from OSS_TESTS and Online_OSS_TESTS as possible
	OnlineSubsystemAutoReg(OnlineSubsystemTestConstructor TestCtor, Catch::SourceLineInfo LineInfo, const char* Name, const char* Tags, const char* AddlOnlineInfo);
};

TArray<TFunction<void()>>* GetGlobalInitalizers();


// INTERNAL_CATCH_UNIQUE_NAME is what Catch uses to translate the name of the test into a unique identifier without any spaces

#define INTERNAL_ONLINESUBSYSTEM_TEST_CASE_NAMED(RegName, Name, Tags, ...)\
namespace {\
class PREPROCESSOR_JOIN(OnlineSubsystemTest_,RegName) : public OnlineSubsystemTestBase {\
protected:\
	virtual void invoke() const override;\
};\
	OnlineSubsystemTestBase* PREPROCESSOR_JOIN(Construct_,RegName) (){\
		return new PREPROCESSOR_JOIN(OnlineSubsystemTest_,RegName) ();\
	}\
	OnlineSubsystemAutoReg RegName( PREPROCESSOR_JOIN(&Construct_,RegName) , CATCH_INTERNAL_LINEINFO , Name, Tags, "");\
}\
void PREPROCESSOR_JOIN(OnlineSubsystemTest_,RegName)::invoke() const\

#define ONLINESUBSYSTEM_TEST_CASE(Name, Tags, ...) \
	INTERNAL_ONLINESUBSYSTEM_TEST_CASE_NAMED(INTERNAL_CATCH_UNIQUE_NAME(OnlineSubsystemRegistrar), Name, Tags, __VA_ARGS__)

#define REQUIRE_OP(Op)\
	CAPTURE(Op);\
	REQUIRE(Op.WasSuccessful());