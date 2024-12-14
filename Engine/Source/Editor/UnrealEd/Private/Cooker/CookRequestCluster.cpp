// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cooker/CookRequestCluster.h"

#include "Algo/AllOf.h"
#include "Algo/BinarySearch.h"
#include "Algo/Sort.h"
#include "Algo/TopologicalSort.h"
#include "Algo/Unique.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Async/Async.h"
#include "Cooker/CookDependency.h"
#include "Cooker/CookGenerationHelper.h"
#include "Cooker/CookPackageData.h"
#include "Cooker/CookPlatformManager.h"
#include "Cooker/CookProfiling.h"
#include "Cooker/CookRequests.h"
#include "Cooker/CookTypes.h"
#include "Cooker/PackageTracker.h"
#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "DerivedDataCache.h"
#include "DerivedDataRequestOwner.h"
#include "EditorDomain/EditorDomainUtils.h"
#include "Engine/AssetManager.h"
#include "Engine/Level.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/ITargetPlatform.h"
#include "Misc/RedirectCollector.h"
#include "Misc/ReverseIterate.h"
#include "Misc/ScopeExit.h"
#include "Misc/StringBuilder.h"
#include "String/Find.h"
#include "UObject/CoreRedirects.h"
#include "UObject/SavePackage.h"

namespace UE::Cook
{

FRequestCluster::FRequestCluster(UCookOnTheFlyServer& InCOTFS)
	: COTFS(InCOTFS)
	, PackageDatas(*InCOTFS.PackageDatas)
	, AssetRegistry(*IAssetRegistry::Get())
	, PackageTracker(*InCOTFS.PackageTracker)
	, BuildDefinitions(*InCOTFS.BuildDefinitions)
{
	// CookByTheBookOptions is always available; in other modes it is set to the default values
	UE::Cook::FCookByTheBookOptions& Options = *COTFS.CookByTheBookOptions;
	bAllowHardDependencies = !Options.bSkipHardReferences;
	bAllowSoftDependencies = !Options.bSkipSoftReferences;
	bErrorOnEngineContentUse = Options.bErrorOnEngineContentUse;
	if (COTFS.IsCookOnTheFlyMode())
	{
		// Do not queue soft-dependencies during CookOnTheFly; wait for them to be requested
		// TODO: Report soft dependencies separately, and mark them as normal priority,
		// and mark all hard dependencies as high priority in cook on the fly.
		bAllowSoftDependencies = false;
	}

	if (bErrorOnEngineContentUse)
	{
		DLCPath = FPaths::Combine(*COTFS.GetBaseDirectoryForDLC(), TEXT("Content"));
		FPaths::MakeStandardFilename(DLCPath);
	}
	GConfig->GetBool(TEXT("CookSettings"), TEXT("PreQueueBuildDefinitions"), bPreQueueBuildDefinitions, GEditorIni);

	bAllowIterativeResults = true;
	bool bFirst = true;
	for (const ITargetPlatform* TargetPlatform : COTFS.PlatformManager->GetSessionPlatforms())
	{
		FPlatformData* PlatformData = COTFS.PlatformManager->GetPlatformData(TargetPlatform);
		if (bFirst)
		{
			bAllowIterativeResults = PlatformData->bAllowIterativeResults;
			bFirst = false;
		}
		else
		{
			if (PlatformData->bAllowIterativeResults != bAllowIterativeResults)
			{
				UE_LOG(LogCook, Warning,
					TEXT("Full build is requested for some platforms but not others, but this is not supported. All platforms will be built full."));
				bAllowIterativeResults = false;
			}
		}
	}
}

FRequestCluster::FRequestCluster(UCookOnTheFlyServer& InCOTFS, TArray<FFilePlatformRequest>&& InRequests)
	: FRequestCluster(InCOTFS)
{
	ReserveInitialRequests(InRequests.Num());
	FilePlatformRequests = MoveTemp(InRequests);
	InRequests.Empty();
}

FRequestCluster::FRequestCluster(UCookOnTheFlyServer& InCOTFS, FPackageDataSet&& InRequests)
	: FRequestCluster(InCOTFS)
{
	ReserveInitialRequests(InRequests.Num());
	for (FPackageData* PackageData : InRequests)
	{
		check(PackageData);
		bool bExisted;
		SetPackageDataSuppressReason(*PackageData, ESuppressCookReason::NotSuppressed, &bExisted);
		check(!bExisted);
	}
	InRequests.Empty();
}

FRequestCluster::FRequestCluster(UCookOnTheFlyServer& InCOTFS, TRingBuffer<FDiscoveryQueueElement>& DiscoveryQueue)
	: FRequestCluster(InCOTFS)
{
	TArray<const ITargetPlatform*, TInlineAllocator<ExpectedMaxNumPlatforms>> BufferPlatforms;
	if (!COTFS.bSkipOnlyEditorOnly)
	{
		BufferPlatforms = COTFS.PlatformManager->GetSessionPlatforms();
		BufferPlatforms.Add(CookerLoadingPlatformKey);
	}

	while (!DiscoveryQueue.IsEmpty())
	{
		FDiscoveryQueueElement* Discovery = &DiscoveryQueue.First();
		FPackageData& PackageData = *Discovery->PackageData;

		TConstArrayView<const ITargetPlatform*> NewReachablePlatforms;
		if (COTFS.bSkipOnlyEditorOnly)
		{
			NewReachablePlatforms = Discovery->ReachablePlatforms.GetPlatforms(COTFS, &Discovery->Instigator,
				TConstArrayView<const ITargetPlatform*>(), &BufferPlatforms);
		}
		else
		{
			NewReachablePlatforms = BufferPlatforms;
		}
		if (Discovery->Instigator.Category == EInstigator::ForceExplorableSaveTimeSoftDependency)
		{
			// This package was possibly previously marked as not explorable, but now it is marked as explorable.
			// One example of this is externalactor packages - they are by default not cookable and not explorable
			// (see comment in FRequestCluster::IsRequestCookable). But once WorldPartition loads them, we need to mark
			// them as explored so that their imports are marked as expected and all of their soft dependencies
			// are included.
			for (const ITargetPlatform* TargetPlatform : NewReachablePlatforms)
			{
				if (TargetPlatform != CookerLoadingPlatformKey)
				{
					PackageData.FindOrAddPlatformData(TargetPlatform).MarkAsExplorable();
				}
			}
		}

		if (PackageData.HasReachablePlatforms(NewReachablePlatforms))
		{
			// If there are no new reachable platforms, add it to the cluster for cooking if it needs
			// it, otherwise let it remain where it is
			EUrgency Urgency = Discovery->Urgency;
			DiscoveryQueue.PopFrontValue();
			Discovery = nullptr;
			if (!PackageData.IsInProgress() && PackageData.GetPlatformsNeedingCookingNum() == 0)
			{
				PackageData.SendToState(EPackageState::Request, ESendFlags::QueueRemove,
					EStateChangeReason::RequestCluster);
				PackageData.RaiseUrgency(Urgency, ESendFlags::QueueNone);
				// SetPackageDataSuppressReason adds it in the proper container of *this
				SetPackageDataSuppressReason(PackageData, ESuppressCookReason::NotSuppressed);
			}
			else if (PackageData.IsInProgress())
			{
				PackageData.RaiseUrgency(Urgency, ESendFlags::QueueAddAndRemove);
			}
			continue;
		}

		// Startup packages and Generated packages are expected discovery types and do not need to add hidden
		// dependencies.
		if (Discovery->Instigator.Category != EInstigator::StartupPackage &&
			Discovery->Instigator.Category != EInstigator::GeneratedPackage)
		{
			// For unsolicited packages, we need to check load-reachability to decide whether the load was expected.
			bool bExpectedDiscoveryType = false;
			if (Discovery->Instigator.Category == EInstigator::Unsolicited)
			{
				bExpectedDiscoveryType = PackageData.FindOrAddPlatformData(CookerLoadingPlatformKey).IsReachable();
				if (bExpectedDiscoveryType && COTFS.bSkipOnlyEditorOnly)
				{
					// In SkipOnlyEditorOnly mode, expected-load unsolicited packages are skipped; merely loading
					// a package is not sufficient to add it to the cook. So take no action on this package.
					DiscoveryQueue.PopFrontValue();
					Discovery = nullptr;
					continue;
				}
			}
			else
			{
				// For other instigator types, the discovery is either expected or unexpected depending on type.
				// Adding packages to the cook should happen only for a few types of instigators, from external
				// package requests, or during cluster exploration. If not expected, add a diagnostic message.
				bExpectedDiscoveryType = Discovery->Instigator.Category == EInstigator::SaveTimeHardDependency ||
					Discovery->Instigator.Category == EInstigator::SaveTimeSoftDependency ||
					Discovery->Instigator.Category == EInstigator::ForceExplorableSaveTimeSoftDependency;
			}

			// If there are other discovered packages we have already added to this cluster, then defer this one
			// until we have explored those; add this one to the next cluster. Exploring those earlier discoveries
			// might add this one through cluster exploration and not require a hidden dependency.
			if (!OwnedPackageDatas.IsEmpty())
			{
				break;
			}

			if (Discovery->ReachablePlatforms.GetSource() == EDiscoveredPlatformSet::CopyFromInstigator)
			{
				// Add it as a hidden dependency so that future platforms discovered as reachable in
				// the instigator will also be marked as reachable in the dependency.
				if (COTFS.bSkipOnlyEditorOnly)
				{
					FPackageData* InstigatorPackageData = Discovery->Instigator.Referencer.IsNone() ? nullptr
						: COTFS.PackageDatas->TryAddPackageDataByPackageName(Discovery->Instigator.Referencer);
					if (InstigatorPackageData)
					{
						COTFS.DiscoveredDependencies.FindOrAdd(InstigatorPackageData->GetPackageName())
							.Add(PackageData.GetPackageName());
					}
				}
			}

			if (!bExpectedDiscoveryType)
			{
				COTFS.OnDiscoveredPackageDebug(PackageData.GetPackageName(), Discovery->Instigator);
			}
		}
		// Add the new reachable platforms
		PackageData.AddReachablePlatforms(*this, NewReachablePlatforms, MoveTemp(Discovery->Instigator));

		// Pop it off the list; note that this invalidates the pointers we had into the DiscoveryQueueElement
		FDiscoveryQueueElement PoppedDiscovery = DiscoveryQueue.PopFrontValue();
		Discovery = &PoppedDiscovery;
		NewReachablePlatforms = TConstArrayView<const ITargetPlatform*>();

		// Send it to the Request state if it's not already there, remove it from its old container
		// and add it to this cluster.
		PackageData.SendToState(EPackageState::Request, ESendFlags::QueueRemove, EStateChangeReason::RequestCluster);
		PackageData.RaiseUrgency(Discovery->Urgency, ESendFlags::QueueNone);
		// SetPackageDataSuppressReason adds it in the proper container of *this
		SetPackageDataSuppressReason(PackageData, ESuppressCookReason::NotSuppressed);
	}
}

FName GInstigatorRequestCluster(TEXT("RequestCluster"));

void FRequestCluster::Process(const FCookerTimer& CookerTimer, bool& bOutComplete)
{
	bOutComplete = true;

	FetchPackageNames(CookerTimer, bOutComplete);
	if (!bOutComplete)
	{
		return;
	}
	PumpExploration(CookerTimer, bOutComplete);
	if (!bOutComplete)
	{
		return;
	}
	StartAsync(CookerTimer, bOutComplete);
}

void FRequestCluster::FetchPackageNames(const FCookerTimer& CookerTimer, bool& bOutComplete)
{
	if (bPackageNamesComplete)
	{
		return;
	}

	constexpr int32 TimerCheckPeriod = 100; // Do not incur the cost of checking the timer on every package
	int32 NextRequest = 0;
	for (; NextRequest < FilePlatformRequests.Num(); ++NextRequest)
	{
		if ((NextRequest+1) % TimerCheckPeriod == 0 && CookerTimer.IsActionTimeUp())
		{
			break;
		}

		FFilePlatformRequest& Request = FilePlatformRequests[NextRequest];
		FName OriginalName = Request.GetFilename();

		// The input filenames are normalized, but might be missing their extension, so allow PackageDatas
		// to correct the filename if the package is found with a different filename
		bool bExactMatchRequired = false;
		FPackageData* PackageData = PackageDatas.TryAddPackageDataByStandardFileName(OriginalName,
			bExactMatchRequired);
		if (!PackageData)
		{
			LogCookerMessage(FString::Printf(TEXT("Could not find package at file %s!"),
				*OriginalName.ToString()), EMessageSeverity::Error);
			UE_LOG(LogCook, Error, TEXT("Could not find package at file %s!"), *OriginalName.ToString());
			FCompletionCallback CompletionCallback(MoveTemp(Request.GetCompletionCallback()));
			if (CompletionCallback)
			{
				CompletionCallback(nullptr);
			}
			continue;
		}

		// If it has new reachable platforms we definitely need to explore it
		if (!PackageData->HasReachablePlatforms(Request.GetPlatforms()))
		{
			PackageData->AddReachablePlatforms(*this, Request.GetPlatforms(), MoveTemp(Request.GetInstigator()));
			PullIntoCluster(*PackageData);
			if (Request.IsUrgent())
			{
				PackageData->SetUrgency(EUrgency::Blocking, ESendFlags::QueueNone);
			}
		}
		else
		{
			if (PackageData->IsInProgress())
			{
				// If it's already in progress with no new platforms, we don't need to add it to the cluster, but add
				// add on our urgency setting
				if (Request.IsUrgent())
				{
					PackageData->SetUrgency(EUrgency::Blocking, ESendFlags::QueueAddAndRemove);
				}
			}
			else if (PackageData->GetPlatformsNeedingCookingNum() > 0)
			{
				// If it's missing cookable platforms and not in progress we need to add it to the cluster for cooking
				PullIntoCluster(*PackageData);
				if (Request.IsUrgent())
				{
					PackageData->SetUrgency(EUrgency::Blocking, ESendFlags::QueueNone);
				}
			}
		}
		// Add on our completion callback, or call it immediately if already done
		PackageData->AddCompletionCallback(Request.GetPlatforms(), MoveTemp(Request.GetCompletionCallback()));
	}
	if (NextRequest < FilePlatformRequests.Num())
	{
		FilePlatformRequests.RemoveAt(0, NextRequest);
		bOutComplete = false;
		return;
	}

	FilePlatformRequests.Empty();
	bPackageNamesComplete = true;
}

void FRequestCluster::ReserveInitialRequests(int32 RequestNum)
{
	OwnedPackageDatas.Reserve(FMath::Max(RequestNum, 1024));
}

void FRequestCluster::PullIntoCluster(FPackageData& PackageData)
{
	bool bExisted;
	SetPackageDataSuppressReason(PackageData, ESuppressCookReason::NotSuppressed, &bExisted);
	if (!bExisted)
	{
		// Steal it from wherever it is and send it to Request State. It has already been added to this cluster
		if (PackageData.GetState() == EPackageState::Request)
		{
			COTFS.PackageDatas->GetRequestQueue().RemoveRequestExceptFromCluster(&PackageData, this);
		}
		else
		{
			PackageData.SendToState(EPackageState::Request, ESendFlags::QueueRemove,
				EStateChangeReason::RequestCluster);
		}
	}
}

void FRequestCluster::StartAsync(const FCookerTimer& CookerTimer, bool& bOutComplete)
{
	using namespace UE::DerivedData;
	using namespace UE::EditorDomain;

	if (bStartAsyncComplete)
	{
		return;
	}

	FEditorDomain* EditorDomain = FEditorDomain::Get();
	if (EditorDomain && EditorDomain->IsReadingPackages())
	{
		bool bBatchDownloadEnabled = true;
		GConfig->GetBool(TEXT("EditorDomain"), TEXT("BatchDownloadEnabled"), bBatchDownloadEnabled, GEditorIni);
		if (bBatchDownloadEnabled)
		{
			// If the EditorDomain is active, then batch-download all packages to cook from remote cache into local
			TArray<FName> BatchDownload;
			BatchDownload.Reserve(OwnedPackageDatas.Num());
			for (TPair<FPackageData*, FProcessingFlags>& Pair : OwnedPackageDatas)
			{
				if (Pair.Value.GetSuppressReason() == ESuppressCookReason::NotSuppressed)
				{
					BatchDownload.Add(Pair.Key->GetPackageName());
				}
			};
			EditorDomain->BatchDownload(BatchDownload);
		}
	}

	bStartAsyncComplete = true;
}

int32 FRequestCluster::NumPackageDatas() const
{
	return OwnedPackageDatas.Num();
}

void FRequestCluster::RemovePackageData(FPackageData* PackageData)
{
	FProcessingFlags RemovedFlags;
	if (!OwnedPackageDatas.RemoveAndCopyValue(PackageData, RemovedFlags))
	{
		return;
	}
	check(RemovedFlags.IsValid());
	if (RemovedFlags.ShouldMarkNotInProgress())
	{
		--PackagesToMarkNotInProgressCount;
	}
}

void FRequestCluster::SetPackageDataSuppressReason(FPackageData& PackageData, ESuppressCookReason Reason, bool* bOutExisted)
{
	check(Reason != ESuppressCookReason::Invalid);

	FProcessingFlags& Existing = OwnedPackageDatas.FindOrAdd(&PackageData);
	if (bOutExisted)
	{
		*bOutExisted = Existing.IsValid();
	}
	if (Existing.ShouldMarkNotInProgress())
	{
		--PackagesToMarkNotInProgressCount;
	}
	Existing.SetValid();
	Existing.SetSuppressReason(Reason);
	if (Existing.ShouldMarkNotInProgress())
	{
		++PackagesToMarkNotInProgressCount;
	}
}

void FRequestCluster::SetPackageDataWasMarkedCooked(FPackageData& PackageData, bool bValue,
	bool* bOutExisted)
{
	FProcessingFlags& Existing = OwnedPackageDatas.FindOrAdd(&PackageData);
	if (bOutExisted)
	{
		*bOutExisted = Existing.IsValid();
	}
	if (Existing.ShouldMarkNotInProgress())
	{
		--PackagesToMarkNotInProgressCount;
	}
	Existing.SetValid();
	Existing.SetWasMarkedCooked(bValue);
	if (Existing.ShouldMarkNotInProgress())
	{
		++PackagesToMarkNotInProgressCount;
	}
}

void FRequestCluster::OnNewReachablePlatforms(FPackageData* PackageData)
{
	if (GraphSearch)
	{
		GraphSearch->OnNewReachablePlatforms(PackageData);
	}
}

void FRequestCluster::OnPlatformAddedToSession(const ITargetPlatform* TargetPlatform)
{
	if (GraphSearch)
	{
		FCookerTimer CookerTimer(FCookerTimer::Forever);
		bool bComplete;
		while (PumpExploration(CookerTimer, bComplete), !bComplete)
		{
			UE_LOG(LogCook, Display, TEXT("Waiting for RequestCluster to finish before adding platform to session."));
			FPlatformProcess::Sleep(.001f);
		}
	}
}

void FRequestCluster::OnRemoveSessionPlatform(const ITargetPlatform* TargetPlatform)
{
	if (GraphSearch)
	{
		FCookerTimer CookerTimer(FCookerTimer::Forever);
		bool bComplete;
		while (PumpExploration(CookerTimer, bComplete), !bComplete)
		{
			UE_LOG(LogCook, Display,
				TEXT("Waiting for RequestCluster to finish before removing platform from session."));
			FPlatformProcess::Sleep(.001f);
		}
	}
}

void FRequestCluster::RemapTargetPlatforms(TMap<ITargetPlatform*, ITargetPlatform*>& Remap)
{
	if (GraphSearch)
	{
		// The platforms have already been invalidated, which means we can't wait for GraphSearch to finish
		// Need to wait for all async operations to finish, then remap all the platforms
		checkNoEntry(); // Not yet implemented
	}
}

bool FRequestCluster::Contains(FPackageData* PackageData) const
{
	return OwnedPackageDatas.Contains(PackageData);
}

void FRequestCluster::ClearAndDetachOwnedPackageDatas(TArray<FPackageData*>& OutRequestsToLoad,
	TArray<TPair<FPackageData*, ESuppressCookReason>>& OutRequestsToDemote,
	TMap<FPackageData*, TArray<FPackageData*>>& OutRequestGraph)
{
	if (bStartAsyncComplete)
	{
		check(!GraphSearch);
		OutRequestsToLoad.Reset();
		OutRequestsToDemote.Reset();
		for (TPair<FPackageData*, FProcessingFlags>& Pair : OwnedPackageDatas)
		{
			if (Pair.Value.GetSuppressReason() == ESuppressCookReason::NotSuppressed)
			{
				OutRequestsToLoad.Add(Pair.Key);
			}
			else
			{
				OutRequestsToDemote.Add({ Pair.Key, Pair.Value.GetSuppressReason() });
			}
		}
		OutRequestGraph = MoveTemp(RequestGraph);
	}
	else
	{
		OutRequestsToLoad.Reset();
		for (TPair<FPackageData*, FProcessingFlags>& Pair : OwnedPackageDatas)
		{
			OutRequestsToLoad.Add(Pair.Key);
		}
		OutRequestsToDemote.Reset();
		OutRequestGraph.Reset();
	}
	FilePlatformRequests.Empty();
	OwnedPackageDatas.Empty();
	PackagesToMarkNotInProgressCount = 0;
	GraphSearch.Reset();
	RequestGraph.Reset();
}

void FRequestCluster::PumpExploration(const FCookerTimer& CookerTimer, bool& bOutComplete)
{
	if (bDependenciesComplete)
	{
		return;
	}

	if (!GraphSearch)
	{
		ETraversalTier TraversalTier = ETraversalTier::None;
		if (COTFS.IsCookWorkerMode())
		{
			TraversalTier = ETraversalTier::None;
		}
		else
		{
			TraversalTier = bAllowHardDependencies ? ETraversalTier::FollowDependencies :
				ETraversalTier::FetchEdgeData;
		}
		GraphSearch.Reset(new FGraphSearch(*this, TraversalTier));

		if (TraversalTier == ETraversalTier::None)
		{
			GraphSearch->VisitWithoutDependencies();
			GraphSearch.Reset();
			bDependenciesComplete = true;
			return;
		}
		GraphSearch->StartSearch();
	}

	constexpr double WaitTime = 0.50;
	bool bDone;
	while (GraphSearch->TickExploration(bDone), !bDone)
	{
		GraphSearch->WaitForAsyncQueue(WaitTime);
		if (CookerTimer.IsActionTimeUp())
		{
			bOutComplete = false;
			return;
		}
	}

	TArray<FPackageData*> SortedPackages;
	SortedPackages.Reserve(OwnedPackageDatas.Num());
	for (TPair<FPackageData*, FProcessingFlags>& Pair : OwnedPackageDatas)
	{
		if (Pair.Value.GetSuppressReason() == ESuppressCookReason::NotSuppressed)
		{
			SortedPackages.Add(Pair.Key);
		}
	}

	// Sort the NewRequests in leaf to root order and replace the requests list with NewRequests
	TArray<FPackageData*> Empty;
	auto GetElementDependencies = [this, &Empty](FPackageData* PackageData) -> const TArray<FPackageData*>&
	{
		const TArray<FPackageData*>* VertexEdges = GraphSearch->GetGraphEdges().Find(PackageData);
		return VertexEdges ? *VertexEdges : Empty;
	};

	Algo::TopologicalSort(SortedPackages, GetElementDependencies, Algo::ETopologicalSort::AllowCycles);
	TMap<FPackageData*, int32> SortOrder;
	int32 Counter = 0;
	SortOrder.Reserve(SortedPackages.Num());
	for (FPackageData* PackageData : SortedPackages)
	{
		SortOrder.Add(PackageData, Counter++);
	}
	OwnedPackageDatas.KeySort([&SortOrder](const FPackageData& A, const FPackageData& B)
		{
			int32* CounterA = SortOrder.Find(&A);
			int32* CounterB = SortOrder.Find(&B);
			if ((CounterA != nullptr) != (CounterB != nullptr))
			{
				// Sort the demotes to occur last
				return CounterB == nullptr;
			}
			else if (CounterA)
			{
				return *CounterA < *CounterB;
			}
			else
			{
				return false; // demotes are unsorted
			}
		});

	RequestGraph = MoveTemp(GraphSearch->GetGraphEdges());
	GraphSearch.Reset();
	bDependenciesComplete = true;
}

FRequestCluster::FGraphSearch::FGraphSearch(FRequestCluster& InCluster, ETraversalTier InTraversalTier)
	: Cluster(InCluster)
	, TraversalTier(InTraversalTier)
	, ExploreEdgesContext(InCluster, *this)
	, AsyncResultsReadyEvent(EEventMode::ManualReset)
{
	AsyncResultsReadyEvent->Trigger();
	LastActivityTime = FPlatformTime::Seconds();
	VertexAllocator.SetMaxBlockSize(1024);
	VertexAllocator.SetMaxBlockSize(65536);
	BatchAllocator.SetMaxBlockSize(16);
	BatchAllocator.SetMaxBlockSize(16);

	TConstArrayView<const ITargetPlatform*> SessionPlatforms = Cluster.COTFS.PlatformManager->GetSessionPlatforms();
	check(SessionPlatforms.Num() > 0);
	FetchPlatforms.SetNum(SessionPlatforms.Num() + 2);
	FetchPlatforms[PlatformAgnosticPlatformIndex].bIsPlatformAgnosticPlatform = true;
	FetchPlatforms[CookerLoadingPlatformIndex].Platform = CookerLoadingPlatformKey;
	FetchPlatforms[CookerLoadingPlatformIndex].bIsCookerLoadingPlatform = true;
	for (int32 SessionPlatformIndex = 0; SessionPlatformIndex < SessionPlatforms.Num(); ++SessionPlatformIndex)
	{
		FFetchPlatformData& FetchPlatform = FetchPlatforms[SessionPlatformIndex + 2];
		FetchPlatform.Platform = SessionPlatforms[SessionPlatformIndex];
		FetchPlatform.Writer = &Cluster.COTFS.FindOrCreatePackageWriter(FetchPlatform.Platform);
	}
	Algo::Sort(FetchPlatforms, [](const FFetchPlatformData& A, const FFetchPlatformData& B)
		{
			return A.Platform < B.Platform;
		});
	check(FetchPlatforms[PlatformAgnosticPlatformIndex].bIsPlatformAgnosticPlatform);
	check(FetchPlatforms[CookerLoadingPlatformIndex].bIsCookerLoadingPlatform);
}

void FRequestCluster::FGraphSearch::VisitWithoutDependencies()
{
	// PumpExploration is responsible for marking all requests as explored and cookable/uncoookable.
	// If we're skipping the dependencies search, handle that responsibility for the initial requests and return.
	for (TPair<FPackageData*, FProcessingFlags>& Pair : Cluster.OwnedPackageDatas)
	{
		check(Pair.Key);
		FVertexData Vertex(Pair.Key->GetPackageName(), Pair.Key, *this);
		VisitVertex(Vertex);
	}
}

void FRequestCluster::FGraphSearch::StartSearch()
{
	VisitVertexQueue.Reserve(Cluster.OwnedPackageDatas.Num());
	for (TPair<FPackageData*, FProcessingFlags>& Pair : Cluster.OwnedPackageDatas)
	{
		FVertexData& Vertex = FindOrAddVertex(Pair.Key->GetPackageName(), *Pair.Key);
		check(Vertex.PackageData);
		Vertex.bPulledIntoCluster = true;
		AddToVisitVertexQueue(Vertex);
	}
}

FRequestCluster::FGraphSearch::~FGraphSearch()
{
	for (;;)
	{
		bool bHadActivity = false;
		bool bAsyncBatchesEmpty = false;
		{
			FScopeLock ScopeLock(&Lock);
			bAsyncBatchesEmpty = AsyncQueueBatches.IsEmpty();
			if (!bAsyncBatchesEmpty)
			{
				// It is safe to Reset AsyncResultsReadyEvent and wait on it later because we are inside the lock and
				// there is a remaining batch, so it will be triggered after the Reset when that batch completes.
				AsyncResultsReadyEvent->Reset();
			}
		}
		for (;;)
		{
			if (AsyncQueueResults.Dequeue())
			{
				bHadActivity = true;
			}
			else
			{
				break;
			}
		}
		if (bAsyncBatchesEmpty)
		{
			break;
		}
		if (bHadActivity)
		{
			LastActivityTime = FPlatformTime::Seconds();
		}
		else
		{
			UpdateDisplay();
		}
		constexpr double WaitTime = 1.0;
		WaitForAsyncQueue(WaitTime);
	}

	// Call the FVertexData destructors, but do not bother calling DeleteElement or Free on the VertexAllocator
	// since we are destructing the VertexAllocator.
	for (TPair<FName, FVertexData*>& VertexPair : this->Vertices)
	{
		FVertexData* VertexData = VertexPair.Value;
		VertexData->~FVertexData();
	}
	// Empty frees the struct memory for each FVertexData we allocated, but it does not call the destructor.
	VertexAllocator.Empty();
}

void FRequestCluster::FGraphSearch::OnNewReachablePlatforms(FPackageData* PackageData)
{
	FVertexData** VertexPtr = Vertices.Find(PackageData->GetPackageName());
	if (!VertexPtr)
	{
		return;
	}
	AddToVisitVertexQueue(**VertexPtr);
}

void FRequestCluster::FGraphSearch::QueueEdgesFetch(FVertexData& Vertex, TConstArrayView<int32> PlatformIndexes)
{
	check(Vertex.PackageData); // Caller must not call without a PackageData; doing so serves no purpose

	bool bAnyRequestedNeedsPlatformAgnostic = false;
	bool bAnyRequested = false;
	bool bAllHaveAlreadyCompletedFetch = true;

	for (int32 PlatformIndex : PlatformIndexes)
	{
		// The platform data may have already been requested; request it only if current status is NotRequested
		FQueryPlatformData& QueryData = Vertex.PlatformData[PlatformIndex];
		if (!QueryData.bSchedulerThreadFetchCompleted)
		{
			bAllHaveAlreadyCompletedFetch = false;
			EAsyncQueryStatus ExpectedStatus = EAsyncQueryStatus::NotRequested;
			if (QueryData.CompareExchangeAsyncQueryStatus(ExpectedStatus, EAsyncQueryStatus::SchedulerRequested))
			{
				bAnyRequested = true;
			}
		}
	}

	if (bAnyRequested)
	{
		PreAsyncQueue.Add(&Vertex);
		CreateAvailableBatches(false /* bAllowIncompleteBatch */);
	}

	if (bAllHaveAlreadyCompletedFetch)
	{
		// We are contractually obligated to kick the vertex. Normally we would put it into PreAsyncQueue and that
		// queue would take responsibility for kicking it. Also, it might still be in the AsyncQueueResults for one
		// of the platforms so it will be kicked by TickExplore pulling it out of the AsyncQueueResults. But if all
		// requested platforms already previously pulled it out of AsyncQueueResults, then we need to kick it again.
		KickVertex(&Vertex);
	}
}

void FRequestCluster::FGraphSearch::WaitForAsyncQueue(double WaitTimeSeconds)
{
	uint32 WaitTime = (WaitTimeSeconds > 0.0) ? static_cast<uint32>(FMath::Floor(WaitTimeSeconds * 1000)) : MAX_uint32;
	AsyncResultsReadyEvent->Wait(WaitTime);
}

void FRequestCluster::FGraphSearch::TickExploration(bool& bOutDone)
{
	bool bHadActivity = false;

	int32 RunawayLoopCount = 0;
	for (;;)
	{
		TOptional<FVertexData*> FrontVertex = AsyncQueueResults.Dequeue();
		if (!FrontVertex.IsSet())
		{
			break;
		}
		FVertexData* Vertex = *FrontVertex;
		for (FQueryPlatformData& PlatformData : GetPlatformDataArray(*Vertex))
		{
			if (!PlatformData.bSchedulerThreadFetchCompleted)
			{
				PlatformData.bSchedulerThreadFetchCompleted =
					PlatformData.GetAsyncQueryStatus() >= EAsyncQueryStatus::Complete;
				// Note that AsyncQueryStatus might change immediately after we read it, so we might have set
				// FetchCompleted=false but now AsyncQueryStatus is complete. In that case, whatever async thread
				// changed the AsyncQueryStatus will also kick the vertex again and we will detect the new value when
				// we reach the new value of the vertexdata later in AsyncQueueResults
			}
		}

		ExploreEdgesContext.Explore(*Vertex);
		bHadActivity = true;

		if (RunawayLoopCount++ > 2 * Vertices.Num())
		{
			UE_LOG(LogCook, Fatal, TEXT("Infinite loop detected in FRequestCluster::TickExploration's AsyncQueueResults."));
		}
	}

	RunawayLoopCount = 0;
	while (!VisitVertexQueue.IsEmpty())
	{
		bHadActivity = true;
		// VisitVertex might try to add other vertices onto VisitVertexQueue, so move it into a snapshot and process
		// the snapshot. After snapshot processing is done, add on anything that was added and then move it back.
		// We move it back even if it is empty so we can avoid reallocating when we add to it again later.
		TSet<FVertexData*> Snapshot = MoveTemp(VisitVertexQueue);
		VisitVertexQueue.Reset();
		for (FVertexData* Vertex : Snapshot)
		{
			VisitVertex(*Vertex);
		}
		Snapshot.Reset();
		Snapshot.Append(VisitVertexQueue);
		VisitVertexQueue = MoveTemp(Snapshot);

		if (RunawayLoopCount++ > 2 * Vertices.Num())
		{
			UE_LOG(LogCook, Fatal, TEXT("Infinite loop detected in FRequestCluster::TickExploration's VisitVertexQueue."));
		}
	}

	if (bHadActivity)
	{
		++RunAwayTickLoopCount;
		if (RunAwayTickLoopCount++ > 2 * Vertices.Num()*NumFetchPlatforms())
		{
			UE_LOG(LogCook, Fatal, TEXT("Infinite loop detected in reentrant calls to FRequestCluster::TickExploration."));
		}
		LastActivityTime = FPlatformTime::Seconds();
		bOutDone = false;
		return;
	}

	bool bAsyncQueueEmpty;
	{
		FScopeLock ScopeLock(&Lock);
		if (!AsyncQueueResults.IsEmpty())
		{
			bAsyncQueueEmpty = false;
		}
		else
		{
			bAsyncQueueEmpty = AsyncQueueBatches.IsEmpty();
			// AsyncResultsReadyEvent can only be Reset when either the AsyncQueue is empty or it is non-empty and we
			// know the AsyncResultsReadyEvent will be triggered again "later".
			// The guaranteed place where it will be Triggered is when a batch completes. To guarantee that
			// place will be called "later", the batch completion trigger and this reset have to both
			// be done inside the lock.
			AsyncResultsReadyEvent->Reset();
		}
	}
	if (!bAsyncQueueEmpty)
	{
		// Waiting on the AsyncQueue; give a warning if we have been waiting for long with no AsyncQueueResults.
		UpdateDisplay();
		bOutDone = false;
		return;
	}

	// No more work coming in the future from the AsyncQueue, and we are out of work to do
	// without it. If we have any queued vertices in the PreAsyncQueue, send them now and continue
	// waiting. Otherwise we are done.
	if (!PreAsyncQueue.IsEmpty())
	{
		CreateAvailableBatches(true /* bAllowInCompleteBatch */);
		bOutDone = false;
		return;
	}

	if (!VisitVertexQueue.IsEmpty() || !bAsyncQueueEmpty || !PreAsyncQueue.IsEmpty())
	{
		// A container ticked earlier was populated by the tick of a later container; restart tick from beginning
		bOutDone = false;
		return;
	}

	// We are out of direct dependency work to do, but there could be a cycle in the graph of
	// TransitiveBuildDependencies. If so, resolve the cycle and allow those vertices' edges to be explored.
	if (!PendingTransitiveBuildDependencyVertices.IsEmpty())
	{
		ResolveTransitiveBuildDependencyCycle();
		bOutDone = false;
		++RunAwayTickLoopCount;
		if (RunAwayTickLoopCount++ > 2 * Vertices.Num() * NumFetchPlatforms())
		{
			UE_LOG(LogCook, Fatal, TEXT("Infinite loop detected in FRequestCluster::PendingTransitiveBuildDependencyVertices."));
		}
		return;
	}

	bOutDone = true;
}

void FRequestCluster::FGraphSearch::ResolveTransitiveBuildDependencyCycle()
{
	// We interpret cycles in the transitive build dependency graph to mean that every vertex in the cycle is
	// invalidated if and only if any dependency from any vertex that points outside the cycle is invalidated (the
	// dependency pointing outside the cycle might be either a transitive build dependency on a package outside of the
	// cycle or a direct dependency).

	// Using this definition, we can resolve as not iteratively modified, with no further calculation needed, all
	// elements in the PendingTransitiveBuildDependencyVertices graph, when we run out of direct dependency work to do.
	// Proof:

	// Every package in the PendingTransitiveBuildDependencyVertices set is one that is not invalidated by any of its
	// direct dependencies, but it has transitive build dependencies that might be invalidated.
	// If we have run out of direct dependency work to do, then all there are no transitive build dependencies on any
	// vertex not in the set.
	// No direct dependency invalidations and no transitive build dependency invalidations, by our interpretation of a
	// cycle above, mean that the package is not invalidated.

	// Mark all of the currently fetched platforms of all packages in the PendingTransitiveBuildDependencyVertices as
	// ignore transitive build dependencies and kick them.

	FVertexData* FirstVertex = nullptr;
	for (FVertexData* CycleVert : PendingTransitiveBuildDependencyVertices)
	{
		check(CycleVert != nullptr); // Required hint for static analyzers.
		if (!FirstVertex)
		{
			FirstVertex = CycleVert;
		}
		for (FQueryPlatformData& PlatformData : GetPlatformDataArray(*CycleVert))
		{
			if (PlatformData.bIterativelyUnmodifiedRequested || PlatformData.bExploreRequested)
			{
				PlatformData.bTransitiveBuildDependenciesResolvedAsNotModified = true;
			}
		}
		// We can also empty the IterativelyModifiedListeners since any remaining listeners must be in
		// PendingTransitiveBuildDependencyVertices. Emptying the list here avoids the expense of kicking
		// for a second time each of the listeners.
		CycleVert->IterativelyModifiedListeners.Empty();
		KickVertex(CycleVert);
	}
	check(FirstVertex); // This function should not be called if PendingTransitiveBuildDependencyVertices is empty.
	PendingTransitiveBuildDependencyVertices.Empty();
	UE_LOG(LogCook, Display,
		TEXT("Cycle detected in the graph of transitive build dependencies.")
		TEXT(" No vertices in the cycle are invalidated by their direct dependencies, so marking them all as iteratively skippable.")
		TEXT("\n\tVertex in the cycle: %s"),
		*FirstVertex->PackageName.ToString());
}

void FRequestCluster::FGraphSearch::UpdateDisplay()
{
	constexpr double WarningTimeout = 10.0;
	if (FPlatformTime::Seconds() > LastActivityTime + WarningTimeout && Cluster.IsIncrementalCook())
	{
		FScopeLock ScopeLock(&Lock);
		int32 NumPendingRequestsInBatches = 0;
		int32 NumBatches = AsyncQueueBatches.Num();
		for (FQueryVertexBatch* Batch : AsyncQueueBatches)
		{
			NumPendingRequestsInBatches += Batch->NumPendingRequests;
		}

		UE_LOG(LogCook, Warning,
			TEXT("FRequestCluster waited more than %.0lfs for previous build results from the oplog. ")
			TEXT("NumPendingBatches == %d, NumPendingRequestsInBatches == %d. Continuing to wait..."),
			WarningTimeout, NumBatches, NumPendingRequestsInBatches);
		LastActivityTime = FPlatformTime::Seconds();
	}
}

void FRequestCluster::FGraphSearch::VisitVertex(FVertexData& Vertex)
{
	// Only called from scheduler thread

	// The PackageData will not exist if the package does not exist on disk
	if (!Vertex.PackageData)
	{
		return;
	}

	int32 LocalNumFetchPlatforms = NumFetchPlatforms();
	TBitArray<> ShouldFetchPlatforms(false, LocalNumFetchPlatforms);
	
	FPackagePlatformData* CookerLoadingPlatform = nullptr;
	const ITargetPlatform* FirstReachableSessionPlatform = nullptr;
	ESuppressCookReason SuppressCookReason = ESuppressCookReason::Invalid;
	bool bAllReachablesUncookable = true;
	for (TPair<const ITargetPlatform*, FPackagePlatformData>& Pair :
		Vertex.PackageData->GetPlatformDatasConstKeysMutableValues())
	{
		FPackagePlatformData& PlatformData = Pair.Value;
		const ITargetPlatform* TargetPlatform = Pair.Key;
		if (TargetPlatform == CookerLoadingPlatformKey)
		{
			CookerLoadingPlatform = &PlatformData;
		}
		else if (PlatformData.IsReachable())
		{
			int32 PlatformIndex = Algo::BinarySearchBy(FetchPlatforms, TargetPlatform, [](const FFetchPlatformData& D)
				{
					return D.Platform;
				});
			check(PlatformIndex != INDEX_NONE);

			if (!FirstReachableSessionPlatform)
			{
				FirstReachableSessionPlatform = TargetPlatform;
			}
			if (!PlatformData.IsVisitedByCluster())
			{
				VisitVertexForPlatform(Vertex, TargetPlatform, PlatformData, SuppressCookReason);

				if ((TraversalTier >= ETraversalTier::FetchEdgeData) && 
					(((TraversalTier >= ETraversalTier::FollowDependencies) && PlatformData.IsExplorable())
						|| Cluster.IsIncrementalCook()))
				{
					ShouldFetchPlatforms[PlatformIndex] = true;
					Vertex.PlatformData[PlatformIndex].bExploreRequested = true;
					// Exploration of any session platform also requires exploration of PlatformAgnosticPlatform
					Vertex.PlatformData[PlatformAgnosticPlatformIndex].bExploreRequested = true;
				}
			}
			if (PlatformData.IsCookable())
			{
				bAllReachablesUncookable = false;
				SuppressCookReason = ESuppressCookReason::NotSuppressed;
			}
		}
	}
	bool bAnyCookable = (FirstReachableSessionPlatform == nullptr) | !bAllReachablesUncookable;
	if (bAnyCookable != Vertex.bAnyCookable)
	{
		if (!bAnyCookable)
		{
			if (SuppressCookReason == ESuppressCookReason::Invalid)
			{
				// We need the SuppressCookReason for reporting. If we didn't calculate it this Visit and
				// we don't have it stored in this->OwnedPackageDatas, then we must have calculated it in
				// a previous cluster, but we don't store it anywhere. Recalculate it from the
				// FirstReachableSessionPlatform. FirstReachableSessionPlatform must be non-null, otherwise
				// bAnyCookable would be true.
				check(FirstReachableSessionPlatform);
				bool bCookable;
				bool bExplorable;
				Cluster.IsRequestCookable(FirstReachableSessionPlatform, Vertex.PackageData->GetPackageName(),
					*Vertex.PackageData, SuppressCookReason, bCookable, bExplorable);
				check(!bCookable); // We don't support bCookable changing for a given package and platform
				check(SuppressCookReason != ESuppressCookReason::Invalid);
			}
		}
		else
		{
			check(SuppressCookReason == ESuppressCookReason::NotSuppressed);
		}
		Cluster.SetPackageDataSuppressReason(*Vertex.PackageData, SuppressCookReason);
		Vertex.bAnyCookable = bAnyCookable;
	}

	// If any target platform is cookable, then we need to mark the CookerLoadingPlatform as reachable because we will
	// need to load the package to cook it
	if (bAnyCookable)
	{
		if (!CookerLoadingPlatform)
		{
			CookerLoadingPlatform = &Vertex.PackageData->FindOrAddPlatformData(CookerLoadingPlatformKey);
		}
		CookerLoadingPlatform->SetReachable(true);
	}
	if (CookerLoadingPlatform && CookerLoadingPlatform->IsReachable() && !CookerLoadingPlatform->IsVisitedByCluster())
	{
		CookerLoadingPlatform->SetCookable(true);
		CookerLoadingPlatform->SetExplorable(true);
		CookerLoadingPlatform->SetVisitedByCluster(true);
		if (TraversalTier >= ETraversalTier::FollowDependencies)
		{
			ShouldFetchPlatforms[CookerLoadingPlatformIndex] = true;
			Vertex.PlatformData[CookerLoadingPlatformIndex].bExploreRequested = true;
		}
	}

	if (TraversalTier >= ETraversalTier::FetchEdgeData)
	{
		for (int32 PlatformIndex = 0; PlatformIndex < LocalNumFetchPlatforms; ++PlatformIndex)
		{
			FQueryPlatformData& PlatformData = Vertex.PlatformData[PlatformIndex];

			// Add on the fetch (but not the explore) of bIterativelyUnmodifiedRequested platforms
			if (PlatformData.bIterativelyUnmodifiedRequested)
			{
				ShouldFetchPlatforms[PlatformIndex] = true;
			}

			// Also add the fetch (but not necessarily the explore) of PlatformAgnosticPlatform if a
			// SessionPlatform is fetched.
			if (ShouldFetchPlatforms[PlatformIndex] &&
				PlatformIndex != CookerLoadingPlatformIndex && PlatformIndex != PlatformAgnosticPlatformIndex)
			{
				ShouldFetchPlatforms[PlatformAgnosticPlatformIndex] = true;
			}
		}

		// Convert Bit Array to an array of indexes and fetch them if non empty
		TArray<int32, TInlineAllocator<10>> FetchPlatformIndexes;
		for (int32 PlatformIndex = 0; PlatformIndex < LocalNumFetchPlatforms; ++PlatformIndex)
		{
			if (ShouldFetchPlatforms[PlatformIndex])
			{
				FetchPlatformIndexes.Add(PlatformIndex);
			}
		}
		if (!FetchPlatformIndexes.IsEmpty())
		{
			QueueEdgesFetch(Vertex, FetchPlatformIndexes);
		}
	}
}

void FRequestCluster::FGraphSearch::VisitVertexForPlatform(FVertexData& Vertex, const ITargetPlatform* Platform,
	FPackagePlatformData& PlatformData, ESuppressCookReason& AccumulatedSuppressCookReason)
{
	FPackageData& PackageData = *Vertex.PackageData;
	ESuppressCookReason SuppressCookReason = ESuppressCookReason::Invalid;
	bool bCookable;
	bool bExplorable;
	Cluster.IsRequestCookable(Platform, Vertex.PackageData->GetPackageName(), PackageData, SuppressCookReason,
		bCookable, bExplorable);
	PlatformData.SetCookable(bCookable);
	PlatformData.SetExplorable(bExplorable);
	if (bCookable)
	{
		AccumulatedSuppressCookReason = ESuppressCookReason::NotSuppressed;
	}
	else
	{
		check(SuppressCookReason != ESuppressCookReason::Invalid
			&& SuppressCookReason != ESuppressCookReason::NotSuppressed);
		if (AccumulatedSuppressCookReason == ESuppressCookReason::Invalid)
		{
			AccumulatedSuppressCookReason = SuppressCookReason;
		}
	}
	PlatformData.SetVisitedByCluster(true);
}

FRequestCluster::FGraphSearch::FExploreEdgesContext::FExploreEdgesContext(FRequestCluster& InCluster,
	FGraphSearch& InGraphSearch)
	: Cluster(InCluster)
	, GraphSearch(InGraphSearch)
{
}

void FRequestCluster::FGraphSearch::FExploreEdgesContext::Explore(FVertexData& InVertex)
{
	// Only called from scheduler thread

	Initialize(InVertex);
	CalculatePlatformsToProcess();
	if (PlatformsToProcess.IsEmpty())
	{
		return;
	}

	if (!TryCalculateIterativelyUnmodified())
	{
		// The vertex was added as a listener to the pending data it needs. Exit from explore
		// for now and we will reenter it later when the data becomes available.
		return;
	}
	if (PlatformsToExplore.IsEmpty())
	{
		// We had platforms we needed to test for iteratively unmodified (for e.g. TransitiveBuildDependencies), but
		// nothing to explore. No more work to do until/unless they become marked for explore later.
		return;
	}

	CalculatePackageDataDependenciesPlatformAgnostic();
	CalculateDependenciesAndIterativelySkippable();
	QueueVisitsOfDependencies();
	MarkExploreComplete();
}

void FRequestCluster::FGraphSearch::FExploreEdgesContext::Initialize(FVertexData& InVertex)
{
	Vertex = &InVertex;
	// Vertices without a package data are never queued for fetch
	check(Vertex->PackageData);
	PackageData = Vertex->PackageData;
	PackageName = Vertex->PackageName;

	HardGameDependencies.Reset();
	HardEditorDependencies.Reset();
	SoftGameDependencies.Reset();
	CookerLoadingDependencies.Reset();
	PlatformsToProcess.Reset();
	PlatformsToExplore.Reset();
	PlatformDependencyMap.Reset();
	HardDependenciesSet.Reset();
	SkippedPackages.Reset();
	UnreadyTransitiveBuildVertices.Reset();

	LocalNumFetchPlatforms = GraphSearch.NumFetchPlatforms();
	bFetchAnyTargetPlatform = false;

	DiscoveredDependencies = Cluster.COTFS.DiscoveredDependencies.Find(PackageName);

	GraphSearch.PendingTransitiveBuildDependencyVertices.Remove(Vertex);
}

void FRequestCluster::FGraphSearch::FExploreEdgesContext::CalculatePlatformsToProcess()
{
	FQueryPlatformData& PlatformAgnosticQueryData = Vertex->PlatformData[PlatformAgnosticPlatformIndex];
	for (int32 PlatformIndex = 0; PlatformIndex < LocalNumFetchPlatforms; ++PlatformIndex)
	{
		if (PlatformIndex == PlatformAgnosticPlatformIndex)
		{
			continue;
		}
		FQueryPlatformData& QueryPlatformData = Vertex->PlatformData[PlatformIndex];
		if (!QueryPlatformData.bSchedulerThreadFetchCompleted)
		{
			continue;
		}
		bool bIterativelyUnmodifiedNeeded = !QueryPlatformData.bIterativelyUnmodified.IsSet();
		bool bExploreNeeded = !QueryPlatformData.bExploreCompleted && QueryPlatformData.bExploreRequested;
		if (!bIterativelyUnmodifiedNeeded && !bExploreNeeded)
		{
			continue;
		}
		if (bExploreNeeded && PlatformIndex != CookerLoadingPlatformIndex)
		{
			if (!PlatformAgnosticQueryData.bSchedulerThreadFetchCompleted)
			{
				continue;
			}
			// bExploreNeeded implies bExploreRequested, and wherever bExploreRequested is set to true we also set it
			// to true for PlatformAgnosticQueryData.
			check(PlatformAgnosticQueryData.bExploreRequested);
			bFetchAnyTargetPlatform = true;
		}
		PlatformsToProcess.Add(PlatformIndex);
		if (bExploreNeeded)
		{
			PlatformsToExplore.Add(PlatformIndex);
		}
	}
}

const FAssetPackageData* FRequestCluster::FVertexData::GetGeneratedAssetPackageData()
{
	check(PackageData); // Caller should not call if no PackageData
	FPackageDatas& LocalPackageDatas = PackageData->GetPackageDatas();
	FPackageData* ParentPackageData = LocalPackageDatas.FindPackageDataByPackageName(PackageData->GetParentGenerator());
	if (ParentPackageData)
	{
		TRefCountPtr<FGenerationHelper> ParentGenerationHelper = ParentPackageData->GetGenerationHelper();
		if (ParentGenerationHelper)
		{
			return ParentGenerationHelper->GetIncrementalCookAssetPackageData(*PackageData);
		}
	}
	return nullptr;
}

bool FRequestCluster::FGraphSearch::FExploreEdgesContext::TryCalculateIterativelyUnmodified()
{
	using namespace UE::TargetDomain;

	if (!Cluster.IsIncrementalCook())
	{
		return true;
	}

	bool bAllPlatformsAreReady = true;
	for (int32 PlatformIndex : PlatformsToProcess)
	{
		if (PlatformIndex == CookerLoadingPlatformIndex)
		{
			continue;
		}

		FQueryPlatformData& QueryPlatformData = Vertex->PlatformData[PlatformIndex];
		if (QueryPlatformData.bIterativelyUnmodified.IsSet())
		{
			continue;
		}

		FFetchPlatformData& FetchPlatformData = GraphSearch.FetchPlatforms[PlatformIndex];
		const ITargetPlatform* TargetPlatform = FetchPlatformData.Platform;
		FPackagePlatformData& PackagePlatformData = PackageData->FindOrAddPlatformData(TargetPlatform);

		if (!PackagePlatformData.IsCookable())
		{
			SetIsIterativelyUnmodified(PlatformIndex, false, PackagePlatformData);
			continue;
		}

		UE::TargetDomain::FCookDependencies& CookDependencies = QueryPlatformData.CookAttachments.Dependencies;
		const FAssetPackageData* OverrideAssetPackageData = nullptr;
		FPackageData* ParentPackageData = nullptr;
		if (PackageData->IsGenerated())
		{
			// If a generator is marked iteratively unmodified, then by contract we are not required to test its
			// generated packages; they are all marked iteratively unmodified as well
			ParentPackageData = Cluster.PackageDatas.FindPackageDataByPackageName(
				PackageData->GetParentGenerator());
			if (ParentPackageData)
			{
				const FPackagePlatformData* ParentPlatformData =
					ParentPackageData->GetPlatformDatas().Find(TargetPlatform);
				if (ParentPlatformData)
				{
					if (ParentPlatformData->IsIterativelyUnmodified())
					{
						SetIsIterativelyUnmodified(PlatformIndex, true, PackagePlatformData);
						continue;
					}
				}
			}

			// If the generator was not marked iteratively unmodified, then we use the data provided by the generator
			// to decide whether the generated package is iteratively unmodified.
			OverrideAssetPackageData = Vertex->GetGeneratedAssetPackageData();
			if (!OverrideAssetPackageData)
			{
				SetIsIterativelyUnmodified(PlatformIndex, false, PackagePlatformData);
				continue;
			}
		}
		if (!CookDependencies.HasKeyMatch(OverrideAssetPackageData))
		{
			SetIsIterativelyUnmodified(PlatformIndex, false, PackagePlatformData);
			continue;
		}

		if (!IsIterativeEnabled(PackageName, Cluster.COTFS.bHybridIterativeAllowAllClasses, OverrideAssetPackageData))
		{
			SetIsIterativelyUnmodified(PlatformIndex, false, PackagePlatformData);
			continue;
		}
		// Generated packages of a generator that is not IterativelyEnabled are also not iteratively enabled, even
		// if they would otherwise qualify for iterative on their own. e.g. if worlds are iteratively disallowed,
		// then streamingobject generated packages of the world are also disallowed.
		if (ParentPackageData && !IsIterativeEnabled(ParentPackageData->GetPackageName(),
			Cluster.COTFS.bHybridIterativeAllowAllClasses))
		{
			SetIsIterativelyUnmodified(PlatformIndex, false, PackagePlatformData);
			continue;
		}

		if (!QueryPlatformData.bTransitiveBuildDependenciesResolvedAsNotModified)
		{
			bool bAnyTransitiveBuildDependencyIsModified = false;
			UnreadyTransitiveBuildVertices.Reset();
			for (const UE::Cook::FCookDependency& TransitiveBuildDependency :
				CookDependencies.GetTransitiveBuildDependencies())
			{
				FName TransitiveBuildPackageName = TransitiveBuildDependency.GetPackageName();
				FVertexData& TransitiveBuildVertex = GraphSearch.FindOrAddVertex(TransitiveBuildPackageName);
				if (!TransitiveBuildVertex.PackageData)
				{
					// A build dependency on a non-existent package can occur e.g. if the package is in an
					// unmounted plugin. If the package does not exist we count the transitivebuilddependency
					// as not iteratively unmodified, the same as any package that is not cooked, so mark this
					// package as not iteratively unmodified.
					// This is an unexpected data layout however, so log it as a warning.
					UE_LOG(LogCook, Warning,
						TEXT("TransitiveBuildDependency to non-existent package.")
						TEXT(" Package %s has a transitive build dependency on package %s, which does not exist or is not mounted.")
						TEXT(" Package %s will be marked as not iteratively skippable and will be recooked."),
						*Vertex->PackageName.ToString(), *TransitiveBuildPackageName.ToString(),
						*Vertex->PackageName.ToString());
					bAnyTransitiveBuildDependencyIsModified = true;
					break;
				}

				FQueryPlatformData& TransitivePlatformData = TransitiveBuildVertex.PlatformData[PlatformIndex];
				if (!TransitivePlatformData.bIterativelyUnmodified.IsSet())
				{
					UnreadyTransitiveBuildVertices.Add(&TransitiveBuildVertex);
					continue;
				}
				if (!TransitivePlatformData.bIterativelyUnmodified.GetValue())
				{
					bAnyTransitiveBuildDependencyIsModified = true;
					break;
				}
			}

			if (bAnyTransitiveBuildDependencyIsModified)
			{
				SetIsIterativelyUnmodified(PlatformIndex, false, PackagePlatformData);
				continue;
			}
			if (!UnreadyTransitiveBuildVertices.IsEmpty())
			{
				// Add this vertex as a listener to the TransitiveBuildVertices' TryCalculateIterativelyUnmodified
				for (FVertexData* TransitiveBuildVertex : UnreadyTransitiveBuildVertices)
				{
					FQueryPlatformData& TransitivePlatformData = TransitiveBuildVertex->PlatformData[PlatformIndex];

					// Do not kick the vertex again if it has already been fetched; doing so will create busy work
					// in the case of a cycle and prevent us from detecting the cycle.
					if (!TransitivePlatformData.bSchedulerThreadFetchCompleted)
					{
						TransitivePlatformData.bIterativelyUnmodifiedRequested = true;
						GraphSearch.AddToVisitVertexQueue(*TransitiveBuildVertex);
					}
					// It's okay to add duplicates to IterativelyModifiedListeners; we remove them when broadcasting
					TransitiveBuildVertex->IterativelyModifiedListeners.Add(this->Vertex);
				}

				bAllPlatformsAreReady = false;
				continue;
			}
		}

		SetIsIterativelyUnmodified(PlatformIndex, true, PackagePlatformData);
	}

	if (!bAllPlatformsAreReady)
	{
		GraphSearch.PendingTransitiveBuildDependencyVertices.Add(Vertex);
		return false;
	}

	if (!Vertex->IterativelyModifiedListeners.IsEmpty())
	{
		Algo::Sort(Vertex->IterativelyModifiedListeners);
		Vertex->IterativelyModifiedListeners.SetNum(Algo::Unique(Vertex->IterativelyModifiedListeners));
		for (FVertexData* ListenerVertex : Vertex->IterativelyModifiedListeners)
		{
			GraphSearch.KickVertex(ListenerVertex);
		}
		Vertex->IterativelyModifiedListeners.Empty();
	}
	return true;
}

void FRequestCluster::FGraphSearch::FExploreEdgesContext::CalculatePackageDataDependenciesPlatformAgnostic()
{
	using namespace UE::AssetRegistry;

	if (!bFetchAnyTargetPlatform)
	{
		return;
	}

	EDependencyQuery FlagsForHardDependencyQuery;
	if (Cluster.COTFS.bSkipOnlyEditorOnly)
	{
		Cluster.AssetRegistry.GetDependencies(PackageName, HardGameDependencies, EDependencyCategory::Package,
			EDependencyQuery::Game | EDependencyQuery::Hard);
		HardDependenciesSet.Append(HardGameDependencies);
	}
	else
	{
		// We're not allowed to skip editoronly imports, so include all hard dependencies
		FlagsForHardDependencyQuery = EDependencyQuery::Hard;
		Cluster.AssetRegistry.GetDependencies(PackageName, HardGameDependencies, EDependencyCategory::Package,
			EDependencyQuery::Game | EDependencyQuery::Hard);
		Cluster.AssetRegistry.GetDependencies(PackageName, HardEditorDependencies, EDependencyCategory::Package,
			EDependencyQuery::EditorOnly | EDependencyQuery::Hard);
		HardDependenciesSet.Append(HardGameDependencies);
		HardDependenciesSet.Append(HardEditorDependencies);
	}
	if (DiscoveredDependencies)
	{
		HardDependenciesSet.Append(*DiscoveredDependencies);
	}
	if (Cluster.bAllowSoftDependencies)
	{
		// bSkipOnlyEditorOnly is always true for soft dependencies; skip editoronly soft dependencies
		Cluster.AssetRegistry.GetDependencies(PackageName, SoftGameDependencies, EDependencyCategory::Package,
			EDependencyQuery::Game | EDependencyQuery::Soft);

		// Even if we're following soft references in general, we need to check with the SoftObjectPath registry
		// for any startup packages that marked their softobjectpaths as excluded, and not follow those
		if (GRedirectCollector.RemoveAndCopySoftObjectPathExclusions(PackageName, SkippedPackages))
		{
			SoftGameDependencies.RemoveAll([this](FName SoftDependency)
				{
					return SkippedPackages.Contains(SoftDependency);
				});
		}

		// LocalizationReferences are a source of SoftGameDependencies that are not present in the AssetRegistry
		SoftGameDependencies.Append(GetLocalizationReferences(PackageName, Cluster.COTFS));

		// The AssetManager can provide additional SoftGameDependencies
		SoftGameDependencies.Append(GetAssetManagerReferences(PackageName));
	}
}

void FRequestCluster::FGraphSearch::FExploreEdgesContext::CalculateDependenciesAndIterativelySkippable()
{
	using namespace UE::AssetRegistry;

	for (int32 PlatformIndex : PlatformsToExplore)
	{
		FQueryPlatformData& QueryPlatformData = Vertex->PlatformData[PlatformIndex];
		FFetchPlatformData& FetchPlatformData = GraphSearch.FetchPlatforms[PlatformIndex];
		const ITargetPlatform* TargetPlatform = FetchPlatformData.Platform;
		FPackagePlatformData& PackagePlatformData = PackageData->FindOrAddPlatformData(TargetPlatform);
		if ((GraphSearch.TraversalTier < ETraversalTier::FollowDependencies) || !PackagePlatformData.IsExplorable())
		{
			// ExploreVertexEdges is responsible for updating package modification status so we might
			// have been called for this platform even if not explorable. If not explorable, just update
			// package modification status for the given platform, except for CookerLoadingPlatformIndex which has
			// no status to update.
			if (PlatformIndex != CookerLoadingPlatformIndex)
			{
				ProcessPlatformAttachments(PlatformIndex, TargetPlatform, FetchPlatformData, PackagePlatformData,
					QueryPlatformData.CookAttachments, false /* bExploreDependencies */);
			}
			continue;
		}

		if (PlatformIndex == CookerLoadingPlatformIndex)
		{
			Cluster.AssetRegistry.GetDependencies(PackageName, CookerLoadingDependencies, EDependencyCategory::Package,
				EDependencyQuery::Hard);

			// ITERATIVECOOK_TODO: To improve cooker load performance, we should declare EDependencyQuery::Build
			// packages as packages that will be loaded during the cook, by adding them as edges for the
			// CookerLoadingPlatformIndex platform.
			// But we can't do that yet; in some important cases the build dependencies are declared by a class but not
			// always used - some build dependencies might be a conservative list but unused by the asset, or unused on
			// targetplatform.
			// Adding BuildDependencies also sets up many circular dependencies, because maps declare their external
			// actors as build dependencies and the external actors declare the map as a build or hard dependency.
			// Topological sort done at the end of the Cluster has poor performance when there are 100k+ circular
			// dependencies.
			constexpr bool bAddBuildDependenciesToGraph = false;
			if (bAddBuildDependenciesToGraph)
			{
				Cluster.AssetRegistry.GetDependencies(PackageName, CookerLoadingDependencies,
					EDependencyCategory::Package, EDependencyQuery::Build);
			}
			// CookerLoadingPlatform does not cause SetInstigator so it does not modify the platformdependency's
			// InstigatorType
			AddPlatformDependencyRange(CookerLoadingDependencies, PlatformIndex, EInstigator::InvalidCategory);
		}
		else
		{
			AddPlatformDependencyRange(HardGameDependencies, PlatformIndex, EInstigator::HardDependency);
			AddPlatformDependencyRange(HardEditorDependencies, PlatformIndex, EInstigator::HardEditorOnlyDependency);
			AddPlatformDependencyRange(SoftGameDependencies, PlatformIndex, EInstigator::SoftDependency);
			ProcessPlatformAttachments(PlatformIndex, TargetPlatform, FetchPlatformData, PackagePlatformData,
				QueryPlatformData.CookAttachments, true /* bExploreDependencies  */);
		}
		if (DiscoveredDependencies)
		{
			AddPlatformDependencyRange(*DiscoveredDependencies, PlatformIndex, EInstigator::HardDependency);
		}
	}
}

void FRequestCluster::FGraphSearch::FExploreEdgesContext::QueueVisitsOfDependencies()
{
	if (PlatformDependencyMap.IsEmpty())
	{
		return;
	}

	TArray<FPackageData*>* Edges = nullptr;
	TRefCountPtr<FGenerationHelper> GenerationHelper = PackageData->GetGenerationHelper();
	for (TPair<FName, FScratchPlatformDependencyBits>& PlatformDependencyPair : PlatformDependencyMap)
	{
		FName DependencyName = PlatformDependencyPair.Key;
		TBitArray<>& HasPlatformByIndex = PlatformDependencyPair.Value.HasPlatformByIndex;
		EInstigator InstigatorType = PlatformDependencyPair.Value.InstigatorType;

		// Process any CoreRedirects before checking whether the package exists
		FName Redirected = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Package,
			FCoreRedirectObjectName(NAME_None, NAME_None, DependencyName)).PackageName;
		DependencyName = Redirected;

		FVertexData& DependencyVertex = GraphSearch.FindOrAddVertex(DependencyName,
			GenerationHelper.GetReference());
		if (!DependencyVertex.PackageData)
		{
			continue;
		}
		FPackageData& DependencyPackageData(*DependencyVertex.PackageData);
		bool bAddToVisitVertexQueue = false;

		for (int32 PlatformIndex = 0; PlatformIndex < LocalNumFetchPlatforms; ++PlatformIndex)
		{
			if (!HasPlatformByIndex[PlatformIndex])
			{
				continue;
			}
			FFetchPlatformData& FetchPlatformData = GraphSearch.FetchPlatforms[PlatformIndex];
			const ITargetPlatform* TargetPlatform = FetchPlatformData.Platform;
			FPackagePlatformData& PlatformData = DependencyPackageData.FindOrAddPlatformData(TargetPlatform);

			if (PlatformIndex == CookerLoadingPlatformIndex)
			{
				if (!Edges)
				{
					Edges = &GraphSearch.GraphEdges.FindOrAdd(PackageData);
					Edges->Reset(PlatformDependencyMap.Num());
				}
				Edges->Add(&DependencyPackageData);
			}

			if (!PlatformData.IsReachable())
			{
				PlatformData.SetReachable(true);
				if (!DependencyPackageData.HasInstigator() && TargetPlatform != CookerLoadingPlatformKey)
				{
					DependencyPackageData.SetInstigator(Cluster, FInstigator(InstigatorType, PackageName));
				}
			}
			if (!PlatformData.IsVisitedByCluster())
			{
				bAddToVisitVertexQueue = true;
			}
		}
		if (bAddToVisitVertexQueue)
		{
			if (DependencyVertex.PackageData)
			{
				// Only pull the vertex into the cluster if it has not already been pulled into the cluster.
				// This prevents us from trying to readd a packagedata after COTFS called Cluster->RemovePackageData.
				if (!DependencyVertex.bPulledIntoCluster)
				{
					DependencyVertex.bPulledIntoCluster = true;
					Cluster.PullIntoCluster(*DependencyVertex.PackageData);
				}
			}
			GraphSearch.AddToVisitVertexQueue(DependencyVertex);
		}
	}
}

void FRequestCluster::FGraphSearch::FExploreEdgesContext::MarkExploreComplete()
{
	for (int32 PlatformIndex : PlatformsToExplore)
	{
		Vertex->PlatformData[PlatformIndex].bExploreCompleted = true;
	}
}

void FRequestCluster::FGraphSearch::FExploreEdgesContext::AddPlatformDependency(FName DependencyName,
	int32 PlatformIndex, EInstigator InstigatorType)
{
	FScratchPlatformDependencyBits& PlatformDependencyBits = PlatformDependencyMap.FindOrAdd(DependencyName);
	if (PlatformDependencyBits.HasPlatformByIndex.Num() != LocalNumFetchPlatforms)
	{
		PlatformDependencyBits.HasPlatformByIndex.Init(false, LocalNumFetchPlatforms);
		PlatformDependencyBits.InstigatorType = EInstigator::SoftDependency;
	}
	PlatformDependencyBits.HasPlatformByIndex[PlatformIndex] = true;

	// Calculate PlatformDependencyType.InstigatorType == 
	// Max(InstigatorType, PlatformDependencyType.InstigatorType)
	// based on the enum values, from least required to most: [ Soft, HardEditorOnly, Hard ]
	switch (InstigatorType)
	{
	case EInstigator::HardDependency:
		PlatformDependencyBits.InstigatorType = InstigatorType;
		break;
	case EInstigator::HardEditorOnlyDependency:
		if (PlatformDependencyBits.InstigatorType != EInstigator::HardDependency)
		{
			PlatformDependencyBits.InstigatorType = InstigatorType;
		}
		break;
	case EInstigator::SoftDependency:
		// New value is minimum, so keep the old value
		break;
	case EInstigator::InvalidCategory:
		// Caller indicated they do not want to set the InstigatorType
		break;
	default:
		checkNoEntry();
		break;
	}
}

void FRequestCluster::FGraphSearch::FExploreEdgesContext::AddPlatformDependencyRange(TConstArrayView<FName> Range,
	int32 PlatformIndex, EInstigator InstigatorType)
{
	for (FName DependencyName : Range)
	{
		AddPlatformDependency(DependencyName, PlatformIndex, InstigatorType);
	}
}

void FRequestCluster::FGraphSearch::FExploreEdgesContext::ProcessPlatformAttachments(int32 PlatformIndex,
	const ITargetPlatform* TargetPlatform, FFetchPlatformData& FetchPlatformData,
	FPackagePlatformData& PackagePlatformData, UE::TargetDomain::FCookAttachments& PlatformAttachments,
	bool bExploreDependencies)
{
	bool bFoundBuildDefinitions = false;
	ICookedPackageWriter* PackageWriter = FetchPlatformData.Writer;
	FQueryPlatformData& QueryPlatformData = Vertex->PlatformData[PlatformIndex];

	if (Cluster.IsIncrementalCook() && PackagePlatformData.IsCookable())
	{
		check(QueryPlatformData.bIterativelyUnmodified.IsSet());
		bool bIterativelyUnmodified = QueryPlatformData.bIterativelyUnmodified.GetValue();
		if (bIterativelyUnmodified)
		{
			UE::TargetDomain::FCookDependencies& CookDependencies = PlatformAttachments.Dependencies;
			if (bExploreDependencies && Cluster.bAllowSoftDependencies)
			{
				AddPlatformDependencyRange(CookDependencies.GetRuntimePackageDependencies(), PlatformIndex,
					EInstigator::SoftDependency);
			}

			if (Cluster.bPreQueueBuildDefinitions)
			{
				bFoundBuildDefinitions = true;
				Cluster.BuildDefinitions.AddBuildDefinitionList(PackageName, TargetPlatform,
					PlatformAttachments.BuildDefinitions.Definitions);
			}
		}
		bool bShouldIterativelySkip = bIterativelyUnmodified;
		PackageWriter->UpdatePackageModificationStatus(PackageName, bIterativelyUnmodified,
			bShouldIterativelySkip);

		TRefCountPtr<FGenerationHelper> ParentGenerationHelper;
		if (PackageData->IsGenerated())
		{
			// If a GeneratorPackage is iteratively skipped, its generated packages must be iteratively skipped as well
			FPackageData* ParentPackage = Cluster.PackageDatas.FindPackageDataByPackageName(PackageData->GetParentGenerator());
			if (ParentPackage)
			{
				ParentGenerationHelper = ParentPackage->GetGenerationHelper();
				const FPackagePlatformData* ParentPlatformData = ParentPackage->GetPlatformDatas().Find(TargetPlatform);
				if (ParentPlatformData && ParentPlatformData->IsIterativelySkipped())
				{
					bShouldIterativelySkip = true;
				}
			}
		}
		if (bShouldIterativelySkip)
		{
			// Call SetPlatformCooked instead of just PackagePlatformData.SetCookResults because we might also need
			// to set OnFirstCookedPlatformAdded
			PackageData->SetPlatformCooked(TargetPlatform, ECookResult::Succeeded);
			PackagePlatformData.SetIterativelySkipped(true);
			if (TRefCountPtr<FGenerationHelper> GenerationHelper = PackageData->GetGenerationHelper())
			{
				GenerationHelper->MarkPackageIterativelySkipped(*PackageData);
			}
			if (ParentGenerationHelper)
			{
				ParentGenerationHelper->MarkPackageIterativelySkipped(*PackageData);
			}
			Cluster.SetPackageDataWasMarkedCooked(*PackageData, true);
			if (PlatformIndex == FirstSessionPlatformIndex)
			{
				COOK_STAT(++DetailedCookStats::NumPackagesIterativelySkipped);
			}
			// Declare the package to the EDLCookInfo verification so we don't warn about missing exports from it
			UE::SavePackageUtilities::EDLCookInfoAddIterativelySkippedPackage(PackageName);
		}
	}

	if (Cluster.bPreQueueBuildDefinitions && !bFoundBuildDefinitions)
	{
		FQueryPlatformData& PlatformAgnosticQueryData = Vertex->PlatformData[PlatformAgnosticPlatformIndex];

		if (PlatformAgnosticQueryData.bSchedulerThreadFetchCompleted)
		{
			bool bCanCheckHasKeyMatch = true;
			const FAssetPackageData* OverrideAssetPackageData = nullptr;
			if (PackageData->IsGenerated())
			{
				OverrideAssetPackageData = Vertex->GetGeneratedAssetPackageData();
				if (!OverrideAssetPackageData)
				{
					bCanCheckHasKeyMatch = false;
				}
			}
			if (bCanCheckHasKeyMatch &&
				PlatformAgnosticQueryData.CookAttachments.Dependencies.HasKeyMatch(OverrideAssetPackageData))
			{
				Cluster.BuildDefinitions.AddBuildDefinitionList(PackageName, TargetPlatform,
					PlatformAgnosticQueryData.CookAttachments.BuildDefinitions.Definitions);
			}
		}
	}
}

void FRequestCluster::FGraphSearch::FExploreEdgesContext::SetIsIterativelyUnmodified(int32 PlatformIndex,
	bool bIterativelyUnmodified, FPackagePlatformData& PackagePlatformData)
{
	Vertex->PlatformData[PlatformIndex].bIterativelyUnmodified.Emplace(bIterativelyUnmodified);
	if (bIterativelyUnmodified)
	{
		PackagePlatformData.SetIterativelyUnmodified(true);
	}
}

FRequestCluster::FVertexData* FRequestCluster::FGraphSearch::AllocateVertex(FName PackageName, FPackageData* PackageData)
{
	// TODO: Change TypeBlockedAllocator to have an optional Size and Align argument,
	// and use it to allocate the array of PlatformData, to reduce cpu time of allocating the array.
	return VertexAllocator.NewElement(PackageName, PackageData, *this);
}

FRequestCluster::FVertexData::FVertexData(FName InPackageName, UE::Cook::FPackageData* InPackageData,
	FGraphSearch& GraphSearch)
	: PackageName(InPackageName)
	, PackageData(InPackageData)
{
	PlatformData.Reset(new FQueryPlatformData[GraphSearch.NumFetchPlatforms()]);
}

FRequestCluster::FVertexData&
FRequestCluster::FGraphSearch::FindOrAddVertex(FName PackageName, FGenerationHelper* ParentGenerationHelper)
{
	// Only called from scheduler thread
	FVertexData*& ExistingVertex = Vertices.FindOrAdd(PackageName);
	if (ExistingVertex)
	{
		return *ExistingVertex;
	}

	FPackageData* PackageData = nullptr;
	TStringBuilder<256> NameBuffer;
	PackageName.ToString(NameBuffer);
	if (!FPackageName::IsScriptPackage(NameBuffer))
	{
		PackageData = Cluster.COTFS.PackageDatas->TryAddPackageDataByPackageName(PackageName);
		if (!PackageData && ParentGenerationHelper && ICookPackageSplitter::IsUnderGeneratedPackageSubPath(NameBuffer))
		{
			const FAssetPackageData* PreviousPackageData =
				ParentGenerationHelper->GetIncrementalCookAssetPackageData(PackageName);
			if (PreviousPackageData)
			{
				bool bIsMap = PreviousPackageData->Extension == EPackageExtension::Map;
				PackageData = Cluster.COTFS.PackageDatas->TryAddPackageDataByPackageName(PackageName,
					false /* bRequireExists */, bIsMap);
				if (PackageData)
				{
					PackageData->SetGenerated(ParentGenerationHelper->GetOwner().GetPackageName());
				}
			}
		}
	}

	ExistingVertex = AllocateVertex(PackageName, PackageData);
	return *ExistingVertex;
}

FRequestCluster::FVertexData&
FRequestCluster::FGraphSearch::FindOrAddVertex(FName PackageName, FPackageData& PackageData)
{
	// Only called from scheduler thread
	FVertexData*& ExistingVertex = Vertices.FindOrAdd(PackageName);
	if (ExistingVertex)
	{
		check(ExistingVertex->PackageData == &PackageData);
		return *ExistingVertex;
	}

	ExistingVertex = AllocateVertex(PackageName, &PackageData);
	return *ExistingVertex;
}

void FRequestCluster::FGraphSearch::AddToVisitVertexQueue(FVertexData& Vertex)
{
	VisitVertexQueue.Add(&Vertex);
}

void FRequestCluster::FGraphSearch::CreateAvailableBatches(bool bAllowIncompleteBatch)
{
	constexpr int32 BatchSize = 1000;
	if (PreAsyncQueue.IsEmpty() || (!bAllowIncompleteBatch && PreAsyncQueue.Num() < BatchSize))
	{
		return;
	}

	TArray<FQueryVertexBatch*> NewBatches;
	NewBatches.Reserve((PreAsyncQueue.Num() + BatchSize - 1) / BatchSize);
	{
		FScopeLock ScopeLock(&Lock);
		while (PreAsyncQueue.Num() >= BatchSize)
		{
			NewBatches.Add(CreateBatchOfPoppedVertices(BatchSize));
		}
		if (PreAsyncQueue.Num() > 0 && bAllowIncompleteBatch)
		{
			NewBatches.Add(CreateBatchOfPoppedVertices(PreAsyncQueue.Num()));
		}
	}
	for (FQueryVertexBatch* NewBatch : NewBatches)
	{
		NewBatch->Send();
	}
}

FRequestCluster::FQueryVertexBatch* FRequestCluster::FGraphSearch::AllocateBatch()
{
	// Called from inside this->Lock
	// BatchAllocator uses DeferredDestruction, so this might be a resused Batch, but we don't need to Reset it during
	// allocation because Batches are Reset during Free.
	return BatchAllocator.NewElement(*this);
}

void FRequestCluster::FGraphSearch::FreeBatch(FQueryVertexBatch* Batch)
{
	// Called from inside this->Lock
	Batch->Reset();
	BatchAllocator.Free(Batch);
}

FRequestCluster::FQueryVertexBatch* FRequestCluster::FGraphSearch::CreateBatchOfPoppedVertices(int32 BatchSize)
{
	// Called from inside this->Lock
	check(BatchSize <= PreAsyncQueue.Num());
	FQueryVertexBatch* BatchData = AllocateBatch();
	BatchData->Vertices.Reserve(BatchSize);
	for (int32 BatchIndex = 0; BatchIndex < BatchSize; ++BatchIndex)
	{
		FVertexData* Vertex = PreAsyncQueue.PopFrontValue();
		FVertexData*& ExistingVert = BatchData->Vertices.FindOrAdd(Vertex->PackageName);
		// Each PackageName should be used by just a single vertex.
		check(!ExistingVert || ExistingVert == Vertex);
		// If the vertex was already previously added to the batch that's okay, just ignore the new add.
		// A batch size of 0 is a problem but that can't happen just because a vertex is in the batch twice.
		// A batch size smaller than the expected `BatchSize` parameter is a minor performance issue but not a problem.
		ExistingVert = Vertex;
	}
	AsyncQueueBatches.Add(BatchData);
	return BatchData;
}

void FRequestCluster::FGraphSearch::OnBatchCompleted(FQueryVertexBatch* Batch)
{
	FScopeLock ScopeLock(&Lock);
	AsyncQueueBatches.Remove(Batch);
	FreeBatch(Batch);
	AsyncResultsReadyEvent->Trigger();
}

void FRequestCluster::FGraphSearch::KickVertex(FVertexData* Vertex)
{
	// The trigger occurs outside of the lock, and might get clobbered and incorrectly ignored by a call from the
	// scheduler thread if the scheduler tried to pop the AsyncQueueResults and found it empty before KickVertex calls
	// Enqueue but then pauses and calls AsyncResultsReadyEvent->Reset after KicKVertex calls Trigger. This clobbering
	// will not cause a deadlock, because eventually DestroyBatch will be called which triggers it inside the lock. Doing
	// the per-vertex trigger outside the lock is good for performance.
	AsyncQueueResults.Enqueue(Vertex);
	AsyncResultsReadyEvent->Trigger();
}

FRequestCluster::FQueryVertexBatch::FQueryVertexBatch(FGraphSearch& InGraphSearch)
	: ThreadSafeOnlyVars(InGraphSearch)
{
	PlatformDatas.SetNum(InGraphSearch.FetchPlatforms.Num());
}

void FRequestCluster::FQueryVertexBatch::Reset()
{
	for (FPlatformData& PlatformData : PlatformDatas)
	{
		PlatformData.PackageNames.Reset();
	}
	Vertices.Reset();
}

void FRequestCluster::FQueryVertexBatch::Send()
{
	int32 NumAddedRequests = 0;
	for (const TPair<FName, FVertexData*>& Pair : Vertices)
	{
		FVertexData* Vertex = Pair.Value;
		bool bAnyRequested = false;
		bool bAllHaveAlreadyCompletedFetch = false;
		for (int32 PlatformIndex = 0; PlatformIndex < PlatformDatas.Num(); ++PlatformIndex)
		{
			// The platform data may have already been requested; request it only if current status is NotRequested
			FQueryPlatformData& PlatformData = Vertex->PlatformData[PlatformIndex];
			if (!PlatformData.bSchedulerThreadFetchCompleted)
			{
				bAllHaveAlreadyCompletedFetch = false;
				EAsyncQueryStatus ExpectedStatus = EAsyncQueryStatus::SchedulerRequested;
				if (PlatformData.CompareExchangeAsyncQueryStatus(ExpectedStatus,
					EAsyncQueryStatus::AsyncRequested))
				{
					PlatformDatas[PlatformIndex].PackageNames.Add(Pair.Key);
					++NumAddedRequests;
				}
			}
		}
		if (bAllHaveAlreadyCompletedFetch)
		{
			// We are contractually obligated to kick the vertex. Normally we would call FCookAttachments::Fetch with it
			// and would then kick the vertex in our callback. Also, it might still be in the AsyncQueueResults for one
			// of the platforms so it will be kicked by TickExplore pulling it out of the AsyncQueueResults. But if all
			// requested platforms already previously pulled it out of AsyncQueueResults, then we need to kick it again.
			ThreadSafeOnlyVars.KickVertex(Vertex);
		}
	}
	if (NumAddedRequests == 0)
	{
		// We turned out not to need to send any from this batch. Report that the batch is complete.
		ThreadSafeOnlyVars.OnBatchCompleted(this);
		// *this is no longer accessible
		return;
	}

	NumPendingRequests.store(NumAddedRequests, std::memory_order_release);

	for (int32 PlatformIndex = 0; PlatformIndex < PlatformDatas.Num(); ++PlatformIndex)
	{
		FPlatformData& PlatformData = PlatformDatas[PlatformIndex];
		if (PlatformData.PackageNames.IsEmpty())
		{
			continue;
		}
		FFetchPlatformData& FetchPlatformData = ThreadSafeOnlyVars.FetchPlatforms[PlatformIndex];

		if (ThreadSafeOnlyVars.Cluster.IsIncrementalCook() // Only FetchCookAttachments if our cookmode supports it.
															// Otherwise keep them all empty
			&& !FetchPlatformData.bIsPlatformAgnosticPlatform // The PlatformAgnosticPlatform has no stored
															// CookAttachments; always use empty
			&& !FetchPlatformData.bIsCookerLoadingPlatform // The CookerLoadingPlatform has no stored CookAttachments;
															// always use empty
			)
		{
			TUniqueFunction<void(FName PackageName, UE::TargetDomain::FCookAttachments&& Result)> Callback =
				[this, PlatformIndex](FName PackageName, UE::TargetDomain::FCookAttachments&& Attachments)
			{
				RecordCacheResults(PackageName, PlatformIndex, MoveTemp(Attachments));
			};
			UE::TargetDomain::FCookAttachments::Fetch(PlatformData.PackageNames, FetchPlatformData.Platform,
				FetchPlatformData.Writer, MoveTemp(Callback));
		}
		else
		{
			// When we do not need to asynchronously fetch, we record empty cache results to keep the edgefetch
			// flow similar to the FetchCookAttachments case

			// Don't use a ranged-for, as we are not allowed to access this or this->PackageNames after the
			// last index, and ranged-for != at the end of the final loop iteration can read from PackageNames
			int32 NumPackageNames = PlatformData.PackageNames.Num();
			FName* PackageNamesData = PlatformData.PackageNames.GetData();
			for (int32 PackageNameIndex = 0; PackageNameIndex < NumPackageNames; ++PackageNameIndex)
			{
				FName PackageName = PackageNamesData[PackageNameIndex];
				UE::TargetDomain::FCookAttachments Attachments;
				RecordCacheResults(PackageName, PlatformIndex, MoveTemp(Attachments));
			}
		}
	}
}

void FRequestCluster::FQueryVertexBatch::RecordCacheResults(FName PackageName, int32 PlatformIndex,
	UE::TargetDomain::FCookAttachments&& CookAttachments)
{
	FVertexData* Vertex = Vertices.FindChecked(PackageName);
	FQueryPlatformData& PlatformData = Vertex->PlatformData[PlatformIndex];
	PlatformData.CookAttachments = MoveTemp(CookAttachments);

	EAsyncQueryStatus Expected = EAsyncQueryStatus::AsyncRequested;
	if (PlatformData.CompareExchangeAsyncQueryStatus(Expected, EAsyncQueryStatus::Complete))
	{
		// Kick the vertex if it has no more platforms in pending. Otherwise keep waiting and the later
		// call to RecordCacheResults will kick the vertex. Note that the "later call" might be another
		// call to RecordCacheResults on a different thread executing at the same time, and we are racing.
		// The last one to set CompareExchangeAsyncQueryStatus(EAsyncQueryStatus::Complete) will definitely
		// see all other values as complete, because we are using std::memory_order_release. It is possible
		// that both calls to RecordCacheResults will see all values complete, and we will kick it twice.
		// Kicking twice is okay; it is supported and is a noop.
		bool bAllPlatformsComplete = true;
		int32 LocalNumFetchPlatforms = ThreadSafeOnlyVars.NumFetchPlatforms();
		for (int32 OtherPlatformIndex = 0; OtherPlatformIndex < LocalNumFetchPlatforms; ++OtherPlatformIndex)
		{
			if (OtherPlatformIndex == PlatformIndex)
			{
				continue;
			}
			FQueryPlatformData& OtherPlatformData = Vertex->PlatformData[OtherPlatformIndex];
			EAsyncQueryStatus OtherStatus = OtherPlatformData.GetAsyncQueryStatus();
			if (EAsyncQueryStatus::AsyncRequested <= OtherStatus && OtherStatus < EAsyncQueryStatus::Complete)
			{
				bAllPlatformsComplete = false;
				break;
			}
		}
		if (bAllPlatformsComplete)
		{
			ThreadSafeOnlyVars.KickVertex(Vertex);
		}
	}

	if (NumPendingRequests.fetch_sub(1, std::memory_order_relaxed) == 1)
	{
		ThreadSafeOnlyVars.OnBatchCompleted(this);
		// *this is no longer accessible
	}
}

TMap<FPackageData*, TArray<FPackageData*>>& FRequestCluster::FGraphSearch::GetGraphEdges()
{
	return GraphEdges;
}

bool FRequestCluster::IsIncrementalCook() const
{
	return bAllowIterativeResults && COTFS.bHybridIterativeEnabled;
}

void FRequestCluster::IsRequestCookable(const ITargetPlatform* Platform, FPackageData& PackageData,
	UCookOnTheFlyServer& COTFS, ESuppressCookReason& OutReason, bool& bOutCookable, bool& bOutExplorable)
{
	FString LocalDLCPath;
	if (COTFS.CookByTheBookOptions->bErrorOnEngineContentUse)
	{
		LocalDLCPath = FPaths::Combine(*COTFS.GetBaseDirectoryForDLC(), TEXT("Content"));
		FPaths::MakeStandardFilename(LocalDLCPath);
	}

	IsRequestCookable(Platform, PackageData.GetPackageName(), PackageData, COTFS,
		LocalDLCPath, OutReason, bOutCookable, bOutExplorable);
}

void FRequestCluster::IsRequestCookable(const ITargetPlatform* Platform, FName PackageName, FPackageData& PackageData,
	ESuppressCookReason& OutReason, bool& bOutCookable, bool& bOutExplorable)
{
	return IsRequestCookable(Platform, PackageName, PackageData, COTFS,
		DLCPath, OutReason, bOutCookable, bOutExplorable);
}

void FRequestCluster::IsRequestCookable(const ITargetPlatform* Platform, FName PackageName, FPackageData& PackageData,
	UCookOnTheFlyServer& InCOTFS, FStringView InDLCPath, ESuppressCookReason& OutReason, bool& bOutCookable,
	bool& bOutExplorable)
{
	// IsRequestCookable should not be called for The CookerLoadingPlatform; it has different rules
	check(Platform != CookerLoadingPlatformKey);

	TStringBuilder<256> NameBuffer;
	// We need to reject packagenames from adding themselves or their transitive dependencies using all the same rules
	// that UCookOnTheFlyServer::ProcessRequest uses. Packages that are rejected from cook do not add their 
	// dependencies to the cook.
	PackageName.ToString(NameBuffer);
	if (FPackageName::IsScriptPackage(NameBuffer))
	{
		OutReason = ESuppressCookReason::ScriptPackage;
		bOutCookable = false;
		bOutExplorable = false;
		return;
	}

	FPackagePlatformData* PlatformData = PackageData.FindPlatformData(Platform);
	bool bExplorableOverride = PlatformData ? PlatformData->IsExplorableOverride() : false;
	ON_SCOPE_EXIT
	{
		bOutExplorable = bOutExplorable | bExplorableOverride;
	};

	FName FileName = PackageData.GetFileName();
	if (InCOTFS.PackageTracker->NeverCookPackageList.Contains(PackageName))
	{
		if (INDEX_NONE != UE::String::FindFirst(NameBuffer, ULevel::GetExternalActorsFolderName(), 
			ESearchCase::IgnoreCase))
		{
			// EXTERNALACTOR_TODO: Add a separate category for ExternalActors rather than putting them in
			// NeverCookPackageList and checking naming convention here.
			OutReason = ESuppressCookReason::NeverCook;
			bOutCookable = false;

			// EXTERNALACTOR_TODO: We want to explore externalactors, because they add references to the cook that will
			// otherwise not be found until the map package loads them and adds them as unsolicited packages
			// But some externalactor packages will never be loaded by the generator, and we don't have a way to
			// discover which ones will not be loaded until we load the Map and WorldPartition object.
			// So set them to explorable = false until we implement an interface to determine which actors will be
			// loaded up front.
			bOutExplorable = false;
		}
		else
		{
			UE_LOG(LogCook, Verbose,
				TEXT("Package %s is referenced but is in the never cook package list, discarding request"),
				*NameBuffer);
			OutReason = ESuppressCookReason::NeverCook;
			bOutCookable = false;
			bOutExplorable = false;
		}
		return;
	}

	if (InCOTFS.CookByTheBookOptions->bErrorOnEngineContentUse && !InDLCPath.IsEmpty())
	{
		FileName.ToString(NameBuffer);
		if (!FStringView(NameBuffer).StartsWith(InDLCPath))
		{
			// Editoronly content that was not cooked by the base game is allowed to be "cooked"; if it references
			// something not editoronly then we will exclude and give a warning on that followup asset. We need to
			// handle editoronly objects being referenced because the base game will not have marked them as cooked so
			// we will think we still need to "cook" them.
			// The only case where this comes up is in ObjectRedirectors, so we only test for those for performance.
			TArray<FAssetData> Assets;
			IAssetRegistry::GetChecked().GetAssetsByPackageName(PackageName, Assets,
				true /* bIncludeOnlyOnDiskAssets */);
			bool bEditorOnly = !Assets.IsEmpty() &&
				Algo::AllOf(Assets, [](const FAssetData& Asset)
					{
						return Asset.IsRedirector();
					});

			if (!bEditorOnly)
			{
				if (!PackageData.HasCookedPlatform(Platform, true /* bIncludeFailed */))
				{
					// AllowUncookedAssetReferences should only be used when the DLC plugin to cook is going to be
					// mounted where uncooked packages are available. This will allow a DLC plugin to be recooked 
					// continually and mounted in an uncooked editor which is useful for CI.
					if (!InCOTFS.CookByTheBookOptions->bAllowUncookedAssetReferences)
					{
						UE_LOG(LogCook, Error,
							TEXT("Uncooked Engine or Game content %s is being referenced by DLC!"), *NameBuffer);
					}
				}
				OutReason = ESuppressCookReason::NotInCurrentPlugin;
				bOutCookable = false;
				bOutExplorable = false;
				return;
			}
		}
	}

	// The package is ordinarily cookable and explorable. In some cases we filter out for testing
	// packages that are ordinarily cookable; set bOutCookable to false if so.
	bOutExplorable = true;
	if (InCOTFS.bCookFilter)
	{
		IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
		FName PackageNameToTest = PackageName;
		if (PackageData.IsGenerated())
		{
			FName ParentName = PackageData.GetParentGenerator();
			FPackageData* ParentData = InCOTFS.PackageDatas->FindPackageDataByPackageName(ParentName);
			if (ParentData)
			{
				PackageNameToTest = ParentName;
			}
		}

		if (!InCOTFS.CookFilterIncludedClasses.IsEmpty())
		{
			TOptional<FAssetPackageData> AssetData = AssetRegistry.GetAssetPackageDataCopy(PackageNameToTest);
			bool bIncluded = false;
			if (AssetData)
			{
				for (FName ClassName : AssetData->ImportedClasses)
				{
					if (InCOTFS.CookFilterIncludedClasses.Contains(ClassName))
					{
						bIncluded = true;
						break;
					}
				}
			}
			if (!bIncluded)
			{
				OutReason = ESuppressCookReason::CookFilter;
				bOutCookable = false;
				return;
			}
		}
		if (!InCOTFS.CookFilterIncludedAssetClasses.IsEmpty())
		{
			TArray<FAssetData> AssetDatas;
			AssetRegistry.GetAssetsByPackageName(PackageNameToTest, AssetDatas, true /* bIncludeOnlyDiskAssets */);
			bool bIncluded = false;
			for (FAssetData& AssetData : AssetDatas)
			{
				if (InCOTFS.CookFilterIncludedAssetClasses.Contains(FName(*AssetData.AssetClassPath.ToString())))
				{
					bIncluded = true;
					break;
				}
			}
			if (!bIncluded)
			{
				OutReason = ESuppressCookReason::CookFilter;
				bOutCookable = false;
				return;
			}
		}
	}

	OutReason = ESuppressCookReason::NotSuppressed;
	bOutCookable = true;
}

TConstArrayView<FName> FRequestCluster::GetLocalizationReferences(FName PackageName, UCookOnTheFlyServer& InCOTFS)
{
	if (!FPackageName::IsLocalizedPackage(WriteToString<256>(PackageName)))
	{
		TArray<FName>* Result = InCOTFS.CookByTheBookOptions->SourceToLocalizedPackageVariants.Find(PackageName);
		if (Result)
		{
			return TConstArrayView<FName>(*Result);
		}
	}
	return TConstArrayView<FName>();
}

TArray<FName> FRequestCluster::GetAssetManagerReferences(FName PackageName)
{
	TArray<FName> Results;
	UAssetManager::Get().ModifyCookReferences(PackageName, Results);
	return Results;
}

template <typename T>
static void ArrayShuffle(TArray<T>& Array)
{
	// iterate 0 to N-1, picking a random remaining vertex each loop
	int32 N = Array.Num();
	for (int32 I = 0; I < N; ++I)
	{
		Array.Swap(I, FMath::RandRange(I, N - 1));
	}
}

template <typename T>
static TArray<T> FindRootsFromLeafToRootOrderList(TConstArrayView<T> LeafToRootOrder, const TMap<T, TArray<T>>& Edges,
	const TSet<T>& ValidVertices)
{
	// Iteratively
	//    1) Add the leading rootward non-visited element to the root
	//    2) Visit all elements reachable from that root
	// This works because the input array is already sorted RootToLeaf, so we
	// know the leading element has no incoming edges from anything later.
	TArray<T> Roots;
	TSet<T> Visited;
	Visited.Reserve(LeafToRootOrder.Num());
	struct FVisitEntry
	{
		T Vertex;
		const TArray<T>* Edges;
		int32 NextEdge;
		void Set(T V, const TMap<T, TArray<T>>& AllEdges)
		{
			Vertex = V;
			Edges = AllEdges.Find(V);
			NextEdge = 0;
		}
	};
	TArray<FVisitEntry> DFSStack;
	int32 StackNum = 0;
	auto Push = [&DFSStack, &Edges, &StackNum](T Vertex)
	{
		while (DFSStack.Num() <= StackNum)
		{
			DFSStack.Emplace();
		}
		DFSStack[StackNum++].Set(Vertex, Edges);
	};
	auto Pop = [&StackNum]()
	{
		--StackNum;
	};

	for (T Root : ReverseIterate(LeafToRootOrder))
	{
		bool bAlreadyExists;
		Visited.Add(Root, &bAlreadyExists);
		if (bAlreadyExists)
		{
			continue;
		}
		Roots.Add(Root);

		Push(Root);
		check(StackNum == 1);
		while (StackNum > 0)
		{
			FVisitEntry& Entry = DFSStack[StackNum - 1];
			bool bPushed = false;
			while (Entry.Edges && Entry.NextEdge < Entry.Edges->Num())
			{
				T Target = (*Entry.Edges)[Entry.NextEdge++];
				Visited.Add(Target, &bAlreadyExists);
				if (!bAlreadyExists && ValidVertices.Contains(Target))
				{
					Push(Target);
					bPushed = true;
					break;
				}
			}
			if (!bPushed)
			{
				Pop();
			}
		}
	}
	return Roots;
}

} // namespace UE::Cook
