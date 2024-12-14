// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetDefaultMode.h"

#include "EditorViewportClient.h"
#include "AssetEditorModeManager.h"
#include "EngineUtils.h"
#include "IKRigDebugRendering.h"
#include "IPersonaPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetHitProxies.h"
#include "ReferenceSkeleton.h"


#define LOCTEXT_NAMESPACE "IKRetargetDefaultMode"

FName FIKRetargetDefaultMode::ModeName("IKRetargetAssetDefaultMode");

bool FIKRetargetDefaultMode::GetCameraTarget(FSphere& OutTarget) const
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false;
	}
	return Controller->GetCameraTargetForSelection(OutTarget);
}

IPersonaPreviewScene& FIKRetargetDefaultMode::GetAnimPreviewScene() const
{
	return *static_cast<IPersonaPreviewScene*>(static_cast<FAssetEditorModeManager*>(Owner)->GetPreviewScene());
}

void FIKRetargetDefaultMode::Initialize()
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}

	bIsInitialized = true;
}

void FIKRetargetDefaultMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);

	if (!EditorController.IsValid())
	{
		return;
	}
	
	const FIKRetargetEditorController* Controller = EditorController.Pin().Get();

	// render source and target skeletons
	Controller->RenderSkeleton(PDI, ERetargetSourceOrTarget::Source);
	Controller->RenderSkeleton(PDI, ERetargetSourceOrTarget::Target);

	// render all the chain and root debug proxies
	RenderDebugProxies(PDI, Controller);
}

void FIKRetargetDefaultMode::RenderDebugProxies(FPrimitiveDrawInterface* PDI, const FIKRetargetEditorController* Controller) const
{
	const UIKRetargeter* Asset = Controller->AssetController->GetAsset();
	if (!Asset->bDebugDraw)
	{
		return;
	}
	
	const UIKRetargetProcessor* RetargetProcessor = Controller->GetRetargetProcessor();
	if (!(RetargetProcessor && RetargetProcessor->IsInitialized()))
	{
		return;
	}

	UDebugSkelMeshComponent* TargetSkelMesh = Controller->GetSkeletalMeshComponent(ERetargetSourceOrTarget::Target);
	const FTransform ComponentTransform = TargetSkelMesh->GetComponentTransform();
	const float ComponentScale = ComponentTransform.GetScale3D().GetMax();

	const TArray<FName>& SelectedChains = Controller->GetSelectedChains();

	constexpr FLinearColor Muted = FLinearColor(0.5,0.5,0.5, 0.5);
	const FLinearColor SourceColor = (FLinearColor::Gray * FLinearColor::Blue) * Muted;
	const FLinearColor GoalColor = FLinearColor::Yellow;
	const FLinearColor MainColor = FLinearColor::Green;
	const FLinearColor NonSelected = FLinearColor::Gray * 0.3f;

	// draw IK goals on each IK chain
	if (Asset->bDrawFinalGoals || Asset->bDrawSourceLocations)
	{
		// get the root modification
		const FRootRetargeter& RootRetargeter = RetargetProcessor->GetRootRetargeter();
		const FVector RootModification = RootRetargeter.Target.RootTranslationDelta * RootRetargeter.Settings.GetAffectIKWeightVector();
		
		// spin through all IK chains
		const TArray<FRetargetChainPairIK>& IKChainPairs = RetargetProcessor->GetIKChainPairs();
		for (const FRetargetChainPairIK& IKChainPair : IKChainPairs)
		{
			const FChainDebugData& ChainDebugData = IKChainPair.IKChainRetargeter.DebugData;
			FTransform FinalTransform = ChainDebugData.OutputTransformEnd * ComponentTransform;

			const bool bIsSelected = SelectedChains.Contains(IKChainPair.TargetBoneChainName);

			PDI->SetHitProxy(new HIKRetargetEditorChainProxy(IKChainPair.TargetBoneChainName));

			if (Asset->bDrawFinalGoals)
			{
				IKRigDebugRendering::DrawWireCube(
				PDI,
				FinalTransform,
				bIsSelected ? GoalColor : GoalColor * NonSelected,
				Asset->ChainDrawSize,
				Asset->ChainDrawThickness * ComponentScale);
			}
		
			if (Asset->bDrawSourceLocations)
			{
				const FSourceChainIK& SourceChain = IKChainPair.IKChainRetargeter.Source;
				FTransform SourceGoalTransform;
				SourceGoalTransform.SetTranslation(SourceChain.CurrentEndPosition + RootModification);
				SourceGoalTransform.SetRotation(SourceChain.CurrentEndRotation);
				SourceGoalTransform *= ComponentTransform;

				FLinearColor Color = bIsSelected ? SourceColor : SourceColor * NonSelected;

				DrawWireSphere(
					PDI,
					SourceGoalTransform,
					Color,
					Asset->ChainDrawSize * 0.5f,
					12,
					SDPG_World,
					0.0f,
					0.001f,
					false);

				if (Asset->bDrawFinalGoals)
				{
					DrawDashedLine(
						PDI,
						SourceGoalTransform.GetLocation(),
						FinalTransform.GetLocation(),
						Color,
						1.0f,
						SDPG_Foreground);
				}
			}

			// done drawing chain proxies
			PDI->SetHitProxy(nullptr);
		}
	}
	

	// draw lines on each FK chain
	if (Asset->bDrawChainLines || Asset->bDrawSingleBoneChains)
	{
		const TArray<FRetargetChainPairFK>& FKChainPairs = RetargetProcessor->GetFKChainPairs();
		for (const FRetargetChainPairFK& FKChainPair : FKChainPairs)
		{
			const TArray<int32>& TargetChainBoneIndices = FKChainPair.FKDecoder.BoneIndices;
			if (TargetChainBoneIndices.IsEmpty())
			{
				continue;
			}
		
			const bool bIsSelected = SelectedChains.Contains(FKChainPair.TargetBoneChainName);
			FLinearColor Color = bIsSelected ? MainColor : MainColor * NonSelected;
		
			// draw a line from start to end of chain, or in the case of a chain with only 1 bone in it, draw a sphere
			PDI->SetHitProxy(new HIKRetargetEditorChainProxy(FKChainPair.TargetBoneChainName));
			if (Asset->bDrawChainLines && TargetChainBoneIndices.Num() > 1)
			{
				FTransform StartTransform = TargetSkelMesh->GetBoneTransform(TargetChainBoneIndices[0], ComponentTransform);
				FTransform EndTransform = TargetSkelMesh->GetBoneTransform(TargetChainBoneIndices.Last(), ComponentTransform);
				PDI->DrawLine(
				StartTransform.GetLocation(),
				EndTransform.GetLocation(),
				Color,
				SDPG_Foreground,
				Asset->ChainDrawThickness * ComponentScale);
			}
			else if (Asset->bDrawSingleBoneChains)
			{
				// single bone chain, just draw a sphere on the bone
				FTransform BoneTransform = TargetSkelMesh->GetBoneTransform(TargetChainBoneIndices[0], ComponentTransform);
				DrawWireSphere(
					PDI,
					BoneTransform,
					Color,
					Asset->ChainDrawSize,
					12,
					SDPG_World,
					Asset->ChainDrawThickness * ComponentScale,
					0.001f,
					false);
			}
		
			PDI->SetHitProxy(nullptr);
		}
	}

	// draw stride warping frame
	if (Asset->bDrawWarpingFrame)
	{
		FTransform WarpingFrame = RetargetProcessor->DebugData.StrideWarpingFrame * ComponentTransform;
		DrawCoordinateSystem(
			PDI,
			WarpingFrame.GetLocation(),
			WarpingFrame.GetRotation().Rotator(),
			Asset->ChainDrawSize * ComponentScale,
			SDPG_World,
			Asset->ChainDrawThickness * ComponentScale);	
	}

	// root bone name
	if (Asset->bDrawRootCircle)
	{
		const FName RootBoneName = Controller->AssetController->GetRetargetRootBone(ERetargetSourceOrTarget::Target);
		const int32 RootBoneIndex = TargetSkelMesh->GetReferenceSkeleton().FindBoneIndex(RootBoneName);
		if (RootBoneIndex != INDEX_NONE)
		{
			const FTransform RootTransform = TargetSkelMesh->GetBoneTransform(RootBoneIndex, ComponentTransform);
			const FVector RootCircleLocation = RootTransform.GetLocation() * FVector(1,1,0);
			const bool bIsSelected = EditorController.Pin()->GetRootSelected();
			const FLinearColor RootColor = bIsSelected ? MainColor : MainColor * NonSelected;
	
			PDI->SetHitProxy(new HIKRetargetEditorRootProxy());
			DrawCircle(
				PDI,
				RootCircleLocation,
				FVector(1, 0, 0),
				FVector(0, 1, 0),
				RootColor,
				Asset->ChainDrawSize * 10.f * ComponentTransform.GetScale3D().GetMax(),
				30,
				SDPG_World,
				Asset->ChainDrawThickness * 2.0f * ComponentScale);
			PDI->SetHitProxy(nullptr);
		}	
	}
}

bool FIKRetargetDefaultMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}

	const bool bLeftButtonClicked = Click.GetKey() == EKeys::LeftMouseButton;
	const bool bCtrlOrShiftHeld = Click.IsControlDown() || Click.IsShiftDown();
	const ESelectionEdit EditMode = bCtrlOrShiftHeld ? ESelectionEdit::Add : ESelectionEdit::Replace;
	
	// did we click on a bone in the viewport?
	const bool bHitBone = HitProxy && HitProxy->IsA(HIKRetargetEditorBoneProxy::StaticGetType());
	if (bLeftButtonClicked && bHitBone)
	{
		const HIKRetargetEditorBoneProxy* BoneProxy = static_cast<HIKRetargetEditorBoneProxy*>(HitProxy);
		const TArray BoneNames{BoneProxy->BoneName};
		constexpr bool bFromHierarchy = false;
		Controller->EditBoneSelection(BoneNames, EditMode, bFromHierarchy);
		return true;
	}

	// did we click on a chain in the viewport?
	const bool bHitChain = HitProxy && HitProxy->IsA(HIKRetargetEditorChainProxy::StaticGetType());
	if (bLeftButtonClicked && bHitChain)
	{
		const HIKRetargetEditorChainProxy* ChainProxy = static_cast<HIKRetargetEditorChainProxy*>(HitProxy);
		const TArray ChainNames{ChainProxy->TargetChainName};
		constexpr bool bFromChainView = false;
		Controller->EditChainSelection(ChainNames, EditMode, bFromChainView);
		return true;
	}

	// did we click on the root in the viewport?
	const bool bHitRoot = HitProxy && HitProxy->IsA(HIKRetargetEditorRootProxy::StaticGetType());
	if (bLeftButtonClicked && bHitRoot)
	{
		Controller->SetRootSelected(true);
		return true;
	}

	// we didn't hit anything, therefore clicked in empty space in viewport
	Controller->ClearSelection(); // deselect all meshes, bones, chains and update details view
	return true;
}

void FIKRetargetDefaultMode::Enter()
{
	IPersonaEditMode::Enter();

	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}

	// record which skeleton is being viewed/edited
	SkeletonMode = Controller->GetSourceOrTarget();
}

void FIKRetargetDefaultMode::Exit()
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}
	
	IPersonaEditMode::Exit();
}

void FIKRetargetDefaultMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);
	
	CurrentWidgetMode = ViewportClient->GetWidgetMode();

	// ensure selection callbacks have been generated
	if (!bIsInitialized)
	{
		Initialize();
	}
}

#undef LOCTEXT_NAMESPACE
