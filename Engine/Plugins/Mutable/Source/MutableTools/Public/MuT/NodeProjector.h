// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/Parameters.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"


namespace mu
{

	class NodeProjector;
    using NodeProjectorPtr = Ptr<NodeProjector>;
    using NodeProjectorPtrConst = Ptr<const NodeProjector>;

	class NodeProjectorConstant;
    using NodeProjectorConstantPtr = Ptr<NodeProjectorConstant>;
    using NodeProjectorConstantPtrConst = Ptr<const NodeProjectorConstant>;

	class NodeProjectorParameter;
    using NodeProjectorParameterPtr = Ptr<NodeProjectorParameter>;
    using NodeProjectorParameterPtrConst = Ptr<const NodeProjectorParameter>;

    class NodeRange;
    using NodeRangePtr = Ptr<NodeRange>;
    using NodeRangePtrConst = Ptr<const NodeRange>;


    //! Base class of any node that outputs a Projector.
	class MUTABLETOOLS_API NodeProjector : public Node
	{
	public:

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

        const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();


		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		inline ~NodeProjector() {}

	};


	//! This node outputs a predefined Projector value.
	class MUTABLETOOLS_API NodeProjectorConstant : public NodeProjector
	{
	public:

		NodeProjectorConstant();

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

        const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//! Get the value that this node returns
        void GetValue( PROJECTOR_TYPE* OutType,
			FVector3f* OutPos,
			FVector3f* OutDir,
			FVector3f* OutUp,
			FVector3f* OutScaleU,
			float* OutProjectionAngle ) const;

		//! Set the value to be returned by this node
        void SetValue( PROJECTOR_TYPE type, FVector3f pos, FVector3f dir, FVector3f up, FVector3f scale, float projectionAngle);

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeProjectorConstant();

	private:

		Private* m_pD;

	};


	//! Node that defines a Projector model parameter.
	class MUTABLETOOLS_API NodeProjectorParameter : public NodeProjector
	{
	public:

		NodeProjectorParameter();

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

        const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//! Set the name of the parameter.
		void SetName( const FString& );


		//! Get the uid of the parameter. It will be exposed in the final compiled data.
		const FString& GetUid() const;
		void SetUid( const FString& );

		//! Set the default value of the parameter.
        void SetDefaultValue( PROJECTOR_TYPE type,
			FVector3f pos,
			FVector3f dir,
			FVector3f up,
			FVector3f scale,
			float projectionAngle );

        //! Set the number of ranges (dimensions) for this parameter.
        //! By default a parameter has 0 ranges, meaning it only has one value.
        void SetRangeCount( int i );
        void SetRange( int i, NodeRangePtr pRange );

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeProjectorParameter();

	private:

		Private* m_pD;

	};


}
