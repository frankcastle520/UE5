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

#ifndef VERSE_HEAP_INLINES_H
#define VERSE_HEAP_INLINES_H

#include "pas_fd_stream.h"
#include "pas_object_kind.h"
#include "pas_segregated_page_config_inlines.h"
#include "verse_heap.h"
#include "verse_heap_chunk_map_entry.h"

#if PAS_ENABLE_VERSE

PAS_BEGIN_EXTERN_C;

/* Note that this is totally racy, meaning that if you perform this query for an object that is currently being
   allocated, you might find that it's allocated or you might not.

   It's straightforward to make that OK: just make sure that you mark the object using fenced CAS. You only need
   to use a fenced CAS if you're unsure if the object is allocated (conservative marking). If you know that it is
   allocated (interior pointer marking), then you can use unfenced CAS.

   The bad cases we want to avoid are:
       B1) We see that a newly allocated object is indeed allocated, but think that it's white, and then proceed
           to mark it, causing it to become grey. We don't want newly allocated objects to be grey, since they
           may not be ready for tracing.
       B2) We think that an object that wasn't allocated is actually allocated and then mark it.
       B3) Some object that was allocated before the GC cycle is not recognized as being allocated.

   B3 is a non-concern; this algorithm is designed in such a way that a race on the allocation state of one object
   cannot possibly affect a query for a different object. B1 and B2 are similar, in the sense that we could have just
   "allocated" the object from libpas's standpoint but haven't constructed it. This algorithm - and the way we
   handle the race - ensure that those objects appear black. The outcomes we want are that:

       G1) Objects that were allocated before the start of GC cycle are reported as allocated and the mark state
           reflects whether this GC cycle marked them.
       G2) Objects allocated in this allocation cycle appear black (they are marked already).
       G3) Totally not allocated objects are ignored.

   So, we want G2 or G3 for all races between allocation and this algorithm.

   Here's a reason why that works, and why you must use a fenced CAS to attempt marking after this runs. Newly
   allocated objects will be marked before they appear allocated. The racing threads will perform these actions:

   Allocate:
       A1. Mark black, either with a fenced atomic op, or with a store barrier right after.
       A2. Change the alloc data structures that this function reads, under lock, but using relaxed ops.

   This query:
       Q1. Query the alloc data structures using this function with no locking using relaxed ops.
       Q2. Attempt to mark grey using a fenced atomic op.

   A2 happens after A1 because either there will be a store barrier between them (if A1 uses relaxed ops) or because
   A1 uses atomic ops. Q2 happens after Q1 because Q2 uses atomic ops.

   Therefore, the possible interleavings are:

   A1 A2 Q1 Q2    Object allocated and already marked, nothing happens (G2).
   A1 Q1 A2 Q2    Object not allocated, nothing happens (G3).
   A1 Q1 Q2 A2    Object not allocated, nothing happens (G3).
   Q1 A1 A2 Q2    Object not allocated, nothing happens (G3).
   Q1 A1 Q2 A2    Object not allocated, nothing happens (G3).
   Q1 Q2 A1 A2    Object not allocated, nothing happens (G3).

   Note that if we failed to fence, we could have Q2 happen before Q1 and then we'd have:

   A1 A2 Q2 Q1    Object allocated and already marked, nothing happens (G2).
   A1 Q2 A2 Q1    Object already marked, nothing happens (G2).
   A1 Q2 Q1 A2    Object already marked, nothing happens (G2).
   Q2 A1 A2 Q1    Marking sees white and marks, query sees an allocated object (B1).
   Q2 A1 Q1 A2    Marking sees white and would mark, but query sees a dead object (G3).
   Q2 Q1 A1 A2    Marking sees white and would mark, but query sees a dead object (G3).

   So, make sure you fence your marking after calling this, or use a load-load fence before unfenced marking. */
static PAS_ALWAYS_INLINE uintptr_t verse_heap_find_allocated_object_start_inline(uintptr_t inner_ptr)
{
    static const bool verbose = false;
    
    verse_heap_chunk_map_entry_header entry_header;
    verse_heap_large_entry* large_entry;

    entry_header = verse_heap_get_chunk_map_entry_header(inner_ptr);

    if (verbose) {
        pas_log("entry_header = ");
        verse_heap_chunk_map_entry_header_dump(entry_header, &pas_log_stream.base);
        pas_log("\n");
    }

    if (verse_heap_chunk_map_entry_header_is_empty(entry_header))
        return 0;

    if (PAS_LIKELY(verse_heap_chunk_map_entry_header_is_small_segregated(entry_header))) {
        unsigned* bitvector;
        size_t index;

        bitvector = verse_heap_chunk_map_entry_small_segregated_ownership_bitvector(
            verse_heap_get_chunk_map_entry_ptr(inner_ptr));

        index = pas_modulo_power_of_2(inner_ptr, VERSE_HEAP_CHUNK_SIZE)
            / VERSE_HEAP_SMALL_SEGREGATED_PAGE_SIZE;
        PAS_TESTING_ASSERT(index < VERSE_HEAP_SMALL_SEGREGATED_PAGES_PER_CHUNK);

        if (verbose)
            pas_log("index = %zu\n", index);

        if (!index)
            return 0;
        
        if (!pas_bitvector_get(bitvector, index))
            return 0;

        if (verbose)
            pas_log("Calling pas_segregated_page_try_find_allocated_object_start\n");
        
        return pas_segregated_page_try_find_allocated_object_start(
            inner_ptr, VERSE_HEAP_CONFIG.small_segregated_config, pas_segregated_page_exclusive_role);
    }

    if (verse_heap_chunk_map_entry_header_is_medium_segregated(entry_header)) {
		if (verse_heap_chunk_map_entry_header_medium_segregated_empty_mode(entry_header) == pas_is_empty) {
			/* This means that the page might be decommitted at any time. The scavenger runs concurrently
               to marking, including conservative marking! */
			return 0;
		}
        return pas_segregated_page_try_find_allocated_object_start_with_page(
            &verse_heap_chunk_map_entry_header_medium_segregated_header_object(entry_header)->segregated,
			inner_ptr, VERSE_HEAP_CONFIG.medium_segregated_config, pas_segregated_page_exclusive_role);
    }

    PAS_TESTING_ASSERT(verse_heap_chunk_map_entry_header_is_large(entry_header));
    
    /* NOTE: This handles allocation races correctly, but not deallocation races. Fortunately,
       we only deallocate during times when we aren't marking, and we only try to find the allocated
       object start during marking. */
    
    large_entry = verse_heap_chunk_map_entry_header_large_entry(entry_header);
    if (inner_ptr < large_entry->begin)
        return 0;
    if (inner_ptr >= large_entry->end)
        return 0;
    return large_entry->begin;
}

static PAS_ALWAYS_INLINE pas_heap* verse_heap_get_heap_inline(uintptr_t inner_ptr)
{
    verse_heap_chunk_map_entry_header entry_header;
    pas_segregated_page* page;

    entry_header = verse_heap_get_chunk_map_entry_header(inner_ptr);

    if (verse_heap_chunk_map_entry_header_is_empty(entry_header))
        return NULL;

    if (verse_heap_chunk_map_entry_header_is_large(entry_header))
        return verse_heap_chunk_map_entry_header_large_entry(entry_header)->heap;

    if (verse_heap_chunk_map_entry_header_is_small_segregated(entry_header)) {
        page = pas_segregated_page_for_address_and_page_config(
            inner_ptr, VERSE_HEAP_CONFIG.small_segregated_config);
    } else {
        PAS_ASSERT(verse_heap_chunk_map_entry_header_is_medium_segregated(entry_header));
        page = pas_segregated_page_for_address_and_page_config(
            inner_ptr, VERSE_HEAP_CONFIG.medium_segregated_config);
    }

    return pas_heap_for_segregated_heap(pas_segregated_view_get_size_directory(page->owner)->heap);
}

static PAS_ALWAYS_INLINE size_t verse_heap_get_allocation_size_inline(uintptr_t inner_ptr)
{
    verse_heap_chunk_map_entry_header entry_header;
    pas_segregated_page* page;

    entry_header = verse_heap_get_chunk_map_entry_header(inner_ptr);

    if (verse_heap_chunk_map_entry_header_is_empty(entry_header))
        return 0;

    if (verse_heap_chunk_map_entry_header_is_large(entry_header)) {
        verse_heap_large_entry* large_entry;
        large_entry = verse_heap_chunk_map_entry_header_large_entry(entry_header);
        return large_entry->end - large_entry->begin;
    }

    if (verse_heap_chunk_map_entry_header_is_small_segregated(entry_header)) {
        page = pas_segregated_page_for_address_and_page_config(
            inner_ptr, VERSE_HEAP_CONFIG.small_segregated_config);
    } else {
        PAS_ASSERT(verse_heap_chunk_map_entry_header_is_medium_segregated(entry_header));
        page = pas_segregated_page_for_address_and_page_config(
            inner_ptr, VERSE_HEAP_CONFIG.medium_segregated_config);
    }

    return page->object_size;
}

static PAS_ALWAYS_INLINE pas_segregated_page* verse_heap_get_segregated_page(uintptr_t inner_ptr)
{
    verse_heap_chunk_map_entry_header entry_header;

    entry_header = verse_heap_get_chunk_map_entry_header(inner_ptr);

    if (verse_heap_chunk_map_entry_header_is_empty(entry_header))
        return NULL;

    if (verse_heap_chunk_map_entry_header_is_large(entry_header))
        return NULL;

    if (verse_heap_chunk_map_entry_header_is_small_segregated(entry_header)) {
        return pas_segregated_page_for_address_and_page_config(
            inner_ptr, VERSE_HEAP_CONFIG.small_segregated_config);
    }
    
    PAS_ASSERT(verse_heap_chunk_map_entry_header_is_medium_segregated(entry_header));
    return pas_segregated_page_for_address_and_page_config(
        inner_ptr, VERSE_HEAP_CONFIG.medium_segregated_config);
}

static PAS_ALWAYS_INLINE pas_segregated_view verse_heap_get_segregated_view(uintptr_t inner_ptr)
{
    pas_segregated_page* page = verse_heap_get_segregated_page(inner_ptr);
    if (page)
        return page->owner;
    return NULL;
}

static PAS_ALWAYS_INLINE pas_object_kind verse_heap_get_object_kind(uintptr_t inner_ptr)
{
    verse_heap_chunk_map_entry_header entry_header;

    entry_header = verse_heap_get_chunk_map_entry_header(inner_ptr);

    if (verse_heap_chunk_map_entry_header_is_empty(entry_header))
        return pas_not_an_object_kind;

    if (verse_heap_chunk_map_entry_header_is_large(entry_header))
        return pas_large_object_kind;

    if (verse_heap_chunk_map_entry_header_is_small_segregated(entry_header))
        return pas_small_segregated_object_kind;
    
    PAS_ASSERT(verse_heap_chunk_map_entry_header_is_medium_segregated(entry_header));
    return pas_medium_segregated_object_kind;
}

static PAS_ALWAYS_INLINE verse_heap_page_header* verse_heap_get_page_header_inline(uintptr_t inner_ptr)
{
    verse_heap_chunk_map_entry_header entry_header;
	pas_segregated_page* page;

    entry_header = verse_heap_get_chunk_map_entry_header(inner_ptr);

	PAS_TESTING_ASSERT(!verse_heap_chunk_map_entry_header_is_empty(entry_header));

    if (PAS_LIKELY(verse_heap_chunk_map_entry_header_is_small_segregated(entry_header))) {
        page = pas_segregated_page_for_address_and_page_config(
            inner_ptr, VERSE_HEAP_CONFIG.small_segregated_config);
    } else {
		if (verse_heap_chunk_map_entry_header_is_large(entry_header))
			return &verse_heap_large_objects_header;

        PAS_ASSERT(verse_heap_chunk_map_entry_header_is_medium_segregated(entry_header));
        page = pas_segregated_page_for_address_and_page_config(
            inner_ptr, VERSE_HEAP_CONFIG.medium_segregated_config);
    }

	return verse_heap_page_header_for_segregated_page(page);
}

PAS_END_EXTERN_C;

#endif /* PAS_ENABLE_VERSE */

#endif /* VERSE_HEAP_INLINES_H */

