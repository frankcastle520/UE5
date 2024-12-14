// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Blueprint.h"
#include "MovieSceneSequenceEditor.h"
#include "DaySequenceDirector.h"
#include "DaySequence.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"

struct FMovieSceneSequenceEditor_DaySequence : FMovieSceneSequenceEditor
{
	virtual bool CanCreateEvents(UMovieSceneSequence* InSequence) const
	{
		return true;
	}

	virtual UBlueprint* GetBlueprintForSequence(UMovieSceneSequence* InSequence) const override
	{
		UDaySequence* DaySequence = CastChecked<UDaySequence>(InSequence);
		return DaySequence->GetDirectorBlueprint();
	}

	virtual UBlueprint* CreateBlueprintForSequence(UMovieSceneSequence* InSequence) const override
	{
		UBlueprint* Blueprint = GetBlueprintForSequence(InSequence);
		if (!ensureMsgf(!Blueprint, TEXT("Should not call CreateBlueprintForSequence when one already exists")))
		{
			return Blueprint;
		}

		UDaySequence* DaySequence = CastChecked<UDaySequence>(InSequence);

		Blueprint = FKismetEditorUtilities::CreateBlueprint(UDaySequenceDirector::StaticClass(), InSequence, FName(*DaySequence->GetDirectorBlueprintName()), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		Blueprint->ClearFlags(RF_Standalone);

		DaySequence->SetDirectorBlueprint(Blueprint);
		return Blueprint;
	}
};
