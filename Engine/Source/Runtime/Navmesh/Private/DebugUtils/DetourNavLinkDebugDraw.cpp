// Copyright Epic Games, Inc. All Rights Reserved.
#include "DebugUtils/DetourNavLinkDebugDraw.h"

#include "DebugUtils/DebugDraw.h"
#include "DebugUtils/RecastDebugDraw.h"
#include "Detour/DetourCommon.h"
#include "Detour/DetourNavLinkBuilder.h"
#include "Recast/Recast.h"

//@UE BEGIN
namespace UE::Detour::Private
{
	dtReal getPathLen(const dtReal* path, int npath)
	{
		if (npath < 2)
			return 0;
		
		dtReal totd = 0;
		for (int i = 0; i < npath-1; ++i)
		{
			const dtReal* sp = &path[i*3];
			const dtReal* sq = &path[(i+1)*3];
			totd += sqrtf(dtVdistSqr(sp,sq));
		}
		
		return totd;
	}

	void getPointAlongPath(dtReal dist, const dtReal* path, int npath, dtReal* pt, dtReal* dir)
	{
		if (npath == 0)
			return;
		
		if (npath == 1)
		{
			if (dir)
				dtVset(dir,1,0,0);
			dtVcopy(pt, path);
			return;
		}
		
		if (dist <= 0)
		{
			if (dir)
				dtVsub(dir, &path[1*3], &path[0*3]);
			
			dtVcopy(pt, path);
			return;
		}
		
		float totd = 0;
		for (int i = 0; i < npath-1; ++i)
		{
			const dtReal* sp = &path[i*3];
			const dtReal* sq = &path[(i+1)*3];
			const float sd = sqrtf(dtVdistSqr(sp,sq));
			if (dist >= totd && dist <= totd+sd)
			{
				dtVlerp(pt, sp,sq,(dist-totd)/sd);
				if (dir)
					dtVsub(dir, sq,sp);
				return;
			}
			totd += sd;
		}
		
		dtVcopy(pt, &path[(npath-1)*3]);
		if (dir)
		{
			dtVsub(dir, &path[(npath-1)*3], &path[(npath-2)*3]);
		}
	}

	void drawTrajectory(duDebugDraw* dd, const dtReal* pa, const dtReal* pb, const dtNavLinkBuilder::Trajectory2D* trajectory, const unsigned int color)
	{
		const float startx = trajectory->spine[0];
		const float endx = trajectory->spine[(trajectory->nspine-1)*2];
		const float deltax = endx - startx;
		
		const float starty = trajectory->spine[1];
		const float endy = trajectory->spine[(trajectory->nspine-1)*2+1];
		
		dtReal pts[3*dtNavLinkBuilder::MAX_SPINE];
		int npts = trajectory->nspine;
		for (int i = 0; i < trajectory->nspine; ++i)
		{
			const float* spt = &trajectory->spine[i*2];
			const float u = (spt[0] - startx)/deltax;
			const float dy = spt[1] - dtLerp(starty, endy, u);
			dtReal* p = &pts[i*3];
			dtVlerp(p, pa, pb, u);
			p[1] += dy;
		}
			
		const float len = getPathLen(pts, npts);
		const int nsegs = ceilf(len / 0.3f);
		
		for (int i = 0; i < nsegs*2; ++i)
		{
			float u = (float)i / (float)(nsegs*2);
			dtReal pt[3];
			getPointAlongPath(u*len, pts, npts, pt, nullptr);
			dd->vertex(pt, color);
		}
	}

	void drawTrajectorySlice(duDebugDraw* dd, const dtReal* pa, const dtReal* pb, const dtNavLinkBuilder::Trajectory2D* trajectory, const dtReal* trajectoryDir, const unsigned int color)
	{
		dtReal start[3];
		dtReal end[3];
		dtVcopy(start, pa);
		dtVcopy(end, pb);
		
		// Offset start and end points to account for the agent radius.
		dtVmad(start, pa, trajectoryDir, -trajectory->radiusOverflow);
		dtVmad(end, pb, trajectoryDir,  trajectory->radiusOverflow);
		
		unsigned int colt = duTransCol(color, 50);
		unsigned int colb = duTransCol(duLerpCol(color,duColor::black,96), 50);
		
		dd->begin(DU_DRAW_QUADS);
		dtReal p0[3], p1[3], p2[3], p3[3];
		const int nsamples = trajectory->samples.Num();
		for (int i = 0; i < nsamples; ++i)
		{
			const dtNavLinkBuilder::TrajectorySample& s = trajectory->samples[i];
			const dtReal u = (float)i / (float)(nsamples-1);
			dtVlerp(p1, start, end, u);
			dtVcopy(p0, p1);
			p0[1] += s.ymin;
			p1[1] += s.ymax;
			
			if (i > 0)
			{
				dd->vertex(p0, colb);
				dd->vertex(p1, colt);
				dd->vertex(p3, colt);
				dd->vertex(p2, colb);

				dd->vertex(p2, colb);
				dd->vertex(p3, colt);
				dd->vertex(p1, colt);
				dd->vertex(p0, colb);
			}
			
			dtVcopy(p2, p0);
			dtVcopy(p3, p1);
		}
		dd->end();

		colb = duDarkenCol(colb);
		colt = duDarkenCol(colt);
		
		dd->begin(DU_DRAW_LINES, 2.0f);
		for (int i = 0; i < nsamples; ++i)
		{
			const dtNavLinkBuilder::TrajectorySample& s = trajectory->samples[i];
			const float u = (float)i / (float)(nsamples-1);
			dtVlerp(p1, start, end, u);
			dtVcopy(p0, p1);
			p0[1] += s.ymin;
			p1[1] += s.ymax;
			
			if (i == 0 || i == (nsamples-1))
			{
				dd->vertex(p0, colb);
				dd->vertex(p1, colt);
			}
			if (i > 0)
			{
				dd->vertex(p0, colb);
				dd->vertex(p2, colb);
				dd->vertex(p1, colt);
				dd->vertex(p3, colt);
			}
			
			dtVcopy(p2, p0);
			dtVcopy(p3, p1);
		}
		dd->end();
	}
} // UE::Detour::Private

void duDebugDrawNavLinkBuilder(duDebugDraw* dd, const dtNavLinkBuilder& linkBuilder, unsigned int drawFlags, const dtNavLinkBuilder::EdgeSampler* es)
{
	if (dd == nullptr)
		return;
	
	if (drawFlags & DRAW_WALKABLE_SURFACE)
	{
		if (linkBuilder.m_chf)
		{
			duDebugDrawCompactHeightfieldSolid(dd, *linkBuilder.m_chf);	
		}
	}

	if (drawFlags & DRAW_BORDERS)
	{
		const TArray<dtNavLinkBuilder::Edge, TInlineAllocator<32>>& edges = linkBuilder.m_edges;
		const int nedges = linkBuilder.m_edges.Num();
		const int selectedEdge = linkBuilder.m_debugSelectedEdge;
		
		if (nedges)
		{
			dd->begin(DU_DRAW_LINES, 3.0f);
			for (int i = 0; i < nedges; ++i)
			{
				// Display edge index
				dtReal x = 0.5*(edges[i].sp[0]+edges[i].sq[0]);
				dtReal y = 0.5*(edges[i].sp[1]+edges[i].sq[1]);
				dtReal z = 0.5*(edges[i].sp[2]+edges[i].sq[2]);
				constexpr int bufferSize = 32;
				char buffer[bufferSize];
				snprintf(buffer, bufferSize, "%i", i);
				dd->text(x, y, z, buffer);
				
				unsigned int col = duRGBA(0,96,128,255);
				if (i == selectedEdge)
					continue;

				dd->vertex(edges[i].sp, col);
				dd->vertex(edges[i].sq, col);
			}
			dd->end();
			
			dd->begin(DU_DRAW_POINTS, 8.0f);
			for (int i = 0; i < nedges; ++i)
			{
				unsigned int col = duRGBA(0,96,128,255);
				if (i == selectedEdge)
					continue;
				
				dd->vertex(edges[i].sp, col);
				dd->vertex(edges[i].sq, col);
			}
			dd->end();
			
			if (selectedEdge >= 0 && selectedEdge < nedges)
			{
				unsigned int col = duRGBA(68,36,36,255);
				dd->begin(DU_DRAW_LINES, 3.0f);
				dd->vertex(edges[selectedEdge].sp, col);
				dd->vertex(edges[selectedEdge].sq, col);
				dd->end();
				dd->begin(DU_DRAW_POINTS, 8.0f);
				dd->vertex(edges[selectedEdge].sp, col);
				dd->vertex(edges[selectedEdge].sq, col);
				dd->end();
			}

			dd->begin(DU_DRAW_POINTS, 4.0f);
			for (int i = 0; i < nedges; ++i)
			{
				unsigned int col = duColor::lightGrey;
				dd->vertex(edges[i].sp, col);
				dd->vertex(edges[i].sq, col);
			}
			dd->end();
		}
	}
	
	if (drawFlags & DRAW_LINKS)
	{
		const bool drawFilteredLinks = drawFlags & DRAW_FILTERED_LINKS;
		unsigned int jumpDownCol0 = duLerpCol(duColor::blue, duColor::white, 200);
		unsigned int jumpDownCol1 = duColor::blue;
		unsigned int jumpOverCol0 = duLerpCol(duColor::lightGrey, duColor::white, 200);
		unsigned int jumpOverCol1 = duColor::lightGrey;

		auto selectColors = [&](dtNavLinkBuilder::JumpLinkFlag flag, dtNavLinkAction action, unsigned int& col0, unsigned int& col1)
		{
			if (action == DT_LINK_ACTION_JUMP_DOWN)
			{
				col0 = jumpDownCol0;
				col1 = jumpDownCol1;
			}
			else if (action == DT_LINK_ACTION_JUMP_OVER)
			{
				col0 = jumpOverCol0;
				col1 = jumpOverCol1;
			}

			if (flag == dtNavLinkBuilder::FILTERED)
			{
				col0 = duColor::grey;
				col1 = duColor::darkGrey;
			}
		};

		unsigned int col0 = duColor::black;
		unsigned int col1 = duColor::black;
		
		const int nlinks = linkBuilder.m_links.Num();
		if (nlinks)
		{
			dd->begin(DU_DRAW_QUADS);
			for (const dtNavLinkBuilder::JumpLink& link : linkBuilder.m_links)
			{
				if (!drawFilteredLinks && link.flags == dtNavLinkBuilder::FILTERED)
					continue;

				selectColors(link.flags, link.action, col0, col1);
				
				for (int j = 0; j < link.nspine-1; ++j)
				{
					int u = (j*255)/link.nspine;
					unsigned int col = duTransCol(duLerpCol(col0,col1,u),128);
					
					dd->vertex(&link.spine1[j*3], col);
					dd->vertex(&link.spine1[(j+1)*3], col);
					dd->vertex(&link.spine0[(j+1)*3], col);
					dd->vertex(&link.spine0[j*3], col);
				}
			}
			dd->end();
			
			dd->begin(DU_DRAW_LINES, 3.0f);
			for (const dtNavLinkBuilder::JumpLink& link : linkBuilder.m_links)
			{
				if (!drawFilteredLinks && link.flags == dtNavLinkBuilder::FILTERED)
					continue;

				selectColors(link.flags, link.action, col0, col1);
				
				for (int j = 0; j < link.nspine-1; ++j)
				{
					unsigned int col = duTransCol(duDarkenCol(col1),128);
					
					dd->vertex(&link.spine0[j*3], col);
					dd->vertex(&link.spine0[(j+1)*3], col);
					dd->vertex(&link.spine1[j*3], col);
					dd->vertex(&link.spine1[(j+1)*3], col);
				}

				dd->vertex(&link.spine0[0], duDarkenCol(col1));
				dd->vertex(&link.spine1[0], duDarkenCol(col1));

				dd->vertex(&link.spine0[(link.nspine-1)*3], duDarkenCol(col1));
				dd->vertex(&link.spine1[(link.nspine-1)*3], duDarkenCol(col1));
			}
			dd->end();

			dd->begin(DU_DRAW_POINTS, 8.0f);
			for (const dtNavLinkBuilder::JumpLink& link : linkBuilder.m_links)
			{
				if (link.flags == dtNavLinkBuilder::FILTERED)
					continue;

				selectColors(link.flags, link.action, col0, col1);
				
				dd->vertex(&link.spine0[0], duDarkenCol(col1));
				dd->vertex(&link.spine1[0], duDarkenCol(col1));
				dd->vertex(&link.spine0[(link.nspine-1)*3], duDarkenCol(col1));
				dd->vertex(&link.spine1[(link.nspine-1)*3], duDarkenCol(col1));
			}
			dd->end();
			
			dd->begin(DU_DRAW_POINTS, 4.0f);
			for (const dtNavLinkBuilder::JumpLink& link : linkBuilder.m_links)
			{
				if (link.flags == dtNavLinkBuilder::FILTERED)
					continue;
				
				dd->vertex(&link.spine0[0], duColor::lightGrey);
				dd->vertex(&link.spine1[0], duColor::lightGrey);
				dd->vertex(&link.spine0[(link.nspine-1)*3], duColor::lightGrey);
				dd->vertex(&link.spine1[(link.nspine-1)*3], duColor::lightGrey);
			}
			dd->end();
		}
	}

	if (drawFlags & DRAW_SELECTED_EDGE)
	{
		if (es && es->action != DT_LINK_ACTION_UNSET)
		{
			dd->begin(DU_DRAW_LINES, 2.0f);

			// Axis
			constexpr float d = 20.f;
			dd->vertex(es->rigp, duColor::red);
			dd->vertex(es->rigp[0]+es->ax[0]*d, es->rigp[1]+es->ax[1]*d, es->rigp[2]+es->ax[2]*d, duColor::red);

			dd->vertex(es->rigp, duColor::green);
			dd->vertex(es->rigp[0]+es->ay[0]*d, es->rigp[1]+es->ay[1]*d, es->rigp[2]+es->ay[2]*d, duColor::green);

			dd->vertex(es->rigp, duColor::blue);
			dd->vertex(es->rigp[0]+es->az[0]*d, es->rigp[1]+es->az[1]*d, es->rigp[2]+es->az[2]*d, duColor::blue);
			
			dd->end();
			
			if (drawFlags & DRAW_TRAJECTORY)
			{
				const float r = es->groundRange;

				unsigned int col = duLerpCol(duRGBA(255,192,0,255), duColor::white, 64);
				unsigned int cola = duTransCol(col,192);
				unsigned int colb = duColor::white;
				
				// Start segment.
				dd->begin(DU_DRAW_LINES, 3.0f);
				dd->vertex(es->start.p[0],es->start.p[1],es->start.p[2], col);
				dd->vertex(es->start.q[0],es->start.q[1],es->start.q[2], col);
				dd->end();
				
				dd->begin(DU_DRAW_LINES, 1.0f);
				dd->vertex(es->start.p[0],es->start.p[1]-r,es->start.p[2], colb);
				dd->vertex(es->start.p[0],es->start.p[1]+r,es->start.p[2], colb);
				dd->vertex(es->start.p[0],es->start.p[1]+r,es->start.p[2], colb);
				dd->vertex(es->start.q[0],es->start.q[1]+r,es->start.q[2], colb);
				dd->vertex(es->start.q[0],es->start.q[1]+r,es->start.q[2], colb);
				dd->vertex(es->start.q[0],es->start.q[1]-r,es->start.q[2], colb);
				dd->vertex(es->start.q[0],es->start.q[1]-r,es->start.q[2], colb);
				dd->vertex(es->start.p[0],es->start.p[1]-r,es->start.p[2], colb);
				dd->end();
				
				// End segment.
				dd->begin(DU_DRAW_LINES, 3.0f);
				dd->vertex(es->end.p[0],es->end.p[1],es->end.p[2], col);
				dd->vertex(es->end.q[0],es->end.q[1],es->end.q[2], col);
				dd->end();
				
				dd->begin(DU_DRAW_LINES, 1.0f);
				dd->vertex(es->end.p[0],es->end.p[1]-r,es->end.p[2], colb);
				dd->vertex(es->end.p[0],es->end.p[1]+r,es->end.p[2], colb);
				dd->vertex(es->end.p[0],es->end.p[1]+r,es->end.p[2], colb);
				dd->vertex(es->end.q[0],es->end.q[1]+r,es->end.q[2], colb);
				dd->vertex(es->end.q[0],es->end.q[1]+r,es->end.q[2], colb);
				dd->vertex(es->end.q[0],es->end.q[1]-r,es->end.q[2], colb);
				dd->vertex(es->end.q[0],es->end.q[1]-r,es->end.q[2], colb);
				dd->vertex(es->end.p[0],es->end.p[1]-r,es->end.p[2], colb);
				dd->end();
		 
				dd->begin(DU_DRAW_LINES, 4.0f);
				UE::Detour::Private::drawTrajectory(dd, es->start.p, es->end.p, &es->trajectory, cola);
				UE::Detour::Private::drawTrajectory(dd, es->start.q, es->end.q, &es->trajectory, cola);
				dd->end();


				dd->begin(DU_DRAW_POINTS, 8.0f);
				dd->vertex(es->start.p[0],es->start.p[1],es->start.p[2], duDarkenCol(col));
				dd->vertex(es->start.q[0],es->start.q[1],es->start.q[2], duDarkenCol(col));
				dd->vertex(es->end.p[0],es->end.p[1],es->end.p[2], duDarkenCol(col));
				dd->vertex(es->end.q[0],es->end.q[1],es->end.q[2], duDarkenCol(col));
				dd->end();
				
				unsigned int colm = duColor::white;
				dd->begin(DU_DRAW_POINTS, 3.0f);
				dd->vertex(es->start.p[0],es->start.p[1],es->start.p[2], colm);
				dd->vertex(es->start.q[0],es->start.q[1],es->start.q[2], colm);
				dd->vertex(es->end.p[0],es->end.p[1],es->end.p[2], colm);
				dd->vertex(es->end.q[0],es->end.q[1],es->end.q[2], colm);
				dd->end();
			}
			
			if (drawFlags & DRAW_LAND_SAMPLES)
			{
				if (es->start.ngsamples)
				{
					dd->begin(DU_DRAW_POINTS, 8.0f);
					for (int i = 0; i < es->start.ngsamples; ++i)
					{
						const dtNavLinkBuilder::GroundSample* s = &es->start.gsamples[i];
						const float u = (float)i/(float)(es->start.ngsamples-1);
						dtReal spt[3], ept[3];
						dtVlerp(spt, es->start.p, es->start.q, u);
						dtVlerp(ept, es->start.p, es->start.q, u);
						
						unsigned int col = duColor::darkGrey;
						dtReal off = 1;
						if ((s->flags & dtNavLinkBuilder::HAS_GROUND) == 0)
						{
							off = 0;
							col = duColor::orangeRed;
						}
						
						spt[1] = s->height + off;
						dd->vertex(spt, col);
					}
					dd->end();
					
					dd->begin(DU_DRAW_POINTS, 4.0f);
					for (int i = 0; i < es->start.ngsamples; ++i)
					{
						const dtNavLinkBuilder::GroundSample* s = &es->start.gsamples[i];
						const float u = (float)i/(float)(es->start.ngsamples-1);
						dtReal spt[3], ept[3];
						dtVlerp(spt, es->start.p, es->start.q,u);
						dtVlerp(ept, es->start.p, es->start.q,u);
						float off = 0;
						if (s->flags & dtNavLinkBuilder::HAS_GROUND)
						{
							off = 1.f;
						}
						spt[1] = s->height + off;
						dd->vertex(spt, duColor::green);
					}
					dd->end();
					
				}

				if (es->end.ngsamples)
				{
					dd->begin(DU_DRAW_POINTS, 8.0f);
					for (int i = 0; i < es->end.ngsamples; ++i)
					{
						const dtNavLinkBuilder::GroundSample* s = &es->end.gsamples[i];
						const float u = (float)i/(float)(es->end.ngsamples-1);
						dtReal spt[3], ept[3];
						dtVlerp(spt, es->end.p, es->end.q, u);
						dtVlerp(ept, es->end.p, es->end.q, u);
						
						unsigned int col = duColor::darkGrey;
						float off = 1.f;
						if ((s->flags & dtNavLinkBuilder::HAS_GROUND) == 0)
						{
							off = 0;
							col = duColor::orangeRed;
						}
						
						spt[1] = s->height + off;
						dd->vertex(spt, col);
					}
					dd->end();
					
					dd->begin(DU_DRAW_POINTS, 4.0f);
					for (int i = 0; i < es->end.ngsamples; ++i)
					{
						const dtNavLinkBuilder::GroundSample* s = &es->end.gsamples[i];
						const float u = (float)i/(float)(es->end.ngsamples-1);
						dtReal spt[3], ept[3];
						dtVlerp(spt, es->end.p, es->end.q, u);
						dtVlerp(ept, es->end.p, es->end.q, u);
						
						float off = 0;
						if (s->flags & dtNavLinkBuilder::HAS_GROUND)
						{
							off = 1.f;
						}
						spt[1] = s->height + off;
						dd->vertex(spt, duColor::green);
					}
					dd->end();
				}
			}
			
			if (drawFlags & DRAW_COLLISION_SLICES)
			{
				if (es->start.ngsamples && es->start.ngsamples == es->end.ngsamples)
				{
					const int nsamples = es->start.ngsamples;
					dd->depthMask(true);
					for (int i = 0; i < nsamples; ++i)
					{
						const dtNavLinkBuilder::GroundSample* ssmp = &es->start.gsamples[i];
						const dtNavLinkBuilder::GroundSample* esmp = &es->end.gsamples[i];
						if ((ssmp->flags & dtNavLinkBuilder::HAS_GROUND) == 0 || (esmp->flags & dtNavLinkBuilder::HAS_GROUND) == 0)
							continue;
						
						const float u = (float)i/(float)(nsamples-1);
						dtReal spt[3], ept[3];
						dtVlerp(spt, es->start.p, es->start.q, u);
						dtVlerp(ept, es->end.p, es->end.q, u);
						
						if (ssmp->flags & dtNavLinkBuilder::UNRESTRICTED)
							UE::Detour::Private::drawTrajectorySlice(dd, spt, ept, &es->trajectory, es->az, duColor::green);
						else
							UE::Detour::Private::drawTrajectorySlice(dd, spt, ept, &es->trajectory, es->az, duColor::orangeRed);
					}
					dd->depthMask(true);
				}
			}
		}
	}
}
//@UE END
