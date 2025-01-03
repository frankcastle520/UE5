// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using SolidWorks.Interop.sldworks;
using System.Runtime.InteropServices;
using SolidWorks.Interop.swconst;
using System.Threading.Tasks;
using System.Collections.Concurrent;
using System.Diagnostics;

namespace DatasmithSolidworks
{
	public class FBody
	{
		public Body2 Body { get; private set; } = null;
		public FBoundingBox Bounds { get; private set; }

		public class FBodyFace
		{
			public Face2 Face { get; private set; } = null;

			public FBodyFace(Face2 InFace)
			{
				Face = InFace;
			}

			public FTriangleStrip ExtractGeometry()
			{
				FTriangleStrip Triangles = null;

				dynamic DynamicTris = Face.GetTessTriStrips(true);
				dynamic DynamicNormals = Face.GetTessTriStripNorms();

				if (DynamicTris != null && DynamicNormals != null)
				{
					Triangles = new FTriangleStrip(DynamicTris as float[], DynamicNormals as float[]);
				}
				return (Triangles.NumTris > 0) ? Triangles : null;
			}
		}

		public FBody(Body2 InBody)
		{
			Body = InBody;

			double[] BoundsArray = Body.GetBodyBox() as double[];
			Bounds = new FBoundingBox();
			Bounds.Add(new FVec3(BoundsArray[0], BoundsArray[1], BoundsArray[2]));
			Bounds.Add(new FVec3(BoundsArray[3], BoundsArray[4], BoundsArray[5]));

		}

		public static ConcurrentBag<FBody> FetchBodies(object[] ObjSolidBodies, object[] ObjSheetBodies)
		{
			ConcurrentBag<FBody> ResultBodies = new ConcurrentBag<FBody>();
			List <object> AllBodies = new List<object>();

			if (ObjSolidBodies != null)
			{
				foreach (object ObjSolidBody in ObjSolidBodies)
				{
					AllBodies.Add(ObjSolidBody);
				}
			}
			if (ObjSheetBodies != null)
			{
				foreach (object ObjSheetBody in ObjSheetBodies)
				{
					AllBodies.Add(ObjSheetBody);
				}
			}

			foreach(object ObjBody in AllBodies)
			{
				if (ObjBody is Body2 Body && Body.Visible && !Body.IsTemporaryBody())
				{
					ResultBodies.Add(new FBody(Body));
				}
			}

			return ResultBodies;
		}

		public static ConcurrentBag<FBody> FetchBodies(Component2 InComponent)
		{
			return FetchBodies(
				InComponent.GetBodies((int)swBodyType_e.swSolidBody),
				InComponent.GetBodies((int)swBodyType_e.swSheetBody));
		}

		public static ConcurrentBag<FBody> FetchBodies(PartDoc InPartDoc)
		{
			return FetchBodies(
				InPartDoc.GetBodies((int)swBodyType_e.swSolidBody),
				InPartDoc.GetBodies((int)swBodyType_e.swSheetBody));
		}
	}
}