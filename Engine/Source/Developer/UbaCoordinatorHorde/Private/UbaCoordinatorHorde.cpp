// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

#if defined(UBA_COORDINATOR_HORDE_DLL)

#include "DesktopPlatformModule.h"
#include "HAL/Platform.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/OutputDeviceConsole.h"
#include "Misc/OutputDeviceRedirector.h"
#include "SocketSubsystem.h"
#include "UbaCoordinator.h"
#include "UbaHordeAgentManager.h"

#if PLATFORM_WINDOWS
#define UBA_EXPORT __declspec(dllexport) 
#else
#define UBA_EXPORT __attribute__ ((visibility("default")))
#endif

TCHAR GInternalProjectName[64] = TEXT( "UbaCoordinatorHorde" );
const TCHAR *GForeignEngineDir = nullptr;

class CoordinatorHorde : public uba::Coordinator
{
public:
	CoordinatorHorde(const FString& workDir, const FString& binariesDir) : m_manager(workDir, binariesDir)
	{
	}

	virtual ~CoordinatorHorde()
	{
	}

	virtual void SetAddClientCallback(AddClientCallback callback, void* userData) override
	{
		m_manager.SetAddClientCallback(callback, userData);
	}

	virtual void SetTargetCoreCount(uba::u32 count) override
	{
		m_manager.SetTargetCoreCount(count);
	}

	FUbaHordeAgentManager m_manager;
};


extern "C"
{
	UBA_EXPORT uba::Coordinator* UbaCreateCoordinator(const uba::CoordinatorCreateInfo& info)
	{
		FCommandLine::Set(TEXT(""));
		GWarn = FPlatformApplicationMisc::GetFeedbackContext();

		if (info.logging)
		{
			FConfigCacheIni::InitializeConfigSystem();
			static auto c = TUniquePtr<FOutputDeviceConsole>(FPlatformApplicationMisc::CreateConsoleOutputDevice());
			GLogConsole = c.Get();
			GLogConsole->Show(true);
			GLog->SetCurrentThreadAsPrimaryThread();
			GLog->TryStartDedicatedPrimaryThread();
			GLog->AddOutputDevice(GLogConsole);
		}

		GGameThreadId = FPlatformTLS::GetCurrentThreadId();
		GIsGameThreadIdInitialized = true;

		// Since we are not setting CWD we need to manually call these systems from this thread (if not called from game thread LoadModule uses CWD which is not set)
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		FHttpModule::Get();
		FDesktopPlatformModule::TryGet();

		auto coordinator = new CoordinatorHorde(info.workDir, info.binariesDir);
		auto& m = coordinator->m_manager;
		m.SetPool(info.pool);
		m.SetMaxCoreCount(info.maxCoreCount);

		return coordinator;
	}

	UBA_EXPORT void UbaDestroyCoordinator(uba::Coordinator* coordinator)
	{
		delete (CoordinatorHorde*)coordinator;
		if (GLog)
			GLog->SetCurrentThreadAsPrimaryThread();
		FTextKey::TearDown(); // This is for clean shutdown with tsan
		FHttpModule::Get().GetHttpManager().Shutdown();
	}
}

#else

class FUbaCoordinatorHordeModule : public IModuleInterface
{
	UBACOORDINATORHORDE_API FUbaCoordinatorHordeModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FUbaCoordinatorHordeModule>(TEXT("UbaCoordinatorHorde"));
	}
};

IMPLEMENT_MODULE(FUbaCoordinatorHordeModule, UbaCoordinatorHorde);

#endif