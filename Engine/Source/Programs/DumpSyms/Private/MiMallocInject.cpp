// Copyright Epic Games, Inc. All Rights Reserved.

// This file exists solely to pull in mimalloc because otherwise the performance of our built dump_syms will suck (it allocates a looooot of memory).
#include "static.c"
