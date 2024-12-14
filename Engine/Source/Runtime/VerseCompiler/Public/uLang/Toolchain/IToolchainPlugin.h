// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"

namespace uLang
{

class IToolchainPlugin
{
public:
    virtual void OnLoad() = 0;
    virtual void OnUnLoad() = 0;
};

} // namespace uLang

#include "IToolchainPlugin.inl"

#define ULANG_TOOLCHAIN_PLUGIN_CLASS(LibName, DyLibInterface) \
    extern "C" ULANG_DLLEXPORT int32_t ULANG_PLUGIN_GETVER_PROCNAME() { return ULANG_API_VERSION; } \
    extern "C" ULANG_DLLEXPORT ::uLang::IToolchainPlugin* ULANG_PLUGIN_INIT_PROCNAME(const ::uLang::Private::SToolchainPluginParams& Params) \
    { \
        return ::uLang::Private::InitVToolchainPlugin<DyLibInterface>(Params, #LibName); \
    }
#define ULANG_PLUGIN_INIT_PROCNAME _InitVToolchainPlugin___
#define ULANG_PLUGIN_GETVER_PROCNAME _GetVToolchainPluginVer___
