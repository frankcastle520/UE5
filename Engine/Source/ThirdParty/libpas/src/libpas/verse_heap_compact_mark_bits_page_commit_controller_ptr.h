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

#ifndef VERSE_HEAP_COMPACT_MARK_BITS_PAGE_COMMIT_CONTROLLER_PTR_H
#define VERSE_HEAP_COMPACT_MARK_BITS_PAGE_COMMIT_CONTROLLER_PTR_H

#include "pas_compact_ptr.h"

PAS_BEGIN_EXTERN_C;

struct verse_heap_mark_bits_page_commit_controller;
typedef struct verse_heap_mark_bits_page_commit_controller verse_heap_mark_bits_page_commit_controller;

PAS_DEFINE_COMPACT_PTR(verse_heap_mark_bits_page_commit_controller,
                       verse_heap_compact_mark_bits_page_commit_controller_ptr);

PAS_END_EXTERN_C;

#endif /* VERSE_HEAP_COMPACT_MARK_BITS_PAGE_COMMIT_CONTROLLER_PTR_H */

