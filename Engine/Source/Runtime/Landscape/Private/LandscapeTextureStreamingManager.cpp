// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeTextureStreamingManager.h"
#include "Engine/Texture.h"
#include "TextureCompiler.h"
#include "LandscapePrivate.h"

namespace UE::Landscape
{
	// double check that a texture is forced resident
	static inline void EnsureTextureForcedResident(UTexture *Texture)
	{
		// if other systems mess with this flag, then restore it to what it should be
		// Any code that is directly messing with the flag on one of our
		// landscape related textures should go through this streaming system instead
		if (!ensure(Texture->bForceMiplevelsToBeResident))
		{
			Texture->bForceMiplevelsToBeResident = true;
		}
	}
}

TArray<FLandscapeTextureStreamingManager*> FLandscapeTextureStreamingManager::AllStreamingManagers;

bool FLandscapeTextureStreamingManager::RequestTextureFullyStreamedIn(UTexture* Texture, bool bWaitForStreaming)
{
	TWeakObjectPtr<UTexture> TexturePtr = Texture;
	FTextureState& State = TextureStates.FindOrAdd(TexturePtr);

	if (State.RequestCount == 0)
	{
		Texture->bForceMiplevelsToBeResident = true;
	}
	else
	{
		UE::Landscape::EnsureTextureForcedResident(Texture);
	}
	State.RequestCount++;

	if (IsTextureFullyStreamedIn(Texture))
	{
		return true;
	}
	else if (bWaitForStreaming)
	{
		Texture->WaitForStreaming();
		return IsTextureFullyStreamedIn(Texture);
	}
	return false;
}

bool FLandscapeTextureStreamingManager::RequestTextureFullyStreamedInForever(UTexture* Texture, bool bWaitForStreaming)
{
	TWeakObjectPtr<UTexture> TexturePtr = Texture;
	FTextureState& State = TextureStates.FindOrAdd(TexturePtr);
	State.bForever = true;
	Texture->bForceMiplevelsToBeResident = true;

	if (IsTextureFullyStreamedIn(Texture))
	{
		return true;
	}
	else if (bWaitForStreaming)
	{
		Texture->WaitForStreaming();
		return IsTextureFullyStreamedIn(Texture);
	}
	return false;
}

void FLandscapeTextureStreamingManager::UnrequestTextureFullyStreamedIn(UTexture* Texture)
{
	if (Texture == nullptr)
	{
		return;
	}

	TWeakObjectPtr<UTexture> TexturePtr = Texture;
	FTextureState* State = TextureStates.Find(TexturePtr);
	if (State)
	{
		if (State->RequestCount > 0)
		{
			State->RequestCount--;
			if (!State->WantsTextureStreamedIn())
			{
				// remove state tracking for this texture
				TextureStates.Remove(TexturePtr);
				if ((AllStreamingManagers.Num() == 1) || !AnyStreamingManagerWantsTextureStreamedIn(TexturePtr))
				{
					// allow stream out
					Texture->bForceMiplevelsToBeResident = false;
				}
				else
				{
					UE::Landscape::EnsureTextureForcedResident(Texture);
				}
			}
			else
			{
				UE::Landscape::EnsureTextureForcedResident(Texture);
			}
		}
		else
		{
			UE_LOG(LogLandscape, Warning, TEXT("Texture Streaming Manager received more Unrequests than Requests to stream texture %s"), *Texture->GetName());
		}
	}
}

bool FLandscapeTextureStreamingManager::WaitForTextureStreaming()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeTextureStreamingaAnager_WaitForTextureStreaming);
	bool bFullyStreamed = true;
	for (auto It = TextureStates.CreateIterator(); It; ++It)
	{
		UTexture* Texture = It.Key().Get();
		if (Texture)
		{
			UE::Landscape::EnsureTextureForcedResident(Texture);
			if (!IsTextureFullyStreamedIn(Texture))
			{
#if WITH_EDITOR
				// in editor, textures can be not compiled yet
				FTextureCompilingManager::Get().FinishCompilation({ Texture });
#endif // WITH_EDITOR
				Texture->WaitForStreaming();
			}
			bFullyStreamed = bFullyStreamed && IsTextureFullyStreamedIn(Texture);
		}
		else
		{
			// the texture was unloaded... we can remove this entry
			It.RemoveCurrent();
		}
	}
	return bFullyStreamed;
}

void FLandscapeTextureStreamingManager::CleanupPostGarbageCollect()
{
	for (auto It = TextureStates.CreateIterator(); It; ++It)
	{
		UTexture* Texture = It.Key().Get();
		if (Texture == nullptr)
		{
			It.RemoveCurrent();
		}
		else
		{
			// reset the texture force resident after garbage collection (which clears it sometimes)
			FTextureState& State = It.Value();
			if (State.WantsTextureStreamedIn())
			{
				Texture->bForceMiplevelsToBeResident = true;
			}
		}
	}
}

void FLandscapeTextureStreamingManager::CheckRequestedTextures()
{
#if WITH_EDITOR
	if (UndoDetector.bUndoRedoPerformed)
	{
		// the force mip levels resident flag sometimes gets cleared on an undo after landscape creation, but we can fix it
		// (otherwise we may wait forever for them to become resident)
		for (auto It = TextureStates.CreateIterator(); It; ++It)
		{
			if (UTexture* Texture = It.Key().Get())
			{
				FTextureState& State = It.Value();
				if (State.WantsTextureStreamedIn())
				{
					if (!Texture->bForceMiplevelsToBeResident)
					{
						Texture->bForceMiplevelsToBeResident = true;
					}
				}
			}
		}
		UndoDetector.bUndoRedoPerformed = false;
	}
#endif // WITH_EDITOR
}

bool FLandscapeTextureStreamingManager::IsTextureFullyStreamedIn(UTexture* InTexture)
{
	return InTexture &&
#if WITH_EDITOR
		!InTexture->IsDefaultTexture() &&
#endif // WITH_EDITOR
		!InTexture->HasPendingInitOrStreaming() && InTexture->IsFullyStreamedIn();
}

bool FLandscapeTextureStreamingManager::AnyStreamingManagerWantsTextureStreamedIn(TWeakObjectPtr<UTexture> TexturePtr)
{
	for (FLandscapeTextureStreamingManager* Manager : AllStreamingManagers)
	{
		if (FTextureState* State = Manager->TextureStates.Find(TexturePtr))
		{
			if (State->WantsTextureStreamedIn())
			{
				return true;
			}
		}
	}
	return false;
}

FLandscapeTextureStreamingManager::FLandscapeTextureStreamingManager()
{
	AllStreamingManagers.Add(this);
}

FLandscapeTextureStreamingManager::~FLandscapeTextureStreamingManager()
{
	AllStreamingManagers.RemoveSwap(this, EAllowShrinking::No);

	// there could be some textures still requested, if they were requested "forever".
	// since the world is going away, we can re-evaluate whether they should remain streamed or not.
	int32 RemainingRequests = 0;
	for (auto It = TextureStates.CreateIterator(); It; ++It)
	{
		FTextureState& State = It.Value();
		if (State.RequestCount > 0)
		{
			RemainingRequests++;
		}

		if (UTexture* Texture = It.Key().Get())
		{
			if (!AnyStreamingManagerWantsTextureStreamedIn(It.Key()))
			{
				// none of the remaining streaming managers request this texture, we can disable the mip requests
				Texture->bForceMiplevelsToBeResident = false;
			}
		}
	}

	if (RemainingRequests > 0)
	{
		UE_LOG(LogLandscape, Display, TEXT("At destruction, the Landscape Texture Streaming Manager still has streaming requests for %d Textures, this may indicate failure to clean them up."), RemainingRequests);
	}
}
