// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/Node.h"
#include "MuR/Operations.h"


namespace mu
{

    class Node::Private
    {
    public:

		/** Force a virtual destructor. */
		virtual ~Private() = default; 

		/** Generic pointer to the node owning this private. */
		const Node* m_pNode = nullptr;
    };



#define MUTABLE_IMPLEMENT_NODE( N )				\
    N::N()										\
    {											\
        m_pD = new Private();					\
		m_pD->m_pNode = this;					\
	}											\
                                                \
    N::~N()										\
    {											\
        check( m_pD );							\
        delete m_pD;							\
        m_pD = nullptr;							\
    }											\
                                                \
    N::Private* N::GetPrivate() const			\
    {											\
        return m_pD;							\
    }											\
                                                \
    const FNodeType* N::GetType() const			\
    {											\
        return GetStaticType();					\
    }											\
                                                \
    const FNodeType* N::GetStaticType()			\
    {											\
        return &Private::s_type;				\
    }

}
