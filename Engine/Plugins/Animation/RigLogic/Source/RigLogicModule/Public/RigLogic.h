// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "CoreMinimal.h"
#include "Math/Vector.h"
#include "UObject/NoExportTypes.h"
#include "UObject/ObjectMacros.h"

#include "FMemoryResource.h"

#include "RigLogic.generated.h"

class FRigInstance;
class IDNAReader;

namespace rl4
{

class RigLogic;

}  // namespace rl4

UENUM(BlueprintType)
enum class ERigLogicCalculationType: uint8
{
	Scalar,
	SSE,
	AVX,
	NEON,
	AnyVector
};

UENUM(BlueprintType)
enum class ERigLogicTranslationType : uint8 {
	None,
	Vector = 3
};

UENUM(BlueprintType)
enum class ERigLogicRotationType : uint8 {
	None,
	EulerAngles = 3,
	Quaternions = 4
};

UENUM(BlueprintType)
enum class ERigLogicRotationOrder : uint8 {
	XYZ,
	XZY,
	YXZ,
	YZX,
	ZXY,
	ZYX
};

UENUM(BlueprintType)
enum class ERigLogicScaleType : uint8 {
	None,
	Vector = 3
};

USTRUCT(BlueprintType)
struct FRigLogicConfiguration
{
	GENERATED_BODY()

	FRigLogicConfiguration() :
		CalculationType(ERigLogicCalculationType::AnyVector),
		LoadJoints(true),
		LoadBlendShapes(true),
		LoadAnimatedMaps(true),
		LoadMachineLearnedBehavior(true),
		LoadRBFBehavior(true),
		LoadTwistSwingBehavior(true),
		TranslationType(ERigLogicTranslationType::Vector),
		RotationType(ERigLogicRotationType::Quaternions),
		RotationOrder(ERigLogicRotationOrder::ZYX),
		ScaleType(ERigLogicScaleType::Vector)
	{
	}

	FRigLogicConfiguration(ERigLogicCalculationType CalculationType,
							bool LoadJoints,
							bool LoadBlendShapes,
							bool LoadAnimatedMaps,
							bool LoadMachineLearnedBehavior,
							bool LoadRBFBehavior,
							bool LoadTwistSwingBehavior,
							ERigLogicTranslationType TranslationType,
							ERigLogicRotationType RotationType,
							ERigLogicRotationOrder RotationOrder,
							ERigLogicScaleType ScaleType) :
		CalculationType(CalculationType),
		LoadJoints(LoadJoints),
		LoadBlendShapes(LoadBlendShapes),
		LoadAnimatedMaps(LoadAnimatedMaps),
		LoadMachineLearnedBehavior(LoadMachineLearnedBehavior),
		LoadRBFBehavior(LoadRBFBehavior),
		LoadTwistSwingBehavior(LoadTwistSwingBehavior),
		TranslationType(TranslationType),
		RotationType(RotationType),
		RotationOrder(RotationOrder),
		ScaleType(ScaleType
	)
	{
	}

	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	ERigLogicCalculationType CalculationType;

	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	bool LoadJoints;

	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	bool LoadBlendShapes;

	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	bool LoadAnimatedMaps;

	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	bool LoadMachineLearnedBehavior;
	
	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	bool LoadRBFBehavior;
	
	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	bool LoadTwistSwingBehavior;
	
	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	ERigLogicTranslationType TranslationType;
	
	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	ERigLogicRotationType RotationType;
	
	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	ERigLogicRotationOrder RotationOrder;

	UPROPERTY(BlueprintReadOnly, Category = "RigLogic")
	ERigLogicScaleType ScaleType;
};

class RIGLOGICMODULE_API FRigLogic
{
public:
	explicit FRigLogic(const IDNAReader* Reader, FRigLogicConfiguration Config = FRigLogicConfiguration());
	~FRigLogic();

	FRigLogic(const FRigLogic&) = delete;
	FRigLogic& operator=(const FRigLogic&) = delete;

	FRigLogic(FRigLogic&&) = default;
	FRigLogic& operator=(FRigLogic&&) = default;

	uint16 GetLODCount() const;
	TArrayView<const float> GetNeutralJointValues() const;
	TArrayView<const uint16> GetJointVariableAttributeIndices(uint16 LOD) const;
	uint16 GetJointGroupCount() const;
	uint16 GetNeuralNetworkCount() const;
	uint16 GetRBFSolverCount() const;
	uint16 GetMeshCount() const;
	uint16 GetMeshRegionCount(uint16 MeshIndex) const;
	TArrayView<const uint16> GetNeuralNetworkIndices(uint16 MeshIndex, uint16 RegionIndex) const;

	void MapGUIToRawControls(FRigInstance* Instance) const;
	void MapRawToGUIControls(FRigInstance* Instance) const;
	void CalculateControls(FRigInstance* Instance) const;
	void CalculateMachineLearnedBehaviorControls(FRigInstance* Instance) const;
	void CalculateMachineLearnedBehaviorControls(FRigInstance* Instance, uint16 NeuralNetIndex) const;
	void CalculateRBFControls(FRigInstance* Instance) const;
	void CalculateRBFControls(FRigInstance* Instance, uint16 SolverIndex) const;
	void CalculateJoints(FRigInstance* Instance) const;
	void CalculateJoints(FRigInstance* Instance, uint16 JointGroupIndex) const;
	void CalculateBlendShapes(FRigInstance* Instance) const;
	void CalculateAnimatedMaps(FRigInstance* Instance) const;
	void Calculate(FRigInstance* Instance) const;

private:
	friend FRigInstance;
	rl4::RigLogic* Unwrap() const;

private:
	TSharedPtr<FMemoryResource> MemoryResource;

	struct FRigLogicDeleter
	{
		void operator()(rl4::RigLogic* Pointer);
	};
	TUniquePtr<rl4::RigLogic, FRigLogicDeleter> RigLogic;
};

