using Dia2Lib;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace CruncherSharp
{
    public class SymbolMemberInfo
    {
        public enum MemberCategory
        {
            VTable = 0,
            Base = 1,
            Member = 2,
            UDT = 3,
            Pointer = 4
        }

        public MemberCategory Category { get; set; }
        public string Name { get; set; }
        public string DisplayName
        {
            get
            {
                switch (Category)
                {
                    case MemberCategory.VTable:
                        return "vtable";
                    case MemberCategory.Base:
                        return $"Base: {Name}";
                    default:
                        return Name;
                }
            }
        }
        public string TypeName { get;  }
        public ulong Size { get; set; }

		public uint Count { get;  }
		public uint BitSize { get;  }
        public ulong Offset { get;  }
        public uint BitPosition { get;  }
        public uint PaddingBefore { get; set; }
        public uint BitPaddingAfter { get; set; }

		public uint MinAlignment 
		{ 
			get
			{
				switch (Category)
				{
					case MemberCategory.VTable:
					case MemberCategory.Pointer:
						return 8;

					default:
						if (TypeInfo != null)
							return TypeInfo.ComputeMinAlignment();

						if (PaddingBefore >= 8)
						{
							return 16;
						}

						uint Alignemnt = (uint)Size;
						if (Count > 0)
							Alignemnt = (uint)(Size / Count);

						if (Alignemnt > 16) // impossible, ignore
						{
							Alignemnt = 1;
						}

						if (PaddingBefore >= 4 && Alignemnt < 8)
						{
							Alignemnt = 8;
						}
						else if (PaddingBefore >= 2 && Alignemnt < 4)
						{
							Alignemnt = 4;
						}
						else if (PaddingBefore > 0 && Alignemnt < 2)
						{
							Alignemnt = 2;
						}

						return Alignemnt;
				}
			}
		}

		public bool AlignWithPrevious { get; set; }
        public bool BitField { get; set; }

        public bool Volatile { get; set; }
        public bool Expanded { get; set; }

		public SymbolInfo TypeInfo { get; set; }

		public uint? PotentialSaving { get; set; }

		public SymbolMemberInfo(MemberCategory category, string name, string typeName, ulong size, uint bitSize, ulong offset, uint bitPosition)
        {
            Category = category;
            Name = name;
            TypeName = typeName;
            Size = size;
            BitSize = bitSize;
            Offset = offset;
            BitPosition = bitPosition;
            AlignWithPrevious = false;
            PaddingBefore = 0;
            BitPaddingAfter = 0;
            BitField = false;
            Volatile = false;
            Expanded = false;
			PotentialSaving = null;

			if (TypeName.EndsWith("]"))
			{
				try
				{
					string arraySizestring = TypeName.Substring(TypeName.IndexOf('[') + 1, TypeName.IndexOf(']') - TypeName.IndexOf('[') - 1);
					Count = UInt32.Parse(arraySizestring);
					if (Count == 0)
						Count = 1;
				}
				catch
				{
					Count = 1;
				}
			}
			else
			{
				Count = 1;
			}
		}

        public bool IsBase => Category == MemberCategory.Base;

        public bool IsExapandable => (Category == MemberCategory.Base || Category == MemberCategory.UDT);

        public static int CompareOffsets(SymbolMemberInfo a, SymbolMemberInfo b)
        {
            if (a.Offset != b.Offset)
            {
                return a.Offset < b.Offset ? -1 : 1;
            }
            if (a.IsBase != b.IsBase)
            {
                return a.IsBase ? -1 : 1;
            }
            if (a.BitPosition != b.BitPosition)
            {
                return a.BitPosition < b.BitPosition ? -1 : 1;
            }
            if (a.Size != b.Size)
            {
                return a.Size > b.Size ? -1 : 1;
            }
            return 0;
        }

		public bool UpdateTypeInfo(SymbolAnalyzer analyzer)
		{
			if (TypeInfo == null)
			{
				if (Category != MemberCategory.VTable)
				{
					TypeInfo = analyzer.FindSymbolInfo(TypeName);
					if (TypeInfo != null)
					{
						if (Size == 0)
						{
							Size = (uint)TypeInfo.Size;
							return true;
						}
						else if (Size != TypeInfo.Size)
						{
							TypeInfo = null;
							return false;
						}
					}
					else if (TypeName.Contains('['))
					{
						string typeName = TypeName.Substring(0, TypeName.IndexOf("["));
						TypeInfo = analyzer.FindSymbolInfo(TypeName);
					}
				}
				return false;
			}
			return true;
		}

    }
}