// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Linq;
using System.Security.Policy;
using System.Text;

namespace DatasmithSolidworks
{
    public class FMeshData
    {
	    public readonly FVec3[] Vertices;
	    public readonly FVec3[] Normals;
	    public readonly FVec2[] TexCoords;
        public readonly FTriangle[] Triangles;
        public int HashCode => ComputeHashCode();
        
        public FMeshData(FVec3[] InVertices, FVec3[] InNormals, FVec2[] InTexCoords, FTriangle[] InTriangles)
        {
	        Vertices = InVertices;
	        Normals = InNormals;
	        TexCoords = InTexCoords;
	        Triangles = InTriangles;
        }
        
        public static FMeshData Create(List<FGeometryChunk> InChunks)
		{
			List<FTriangle> AllTriangles = new List<FTriangle>();

			List<FVertex> Verts = new List<FVertex>();

			int VertexOffset = 0;
			foreach (FGeometryChunk Chunk in InChunks)
			{
				bool bHasUv = Chunk.TexCoords != null;
				for (int Idx = 0; Idx < Chunk.Vertices.Length; Idx++)
				{
					Verts.Add(
						new FVertex(Chunk.Vertices[Idx] * 100f /*SwSingleton.GeometryScale*/,
						Chunk.Normals[Idx],
						bHasUv ? Chunk.TexCoords[Idx] : FVec2.Zero,
						VertexOffset + Idx));

				}
				foreach (FTriangle Triangle in Chunk.Triangles)
				{
					FTriangle OffsetTriangle = Triangle.Offset(VertexOffset);
					AllTriangles.Add(new FTriangle(OffsetTriangle[0], OffsetTriangle[1], OffsetTriangle[2], Triangle.MaterialID));
				}
				VertexOffset += Chunk.Vertices.Length;
			}
			
			FVec3[] Vertices = new FVec3[Verts.Count];
			FVec2[] TexCoords = new FVec2[Verts.Count];
			FVec3[] Normals = new FVec3[AllTriangles.Count * 3];

			for (int Idx = 0; Idx < Verts.Count; Idx++)
			{
				Vertices[Idx] = Verts[Idx].P;
				TexCoords[Idx] = Verts[Idx].UV;
			}

			for (int I = 0; I < AllTriangles.Count; I++)
			{
				FTriangle Triangle = AllTriangles[I];
				int Idx = I * 3;

				Normals[Idx + 0] = Verts[Triangle.Index1].N;
				Normals[Idx + 1] = Verts[Triangle.Index2].N;
				Normals[Idx + 2] = Verts[Triangle.Index3].N;
			}
			
			return new FMeshData(Vertices, Normals, TexCoords, AllTriangles.ToArray());
		}

		private int ComputeHashCode()
		{
			int Hash = 0;
			
			foreach (FVec3 Vertex in Vertices)
			{
				Hash ^= Vertex.GetHashCode();
			}
			
			foreach (FVec3 Normal in Normals)
			{
				Hash ^= Normal.GetHashCode();
			}
			
			foreach (FVec2 TexCoord in TexCoords)
			{
				Hash ^= TexCoord.GetHashCode();
			}
			
			foreach (FTriangle Triangle in Triangles)
			{
				Hash ^= Triangle.GetHashCode();
			}
			
			return Hash;
		}
		
		public override string ToString()
		{
			StringBuilder S = new StringBuilder();
			
			S.Append($"HashCode: {ComputeHashCode()}");
			S.AppendLine();
			
			S.Append("Vertices:");
			S.AppendLine();
			if (Vertices != null)
			{
				foreach (FVec3 V in Vertices)
				{
					S.Append($"{V}");
					S.AppendLine();
				}
			}
			else
			{
				S.Append("<null>");
				S.AppendLine();
			}
			S.Append("Normals:");
			S.AppendLine();
			if (Vertices != null)
			{
				foreach (FVec3 N in Normals)
				{
					S.Append($"{N}");
					S.AppendLine();
				}
			}
			else
			{
				S.Append("<null>");
				S.AppendLine();
			}
			S.Append("TexCoords:");
			S.AppendLine();
			if (Vertices != null)
			{
				foreach (FVec2 T in TexCoords)
				{
					S.Append($"{T}");
					S.AppendLine();
				}
			}
			else
			{
				S.Append("<null>");
				S.AppendLine();
			}
			S.Append("Triangles:");
			S.AppendLine();
			if (Triangles != null)
			{
				foreach (FTriangle T in Triangles)
				{
					S.Append($"{T}");
					S.AppendLine();
				}
			}
			else
			{
				S.Append("<null>");
				S.AppendLine();
			}
			return S.ToString();
		}
    }
}
