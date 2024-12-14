// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/Common/Containers/SharedPointer.h"

namespace uLang
{

class ILibLoader : public CSharedMix
{
public:
    using DyLibHandle  = uintptr_t;
    using DyLibProcPtr = uintptr_t;

    static const DyLibHandle InvalidDyLibHandle = 0x00;

    virtual DyLibHandle  LoadLibrary(const char* LibName) = 0;
    virtual void         AddLibSearchPath(const char* DirPath) = 0;
    virtual DyLibProcPtr FindProcExport(DyLibHandle LibHandle, const char* ProcName) = 0;

    virtual ~ILibLoader() {}
};

} // namespace uLang
