// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"
#include "Extensions/UserWidgetExtension.h"

#include "UIComponentContainer.generated.h"

class UUserWidget;
class UWidget;
class UUIComponent;

USTRUCT()
struct FUIComponentTarget
{
	GENERATED_BODY();
public:

	FUIComponentTarget();
	FUIComponentTarget(UUIComponent* Component, FName InChildName);

	/** Resolves the Widget ptr using it's name. */
	UWidget* Resolve(const class UWidgetTree* WidgetTree);
	FName GetTargetName() const { return TargetName; }
	void SetTargetName(FName NewName);
	UUIComponent* GetComponent() const { return Component; }

private:

	// We use a TargetName to resolve the Widget only at compile time and on the Runtime Widget. 
	// It simplify edition in UMG Designer and make sure we do not need to keep Association in sync with the WidgetTree.
	UPROPERTY()
	FName TargetName;
	
	UPROPERTY(Instanced)
	TObjectPtr<UUIComponent> Component;
};


/**
 * Class that holds all the UIComponents for a UUserWidget.
 */
UCLASS(NotBlueprintType, Experimental)
class UMG_API UUIComponentContainer : public UUserWidgetExtension
{
	GENERATED_BODY()

public:
	virtual void Initialize() override;
	virtual void Construct() override;		
	virtual void Destruct() override;

	TArray<UUIComponent*> GetExtensionsFor(FName TargetName);

	void AddComponent(FName TargetName, UUIComponent* Component);
	void RemoveComponent(FName TargetName, UUIComponent* Component);
	void RemoveAllComponentsFor(FName TargetName);

	bool IsEmpty() const;

	void RenameWidget(FName OldName, FName NewName);

	void OnPreviewContentChanged(TSharedRef<SWidget> NewContent);

private:
	void Resolve();
	void CleanupUIComponents(UUserWidget* UserWidget);

	// Use a single TArray for the Entire UUserWidget to reduce memory usage.
	UPROPERTY()
	TArray<FUIComponentTarget> Components;
};
