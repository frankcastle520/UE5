// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc_helper/memory/ref_count_ptr.h"
#include "epic_rtc/core/ref_count.h"

#include <cassert>
#include <cstdint>
#include <utility>
#include <type_traits>

#include <iostream>

namespace EpicRtc
{
    // TODO (aidan.possemiers) replace auto with something that ensures at compile time that
    // what is passed to Members is derived from EpicRtcRefCountInterface
    template <typename ElementType, auto ElementType::*... Members>
    class RefCountedMembersWrapper
    {
    public:
        RefCountedMembersWrapper(ElementType inInstance)
            : _instance(inInstance)
        {
            ([&]
                { 
                    if (_instance.*Members)
                    {(_instance.*Members)->AddRef(); } }(),
                ...);
        }

        RefCountedMembersWrapper(RefCountedMembersWrapper& other)
            : _instance(other._instance)
        {
            ([&]
                { 
                    if (_instance.*Members)
                    {(_instance.*Members)->AddRef(); } }(),
                ...);
        }

        RefCountedMembersWrapper(RefCountedMembersWrapper&& other)
            : _instance(other._instance)
        {
            ([&]
                { other._instance.*Members = nullptr; }(),
                ...);
        }

        ~RefCountedMembersWrapper()
        {
            ([&]
                {
                    if (_instance.*Members)
                    { (_instance.*Members)->Release(); } }(),
                ...);
        }

        const ElementType& Get()
        {
            return _instance;
        }

    private:
        ElementType _instance;
    };
}  // namespace EpicRtc
