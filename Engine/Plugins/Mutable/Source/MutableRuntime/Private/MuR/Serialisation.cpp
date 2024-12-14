// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuR/Serialisation.h"

#include "Containers/Array.h"
#include "HAL/PlatformCrt.h"
#include "HAL/PlatformMath.h"
#include "Misc/AssertionMacros.h"
#include "Hash/CityHash.h"
#include "MuR/Image.h"
#include "MuR/SerialisationPrivate.h"


namespace mu
{
	MUTABLE_IMPLEMENT_POD_SERIALISABLE(float);    
	MUTABLE_IMPLEMENT_POD_SERIALISABLE(double);   
                                                  
    MUTABLE_IMPLEMENT_POD_SERIALISABLE( int8 );   
    MUTABLE_IMPLEMENT_POD_SERIALISABLE( int16 );  
    MUTABLE_IMPLEMENT_POD_SERIALISABLE( int32 );  
    MUTABLE_IMPLEMENT_POD_SERIALISABLE( int64 );  
                                                  
    MUTABLE_IMPLEMENT_POD_SERIALISABLE( uint8 );  
    MUTABLE_IMPLEMENT_POD_SERIALISABLE( uint16 ); 
    MUTABLE_IMPLEMENT_POD_SERIALISABLE( uint32 ); 
    MUTABLE_IMPLEMENT_POD_SERIALISABLE( uint64 )
	
	// Unreal POD Serializables                                                       
	MUTABLE_IMPLEMENT_POD_SERIALISABLE(FGuid);
	MUTABLE_IMPLEMENT_POD_SERIALISABLE(FUintVector2);
	MUTABLE_IMPLEMENT_POD_SERIALISABLE(FIntVector2);
	MUTABLE_IMPLEMENT_POD_SERIALISABLE(UE::Math::TIntVector2<uint16>);
	MUTABLE_IMPLEMENT_POD_SERIALISABLE(UE::Math::TIntVector2<int16>);
	MUTABLE_IMPLEMENT_POD_SERIALISABLE(FVector2f);
	MUTABLE_IMPLEMENT_POD_SERIALISABLE(FVector4f);
	MUTABLE_IMPLEMENT_POD_SERIALISABLE(FMatrix44f);
	MUTABLE_IMPLEMENT_POD_SERIALISABLE(FRichCurveKey);

	
    void operator<<(OutputArchive& arch, const FString& t)
    {
	    const TArray<TCHAR>& Data = t.GetCharArray();
    	arch << Data;
    }


    void operator>>(InputArchive& arch, FString& t)
    {
		TArray<TCHAR> Data;
    	arch >> Data;

    	t = FString(Data.GetData()); // Construct from raw pointer to avoid double zero terminating character
    }


	void operator<<(OutputArchive& arch, const FRichCurve& t)
	{
		arch << t.Keys;
	}


	void operator>>(InputArchive& arch, FRichCurve& t)
	{
		arch >> t.Keys;
	}


    void operator<<(OutputArchive& arch, const FName& v)
    {
	    arch << v.ToString();
    }


    void operator>>(InputArchive& arch, FName& v)
    {
	    FString Temp;
	    arch >> Temp;
	    v = FName(Temp);
    }
	

	void operator>> ( InputArchive& arch, std::string& v )
    {
    	uint32 size;
    	arch >> size;
    	v.resize( size );
    	if (size)
    	{
    		arch.Stream->Read( &v[0], (unsigned)size*sizeof(char) );
    	}
    }

	
	void operator<<(OutputArchive& Arch, const bool& T)
    {
    	uint8 S = T ? 1 : 0;
    	Arch.Stream->Write(&S, sizeof(uint8));
    }


	void operator>>(InputArchive& Arch, bool& T)
    {
    	uint8 S;
    	Arch.Stream->Read(&S, sizeof(uint8));
    	T = S != 0;
    }

	
    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------
    InputMemoryStream::InputMemoryStream( const void* InBuffer, uint64 InSize )
    {
        Buffer = InBuffer;
        Size = InSize;
    }


    //---------------------------------------------------------------------------------------------
    void InputMemoryStream::Read( void* Data, uint64 InSize)
    {
        if (InSize)
        {
            check( Pos + InSize <= Size );

            const uint8* Source = reinterpret_cast<const uint8*>(Buffer)+Pos;
            FMemory::Memcpy( Data, Source, (SIZE_T)InSize);
            Pos += InSize;
        }
    }


    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------
    OutputMemoryStream::OutputMemoryStream(uint64 Reserve )
    {
        if (Reserve)
        {
             Buffer.Reserve( Reserve );
        }
    }


    //---------------------------------------------------------------------------------------------
    void OutputMemoryStream::Write( const void* Data, uint64 Size)
    {
        if (Size)
        {
            uint64 Pos = Buffer.Num();
			Buffer.SetNum( Pos + Size, EAllowShrinking::No );
			FMemory::Memcpy( Buffer.GetData()+Pos, Data, Size);
        }
    }


    //---------------------------------------------------------------------------------------------
    const void* OutputMemoryStream::GetBuffer() const
    {
        const void* pResult = 0;

        if (Buffer.Num() )
        {
            pResult = Buffer.GetData();
        }

        return pResult;
    }


	//---------------------------------------------------------------------------------------------
	uint64 OutputMemoryStream::GetBufferSize() const
    {
        return Buffer.Num();
    }


	//---------------------------------------------------------------------------------------------
	void OutputMemoryStream::Reset()
	{
		Buffer.SetNum(0,EAllowShrinking::No);
	}


    //-------------------------------------------------------------------------------------------------
    //-------------------------------------------------------------------------------------------------
    //-------------------------------------------------------------------------------------------------
    void OutputSizeStream::Write( const void*, uint64 size )
    {
        WrittenBytes += size;
    }

	uint64 OutputSizeStream::GetBufferSize() const
    {
        return WrittenBytes;
    }


	//-------------------------------------------------------------------------------------------------
	void OutputHashStream::Write(const void* Data, uint64 size)
	{
		Hash = CityHash64WithSeed(reinterpret_cast<const char*>(Data), size, Hash);
	}


	uint64 OutputHashStream::GetHash() const
	{
		return Hash;
	}


    //---------------------------------------------------------------------------------------------
	//---------------------------------------------------------------------------------------------
	//---------------------------------------------------------------------------------------------
	InputArchive::InputArchive( InputStream* InStream )
	{
		Stream = InStream;
	}


    //---------------------------------------------------------------------------------------------
    Ptr<ResourceProxy<Image>> InputArchive::NewImageProxy()
    {
        return nullptr;
    }


	//---------------------------------------------------------------------------------------------
	//---------------------------------------------------------------------------------------------
	//---------------------------------------------------------------------------------------------
	OutputArchive::OutputArchive( OutputStream* InStream )
	{
		Stream = InStream;
	}


    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------
    InputArchiveWithProxies::InputArchiveWithProxies( InputStream* s, ProxyFactory* f )
        : InputArchive( s )
    {
        Factory = f;
    }


    //---------------------------------------------------------------------------------------------
    Ptr<ResourceProxy<Image>> InputArchiveWithProxies::NewImageProxy()
    {
        // Similar to Ptr serisalisation in SerialisationPrivate
        Ptr<ResourceProxy<Image>> p;
        {
            int32 id;
            (*this) >> id;

            if ( id == -1 )
            {
                // We consumed the serialisation, so we need to return something.
                class ImageProxyNull : public ResourceProxy<Image>
                {
                public:
                    ImagePtrConst Get() override
                    {
                        return nullptr;
                    }
                };
                p = new ImageProxyNull();
            }
            else
            {
                if ( id < ProxyHistory.Num() )
                {
                    p = ProxyHistory[id];

                    // If the pointer was nullptr it means the position in history is used, but not set
                    // yet: we have a smart pointer loop which is very bad.
                    check( p );
                }
                else
                {
                    // Ids come in order.
                    ProxyHistory.SetNum(id+1);

                    p = Factory->NewImageProxy(*this);
                    ProxyHistory[id] = p;
                }
            }
        }

        return p;
    }

}

