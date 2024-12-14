// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/Image.h"
#include "MuR/Operations.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/AST.h"


namespace mu
{
	struct FProgram;
	struct FProxyFileContext;

	/** A constant mesh, image, volume or layout. */
	class ASTOpConstantResource final : public ASTOp
	{
	private:

		//!
		Ptr<const RefCounted> LoadedValue;
		Ptr<RefCounted> Proxy;

		//! Value hash
		uint64 ValueHash;

		//! We tried to link already but the result is a null op.
		bool bLinkedAndNull = false;

	public:

		//! Type of constant
		OP_TYPE Type;

		/** Source data descriptor. */
		FSourceDataDescriptor SourceDataDescriptor;

	public:

		~ASTOpConstantResource() override;

		// Own interface

		//! Get a hash of the stored value.
		uint64 GetValueHash() const;

		//! Get a copy of the stored value
		Ptr<const RefCounted> GetValue() const;

		/** Set the value to store in this op.
		* If the DiskCacheContext is not null, the disk cache will be used.
		*/
		void SetValue(const Ptr<const RefCounted>& v, FProxyFileContext* DiskCacheContext);


		// ASTOp interface
		virtual OP_TYPE GetOpType() const override { return Type; }
		virtual void ForEachChild(const TFunctionRef<void(ASTChild&)>) override;
		virtual bool IsEqual(const ASTOp& otherUntyped) const override;
		virtual Ptr<ASTOp> Clone(MapChildFuncRef mapChild) const override;
		virtual uint64 Hash() const override;
		virtual void Link(FProgram& program, FLinkerOptions*) override;
		virtual FImageDesc GetImageDesc(bool, class FGetImageDescContext*) const override;
		virtual void GetBlockLayoutSize(uint64 BlockId, int32* pBlockX, int32* pBlockY, FBlockLayoutSizeCache* cache) override;
		virtual void GetLayoutBlockSize(int32* pBlockX, int32* pBlockY) override;
		virtual bool GetNonBlackRect(FImageRect& maskUsage) const override;
		virtual bool IsImagePlainConstant(FVector4f& colour) const override;
		virtual Ptr<ImageSizeExpression> GetImageSizeExpression() const override;
		virtual FSourceDataDescriptor GetSourceDataDescriptor(FGetSourceDataDescriptorContext*) const override;
	};


}

