// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Toolchain/ToolchainPluginManager.h"

namespace uLang
{
class CLibAllocator : public ::uLang::CAllocatorInstance
{
public:
    CLibAllocator()
        : CAllocatorInstance(&Allocate, &Reallocate, &Deallocate)
    {}

private:
    static void* Allocate(const CAllocatorInstance* /*This*/, size_t NumBytes)
    {
        return CHeapRawAllocator::Allocate(NumBytes);
    }

    static void* Reallocate(const CAllocatorInstance* /*This*/, void* Memory, size_t NumBytes)
    {
        return CHeapRawAllocator::Reallocate(Memory, NumBytes);
    }

    static void Deallocate(const CAllocatorInstance* /*This*/, void* Memory)
    {
        CHeapRawAllocator::Deallocate(Memory);
    }
};

IToolchainPlugin* CToolchainPluginManager::LoadPluginLib(const char* LibName)
{
    ILibLoader::DyLibHandle LibHandle = _LibLoader->LoadLibrary(LibName);

    IToolchainPlugin* LibInterface = nullptr;
    if (ULANG_ENSUREF(LibHandle != ILibLoader::InvalidDyLibHandle, "Failed to load target library: %s", LibName))
    {
        SPluginInfo* ExistingDyLib = _LoadedLibs.FindByPredicate([LibHandle](const SPluginInfo* DyLib)->bool
        {
            return (LibHandle == DyLib->LibHandle);
        });
        if (ExistingDyLib)
        {
            LibInterface = ExistingDyLib->PluginInterface;
        }
        else
        {
            const char* GetVerProcName = ULANG_STRINGIFY(ULANG_PLUGIN_GETVER_PROCNAME);
            ::uLang::Private::ToolchainPluginGetVerPtr GetVerPtr = (::uLang::Private::ToolchainPluginGetVerPtr)_LibLoader->FindProcExport(LibHandle, GetVerProcName);

            if (ULANG_ENSUREF(GetVerPtr, "Failed to find the expected version getter (%s), within the '%s' library.", GetVerProcName, LibName))
            {
                const int32_t LibVersion = GetVerPtr();
                if (ULANG_ENSUREF(LibVersion == ULANG_API_VERSION,
                    "Mismatched API version -- %s lib (v%d) needs to be rebuilt with an updated core version (expected: v%d).",
                    LibName,
                    LibVersion,
                    ULANG_API_VERSION))
                {
                    const char* InitProcName = ULANG_STRINGIFY(ULANG_PLUGIN_INIT_PROCNAME);

                    ILibLoader::DyLibProcPtr InitProcPtr = _LibLoader->FindProcExport(LibHandle, InitProcName);
                    if (ULANG_ENSUREF(InitProcPtr, "Failed to find expected entry point (%s), within the '%s' library.", InitProcName, LibName))
                    {
                        ULANG_ASSERTF(IsInitialized(), "Core should be properly initialized before loading any supplamentary libs.");

                        CLibAllocator Allocator;
                        Private::SToolchainPluginParams PluginParams(Allocator);

                        LibInterface = (::uLang::Private::ToolchainPluginInitPtr(InitProcPtr))(PluginParams);

                        if (ULANG_ENSUREF(LibInterface, "Library failed to produce the expected interface."))
                        {
                            LibInterface->OnLoad();
                        }
                    }
                }
            }

            _LoadedLibs.AddNew(LibHandle, LibInterface);
        }
    }

    return LibInterface;
}

} // namespace uLang
