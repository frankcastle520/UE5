// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/NameTypes.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/Text.h"

class FPrimitiveDrawInterface;
class FSceneView;
class FCanvas;
class FMenuBuilder;
class FDataflowSimulationScene;
class FDataflowSimulationViewportClient;

namespace UE::Dataflow
{

	class IDataflowSimulationVisualization
	{
	public:
		virtual ~IDataflowSimulationVisualization() = default;

		DATAFLOWEDITOR_API virtual FName GetName() const = 0;
		DATAFLOWEDITOR_API virtual void ExtendSimulationVisualizationMenu(const TSharedPtr<FDataflowSimulationViewportClient>& ViewportClient, FMenuBuilder& MenuBuilder) {};
		DATAFLOWEDITOR_API virtual void Draw(const FDataflowSimulationScene* SimulationScene, FPrimitiveDrawInterface* PDI) {};
		DATAFLOWEDITOR_API virtual void DrawCanvas(const FDataflowSimulationScene* SimulationScene, FCanvas* Canvas, const FSceneView* SceneView) {};
		DATAFLOWEDITOR_API virtual FText GetDisplayString(const FDataflowSimulationScene* SimulationScene) const { return FText(); };
	};

	class FDataflowSimulationVisualizationRegistry
	{
	public:

		// FLazySingleton
		static DATAFLOWEDITOR_API FDataflowSimulationVisualizationRegistry& GetInstance();
		static DATAFLOWEDITOR_API void TearDown();

		DATAFLOWEDITOR_API void RegisterVisualization(TUniquePtr<IDataflowSimulationVisualization>&& Visualization);
		DATAFLOWEDITOR_API void DeregisterVisualization(const FName& VisualizationName);

		DATAFLOWEDITOR_API const TMap<FName, TUniquePtr<IDataflowSimulationVisualization>>& GetVisualizations() const;
		DATAFLOWEDITOR_API const IDataflowSimulationVisualization* GetVisualization(const FName& VisualizationOption) const;

	private:

		DATAFLOWEDITOR_API static TUniquePtr<FDataflowSimulationVisualizationRegistry> Instance;

		TMap<FName, TUniquePtr<IDataflowSimulationVisualization>> VisualizationMap;
	};

}

