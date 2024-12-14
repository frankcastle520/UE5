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

#ifndef VERSE_HEAP_MEDIUM_PAGE_HEADER_OBJECT_H
#define VERSE_HEAP_MEDIUM_PAGE_HEADER_OBJECT_H

#include "pas_segregated_page.h"
#include "verse_heap_page_header.h"

#if PAS_ENABLE_VERSE

PAS_BEGIN_EXTERN_C;

struct verse_heap_medium_page_header_object;
typedef struct verse_heap_medium_page_header_object verse_heap_medium_page_header_object;

struct verse_heap_medium_page_header_object {
	uintptr_t boundary;
	verse_heap_page_header verse;
	pas_segregated_page segregated;
};

/* These allocation functions need to be called with the heap lock held. That could be fixed if we had a variant of the utility heap
   that operated without heap lock and used normal TLCs. It wouldn't be super hard to make such a thing. One annoying feature of that
   would be that you'd have to pass physical memory transactions down to it, yuck. */

/* Returns an uninitialized page header object. */
PAS_API verse_heap_medium_page_header_object* verse_heap_medium_page_header_object_create(void);
PAS_API void verse_heap_medium_page_header_object_destroy(verse_heap_medium_page_header_object* header);

PAS_END_EXTERN_C;

#endif /* PAS_ENABLE_VERSE */

#endif /* VERSE_HEAP_MEDIUM_PAGE_HEADER_OBJECT_H */

