// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuR/Mesh.h"
#include "MuT/Node.h"
#include "MuT/NodeModifier.h"
#include "MuT/NodeMesh.h"

namespace mu
{
	
	/** */
	class MUTABLETOOLS_API NodeModifierMeshClipDeform : public NodeModifier
	{
	public:

		Ptr<NodeMesh> ClipMesh;

		EShapeBindingMethod BindingMethod = EShapeBindingMethod::ClipDeformClosestProject;

		/** */
		EFaceCullStrategy FaceCullStrategy = EFaceCullStrategy::AllVerticesCulled;

	public:

		// Node interface
		virtual const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		inline ~NodeModifierMeshClipDeform() {}

	private:

		static FNodeType StaticType;

	};


}
