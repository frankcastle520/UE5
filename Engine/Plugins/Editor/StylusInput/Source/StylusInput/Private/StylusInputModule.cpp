// Copyright Epic Games, Inc. All Rights Reserved.

#include <Modules/ModuleManager.h>

namespace UE::StylusInput::Private
{
	class FStylusInputModule : public IModuleInterface
	{
	};
}

IMPLEMENT_MODULE(UE::StylusInput::Private::FStylusInputModule, StylusInput);
