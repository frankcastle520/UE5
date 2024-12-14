// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeMesh.h"


namespace mu
{

    // Forward definitions
    class NodeMeshVariation;
    typedef Ptr<NodeMeshVariation> NodeMeshVariationPtr;
    typedef Ptr<const NodeMeshVariation> NodeMeshVariationPtrConst;

    //!
    //! \ingroup model
    class MUTABLETOOLS_API NodeMeshVariation : public NodeMesh
    {
    public:

        NodeMeshVariation();

        //-----------------------------------------------------------------------------------------
        // Node Interface
        //-----------------------------------------------------------------------------------------

        const FNodeType* GetType() const override;
        static const FNodeType* GetStaticType();

        //-----------------------------------------------------------------------------------------
        // Own Interface
        //-----------------------------------------------------------------------------------------

        //!
        void SetDefaultMesh( NodeMesh* Mesh );

        //! Set the number of tags to consider in this variation
        void SetVariationCount( int count );

        //!
        int GetVariationCount() const;

        //! Set the tag or state name that will enable a specific vartiation
        void SetVariationTag( int index, const char* strTag );

        //!
        void SetVariationMesh( int index, NodeMesh* Mesh );

        //!}


        //-----------------------------------------------------------------------------------------
        // Interface pattern
        //-----------------------------------------------------------------------------------------
        class Private;
        Private* GetPrivate() const;

    protected:
        //! Forbidden. Manage with the Ptr<> template.
        ~NodeMeshVariation();

    private:
        Private* m_pD;
    };


} // namespace mu
