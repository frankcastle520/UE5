// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeMesh.h"

namespace mu
{

	// Forward definitions
	class FMeshBufferSet;

	/** This node can change the buffer formats of a mesh vertices, indices and faces. */
	class MUTABLETOOLS_API NodeMeshFormat : public NodeMesh
	{
	public:

		NodeMeshFormat();

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

		const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		/** Mesh to be reformatted. */
		Ptr<NodeMesh> GetSource() const;
		void SetSource( NodeMesh* );

		/** Access the MeshBufferSet that defines the new format for the mesh vertices.These
		* buffers don't really contain any data (they have 0 elements) but they define the
		* structure. If this is null, the vertex buffers will not be changed.
		*/
		FMeshBufferSet& GetVertexBuffers();

		/** Access the MeshBufferSet that defines the new format for the mesh indices. These
		* buffers don't really contain any data (they have 0 elements) but they define the
		* structure. If this is null, the vertex buffers will not be changed.
		*/
		FMeshBufferSet& GetIndexBuffers();

		/** Optimize the buffers is possible. This may change the target format to reduce the number
		* of channels or even the type if possible to minimize size.
		* By default it is disabled.
		*/
		void SetOptimizeBuffers(bool bEnable);

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeMeshFormat();

	private:

		Private* m_pD;

	};


}
