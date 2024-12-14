// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClothEditorSimulationVisualization.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothEditor3DViewportClient.h"
#include "ChaosClothAsset/ClothSimulationProxy.h"
#include "ChaosClothAsset/ClothComponent.h"
#include "ChaosCloth/ChaosClothVisualization.h"
#include "Dataflow/DataflowEditorToolkit.h"
#include "Dataflow/DataflowSimulationScene.h"
#include "Dataflow/DataflowSimulationViewportClient.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Delegates/Delegate.h"
#include "Delegates/DelegateCombinations.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Types/SlateEnums.h"


#define LOCTEXT_NAMESPACE "ClothEditorSimulationVisualization"

namespace UE::Chaos::ClothAsset
{
namespace Private
{
FText ConcatenateLine(const FText& InText, const FText& InNewLine)
{
	if (InText.IsEmpty())
	{
		return InNewLine;
	}
	return FText::Format(LOCTEXT("ViewportTextNewlineFormatter", "{0}\n{1}"), InText, InNewLine);
}

static FText GetSimulationStatisticsString(const FClothSimulationProxy& SimProxy)
{
	FText TextValue;
	// Cloth stats
	if (const int32 NumActiveCloths = SimProxy.GetNumCloths())
	{
		TextValue = ConcatenateLine(TextValue, FText::Format(LOCTEXT("NumActiveCloths", "Active Cloths: {0}"), NumActiveCloths));
	}
	if (const int32 NumKinematicParticles = SimProxy.GetNumKinematicParticles())
	{
		TextValue = ConcatenateLine(TextValue, FText::Format(LOCTEXT("NumKinematicParticles", "Kinematic Particles: {0}"), NumKinematicParticles));
	}
	if (const int32 NumDynamicParticles = SimProxy.GetNumDynamicParticles())
	{
		TextValue = ConcatenateLine(TextValue, FText::Format(LOCTEXT("NumDynamicParticles", "Dynamic Particles: {0}"), NumDynamicParticles));
	}
	if (const int32 NumIterations = SimProxy.GetNumIterations())
	{
		TextValue = ConcatenateLine(TextValue, FText::Format(LOCTEXT("NumIterations", "Iterations: {0}"), NumIterations));
	}
	if (const int32 NumSubSteps = SimProxy.GetNumSubsteps())
	{
		TextValue = ConcatenateLine(TextValue, FText::Format(LOCTEXT("NumSubsteps", "Substeps: {0}"), NumSubSteps));
	}
	if (const int32 NumLinearSolveIterations = SimProxy.GetNumLinearSolveIterations())
	{
		TextValue = ConcatenateLine(TextValue, FText::Format(LOCTEXT("NumCGIterations", "CGIterations: {0}"), NumLinearSolveIterations));
	}
	if (const float LinearSolveError = SimProxy.GetLinearSolveError())
	{
		FNumberFormattingOptions NumberFormatOptions;
		NumberFormatOptions.AlwaysSign = false;
		NumberFormatOptions.UseGrouping = false;
		NumberFormatOptions.RoundingMode = ERoundingMode::HalfFromZero;
		NumberFormatOptions.MinimumIntegralDigits = 1;
		NumberFormatOptions.MaximumIntegralDigits = 6;
		NumberFormatOptions.MinimumFractionalDigits = 2;
		NumberFormatOptions.MaximumFractionalDigits = 6;
		TextValue = ConcatenateLine(TextValue, FText::Format(LOCTEXT("CGError", "CGError: {0}"), FText::AsNumber(LinearSolveError, &NumberFormatOptions)));
	}
	if (const float SimulationTime = SimProxy.GetSimulationTime())
	{
		FNumberFormattingOptions NumberFormatOptions;
		NumberFormatOptions.AlwaysSign = false;
		NumberFormatOptions.UseGrouping = false;
		NumberFormatOptions.RoundingMode = ERoundingMode::HalfFromZero;
		NumberFormatOptions.MinimumIntegralDigits = 1;
		NumberFormatOptions.MaximumIntegralDigits = 6;
		NumberFormatOptions.MinimumFractionalDigits = 2;
		NumberFormatOptions.MaximumFractionalDigits = 2;
		TextValue = ConcatenateLine(TextValue, FText::Format(LOCTEXT("SimulationTime", "Simulation Time: {0}ms"), FText::AsNumber(SimulationTime, &NumberFormatOptions)));
	}
	if (SimProxy.IsTeleported())
	{
		TextValue = ConcatenateLine(TextValue, LOCTEXT("IsTeleported", "Simulation Teleport Activated"));
	}
	return TextValue;
}

DECLARE_DELEGATE_ThreeParams(FClothVisualizationDebugDraw, const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC&, FPrimitiveDrawInterface*);

DECLARE_DELEGATE_FourParams(FClothVisualizationDebugDrawTexts, const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC&, FCanvas*, const FSceneView*);

DECLARE_DELEGATE_RetVal_TwoParams(FText, FLocalDebugDisplayString, const FClothEditorSimulationVisualization&, const FClothSimulationProxy& );

DECLARE_DELEGATE_TwoParams(FAdditionalMenus, FClothEditorSimulationVisualization&, FMenuBuilder&);

struct FVisualizationOption
{
	// Actual option entries
	static const FVisualizationOption OptionData[];
	static const uint32 Count;

	// Chaos debug draw function
	FClothVisualizationDebugDraw ClothVisualizationDebugDraw;
	FClothVisualizationDebugDrawTexts ClothVisualizationDebugDrawTexts;
	FLocalDebugDisplayString LocalDebugDisplayString;

	// Extra menu building function
	FAdditionalMenus AdditionalMenus;

	FText DisplayName;         // Text for menu entries.
	FText ToolTip;             // Text for menu tooltips.
	bool bDisablesSimulation;  // Whether or not this option requires the simulation to be disabled.
	bool bHidesClothSections;  // Hides the cloth section to avoid zfighting with the debug geometry.
	bool bDefaultFlagValue = false;

	FVisualizationOption(const FClothVisualizationDebugDraw& InClothVisualizationDebugDraw, const FText& InDisplayName, const FText& InToolTip, bool bInDisablesSimulation = false, bool bInHidesClothSections = false, bool bInDefaultFlagValue = false, const FAdditionalMenus& InAdditionalMenus = FAdditionalMenus())
		: ClothVisualizationDebugDraw(InClothVisualizationDebugDraw)
		, AdditionalMenus(InAdditionalMenus)
		, DisplayName(InDisplayName)
		, ToolTip(InToolTip)
		, bDisablesSimulation(bInDisablesSimulation)
		, bHidesClothSections(bInHidesClothSections)
		, bDefaultFlagValue(bInDefaultFlagValue)
	{}

	FVisualizationOption(const FLocalDebugDisplayString& InLocalDebugDisplayString, const FText& InDisplayName, const FText& InToolTip, bool bInDisablesSimulation = false, bool bInHidesClothSections = false, bool bInDefaultFlagValue = false)
		: LocalDebugDisplayString(InLocalDebugDisplayString)
		, DisplayName(InDisplayName)
		, ToolTip(InToolTip)
		, bDisablesSimulation(bInDisablesSimulation)
		, bHidesClothSections(bInHidesClothSections)
		, bDefaultFlagValue(bInDefaultFlagValue)
	{}

	FVisualizationOption(const FClothVisualizationDebugDrawTexts& InClothVisualizationDebugDrawTexts, const FText & InDisplayName, const FText & InToolTip, bool bInDisablesSimulation = false, bool bInHidesClothSections = false, bool bInDefaultFlagValue = false)
		: ClothVisualizationDebugDrawTexts(InClothVisualizationDebugDrawTexts)
		, DisplayName(InDisplayName)
		, ToolTip(InToolTip)
		, bDisablesSimulation(bInDisablesSimulation)
		, bHidesClothSections(bInHidesClothSections)
		, bDefaultFlagValue(bInDefaultFlagValue)
	{}
	
	FVisualizationOption(const FText & InDisplayName, const FText & InToolTip, bool bInDisablesSimulation = false, bool bInHidesClothSections = false, bool bInDefaultFlagValue = false)
		: DisplayName(InDisplayName)
		, ToolTip(InToolTip)
		, bDisablesSimulation(bInDisablesSimulation)
		, bHidesClothSections(bInHidesClothSections)
		, bDefaultFlagValue(bInDefaultFlagValue)
	{}

	~FVisualizationOption()
	{
	}
};

const FVisualizationOption FVisualizationOption::OptionData[] =
{
	FVisualizationOption(
		FLocalDebugDisplayString::CreateLambda([](const FClothEditorSimulationVisualization&,const FClothSimulationProxy& Proxy)
		{
			return GetSimulationStatisticsString(Proxy);
		}),
		LOCTEXT("ChaosVisName_SimulationStatistics", "Simulation Statistics"),
		LOCTEXT("ChaosVisName_SimulationStatistics_Tooltip", "Displays simulation statistics"),
		/*bDisablesSimulation =*/false, /*bHidesClothSections=*/false, /*bDefaultFlagValue=*/true),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawPhysMeshShaded(PDI);
		}),
		LOCTEXT("ChaosVisName_PhysMesh", "Physical Mesh (Flat Shaded)"),
		LOCTEXT("ChaosVisName_PhysMeshShaded_ToolTip", "Draws the current physical result as a doubled sided flat shaded mesh"),
		/*bDisablesSimulation =*/false, /*bHidesClothSections=*/true),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawPhysMeshWired(PDI);
		}), 
		LOCTEXT("ChaosVisName_PhysMeshWire", "Physical Mesh (Wireframe)"), 
		LOCTEXT("ChaosVisName_PhysMeshWired_ToolTip", "Draws the current physical mesh result in wireframe")),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawAnimMeshWired(PDI);
		}),
		LOCTEXT("ChaosVisName_AnimMeshWire", "Animated Mesh (Wireframe)"), 
		LOCTEXT("ChaosVisName_AnimMeshWired_ToolTip", "Draws the current animated mesh input in wireframe")),
	FVisualizationOption(
		LOCTEXT("ChaosVisName_HideRenderMesh", "Hide Render Mesh"), 
		LOCTEXT("ChaosVisName_HideRenderMesh_ToolTip", "Hide the render mesh."), 
		/*bDisablesSimulation =*/false, /*bHidesClothSections=*/true),
	FVisualizationOption(
		FClothVisualizationDebugDrawTexts::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FCanvas* Canvas, const FSceneView* SceneView)
		{
			Visualization.DrawParticleIndices(Canvas, SceneView);
		}),
		LOCTEXT("ChaosVisName_ParticleIndices", "Particle Indices"), 
		LOCTEXT("ChaosVisName_ParticleIndices_ToolTip", "Draws the particle indices as instantiated by the solver")),
	FVisualizationOption(
		FClothVisualizationDebugDrawTexts::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FCanvas* Canvas, const FSceneView* SceneView)
		{
			Visualization.DrawElementIndices(Canvas, SceneView);
		}),
		LOCTEXT("ChaosVisName_ElementIndices", "Element Indices"), 
		LOCTEXT("ChaosVisName_ElementIndices_ToolTip", "Draws the element's (triangle or other) indices as instantiated by the solver")),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization& EditorVisualization, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawPointNormals(PDI, EditorVisualization.GetPointNormalLength());
		}),
		LOCTEXT("ChaosVisName_PointNormals", "Physical Mesh Normals"), 
		LOCTEXT("ChaosVisName_PointNormals_ToolTip", "Draws the current point normals for the simulation mesh"),
		/*bDisablesSimulation =*/false, /*bHidesClothSections=*/false, /*bDefaultFlagValue=*/false, 
		FAdditionalMenus::CreateLambda([](FClothEditorSimulationVisualization& EditorVisualization, FMenuBuilder& MenuBuilder)
		{
			EditorVisualization.ExtendViewportShowMenuPointNormalsLength(MenuBuilder);
		})),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawPointVelocities(PDI);
		}),
		LOCTEXT("ChaosVisName_PointVelocities", "Point Velocities"), 
		LOCTEXT("ChaosVisName_PointVelocities_ToolTip", "Draws the current point velocities for the simulation mesh")),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization& EditorVisualization, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawAnimNormals(PDI, EditorVisualization.GetAnimatedNormalLength());
		}),
		LOCTEXT("ChaosVisName_AnimNormals", "Animated Mesh Normals"), 
		LOCTEXT("ChaosVisName_AnimNormals_ToolTip", "Draws the current point normals for the animated mesh"),
		/*bDisablesSimulation =*/false, /*bHidesClothSections=*/false, /*bDefaultFlagValue=*/false,
		FAdditionalMenus::CreateLambda([](FClothEditorSimulationVisualization& EditorVisualization, FMenuBuilder& MenuBuilder)
		{
			EditorVisualization.ExtendViewportShowMenuAnimatedNormalsLength(MenuBuilder);
		})),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawCollision(PDI);
		}),
		LOCTEXT("ChaosVisName_Collision", "Collisions"), 
		LOCTEXT("ChaosVisName_Collision_ToolTip", "Draws the collision bodies the simulation is currently using")),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawBackstops(PDI);
		}),
		LOCTEXT("ChaosVisName_Backstop", "Backstops"), 
		LOCTEXT("ChaosVisName_Backstop_ToolTip", "Draws the backstop radius and position for each simulation particle")),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawBackstopDistances(PDI);
		}),
		LOCTEXT("ChaosVisName_BackstopDistance", "Backstop Distances"), 
		LOCTEXT("ChaosVisName_BackstopDistance_ToolTip", "Draws the backstop distance offset for each simulation particle"), 
		/*bDisablesSimulation =*/true),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawMaxDistances(PDI);
		}),
		LOCTEXT("ChaosVisName_MaxDistance", "Max Distances"), 
		LOCTEXT("ChaosVisName_MaxDistance_ToolTip", "Draws the current max distances for the sim particles as a line along its normal")),
	FVisualizationOption(
		FClothVisualizationDebugDrawTexts::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FCanvas* Canvas, const FSceneView* SceneView)
		{
			Visualization.DrawMaxDistanceValues(Canvas, SceneView);
		}),
		LOCTEXT("ChaosVisName_MaxDistanceValue", "Max Distances As Numbers"), 
		LOCTEXT("ChaosVisName_MaxDistanceValue_ToolTip", "Draws the current max distances as numbers")),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawAnimDrive(PDI);
		}),
		LOCTEXT("ChaosVisName_AnimDrive", "Anim Drive"), 
		LOCTEXT("ChaosVisName_AnimDrive_Tooltip", "Draws the current skinned reference mesh for the simulation which anim drive will attempt to reach if enabled")),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawEdgeConstraint(PDI);
		}),
		LOCTEXT("ChaosVisName_EdgeConstraint", "Edge Constraint"),
		LOCTEXT("ChaosVisName_EdgeConstraint_Tooltip", "Draws the edge spring constraints")),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawBendingConstraint(PDI);
		}),
		LOCTEXT("ChaosVisName_BendingConstraint", "Bending Constraint"), 
		LOCTEXT("ChaosVisName_BendingConstraint_Tooltip", "Draws the bending spring constraints")),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawLongRangeConstraint(PDI);
		}),
		LOCTEXT("ChaosVisName_LongRangeConstraint" , "Long Range Constraint"), 
		LOCTEXT("ChaosVisName_LongRangeConstraint_Tooltip", "Draws the long range attachment constraint distances")),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization& EditorVisualization, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawWindAndPressureForces(PDI, EditorVisualization.GetAerodynamicsLengthScale());
		}),
		LOCTEXT("ChaosVisName_WindAndPressureForces", "Wind Aerodynamic And Pressure Forces"), 
		LOCTEXT("ChaosVisName_WindAndPressure_Tooltip", "Draws the Wind drag and lift and pressure forces"),
		/*bDisablesSimulation =*/false, /*bHidesClothSections=*/false, /*bDefaultFlagValue=*/false,
		FAdditionalMenus::CreateLambda([](FClothEditorSimulationVisualization& EditorVisualization, FMenuBuilder& MenuBuilder)
		{
			EditorVisualization.ExtendViewportShowMenuAerodynamicsLengthScale(MenuBuilder);
		})),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawLocalSpace(PDI);
		}),
		LOCTEXT("ChaosVisName_LocalSpace", "Local Space Reference Bone"), 
		LOCTEXT("ChaosVisName_LocalSpace_Tooltip", "Draws the local space reference bone")),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawGravity(PDI);
		}),
		LOCTEXT("ChaosVisName_Gravity", "Gravity"), 
		LOCTEXT("ChaosVisName_Gravity_Tooltip", "Draws gravity")),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawFictitiousAngularForces(PDI);
		}),
		LOCTEXT("ChaosVisName_FictitiousAngularForces", "Fictitious Angular Forces"),
		LOCTEXT("ChaosVisName_Gravity_FictitiousAngularForces", "Draws fictitious angular forces (force based solver only)")),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawSelfCollision(PDI);
		}),
		LOCTEXT("ChaosVisName_SelfCollision", "Self Collision"), 
		LOCTEXT("ChaosVisName_SelfCollision_Tooltip", "Draws the self collision thickness/debugging information")),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawSelfIntersection(PDI);
		}),
		LOCTEXT("ChaosVisName_SelfIntersection", "Self Intersection"), 
		LOCTEXT("ChaosVisName_SelfIntersection_Tooltip", "Draws the self intersection contour/region information")),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawSelfCollisionLayers(PDI);
		}),
		LOCTEXT("ChaosVisName_SelfCollisionLayers", "Self Collision Layers"), 
		LOCTEXT("ChaosVisName_SelfCollisionLayers_Tooltip", "Draws the self collision layers"),
		/*bDisablesSimulation =*/false, /*bHidesClothSections=*/true),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawSelfCollisionThickness(PDI);
		}),
		LOCTEXT("ChaosVisName_SelfCollisionThickness", "Self Collision Thickness"), 
		LOCTEXT("ChaosVisName_SelfCollisionThickness_Tooltip", "Draws the self collision Thickness")),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawKinematicColliderShaded(PDI);
		}),
		LOCTEXT("ChaosVisName_DrawKinematicColliderShaded", "Draw Kinematic Colliders (Shaded)"), 
		LOCTEXT("ChaosVisName_DrawKinematicColliderShaded_Tooltip", "Draw kinematic cloth colliders with flat shading.")),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawKinematicColliderWired(PDI);
		}),
		LOCTEXT("ChaosVisName_DrawKinematicColliderWired", "Draw Kinematic Colliders (Wireframe)"), 
		LOCTEXT("ChaosVisName_DrawKinematicColliderWired_Tooltip", "Draw kinematic cloth colliders in wireframe.")),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawMultiResConstraint(PDI);
		}),
		LOCTEXT("ChaosVisName_DrawMultiResConstraint", "Draw Multi Res Constraint"), 
		LOCTEXT("ChaosVisName_DrawMultiResConstraint_Tooltip", "Draw multi res constraint coarse mesh and targets.")),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization& EditorVisualization, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			const FString* const WeightMap = EditorVisualization.GetCurrentlySelectedWeightMap();
			Visualization.DrawWeightMapWithName(PDI, WeightMap ? *WeightMap : FString());
		}),
		LOCTEXT("ChaosVisName_DrawWeightMap", "Weight Map"), 
		LOCTEXT("ChaosVisName_DrawWeightMap_ToolTip", "Draw the weight map for the simulation mesh. You can control the name of the map to be visualized by setting the p.ChaosClothVisualization.WeightMapName console variable."), 
		/*bDisablesSimulation =*/false, /*bHidesClothSections=*/true, /*bDefaultFlagValue=*/false,
		FAdditionalMenus::CreateLambda([](FClothEditorSimulationVisualization& EditorVisualization, FMenuBuilder& MenuBuilder)
		{
			EditorVisualization.ExtendViewportShowMenuWeightMapSelector(MenuBuilder);
		})),
	FVisualizationOption(
		FClothVisualizationDebugDraw::CreateLambda([](const FClothEditorSimulationVisualization&, const ::Chaos::FClothVisualizationNoGC& Visualization, FPrimitiveDrawInterface* PDI)
		{
			Visualization.DrawInpaintWeightsMatched(PDI);
		}),
		LOCTEXT("ChaosVisName_DrawInpaintWeightsMatched", "Transfer Skin Weights Node: Matched Vertices"), 
		LOCTEXT("ChaosVisName_DrawInpaintWeightsMatched_ToolTip", "When transferring weights using the InpaintWeights method, will highlight the vertices for which we copied the weights directly from the source mesh. For all other vertices, the weights were computed automatically."), 
		/*bDisablesSimulation =*/false, /*bHidesClothSections=*/true),
};
const uint32 FVisualizationOption::Count = sizeof(OptionData) / sizeof(FVisualizationOption);



} // namespace Private

FClothEditorSimulationVisualization::FClothEditorSimulationVisualization()
	: Flags(false, Private::FVisualizationOption::Count)
{
	for (uint32 OptionIndex = 0; OptionIndex < Private::FVisualizationOption::Count; ++OptionIndex)
	{
		Flags[OptionIndex] = Private::FVisualizationOption::OptionData[OptionIndex].bDefaultFlagValue;
	}
}


void FClothEditorSimulationVisualization::ExtendViewportShowMenu(FMenuBuilder& MenuBuilder, const TFunction<UChaosClothComponent*()>& GetClothComponentFunc)
{
	MenuBuilder.BeginSection(TEXT("ChaosSimulation_Visualizations"), LOCTEXT("ClothVisualizationSection", "Chaos Cloth Visualization"));
	{
		for (uint32 OptionIndex = 0; OptionIndex < Private::FVisualizationOption::Count; ++OptionIndex)
		{
			// Handler for visualization entry being clicked
			const FExecuteAction ExecuteAction = FExecuteAction::CreateLambda([this, OptionIndex, GetClothComponentFunc]()
			{
				Flags[OptionIndex] = !Flags[OptionIndex];

				if (UChaosClothComponent* const ClothComponent = GetClothComponentFunc())
				{
					// If we need to toggle the disabled or visibility states, handle it
					// Disable simulation
					const bool bShouldDisableSimulation = ShouldDisableSimulation();
					if (ClothComponent->IsSimulationEnabled() == bShouldDisableSimulation)
					{
						ClothComponent->SetEnableSimulation(!bShouldDisableSimulation);
					}
					// Hide cloth section
					if (Private::FVisualizationOption::OptionData[OptionIndex].bHidesClothSections)
					{
						const bool bIsClothSectionsVisible = !Flags[OptionIndex];
						ShowClothSections(ClothComponent, bIsClothSectionsVisible);
					}
				}
			});

			// Checkstate function for visualization entries
			const FIsActionChecked IsActionChecked = FIsActionChecked::CreateLambda([this, OptionIndex]()
			{
				return Flags[OptionIndex];
			});

			const FUIAction Action(ExecuteAction, FCanExecuteAction(), IsActionChecked);

			// Add menu entry
			MenuBuilder.AddMenuEntry(Private::FVisualizationOption::OptionData[OptionIndex].DisplayName, Private::FVisualizationOption::OptionData[OptionIndex].ToolTip, FSlateIcon(), Action, NAME_None, EUserInterfaceActionType::ToggleButton);
			Private::FVisualizationOption::OptionData[OptionIndex].AdditionalMenus.ExecuteIfBound(*this, MenuBuilder);
		}
	}
	MenuBuilder.EndSection();
}


void FClothEditorSimulationVisualization::ExtendViewportShowMenu(FMenuBuilder& MenuBuilder, const TSharedRef<FChaosClothAssetEditor3DViewportClient>& ViewportClient)
{
	TWeakPtr<FChaosClothAssetEditor3DViewportClient> ViewportClientWeakPtr(ViewportClient);

	ExtendViewportShowMenu(MenuBuilder, [ViewportClientWeakPtr]() -> UChaosClothComponent*
	{
		if (TSharedPtr<FChaosClothAssetEditor3DViewportClient> ViewportClient = ViewportClientWeakPtr.Pin())
		{
			return ViewportClient->GetPreviewClothComponent();
		}

		return nullptr;
	});
}


void FClothEditorSimulationVisualization::ExtendViewportShowMenu(FMenuBuilder& MenuBuilder, const TSharedRef<FDataflowSimulationViewportClient>& ViewportClient)
{
	TWeakPtr<FDataflowSimulationViewportClient> ViewportClientWeakPtr(ViewportClient);

	ExtendViewportShowMenu(MenuBuilder, [ViewportClientWeakPtr]() -> UChaosClothComponent*
	{
		if (TSharedPtr<FDataflowSimulationViewportClient> ViewportClient = ViewportClientWeakPtr.Pin())
		{
			if (TSharedPtr<FDataflowEditorToolkit> Toolkit = ViewportClient->GetDataflowEditorToolkit().Pin())
			{
				if (const TSharedPtr<FDataflowSimulationScene>& SimulationScene = Toolkit->GetSimulationScene())
				{
					if (const TObjectPtr<AActor> PreviewActor = SimulationScene->GetPreviewActor())
					{
						return PreviewActor->GetComponentByClass<UChaosClothComponent>();
					}
				}
			}
		}

		return nullptr;
	});
}


void FClothEditorSimulationVisualization::ExtendViewportShowMenuWeightMapSelector(FMenuBuilder& MenuBuilder)
{
	WeightMapSelector =
		SNew(STextComboBox)
		.OptionsSource(&WeightMapNames)
		.OnSelectionChanged(STextComboBox::FOnTextSelectionChanged::CreateRaw(this, &FClothEditorSimulationVisualization::WeightMapSelectionChanged));

	MenuBuilder.AddMenuEntry(FUIAction(), WeightMapSelector.ToSharedRef(),
		NAME_None, LOCTEXT("WeightMapNameSelection", "Select weight map to draw."), EUserInterfaceActionType::None);
}

void FClothEditorSimulationVisualization::WeightMapSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo)
{
	CurrentlySelectedWeightMap = Selection;
}

void FClothEditorSimulationVisualization::ExtendViewportShowMenuSpinBox(FMenuBuilder& MenuBuilder, float& Value, const float MinValue, const float MaxValue, const float MinSliderValue, const float MaxSliderValue)
{
	TSharedRef<SWidget> SpinBox =
		SNew(SSpinBox<float>)
		.Value(Value)
		.OnValueChanged_Lambda([&Value](float NewValue) { Value = NewValue; })
		.MinValue(MinValue)
		.MaxValue(MaxValue)
		.MinSliderValue(MinSliderValue)
		.MaxSliderValue(MaxSliderValue);

	MenuBuilder.AddMenuEntry(FUIAction(), SpinBox,
		NAME_None, LOCTEXT("SetNormalLength", "Set the normal display length."), EUserInterfaceActionType::None);
}

void FClothEditorSimulationVisualization::RefreshMenusForClothComponent(const UChaosClothComponent* ClothComponent)
{
	TArray<FString> WeightMaps;
	if (const ::Chaos::FClothVisualizationNoGC* const Visualization = ClothComponent && ClothComponent->GetClothSimulationProxy() ? ClothComponent->GetClothSimulationProxy()->GetClothVisualization() : nullptr)
	{
		WeightMaps = Visualization->GetAllWeightMapNames();
	}

	bool bFoundCurrentlySelected = false;
	WeightMapNames.Empty(WeightMaps.Num());
	for (const FString& Map : WeightMaps)
	{
		const int32 Index = WeightMapNames.Emplace(MakeShared<FString>(Map));
		if (!bFoundCurrentlySelected && CurrentlySelectedWeightMap && Map == *CurrentlySelectedWeightMap)
		{
			CurrentlySelectedWeightMap = WeightMapNames[Index];
			bFoundCurrentlySelected = true;
		}
	}
	if (!bFoundCurrentlySelected)
	{
		if (WeightMapNames.IsEmpty())
		{
			CurrentlySelectedWeightMap.Reset();
		}
		else
		{
			CurrentlySelectedWeightMap = WeightMapNames[0];
		}
	}
	if (WeightMapSelector)
	{
		WeightMapSelector->RefreshOptions();
		WeightMapSelector->SetSelectedItem(CurrentlySelectedWeightMap);
	}
}

void FClothEditorSimulationVisualization::DebugDrawSimulation(const UChaosClothComponent* ClothComponent, FPrimitiveDrawInterface* PDI)
{
	const ::Chaos::FClothVisualizationNoGC* const Visualization = ClothComponent && ClothComponent->GetClothSimulationProxy() ? ClothComponent->GetClothSimulationProxy()->GetClothVisualization() : nullptr;
	if (!Visualization)
	{
		return;
	}

	for (int32 OptionIndex = 0; OptionIndex < Private::FVisualizationOption::Count; ++OptionIndex)
	{
		if (Flags[OptionIndex])
		{
			Private::FVisualizationOption::OptionData[OptionIndex].ClothVisualizationDebugDraw.ExecuteIfBound(*this, *Visualization, PDI);
		}
	}
}

void FClothEditorSimulationVisualization::DebugDrawSimulationTexts(const UChaosClothComponent* ClothComponent, FCanvas* Canvas, const FSceneView* SceneView)
{
	const ::Chaos::FClothVisualizationNoGC* const Visualization = ClothComponent && ClothComponent->GetClothSimulationProxy() ? ClothComponent->GetClothSimulationProxy()->GetClothVisualization() : nullptr;
	if (!Visualization)
	{
		return;
	}
	for (int32 OptionIndex = 0; OptionIndex < Private::FVisualizationOption::Count; ++OptionIndex)
	{
		if (Flags[OptionIndex])
		{
			Private::FVisualizationOption::OptionData[OptionIndex].ClothVisualizationDebugDrawTexts.ExecuteIfBound(*this, *Visualization, Canvas, SceneView);
		}
	}
}

FText FClothEditorSimulationVisualization::GetDisplayString(const UChaosClothComponent* ClothComponent) const
{
	const FClothSimulationProxy* const SimProxy = ClothComponent ? ClothComponent->GetClothSimulationProxy() : nullptr;
	if (!SimProxy)
	{
		return FText();
	}

	FText DisplayString;
	for (int32 OptionIndex = 0; OptionIndex < Private::FVisualizationOption::Count; ++OptionIndex)
	{
		if (Private::FVisualizationOption::OptionData[OptionIndex].LocalDebugDisplayString.IsBound() && Flags[OptionIndex])
		{
			DisplayString = Private::ConcatenateLine(DisplayString, Private::FVisualizationOption::OptionData[OptionIndex].LocalDebugDisplayString.Execute(*this, *SimProxy));
		}
	}
	return DisplayString;
}

bool FClothEditorSimulationVisualization::ShouldDisableSimulation() const
{
	for (uint32 OptionIndex = 0; OptionIndex < Private::FVisualizationOption::Count; ++OptionIndex)
	{
		if (Flags[OptionIndex])
		{
			const Private::FVisualizationOption& Data = Private::FVisualizationOption::OptionData[OptionIndex];

			if (Data.bDisablesSimulation)
			{
				return true;
			}
		}
	}
	return false;
}

void FClothEditorSimulationVisualization::ShowClothSections(UChaosClothComponent* ClothComponent, bool bIsClothSectionsVisible) const
{
	if (FSkeletalMeshRenderData* const SkeletalMeshRenderData = ClothComponent->GetSkeletalMeshRenderData())
	{
		for (int32 LODIndex = 0; LODIndex < SkeletalMeshRenderData->LODRenderData.Num(); ++LODIndex)
		{
			FSkeletalMeshLODRenderData& SkeletalMeshLODRenderData = SkeletalMeshRenderData->LODRenderData[LODIndex];

			for (int32 SectionIndex = 0; SectionIndex < SkeletalMeshLODRenderData.RenderSections.Num(); ++SectionIndex)
			{
				FSkelMeshRenderSection& SkelMeshRenderSection = SkeletalMeshLODRenderData.RenderSections[SectionIndex];

				if (SkelMeshRenderSection.HasClothingData())
				{
					ClothComponent->ShowMaterialSection(SkelMeshRenderSection.MaterialIndex, SectionIndex, bIsClothSectionsVisible, LODIndex);
				}
			}
		}
	}
}
} // namespace UE::Chaos::ClothAsset

#undef LOCTEXT_NAMESPACE
