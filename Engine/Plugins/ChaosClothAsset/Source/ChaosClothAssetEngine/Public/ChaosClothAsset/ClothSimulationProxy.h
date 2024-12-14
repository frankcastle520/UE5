// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/TaskGraphInterfaces.h"
#include "Containers/Array.h"
#include "Math/Transform.h"
#include "Templates/UniquePtr.h"
#include "ClothingSystemRuntimeTypes.h"
#include "Dataflow/Interfaces/DataflowPhysicsSolver.h"

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5
namespace Dataflow = UE::Dataflow;
#else
namespace UE_DEPRECATED(5.5, "Use UE::Dataflow instead.") Dataflow {}
#endif

namespace Chaos
{
	class FClothingSimulationSolver;
	class FClothingSimulationMesh;
	class FClothingSimulationCloth;
	class FClothingSimulationConfig;
	class FClothingSimulationCollider;
	class FClothVisualization;
	class FClothVisualizationNoGC;
}

class UChaosClothComponent;
class UChaosClothAsset;
struct FChaosClothSimulationModel;
struct FReferenceSkeleton;
struct FClothingSimulationCacheData;

namespace UE::Chaos::ClothAsset
{
	class FCollisionSourcesProxy;
	struct FClothSimulationContext;

	/**
	 * Cloth simulation proxy.
	 * Class used to share data between the cloth simulation and the cloth component.
	 */
	class CHAOSCLOTHASSETENGINE_API FClothSimulationProxy : public FDataflowPhysicsSolverProxy
	{
	public:
		explicit FClothSimulationProxy(const UChaosClothComponent& InClothComponent);
		~FClothSimulationProxy();

		FClothSimulationProxy() = delete;  // This object cannot be created without a valid reference to a parent UChaosClothComponent 
		FClothSimulationProxy(const FClothSimulationProxy&) = delete;  // Disable the copy as there must be a single unique proxy per component
		FClothSimulationProxy(FClothSimulationProxy&&) = delete;  // Disable the move to force it to be associated with a valid component reference
		FClothSimulationProxy& operator=(const FClothSimulationProxy&) = delete;
		FClothSimulationProxy& operator=(FClothSimulationProxy&&) = delete;

		/** Start the simulation if it isn't paused or suspended and return true, or simply update the existing simulation data and return false otherwise. */
		bool Tick_GameThread(float DeltaTime);

		/** Wait for the parallel task to complete if one was running, and update the simulation data. */
		void CompleteParallelSimulation_GameThread();

		/** write simulation data back onto GT after the simulation is done */
		void PostSimulate_GameThread() {WriteSimulationData();}
		
		/** setup simulation data from GT before the simulation starts */
        bool PreSimulate_GameThread(float DeltaTime);

		/**
		 * Return a map of all simulation data as used by the skeletal rendering code.
		 * The map key is the rendering section's cloth index as set in FSkelMeshRenderSection::CorrespondClothAssetIndex,
		 * which is 0 for the entire cloth component since all of its sections share the same simulation data.
		 */
		const TMap<int32, FClothSimulData>& GetCurrentSimulationData_AnyThread() const;

		FBoxSphereBounds CalculateBounds_AnyThread() const;

		UE_DEPRECATED(5.5, "Use GetClothVisualization instead.")
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		const ::Chaos::FClothVisualization* GetVisualization() const { return Visualization.Get(); }
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		const ::Chaos::FClothVisualizationNoGC* GetClothVisualization() const;

		int32 GetNumCloths() const { return NumCloths; }
		int32 GetNumKinematicParticles() const { return NumKinematicParticles; }
		int32 GetNumDynamicParticles() const { return NumDynamicParticles; }
		int32 GetNumIterations() const { return NumIterations; }
		int32 GetNumSubsteps() const { return NumSubsteps; }
		int32 GetNumLinearSolveIterations() const { return LastLinearSolveIterations; }
		float GetLinearSolveError() const { return LastLinearSolveError; }
		float GetSimulationTime() const { return SimulationTime; }
		bool IsTeleported() const { return bIsTeleported; }
		bool HasCacheData() const { return CacheData.IsValid(); }

	protected:
		void Tick();
		void WriteSimulationData();
		bool SetupSimulationData(float DeltaTime);
		void InitializeConfigs();
		void FillSimulationContext(float DeltaTime, bool bIsInitialization = false);

	private:
		bool ShouldEnableSolver(bool bSolverCurrentlyEnabled) const;
		void UpdateClothLODs();

		// Begin FDataflowPhysicsSolverProxy overrides
		virtual void AdvanceSolverDatas(const float DeltaTime) override {Tick();}
		virtual void PreSolveProxy(const float DeltaTime);
		// End FDataflowPhysicsSolverProxy overrides

		// Internal physics thread object
		friend class FClothSimulationProxyParallelTask;

		// Reference for the cloth parallel task, to detect whether or not a simulation is running
		FGraphEventRef ParallelTask;

		// Simulation data written back to the component after the simulation has taken place
		TMap<int32, FClothSimulData> CurrentSimulationData;

		// Owner component
		const UChaosClothComponent& ClothComponent;

		// Simulation context used to store the required component data for the duration of the simulation
		TUniquePtr<FClothSimulationContext> ClothSimulationContext;

		// The collision data for the external collision sources
		TUniquePtr<FCollisionSourcesProxy> CollisionSourcesProxy;

		// The cloth simulation model used to create this simulation, ownership might get transferred to this proxy if it changes during the simulation
		TSharedPtr<const FChaosClothSimulationModel> ClothSimulationModel;

		// Simulation objects
		TUniquePtr<::Chaos::FClothingSimulationSolver> Solver;
		TArray<TUniquePtr<::Chaos::FClothingSimulationMesh>> Meshes;
		TArray<TUniquePtr<::Chaos::FClothingSimulationCloth>> Cloths;
		TArray<TUniquePtr<::Chaos::FClothingSimulationConfig>> Configs;
		TArray<TUniquePtr<::Chaos::FClothingSimulationCollider>> Colliders;
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		TUniquePtr<::Chaos::FClothVisualization> Visualization;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

		// Chaos Cache needs to have access to the solver.
		friend class FClothComponentCacheAdapter;

		// Additional data used by the cache adapter
		enum struct ESolverMode : uint8
		{
			Default = 0, // Default behavior. Enable solver if no cache data available.
			EnableSolverForSimulateRecord = 1, // Normal simulation. Also used when Recording.
			DisableSolverForPlayback = 2, // Solver is disabled. Used when live playing back cache.
		};
		TUniquePtr<FClothingSimulationCacheData> CacheData;
		ESolverMode SolverMode = ESolverMode::Default;

		// Properties that must be readable from all threads
		std::atomic<int32> NumCloths;
		std::atomic<int32> NumKinematicParticles;
		std::atomic<int32> NumDynamicParticles;
		std::atomic<int32> NumIterations;
		std::atomic<int32> NumSubsteps;
		std::atomic<float> SimulationTime;
		std::atomic<bool> bIsTeleported;
		std::atomic<int32> LastLinearSolveIterations; // For single cloth only.
		std::atomic<float> LastLinearSolveError; // For single cloth only.

		mutable bool bHasInvalidReferenceBoneTransforms = false;

		// Cached value of the MaxPhysicsDeltaTime setting for the life of this proxy
		const float MaxDeltaTime;
	};

#if !defined(CHAOS_TRANSFORM_CLOTH_SIMUL_DATA_ISPC_ENABLED_DEFAULT)
#define CHAOS_TRANSFORM_CLOTH_SIMUL_DATA_ISPC_ENABLED_DEFAULT 1
#endif

	// Support run-time toggling on supported platforms in non-shipping configurations
#if !INTEL_ISPC || UE_BUILD_SHIPPING
	static constexpr bool bTransformClothSimulData_ISPC_Enabled = INTEL_ISPC && CHAOS_TRANSFORM_CLOTH_SIMUL_DATA_ISPC_ENABLED_DEFAULT;
#else
	extern bool bTransformClothSimulData_ISPC_Enabled;
#endif
}
