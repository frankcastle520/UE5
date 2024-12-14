// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Text/UTF8StringView.h"
#include "uLang/Toolchain/ILibLoader.h"
#include "uLang/Toolchain/IToolchainPlugin.h"
#include "uLang/Common/Containers/UniquePointerArray.h"

namespace uLang
{

/**
 * Utility for loading Verse specific dy-libs. Expects that targeted libraries
 * implement IToolchainPlugin using the ULANG_TOOLCHAIN_PLUGIN_CLASS() macro.
 */
class CToolchainPluginManager : public CSharedMix
{
public:
    CToolchainPluginManager(const TSRef<ILibLoader>& InLibLoader)
        : _LibLoader(InLibLoader)
    {}

    /**
     * Attempts to load, initialize, and spawn an interface for the specified library.
     * The targeted library is expected to have employed the ULANG_TOOLCHAIN_PLUGIN_CLASS()
     * macro, and be built with the same matching ULANG_API_VERSION.
     *
     * @param  LibName  The library file name to load (including the file extension).
     * @return Null if the plugin failed to load (or failed to initialize).
     */
    VERSECOMPILER_API IToolchainPlugin* LoadPluginLib(const char* LibName);

private:
    struct SPluginInfo
    {
    public:
        SPluginInfo(ILibLoader::DyLibHandle InHandle = ILibLoader::InvalidDyLibHandle, IToolchainPlugin* InLibInterface = nullptr)
            : LibHandle(InHandle)
            , PluginInterface(InLibInterface)
        {
        }

        ~SPluginInfo()
        {
            if (PluginInterface)
            {
                PluginInterface->OnUnLoad();
                CHeapRawAllocator::Deallocate(PluginInterface);
            }
        }

        ILibLoader::DyLibHandle LibHandle;
        IToolchainPlugin* PluginInterface;
    };

    TSRef<ILibLoader> _LibLoader;
    TURefArray<SPluginInfo> _LoadedLibs;
};

} // namespace uLang
