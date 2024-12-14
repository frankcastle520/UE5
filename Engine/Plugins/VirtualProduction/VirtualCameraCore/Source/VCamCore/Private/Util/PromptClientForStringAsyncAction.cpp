// Copyright Epic Games, Inc. All Rights Reserved.

#include "Util/PromptClientForStringAsyncAction.h"

#include "Algo/Find.h"
#include "Async/Async.h"
#include "VCamComponent.h"

UPromptClientForStringAsyncAction* UPromptClientForStringAsyncAction::PromptClientForString(UVCamComponent* VCamComponent, FText PromptTitle, const FString& DefaultValue)
{
	UPromptClientForStringAsyncAction* AsyncAction = NewObject<UPromptClientForStringAsyncAction>();
	AsyncAction->VCamComponent = VCamComponent;

	FVCamStringPromptRequest Request;
	Request.DefaultValue = DefaultValue;
	Request.PromptTitle = PromptTitle.ToString();

	AsyncAction->PromptRequest = Request;

	return AsyncAction;
}

void UPromptClientForStringAsyncAction::Activate()
{
	Super::Activate();

	if (!VCamComponent)
	{
		return;
	}

	TArray<UVCamOutputProviderBase*> Providers;
	VCamComponent->GetAllOutputProviders(Providers);

	TFuture<FVCamStringPromptResponse> ResponseFuture;

	// Try calling the function on all providers until we find one that doesn't immediately return Unavailable
	UVCamOutputProviderBase** ValidProvider = Algo::FindByPredicate(Providers, [this, &ResponseFuture](UVCamOutputProviderBase* Provider)
	{
		ResponseFuture = Provider->PromptClientForString(PromptRequest);

		return !ResponseFuture.IsReady() || ResponseFuture.Get().Result != EVCamStringPromptResult::Unavailable;
	});

	if (ValidProvider)
	{
		ResponseFuture.Next([WeakThis = TWeakObjectPtr<UPromptClientForStringAsyncAction>(this)](FVCamStringPromptResponse&& Response)
		{
			if (IsInGameThread())
			{
				// As per documentation, IsValid is unsafe to call outside of the game thread
				if (WeakThis.IsValid())
				{
					WeakThis->OnCompleted.Broadcast(MoveTemp(Response));
				}
			}
			else
			{
				// Result is on stack... do not reference in latent operation.
				FVCamStringPromptResponse ResponseCopy = MoveTemp(Response);
				AsyncTask(ENamedThreads::GameThread, [WeakThis, Response = MoveTemp(ResponseCopy)]() mutable
				{
					if (WeakThis.IsValid())
					{
						WeakThis->OnCompleted.Broadcast(MoveTemp(Response));
					}
				});
			}
		});
	}
	else
	{
		OnCompleted.Broadcast(FVCamStringPromptResponse(EVCamStringPromptResult::Unavailable));
	}
}