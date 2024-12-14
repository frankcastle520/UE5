// Copyright Epic Games, Inc. All Rights Reserved.

#include "EVCamTargetViewportID.h"
#include "Util/Viewport/OwnershipMapping.h"

#include "Misc/AutomationTest.h"
#include "Misc/Optional.h"

namespace UE::VCamCore
{
	BEGIN_DEFINE_SPEC(FOwnershipMappingSpec, "VirtualProduction.VCam.Viewport.OwnershipMapping", EAutomationTestFlags::EngineFilter | EAutomationTestFlags_ApplicationContextMask)
		using FOwner = int32;
		struct FOwnerData
		{
			EVCamTargetViewportID Viewport;
			TOptional<FOwner> NewOwner;
		};
		
		TOwnershipMapping<EVCamTargetViewportID, FOwner> Ownership;
		TOptional<FOwnerData> LastOwnershipChangeInvocation;
		
		const FOwner VCam1 = 0;
		const FOwner VCam2 = 1;
		const TOptional<FOwner> NoOwner;

		void TestDelegateExecuted(EVCamTargetViewportID ExpectedViewport, TOptional<FOwner> ExpectedOwer)
		{
			TestTrue(TEXT("Delegate executed"), LastOwnershipChangeInvocation.IsSet());
			TestTrue(TEXT("Viewport"), LastOwnershipChangeInvocation && LastOwnershipChangeInvocation->Viewport == ExpectedViewport);

			// This stuff makes it easier to read the output log
			if (ExpectedOwer)
			{
				if (!LastOwnershipChangeInvocation->NewOwner)
				{
					AddError(TEXT("Expected owner but none is set"));
				}
				else
				{
					TestEqual(TEXT("Owner"), *LastOwnershipChangeInvocation->NewOwner, *ExpectedOwer);
				}
			}
			else
			{
				TestTrue(TEXT("No owner"), LastOwnershipChangeInvocation && LastOwnershipChangeInvocation->NewOwner == ExpectedOwer);
			}
		}
	
	END_DEFINE_SPEC(FOwnershipMappingSpec);

	void FOwnershipMappingSpec::Define()
	{
		BeforeEach([this]()
		{
			Ownership.Clear();
			Ownership.OnOwnershipChanged().AddLambda([this](const EVCamTargetViewportID& Thing, const TOptional<FOwner> NewOwner)
			{
				LastOwnershipChangeInvocation = { Thing, NewOwner };
			});
			LastOwnershipChangeInvocation.Reset();
		});
		AfterEach([this]()
		{
			Ownership.OnOwnershipChanged().Clear();
			Ownership.Clear();
		});

		It("Can take ownership", [this]()
		{
			const bool bHasOwnership = Ownership.TryTakeOwnership(VCam1, EVCamTargetViewportID::Viewport1);

			TestTrue(TEXT("Return value"), bHasOwnership);
			
			TestTrue(TEXT("HasOwner"), Ownership.HasOwner(EVCamTargetViewportID::Viewport1));
			TestTrue(TEXT("IsOwnedBy"), Ownership.IsOwnedBy(EVCamTargetViewportID::Viewport1, VCam1));
			TestTrue(TEXT("GetOwner"), Ownership.GetOwner(EVCamTargetViewportID::Viewport1) && *Ownership.GetOwner(EVCamTargetViewportID::Viewport1) == VCam1);
		});

		It("Can release ownership (single)", [this]()
		{
			Ownership.TryTakeOwnership(VCam1, EVCamTargetViewportID::Viewport1);
			Ownership.TryTakeOwnership(VCam1, EVCamTargetViewportID::Viewport2);
			Ownership.ReleaseOwnership(VCam1, EVCamTargetViewportID::Viewport1);

			TestDelegateExecuted(EVCamTargetViewportID::Viewport1, NoOwner);

			// Removed Viewport 1
			TestFalse(TEXT("HasOwner(Viewport1)"), Ownership.HasOwner(EVCamTargetViewportID::Viewport1));
			TestFalse(TEXT("IsOwnedBy(Viewport1)"), Ownership.IsOwnedBy(EVCamTargetViewportID::Viewport1, VCam1));
			TestTrue(TEXT("GetOwner(Viewport1)"), Ownership.GetOwner(EVCamTargetViewportID::Viewport1) == nullptr);
			
			// Retain Viewport2
			TestTrue(TEXT("HasOwner(Viewport2)"), Ownership.HasOwner(EVCamTargetViewportID::Viewport2));
			TestTrue(TEXT("IsOwnedBy(Viewport2)"), Ownership.IsOwnedBy(EVCamTargetViewportID::Viewport2, VCam1));
			TestTrue(TEXT("GetOwner(Viewport2)"), Ownership.GetOwner(EVCamTargetViewportID::Viewport2) && *Ownership.GetOwner(EVCamTargetViewportID::Viewport2) == VCam1);
		});

		It("Can release ownership (all)", [this]()
		{
			Ownership.TryTakeOwnership(VCam1, EVCamTargetViewportID::Viewport1);
			Ownership.TryTakeOwnership(VCam1, EVCamTargetViewportID::Viewport2);

			int32 NumInvocations = 0;
			TSet<EVCamTargetViewportID> RemoveViewports;
			Ownership.OnOwnershipChanged().AddLambda([this, &NumInvocations, &RemoveViewports](EVCamTargetViewportID Viewport, const TOptional<FOwner>& NewOwner)
			{
				++NumInvocations;
				RemoveViewports.Add(Viewport);
				TestFalse(TEXT("Removed ownership"), NewOwner.IsSet());
			});
			Ownership.ReleaseOwnership(VCam1);

			TestEqual(TEXT("NumInvocations == 2"), NumInvocations, 2);
			TestTrue(TEXT("Invoked with both viewports"), RemoveViewports.Contains(EVCamTargetViewportID::Viewport1) && RemoveViewports.Contains(EVCamTargetViewportID::Viewport2));
			
			// Removed Viewport 1
			TestFalse(TEXT("HasOwner(Viewport1)"), Ownership.HasOwner(EVCamTargetViewportID::Viewport1));
			TestFalse(TEXT("IsOwnedBy(Viewport1)"), Ownership.IsOwnedBy(EVCamTargetViewportID::Viewport1, VCam1));
			TestTrue(TEXT("GetOwner(Viewport1)"), Ownership.GetOwner(EVCamTargetViewportID::Viewport1) == nullptr);
			
			// Removed Viewport 2
			TestFalse(TEXT("HasOwner(Viewport2)"), Ownership.HasOwner(EVCamTargetViewportID::Viewport2));
			TestFalse(TEXT("IsOwnedBy(Viewport2)"), Ownership.IsOwnedBy(EVCamTargetViewportID::Viewport2, VCam1));
			TestTrue(TEXT("GetOwner(Viewport2("), Ownership.GetOwner(EVCamTargetViewportID::Viewport2) == nullptr);
		});

		It("Ownership is queued", [this]()
		{
			Ownership.TryTakeOwnership(VCam1, EVCamTargetViewportID::Viewport1);
			Ownership.TryTakeOwnership(VCam2, EVCamTargetViewportID::Viewport1);
			Ownership.ReleaseOwnership(VCam1);

			TestDelegateExecuted(EVCamTargetViewportID::Viewport1, VCam2);
		});
	}
}

