// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "TG_Expression_InputParam.h"
#include "Engine/Texture.h"
#include "TG_Texture.h"

#include "TG_Expression_TexturePath.generated.h"

UCLASS()
class TEXTUREGRAPH_API UTG_Expression_TexturePath : public UTG_Expression_InputParam
{
	GENERATED_BODY()
	TG_DECLARE_INPUT_PARAM_EXPRESSION(TG_Category::Input);

protected:
	// Special case for TexturePath Constant signature, we want to keep the Path Input connectable in that case
	// so do this in the override version of BuildInputConstantSignature()
	virtual FTG_SignaturePtr BuildInputConstantSignature() const override;

	// Validate the input path, returning the actual path to use
	// empty if the input path is NOT valid
	bool ValidateInputPath(FString& ValidatedPath) const;

	void ReportError(MixUpdateCyclePtr Cycle);
public:

	virtual void Evaluate(FTG_EvaluationContext* InContext) override;
	virtual bool Validate(MixUpdateCyclePtr	Cycle) override;
	
	// The output of the node, which is the loaded texture from the path
	UPROPERTY(meta = (TGType = "TG_Output", PinDisplayName = "", HideInnerPropertiesInNode))
	FTG_Texture Output;

	// Input file path of the texture
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NoCategory, meta = (TGType = "TG_InputParam", NoResetToDefault) )
	FString Path;

	class ULayerChannel* Channel;
	virtual FTG_Name GetDefaultName() const override { return TEXT("TexturePath"); }
	virtual void SetTitleName(FName NewName) override;
	virtual FName GetTitleName() const override;
	virtual FText GetTooltipText() const override { return FText::FromString(TEXT("Loads a texture from a path.")); }
};

