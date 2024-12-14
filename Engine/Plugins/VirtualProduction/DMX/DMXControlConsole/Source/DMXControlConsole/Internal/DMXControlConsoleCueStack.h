// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"

#include "DMXControlConsoleCueStack.generated.h"

class UDMXControlConsoleFaderBase;


/** Struct which describes a cue in the DMX Control Console */
USTRUCT()
struct FDMXControlConsoleCue
{
	GENERATED_BODY()

	/** The unique id of this cue */
	UPROPERTY(meta = (IgnoreForMemberInitializationTest))
	FGuid CueID = FGuid::NewGuid();

	/** The name label of this cue */
	UPROPERTY()
	FString CueLabel;

	/** The color of this cue */
	UPROPERTY()
	FLinearColor CueColor = FLinearColor::Transparent;

	/** A fader object to value map */
	UPROPERTY()
	TMap<TWeakObjectPtr<UDMXControlConsoleFaderBase>, uint32> FaderToValueMap;

	bool operator==(const FDMXControlConsoleCue& Other) const
	{
		return CueID == Other.CueID;
	}
};

/** A stack of cues in the DMX Control Console */
UCLASS()
class DMXCONTROLCONSOLE_API UDMXControlConsoleCueStack
	: public UObject
{
	GENERATED_BODY()

public:
	/**
	* Adds a new cue to this stack by using the given faders array
	*
	* @param Faders the array of faders to provide ad cue data.
	* @param CueLabel (optional) the label name of the new cue.
	* @param CueColor (optional) the color used for highlight the cue in the editor.
	* 
	* @return Returns a pointer to the newly created cue, or nullptr if no cue could be created.
	*/
	FDMXControlConsoleCue* AddNewCue(const TArray<UDMXControlConsoleFaderBase*>& Faders, const FString CueLabel = TEXT(""), const FLinearColor CueColor = FLinearColor::Transparent);

	/** Removes the given cue from the stack, if valid */
	void RemoveCue(const FDMXControlConsoleCue& Cue);

	/** Finds the cue with the given unique id in this cue stack */
	FDMXControlConsoleCue* FindCue(const FGuid CueID);

	/** Finds the cue with the given label in this cue stack */
	FDMXControlConsoleCue* FindCue(const FString& CueLabel);

	/** Updates the given cue with the given faders data if contained by this cue stack */
	void UpdateCueData(const FGuid CueID, const TArray<UDMXControlConsoleFaderBase*>& Faders);

	/** Moves the given Cue to the specified index, if valid */
	void MoveCueToIndex(const FDMXControlConsoleCue& Cue, const uint32 NewIndex);

	/** Recalls the given Cue if contained by this cue stack */
	void Recall(const FDMXControlConsoleCue& Cue);

	/** Gets a reference to the cue stack array */
	const TArray<FDMXControlConsoleCue>& GetCuesArray() const { return CuesArray; }

	/** Clears the cues array */
	void Clear();

#if WITH_EDITOR
	/** Returns true if the cues stack can store cue data */
	bool CanStore() const { return bCanStore; }

	/** Called when a property of a fader in the console has changed  */
	void OnFadersPropertiesChanged(FPropertyChangedEvent& PropertyChangedEvent);
#endif // WITH_EDITOR 

	/** Called when the cue stack has been changed */
	FSimpleMulticastDelegate& GetOnCueStackChanged() { return OnCueStackChanged; }

	//~ Begin UObject interface
	virtual void PostInitProperties() override;
	//~ End UObject interface

private:
	/** Generates a unique label name for a cue */
	FString GenerateUniqueCueLabel(const FString& CueLabel);

	/** Executed when the cue stack has been changed */
	FSimpleMulticastDelegate OnCueStackChanged;

	/** The array of cues */
	UPROPERTY()
	TArray<FDMXControlConsoleCue> CuesArray;

#if WITH_EDITORONLY_DATA
	/** True if the stack can store cues data */
	UPROPERTY()
	bool bCanStore = false;
#endif // WITH_EDITORONLY_DATA
};
