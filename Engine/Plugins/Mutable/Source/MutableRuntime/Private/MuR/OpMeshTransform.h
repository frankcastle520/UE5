// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/MeshPrivate.h"
#include "MuR/ConvertData.h"
#include "MuR/Platform.h"


namespace mu
{

    //---------------------------------------------------------------------------------------------
    //! Reference version
    //---------------------------------------------------------------------------------------------
    inline void MeshTransform(Mesh* Result, const Mesh* pBase, const FMatrix44f& transform, bool& bOutSuccess)
	{
		bOutSuccess = true;

        uint32_t vcount = pBase->GetVertexBuffers().GetElementCount();

        if ( !vcount )
		{
			bOutSuccess = false;
			return;
		}


		Result->CopyFrom(*pBase);

		FMatrix44f transformIT = transform.Inverse().GetTransposed();

        const FMeshBufferSet& MBSPriv = Result->GetVertexBuffers();
        for ( int32 b=0; b<MBSPriv.Buffers.Num(); ++b )
        {

            for ( int32 c=0; c<MBSPriv.Buffers[b].Channels.Num(); ++c )
            {
                EMeshBufferSemantic sem = MBSPriv.Buffers[b].Channels[c].Semantic;
                int semIndex = MBSPriv.Buffers[b].Channels[c].SemanticIndex;

                UntypedMeshBufferIterator it( Result->GetVertexBuffers(), sem, semIndex );

                switch ( sem )
                {
                case MBS_POSITION:
                    for ( uint32_t v=0; v<vcount; ++v )
                    {
                        FVector4f value( 0.0f, 0.0f, 0.0f, 1.0f );
                        for( int i=0; i<it.GetComponents(); ++i )
                        {
                            ConvertData( i, &value[0], MBF_FLOAT32, it.ptr(), it.GetFormat() );
                        }

                        value = transform.TransformFVector4( value );

                        for( int i=0; i<it.GetComponents(); ++i )
                        {
                            ConvertData( i, it.ptr(), it.GetFormat(), &value[0], MBF_FLOAT32 );
                        }

                        ++it;
                    }
                    break;

                case MBS_NORMAL:
                case MBS_TANGENT:
                case MBS_BINORMAL:
                    for ( uint32_t v=0; v<vcount; ++v )
                    {
						FVector4f value( 0.0f, 0.0f, 0.0f, 1.0f );
                        for( int i=0; i<it.GetComponents(); ++i )
                        {
                            ConvertData( i, &value[0], MBF_FLOAT32, it.ptr(), it.GetFormat() );
                        }

                        value = transformIT.TransformFVector4(value);

                        for( int i=0; i<it.GetComponents(); ++i )
                        {
                            ConvertData( i, it.ptr(), it.GetFormat(), &value[0], MBF_FLOAT32 );
                        }

                        ++it;
                    }
                    break;

                default:
                    break;
                }
            }
        }
	}

}

