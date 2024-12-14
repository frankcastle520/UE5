// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Graph/RigUnit_AnimNextBase.h"
#include "RigUnit_AnimNextAnimationGraphSelf.generated.h"

class UAnimNextAnimationGraph;

/** Get a reference to the currently executing animation graph */
USTRUCT(meta=(Deprecated, DisplayName="Self", Category="Animation Graph", NodeColor="0, 0, 1", Keywords="Current,This"))
struct ANIMNEXT_API FRigUnit_AnimNextAnimationGraphSelf : public FRigUnit_AnimNextBase
{
	GENERATED_BODY()

	RIGVM_METHOD()
	void Execute();

	RIGVM_METHOD()
	virtual FRigVMStructUpgradeInfo GetUpgradeInfo() const override;

	// The currently-executing animation graph
	UPROPERTY(meta=(Output))
	TObjectPtr<UAnimNextAnimationGraph> Self;
};
