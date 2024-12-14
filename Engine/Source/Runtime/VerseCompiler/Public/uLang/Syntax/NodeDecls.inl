// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
//         Name               | RequiredChildren | Precedence | ESupportsManyChildren | EChildDeletionBehavior
#define VERSE_ENUM_VSTNODES(DECL_NODE) \
DECL_NODE( Project            , 0                , 0          , Anywhere              , Default                  )\
DECL_NODE( Package            , 0                , 0          , Anywhere              , Default                  )\
DECL_NODE( Module             , 0                , 0          , Anywhere              , Default                  )\
DECL_NODE( Snippet            , 0                , 0          , Anywhere              , Default                  )\
DECL_NODE( Assignment         , 2                , 110        , Nowhere               , Default                  )\
DECL_NODE( TypeSpec           , 2                , 120        , Nowhere               , Default                  )\
DECL_NODE( BinaryOpLogicalOr  , 2                , 130        , Anywhere              , Default                  )\
DECL_NODE( BinaryOpLogicalAnd , 2                , 140        , Anywhere              , Default                  )\
DECL_NODE( PrefixOpLogicalNot , 1                , 150        , Nowhere               , Default                  )\
DECL_NODE( BinaryOpCompare    , 2                , 160        , Nowhere               , Default                  )\
DECL_NODE( BinaryOpArrow      , 2                , 170        , Nowhere               , Default                  )\
DECL_NODE( BinaryOpAddSub     , 2                , 180        , Anywhere              , Default                  )\
DECL_NODE( BinaryOpMulDivInfix, 2                , 190        , Anywhere              , Default                  )\
DECL_NODE( BinaryOpRange      , 2                , 200        , Nowhere               , Default                  )\
DECL_NODE( PrePostCall        , 2                , 210        , Anywhere              , Delete                   )\
DECL_NODE( Identifier         , 0                , INT32_MAX  , Nowhere               , CreatePlaceholder        )\
DECL_NODE( Operator           , 0                , INT32_MAX  , Nowhere               , Default                  )\
DECL_NODE( FlowIf             , 1                , INT32_MAX  , TrailingOnly          , Default                  )\
DECL_NODE( IntLiteral         , 0                , INT32_MAX  , Nowhere               , Default                  )\
DECL_NODE( FloatLiteral       , 0                , INT32_MAX  , Nowhere               , Default                  )\
DECL_NODE( CharLiteral        , 0                , INT32_MAX  , Nowhere               , Default                  )\
DECL_NODE( StringLiteral      , 0                , INT32_MAX  , Nowhere               , Default                  )\
DECL_NODE( PathLiteral        , 0                , INT32_MAX  , Nowhere               , Default                  )\
DECL_NODE( InterpolatedString , 1                , INT32_MAX  , Nowhere               , Default                  )\
DECL_NODE( Lambda             , 2                , INT32_MAX  , Nowhere               , Default                  )\
DECL_NODE( Control            , 1                , INT32_MAX  , Nowhere               , Default                  )\
DECL_NODE( Macro              , 0                , INT32_MAX  , Anywhere              , Default                  )\
DECL_NODE( Clause             , 0                , INT32_MAX  , Anywhere              , Default                  )\
DECL_NODE( Parens             , 0                , INT32_MAX  , Anywhere              , Default                  )\
DECL_NODE( Commas             , 0                , INT32_MAX  , Anywhere              , Default                  )\
DECL_NODE( Placeholder        , 0                , INT32_MAX  , Nowhere               , Default                  )\
DECL_NODE( ParseError         , 0                , INT32_MAX  , Anywhere              , Default                  )\
DECL_NODE( Escape             , 1                , INT32_MAX  , Nowhere               , Default                  )\
DECL_NODE( Comment            , 0                , INT32_MAX  , Nowhere               , Default                  )\
DECL_NODE( Where              , 2                , 100        , Anywhere              , Default                  )\
DECL_NODE( Mutation           , 1                , INT32_MAX  , Nowhere               , Default                  )\
DECL_NODE( Definition         , 2                , 110        , Nowhere               , Default                  )
