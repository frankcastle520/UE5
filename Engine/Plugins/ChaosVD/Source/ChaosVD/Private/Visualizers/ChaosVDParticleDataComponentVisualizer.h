// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosVDComponentVisualizerBase.h"
#include "ComponentVisualizer.h"
#include "HAL/Platform.h"
#include "IChaosVDParticleVisualizationDataProvider.h"
#include "Settings/ChaosVDParticleVisualizationSettings.h"
#include "Templates/SharedPointer.h"

class FChaosVDGeometryBuilder;
struct FChaosParticleDataDebugDrawSettings;

class UChaosVDParticleVisualizationDebugDrawSettings;

struct FChaosVDParticleDataVisualizationContext : public FChaosVDVisualizationContext
{
	TWeakPtr<FChaosVDGeometryBuilder> GeometryGenerator = nullptr;
	bool bIsSelectedData = false;
	bool bShowDebugText = false;

	const UChaosVDParticleVisualizationDebugDrawSettings* DebugDrawSettings = nullptr;

	bool IsVisualizationFlagEnabled(EChaosVDParticleDataVisualizationFlags Flag) const
	{
		const EChaosVDParticleDataVisualizationFlags FlagsAsParticleFlags = static_cast<EChaosVDParticleDataVisualizationFlags>(VisualizationFlags);
		return EnumHasAnyFlags(FlagsAsParticleFlags, Flag);
	}
};

/**
 * Component visualizer in charge of generating debug draw visualizations for for particles
 */
class FChaosVDParticleDataComponentVisualizer : public FChaosVDComponentVisualizerBase
{
public:
	FChaosVDParticleDataComponentVisualizer();

	virtual void RegisterVisualizerMenus() override;

	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;

	virtual bool CanHandleClick(const HChaosVDComponentVisProxy& VisProxy) override;

	virtual bool SelectVisualizedData(const HChaosVDComponentVisProxy& VisProxy, const TSharedRef<FChaosVDScene>& InCVDScene, const TSharedRef<SChaosVDMainTab>& InMainTabToolkitHost) override;

protected:
	
	void DrawParticleVector(FPrimitiveDrawInterface* PDI, const FVector& StartLocation, const FVector& InVector, EChaosVDParticleDataVisualizationFlags VectorID, const FChaosVDParticleDataVisualizationContext& InVisualizationContext, float LineThickness);
	void DrawVisualizationForParticleData(const UActorComponent* Component, FPrimitiveDrawInterface* PDI, const FSceneView* View, const FChaosVDParticleDataVisualizationContext& InVisualizationContext, const TSharedPtr<const FChaosVDParticleDataWrapper>& InParticleDataViewer);
};
