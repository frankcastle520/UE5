// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"

#include "DMXControlConsoleActor.generated.h"

class UDMXControlConsoleData;
class USceneComponent;


/** Actor class for DMX Control Console */
UCLASS(NotBlueprintable)
class DMXCONTROLCONSOLE_API ADMXControlConsoleActor
	: public AActor
{
	GENERATED_BODY()

public:
	/** Constructor */
	ADMXControlConsoleActor();

	/** Sets the Control Console Data used in this actor */
	void SetDMXControlConsoleData(UDMXControlConsoleData* InDMXControlConsoleData);

	/** Returns the Control Console Data used for this actor */
	UDMXControlConsoleData* GetControlConsoleData() const { return ControlConsoleData; }

	/** Sets the current DMX Control Console to start sending DMX data */
	UFUNCTION(BlueprintCallable, Category = "DMX Control Console")
	void StartSendingDMX();

	/** Sets the current DMX Control Console to stop sending DMX data */
	UFUNCTION(BlueprintCallable, Category = "DMX Control Console")
	void StopSendingDMX();

	/** Sets the current DMX Control Console to pause sending DMX data */
	UFUNCTION(BlueprintCallable, Category = "DMX Control Console")
	void PauseSendingDMX();

	/** Resets all the faders in this Control Console to their default values */
	UFUNCTION(BlueprintCallable, Category = "DMX Control Console")
	void ResetToDefault();

	/** Resets all the faders in this Control Console to zero */
	UFUNCTION(BlueprintCallable, Category = "DMX Control Console")
	void ResetToZero();

#if WITH_EDITOR
	/** Returns the delegate called when the Control Console has been reset */
	static FSimpleMulticastDelegate& GetOnControlConsoleReset() { return OnControlConsoleReset; }

	// Property name getters
	static FName GetControlConsoleDataPropertyNameChecked() { return GET_MEMBER_NAME_CHECKED(ADMXControlConsoleActor, ControlConsoleData); }
	static FName GetAutoActivatePropertyNameChecked() { return GET_MEMBER_NAME_CHECKED(ADMXControlConsoleActor, bAutoActivate); }
	static FName GetSendDMXInEditorPropertyNameChecked() { return GET_MEMBER_NAME_CHECKED(ADMXControlConsoleActor, bSendDMXInEditor); }
#endif

protected:
	//~ Begin AActor interface
	virtual void PostLoad() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End AActor interface

private:
#if WITH_EDITOR
	/** Applies the play in editor state. Enables send when auto activate is on, send DMX in editor is enabled and not currently playing */
	void ApplySendDMXInEditorState();
#endif // WITH_EDITOR

	/** The Control Console Data used in this actor */
	UPROPERTY(VisibleAnywhere, Category = "DMX Control Console")
	TObjectPtr<UDMXControlConsoleData> ControlConsoleData;

	/** True if the Control Console should send DMX data in runtime */
	UPROPERTY(EditAnywhere, Category = "DMX Control Console")
	bool bAutoActivate = true;

#if WITH_EDITORONLY_DATA
	/** True if the Control Console should send DMX data in Editor */
	UPROPERTY(EditAnywhere, Category = "DMX Control Console", Meta = (DisplayName = "Send DMX in Editor"))
	bool bSendDMXInEditor = false;
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
	/** Called when the Control Console has been reset */
	static FSimpleMulticastDelegate OnControlConsoleReset;

	/** True while the actor plays in a world */
	bool bIsPlayInWorld = false;
#endif // WITH_EDITOR

	/** Scene component to make the Actor easily visible in Editor */
	UPROPERTY(VisibleAnywhere, Category = "Actor", AdvancedDisplay, Meta = (AllowPrivateAccess = true))
	TObjectPtr<USceneComponent> RootSceneComponent;
};
