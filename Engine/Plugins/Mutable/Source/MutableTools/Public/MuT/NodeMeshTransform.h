// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeMesh.h"
#include "Math/MathFwd.h"

namespace mu
{

	// Forward definitions
    class NodeMeshTransform;
    typedef Ptr<NodeMeshTransform> NodeMeshTransformPtr;
    typedef Ptr<const NodeMeshTransform> NodeMeshTransformPtrConst;


    //! This node applies a geometric transform represented by a 4x4 matrix to a mesh
    class MUTABLETOOLS_API NodeMeshTransform : public NodeMesh
	{
	public:

        NodeMeshTransform();

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

        const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//! Source mesh to be re-formatted
        NodeMeshPtr GetSource() const;
		void SetSource( NodeMesh* );

        void SetTransform(const FMatrix44f&);
		const FMatrix44f& GetTransform() const;

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
        ~NodeMeshTransform();

	private:

		Private* m_pD;

	};


}
