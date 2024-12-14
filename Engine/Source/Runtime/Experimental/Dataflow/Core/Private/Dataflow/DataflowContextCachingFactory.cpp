// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowContextCachingFactory.h"

#include "Dataflow/DataflowNodeParameters.h"
#include "Logging/LogMacros.h"
#include "Misc/MessageDialog.h"

DEFINE_LOG_CATEGORY_STATIC(LogDataflowContextCachingFactory, Warning, All);


namespace UE::Dataflow
{
	FContextCachingFactory* FContextCachingFactory::Instance = nullptr;

	void FContextCachingFactory::RegisterSerializeFunction(const FName& Type, FSerializeFunction InSerializeFunc)
	{
		if (CachingMap.Contains(Type))
		{
			UE_LOG(LogDataflowContextCachingFactory, Warning,
				TEXT("Warning : Dataflow output caching registration conflicts with "
					"existing type(%s)"), *Type.ToString());
		}
		else
		{
			CachingMap.Add(Type, InSerializeFunc);
		}
	}


	FContextCacheElementBase* FContextCachingFactory::Serialize(FArchive& Ar, FContextCacheData&& Element)
	{
		FContextCacheElementBase* RetVal = nullptr;
		if (CachingMap.Contains(Element.Type))
		{
			RetVal = CachingMap[Element.Type](Ar, Element.Data);
			if (Ar.IsSaving())
			{
				check(RetVal == nullptr);
			}
			else if( Ar.IsLoading())
			{
				check(RetVal != nullptr);
			}
		}
		else
		{
			UE_LOG(LogDataflowContextCachingFactory, Warning,
				TEXT("Warning : Dataflow missing context chaching callback type(%s)"), *Element.Type.ToString());
		}
		return RetVal;
	}
}

