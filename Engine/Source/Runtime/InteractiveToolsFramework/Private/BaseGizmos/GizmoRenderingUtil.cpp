// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/GizmoRenderingUtil.h"

#include "BaseGizmos/GizmoPrivateUtil.h" // GetGizmoViewContext
#include "BaseGizmos/GizmoViewContext.h"
#include "BaseGizmos/ViewAdjustedStaticMeshGizmoComponent.h"
#include "RHI.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/UnrealMathUtility.h"
#include "Math/NumericLimits.h"

// yuck global value set by Editor
static const FSceneView* GlobalCurrentSceneView = nullptr;

static FCriticalSection GlobalCurrentSceneViewLock;

#if WITH_EDITOR
static bool bGlobalUseCurrentSceneViewTracking = true;
#else
static bool bGlobalUseCurrentSceneViewTracking = false;
#endif

namespace GizmoRenderingUtilLocals
{
	static double VectorDifferenceSqr(const FVector2D& A, const FVector2D& B)
	{
		double ax = A.X, ay = A.Y;
		double bx = B.X, by = B.Y;
		ax -= bx;
		ay -= by;
		return ax * ax + ay * ay;
	}

	static double VectorDifferenceSqr(const FVector& A, const FVector& B)
	{
		double ax = A.X, ay = A.Y, az = A.Z;
		double bx = B.X, by = B.Y, bz = B.Z;
		ax -= bx;
		ay -= by;
		az -= bz;
		return ax * ax + ay * ay + az * az;
	}

	// duplicates FSceneView::WorldToPixel but in double where possible (unfortunately WorldToScreen still in float)
	static FVector2D WorldToPixelDouble(const UE::GizmoRenderingUtil::ISceneViewInterface* View, const FVector& Location)
	{
		FVector4 ScreenPoint = View->WorldToScreen(Location);

		double InvW = (ScreenPoint.W > 0.0 ? 1.0 : -1.0) / (double)ScreenPoint.W;
		double Y = (GProjectionSignY > 0.0) ? (double)ScreenPoint.Y : 1.0 - (double)ScreenPoint.Y;

		const FIntRect& UnscaledViewRect = View->GetUnscaledViewRect();
		double PosX = (double)UnscaledViewRect.Min.X + (0.5 + (double)ScreenPoint.X * 0.5 * InvW) * (double)UnscaledViewRect.Width();
		double PosY = (double)UnscaledViewRect.Min.Y + (0.5 - Y * 0.5 * InvW) * (double)UnscaledViewRect.Height();

		return FVector2D((float)PosX, (float)PosY);
	}

	// This matches "CurrentColor" in FWidget::Widget()
	FColor HoverColor = FColor::Yellow;
}

UViewAdjustedStaticMeshGizmoComponent* UE::GizmoRenderingUtil::CreateDefaultMaterialGizmoMeshComponent(
	UStaticMesh* Mesh, UGizmoViewContext* GizmoViewContext, UObject* OwnerComponentOrActor, 
	const FLinearColor& Color, bool bAddHoverMaterial)
{
	using namespace GizmoRenderingUtilLocals;

	if (!ensureMsgf(OwnerComponentOrActor, TEXT("CreateDefaultMaterialGizmoMeshComponent: Need owner component or actor to create component.")))
	{
		return nullptr;
	}
	UViewAdjustedStaticMeshGizmoComponent* Component = NewObject<UViewAdjustedStaticMeshGizmoComponent>(OwnerComponentOrActor);
	Component->SetStaticMesh(Mesh);

	Component->SetGizmoViewContext(GizmoViewContext);
	Component->TranslucencySortPriority = UE::GizmoRenderingUtil::GIZMO_TRANSLUCENCY_SORT_PRIORITY;
	// Used by the default material to be able to be occluded by other gizmo elements
	Component->bRenderCustomDepth = true;
	// Not sure that this actually gets respected in any way for non-PDI calls, but just in case
	Component->DepthPriorityGroup = SDPG_Foreground;

	Component->SetAllMaterials(UE::GizmoRenderingUtil::GetDefaultGizmoComponentMaterial(Color, Component));
	if (bAddHoverMaterial)
	{
		Component->SetHoverOverrideMaterial(UE::GizmoRenderingUtil::GetDefaultGizmoComponentMaterial(HoverColor, Component));
	}

	return Component;
}

UViewAdjustedStaticMeshGizmoComponent* UE::GizmoRenderingUtil::CreateDefaultMaterialGizmoMeshComponent(
	UStaticMesh* Mesh, UInteractiveGizmoManager* GizmoManager, UObject* OwnerComponentOrActor, 
	const FLinearColor& Color, bool bAddHoverMaterial)
{
	return CreateDefaultMaterialGizmoMeshComponent(Mesh, UE::GizmoUtil::GetGizmoViewContext(GizmoManager), 
		OwnerComponentOrActor, Color, bAddHoverMaterial);
}


float UE::GizmoRenderingUtil::CalculateLocalPixelToWorldScale(
	const FSceneView* View,
	const FVector& Location)
{
	using namespace GizmoRenderingUtilLocals;

	if (!View)
	{
		return 0.0f;
	}

	UE::GizmoRenderingUtil::FSceneViewWrapper Wrapper(*View);
	return CalculateLocalPixelToWorldScale(&Wrapper, Location);
}

float UE::GizmoRenderingUtil::CalculateLocalPixelToWorldScale(
	const UE::GizmoRenderingUtil::ISceneViewInterface* View, 
	const FVector& Location)
{
	using namespace GizmoRenderingUtilLocals;

	// To calculate this scale at Location, we project Location to screen and also project a second
	// point at a small distance from Location in a camera-perpendicular plane, then measure 2D/3D distance ratio. 
	// However, because some of the computations are done in float, there will be enormous numerical error 
	// when the camera is very far from the location if the distance is relatively small. The "W" value
	// below gives us a sense of this distance, so we make the offset relative to that
	// (this does do one redundant WorldToScreen)
	FVector4 LocationScreenPoint = View->WorldToScreen(Location);
	double OffsetDelta = LocationScreenPoint.W * 0.01;

	FVector2D PixelA = WorldToPixelDouble(View, Location);
	FVector OffsetPointWorld = Location + OffsetDelta * View->GetViewRight() + OffsetDelta * View->GetViewUp();
	FVector2D PixelB = WorldToPixelDouble(View, OffsetPointWorld);

	double PixelDeltaSqr = VectorDifferenceSqr(PixelA, PixelB);
	double WorldDeltaSqr = VectorDifferenceSqr(Location, OffsetPointWorld);
	return (float)(sqrt(WorldDeltaSqr / PixelDeltaSqr));
}

float UE::GizmoRenderingUtil::CalculateViewDependentScaleAndFlatten(
	const FSceneView* View,
	const FVector& Location,
	const float InScale,
	FVector& OutFlattenScale)
{
	const FMatrix& ViewMatrix = View->ViewMatrices.GetViewMatrix();
	bool bIsPerspective = View->ViewMatrices.GetProjectionMatrix().M[3][3] < 1.0f;
	bool bIsOrthoXY = !bIsPerspective && FMath::Abs(ViewMatrix.M[2][2]) > 0.0f;
	bool bIsOrthoXZ = !bIsPerspective && FMath::Abs(ViewMatrix.M[1][2]) > 0.0f;
	bool bIsOrthoYZ = !bIsPerspective && FMath::Abs(ViewMatrix.M[0][2]) > 0.0f;
	float UniformScale = static_cast<float> (InScale * View->WorldToScreen(Location).W * (4.0 / View->UnscaledViewRect.Width() / View->ViewMatrices.GetProjectionMatrix().M[0][0]));

	// Clamp to tolerance to prevent division by zero.
	// @todo change to use MathUtil<RealType>::ZeroTolerance and TMathUtil<RealType>::SignNonZero(Value)
	float MinimumScale = TNumericLimits<float>::Lowest();
	if (FMath::Abs(UniformScale) < MinimumScale)
	{
		UniformScale = MinimumScale * (UniformScale < 0.0f ? -1.0f : 1.0f);
	}

	if (bIsPerspective)
	{
		OutFlattenScale = FVector(1.0f, 1.0f, 1.0f);
	}
	else
	{
		// Flatten scale prevents scaling in the direction of the camera and thus intersecting the near plane.
		// Based on legacy FWidget render code but is flatten actually necessary?? that axis wasn't scaled anyways!
		if (bIsOrthoXY)
		{
			OutFlattenScale = FVector(1.0f, 1.0f, 1.0f / UniformScale);
		}
		else if (bIsOrthoXZ)
		{
			OutFlattenScale = FVector(1.0f, 1.0f / UniformScale, 1.0f);
		}
		else if (bIsOrthoYZ)
		{
			OutFlattenScale = FVector(1.0f / UniformScale, 1.0f, 1.0f);
		}
	}
	return UniformScale;
}

UMaterialInterface* UE::GizmoRenderingUtil::GetDefaultGizmoComponentMaterial(const FLinearColor& Color, UObject* Outer)
{
	UMaterialInterface* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolsetExp/Materials/GizmoComponentMaterial"));
	if (Material == nullptr)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* MatInstance = UMaterialInstanceDynamic::Create(Material, Outer);

	MatInstance->SetVectorParameterValue(TEXT("GizmoColor"), Color);
	return MatInstance;
}

FLinearColor UE::GizmoRenderingUtil::GetDefaultAxisColor(EAxis::Type Axis)
{
	// The below colors come from FWidget::FWidget() and elsewhere
	switch (Axis)
	{
	case EAxis::X:
		return FLinearColor(0.594f, 0.0197f, 0.0f);
	case EAxis::Y:
		return FLinearColor(0.1349f, 0.3959f, 0.0f);
	case EAxis::Z:
		return FLinearColor(0.0251f, 0.207f, 0.85f);
	default:
		return FLinearColor::White;
	}
}

// Forward the deprecated methods (that are in the wrong namespace) to the proper namespace methods
float GizmoRenderingUtil::CalculateLocalPixelToWorldScale(
	const FSceneView* View,
	const FVector& Location)
{
	return UE::GizmoRenderingUtil::CalculateLocalPixelToWorldScale(View, Location);
}
float GizmoRenderingUtil::CalculateLocalPixelToWorldScale(
	const UGizmoViewContext* ViewContext,
	const FVector& Location)
{
	return UE::GizmoRenderingUtil::CalculateLocalPixelToWorldScale(ViewContext, Location);
}
float GizmoRenderingUtil::CalculateViewDependentScaleAndFlatten(
	const FSceneView* View,
	const FVector& Location,
	const float InScale,
	FVector& OutFlattenScale)
{
	return UE::GizmoRenderingUtil::CalculateViewDependentScaleAndFlatten(View, Location, InScale, OutFlattenScale);
}