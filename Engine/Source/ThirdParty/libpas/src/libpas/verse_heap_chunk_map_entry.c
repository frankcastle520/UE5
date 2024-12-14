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

#include "pas_config.h"

#if LIBPAS_ENABLED

#include "verse_heap_chunk_map_entry.h"

#include "verse_heap_large_entry.h"
#include "pas_stream.h"

#if PAS_ENABLE_VERSE

void verse_heap_chunk_map_entry_header_dump(verse_heap_chunk_map_entry_header header, pas_stream* stream)
{
    if (verse_heap_chunk_map_entry_header_is_empty(header)) {
        pas_stream_printf(stream, "empty");
        return;
    }

    if (verse_heap_chunk_map_entry_header_is_small_segregated(header)) {
        pas_stream_printf(stream, "small");
        return;
    }

    if (verse_heap_chunk_map_entry_header_is_medium_segregated(header)) {
        pas_stream_printf(
            stream, "medium:%p/%s",
            verse_heap_chunk_map_entry_header_medium_segregated_header_object(header),
            pas_empty_mode_get_string(
                verse_heap_chunk_map_entry_header_medium_segregated_empty_mode(header)));
        return;
    }

    if (verse_heap_chunk_map_entry_header_is_large(header)) {
        verse_heap_large_entry* large_entry;

        large_entry = verse_heap_chunk_map_entry_header_large_entry(header);

        pas_stream_printf(stream, "large:%zx-%zx,%p", large_entry->begin, large_entry->end, large_entry->heap);
        return;
    }

    PAS_ASSERT(!"Should not be reached");
}

void verse_heap_chunk_map_entry_dump(verse_heap_chunk_map_entry entry, pas_stream* stream)
{
    verse_heap_chunk_map_entry_header header;
    header = verse_heap_chunk_map_entry_get_header(entry);
    if (verse_heap_chunk_map_entry_header_is_small_segregated(header)) {
        size_t index;
        pas_stream_printf(stream, "small:");
        for (index = 0; index < VERSE_HEAP_CHUNK_MAP_ENTRY_NUM_WORDS; ++index) {
            pas_stream_printf(
                stream, "%08x",
                verse_heap_chunk_map_entry_small_segregated_ownership_bitvector(&entry)[index]);
        }
        return;
    }

    verse_heap_chunk_map_entry_header_dump(header, stream);
}

#endif /* PAS_ENABLE_VERSE */

#endif /* LIBPAS_ENABLED */

