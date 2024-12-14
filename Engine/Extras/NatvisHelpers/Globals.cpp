// Copyright Epic Games, Inc. All Rights Reserved.

typedef unsigned char uint8;
class FChunkedFixedUObjectArray;

uint8** GNameBlocksDebug = nullptr;
FChunkedFixedUObjectArray* GObjectArrayForDebugVisualizers = nullptr;

namespace UE { namespace Core { struct FVisualizerDebuggingState; } }
UE::Core::FVisualizerDebuggingState* GDebuggingState = nullptr;