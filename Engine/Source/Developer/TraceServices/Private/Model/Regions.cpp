// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraceServices/Model/Regions.h"
#include "Model/RegionsPrivate.h"

#include "AnalysisServicePrivate.h"
#include "Algo/ForEach.h"
#include "Common/FormatArgs.h"
#include "Common/Utils.h"
#include "Internationalization/Internationalization.h"

#define LOCTEXT_NAMESPACE "RegionProvider"

namespace TraceServices
{

thread_local FProviderLock::FThreadLocalState GRegionsProviderLockState;

FRegionProvider::FRegionProvider(IAnalysisSession& InSession)
	: Session(InSession)
{
}

uint64 FRegionProvider::GetRegionCount() const
{
	ReadAccessCheck();

	uint64 RegionCount = 0;
	for (const FRegionLane& Lane : Lanes)
	{
		RegionCount += Lane.Num();
	}
	return RegionCount;
}

const FRegionLane* FRegionProvider::GetLane(int32 index) const
{
	ReadAccessCheck();

	if (index < Lanes.Num())
	{
		return &(Lanes[index]);
	}
	return nullptr;
}

void FRegionProvider::AppendRegionBegin(const TCHAR* Name, double Time)
{
	EditAccessCheck();

	check(Name)

	FTimeRegion** OpenRegion = OpenRegionsByName.Find(Name);

	if (OpenRegion)
	{
		++NumWarnings;
		if (NumWarnings <= MaxWarningMessages)
		{
			UE_LOG(LogTraceServices, Warning, TEXT("[Regions] A region begin event (BeginTime=%f, Name=\"%s\") was encountered while a region with same name is already open."), Time, Name)
		}

		// Automatically end the previous region.
		AppendRegionEnd(Name, Time);
	}

	FTimeRegion* NewRegion = InsertNewRegion(Time, Name, 0);
	OpenRegionsByName.Add(NewRegion->Text, NewRegion);
}

void FRegionProvider::AppendRegionBeginWithId(const TCHAR* Name, uint64 Id, double Time)
{
	EditAccessCheck();

	check(Name && Id)
	FTimeRegion** OpenRegion = OpenRegionsById.Find(Id);

	if (OpenRegion)
	{
		++NumWarnings;
		if (NumWarnings <= MaxWarningMessages)
		{
			UE_LOG(LogTraceServices, Warning, TEXT("[Regions] A region begin event (BeginTime=%f, Name=\"%s\", Id=%llu) was encountered while a region with same name is already open."), Time, Name, Id)
		}

		// Automatically end the previous region.
		AppendRegionEndWithId(Id, Time);
	}

	FTimeRegion* NewRegion = InsertNewRegion(Time, Name, Id);
	OpenRegionsById.Add(Id, NewRegion);
}

void FRegionProvider::AppendRegionEnd(const TCHAR* Name, double Time)
{
	EditAccessCheck();

	check(Name)
	FTimeRegion** OpenRegionPos = OpenRegionsByName.Find(Name);

	if (!OpenRegionPos)
	{
		++NumWarnings;
		if (NumWarnings <= MaxWarningMessages)
		{
			UE_LOG(LogTraceServices, Warning, TEXT("[Regions] A region end event (EndTime=%f, Name=\"%s\") was encountered without having seen a matching region begin event first."), Time, Name)
		}

		AppendRegionBegin(Name, Time);
		OpenRegionPos = OpenRegionsByName.Find(Name);
		check(OpenRegionPos);
	}

	FTimeRegion* OpenRegion = *OpenRegionPos;
	check(OpenRegion);

	OpenRegion->EndTime = Time;

	OpenRegionsByName.Remove(OpenRegion->Text);
	UpdateCounter++;

	// Update session time
	{
		FAnalysisSessionEditScope _(Session);
		Session.UpdateDurationSeconds(Time);
	}
}

void FRegionProvider::AppendRegionEndWithId(uint64 Id, double Time)
{
	EditAccessCheck();

	check(Id)
	FTimeRegion** OpenRegionPos = OpenRegionsById.Find(Id);

	if (!OpenRegionPos)
	{
		++NumWarnings;
		if (NumWarnings <= MaxWarningMessages)
		{
			UE_LOG(LogTraceServices, Warning, TEXT("[Regions] A region end event (EndTime=%f, Id=%llu) was encountered without having seen a matching region begin event first."), Time, Id)
		}

		// Automatically create a new region.
		// Generates a display name if we're missing a begin and are closing by ID
		FString GeneratedName = FString::Printf(TEXT("Unknown Region (missing begin, Id=%llu)"), Id);
		AppendRegionBeginWithId(*GeneratedName, Id, Time);
		OpenRegionPos = OpenRegionsById.Find(Id);
		check(OpenRegionPos);
	}

	FTimeRegion* OpenRegion = *OpenRegionPos;
	check(OpenRegion);

	OpenRegion->EndTime = Time;

	OpenRegionsById.Remove(OpenRegion->Id);
	UpdateCounter++;

	// Update session time
	{
		FAnalysisSessionEditScope _(Session);
		Session.UpdateDurationSeconds(Time);
	}
}

void FRegionProvider::OnAnalysisSessionEnded()
{
	EditAccessCheck();

	auto printOpenRegionMessage = [this](const auto& KV)
	{
		const FTimeRegion* Region = KV.Value;

		++NumWarnings;
		if (NumWarnings <= MaxWarningMessages)
		{
			UE_LOG(LogTraceServices, Warning, TEXT("[Regions] A region (BeginTime=%f, Name=\"%s\", Id=%llu) was never closed."), Region->BeginTime, Region->Text, Region->Id)
		}
	};
	Algo::ForEach(OpenRegionsById, printOpenRegionMessage);
	Algo::ForEach(OpenRegionsByName, printOpenRegionMessage);

	if (NumWarnings > 0)
	{
		UE_LOG(LogTraceServices, Warning, TEXT("[Regions] %u warnings"), NumWarnings);
	}
	if (NumErrors > 0)
	{
		UE_LOG(LogTraceServices, Error, TEXT("[Regions] %u errors"), NumErrors);
	}

	uint64 TotalRegionCount = GetRegionCount();
	UE_LOG(LogTraceServices, Log, TEXT("[Regions] Analysis completed (%llu regions, %d lanes)."), TotalRegionCount, Lanes.Num());
}

int32 FRegionProvider::CalculateRegionDepth(const FTimeRegion& Region) const
{
	constexpr int32 DepthLimit = 100;

	int32 NewDepth = 0;

	// Find first free lane/depth
	while (NewDepth < DepthLimit)
	{
		if (!Lanes.IsValidIndex(NewDepth))
		{
			break;
		}

		const FTimeRegion& LastRegion = Lanes[NewDepth].Regions.Last();
		if (LastRegion.EndTime <= Region.BeginTime)
		{
			break;
		}
		NewDepth++;
	}

	ensureMsgf(NewDepth < DepthLimit, TEXT("Regions are nested too deep."));

	return NewDepth;
}

FTimeRegion* FRegionProvider::InsertNewRegion(double BeginTime, const TCHAR* Name, uint64 Id)
{
	FTimeRegion Region;
	Region.BeginTime = BeginTime;
	Region.Text = Session.StoreString(Name);
	Region.Id = Id;
	Region.Depth = CalculateRegionDepth(Region);

	if (Region.Depth == Lanes.Num())
	{
		Lanes.Emplace(Session.GetLinearAllocator());
	}

	Lanes[Region.Depth].Regions.EmplaceBack(Region);
	FTimeRegion* NewOpenRegion = &(Lanes[Region.Depth].Regions.Last());
	UpdateCounter++;

	// Update session time
	{
		FAnalysisSessionEditScope _(Session);
		Session.UpdateDurationSeconds(BeginTime);
	}
	return NewOpenRegion;
}

void FRegionProvider::EnumerateLanes(TFunctionRef<void(const FRegionLane&, int32)> Callback) const
{
	ReadAccessCheck();

	for (int32 LaneIndex = 0; LaneIndex < Lanes.Num(); ++LaneIndex)
	{
		Callback(Lanes[LaneIndex], LaneIndex);
	}
}

bool FRegionProvider::EnumerateRegions(double IntervalStart, double IntervalEnd, TFunctionRef<bool(const FTimeRegion&)> Callback) const
{
	ReadAccessCheck();

	if (IntervalStart > IntervalEnd)
	{
		return false;
	}

	for (const FRegionLane& Lane : Lanes)
	{
		if (!Lane.EnumerateRegions(IntervalStart, IntervalEnd, Callback))
		{
			return false;
		}
	}

	return true;
}

bool FRegionLane::EnumerateRegions(double IntervalStart, double IntervalEnd, TFunctionRef<bool(const FTimeRegion&)> Callback) const
{
	const FInt32Interval OverlapRange = GetElementRangeOverlappingGivenRange<FTimeRegion>(Regions, IntervalStart, IntervalEnd,
		[](const FTimeRegion& r) { return r.BeginTime; },
		[](const FTimeRegion& r) { return r.EndTime; });

	if (OverlapRange.Min == -1)
	{
		return true;
	}

	for (int32 Index = OverlapRange.Min; Index <= OverlapRange.Max; ++Index)
	{
		if (!Callback(Regions[Index]))
		{
			return false;
		}
	}

	return true;
}

FName GetRegionProviderName()
{
	static const FName Name("RegionProvider");
	return Name;
}

const IRegionProvider& ReadRegionProvider(const IAnalysisSession& Session)
{
	return *Session.ReadProvider<IRegionProvider>(GetRegionProviderName());
}

IEditableRegionProvider& EditRegionProvider(IAnalysisSession& Session)
{
	return *Session.EditProvider<IEditableRegionProvider>(GetRegionProviderName());
}

} // namespace TraceServices

#undef LOCTEXT_NAMESPACE
