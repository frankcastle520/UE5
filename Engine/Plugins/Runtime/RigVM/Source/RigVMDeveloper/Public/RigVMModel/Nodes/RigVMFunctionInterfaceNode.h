// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMModel/Nodes/RigVMTemplateNode.h"
#include "RigVMFunctionInterfaceNode.generated.h"

/**
 * The Function Interface node is is used as the base class for
 * both the entry and return nodes.
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMFunctionInterfaceNode : public URigVMTemplateNode
{
	GENERATED_BODY()

public:

	// Override template node functions
	virtual uint32 GetStructureHash() const override;

	// Override node functions
	virtual FLinearColor GetNodeColor() const override;
	virtual bool IsDefinedAsVarying() const override;
	
	// URigVMNode interface
	virtual  FText GetToolTipText() const override;
	virtual FText GetToolTipTextForPin(const URigVMPin* InPin) const override;	

protected:

	const URigVMPin* FindReferencedPin(const URigVMPin* InPin) const;
	const URigVMPin* FindReferencedPin(const FString& InPinPath) const;

private:

	friend class URigVMController;
};

