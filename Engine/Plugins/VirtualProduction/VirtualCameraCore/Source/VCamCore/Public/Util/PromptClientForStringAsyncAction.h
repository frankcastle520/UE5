// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintAsyncActionBase.h"
#include "Output/VCamOutputProviderBase.h"

#include "PromptClientForStringAsyncAction.generated.h"

class UVCamComponent;

/**
 * Sends a prompt for a string to the current VCam client.
 */
UCLASS()
class VCAMCORE_API UPromptClientForStringAsyncAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()
public:

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FStringPromptResponseDelegate, const FVCamStringPromptResponse&, Response);

	/** Event that triggers when the operation completes. */
	UPROPERTY(BlueprintAssignable, Category = "VirtualCamera")
	FStringPromptResponseDelegate OnCompleted;

	/**
	 * Prompt the VCam client to provide a string value.
	 * 
	 * @param VCamComponent The VCam component streaming video to the client.
	 * @param PromptTitle The title of the prompt to show to the user. If empty, a default title will be used.
	 * @param DefaultValue The default string to fill in the client's text box.
	 */
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera", meta = (BlueprintInternalUseOnly = "true"))
	static UPromptClientForStringAsyncAction* PromptClientForString(UVCamComponent* VCamComponent, FText PromptTitle, const FString& DefaultValue);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	UPROPERTY(Transient)
	TObjectPtr<UVCamComponent> VCamComponent;

	UPROPERTY(Transient)
	FVCamStringPromptRequest PromptRequest;
};
