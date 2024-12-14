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

#ifndef VERSE_HEAP_PAGE_HEADER_H
#define VERSE_HEAP_PAGE_HEADER_H

#include "pas_lock.h"
#include "ue_include/verse_heap_page_header_ue.h"

#if PAS_ENABLE_VERSE

PAS_BEGIN_EXTERN_C;

#define VERSE_HEAP_FIRST_VERSION ((uint64_t)10)

struct verse_heap_page_header;
typedef struct verse_heap_page_header verse_heap_page_header;

struct PAS_ALIGNED(PAS_PAIR_SIZE) verse_heap_page_header {
    uint64_t version;
	unsigned* stashed_alloc_bits;
    bool may_have_set_mark_bits_for_dead_objects;
	bool is_stashing_alloc_bits;
	void* client_data;
	pas_lock client_data_lock;
};

#define VERSE_HEAP_PAGE_HEADER_INITIALIZER ((verse_heap_page_header){ \
        .version = VERSE_HEAP_FIRST_VERSION, \
		.stashed_alloc_bits = NULL, \
        .may_have_set_mark_bits_for_dead_objects = false, \
		.is_stashing_alloc_bits = false, \
		.client_data = NULL, \
		.client_data_lock = PAS_LOCK_INITIALIZER \
    })

PAS_API void verse_heap_page_header_construct(verse_heap_page_header* header);

PAS_END_EXTERN_C;

#endif /* PAS_ENABLE_VERSE */

#endif /* VERSE_HEAP_PAGE_HEADER_H */

