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

#include "verse_heap_runtime_config.h"

#include "verse_heap.h"

#if PAS_ENABLE_VERSE

pas_allocation_result verse_heap_runtime_config_allocate_chunks(verse_heap_runtime_config* config,
                                                                size_t size,
																pas_physical_memory_transaction* transaction,
																pas_primordial_page_state desired_state)
{
    pas_allocation_result result;

    PAS_ASSERT(pas_is_aligned(size, VERSE_HEAP_CHUNK_SIZE));

    result = config->page_provider(
		size, pas_alignment_create_traditional(VERSE_HEAP_CHUNK_SIZE), "verse_heap_chunk", NULL, transaction, desired_state,
		config->page_provider_arg);
    
    if (result.did_succeed) {
        uintptr_t address;
        PAS_ASSERT(result.zero_mode);
        for (address = result.begin; address < result.begin + size; address += VERSE_HEAP_CHUNK_SIZE)
            verse_heap_initialize_chunk_map_entry_ptr(address);
    }
    
    return result;
}

pas_allocation_result verse_heap_runtime_config_chunks_provider(size_t size,
																pas_alignment alignment,
																const char* name,
																pas_heap* heap,
																pas_physical_memory_transaction* transaction,
																pas_primordial_page_state desired_state,
																void* arg)
{
	verse_heap_runtime_config* config;

    PAS_UNUSED_PARAM(heap);

    PAS_ASSERT(pas_is_aligned(size, VERSE_HEAP_CHUNK_SIZE));
    PAS_ASSERT(!alignment.alignment_begin);
    PAS_ASSERT(alignment.alignment == VERSE_HEAP_CHUNK_SIZE);

	config = (verse_heap_runtime_config*)arg;

	return verse_heap_runtime_config_allocate_chunks(config, size, transaction, desired_state);
}

#endif /* PAS_ENABLE_VERSE */

#endif /* LIBPAS_ENABLED */


