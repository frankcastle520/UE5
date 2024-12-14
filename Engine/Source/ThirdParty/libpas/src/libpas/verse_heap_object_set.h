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

#ifndef VERSE_HEAP_OBJECT_SET_H
#define VERSE_HEAP_OBJECT_SET_H

#include "pas_compact_atomic_segregated_exclusive_view_ptr.h"
#include "pas_segmented_vector.h"
#include "verse_heap_compact_large_entry_ptr.h"
#include "verse_heap_iterate_filter.h"
#include "ue_include/verse_heap_object_set_ue.h"

#if PAS_ENABLE_VERSE

PAS_BEGIN_EXTERN_C;

struct verse_heap_object_set;
typedef struct verse_heap_object_set verse_heap_object_set;

PAS_DECLARE_SEGMENTED_VECTOR(verse_heap_view_vector,
                             pas_compact_atomic_segregated_exclusive_view_ptr,
                             128);

struct verse_heap_object_set {
    verse_heap_compact_large_entry_ptr* large_entries;
    size_t num_large_entries;
    size_t large_entries_capacity;

    verse_heap_view_vector views;
};

#define VERSE_HEAP_OBJECT_SET_INITIALIZER ((verse_heap_object_set){ \
        .large_entries = NULL, \
        .num_large_entries = 0, \
        .large_entries_capacity = 0, \
        .views = PAS_SEGMENTED_VECTOR_INITIALIZER, \
    })

PAS_API void verse_heap_object_set_add_view(verse_heap_object_set* set, pas_segregated_exclusive_view* view);
PAS_API void verse_heap_object_set_add_large_entry(verse_heap_object_set* set, verse_heap_large_entry* entry);

PAS_END_EXTERN_C;

#endif /* PAS_ENABLE_VERSE */

#endif /* VERSE_HEAP_OBJECT_SET_H */

