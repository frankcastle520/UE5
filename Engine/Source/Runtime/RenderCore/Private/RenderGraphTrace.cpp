// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphTrace.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphPrivate.h"
#include "Trace/Trace.inl"

#if RDG_ENABLE_TRACE

UE_TRACE_CHANNEL_DEFINE(RDGChannel)

UE_TRACE_EVENT_BEGIN(RDGTrace, GraphMessage)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, Name)
	UE_TRACE_EVENT_FIELD(uint64, StartCycles)
	UE_TRACE_EVENT_FIELD(uint64, EndCycles)
	UE_TRACE_EVENT_FIELD(uint32, PassCount)
	UE_TRACE_EVENT_FIELD(uint64[], TransientMemoryCommitSizes)
	UE_TRACE_EVENT_FIELD(uint64[], TransientMemoryCapacities)
	UE_TRACE_EVENT_FIELD(uint8[],  TransientMemoryFlags)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(RDGTrace, GraphEndMessage)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(RDGTrace, PassMessage)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, Name)
	UE_TRACE_EVENT_FIELD(uint64, StartCycles)
	UE_TRACE_EVENT_FIELD(uint64, EndCycles)
	UE_TRACE_EVENT_FIELD(uint32, Handle)
	UE_TRACE_EVENT_FIELD(uint32, GraphicsForkPass)
	UE_TRACE_EVENT_FIELD(uint32, GraphicsJoinPass)
	UE_TRACE_EVENT_FIELD(uint32[], Textures)
	UE_TRACE_EVENT_FIELD(uint32[], Buffers)
	UE_TRACE_EVENT_FIELD(uint16, Flags)
	UE_TRACE_EVENT_FIELD(uint16, Pipeline)
	UE_TRACE_EVENT_FIELD(bool, IsCulled)
	UE_TRACE_EVENT_FIELD(bool, IsAsyncComputeBegin)
	UE_TRACE_EVENT_FIELD(bool, IsAsyncComputeEnd)
	UE_TRACE_EVENT_FIELD(bool, SkipRenderPassBegin)
	UE_TRACE_EVENT_FIELD(bool, SkipRenderPassEnd)
	UE_TRACE_EVENT_FIELD(bool, IsParallelExecuteBegin)
	UE_TRACE_EVENT_FIELD(bool, IsParallelExecuteEnd)
	UE_TRACE_EVENT_FIELD(bool, IsParallelExecute)
	UE_TRACE_EVENT_FIELD(bool, IsParallelExecuteAllowed)
	UE_TRACE_EVENT_FIELD(bool, IsParallelExecuteAsyncAllowed)
	UE_TRACE_EVENT_FIELD(bool, IsHandleType32Bits)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(RDGTrace, BufferMessage)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, Name)
	UE_TRACE_EVENT_FIELD(uint32, UsageFlags)
	UE_TRACE_EVENT_FIELD(uint32, BytesPerElement)
	UE_TRACE_EVENT_FIELD(uint32, NumElements)
	UE_TRACE_EVENT_FIELD(uint32, Handle)
	UE_TRACE_EVENT_FIELD(uint32, NextOwnerHandle)
	UE_TRACE_EVENT_FIELD(uint32, Order)
	UE_TRACE_EVENT_FIELD(uint32[], Passes)
	UE_TRACE_EVENT_FIELD(uint64[], TransientAllocationOffsetMins)
	UE_TRACE_EVENT_FIELD(uint64[], TransientAllocationOffsetMaxs)
	UE_TRACE_EVENT_FIELD(uint16[], TransientAllocationMemoryRanges)
	UE_TRACE_EVENT_FIELD(FRDGPassHandle::IndexType, TransientAcquirePass)
	UE_TRACE_EVENT_FIELD(FRDGPassHandle::IndexType, TransientDiscardPass)
	UE_TRACE_EVENT_FIELD(bool, IsExternal)
	UE_TRACE_EVENT_FIELD(bool, IsExtracted)
	UE_TRACE_EVENT_FIELD(bool, IsCulled)
	UE_TRACE_EVENT_FIELD(bool, IsTrackingSkipped)
	UE_TRACE_EVENT_FIELD(bool, IsTransient)
	UE_TRACE_EVENT_FIELD(bool, IsTransientUntracked)
	UE_TRACE_EVENT_FIELD(bool, IsTransientCacheHit)
	UE_TRACE_EVENT_FIELD(bool, IsHandleType32Bits)
UE_TRACE_EVENT_END()
 
UE_TRACE_EVENT_BEGIN(RDGTrace, TextureMessage)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, Name)
	UE_TRACE_EVENT_FIELD(uint64, StartCycles)
	UE_TRACE_EVENT_FIELD(uint64, EndCycles)
	UE_TRACE_EVENT_FIELD(uint32, Handle)
	UE_TRACE_EVENT_FIELD(uint32, NextOwnerHandle)
	UE_TRACE_EVENT_FIELD(uint32, Order)
	UE_TRACE_EVENT_FIELD(uint32[], Passes)
	UE_TRACE_EVENT_FIELD(uint64[], TransientAllocationOffsetMins)
	UE_TRACE_EVENT_FIELD(uint64[], TransientAllocationOffsetMaxs)
	UE_TRACE_EVENT_FIELD(uint16[], TransientAllocationMemoryRanges)
	UE_TRACE_EVENT_FIELD(FRDGPassHandle::IndexType, TransientAcquirePass)
	UE_TRACE_EVENT_FIELD(FRDGPassHandle::IndexType, TransientDiscardPass)
	UE_TRACE_EVENT_FIELD(uint64, SizeInBytes)
	UE_TRACE_EVENT_FIELD(uint64, CreateFlags)
	UE_TRACE_EVENT_FIELD(uint32, Dimension)
	UE_TRACE_EVENT_FIELD(uint32, Format)
	UE_TRACE_EVENT_FIELD(uint32, ExtentX)
	UE_TRACE_EVENT_FIELD(uint32, ExtentY)
	UE_TRACE_EVENT_FIELD(uint16, Depth)
	UE_TRACE_EVENT_FIELD(uint16, ArraySize)
	UE_TRACE_EVENT_FIELD(uint8, NumMips)
	UE_TRACE_EVENT_FIELD(uint8, NumSamples)
	UE_TRACE_EVENT_FIELD(bool, IsExternal)
	UE_TRACE_EVENT_FIELD(bool, IsExtracted)
	UE_TRACE_EVENT_FIELD(bool, IsCulled)
	UE_TRACE_EVENT_FIELD(bool, IsTrackingSkipped)
	UE_TRACE_EVENT_FIELD(bool, IsTransient)
	UE_TRACE_EVENT_FIELD(bool, IsTransientUntracked)
	UE_TRACE_EVENT_FIELD(bool, IsTransientCacheHit)
	UE_TRACE_EVENT_FIELD(bool, IsHandleType32Bits)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(RDGTrace, ScopeMessage)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, Name)
	UE_TRACE_EVENT_FIELD(uint32, FirstPass)
	UE_TRACE_EVENT_FIELD(uint32, LastPass)
	UE_TRACE_EVENT_FIELD(uint16, Depth)
	UE_TRACE_EVENT_FIELD(bool, IsHandleType32Bits)
UE_TRACE_EVENT_END()

static_assert(sizeof(FRDGPassHandle) == sizeof(uint32), "Expected 32 bit pass handles.");
static_assert(sizeof(FRDGTextureHandle) == sizeof(uint32), "Expected 32 bit texture handles.");
static_assert(sizeof(FRDGBufferHandle) == sizeof(uint32), "Expected 32 bit buffer handles.");

FRDGTrace::FRDGTrace()
	: bEnabled(UE_TRACE_CHANNELEXPR_IS_ENABLED(RDGChannel) && !IsImmediateMode())
{}

bool FRDGTrace::IsEnabled() const
{
	return bEnabled;
}

void FRDGTrace::OutputGraphBegin()
{
	if (!IsEnabled())
	{
		return;
	}

	GraphStartCycles = FPlatformTime::Cycles64();
}

void FRDGTrace::OutputGraphEnd(const FRDGBuilder& GraphBuilder)
{
	if (!IsEnabled())
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FRDGTrace::OutputGraphEnd);

	const FRDGPassHandle ProloguePassHandle = GraphBuilder.GetProloguePassHandle();

	const auto& Passes = GraphBuilder.Passes;
	const auto& Textures = GraphBuilder.Textures;
	const auto& Buffers = GraphBuilder.Buffers;

	{
		const TCHAR* Name = GraphBuilder.BuilderName.GetTCHAR();

		TArray<uint64> TransientMemoryCommitSizes;
		TArray<uint64> TransientMemoryCapacities;
		TArray<uint8>  TransientMemoryFlags;

		TransientMemoryCommitSizes.Reserve(TransientAllocationStats.MemoryRanges.Num());
		TransientMemoryCapacities.Reserve(TransientAllocationStats.MemoryRanges.Num());
		TransientMemoryFlags.Reserve(TransientAllocationStats.MemoryRanges.Num());

		for (const auto& MemoryRange : TransientAllocationStats.MemoryRanges)
		{
			TransientMemoryCommitSizes.Emplace(MemoryRange.CommitSize);
			TransientMemoryCapacities.Emplace(MemoryRange.Capacity);
			TransientMemoryFlags.Emplace((uint8)MemoryRange.Flags);
		}

		UE_TRACE_LOG(RDGTrace, GraphMessage, RDGChannel)
			<< GraphMessage.Name(Name, uint16(FCString::Strlen(Name)))
			<< GraphMessage.StartCycles(GraphStartCycles)
			<< GraphMessage.EndCycles(FPlatformTime::Cycles64())
			<< GraphMessage.PassCount(uint32(Passes.Num()))
			<< GraphMessage.TransientMemoryCommitSizes(TransientMemoryCommitSizes.GetData(), (uint16)TransientMemoryCommitSizes.Num())
			<< GraphMessage.TransientMemoryCapacities(TransientMemoryCapacities.GetData(), (uint16)TransientMemoryCapacities.Num())
			<< GraphMessage.TransientMemoryFlags(TransientMemoryFlags.GetData(), (uint16)TransientMemoryFlags.Num());
	}

	for (FRDGPassHandle Handle = Passes.Begin(); Handle != Passes.End(); ++Handle)
	{
		const FRDGPass* Pass = Passes[Handle];
		const TCHAR* Name = Pass->GetEventName().GetTCHAR();

		UE_TRACE_LOG(RDGTrace, PassMessage, RDGChannel)
			<< PassMessage.Name(Name, uint16(FCString::Strlen(Name)))
			<< PassMessage.Handle(Handle.GetIndex())
			<< PassMessage.GraphicsForkPass(Pass->GetGraphicsForkPass().GetIndexUnchecked())
			<< PassMessage.GraphicsJoinPass(Pass->GetGraphicsJoinPass().GetIndexUnchecked())
			<< PassMessage.Textures((const uint32*)Pass->TraceTextures.GetData(), (uint32)Pass->TraceTextures.Num())
			<< PassMessage.Buffers((const uint32*)Pass->TraceBuffers.GetData(), (uint32)Pass->TraceBuffers.Num())
			<< PassMessage.Flags(uint16(Pass->GetFlags()))
			<< PassMessage.Pipeline(uint16(Pass->GetPipeline()))
			<< PassMessage.IsCulled(Pass->bCulled != 0)
			<< PassMessage.IsAsyncComputeBegin(Pass->bAsyncComputeBegin != 0)
			<< PassMessage.IsAsyncComputeEnd(Pass->bAsyncComputeEnd != 0)
			<< PassMessage.SkipRenderPassBegin(Pass->bSkipRenderPassBegin != 0)
			<< PassMessage.SkipRenderPassEnd(Pass->bSkipRenderPassEnd != 0)
			<< PassMessage.IsParallelExecuteBegin(Pass->bParallelExecuteBegin != 0)
			<< PassMessage.IsParallelExecuteEnd(Pass->bParallelExecuteEnd != 0)
			<< PassMessage.IsParallelExecute(Pass->bParallelExecute != 0)
			<< PassMessage.IsParallelExecuteAllowed(Pass->TaskMode != ERDGPassTaskMode::Inline)
			<< PassMessage.IsParallelExecuteAsyncAllowed(Pass->TaskMode == ERDGPassTaskMode::Async)
			<< PassMessage.IsHandleType32Bits(true);
	}

#if RDG_EVENTS
	{
		auto DumpScopes = [&](FRDGScope* Current, auto& DumpScopes)
		{
			if (!Current || Current->bVisited)
				return;

			Current->bVisited = true;
			DumpScopes(Current->Parent, DumpScopes);

			if (FRDGScope_RHI* RHIScope = Current->Get<FRDGScope_RHI>())
			{
				if (Current->CPUFirstPass && Current->CPULastPass)
				{
					const TCHAR* Name = RHIScope->Name.GetTCHAR();

					uint32 Depth = 0;

					for (FRDGScope* Scope = Current; Scope->Parent; Scope = Scope->Parent)
					{
						if (Scope->Get<FRDGScope_RHI>())
						{
							if (Scope->CPUFirstPass && Scope->CPULastPass)
							{
								Depth++;
							}
						}
					}

					UE_TRACE_LOG(RDGTrace, ScopeMessage, RDGChannel)
						<< ScopeMessage.Name(Name, uint16(FCString::Strlen(Name)))
						<< ScopeMessage.FirstPass(Current->CPUFirstPass->GetHandle().GetIndexUnchecked())
						<< ScopeMessage.LastPass(Current->CPULastPass->GetHandle().GetIndexUnchecked())
						<< ScopeMessage.Depth(Depth)
						<< ScopeMessage.IsHandleType32Bits(true);
				}
			}
		};

		for (FRDGPassHandle Handle = Passes.Begin(); Handle != Passes.End(); ++Handle)
		{
			const FRDGPass* Pass = Passes[Handle];
			DumpScopes(Pass->Scope, DumpScopes);
		}
	}
#endif

	struct FTransientAllocation
	{
		TArray<uint64> OffsetMins;
		TArray<uint64> OffsetMaxs;
		TArray<uint16> MemoryRanges;
		bool bCacheHit = false;

		void Reset()
		{
			OffsetMins.Reset();
			OffsetMaxs.Reset();
			MemoryRanges.Reset();
			bCacheHit = false;
		}

		void Fill(const FRHITransientAllocationStats& Stats, const FRHITransientResource* Resource)
		{
			const FRHITransientAllocationStats::FAllocationArray& Allocations = Stats.Resources.FindChecked(Resource);

			for (const FRHITransientAllocationStats::FAllocation& Allocation : Allocations)
			{
				OffsetMins.Emplace(Allocation.OffsetMin);
				OffsetMaxs.Emplace(Allocation.OffsetMax);
				MemoryRanges.Emplace(Allocation.MemoryRangeIndex);
			}

			bCacheHit = Resource->GetAcquireCount() > 1;
		}

	} TransientAllocation;

	FRDGPassHandle TransientAcquirePass;
	FRDGPassHandle TransientDiscardPass;

	const auto FillTransientResourceArrays = [&](const FRHITransientResource* Resource, bool bRemoveFromStats)
	{
		TransientAllocation.Reset();
		TransientAcquirePass = {};
		TransientDiscardPass = {};

		if (Resource)
		{
			TransientAllocation.Fill(TransientAllocationStats, Resource);

			if (Resource->GetAcquirePass() != FRHITransientResource::kInvalidPassIndex)
			{
				TransientAcquirePass = FRDGPassHandle(Resource->GetAcquirePass());
			}

			if (Resource->GetDiscardPass() != FRHITransientResource::kInvalidPassIndex)
			{
				TransientDiscardPass = FRDGPassHandle(Resource->GetDiscardPass());
			}

			if (bRemoveFromStats)
			{
				TransientAllocationStats.Resources.Remove(Resource);
			}
		}
	};

	for (FRDGTextureHandle Handle = Textures.Begin(); Handle != Textures.End(); ++Handle)
	{
		const FRDGTexture* Texture = Textures[Handle];

		uint64 SizeInBytes = 0;
		if (FRHITexture* TextureRHI = Texture->GetRHIUnchecked())
		{
			if (Texture->TransientTexture)
			{
				SizeInBytes = Texture->TransientTexture->GetSize();
			}
			else
			{
				SizeInBytes = RHIComputeMemorySize(TextureRHI);
			}
		}

		const bool bRemoveFromStats = true;
		FillTransientResourceArrays(Texture->TransientTexture, bRemoveFromStats);

		UE_TRACE_LOG(RDGTrace, TextureMessage, RDGChannel)
			<< TextureMessage.Name(Texture->Name, uint16(FCString::Strlen(Texture->Name)))
			<< TextureMessage.Handle(Handle.GetIndex())
			<< TextureMessage.NextOwnerHandle(Texture->NextOwner.GetIndexUnchecked())
			<< TextureMessage.Order(Texture->TraceOrder)
			<< TextureMessage.Passes((const uint32*)Texture->TracePasses.GetData(), (uint32)Texture->TracePasses.Num())
			<< TextureMessage.TransientAllocationOffsetMins(TransientAllocation.OffsetMins.GetData(), TransientAllocation.OffsetMins.Num())
			<< TextureMessage.TransientAllocationOffsetMaxs(TransientAllocation.OffsetMaxs.GetData(), TransientAllocation.OffsetMaxs.Num())
			<< TextureMessage.TransientAllocationMemoryRanges(TransientAllocation.MemoryRanges.GetData(), TransientAllocation.MemoryRanges.Num())
			<< TextureMessage.TransientAcquirePass(TransientAcquirePass.GetIndexUnchecked())
			<< TextureMessage.TransientDiscardPass(TransientDiscardPass.GetIndexUnchecked())
			<< TextureMessage.SizeInBytes(SizeInBytes)
			<< TextureMessage.CreateFlags(uint32(Texture->Desc.Flags))
			<< TextureMessage.Dimension(uint32(Texture->Desc.Dimension))
			<< TextureMessage.Format(uint32(Texture->Desc.Format))
			<< TextureMessage.ExtentX(Texture->Desc.Extent.X)
			<< TextureMessage.ExtentY(Texture->Desc.Extent.Y)
			<< TextureMessage.Depth(Texture->Desc.Depth)
			<< TextureMessage.ArraySize(Texture->Desc.ArraySize)
			<< TextureMessage.NumMips(Texture->Desc.NumMips)
			<< TextureMessage.NumSamples(Texture->Desc.NumSamples)
			<< TextureMessage.IsExternal(bool(Texture->bExternal))
			<< TextureMessage.IsExtracted(bool(Texture->bExtracted))
			<< TextureMessage.IsCulled(bool(Texture->ReferenceCount == 0))
			<< TextureMessage.IsTrackingSkipped(EnumHasAnyFlags(Texture->Flags, ERDGTextureFlags::SkipTracking))
			<< TextureMessage.IsTransient(bool(Texture->bTransient))
			<< TextureMessage.IsTransientUntracked(false)
			<< TextureMessage.IsTransientCacheHit(TransientAllocation.bCacheHit)
			<< TextureMessage.IsHandleType32Bits(true);
	}

	for (FRDGBufferHandle Handle = Buffers.Begin(); Handle != Buffers.End(); ++Handle)
	{
		const FRDGBuffer* Buffer = Buffers[Handle];
		
		const bool bRemoveFromStats = true;
		FillTransientResourceArrays(Buffer->TransientBuffer, bRemoveFromStats);

		UE_TRACE_LOG(RDGTrace, BufferMessage, RDGChannel)
			<< BufferMessage.Name(Buffer->Name, uint16(FCString::Strlen(Buffer->Name)))
			<< BufferMessage.Handle(Buffer->Handle.GetIndex())
			<< BufferMessage.NextOwnerHandle(Buffer->NextOwner.GetIndexUnchecked())
			<< BufferMessage.Order(Buffer->TraceOrder)
			<< BufferMessage.Passes((const uint32*)Buffer->TracePasses.GetData(), (uint32)Buffer->TracePasses.Num())
			<< BufferMessage.TransientAllocationOffsetMins(TransientAllocation.OffsetMins.GetData(), TransientAllocation.OffsetMins.Num())
			<< BufferMessage.TransientAllocationOffsetMaxs(TransientAllocation.OffsetMaxs.GetData(), TransientAllocation.OffsetMaxs.Num())
			<< BufferMessage.TransientAllocationMemoryRanges(TransientAllocation.MemoryRanges.GetData(), TransientAllocation.MemoryRanges.Num())
			<< BufferMessage.TransientAcquirePass(TransientAcquirePass.GetIndexUnchecked())
			<< BufferMessage.TransientDiscardPass(TransientDiscardPass.GetIndexUnchecked())
			<< BufferMessage.UsageFlags(uint32(Buffer->Desc.Usage))
			<< BufferMessage.BytesPerElement(Buffer->Desc.BytesPerElement)
			<< BufferMessage.NumElements(Buffer->Desc.NumElements)
			<< BufferMessage.IsExternal(bool(Buffer->bExternal))
			<< BufferMessage.IsExtracted(bool(Buffer->bExtracted))
			<< BufferMessage.IsCulled(bool(Buffer->ReferenceCount == 0))
			<< BufferMessage.IsTrackingSkipped(EnumHasAnyFlags(Buffer->Flags, ERDGBufferFlags::SkipTracking))
			<< BufferMessage.IsTransient(bool(Buffer->bTransient))
			<< BufferMessage.IsTransientUntracked(false)
			<< BufferMessage.IsTransientCacheHit(TransientAllocation.bCacheHit)
			<< BufferMessage.IsHandleType32Bits(true);
	}

	int32 TextureIndex = Textures.Num();
	int32 BufferIndex = Buffers.Num();

	for (auto KeyValue : TransientAllocationStats.Resources)
	{
		const FRHITransientResource* Resource = KeyValue.Key;

		if (!Resource->IsAcquired())
		{
			continue;
		}

		const bool bRemoveFromStats = false;
		FillTransientResourceArrays(Resource, bRemoveFromStats);

		if (Resource->GetResourceType() == ERHITransientResourceType::Texture)
		{
			const FRHITransientTexture* Texture = static_cast<const FRHITransientTexture*>(Resource);

			UE_TRACE_LOG(RDGTrace, TextureMessage, RDGChannel)
				<< TextureMessage.Name(Texture->GetName(), uint16(FCString::Strlen(Texture->GetName())))
				<< TextureMessage.Handle(TextureIndex)
				<< TextureMessage.TransientAllocationOffsetMins(TransientAllocation.OffsetMins.GetData(), TransientAllocation.OffsetMins.Num())
				<< TextureMessage.TransientAllocationOffsetMaxs(TransientAllocation.OffsetMaxs.GetData(), TransientAllocation.OffsetMaxs.Num())
				<< TextureMessage.TransientAllocationMemoryRanges(TransientAllocation.MemoryRanges.GetData(), TransientAllocation.MemoryRanges.Num())
				<< TextureMessage.TransientAcquirePass(TransientAcquirePass.GetIndexUnchecked())
				<< TextureMessage.TransientDiscardPass(TransientDiscardPass.GetIndexUnchecked())
				<< TextureMessage.SizeInBytes(Resource->GetSize())
				<< TextureMessage.CreateFlags(uint32(Texture->CreateInfo.Flags))
				<< TextureMessage.Dimension(uint32(Texture->CreateInfo.Dimension))
				<< TextureMessage.Format(uint32(Texture->CreateInfo.Format))
				<< TextureMessage.ExtentX(Texture->CreateInfo.Extent.X)
				<< TextureMessage.ExtentY(Texture->CreateInfo.Extent.Y)
				<< TextureMessage.Depth(Texture->CreateInfo.Depth)
				<< TextureMessage.ArraySize(Texture->CreateInfo.ArraySize)
				<< TextureMessage.NumMips(Texture->CreateInfo.NumMips)
				<< TextureMessage.NumSamples(Texture->CreateInfo.NumSamples)
				<< TextureMessage.IsExternal(false)
				<< TextureMessage.IsExtracted(false)
				<< TextureMessage.IsCulled(false)
				<< TextureMessage.IsTrackingSkipped(false)
				<< TextureMessage.IsTransient(true)
				<< TextureMessage.IsTransientUntracked(true)
				<< TextureMessage.IsTransientCacheHit(TransientAllocation.bCacheHit)
				<< TextureMessage.IsHandleType32Bits(true);

			TextureIndex++;
		}
		else
		{
			const FRHITransientBuffer* Buffer = static_cast<const FRHITransientBuffer*>(Resource);

			UE_TRACE_LOG(RDGTrace, BufferMessage, RDGChannel)
				<< BufferMessage.Name(Buffer->GetName(), uint16(FCString::Strlen(Buffer->GetName())))
				<< BufferMessage.Handle(BufferIndex)
				<< BufferMessage.TransientAllocationOffsetMins(TransientAllocation.OffsetMins.GetData(), TransientAllocation.OffsetMins.Num())
				<< BufferMessage.TransientAllocationOffsetMaxs(TransientAllocation.OffsetMaxs.GetData(), TransientAllocation.OffsetMaxs.Num())
				<< BufferMessage.TransientAllocationMemoryRanges(TransientAllocation.MemoryRanges.GetData(), TransientAllocation.MemoryRanges.Num())
				<< BufferMessage.TransientAcquirePass(TransientAcquirePass.GetIndexUnchecked())
				<< BufferMessage.TransientDiscardPass(TransientDiscardPass.GetIndexUnchecked())
				<< BufferMessage.UsageFlags(uint32(Buffer->CreateInfo.Usage))
				<< BufferMessage.BytesPerElement(Buffer->CreateInfo.Stride)
				<< BufferMessage.NumElements(Buffer->CreateInfo.Size / Buffer->CreateInfo.Stride)
				<< BufferMessage.IsExternal(false)
				<< BufferMessage.IsExtracted(false)
				<< BufferMessage.IsCulled(false)
				<< BufferMessage.IsTrackingSkipped(false)
				<< BufferMessage.IsTransient(true)
				<< BufferMessage.IsTransientUntracked(true)
				<< BufferMessage.IsTransientCacheHit(TransientAllocation.bCacheHit)
				<< BufferMessage.IsHandleType32Bits(true);

			BufferIndex++;
		}
	}

	UE_TRACE_LOG(RDGTrace, GraphEndMessage, RDGChannel);
}

void FRDGTrace::AddResource(FRDGViewableResource* Resource)
{
	Resource->TraceOrder = ResourceOrder++;
}

void FRDGTrace::AddTexturePassDependency(FRDGTexture* Texture, FRDGPass* Pass)
{
	if (!IsEnabled())
	{
		return;
	}

	Pass->TraceTextures.Add(Texture->Handle);
	Texture->TracePasses.Add(Pass->Handle);
}

void FRDGTrace::AddBufferPassDependency(FRDGBuffer* Buffer, FRDGPass* Pass)
{
	if (!IsEnabled())
	{
		return;
	}

	Pass->TraceBuffers.Add(Buffer->Handle);
	Buffer->TracePasses.Add(Pass->Handle);
}

#endif