// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshEdges.h"

class FRDGBuilder;
class FViewInfo;
struct FCompositePrimitiveInputs;
struct FScreenPassRenderTarget;

void ComposeMeshEdges(FRDGBuilder& GraphBuilder,
						const FViewInfo& View,
						FScreenPassRenderTarget& EditorPrimitivesColor,
						FScreenPassRenderTarget& EditorPrimitivesDepth);
