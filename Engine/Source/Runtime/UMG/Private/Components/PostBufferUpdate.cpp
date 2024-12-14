// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/PostBufferUpdate.h"

#include "Slate/SPostBufferUpdate.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PostBufferUpdate)

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// UPostBufferUpdate

UPostBufferUpdate::UPostBufferUpdate()
{
	bPerformDefaultPostBufferUpdate = true;
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	BuffersToUpdate = {};
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	UpdateBufferInfos = {};
}

void UPostBufferUpdate::SetPerformDefaultPostBufferUpdate(bool bInPerformDefaultPostBufferUpdate)
{
	if (bPerformDefaultPostBufferUpdate != bInPerformDefaultPostBufferUpdate)
	{
		bPerformDefaultPostBufferUpdate = bInPerformDefaultPostBufferUpdate;
		if (MyPostBufferUpdate)
		{
			MyPostBufferUpdate->SetPerformDefaultPostBufferUpdate(bPerformDefaultPostBufferUpdate);
		}
	}
}

TSharedRef<SWidget> UPostBufferUpdate::RebuildWidget()
{
	MyPostBufferUpdate = SNew(SPostBufferUpdate)
		.bPerformDefaultPostBufferUpdate(bPerformDefaultPostBufferUpdate);

	bool bSetBuffersToUpdate = true;

#if WITH_EDITOR
	if (IsDesignTime())
	{
		bSetBuffersToUpdate = false;
	}
#endif // WITH_EDITOR

	if (bSetBuffersToUpdate)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (!UpdateBufferInfos.IsEmpty())
		{
			TArray<ESlatePostRT> TempBuffersToUpdate;
			TMap<ESlatePostRT, TSharedPtr<FSlatePostProcessorUpdaterProxy>> TempProcessorUpdaters;

			for (const FSlatePostBufferUpdateInfo& UpdateBufferInfo : UpdateBufferInfos)
			{
				TempBuffersToUpdate.Add(UpdateBufferInfo.BufferToUpdate);
				if (TObjectPtr<USlatePostBufferProcessorUpdater> PostParamUpdater = UpdateBufferInfo.PostParamUpdater)
				{
					TSharedPtr<FSlatePostProcessorUpdaterProxy> PostParamUpdaterProxy = PostParamUpdater->GetRenderThreadProxy();
					PostParamUpdaterProxy->bSkipBufferUpdate = PostParamUpdater->bSkipBufferUpdate;
					TempProcessorUpdaters.Add(UpdateBufferInfo.BufferToUpdate, PostParamUpdaterProxy);
				}
			}

			MyPostBufferUpdate->SetBuffersToUpdate(TempBuffersToUpdate);
			MyPostBufferUpdate->SetProcessorUpdaters(TempProcessorUpdaters);
		}
		else
		{
			MyPostBufferUpdate->SetBuffersToUpdate(BuffersToUpdate);
		}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	return MyPostBufferUpdate.ToSharedRef();
}

void UPostBufferUpdate::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	if (!MyPostBufferUpdate.IsValid())
	{
		return;
	}

	MyPostBufferUpdate->SetPerformDefaultPostBufferUpdate(bPerformDefaultPostBufferUpdate);

	bool bSetBuffersToUpdate = true;

#if WITH_EDITOR
	if (IsDesignTime())
	{
		bSetBuffersToUpdate = false;
	}
#endif // WITH_EDITOR

	if (bSetBuffersToUpdate)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (!UpdateBufferInfos.IsEmpty())
		{
			TArray<ESlatePostRT> TempBuffersToUpdate;
			TMap<ESlatePostRT, TSharedPtr<FSlatePostProcessorUpdaterProxy>> TempProcessorUpdaters;

			for (const FSlatePostBufferUpdateInfo& UpdateBufferInfo : UpdateBufferInfos)
			{
				TempBuffersToUpdate.Add(UpdateBufferInfo.BufferToUpdate);
				if (TObjectPtr<USlatePostBufferProcessorUpdater> PostParamUpdater = UpdateBufferInfo.PostParamUpdater)
				{
					TSharedPtr<FSlatePostProcessorUpdaterProxy> PostParamUpdaterProxy = PostParamUpdater->GetRenderThreadProxy();
					PostParamUpdaterProxy->bSkipBufferUpdate = PostParamUpdater->bSkipBufferUpdate;
					TempProcessorUpdaters.Add(UpdateBufferInfo.BufferToUpdate, PostParamUpdaterProxy);
				}
			}

			MyPostBufferUpdate->SetBuffersToUpdate(TempBuffersToUpdate);
			MyPostBufferUpdate->SetProcessorUpdaters(TempProcessorUpdaters);
		}
		else
		{
			MyPostBufferUpdate->SetBuffersToUpdate(BuffersToUpdate);
		}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}

UMG_API void UPostBufferUpdate::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	if (MyPostBufferUpdate.IsValid())
	{
		MyPostBufferUpdate->ReleasePostBufferUpdater();
	}

	MyPostBufferUpdate.Reset();
}

/////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE

