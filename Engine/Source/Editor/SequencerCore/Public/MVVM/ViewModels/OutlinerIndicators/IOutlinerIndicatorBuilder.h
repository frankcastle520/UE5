// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

class SWidget;
class UMovieSceneSequence;

namespace UE::Sequencer
{

struct FCreateOutlinerColumnParams;
class IOutlinerColumn;
class ISequencerTreeViewRow;

/**
* Interface for building sequencer indicator outliner items.
*/
class IOutlinerIndicatorBuilder : public TSharedFromThis<IOutlinerIndicatorBuilder>
{

public:

	/** Returns the name of the indicator outliner item. */
	virtual FName GetIndicatorName() const = 0;

	/* Gets whether or not this indicator outliner item is supported by a given Sequencer. */
	virtual bool SupportsSequence(UMovieSceneSequence* InSequence) const { return true; }

	virtual bool IsItemCompatibleWithIndicator(const FCreateOutlinerColumnParams& InParams) const = 0;
	virtual TSharedPtr<SWidget> CreateIndicatorWidget(const FCreateOutlinerColumnParams& InParams, const TSharedRef<ISequencerTreeViewRow>& TreeViewRow, const TSharedRef<IOutlinerColumn>& OutlinerColumn, const int32 NumCompatibleIndicators) = 0;

public:

	/** Virtual destructor. */
	virtual ~IOutlinerIndicatorBuilder() { }

};

} // namespace UE::Sequencer

