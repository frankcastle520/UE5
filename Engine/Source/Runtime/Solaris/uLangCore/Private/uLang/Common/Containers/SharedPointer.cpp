// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Common.h"

namespace uLang
{
CSharedMix::~CSharedMix()
{
    ULANG_ASSERTF(_RefCount == 0, "Shared pointer being destructed still has references!");
}
} // namespace uLang