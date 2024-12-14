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

#ifndef VERSE_HEAP_CHUNK_MAP_ENTRY_H
#define VERSE_HEAP_CHUNK_MAP_ENTRY_H

#include "pas_bitvector.h"
#include "pas_compact_tagged_atomic_ptr.h"
#include "pas_empty_mode.h"
#include "pas_page_kind.h"
#include "ue_include/verse_heap_config_ue.h"

#if PAS_ENABLE_VERSE

PAS_BEGIN_EXTERN_C;

struct pas_stream;
struct verse_heap_chunk_map_entry;
struct verse_heap_chunk_map_entry_header;
struct verse_heap_large_entry;
struct verse_heap_medium_page_header_object;
typedef struct pas_stream pas_stream;
typedef struct verse_heap_chunk_map_entry verse_heap_chunk_map_entry;
typedef struct verse_heap_chunk_map_entry_header verse_heap_chunk_map_entry_header;
typedef struct verse_heap_large_entry verse_heap_large_entry;
typedef struct verse_heap_medium_page_header_object verse_heap_medium_page_header_object;

#define VERSE_HEAP_CHUNK_MAP_ENTRY_NUM_WORDS \
    PAS_BITVECTOR_NUM_WORDS(VERSE_HEAP_SMALL_SEGREGATED_PAGES_PER_CHUNK)

struct verse_heap_chunk_map_entry {
    unsigned encoded_value[VERSE_HEAP_CHUNK_MAP_ENTRY_NUM_WORDS];
};

struct verse_heap_chunk_map_entry_header {
    unsigned encoded_value;
};

#define VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_IS_EMPTY_VALUE ((pas_compact_tagged_atomic_ptr_impl)0)
#define VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_IS_SMALL_SEGREGATED_BIT ((pas_compact_tagged_atomic_ptr_impl)1)
#define VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_IS_MEDIUM_SEGREGATED_BIT ((pas_compact_tagged_atomic_ptr_impl)2)
#define VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_MEDIUM_IS_NONEMPTY_BIT ((pas_compact_tagged_atomic_ptr_impl)4)
#define VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_NOT_LARGE_BITS \
    (VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_IS_SMALL_SEGREGATED_BIT \
     | VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_IS_MEDIUM_SEGREGATED_BIT)
#define VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_ALL_BITS \
    (VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_IS_SMALL_SEGREGATED_BIT \
     | VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_IS_MEDIUM_SEGREGATED_BIT \
     | VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_MEDIUM_IS_NONEMPTY_BIT)

static PAS_ALWAYS_INLINE verse_heap_chunk_map_entry_header
verse_heap_chunk_map_entry_get_header(verse_heap_chunk_map_entry entry)
{
    verse_heap_chunk_map_entry_header result;
    result.encoded_value = entry.encoded_value[0];
    return result;
}

static PAS_ALWAYS_INLINE verse_heap_chunk_map_entry_header
verse_heap_chunk_map_entry_load_header(verse_heap_chunk_map_entry* entry)
{
    verse_heap_chunk_map_entry_header result;
    result.encoded_value = entry->encoded_value[0];
    return result;
}

static PAS_ALWAYS_INLINE bool
verse_heap_chunk_map_entry_header_is_empty(verse_heap_chunk_map_entry_header header)
{
    return header.encoded_value == VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_IS_EMPTY_VALUE;
}

static PAS_ALWAYS_INLINE bool
verse_heap_chunk_map_entry_header_is_small_segregated(verse_heap_chunk_map_entry_header header)
{
    return !!(header.encoded_value & VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_IS_SMALL_SEGREGATED_BIT);
}

static PAS_ALWAYS_INLINE bool
verse_heap_chunk_map_entry_header_is_medium_segregated(verse_heap_chunk_map_entry_header header)
{
    return !!(header.encoded_value
              & VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_IS_MEDIUM_SEGREGATED_BIT)
        && !(header.encoded_value
             & VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_IS_SMALL_SEGREGATED_BIT);
}

static PAS_ALWAYS_INLINE bool
verse_heap_chunk_map_entry_header_is_large(verse_heap_chunk_map_entry_header header)
{
	return !(header.encoded_value & VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_NOT_LARGE_BITS)
        && header.encoded_value;
}

static PAS_ALWAYS_INLINE unsigned* verse_heap_chunk_map_entry_small_segregated_ownership_bitvector(
    verse_heap_chunk_map_entry* entry)
{
    PAS_TESTING_ASSERT(verse_heap_chunk_map_entry_header_is_small_segregated(
                           verse_heap_chunk_map_entry_load_header(entry)));
    return entry->encoded_value;
}

static PAS_ALWAYS_INLINE verse_heap_large_entry*
verse_heap_chunk_map_entry_header_large_entry(verse_heap_chunk_map_entry_header header)
{
    PAS_TESTING_ASSERT(verse_heap_chunk_map_entry_header_is_large(header));
    return (verse_heap_large_entry*)(header.encoded_value + pas_compact_heap_reservation_base);
}

static PAS_ALWAYS_INLINE verse_heap_medium_page_header_object*
verse_heap_chunk_map_entry_header_medium_segregated_header_object(verse_heap_chunk_map_entry_header header)
{
	PAS_TESTING_ASSERT(verse_heap_chunk_map_entry_header_is_medium_segregated(header));
	return (verse_heap_medium_page_header_object*)(
        (header.encoded_value & ~(VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_IS_MEDIUM_SEGREGATED_BIT |
                                  VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_MEDIUM_IS_NONEMPTY_BIT))
        + pas_compact_heap_reservation_base);
}

static PAS_ALWAYS_INLINE pas_empty_mode
verse_heap_chunk_map_entry_header_medium_segregated_empty_mode(verse_heap_chunk_map_entry_header header)
{
	PAS_TESTING_ASSERT(verse_heap_chunk_map_entry_header_is_medium_segregated(header));
	if ((header.encoded_value & VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_MEDIUM_IS_NONEMPTY_BIT))
		return pas_is_not_empty;
	return pas_is_empty;
}

static PAS_ALWAYS_INLINE void
verse_heap_chunk_map_entry_check_auxiliary_words_are_clear(verse_heap_chunk_map_entry* entry)
{
    size_t index;
    for (index = VERSE_HEAP_CHUNK_MAP_ENTRY_NUM_WORDS; index-- > 1;)
        PAS_ASSERT(!entry->encoded_value[index]);
}

static PAS_ALWAYS_INLINE void
verse_heap_chunk_map_entry_store_header(verse_heap_chunk_map_entry* entry,
                                        verse_heap_chunk_map_entry_header header)
{
    PAS_ASSERT(!verse_heap_chunk_map_entry_header_is_small_segregated(header));
    entry->encoded_value[0] = header.encoded_value;
    verse_heap_chunk_map_entry_check_auxiliary_words_are_clear(entry);
}

static PAS_ALWAYS_INLINE verse_heap_chunk_map_entry_header
verse_heap_chunk_map_entry_header_create_empty(void)
{
    verse_heap_chunk_map_entry_header result;
    result.encoded_value = 0;
    PAS_TESTING_ASSERT(verse_heap_chunk_map_entry_header_is_empty(result));
    PAS_TESTING_ASSERT(!verse_heap_chunk_map_entry_header_is_small_segregated(result));
    PAS_TESTING_ASSERT(!verse_heap_chunk_map_entry_header_is_medium_segregated(result));
    PAS_TESTING_ASSERT(!verse_heap_chunk_map_entry_header_is_large(result));
    return result;
}

static PAS_ALWAYS_INLINE verse_heap_chunk_map_entry_header
verse_heap_chunk_map_entry_header_create_large(verse_heap_large_entry* entry)
{
    verse_heap_chunk_map_entry_header result;
    uintptr_t offset;
    offset = (uintptr_t)entry - pas_compact_heap_reservation_base;
    PAS_ASSERT(entry);
    PAS_ASSERT(offset < pas_compact_heap_reservation_size);
	PAS_ASSERT(!(offset & VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_NOT_LARGE_BITS));
    result.encoded_value = offset;
    PAS_TESTING_ASSERT(verse_heap_chunk_map_entry_header_is_large(result));
    PAS_TESTING_ASSERT(!verse_heap_chunk_map_entry_header_is_empty(result));
    PAS_TESTING_ASSERT(!verse_heap_chunk_map_entry_header_is_small_segregated(result));
    PAS_TESTING_ASSERT(!verse_heap_chunk_map_entry_header_is_medium_segregated(result));
    PAS_TESTING_ASSERT(verse_heap_chunk_map_entry_header_large_entry(result) == entry);
    return result;
}

static PAS_ALWAYS_INLINE verse_heap_chunk_map_entry_header
verse_heap_chunk_map_entry_header_create_small_segregated(void)
{
    verse_heap_chunk_map_entry_header result;
    result.encoded_value = VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_IS_SMALL_SEGREGATED_BIT;
    PAS_TESTING_ASSERT(verse_heap_chunk_map_entry_header_is_small_segregated(result));
    PAS_TESTING_ASSERT(!verse_heap_chunk_map_entry_header_is_large(result));
    PAS_TESTING_ASSERT(!verse_heap_chunk_map_entry_header_is_medium_segregated(result));
    PAS_TESTING_ASSERT(!verse_heap_chunk_map_entry_header_is_empty(result));
    return result;
}

static PAS_ALWAYS_INLINE verse_heap_chunk_map_entry_header
verse_heap_chunk_map_entry_header_create_medium_segregated(
	verse_heap_medium_page_header_object* header, pas_empty_mode empty_mode)
{
    verse_heap_chunk_map_entry_header result;
	uintptr_t offset;
	offset = (uintptr_t)header - pas_compact_heap_reservation_base;
	PAS_ASSERT(header);
	PAS_ASSERT(offset < pas_compact_heap_reservation_size);
	PAS_ASSERT(!(offset & VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_NOT_LARGE_BITS));
    result.encoded_value = VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_IS_MEDIUM_SEGREGATED_BIT | offset;
	if (empty_mode == pas_is_not_empty)
		result.encoded_value |= VERSE_HEAP_CHUNK_MAP_ENTRY_HEADER_MEDIUM_IS_NONEMPTY_BIT;
    PAS_TESTING_ASSERT(verse_heap_chunk_map_entry_header_is_medium_segregated(result));
    PAS_TESTING_ASSERT(!verse_heap_chunk_map_entry_header_is_small_segregated(result));
    PAS_TESTING_ASSERT(!verse_heap_chunk_map_entry_header_is_large(result));
    PAS_TESTING_ASSERT(!verse_heap_chunk_map_entry_header_is_empty(result));
	PAS_TESTING_ASSERT(verse_heap_chunk_map_entry_header_medium_segregated_header_object(result) == header);
	PAS_TESTING_ASSERT(
        verse_heap_chunk_map_entry_header_medium_segregated_empty_mode(result) == empty_mode);
    return result;
}

/* You *cannot* use this to set entries in the small segregated bitvector. */
static PAS_ALWAYS_INLINE bool verse_heap_chunk_map_entry_weak_cas_header(
    verse_heap_chunk_map_entry* destination,
    verse_heap_chunk_map_entry_header expected, verse_heap_chunk_map_entry_header new_value)
{
    /* Something is very wrong if we're CASing the header away from small. */
    PAS_ASSERT(!verse_heap_chunk_map_entry_header_is_small_segregated(expected));
    if (pas_compare_and_swap_uint32_weak(
            destination->encoded_value, expected.encoded_value, new_value.encoded_value)) {
        if (!verse_heap_chunk_map_entry_header_is_small_segregated(new_value)) {
            /* If we CAS to something other than small_segregated, then there is no way it could
               have ever been small_segragated, now or in the future.
               
               Why? Because the empty->small_segregated transition is permanent.
               
               Therefore, if we CAS to something other than small_segregated, then the aux words
               must be empty. */
            verse_heap_chunk_map_entry_check_auxiliary_words_are_clear(destination);
        }
    }
    return false;
}

PAS_API void verse_heap_chunk_map_entry_header_dump(verse_heap_chunk_map_entry_header header,
                                                    pas_stream* stream);
PAS_API void verse_heap_chunk_map_entry_dump(verse_heap_chunk_map_entry entry, pas_stream* stream);

PAS_END_EXTERN_C;

#endif /* PAS_ENABLE_VERSE */

#endif /* VERSE_HEAP_CHUNK_MAP_ENTRY_H */

