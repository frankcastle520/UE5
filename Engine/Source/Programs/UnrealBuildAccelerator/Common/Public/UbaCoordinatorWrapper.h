// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaCoordinator.h"
#include "UbaFile.h"
#include "UbaNetworkServer.h"
#include "UbaScheduler.h"
#include "UbaStringBuffer.h"

#if !PLATFORM_WINDOWS
#include <dlfcn.h>
#define GetProcAddress dlsym
#define LoadLibrary(name) dlopen(name, RTLD_LAZY);
#define LoadLibraryError dlerror()
#define HMODULE void*
#else
#define LoadLibraryError LastErrorToText().data
#endif

namespace uba
{
	class CoordinatorWrapper
	{
	public:
		bool Create(Logger& logger, const tchar* coordinatorType, const CoordinatorCreateInfo& info, NetworkBackend& networkBackend, NetworkServer& networkServer, Scheduler* scheduler = nullptr)
		{
			UbaCreateCoordinatorFunc* createCoordinator = nullptr;

			if (!*coordinatorType)
				return false;

			StringBuffer<128> coordinatorBin(info.binariesDir);
			coordinatorBin.EnsureEndsWithSlash();

			#if PLATFORM_WINDOWS
			coordinatorBin.Append(TC("UbaCoordinator")).Append(coordinatorType).Append(TC(".dll"));
			#else
			coordinatorBin.Append(TC("libUbaCoordinator")).Append(coordinatorType).Append(TC(".so"));
			#endif

			HMODULE coordinatorModule = LoadLibrary(coordinatorBin.data);
			if (!coordinatorModule)
				return logger.Error(TC("Failed to load coordinator binary %s (%s)"), coordinatorBin.data, LoadLibraryError);

			if (!coordinatorModule)
				return logger.Error(TC("Failed to load coordinator binary %s (%s)"), coordinatorBin.data, LastErrorToText().data);
			createCoordinator = (UbaCreateCoordinatorFunc*)(void*)GetProcAddress(coordinatorModule,"UbaCreateCoordinator");
			if (!createCoordinator)
				return logger.Error(TC("Failed to find UbaCreateCoordinator function inside %s (%s)"), coordinatorBin.data, LastErrorToText().data);
			m_destroyCoordinator = (UbaDestroyCoordinatorFunc*)(void*)GetProcAddress(coordinatorModule, "UbaDestroyCoordinator");
			if (!m_destroyCoordinator)
				return logger.Error(TC("Failed to find UbaDestroyCoordinator function inside %s (%s)"), coordinatorBin.data, LastErrorToText().data);

			m_coordinator = createCoordinator(info);
			if (!m_coordinator)
				return false;

			m_loopCoordinator.Create(true);
			m_networkBackend = &networkBackend;
			m_networkServer = &networkServer;
			m_scheduler = scheduler;

			m_coordinatorThread.Start([this, mcc = info.maxCoreCount]() { ThreadUpdate(mcc); return 0; });

			return true;
		}

		void ThreadUpdate(u32 maxCoreCount)
		{
			m_coordinator->SetAddClientCallback([](void* userData, const tchar* ip, u16 port)
				{
					auto& cw = *(CoordinatorWrapper*)userData;
					return cw.m_networkServer->AddClient(*cw.m_networkBackend, ip, port);
				}, this);

			do
			{
				u32 coreCount = maxCoreCount;
				if (m_scheduler)
					coreCount = Min(m_scheduler->GetProcessCountThatCanRunRemotelyNow(), maxCoreCount);

				m_coordinator->SetTargetCoreCount(coreCount);
			}
			while (!m_loopCoordinator.IsSet(3000));
		}

		void Destroy()
		{
			if (!m_coordinator)
				return;
			m_loopCoordinator.Set();
			m_coordinatorThread.Wait();
			m_destroyCoordinator(m_coordinator);
			m_coordinator = nullptr;
		}

		Coordinator* m_coordinator = nullptr;
		NetworkBackend* m_networkBackend = nullptr;
		NetworkServer* m_networkServer = nullptr;
		Scheduler* m_scheduler = nullptr;
		UbaDestroyCoordinatorFunc* m_destroyCoordinator = nullptr;
		Event m_loopCoordinator;
		Thread m_coordinatorThread;
	};
}