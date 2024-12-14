// Copyright Epic Games, Inc. All Rights Reserved.


#include "Modules/ModuleManager.h"

/**
* The public interface of the DatasmithImporter module
*/
class FEditorScriptingUtilitiesModule : public IModuleInterface
{
	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};


IMPLEMENT_MODULE(FEditorScriptingUtilitiesModule, EditorScriptingUtilities)
