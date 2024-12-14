// Copyright Epic Games, Inc. All Rights Reserved.


#include "RigUnit_EvaluateChooser.h"
#include "DataInterface/AnimNextDataInterfaceInstance.h"
#include "ControlRig.h"

static void RunChooserHelper(TConstArrayView<TObjectPtr<UObject>> ContextObjects, FStructView ContextStruct, TObjectPtr<UChooserTable> Chooser, TObjectPtr<UObject>& OutResult)
{
	if ((!ContextObjects.IsEmpty() || ContextStruct.IsValid()) && Chooser != nullptr)
	{
		FChooserEvaluationContext ChooserContext;
		for(TObjectPtr<UObject> ContextObject : ContextObjects)
		{
			if(ContextObject != nullptr)
			{
				ChooserContext.AddObjectParam(ContextObject);
			}
		}
		if(ContextStruct.IsValid())
		{
			ChooserContext.AddStructViewParam(ContextStruct);
		}

		UChooserTable::EvaluateChooser(ChooserContext, Chooser, FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda([&OutResult](UObject* InResult)
		{
			OutResult = InResult;
			return FObjectChooserBase::EIteratorStatus::Stop;
		}));
	}
}

FRigUnit_EvaluateChooser_ControlRig_Execute()
{
	Result = nullptr;

	RunChooserHelper( { ContextObject, ExecuteContext.ControlRig }, FStructView(), Chooser, Result);
}

FRigUnit_EvaluateChooser_AnimNext_Execute()
{
	Result = nullptr;

	const FAnimNextDataInterfaceInstance& Instance = ExecuteContext.GetInstance();
	RunChooserHelper({ ContextObject }, FStructView::Make(const_cast<FAnimNextDataInterfaceInstance&>(Instance)), Chooser, Result);
}