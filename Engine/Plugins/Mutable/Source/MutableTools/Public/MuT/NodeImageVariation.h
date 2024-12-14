// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeImage.h"


namespace mu
{

    // Forward definitions
    class NodeImageVariation;
    typedef Ptr<NodeImageVariation> NodeImageVariationPtr;
    typedef Ptr<const NodeImageVariation> NodeImageVariationPtrConst;


    //!
    //! \ingroup model
    class MUTABLETOOLS_API NodeImageVariation : public NodeImage
    {
    public:
        //-----------------------------------------------------------------------------------------
        // Life cycle
        //-----------------------------------------------------------------------------------------

        NodeImageVariation();

        //-----------------------------------------------------------------------------------------
        // Node Interface
        //-----------------------------------------------------------------------------------------

        const FNodeType* GetType() const override;
        static const FNodeType* GetStaticType();

        //-----------------------------------------------------------------------------------------
        // Own Interface
        //-----------------------------------------------------------------------------------------

        //!
        void SetDefaultImage( NodeImage* Image );

        //! Set the number of tags to consider in this variation
        void SetVariationCount( int count );

        //!
        int GetVariationCount() const;

        //! Set the tag or state name that will enable a specific vartiation
        void SetVariationTag( int index, const FString& Tag );

        //!
        void SetVariationImage( int index, NodeImage* Image );

        //!}


        //-----------------------------------------------------------------------------------------
        // Interface pattern
        //-----------------------------------------------------------------------------------------
        class Private;
        Private* GetPrivate() const;

    protected:
        //! Forbidden. Manage with the Ptr<> template.
        ~NodeImageVariation();

    private:
        Private* m_pD;
    };


} // namespace mu
