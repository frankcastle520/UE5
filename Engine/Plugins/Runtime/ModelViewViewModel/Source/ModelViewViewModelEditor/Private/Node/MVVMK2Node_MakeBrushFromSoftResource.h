// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_LoadAsset.h"
#include "View/MVVMViewTypes.h"

#include "MVVMK2Node_MakeBrushFromSoftResource.generated.h"

class UEdGraphPin;
class FKismetCompilerContext;


/**
 * Utility async node to create a slate brush from a soft resource (Texture, Material, etc). 
 * Needed since we cannot nest conversion functions (Ex: LoadAsset->MakeBrushFromTexture).
 */
UCLASS(MinimalAPI, Abstract, Hidden, HideDropDown)
class UMVVMK2Node_MakeBrushFromSoftResource : public UK2Node_LoadAsset
{
	GENERATED_BODY()

public:
	//~ Begin UEdGraphNode Interface.
	virtual void AllocateDefaultPins() override;
	//~ End UEdGraphNode Interface.
	
	//~ Begin K2Node Interface.
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	//~ End K2Node Interface.

	/** Get the then output pin */
	UEdGraphPin* GetThenPin() const;

	/** Get the then output pin */
	UEdGraphPin* GetCompletedPin() const;

protected:

	//~ Begin UK2Node_LoadAsset Interface.
	virtual const FName& GetOutputPinName() const;
	//~ End UK2Node_LoadAsset Interface.

	/** Get the type of the input arg pin */
	virtual UClass* GetInputResourceClass() const;

	/** Get the type of the input arg pin */
	virtual FName GetMakeBrushFunctionName() const;
};


//////////////////////////////////////////////////////////////////////////


UCLASS(MinimalAPI)
class UMVVMK2Node_MakeBrushFromSoftTexture : public UMVVMK2Node_MakeBrushFromSoftResource
{
	GENERATED_BODY()

public:
	//~ Begin UEdGraphNode Interface.
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	//~ End UEdGraphNode Interface.

protected:
	//~ Begin UK2Node_LoadAsset Interface.
	virtual const FName& GetInputPinName() const;
	//~ End UK2Node_LoadAsset Interface.

	//~ Begin UMVVMK2Node_MakeBrushFromSoftResource Interface.
	virtual UClass* GetInputResourceClass() const override;
	virtual FName GetMakeBrushFunctionName() const override;
	//~ End UMVVMK2Node_MakeBrushFromSoftResource Interface.
};


//////////////////////////////////////////////////////////////////////////


UCLASS(MinimalAPI)
class UMVVMK2Node_MakeBrushFromSoftMaterial: public UMVVMK2Node_MakeBrushFromSoftResource
{
	GENERATED_BODY()

public:
	//~ Begin UEdGraphNode Interface.
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	//~ End UEdGraphNode Interface.

protected:
	//~ Begin UK2Node_LoadAsset Interface.
	virtual const FName& GetInputPinName() const;
	//~ End UK2Node_LoadAsset Interface.

	//~ Begin UMVVMK2Node_MakeBrushFromSoftResource Interface.
	virtual UClass* GetInputResourceClass() const override;
	virtual FName GetMakeBrushFunctionName() const override;
	//~ End UMVVMK2Node_MakeBrushFromSoftResource Interface.
};
