// Copyright Epic Games, Inc. All Rights Reserved.


#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicMeshOverlay.h"
#include "Async/ParallelFor.h"

using namespace UE::Geometry;

void MeshTransforms::Translate(FDynamicMesh3& Mesh, const FVector3d& Translation)
{
	int NumVertices = Mesh.MaxVertexID();
	ParallelFor(NumVertices, [&](int vid) 
	{
		if (Mesh.IsVertex(vid))
		{
			Mesh.SetVertex(vid, Mesh.GetVertex(vid) + Translation);
		}
	});
}


void MeshTransforms::Scale(FDynamicMesh3& Mesh, const FVector3d& Scale, const FVector3d& Origin, bool bReverseOrientationIfNeeded, bool bOnlyPositions)
{
	bool bVertexNormals = Mesh.HasVertexNormals();
	bool bNeedNormalTangentScaling = !bOnlyPositions && !Scale.IsUniform();
	int NumVertices = Mesh.MaxVertexID();
	FVector3f NormalScale = (FVector3f)Scale;
	if (bNeedNormalTangentScaling)
	{
		// compute a safe inverse-scale to apply to normal vectors
		for (int32 Idx = 0; Idx < 3; ++Idx)
		{
			if (!FMath::IsNearlyZero(NormalScale[Idx]))
			{
				NormalScale[Idx] = 1.0f / NormalScale[Idx];
			}
		}
	}
	ParallelFor(NumVertices, [&](int32 vid)
	{
		if (Mesh.IsVertex(vid))
		{
			Mesh.SetVertex(vid, (Mesh.GetVertex(vid) - Origin) * Scale + Origin);

			if (bNeedNormalTangentScaling && bVertexNormals)
			{
				FVector3f ScaledNormal = Mesh.GetVertexNormal(vid) * NormalScale;
				ScaledNormal.Normalize();
				Mesh.SetVertexNormal(vid, ScaledNormal);
			}
		}
	});
	if (bNeedNormalTangentScaling && Mesh.HasAttributes())
	{
		const int32 NumNormalLayers = FMath::Min(Mesh.Attributes()->NumNormalLayers(), 3);
		for (int32 NormalTangentLayerIdx = 0; NormalTangentLayerIdx < NumNormalLayers; ++NormalTangentLayerIdx)
		{
			FVector3f ScaleBy = (FVector3f)Scale; // tangents are transformed by the scale directly
			if (NormalTangentLayerIdx == 0) // normal is transformed by the inverse scale
			{
				ScaleBy = NormalScale;
			}
			FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->GetNormalLayer(NormalTangentLayerIdx);
			int NumNormals = Normals->MaxElementID();
			ParallelFor(NumNormals, [&](int ElemID)
				{
					if (Normals->IsElement(ElemID))
					{
						FVector3f ScaledNormal = Normals->GetElement(ElemID) * ScaleBy;
						ScaledNormal.Normalize();
						Normals->SetElement(ElemID, ScaledNormal);
					}
				});
		}
	}

	if (bReverseOrientationIfNeeded && Scale.X * Scale.Y * Scale.Z < 0)
	{
		Mesh.ReverseOrientation(false);
	}
}



void MeshTransforms::WorldToFrameCoords(FDynamicMesh3& Mesh, const FFrame3d& Frame)
{
	bool bVertexNormals = Mesh.HasVertexNormals();

	int NumVertices = Mesh.MaxVertexID();
	ParallelFor(NumVertices, [&](int vid)
	{
		if (Mesh.IsVertex(vid))
		{
			FVector3d Position = Mesh.GetVertex(vid);
			Mesh.SetVertex(vid, Frame.ToFramePoint(Position));

			if (bVertexNormals)
			{
				FVector3f Normal = Mesh.GetVertexNormal(vid);
				Mesh.SetVertexNormal(vid, (FVector3f)Frame.ToFrameVector((FVector3d)Normal));
			}
		}
	});

	if (Mesh.HasAttributes())
	{
		for (int32 NormalTangentLayerIdx = 0; NormalTangentLayerIdx < Mesh.Attributes()->NumNormalLayers(); ++NormalTangentLayerIdx)
		{
			// Note: Because this is a rotation transform, normals and tangents transform the same way
			FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->GetNormalLayer(NormalTangentLayerIdx);
			int NumNormals = Normals->MaxElementID();
			ParallelFor(NumNormals, [&](int ElemID)
			{
				if (Normals->IsElement(ElemID))
				{
					FVector3f Normal = Normals->GetElement(ElemID);
					Normals->SetElement(ElemID, (FVector3f)Frame.ToFrameVector((FVector3d)Normal));
				}
			});
		}
	}
}





void MeshTransforms::FrameCoordsToWorld(FDynamicMesh3& Mesh, const FFrame3d& Frame)
{
	bool bVertexNormals = Mesh.HasVertexNormals();

	int NumVertices = Mesh.MaxVertexID();
	ParallelFor(NumVertices, [&](int vid)
	{
		if (Mesh.IsVertex(vid))
		{
			FVector3d Position = Mesh.GetVertex(vid);
			Mesh.SetVertex(vid, Frame.FromFramePoint(Position));

			if (bVertexNormals)
			{
				FVector3f Normal = Mesh.GetVertexNormal(vid);
				Mesh.SetVertexNormal(vid, (FVector3f)Frame.FromFrameVector((FVector3d)Normal));
			}
		}
	});

	if (Mesh.HasAttributes())
	{
		for (int32 NormalTangentLayerIdx = 0; NormalTangentLayerIdx < Mesh.Attributes()->NumNormalLayers(); ++NormalTangentLayerIdx)
		{
			// Note: Because this is a rotation transform, normals and tangents transform the same way
			FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->GetNormalLayer(NormalTangentLayerIdx);
			int NumNormals = Normals->MaxElementID();
			ParallelFor(NumNormals, [&](int ElemID)
			{
				if (Normals->IsElement(ElemID))
				{
					FVector3f Normal = Normals->GetElement(ElemID);
					Normals->SetElement(ElemID, (FVector3f)Frame.FromFrameVector((FVector3d)Normal));
				}
			});
		}
	}
}


void MeshTransforms::Rotate(FDynamicMesh3& Mesh, const FRotator& Rotation, const FVector3d& RotationOrigin)
{
	bool bVertexNormals = Mesh.HasVertexNormals();

	int NumVertices = Mesh.MaxVertexID();
	ParallelFor(NumVertices, [&](int vid)
	{
		if (Mesh.IsVertex(vid)) 
		{
			FVector3d Position = Rotation.RotateVector((Mesh.GetVertex(vid) - RotationOrigin)) + RotationOrigin;
			Mesh.SetVertex(vid, Position);

			if (bVertexNormals)
			{
				FVector3f Normal = Mesh.GetVertexNormal(vid);
				Normal = (FVector3f)Rotation.RotateVector((FVector3d)Normal);
				Mesh.SetVertexNormal(vid, Normal);
			}
		}
	});

	if (Mesh.HasAttributes())
	{
		for (int32 NormalLayerIdx = 0; NormalLayerIdx < Mesh.Attributes()->NumNormalLayers(); ++NormalLayerIdx)
		{
			FDynamicMeshNormalOverlay* NormalTangentLayer = Mesh.Attributes()->GetNormalLayer(NormalLayerIdx);
			int NumElements = NormalTangentLayer->MaxElementID();
			ParallelFor(NumElements, [&](int ElemID)
			{
				if (NormalTangentLayer->IsElement(ElemID))
				{
					FVector3f NormalTangent = NormalTangentLayer->GetElement(ElemID);
					NormalTangent = (FVector3f)Rotation.RotateVector((FVector3d)NormalTangent);
					NormalTangentLayer->SetElement(ElemID, NormalTangent);
				}
			});
		}
	}
}

void MeshTransforms::ApplyTransform(FDynamicMesh3& Mesh, const FTransformSRT3d& Transform, bool bReverseOrientationIfNeeded)
{
	bool bVertexNormals = Mesh.HasVertexNormals();

	int NumVertices = Mesh.MaxVertexID();
	ParallelFor(NumVertices, [&](int vid)
	{
		if (Mesh.IsVertex(vid)) 
		{
			FVector3d Position = Mesh.GetVertex(vid);
			Position = Transform.TransformPosition(Position);
			Mesh.SetVertex(vid, Position);

			if (bVertexNormals)
			{
				FVector3f Normal = Mesh.GetVertexNormal(vid);
				Normal = (FVector3f)Transform.TransformNormal((FVector3d)Normal);
				Mesh.SetVertexNormal(vid, Normal);
			}
		}
	});

	if (Mesh.HasAttributes())
	{
		FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
		if (Normals)
		{
			int NumNormals = Normals->MaxElementID();
			ParallelFor(NumNormals, [&](int ElemID)
			{
				if (Normals->IsElement(ElemID))
				{
					FVector3f Normal = Normals->GetElement(ElemID);
					Normal = (FVector3f)Transform.TransformNormal((FVector3d)Normal);
					Normals->SetElement(ElemID, Normal);
				}
			});
		}
		if (Mesh.Attributes()->HasTangentSpace())
		{
			for (int32 TangentLayerIdx = 1; TangentLayerIdx < 3; ++TangentLayerIdx)
			{
				FDynamicMeshNormalOverlay* TangentLayer = Mesh.Attributes()->GetNormalLayer(TangentLayerIdx);
				int NumTangents = TangentLayer->MaxElementID();
				ParallelFor(NumTangents, [&](int ElemID)
				{
					if (TangentLayer->IsElement(ElemID))
					{
						FVector3f Tangent = TangentLayer->GetElement(ElemID);
						Tangent = (FVector3f)Transform.TransformVector((FVector3d)Tangent);
						TangentLayer->SetElement(ElemID, Normalized(Tangent));
					}
				});
			}
		}
	}

	if (bReverseOrientationIfNeeded && Transform.GetDeterminant() < 0)
	{
		Mesh.ReverseOrientation(false);
	}
}



void MeshTransforms::ApplyTransformInverse(FDynamicMesh3& Mesh, const FTransformSRT3d& Transform, bool bReverseOrientationIfNeeded)
{
	bool bVertexNormals = Mesh.HasVertexNormals();

	int NumVertices = Mesh.MaxVertexID();
	ParallelFor(NumVertices, [&](int vid)
	{
		if (Mesh.IsVertex(vid)) 
		{
			FVector3d Position = Mesh.GetVertex(vid);
			Position = Transform.InverseTransformPosition(Position);
			Mesh.SetVertex(vid, Position);

			if (bVertexNormals)
			{
				FVector3f Normal = Mesh.GetVertexNormal(vid);
				Normal = (FVector3f)Transform.InverseTransformNormal((FVector3d)Normal);
				Mesh.SetVertexNormal(vid, Normal);
			}
		}
	});

	if (Mesh.HasAttributes())
	{
		FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
		if (Normals)
		{
			int NumNormals = Normals->MaxElementID();
			ParallelFor(NumNormals, [&](int ElemID)
			{
				if (Normals->IsElement(ElemID))
				{
					FVector3f Normal = Normals->GetElement(ElemID);
					Normal = (FVector3f)Transform.InverseTransformNormal((FVector3d)Normal);
					Normals->SetElement(ElemID, Normal);
				}
			});
		}
		if (Mesh.Attributes()->HasTangentSpace())
		{
			for (int32 TangentLayerIdx = 1; TangentLayerIdx < 3; ++TangentLayerIdx)
			{
				FDynamicMeshNormalOverlay* TangentLayer = Mesh.Attributes()->GetNormalLayer(TangentLayerIdx);
				int NumTangents = TangentLayer->MaxElementID();
				ParallelFor(NumTangents, [&](int ElemID)
				{
					if (TangentLayer->IsElement(ElemID))
					{
						FVector3f Tangent = TangentLayer->GetElement(ElemID);
						Tangent = (FVector3f)Transform.InverseTransformVector((FVector3d)Tangent);
						TangentLayer->SetElement(ElemID, Normalized(Tangent));
					}
				});
			}
		}
	}

	if (bReverseOrientationIfNeeded && Transform.GetDeterminant() < 0)
	{
		Mesh.ReverseOrientation(false);
	}
}


void MeshTransforms::ReverseOrientationIfNeeded(FDynamicMesh3& Mesh, const FTransformSRT3d& Transform)
{
	if (Transform.GetDeterminant() < 0)
	{
		Mesh.ReverseOrientation(false);
	}
}



void MeshTransforms::ApplyTransform(FDynamicMesh3& Mesh,
	TFunctionRef<FVector3d(const FVector3d&)> PositionTransform,
	TFunctionRef<FVector3f(const FVector3f&)> NormalTransform)
{
	bool bVertexNormals = Mesh.HasVertexNormals();

	int NumVertices = Mesh.MaxVertexID();
	ParallelFor(NumVertices, [&](int vid)
	{
		if (Mesh.IsVertex(vid))
		{
			FVector3d Position = Mesh.GetVertex(vid);
			Position = PositionTransform(Position);
			Mesh.SetVertex(vid, Position);

			if (bVertexNormals)
			{
				FVector3f Normal = Mesh.GetVertexNormal(vid);
				Normal = NormalTransform(Normal);
				Mesh.SetVertexNormal(vid, Normalized(Normal));
			}
		}
	});

	if (Mesh.HasAttributes())
	{
		FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
		if (Normals)
		{
			int NumNormals = Normals->MaxElementID();
			ParallelFor(NumNormals, [&](int ElemID)
			{
				if (Normals->IsElement(ElemID))
				{
					FVector3f Normal = Normals->GetElement(ElemID);
					Normal = NormalTransform(Normal);
					Normals->SetElement(ElemID, Normalized(Normal));
				}
			});
		}
	}
}

void MeshTransforms::ApplyTransform(FDynamicMesh3& Mesh,
	TFunctionRef<FVector3d(const FVector3d&)> PositionTransform,
	TFunctionRef<FVector3f(const FVector3f&)> NormalTransform,
	TFunctionRef<FVector3f(const FVector3f&)> TangentTransform)
{
	bool bVertexNormals = Mesh.HasVertexNormals();

	int NumVertices = Mesh.MaxVertexID();
	ParallelFor(NumVertices, [&](int vid)
	{
		if (Mesh.IsVertex(vid))
		{
			FVector3d Position = Mesh.GetVertex(vid);
			Position = PositionTransform(Position);
			Mesh.SetVertex(vid, Position);

			if (bVertexNormals)
			{
				FVector3f Normal = Mesh.GetVertexNormal(vid);
				Normal = NormalTransform(Normal);
				Mesh.SetVertexNormal(vid, Normalized(Normal));
			}
		}
	});

	if (Mesh.HasAttributes())
	{
		FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
		if (Normals)
		{
			int NumNormals = Normals->MaxElementID();
			ParallelFor(NumNormals, [&](int ElemID)
			{
				if (Normals->IsElement(ElemID))
				{
					FVector3f Normal = Normals->GetElement(ElemID);
					Normal = NormalTransform(Normal);
					Normals->SetElement(ElemID, Normalized(Normal));
				}
			});
		}
		if (Mesh.Attributes()->HasTangentSpace())
		{
			for (int32 TangentLayerIdx = 1; TangentLayerIdx < 3; ++TangentLayerIdx)
			{
				FDynamicMeshNormalOverlay* TangentLayer = Mesh.Attributes()->GetNormalLayer(TangentLayerIdx);
				int32 NumTangents = TangentLayer->MaxElementID();
				ParallelFor(NumTangents, [&](int ElemID)
				{
					if (TangentLayer->IsElement(ElemID))
					{
						FVector3f Tangent = TangentLayer->GetElement(ElemID);
						Tangent = TangentTransform(Tangent);
						TangentLayer->SetElement(ElemID, Normalized(Tangent));
					}
				});
			}
		}
	}
}

