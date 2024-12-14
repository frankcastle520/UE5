// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeMesh.h"


namespace mu
{

    //! This node applies a geometric transform represented by a 4x4 matrix to a mesh
	//! \ingroup model
    class MUTABLETOOLS_API NodeMeshClipWithMesh : public NodeMesh
	{
	public:

		Ptr<NodeMesh> Source;
		Ptr<NodeMesh> ClipMesh;

	public:

		// Node interface
		virtual const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		inline ~NodeMeshClipWithMesh() {}

	private:

		static FNodeType StaticType;

	};


}
