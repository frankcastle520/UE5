// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SRigVMGraphNode.h"
#include "Editor/RigVMMinimalEnvironment.h"

class RIGVMEDITOR_API SRigVMNodePreviewWidget : public SCompoundWidget
{
public:
	
	SLATE_BEGIN_ARGS(SRigVMNodePreviewWidget)
		: _Padding(16)
	{}
	SLATE_ATTRIBUTE(FMargin, Padding)
	SLATE_ARGUMENT(TSharedPtr<FRigVMMinimalEnvironment>, Environment)
	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	void UpdateNodeWidget();
	
	TSharedPtr<FRigVMMinimalEnvironment> Environment;
};
