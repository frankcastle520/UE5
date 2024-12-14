// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

inline FIntPoint ToIntPoint( const FVector3f& V )
{
	return FIntPoint(
		FMath::RoundToInt( V.X ),
		FMath::RoundToInt( V.Y ) );
}

template< typename FWritePixel >
void RasterizeTri( const FVector3f Verts[3], const FIntRect& ScissorRect, uint32 SubpixelDilate, bool bBackFaceCull, FWritePixel WritePixel )
{
	constexpr uint32 SubpixelBits = 8;
	constexpr uint32 SubpixelSamples = 1 << SubpixelBits;

	// 24.8 fixed point
	FIntPoint Vert0 = ToIntPoint( Verts[0] * SubpixelSamples );
	FIntPoint Vert1 = ToIntPoint( Verts[1] * SubpixelSamples );
	FIntPoint Vert2 = ToIntPoint( Verts[2] * SubpixelSamples );

	// 12.8 fixed point
	FIntPoint Edge01 = Vert0 - Vert1;
	FIntPoint Edge12 = Vert1 - Vert2;
	FIntPoint Edge20 = Vert2 - Vert0;

	int64 DetXY = Edge01.Y * Edge20.X - Edge01.X * Edge20.Y;
	bool bBackFace = DetXY >= 0;
	if( bBackFace )
	{
		if( bBackFaceCull )
		{
			return;
		}
		else
		{
			// Swap winding order
			Edge01 *= -1;
			Edge12 *= -1;
			Edge20 *= -1;
		}
	}

	// Bounding rect
	FIntRect RectSubpixel( Vert0, Vert0 );
	RectSubpixel.Include( Vert1 );
	RectSubpixel.Include( Vert2 );
	RectSubpixel.InflateRect( SubpixelDilate );

	// Round to nearest pixel
	FIntRect RectPixel = ( ( RectSubpixel + (SubpixelSamples / 2) - 1 ) ) / SubpixelSamples;

	// Clip to viewport
	RectPixel.Clip( ScissorRect );
	
	// Cull when no pixels covered
	if( RectPixel.IsEmpty() )
		return;

	// Rebase off MinPixel with half pixel offset
	// 12.8 fixed point
	// Max triangle size = 2047x2047 pixels
	const FIntPoint BaseSubpixel = RectPixel.Min * SubpixelSamples + (SubpixelSamples / 2);
	Vert0 -= BaseSubpixel;
	Vert1 -= BaseSubpixel;
	Vert2 -= BaseSubpixel;

	auto EdgeC = [=]( const FIntPoint& Edge, const FIntPoint& Vert )
	{
		int64 ex = Edge.X;
		int64 ey = Edge.Y;
		int64 vx = Vert.X;
		int64 vy = Vert.Y;

		// Half-edge constants
		// 24.16 fixed point
		int64 C = ey * vx - ex * vy;

		// Correct for fill convention
		// Top left rule for CCW
		C -= ( Edge.Y < 0 || ( Edge.Y == 0 && Edge.X > 0 ) ) ? 0 : 1;

		// Dilate edges
		C += ( FMath::Abs( Edge.X ) + FMath::Abs( Edge.Y ) ) * SubpixelDilate;

		// Step in pixel increments
		// Low bits would always be the same and thus don't matter when testing sign.
		// 24.8 fixed point
		return int32( C >> SubpixelBits );
	};

	int32 C0 = EdgeC( Edge12, Vert1 );
	int32 C1 = EdgeC( Edge20, Vert2 );
	int32 C2 = EdgeC( Edge01, Vert0 );
	
	int32 CY0 = C0;
	int32 CY1 = C1;
	int32 CY2 = C2;

	for( int32 y = RectPixel.Min.Y; y < RectPixel.Max.Y; y++ )
	{
		int32 CX0 = CY0;
		int32 CX1 = CY1;
		int32 CX2 = CY2;

		for( int32 x = RectPixel.Min.X; x < RectPixel.Max.X; x++ )
		{
			if( ( CX0 | CX1 | CX2 ) >= 0 )
			{
				FIntPoint p = ( FIntPoint(x,y) - RectPixel.Min ) * SubpixelSamples;
				FVector2f p0 = Vert0 - p;
				FVector2f p1 = Vert1 - p;
				FVector2f p2 = Vert2 - p;
				// Not perspective correct
				FVector3f Barycentrics(
					(float)Edge12.Y * p1.X - (float)Edge12.X * p1.Y,
					(float)Edge20.Y * p2.X - (float)Edge20.X * p2.Y,
					(float)Edge01.Y * p0.X - (float)Edge01.X * p0.Y );
				Barycentrics /= Barycentrics[0] + Barycentrics[1] + Barycentrics[2];

				float Depth =
					Verts[0].Z * Barycentrics[0] +
					Verts[1].Z * Barycentrics[1] +
					Verts[2].Z * Barycentrics[2];

				WritePixel( x, y, Depth, Barycentrics );
			}

			CX0 -= Edge12.Y;
			CX1 -= Edge20.Y;
			CX2 -= Edge01.Y;
		}

		CY0 += Edge12.X;
		CY1 += Edge20.X;
		CY2 += Edge01.X;
	}
}

template< typename FWriteVoxel >
void VoxelizeTri( const FVector3f Triangle[3], FIntVector3 ScissorMin, FIntVector3 ScissorMax, FWriteVoxel WriteVoxel )
{
	// 6-separating voxelization
	{
		FIntRect Scissor(
			ScissorMin.X, ScissorMin.Y,
			ScissorMax.X, ScissorMax.Y );

		RasterizeTri( Triangle, Scissor, 0, false,
			[&]( int32 x, int32 y, float fz, const FVector3f& Barycentrics )
			{
				int32 z = FMath::RoundToInt( fz );
				if( ScissorMin.Z <= z && z < ScissorMax.Z )
					WriteVoxel( x, y, z, Barycentrics );
			} );
	}
	{
		FVector3f TriangleYZX[3];
		for( int i = 0; i < 3; i++ )
			TriangleYZX[i] = FVector3f( Triangle[i].Y, Triangle[i].Z, Triangle[i].X );

		FIntRect Scissor(
			ScissorMin.Y, ScissorMin.Z,
			ScissorMax.Y, ScissorMax.Z );

		RasterizeTri( TriangleYZX, Scissor, 0, false,
			[&]( int32 y, int32 z, float fx, const FVector3f& Barycentrics )
			{
				int32 x = FMath::RoundToInt( fx );
				if( ScissorMin.X <= x && x < ScissorMax.X )
					WriteVoxel( x, y, z, Barycentrics );
			} );
	}
	{
		FVector3f TriangleZXY[3];
		for( int i = 0; i < 3; i++ )
			TriangleZXY[i] = FVector3f( Triangle[i].Z, Triangle[i].X, Triangle[i].Y );

		FIntRect Scissor(
			ScissorMin.Z, ScissorMin.X,
			ScissorMax.Z, ScissorMax.X );

		RasterizeTri( TriangleZXY, Scissor, 0, false,
			[&]( int32 z, int32 x, float fy, const FVector3f& Barycentrics )
			{
				int32 y = FMath::RoundToInt( fy );
				if( ScissorMin.Y <= y && y < ScissorMax.Y )
					WriteVoxel( x, y, z, Barycentrics );
			} );
	}
}