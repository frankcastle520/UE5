// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetalThirdParty.h"
#include "MetalProfiler.h"

class FMetalCommandBuffer;
class FMetalCommandQueue;

/**
 * FMetalCommandList:
 * Encapsulates multiple command-buffers into an ordered list for submission. 
 * For the immediate context this is irrelevant and is merely a pass-through into the CommandQueue, but
 * for deferred/parallel contexts it is required as they must queue their command buffers until they can 
 * be committed to the command-queue in the proper order which is only known at the end of parallel encoding.
 */
class FMetalCommandList
{
public:
#pragma mark - Public C++ Boilerplate -

	/**
	 * Constructor
	 * @param InCommandQueue The command-queue to which the command-list's buffers will be submitted.
	 */
	FMetalCommandList(FMetalCommandQueue& InCommandQueue);
	
	/** Destructor */
	~FMetalCommandList(void);
	
	/**
	 * Command buffer failure reporting function.
	 * @param CompletedBuffer The buffer to check for failure.
	 */
	static void HandleMetalCommandBufferFailure(MTL::CommandBuffer* CompletedBuffer);
	
#pragma mark - Public Command List Mutators -

	/** 
	 * Finalizes the command buffer ready for submission
	 * @param Buffer The buffer to submit to the command-list.
	 * @param CompletionHandlers The completion handlers that should be attached to this command-buffer.
	 */
	void FinalizeCommandBuffer(FMetalCommandBuffer* Buffer, TArray<FMetalCommandBufferCompletionHandler> CompletionHandlers);
	
#pragma mark - Public Command List Accessors -
	
	/**
	 * The index of this command-list within the parallel pass.
	 * @returns The index of this command-list within the parallel pass, 0 when IsImmediate() is true.
	 */
	uint32 GetParallelIndex(void) const { return 0; }

	/** @returns The command queue to which this command-list submits command-buffers. */
	FMetalCommandQueue& GetCommandQueue(void) const { return CommandQueue; }
	
private:
#pragma mark - Private Member Variables -
	FMetalCommandQueue& CommandQueue;
	TSharedPtr<FMetalCommandBufferTiming, ESPMode::ThreadSafe> LastCompletedBufferTiming;
};
