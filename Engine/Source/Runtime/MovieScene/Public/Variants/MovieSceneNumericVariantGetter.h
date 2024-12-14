// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "MovieSceneSignedObject.h"
#include "MovieSceneNumericVariantGetter.generated.h"


/**
 * Base class for all dynamic getter types supplied to an FMovieSceneNumericVariant
 */
UCLASS(Abstract, MinimalAPI)
class UMovieSceneNumericVariantGetter : public UMovieSceneSignedObject
{
public:

	GENERATED_BODY()

	/**
	 * Retrieve the value for this getter
	 */
	virtual double GetValue() const
	{
		return 0.0;
	}

public:

	/**
	 * Reference to self used to report this object to the reference graph inside FMovieSceneNumericVariant::AddStructReferencedObjects
	 */
	UPROPERTY(transient)
	TObjectPtr<UMovieSceneNumericVariantGetter> ReferenceToSelf;
};

