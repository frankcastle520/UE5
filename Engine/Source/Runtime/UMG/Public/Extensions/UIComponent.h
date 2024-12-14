// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "UIComponent.generated.h"

class UWidget;

/** 
 * This is the base class to for UI Components that can be added to any UMG Widgets
 * in UMG Designer.When initialized, it will pass the widget it's attached to.
 */
UCLASS(Abstract, MinimalAPI, Experimental)
class UUIComponent : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Called when the owner widget is inititalized.
	 */
	UMG_API bool Initialize(UWidget* target);

	/**
	 * Called when the owner widget is constructed.
	 */
	UMG_API void Construct();
	

	/**
	 * Called when the owner widget is destructed.
	 */
	UMG_API void Destruct();

	/**
	 * Returns the Owner Widget this component is attached to.
	 *  @returns The owner widget
	*/
	UMG_API TWeakObjectPtr<UWidget> GetOwner() const;

protected:
	UMG_API virtual bool OnInitialize();
	UMG_API virtual void OnConstruct();
	UMG_API virtual void OnDestruct();

private:
	UPROPERTY(transient, DuplicateTransient)
	TWeakObjectPtr<UWidget> Owner;


};
