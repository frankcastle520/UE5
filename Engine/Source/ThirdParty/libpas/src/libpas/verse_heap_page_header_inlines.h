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

#ifndef VERSE_HEAP_PAGE_HEADER_INLINES_H
#define VERSE_HEAP_PAGE_HEADER_INLINES_H

#include "verse_heap.h"
#include "verse_heap_black_allocation_mode.h"
#include "verse_heap_page_header.h"

#if PAS_ENABLE_VERSE

PAS_BEGIN_EXTERN_C;

static PAS_ALWAYS_INLINE bool verse_heap_page_header_could_need_iteration(verse_heap_page_header* header)
{
    uint64_t version;

    version = verse_heap_current_iteration_state.version;
    if (!version)
        return false;
    if (version != header->version) {
        PAS_ASSERT(version > header->version);
        return true;
    }
    return false;
}

static PAS_ALWAYS_INLINE bool verse_heap_page_header_handle_iteration(verse_heap_page_header* header,
                                                                      uint64_t iteration_version)
{
    PAS_TESTING_ASSERT(iteration_version);
    
    if (header->version == iteration_version)
        return false;
    
    PAS_TESTING_ASSERT(header->version < iteration_version);
    header->version = iteration_version;

    return true;
}

static PAS_ALWAYS_INLINE verse_heap_black_allocation_mode
verse_heap_page_header_black_allocation_mode(verse_heap_page_header* header)
{
    uint64_t version = verse_heap_allocating_black_version;
    if (version < VERSE_HEAP_FIRST_VERSION)
        return (verse_heap_black_allocation_mode)version;
    if (header->version != version) {
        PAS_ASSERT(header->version < version);
        return verse_heap_allocate_black;
    }
    return verse_heap_do_not_allocate_black;
}

static PAS_ALWAYS_INLINE bool verse_heap_page_header_should_allocate_black(verse_heap_page_header* header)
{
    switch (verse_heap_page_header_black_allocation_mode(header)) {
    case verse_heap_do_not_allocate_black:
        return false;
    case verse_heap_allocate_black:
        return true;
    }
    PAS_ASSERT(!"Should not be reached");
    return false;
}

PAS_END_EXTERN_C;

#endif /* PAS_ENABLE_VERSE */

#endif /* VERSE_HEAP_PAGE_HEADER_INLINES_H */



