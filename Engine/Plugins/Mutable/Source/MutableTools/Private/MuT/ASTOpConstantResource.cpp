// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpConstantResource.h"

#include "MuT/CompilerPrivate.h"
#include "MuR/Layout.h"
#include "MuR/Mesh.h"
#include "MuR/ModelPrivate.h"
#include "MuR/ImagePrivate.h"
#include "MuR/MutableMath.h"
#include "MuR/PhysicsBody.h"
#include "MuR/Serialisation.h"
#include "MuR/Skeleton.h"
#include "MuR/Types.h"

#include "Containers/Array.h"
#include "HAL/PlatformMath.h"
#include "HAL/PlatformFileManager.h"
#include "Hash/CityHash.h"
#include "Misc/AssertionMacros.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Compression/OodleDataCompression.h"

#include <inttypes.h> // Required for 64-bit printf macros

namespace mu
{

	/** Proxy class for a temporary resource while compiling. 
	* The resource may be stored in different ways:
	* - as is, in memory with its own pointer.
	* - in a compressed buffer
	* - saved to a disk file compressed or uncompressed.
	*/
	template<class R>
	class MUTABLETOOLS_API ResourceProxyTempFile : public ResourceProxy<R>
	{
	private:

		/** Actual resource to store. If the pointer is valid, it wasn't worth dumping to disk or compressing. */
		Ptr<const R> Resource;

		/** Temp filename used if it was necessary. */
		FString FileName;

		/** Size of the resource in memory. */
		uint32 UncompressedSize = 0;

		/** Size of the saved file. It may be the size of the resource in memory, or its compressed size. */
		uint32 FileSize = 0;

		/** Valid if the resource was compressed and stored in memory instead of dumped to disk. */
		TArray<uint8> CompressedBuffer;

		/** Shared context with cache settings and stats. */
		FProxyFileContext& Options;

		/** Prevent concurrent access to a signel resource. */
		FCriticalSection Mutex;

	public:

		ResourceProxyTempFile(const R* InResource, FProxyFileContext& InOptions)
			: Options(InOptions)
		{
			if (!InResource)
			{
				return;
			}

			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

			OutputMemoryStream stream(128*1024);
			OutputArchive arch(&stream);
			R::Serialise(InResource, arch);

			UncompressedSize = stream.GetBufferSize();

			if (stream.GetBufferSize() <= Options.MinProxyFileSize)
			{
				// Not worth compressing or caching to disk
				Resource = InResource;
			}
			else
			{
				// Compress
				int64 CompressedSize = 0;
				constexpr bool bEnableCompression = true;
				if (bEnableCompression)
				{
					int64 CompressedBufferSize = FOodleDataCompression::CompressedBufferSizeNeeded(stream.GetBufferSize());
					CompressedBufferSize = FMath::Max(CompressedBufferSize, int64(stream.GetBufferSize() / 2));
					CompressedBuffer.SetNumUninitialized(CompressedBufferSize);

					CompressedSize = FOodleDataCompression::CompressParallel(
						CompressedBuffer.GetData(), CompressedBufferSize,
						stream.GetBuffer(), stream.GetBufferSize(),
						FOodleDataCompression::ECompressor::Kraken,
						FOodleDataCompression::ECompressionLevel::SuperFast,
						true // CompressIndependentChunks
					);
				}

				bool bCompressed = CompressedSize != 0;

				if (bCompressed && uint64(CompressedSize) <= Options.MinProxyFileSize)
				{
					// Keep the compressed data, and don't store to file
					CompressedBuffer.SetNum(CompressedSize,EAllowShrinking::Yes);
				}
				else
				{
					// Save
					FString Prefix = FPlatformProcess::UserTempDir();

					uint32 PID = FPlatformProcess::GetCurrentProcessId();
					Prefix += FString::Printf(TEXT("mut.temp.%u"), PID);

					FString FinalTempPath;
					IFileHandle* ResourceFile = nullptr;
					uint64 AttemptCount = 0;
					while (!ResourceFile && AttemptCount < Options.MaxFileCreateAttempts)
					{
						uint64 ThisThreadFileIndex = Options.CurrentFileIndex.load();
						while (!Options.CurrentFileIndex.compare_exchange_strong(ThisThreadFileIndex, ThisThreadFileIndex + 1));

						FinalTempPath = Prefix + FString::Printf(TEXT(".%.16" PRIx64), ThisThreadFileIndex);
						ResourceFile = PlatformFile.OpenWrite(*FinalTempPath);
						++AttemptCount;
					}

					if (!ResourceFile)
					{
						UE_LOG(LogMutableCore, Error, TEXT("Failed to create temporary file. Disk full?"));
						check(false);
					}

					if (bCompressed)
					{
						FileSize = CompressedSize;
						ResourceFile->Write(CompressedBuffer.GetData(), FileSize);
					}
					else
					{
						FileSize = UncompressedSize;
						ResourceFile->Write((const uint8*)stream.GetBuffer(), FileSize);
					}

					CompressedBuffer.SetNum(0, EAllowShrinking::Yes);

					delete ResourceFile;

					FileName = FinalTempPath;
					Options.FilesWritten++;
					Options.BytesWritten += FileSize;
				}
			}
		}

		~ResourceProxyTempFile()
		{
			FScopeLock Lock(&Mutex);

			if (!FileName.IsEmpty())
			{
				// Delete temp file
				FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*FileName);
				FileName.Empty();
			}
		}

		Ptr<const R> Get() override
		{
			FScopeLock Lock(&Mutex);

			Ptr<const R> Result;
			if (Resource)
			{
				// Cached as is
				Result = Resource;
			}
			else if (!CompressedBuffer.Num() && !FileName.IsEmpty())
			{
				IFileHandle* resourceFile = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*FileName);
				check(resourceFile);

				CompressedBuffer.SetNumUninitialized(FileSize);
				resourceFile->Read(CompressedBuffer.GetData(), FileSize);
				delete resourceFile;

				bool bCompressed = FileSize != UncompressedSize;

				if (!bCompressed)
				{
					InputMemoryStream stream(CompressedBuffer.GetData(), FileSize);
					InputArchive arch(&stream);
					Result = R::StaticUnserialise(arch);

					CompressedBuffer.SetNum(0, EAllowShrinking::Yes);
				}

				Options.FilesRead++;
				Options.BytesRead += FileSize;
			}

			if (CompressedBuffer.Num())
			{
				// Cached compressed
				TArray<uint8> UncompressedBuf;
				UncompressedBuf.SetNumUninitialized(UncompressedSize);

				bool bSuccess = FOodleDataCompression::DecompressParallel(
					UncompressedBuf.GetData(), UncompressedSize,
					CompressedBuffer.GetData(), CompressedBuffer.Num());
				check(bSuccess);

				if (bSuccess)
				{
					InputMemoryStream stream(UncompressedBuf.GetData(), UncompressedSize);
					InputArchive arch(&stream);
					Result = R::StaticUnserialise(arch);
				}

				if (!FileName.IsEmpty())
				{
					CompressedBuffer.SetNum(0, EAllowShrinking::Yes);
				}
			}

			return Result;
		}

	};


	//-------------------------------------------------------------------------------------------------
	//-------------------------------------------------------------------------------------------------
	//-------------------------------------------------------------------------------------------------
	void ASTOpConstantResource::ForEachChild(const TFunctionRef<void(ASTChild&)>)
	{
	}


	//-------------------------------------------------------------------------------------------------
	bool ASTOpConstantResource::IsEqual(const ASTOp& OtherUntyped) const
	{
		if (OtherUntyped.GetOpType()==GetOpType())
		{
			const ASTOpConstantResource* Other = static_cast<const ASTOpConstantResource*>(&OtherUntyped);
			return Type == Other->Type && ValueHash == Other->ValueHash &&
				LoadedValue == Other->LoadedValue && Proxy == Other->Proxy
				&& SourceDataDescriptor == Other->SourceDataDescriptor;
		}
		return false;
	}


	//-------------------------------------------------------------------------------------------------
	mu::Ptr<ASTOp> ASTOpConstantResource::Clone(MapChildFuncRef) const
	{
		Ptr<ASTOpConstantResource> n = new ASTOpConstantResource();
		n->Type = Type;
		n->Proxy = Proxy;
		n->LoadedValue = LoadedValue;
		n->ValueHash = ValueHash;
		n->SourceDataDescriptor = SourceDataDescriptor; 
		return n;
	}


	//-------------------------------------------------------------------------------------------------
	uint64 ASTOpConstantResource::Hash() const
	{
		uint64 res = std::hash<uint64>()(uint64(Type));
		hash_combine(res, ValueHash);
		return res;
	}


	namespace
	{
		/** Adds a constant image data to a program and returns its constant index. */
		int32 AddConstantImage(FProgram& Program, const Ptr<const Image>& pImage, FLinkerOptions& Options)
		{
			MUTABLE_CPUPROFILER_SCOPE(AddConstantImage);

			check(pImage->GetSizeX() * pImage->GetSizeY() > 0);
			
			// Mips to store
			int32 MipsToStore = 1;

			int32 FirstLODIndexIndex = Program.ConstantImageLODIndices.Num();

			FImageOperator& ImOp = Options.ImageOperator;
			Ptr<const Image> pMip;

			if (!Options.bSeparateImageMips)
			{
				pMip = pImage;
			}
			else
			{
				// We may want the full mipmaps for fragments of images, regardless of the resident mip size, for intermediate operations.
				// \TODO: Calculate the mip ranges that makes sense to store.
				int32 MaxMipmaps = Image::GetMipmapCount(pImage->GetSizeX(), pImage->GetSizeY());
				MipsToStore = MaxMipmaps;

				// Some images cannot be resized or mipmaped
				bool bCannotBeScaled = pImage->m_flags & Image::IF_CANNOT_BE_SCALED;
				if (bCannotBeScaled)
				{
					// Store only the mips that we have already calculated. We assume we have calculated them correctly.
					MipsToStore = pImage->GetLODCount();
				}

				if (pImage->GetLODCount() == 1)
				{
					pMip = pImage;
				}
				else
				{
					pMip = ImOp.ExtractMip(pImage.get(), 0);
				}
			}

			// Temporary uncompressed version of the image, if we need to generate the mips and the source is compressed.
			Ptr<const Image> UncompressedMip;
			EImageFormat UncompressedFormat = GetUncompressedFormat( pMip->GetFormat() );

			for (int32 Mip = 0; Mip < MipsToStore; ++Mip)
			{
				check(pMip->GetFormat() == pImage->GetFormat());

				// Ensure unique at mip level
				int32 MipIndex = -1;

				// Use a map-based deduplication only if we are splitting mips.
				if (Options.bSeparateImageMips)
				{
					MUTABLE_CPUPROFILER_SCOPE(Deduplicate);

					const int32* IndexPtr = Options.ImageConstantMipMap.Find(pMip);
					if (IndexPtr)
					{
						MipIndex = *IndexPtr;
					}
				}

				if (MipIndex<0)
				{
					MipIndex = Program.ConstantImageLODs.Add(TPair<int32, Ptr<const Image>>(-1, pMip));
					Options.ImageConstantMipMap.Add(pMip, MipIndex);
				}

				Program.ConstantImageLODIndices.Add(uint32(MipIndex));

				// Generate next mip if necessary
				if (Mip + 1 < MipsToStore)
				{
					Ptr<Image> NewMip;
					if (Mip+1 < pImage->GetLODCount())
					{
						// Extract directly from source image
						NewMip = ImOp.ExtractMip(pImage.get(), Mip + 1);
					}
					else
					{
						// Generate from the last mip.
						if (UncompressedFormat!=pMip->GetFormat())
						{
							int32 Quality = 4; // TODO

							if (!UncompressedMip)
							{
								UncompressedMip = ImOp.ImagePixelFormat(Quality, pMip.get(), UncompressedFormat);
							}

							UncompressedMip = ImOp.ExtractMip(UncompressedMip.get(), 1);
							NewMip = ImOp.ImagePixelFormat(Quality, UncompressedMip.get(), pMip->GetFormat());
						}
						else
						{
							NewMip = ImOp.ExtractMip(pMip.get(), 1);
						}
					}
					check(NewMip);

					pMip = NewMip;
				}
			}

			FImageLODRange LODRange;
			LODRange.FirstIndex = FirstLODIndexIndex;
			LODRange.LODCount = MipsToStore;
			LODRange.ImageFormat = pImage->GetFormat();
			LODRange.ImageSizeX = pImage->GetSizeX();
			LODRange.ImageSizeY = pImage->GetSizeY();
			int32 ImageIndex = Program.ConstantImages.Add(LODRange);
			return ImageIndex;
		}
	}

	//-------------------------------------------------------------------------------------------------
	void ASTOpConstantResource::Link(FProgram& program, FLinkerOptions* Options)
	{
		MUTABLE_CPUPROFILER_SCOPE(ASTOpConstantResource_Link);

		if (!linkedAddress && !bLinkedAndNull)
		{
			if (Type == OP_TYPE::ME_CONSTANT)
			{
				OP::MeshConstantArgs args;
				FMemory::Memset(&args, 0, sizeof(args));

				Ptr<Mesh> MeshData = static_cast<const Mesh*>(GetValue().get())->Clone();
				check(MeshData);

				args.skeleton = -1;
				if (Ptr<const Skeleton> pSkeleton = MeshData->GetSkeleton())
				{
					args.skeleton = program.AddConstant(pSkeleton.get());
					MeshData->SetSkeleton(nullptr);
				}

				args.physicsBody = -1;
				if (Ptr<const PhysicsBody> pPhysicsBody = MeshData->GetPhysicsBody())
				{
					args.physicsBody = program.AddConstant(pPhysicsBody.get());
					MeshData->SetPhysicsBody(nullptr);
				}

				// Use a map-based deduplication
				mu::Ptr<const mu::Mesh> Key = MeshData;
				const int32* IndexPtr = Options->MeshConstantMap.Find(Key);
				if (!IndexPtr)
				{
					args.value = program.AddConstant(MeshData.get());

					int32 DataDescIndex = Options->AdditionalData.SourceMeshPerConstant.Add(SourceDataDescriptor);
					check(DataDescIndex == args.value);

					Options->MeshConstantMap.Add(MeshData, int32(args.value));
				}
				else
				{
					args.value = *IndexPtr;
				}

				linkedAddress = (OP::ADDRESS)program.m_opAddress.Num();
				program.m_opAddress.Add((uint32_t)program.m_byteCode.Num());
				AppendCode(program.m_byteCode, Type);
				AppendCode(program.m_byteCode, args);
			}
			else
			{
				OP::ResourceConstantArgs args;
				FMemory::Memset(&args, 0, sizeof(args));

				bool bValidData = true;

				switch (Type)
				{
				case OP_TYPE::IM_CONSTANT:
				{
					Ptr<const Image> pTyped = static_cast<const Image*>(GetValue().get());
					check(pTyped);

					if (pTyped->GetSizeX() * pTyped->GetSizeY() == 0)
					{
						// It's an empty or degenerated image, return a null operation.
						bValidData = false;
					}
					else
					{
						args.value = AddConstantImage( program, pTyped, *Options);

						int32 DataDescIndex = Options->AdditionalData.SourceImagePerConstant.Add(SourceDataDescriptor);
						check(DataDescIndex == args.value);
					}

					break;
				}
				case OP_TYPE::LA_CONSTANT:
				{
					Ptr<const Layout> pTyped = static_cast<const Layout*>(GetValue().get());
					check(pTyped);
					args.value = program.AddConstant(pTyped);
					break;
				}
				default:
					check(false);
				}

				if (bValidData)
				{
					linkedAddress = (OP::ADDRESS)program.m_opAddress.Num();
					program.m_opAddress.Add((uint32)program.m_byteCode.Num());
					AppendCode(program.m_byteCode, Type);
					AppendCode(program.m_byteCode, args);
				}
				else
				{
					// Null op
					linkedAddress = 0;
					bLinkedAndNull = true;
				}
			}

			// Clear stored value to reduce memory usage.
			LoadedValue = nullptr;
			Proxy = nullptr;
		}
	}


	//-------------------------------------------------------------------------------------------------
	FImageDesc ASTOpConstantResource::GetImageDesc(bool, class FGetImageDescContext*) const
	{
		FImageDesc Result;

		if (Type == OP_TYPE::IM_CONSTANT)
		{
			// TODO: cache to avoid disk loading
			Ptr<const Image> ConstImage = static_cast<const Image*>(GetValue().get());
			Result.m_format = ConstImage->GetFormat();
			Result.m_lods = ConstImage->GetLODCount();
			Result.m_size = ConstImage->GetSize();
		}
		else
		{
			check(false);
		}

		return Result;
	}

	//-------------------------------------------------------------------------------------------------
	void ASTOpConstantResource::GetBlockLayoutSize(uint64 BlockId, int32* BlockX, int32* BlockY, FBlockLayoutSizeCache*)
	{
		switch (Type)
		{
		case OP_TYPE::LA_CONSTANT:
		{
			Ptr<const Layout> pLayout = static_cast<const Layout*>(GetValue().get());
			check(pLayout);

			if (pLayout)
			{
				int relId = pLayout->FindBlock(BlockId);
				if (relId >= 0)
				{
					*BlockX = pLayout->Blocks[relId].Size[0];
					*BlockY = pLayout->Blocks[relId].Size[1];
				}
				else
				{
					*BlockX = 0;
					*BlockY = 0;
				}
			}

			break;
		}
		default:
			check(false);
		}
	}


	//-------------------------------------------------------------------------------------------------
	void ASTOpConstantResource::GetLayoutBlockSize(int32* pBlockX, int32* pBlockY)
	{
		switch (Type)
		{

		case OP_TYPE::IM_CONSTANT:
		{
			// We didn't find any layout.
			*pBlockX = 0;
			*pBlockY = 0;
			break;
		}

		default:
			checkf(false, TEXT("Instruction not supported"));
		}
	}


	//-------------------------------------------------------------------------------------------------
	bool ASTOpConstantResource::GetNonBlackRect(FImageRect& maskUsage) const
	{
		if (Type == OP_TYPE::IM_CONSTANT)
		{
			// TODO: cache
			Ptr<const Image> pMask = static_cast<const Image*>(GetValue().get());
			pMask->GetNonBlackRect(maskUsage);
			return true;
		}

		return false;
	}


	//-------------------------------------------------------------------------------------------------
	bool ASTOpConstantResource::IsImagePlainConstant(FVector4f& colour) const
	{
		bool res = false;
		switch (Type)
		{

		case OP_TYPE::IM_CONSTANT:
		{
			Ptr<const Image> pImage = static_cast<const Image*>(GetValue().get());
			if (pImage->GetSizeX() <= 0 || pImage->GetSizeY() <= 0)
			{
				res = true;
				colour = FVector4f(0.0f,0.0f,0.0f,1.0f);
			}
			else if (pImage->m_flags & Image::IF_IS_PLAIN_COLOUR_VALID)
			{
				if (pImage->m_flags & Image::IF_IS_PLAIN_COLOUR)
				{
					res = true;
					colour = pImage->Sample(FVector2f(0, 0));
				}
				else
				{
					res = false;
				}
			}
			else
			{
				if (pImage->IsPlainColour(colour))
				{
					res = true;
					pImage->m_flags |= Image::IF_IS_PLAIN_COLOUR;
				}

				pImage->m_flags |= Image::IF_IS_PLAIN_COLOUR_VALID;
			}
			break;
		}

		default:
			break;
		}

		return res;
	}


	//-------------------------------------------------------------------------------------------------
	ASTOpConstantResource::~ASTOpConstantResource()
	{
	}


	//-------------------------------------------------------------------------------------------------
	uint64 ASTOpConstantResource::GetValueHash() const
	{
		return ValueHash;
	}


	//-------------------------------------------------------------------------------------------------
	mu::Ptr<const RefCounted> ASTOpConstantResource::GetValue() const
	{
		if (LoadedValue)
		{
			return LoadedValue;
		}
		else
		{
			switch (Type)
			{

			case OP_TYPE::IM_CONSTANT:
			{
				Ptr<ResourceProxy<Image>> typedProxy = static_cast<ResourceProxy<Image>*>(Proxy.get());
				Ptr<const Image> r = typedProxy->Get();
				return r;
			}

			default:
				check(false);
				break;
			}
		}

		return nullptr;
	}


	//-------------------------------------------------------------------------------------------------
	void ASTOpConstantResource::SetValue(const Ptr<const RefCounted>& v, FProxyFileContext* DiskCacheContext)
	{
		MUTABLE_CPUPROFILER_SCOPE(ASTOpConstantResource_SetValue);

		switch (Type)
		{
		case OP_TYPE::IM_CONSTANT:
		{
			Ptr<const Image> r = static_cast<const Image*>(v.get());

			OutputHashStream stream;
			{
				MUTABLE_CPUPROFILER_SCOPE(Serialize);
				OutputArchive arch(&stream);
				Image::Serialise(r.get(), arch);
			}

			ValueHash = stream.GetHash();

			if (DiskCacheContext)
			{
				Proxy = new ResourceProxyTempFile<Image>(r.get(), *DiskCacheContext);
			}
			else
			{
				LoadedValue = r;
			}
			break;
		}

		case OP_TYPE::ME_CONSTANT:
		{
			Ptr<const Mesh> r = static_cast<const Mesh*>(v.get());

			OutputHashStream stream;
			{
				MUTABLE_CPUPROFILER_SCOPE(Serialize);
				OutputArchive arch(&stream);
				Mesh::Serialise(r.get(), arch);
			}

			ValueHash = stream.GetHash();

			LoadedValue = v;
			break;
		}

		case OP_TYPE::LA_CONSTANT:
		{
			Ptr<const Layout> r = static_cast<const Layout*>(v.get());

			OutputHashStream stream;
			{
				MUTABLE_CPUPROFILER_SCOPE(Serialize);
				OutputArchive arch(&stream);
				Layout::Serialise(r.get(), arch);
			}

			ValueHash = stream.GetHash();

			LoadedValue = v;
			break;
		}

		default:
			LoadedValue = v;
			break;
		}
	}


	mu::Ptr<ImageSizeExpression> ASTOpConstantResource::GetImageSizeExpression() const
	{
		if (Type==OP_TYPE::IM_CONSTANT)
		{
			Ptr<ImageSizeExpression> pRes = new ImageSizeExpression;
			pRes->type = ImageSizeExpression::ISET_CONSTANT;
			Ptr<const Image> pConst = static_cast<const Image*>(GetValue().get());
			pRes->size = pConst->GetSize();
			return pRes;
		}

		return nullptr;
	}


	FSourceDataDescriptor ASTOpConstantResource::GetSourceDataDescriptor(FGetSourceDataDescriptorContext*) const
	{
		return SourceDataDescriptor;
	}

}
