// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuR/Types.h"
#include "MuR/Skeleton.h"
#include "MuT/Node.h"
#include "MuT/NodeModifier.h"


namespace mu
{

	/** */

	struct FBoneName;

	struct FClipMorphPlaneParameters
	{
		// Morph field parameters

		// Distance to the plane of last affected vertex
		float DistanceToPlane = 0;

		// "Linearity" factor of the influence.
		float LinearityFactor = 0;

		// Ellipse location
		FVector3f Origin = FVector3f(0, 0, 0);
		FVector3f Normal = FVector3f(0, 0, 0);
		float Radius1 = 0, Radius2 = 0, Rotation = 0;

		// Vertex selection box
		EClipVertexSelectionType VertexSelectionType = EClipVertexSelectionType::None;
		FVector3f SelectionBoxOrigin = FVector3f(0, 0, 0);
		FVector3f SelectionBoxRadius = FVector3f(0, 0, 0);
		FBoneName VertexSelectionBone;

		// Max distance a vertex can have to the bone in order to be affected. A negative value
		// means no limit.
		float MaxEffectRadius = -1.0f;

		EFaceCullStrategy FaceCullStrategy = EFaceCullStrategy::AllVerticesCulled;
	};


	/** */
	class MUTABLETOOLS_API NodeModifierMeshClipMorphPlane : public NodeModifier
	{
	public:

		FClipMorphPlaneParameters Parameters;

	public:

		// Node interface
		virtual const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

		//-----------------------------------------------------------------------------------------
        // Own interface
		//-----------------------------------------------------------------------------------------

		void SetPlane(FVector3f Center, FVector3f Normal);
		void SetParams(float dist, float factor);
		void SetMorphEllipse(float radius1, float radius2, float rotation);

		//! Define an axis-aligned box that will select the vertices to be morphed.
		//! Only one of Box or Bone Hierarchy can be used (the last one set)
		void SetVertexSelectionBox(float centerX, float centerY, float centerZ, float radiusX, float radiusY, float radiusZ);

		//! Define the root bone of the subhierarchy of the mesh that will be affected.
		//! Only one of Box or Bone Hierarchy can be used (the last one set)
		void SetVertexSelectionBone(const FBoneName& BoneId, float maxEffectRadius);

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeModifierMeshClipMorphPlane() {}

	private:

		static FNodeType StaticType;

	};


}
