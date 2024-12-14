// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/MathFwd.h"

namespace mu
{
class Mesh;

/**  */
extern void MeshTransformWithMesh(Mesh* Result, const Mesh* SourceMesh, const Mesh* BoundingMesh, const FMatrix44f& Transform, bool& bOutSuccess);

}
