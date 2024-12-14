// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"


namespace mu
{
	class NodeRange;

	//---------------------------------------------------------------------------------------------
    //! %Base class of any node that outputs a Bool value.
	//! \ingroup model
	//---------------------------------------------------------------------------------------------
	class MUTABLETOOLS_API NodeBool : public Node
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
		inline ~NodeBool() {}
	};


	//---------------------------------------------------------------------------------------------
	//! Node returning a Bool constant value.
	//! \ingroup model
	//---------------------------------------------------------------------------------------------
	class MUTABLETOOLS_API NodeBoolConstant : public NodeBool
	{
	public:

		NodeBoolConstant();


		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

        const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//! Get the value to be returned by the node.
		bool GetValue() const;

		//! Set the value to be returned by the node.
		void SetValue( bool v );

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;

	protected:

		//! Forbidden.
		~NodeBoolConstant();

	private:

		Private* m_pD;

	};


	//---------------------------------------------------------------------------------------------
	//! Node that defines a Bool model parameter.
	//! \ingroup model
	//---------------------------------------------------------------------------------------------
	class MUTABLETOOLS_API NodeBoolParameter : public NodeBool
	{
	public:

		NodeBoolParameter();

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

        const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//! Set the name of the parameter. It will be exposed in the final compiled data.
		void SetName( const FString& );

		//! Get the default value of the parameter.
		void SetDefaultValue( bool v );

        //! Set the number of ranges (dimensions) for this parameter.
        //! By default a parameter has 0 ranges, meaning it only has one value.
        void SetRangeCount( int i );
        void SetRange( int i, Ptr<NodeRange> );

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;

	protected:

		//! Forbidden. 
		~NodeBoolParameter();

	private:

		Private* m_pD;

	};


	//---------------------------------------------------------------------------------------------
	//! Node that returns the oposite of the input value.
	//! \ingroup model
	//---------------------------------------------------------------------------------------------
	class MUTABLETOOLS_API NodeBoolNot : public NodeBool
	{
	public:

		NodeBoolNot();

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

        const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------
		
		//! Input
		Ptr<NodeBool> GetInput() const;
		void SetInput( Ptr<NodeBool> );

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;

	protected:

		//! Forbidden. 
		~NodeBoolNot();

	private:

		Private* m_pD;

	};


	//---------------------------------------------------------------------------------------------
	//! 
	//! \ingroup model
	//---------------------------------------------------------------------------------------------
	class MUTABLETOOLS_API NodeBoolAnd : public NodeBool
	{
	public:

		NodeBoolAnd();

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

        const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------
		
		//! Inputs
		Ptr<NodeBool> GetA() const;
		void SetA(Ptr<NodeBool>);

		Ptr<NodeBool> GetB() const;
		void SetB(Ptr<NodeBool>);

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;

	protected:

		//! Forbidden. 
		~NodeBoolAnd();

	private:

		Private* m_pD;

	};


}
