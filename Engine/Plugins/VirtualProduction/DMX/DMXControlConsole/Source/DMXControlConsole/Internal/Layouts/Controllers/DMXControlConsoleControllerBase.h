// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "DMXControlConsoleControllerBase.generated.h"


#if WITH_EDITOR
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPropertiesChangedDelegate, FPropertyChangedEvent&);
#endif // WITH_EDITOR 

/** Base class for Controllers */
UCLASS(Abstract)
class DMXCONTROLCONSOLE_API UDMXControlConsoleControllerBase
	: public UObject
{
	GENERATED_BODY()

public:
	/** True if the value of the Controller can't be changed */
	bool IsLocked() const { return bIsLocked; }

#if WITH_EDITOR
	/** Returns the delegate called when a Controller property has been changed */
	static FOnPropertiesChangedDelegate& GetOnPropertiesChanged() { return OnPropertiesChanged; }

	//~ Begin of UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End of UObject interface
#endif // WITH_EDITOR 

	// Property Name getters
	FORCEINLINE static FName GetIsLockedPropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXControlConsoleControllerBase, bIsLocked); }

protected:
	/** If true, the value of the Controller can't be changed */
	UPROPERTY(EditAnywhere, Category = "DMX Controller")
	bool bIsLocked = false;

private:
#if WITH_EDITORONLY_DATA
	/** Called when a Controller property has been changed */
	static FOnPropertiesChangedDelegate OnPropertiesChanged;
#endif // WITH_EDITORONLY_DATA
};
