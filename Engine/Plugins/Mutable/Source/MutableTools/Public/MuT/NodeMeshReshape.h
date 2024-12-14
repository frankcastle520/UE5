// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuT/Node.h"
#include "MuT/NodeMesh.h"
#include "MuR/Mesh.h"

namespace mu
{
	// Forward declarations
	struct FBoneName;

	//! Node that morphs a base mesh with one or two weighted targets from a sequence.
	class MUTABLETOOLS_API NodeMeshReshape : public NodeMesh
	{
	public:

		NodeMeshReshape();

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

		const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		const NodeMeshPtr& GetBaseMesh() const;
		void SetBaseMesh( const NodeMeshPtr& );

		const NodeMeshPtr& GetBaseShape() const;
		void SetBaseShape(const NodeMeshPtr&);

		const NodeMeshPtr& GetTargetShape() const;
		void SetTargetShape(const NodeMeshPtr&);

		void SetReshapeVertices(bool);

		/** Recompute normals after reshaping. This will ignore the reshaped normals. Disabled by default. */
		void SetRecomputeNormals(bool);


		/** Also deform the mesh skeleton. Disabled by default. */
		void SetReshapeSkeleton(bool);
	
		/** Apply Laplacian smoothing to the reshaped mesh.  */
		void SetApplyLaplacian(bool);
		
		/** Set vertex color channel usages for Reshape operations. */
		void SetColorUsages(EVertexColorUsage R, EVertexColorUsage G, EVertexColorUsage B, EVertexColorUsage A);

		/** Deform Mesh Physics Volumes */
		void SetReshapePhysicsVolumes(bool);

		/** Sets the number of bones that will be deform */
		void AddBoneToDeform(const FBoneName& BoneId);
	
		/** Add a Physics Body to deform */
		void AddPhysicsBodyToDeform(const FBoneName& BoneId);
        
		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeMeshReshape();

	private:

		Private* m_pD;

	};

}
