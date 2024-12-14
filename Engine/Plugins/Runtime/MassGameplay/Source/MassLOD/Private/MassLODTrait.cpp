// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassLODTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "MassLODFragments.h"
#include "MassEntityUtils.h"


//-----------------------------------------------------------------------------
// UMassLODCollectorTrait
//-----------------------------------------------------------------------------
void UMassLODCollectorTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.AddFragment<FMassViewerInfoFragment>();
	BuildContext.AddTag<FMassCollectLODViewerInfoTag>();
	BuildContext.RequireFragment<FTransformFragment>();
}

//-----------------------------------------------------------------------------
// UMassDistanceLODCollectorTrait
//-----------------------------------------------------------------------------
void UMassDistanceLODCollectorTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.AddFragment<FMassViewerInfoFragment>();
	BuildContext.AddTag<FMassCollectDistanceLODViewerInfoTag>();
	BuildContext.RequireFragment<FTransformFragment>();
}

//-----------------------------------------------------------------------------
// UMassSimulationLODTrait
//-----------------------------------------------------------------------------
void UMassSimulationLODTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.RequireFragment<FMassViewerInfoFragment>();
	BuildContext.RequireFragment<FTransformFragment>();

	FMassSimulationLODFragment& LODFragment = BuildContext.AddFragment_GetRef<FMassSimulationLODFragment>();

	// Start all simulation LOD in the Off 
	if (Params.bSetLODTags || bEnableVariableTicking || BuildContext.IsInspectingData())
	{
		LODFragment.LOD = EMassLOD::Off;
		BuildContext.AddTag<FMassOffLODTag>();
	}

	FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);

	FConstSharedStruct ParamsFragment = EntityManager.GetOrCreateConstSharedFragment(Params);
	BuildContext.AddConstSharedFragment(ParamsFragment);

	FSharedStruct SharedFragment = EntityManager.GetOrCreateSharedFragment<FMassSimulationLODSharedFragment>(FConstStructView::Make(Params), Params);
	BuildContext.AddSharedFragment(SharedFragment);

	// Variable ticking from simulation LOD
	if (bEnableVariableTicking || BuildContext.IsInspectingData())
	{
		BuildContext.AddFragment<FMassSimulationVariableTickFragment>();
		BuildContext.AddChunkFragment<FMassSimulationVariableTickChunkFragment>();

		FConstSharedStruct VariableTickParamsFragment = EntityManager.GetOrCreateConstSharedFragment(VariableTickParams);
		BuildContext.AddConstSharedFragment(VariableTickParamsFragment);

		FSharedStruct VariableTickSharedFragment = EntityManager.GetOrCreateSharedFragment<FMassSimulationVariableTickSharedFragment>(FConstStructView::Make(VariableTickParams), VariableTickParams);
		BuildContext.AddSharedFragment(VariableTickSharedFragment);
	}
}
