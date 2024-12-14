// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Behaviors/ScriptableToolBehaviorDelegates.h"
#include "ScriptableToolBehavior.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"

#include "ScriptableToolKeyInputBehavior.generated.h"

class UScriptableModularBehaviorTool;
class UKeyInputBehavior;

UCLASS()
class UScriptableToolKeyInputBehavior : public UScriptableToolBehavior, public IKeyInputBehaviorTarget
{
	GENERATED_BODY()

public:
	UScriptableToolKeyInputBehavior() {};

	void Init(TObjectPtr<UScriptableModularBehaviorTool> BehaviorHostIn,
		FMouseBehaviorModiferCheckDelegate ModifierCheckFuncIn,
		FOnKeyStateToggleDelegate OnKeyPressedFuncIn,
		FOnKeyStateToggleDelegate OnKeyReleasedFuncIn,
		const TArray<FKey>& ListenKeysIn,
		bool bRequireAllKeys);

	virtual UKeyInputBehavior* CreateNewBehavior() const;
	virtual UInputBehavior* GetWrappedBehavior() override;

	virtual void OnKeyPressed(const FKey& KeyID) override;
	virtual void OnKeyReleased(const FKey& KeyID) override;

	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn) override;

private:

	UPROPERTY()
	TObjectPtr<UScriptableModularBehaviorTool> BehaviorHost;

	UPROPERTY()
	TObjectPtr<UKeyInputBehavior> Behavior;

	UPROPERTY()
	FMouseBehaviorModiferCheckDelegate ModifierCheckFunc;

	UPROPERTY()
	FOnKeyStateToggleDelegate	OnKeyPressedFunc;

	UPROPERTY()
	FOnKeyStateToggleDelegate OnKeyReleasedFunc;

	UPROPERTY()
	TArray<FKey> ListenKeys;
};
