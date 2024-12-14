// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeScalar.h"


namespace mu
{

    // Forward definitions
    class NodeScalarVariation;
    typedef Ptr<NodeScalarVariation> NodeScalarVariationPtr;
    typedef Ptr<const NodeScalarVariation> NodeScalarVariationPtrConst;


     class MUTABLETOOLS_API NodeScalarVariation : public NodeScalar
    {
    public:
 
        NodeScalarVariation();

        //-----------------------------------------------------------------------------------------
        // Node Interface
        //-----------------------------------------------------------------------------------------

        const FNodeType* GetType() const override;
        static const FNodeType* GetStaticType();

        //-----------------------------------------------------------------------------------------
        // Own Interface
        //-----------------------------------------------------------------------------------------

        //!
        void SetDefaultScalar( NodeScalar* Scalar );

        //! Set the number of tags to consider in this variation
        void SetVariationCount( int32 count );

        //! Set the tag or state name that will enable a specific vartiation
        void SetVariationTag( int32 index, const FString& strTag );

        //!
        void SetVariationScalar( int32 index, NodeScalar* Scalar );

        //!}


        //-----------------------------------------------------------------------------------------
        // Interface pattern
        //-----------------------------------------------------------------------------------------
        class Private;
        Private* GetPrivate() const;

    protected:
        //! Forbidden. Manage with the Ptr<> template.
        ~NodeScalarVariation();

    private:
        Private* m_pD;
    };


} // namespace mu
