// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeModule.h"
#include "Debugger/StateTreeTraceModule.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

namespace TraceServices { class IAnalysisService; }
namespace TraceServices { class IModuleService; }
namespace UE::Trace { class FStoreClient; }
class UUserDefinedStruct;

class FStateTreeModule : public IStateTreeModule
{
public:
	FStateTreeModule();

	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

	virtual bool StartTraces(int32& OutTraceId) override;
	virtual bool IsTracing() const override;
	virtual void StopTraces() override;

#if WITH_EDITOR
	using FReplacementObjectMap = FCoreUObjectDelegates::FReplacementObjectMap;
	DECLARE_TS_MULTICAST_DELEGATE_OneParam(FOnObjectsReinstanced, const FReplacementObjectMap&);
	static FOnObjectsReinstanced OnObjectsReinstanced;

	DECLARE_TS_MULTICAST_DELEGATE_OneParam(FOnPIEEvent, bool);
	static FOnPIEEvent OnPreBeginPIE;

	DECLARE_TS_MULTICAST_DELEGATE_OneParam(FOnUserDefinedStructReinstanced, const UUserDefinedStruct& /*UserDefinedStruct*/);
	static FOnUserDefinedStructReinstanced OnUserDefinedStructReinstanced;
#endif

private:
#if WITH_EDITOR
	void HandleObjectsReinstanced(const FReplacementObjectMap& ObjectMap);
	void HandlePreBeginPIE(const bool bIsSimulating);
	void HandleUserDefinedStructReinstanced(const UUserDefinedStruct& UserDefinedStruct);
#endif

#if WITH_STATETREE_TRACE_DEBUGGER
	/**
	 * Gets the store client.
	 */
	virtual UE::Trace::FStoreClient* GetStoreClient() override;

	TSharedPtr<TraceServices::IAnalysisService> TraceAnalysisService;
	TSharedPtr<TraceServices::IModuleService> TraceModuleService;

	/** The client used to connect to the trace store. */
	TUniquePtr<UE::Trace::FStoreClient> StoreClient;

	FStateTreeTraceModule StateTreeTraceModule;
#endif // WITH_STATETREE_TRACE_DEBUGGER

#if WITH_STATETREE_TRACE
	TArray<const FString> ChannelsToRestore;

	/** Keep track if StartTraces was explicitly called. */
	bool bIsTracing = false;

	FAutoConsoleCommand StartDebuggerTracesCommand;

	FAutoConsoleCommand StopDebuggerTracesCommand;
#endif // WITH_STATETREE_TRACE

#if WITH_EDITOR
	FDelegateHandle OnObjectsReinstancedHandle;
	FDelegateHandle OnUserDefinedStructReinstancedHandle;
	FDelegateHandle OnPreBeginPIEHandle;
#endif 
};
