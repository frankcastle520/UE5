// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithSketchUpCommon.h"


namespace DatasmithSketchUpUtils
{
	namespace FromSketchUp
	{
		static FORCEINLINE FVector ConvertDirection(const SUVector3D& V)
		{
			return FVector(float(V.x), float(-V.y), float(V.z));
		}

		static FORCEINLINE FVector ConvertPosition(double X, double Y, double Z)
		{
			const float UnitScaleSketchupToUnreal = 2.54; // centimeters per inch
			return FVector(float(X * UnitScaleSketchupToUnreal), float(-Y * UnitScaleSketchupToUnreal), float(Z * UnitScaleSketchupToUnreal));
		}

		static FORCEINLINE FVector ConvertPosition(const SUPoint3D& V)
		{
			return ConvertPosition(V.x, V.y, V.z);
		}
	}

	DatasmithSketchUp::FEntityIDType GetEntityID(
		SUEntityRef InSuEntityRef // valid SketckUp component definition
	);

	DatasmithSketchUp::FComponentDefinitionIDType GetComponentID(
		SUComponentDefinitionRef InSComponentDefinitionRef // valid SketckUp component definition
	);

	DatasmithSketchUp::FComponentInstanceIDType GetComponentInstanceID(
		SUComponentInstanceRef ComponentInstanceRef
	);

	DatasmithSketchUp::FComponentInstanceIDType GetGroupID(
		SUGroupRef GroupRef
	);

	// Get the face ID of a SketckUp face.
	int32 GetFaceID(
		SUFaceRef InSFaceRef // valid SketckUp face
	);

	// Get the edge ID of a SketckUp edge.
	int32 GetEdgeID(
		SUEdgeRef InSEdgeRef // valid SketckUp edge
	);

	// Get the material ID of a SketckUp material.
	DatasmithSketchUp::FMaterialIDType GetMaterialID(
		SUMaterialRef InSMaterialRef // valid SketckUp material
	);

	// Get the camera ID of a SketckUp scene.
	DatasmithSketchUp::FEntityIDType GetSceneID(
		SUSceneRef InSSceneRef // valid SketckUp scene
	);

	// Get the component persistent ID of a SketckUp component instance.
	int64 GetComponentPID(
		SUComponentInstanceRef InSComponentInstanceRef // valid SketckUp component instance
	);

	// Return the effective layer of a SketckUp component instance.
	SULayerRef GetEffectiveLayer(
		SUComponentInstanceRef InSComponentInstanceRef, // valid SketckUp component instance
		SULayerRef             InSInheritedLayerRef     // SketchUp inherited layer
	);
	SULayerRef GetEffectiveLayer(SUDrawingElementRef DrawingElementRef, SULayerRef InInheritedLayerRef);

	// Return whether or not a SketckUp component instance is visible in the current SketchUp scene.
	bool IsVisible(
		SUComponentInstanceRef InSComponentInstanceRef, // valid SketckUp component instance
		SULayerRef             InSEffectiveLayerRef     // SketchUp component instance effective layer
	);

	// Return whether or not a SketckUp layer is visible in the current SketchUp scene taking into account folder visibility
	bool IsLayerVisible(
		SULayerRef LayerRef
	);

	// Get the material of a SketckUp component instance.
	SUMaterialRef GetMaterial(
		SUComponentInstanceRef InSComponentInstanceRef // valid SketckUp component instance
	);

	bool CompareSUTransformations(const SUTransformation& A, const SUTransformation& B);

	// Set the world transform of a Datasmith actor.
	void SetActorTransform(
		const TSharedPtr<IDatasmithActorElement>& IODActorPtr,      // Datasmith actor to transform
		SUTransformation const& InWorldTransform // SketchUp world transform to apply
	);

	bool DecomposeTransform(
		SUTransformation const& InWorldTransform, // SketchUp world transform to convert
		FVector& OutTranslation,
		FQuat& OutRotation,
		FVector& OutScale,
		FVector& OutShear
	);

	// Split a source SketchUp transformation to a set of transformations supported by Unreal
	// Transform which comes from SketchUp can be any affine transform, represented like: T*R*H*S
	// TRS - are supported by Unreal TranslationRotationScaling
	// and H is the 'Shear'/'Skew' unsupported by unreal
	// In order to correctly display geometry with Shear the H*S part of the transform needs to be 'baked' into the
	// exported geometry, meaning that the vertices need to be pre-transformed by the S*H matrix
	//
	// @param OutWorldTransform Is a transform without SCALE and SHEAR to set on the Actor
	// @param OutMeshActorWorldTransform Is a transform without SHEAR(but scaling is kept) to set on the MeshActor
	// @param OutBakeTransform Is just SHEAR to apply to mesh vertices before export, to 'bake' it into the exported mesh
	bool SplitTransform(
		SUTransformation const& InWorldTransform, // SketchUp world transform to convert
		SUTransformation& OutWorldTransform,
		SUTransformation& OutMeshActorWorldTransform,
		SUTransformation& OutBakeTransform
	);

	SUTransformation GetComponentInstanceTransform(SUComponentInstanceRef InSComponentInstanceRef, SUTransformation const& InSWorldTransform);

	// Call into Ruby code
	namespace ToRuby
	{
		// Add Warning that will be show in SketchUp UI(plugins Messages dialog)
		void LogWarn(const FString& Message);
	};
}
