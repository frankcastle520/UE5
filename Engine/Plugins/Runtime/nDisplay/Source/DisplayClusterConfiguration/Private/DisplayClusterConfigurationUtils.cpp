// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfigurationUtils.h"

#include "UObject/Object.h"
#include "UObject/UObjectThreadContext.h"


bool FDisplayClusterConfigurationUtils::IsSerializingTemplate(const FArchive& Ar)
{
	const FUObjectSerializeContext* SerializeContext = FUObjectThreadContext::Get().GetSerializeContext();
	const UObject* SerializedObject = SerializeContext ? SerializeContext->SerializedObject : nullptr;

	// We assume that if the SerializedObject is null, it is indicative of a template.
	return SerializedObject ? SerializedObject->IsTemplate() : true;
}
