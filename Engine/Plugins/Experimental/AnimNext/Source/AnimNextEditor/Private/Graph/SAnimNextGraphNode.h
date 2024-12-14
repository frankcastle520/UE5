// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SRigVMGraphNode.h"

class UAnimNextEdGraphNode;

class SAnimNextGraphNode : public SRigVMGraphNode
{
public:
	SLATE_BEGIN_ARGS(SAnimNextGraphNode)
		: _GraphNodeObj(nullptr)
		{}

	SLATE_ARGUMENT(UAnimNextEdGraphNode*, GraphNodeObj)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
