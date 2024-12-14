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

#include "pas_heap_lock.h"
#include "pas_large_utility_free_heap.h"
#include "verse_heap_object_set.h"
#include "verse_heap_object_set_set.h"

#if PAS_ENABLE_VERSE

void verse_heap_object_set_set_construct(verse_heap_object_set_set* set_set)
{
    pas_heap_lock_assert_held();
    *set_set = VERSE_HEAP_OBJECT_SET_SET_INITIALIZER;
}

void verse_heap_object_set_set_add_set(verse_heap_object_set_set* set_set, verse_heap_object_set* set)
{
    pas_heap_lock_assert_held();

    PAS_ASSERT(!verse_heap_object_set_set_contains_set(set_set, set));
    
    if (set_set->num_sets >= set_set->sets_capacity) {
        unsigned new_sets_capacity;
        verse_heap_object_set** new_sets;
        
        PAS_ASSERT(set_set->num_sets == set_set->sets_capacity);

        new_sets_capacity = pas_max_uint32(4, set_set->num_sets * 2);
        new_sets = (verse_heap_object_set**)pas_large_utility_free_heap_allocate(
            new_sets_capacity * sizeof(verse_heap_object_set*),
            "verse_heap_object_set_set/sets");

        if (set_set->num_sets) {
            memcpy(new_sets, set_set->sets, set_set->num_sets * sizeof(verse_heap_object_set*));
        }

        pas_large_utility_free_heap_deallocate(set_set->sets, set_set->sets_capacity * sizeof(verse_heap_object_set*));

        set_set->sets = new_sets;
        set_set->sets_capacity = new_sets_capacity;
    }

    set_set->sets[set_set->num_sets++] = set;
}

void verse_heap_object_set_set_add_view(verse_heap_object_set_set* set_set, pas_segregated_exclusive_view* view)
{
    unsigned index;
    pas_heap_lock_assert_held();
    for (index = set_set->num_sets; index--;)
        verse_heap_object_set_add_view(set_set->sets[index], view);
}

void verse_heap_object_set_set_add_large_entry(verse_heap_object_set_set* set_set, verse_heap_large_entry* large_entry)
{
    unsigned index;
    pas_heap_lock_assert_held();
    for (index = set_set->num_sets; index--;)
        verse_heap_object_set_add_large_entry(set_set->sets[index], large_entry);
}

#endif /* PAS_ENABLE_VERSE */

#endif /* LIBPAS_ENABLED */

