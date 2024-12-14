// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuT/Node.h"
#include "MuT/NodeImage.h"
#include "MuT/NodeMesh.h"


namespace mu
{

	//! 
	class MUTABLETOOLS_API NodeMeshClipDeform : public NodeMesh
	{
	public:

		Ptr<NodeMesh> BaseMesh;
		Ptr<NodeMesh> ClipShape;
		Ptr<NodeImage> ShapeWeights;

	public:

		// Node interface
		virtual const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		inline ~NodeMeshClipDeform() {}

	private:

		static FNodeType StaticType;

	};

}
