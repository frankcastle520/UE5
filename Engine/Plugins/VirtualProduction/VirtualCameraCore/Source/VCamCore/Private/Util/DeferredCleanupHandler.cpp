// Copyright Epic Games, Inc. All Rights Reserved.

#include "DeferredCleanupHandler.h"

#if WITH_EDITOR
#include "VCamComponent.h"
#include "RenderingThread.h"
#include "EditorSupportDelegates.h"
#endif

namespace UE::VCamCore
{
	FDeferredCleanupHandler::FDeferredCleanupHandler()
	{
#if WITH_EDITOR
		FEditorSupportDelegates::PrepareToCleanseEditorObject.AddRaw(this, &FDeferredCleanupHandler::OnPrepareToCleanseEditor);
		FEditorSupportDelegates::CleanseEditor.AddRaw(this, &FDeferredCleanupHandler::OnCleanseEditor);
#endif
	}

	FDeferredCleanupHandler::~FDeferredCleanupHandler()
	{
#if WITH_EDITOR
		FEditorSupportDelegates::PrepareToCleanseEditorObject.RemoveAll(this);
		FEditorSupportDelegates::CleanseEditor.RemoveAll(this);
#endif
	}

	void FDeferredCleanupHandler::OnInitializeVCam(UVCamComponent& Component)
	{
#if WITH_EDITOR
		KnownVCams.AddUnique(&Component);
#endif
	}

	void FDeferredCleanupHandler::OnDeinitializeVCam(UVCamComponent& Component)
	{
#if WITH_EDITOR
		KnownVCams.RemoveSingle(&Component);

		// In case some VCam we do not know about deinitializes, always do the flush on clean-up.
		bNeedsFlush = true;
#endif
	}

#if WITH_EDITOR
	void FDeferredCleanupHandler::OnPrepareToCleanseEditor(UObject* Object)
	{
		// Avoid modifying KnownVCams during iteration since the SetEnabled would call OnDeinitializeVCam (thus modifying KnownVCams). 
		TArray<UVCamComponent*> ComponentsToDeinitialize;
		for (auto It = KnownVCams.CreateIterator(); It; ++It)
		{
			UVCamComponent* Component = It->Get();
			if (!Component)
			{
				It.RemoveCurrent();
				continue;
			}

			if (Component->IsIn(Object))
			{
				ComponentsToDeinitialize.Add(Component);
			}
		}

		if (ComponentsToDeinitialize.IsEmpty())
		{
			return;
		}

		bNeedsFlush = true;
		for (UVCamComponent* Component : ComponentsToDeinitialize)
		{
			Component->SetEnabled(false);
		}
	}
#endif
	
#if WITH_EDITOR
	void FDeferredCleanupHandler::OnCleanseEditor()
	{
		if (bNeedsFlush)
		{
			bNeedsFlush = false;
			
			// This will cause all pending FDeferredCleanupInterface resources indirectly created by the VCams to be cleaned up by the render thread.
			FlushRenderingCommands();
		}
	}
#endif
}
