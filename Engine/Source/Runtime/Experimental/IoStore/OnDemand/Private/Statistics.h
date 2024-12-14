// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/LowLevelMemTracker.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"

#if COUNTERSTRACE_ENABLED || CSV_PROFILER_STATS
#	define IAS_WITH_STATISTICS 1
#else
#	define IAS_WITH_STATISTICS 0
#endif

struct FAnalyticsEventAttribute;

LLM_DECLARE_TAG(Ias);

namespace UE::IoStore
{

#if IAS_WITH_STATISTICS
#	define IAS_STATISTICS_IMPL(...) ;
#else
#	define IAS_STATISTICS_IMPL(...) { return __VA_ARGS__; }
#endif

enum class EStatsFlags
{
	None				= 0,
	CachingDisabled		= 1 << 0
};

ENUM_CLASS_FLAGS(EStatsFlags);

class FOnDemandIoBackendStats
{
public:
	static FOnDemandIoBackendStats* Get() IAS_STATISTICS_IMPL(nullptr)

	FOnDemandIoBackendStats(EStatsFlags Flags) IAS_STATISTICS_IMPL()
	~FOnDemandIoBackendStats() IAS_STATISTICS_IMPL()

	/** Report analytics not directly associated with a specific endpoint */
	void ReportGeneralAnalytics(TArray<FAnalyticsEventAttribute>& OutAnalyticsArray) const IAS_STATISTICS_IMPL()
	/** Report analytics for the current endpoint */
	void ReportEndPointAnalytics(TArray<FAnalyticsEventAttribute>& OutAnalyticsArray) const IAS_STATISTICS_IMPL()

	void OnIoRequestEnqueue() IAS_STATISTICS_IMPL()
	void OnIoRequestComplete(uint64 Size, uint64 DurationMs) IAS_STATISTICS_IMPL()
	void OnIoRequestCancel() IAS_STATISTICS_IMPL()
	void OnIoRequestError() IAS_STATISTICS_IMPL()

	void OnIoDecodeError() IAS_STATISTICS_IMPL()

	void OnCacheError() IAS_STATISTICS_IMPL()
	void OnCacheGet(uint64 DataSize) IAS_STATISTICS_IMPL()
	void OnCachePut() IAS_STATISTICS_IMPL()
	void OnCachePutExisting(uint64 DataSize) IAS_STATISTICS_IMPL()
	void OnCachePutReject(uint64 DataSize) IAS_STATISTICS_IMPL()
	void OnCachePendingBytes(uint64 TotalSize) IAS_STATISTICS_IMPL()
	void OnCachePersistedBytes(uint64 TotalSize) IAS_STATISTICS_IMPL()
	void OnCacheWriteBytes(uint64 WriteSize) IAS_STATISTICS_IMPL()
	void OnCacheSetMaxBytes(uint64 TotalSize) IAS_STATISTICS_IMPL()

	void OnHttpConnected() IAS_STATISTICS_IMPL()
	void OnHttpDisconnected() IAS_STATISTICS_IMPL()

	void OnHttpEnqueue() IAS_STATISTICS_IMPL()
	void OnHttpCancel() IAS_STATISTICS_IMPL()
	void OnHttpDequeue() IAS_STATISTICS_IMPL()
	void OnHttpGet(uint64 SizeBytes, uint64 DurationMs) IAS_STATISTICS_IMPL()
	void OnHttpRetry() IAS_STATISTICS_IMPL()
	void OnHttpError() IAS_STATISTICS_IMPL()

private:

#if IAS_WITH_STATISTICS
	EStatsFlags Flags;
#endif //IAS_WITH_STATISTICS
};

#undef IAS_STATISTICS_IMPL

} // namespace UE::IoStore
