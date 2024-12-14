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

#include "verse_heap_medium_page_header_object.h"

#include "pas_utility_heap.h"
#include "verse_heap_config.h"

#if PAS_ENABLE_VERSE

PAS_API verse_heap_medium_page_header_object* verse_heap_medium_page_header_object_create(void)
{
	verse_heap_medium_page_header_object* result;
	
	result = (verse_heap_medium_page_header_object*)
		pas_utility_heap_allocate(VERSE_HEAP_MEDIUM_SEGREGATED_HEADER_OBJECT_SIZE, "verse_heap_medium_page_header_object");

	PAS_ASSERT(verse_heap_page_base_for_page_header(&result->verse) == &result->segregated.base);
	PAS_ASSERT(verse_heap_page_header_for_segregated_page(&result->segregated) == &result->verse);

	return result;
}

PAS_API void verse_heap_medium_page_header_object_destroy(verse_heap_medium_page_header_object* header)
{
	pas_utility_heap_deallocate(header);
}

#endif /* PAS_ENABLE_VERSE */

#endif /* LIBPAS_ENABLED */

