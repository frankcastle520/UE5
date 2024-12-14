/*
 * Copyright (c) 2023-2024 Epic Games, Inc. All Rights Reserved.
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
 * THIS SOFTWARE IS PROVIDED BY EPIC GAMES, INC. ``AS IS'' AND ANY
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

#ifndef VERSE_HEAP_CHUNK_MAP_H
#define VERSE_HEAP_CHUNK_MAP_H

#include "pas_utils.h"
#include "verse_heap_chunk_map_entry.h"

#if PAS_ENABLE_VERSE

PAS_BEGIN_EXTERN_C;

#define VERSE_HEAP_CHUNK_MAP_FIRST_LEVEL_BITS ((PAS_ADDRESS_BITS - VERSE_HEAP_CHUNK_SIZE_SHIFT) >> 1u)
#define VERSE_HEAP_CHUNK_MAP_FIRST_LEVEL_MASK (((uintptr_t)1 << VERSE_HEAP_CHUNK_MAP_FIRST_LEVEL_BITS) - 1)
#define VERSE_HEAP_CHUNK_MAP_FIRST_LEVEL_SIZE (1u << VERSE_HEAP_CHUNK_MAP_FIRST_LEVEL_BITS)
#define VERSE_HEAP_CHUNK_MAP_SECOND_LEVEL_BITS ((PAS_ADDRESS_BITS - VERSE_HEAP_CHUNK_SIZE_SHIFT + 1) >> 1u)
#define VERSE_HEAP_CHUNK_MAP_SECOND_LEVEL_MASK (((uintptr_t)1 << VERSE_HEAP_CHUNK_MAP_SECOND_LEVEL_BITS) - 1)
#define VERSE_HEAP_CHUNK_MAP_SECOND_LEVEL_SIZE (1u << VERSE_HEAP_CHUNK_MAP_SECOND_LEVEL_BITS)

#define VERSE_HEAP_CHUNK_MAP_SECOND_LEVEL_SHIFT VERSE_HEAP_CHUNK_SIZE_SHIFT
#define VERSE_HEAP_CHUNK_MAP_FIRST_LEVEL_SHIFT \
    (VERSE_HEAP_CHUNK_MAP_SECOND_LEVEL_SHIFT + VERSE_HEAP_CHUNK_MAP_SECOND_LEVEL_BITS)

PAS_API extern verse_heap_chunk_map_entry* verse_heap_first_level_chunk_map[
    VERSE_HEAP_CHUNK_MAP_FIRST_LEVEL_SIZE];

/* Check the chunk map entry for a chunk; if we know nothing about a chunk then we will return an empty
   chunk map entry. */
static PAS_ALWAYS_INLINE verse_heap_chunk_map_entry_header
verse_heap_get_chunk_map_entry_header(uintptr_t address)
{
    verse_heap_chunk_map_entry* second_level;

    if (address > PAS_MAX_ADDRESS)
        return verse_heap_chunk_map_entry_header_create_empty();

    /* FIXME: Do we need the mask here? */
    second_level = verse_heap_first_level_chunk_map[
        (address >> VERSE_HEAP_CHUNK_MAP_FIRST_LEVEL_SHIFT) & VERSE_HEAP_CHUNK_MAP_FIRST_LEVEL_MASK];
    if (!second_level)
        return verse_heap_chunk_map_entry_header_create_empty();

    return verse_heap_chunk_map_entry_load_header(
        second_level + ((address >> VERSE_HEAP_CHUNK_MAP_SECOND_LEVEL_SHIFT)
                        & VERSE_HEAP_CHUNK_MAP_SECOND_LEVEL_MASK));
}

/* Get a pointer to a chunk map entry. This assumes that the chunk map entry must exist. It may crash or
   do weird stuff if it doesn't. */
static PAS_ALWAYS_INLINE verse_heap_chunk_map_entry* verse_heap_get_chunk_map_entry_ptr(uintptr_t address)
{
    verse_heap_chunk_map_entry* second_level;

    PAS_ASSERT(address <= PAS_MAX_ADDRESS);

    second_level = verse_heap_first_level_chunk_map[
        (address >> VERSE_HEAP_CHUNK_MAP_FIRST_LEVEL_SHIFT) & VERSE_HEAP_CHUNK_MAP_FIRST_LEVEL_MASK];
    PAS_TESTING_ASSERT(second_level);

    return second_level + ((address >> VERSE_HEAP_CHUNK_MAP_SECOND_LEVEL_SHIFT)
                           & VERSE_HEAP_CHUNK_MAP_SECOND_LEVEL_MASK);
}

/* This may allocate a second-level chunk map on-demand if needed. Requires holding the heap lock. May
   return an existing chunk map entry if one did not exist before. If one did not exist before then the
   entry will already be zero. Hence a valid idiom for using this is to ignore the return value. */
PAS_API verse_heap_chunk_map_entry* verse_heap_initialize_chunk_map_entry_ptr(uintptr_t address);

PAS_END_EXTERN_C;

#endif /* PAS_ENABLE_VERSE */

#endif /* VERSE_HEAP_CHUNK_MAP_H */

