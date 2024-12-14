// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimingExporter.h"

#include "HAL/PlatformFileManager.h"
#include "Logging/MessageLog.h"

// TraceServices
#include "Common/ProviderLock.h"
#include "TraceServices/Model/Bookmarks.h"
#include "TraceServices/Model/Counters.h"
#include "TraceServices/Model/Frames.h"
#include "TraceServices/Model/Regions.h"
#include "TraceServices/Model/Threads.h"
#include "TraceServices/Model/TimingProfiler.h"

// TraceInsightsCore
#include "InsightsCore/Common/Stopwatch.h"

// TraceInsights
#include "Insights/Log.h"
#include "Insights/TimingProfiler/TimingProfilerManager.h"
#include "Insights/TimingProfiler/Tracks/ThreadTimingTrack.h"

#define LOCTEXT_NAMESPACE "UE::Insights::TimingProfiler::FTimingExporter"

namespace UE::Insights::TimingProfiler
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTimingExporter
////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingExporter::FTimingExporter(const TraceServices::IAnalysisSession& InSession)
	: Session(InSession)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingExporter::~FTimingExporter()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

IFileHandle* FTimingExporter::OpenExportFile(const TCHAR* InFilename) const
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	FString Path = FPaths::GetPath(InFilename);
	if (!PlatformFile.DirectoryExists(*Path))
	{
		PlatformFile.CreateDirectoryTree(*Path);
	}

	IFileHandle* ExportFileHandle = PlatformFile.OpenWrite(InFilename);

	if (ExportFileHandle == nullptr)
	{
		Error(LOCTEXT("FailedToOpenFile", "Export failed. Failed to open file for write."));
		return nullptr;
	}

	return ExportFileHandle;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingExporter::Error(const FText& InMessage) const
{
	FName LogListingName = FTimingProfilerManager::Get()->GetLogListingName();
	FMessageLog ReportMessageLog((LogListingName != NAME_None) ? LogListingName : TEXT("Other"));
	ReportMessageLog.Error(InMessage);
	ReportMessageLog.Notify();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingExporter::FUtf8Writer::AppendString(const TCHAR* Str)
{
	if (Str == nullptr || Str[0] == TCHAR('\0'))
	{
		// nothing to append
	}
	else if (FCString::Strchr(Str, TCHAR(Separator)) != nullptr)
	{
		if (FCString::Strchr(Str, TEXT('\"')) != nullptr)
		{
			FString String = Str;
			String.ReplaceInline(TEXT("\""), TEXT("\"\""));
			String = FString::Printf(TEXT("\"%s\""), *String);
			StringBuilder.Append(TCHAR_TO_UTF8(*String));
		}
		else
		{
			FString String = FString::Printf(TEXT("\"%s\""), Str);
			StringBuilder.Append(TCHAR_TO_UTF8(*String));
		}
	}
	else
	{
		StringBuilder.Append(TCHAR_TO_UTF8(Str));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingExporter::FUtf8Writer::WriteStringBuilder(int32 CacheLen)
{
	if (StringBuilder.Len() > CacheLen)
	{
		FileHandle->Write((const uint8*)StringBuilder.ToString(), StringBuilder.Len() * sizeof(UTF8CHAR));
		StringBuilder.Reset();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FTimingExporter::ExportThreadsAsText(const FString& Filename, FExportThreadsParams& Params) const
{
	if (Params.Columns != nullptr)
	{
		UE_LOG(TraceInsights, Error, TEXT("Custom list of columns is not yet supported!"));
		return -1;
	}

	FStopwatch Stopwatch;
	Stopwatch.Start();

	IFileHandle* ExportFileHandle = OpenExportFile(*Filename);
	if (!ExportFileHandle)
	{
		return -1;
	}
	bool bIsCSV = Filename.EndsWith(TEXT(".csv"));
	FUtf8Writer Writer(ExportFileHandle, bIsCSV);
	FUtf8StringBuilder& StringBuilder = Writer.GetStringBuilder();

	// Write header.
	{
		StringBuilder.Append(UTF8TEXT("Id"));
		Writer.AppendSeparator();
		StringBuilder.Append(UTF8TEXT("Name"));
		Writer.AppendSeparator();
		StringBuilder.Append(UTF8TEXT("Group"));
		Writer.AppendLineEnd();
	}

	int32 ThreadCount = 0;

	// Write values.
	{
		StringBuilder.Appendf(UTF8TEXT("%u"), FGpuTimingTrack::Gpu1ThreadId);
		Writer.AppendSeparator();
		StringBuilder.Append(UTF8TEXT("GPU1"));
		Writer.AppendSeparator();
		StringBuilder.Append(UTF8TEXT("GPU"));
		Writer.AppendLineEnd();
		++ThreadCount;

		StringBuilder.Appendf(UTF8TEXT("%u"), FGpuTimingTrack::Gpu2ThreadId);
		Writer.AppendSeparator();
		StringBuilder.Append(UTF8TEXT("GPU2"));
		Writer.AppendSeparator();
		StringBuilder.Append(UTF8TEXT("GPU"));
		Writer.AppendLineEnd();
		++ThreadCount;

		// Iterate the CPU threads.
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(Session);

			const TraceServices::IThreadProvider& ThreadProvider = TraceServices::ReadThreadProvider(Session);

			ThreadProvider.EnumerateThreads(
				[&](const TraceServices::FThreadInfo& ThreadInfo)
				{
					StringBuilder.Appendf(UTF8TEXT("%u"), ThreadInfo.Id);
					Writer.AppendSeparator();
					Writer.AppendString(ThreadInfo.Name);
					Writer.AppendSeparator();
					Writer.AppendString(ThreadInfo.GroupName);
					Writer.AppendLineEnd();
					++ThreadCount;
				});
		}
	}

	Writer.Flush();
	ExportFileHandle->Flush();
	delete ExportFileHandle;
	ExportFileHandle = nullptr;

	Stopwatch.Stop();
	const double TotalTime = Stopwatch.GetAccumulatedTime();
	UE_LOG(TraceInsights, Log, TEXT("Exported %d threads to file in %.3fs (\"%s\")."), ThreadCount, TotalTime, *Filename);

	return ThreadCount;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FTimingExporter::ExportTimersAsText(const FString& Filename, FExportTimersParams& Params) const
{
	if (Params.Columns != nullptr)
	{
		UE_LOG(TraceInsights, Error, TEXT("Custom list of columns is not yet supported!"));
		return -1;
	}

	FStopwatch Stopwatch;
	Stopwatch.Start();

	IFileHandle* ExportFileHandle = OpenExportFile(*Filename);
	if (!ExportFileHandle)
	{
		return -1;
	}
	bool bIsCSV = Filename.EndsWith(TEXT(".csv"));
	FUtf8Writer Writer(ExportFileHandle, bIsCSV);
	FUtf8StringBuilder& StringBuilder = Writer.GetStringBuilder();

	// Write header.
	{
		StringBuilder.Append(UTF8TEXT("Id"));
		Writer.AppendSeparator();
		StringBuilder.Append(UTF8TEXT("Type"));
		Writer.AppendSeparator();
		StringBuilder.Append(UTF8TEXT("Name"));
		Writer.AppendSeparator();
		StringBuilder.Append(UTF8TEXT("File"));
		Writer.AppendSeparator();
		StringBuilder.Append(UTF8TEXT("Line"));
		Writer.AppendLineEnd();
	}

	uint32 TimerCount = 0;

	// Write values.
	if (TraceServices::ReadTimingProfilerProvider(Session))
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(Session);

		const TraceServices::ITimingProfilerProvider& TimingProfilerProvider = *TraceServices::ReadTimingProfilerProvider(Session);

		const TraceServices::ITimingProfilerTimerReader* TimerReader;
		TimingProfilerProvider.ReadTimers([&TimerReader](const TraceServices::ITimingProfilerTimerReader& Out) { TimerReader = &Out; });

		TimerCount = TimerReader->GetTimerCount();
		for (uint32 TimerIndex = 0; TimerIndex < TimerCount; ++TimerIndex)
		{
			const TraceServices::FTimingProfilerTimer& Timer = *(TimerReader->GetTimer(TimerIndex));
			StringBuilder.Appendf(UTF8TEXT("%u"), Timer.Id);
			Writer.AppendSeparator();
			StringBuilder.Append(Timer.IsGpuTimer ? UTF8TEXT("GPU") : UTF8TEXT("CPU"));
			Writer.AppendSeparator();
			Writer.AppendString(Timer.Name);
			Writer.AppendSeparator();
			if (Timer.File)
			{
				StringBuilder.Append((const UTF8CHAR*)TCHAR_TO_UTF8(Timer.File));
			}
			Writer.AppendSeparator();
			StringBuilder.Appendf(UTF8TEXT("%u"), Timer.Line);
			Writer.AppendLineEnd();
		}
	}

	Writer.Flush();
	ExportFileHandle->Flush();
	delete ExportFileHandle;
	ExportFileHandle = nullptr;

	Stopwatch.Stop();
	const double TotalTime = Stopwatch.GetAccumulatedTime();
	UE_LOG(TraceInsights, Log, TEXT("Exported %u timers to file in %.3fs (\"%s\")."), TimerCount, TotalTime, *Filename);

	return int32(TimerCount);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FName FTimingExporter::ExportTimingEvents_ThreadIdColumn("ThreadId");
const FName FTimingExporter::ExportTimingEvents_ThreadNameColumn("ThreadName");
const FName FTimingExporter::ExportTimingEvents_TimerIdColumn("TimerId");
const FName FTimingExporter::ExportTimingEvents_TimerNameColumn("TimerName");
const FName FTimingExporter::ExportTimingEvents_StartTimeColumn("StartTime");
const FName FTimingExporter::ExportTimingEvents_EndTimeColumn("EndTime");
const FName FTimingExporter::ExportTimingEvents_DurationColumn("Duration");
const FName FTimingExporter::ExportTimingEvents_DepthColumn("Depth");

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingExporter::ExportTimingEvents_InitColumns() const
{
	if (ExportTimingEventsColumns.IsEmpty())
	{
		ExportTimingEventsColumns.Add(ExportTimingEvents_ThreadIdColumn);
		ExportTimingEventsColumns.Add(ExportTimingEvents_ThreadNameColumn);
		ExportTimingEventsColumns.Add(ExportTimingEvents_TimerIdColumn);
		ExportTimingEventsColumns.Add(ExportTimingEvents_TimerNameColumn);
		ExportTimingEventsColumns.Add(ExportTimingEvents_StartTimeColumn);
		ExportTimingEventsColumns.Add(ExportTimingEvents_EndTimeColumn);
		ExportTimingEventsColumns.Add(ExportTimingEvents_DurationColumn);
		ExportTimingEventsColumns.Add(ExportTimingEvents_DepthColumn);

		ExportTimingEventsDefaultColumns.Add(ExportTimingEvents_ThreadIdColumn);
		ExportTimingEventsDefaultColumns.Add(ExportTimingEvents_TimerIdColumn);
		ExportTimingEventsDefaultColumns.Add(ExportTimingEvents_StartTimeColumn);
		ExportTimingEventsDefaultColumns.Add(ExportTimingEvents_EndTimeColumn);
		ExportTimingEventsDefaultColumns.Add(ExportTimingEvents_DepthColumn);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingExporter::MakeExportTimingEventsColumnList(const FString& InColumnsString, TArray<FName>& OutColumnList) const
{
	ExportTimingEvents_InitColumns();

	TArray<FString> Columns;
	InColumnsString.ParseIntoArray(Columns, TEXT(","), true);

	for (const FString& ColumnWildcard : Columns)
	{
		FName ColumnName(*ColumnWildcard);
		if (ExportTimingEventsColumns.Contains(ColumnName))
		{
			OutColumnList.Add(ColumnName);
		}
		else
		{
			for (const FName& Column : ExportTimingEventsColumns)
			{
				if (Column.GetPlainNameString().MatchesWildcard(ColumnWildcard))
				{
					OutColumnList.Add(Column);
				}
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingExporter::ExportTimingEvents_WriteHeader(FExportTimingEventsInternalParams& Params) const
{
	bool bFirst = true;
	for (const FName& Column : Params.Columns)
	{
		if (ExportTimingEventsColumns.Contains(Column))
		{
			if (bFirst)
			{
				bFirst = false;
			}
			else
			{
				Params.Writer.AppendSeparator();
			}
			Params.Writer.Append(Column.GetPlainNameString());
		}
	}
	Params.Writer.AppendLineEnd();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FTimingExporter::ExportTimingEvents_WriteEvents(FExportTimingEventsInternalParams& Params) const
{
	TMap<uint32, const TCHAR*> Timers; // only used if exporting the TimerName column

	if (Params.Columns.Contains(ExportTimingEvents_TimerNameColumn))
	{
		// Iterate the GPU & CPU timers.
		if (TraceServices::ReadTimingProfilerProvider(Session))
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(Session);

			const TraceServices::ITimingProfilerProvider& TimingProfilerProvider = *TraceServices::ReadTimingProfilerProvider(Session);

			const TraceServices::ITimingProfilerTimerReader* TimerReader;
			TimingProfilerProvider.ReadTimers([&TimerReader, &Timers](const TraceServices::ITimingProfilerTimerReader& Out) { TimerReader = &Out; });

			uint32 TimerCount = TimerReader->GetTimerCount();
			for (uint32 TimerIndex = 0; TimerIndex < TimerCount; ++TimerIndex)
			{
				const TraceServices::FTimingProfilerTimer& Timer = *(TimerReader->GetTimer(TimerIndex));
				Timers.Add(Timer.Id, Timer.Name);
			}
		}
	}

	int32 TimingEventCount = 0;

	if (TraceServices::ReadTimingProfilerProvider(Session))
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(Session);

		const TraceServices::ITimingProfilerProvider& TimingProfilerProvider = *TraceServices::ReadTimingProfilerProvider(Session);

		auto TimelineEnumerator = [&Params, &TimingEventCount, &Timers](const TraceServices::ITimingProfilerProvider::Timeline& Timeline)
		{
			// Iterate timing events.
			Timeline.EnumerateEvents(Params.UserParams.IntervalStartTime, Params.UserParams.IntervalEndTime,
				[&Params, &TimingEventCount, &Timers](double EventStartTime, double EventEndTime, uint32 EventDepth, const TraceServices::FTimingProfilerEvent& Event)
				{
					if (!Params.UserParams.TimingEventFilter || Params.UserParams.TimingEventFilter(EventStartTime, EventEndTime, EventDepth, Event))
					{
						FUtf8StringBuilder& StringBuilder = Params.Writer.GetStringBuilder();
						bool bFirst = true;
						for (const FName& Column : Params.Columns)
						{
							if (Params.Exporter.ExportTimingEventsColumns.Contains(Column))
							{
								if (bFirst)
								{
									bFirst = false;
								}
								else
								{
									Params.Writer.AppendSeparator();
								}

								if (Column == ExportTimingEvents_ThreadIdColumn)
								{
									StringBuilder.Appendf(UTF8TEXT("%u"), Params.ThreadId);
								}
								else if (Column == ExportTimingEvents_ThreadNameColumn)
								{
									Params.Writer.AppendString(Params.ThreadName);
								}
								else if (Column == ExportTimingEvents_TimerIdColumn)
								{
									StringBuilder.Appendf(UTF8TEXT("%u"), Event.TimerIndex);
								}
								else if (Column == ExportTimingEvents_TimerNameColumn)
								{
									const TCHAR* TimerName = Timers.FindRef(Event.TimerIndex);
									Params.Writer.AppendString(TimerName);
								}
								else if (Column == ExportTimingEvents_StartTimeColumn)
								{
									StringBuilder.Appendf(UTF8TEXT("%.9g"), EventStartTime);
								}
								else if (Column == ExportTimingEvents_EndTimeColumn)
								{
									StringBuilder.Appendf(UTF8TEXT("%.9g"), EventEndTime);
								}
								else if (Column == ExportTimingEvents_DurationColumn)
								{
									StringBuilder.Appendf(UTF8TEXT("%.9f"), EventEndTime - EventStartTime);
								}
								else if (Column == ExportTimingEvents_DepthColumn)
								{
									StringBuilder.Appendf(UTF8TEXT("%u"), EventDepth);
								}
							}
						}
						Params.Writer.AppendLineEnd();
						++TimingEventCount;
					}

					return TraceServices::EEventEnumerate::Continue;
				});
		};

		// Iterate the GPU timelines.
		{
			if (!Params.UserParams.ThreadFilter || Params.UserParams.ThreadFilter(FGpuTimingTrack::Gpu1ThreadId))
			{
				Params.ThreadId = FGpuTimingTrack::Gpu1ThreadId;
				Params.ThreadName = TEXT("GPU1");
				uint32 GpuTimelineIndex1 = 0;
				TimingProfilerProvider.GetGpuTimelineIndex(GpuTimelineIndex1);
				TimingProfilerProvider.ReadTimeline(GpuTimelineIndex1, TimelineEnumerator);
			}

			if (!Params.UserParams.ThreadFilter || Params.UserParams.ThreadFilter(FGpuTimingTrack::Gpu2ThreadId))
			{
				Params.ThreadId = FGpuTimingTrack::Gpu2ThreadId;
				Params.ThreadName = TEXT("GPU2");
				uint32 GpuTimelineIndex2 = 0;
				TimingProfilerProvider.GetGpu2TimelineIndex(GpuTimelineIndex2);
				TimingProfilerProvider.ReadTimeline(GpuTimelineIndex2, TimelineEnumerator);
			}
		}

		// Iterate the CPU threads and their corresponding timelines.
		const TraceServices::IThreadProvider& ThreadProvider = TraceServices::ReadThreadProvider(Session);
		ThreadProvider.EnumerateThreads(
			[&TimelineEnumerator, &Params, &TimingProfilerProvider](const TraceServices::FThreadInfo& ThreadInfo)
			{
				if (!Params.UserParams.ThreadFilter || Params.UserParams.ThreadFilter(ThreadInfo.Id))
				{
					Params.ThreadId = ThreadInfo.Id;
					Params.ThreadName = ThreadInfo.Name;
					uint32 CpuTimelineIndex = 0;
					TimingProfilerProvider.GetCpuThreadTimelineIndex(ThreadInfo.Id, CpuTimelineIndex);
					TimingProfilerProvider.ReadTimeline(CpuTimelineIndex, TimelineEnumerator);
				}
			});
	}

	return TimingEventCount;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FTimingExporter::ExportTimingEventsAsTextByRegions(const FString& FilenamePattern, FExportTimingEventsParams& Params) const
{
	TMap<FString, FTimeRegionGroup> RegionGroups;
	GetRegions(Params.Region, RegionGroups);

	if (RegionGroups.Num() == 0)
	{
		UE_LOG(TraceInsights, Error, TEXT("Unable to find any region with name pattern '%s'."), *Params.Region);
		return -1;
	}

	FStopwatch Stopwatch;
	Stopwatch.Start();

	// Export timing statistics for each region.
	FExportTimingEventsParams RegionParams = Params;
	RegionParams.Region.Reset();
	int32 ExportedRegionCount = EnumerateRegions(RegionGroups, FilenamePattern,
		[this, &RegionParams](const FString& Filename, const FString& RegionName, double IntervalStartTime, double IntervalEndTime)
		{
			RegionParams.IntervalStartTime = IntervalStartTime;
			RegionParams.IntervalEndTime = IntervalEndTime;
			UE_LOG(TraceInsights, Display, TEXT("Exporting timing statistics for region '%s' [%f .. %f] to '%s'"), *RegionName, RegionParams.IntervalStartTime, RegionParams.IntervalEndTime, *Filename);
			ExportTimingEventsAsText(Filename, RegionParams);
		});

	Stopwatch.Stop();
	const double TotalTime = Stopwatch.GetAccumulatedTime();
	UE_LOG(TraceInsights, Log, TEXT("Exported timing statistics for %d regions in %.3fs."), ExportedRegionCount, TotalTime);
	return ExportedRegionCount;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FTimingExporter::ExportTimingEventsAsText(const FString& Filename, FExportTimingEventsParams& Params) const
{
	if (!Params.Region.IsEmpty())
	{
		return ExportTimingEventsAsTextByRegions(Filename, Params);
	}

	ExportTimingEvents_InitColumns();
	const TArray<FName>& Columns = Params.Columns ? *Params.Columns : ExportTimingEventsDefaultColumns;

	FStopwatch Stopwatch;
	Stopwatch.Start();

	IFileHandle* ExportFileHandle = OpenExportFile(*Filename);
	if (!ExportFileHandle)
	{
		return -1;
	}
	bool bIsCSV = Filename.EndsWith(TEXT(".csv"));
	FUtf8Writer Writer(ExportFileHandle, bIsCSV);
	FUtf8StringBuilder& StringBuilder = Writer.GetStringBuilder();

	FExportTimingEventsInternalParams InternalParams = { *this, Params, Columns, Writer, 0 };

	// Write header.
	ExportTimingEvents_WriteHeader(InternalParams);

	// Write values.
	int32 TimingEventCount = ExportTimingEvents_WriteEvents(InternalParams);

	Writer.Flush();
	ExportFileHandle->Flush();
	delete ExportFileHandle;
	ExportFileHandle = nullptr;

	Stopwatch.Stop();
	const double TotalTime = Stopwatch.GetAccumulatedTime();
	UE_LOG(TraceInsights, Log, TEXT("Exported %d timing events to file in %.3fs (\"%s\")."), TimingEventCount, TotalTime, *Filename);

	return TimingEventCount;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingExporter::GetRegions(const FString& InRegionNamePattern, TMap<FString, FTimeRegionGroup>& OutRegionGroups) const
{
	class FRegionNameSpec
	{
	public:
		FRegionNameSpec(const FString& InNamePatternList)
		{
			InNamePatternList.ParseIntoArray(NamePatterns, TEXT(","), true);
		}

		bool Match(const FString& InRegionName)
		{
			for (const FString& NamePattern : NamePatterns)
			{
				if (InRegionName.MatchesWildcard(NamePattern))
				{
					return true;
				}
			}
			return false;
		}

	private:
		TArray<FString> NamePatterns;
	};
	FRegionNameSpec RegionNameSpec(InRegionNamePattern);

	// Detect regions
	int32 RegionCount = 0;
	FStopwatch DetectRegionsStopwatch;
	DetectRegionsStopwatch.Start();
	{
		const TraceServices::IRegionProvider& RegionProvider = TraceServices::ReadRegionProvider(Session);
		TraceServices::FProviderReadScopeLock RegionProviderScopedLock(RegionProvider);

		UE_LOG(TraceInsights, Log, TEXT("Looking for regions: '%s'"), *InRegionNamePattern);

		RegionProvider.EnumerateRegions(0.0, std::numeric_limits<double>::max(),
			[&RegionCount, &RegionNameSpec, &OutRegionGroups](const TraceServices::FTimeRegion& InRegion) -> bool
			{
				if (!RegionNameSpec.Match(InRegion.Text))
				{
					return true;
				}

				// Handle duplicate region names, individual regions may appear multiple times
				// we append numbers to allow for unique export filenames.
				FString RegionName = InRegion.Text;
				FTimeRegionGroup* ExistingRegionGroup = OutRegionGroups.Find(RegionName);
				if (!ExistingRegionGroup)
				{
					ExistingRegionGroup = &OutRegionGroups.Add(RegionName, FTimeRegionGroup{});
				}
				ExistingRegionGroup->Intervals.Add(FTimeRegionInterval{ InRegion.BeginTime, InRegion.EndTime });
				++RegionCount;
				return true;
			});
	}
	DetectRegionsStopwatch.Stop();
	const double DetectRegionsTime = DetectRegionsStopwatch.GetAccumulatedTime();
	UE_LOG(TraceInsights, Display, TEXT("Detected %d regions in %.3fs."), RegionCount, DetectRegionsTime);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FTimingExporter::EnumerateRegions(const TMap<FString, FTimeRegionGroup>& InRegionGroups, const FString& InFilenamePattern,
	TFunction<void(const FString& /*Filename*/, const FString& /*RegionName*/, double /*IntervalStartTime*/, double /*IntervalEndTime*/)> InCallback) const
{
	constexpr int32 MaxIntervalsPerRegion = 100;
	constexpr int32 MaxExportedRegions = 10000;
	int32 ExportedRegionCount = 0;

	for (auto& KV : InRegionGroups)
	{
		FString RegionName(KV.Key);
		const FString InvalidFileSystemChars = FPaths::GetInvalidFileSystemChars();
		for (int32 CharIndex = 0; CharIndex < InvalidFileSystemChars.Len(); CharIndex++)
		{
			FString Char = FString().AppendChar(InvalidFileSystemChars[CharIndex]);
			RegionName.ReplaceInline(*Char, TEXT("_"));
		}
		RegionName.TrimStartAndEndInline();

		int32 IntervalIndex = 0;
		for (const FTimeRegionInterval& Interval : KV.Value.Intervals)
		{
			FString Filename(InFilenamePattern);
			if (IntervalIndex == 0)
			{
				Filename.ReplaceInline(TEXT("*"), *RegionName); // for backward compatibility
				Filename.ReplaceInline(TEXT("{region}"), *RegionName);
			}
			else
			{
				FString UniqueRegionName = FString::Printf(TEXT("%s_%d"), *RegionName, IntervalIndex);
				Filename.ReplaceInline(TEXT("*"), *UniqueRegionName); // for backward compatibility
				Filename.ReplaceInline(TEXT("{region}"), *UniqueRegionName);
			}
			++IntervalIndex;

			InCallback(Filename, KV.Key, Interval.StartTime, Interval.EndTime);

			++ExportedRegionCount;

			// Avoid writing too many files...
			if (IntervalIndex >= MaxIntervalsPerRegion)
			{
				UE_LOG(TraceInsights, Error, TEXT("Too many intervals for region '%s'! Exporting to separate file per interval for this region is not allowed to continue."), *KV.Key);
				break;
			}
			if (ExportedRegionCount >= MaxExportedRegions)
			{
				UE_LOG(TraceInsights, Error, TEXT("Too many regions! Exporting to separate file per region is not allowed to continue."));
				break;
			}
		}

		if (ExportedRegionCount >= MaxExportedRegions)
		{
			break;
		}
	}

	return ExportedRegionCount;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FTimingExporter::ExportTimerStatisticsAsTextByRegions(const FString& FilenamePattern, FExportTimerStatisticsParams& Params) const
{
	TMap<FString, FTimeRegionGroup> RegionGroups;
	GetRegions(Params.Region, RegionGroups);

	if (RegionGroups.Num() == 0)
	{
		UE_LOG(TraceInsights, Error, TEXT("Unable to find any region with name pattern '%s'."), *Params.Region);
		return -1;
	}

	FStopwatch Stopwatch;
	Stopwatch.Start();

	// Export timing statistics for each region.
	FExportTimerStatisticsParams RegionParams = Params;
	RegionParams.Region.Reset();
	int32 ExportedRegionCount = EnumerateRegions(RegionGroups, FilenamePattern,
		[this, &RegionParams](const FString& Filename, const FString& RegionName, double IntervalStartTime, double IntervalEndTime)
		{
			RegionParams.IntervalStartTime = IntervalStartTime;
			RegionParams.IntervalEndTime = IntervalEndTime;
			UE_LOG(TraceInsights, Display, TEXT("Exporting timing statistics for region '%s' [%f .. %f] to '%s'"), *RegionName, RegionParams.IntervalStartTime, RegionParams.IntervalEndTime, *Filename);
			ExportTimerStatisticsAsText(Filename, RegionParams);
		});

	Stopwatch.Stop();
	const double TotalTime = Stopwatch.GetAccumulatedTime();
	UE_LOG(TraceInsights, Log, TEXT("Exported timing statistics for %d regions in %.3fs."), ExportedRegionCount, TotalTime);
	return ExportedRegionCount;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FTimingExporter::ExportTimerStatisticsAsText(const FString& Filename, FExportTimerStatisticsParams& Params) const
{
	if (!Params.Region.IsEmpty())
	{
		return ExportTimerStatisticsAsTextByRegions(Filename, Params);
	}

	TraceServices::ITable<TraceServices::FTimingProfilerAggregatedStats>* StatsTable;
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(Session);

		const TraceServices::ITimingProfilerProvider* TimingProfilerProvider = TraceServices::ReadTimingProfilerProvider(Session);
		if (!TimingProfilerProvider)
		{
			UE_LOG(TraceInsights, Error, TEXT("Unable to access TimingProfilerProvider for ExportTimerStatisticsAsText"));
			return -1;
		}

		TraceServices::FCreateAggreationParams CreateAggreagationParams;
		CreateAggreagationParams.IntervalStart = Params.IntervalStartTime;
		CreateAggreagationParams.IntervalEnd = Params.IntervalEndTime;
		CreateAggreagationParams.CpuThreadFilter = Params.ThreadFilter;
		CreateAggreagationParams.IncludeGpu = true;

		//@Todo: this does not yet handle the -column and -timers parameters.
		StatsTable = TimingProfilerProvider->CreateAggregation(CreateAggreagationParams);
	}

	FStopwatch Stopwatch;
	Stopwatch.Start();

	bool bSuccess = TraceServices::Table2Csv(*StatsTable, *Filename);

	Stopwatch.Stop();
	const double TotalTime = Stopwatch.GetAccumulatedTime();

	if (bSuccess)
	{
		UE_LOG(TraceInsights, Log, TEXT("Exported timing statistics to file in %.3fs (\"%s\")."), TotalTime, *Filename);
	}
	else
	{
		UE_LOG(TraceInsights, Error, TEXT("Failed to write the CSV file (\"%s\")!"), *Filename);
	}
	return bSuccess ? 1 : -2;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FTimingExporter::ExportTimerCalleesByRegions(const FString& FilenamePattern, const FExportTimerCalleesParams& Params) const
{
	TMap<FString, FTimeRegionGroup> RegionGroups;
	GetRegions(Params.Region, RegionGroups);

	if (RegionGroups.Num() == 0)
	{
		UE_LOG(TraceInsights, Error, TEXT("Unable to find any region with name pattern '%s'."), *Params.Region);
		return -1;
	}

	FStopwatch Stopwatch;
	Stopwatch.Start();

	// Export timing callees for each region.
	FExportTimerCalleesParams RegionParams = Params;
	RegionParams.Region.Reset();
	int32 ExportedRegionCount = EnumerateRegions(RegionGroups, FilenamePattern,
		[this, &RegionParams](const FString& Filename, const FString& RegionName, double IntervalStartTime, double IntervalEndTime)
		{
			RegionParams.IntervalStartTime = IntervalStartTime;
			RegionParams.IntervalEndTime = IntervalEndTime;
			UE_LOG(TraceInsights, Display, TEXT("Exporting timing callees for region '%s' [%f .. %f] to '%s'"), *RegionName, RegionParams.IntervalStartTime, RegionParams.IntervalEndTime, *Filename);
			ExportTimerCalleesAsText(Filename, RegionParams);
		});

	Stopwatch.Stop();
	const double TotalTime = Stopwatch.GetAccumulatedTime();
	UE_LOG(TraceInsights, Log, TEXT("Exported timing callees for %d regions in %.3fs."), ExportedRegionCount, TotalTime);
	return ExportedRegionCount;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FTimingExporter::ExportTimerCalleesAsText(const FString& Filename, const FExportTimerCalleesParams& Params) const
{
	if (!Params.Region.IsEmpty())
	{
		return ExportTimerCalleesByRegions(Filename, Params);
	}

	TUniquePtr<TraceServices::ITimingProfilerButterfly> Butterfly = [&Params, this]() -> TUniquePtr<TraceServices::ITimingProfilerButterfly>
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(Session);

		const TraceServices::ITimingProfilerProvider* TimingProfilerProvider = TraceServices::ReadTimingProfilerProvider(Session);
		if (!TimingProfilerProvider)
		{
			UE_LOG(TraceInsights, Error, TEXT("Unable to access TimingProfilerProvider for ExportTimerCalleesAsText"));
			return nullptr;
		}

		const bool bIncludeGpu = Params.ThreadFilter(FGpuTimingTrack::Gpu1ThreadId) || Params.ThreadFilter(FGpuTimingTrack::Gpu2ThreadId);
	
		// The region end interval may be inf if the capture ended before the region was closed.
		const double ClampedEndTime = FMath::Min(Session.GetDurationSeconds(), Params.IntervalEndTime);

		return TUniquePtr<TraceServices::ITimingProfilerButterfly>
		{
			TimingProfilerProvider->CreateButterfly(Params.IntervalStartTime, ClampedEndTime, Params.ThreadFilter, bIncludeGpu)
		};
	}();

	if (!Butterfly)
	{
		UE_LOG(TraceInsights, Error, TEXT("ExportTimerCalleesAsText failed to create Butterfly for region %s [%.6f-%.6f]"), *Params.Region, Params.IntervalStartTime, Params.IntervalEndTime);
		return -1;
	}

	TUniquePtr<IFileHandle> ExportFileHandle{ OpenExportFile(*Filename) };
	if (!ExportFileHandle)
	{
		UE_LOG(TraceInsights, Error, TEXT("ExportTimerCalleesAsText failed to open export file %s."), *Filename);
		return -1;
	}

	const bool bIsCSV = Filename.EndsWith(TEXT(".csv"));
	FUtf8Writer Writer(ExportFileHandle.Get(), bIsCSV);

	// Write header
	{
		TArray<FStringView> Columns
		{
			TEXTVIEW("TimerId"),
			TEXTVIEW("ParentId"),
			TEXTVIEW("TimerName"),
			TEXTVIEW("Count"),
			TEXTVIEW("Inc.Time"),
			TEXTVIEW("Exc.Time"),
			TEXTVIEW("NumFrames")
		};

		for (const FStringView ColumnName : Columns)
		{
			Writer.AppendString(ColumnName.GetData());
			if (ColumnName != Columns.Last())
			{
				Writer.AppendSeparator();
			}
		}
		Writer.AppendLineEnd();
	}

	// Count the number of frames in this region so it can be output and used to calculate frame averages.
	uint64 NumFrames = 0;
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(Session);
		const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(Session);
		FrameProvider.EnumerateFrames(ETraceFrameType::TraceFrameType_Game, Params.IntervalStartTime, Params.IntervalEndTime, [&NumFrames](const TraceServices::FFrame& Frame)
		{
			++NumFrames;
		});
	}

	// Write rows
	{
		FUtf8StringBuilder& StringBuilder = Writer.GetStringBuilder();

		for (uint32 TimerId : Params.TimerIds)
		{
			TArray<const TraceServices::FTimingProfilerButterflyNode*> NodesToVisit{ &Butterfly->GenerateCalleesTree(TimerId) };
			while (!NodesToVisit.IsEmpty())
			{
				// Root node can be null if the timer id is for a thread we've filtered out.
				const TraceServices::FTimingProfilerButterflyNode* CurrentNode = NodesToVisit.Pop(EAllowShrinking::No);
				if (!CurrentNode || !CurrentNode->Timer)
				{
					continue;
				}

				NodesToVisit.Append(CurrentNode->Children);

				StringBuilder.Appendf(UTF8TEXT("%u"), CurrentNode->Timer->Id);
				Writer.AppendSeparator();
				StringBuilder.Appendf(UTF8TEXT("%u"), CurrentNode->Parent ? CurrentNode->Parent->Timer->Id : uint32(-1));
				Writer.AppendSeparator();
				Writer.AppendString(CurrentNode->Timer->Name);
				Writer.AppendSeparator();
				StringBuilder.Appendf(UTF8TEXT("%llu"), CurrentNode->Count);
				Writer.AppendSeparator();
				StringBuilder.Appendf(UTF8TEXT("%.9g"), CurrentNode->InclusiveTime);
				Writer.AppendSeparator();
				StringBuilder.Appendf(UTF8TEXT("%.9g"), CurrentNode->ExclusiveTime);
				Writer.AppendSeparator();
				StringBuilder.Appendf(UTF8TEXT("%llu"), NumFrames);
				Writer.AppendLineEnd();
			}
		}
	}

	Writer.Flush();
	ExportFileHandle->Flush();
	return 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingExporter::FThreadFilterFunc FTimingExporter::MakeThreadFilterInclusive(const FString& InFilterString, TSet<uint32>& OutIncludedThreads) const
{
	if (InFilterString.Len() == 1 && InFilterString[0] == TEXT('*'))
	{
		return nullptr;
	}

	OutIncludedThreads.Reset();

	TMap<FString, uint32> Threads;

	// Add the GPU threads.
	Threads.Add(FString("GPU"), FGpuTimingTrack::Gpu1ThreadId);
	Threads.Add(FString("GPU1"), FGpuTimingTrack::Gpu1ThreadId);
	Threads.Add(FString("GPU2"), FGpuTimingTrack::Gpu2ThreadId);

	// Iterate the CPU threads.
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(Session);

		const TraceServices::IThreadProvider& ThreadProvider = TraceServices::ReadThreadProvider(Session);

		ThreadProvider.EnumerateThreads(
			[&Threads](const TraceServices::FThreadInfo& ThreadInfo)
			{
				Threads.Add(FString(ThreadInfo.Name), ThreadInfo.Id);
			});
	}

	TArray<FString> Filter;
	InFilterString.ParseIntoArray(Filter, TEXT(","), true);

	for (const FString& ThreadWildcard : Filter)
	{
		const uint32* Id = Threads.Find(ThreadWildcard);
		if (Id)
		{
			OutIncludedThreads.Add(*Id);
		}
		else
		{
			for (const auto& KeyValuePair : Threads)
			{
				if (KeyValuePair.Key.MatchesWildcard(ThreadWildcard))
				{
					OutIncludedThreads.Add(KeyValuePair.Value);
				}
			}
		}
	}

	return MakeThreadFilterInclusive(OutIncludedThreads);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TFunction<bool(uint32)> FTimingExporter::MakeThreadFilterInclusive(const TSet<uint32>& IncludedThreads)
{
	if (IncludedThreads.Num() == 0)
	{
		return [](uint32 ThreadId) -> bool
		{
			return false;
		};
	}

	if (IncludedThreads.Num() == 1)
	{
		const uint32 IncludedThreadId = IncludedThreads[FSetElementId::FromInteger(0)];
		return [IncludedThreadId](uint32 ThreadId) -> bool
		{
			return ThreadId == IncludedThreadId;
		};
	}

	return [&IncludedThreads](uint32 ThreadId) -> bool
	{
		return IncludedThreads.Contains(ThreadId);
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TFunction<bool(uint32)> FTimingExporter::MakeThreadFilterExclusive(const TSet<uint32>& ExcludedThreads)
{
	if (ExcludedThreads.Num() == 0)
	{
		return nullptr;
	}

	if (ExcludedThreads.Num() == 1)
	{
		const uint32 ExcludedThreadId = ExcludedThreads[FSetElementId::FromInteger(0)];
		return [ExcludedThreadId](uint32 ThreadId) -> bool
		{
			return ThreadId != ExcludedThreadId;
		};
	}

	return [&ExcludedThreads](uint32 ThreadId) -> bool
	{
		return !ExcludedThreads.Contains(ThreadId);
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingExporter::FTimingEventFilterFunc FTimingExporter::MakeTimingEventFilterByTimersInclusive(const FString& InFilterString, TSet<uint32>& OutIncludedTimers) const
{
	if (InFilterString.Len() == 1 && InFilterString[0] == TEXT('*'))
	{
		return nullptr;
	}

	OutIncludedTimers.Reset();

	TMultiMap<FString, uint32> Timers;

	// Iterate the GPU & CPU timers.
	if (TraceServices::ReadTimingProfilerProvider(Session))
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(Session);

		const TraceServices::ITimingProfilerProvider& TimingProfilerProvider = *TraceServices::ReadTimingProfilerProvider(Session);

		const TraceServices::ITimingProfilerTimerReader* TimerReader;
		TimingProfilerProvider.ReadTimers([&TimerReader, &Timers](const TraceServices::ITimingProfilerTimerReader& Out) { TimerReader = &Out; });

		uint32 TimerCount = TimerReader->GetTimerCount();
		for (uint32 TimerIndex = 0; TimerIndex < TimerCount; ++TimerIndex)
		{
			const TraceServices::FTimingProfilerTimer& Timer = *(TimerReader->GetTimer(TimerIndex));
			Timers.Add(FString(Timer.Name), Timer.Id);
		}
	}

	TArray<FString> Filter;
	InFilterString.ParseIntoArray(Filter, TEXT(","), true);

	TArray<uint32> Ids;
	for (const FString& TimerWildcard : Filter)
	{
		Ids.Reset();

		Timers.MultiFind(TimerWildcard, Ids, false);
		if (!Ids.IsEmpty())
		{
			OutIncludedTimers.Append(Ids);
		}
		else
		{
			for (const auto& KeyValuePair : Timers)
			{
				if (KeyValuePair.Key.MatchesWildcard(TimerWildcard))
				{
					OutIncludedTimers.Add(KeyValuePair.Value);
				}
			}
		}
	}

	return MakeTimingEventFilterByTimersInclusive(OutIncludedTimers);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingExporter::FTimingEventFilterFunc FTimingExporter::MakeTimingEventFilterByTimersInclusive(const TSet<uint32>& IncludedTimers)
{
	if (IncludedTimers.Num() == 0)
	{
		return [](double EventStartTime, double EventEndTime, uint32 EventDepth, const TraceServices::FTimingProfilerEvent& Event) -> bool
		{
			return false;
		};
	}

	if (IncludedTimers.Num() == 1)
	{
		const uint32 IncludedTimerId = IncludedTimers[FSetElementId::FromInteger(0)];
		return [IncludedTimerId](double EventStartTime, double EventEndTime, uint32 EventDepth, const TraceServices::FTimingProfilerEvent& Event) -> bool
		{
			return Event.TimerIndex == IncludedTimerId;
		};
	}

	return [&IncludedTimers](double EventStartTime, double EventEndTime, uint32 EventDepth, const TraceServices::FTimingProfilerEvent& Event) -> bool
	{
		return IncludedTimers.Contains(Event.TimerIndex);
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingExporter::FTimingEventFilterFunc FTimingExporter::MakeTimingEventFilterByTimersExclusive(const TSet<uint32>& ExcludedTimers)
{
	if (ExcludedTimers.Num() == 0)
	{
		return nullptr;
	}

	if (ExcludedTimers.Num() == 1)
	{
		const uint32 ExcludedTimerId = ExcludedTimers[FSetElementId::FromInteger(0)];
		return [ExcludedTimerId](double EventStartTime, double EventEndTime, uint32 EventDepth, const TraceServices::FTimingProfilerEvent& Event) -> bool
		{
			return Event.TimerIndex != ExcludedTimerId;
		};
	}

	return [&ExcludedTimers](double EventStartTime, double EventEndTime, uint32 EventDepth, const TraceServices::FTimingProfilerEvent& Event) -> bool
	{
		return !ExcludedTimers.Contains(Event.TimerIndex);
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FTimingExporter::ExportCountersAsText(const FString& Filename, FExportCountersParams& Params) const
{
	if (Params.Columns != nullptr)
	{
		UE_LOG(TraceInsights, Error, TEXT("Custom list of columns is not yet supported!"));
		return -1;
	}

	FStopwatch Stopwatch;
	Stopwatch.Start();

	IFileHandle* ExportFileHandle = OpenExportFile(*Filename);
	if (!ExportFileHandle)
	{
		return -1;
	}
	bool bIsCSV = Filename.EndsWith(TEXT(".csv"));
	FUtf8Writer Writer(ExportFileHandle, bIsCSV);
	FUtf8StringBuilder& StringBuilder = Writer.GetStringBuilder();

	// Write header.
	{
		StringBuilder.Append(UTF8TEXT("Id"));
		Writer.AppendSeparator();
		StringBuilder.Append(UTF8TEXT("Type"));
		Writer.AppendSeparator();
		StringBuilder.Append(UTF8TEXT("Name"));
		Writer.AppendLineEnd();
	}

	int32 CounterCount = 0;

	// Write values.
	if (true) // TraceServices::ReadCounterProvider(Session)
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(Session);

		const TraceServices::ICounterProvider& CounterProvider = TraceServices::ReadCounterProvider(Session);

		CounterProvider.EnumerateCounters(
			[&](uint32 CounterId, const TraceServices::ICounter& Counter)
			{
				StringBuilder.Appendf(UTF8TEXT("%u"), CounterId);
				Writer.AppendSeparator();
				if (Counter.IsFloatingPoint())
				{
					StringBuilder.Append(UTF8TEXT("Double"));
				}
				else
				{
					StringBuilder.Append(UTF8TEXT("Int64"));
				}
				if (Counter.IsResetEveryFrame())
				{
					StringBuilder.Append(UTF8TEXT("|ResetEveryFrame"));
				}
				Writer.AppendSeparator();
				Writer.AppendString(Counter.GetName());
				Writer.AppendLineEnd();
				++CounterCount;
			});
	}

	Writer.Flush();
	ExportFileHandle->Flush();
	delete ExportFileHandle;
	ExportFileHandle = nullptr;

	Stopwatch.Stop();
	const double TotalTime = Stopwatch.GetAccumulatedTime();
	UE_LOG(TraceInsights, Log, TEXT("Exported %d counters to file in %.3fs (\"%s\")."), CounterCount, TotalTime, *Filename);

	return CounterCount;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FTimingExporter::ExportCounterAsTextByRegions(const FString& FilenamePattern, uint32 CounterId, FExportCounterParams& Params) const
{
	TMap<FString, FTimeRegionGroup> RegionGroups;
	GetRegions(Params.Region, RegionGroups);

	if (RegionGroups.Num() == 0)
	{
		UE_LOG(TraceInsights, Error, TEXT("Unable to find any region with name pattern '%s'."), *Params.Region);
		return -1;
	}

	FStopwatch Stopwatch;
	Stopwatch.Start();

	// Export counter for each region.
	FExportCounterParams RegionParams = Params;
	RegionParams.Region.Reset();
	int32 ExportedRegionCount = EnumerateRegions(RegionGroups, FilenamePattern,
		[this, CounterId, &RegionParams](const FString& Filename, const FString& RegionName, double IntervalStartTime, double IntervalEndTime)
		{
			RegionParams.IntervalStartTime = IntervalStartTime;
			RegionParams.IntervalEndTime = IntervalEndTime;
			UE_LOG(TraceInsights, Display, TEXT("Exporting counter %u for region '%s' [%f .. %f] to '%s'"), CounterId , *RegionName, RegionParams.IntervalStartTime, RegionParams.IntervalEndTime, *Filename);
			ExportCounterAsText(Filename, CounterId, RegionParams);
		});

	Stopwatch.Stop();
	const double TotalTime = Stopwatch.GetAccumulatedTime();
	UE_LOG(TraceInsights, Log, TEXT("Exported counter values for %d regions in %.3fs."), ExportedRegionCount, TotalTime);
	return ExportedRegionCount;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FTimingExporter::ExportCounterAsText(const FString& FilenamePattern, uint32 CounterId, FExportCounterParams& Params) const
{
	if (Params.Columns != nullptr)
	{
		UE_LOG(TraceInsights, Error, TEXT("Custom list of columns is not yet supported!"));
		return -1;
	}

	if (!Params.Region.IsEmpty())
	{
		return ExportCounterAsTextByRegions(FilenamePattern, CounterId, Params);
	}

	FStopwatch Stopwatch;
	Stopwatch.Start();

	FString CounterName;

	if (true) // TraceServices::ReadCounterProvider(Session)
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(Session);
		const TraceServices::ICounterProvider& CounterProvider = TraceServices::ReadCounterProvider(Session);
		CounterProvider.ReadCounter(CounterId,
			[&](const TraceServices::ICounter& Counter)
			{
				CounterName = Counter.GetName();
			});
	}

	if (CounterName.IsEmpty())
	{
		UE_LOG(TraceInsights, Error, TEXT("Invalid counter!"));
		return -1;
	}

	FString Filename(FilenamePattern);
	if (Filename.Contains(TEXT("{counter}")))
	{
		FString CounterFilename(CounterName);
		const FString InvalidFileSystemChars = FPaths::GetInvalidFileSystemChars();
		for (int32 CharIndex = 0; CharIndex < InvalidFileSystemChars.Len(); CharIndex++)
		{
			FString Char = FString().AppendChar(InvalidFileSystemChars[CharIndex]);
			CounterFilename.ReplaceInline(*Char, TEXT("_"));
		}
		CounterFilename.TrimStartAndEndInline();
		Filename.ReplaceInline(TEXT("{counter}"), *CounterFilename);
	}

	IFileHandle* ExportFileHandle = OpenExportFile(*Filename);
	if (!ExportFileHandle)
	{
		return -1;
	}
	bool bIsCSV = Filename.EndsWith(TEXT(".csv"));
	FUtf8Writer Writer(ExportFileHandle, bIsCSV);
	FUtf8StringBuilder& StringBuilder = Writer.GetStringBuilder();

	// Write header.
	if (Params.bExportOps)
	{
		StringBuilder.Append(UTF8TEXT("Time"));
		Writer.AppendSeparator();
		StringBuilder.Append(UTF8TEXT("Op"));
		Writer.AppendSeparator();
		StringBuilder.Append(UTF8TEXT("Value"));
		Writer.AppendLineEnd();
	}
	else
	{
		StringBuilder.Append(UTF8TEXT("Time"));
		Writer.AppendSeparator();
		StringBuilder.Append(UTF8TEXT("Value"));
		Writer.AppendLineEnd();
	}

	int32 ValueCount = 0;

	// Write values.
	if (true) // TraceServices::ReadCounterProvider(Session)
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(Session);

		const TraceServices::ICounterProvider& CounterProvider = TraceServices::ReadCounterProvider(Session);

		CounterProvider.ReadCounter(CounterId,
			[&](const TraceServices::ICounter& Counter)
			{
				// Iterate the counter values.
				if (Params.bExportOps)
				{
					if (Counter.IsFloatingPoint())
					{
						Counter.EnumerateFloatOps(Params.IntervalStartTime, Params.IntervalEndTime, false,
							[&](double Time, TraceServices::ECounterOpType Op, double Value)
							{
								StringBuilder.Appendf(UTF8TEXT("%.9f"), Time);
								Writer.AppendSeparator();
								switch (Op)
								{
									case TraceServices::ECounterOpType::Set:
										StringBuilder.Append(UTF8TEXT("Set"));
										break;
									case TraceServices::ECounterOpType::Add:
										StringBuilder.Append(UTF8TEXT("Add"));
										break;
									default:
										StringBuilder.Appendf(UTF8TEXT("%d"), int32(Op));
								}
								Writer.AppendSeparator();
								StringBuilder.Appendf(UTF8TEXT("%.9f"), Value);
								Writer.AppendLineEnd();
								++ValueCount;
							});
					}
					else
					{
						Counter.EnumerateOps(Params.IntervalStartTime, Params.IntervalEndTime, false,
							[&](double Time, TraceServices::ECounterOpType Op, int64 IntValue)
							{
								StringBuilder.Appendf(UTF8TEXT("%.9f"), Time);
								Writer.AppendSeparator();
								switch (Op)
								{
									case TraceServices::ECounterOpType::Set:
										StringBuilder.Append(UTF8TEXT("Set"));
										break;
									case TraceServices::ECounterOpType::Add:
										StringBuilder.Append(UTF8TEXT("Add"));
										break;
									default:
										StringBuilder.Appendf(UTF8TEXT("%d"), int32(Op));
								}
								Writer.AppendSeparator();
								StringBuilder.Appendf(UTF8TEXT("%lli"), IntValue);
								Writer.AppendLineEnd();
								++ValueCount;
							});
					}
				}
				else
				{
					if (Counter.IsFloatingPoint())
					{
						Counter.EnumerateFloatValues(Params.IntervalStartTime, Params.IntervalEndTime, false,
							[&](double Time, double Value)
							{
								StringBuilder.Appendf(UTF8TEXT("%.9f"), Time);
								Writer.AppendSeparator();
								StringBuilder.Appendf(UTF8TEXT("%.9f"), Value);
								Writer.AppendLineEnd();
								++ValueCount;
							});
					}
					else
					{
						Counter.EnumerateValues(Params.IntervalStartTime, Params.IntervalEndTime, false,
							[&](double Time, int64 IntValue)
							{
								StringBuilder.Appendf(UTF8TEXT("%.9f"), Time);
								Writer.AppendSeparator();
								StringBuilder.Appendf(UTF8TEXT("%lli"), IntValue);
								Writer.AppendLineEnd();
								++ValueCount;
							});
					}
				}
			});
	}

	Writer.Flush();
	ExportFileHandle->Flush();
	delete ExportFileHandle;
	ExportFileHandle = nullptr;

	Stopwatch.Stop();
	const double TotalTime = Stopwatch.GetAccumulatedTime();
	UE_LOG(TraceInsights, Log, TEXT("Exported counter %d (\"%s\", %d values) to file in %.3fs (\"%s\")."), CounterId, *CounterName, ValueCount, TotalTime, *Filename);

	return 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::TimingProfiler

#undef LOCTEXT_NAMESPACE
