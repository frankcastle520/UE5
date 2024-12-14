// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorConfigBase.h"
#include "NiagaraAssetBrowserConfig.generated.h"

USTRUCT()
struct FNiagaraAssetBrowserConfiguration
{
	GENERATED_BODY()
	
	UPROPERTY()
	TArray<FName> MainFilterSelection;

	UPROPERTY()
	bool bShouldDisplayViewport = false;
};

UCLASS(EditorConfig="NiagaraAssetBrowser")
class NIAGARAEDITOR_API UNiagaraAssetBrowserConfig : public UEditorConfigBase
{
	GENERATED_BODY()
public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPropertyChanged, const FPropertyChangedEvent&);
	
	static void Initialize();
	static UNiagaraAssetBrowserConfig* Get() { return Instance; }
	FOnPropertyChanged& OnPropertyChanged() { return OnPropertyChangedDelegate; }
	
	UPROPERTY(meta=(EditorConfig))
	TMap<FName, FNiagaraAssetBrowserConfiguration> MainFilterSelection;

	UPROPERTY(meta=(EditorConfig))
    bool bShowHiddenAssets = false;
    	
	UPROPERTY(meta=(EditorConfig))
	bool bShowDeprecatedAssets = false;

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;	
private:
	static TObjectPtr<UNiagaraAssetBrowserConfig> Instance;

	FOnPropertyChanged OnPropertyChangedDelegate;
};
