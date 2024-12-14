// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Toolchain/ModularFeatureManager.h" // for IModularFeatureRegistry
#include "uLang/Toolchain/CommandLine.h"

namespace uLang
{
namespace Private
{
    struct SToolchainPluginParams
    {
        SToolchainPluginParams(CAllocatorInstance& InAllocator)
            : _SysParams(uLang::GetSystemParams())
            , _Allocator(InAllocator)
            , _PluginRegistry(CModularFeatureRegistrar::GetRegistry())
            , _CommandLine(CommandLine::Get())
        {}

        const SSystemParams& _SysParams;
        CAllocatorInstance&  _Allocator;
        TSRef<IModularFeatureRegistry> _PluginRegistry;
        const SCommandLine& _CommandLine;
    };

    template<typename DyLibClass>
    DyLibClass* InitVToolchainPlugin(const SToolchainPluginParams& Params, const char* LibName)
    {
        DyLibClass* DyLibInstance = nullptr;
        if (Params._SysParams._APIVersion == ULANG_API_VERSION)
        {
            if (!::uLang::IsInitialized())
            {
                ::uLang::Initialize(Params._SysParams);
                CommandLine::Init(Params._CommandLine);
            }
            else
            {
                ULANG_ASSERTF(::uLang::GetSystemParams() == Params._SysParams, "Library (%s) already initialized w/ incompatible core settings.", LibName);
            }
            CModularFeatureRegistrar::SetRegistry(Params._PluginRegistry);

            DyLibInstance = new (::uLang::CInstancedRawAllocator(&Params._Allocator))DyLibClass();
        }
        else
        {
            ULANG_ERRORF("Mismatched API version -- %s lib (v%d) needs to be rebuilt with an updated core version (expected: v%d).", LibName, ULANG_API_VERSION, Params._SysParams._APIVersion);
        }
        return DyLibInstance;
    }

    using ToolchainPluginGetVerPtr = int32_t(*)();
    using ToolchainPluginInitPtr = ::uLang::IToolchainPlugin*(*)(const SToolchainPluginParams&);

} // namespace Private
} // namespace uLang
