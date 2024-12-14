// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowContextObject.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowContextObject)


void UDataflowContextObject::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UDataflowContextObject* This = CastChecked<UDataflowContextObject>(InThis);
	Collector.AddReferencedObject(This->SelectedNode);
	Super::AddReferencedObjects(InThis, Collector);
}

