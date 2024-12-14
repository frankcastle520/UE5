// Copyright Epic Games, Inc. All Rights Reserved.

#include "TargetInterfaces/DynamicMeshProvider.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "TargetInterfaces/MeshTargetInterfaceTypes.h"

#define LOCTEXT_NAMESPACE "DynamicMeshProvider"

using namespace UE::Geometry;

FDynamicMesh3 IDynamicMeshProvider::GetDynamicMesh(bool bRequestTangents)
{
	FGetMeshParameters GetMeshParams;
	GetMeshParams.bWantMeshTangents = bRequestTangents;
	return GetDynamicMesh(GetMeshParams);
}

FDynamicMesh3 IDynamicMeshProvider::GetDynamicMesh(const FGetMeshParameters& InGetMeshParams)
{
	return GetDynamicMesh();
}

#undef LOCTEXT_NAMESPACE 