// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowCoreNodes.h"
#include "Dataflow/DataflowNode.h"
#include "Dataflow/DataflowNodeFactory.h"

namespace UE::Dataflow
{
	void RegisterCoreNodes()
	{
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowReRouteNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowBranchNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowSelectNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowPrintNode);
	}
}

FDataflowReRouteNode::FDataflowReRouteNode(const UE::Dataflow::FNodeParameters& Param, FGuid InGuid)
	: Super(Param, InGuid)
{
	RegisterInputConnection(&Value);
	RegisterOutputConnection(&Value)
		.SetPassthroughInput(&Value);
}

void FDataflowReRouteNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	ForwardInput(Context, &Value, &Value);
}

bool FDataflowReRouteNode::OnInputTypeChanged(const FDataflowInput* Input)
{
	return SetOutputConcreteType(&Value, Input->GetType());
}

bool FDataflowReRouteNode::OnOutputTypeChanged(const FDataflowOutput* Input)
{
	return SetInputConcreteType(&Value, Input->GetType());
}

FDataflowBranchNode::FDataflowBranchNode(const UE::Dataflow::FNodeParameters& Param, FGuid InGuid)
	: Super(Param, InGuid)
{
	RegisterInputConnection(&TrueValue);
	RegisterInputConnection(&FalseValue);
	RegisterInputConnection(&bCondition);
	RegisterOutputConnection(&Result);
}

void FDataflowBranchNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Result))
	{
		const bool InCondition = GetValue<bool>(Context, &bCondition);
		const void* SelectedInputReference = InCondition ? &TrueValue : &FalseValue;
		if (IsConnected(SelectedInputReference))
		{
			ForwardInput(Context, SelectedInputReference, &Result);
		}
		else
		{
			// TODO : throw an invalid type error when context error is available 
			// Context.Error(TEXT("Both True and False Inputs must be connected"));
		}
	}
}

bool FDataflowBranchNode::OnInputTypeChanged(const FDataflowInput* Input)
{
	// using single bitwise | operator to avoid skipping calls to SetInputConcreteType because of the shortcircuiting of ||
	// need to disable the warning for static analysis ( V792 warning )
	return bool(
		SetInputConcreteType(&TrueValue, Input->GetType())
	  | SetInputConcreteType(&FalseValue, Input->GetType()) //-V792
	  | SetOutputConcreteType(&Result, Input->GetType()) //-V792
		);
}

bool FDataflowBranchNode::OnOutputTypeChanged(const FDataflowOutput* Input)
{
	// using single bitwise | operator to avoid skipping calls to SetInputConcreteType because of the shortcircuiting of ||
	// need to disable the warning for static analysis ( V792 warning )
	return bool(
		  SetInputConcreteType(&TrueValue, Input->GetType())
		| SetInputConcreteType(&FalseValue, Input->GetType()) //-V792
		);
}

FDataflowSelectNode::FDataflowSelectNode(const UE::Dataflow::FNodeParameters& Param, FGuid InGuid)
	: Super(Param, InGuid)
{
	// Add two sets of pins to start.
	RegisterInputConnection(&SelectedIndex);
	for (int32 Index = 0; Index < NumInitialInputs; ++Index)
	{
		AddPins();
	}
	RegisterOutputConnection(&Result)
		.SetPassthroughInput(GetConnectionReference(0));
	check(NumRequiredDataflowInputs + NumInitialInputs == GetNumInputs()); // Update NumRequiredDataflowInputs when adding more inputs. This is used by Serialize
}

void FDataflowSelectNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Result))
	{
		const int32 InSelectedIndex = GetValue<int32>(Context, &SelectedIndex);
		if (Inputs.IsValidIndex(InSelectedIndex))
		{
			const UE::Dataflow::TConnectionReference<FDataflowAnyType> SelectedInputReference = GetConnectionReference(InSelectedIndex);
			if (IsConnected(SelectedInputReference))
			{
				ForwardInput(Context, SelectedInputReference, &Result);
			}
			else
			{
				// TODO : throw an invalid type error when context error is available 
				// Context.Error(TEXT("Both True and False Inputs must be connected"));
			}
		}
	}
}

bool FDataflowSelectNode::OnInputTypeChanged(const FDataflowInput* Input)
{
	bool bResult = SetOutputConcreteType(&Result, Input->GetType());
	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		bResult |= SetInputConcreteType(GetConnectionReference(Index), Input->GetType());
	}
	return bResult;
}

bool FDataflowSelectNode::OnOutputTypeChanged(const FDataflowOutput* Input)
{
	bool bResult = false;
	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		bResult |= SetInputConcreteType(GetConnectionReference(Index), Input->GetType());
	}
	return bResult;
}

TArray<UE::Dataflow::FPin> FDataflowSelectNode::AddPins()
{
	const int32 Index = Inputs.AddDefaulted();
	const FDataflowInput& Input = RegisterInputArrayConnection(GetConnectionReference(Index));
	if (Index > 0)
	{
		// Set concrete type the same as Index0.
		const FDataflowInput* const Input0 = FindInput(GetConnectionReference(0));
		check(Input0);
		SetInputConcreteType(GetConnectionReference(Index), Input0->GetType());
	}
	return { { UE::Dataflow::FPin::EDirection::INPUT, Input.GetType(), Input.GetName() } };
}

TArray<UE::Dataflow::FPin> FDataflowSelectNode::GetPinsToRemove() const
{
	const int32 Index = Inputs.Num() - 1;
	check(Inputs.IsValidIndex(Index));
	if (const FDataflowInput* const Input = FindInput(GetConnectionReference(Index)))
	{
		return { { UE::Dataflow::FPin::EDirection::INPUT, Input->GetType(), Input->GetName() } };
	}
	return Super::GetPinsToRemove();
}

void FDataflowSelectNode::OnPinRemoved(const UE::Dataflow::FPin& Pin)
{
	const int32 Index = Inputs.Num() - 1;
	check(Inputs.IsValidIndex(Index));
#if DO_CHECK
	const FDataflowInput* const Input = FindInput(GetConnectionReference(Index));
	check(Input);
	check(Input->GetName() == Pin.Name);
	check(Input->GetType() == Pin.Type);
#endif
	Inputs.SetNum(Index);

	return Super::OnPinRemoved(Pin);
}

void FDataflowSelectNode::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading())
	{
		check(Inputs.Num() >= NumInitialInputs);
		for (int32 Index = 0; Index < NumInitialInputs; ++Index)
		{
			check(FindInput(GetConnectionReference(Index)));
		}

		for (int32 Index = NumInitialInputs; Index < Inputs.Num(); ++Index)
		{
			FindOrRegisterInputArrayConnection(GetConnectionReference(Index));
		}

		if (Ar.IsTransacting())
		{
			const int32 OrigNumRegisteredInputs = GetNumInputs() - NumRequiredDataflowInputs;
			const int32 OrigNumInputs = Inputs.Num();
			if (OrigNumRegisteredInputs > OrigNumInputs)
			{
				// Inputs have been removed.
				// Temporarily expand Inputs so we can get connection references.
				Inputs.SetNum(OrigNumRegisteredInputs);
				for (int32 Index = OrigNumInputs; Index < Inputs.Num(); ++Index)
				{
					UnregisterInputConnection(GetConnectionReference(Index));
				}
				Inputs.SetNum(OrigNumInputs);
			}
		}
		else
		{
			// Index + all Inputs
			ensureAlways(Inputs.Num() + NumRequiredDataflowInputs == GetNumInputs());
		}
	}
}

UE::Dataflow::TConnectionReference<FDataflowAnyType> FDataflowSelectNode::GetConnectionReference(int32 Index) const
{
	return { &Inputs[Index], Index, &Inputs };
}

FDataflowPrintNode::FDataflowPrintNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Value);
}

void FDataflowPrintNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	const FString InValue = GetValue(Context, &Value);
	UE_LOG(LogTemp, Warning, TEXT("[Dataflow Print] %s"), *InValue);
}