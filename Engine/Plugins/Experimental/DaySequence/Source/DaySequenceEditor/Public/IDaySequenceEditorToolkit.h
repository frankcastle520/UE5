// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Toolkits/AssetEditorToolkit.h"

class ISequencer;

/**
 * Implements an Editor toolkit for day sequences.
 */
class IDaySequenceEditorToolkit : public FAssetEditorToolkit
{
public:
	/** Access the sequencer that is displayed on this asset editor UI */
	virtual TSharedPtr<ISequencer> GetSequencer() const = 0;
};
