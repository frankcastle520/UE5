/*
 * Copyright (c) 2023 Epic Games, Inc. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY EPIC GAMES, INC. ``AS IS AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL EPIC GAMES, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#ifndef PAS_LOCAL_ALLOCATOR_UE_H
#define PAS_LOCAL_ALLOCATOR_UE_H

#include "pas_bitfit_allocator_ue.h"
#include "pas_bitvector_ue.h"
#include "pas_local_allocator_scavenger_data_ue.h"

#ifdef __cplusplus
extern "C" {
#endif

struct pas_fake_local_allocator;
struct pas_local_allocator;
typedef struct pas_fake_local_allocator pas_fake_local_allocator;
typedef struct pas_local_allocator pas_local_allocator;

/* This is only defined so that we can do offsetof on it. The fields must be matched exactly to pas_local_allocator, otherwise a bunch of runtime asserts will fail. */
struct pas_fake_local_allocator {
    pas_local_allocator_scavenger_data scavenger_data;
    
    uint8_t alignment_shift;
    enum {
        pas_fake_local_allocator_fake,
        pas_fake_local_allocator_stuff
    } config_kind : 8;
    bool current_word_is_valid;

    uintptr_t payload_end;
    unsigned remaining;
    unsigned object_size;
    uintptr_t page_ish;
    unsigned current_offset;
    unsigned end_offset;
    uint64_t current_word;
    void* view;
    uint64_t bits[1];
};

#define PAS_LOCAL_ALLOCATOR_ALIGNMENT 8

#define PAS_FAKE_LOCAL_ALLOCATOR_SIZE(num_alloc_bits) \
    (((uintptr_t)&((pas_fake_local_allocator*)0x1000)->bits - 0x1000) + \
     ((sizeof(uint64_t) * PAS_BITVECTOR_NUM_WORDS64(num_alloc_bits) > sizeof(pas_bitfit_allocator)) \
      ? sizeof(uint64_t) * PAS_BITVECTOR_NUM_WORDS64(num_alloc_bits) : sizeof(pas_bitfit_allocator)))

#ifdef __cplusplus
}
#endif

#endif /* PAS_LOCAL_ALLOCATOR_H */

