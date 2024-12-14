// Copyright Epic Games, Inc. All Rights Reserved.

#include "Interfaces/IHttpRequest.h"

class FArchiveWithDelegateV2 final : public FArchive
{
public:
	FArchiveWithDelegateV2(FHttpRequestStreamDelegateV2 InStreamDelegateV2)
		: StreamDelegateV2(InStreamDelegateV2)
	{
	}

	virtual void Serialize(void* V, int64 Length) override
	{
		int64 LengthProcessed = Length;
		StreamDelegateV2.ExecuteIfBound(V, LengthProcessed);
		if (LengthProcessed != Length)
		{
			SetError();
		}
	}

private:
	FHttpRequestStreamDelegateV2 StreamDelegateV2;
};


PRAGMA_DISABLE_DEPRECATION_WARNINGS 
bool IHttpRequest::SetContentFromStreamDelegate(FHttpRequestStreamDelegate StreamDelegate) 
{ 
	return SetContentFromStream(MakeShared<FArchiveWithDelegate>(StreamDelegate)); 
}

bool IHttpRequest::SetResponseBodyReceiveStreamDelegate(FHttpRequestStreamDelegate StreamDelegate) 
{ 
	return SetResponseBodyReceiveStream(MakeShared<FArchiveWithDelegate>(StreamDelegate)); 
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS 

bool IHttpRequest::SetResponseBodyReceiveStreamDelegateV2(FHttpRequestStreamDelegateV2 StreamDelegate) 
{ 
	return SetResponseBodyReceiveStream(MakeShared<FArchiveWithDelegateV2>(StreamDelegate)); 
}
