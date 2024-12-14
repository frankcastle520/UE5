// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeImage.h"


namespace mu
{
	class NodeRange;

    //! Node that defines a Image model parameter.
	//! \ingroup model
    class MUTABLETOOLS_API NodeImageParameter : public NodeImage
	{
	public:

        NodeImageParameter();

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

		const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//! Set the name of the parameter.
		void SetName(const FString&);

		//! Set the uid of the parameter.
		void SetUid(const FString&);

    	//! Get the default value of the parameter.
    	void SetDefaultValue(FName Value);
    	
		//! Set the number of ranges (dimensions) for this parameter.
		//! By default a parameter has 0 ranges, meaning it only has one value.
		void SetRangeCount(int Index);
		void SetRange(int Index, Ptr<NodeRange> Range);

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
        ~NodeImageParameter();

	private:

		Private* m_pD;

	};


}
