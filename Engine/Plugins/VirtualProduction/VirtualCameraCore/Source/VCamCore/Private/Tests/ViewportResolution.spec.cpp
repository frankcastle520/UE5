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
	BEGIN_DEFINE_SPEC(FViewportResolutionSpec, "VirtualProduction.VCam.Viewport.Resolution", EAutomationTestFlags::EngineFilter | EAutomationTestFlags_ApplicationContextMask)
		TUniquePtr<FPreviewScene> ScenePreview;
		AVCamTestActor* VCam1;
		AVCamTestActor* VCam2;

		TUniquePtr<FViewportResolutionChangerMock> ResolutionChangerMock;
		TUniquePtr<FViewportResolutionManager> ResolutionManager;

		/** Tests will add providers here if they should have ownership over their target viewport */
		TSet<UVCamOutputProviderBase*> ProvidersWithOwnership;
	END_DEFINE_SPEC(FViewportResolutionSpec);

	void FViewportResolutionSpec::Define()
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

			ResolutionChangerMock = MakeUnique<FViewportResolutionChangerMock>();
			ResolutionManager = MakeUnique<FViewportResolutionManager>(
				*ResolutionChangerMock,
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

			ResolutionManager.Reset();
			ResolutionChangerMock.Reset();
		});

		It("When provider gets ownership, override resolution is applied", [this]()
		{
			ProvidersWithOwnership.Add(VCam1->OutputProvider1);
			ResolutionManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });

			TestTrue(TEXT("Override resolution is set"), ResolutionChangerMock->OverrideResolutions[0] == AVCamTestActor::DefaultOverrideResolution());
		});
		
		It("When override resolution is switched off, viewport resolution is restored", [this]()
		{
			ProvidersWithOwnership.Add(VCam1->OutputProvider1);
			ResolutionManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });
			// This should reset the resolution
			VCam1->OutputProvider1->bUseOverrideResolution = false;
			ResolutionManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });
			
			TestTrue(TEXT("Override resolution is reset"), ResolutionChangerMock->OverrideResolutions[0] == FIntPoint::ZeroValue);
		});
		It("When override resolution is changed, viewport resolution is updated", [this]()
		{
			ProvidersWithOwnership.Add(VCam1->OutputProvider1);
			ResolutionManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });
			// This should update the resolution
			VCam1->OutputProvider1->OverrideResolution = { 42, 42 };
			ResolutionManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });
			
			TestTrue(TEXT("Override resolution is updated"), ResolutionChangerMock->OverrideResolutions[0] == FIntPoint{ 42, 42 });
		});

		It("When ownership is taken away, override resolution is reset", [this]()
		{
			ProvidersWithOwnership.Add(VCam1->OutputProvider1);
			ResolutionManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });
			// This should reset the resolution since nobody has ownership anymore
			ProvidersWithOwnership.Remove(VCam1->OutputProvider1);
			ResolutionManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });
			
			TestTrue(TEXT("Override resolution is reset"), ResolutionChangerMock->OverrideResolutions[0] == FIntPoint::ZeroValue);
		});

		It("When ownership is transferred, override resolution is set to new output provider", [this]()
		{
			ProvidersWithOwnership.Add(VCam1->OutputProvider1);
			ResolutionManager->UpdateViewportLockState({ VCam1->GetVCamComponent(), VCam2->GetVCamComponent() });
			
			// This should transfer from VCam1 > VCam2
			ProvidersWithOwnership.Remove(VCam1->OutputProvider1);
			ProvidersWithOwnership.Add(VCam2->OutputProvider1);
			VCam2->OutputProvider1->OverrideResolution = { 42, 21 };
			ResolutionManager->UpdateViewportLockState({ VCam1->GetVCamComponent(), VCam2->GetVCamComponent() });
			
			TestTrue(TEXT("Override resolution is updated"), ResolutionChangerMock->OverrideResolutions[0] == FIntPoint{ 42, 21 });
		});

		It("When viewport changes from 1 to 2, the resolution is on the new and old viewports are correct", [this]()
		{
			ProvidersWithOwnership.Add(VCam1->OutputProvider1);
			ResolutionManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });
			// This should switch from viewport 1 > viewport 2
			VCam1->OutputProvider1->InitTargetViewport(EVCamTargetViewportID::Viewport2);
			ResolutionManager->UpdateViewportLockState({ VCam1->GetVCamComponent() });
			
			TestTrue(TEXT("Viewport 1 override resolution is reset"), ResolutionChangerMock->OverrideResolutions[0] == FIntPoint::ZeroValue);
			TestTrue(TEXT("Viewport 2 override resolution is set"), ResolutionChangerMock->OverrideResolutions[1] == AVCamTestActor::DefaultOverrideResolution());
		});
	}
}

