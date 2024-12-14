// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeMesh.h"
#include "MuT/NodeImage.h"


namespace mu
{

	// Forward definitions
	class NodeMeshConstant;
	typedef Ptr<NodeMeshConstant> NodeMeshConstantPtr;
	typedef Ptr<const NodeMeshConstant> NodeMeshConstantPtrConst;

	class Mesh;
	class NodeLayout;

	//! Node that outputs a constant mesh.
	//! It allows to define the layouts for the texture channels of the constant mesh
	class MUTABLETOOLS_API NodeMeshConstant : public NodeMesh
	{
	public:

		FSourceDataDescriptor SourceDataDescriptor;

	public:

		NodeMeshConstant();

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

		const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//! Get the constant mesh that will be returned.
		Ptr<Mesh> GetValue() const;

		//! Set the constant mesh that will be returned.
		void SetValue(Ptr<Mesh>);

		/** */
		void AddMorph(const FString& Name, Ptr<Mesh>);

		/** */
		Ptr<Mesh> FindMorph(const FString& Name) const;

		//! Get the number of layouts defined in this mesh.
		int32 GetLayoutCount() const;
		void SetLayoutCount( int32 );

		//! Get the node defining a layout of the returned mesh.
		Ptr<NodeLayout> GetLayout( int32 index ) const;
		void SetLayout( int32 index, Ptr<NodeLayout>);

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeMeshConstant();

	private:

		Private* m_pD;

	};


}
