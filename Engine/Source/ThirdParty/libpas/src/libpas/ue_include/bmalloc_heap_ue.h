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

#ifndef BMALLOC_HEAP_UE_H
#define BMALLOC_HEAP_UE_H

#include "pas_reallocate_free_mode_ue.h"

#ifdef __cplusplus
extern "C" {
#endif

PAS_API void* bmalloc_allocate(size_t size);

PAS_API void* bmalloc_try_allocate_with_alignment(size_t size,
                                                  size_t alignment);
PAS_API void* bmalloc_allocate_with_alignment(size_t size,
                                              size_t alignment);
	
PAS_API void* bmalloc_try_reallocate_with_alignment(void* old_ptr, size_t new_size, size_t alignment,
                                                    pas_reallocate_free_mode free_mode);

PAS_API void* bmalloc_reallocate_with_alignment(void* old_ptr, size_t new_size, size_t alignment,
                                                pas_reallocate_free_mode free_mode);

PAS_API void bmalloc_deallocate(void*);

PAS_BAPI size_t bmalloc_get_allocation_size(void* ptr);

#ifdef __cplusplus
}
#endif

#endif /* BMALLOC_HEAP_UE_H */

