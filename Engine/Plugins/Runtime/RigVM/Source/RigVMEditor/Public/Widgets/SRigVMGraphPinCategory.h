// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "SGraphPin.h"

class RIGVMEDITOR_API SRigVMGraphPinCategory : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SRigVMGraphPinCategory)
	{}
	SLATE_END_ARGS()


	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

protected:

	//~ Begin SGraphPin Interface
	virtual TSharedRef<SWidget>	GetDefaultValueWidget() override;
	virtual const FSlateBrush* GetPinIcon() const override { return nullptr; }
	//~ End SGraphPin Interface
};
