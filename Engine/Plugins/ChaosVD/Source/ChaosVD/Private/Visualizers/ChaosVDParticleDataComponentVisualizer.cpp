// Copyright Epic Games, Inc. All Rights Reserved.

#include "Visualizers/ChaosVDParticleDataComponentVisualizer.h"

#include "Actors/ChaosVDSolverInfoActor.h"
#include "ChaosVDParticleActor.h"
#include "ChaosVDScene.h"
#include "ChaosVDSettingsManager.h"
#include "ChaosVDTabsIDs.h"
#include "Components/ChaosVDParticleDataComponent.h"
#include "DataWrappers/ChaosVDParticleDataWrapper.h"
#include "SceneView.h"
#include "Settings/ChaosVDParticleVisualizationSettings.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "ToolMenuEntry.h"
#include "ToolMenuSection.h"
#include "Utils/ChaosVDUserInterfaceUtils.h"
#include "Visualizers/ChaosVDDebugDrawUtils.h"
#include "Widgets/SChaosVDEnumFlagsMenu.h"
#include "Widgets/SChaosVDViewportToolbar.h"

#define LOCTEXT_NAMESPACE "ChaosVisualDebugger"

/** Sets a Hit proxy which will be cleared out as soon this struct goes out of scope*/
struct FChaosVDScopedParticleHitProxy
{
	FChaosVDScopedParticleHitProxy(FPrimitiveDrawInterface* PDI, HHitProxy* HitProxy)
	{
		PDIPtr = PDI;
		if (PDIPtr)
		{
			PDIPtr->SetHitProxy(HitProxy);
		}
	}

	~FChaosVDScopedParticleHitProxy()
	{
		if (PDIPtr)
		{
			PDIPtr->SetHitProxy(nullptr);
		}
	}
	
	FPrimitiveDrawInterface* PDIPtr = nullptr;
};

FChaosVDParticleDataComponentVisualizer::FChaosVDParticleDataComponentVisualizer()
{
	FChaosVDParticleDataComponentVisualizer::RegisterVisualizerMenus();

	InspectorTabID = FChaosVDTabID::DetailsPanel;
}

void FChaosVDParticleDataComponentVisualizer::RegisterVisualizerMenus()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	
	if (!ensure(ToolMenus))
	{
		return;
	}

	if (UToolMenu* Menu = ToolMenus->ExtendMenu(SChaosVDViewportToolbar::ShowMenuName))
	{
		FToolMenuSection& Section = Menu->AddSection("ParticleVisualization.Show", LOCTEXT("ParticleVisualizationShowMenuLabel", "Particle Visualization"));

		FNewToolMenuDelegate GeometryVisualizationFlagsMenuBuilder = FNewToolMenuDelegate::CreateLambda([](UToolMenu* Menu)
		{
			if (Menu)
			{
				TSharedRef<SWidget> VisualizationFlagsWidget = SNew(SChaosVDEnumFlagsMenu<EChaosVDGeometryVisibilityFlags>)
					.CurrentValue_Static(&UChaosVDParticleVisualizationSettings::GetGeometryVisualizationFlags)
					.OnEnumSelectionChanged_Lambda(&UChaosVDParticleVisualizationSettings::SetGeometryVisualizationFlags);

				Menu->AddMenuEntry(NAME_None, FToolMenuEntry::InitWidget("GeometryVisualizationFlags",VisualizationFlagsWidget,FText::GetEmpty()));
			}
		});

		FNewToolMenuDelegate ParticleDataVisualizationFlagsMenuBuilder = FNewToolMenuDelegate::CreateLambda([](UToolMenu* Menu)
		{
			if (Menu)
			{
				TSharedRef<SWidget> VisualizationFlagsWidget = SNew(SChaosVDEnumFlagsMenu<EChaosVDParticleDataVisualizationFlags>)
					.CurrentValue_Static(&UChaosVDParticleVisualizationDebugDrawSettings::GetDataDebugDrawVisualizationFlags)
					.OnEnumSelectionChanged_Static(&UChaosVDParticleVisualizationDebugDrawSettings::SetDataDebugDrawVisualizationFlags);

				Menu->AddMenuEntry(NAME_None, FToolMenuEntry::InitWidget("ParticleDebugDrawDataVisualizationFlags", VisualizationFlagsWidget,FText::GetEmpty()));
			}
		});
		
		using namespace Chaos::VisualDebugger::Utils;
		
		FNewToolMenuDelegate GeometryVisualizationSettingsMenuBuilder = FNewToolMenuDelegate::CreateStatic(&CreateMenuEntryForSettingsObject<UChaosVDParticleVisualizationSettings>, EChaosVDSaveSettingsOptions::ShowResetButton);
		FNewToolMenuDelegate ParticleDataVisualizationSettingsMenuBuilder = FNewToolMenuDelegate::CreateStatic(&CreateMenuEntryForSettingsObject<UChaosVDParticleVisualizationDebugDrawSettings>, EChaosVDSaveSettingsOptions::ShowResetButton);
		FNewToolMenuDelegate ParticleColorizationMenuBuilder = FNewToolMenuDelegate::CreateStatic(&CreateMenuEntryForSettingsObject<UChaosVDParticleVisualizationColorSettings>, EChaosVDSaveSettingsOptions::ShowResetButton);

		constexpr bool bOpenSubMenuOnClick = false;
		
		Section.AddSubMenu(TEXT("GeometryVisualizationFlags"), LOCTEXT("GeometryVisualizationFlagsMenuLabel", "Geometry Flags"), LOCTEXT("GeometryVisualizationFlagsMenuToolTip", "Set of flags to enable/disable visibility of specific types of geometry/particles"), GeometryVisualizationFlagsMenuBuilder, bOpenSubMenuOnClick, FSlateIcon(FAppStyle::Get().GetStyleSetName(), TEXT("ShowFlagsMenu.StaticMeshes")));
		Section.AddSubMenu(TEXT("GeometryVisualizationSettings"), LOCTEXT("GeometryVisualizationSettingsMenuLabel", "Geometry Visualization Settings"), LOCTEXT("GeometryVisualizationSettingsMenuToolTip", "Options to control how particle data is debug geometry is visualized"), GeometryVisualizationSettingsMenuBuilder, bOpenSubMenuOnClick, FSlateIcon(FAppStyle::Get().GetStyleSetName(), TEXT("Icons.Toolbar.Settings")));
		Section.AddSubMenu(TEXT("ParticleDataVisualizationFlags"), LOCTEXT("ParticleDataVisualizationFlagsMenuLabel", "Particle Data Flags"), LOCTEXT("ParticleDataVisualizationFlagsMenuToolTip", "Set of flags to enable/disable visualization of specific particle data as debug draw"), ParticleDataVisualizationFlagsMenuBuilder, bOpenSubMenuOnClick, FSlateIcon(FAppStyle::Get().GetStyleSetName(), TEXT("StaticMeshEditor.SetDrawAdditionalData")));
		Section.AddSubMenu(TEXT("ParticleDataVisualizationSettings"), LOCTEXT("ParticleDataVisualizationSettingsMenuLabel", "Particle Data Visualization Settings"), LOCTEXT("ParticleDataVisualizationSettingsMenuToolTip", "Options to control how particle data is debug drawn"), ParticleDataVisualizationSettingsMenuBuilder, bOpenSubMenuOnClick, FSlateIcon(FAppStyle::Get().GetStyleSetName(), TEXT("Icons.Toolbar.Settings")));
		Section.AddSubMenu(TEXT("ParticleColorizationFlags"), LOCTEXT("ParticleColorizationOptionsMenuLabel", "Particle Colorization"), LOCTEXT("Particle ColorizationMenuToolTip", "Changes what colors are used to draw the particles and its data"), ParticleColorizationMenuBuilder, bOpenSubMenuOnClick, FSlateIcon(FAppStyle::Get().GetStyleSetName(), TEXT("ColorPicker.ColorThemes")));
	}
}

void FChaosVDParticleDataComponentVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	const UChaosVDParticleVisualizationDebugDrawSettings* VisualizationSettings = FChaosVDSettingsManager::Get().GetSettingsObject<UChaosVDParticleVisualizationDebugDrawSettings>();
	if (!VisualizationSettings)
	{
		return;
	}

	if (VisualizationSettings->GetDataDebugDrawVisualizationFlags() == EChaosVDParticleDataVisualizationFlags::None)
	{
		// Nothing to visualize
		return;
	}

	const UChaosVDParticleDataComponent* ParticleDataComponent = Cast<UChaosVDParticleDataComponent>(Component);
	if (!ParticleDataComponent)
	{
		return;
	}

	AChaosVDSolverInfoActor* SolverDataActor = Cast<AChaosVDSolverInfoActor>(Component->GetOwner());
	if (!SolverDataActor)
	{
		return;
	}

	if (!SolverDataActor->IsVisible())
	{
		return;
	}

	const TSharedPtr<FChaosVDScene> CVDScene = SolverDataActor->GetScene().Pin();
	if (!CVDScene)
	{
		return;
	}

	FChaosVDParticleDataVisualizationContext VisualizationContext;
	VisualizationContext.VisualizationFlags = static_cast<uint32>(VisualizationSettings->GetDataDebugDrawVisualizationFlags());
	VisualizationContext.SpaceTransform = SolverDataActor->GetSimulationTransform();
	VisualizationContext.CVDScene = CVDScene;
	VisualizationContext.GeometryGenerator = CVDScene->GetGeometryGenerator();
	VisualizationContext.bShowDebugText = VisualizationSettings->bShowDebugText;
	VisualizationContext.DebugDrawSettings = VisualizationSettings;
	VisualizationContext.SolverDataSelectionObject = CVDScene->GetSolverDataSelectionObject().Pin();
	

	if (!VisualizationContext.IsVisualizationFlagEnabled(EChaosVDParticleDataVisualizationFlags::EnableDraw))
	{
		return;
	}

	if (VisualizationContext.IsVisualizationFlagEnabled(EChaosVDParticleDataVisualizationFlags::DrawDataOnlyForSelectedParticle))
	{
		VisualizationContext.bIsSelectedData = true;
		SolverDataActor->VisitSelectedParticleData([this, PDI, View, &VisualizationContext, Component](const TSharedPtr<const FChaosVDParticleDataWrapper>& InParticleDataViewer)
		{
			DrawVisualizationForParticleData(Component, PDI, View, VisualizationContext, InParticleDataViewer);

			return true;
		});
	}
	else
	{
		SolverDataActor->VisitAllParticleData([this, PDI, View, &VisualizationContext, Component, SolverDataActor](const TSharedPtr<const FChaosVDParticleDataWrapper>& InParticleDataViewer)
		{
			VisualizationContext.bIsSelectedData = InParticleDataViewer && SolverDataActor->IsParticleSelectedByID(InParticleDataViewer->ParticleIndex);
			DrawVisualizationForParticleData(Component, PDI, View, VisualizationContext, InParticleDataViewer);

			// If we reach the debug draw limit for this frame, there is no need to continue processing particles
			return FChaosVDDebugDrawUtils::CanDebugDraw();
		});
	}
}

bool FChaosVDParticleDataComponentVisualizer::CanHandleClick(const HChaosVDComponentVisProxy& VisProxy)
{
	return VisProxy.DataSelectionHandle && VisProxy.DataSelectionHandle->IsA<FChaosVDParticleDataWrapper>();
}

bool FChaosVDParticleDataComponentVisualizer::SelectVisualizedData(const HChaosVDComponentVisProxy& VisProxy, const TSharedRef<FChaosVDScene>& InCVDScene, const TSharedRef<SChaosVDMainTab>& InMainTabToolkitHost)
{
	bool bHandled = false;
	const UChaosVDParticleDataComponent* ParticleDataComponent = Cast<UChaosVDParticleDataComponent>(VisProxy.Component.Get());
	if (!ParticleDataComponent)
	{
		return bHandled;
	}

	AChaosVDSolverInfoActor* SolverDataActor = Cast<AChaosVDSolverInfoActor>(ParticleDataComponent->GetOwner());
	if (!SolverDataActor)
	{
		return bHandled;
	}

	if (TSharedPtr<const FChaosVDParticleDataWrapper> ParticleDataViewer = VisProxy.DataSelectionHandle ?  VisProxy.DataSelectionHandle->GetDataAsShared<const FChaosVDParticleDataWrapper>() : nullptr)
	{
		bHandled = SolverDataActor->SelectParticleByID(ParticleDataViewer->ParticleIndex);
	}

	return bHandled;
}

void FChaosVDParticleDataComponentVisualizer::DrawParticleVector(FPrimitiveDrawInterface* PDI, const FVector& StartLocation, const FVector& InVector, EChaosVDParticleDataVisualizationFlags VectorID, const FChaosVDParticleDataVisualizationContext& InVisualizationContext, float LineThickness)
{
	if (!InVisualizationContext.IsVisualizationFlagEnabled(VectorID))
	{
		return;
	}

	if (!ensure(InVisualizationContext.DebugDrawSettings))
	{
		return;
	}

	const FString DebugText = InVisualizationContext.bShowDebugText ? Chaos::VisualDebugger::Utils::GenerateDebugTextForVector(InVector, UEnum::GetDisplayValueAsText(VectorID).ToString(), Chaos::VisualDebugger::ParticleDataUnitsStrings::GetUnitByID(VectorID)) : TEXT("");
	FChaosVDDebugDrawUtils::DrawArrowVector(PDI, StartLocation, StartLocation +  InVisualizationContext.DebugDrawSettings->GetScaleFortDataID(VectorID) * InVector, FText::AsCultureInvariant(DebugText), InVisualizationContext.DebugDrawSettings->ColorSettings.GetColorForDataID(VectorID, InVisualizationContext.bIsSelectedData),  InVisualizationContext.DebugDrawSettings->DepthPriority, LineThickness);
}

void FChaosVDParticleDataComponentVisualizer::DrawVisualizationForParticleData(const UActorComponent* Component, FPrimitiveDrawInterface* PDI, const FSceneView* View, const FChaosVDParticleDataVisualizationContext& InVisualizationContext, const TSharedPtr<const FChaosVDParticleDataWrapper>& InParticleDataViewer)
{
	using namespace Chaos::VisualDebugger::ParticleDataUnitsStrings;

	if (!View)
	{
		return;
	}

	if (!ensure(InVisualizationContext.DebugDrawSettings))
	{
		return;
	}

	if (!ensure(InVisualizationContext.SolverDataSelectionObject))
	{
		return;
	}

	const FVector& OwnerLocation = InVisualizationContext.SpaceTransform.TransformPosition(InParticleDataViewer->ParticlePositionRotation.MX);

	// TODO: See how expensive is get the bounds. It is not something we have recorded
	constexpr float VisibleRadius = 50.0f;
	if (!View->ViewFrustum.IntersectSphere(OwnerLocation, VisibleRadius))
	{
		// If this particle location is not even visible, just ignore it.
		return;
	}

	const FQuat& OwnerRotation =  InVisualizationContext.SpaceTransform.TransformRotation(InParticleDataViewer->ParticlePositionRotation.MR);
	const FVector OwnerCoMLocation = InVisualizationContext.SpaceTransform.TransformPosition(InParticleDataViewer->ParticlePositionRotation.MX + (InParticleDataViewer->ParticlePositionRotation.MR *  InParticleDataViewer->ParticleMassProps.MCenterOfMass));
	
	FChaosVDScopedParticleHitProxy ScopedHitProxy(PDI, new HChaosVDComponentVisProxy(Component, InVisualizationContext.SolverDataSelectionObject->MakeSelectionHandle(ConstCastSharedPtr<FChaosVDParticleDataWrapper>(InParticleDataViewer))));

	constexpr float DefaultLineThickness = 1.5f;
	constexpr float SelectedLineThickness = 3.5f;
	const float LineThickness = InVisualizationContext.bIsSelectedData ? SelectedLineThickness : DefaultLineThickness;
	

	if (InParticleDataViewer->ParticleVelocities.HasValidData())
	{
		DrawParticleVector(PDI, OwnerCoMLocation, InParticleDataViewer->ParticleVelocities.MV,EChaosVDParticleDataVisualizationFlags::Velocity, InVisualizationContext, LineThickness); 
		DrawParticleVector(PDI, OwnerCoMLocation, InParticleDataViewer->ParticleVelocities.MW,EChaosVDParticleDataVisualizationFlags::AngularVelocity, InVisualizationContext, LineThickness); 
	}

	if (InParticleDataViewer->ParticleDynamics.HasValidData())
	{
		DrawParticleVector(PDI, OwnerCoMLocation, InParticleDataViewer->ParticleDynamics.MAcceleration,EChaosVDParticleDataVisualizationFlags::Acceleration, InVisualizationContext, LineThickness); 
		DrawParticleVector(PDI, OwnerCoMLocation, InParticleDataViewer->ParticleDynamics.MAngularAcceleration,EChaosVDParticleDataVisualizationFlags::AngularAcceleration, InVisualizationContext, LineThickness); 
		DrawParticleVector(PDI, OwnerCoMLocation, InParticleDataViewer->ParticleDynamics.MLinearImpulseVelocity,EChaosVDParticleDataVisualizationFlags::LinearImpulse, InVisualizationContext, LineThickness); 
		DrawParticleVector(PDI, OwnerCoMLocation, InParticleDataViewer->ParticleDynamics.MAngularImpulseVelocity,EChaosVDParticleDataVisualizationFlags::AngularImpulse, InVisualizationContext, LineThickness);
	}

	if (InParticleDataViewer->ParticleMassProps.HasValidData())
	{
		if (InVisualizationContext.IsVisualizationFlagEnabled(EChaosVDParticleDataVisualizationFlags::CenterOfMass))
		{
			if (const TSharedPtr<FChaosVDGeometryBuilder> GeometryGenerator = InVisualizationContext.GeometryGenerator.Pin())
			{
				FCollisionShape Sphere;
				Sphere.SetSphere(InVisualizationContext.DebugDrawSettings->CenterOfMassRadius);
				const FPhysicsShapeAdapter SphereShapeAdapter(FQuat::Identity, Sphere);

				FChaosVDDebugDrawUtils::DrawImplicitObject(PDI, GeometryGenerator, &SphereShapeAdapter.GetGeometry(), FTransform(OwnerCoMLocation), InVisualizationContext.DebugDrawSettings->ColorSettings.GetColorForDataID(EChaosVDParticleDataVisualizationFlags::CenterOfMass, InVisualizationContext.bIsSelectedData), UEnum::GetDisplayValueAsText(EChaosVDParticleDataVisualizationFlags::CenterOfMass), InVisualizationContext.DebugDrawSettings->DepthPriority, LineThickness);
			}
		}
	}

	// TODO: This is a Proof of concept to test how debug draw connectivity data will look
	if (InParticleDataViewer->ParticleCluster.HasValidData())
	{
		if (InVisualizationContext.IsVisualizationFlagEnabled(EChaosVDParticleDataVisualizationFlags::ClusterConnectivityEdge))
		{
			for (const FChaosVDConnectivityEdge& ConnectivityEdge : InParticleDataViewer->ParticleCluster.ConnectivityEdges)
			{
				if (const TSharedPtr<FChaosVDScene> ScenePtr = InVisualizationContext.CVDScene.Pin())
				{
					if (AChaosVDParticleActor* SiblingParticle = ScenePtr->GetParticleActor(InVisualizationContext.SolverID, ConnectivityEdge.SiblingParticleID))
					{
						if (TSharedPtr<const FChaosVDParticleDataWrapper> SiblingParticleData = SiblingParticle->GetParticleData())
						{
							FColor DebugDrawColor = InVisualizationContext.DebugDrawSettings->ColorSettings.GetColorForDataID(EChaosVDParticleDataVisualizationFlags::ClusterConnectivityEdge, InVisualizationContext.bIsSelectedData);
							FVector BoxExtents(2,2,2);
							FTransform BoxTransform(OwnerRotation, OwnerLocation);
							FChaosVDDebugDrawUtils::DrawBox(PDI, BoxExtents, DebugDrawColor, BoxTransform, FText::GetEmpty(), InVisualizationContext.DebugDrawSettings->DepthPriority, LineThickness);

							FVector SiblingParticleLocation = InVisualizationContext.SpaceTransform.TransformPosition(SiblingParticleData->ParticlePositionRotation.MX);
							FChaosVDDebugDrawUtils::DrawLine(PDI, OwnerLocation, SiblingParticleLocation, DebugDrawColor, FText::FormatOrdered(LOCTEXT("StrainDebugDraw","Strain {0}"), ConnectivityEdge.Strain), InVisualizationContext.DebugDrawSettings->DepthPriority, LineThickness);
						}
					}	
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
