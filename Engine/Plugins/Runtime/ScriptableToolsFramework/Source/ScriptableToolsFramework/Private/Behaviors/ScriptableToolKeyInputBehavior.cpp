// Copyright Epic Games, Inc. All Rights Reserved.

#include "Behaviors/ScriptableToolKeyInputBehavior.h"

#include "BaseTools/ScriptableModularBehaviorTool.h"
#include "BaseBehaviors/KeyInputBehavior.h"

void UScriptableToolKeyInputBehavior::Init(
	TObjectPtr<UScriptableModularBehaviorTool> BehaviorHostIn,
	FMouseBehaviorModiferCheckDelegate ModifierCheckFuncIn,
	FOnKeyStateToggleDelegate OnKeyPressedFuncIn,
	FOnKeyStateToggleDelegate OnKeyReleasedFuncIn,
	const TArray<FKey>& ListenKeysIn,
	bool bRequireAllKeysIn
)
{
	BehaviorHost = BehaviorHostIn;
	Behavior = CreateNewBehavior();
	ModifierCheckFunc = ModifierCheckFuncIn;
	OnKeyPressedFunc = OnKeyPressedFuncIn;
	OnKeyReleasedFunc = OnKeyReleasedFuncIn;
	ListenKeys = ListenKeysIn;

	Behavior->Initialize(this, ListenKeys);
	Behavior->ModifierCheckFunc = [this](const FInputDeviceState& InputDeviceState) {
		bool bResult = true;
		if (ModifierCheckFunc.IsBound())
		{
			bResult = ModifierCheckFunc.Execute(InputDeviceState);
		}
		return bResult;
	};
	Behavior->bRequireAllKeys = bRequireAllKeysIn;

	BehaviorHost->AddInputBehavior(Behavior);

	Behavior->Modifiers.RegisterModifier(1, FInputDeviceState::IsShiftKeyDown);
	Behavior->Modifiers.RegisterModifier(2, FInputDeviceState::IsCtrlKeyDown);
	Behavior->Modifiers.RegisterModifier(3, FInputDeviceState::IsAltKeyDown);

}

UKeyInputBehavior* UScriptableToolKeyInputBehavior::CreateNewBehavior() const
{
	return NewObject<UKeyInputBehavior>();
}

UInputBehavior* UScriptableToolKeyInputBehavior::GetWrappedBehavior()
{
	return Behavior;
}

void UScriptableToolKeyInputBehavior::OnKeyPressed(const FKey& Key)
{
	if (OnKeyPressedFunc.IsBound())
	{
		OnKeyPressedFunc.Execute(Key, BehaviorHost->GetActiveModifiers());
	}
	return;
}

void UScriptableToolKeyInputBehavior::OnKeyReleased(const FKey& Key)
{
	if (OnKeyReleasedFunc.IsBound())
	{
		OnKeyReleasedFunc.Execute(Key, BehaviorHost->GetActiveModifiers());
	}
	return;
}

void UScriptableToolKeyInputBehavior::OnUpdateModifierState(int ModifierID, bool bIsOn)
{
	BehaviorHost->OnUpdateModifierState(ModifierID, bIsOn);
}
