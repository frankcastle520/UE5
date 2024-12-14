// Copyright Epic Games, Inc. All Rights Reserved.

#if defined(UE_WITH_IAS_TOOL)

#include "UploadQueue.h"

#include <Async/Async.h>
#include <CoreGlobals.h>
#include <Misc/OutputDeviceRedirector.h>
#include <IO/IoStoreOnDemand.h>
#include "S3/S3Client.h"


namespace UE::IoStore::Tool {


FUploadQueue::FUploadQueue(UE::FS3Client& InClient, const FString& InBucket, int32 ThreadCount)
	: Client(InClient)
	, Bucket(InBucket)
{
	ActiveThreadCount = ThreadCount;
	for (int32 Idx = 0; Idx < ThreadCount; ++Idx)
	{
		Threads.Add(AsyncThread([this]()
			{
				ThreadEntry();
			}));
	}
}

bool FUploadQueue::Enqueue(FStringView Key, FIoBuffer Payload)
{
	if (ActiveThreadCount == 0)
	{
		return false;
	}

	for (;;)
	{
		bool bEnqueued = false;
		{
			FScopeLock _(&CriticalSection);
			if (ConcurrentUploads < Threads.Num())
			{
				bEnqueued = Queue.Enqueue(FQueueEntry{ FString(Key), Payload });
			}
		}

		if (bEnqueued)
		{
			WakeUpEvent->Trigger();
			break;
		}

		UploadCompleteEvent->Wait();
	}

	return true;
}

void FUploadQueue::ThreadEntry()
{
	for (;;)
	{
		FQueueEntry Entry;
		bool bDequeued = false;
		{
			FScopeLock _(&CriticalSection);
			bDequeued = Queue.Dequeue(Entry);
		}

		if (!bDequeued)
		{
			if (bCompleteAdding)
			{
				break;
			}
			WakeUpEvent->Wait();
			continue;
		}

		ConcurrentUploads++;
		const FS3PutObjectResponse Response = Client.TryPutObject(FS3PutObjectRequest{ Bucket, Entry.Key, Entry.Payload.GetView() });
		ConcurrentUploads--;
		UploadCompleteEvent->Trigger();

		if (Response.IsOk())
		{
			UE_LOG(LogIoStoreOnDemand, Log, TEXT("Uploaded chunk '%s/%s/%s'"), *Client.GetConfig().ServiceUrl, *Bucket, *Entry.Key);
		}
		else
		{
			TStringBuilder<256> ErrorResponse;
			Response.GetErrorResponse(ErrorResponse);

			UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Failed to upload chunk '%s/%s/%s' (%s)"), *Client.GetConfig().ServiceUrl, *Bucket, *Entry.Key, ErrorResponse.ToString());
			ErrorCount++;
			break;
		}
	}

	ActiveThreadCount--;
}

bool FUploadQueue::Flush()
{
	bCompleteAdding = true;
	for (int32 Idx = 0; Idx < Threads.Num(); ++Idx)
	{
		WakeUpEvent->Trigger();
	}

	for (TFuture<void>& Thread : Threads)
	{
		Thread.Wait();
	}

	return ErrorCount == 0;
}

} // namespace UE::IoStore::Tool




#endif // UE_WITH_IAS_TOOL
