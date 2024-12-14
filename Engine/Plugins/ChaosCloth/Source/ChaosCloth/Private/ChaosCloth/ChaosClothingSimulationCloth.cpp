// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosCloth/ChaosClothingSimulationCloth.h"
#include "ChaosCloth/ChaosClothingSimulationSolver.h"
#include "ChaosCloth/ChaosClothingSimulationMesh.h"
#include "ChaosCloth/ChaosClothingSimulationCollider.h"
#include "ChaosCloth/ChaosClothingSimulationConfig.h"
#include "ChaosCloth/ChaosClothingPatternData.h"
#include "ChaosCloth/ChaosWeightMapTarget.h"
#include "ChaosCloth/ChaosClothPrivate.h"
#include "Chaos/CollectionPropertyFacade.h"
#include "Chaos/PBDFlatWeightMap.h"
#include "Chaos/PBDSoftBodyCollisionConstraint.h"
#include "Chaos/SoftsExternalForces.h"
#include "Chaos/SoftsSimulationSpace.h"
#include "Containers/ArrayView.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "HAL/IConsoleManager.h"
#include "ClothingSimulation.h"

namespace Chaos
{

namespace ClothingSimulationClothDefault
{
	constexpr int32 MassMode = (int32)FClothingSimulationCloth::EMassMode::Density;
	constexpr bool bUseGeodesicTethers = true;
	constexpr float MassValue = 0.35f;
	constexpr float MinPerParticleMass = 0.0001f;
	constexpr float CollisionThickness = Softs::FPBDSoftBodyCollisionConstraint::DefaultCollisionThickness;
	constexpr float FrictionCoefficient = Softs::FPBDSoftBodyCollisionConstraint::DefaultFrictionCoefficient;
	constexpr float DampingCoefficient = 0.01f;
	constexpr float Drag = 0.035f;
	constexpr float Lift = 0.035f;
	constexpr float Pressure = 0.f;
	constexpr float AirDensity = 1.225f;  // Air density in kg/m^3
	constexpr float GravityScale = Softs::FExternalForces::DefaultGravityScale; // 1.f;
	constexpr float GravityZOverride = Softs::FExternalForces::DefaultGravityZOverride; // -980.665f;
	constexpr EChaosSoftsSimulationSpace VelocityScaleSpace = EChaosSoftsSimulationSpace::ReferenceBoneSpace;
	constexpr float VelocityScale = 0.75f;
	constexpr float MaxVelocityScale = 1.f;
	constexpr float MaxVelocity = TNumericLimits<float>::Max();
	constexpr float MaxAcceleration = TNumericLimits<float>::Max();
	constexpr float FictitiousAngularScale = Softs::FExternalForces::DefaultFictitiousAngularScale; // 1.f;
	constexpr int32 MultiResCoarseLODIndex = INDEX_NONE;
}

namespace ClothingSimulationClothConsoleVariables
{
	TAutoConsoleVariable<bool> CVarLegacyDisablesAccurateWind(
		TEXT("p.ChaosCloth.LegacyDisablesAccurateWind"),
		true,
		TEXT("Whether using the Legacy wind model switches off the accurate wind model, or adds up to it"));

	TAutoConsoleVariable<float> CVarGravityMultiplier(
		TEXT("p.ChaosCloth.GravityMultiplier"),
		1.f,
		TEXT("Scalar multiplier applied at the final stage of the cloth's gravity formulation."));
}

using namespace Softs;
struct FClothingSimulationCloth::FLODData
{
	// Input mesh
	const int32 NumParticles;
	const TMap<FString, TConstArrayView<FRealSingle>> WeightMaps;
	const TMap<FString, const TSet<int32>*> VertexSets;
	const TMap<FString, const TSet<int32>*> FaceSets;
	const TMap<FString, TConstArrayView<int32>> FaceIntMaps;
	const TArray<TConstArrayView<TTuple<int32, int32, FRealSingle>>> Tethers;

	const FClothingPatternData PatternData;
	const FTriangleMesh NoOffsetTriangleMesh;

	// Per Solver data
	struct FSolverData
	{
		int32 LODIndex;
		int32 ParticleRangeId;
		FTriangleMesh OffsetTriangleMesh; // Only used if using PBD solver
		int32 MultiResCoarseLODIndex = INDEX_NONE;
	};
	TMap<FClothingSimulationSolver*, FSolverData> SolverData;

	// Stats
	int32 NumKinematicParticles;
	int32 NumDynamicParticles;

	FLODData(
		int32 InNumParticles,
		const TConstArrayView<uint32>& InIndices,
		const TConstArrayView<FVector2f>& InPatternPositions,
		const TConstArrayView<uint32>& InPatternIndices,
		const TConstArrayView<uint32>& InPatternToWeldedIndices,
		TMap<FString, TConstArrayView<FRealSingle>>&& InWeightMaps,
		TMap<FString, const TSet<int32>*>&& InVertexSets,
		TMap<FString, const TSet<int32>*>&& InFaceSets,
		TMap<FString, TConstArrayView<int32>>&& InFaceIntMaps,
		const TArray<TConstArrayView<TTuple<int32, int32, FRealSingle>>>& InTethers);

	static FTriangleMesh BuildTriangleMesh(const TConstArrayView<uint32>& Indices, const int32 NumParticles);

	void AddParticles(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth, int32 LODIndex);
	void Add(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth, int32 LODIndex);
	void Remove(FClothingSimulationSolver* Solver);

	void Update(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth);

	void Enable(FClothingSimulationSolver* Solver, bool bEnable) const;

	void ResetStartPose(FClothingSimulationSolver* Solver) const;

	void UpdateNormals(FClothingSimulationSolver* Solver) const;

	UE_CHAOS_DECLARE_INDEXLESS_PROPERTYCOLLECTION_NAME(MaxDistance, float);
	UE_CHAOS_DECLARE_INDEXLESS_PROPERTYCOLLECTION_NAME(MassValue, float);
	UE_CHAOS_DECLARE_INDEXLESS_PROPERTYCOLLECTION_NAME(MultiResCoarseLODIndex, int32);
	UE_CHAOS_DECLARE_INDEXLESS_PROPERTYCOLLECTION_NAME(IsCoarseMultiResLOD, bool);
	UE_CHAOS_DECLARE_INDEXLESS_PROPERTYCOLLECTION_NAME(KinematicVertices3D, bool); // Selection set name string property; bool value is not actually used.

};

FClothingSimulationCloth::FLODData::FLODData(
	int32 InNumParticles,
	const TConstArrayView<uint32>& InIndices,
	const TConstArrayView<FVector2f>& InPatternPositions,
	const TConstArrayView<uint32>& InPatternIndices,
	const TConstArrayView<uint32>& InPatternToWeldedIndices,
	TMap<FString, TConstArrayView<FRealSingle>>&& InWeightMaps,
	TMap<FString, const TSet<int32>*>&& InVertexSets,
	TMap<FString, const TSet<int32>*>&& InFaceSets,
	TMap<FString, TConstArrayView<int32>>&& InFaceIntMaps,
	const TArray<TConstArrayView<TTuple<int32, int32, FRealSingle>>>& InTethers)
	: NumParticles(InNumParticles)
	, WeightMaps(MoveTemp(InWeightMaps))
	, VertexSets(MoveTemp(InVertexSets))
	, FaceSets(MoveTemp(InFaceSets))
	, FaceIntMaps(MoveTemp(InFaceIntMaps))
	, Tethers(InTethers)
	, PatternData(NumParticles, InIndices, InPatternPositions, InPatternIndices, InPatternToWeldedIndices)
	, NoOffsetTriangleMesh(BuildTriangleMesh(InIndices, NumParticles))
{
}

FTriangleMesh FClothingSimulationCloth::FLODData::BuildTriangleMesh(const TConstArrayView<uint32>& Indices, const int32 InNumParticles)
{
	FTriangleMesh OutTriangleMesh;
	// Build a sim friendly triangle mesh including the solver particle's Offset
	const int32 NumElements = Indices.Num() / 3;
	TArray<TVec3<int32>> Elements;
	Elements.Reserve(NumElements);

	for (int32 i = 0; i < NumElements; ++i)
	{
		const int32 Index = 3 * i;
		Elements.Add(
			{ static_cast<int32>(Indices[Index]),
			 static_cast<int32>(Indices[Index + 1]),
			 static_cast<int32>(Indices[Index + 2]) });
	}

	OutTriangleMesh.Init(MoveTemp(Elements), 0, InNumParticles - 1);
	OutTriangleMesh.GetPointToTriangleMap(); // Builds map for later use by GetPointNormals(), and the velocity fields
	return OutTriangleMesh;
}

void FClothingSimulationCloth::FLODData::AddParticles(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth, int32 InLODIndex)
{
	check(Solver);
	check(Cloth);
	check(Cloth->Mesh);

	// Add a new solver data chunk
	check(!SolverData.Find(Solver));
	FSolverData& SolverDatum = SolverData.Add(Solver);
	SolverDatum.LODIndex = InLODIndex;
	// Add particles
	SolverDatum.ParticleRangeId = Solver->AddParticles(NumParticles, Cloth->GroupId);
	if (!NumParticles)
	{
		return;
	}

	if (Solver->IsLegacySolver())
	{
		const TArray<TVec3<int32>>& Elements = NoOffsetTriangleMesh.GetElements();
		TArray<TVec3<int32>> OffsetElements;
		OffsetElements.Reserve(Elements.Num());
		for (const TVec3<int32>& Element : Elements)
		{
			OffsetElements.Add({
				Element[0] + SolverDatum.ParticleRangeId,
				Element[1] + SolverDatum.ParticleRangeId,
				Element[2] + SolverDatum.ParticleRangeId });
		}

		SolverDatum.OffsetTriangleMesh.Init(MoveTemp(OffsetElements), SolverDatum.ParticleRangeId, SolverDatum.ParticleRangeId + NumParticles - 1);
		SolverDatum.OffsetTriangleMesh.GetPointToTriangleMap(); // Builds map for later use by GetPointNormals(), and the velocity fields
	}

	const FTriangleMesh& TriangleMesh = Solver->IsLegacySolver() ? SolverDatum.OffsetTriangleMesh : NoOffsetTriangleMesh;

	// Update source mesh for this LOD, this is required prior to reset the start pose
	Cloth->Mesh->Update(Solver, INDEX_NONE, InLODIndex, 0, SolverDatum.ParticleRangeId);

	// Reset the particles start pose before setting up mass and constraints
	ResetStartPose(Solver);

	// Initialize the normals, in case the sim data is queried before the simulation steps
	UpdateNormals(Solver);

	// Retrieve config properties
	check(Cloth->Config);
	const Softs::FCollectionPropertyFacade& ConfigProperties = Cloth->Config->GetProperties(InLODIndex);

	// Retrieve MaxDistance information (weight map and Low/High values)
	const Softs::FPBDFlatWeightMapView MaxDistances(
		GetWeightedFloatMaxDistance(ConfigProperties, FVector2f(0.f, 1.f)),
		WeightMaps.FindRef(GetMaxDistanceString(ConfigProperties, MaxDistanceName.ToString())),
		NumParticles);

	const TSet<int32>* const KinematicVertices3DSet = VertexSets.FindRef(GetKinematicVertices3DString(ConfigProperties, KinematicVertices3DName.ToString()), nullptr);

	// Set the particle masses
	static const FRealSingle KinematicDistanceThreshold = 0.1f;  // TODO: This is not the same value as set in the painting UI but we might want to expose this value as parameter
	auto KinematicPredicate =
		[&MaxDistances, KinematicVertices3DSet](int32 Index)
	{
		return MaxDistances.GetValue(Index) < KinematicDistanceThreshold || (KinematicVertices3DSet && KinematicVertices3DSet->Contains(Index));
	};

	const int32 MassMode = ConfigProperties.GetValue<int32>(TEXT("MassMode"), ClothingSimulationClothDefault::MassMode);

	constexpr FRealSingle MinPerParticleMassClampMin = UE_SMALL_NUMBER;
	const FRealSingle MinPerParticleMass = FMath::Max((FRealSingle)ConfigProperties.GetValue<float>(TEXT("MinPerParticleMass"), ClothingSimulationClothDefault::MinPerParticleMass), MinPerParticleMassClampMin);

	switch (MassMode)
	{
	default:
		check(false);
	case EMassMode::UniformMass:
	{
		const FVector2f MassValue = GetWeightedFloatMassValue(ConfigProperties, ClothingSimulationClothDefault::MassValue);
		const TConstArrayView<float> MassValueMultipliers = WeightMaps.FindRef(GetMassValueString(ConfigProperties, MassValueName.ToString()));
		Solver->SetParticleMassUniform(SolverDatum.ParticleRangeId, MassValue, MassValueMultipliers, MinPerParticleMass, TriangleMesh, KinematicPredicate);
	}
	break;
	case EMassMode::TotalMass:
	{
		const FRealSingle MassValue = GetMassValue(ConfigProperties, ClothingSimulationClothDefault::MassValue);
		Solver->SetParticleMassFromTotalMass(SolverDatum.ParticleRangeId, MassValue, MinPerParticleMass, TriangleMesh, KinematicPredicate);
	}
	break;
	case EMassMode::Density:
	{
		const FVector2f MassValue = GetWeightedFloatMassValue(ConfigProperties, ClothingSimulationClothDefault::MassValue);
		const TConstArrayView<float> MassValueMultipliers = WeightMaps.FindRef(GetMassValueString(ConfigProperties, MassValueName.ToString()));
		Solver->SetParticleMassFromDensity(SolverDatum.ParticleRangeId, MassValue, MassValueMultipliers, MinPerParticleMass, TriangleMesh, KinematicPredicate);
	}
	break;
	}
}

void FClothingSimulationCloth::FLODData::Add(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth, int32 InLODIndex)
{
	check(Solver);
	check(Cloth);
	check(Cloth->Mesh);

	FSolverData& SolverDatum = SolverData.FindChecked(Solver);
	check(SolverDatum.LODIndex == InLODIndex);
	if (!NumParticles)
	{
		return;
	}

	check(SolverDatum.ParticleRangeId != INDEX_NONE);
	const int32 ParticleRangeId = SolverDatum.ParticleRangeId;

	// Retrieve the component's scale
	const FReal LocalSpaceScale = Solver->GetLocalSpaceScale();
	check(LocalSpaceScale > UE_SMALL_NUMBER);
	const FReal LocalSpaceScaleInv = 1. / LocalSpaceScale;
	const Softs::FSolverReal MeshScale = Cloth->Mesh->GetScale() * LocalSpaceScaleInv;

	const FTriangleMesh& TriangleMesh = Solver->IsLegacySolver() ? SolverDatum.OffsetTriangleMesh : NoOffsetTriangleMesh;

	// Retrieve config properties
	check(Cloth->Config);
	const Softs::FCollectionPropertyFacade& ConfigProperties = Cloth->Config->GetProperties(InLODIndex);

	// Gather multires constraint data.
	TSharedPtr<Softs::FMultiResConstraints> FineLODMultiResConstraint;
	const FTriangleMesh* CoarseLODTriangleMesh = nullptr;
	int32 CoarseLODParticleRangeId = INDEX_NONE;
	if (!Solver->IsLegacySolver())
	{
		// Multi-Res isn't supported by legacy solver
		if (InLODIndex == 0)
		{
			// Only allow LOD0 to be a fine LOD for now.
			const int32 MultiResCoarseLODIndex = GetMultiResCoarseLODIndex(ConfigProperties, ClothingSimulationClothDefault::MultiResCoarseLODIndex);
			if (MultiResCoarseLODIndex != INDEX_NONE && MultiResCoarseLODIndex != InLODIndex)
			{
				if (Cloth->Config->IsValidLOD(MultiResCoarseLODIndex) && Cloth->LODData.IsValidIndex(MultiResCoarseLODIndex))
				{
					// Check if coarse lod is setup correctly.
					const Softs::FCollectionPropertyFacade& CoarseConfigProperties = Cloth->Config->GetProperties(MultiResCoarseLODIndex);
					if (GetIsCoarseMultiResLOD(CoarseConfigProperties, false))
					{
						CoarseLODTriangleMesh = &Cloth->LODData[MultiResCoarseLODIndex]->NoOffsetTriangleMesh;
						CoarseLODParticleRangeId = Cloth->LODData[MultiResCoarseLODIndex]->SolverData.FindChecked(Solver).ParticleRangeId;
						SolverDatum.MultiResCoarseLODIndex = MultiResCoarseLODIndex;
					}
				}
			}
		}
		else if (GetIsCoarseMultiResLOD(ConfigProperties, false))
		{
			// check that fine lod is setup correctly.
			const Softs::FCollectionPropertyFacade& FineConfigProperties = Cloth->Config->GetProperties(0);
			const int32 MultiResCoarseLODIndex = GetMultiResCoarseLODIndex(FineConfigProperties, ClothingSimulationClothDefault::MultiResCoarseLODIndex);
			if (MultiResCoarseLODIndex == InLODIndex)
			{
				const int32 FineLODParticleRangeId = Cloth->LODData[0]->SolverData.FindChecked(Solver).ParticleRangeId;
				FineLODMultiResConstraint = Solver->GetClothConstraints(FineLODParticleRangeId).GetMultiResConstraints();
			}
		}
	}

	// Setup solver constraints
	FClothConstraints& ClothConstraints = Solver->GetClothConstraints(ParticleRangeId);

	// Create constraints
	const bool bEnabled = false;  // Set constraint disabled by default
	ClothConstraints.AddRules(ConfigProperties, TriangleMesh, &PatternData, WeightMaps, VertexSets, FaceSets, FaceIntMaps, Tethers, MeshScale, bEnabled, CoarseLODTriangleMesh, CoarseLODParticleRangeId, FineLODMultiResConstraint);

	// Update LOD stats
	const TConstArrayView<Softs::FSolverReal> InvMasses(Solver->GetParticleInvMasses(ParticleRangeId), NumParticles);
	NumKinematicParticles = 0;
	NumDynamicParticles = 0;
	for (int32 Index = 0; Index < NumParticles; ++Index)
	{
		if (InvMasses[Index] == (Softs::FSolverReal)0.)
		{
			++NumKinematicParticles;
		}
		else
		{
			++NumDynamicParticles;
		}
	}
}

void FClothingSimulationCloth::FLODData::Remove(FClothingSimulationSolver* Solver)
{
	SolverData.Remove(Solver);
}

void FClothingSimulationCloth::FLODData::Update(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth)
{
	check(Solver);
	check(Cloth); 
	const FSolverData& SolverDatum = SolverData.FindChecked(Solver);

	const int32 ParticleRangeId = SolverDatum.ParticleRangeId;
	check(ParticleRangeId != INDEX_NONE);

	// Update the animatable constraint parameters
	FClothConstraints& ClothConstraints = Solver->GetClothConstraints(ParticleRangeId);

	check(Cloth->Config);
	const FReal LocalSpaceScale = Solver->GetLocalSpaceScale();
	check(LocalSpaceScale > UE_SMALL_NUMBER);
	const FReal LocalSpaceScaleInv = 1. / LocalSpaceScale;
	const Softs::FSolverReal MeshScale = Cloth->Mesh->GetScale() * LocalSpaceScaleInv;
	const Softs::FSolverReal MaxDistancesScale = (Softs::FSolverReal)Cloth->MaxDistancesMultiplier;
	ClothConstraints.Update(Cloth->Config->GetProperties(SolverDatum.LODIndex), WeightMaps, VertexSets, FaceSets, FaceIntMaps, MeshScale, MaxDistancesScale, Solver->GetLocalSpaceRotation(), Cloth->ReferenceSpaceTransform.GetRotation());
}

void FClothingSimulationCloth::FLODData::Enable(FClothingSimulationSolver* Solver, bool bEnable) const
{
	check(Solver);
	const int32 ParticleRangeId = SolverData.FindChecked(Solver).ParticleRangeId;
	check(ParticleRangeId != INDEX_NONE);
	
	// Enable particles (and related constraints)
	Solver->EnableParticles(ParticleRangeId, bEnable);
}

void FClothingSimulationCloth::FLODData::ResetStartPose(FClothingSimulationSolver* Solver) const
{
	check(Solver);
	const int32 ParticleRangeId = SolverData.FindChecked(Solver).ParticleRangeId;
	check(ParticleRangeId != INDEX_NONE);

	Solver->ResetStartPose(ParticleRangeId, NumParticles);
}

void FClothingSimulationCloth::FLODData::UpdateNormals(FClothingSimulationSolver* Solver) const
{
	check(Solver);

	const FSolverData& SolverDatum = SolverData.FindChecked(Solver);
	const int32 ParticleRangeId = SolverDatum.ParticleRangeId;

	if (ParticleRangeId != INDEX_NONE)
	{
		TConstArrayView<Softs::FSolverVec3> Points = Solver->GetParticleXsView(ParticleRangeId);
		TArrayView<Softs::FSolverVec3> Normals = Solver->GetNormalsView(ParticleRangeId);
		TArray<Softs::FSolverVec3> FaceNormals;
		NoOffsetTriangleMesh.GetFaceNormals(FaceNormals, Points, /*ReturnEmptyOnError =*/ false);
		NoOffsetTriangleMesh.GetPointNormals(Normals, TConstArrayView<Softs::FSolverVec3>(FaceNormals), /*bUseGlobalArray =*/ false);
	}
}

FClothingSimulationCloth::FClothingSimulationCloth(
	FClothingSimulationConfig* InConfig,
	FClothingSimulationMesh* InMesh,
	TArray<FClothingSimulationCollider*>&& InColliders,
	uint32 InGroupId)
	: GroupId(InGroupId)
{
	SetConfig(InConfig);
	SetMesh(InMesh);
	SetColliders(MoveTemp(InColliders));
}

FClothingSimulationCloth::~FClothingSimulationCloth()
{
	// If the PropertyCollection is owned by this object, so does the current config object
	if (PropertyCollection.IsValid())
	{
		delete Config;
	}
}

void FClothingSimulationCloth::SetMesh(FClothingSimulationMesh* InMesh)
{
	Mesh = InMesh;
	
	// Reset LODs
	const int32 NumLODs = Mesh ? Mesh->GetNumLODs() : 0;
	LODData.Reset(NumLODs);
	for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
	{
		// Regenerate LOD weight maps lookup map
		const TArray<FName> WeightMapNames = Mesh->GetWeightMapNames(LODIndex);
		TMap<FString, TConstArrayView<FRealSingle>> WeightMaps;
		WeightMaps.Reserve(WeightMapNames.Num());

		const TArray<TConstArrayView<FRealSingle>> WeightMapArray = Mesh->GetWeightMaps(LODIndex);
		ensure(WeightMapArray.Num() == WeightMapNames.Num());

		for (int32 WeightMapIndex = 0; WeightMapIndex < WeightMapNames.Num(); ++WeightMapIndex)
		{
			WeightMaps.Add(WeightMapNames[WeightMapIndex].ToString(),
				WeightMapArray.IsValidIndex(WeightMapIndex) ?
					WeightMapArray[WeightMapIndex] :
					TConstArrayView<FRealSingle>());
		}

		TMap<FString, const TSet<int32>*> VertexSets = Mesh->GetVertexSets(LODIndex);
		TMap<FString, const TSet<int32>*> FaceSets = Mesh->GetFaceSets(LODIndex);
		TMap<FString, TConstArrayView<int32>> FaceIntMaps = Mesh->GetFaceIntMaps(LODIndex);

		const bool bUseGeodesicTethers = Config->GetProperties(LODIndex).GetValue<bool>(TEXT("UseGeodesicTethers"), ClothingSimulationClothDefault::bUseGeodesicTethers);

		// Add LOD data
		LODData.Add(MakeUnique<FLODData>(
			Mesh->GetNumPoints(LODIndex),
			Mesh->GetIndices(LODIndex),
			Mesh->GetPatternPositions(LODIndex),
			Mesh->GetPatternIndices(LODIndex),
			Mesh->GetPatternToWeldedIndices(LODIndex),
			MoveTemp(WeightMaps),
			MoveTemp(VertexSets),
			MoveTemp(FaceSets),
			MoveTemp(FaceIntMaps),
			Mesh->GetTethers(LODIndex, bUseGeodesicTethers)));
	}

	// Iterate all known solvers
	TArray<FClothingSimulationSolver*> Solvers;
	LODIndices.GetKeys(Solvers);
	for (FClothingSimulationSolver* const Solver : Solvers)
	{
		// Refresh this cloth to recreate particles
		Solver->RefreshCloth(this);
	}
}

void FClothingSimulationCloth::SetConfig(FClothingSimulationConfig* InConfig)
{
	// If the PropertyCollection is owned by this object, so does the current config object
	if (PropertyCollection.IsValid())
	{
		delete Config;
		PropertyCollection.Reset();
	}

	if (InConfig)
	{
		Config = InConfig;
	}
	else
	{
		// Create a default empty config object for coherence
		PropertyCollection = MakeShared<FManagedArrayCollection>();
		Config = new FClothingSimulationConfig({ PropertyCollection });
	}
}

void FClothingSimulationCloth::SetColliders(TArray<FClothingSimulationCollider*>&& InColliders)
{
	// Empty the collider list, but keep the pointers around for the removal operation below
	const TArray<FClothingSimulationCollider*> TempColliders = MoveTemp(Colliders);

	// Replace with the new colliders
	Colliders = InColliders;

	// Iterate all known solvers
	TArray<FClothingSimulationSolver*> Solvers;
	LODIndices.GetKeys(Solvers);
	for (FClothingSimulationSolver* const Solver : Solvers)
	{
		// Remove any held collider data related to this cloth simulation
		for (FClothingSimulationCollider* const Collider : TempColliders)
		{
			Collider->Remove(Solver, this);
		}

		// Refresh this cloth to recreate collision particles
		Solver->RefreshCloth(this);
	}
}

void FClothingSimulationCloth::AddCollider(FClothingSimulationCollider* InCollider)
{
	check(InCollider);

	if (Colliders.Find(InCollider) != INDEX_NONE)
	{
		return;
	}

	// Add the collider to the solver update array
	Colliders.Emplace(InCollider);

	// Iterate all known solvers
	TArray<FClothingSimulationSolver*> Solvers;
	LODIndices.GetKeys(Solvers);
	for (FClothingSimulationSolver* const Solver : Solvers)
	{
		// Refresh this cloth to recreate collision particles
		Solver->RefreshCloth(this);
	}
}

void FClothingSimulationCloth::RemoveCollider(FClothingSimulationCollider* InCollider)
{
	if (Colliders.Find(InCollider) == INDEX_NONE)
	{
		return;
	}

	// Remove collider from array
	Colliders.RemoveSwap(InCollider);

	// Iterate all known solvers
	TArray<FClothingSimulationSolver*> Solvers;
	LODIndices.GetKeys(Solvers);
	for (FClothingSimulationSolver* const Solver : Solvers)
	{
		// Remove any held collider data related to this cloth simulation
		InCollider->Remove(Solver, this);

		// Refresh this cloth to recreate collision particles
		Solver->RefreshCloth(this);
	}
}

void FClothingSimulationCloth::RemoveColliders()
{
	// Empty the collider list, but keep the pointers around for the removal operation below
	const TArray<FClothingSimulationCollider*> TempColliders = MoveTemp(Colliders);

	// Iterate all known solvers
	TArray<FClothingSimulationSolver*> Solvers;
	LODIndices.GetKeys(Solvers);
	for (FClothingSimulationSolver* const Solver : Solvers)
	{
		// Remove any held collider data related to this cloth simulation
		for (FClothingSimulationCollider* const Collider : TempColliders)
		{
			Collider->Remove(Solver, this);
		}

		// Refresh this cloth to recreate collision particles
		Solver->RefreshCloth(this);
	}
}

void FClothingSimulationCloth::Add(FClothingSimulationSolver* Solver)
{
	check(Solver);

	// Can't add a cloth twice to the same solver
	check(!LODIndices.Find(Solver));

	// Initialize LODIndex
	int32& LODIndex = LODIndices.Add(Solver);
	LODIndex = INDEX_NONE;

	// Add all particles first and in reverse order. This is necessary so that any multires coarse lods soft bodies are added first, and all particle offsets are setup
	// when adding the LOD constraints.
	for (int32 Index = LODData.Num() - 1; Index >= 0; --Index)
	{
		LODData[Index]->AddParticles(Solver, this, Index);
	}
	// Now add the LODs themselves. These need to go in normal order since the coarse lod needs the fine lod constraints.
	for (int32 Index = 0; Index < LODData.Num(); ++Index)
	{
		LODData[Index]->Add(Solver, this, Index);
	}

	// Add Colliders
	for (FClothingSimulationCollider* Collider : Colliders)
	{
		Collider->Add(Solver, this);
	}
}

void FClothingSimulationCloth::Remove(FClothingSimulationSolver* Solver)
{
	// Remove Colliders
	for (FClothingSimulationCollider* Collider : Colliders)
	{
		Collider->Remove(Solver, this);
	}

	// Remove solver from maps
	LODIndices.Remove(Solver);
	for (TUniquePtr<FLODData>& LODDatum: LODData)
	{
		LODDatum->Remove(Solver);
	}
}

int32 FClothingSimulationCloth::GetNumParticles(int32 InLODIndex) const
{
	return LODData.IsValidIndex(InLODIndex) ? LODData[InLODIndex]->NumParticles : 0;
}

int32 FClothingSimulationCloth::GetParticleRangeId(const FClothingSimulationSolver* Solver, int32 InLODIndex) const
{
	return LODData.IsValidIndex(InLODIndex) ? LODData[InLODIndex]->SolverData.FindChecked(Solver).ParticleRangeId : 0;
}

TVec3<FRealSingle> FClothingSimulationCloth::GetGravity(const FClothingSimulationSolver* Solver) const
{
	check(Solver);
	if (!Solver->IsLegacySolver())
	{
		const int32 ParticleRangeId = GetParticleRangeId(Solver);
		if (const Softs::FExternalForces* const ExternalForces = Solver->GetClothConstraints(ParticleRangeId).GetExternalForces().Get())
		{
			return ExternalForces->GetGravity();
		}
		else
		{
			return TVec3<FRealSingle>(0.f, 0.f, Softs::FExternalForces::DefaultGravityZOverride);
		}
	}
	else
	{
		check(Config);
		const Softs::FCollectionPropertyFacade& ConfigProperties = Config->GetProperties(GetLODIndex(Solver));

		const bool bUseGravityOverride = ConfigProperties.GetValue<bool>(TEXT("UseGravityOverride"));
		const TVec3<FRealSingle> GravityOverride = (TVec3<FRealSingle>)ConfigProperties.GetValue<FVector3f>(TEXT("GravityOverride"), FVector3f(0.f, 0.f, ClothingSimulationClothDefault::GravityZOverride));
		const FRealSingle GravityScale = (FRealSingle)ConfigProperties.GetValue<float>(TEXT("GravityScale"), 1.f);
		const FRealSingle GravityMultiplier = (FRealSingle)ClothingSimulationClothConsoleVariables::CVarGravityMultiplier.GetValueOnAnyThread();

		return (Solver->IsClothGravityOverrideEnabled() && bUseGravityOverride ? GravityOverride : Solver->GetGravity() * GravityScale) * GravityMultiplier;
	}
}

FAABB3 FClothingSimulationCloth::CalculateBoundingBox(const FClothingSimulationSolver* Solver) const
{
	check(Solver);

	// Calculate local space bounding box
	Softs::FSolverAABB3 BoundingBox = Softs::FSolverAABB3::EmptyAABB();

	const TConstArrayView<Softs::FSolverVec3> ParticlePositions = GetParticlePositions(Solver);
	for (const Softs::FSolverVec3& ParticlePosition : ParticlePositions)
	{
		BoundingBox.GrowToInclude(ParticlePosition);
	}

	// Return world space bounding box
	return FAABB3(BoundingBox).TransformedAABB(FTransform(FRotation3::Identity, Solver->GetLocalSpaceLocation(), FVector(Solver->GetLocalSpaceScale())));
}

int32 FClothingSimulationCloth::GetParticleRangeId(const FClothingSimulationSolver* Solver) const
{
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	return LODData.IsValidIndex(LODIndex) ? GetParticleRangeId(Solver, LODIndex) : INDEX_NONE;
}

int32 FClothingSimulationCloth::GetNumParticles(const FClothingSimulationSolver* Solver) const
{
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	return LODData.IsValidIndex(LODIndex) ? GetNumParticles(LODIndex) : 0;
}

const FTriangleMesh& FClothingSimulationCloth::GetTriangleMesh(const FClothingSimulationSolver* Solver) const
{
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	static const FTriangleMesh EmptyTriangleMesh;

	return LODData.IsValidIndex(LODIndex) ? (Solver->IsLegacySolver() ? LODData[LODIndex]->SolverData.FindChecked(Solver).OffsetTriangleMesh: LODData[LODIndex]->NoOffsetTriangleMesh) : EmptyTriangleMesh;
}

TConstArrayView<FRealSingle> FClothingSimulationCloth::GetWeightMapByName(const FClothingSimulationSolver* Solver, const FString& Name) const
{
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	return LODData.IsValidIndex(LODIndex) ? LODData[LODIndex]->WeightMaps.FindRef(Name) : TConstArrayView<FRealSingle>();
}

TConstArrayView<FRealSingle> FClothingSimulationCloth::GetWeightMapByProperty(const FClothingSimulationSolver* Solver, const FString& Property) const
{
	check(Config);
	const FString PropertyString = Config->GetProperties(GetLODIndex(Solver)).GetStringValue(Property);
	return GetWeightMapByName(Solver, PropertyString);
}

TSet<FString> FClothingSimulationCloth::GetAllWeightMapNames() const
{
	TSet<FString> Names;
	for (const TUniquePtr<FLODData>& Data : LODData)
	{
		TSet<FString> LODNames;
		Data->WeightMaps.GetKeys(LODNames);
		Names.Append(MoveTemp(LODNames));
	}
	return Names;
}

TConstArrayView<int32> FClothingSimulationCloth::GetFaceIntMapByName(const FClothingSimulationSolver* Solver, const FString& Name) const
{
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	return LODData.IsValidIndex(LODIndex) ? LODData[LODIndex]->FaceIntMaps.FindRef(Name) : TConstArrayView<int32>();
}

TConstArrayView<int32> FClothingSimulationCloth::GetFaceIntMapByProperty(const FClothingSimulationSolver* Solver, const FString& Property) const
{
	check(Config);
	const FString PropertyString = Config->GetProperties(GetLODIndex(Solver)).GetStringValue(Property);
	return GetFaceIntMapByName(Solver, PropertyString);
}

const TArray<TConstArrayView<TTuple<int32, int32, float>>>& FClothingSimulationCloth::GetTethers(const FClothingSimulationSolver* Solver) const
{
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	static const TArray<TConstArrayView<TTuple<int32, int32, float>>> EmptyTethers;
	return LODData.IsValidIndex(LODIndex) ? LODData[LODIndex]->Tethers : EmptyTethers;
}

int32 FClothingSimulationCloth::GetReferenceBoneIndex() const
{
	return Mesh ? Mesh->GetReferenceBoneIndex() : INDEX_NONE;
}

void FClothingSimulationCloth::PreUpdate(FClothingSimulationSolver* Solver)
{
	check(Solver);

	// Exit if the input mesh is missing
	if (!Mesh)
	{
		return;
	}

	// Update Cloth Colliders
	{
		SCOPE_CYCLE_COUNTER(STAT_ClothUpdateCollisions);

		for (FClothingSimulationCollider* Collider : Colliders)
		{
			Collider->PreUpdate(Solver, this);
		}
	}
}

void FClothingSimulationCloth::Update(FClothingSimulationSolver* Solver)
{
	check(Solver);

	// Exit if the input mesh is missing
	if (!Mesh)
	{
		return;
	}

	// Retrieve LOD Index, either from the override, or from the mesh input
	int32& LODIndex = LODIndices.FindChecked(Solver);  // Must be added to solver first

	const int32 PrevLODIndex = LODIndex;
	LODIndex = bUseLODIndexOverride && LODData.IsValidIndex(LODIndexOverride) ? LODIndexOverride : Mesh->GetLODIndex();

	// Update reference space transform from the mesh's reference bone transform  TODO: Add override in the style of LODIndexOverride
	const FRigidTransform3 OldReferenceSpaceTransform = ReferenceSpaceTransform;
	ReferenceSpaceTransform = Mesh->GetReferenceBoneTransform();
	ReferenceSpaceTransform.SetScale3D(FVec3(1.f));

	// Update Cloth Colliders
	{
		SCOPE_CYCLE_COUNTER(STAT_ClothUpdateCollisions);

		for (FClothingSimulationCollider* Collider : Colliders)
		{
			Collider->Update(Solver, this);
		}
	}

	// Update the source mesh skinned positions
	const int32 PrevParticleRangeId = GetParticleRangeId(Solver, PrevLODIndex);
	const int32 ParticleRangeId = GetParticleRangeId(Solver, LODIndex);
	if (PrevParticleRangeId == INDEX_NONE || ParticleRangeId == INDEX_NONE)
	{
		return;
	}

	Mesh->Update(Solver, PrevLODIndex, LODIndex, PrevParticleRangeId, ParticleRangeId);

	// Retrieve config
	check(Config);
	const Softs::FCollectionPropertyFacade& ConfigProperties = Config->GetProperties(LODIndex);

	const int32 CoarseLODIndex = LODIndex != INDEX_NONE ? LODData[LODIndex]->SolverData.FindChecked(Solver).MultiResCoarseLODIndex : INDEX_NONE;
	const int32 CoarseParticleRangeId = CoarseLODIndex != INDEX_NONE ? GetParticleRangeId(Solver, CoarseLODIndex) : INDEX_NONE;
	if (CoarseLODIndex != INDEX_NONE)
	{
		LODData[CoarseLODIndex]->Enable(Solver, true);
		// TODO: interpolate/ reset when LOD switching to enable multires
		Mesh->Update(Solver, CoarseLODIndex, CoarseLODIndex, CoarseParticleRangeId, CoarseParticleRangeId);
	}

	// LOD Switching
	if (LODIndex != PrevLODIndex)
	{
		if (PrevLODIndex != INDEX_NONE)
		{
			if (PrevLODIndex != CoarseLODIndex)
			{
				// Disable previous LOD's particles
				LODData[PrevLODIndex]->Enable(Solver, false);
			}
			const int32 PrevCoarseLODIndex = LODData[PrevLODIndex]->SolverData.FindChecked(Solver).MultiResCoarseLODIndex;
			if (PrevCoarseLODIndex != INDEX_NONE && PrevCoarseLODIndex != CoarseLODIndex && PrevCoarseLODIndex != LODIndex)
			{
				// Disable previous coarse LOD's particles
				LODData[PrevCoarseLODIndex]->Enable(Solver, false);
			}
		}
		if (LODIndex != INDEX_NONE)
		{
			// Enable new LOD's particles
			LODData[LODIndex]->Enable(Solver, true);
			NumActiveKinematicParticles = LODData[LODIndex]->NumKinematicParticles;
			NumActiveDynamicParticles = LODData[LODIndex]->NumDynamicParticles;

			// Wrap new LOD based on previous LOD if possible (can only do 1 level LOD at a time, and if previous LOD exists)
			bNeedsReset = bNeedsReset || !Mesh->WrapDeformLOD(
				PrevLODIndex,
				LODIndex,
				Solver->GetNormals(PrevParticleRangeId),
				Solver->GetParticlePandInvMs(PrevParticleRangeId),
				Solver->GetParticleVs(PrevParticleRangeId),
				Solver->GetParticlePandInvMs(ParticleRangeId),
				Solver->GetParticleXs(ParticleRangeId),
				Solver->GetParticleVs(ParticleRangeId));

			if(Solver->IsLegacySolver())
			{
				// Update the wind velocity field for the new LOD mesh
				Solver->SetWindAndPressureGeometry(GroupId, GetTriangleMesh(Solver), ConfigProperties, LODData[LODIndex]->WeightMaps);
			}
		}
		else
		{
			NumActiveKinematicParticles = 0;
			NumActiveDynamicParticles = 0;
		}
	}

	// Update Cloth group parameters  TODO: Cloth groups should exist as their own node object so that they can be used by several cloth objects
	if (LODIndex != INDEX_NONE)
	{
		// TODO: Move all groupID updates out of the cloth update to allow to use of the same GroupId with different cloths

		// Set the reference input velocity and deal with teleport & reset; external forces depends on these values, so they must be initialized before then
		EChaosSoftsSimulationSpace VelocityScaleSpace = ClothingSimulationClothDefault::VelocityScaleSpace;
		FVec3f OutLinearVelocityScale;
		FRealSingle OutAngularVelocityScale;
		FRealSingle OutMaxVelocityScale;
		bool bDisableFictitiousForces = false;
		FVec3f MaxLinearVelocity(ClothingSimulationClothDefault::MaxVelocity);
		FVec3f MaxLinearAcceleration(ClothingSimulationClothDefault::MaxAcceleration);
		FRealSingle MaxAngularVelocity = ClothingSimulationClothDefault::MaxVelocity;
		FRealSingle MaxAngularAcceleration = ClothingSimulationClothDefault::MaxAcceleration;
		if (bNeedsReset)
		{
			// Make sure not to do any pre-sim transform just after a reset
			OutLinearVelocityScale = FVec3f(1.f);
			OutAngularVelocityScale = 1.f;
			OutMaxVelocityScale = 1.f;
			ReferenceSpaceAngularVelocity = FVec3(0.);
			ReferenceSpaceVelocity = FVec3(0.);
			bDisableFictitiousForces = true; // It doesn't actually matter what value we set here since AngularVelocityScale == 1 means fictitious forces will be 0.

			// Reset to start pose
			LODData[LODIndex]->ResetStartPose(Solver);
			for (FClothingSimulationCollider* Collider : Colliders)
			{
				Collider->ResetStartPose(Solver, this);
			}

			if (CoarseLODIndex != INDEX_NONE)
			{
				LODData[CoarseLODIndex]->ResetStartPose(Solver);
			}
			UE_LOG(LogChaosCloth, Verbose, TEXT("Cloth in group Id %d Needs reset."), GroupId);
		}
		else if (bNeedsTeleport)
		{
			// Remove all impulse velocity from the last frame
			OutLinearVelocityScale = FVec3f(0.f);
			OutAngularVelocityScale = 0.f;
			OutMaxVelocityScale = 1.f;
			ReferenceSpaceAngularVelocity = FVec3(0.);
			ReferenceSpaceVelocity = FVec3(0.);
			bDisableFictitiousForces = true; // Disable fictitious forces. Otherwise they will be applied since AngularVelocityScale < 1.
			UE_LOG(LogChaosCloth, Verbose, TEXT("Cloth in group Id %d Needs teleport."), GroupId);
		}
		else
		{
			// Use the cloth config parameters
			VelocityScaleSpace = (EChaosSoftsSimulationSpace)ConfigProperties.GetValue<int32>(TEXT("VelocityScaleSpace"), (int32)ClothingSimulationClothDefault::VelocityScaleSpace);
			OutLinearVelocityScale = ConfigProperties.GetValue<FVector3f>(TEXT("LinearVelocityScale"), FVector3f(ClothingSimulationClothDefault::VelocityScale));
			OutAngularVelocityScale = ConfigProperties.GetValue<float>(TEXT("AngularVelocityScale"), ClothingSimulationClothDefault::VelocityScale);
			OutMaxVelocityScale = ConfigProperties.GetValue<float>(TEXT("MaxVelocityScale"), ClothingSimulationClothDefault::MaxVelocityScale);
			MaxLinearVelocity = ConfigProperties.GetValue<FVector3f>(TEXT("MaxLinearVelocity"), MaxLinearVelocity);
			MaxLinearAcceleration = ConfigProperties.GetValue<FVector3f>(TEXT("MaxLinearAcceleration"), MaxLinearAcceleration);
			MaxAngularVelocity = ConfigProperties.GetValue<float>(TEXT("MaxAngularVelocity"), MaxAngularVelocity);
			MaxAngularAcceleration = ConfigProperties.GetValue<float>(TEXT("MaxAngularAcceleration"), MaxAngularAcceleration);
		}

		// NOTE: Force-based solver doesn't actually use FictitiousAngularScale here. It gets it from the property collection directly.
		const FRealSingle FictitiousAngularScale = ConfigProperties.GetValue<float>(TEXT("FictitiousAngularScale"), ClothingSimulationClothDefault::FictitiousAngularScale);
		Solver->SetReferenceVelocityScale(
			GroupId,
			OldReferenceSpaceTransform,
			ReferenceSpaceTransform,
			ReferenceSpaceVelocity,
			ReferenceSpaceAngularVelocity,
			VelocityScaleSpace,
			OutLinearVelocityScale,
			MaxLinearVelocity,
			MaxLinearAcceleration,
			OutAngularVelocityScale,
			MaxAngularVelocity,
			MaxAngularAcceleration,
			FictitiousAngularScale,
			OutMaxVelocityScale,
			bDisableFictitiousForces);
		if (!Solver->IsLegacySolver())
		{
			Solver->SetProperties(ParticleRangeId, ConfigProperties, LODData[LODIndex]->WeightMaps);
			if (CoarseLODIndex != INDEX_NONE)
			{
				Solver->SetProperties(CoarseParticleRangeId, Config->GetProperties(CoarseLODIndex), LODData[CoarseLODIndex]->WeightMaps);
			}
		}
		else
		{
			// Update gravity
			// This code relies on the solver gravity property being already set.
			// In order to use a cloth gravity override, it must first be enabled by the solver so that an override at solver level can still take precedence if needed.
			// In all cases apart from when the cloth override is used, the gravity scale must be combined to the solver gravity value.
			Solver->SetGravity(GroupId, GetGravity(Solver));

			// External forces (legacy wind+field)
			const bool bUsePointBasedWindModel = ConfigProperties.GetValue<bool>(TEXT("UsePointBasedWindModel"));
			Solver->AddExternalForces(GroupId, bUsePointBasedWindModel);

			const bool bPointBasedWindDisablesAccurateWind = ClothingSimulationClothConsoleVariables::CVarLegacyDisablesAccurateWind.GetValueOnAnyThread();
			const bool bEnableAerodynamics = !(bUsePointBasedWindModel && bPointBasedWindDisablesAccurateWind);
			Solver->SetWindAndPressureProperties(GroupId, ConfigProperties, LODData[LODIndex]->WeightMaps, bEnableAerodynamics);

			constexpr float WorldScale = 100.f;  // VelocityField wind is in m/s in the config (same as the wind unit), but cm/s in the solver  TODO: Cleanup the Solver SetWindVelocity functions to be consistent with the unit
			const FVec3f WindVelocity = ConfigProperties.GetValue<FVector3f>(TEXT("WindVelocity")) * WorldScale;
			Solver->SetWindVelocity(GroupId, WindVelocity + Solver->GetWindVelocity());

			// Update general solver properties
			const FReal LocalSpaceScale = Solver->GetLocalSpaceScale();
			check(LocalSpaceScale > UE_SMALL_NUMBER);
			const FReal LocalSpaceScaleInv = 1. / LocalSpaceScale;
			const Softs::FSolverReal MeshScale = Mesh->GetScale() * LocalSpaceScaleInv;

			const FRealSingle DampingCoefficient = ConfigProperties.GetValue<float>(TEXT("DampingCoefficient"), ClothingSimulationClothDefault::DampingCoefficient);
			const FRealSingle LocalDampingCoefficient = ConfigProperties.GetValue<float>(TEXT("LocalDampingCoefficient"));
			const FRealSingle CollisionThickness = ConfigProperties.GetValue<float>(TEXT("CollisionThickness"), ClothingSimulationClothDefault::CollisionThickness);
			const FRealSingle FrictionCoefficient = ConfigProperties.GetValue<float>(TEXT("FrictionCoefficient"), ClothingSimulationClothDefault::FrictionCoefficient);
			Solver->SetProperties(GroupId, DampingCoefficient, LocalDampingCoefficient, CollisionThickness * MeshScale, FrictionCoefficient);

			// Update use of continuous collision detection
			const bool bUseCCD = ConfigProperties.GetValue<bool>(TEXT("UseCCD"));
			Solver->SetUseCCD(GroupId, bUseCCD);
		}

		// This will be updated below if single legacy lod
		if (!Config->IsLegacySingleLOD())
		{
			LODData[LODIndex]->Update(Solver, this);
			if (CoarseLODIndex != INDEX_NONE)
			{
				LODData[CoarseLODIndex]->Update(Solver, this);
			}
		}
	}

	// Update all LODs dirty properties, since it is easier done than re-updating all properties when switching LODs
	if (Config->IsLegacySingleLOD())
	{
		for (TUniquePtr<FLODData>& LODDatum : LODData)
		{
			LODDatum->Update(Solver, this);
		}
	}

	// Reset trigger flags
	bNeedsTeleport = false;
	bNeedsReset = false;
}

void FClothingSimulationCloth::PostUpdate(FClothingSimulationSolver* Solver)
{
	check(Solver);

	const int32 LODIndex = LODIndices.FindChecked(Solver);
	if (LODIndex != INDEX_NONE)
	{
		// Update normals
		LODData[LODIndex]->UpdateNormals(Solver);
	}
}

void FClothingSimulationCloth::UpdateFromCache(const FClothingSimulationCacheData& CacheData)
{
	if (const FTransform* CachedReferenceSpaceTransform = CacheData.CachedReferenceSpaceTransforms.Find(GetGroupId()))
	{
		ReferenceSpaceTransform = *CachedReferenceSpaceTransform;
		ReferenceSpaceTransform.SetScale3D(FVec3(1.f));
	}
}

TConstArrayView<Softs::FSolverVec3> FClothingSimulationCloth::GetAnimationPositions(const FClothingSimulationSolver* Solver) const
{
	check(Solver);
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	check(GetParticleRangeId(Solver, LODIndex) != INDEX_NONE);
	return TConstArrayView<Softs::FSolverVec3>(Solver->GetAnimationPositions(GetParticleRangeId(Solver, LODIndex)), GetNumParticles(LODIndex));
}

TConstArrayView<Softs::FSolverVec3> FClothingSimulationCloth::GetOldAnimationPositions(const FClothingSimulationSolver* Solver) const
{
	check(Solver);
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	check(GetParticleRangeId(Solver, LODIndex) != INDEX_NONE);
	return TConstArrayView<Softs::FSolverVec3>(Solver->GetOldAnimationPositions(GetParticleRangeId(Solver, LODIndex)), GetNumParticles(LODIndex));
}

TConstArrayView<Softs::FSolverVec3> FClothingSimulationCloth::GetAnimationVelocities(const FClothingSimulationSolver* Solver) const
{
	check(Solver);
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	check(GetParticleRangeId(Solver, LODIndex) != INDEX_NONE);
	return TConstArrayView<Softs::FSolverVec3>(Solver->GetAnimationVelocities(GetParticleRangeId(Solver, LODIndex)), GetNumParticles(LODIndex));
}

TConstArrayView<Softs::FSolverVec3> FClothingSimulationCloth::GetAnimationNormals(const FClothingSimulationSolver* Solver) const
{
	check(Solver);
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	check(GetParticleRangeId(Solver, LODIndex) != INDEX_NONE);
	return TConstArrayView<Softs::FSolverVec3>(Solver->GetAnimationNormals(GetParticleRangeId(Solver, LODIndex)), GetNumParticles(LODIndex));
}

TConstArrayView<Softs::FSolverVec3> FClothingSimulationCloth::GetParticlePositions(const FClothingSimulationSolver* Solver) const
{
	check(Solver);
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	check(GetParticleRangeId(Solver, LODIndex) != INDEX_NONE);
	return TConstArrayView<Softs::FSolverVec3>(Solver->GetParticleXs(GetParticleRangeId(Solver, LODIndex)), GetNumParticles(LODIndex));
}

TConstArrayView<Softs::FSolverVec3> FClothingSimulationCloth::GetParticleNormals(const FClothingSimulationSolver* Solver) const
{
	check(Solver);
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	check(GetParticleRangeId(Solver, LODIndex) != INDEX_NONE);
	return TConstArrayView<Softs::FSolverVec3>(Solver->GetNormals(GetParticleRangeId(Solver, LODIndex)), GetNumParticles(LODIndex));
}

TConstArrayView<Softs::FSolverVec3> FClothingSimulationCloth::GetParticleVelocities(const FClothingSimulationSolver* Solver) const
{
	check(Solver);
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	check(GetParticleRangeId(Solver, LODIndex) != INDEX_NONE);
	return TConstArrayView<Softs::FSolverVec3>(Solver->GetParticleVs(GetParticleRangeId(Solver, LODIndex)), GetNumParticles(LODIndex));
}

TConstArrayView<Softs::FSolverReal> FClothingSimulationCloth::GetParticleInvMasses(const FClothingSimulationSolver* Solver) const
{
	check(Solver);
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	check(GetParticleRangeId(Solver, LODIndex) != INDEX_NONE);
	return TConstArrayView<Softs::FSolverReal>(Solver->GetParticleInvMasses(GetParticleRangeId(Solver, LODIndex)), GetNumParticles(LODIndex));
}
}  // End namespace Chaos
