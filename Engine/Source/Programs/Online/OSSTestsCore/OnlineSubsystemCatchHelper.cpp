// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemCatchHelper.h"

#include "Algo/AllOf.h"
#include "Algo/Sort.h"
#include "Algo/ForEach.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Helpers/Identity/IdentityAutoLoginHelper.h"
#include "Helpers/Identity/IdentityLoginHelper.h"
#include "Helpers/Identity/IdentityLogoutHelper.h"
#include "OnlineSubsystemNames.h"
#include "Misc/CommandLine.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"

#include "Online/CoreOnline.h"

// Make sure there are registered input devices for N users and fire
// OnInputDeviceConnectionChange delegate for interested online service code.
void EnsureLocalUserCount(uint32 NumUsers)
{
	TArray<FPlatformUserId> Users;
	IPlatformInputDeviceMapper::Get().GetAllActiveUsers(Users);

	const uint32 PreviousUserCount = Users.Num();
	const uint32 NewUserCount = NumUsers > PreviousUserCount ? NumUsers - PreviousUserCount : 0;

	for (uint32 Index = 0; Index < NewUserCount; ++Index)
	{
		const uint32 NewUserIndex = PreviousUserCount + Index;
		IPlatformInputDeviceMapper::Get().Internal_MapInputDeviceToUser(
			FInputDeviceId::CreateFromInternalId(NewUserIndex),
			FPlatformMisc::GetPlatformUserForUserIndex(NewUserIndex),
			EInputDeviceConnectionState::Connected);
	}
}

TArray<TFunction<void()>>* GetGlobalInitalizers()
{
	static TArray<TFunction<void()>> gInitalizersToCallInMain;
	return &gInitalizersToCallInMain;
}

TArray<OnlineSubsystemAutoReg::FApplicableServicesConfig> OnlineSubsystemAutoReg::GetApplicableServices()
{
	static TArray<FApplicableServicesConfig> ServicesConfig =
		[]()
		{
			TArray<FApplicableServicesConfig> ServicesConfigInit;
			if (const TCHAR* CmdLine = FCommandLine::Get())	
			{
				FString Values;
				TArray<FString> ServicesTags;
				if (FParse::Value(CmdLine, TEXT("-Services="), Values, false))
				{
					Values.ParseIntoArray(ServicesTags, TEXT(","));
				}

				if (ServicesTags.IsEmpty())
				{
					GConfig->GetArray(TEXT("OnlineServicesTests"), TEXT("DefaultServices"), ServicesTags, GEngineIni);
				}

				for (const FString& ServicesTag : ServicesTags)
				{
					FString ConfigCategory = FString::Printf(TEXT("OnlineServicesTests %s"), *ServicesTag);
					FApplicableServicesConfig Config;
					Config.Tag = ServicesTag;

					FString ServicesType;
					GConfig->GetString(*ConfigCategory, TEXT("ServicesType"), ServicesType, GEngineIni);
					GConfig->GetArray(*ConfigCategory, TEXT("ModulesToLoad"), Config.ModulesToLoad, GEngineIni);

					LexFromString(Config.ServicesType, *ServicesType);
					if (Config.ServicesType != UE::Online::EOnlineServices::None)
					{
						ServicesConfigInit.Add(MoveTemp(Config));
					}
				}
			}

			return ServicesConfigInit;
		}();

		return ServicesConfig;
}

TArray<FString> GetServiceModules()
{
	TArray<FString> Modules;

	for (const OnlineSubsystemAutoReg::FApplicableServicesConfig& Config : OnlineSubsystemAutoReg::GetApplicableServices())
	{
		for (const FString& Module : Config.ModulesToLoad)
		{
			Modules.AddUnique(Module);
		}
	}

	return Modules;
}

void OnlineSubsystemTestBase::LoadServiceModules()
{
	for (const FString& Module : GetServiceModules())
	{
		FModuleManager::LoadModulePtr<IModuleInterface>(*Module);
	}
}

void OnlineSubsystemTestBase::UnloadServiceModules()
{
	const TArray<FString>& Modules = GetServiceModules();
	// Shutdown in reverse order
	for (int Index = Modules.Num() - 1; Index >= 0; --Index)
	{
		if (IModuleInterface* Module = FModuleManager::Get().GetModule(*Modules[Index]))
		{
			Module->ShutdownModule();
		}
	}
}

void OnlineSubsystemTestBase::ConstructInternal(FString SubsystemName)
{
	Subsystem = SubsystemName;
}

OnlineSubsystemTestBase::OnlineSubsystemTestBase()
	: Driver()
	, Pipeline(Driver.MakePipeline())
{
	// handle most cxn in ConstructInternal
}

OnlineSubsystemTestBase::~OnlineSubsystemTestBase()
{

}

FString OnlineSubsystemTestBase::GetSubsystem() const
{
	return Subsystem;
}

TArray<FOnlineAccountCredentials> OnlineSubsystemTestBase::GetIniCredentials(int32 NumUsers) const
{
	FString LoginCredentialCategory = FString::Printf(TEXT("LoginCredentials %s"), *Subsystem);
	TArray<FString> CredentialsArr;
	GConfig->GetArray(*LoginCredentialCategory, TEXT("Credentials"), CredentialsArr, GEngineIni);

	if (NumUsers > CredentialsArr.Num())
	{
		UE_LOG(LogOSSTests, Error, TEXT("Attempted to GetCredentials for more than we have stored! Add more credentials to the DefaultEngine.ini for OssTests"));
		return TArray<FOnlineAccountCredentials>();
	}

	TArray<FOnlineAccountCredentials> OnlineAccountCredentials;
	for (int32 Index = 0; Index < CredentialsArr.Num(); ++Index)
	{
		FString LoginUsername, LoginType, LoginToken;
		FParse::Value(*CredentialsArr[Index], TEXT("Type="), LoginType);
		FParse::Value(*CredentialsArr[Index], TEXT("Id="), LoginUsername);
		FParse::Value(*CredentialsArr[Index], TEXT("Token="), LoginToken);
		INFO(*FString::Printf(TEXT("Logging in with type %s, id %s, password %s"), *LoginType, *LoginUsername, *LoginToken));

		OnlineAccountCredentials.Add(FOnlineAccountCredentials{ LoginType, LoginUsername, LoginToken });
	}
	return OnlineAccountCredentials;
}

TArray<FOnlineAccountCredentials> OnlineSubsystemTestBase::GetCredentials(int32 LocalUserNum, int32 NumUsers) const
{
#if OSSTESTS_USEEXTERNAUTH
	return CustomCredentials(LocalUserNum, NumUsers);
#else // OSSTESTS_USEEXTERNAUTH
	return GetIniCredentials(LocalUserNum);
#endif // OSSTESTS_USEEXTERNAUTH
}

FString OnlineSubsystemTestBase::GetLoginCredentialCategory() const
{
	return FString::Printf(TEXT("LoginCredentials %s"), *Subsystem);
}

FTestPipeline& OnlineSubsystemTestBase::GetLoginPipeline(uint32 NumUsersToLogin) const
{
	REQUIRE(NumLocalUsers == -1); // Don't call GetLoginPipeline more than once per test
	NumLocalUsers = NumUsersToLogin;
	NumUsersToLogout = NumUsersToLogin;

	bool bUseAutoLogin = false;
	bool bUseImplicitLogin = false;
	FString LoginCredentialCategory = FString::Printf(TEXT("LoginCredentials %s"), *Subsystem);
	GConfig->GetBool(*LoginCredentialCategory, TEXT("UseAutoLogin"), bUseAutoLogin, GEngineIni);
	GConfig->GetBool(*LoginCredentialCategory, TEXT("UseImplicitLogin"), bUseImplicitLogin, GEngineIni);

	// Make sure input delegates are fired for adding the required user count.
	EnsureLocalUserCount(NumUsersToLogin);

	if (bUseImplicitLogin)
	{
		// Users are expected to already be valid.
	}
	else if (bUseAutoLogin)
	{
		NumLocalUsers = 1;
		Pipeline.EmplaceStep<FIdentityAutoLoginStep>(0);
	}
	else
	{
		TArray<FOnlineAccountCredentials> AuthLoginParams = GetCredentials(0, NumUsersToLogin);

		for (uint32 Index = 0; Index < NumUsersToLogin; ++Index)
		{
			Pipeline.EmplaceStep<FIdentityLoginStep>(Index, AuthLoginParams[Index]);
		}
	}

	return Pipeline;
}

FTestPipeline& OnlineSubsystemTestBase::GetPipeline() const
{
	return GetLoginPipeline(0);
}

void OnlineSubsystemTestBase::RunToCompletion() const
{
	bool bUseAutoLogin = false;
	bool bUseImplicitLogin = false;
	FString LoginCredentialCategory = FString::Printf(TEXT("LoginCredentials %s"), *Subsystem);
	GConfig->GetBool(*LoginCredentialCategory, TEXT("UseAutoLogin"), bUseAutoLogin, GEngineIni);
	GConfig->GetBool(*LoginCredentialCategory, TEXT("UseImplicitLogin"), bUseImplicitLogin, GEngineIni);

	if (bUseImplicitLogin)
	{
		// Users are expected to already be valid.
	}
	else if (bUseAutoLogin)
	{
		NumLocalUsers = 1;
		Pipeline.EmplaceStep<FIdentityAutoLoginStep>(0);
	}
	else
	{
		for (uint32 i = 0; i < NumLocalUsers; i++)
		{
			Pipeline.EmplaceStep<FIdentityLogoutStep>(i);
		}
	}
	
	FName SubsystemName = FName(GetSubsystem());
	FPipelineTestContext TestContext = FPipelineTestContext(SubsystemName);
	CHECK(Driver.AddPipeline(MoveTemp(Pipeline), TestContext));
	REQUIRE(IOnlineSubsystem::IsEnabled(SubsystemName));
	Driver.RunToCompletion();
}

// TODO: This should poll some info from the tags and generate the list based on that
TArray<FString> OnlineSubsystemAutoReg::GetApplicableSubsystems()
{
	TArray<FString> Subsystems;
	GConfig->GetArray(TEXT("OnlineSubsystemTests"), TEXT("Subsystems"), Subsystems, GEngineIni);
	return Subsystems;
}

bool OnlineSubsystemAutoReg::CheckAllTagsIsIn(const TArray<FString>& TestTags, const TArray<FString>& InputTags)
{
	if (InputTags.Num() == 0)
	{
		return false;
	}

	if (InputTags.Num() > TestTags.Num())
	{
		return false;
	}

	bool bAllInputTagsInTestTags = Algo::AllOf(InputTags, [&TestTags](const FString& CheckTag) -> bool
		{
			auto CheckStringCaseInsenstive = [&CheckTag](const FString& TestString) -> bool
			{
				return TestString.Equals(CheckTag, ESearchCase::IgnoreCase);
			};

			if (TestTags.ContainsByPredicate(CheckStringCaseInsenstive))
			{
				return true;
			}

			return false;
		});

	return bAllInputTagsInTestTags;
}

bool OnlineSubsystemAutoReg::CheckAllTagsIsIn(const TArray<FString>& TestTags, const FString& RawTagString)
{
	TArray<FString> InputTags;
	RawTagString.ParseIntoArray(InputTags, TEXT(","));
	Algo::ForEach(InputTags, [](FString& String)
		{
			String.TrimStartAndEndInline();
			String.RemoveFromStart("[");
			String.RemoveFromEnd("]");
		});
	return CheckAllTagsIsIn(TestTags, InputTags);
}

FString OnlineSubsystemAutoReg::GenerateTags(const FString& ServiceName, const FReportingSkippableTags& SkippableTags, const TCHAR* InTag)
{
	//Copy String here for ease-of-manipulation
	FString RawInTag = InTag;

	TArray<FString> TestTagsArray;
	RawInTag.ParseIntoArray(TestTagsArray, TEXT("]"));
	Algo::ForEach(TestTagsArray, [](FString& String)
		{
			String.TrimStartAndEndInline();
			String.RemoveFromStart("[");
		});
	Algo::Sort(TestTagsArray);

	// Search if we need to append [!mayfail] tag to indicate to 
	// catch2 this test is in a in-development phase and failures 
	// should be ignored.
	for (const FString& FailableTags : SkippableTags.MayFailTags)
	{
		if (CheckAllTagsIsIn(TestTagsArray, FailableTags))
		{
			RawInTag.Append(TEXT("[!mayfail]"));
			break;
		}
	}

	// Search if we need to append [!shouldfail] tag to indicate to 
	// catch2 this test should fail, and if it ever passes we should
	// should fail.
	for (const FString& FailableTags : SkippableTags.ShouldFailTags)
	{
		if (CheckAllTagsIsIn(TestTagsArray, FailableTags))
		{
			RawInTag.Append(TEXT("[!shouldfail]"));
			break;
		}
	}

	return FString::Printf(TEXT("[%s] %s"), *ServiceName, *RawInTag);
}

bool OnlineSubsystemAutoReg::ShouldDisableTest(const FString& ServiceName, const FReportingSkippableTags& SkippableTags, const TCHAR* InTag)
{
	//Copy String here for ease-of-manipulation
	const FString RawInTag = InTag;

	TArray<FString> TestTagsArray;
	RawInTag.ParseIntoArray(TestTagsArray, TEXT("]"));
	Algo::ForEach(TestTagsArray, [](FString& String)
		{
			String.TrimStartAndEndInline();
			String.RemoveFromStart("[");
		});
	Algo::Sort(TestTagsArray);

	// If we contain [!<service>] it means we shouldn't run this
	// test against this service.
	if (RawInTag.Contains("!" + ServiceName))
	{
		return true;
	}

	// If we contain tags from config it means 
	// we shouldn't run this test
	for (const FString& DisableTag : SkippableTags.DisableTestTags)
	{
		if (CheckAllTagsIsIn(TestTagsArray, DisableTag))
		{
			return true;
		}
	}

	// We should run the test!
	return false;
}

// This code is kept identical to Catch internals so that there is as little deviation from OSS_TESTS and Online_OSS_TESTS as possible
OnlineSubsystemAutoReg::OnlineSubsystemAutoReg(OnlineSubsystemTestConstructor TestCtor, Catch::SourceLineInfo LineInfo, const char* Name, const char* Tags, const char* AddlOnlineInfo)
{
	auto GlobalInitalizersPtr = GetGlobalInitalizers();
	ensure(GlobalInitalizersPtr);
	GlobalInitalizersPtr->Add([=, this]() -> void
		{
			for (const FString& Subsystem : GetApplicableSubsystems())
			{
				FString ReportingCategory = FString::Printf(TEXT("TestReporting %s"), *Subsystem);
				FReportingSkippableTags SkippableTags;
				GConfig->GetArray(*ReportingCategory, TEXT("MayFailTestTags"), SkippableTags.MayFailTags, GEngineIni);
				GConfig->GetArray(*ReportingCategory, TEXT("ShouldFailTestTags"), SkippableTags.ShouldFailTags, GEngineIni);
				GConfig->GetArray(*ReportingCategory, TEXT("DisableTestTags"), SkippableTags.DisableTestTags, GEngineIni);

				auto NewName = StringCast<ANSICHAR>(*FString::Printf(TEXT("[%s] %s"), *Subsystem, ANSI_TO_TCHAR(Name)));
				auto NewTags = StringCast<ANSICHAR>(*GenerateTags(Subsystem, SkippableTags, ANSI_TO_TCHAR(Tags)));

				// If we have tags present indicating we should not enable the test at all
				if (ShouldDisableTest(Subsystem, SkippableTags, ANSI_TO_TCHAR(NewTags.Get())))
				{
					continue;
				}

				// TestCtor will create a new instance of the test we are calling- ConstructInternal is separate so that we can pass any arguments we want instead of baking them into the macro
				OnlineSubsystemTestBase* NewTest = TestCtor();
				NewTest->ConstructInternal(Subsystem);

				// This code is lifted from Catch internals to register a test
				Catch::getMutableRegistryHub().registerTest(Catch::makeTestCaseInfo(
					std::string(Catch::StringRef()),  // Used for testing a static method instead of a function- not needed since we're passing an ITestInvoker macro
					Catch::NameAndTags{ NewName.Get(), NewTags.Get() },
					LineInfo),
					Catch::Detail::unique_ptr(NewTest) // This is taking the ITestInvoker macro and will call invoke() to run the test
				);
			}
		});
}