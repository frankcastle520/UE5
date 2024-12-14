// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ICoreUObjectPluginManager.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"

namespace UE::CoreUObject::Private
{
	class PluginHandler : public UE::PluginManager::Private::ICoreUObjectPluginManager
	{
	public:
		static void Install();

		virtual void OnPluginUnload(IPlugin& Plugin) override;

		virtual void SuppressPluginUnloadGC() override;
		virtual void ResumePluginUnloadGC() override;

		virtual ~PluginHandler() {}

	private:

		TArray<FString> DeferredPluginsToGC;

		/** Ref count for deferring calls to OnPluginUnload. When the ref count reaches 0 we GC and leak test all deferred plugins */
		int32 SuppressGCRefCount = 0;
	};
}