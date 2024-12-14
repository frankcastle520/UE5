// Copyright Epic Games, Inc. All Rights Reserved.

#include "Channels/DoubleChannelKeyProxy.h"

#include "HAL/PlatformCrt.h"

struct FPropertyChangedEvent;

void UDoubleChannelKeyProxy::Initialize(FKeyHandle InKeyHandle, TMovieSceneChannelHandle<FMovieSceneDoubleChannel> InChannelHandle, TWeakObjectPtr<UMovieSceneSignedObject> InWeakSignedObject)
{
	KeyHandle          = InKeyHandle;
	ChannelHandle      = InChannelHandle;
	WeakSignedObject   = InWeakSignedObject;
}


void UDoubleChannelKeyProxy::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	OnProxyValueChanged(ChannelHandle, WeakSignedObject.Get(), KeyHandle, Value, Time);
}

void UDoubleChannelKeyProxy::UpdateValuesFromRawData()
{
	RefreshCurrentValue(ChannelHandle, KeyHandle, Value, Time);
}
