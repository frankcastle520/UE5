// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"

class FSVGImporterEditorCommands : public TCommands<FSVGImporterEditorCommands>
{
public:
	SVGIMPORTEREDITOR_API static const FSVGImporterEditorCommands& GetExternal();

	static const FSVGImporterEditorCommands& GetInternal();

	FSVGImporterEditorCommands();

	//~ Begin TCommands
	virtual void RegisterCommands() override;
	//~ End TCommands

	TSharedPtr<FUICommandInfo> SpawnSVGActor;

private:
	// Make this unavailable to the public
	using TCommands<FSVGImporterEditorCommands>::Get;
};
