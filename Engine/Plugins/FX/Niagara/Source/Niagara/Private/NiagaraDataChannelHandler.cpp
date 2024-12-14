// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataChannelHandler.h"
#include "NiagaraDataChannelAccessor.h"
#include "NiagaraDataChannelCommon.h"
#include "Logging/StructuredLog.h"
#include "NiagaraGpuComputeDispatchInterface.h"

void UNiagaraDataChannelHandler::BeginDestroy()
{
	Super::BeginDestroy();
	Cleanup();
	
	RTFence.BeginFence();
}

bool UNiagaraDataChannelHandler::IsReadyForFinishDestroy()
{
	return RTFence.IsFenceComplete() && Super::IsReadyForFinishDestroy();
}

void UNiagaraDataChannelHandler::Init(const UNiagaraDataChannel* InChannel)
{
	DataChannel = InChannel;
}

void UNiagaraDataChannelHandler::Cleanup()
{
	if(Reader)
	{
		Reader->Cleanup();
		Reader = nullptr;
	}
	
	if(Writer)
	{
		Writer->Cleanup();
		Writer = nullptr;
	}

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		//Mark this handler as garbage so any reading DIs will know to stop using it.
		MarkAsGarbage();
	}
}

void UNiagaraDataChannelHandler::BeginFrame(float DeltaTime, FNiagaraWorldManager* OwningWorld)
{
	CurrentTG = TG_PrePhysics;

	for(auto It = WeakDataArray.CreateIterator(); It; ++It)
	{
		if(It->IsValid() == false)
		{
			It.RemoveCurrentSwap();
		}
	}
}

void UNiagaraDataChannelHandler::EndFrame(float DeltaTime, FNiagaraWorldManager* OwningWorld)
{

}

void UNiagaraDataChannelHandler::Tick(float DeltaTime, ETickingGroup TickGroup, FNiagaraWorldManager* OwningWorld)
{
	CurrentTG = TickGroup;
}

UNiagaraDataChannelWriter* UNiagaraDataChannelHandler::GetDataChannelWriter()
{
	if(Writer == nullptr)
	{
		Writer =  NewObject<UNiagaraDataChannelWriter>();
		Writer->Owner = this;
	}
	return Writer;
}

UNiagaraDataChannelReader* UNiagaraDataChannelHandler::GetDataChannelReader()
{
	if (Reader == nullptr)
	{
		Reader = NewObject<UNiagaraDataChannelReader>();
		Reader->Owner = this;
	}
	return Reader;
}

FNiagaraDataChannelDataPtr UNiagaraDataChannelHandler::CreateData()
{
	FNiagaraDataChannelDataPtr Ret = MakeShared<FNiagaraDataChannelData>();
	WeakDataArray.Add(Ret);
	Ret->Init(this);
	return Ret;
}

void UNiagaraDataChannelHandler::OnComputeDispatchInterfaceDestroyed(FNiagaraGpuComputeDispatchInterface* InComputeDispatchInterface)
{
	//Destroy all RT proxies when the dispatcher is destroyed.
	//In cases where this is done on a running world, we'll do a lazy reinit next frame.
	ForEachNDCData([InComputeDispatchInterface](FNiagaraDataChannelDataPtr& NDCData)
	{
		check(NDCData);
		NDCData->DestroyRenderThreadProxy(InComputeDispatchInterface);
	});
}