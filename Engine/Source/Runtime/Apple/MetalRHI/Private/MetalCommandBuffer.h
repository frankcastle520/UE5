// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetalThirdParty.h"
#include "MetalResources.h"
#include "MetalShaderResources.h"

class FMetalCommandBuffer
{
public:
    FMetalCommandBuffer(MTL::CommandBuffer* InCommandBuffer)
    {
        CommandBuffer = NS::RetainPtr(InCommandBuffer);
    }
    
    FORCEINLINE MTLCommandBufferPtr& GetMTLCmdBuffer() {return CommandBuffer;}
    
    FORCEINLINE TSharedPtr<FMetalCommandBufferFence, ESPMode::ThreadSafe> GetCompletionFence()
    {
        if(CmdBufferFence == nullptr)
        {
            CmdBufferFence = MakeShareable(new FMetalCommandBufferFence());
            InsertCompletionFence(CmdBufferFence);
        }
        assert(CmdBufferFence != nullptr);
        return CmdBufferFence;
    }
    
    FORCEINLINE void InsertCompletionFence(TSharedPtr<FMetalCommandBufferFence, ESPMode::ThreadSafe> Fence)
    {
        Fence->Insert(CommandBuffer);
    }
    
private:
    MTLCommandBufferPtr CommandBuffer;
    TSharedPtr<FMetalCommandBufferFence, ESPMode::ThreadSafe> CmdBufferFence = nullptr;
};
