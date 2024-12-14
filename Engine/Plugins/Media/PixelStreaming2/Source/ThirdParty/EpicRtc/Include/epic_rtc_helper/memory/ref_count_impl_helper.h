// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <atomic>

#include "epic_rtc_helper/memory/ref_count_ptr.h"

// NOLINTBEGIN

#define EPICRTC_REFCOUNT_INTERFACE                                           \
public:                                                                      \
    template <typename T, typename... Args>                                  \
    friend EpicRtc::RefCountPtr<T> EpicRtc::MakeRefCountPtr(Args&&... args); \
    virtual uint32_t AddRef() override;                                      \
    virtual uint32_t Release() override;                                     \
    virtual uint32_t Count() const override;                                 \
                                                                             \
private:                                                                     \
    std::atomic<uint32_t> _refCount{ 0 };

#define EPICRTC_REFCOUNT_FUNCTIONS(ClassName)                                        \
    uint32_t ClassName::AddRef()                                                     \
    {                                                                                \
        return _refCount.fetch_add(1, std::memory_order_relaxed) + 1;                \
    }                                                                                \
    uint32_t ClassName::Release()                                                    \
    {                                                                                \
        uint32_t original_count = _refCount.fetch_sub(1, std::memory_order_acq_rel); \
        if (original_count == 1)                                                     \
        {                                                                            \
            delete this;                                                             \
        }                                                                            \
        else                                                                         \
        {                                                                            \
            assert(original_count > 0);                                              \
        }                                                                            \
        return original_count - 1;                                                   \
    }                                                                                \
    uint32_t ClassName::Count() const                                                \
    {                                                                                \
        return _refCount;                                                            \
    }

#define EPICRTC_REFCOUNT_INTERFACE_IN_PLACE                                          \
public:                                                                              \
    template <typename T, typename... Args>                                          \
    friend EpicRtc::RefCountPtr<T> EpicRtc::MakeRefCountPtr(Args&&... args);         \
    virtual uint32_t AddRef() override                                               \
    {                                                                                \
        return _refCount.fetch_add(1, std::memory_order_relaxed) + 1;                \
    }                                                                                \
    virtual uint32_t Release() override                                              \
    {                                                                                \
        uint32_t original_count = _refCount.fetch_sub(1, std::memory_order_acq_rel); \
        if (original_count == 1)                                                     \
        {                                                                            \
            delete this;                                                             \
        }                                                                            \
        else                                                                         \
        {                                                                            \
            assert(original_count > 0);                                              \
        }                                                                            \
        return original_count - 1;                                                   \
    }                                                                                \
    virtual uint32_t Count() const override                                          \
    {                                                                                \
        return _refCount;                                                            \
    }                                                                                \
                                                                                     \
private:                                                                             \
    std::atomic<uint32_t> _refCount{ 0 };

// NOLINTEND
