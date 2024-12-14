// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Logging/TokenizedMessage.h"
#include "MassEntityEditor.generated.h"


USTRUCT()
struct MASSENTITYEDITOR_API FMassEditorNotification
{
	GENERATED_BODY()

	EMessageSeverity::Type Severity = EMessageSeverity::Error;
	FText Message;

	/** 
	 * If set to true then a clickable "see log for details" message will be added to the message log. 
	 * Clicking the message takes the user to the Output Log.
	 */
	bool bIncludeSeeOutputLogForDetails = false;

	void Show();
};

namespace UE::Mass::Editor
{
	const FName MessageLogPageName(TEXT("MassEntity"));
}