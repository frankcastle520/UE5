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

#ifndef PAS_BITVECTOR_UE_H
#define PAS_BITVECTOR_UE_H

#include <stddef.h>

#define PAS_BITVECTOR_BITS_PER_WORD 32
#define PAS_BITVECTOR_BITS_PER_WORD64 64
#define PAS_BITVECTOR_WORD_SHIFT 5

#define PAS_BITVECTOR_NUM_WORDS(num_bits) (((num_bits) + 31) >> 5)
#define PAS_BITVECTOR_NUM_WORDS64(num_bits) (((num_bits) + 63) >> 6)
#define PAS_BITVECTOR_NUM_BYTES(num_bits) (PAS_BITVECTOR_NUM_WORDS(num_bits) * sizeof(unsigned))
#define PAS_BITVECTOR_NUM_BITS(num_words) ((num_words) << 5)
#define PAS_BITVECTOR_NUM_BITS64(num_words) ((num_words) << 6)
#define PAS_BITVECTOR_NUM_BYTES64(num_bits) (PAS_BITVECTOR_NUM_WORDS64(num_bits) * sizeof(uint64_t))

#define PAS_BITVECTOR_WORD_INDEX(bit_index) ((bit_index) >> 5)
#define PAS_BITVECTOR_WORD64_INDEX(bit_index) ((bit_index) >> 6)
#define PAS_BITVECTOR_BIT_INDEX(word_index) ((word_index) << 5)
#define PAS_BITVECTOR_BIT_INDEX64(word_index) ((word_index) << 6)
#define PAS_BITVECTOR_BIT_SHIFT(bit_index) ((bit_index) & 31)
#define PAS_BITVECTOR_BIT_SHIFT64(bit_index) ((bit_index) & 63)
#define PAS_BITVECTOR_BIT_MASK(bit_index) (((uint32_t)1) << PAS_BITVECTOR_BIT_SHIFT(bit_index))
#define PAS_BITVECTOR_BIT_MASK64(bit_index) (((uint64_t)1) << PAS_BITVECTOR_BIT_SHIFT64(bit_index))

#endif /* PAS_BITVECTOR_UE_H */

