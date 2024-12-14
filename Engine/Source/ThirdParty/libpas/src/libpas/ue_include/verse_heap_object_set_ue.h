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

#ifndef VERSE_HEAP_OBJECT_SET_UE_H
#define VERSE_HEAP_OBJECT_SET_UE_H

#include <stddef.h>
#include "verse_heap_iterate_filter_ue.h"

#ifdef __cplusplus
extern "C" {
#endif

struct verse_heap_object_set;
typedef struct verse_heap_object_set verse_heap_object_set;

PAS_API verse_heap_object_set* verse_heap_object_set_create(void);

PAS_API void verse_heap_object_set_start_iterate_before_handshake(verse_heap_object_set* set);
PAS_API size_t verse_heap_object_set_start_iterate_after_handshake(verse_heap_object_set* set);
PAS_API void verse_heap_object_set_iterate_range(
    verse_heap_object_set* set,
    size_t begin,
    size_t end,
    verse_heap_iterate_filter filter,
    void (*callback)(void* object, void* arg),
    void* arg);
PAS_API void verse_heap_object_set_end_iterate(verse_heap_object_set* set);

#ifdef __cplusplus
}
#endif

#endif /* VERSE_HEAP_OBJECT_SET_UE_H */

