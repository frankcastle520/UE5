// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundGeneratorModule.h"
#include "MetasoundGeneratorModuleImpl.h"

#include "Containers/Array.h"
#include "HAL/IConsoleManager.h"
#include "Logging/LogMacros.h"
#include "MetasoundOperatorCache.h"
#include "Misc/CString.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "ProfilingDebugging/CsvProfiler.h"

DEFINE_LOG_CATEGORY(LogMetasoundGenerator);

CSV_DEFINE_CATEGORY(MetaSound_ActiveOperators, true);

// Note: disabled by default as it bloats the csvs quite a bit.
static TAutoConsoleVariable<bool> CVarRecordActiveOperatorsToCsv(
	TEXT("au.MetaSound.RecordActiveMetasoundsToCsv"),
	false,
	TEXT("Record the name of each active Metasound when csv profiling is recording.")
);

static FAutoConsoleCommand CommandMetaSoundExperimentalOperatorPoolSetMaxNumOperators(
	TEXT("au.MetaSound.Experimental.OperatorPool.SetMaxNumOperators"),
	TEXT("Set the maximum number of operators in the MetaSound operator cache."),
	FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray< FString >& Args)
	{
		using namespace Metasound;

		if (Args.Num() < 1)
		{
			return;
		}

		const int32 MaxNumOperators = FCString::Atoi(*Args[0]);

		if (MaxNumOperators < 0)
		{
			return;
		}

		FMetasoundGeneratorModule& Module = FModuleManager::GetModuleChecked<FMetasoundGeneratorModule>("MetasoundGenerator");
		TSharedPtr<FOperatorPool> OperatorPool = Module.GetOperatorPool();
		if (OperatorPool.IsValid())
		{
			OperatorPool->SetMaxNumOperators(static_cast<uint32>(MaxNumOperators));
			UE_LOG(LogMetasoundGenerator, Display, TEXT("Metasound operator cache size set to %d operators."), MaxNumOperators);
		}
	})
);

namespace Metasound
{
	static const FString InstanceCounterCategory(TEXT("Metasound/Active_Generators"));

	void FMetasoundGeneratorModule::StartupModule()
	{
		FOperatorPoolSettings OperatorPoolSettings;
		OperatorPoolSettings.MaxNumOperators = 64;

		OperatorPool = MakeShared<FOperatorPool>(OperatorPoolSettings);
		OperatorInstanceCounterManager = MakeShared<FConcurrentInstanceCounterManager>(InstanceCounterCategory);

#if CSV_PROFILER && !UE_BUILD_SHIPPING
		CsvEndFrameDelegateHandle = FCsvProfiler::Get()->OnCSVProfileEndFrame().AddLambda([WeakCounterManager = OperatorInstanceCounterManager.ToWeakPtr()]
		{
			if (!CVarRecordActiveOperatorsToCsv->GetBool())
			{
				return;
			}

			if (TSharedPtr<FConcurrentInstanceCounterManager> CounterManager = WeakCounterManager.Pin())
			{
				CounterManager->VisitStats([](const FName& StatName, int64 Value)
				{
					FCsvProfiler::RecordCustomStat(StatName, CSV_CATEGORY_INDEX(MetaSound_ActiveOperators), (int32)Value, ECsvCustomStatOp::Set);
				});
			}
		});
#endif // #if CSV_PROFILER && !UE_BUILD_SHIPPING
	}

	void FMetasoundGeneratorModule::ShutdownModule()
	{
#if CSV_PROFILER && !UE_BUILD_SHIPPING
		FCsvProfiler::Get()->OnCSVProfileEndFrame().Remove(CsvEndFrameDelegateHandle);
		CsvEndFrameDelegateHandle.Reset();
#endif // #if CSV_PROFILER && !UE_BUILD_SHIPPING

		if (OperatorPool.IsValid())
		{
			TSharedPtr<FOperatorPool> PoolShuttingDown = OperatorPool;
			OperatorPool.Reset();

			// Clear the pool reference and cancel independent of resetting
			// the shared pointer to ensure if any references are held elsewhere,
			// they are properly invalidated.
			PoolShuttingDown->StopAsyncTasks();
		}

		OperatorInstanceCounterManager.Reset();
	}

	TSharedPtr<FOperatorPool> FMetasoundGeneratorModule::GetOperatorPool()
	{
		return OperatorPool;
	}

	TSharedPtr<FConcurrentInstanceCounterManager> FMetasoundGeneratorModule::GetOperatorInstanceCounterManager()
	{
		return OperatorInstanceCounterManager;
	}
}

IMPLEMENT_MODULE(Metasound::FMetasoundGeneratorModule, MetasoundGenerator);

