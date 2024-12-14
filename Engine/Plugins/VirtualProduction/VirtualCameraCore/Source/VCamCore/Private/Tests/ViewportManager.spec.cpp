// Copyright Epic Games, Inc. All Rights Reserved.

#include "EVCamTargetViewportID.h"
#include "VCamTestActor.h"
#include "ViewportLockerMock.h"
#include "Util/Viewport/ViewportManager.h"
#include "Util/Viewport/Interfaces/IViewportLocker.h"
#include "ViewportResolutionChangerMock.h"

#include "Misc/AutomationTest.h"
#include "Misc/Optional.h"
#include "PreviewScene.h"

namespace UE::VCamCore
{
	/** Tests FViewportLockManager in isolation. */
	BEGIN_DEFINE_SPEC(FViewportManagerSpec, "VirtualProduction.VCam.Viewport.Manager", EAutomationTestFlags::EngineFilter | EAutomationTestFlags_ApplicationContextMask)
		TUniquePtr<FPreviewScene> ScenePreview;
		AVCamTestActor* VCam1;
		AVCamTestActor* VCam2;

		TUniquePtr<FViewportLockerMock> ViewportLockMock;
		TUniquePtr<FViewportResolutionChangerMock> ResolutionChangerMock;
		TUniquePtr<FViewportManagerBase> ViewportManager;

		/** Tests will add providers here if they should have ownership over their target viewport */
		TSet<UVCamOutputProviderBase*> ProvidersWithOwnership;

		/** To avoid exposing a public Tick API to FViewportManagerBase, we'll just hackily execute the global FCoreDelegates::OnEndFrame. */
		FSimpleMulticastDelegate OnEndFrameBackup;

		void TickManager() { FCoreDelegates::OnEndFrame.Broadcast(); }
	END_DEFINE_SPEC(FViewportManagerSpec);

	void FViewportManagerSpec::Define()
	{
		BeforeEach([this]()
		{
			OnEndFrameBackup = MoveTemp(FCoreDelegates::OnEndFrame);
			
			// Disable these features because they are unnecessary
			ScenePreview = MakeUnique<FPreviewScene>(FPreviewScene::ConstructionValues()
				.SetCreatePhysicsScene(false)
				.SetForceMipsResident(false)
				.SetTransactional(false)
				);
			UWorld* World = ScenePreview->GetWorld();
			VCam1 = World->SpawnActor<AVCamTestActor>();
			VCam2 = World->SpawnActor<AVCamTestActor>();

			ViewportLockMock = MakeUnique<FViewportLockerMock>();
			ResolutionChangerMock = MakeUnique<FViewportResolutionChangerMock>();
			ViewportManager = MakeUnique<FViewportManagerBase>(
				*ViewportLockMock,
				*ResolutionChangerMock,
				FViewportManagerBase::FOverrideShouldHaveOwnership::CreateLambda([this](const UVCamOutputProviderBase& Output)
				{
					return ProvidersWithOwnership.Contains(&Output);
				}));

			ViewportManager->RegisterVCamComponent(*VCam1->GetVCamComponent());
			ViewportManager->RegisterVCamComponent(*VCam2->GetVCamComponent());
		});
		AfterEach([this]()
		{
			ScenePreview.Reset();
			VCam1 = nullptr;
			VCam2 = nullptr;

			ViewportManager.Reset();
			ViewportLockMock.Reset();
			ResolutionChangerMock.Reset();
			
			FCoreDelegates::OnEndFrame = MoveTemp(OnEndFrameBackup);
		});

		It("When user manually unpilots, camera is repiloted", [this]()
		{
			ProvidersWithOwnership.Add(VCam1->OutputProvider1);
			TickManager();
			// Simulate user clicking the unpilot button
			ViewportLockMock->LockedViewports[0] = false;
			TickManager();
			
			TestTrue(TEXT("Viewport is piloted again"), ViewportLockMock->LockedViewports[0]);
		});
	}
}

