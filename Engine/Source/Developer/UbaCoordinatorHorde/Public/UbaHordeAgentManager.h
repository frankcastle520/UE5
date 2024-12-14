// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/AnsiString.h"
#include "Containers/Array.h"
#include "HAL/Thread.h"
#include "UbaBase.h"

class FEvent;
class FUbaHordeMetaClient;
namespace uba { class NetworkServer; }

class FUbaHordeAgentManager
{
public:
	UBACOORDINATORHORDE_API FUbaHordeAgentManager(const FString& InWorkingDir, const FString& BinariesPath);
	UBACOORDINATORHORDE_API ~FUbaHordeAgentManager();

	inline void SetPool(const FString& InPool) { Pool = InPool; }
	inline void SetMaxCoreCount(uint32 Count) { MaxCores = Count; }
	inline void SetUbaHost(const FAnsiString& InHost) { UbaHost = InHost; }
	inline void SetUbaPort(uint32 InPort) { UbaPort = InPort; }

	UBACOORDINATORHORDE_API void SetTargetCoreCount(uint32 Count);

	using AddClientCallback = bool(void* userData, const uba::tchar* ip, uint16 port);
	UBACOORDINATORHORDE_API void SetAddClientCallback(AddClientCallback* callback, void* userData);

	// Returns the number of agents currently handled by this agent manager.
	UBACOORDINATORHORDE_API int32 GetAgentCount() const;

	// Returns the active number of cores allocated across all agents.
	UBACOORDINATORHORDE_API uint32 GetActiveCoreCount() const;

private:
	struct FHordeAgentWrapper
	{
		FThread Thread;
		FEvent* ShouldExit;
	};

	void RequestAgent();
	void ThreadAgent(FHordeAgentWrapper& Wrapper);

	FString WorkingDir;
	FString BinariesPath;

	FString Pool;
	FAnsiString UbaHost;
	uint32 UbaPort = 7001;
	uint32 MaxCores = 500; // Assume 500 by default in case this configuration is missing but a UBA pool was found

	TUniquePtr<FUbaHordeMetaClient> HordeMetaClient;

	FCriticalSection BundleRefPathsLock;
	TArray<FString> BundleRefPaths;

	mutable FCriticalSection AgentsLock;
	TArray<TUniquePtr<FHordeAgentWrapper>> Agents;

	TAtomic<uint64> LastRequestFailTime;
	TAtomic<uint32> TargetCoreCount;
	TAtomic<uint32> EstimatedCoreCount;
	TAtomic<uint32> ActiveCoreCount;
	TAtomic<bool> bAskForAgents;

	AddClientCallback* m_callback = nullptr;
	void* m_userData = nullptr;

	FUbaHordeAgentManager(const FUbaHordeAgentManager&) = delete;
	void operator=(const FUbaHordeAgentManager&) = delete;
};
