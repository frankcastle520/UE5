// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/ViewBasedTransformAdjusters.h"

#include "BaseGizmos/GizmoRenderingUtil.h"
#include "BaseGizmos/ViewAdjustedStaticMeshGizmoComponent.h"
#include "FrameTypes.h"

namespace ViewBasedTransformAdjustersLocals
{
	double GetComponentToGizmoScaling(
		const UE::GizmoRenderingUtil::ISceneViewInterface& View,
		const FTransform& GizmoToWorld)
	{
		// It might seem that all we want is CalculateLocalPixelToWorldScale(View, CurrentComponentToWorld.GetLocation());
		//  However we're in a weird situation where scaling gets applied around the gizmo origin, not the component (to
		//  preserve component positioning within the gizmo); moreover we want different sub components of the same gizmo
		//  to be scaled the same way. So, we need to use gizmo location as the basis of our scaling.
		// On the other hand, we can't directly use CalculateLocalPixelToWorldScale with gizmo origin either because it's
		//  possible to see the sub gizmo while the gizmo itself is off screen, breaking that calculation. What we want 
		//  instead is a scale value that is dependent on FOV and distance to gizmo, but independent of view direction. 
		//  The way we do this is we take the distance to gizmo, and then evaluate the scale at a point that far along the
		//  view direction (so the actual view direction no longer matters- we're always looking at the scale around the
		//  center of the screen).
		// Note that this doesn't fully fix all problems with large component-to-gizmo offsets. Namely, moving the component
		//  with the gizmo off screen can sometimes make it seem like the component stops moving and starts growing, which 
		//  looks unnatural if you're not looking at the gizmo and realizing that you are just modifying your angle relative
		//  to it. However the approach here tends to be the least broken overall.
		double DistanceToGizmo = FVector3d::Distance(GizmoToWorld.GetLocation(), View.GetViewLocation());
		FVector3d PointAtWhichToCheckScale = View.GetViewLocation() + DistanceToGizmo * View.GetViewDirection();
		return UE::GizmoRenderingUtil::CalculateLocalPixelToWorldScale(&View, PointAtWhichToCheckScale);
	}

	// Apply the settings to get a transform modified by view
	FTransform GetSubGizmoAdjustedTransform(
		const UE::GizmoRenderingUtil::ISceneViewInterface& View,
		const FTransform& CurrentComponentToWorld,
		const FTransform& GizmoOriginToComponent,
		const UE::GizmoRenderingUtil::FSubGizmoTransformAdjuster::FSettings& Settings)
	{
		if (!Settings.bKeepConstantViewSize && !Settings.bMirrorBasedOnOctant && !Settings.bUseWorldAxesForGizmo)
		{
			return CurrentComponentToWorld;
		}
		
		FTransform GizmoToWorld = GizmoOriginToComponent * CurrentComponentToWorld;
		FTransform ComponentToGizmo = CurrentComponentToWorld.GetRelativeTransform(GizmoToWorld);

		if (Settings.bUseWorldAxesForGizmo)
		{
			GizmoToWorld.SetRotation(FQuat::Identity);
		}

		if (Settings.bMirrorBasedOnOctant)
		{
			FVector3d GizmoSpaceDirectionTowardCamera;
			if (View.IsPerspectiveProjection())
			{
				GizmoSpaceDirectionTowardCamera = GizmoToWorld.InverseTransformPosition(View.GetViewLocation());
			}
			else
			{
				GizmoSpaceDirectionTowardCamera = GizmoToWorld.InverseTransformVector(View.GetViewDirection());
			}

			EAxis::Type Axes[3] = { EAxis::X, EAxis::Y, EAxis::Z };
			for (int Dim = 0; Dim < 3; ++Dim)
			{
				if (GizmoSpaceDirectionTowardCamera[Dim] < 0)
				{
					ComponentToGizmo.Mirror(Axes[Dim], EAxis::None);
				}
			}
		}

		if (Settings.bKeepConstantViewSize)
		{
			double ExtraScaling = GetComponentToGizmoScaling(View, GizmoToWorld);

			ComponentToGizmo.MultiplyScale3D(FVector3d(ExtraScaling));
			ComponentToGizmo.ScaleTranslation(ExtraScaling);
		}

		return ComponentToGizmo * GizmoToWorld;
	}
}

// FSimpleConstantViewScaleAdjuster:

FTransform UE::GizmoRenderingUtil::FSimpleConstantViewScaleAdjuster::GetAdjustedComponentToWorld(
	const ISceneViewInterface& View, const FTransform& CurrentComponentToWorld)
{
	double ExtraScaling = UE::GizmoRenderingUtil::CalculateLocalPixelToWorldScale(&View, CurrentComponentToWorld.GetLocation());
	FTransform AdjustedTransform = CurrentComponentToWorld;
	AdjustedTransform.MultiplyScale3D(FVector(ExtraScaling));

	return AdjustedTransform;
}


// FSubGizmoTransformAdjuster:

void UE::GizmoRenderingUtil::FSubGizmoTransformAdjuster::SetGizmoOriginTransform(
	const FTransform& GizmoOriginToComponentIn)
{
	GizmoOriginToComponent_GameThread = GizmoOriginToComponentIn;

	// Safely update the render thread side transform.
	ENQUEUE_RENDER_COMMAND(FViewpointOctantMirrorTransformAdjusterUpdate)(
		[GizmoOriginToComponentIn, this](FRHICommandListImmediate& RHICmdList)
	{
		GizmoOriginToComponent_RenderThread = GizmoOriginToComponentIn;
	});
}

TSharedPtr<UE::GizmoRenderingUtil::FSubGizmoTransformAdjuster> UE::GizmoRenderingUtil::FSubGizmoTransformAdjuster::AddTransformAdjuster(
	UViewAdjustedStaticMeshGizmoComponent* ComponentIn, USceneComponent* ComponentToKeepDistanceConstantTo, bool bMirrorBasedOnOctant)
{
	if (!ComponentIn)
	{
		return nullptr;
	}

	TSharedPtr<UE::GizmoRenderingUtil::FSubGizmoTransformAdjuster> TransformAdjuster =
		MakeShared<UE::GizmoRenderingUtil::FSubGizmoTransformAdjuster>();
	UE::GizmoRenderingUtil::FSubGizmoTransformAdjuster::FSettings Settings;
	Settings.bKeepConstantViewSize = true;
	Settings.bMirrorBasedOnOctant = bMirrorBasedOnOctant;
	// bUseWorldAxes gets updated automatically as part of UViewAdjustedStaticMeshGizmoComponent::UpdateWorldLocalState
	TransformAdjuster->SetSettings(Settings);
	if (ComponentToKeepDistanceConstantTo)
	{
		TransformAdjuster->SetGizmoOriginTransform(
			ComponentToKeepDistanceConstantTo->GetComponentTransform().GetRelativeTransform(ComponentIn->GetComponentToWorld()));
	}
	ComponentIn->SetTransformAdjuster(TransformAdjuster);

	return TransformAdjuster;
}

FTransform UE::GizmoRenderingUtil::FSubGizmoTransformAdjuster::GetAdjustedComponentToWorld(
	const ISceneViewInterface& View, const FTransform& CurrentComponentToWorld)
{
	return ViewBasedTransformAdjustersLocals::GetSubGizmoAdjustedTransform(View, CurrentComponentToWorld,
		GizmoOriginToComponent_GameThread, Settings);
}

FTransform UE::GizmoRenderingUtil::FSubGizmoTransformAdjuster::GetAdjustedComponentToWorld_RenderThread(
	const ISceneViewInterface& View, const FTransform& CurrentComponentToWorld)
{
	return ViewBasedTransformAdjustersLocals::GetSubGizmoAdjustedTransform(View, CurrentComponentToWorld,
		GizmoOriginToComponent_RenderThread, Settings);
}


// FConstantViewRelativeTransformAdjuster:

FTransform UE::GizmoRenderingUtil::FConstantViewRelativeTransformAdjuster::GetAdjustedComponentToWorld(
	const ISceneViewInterface& View, const FTransform& CurrentComponentToWorld)
{
	UE::Geometry::FFrame3d ViewFrameAtComponent(CurrentComponentToWorld.GetLocation(), View.GetViewDirection(), View.GetViewRight(), View.GetViewUp());
	FTransform ViewRelativeTransformToUse = ViewRelativeTransform;

	if (bKeepConstantViewSize)
	{
		// We're assuming that the gizmo origin is at the component location, so we don't need to worry about scaling
		//  relative to a different point, but we still want our scaling to be view independent for cases where the
		//  relative transform is big enough that the origin is offscreen while the component is visible.
		double ExtraScaling = ViewBasedTransformAdjustersLocals::GetComponentToGizmoScaling(View, CurrentComponentToWorld);

		ViewRelativeTransformToUse.MultiplyScale3D(FVector3d(ExtraScaling));
		ViewRelativeTransformToUse.ScaleTranslation(ExtraScaling);
	}

	return ViewRelativeTransformToUse * ViewFrameAtComponent.ToFTransform();
}