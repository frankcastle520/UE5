// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Animation/BlendProfile.h"
#include "Curves/CurveFloat.h"
#include "Module/AnimNextModule.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "RigVMCore/RigVMRegistry.h"
#include "Animation/AnimSequence.h"
#include "Chooser.h"

#include "IAnimNextModuleInterface.h"
#include "TraitInterfaces/IEvaluate.h"
#include "TraitInterfaces/IUpdate.h"
#include "EvaluationVM/EvaluationVM.h"
#include "Graph/AnimNext_LODPose.h"

namespace UE::AnimNext::AnimGraph
{

class FModule : public IModuleInterface, IAnimNextAnimGraph
{
public:
	virtual void StartupModule() override
	{
		static TPair<UClass*, FRigVMRegistry::ERegisterObjectOperation> const AllowedObjectTypes[] =
		{
			{ UAnimSequence::StaticClass(), FRigVMRegistry::ERegisterObjectOperation::Class },
			{ UScriptStruct::StaticClass(), FRigVMRegistry::ERegisterObjectOperation::Class },
			{ UBlendProfile::StaticClass(), FRigVMRegistry::ERegisterObjectOperation::Class },
			{ UCurveFloat::StaticClass(), FRigVMRegistry::ERegisterObjectOperation::Class },
			{ UAnimNextModule::StaticClass(), FRigVMRegistry::ERegisterObjectOperation::Class },
			{ UChooserTable::StaticClass(), FRigVMRegistry::ERegisterObjectOperation::Class },
		};

		FRigVMRegistry::Get().RegisterObjectTypes(AllowedObjectTypes);

		IAnimNextModuleInterface::Get().RegisterAnimNextAnimGraph(*this);
	}

	virtual void ShutdownModule() override
	{
		IAnimNextModuleInterface::Get().UnregisterAnimNextAnimGraph();
	}

	virtual void UpdateGraph(const FAnimNextGraphInstancePtr& GraphInstance, float DeltaTime, UE::AnimNext::FTraitEventList& InputEventList, FTraitEventList& OutputEventList) const override
	{
		UE::AnimNext::UpdateGraph(GraphInstance, DeltaTime, InputEventList, OutputEventList);
	}

	virtual void EvaluateGraph(const FAnimNextGraphInstancePtr& GraphInstance, const UE::AnimNext::FReferencePose& RefPose, int32 GraphLODLevel, FAnimNextGraphLODPose& OutputPose) const override
	{
		const FEvaluationProgram EvaluationProgram = UE::AnimNext::EvaluateGraph(GraphInstance);

		FEvaluationVM EvaluationVM(EEvaluationFlags::All, RefPose, GraphLODLevel);
		bool bHasValidOutput = false;

		if (!EvaluationProgram.IsEmpty())
		{
			EvaluationProgram.Execute(EvaluationVM);

			TUniquePtr<FKeyframeState> EvaluatedKeyframe;
			if (EvaluationVM.PopValue(KEYFRAME_STACK_NAME, EvaluatedKeyframe))
			{
				OutputPose.LODPose.CopyFrom(EvaluatedKeyframe->Pose);
				OutputPose.Curves.CopyFrom(EvaluatedKeyframe->Curves);
				OutputPose.Attributes.CopyFrom(EvaluatedKeyframe->Attributes);
				bHasValidOutput = true;
			}
		}

		if (!bHasValidOutput)
		{
			// We need to output a valid pose, generate one
			FKeyframeState ReferenceKeyframe = EvaluationVM.MakeReferenceKeyframe(false);
			OutputPose.LODPose.CopyFrom(ReferenceKeyframe.Pose);
			OutputPose.Curves.CopyFrom(ReferenceKeyframe.Curves);
			OutputPose.Attributes.CopyFrom(ReferenceKeyframe.Attributes);
		}
	}
};

}

IMPLEMENT_MODULE(UE::AnimNext::AnimGraph::FModule, AnimNextAnimGraph)
