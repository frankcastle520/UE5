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

#include "pas_config.h"

#if LIBPAS_ENABLED

#include "verse_heap_chunk_map.h"

#include "pas_heap_lock.h"
#include "pas_reservation_free_heap.h"
#include "pas_reservation.h"

#if PAS_ENABLE_VERSE

verse_heap_chunk_map_entry* verse_heap_first_level_chunk_map[VERSE_HEAP_CHUNK_MAP_FIRST_LEVEL_SIZE];

verse_heap_chunk_map_entry* verse_heap_initialize_chunk_map_entry_ptr(uintptr_t address)
{
    verse_heap_chunk_map_entry** second_level_ptr;

    pas_heap_lock_assert_held();

    PAS_ASSERT(address <= PAS_MAX_ADDRESS);

    second_level_ptr = verse_heap_first_level_chunk_map + 
        ((address >> VERSE_HEAP_CHUNK_MAP_FIRST_LEVEL_SHIFT) & VERSE_HEAP_CHUNK_MAP_FIRST_LEVEL_MASK);

    if (!*second_level_ptr) {
        /* We allocate from our reservation because that way we know we'll get all zeroes. */
        pas_allocation_result allocation_result;
        size_t size;
        size = sizeof(verse_heap_chunk_map_entry) * VERSE_HEAP_CHUNK_MAP_SECOND_LEVEL_SIZE;
        allocation_result = pas_reservation_free_heap_allocate_with_alignment(
			size, pas_alignment_create_traditional(sizeof(verse_heap_chunk_map_entry)), "verse_heap_chunk_map/second_level", pas_object_allocation);
        PAS_ASSERT(allocation_result.did_succeed);
        PAS_ASSERT(allocation_result.zero_mode == pas_zero_mode_is_all_zero);
        pas_reservation_commit((void*)allocation_result.begin, size);
        *second_level_ptr = (verse_heap_chunk_map_entry*)allocation_result.begin;
    }

    return (*second_level_ptr) + ((address >> VERSE_HEAP_CHUNK_MAP_SECOND_LEVEL_SHIFT)
                                  & VERSE_HEAP_CHUNK_MAP_SECOND_LEVEL_MASK);
}

#endif /* PAS_ENABLE_VERSE */

#endif /* LIBPAS_ENABLED */

