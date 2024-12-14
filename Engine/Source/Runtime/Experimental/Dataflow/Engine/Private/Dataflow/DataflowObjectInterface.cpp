// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowObjectInterface.h"

namespace UE::Dataflow
{
	template class TEngineContext<FContextSingle>;
	template class TEngineContext<FContextThreaded>;
}
