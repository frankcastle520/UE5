// Copyright Epic Games, Inc. All Rights Reserved.

#include "EVCamTargetViewportID.h"
#include "VCamTestActor.h"
#include "ViewportLockerMock.h"
#include "Util/Viewport/ViewportManager.h"
#include "Util/Viewport/Interfaces/IViewportLocker.h"

#include "Misc/AutomationTest.h"
#include "Misc/Optional.h"
#include "PreviewScene.h"

namespace UE::VCamCore
{
	/** Tests FViewportLockManager in isolation. */
	BEGIN_DEFINE_SPEC(FViewportLockingSpec, "VirtualProduction.VCam.Viewport.Locking", EAutomationTestFlags::EngineFilter | EAutomationTestFlags_ApplicationContextMask)
		TUniquePtr<FPreviewScene> ScenePreview;
		AVCamTestActor* VCam1;
		AVCamTestActor* VCam2;
		AActor* CinematicLock;

		TUniquePtr<FViewportLockerMock> ViewportLockMock;
		TUniquePtr<FViewportLockManager> LockManager;

		/** Tests will add providers here if they should have ownership over their target viewport */
		TSet<UVCamOutputProviderBase*> ProvidersWithOwnership;
	END_DEFINE_SPEC(FViewportLockingSpec);

	void FViewportLockingSpec::Define()
	{
		BeforeEach([this]()
		{
			// Disable these features because they are unnecessary
			ScenePreview = MakeUnique<FPreviewScene>(FPreviewScene::ConstructionValues()
				.SetCreatePhysicsScene(false)
				.SetForceMipsResident(false)
				.SetTransactional(false)
				);
			UWorld* World = ScenePreview->GetWorld();
			VCam1 = World->SpawnActor<AVCamTestActor>();
			VCam2 = World->SpawnActor<AVCamTestActor>();
			CinematicLock = World->SpawnActor<AActor>();

			ViewportLockMock = MakeUnique<FViewportLockerMock>();
			LockManager = MakeUnique<FViewportLockManager>(
				*ViewportLockMock,
				FViewportLockManager::FHasViewportOwnership::CreateLambda([this](const UVCamOutputProviderBase& OutputProvider)
				{
					return ProvidersWithOwnership.Contains(&OutputProvider);
				}));
		});
		AfterEach([this]()
		{
			ScenePreview.Reset();
			VCam1 = nullptr;
			VCam2 = nullptr;
			CinematicLock = nullptr;

			LockManager.Reset();
			ViewportLockMock.Reset();
		});

		It("Giving ownership to output provider locks the viewport", [this]()
		{
			ProvidersWithOwnership.Add(VCam1->OutputProvider1);
			LockManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });

			TestTrue(TEXT("VCam1 has lock for viewport 1"), ViewportLockMock->GetActorLock(EVCamTargetViewportID::Viewport1).Get() == VCam1);
			TestTrue(TEXT("VCam1 does not have lock for viewport 2"), ViewportLockMock->GetActorLock(EVCamTargetViewportID::Viewport2).Get() == nullptr);
			TestTrue(TEXT("VCam1 does not have lock for viewport 2"), ViewportLockMock->GetActorLock(EVCamTargetViewportID::Viewport3).Get() == nullptr);
			TestTrue(TEXT("VCam1 does not have lock for viewport 2"), ViewportLockMock->GetActorLock(EVCamTargetViewportID::Viewport4).Get() == nullptr);
		});

		It("If VCam is set not to lock the viewport, no lock is applied", [this]()
		{
			FVCamViewportLocker NewLockState = VCam1->GetVCamComponent()->GetViewportLockState();
			NewLockState.SetLockState(EVCamTargetViewportID::Viewport1, false);
			VCam1->GetVCamComponent()->SetViewportLockState(NewLockState);
			
			ProvidersWithOwnership.Add(VCam1->OutputProvider1);
			LockManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });
			
			TestTrue(TEXT("VCam1 does not have lock for viewport 1"), ViewportLockMock->GetActorLock(EVCamTargetViewportID::Viewport1).Get() == nullptr);
		});

		It("Changing target viewport switches lock", [this]()
		{
			ProvidersWithOwnership.Add(VCam1->OutputProvider1);
			LockManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });
			
			// This changes it from viewport 1 to viewport 2
			VCam1->OutputProvider1->InitTargetViewport(EVCamTargetViewportID::Viewport2);
			LockManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });

			TestTrue(TEXT("VCam1 does not have lock on viewport 1"), ViewportLockMock->GetActorLock(EVCamTargetViewportID::Viewport1).Get() == nullptr);
			TestTrue(TEXT("VCam1 has lock on viewport 2"), ViewportLockMock->GetActorLock(EVCamTargetViewportID::Viewport2).Get() == VCam1);
		});

		It("Transfer ownership from VCam 1 to 2 updates lock", [this]()
		{
			ProvidersWithOwnership.Add(VCam1->OutputProvider1);
			LockManager->UpdateViewportLockState({ VCam1->GetVCamComponent(), VCam2->GetVCamComponent() });

			// This should give the lock from VCam1 -> VCam2
			ProvidersWithOwnership.Remove(VCam1->OutputProvider1);
			ProvidersWithOwnership.Add(VCam2->OutputProvider1);
			LockManager->UpdateViewportLockState({ VCam1->GetVCamComponent(), VCam2->GetVCamComponent() });
			
			TestTrue(TEXT("VCam2 has lock on viewport 1"), ViewportLockMock->GetActorLock(EVCamTargetViewportID::Viewport1).Get() == VCam2);
		});

		It("Removing ownership removes lock", [this]()
		{
			ProvidersWithOwnership.Add(VCam1->OutputProvider1);
			LockManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });
			
			ProvidersWithOwnership.Remove(VCam1->OutputProvider1);
			LockManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });
			
			TestTrue(TEXT("VCam1 does not have lock on viewport 1"), ViewportLockMock->GetActorLock(EVCamTargetViewportID::Viewport1).Get() == nullptr);
		});

		It("When viewport is locked externally", [this]()
		{
			ViewportLockMock->LockedViewports[0] = true;
			ViewportLockMock->FakeCinematicLocks[0] = CinematicLock;
				
			// Will not receive lock of FakeCinematicLocks
			ProvidersWithOwnership.Add(VCam1->OutputProvider1);
			LockManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });
			TestTrue(TEXT("VCam1 does not have lock on viewport 1"), ViewportLockMock->GetActorLock(EVCamTargetViewportID::Viewport1).Get() == nullptr);
			
			// Now it will receive the lock
			ViewportLockMock->FakeCinematicLocks[0] = nullptr;
			LockManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });
			TestTrue(TEXT("VCam1 has lock on viewport 1"), ViewportLockMock->GetActorLock(EVCamTargetViewportID::Viewport1).Get() == VCam1);
		});

		Describe("When viewport is locked externally", [this]()
		{
			It("No lock is applied", [this]()
			{
				ViewportLockMock->LockedViewports[0] = true;
				ViewportLockMock->FakeCinematicLocks[0] = CinematicLock;
					
				// Will not receive lock of FakeCinematicLocks
				ProvidersWithOwnership.Add(VCam1->OutputProvider1);
				LockManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });
				TestTrue(TEXT("VCam1 does not have lock on viewport 1"), ViewportLockMock->GetActorLock(EVCamTargetViewportID::Viewport1).Get() == nullptr);
			});
			
			It("Regains lock when lock is lifted", [this]()
			{
				ViewportLockMock->LockedViewports[0] = true;
				ViewportLockMock->FakeCinematicLocks[0] = CinematicLock;
					
				// Will not receive lock of FakeCinematicLocks
				ProvidersWithOwnership.Add(VCam1->OutputProvider1);
				LockManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });
				
				// Now it will receive the lock
				ViewportLockMock->FakeCinematicLocks[0] = nullptr;
				LockManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });
				TestTrue(TEXT("VCam1 has lock on viewport 1"), ViewportLockMock->GetActorLock(EVCamTargetViewportID::Viewport1).Get() == VCam1);
			});
			
			It("Lock is applied if external lock actor missing", [this]()
			{
				ViewportLockMock->LockedViewports[0] = true;
				// Do not set FakeCinematicLocks
				
				ProvidersWithOwnership.Add(VCam1->OutputProvider1);
				LockManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });
				TestTrue(TEXT("VCam1 has lock on viewport 1"), ViewportLockMock->GetActorLock(EVCamTargetViewportID::Viewport1).Get() == VCam1);
			});

			It("Lock is applied if external lock actor is set but not set to lock", [this]()
			{
				// Do not set ViewportLockMock->LockedViewports
				ViewportLockMock->FakeCinematicLocks[0] = CinematicLock;
				
				ProvidersWithOwnership.Add(VCam1->OutputProvider1);
				LockManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });
				TestTrue(TEXT("VCam1 has lock on viewport 1"), ViewportLockMock->GetActorLock(EVCamTargetViewportID::Viewport1).Get() == VCam1);
			});
		});
	}
}

