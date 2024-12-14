// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigLogic.h"

#include "DNAReader.h"
#include "FMemoryResource.h"
#include "RigInstance.h"

#include "riglogic/RigLogic.h"

void FRigLogic::FRigLogicDeleter::operator()(rl4::RigLogic* Pointer)
{
	rl4::RigLogic::destroy(Pointer);
}

static rl4::Configuration AdaptRigLogicConfig(const FRigLogicConfiguration& Config)
{
	rl4::Configuration Copy = {};
	Copy.calculationType = static_cast<rl4::CalculationType>(Config.CalculationType);
	Copy.loadJoints = Config.LoadJoints;
	Copy.loadBlendShapes = Config.LoadBlendShapes;
	Copy.loadAnimatedMaps = Config.LoadAnimatedMaps;
	Copy.loadMachineLearnedBehavior = Config.LoadMachineLearnedBehavior;
	Copy.loadRBFBehavior = Config.LoadRBFBehavior;
	Copy.loadTwistSwingBehavior = Config.LoadTwistSwingBehavior;
	Copy.translationType = static_cast<rl4::TranslationType>(Config.TranslationType);
	Copy.rotationType = static_cast<rl4::RotationType>(Config.RotationType);
	Copy.rotationOrder = static_cast<rl4::RotationOrder>(Config.RotationOrder);
	Copy.scaleType = static_cast<rl4::ScaleType>(Config.ScaleType);
	return Copy;
}

FRigLogic::FRigLogic(const IDNAReader* Reader, FRigLogicConfiguration Config) :
	MemoryResource{FMemoryResource::SharedInstance()},
	RigLogic{rl4::RigLogic::create(Reader->Unwrap(), AdaptRigLogicConfig(Config), FMemoryResource::Instance())}
{
}

FRigLogic::~FRigLogic() = default;

uint16 FRigLogic::GetLODCount() const
{
	return RigLogic->getLODCount();
}

TArrayView<const float> FRigLogic::GetNeutralJointValues() const
{
	rl4::ConstArrayView<float> Values = RigLogic->getNeutralJointValues();
	return TArrayView<const float>{Values.data(), static_cast<int32>(Values.size())};
}

TArrayView<const uint16> FRigLogic::GetJointVariableAttributeIndices(uint16 LOD) const
{
	rl4::ConstArrayView<uint16> Indices = RigLogic->getJointVariableAttributeIndices(LOD);
	return TArrayView<const uint16>{Indices.data(), static_cast<int32>(Indices.size())};
}

uint16 FRigLogic::GetJointGroupCount() const
{
	return RigLogic->getJointGroupCount();
}

uint16 FRigLogic::GetNeuralNetworkCount() const
{
	return RigLogic->getNeuralNetworkCount();
}

uint16 FRigLogic::GetRBFSolverCount() const
{
	return RigLogic->getRBFSolverCount();
}

uint16 FRigLogic::GetMeshCount() const
{
	return RigLogic->getMeshCount();
}

uint16 FRigLogic::GetMeshRegionCount(uint16 MeshIndex) const
{
	return RigLogic->getMeshRegionCount(MeshIndex);
}

TArrayView<const uint16> FRigLogic::GetNeuralNetworkIndices(uint16 MeshIndex, uint16 RegionIndex) const
{
	rl4::ConstArrayView<uint16> Indices = RigLogic->getNeuralNetworkIndices(MeshIndex, RegionIndex);
	return TArrayView<const uint16>{Indices.data(), static_cast<int32>(Indices.size())};
}

void FRigLogic::MapGUIToRawControls(FRigInstance* Instance) const
{
	RigLogic->mapGUIToRawControls(Instance->Unwrap());
}

void FRigLogic::MapRawToGUIControls(FRigInstance* Instance) const
{
	RigLogic->mapRawToGUIControls(Instance->Unwrap());
}

void FRigLogic::CalculateControls(FRigInstance* Instance) const
{
	RigLogic->calculateControls(Instance->Unwrap());
}

void FRigLogic::CalculateMachineLearnedBehaviorControls(FRigInstance* Instance) const
{
	RigLogic->calculateMachineLearnedBehaviorControls(Instance->Unwrap());
}

void FRigLogic::CalculateMachineLearnedBehaviorControls(FRigInstance* Instance, uint16 NeuralNetIndex) const
{
	RigLogic->calculateMachineLearnedBehaviorControls(Instance->Unwrap(), NeuralNetIndex);
}

void FRigLogic::CalculateRBFControls(FRigInstance* Instance) const
{
	RigLogic->calculateRBFControls(Instance->Unwrap());
}

void FRigLogic::CalculateRBFControls(FRigInstance* Instance, uint16 SolverIndex) const
{
	RigLogic->calculateRBFControls(Instance->Unwrap(), SolverIndex);
}

void FRigLogic::CalculateJoints(FRigInstance* Instance) const
{
	RigLogic->calculateJoints(Instance->Unwrap());
}

void FRigLogic::CalculateJoints(FRigInstance* Instance, uint16 JointGroupIndex) const
{
	RigLogic->calculateJoints(Instance->Unwrap(), JointGroupIndex);
}

void FRigLogic::CalculateBlendShapes(FRigInstance* Instance) const
{
	RigLogic->calculateBlendShapes(Instance->Unwrap());
}

void FRigLogic::CalculateAnimatedMaps(FRigInstance* Instance) const
{
	RigLogic->calculateAnimatedMaps(Instance->Unwrap());
}

void FRigLogic::Calculate(FRigInstance* Instance) const
{
	RigLogic->calculate(Instance->Unwrap());
}

rl4::RigLogic* FRigLogic::Unwrap() const
{
	return RigLogic.Get();
}
