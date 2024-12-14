// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/UniquePtr.h"

class UTextureGraph;

struct FTG_Exporter
{
	// Initializes the global state of the exporter (commands, tab spawners, etc):
	FTG_Exporter();

	// Destructor declaration purely so that we can pimpl:
	~FTG_Exporter();

	/** Sets the current texture graph to be used with the exporter */
	void SetTextureGraphToExport(UTextureGraph* InTextureGraph);

private:
	TUniquePtr< struct FTG_ExporterImpl > Impl;

	// prevent copying:
	FTG_Exporter(const FTG_Exporter&);
	FTG_Exporter(FTG_Exporter&&);
	FTG_Exporter& operator=(FTG_Exporter const&);
	FTG_Exporter& operator=(FTG_Exporter&&);
};

