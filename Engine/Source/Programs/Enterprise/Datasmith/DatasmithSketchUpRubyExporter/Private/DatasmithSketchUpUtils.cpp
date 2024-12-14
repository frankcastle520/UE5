// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpUtils.h"
#include "DatasmithSketchUpCommon.h"
#include "DatasmithSketchUpString.h"
#include "DatasmithSketchUpSummary.h"

#include "IDatasmithSceneElements.h"

// SketchUp SDK.
#include "DatasmithSketchUpSDKBegins.h"

#include "SketchUpAPI/model/camera.h"
#include "SketchUpAPI/model/component_definition.h"
#include "SketchUpAPI/model/component_instance.h"
#include "SketchUpAPI/model/drawing_element.h"
#include "SketchUpAPI/model/edge.h"
#include "SketchUpAPI/model/entities.h"
#include "SketchUpAPI/model/entity.h"
#include "SketchUpAPI/model/face.h"
#include "SketchUpAPI/model/geometry.h"
#include "SketchUpAPI/model/group.h"
#include "SketchUpAPI/model/layer.h"
#if !defined(SKP_SDK_2019) && !defined(SKP_SDK_2020)
#include "SketchUpAPI/model/layer_folder.h"
#endif
#include "SketchUpAPI/model/material.h"
#include "SketchUpAPI/model/mesh_helper.h"
#include "SketchUpAPI/model/model.h"
#include "SketchUpAPI/model/scene.h"
#include "SketchUpAPI/model/texture.h"
#include "SketchUpAPI/model/uv_helper.h"

#include "DatasmithSketchUpSDKCeases.h"

// Imath third party library.
#include "Algo/Compare.h"
#include "Algo/Copy.h"
#include "Imath/ImathMatrixAlgo.h"



using namespace DatasmithSketchUp;

DatasmithSketchUp::FEntityIDType DatasmithSketchUpUtils::GetEntityID(SUEntityRef InEntityRef)
{
	int32 Id = 0;
	SUEntityGetID(InEntityRef, &Id); // we can ignore the returned SU_RESULT
	return FEntityIDType{ Id };
}

SULayerRef DatasmithSketchUpUtils::GetEffectiveLayer(
	SUComponentInstanceRef InComponentInstanceRef, // valid SketckUp component instance
	SULayerRef             InInheritedLayerRef
)
{
	SUDrawingElementRef DrawingElementRef = SUComponentInstanceToDrawingElement(InComponentInstanceRef);

	return GetEffectiveLayer(DrawingElementRef, InInheritedLayerRef);
}

SULayerRef DatasmithSketchUpUtils::GetEffectiveLayer(SUDrawingElementRef DrawingElementRef, SULayerRef InInheritedLayerRef)
{
	// Retrieve the SketckUp component instance layer.
	SULayerRef SComponentInstanceLayerRef = SU_INVALID;
	SUDrawingElementGetLayer(DrawingElementRef, &SComponentInstanceLayerRef); // we can ignore the returned SU_RESULT

	// Retrieve the SketchUp component instance layer name.
	FString SComponentInstanceLayerName;
	SComponentInstanceLayerName = SuGetString(SULayerGetName, SComponentInstanceLayerRef);

	// Return the effective layer.
	return SComponentInstanceLayerName.Equals(TEXT("Layer0")) ? InInheritedLayerRef : SComponentInstanceLayerRef;
}



FComponentDefinitionIDType DatasmithSketchUpUtils::GetComponentID(
	SUComponentDefinitionRef InComponentDefinitionRef
)
{
	return GetEntityID(SUComponentDefinitionToEntity(InComponentDefinitionRef));
}

FComponentInstanceIDType DatasmithSketchUpUtils::GetComponentInstanceID(
	SUComponentInstanceRef InComponentInstanceRef
)
{
	return GetEntityID(SUComponentInstanceToEntity(InComponentInstanceRef));
}

FComponentInstanceIDType DatasmithSketchUpUtils::GetGroupID(
	SUGroupRef InGroupRef
)
{
	return GetEntityID(SUGroupToEntity(InGroupRef));
}

int64 DatasmithSketchUpUtils::GetComponentPID(
	SUComponentInstanceRef InComponentInstanceRef
)
{
	// Get the SketckUp component instance persistent ID.
	int64 SPersistentID = 0;
	SUEntityGetPersistentID(SUComponentInstanceToEntity(InComponentInstanceRef), &SPersistentID); // we can ignore the returned SU_RESULT

	return SPersistentID;
}

FEntityIDType DatasmithSketchUpUtils::GetSceneID(
	SUSceneRef InSceneRef
)
{
	return GetEntityID(SUSceneToEntity(InSceneRef));;
}

FEntityIDType  DatasmithSketchUpUtils::GetMaterialID(
	SUMaterialRef InMaterialRef
)
{
	return GetEntityID(SUMaterialToEntity(InMaterialRef));
}


int32 DatasmithSketchUpUtils::GetFaceID(
	SUFaceRef InFaceRef
)
{
	// Get the SketckUp face ID.
	int32 SFaceID = 0;
	SUEntityGetID(SUFaceToEntity(InFaceRef), &SFaceID); // we can ignore the returned SU_RESULT

	return SFaceID;
}

int32 DatasmithSketchUpUtils::GetEdgeID(
	SUEdgeRef InEdgeRef
)
{
	// Get the SketckUp edge ID.
	int32 SEdgeID = 0;
	SUEntityGetID(SUEdgeToEntity(InEdgeRef), &SEdgeID); // we can ignore the returned SU_RESULT

	return SEdgeID;
}

bool DatasmithSketchUpUtils::IsVisible(
	SUComponentInstanceRef InSComponentInstanceRef,
	SULayerRef             InSEffectiveLayerRef
)
{
	// Get the flag indicating whether or not the SketchUp component instance is hidden.
	bool bSComponentInstanceHidden = false;
	SUDrawingElementGetHidden(SUComponentInstanceToDrawingElement(InSComponentInstanceRef), &bSComponentInstanceHidden); // we can ignore the returned SU_RESULT

	// Get the flag indicating whether or not the SketchUp component instance effective layer is visible.
	bool bSEffectiveLayerVisible = true;
	SULayerGetVisibility(InSEffectiveLayerRef, &bSEffectiveLayerVisible); // we can ignore the returned SU_RESULT

	return (!bSComponentInstanceHidden && bSEffectiveLayerVisible);
}

bool DatasmithSketchUpUtils::IsLayerVisible(
	SULayerRef LayerRef
)
{
	bool bVisible = true;
	SULayerGetVisibility(LayerRef, &bVisible);

	// Search for invisible ancestor folder (parent invisibility overrides child's visibility) 
	// LayerFolder introduced in SketchUp 2021
#if !defined(SKP_SDK_2019) && !defined(SKP_SDK_2020)
	SULayerFolderRef LayerFolderRef = SU_INVALID;
	SULayerGetParentLayerFolder(LayerRef, &LayerFolderRef);
	while (bVisible && SUIsValid(LayerFolderRef))
	{
		bool bLayerFolderVisible = true;
		SULayerFolderGetVisibility(LayerFolderRef, &bLayerFolderVisible);
		bVisible = bVisible && bLayerFolderVisible;

		SULayerFolderRef ParentLayerFolderRef = SU_INVALID;
		SULayerFolderGetParentLayerFolder(LayerFolderRef, &ParentLayerFolderRef);
		LayerFolderRef = ParentLayerFolderRef;
	}
#endif
	return bVisible;
}

SUMaterialRef DatasmithSketchUpUtils::GetMaterial(
	SUComponentInstanceRef InComponentInstanceRef
)
{
	// Retrieve the SketckUp drawing element material.
	SUMaterialRef SMaterialRef = SU_INVALID;
	SUDrawingElementGetMaterial(SUComponentInstanceToDrawingElement(InComponentInstanceRef), &SMaterialRef); // we can ignore the returned SU_RESULT

	return SMaterialRef;
}

void DatasmithSketchUpUtils::SetActorTransform(
	const TSharedPtr<IDatasmithActorElement>& InActorElement,
	SUTransformation const& InWorldTransform
)
{
	FVector Translation{};
	FQuat Rotation{};
	FVector Scale{};
	FVector Shear{};

	if (DecomposeTransform(InWorldTransform, Translation, Rotation, Scale, Shear))
	{
		if (Rotation.IsNormalized())
		{
			InActorElement->SetTranslation(Translation.X, Translation.Y, Translation.Z, false);
			InActorElement->SetRotation(Rotation.X, Rotation.Y, Rotation.Z, Rotation.W, false);
			InActorElement->SetScale(Scale.X, Scale.Y, Scale.Z, false);
		}
		else
		{
			ToRuby::LogWarn(
				FString::Printf(TEXT("WARNING: Actor %ls (%ls) has unnormalized rotation"), 
					InActorElement->GetName(), InActorElement->GetLabel()));
		}

	}
	else
	{
		ToRuby::LogWarn(
			FString::Printf(TEXT("WARNING: Actor %ls (%ls) has some zero scaling or rotation can't be converted"), 
				InActorElement->GetName(), InActorElement->GetLabel()));
	}
}

bool DatasmithSketchUpUtils::DecomposeTransform(
	SUTransformation const& InWorldTransform, // SketchUp world transform to convert
	FVector& OutTranslation,
	FQuat& OutRotation,
	FVector& OutScale,
	FVector& OutShear)
{
	// We use Imath::extractAndRemoveScalingAndShear() because FMatrix::ExtractScaling() is deemed unreliable.

	// Set up a scaling and rotation matrix.
	auto& SMatrix = InWorldTransform.values;
	Imath::Matrix44<float> Matrix(float(SMatrix[0]), float(SMatrix[1]), float(SMatrix[2]), 0.0,
		float(SMatrix[4]), float(SMatrix[5]), float(SMatrix[6]), 0.0,
		float(SMatrix[8]), float(SMatrix[9]), float(SMatrix[10]), 0.0,
		0.0, 0.0, 0.0, 1.0);

	// Remove any scaling from the matrix and get the scale vector that was initially present.
	Imath::Vec3<float> Scale;
	Imath::Vec3<float> Shear;
	if (!Imath::extractAndRemoveScalingAndShear<float>(Matrix, Scale, Shear, false))
	{
		// Scaling is zero
		return false;
	}

	if (SMatrix[15] != 1.0)
	{
		// Apply the extra SketchUp uniform scaling factor.
		Scale *= SMatrix[15];
	}

	OutScale = FVector(Scale.x, Scale.y, Scale.z);
	OutShear = FVector(Shear.x, Shear.y, Shear.z);

	// Initialize a rotation quaternion with the rotation matrix.
	Imath::Quat<float> Quaternion = Imath::extractQuat<float>(Matrix);

	// Convert the SketchUp right-handed Z-up coordinate rotation into an Unreal left-handed Z-up coordinate rotation.
	// This is done by inverting the X and Z components of the quaternion to mirror the quaternion on the XZ-plane.
	Quaternion.v.x = -Quaternion.v.x;
	Quaternion.v.z = -Quaternion.v.z;
	Quaternion.normalize();

	// Make sure Unreal will be able to handle the rotation quaternion.
	float              Angle = Quaternion.angle();
	Imath::Vec3<float> Axis = Quaternion.axis();
	OutRotation = FQuat(FVector(Axis.x, Axis.y, Axis.z), Angle);

	// Convert the SketchUp right-handed Z-up coordinate translation into an Unreal left-handed Z-up coordinate translation.
	// To avoid perturbating X, which is forward in Unreal, the handedness conversion is done by flipping the side vector Y.
	// SketchUp uses inches as internal system unit for all 3D coordinates in the model while Unreal uses centimeters.
	OutTranslation = FromSketchUp::ConvertPosition(SMatrix[12], SMatrix[13], SMatrix[14]);

	return true;
}

bool DatasmithSketchUpUtils::SplitTransform(
	SUTransformation const& InWorldTransform, // SketchUp world transform to convert
	SUTransformation& OutWorldTransform,
	SUTransformation& OutMeshActorWorldTransform,
	SUTransformation& OutBakeTransform
)
{
	// Set up a scaling and rotation matrix.
	auto& SMatrix = InWorldTransform.values;
	Imath::Matrix44<double> Matrix(
		SMatrix[0], SMatrix[1],  SMatrix[2], 0.0,
		SMatrix[4], SMatrix[5],  SMatrix[6], 0.0,
		SMatrix[8], SMatrix[9], SMatrix[10], 0.0,
		       0.0,        0.0,         0.0, 1.0);

	// Remove any scaling from the matrix and get the scale vector that was initially present.
	Imath::Vec3<double> Scale;
	Imath::Vec3<double> Shear;
	if (!Imath::extractAndRemoveScalingAndShear<double>(Matrix, Scale, Shear, false))
	{
		// Scaling is zero
		return false;
	}
	// Scale/Shear removed, now it's just Rotation
	Imath::Matrix44<double> RotationMatrix(Matrix);

	Imath::Matrix44<double> TranslationMatrix;
	TranslationMatrix.translate(Imath::Vec3<double>(SMatrix[12], SMatrix[13], SMatrix[14]));

	if (SMatrix[15] > UE_SMALL_NUMBER)
	{
		// Apply the extra SketchUp uniform (inverse)scaling factor.
		Scale /= SMatrix[15];
	}

	Imath::Matrix44<double> ScaleMatrix;
	ScaleMatrix.scale(Scale);

	Imath::Matrix44<double> ShearMatrix;
	ShearMatrix.shear(Shear);

	Imath::Matrix44<double> ActorMatrix = RotationMatrix*TranslationMatrix;
	Imath::Matrix44<double> MeshActorMatrix = RotationMatrix*TranslationMatrix;

	auto CopyImathMatrixToSU = [](const Imath::Matrix44<double>& Source, SUTransformation& Dest)
	{
		for (int32 Index=0; Index<16; ++Index)
		{
			Dest.values[Index] = Source.getValue()[Index];
		}
	};

	CopyImathMatrixToSU(ActorMatrix, OutWorldTransform);
	CopyImathMatrixToSU(MeshActorMatrix, OutMeshActorWorldTransform);
	CopyImathMatrixToSU(ScaleMatrix*ShearMatrix, OutBakeTransform);

	return true;
}


SUTransformation DatasmithSketchUpUtils::GetComponentInstanceTransform(SUComponentInstanceRef InComponentInstanceRef, SUTransformation const& InWorldTransform)
{
	// Get the SketchUp component instance transform.
	SUTransformation SComponentInstanceTransform;
	SUComponentInstanceGetTransform(InComponentInstanceRef, &SComponentInstanceTransform); // we can ignore the returned SU_RESULT

	// Compute the world transform of the SketchUp component instance.
	SUTransformation SComponentInstanceWorldTransform;
	SUTransformationMultiply(&InWorldTransform, &SComponentInstanceTransform, &SComponentInstanceWorldTransform); // we can ignore the returned SU_RESULT
	return SComponentInstanceWorldTransform;
}

bool DatasmithSketchUpUtils::CompareSUTransformations(const SUTransformation& A, const SUTransformation& B)
{
	return Algo::Compare(
		MakeArrayView(A.values, std::size(A.values)), 
		MakeArrayView(B.values, std::size(B.values)),
		[](const double& A, const double& B) {  return FMath::Abs(A-B) < UE_KINDA_SMALL_NUMBER; });
}

