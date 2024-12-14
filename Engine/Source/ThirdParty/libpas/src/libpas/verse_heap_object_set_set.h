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

#ifndef VERSE_HEAP_OBJECT_SET_SET_H
#define VERSE_HEAP_OBJECT_SET_SET_H

#include "pas_segregated_view.h"

#if PAS_ENABLE_VERSE

PAS_BEGIN_EXTERN_C;

struct verse_heap_large_entry;
struct verse_heap_object_set;
struct verse_heap_object_set_set;
typedef struct verse_heap_large_entry verse_heap_large_entry;
typedef struct verse_heap_object_set verse_heap_object_set;
typedef struct verse_heap_object_set_set verse_heap_object_set_set;

/* This is the set of sets that a heap belongs to. */
struct verse_heap_object_set_set {
    /* FIXME: This should almost certainly be a fixed-size array to reduce pointer chasing, since no set of
       object sets will be that big. Maybe they're bounded at like 2 or 3 in practice. */
    verse_heap_object_set** sets;
    unsigned num_sets;
    unsigned sets_capacity;
};

#define VERSE_HEAP_OBJECT_SET_SET_INITIALIZER ((verse_heap_object_set_set){ \
        .sets = NULL, \
        .num_sets = 0, \
        .sets_capacity = 0 \
    })

PAS_API void verse_heap_object_set_set_construct(verse_heap_object_set_set* set_set);

/* This should be called before any objects are allocated. */
PAS_API void verse_heap_object_set_set_add_set(verse_heap_object_set_set* set_set, verse_heap_object_set* set);

static PAS_ALWAYS_INLINE bool verse_heap_object_set_set_contains_set(verse_heap_object_set_set* set_set,
                                                                     verse_heap_object_set* set)
{
    unsigned index;

    for (index = set_set->num_sets; index--;) {
        if (set_set->sets[index] == set)
            return true;
    }

    return false;
}

PAS_API void verse_heap_object_set_set_add_view(verse_heap_object_set_set* set_set,
                                                pas_segregated_exclusive_view* view);
PAS_API void verse_heap_object_set_set_add_large_entry(verse_heap_object_set_set* set_set,
                                                       verse_heap_large_entry* entry);

PAS_END_EXTERN_C;

#endif /* PAS_ENABLE_VERSE */

#endif /* VERSE_HEAP_OBJECT_SET_SET_H */


