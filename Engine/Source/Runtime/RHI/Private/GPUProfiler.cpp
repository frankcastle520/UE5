// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GPUProfiler.h: Hierarchical GPU Profiler Implementation.
=============================================================================*/

#include "GPUProfiler.h"
#include "Async/TaskGraphInterfaces.h"
#include "Misc/WildcardString.h"
#include "Misc/CommandLine.h"
#include "RHI.h"
#include "GpuProfilerTrace.h"

#if !UE_BUILD_SHIPPING
#include "VisualizerEvents.h"
#include "ProfileVisualizerModule.h"
#include "Modules/ModuleManager.h"
#endif

#if HAS_GPU_STATS
CSV_DEFINE_CATEGORY_MODULE(RHI_API, GPU, true);
DECLARE_FLOAT_COUNTER_STAT(TEXT("[TOTAL]"), Stat_GPU_Total, STATGROUP_GPU);
CSV_DEFINE_STAT(GPU, Total);
#endif

// Temporary function to resolve link issues with the above "Total" GPU stat moving from RenderCore to RHI.
// We can remove this once the old GPU profiler code has been deleted, and RHI_NEW_GPU_PROFILER is set to 1 permanently.
extern RHI_API void RHISetGPUStatTotals(bool bCsvStatsEnabled, double TotalMs)
{
#if HAS_GPU_STATS

#if STATS
	FThreadStats::AddMessage(GET_STATFNAME(Stat_GPU_Total), EStatOperation::Set, TotalMs);
	TRACE_STAT_SET(GET_STATFNAME(Stat_GPU_Total), TotalMs);
#endif

#if CSV_PROFILER_STATS
	if (bCsvStatsEnabled)
	{
		FCsvProfiler::Get()->RecordCustomStat(CSV_STAT_FNAME(Total), CSV_CATEGORY_INDEX(GPU), TotalMs, ECsvCustomStatOp::Set);
	}
#endif

#endif // HAS_GPU_STATS
}

static TAutoConsoleVariable<int> CVarGPUCsvStatsEnabled(
	TEXT("r.GPUCsvStatsEnabled"),
	0,
	TEXT("Enables or disables GPU stat recording to CSVs"));

#define LOCTEXT_NAMESPACE "GpuProfiler"

static TAutoConsoleVariable<FString> GProfileGPUPatternCVar(
	TEXT("r.ProfileGPU.Pattern"),
	TEXT("*"),
	TEXT("Allows to filter the entries when using ProfileGPU, the pattern match is case sensitive.\n")
	TEXT("'*' can be used in the end to get all entries starting with the string.\n")
	TEXT("    '*' without any leading characters disables the pattern matching and uses a time threshold instead (default).\n")
	TEXT("'?' allows to ignore one character.\n")
	TEXT("e.g. AmbientOcclusionSetup, AmbientOcclusion*, Ambient???lusion*, *"),
	ECVF_Default);

static TAutoConsoleVariable<FString> GProfileGPURootCVar(
	TEXT("r.ProfileGPU.Root"),
	TEXT("*"),
	TEXT("Allows to filter the tree when using ProfileGPU, the pattern match is case sensitive."),
	ECVF_Default);

static TAutoConsoleVariable<float> GProfileThresholdPercent(
	TEXT("r.ProfileGPU.ThresholdPercent"),
	0.0f,
	TEXT("Percent of the total execution duration the event needs to be larger than to be printed."),
	ECVF_Default);

static TAutoConsoleVariable<int32> GProfileShowEventHistogram(
	TEXT("r.ProfileGPU.ShowEventHistogram"),
	0,
	TEXT("Whether the event histogram should be shown."),
	ECVF_Default);

static TAutoConsoleVariable<int32> GProfileGPUShowEvents(
	TEXT("r.ProfileGPU.ShowLeafEvents"),
	1,
	TEXT("Allows profileGPU to display event-only leaf nodes with no draws associated."),
	ECVF_Default);

RHI_API TAutoConsoleVariable<int32> GProfileGPUTransitions(
	TEXT("r.ProfileGPU.ShowTransitions"),
	0,
	TEXT("Allows profileGPU to display resource transition events."),
	ECVF_Default);

// Should we print a summary at the end?
static TAutoConsoleVariable<int32> GProfilePrintAssetSummary(
	TEXT("r.ProfileGPU.PrintAssetSummary"),
	0,
	TEXT("Should we print a summary split by asset (r.ShowMaterialDrawEvents is strongly recommended as well).\n"),
	ECVF_Default);

// Should we print a summary at the end?
static TAutoConsoleVariable<FString> GProfileAssetSummaryCallOuts(
	TEXT("r.ProfileGPU.AssetSummaryCallOuts"),
	TEXT(""),
	TEXT("Comma separated list of substrings that deserve special mention in the final summary (e.g., \"LOD,HeroName\"\n")
	TEXT("r.ProfileGPU.PrintAssetSummary must be true to enable this feature"),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarGPUCrashDataCollectionEnable(
	TEXT("r.gpucrash.collectionenable"),
	1,
	TEXT("Stores GPU crash data from scoped events when a applicable crash debugging system is available."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarGPUCrashDataDepth(
	TEXT("r.gpucrash.datadepth"),
	-1,
	TEXT("Limits the amount of marker scope depth we record for GPU crash debugging to the given scope depth."),
	ECVF_RenderThreadSafe);

enum class EGPUProfileSortMode
{
	EChronological,
	ETimeElapsed,
	ENumPrims,
	ENumVerts,
	EMax
};

static TAutoConsoleVariable<int32> GProfileGPUSort(
	TEXT("r.ProfileGPU.Sort"),	
	0,
	TEXT("Sorts the TTY Dump independently at each level of the tree in various modes.\n")
	TEXT("0 : Chronological\n")
	TEXT("1 : By time elapsed\n")
	TEXT("2 : By number of prims\n")
	TEXT("3 : By number of verts\n"),
	ECVF_Default);

#if (RHI_NEW_GPU_PROFILER == 0)

/** Recursively generates a histogram of nodes and stores their timing in TimingResult. */
static void GatherStatsEventNode(FGPUProfilerEventNode* Node, int32 Depth, TMap<FString, FGPUProfilerEventNodeStats>& EventHistogram)
{
	if (Node->NumDraws > 0 || Node->NumDispatches > 0 || Node->Children.Num() > 0)
	{
		Node->TimingResult = Node->GetTiming() * 1000.0f;
		Node->NumTotalDraws = Node->NumDraws;
		Node->NumTotalDispatches = Node->NumDispatches;
		Node->NumTotalPrimitives = Node->NumPrimitives;
		Node->NumTotalVertices = Node->NumVertices;

		FGPUProfilerEventNode* Parent = Node->Parent;		
		while (Parent)
		{
			Parent->NumTotalDraws += Node->NumDraws;
			Parent->NumTotalDispatches += Node->NumDispatches;
			Parent->NumTotalPrimitives += Node->NumPrimitives;
			Parent->NumTotalVertices += Node->NumVertices;

			Parent = Parent->Parent;
		}

		for (int32 ChildIndex = 0; ChildIndex < Node->Children.Num(); ChildIndex++)
		{
			// Traverse children
			GatherStatsEventNode(Node->Children[ChildIndex], Depth + 1, EventHistogram);
		}

		FGPUProfilerEventNodeStats* FoundHistogramBucket = EventHistogram.Find(Node->Name);
		if (FoundHistogramBucket)
		{
			FoundHistogramBucket->NumDraws += Node->NumTotalDraws;
			FoundHistogramBucket->NumPrimitives += Node->NumTotalPrimitives;
			FoundHistogramBucket->NumVertices += Node->NumTotalVertices;
			FoundHistogramBucket->TimingResult += Node->TimingResult;
			FoundHistogramBucket->NumEvents++;
		}
		else
		{
			FGPUProfilerEventNodeStats NewNodeStats;
			NewNodeStats.NumDraws = Node->NumTotalDraws;
			NewNodeStats.NumPrimitives = Node->NumTotalPrimitives;
			NewNodeStats.NumVertices = Node->NumTotalVertices;
			NewNodeStats.TimingResult = Node->TimingResult;
			NewNodeStats.NumEvents = 1;
			EventHistogram.Add(Node->Name, NewNodeStats);
		}
	}
}

struct FGPUProfileInfoPair
{
	int64 Triangles;
	int32 DrawCalls;

	FGPUProfileInfoPair()
		: Triangles(0)
		, DrawCalls(0)
	{
	}

	void AddDraw(int64 InTriangleCount)
	{
		Triangles += InTriangleCount;
		++DrawCalls;
	}
};

struct FGPUProfileStatSummary
{
	TMap<FString, FGPUProfileInfoPair> TrianglesPerMaterial;
	TMap<FString, FGPUProfileInfoPair> TrianglesPerMesh;
	TMap<FString, FGPUProfileInfoPair> TrianglesPerNonMesh;

	int32 TotalNumNodes;
	int32 TotalNumDraws;

	bool bGatherSummaryStats;
	bool bDumpEventLeafNodes;

	FGPUProfileStatSummary()
		: TotalNumNodes(0)
		, TotalNumDraws(0)
		, bGatherSummaryStats(false)
		, bDumpEventLeafNodes(false)
	{
		bDumpEventLeafNodes = GProfileGPUShowEvents.GetValueOnRenderThread() != 0;
		bGatherSummaryStats = GProfilePrintAssetSummary.GetValueOnRenderThread() != 0;
	}

	void ProcessMatch(FGPUProfilerEventNode* Node)
	{
		if (bGatherSummaryStats && (Node->NumTotalPrimitives > 0) && (Node->NumTotalVertices > 0) && (Node->Children.Num() == 0))
		{
			FString MaterialPart;
			FString AssetPart;
			if (Node->Name.Split(TEXT(" "), &MaterialPart, &AssetPart, ESearchCase::CaseSensitive))
			{
				TrianglesPerMaterial.FindOrAdd(MaterialPart).AddDraw(Node->NumTotalPrimitives);
				TrianglesPerMesh.FindOrAdd(AssetPart).AddDraw(Node->NumTotalPrimitives);
			}
			else
			{
				TrianglesPerNonMesh.FindOrAdd(Node->Name).AddDraw(Node->NumTotalPrimitives);
			}
		}
	}

	void PrintSummary()
	{
		UE_LOG(LogRHI, Log, TEXT("Total Nodes %u Draws %u"), TotalNumNodes, TotalNumDraws);
		UE_LOG(LogRHI, Log, TEXT(""));
		UE_LOG(LogRHI, Log, TEXT(""));

		if (bGatherSummaryStats)
		{
			// Sort the lists and print them out
			TrianglesPerMesh.ValueSort([](const FGPUProfileInfoPair& A, const FGPUProfileInfoPair& B){ return A.Triangles > B.Triangles; });
			UE_LOG(LogRHI, Log, TEXT(""));
			UE_LOG(LogRHI, Log, TEXT("MeshList,TriangleCount,DrawCallCount"));
			for (auto& Pair : TrianglesPerMesh)
			{
				UE_LOG(LogRHI, Log, TEXT("%s,%d,%d"), *Pair.Key, Pair.Value.Triangles, Pair.Value.DrawCalls);
			}

			TrianglesPerMaterial.ValueSort([](const FGPUProfileInfoPair& A, const FGPUProfileInfoPair& B){ return A.Triangles > B.Triangles; });
			UE_LOG(LogRHI, Log, TEXT(""));
			UE_LOG(LogRHI, Log, TEXT("MaterialList,TriangleCount,DrawCallCount"));
			for (auto& Pair : TrianglesPerMaterial)
			{
				UE_LOG(LogRHI, Log, TEXT("%s,%d,%d"), *Pair.Key, Pair.Value.Triangles, Pair.Value.DrawCalls);
			}

			TrianglesPerNonMesh.ValueSort([](const FGPUProfileInfoPair& A, const FGPUProfileInfoPair& B){ return A.Triangles > B.Triangles; });
			UE_LOG(LogRHI, Log, TEXT(""));
			UE_LOG(LogRHI, Log, TEXT("MiscList,TriangleCount,DrawCallCount"));
			for (auto& Pair : TrianglesPerNonMesh)
			{
				UE_LOG(LogRHI, Log, TEXT("%s,%d,%d"), *Pair.Key, Pair.Value.Triangles, Pair.Value.DrawCalls);
			}

			// See if we want to call out any particularly interesting matches
			TArray<FString> InterestingSubstrings;
			GProfileAssetSummaryCallOuts.GetValueOnRenderThread().ParseIntoArray(InterestingSubstrings, TEXT(","), true);

			if (InterestingSubstrings.Num() > 0)
			{
				UE_LOG(LogRHI, Log, TEXT(""));
				UE_LOG(LogRHI, Log, TEXT("Information about specified mesh substring matches (r.ProfileGPU.AssetSummaryCallOuts)"));
				for (const FString& InterestingSubstring : InterestingSubstrings)
				{
					int32 InterestingNumDraws = 0;
					int64 InterestingNumTriangles = 0;

					for (auto& Pair : TrianglesPerMesh)
					{
						if (Pair.Key.Contains(InterestingSubstring))
						{
							InterestingNumDraws += Pair.Value.DrawCalls;
							InterestingNumTriangles += Pair.Value.Triangles;
						}
					}

					UE_LOG(LogRHI, Log, TEXT("Matching '%s': %d draw calls, with %d tris (%.2f M)"), *InterestingSubstring, InterestingNumDraws, InterestingNumTriangles, InterestingNumTriangles * 1e-6);
				}
				UE_LOG(LogRHI, Log, TEXT(""));
			}
		}
	}
};

/** Recursively dumps stats for each node with a depth first traversal. */
static void DumpStatsEventNode(FGPUProfilerEventNode* Node, float RootResult, int32 Depth, const FWildcardString& WildcardFilter, bool bParentMatchedFilter, float& ReportedTiming, FGPUProfileStatSummary& Summary)
{
	Summary.TotalNumNodes++;
	ReportedTiming = 0;

	if (Node->NumDraws > 0 || Node->NumDispatches > 0 || Node->Children.Num() > 0 || Summary.bDumpEventLeafNodes)
	{
		Summary.TotalNumDraws += Node->NumDraws;
		// Percent that this node was of the total frame time
		const float Percent = Node->TimingResult * 100.0f / (RootResult * 1000.0f);
		const float PercentThreshold = GProfileThresholdPercent.GetValueOnRenderThread();
		const int32 EffectiveDepth = FMath::Max(Depth - 1, 0);
		const bool bDisplayEvent = (bParentMatchedFilter || WildcardFilter.IsMatch(Node->Name)) && (Percent > PercentThreshold || Summary.bDumpEventLeafNodes);

		if (bDisplayEvent)
		{
			FString NodeStats = TEXT("");

			if (Node->NumTotalDraws > 0)
			{
				NodeStats = FString::Printf(TEXT("%u %s %u prims %u verts "), Node->NumTotalDraws, Node->NumTotalDraws == 1 ? TEXT("draw") : TEXT("draws"), Node->NumTotalPrimitives, Node->NumTotalVertices);
			}

			if (Node->NumTotalDispatches > 0)
			{
				NodeStats += FString::Printf(TEXT("%u %s"), Node->NumTotalDispatches, Node->NumTotalDispatches == 1 ? TEXT("dispatch") : TEXT("dispatches"));
			
				// Cumulative group stats are not meaningful, only include dispatch stats if there was one in the current node
				if (Node->GroupCount.X > 0 && Node->NumDispatches == 1)
				{
					NodeStats += FString::Printf(TEXT(" %u"), Node->GroupCount.X);

					if (Node->GroupCount.Y > 1)
					{
						NodeStats += FString::Printf(TEXT("x%u"), Node->GroupCount.Y);
					}

					if (Node->GroupCount.Z > 1)
					{
						NodeStats += FString::Printf(TEXT("x%u"), Node->GroupCount.Z);
					}

					NodeStats += TEXT(" groups");
				}
			}

			// Print information about this node, padded to its depth in the tree
			UE_LOG(LogRHI, Log, TEXT("%s%4.1f%%%5.2fms   %s %s"), 
				*FString(TEXT("")).LeftPad(EffectiveDepth * 3), 
				Percent,
				Node->TimingResult,
				*Node->Name,
				*NodeStats
				);

			ReportedTiming = Node->TimingResult;
			Summary.ProcessMatch(Node);
		}

		struct FCompareGPUProfileNode
		{
			EGPUProfileSortMode SortMode;
			FCompareGPUProfileNode(EGPUProfileSortMode InSortMode)
				: SortMode(InSortMode)
			{}
			FORCEINLINE bool operator()(const FGPUProfilerEventNode* A, const FGPUProfilerEventNode* B) const
			{
				switch (SortMode)
				{
					case EGPUProfileSortMode::ENumPrims:
						return B->NumTotalPrimitives < A->NumTotalPrimitives;
					case EGPUProfileSortMode::ENumVerts:
						return B->NumTotalVertices < A->NumTotalVertices;
					case EGPUProfileSortMode::ETimeElapsed:
					default:
						return B->TimingResult < A->TimingResult;
				}
			}
		};

		EGPUProfileSortMode SortMode = (EGPUProfileSortMode)FMath::Clamp(GProfileGPUSort.GetValueOnRenderThread(), 0, ((int32)EGPUProfileSortMode::EMax - 1));
		if (SortMode != EGPUProfileSortMode::EChronological)
		{
			Node->Children.Sort(FCompareGPUProfileNode(SortMode));
		}

		float TotalChildTime = 0;
		uint32 TotalChildDraws = 0;
		for (int32 ChildIndex = 0; ChildIndex < Node->Children.Num(); ChildIndex++)
		{
			FGPUProfilerEventNode* ChildNode = Node->Children[ChildIndex];

			// Traverse children			
			const int32 PrevNumDraws = Summary.TotalNumDraws;
			float ChildReportedTiming = 0;
			DumpStatsEventNode(Node->Children[ChildIndex], RootResult, Depth + 1, WildcardFilter, bDisplayEvent, ChildReportedTiming, Summary);
			const int32 NumChildDraws = Summary.TotalNumDraws - PrevNumDraws;

			TotalChildTime += ChildReportedTiming;
			TotalChildDraws += NumChildDraws;
		}

		const float UnaccountedTime = FMath::Max(Node->TimingResult - TotalChildTime, 0.0f);
		const float UnaccountedPercent = UnaccountedTime * 100.0f / (RootResult * 1000.0f);

		// Add an 'Other Children' node if necessary to show time spent in the current node that is not in any of its children
		if (bDisplayEvent && Node->Children.Num() > 0 && TotalChildDraws > 0 && (UnaccountedPercent > 2.0f || UnaccountedTime > .2f))
		{
			UE_LOG(LogRHI, Log, TEXT("%s%4.1f%%%5.2fms   Other Children"), 
				*FString(TEXT("")).LeftPad((EffectiveDepth + 1) * 3), 
				UnaccountedPercent,
				UnaccountedTime);
		}
	}
}

#if !UE_BUILD_SHIPPING

/**
 * Converts GPU profile data to Visualizer data
 *
 * @param InProfileData GPU profile data
 * @param OutVisualizerData Visualizer data
 */
static TSharedPtr< FVisualizerEvent > CreateVisualizerDataRecursively( const TRefCountPtr< class FGPUProfilerEventNode >& InNode, TSharedPtr< FVisualizerEvent > InParentEvent, const double InStartTimeMs, const double InTotalTimeMs )
{
	TSharedPtr< FVisualizerEvent > VisualizerEvent( new FVisualizerEvent( InStartTimeMs / InTotalTimeMs, InNode->TimingResult / InTotalTimeMs, InNode->TimingResult, 0, InNode->Name ) );
	VisualizerEvent->ParentEvent = InParentEvent;

	double ChildStartTimeMs = InStartTimeMs;
	for( int32 ChildIndex = 0; ChildIndex < InNode->Children.Num(); ChildIndex++ )
	{
		TRefCountPtr< FGPUProfilerEventNode > ChildNode = InNode->Children[ ChildIndex ];
		TSharedPtr< FVisualizerEvent > ChildEvent = CreateVisualizerDataRecursively( ChildNode, VisualizerEvent, ChildStartTimeMs, InTotalTimeMs );
		VisualizerEvent->Children.Add( ChildEvent );

		ChildStartTimeMs += ChildNode->TimingResult;
	}

	return VisualizerEvent;
}

/**
 * Converts GPU profile data to Visualizer data
 *
 * @param InProfileData GPU profile data
 * @param OutVisualizerData Visualizer data
 */
static TSharedPtr< FVisualizerEvent > CreateVisualizerData( const TArray<TRefCountPtr<class FGPUProfilerEventNode> >& InProfileData )
{
	// Calculate total time first
	double TotalTimeMs = 0.0;
	for( int32 Index = 0; Index < InProfileData.Num(); ++Index )
	{
		TotalTimeMs += InProfileData[ Index ]->TimingResult;
	}
	
	// Assumption: InProfileData contains only one (root) element. Otherwise an extra FVisualizerEvent root event is required.
	TSharedPtr< FVisualizerEvent > DummyRoot;
	// Recursively create visualizer event data.
	TSharedPtr< FVisualizerEvent > StatEvents( CreateVisualizerDataRecursively( InProfileData[0], DummyRoot, 0.0, TotalTimeMs ) );
	return StatEvents;
}

#endif

void FGPUProfilerEventNodeFrame::DumpEventTree()
{
	if (EventTree.Num() > 0)
	{
		float RootResult = GetRootTimingResults();

		FString ConfigString;

		if (GProfileGPURootCVar.GetValueOnRenderThread() != TEXT("*"))
		{
			ConfigString += FString::Printf(TEXT("Root filter: %s "), *GProfileGPURootCVar.GetValueOnRenderThread());
		}

		if (GProfileThresholdPercent.GetValueOnRenderThread() > 0.0f)
		{
			ConfigString += FString::Printf(TEXT("Threshold: %.2f%% "), GProfileThresholdPercent.GetValueOnRenderThread());
		}

		if (ConfigString.Len() > 0)
		{
			ConfigString = FString(TEXT(", ")) + ConfigString;
		}

		UE_LOG(LogRHI, Log, TEXT("Perf marker hierarchy, total GPU time %.2fms%s"), RootResult * 1000.0f, *ConfigString);
		UE_LOG(LogRHI, Log, TEXT(""));

		// Display a warning if this is a GPU profile and the GPU was profiled with v-sync enabled
		FText VsyncEnabledWarningText = FText::GetEmpty();
		static IConsoleVariable* CVSyncVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
		if (CVSyncVar->GetInt() != 0 && !PlatformDisablesVSync())
		{
			VsyncEnabledWarningText = LOCTEXT("GpuProfileVsyncEnabledWarning", "WARNING: This GPU profile was captured with v-sync enabled.  V-sync wait time may show up in any bucket, and as a result the data in this profile may be skewed. Please profile with v-sync disabled to obtain the most accurate data.");
			UE_LOG(LogRHI, Log, TEXT("%s"), *(VsyncEnabledWarningText.ToString()));
		}

		LogDisjointQuery();

		TMap<FString, FGPUProfilerEventNodeStats> EventHistogram;
		for (int32 BaseNodeIndex = 0; BaseNodeIndex < EventTree.Num(); BaseNodeIndex++)
		{
			GatherStatsEventNode(EventTree[BaseNodeIndex], 0, EventHistogram);
		}

		static IConsoleVariable* CVar2 = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ProfileGPU.Root"));
		FString RootWildcardString = CVar2->GetString(); 
		FWildcardString RootWildcard(RootWildcardString);

		FGPUProfileStatSummary Summary;
		for (int32 BaseNodeIndex = 0; BaseNodeIndex < EventTree.Num(); BaseNodeIndex++)
		{
			float Unused = 0;
			DumpStatsEventNode(EventTree[BaseNodeIndex], RootResult, 0, RootWildcard, false, Unused, /*inout*/ Summary);
		}
		Summary.PrintSummary();

		const bool bShowHistogram = GProfileShowEventHistogram.GetValueOnRenderThread() != 0;

		if (RootWildcardString == TEXT("*") && bShowHistogram)
		{
			struct FNodeStatsCompare
			{
				/** Sorts nodes by descending durations. */
				FORCEINLINE bool operator()(const FGPUProfilerEventNodeStats& A, const FGPUProfilerEventNodeStats& B) const
				{
					return B.TimingResult < A.TimingResult;
				}
			};

			// Sort descending based on node duration
			EventHistogram.ValueSort( FNodeStatsCompare() );

			// Log stats about the node histogram
			UE_LOG(LogRHI, Log, TEXT("Node histogram %u buckets"), EventHistogram.Num());

			static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ProfileGPU.Pattern"));

			// bad: reading on render thread but we don't support ECVF_RenderThreadSafe on strings yet
			// It's very unlikely to cause a problem as the cvar is only changes by the user.
			FString WildcardString = CVar->GetString(); 

			FGPUProfilerEventNodeStats Sum;

			const float ThresholdInMS = 5.0f;

			if(WildcardString == FString(TEXT("*")))
			{
				// disable Wildcard functionality
				WildcardString.Empty();
			}

			if(WildcardString.IsEmpty())
			{
				UE_LOG(LogRHI, Log, TEXT(" r.ProfileGPU.Pattern = '*' (using threshold of %g ms)"), ThresholdInMS);
			}
			else
			{
				UE_LOG(LogRHI, Log, TEXT(" r.ProfileGPU.Pattern = '%s' (not using time threshold)"), *WildcardString);
			}

			FWildcardString Wildcard(WildcardString);

			int32 NumNotShown = 0;
			for (TMap<FString, FGPUProfilerEventNodeStats>::TIterator It(EventHistogram); It; ++It)
			{
				const FGPUProfilerEventNodeStats& NodeStats = It.Value();

				bool bDump = NodeStats.TimingResult > RootResult * ThresholdInMS;

				if(!Wildcard.IsEmpty())
				{
					// if a Wildcard string was specified, we want to always dump all entries
					bDump = Wildcard.IsMatch(*It.Key());
				}

				if (bDump)
				{
					UE_LOG(LogRHI, Log, TEXT("   %.2fms   %s   Events %u   Draws %u"), NodeStats.TimingResult, *It.Key(), NodeStats.NumEvents, NodeStats.NumDraws);
					Sum += NodeStats;
				}
				else
				{
					NumNotShown++;
				}
			}

			UE_LOG(LogRHI, Log, TEXT("   Total %.2fms   Events %u   Draws %u,    %u buckets not shown"), 
				Sum.TimingResult, Sum.NumEvents, Sum.NumDraws, NumNotShown);
		}

#if !UE_BUILD_SHIPPING
		// Create and display profile visualizer data
		if (RHIConfig::ShouldShowProfilerAfterProfilingGPU())
		{

		// execute on main thread
			{
				struct FDisplayProfilerVisualizer
				{
					void Thread( TSharedPtr<FVisualizerEvent> InVisualizerData, const FText InVsyncEnabledWarningText )
					{
						static FName ProfileVisualizerModule(TEXT("ProfileVisualizer"));			
						if (FModuleManager::Get().IsModuleLoaded(ProfileVisualizerModule))
						{
							IProfileVisualizerModule& ProfileVisualizer = FModuleManager::GetModuleChecked<IProfileVisualizerModule>(ProfileVisualizerModule);
							// Display a warning if this is a GPU profile and the GPU was profiled with v-sync enabled (otherwise InVsyncEnabledWarningText is empty)
							ProfileVisualizer.DisplayProfileVisualizer( InVisualizerData, TEXT("GPU"), InVsyncEnabledWarningText, FLinearColor::Red );
						}
					}
				} DisplayProfilerVisualizer;

				TSharedPtr<FVisualizerEvent> VisualizerData = CreateVisualizerData( EventTree );

				DECLARE_CYCLE_STAT(TEXT("FSimpleDelegateGraphTask.DisplayProfilerVisualizer"),
					STAT_FSimpleDelegateGraphTask_DisplayProfilerVisualizer,
					STATGROUP_TaskGraphTasks);

				FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
					FSimpleDelegateGraphTask::FDelegate::CreateRaw(&DisplayProfilerVisualizer, &FDisplayProfilerVisualizer::Thread, VisualizerData, VsyncEnabledWarningText),
					GET_STATID(STAT_FSimpleDelegateGraphTask_DisplayProfilerVisualizer), nullptr, ENamedThreads::GameThread
				);
			}

			
		}
#endif
	}
}

void FGPUProfiler::PushEvent(const TCHAR* Name, FColor Color)
{
	if (bTrackingEvents)
	{
		check(StackDepth >= 0);
		StackDepth++;

		check(IsInRenderingThread() || IsInRHIThread());
		if (CurrentEventNode)
		{
			// Add to the current node's children
			CurrentEventNode->Children.Add(CreateEventNode(Name, CurrentEventNode));
			CurrentEventNode = CurrentEventNode->Children.Last();
		}
		else
		{
			// Add a new root node to the tree
			CurrentEventNodeFrame->EventTree.Add(CreateEventNode(Name, NULL));
			CurrentEventNode = CurrentEventNodeFrame->EventTree.Last();
		}

		check(CurrentEventNode);
		// Start timing the current node
		CurrentEventNode->StartTiming();
	}
}

void FGPUProfiler::PopEvent()
{
	if (bTrackingEvents)
	{
		check(StackDepth >= 1);
		StackDepth--;

		check(CurrentEventNode && (IsInRenderingThread() || IsInRHIThread()));
		// Stop timing the current node and move one level up the tree
		CurrentEventNode->StopTiming();
		CurrentEventNode = CurrentEventNode->Parent;
	}
}

/** Whether GPU timing measurements are supported by the driver. */
bool FGPUTiming::GIsSupported = false;

/** Frequency for the timing values, in number of ticks per seconds, or 0 if the feature isn't supported. */
TStaticArray<uint64, MAX_NUM_GPUS> FGPUTiming::GTimingFrequency(InPlace, 0);

/**
* Two timestamps performed on GPU and CPU at nearly the same time.
* This can be used to visualize GPU and CPU timing events on the same timeline.
*/
TStaticArray<FGPUTimingCalibrationTimestamp, MAX_NUM_GPUS> FGPUTiming::GCalibrationTimestamp;

/** Whether the static variables have been initialized. */
bool FGPUTiming::GAreGlobalsInitialized = false;

#else

// Temporary. Adds Insights markers for the 0th GPU graphics queue
// until we have a new API that is capable of displaying more info.
#define RHI_TEMP_USE_GPU_TRACE (1 && GPUPROFILERTRACE_ENABLED)

// When enabled, and running with a single GPU, repurposes the "GPU2" track
// in Insights to show the single GPU's async compute queue.
#define RHI_TEMP_USE_TRACK2_FOR_COMPUTE (1 && RHI_TEMP_USE_GPU_TRACE)

namespace UE::RHI::GPUProfiler
{
	static TArray<FEventSink*>& GetSinks()
	{
		static TArray<FEventSink*> Sinks;
		return Sinks;
	}

	FEventSink::FEventSink()
	{
		GetSinks().Add(this);
	}

	FEventSink::~FEventSink()
	{
		GetSinks().RemoveSingle(this);
	}

	void ProcessEvents(FQueue Queue, FEventStream EventStream)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UE::RHI::GPUProfiler::ProcessEvents);

		if (!EventStream.IsEmpty())
		{
			for (FEventSink* Sink : GetSinks())
			{
				Sink->ProcessEvents(Queue, EventStream);
			}
		}
	}

	void InitializeQueues(TConstArrayView<FQueue> Queues)
	{
		for (FEventSink* Sink : GetSinks())
		{
			Sink->InitializeQueues(Queues);
		}
	}

	// Handles computing the "stat unit" GPU time, and "stat gpu" stats.
	struct FGPUProfilerSink_StatSystem final : public FEventSink
	{
		class FTimestampStream
		{
		private:
			TArray<uint64> Values;

		public:
			struct FState
			{
				FTimestampStream const& Stream;
				int32 TimestampIndex = 0;
				uint64 BusyCycles = 0;

				FState(FTimestampStream const& Stream)
					: Stream(Stream)
				{}

				uint64 GetCurrentTimestamp (uint64 Anchor) const { return Stream.Values[TimestampIndex] - Anchor; }
				uint64 GetPreviousTimestamp(uint64 Anchor) const { return Stream.Values[TimestampIndex - 1] - Anchor; }

				bool HasMoreTimestamps() const { return TimestampIndex < Stream.Values.Num(); }
				bool IsStartingWork   () const { return (TimestampIndex & 0x01) == 0x00; }
				void AdvanceTimestamp () { TimestampIndex++; }
			};

			void AddTimestamp(uint64 Value, bool bBegin)
			{
				if (bBegin)
				{
					if (!Values.IsEmpty() && Value <= Values.Last())
					{
						//
						// The Begin TOP event is sooner than the last End BOP event.
						// The markers overlap, and the GPU was not idle.
						// 
						// Remove the previous End event, and discard this Begin event.
						//
						Values.RemoveAt(Values.Num() - 1, EAllowShrinking::No);
					}
					else
					{
						// GPU was idle. Keep this timestamp.
						Values.Add(Value);
					}
				}
				else
				{
					Values.Add(Value);
				}
			}

			static uint64 ComputeUnion(TArrayView<FTimestampStream::FState> Streams)
			{
				// The total number of cycles where at least one GPU pipe was busy.
				uint64 UnionBusyCycles = 0;

				uint64 LastMinCycles = 0;
				int32 BusyPipes = 0;
				bool bFirst = true;

				uint64 Anchor = 0; // @todo - handle possible timestamp wraparound

				// Process the time ranges from each pipe.
				while (true)
				{
					// Find the next minimum timestamp
					FTimestampStream::FState* NextMin = nullptr;
					for (auto& Current : Streams)
					{
						if (Current.HasMoreTimestamps() && (!NextMin || Current.GetCurrentTimestamp(Anchor) < NextMin->GetCurrentTimestamp(Anchor)))
						{
							NextMin = &Current;
						}
					}

					if (!NextMin)
						break; // No more timestamps to process

					if (!bFirst)
					{
						if (BusyPipes > 0 && NextMin->GetCurrentTimestamp(Anchor) > LastMinCycles)
						{
							// Accumulate the union busy time across all pipes
							UnionBusyCycles += NextMin->GetCurrentTimestamp(Anchor) - LastMinCycles;
						}

						if (!NextMin->IsStartingWork())
						{
							// Accumulate the busy time for this pipe specifically.
							NextMin->BusyCycles += NextMin->GetCurrentTimestamp(Anchor) - NextMin->GetPreviousTimestamp(Anchor);
						}
					}

					LastMinCycles = NextMin->GetCurrentTimestamp(Anchor);

					BusyPipes += NextMin->IsStartingWork() ? 1 : -1;
					check(BusyPipes >= 0);

					NextMin->AdvanceTimestamp();
					bFirst = false;
				}

				check(BusyPipes == 0);

				return UnionBusyCycles;
			}
		};

		struct FQueueTimestamps
		{
			FTimestampStream Queue;

		#if WITH_RHI_BREADCRUMBS
			TMap<FRHIBreadcrumbData_Stats, FTimestampStream> Stats;
		#endif
		};

		struct FQueueState
		{
			FQueue::EType Type;

			bool bBusy = false;
			FQueueTimestamps Timestamps;

		#if WITH_RHI_BREADCRUMBS
			TMap<FRHIBreadcrumbData_Stats, int32> ActiveStats;
			FRHIBreadcrumbNode* Breadcrumb = nullptr;
		#endif

			FQueueState(FQueue const& Queue)
				: Type(Queue.Type)
			{}
		};

		using FFrameState = TMap<FQueue, FQueueTimestamps>;

		TMap<FQueue, FQueueState> QueueStates;
		TMap<uint32, FFrameState> Frames;

	#if RHI_TEMP_USE_GPU_TRACE
		struct FInsightsTrack
		{
			uint32 const Index;
			uint64 MaxTraceTime = 0;
			uint32 FrameNumber = 0;

		#if DO_CHECK
			int32 EventCounter = 0;
		#endif

			FInsightsTrack(uint32 Index)
				: Index(Index)
			{}

			uint64 GPUToTrace(uint64 GPUTimestamp)
			{
				uint64 TraceTime = (uint64)(FPlatformTime::ToMilliseconds64(GPUTimestamp) * 1000.0);

				//
				// Some platforms support top-of-pipe timestamps, meaning BeginWork/BeginBreadcrumb events
				// that occur logically after EndWork/EndBreadcrumb events in the command stream can have
				// a timestamp that is earlier than the subsequent begin event due the GPU workload overlap.
				//
				// The old Insights API cannot support this, and simply doesn't display the events if their
				// timestamps aren't strictly sequential. Work around this by emitting the Max() of the current
				// timestamp, and the largest timestamp we've seen before.
				//

				MaxTraceTime = FMath::Max(TraceTime, MaxTraceTime);
				return MaxTraceTime;
			}

			bool bShowWork = false;
			bool bEmittedGPUWorkName = false;

			bool bNeedsEnd = false;
			uint64 MaxEndTimeBOP = 0;
			TOptional<uint64> LastBeginTimestampTOP;

			// Emitting FEndWork events to Insights are deferred until we know there isn't an overlapping FBeginWork event
			// that would otherwise prevent the GPU going idle. This is done to coalesce markers to make them less noisy.
			bool EmitEndWork(FQueueState const& QueueState)
			{
				bool bEmitEnd = bNeedsEnd && (!LastBeginTimestampTOP.IsSet() || LastBeginTimestampTOP.GetValue() > MaxEndTimeBOP);
				bool bNeedsBegin = !bNeedsEnd || bEmitEnd;

				if (bEmitEnd)
				{
					uint64 TraceTime = GPUToTrace(MaxEndTimeBOP);

				#if WITH_RHI_BREADCRUMBS
					for (FRHIBreadcrumbNode* Current = QueueState.Breadcrumb; Current; Current = Current->GetParent())
					{
						check(EventCounter-- > 0);
						FGpuProfilerTrace::EndEvent(TraceTime, Index);
					}
				#endif

					if (bShowWork)
					{
						check(EventCounter-- > 0);
						FGpuProfilerTrace::EndEvent(TraceTime, Index); // GPUWork event
					}

					bNeedsEnd = false;
				}

				return bNeedsBegin;
			}

			void EmitBeginWork(FQueueState const& QueueState)
			{
				if (LastBeginTimestampTOP.IsSet())
				{
					uint64 TraceTime = GPUToTrace(LastBeginTimestampTOP.GetValue());
							
					if (bShowWork)
					{
						static FName GraphicsWorkName("Graphics Work");
						static FName ComputeWorkName("Compute Work");
						FName WorkName = QueueState.Type == FQueue::EType::Graphics
							? GraphicsWorkName
							: ComputeWorkName;

						if (!bEmittedGPUWorkName)
						{
							FGpuProfilerTrace::SpecifyEventByName(WorkName, Index);
						}

						FGpuProfilerTrace::BeginEventByName(WorkName, FrameNumber, TraceTime, Index);
						check(++EventCounter);
					}

				#if WITH_RHI_BREADCRUMBS
					{
						FRHIBreadcrumb::FBuffer Buffer;
						auto Recurse = [&](auto& Recurse, FRHIBreadcrumbNode* Current) -> void
						{
							if (!Current)
								return;

							Recurse(Recurse, Current->GetParent());

							FName Name(Current->Name.GetTCHAR(Buffer));
							FGpuProfilerTrace::BeginEventByName(Name, FrameNumber, TraceTime, Index);
							check(++EventCounter);
						};
						Recurse(Recurse, QueueState.Breadcrumb);
					}
				#endif

					LastBeginTimestampTOP.Reset();
				}
			}

			void BeginWork(FQueueState const& QueueState, FEvent::FBeginWork const& BeginWork)
			{
				LastBeginTimestampTOP = BeginWork.GPUTimestampTOP;

				if (bShowWork)
				{
					if (EmitEndWork(QueueState))
					{
						EmitBeginWork(QueueState);
					}
				}
			}

			void EndWork(FQueueState const& QueueState, FEvent::FEndWork const& EndWork)
			{
				MaxEndTimeBOP = FMath::Max(MaxEndTimeBOP, EndWork.GPUTimestampBOP);
				bNeedsEnd = true;
			}

			void BeginBreadcrumb(FQueueState const& QueueState, FEvent::FBeginBreadcrumb const& BeginBreadcrumb)
			{
				FRHIBreadcrumb::FBuffer Buffer;
				TCHAR const* Str = BeginBreadcrumb.Breadcrumb->Name.GetTCHAR(Buffer);
				FName Name(Str);

				FGpuProfilerTrace::SpecifyEventByName(Name, Index);
				FGpuProfilerTrace::BeginEventByName(Name, FrameNumber, GPUToTrace(BeginBreadcrumb.GPUTimestampTOP), Index);
				check(++EventCounter);
			}

			void EndBreadcrumb(FQueueState const& QueueState, FEvent::FEndBreadcrumb const& EndBreadcrumb)
			{
				check(EventCounter-- > 0);
				FGpuProfilerTrace::EndEvent(GPUToTrace(EndBreadcrumb.GPUTimestampBOP), Index);
			}

			void FrameBoundary(FQueueState const& QueueState, FEvent::FFrameBoundary const& FrameBoundary)
			{
				// End the current Insights GPU frame + start the next one

				// All breadcrumbs must be ended before the frame boundary can be emitted.
				LastBeginTimestampTOP.Reset();
				bool bNeedsBegin = EmitEndWork(QueueState);

				check(FrameNumber == FrameBoundary.FrameNumber);
				check(EventCounter == 0);
				FGpuProfilerTrace::EndFrame(Index);

				FrameNumber++;

				// Use 1,1 calibration to disable any adjustments Insights makes.
				// The timestamps we use in the GPU event stream are already in the CPU clock domain.
				FGPUTimingCalibrationTimestamp Calibration{ 1, 1 };
				FGpuProfilerTrace::BeginFrame(Calibration, Index);

				if (!bShowWork)
				{
					if (bNeedsBegin)
					{
						LastBeginTimestampTOP = MaxEndTimeBOP;
						EmitBeginWork(QueueState);
					}
				}
			}

			void Initialize()
			{
				// When enabled, adds "GPUWork" markers to the GPU Insights trace to show where the GPU is busy or idle.
				// Causes breadcrumbs to be pushed / popped multiple times, breaking them up on the timeline.
				bShowWork = FParse::Param(FCommandLine::Get(), TEXT("tracegpuwork"));

				// Use 1,1 calibration to disable any adjustments Insights makes.
				// The timestamps we use in the GPU event stream are already in the CPU clock domain.
				FGPUTimingCalibrationTimestamp Calibration{ 1, 1 };
				check(EventCounter == 0);
				FGpuProfilerTrace::BeginFrame(Calibration, Index);
			}

		} InsightsTracks[2] { {0}, {1} };

		FInsightsTrack* GetInsightsTrack(FQueue const& Queue)
		{
			if (GNumExplicitGPUsForRendering > 1)
			{
				// MGPU Mode - GPU0 Graphics + GPU1 Graphics
				if (Queue.Type == FQueue::EType::Graphics && Queue.Index == 0 && Queue.GPU < 2)
				{
					return &InsightsTracks[Queue.GPU];
				}
			}
			else
			{
				// GPU0 Graphics + GPU0 Compute Mode
				if (Queue.GPU == 0 && Queue.Index == 0)
				{
					switch (Queue.Type)
					{
					case FQueue::EType::Graphics: return &InsightsTracks[0];
				#if RHI_TEMP_USE_TRACK2_FOR_COMPUTE
					case FQueue::EType::Compute : return &InsightsTracks[1];
				#endif
					}
				}
			}

			return nullptr;
		}

	#endif // RHI_TEMP_USE_GPU_TRACE

		void InitializeQueues(TConstArrayView<FQueue> Queues) override
		{
			for (FQueue const& Queue : Queues)
			{
				check(QueueStates.Find(Queue) == nullptr);
				QueueStates.Add(Queue, Queue);

			#if RHI_TEMP_USE_GPU_TRACE
				// Start the first Insights GPU frame
				FInsightsTrack* Track = GetInsightsTrack(Queue);
				if (Track)
				{
					Track->Initialize();
				}
			#endif
			}
		}

		void ProcessEvents(FQueue Queue, FEventStream const& EventStream) override
		{
		#if RHI_TEMP_USE_GPU_TRACE
			FInsightsTrack* Track = GetInsightsTrack(Queue);
		#endif
			FQueueState& QueueState = QueueStates.FindChecked(Queue);

			for (FEvent const* Event : EventStream)
			{
				switch (Event->GetType())
				{
				case FEvent::EType::BeginWork:
					{
						check(!QueueState.bBusy);
						QueueState.bBusy = true;

						FEvent::FBeginWork const& BeginWork = Event->Value.Get<FEvent::FBeginWork>();
						QueueState.Timestamps.Queue.AddTimestamp(BeginWork.GPUTimestampTOP, true);

					#if RHI_TEMP_USE_GPU_TRACE
						if (Track)
						{
							Track->BeginWork(QueueState, BeginWork);
						}
					#endif

					#if WITH_RHI_BREADCRUMBS
						// Apply the timestamp to all active stats
						for (auto const& [Stat, RefCount] : QueueState.ActiveStats)
						{
							QueueState.Timestamps.Stats.FindChecked(Stat).AddTimestamp(BeginWork.GPUTimestampTOP, true);
						}
					#endif
					}
					break;

				case FEvent::EType::EndWork:
					{
						check(QueueState.bBusy);
						QueueState.bBusy = false;

						FEvent::FEndWork const& EndWork = Event->Value.Get<FEvent::FEndWork>();
						QueueState.Timestamps.Queue.AddTimestamp(EndWork.GPUTimestampBOP, false);

					#if WITH_RHI_BREADCRUMBS
						// Apply the timestamp to all active stats
						for (auto const& [Stat, RefCount] : QueueState.ActiveStats)
						{
							QueueState.Timestamps.Stats.FindChecked(Stat).AddTimestamp(EndWork.GPUTimestampBOP, false);
						}
					#endif

					#if RHI_TEMP_USE_GPU_TRACE
						if (Track)
						{
							Track->EndWork(QueueState, EndWork);
						}
					#endif
					}
					break;

			#if WITH_RHI_BREADCRUMBS
				case FEvent::EType::BeginBreadcrumb:
					{
						check(QueueState.bBusy);

						FEvent::FBeginBreadcrumb const& BeginBreadcrumb = Event->Value.Get<FEvent::FBeginBreadcrumb>();
						FRHIBreadcrumbData_Stats const& Stat = BeginBreadcrumb.Breadcrumb->Name.Data;

					#if RHI_TEMP_USE_GPU_TRACE
						if (Track)
						{
							Track->BeginBreadcrumb(QueueState, BeginBreadcrumb);
						}
					#endif

						if (Stat.ShouldComputeStat())
						{
							// Disregard the stat if it is nested within itself (i.e. its already in the ActiveStats map with a non-zero ref count).
							// Only the outermost stat will count the busy time, otherwise we'd be double-counting the nested time.
							int32 RefCount = QueueState.ActiveStats.FindOrAdd(Stat)++;
							if (RefCount == 0)
							{
								QueueState.Timestamps.Stats.FindOrAdd(Stat).AddTimestamp(BeginBreadcrumb.GPUTimestampTOP, true);
							}
						}

						QueueState.Breadcrumb = BeginBreadcrumb.Breadcrumb;
					}
					break;

				case FEvent::EType::EndBreadcrumb:
					{
						check(QueueState.bBusy);

						FEvent::FEndBreadcrumb const& EndBreadcrumb = Event->Value.Get<FEvent::FEndBreadcrumb>();
						FRHIBreadcrumbData_Stats const& Stat = EndBreadcrumb.Breadcrumb->Name.Data;

					#if RHI_TEMP_USE_GPU_TRACE
						if (Track)
						{
							Track->EndBreadcrumb(QueueState, EndBreadcrumb);
						}
					#endif

						if (Stat.ShouldComputeStat())
						{
							// Pop the stat when the refcount hits zero.
							int32 RefCount = --QueueState.ActiveStats.FindChecked(Stat);
							if (RefCount == 0)
							{
								QueueState.Timestamps.Stats.FindChecked(Stat).AddTimestamp(EndBreadcrumb.GPUTimestampBOP, false);
								QueueState.ActiveStats.FindAndRemoveChecked(Stat);
							}							
						}

						QueueState.Breadcrumb = EndBreadcrumb.Breadcrumb->GetParent();
					}
					break;
			#endif // WITH_RHI_BREADCRUMBS

				case FEvent::EType::SignalFence:
					{
						check(!QueueState.bBusy);
						FEvent::FSignalFence const& SignalFence = Event->Value.Get<FEvent::FSignalFence>();
					}
					break;

				case FEvent::EType::WaitFence:
					{
						check(!QueueState.bBusy);
						FEvent::FWaitFence const& WaitFence = Event->Value.Get<FEvent::FWaitFence>();
					}
					break;

				case FEvent::EType::FrameBoundary:
					{
						check(!QueueState.bBusy);
						FEvent::FFrameBoundary const& FrameBoundary = Event->Value.Get<FEvent::FFrameBoundary>();

						FFrameState& FrameState = Frames.FindOrAdd(FrameBoundary.FrameNumber);
						FrameState.Emplace(Queue, MoveTemp(QueueState.Timestamps));

						// Reinsert timestamp streams for the current active stats on 
						// this queue, since these got moved into the frame state.
						for (auto& [Stat, RefCount] : QueueState.ActiveStats)
						{
							QueueState.Timestamps.Stats.FindOrAdd(Stat);
						}

						if (FrameState.Num() == QueueStates.Num())
						{
							// All registered queues have reported their frame boundary event.
							// We have a full set of data to compute the total frame GPU stats.
							ProcessFrame(FrameState);

							Frames.Remove(FrameBoundary.FrameNumber);
						}

					#if RHI_TEMP_USE_GPU_TRACE
						if (Track)
						{
							Track->FrameBoundary(QueueState, FrameBoundary);
						}
					#endif
					}
					break;
				}
			}
		}

		void ProcessFrame(FFrameState& FrameState)
		{
			TArray<FTimestampStream::FState, TInlineAllocator<GetRHIPipelineCount() * MAX_NUM_GPUS>> StreamPointers;

		#if CSV_PROFILER_STATS
			const bool bCsvStatsEnabled = !!CVarGPUCsvStatsEnabled.GetValueOnAnyThread();
			FCsvProfiler* CsvProfiler = bCsvStatsEnabled ? FCsvProfiler::Get() : nullptr;
		#else
			const bool bCsvStatsEnabled = false;
		#endif

			// Compute the individual GPU stats
		#if WITH_RHI_BREADCRUMBS

			TSet<FRHIBreadcrumbData_Stats> UniqueStats;
			for (auto const& [Queue, State] : FrameState)
			{
				for (auto const& [Stat, Timestamps] : State.Stats)
				{
					UniqueStats.Add(Stat);
				}
			}

			for (FRHIBreadcrumbData_Stats const& Stat : UniqueStats)
			{
				StreamPointers.Reset();
				for (auto const& [Queue, State] : FrameState)
				{
					FTimestampStream const* Stream = State.Stats.Find(Stat);
					if (Stream)
					{
						StreamPointers.Emplace(*Stream);
					}
				}

				uint64 Union = FTimestampStream::ComputeUnion(StreamPointers);
				double Milliseconds = FPlatformTime::ToMilliseconds64(Union);

				SET_FLOAT_STAT_FName(Stat.StatId.GetName(), Milliseconds);

			#if CSV_PROFILER_STATS
				if (CsvProfiler)
				{
					CsvProfiler->RecordCustomStat(Stat.CsvStat, CSV_CATEGORY_INDEX(GPU), Milliseconds, ECsvCustomStatOp::Set);
				}
			#endif
			}

		#endif // WITH_RHI_BREADCRUMBS

			// Compute the whole-frame total GPU time.
			StreamPointers.Reset();
			for (auto const& [Queue, State] : FrameState)
			{
				StreamPointers.Emplace(State.Queue);
			}
			uint64 WholeFrameUnion = FTimestampStream::ComputeUnion(StreamPointers);

			// Update the global GPU frame time stats - need to convert to Cycles32 rather than Cycles64.
			GGPUFrameTime = FPlatformMath::TruncToInt(FPlatformTime::ToSeconds64(WholeFrameUnion) / FPlatformTime::GetSecondsPerCycle());

			RHISetGPUStatTotals(bCsvStatsEnabled, FPlatformTime::ToMilliseconds64(WholeFrameUnion));
		}

	} GGPUProfilerSink_StatSystem;

	struct FGPUProfilerSink_ProfileGPU final : public FEventSink
	{
		struct FNode
		{
			FString Name;
			FNode* Parent = nullptr;
			FNode* Next = nullptr;
			uint32 Level = 0;
			TArray<uint64> Timestamps;

			uint32 NumDraws = 0;
			uint32 NumPrimitives = 0;

			uint64 BusyCycles = 0;

			FNode(FString&& Name)
				: Name(MoveTemp(Name))
			{}
		};

		struct FQueueState
		{
			FQueue const Queue;
			TArray<TUniquePtr<FNode>> Nodes;
			FNode* Current = nullptr;
			FNode* Prev = nullptr;
			FNode* First = nullptr;

			enum class EState
			{
				Idle,
				WaitingFrame,
				Active
			} State = EState::Idle;

			std::atomic<bool> bTriggerProfile { false };

			FQueueState(FQueue Queue)
				: Queue(Queue)
			{}

			void PushNode(FString&& Name)
			{
				FNode* Parent = Current;
				Current = Nodes.Emplace_GetRef(MakeUnique<FNode>(MoveTemp(Name))).Get();
				Current->Parent = Parent;

				if (!First)
				{
					First = Current;
				}

				if (Parent)
				{
					Current->Level = Parent->Level + 1;
				}

				if (Prev)
				{
					Prev->Next = Current;
				}
				Prev = Current;
			}

			void PopNode()
			{
				check(Current && Current->Parent);
				Current = Current->Parent;
			}

			void ProcessEvents(FEventStream const& EventStream);

			void LogTree(uint32 FrameNumber);
		};

		TMap<FQueue, TUniquePtr<FQueueState>> QueueStates;

		void InitializeQueues(TConstArrayView<FQueue> Queues) override
		{
			for (FQueue const& Queue : Queues)
			{
				check(QueueStates.Find(Queue) == nullptr);
				QueueStates.Add(Queue, MakeUnique<FQueueState>(Queue));
			}
		}

		void ProcessEvents(FQueue Queue, FEventStream const& EventStream) override
		{
			QueueStates.FindChecked(Queue)->ProcessEvents(EventStream);
		}

		void ProfileNextFrame()
		{
			for (auto& [Queue, State] : QueueStates)
			{
				State->bTriggerProfile = true;
			}
		}

	} GGPUProfilerSink_ProfileGPU;

	void FGPUProfilerSink_ProfileGPU::FQueueState::ProcessEvents(FEventStream const& EventStream)
	{
		auto Iterator = std::begin(EventStream);
		auto End = std::end(EventStream);

	Restart:
		if (State == EState::Idle && bTriggerProfile.exchange(false))
		{
			State = EState::WaitingFrame;
		}

		if (State == EState::WaitingFrame)
		{
			// Discard all received events until we reach a FrameBoundary event
			for (; Iterator != End; ++Iterator)
			{
				if ((*Iterator)->GetType() == FEvent::EType::FrameBoundary)
				{
					// Start profiling until we receive another FrameBoundary event
					State = EState::Active;

					FEvent::FFrameBoundary const& FrameBoundary = (*Iterator)->Value.Get<FEvent::FFrameBoundary>();

					// Build the node tree 
					PushNode(TEXT("<root>"));

				#if WITH_RHI_BREADCRUMBS
					auto Recurse = [&](auto& Recurse, FRHIBreadcrumbNode* Breadcrumb) -> void
					{
						if (!Breadcrumb)
						{
							return;
						}

						Recurse(Recurse, Breadcrumb->GetParent());

						FRHIBreadcrumb::FBuffer Buffer;
						PushNode(Breadcrumb->Name.GetTCHAR(Buffer));
					};
					Recurse(Recurse, FrameBoundary.Breadcrumb);
				#endif // WITH_RHI_BREADCRUMBS

					++Iterator;
					break;
				}
			}
		}

		if (State == EState::Active)
		{
			for (; Iterator != End; ++Iterator)
			{
				FEvent const* Event = *Iterator;

				switch (Event->GetType())
				{
				case FEvent::EType::BeginWork:
					{
						uint64 Timestamp = Event->Value.Get<FEvent::FBeginWork>().GPUTimestampTOP;
						for (FNode* Node = Current; Node; Node = Node->Parent)
						{
							Node->Timestamps.Add(Timestamp);
						}
					}
					break;

				case FEvent::EType::EndWork:
					{
						uint64 Timestamp = Event->Value.Get<FEvent::FEndWork>().GPUTimestampBOP;
						for (FNode* Node = Current; Node; Node = Node->Parent)
						{
							Node->Timestamps.Add(Timestamp);
						}
					}
					break;

			#if WITH_RHI_BREADCRUMBS
				case FEvent::EType::BeginBreadcrumb:
					{
						FEvent::FBeginBreadcrumb const& BeginBreadcrumb = Event->Value.Get<FEvent::FBeginBreadcrumb>();

						// Push a new node
						FRHIBreadcrumb::FBuffer Buffer;
						PushNode(BeginBreadcrumb.Breadcrumb->Name.GetTCHAR(Buffer));

						Current->Timestamps.Add(BeginBreadcrumb.GPUTimestampTOP);
					}
					break;

				case FEvent::EType::EndBreadcrumb:
					{
						FEvent::FEndBreadcrumb const& EndBreadcrumb = Event->Value.Get<FEvent::FEndBreadcrumb>();
						Current->Timestamps.Add(EndBreadcrumb.GPUTimestampBOP);

						PopNode();
					}
					break;
			#endif // WITH_RHI_BREADCRUMBS

				case FEvent::EType::Stats:
					{
						FEvent::FStats const& Stats = Event->Value.Get<FEvent::FStats>();
						Current->NumDraws += Stats.NumDraws;
						Current->NumPrimitives += Stats.NumPrimitives;
					}
					break;

				case FEvent::EType::FrameBoundary:
					{
						FEvent::FFrameBoundary const& FrameBoundary = Event->Value.Get<FEvent::FFrameBoundary>();
						LogTree(FrameBoundary.FrameNumber);

						// Reset tracking
						Nodes.Reset();
						Current = nullptr;
						Prev = nullptr;
						First = nullptr;

						State = EState::Idle;
						goto Restart;
					}
					break;
				}
			}
		}
	}

	template <uint32 Width>
	struct TUnicodeHorizontalBar
	{
		TCHAR Text[Width + 1];

		// 0 <= Value <= 1
		TUnicodeHorizontalBar(double Value)
		{
			TCHAR* Output = Text;
			int32 Solid, Partial, Blank;
			{
				double Integer;
				double Remainder = FMath::Modf(FMath::Clamp(Value, 0.0, 1.0) * Width, &Integer);

				Solid = (int32)Integer;
				Partial = (int32)FMath::Floor(Remainder * 8);
				Blank = (Width - Solid - (Partial > 0 ? 1 : 0));
			}

			// Solid characters
			for (int32 Index = 0; Index < Solid; ++Index)
			{
				*Output++ = TEXT('█');
			}

			// Partially filled character
			if (Partial > 0)
			{
				static constexpr TCHAR const Data[] = TEXT("▏▎▍▌▋▊▉");
				*Output++ = Data[Partial - 1];
			}

			// Blank Characters to pad out the width
			for (int32 Index = 0; Index < Blank; ++Index)
			{
				*Output++ = TEXT(' ');
			}

			*Output++ = 0;
			check(uintptr_t(Output) == (uintptr_t(Text) + sizeof(Text)));
		}
	};

	void FGPUProfilerSink_ProfileGPU::FQueueState::LogTree(uint32 FrameNumber)
	{
		for (FNode* Node = First; Node; Node = Node->Next)
		{
			check(Node->Timestamps.Num() % 2 == 0);
			Node->BusyCycles = 0;

			uint64 LastBeginCycles = 0;
			uint64 LastEndCycles = 0;

			for (int32 Index = 0; Index < Node->Timestamps.Num(); ++Index)
			{
				if ((Index & 1) == 0)
				{
					// Begin
					LastBeginCycles = FMath::Max(LastEndCycles, Node->Timestamps[Index]);
				}
				else
				{
					// End
					uint64 End = Node->Timestamps[Index];
					Node->BusyCycles += End - LastBeginCycles;

					LastEndCycles = End;
				}
			}
		}

		FString LogMessage;
		for (FNode* Node = First; Node; Node = Node->Next)
		{
			double Milliseconds = FPlatformTime::ToMilliseconds64(Node->BusyCycles);

			TUnicodeHorizontalBar<8> Bar = Milliseconds / FPlatformTime::ToMilliseconds64(First->BusyCycles);
			LogMessage += FString::Printf(TEXT("%9.3f ms |%s| %6d | %6d | %*s\n")
				, Milliseconds
				, Bar.Text
				, Node->NumDraws
				, Node->NumPrimitives
				, Node->Name.Len() + (Node->Level * 4)
				, *Node->Name
			);
		}

		UE_LOG(LogRHI, Display, TEXT("GPU Profile for Frame %d, Queue [%s, GPU: %d, Idx: %d]:\n%s\n\n"), FrameNumber, Queue.GetTypeString(), Queue.GPU, Queue.Index, *LogMessage);
	}

	static FAutoConsoleCommand ProfileGPUNew(
		TEXT("ProfileGPUNew"),
		TEXT(""),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			GGPUProfilerSink_ProfileGPU.ProfileNextFrame();
		}));

	TLockFreePointerListUnordered<void, PLATFORM_CACHE_LINE_SIZE> FEventStream::FChunk::MemoryPool;
}

#endif // RHI_NEW_GPU_PROFILER

#undef LOCTEXT_NAMESPACE
