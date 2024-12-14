// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFramework/ActorPrimitiveColorHandler.h"
#include "GameFramework/Actor.h"
#include "Misc/LazySingleton.h"
#include "EngineUtils.h"

#define LOCTEXT_NAMESPACE "ActorColoration"

FActorPrimitiveColorHandler::FActorPrimitiveColorHandler()
{
#if ENABLE_ACTOR_PRIMITIVE_COLOR_HANDLER
	InitActivePrimitiveColorHandler();
#endif
}

void FActorPrimitiveColorHandler::InitActivePrimitiveColorHandler()
{
#if ENABLE_ACTOR_PRIMITIVE_COLOR_HANDLER
	ActivePrimitiveColorHandlerName = NAME_None;
	ActivePrimitiveColorHandler = nullptr;
#endif
}

FActorPrimitiveColorHandler& FActorPrimitiveColorHandler::Get()
{
	return TLazySingleton<FActorPrimitiveColorHandler>::Get();
}

void FActorPrimitiveColorHandler::RegisterPrimitiveColorHandler(FName InHandlerName, FText InHandlerText, const FGetColorFunc& InHandlerFunc, const FActivateFunc& InActivateFunc, FText InHandlerToolTipText)
{
	RegisterPrimitiveColorHandler(InHandlerName, InHandlerText, true, InHandlerFunc, InActivateFunc, InHandlerToolTipText);
}

void FActorPrimitiveColorHandler::RegisterPrimitiveColorHandler(FName InHandlerName, FText InHandlerText, bool bInAvailalbleInEditor, const FGetColorFunc& InHandlerFunc, const FActivateFunc& InActivateFunc, FText InHandlerToolTipText)
{
#if ENABLE_ACTOR_PRIMITIVE_COLOR_HANDLER
	check(!Handlers.Contains(InHandlerName));
	Handlers.Add(InHandlerName, { InHandlerName, InHandlerText, bInAvailalbleInEditor, InHandlerFunc, InActivateFunc, InHandlerToolTipText });
#endif
}

void FActorPrimitiveColorHandler::UnregisterPrimitiveColorHandler(FName InHandlerName)
{
#if ENABLE_ACTOR_PRIMITIVE_COLOR_HANDLER
	check(!InHandlerName.IsNone());
	check(Handlers.Contains(InHandlerName));
	Handlers.Remove(InHandlerName);
	
	if (InHandlerName == ActivePrimitiveColorHandlerName)
	{
		InitActivePrimitiveColorHandler();
	}

#endif
}

bool FActorPrimitiveColorHandler::SetActivePrimitiveColorHandler(FName InHandlerName, UWorld* InWorld)
{
#if ENABLE_ACTOR_PRIMITIVE_COLOR_HANDLER
	if (FPrimitiveColorHandler* NewActivePrimitiveColorHandler = Handlers.Find(InHandlerName); NewActivePrimitiveColorHandler && (NewActivePrimitiveColorHandler != ActivePrimitiveColorHandler))
	{
		ActivePrimitiveColorHandlerName = InHandlerName;
		ActivePrimitiveColorHandler = NewActivePrimitiveColorHandler;
		NewActivePrimitiveColorHandler->ActivateFunc();
		RefreshPrimitiveColorHandler(InHandlerName, InWorld);
		return true;
	}
#endif

	return false;
}

void FActorPrimitiveColorHandler::RefreshPrimitiveColorHandler(FName InHandlerName, UWorld* InWorld)
{
#if ENABLE_ACTOR_PRIMITIVE_COLOR_HANDLER
	if (ActivePrimitiveColorHandlerName == InHandlerName)
	{
		for (TActorIterator<AActor> It(InWorld); It; ++It)
		{
			It->ForEachComponent<UPrimitiveComponent>(false, [this](UPrimitiveComponent* PrimitiveComponent)
			{
				if (PrimitiveComponent->IsRegistered())
				{
					PrimitiveComponent->PushPrimitiveColorToProxy(GetPrimitiveColor(PrimitiveComponent));
				}
			});
		}
	}
#endif
}

void FActorPrimitiveColorHandler::RefreshPrimitiveColorHandler(FName InHandlerName, const TArray<AActor*>& InActors)
{
#if ENABLE_ACTOR_PRIMITIVE_COLOR_HANDLER
	if (ActivePrimitiveColorHandlerName == InHandlerName)
	{
		for (AActor* Actor : InActors)
		{
			Actor->ForEachComponent<UPrimitiveComponent>(false, [this](UPrimitiveComponent* PrimitiveComponent)
			{
				if (PrimitiveComponent->IsRegistered())
				{
					PrimitiveComponent->PushPrimitiveColorToProxy(GetPrimitiveColor(PrimitiveComponent));
				}
			});
		}
	}
#endif
}

void FActorPrimitiveColorHandler::RefreshPrimitiveColorHandler(FName InHandlerName, const TArray<UPrimitiveComponent*>& InPrimitiveComponents)
{
#if ENABLE_ACTOR_PRIMITIVE_COLOR_HANDLER
	if (ActivePrimitiveColorHandlerName == InHandlerName)
	{
		for (UPrimitiveComponent* PrimitiveComponent : InPrimitiveComponents)
		{
			if (PrimitiveComponent && PrimitiveComponent->IsRegistered())
			{
				PrimitiveComponent->PushPrimitiveColorToProxy(GetPrimitiveColor(PrimitiveComponent));
			}
		}
	}
#endif
}

FName FActorPrimitiveColorHandler::GetActivePrimitiveColorHandler() const
{
#if ENABLE_ACTOR_PRIMITIVE_COLOR_HANDLER
	return ActivePrimitiveColorHandlerName;
#else
	return NAME_None;
#endif
}

FText FActorPrimitiveColorHandler::GetActivePrimitiveColorHandlerDisplayName() const
{
#if ENABLE_ACTOR_PRIMITIVE_COLOR_HANDLER
	return ActivePrimitiveColorHandler != nullptr ? ActivePrimitiveColorHandler->HandlerText : FText();
#else
	return FText();
#endif
}

void FActorPrimitiveColorHandler::GetRegisteredPrimitiveColorHandlers(TArray<FPrimitiveColorHandler>& OutPrimitiveColorHandlers) const
{
#if ENABLE_ACTOR_PRIMITIVE_COLOR_HANDLER
	Handlers.GenerateValueArray(OutPrimitiveColorHandlers);
#endif
}

FLinearColor FActorPrimitiveColorHandler::GetPrimitiveColor(const UPrimitiveComponent* InPrimitiveComponent) const
{
#if ENABLE_ACTOR_PRIMITIVE_COLOR_HANDLER
	return ActivePrimitiveColorHandler != nullptr ? ActivePrimitiveColorHandler->GetColorFunc(InPrimitiveComponent) : FLinearColor::White;
#else
	return FLinearColor::White;
#endif
}

#undef LOCTEXT_NAMESPACE