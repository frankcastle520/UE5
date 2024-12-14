using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Ports;
using System.Linq;
using System.Reflection;

namespace CruncherSharp
{
    public class SymbolInfo
    {
        public string Name { get; private set; }
        public string TypeName { get; set; }
        public ulong Size { get; set; }
        public ulong? NewSize { get; set; }
        public uint EndPadding { get; set; }
        public uint Padding => (uint)(EndPadding + Members.Sum(info => info.PaddingBefore)); // This is the local (intrinsic) padding
		public uint? MinAlignment { get; set; }
		public uint PaddingZonesCount => (uint)((EndPadding > 0 ? 1 : 0) + Members.Sum(info => info.PaddingBefore > 0 ? 1 : 0));
        public uint? TotalPadding { get; set; } // Includes padding from base classes and members
		public ulong? PotentialSaving { get; set; }
		public ulong NumInstances { get; set; }
        public ulong TotalCount { get; set; }
		public ulong LowerMemPool { get; set; }
		public ulong CurrentMemPool { get; set; }

		public ulong? NewMemPool { get; set; }
		public bool IsAbstract { get; set; }
        public bool IsTemplate { get; set; }
		public bool IsImportedFromCSV { get; set; }
		public List<SymbolMemberInfo> Members { get; set; }
        public List<SymbolFunctionInfo> Functions { get; set; }
        public List<SymbolInfo> DerivedClasses { get; set; }		       

        public SymbolInfo(string name, string typeName, ulong size, List<uint> MemPools)
        {
            Name = name;
            TypeName = typeName;
            Size = size;
			NewSize = null;
			EndPadding = 0;
			LowerMemPool = 0;
			CurrentMemPool = 0;
			NewMemPool = null;
			MinAlignment = null;
			TotalPadding = null;
			PotentialSaving = null;
            Members = new List<SymbolMemberInfo>();
            Functions = null;
            IsAbstract = false;
			IsImportedFromCSV = false;

			if (Name.Contains("<") && Name.Contains(">"))
                IsTemplate = true;

			SetMemPools(MemPools);
        }

        public void AddMember(SymbolMemberInfo member)
        {
            Members.Add(member);
        }

        public void AddFunction(SymbolFunctionInfo function)
        {
			if (Functions == null)
			{
				Functions = new List<SymbolFunctionInfo>();
			}
            Functions.Add(function);
			if (function.IsPure)
			{
				IsAbstract = true;
			}
        }

		public void SetMemPools(List<uint> MemPools)
		{
			uint previousMemPool = 0;
			foreach (var memPool in MemPools)
			{
				if (Size > memPool)
				{
					previousMemPool = memPool;
					continue;
				}
				CurrentMemPool = memPool;
				LowerMemPool = previousMemPool;
				break;
			}
			if (CurrentMemPool == 0)
			{
				CurrentMemPool = Size;
			}


			SetNewMemPools(MemPools); 
			
		}

		public void SetNewMemPools(List<uint> MemPools)
		{
			if (!NewSize.HasValue)
				return;
	
			foreach (var memPool in MemPools)
			{
				if (NewSize > memPool)
				{
					continue;
				}
				NewMemPool = memPool;
				return;
			}
			NewMemPool = NewSize;
		}


		private bool ComputeOffsetCollision(int index)
        {
            return index > 0 && Members[index].Offset == Members[index - 1].Offset;
        }

        public bool HasVtable
        {
            get
            {
                foreach (var member in Members)
                {
                    if (member.Category == SymbolMemberInfo.MemberCategory.VTable)
                    {
                        return true;
                    }
                }
                return false;
            }
        }

        public bool HasBaseClass
        {
            get
            {
                foreach (var member in Members)
                {
                    if (member.Category == SymbolMemberInfo.MemberCategory.Base)
                    {
                        return true;
                    }
                }
                return false;
            }
        }

		public bool HasUnusedVTable()
		{
			return HasVtable && !HasBaseClassWithVTable() && DerivedClasses == null;
		}

		public bool HasBaseClassWithVTable()
		{
			foreach (var member in Members)
			{
				if (member.Category == SymbolMemberInfo.MemberCategory.Base)
				{
					if (member.TypeInfo == null)
					{
						continue;
					}
					if (member.TypeInfo.HasVtable)
						return true;
				}
			}
			return false;
		}

		// https://randomascii.wordpress.com/2013/12/01/vc-2013-class-layout-change-and-wasted-space/
		public bool HasMSVCExtraPadding()
		{
            if (HasBaseClassWithVTable())
                return false;
            if (!HasVtable)
                return false;
            if (Members.Count < 2)
                return false;
            return Members[1].Size < 16 && Members[1].Offset == 16;
        }

        public bool HasMSVCEmptyBaseClass
        {
            get
            {
                var n = 1;
                while (n + 2 < Members.Count)
                {
                    if (Members[n - 1].IsBase && Members[n].IsBase && Members[n].Size == 1 && Members[n].Offset > Members[n - 1].Offset && Members[n + 1].Offset > Members[n].Offset)
                        return true;
                    n++;
                }
                return false;

            }
        }

		public void SortAndCalculate()
		{
			// Sort members by offset, recompute padding.
			// Sorting is usually not needed (for data fields), but sometimes base class order is wrong.
			Members.Sort(SymbolMemberInfo.CompareOffsets);
			for (int i = 0; i < Members.Count; ++i)
			{
				var member = Members[i];
				member.AlignWithPrevious = ComputeOffsetCollision(i);
				member.PaddingBefore = ComputePadding(i);
				member.BitPaddingAfter = ComputeBitPadding(i);
			}
			EndPadding = ComputeEndPadding();
		}

        private uint ComputePadding(int index)
        {
            if (index < 1 || index > Members.Count)
            {
                return 0;
            }
            if (index < Members.Count && Members[index].AlignWithPrevious)
            {
                return 0;
            }
            int previousIndex = index - 1;
            ulong biggestSize = Members[previousIndex].Size;
            while (Members[previousIndex].AlignWithPrevious && previousIndex > 0)
            {
                previousIndex--;
                if (biggestSize < Members[previousIndex].Size)
                    biggestSize = Members[previousIndex].Size;
            }

            ulong currentOffset = index > Members.Count - 1 ? Size : Members[index].Offset;
            ulong previousEnd = Members[previousIndex].Offset + biggestSize;
            return currentOffset > previousEnd ? (uint)(currentOffset - previousEnd) : 0u;
        }

        private uint ComputeBitPadding(int index)
        {
            if (index > Members.Count)
            {
                return 0;
            }
            if (!Members[index].BitField)
            {
                return 0;
            }
            if (index + 1 < Members.Count)
            {
                if (Members[index + 1].BitField && Members[index + 1].Offset == Members[index].Offset)
                    return 0;
            }
            return (uint)(8 * Members[index].Size) - (Members[index].BitPosition + Members[index].BitSize);
        }

        private uint ComputeEndPadding()
        {
            return ComputePadding(Members.Count);
        }

        public uint ComputeTotalPadding()
        {
            if (TotalPadding.HasValue)
            {
                return TotalPadding.Value;
            }
            TotalPadding = (uint)((uint)Padding + Members.Sum(info =>
            {
                if (info.AlignWithPrevious)
                    return 0;
                if (info.Category == SymbolMemberInfo.MemberCategory.Member)
                    return 0;
                if (info.TypeInfo == this)
                    return 0; // avoid infinite loops
                if (info.TypeInfo == null)
                {
                    return 0;
                }
                return info.TypeInfo.ComputeTotalPadding();
            }));
            return TotalPadding.Value;
        }

		public ulong ComputePotentialSaving(SymbolAnalyzer symbolAnalyzer)
		{
			if (PotentialSaving.HasValue)
			{
				return PotentialSaving.Value;
			}

			if (Name == "UObjectBase")
			{
				PotentialSaving = 0;
			}

			//List<uint> BitsRemaining = new List<uint>();
			uint BitsRemaining = 0;
			foreach (var member in Members)
			{
				if (member.Category != SymbolMemberInfo.MemberCategory.Member)
				{
					continue;
				}
				if (member.Size == 1 &&member.TypeName.StartsWith("bool"))
				{
					if (member.BitField)
					{
						BitsRemaining += member.BitPaddingAfter; 
					}
					else if (BitsRemaining == 0)
					{
						BitsRemaining = 7u;
					}
					else
					{
						BitsRemaining--;
						member.PotentialSaving = 1u;
					}
				}
				else if (member.Size == 4 && member.TypeName.StartsWith("enum "))
				{
					if (!symbolAnalyzer.ConfigEnum.ContainsKey(member.TypeName.Remove(0, 5)))
					{
						member.PotentialSaving = 1u;
					}
				}
			}

			PotentialSaving = 0;

			if (EndPadding < 16)
			{
				uint InternalPadding = (uint)Members.Sum(info => (long)info.PaddingBefore);

				InternalPadding += BitsRemaining / 8u;
				
				InternalPadding += (uint)(Members.Sum(info =>
				{
					if (info.AlignWithPrevious)
						return 0u;

					if (info.PotentialSaving.HasValue)
						return info.PotentialSaving;

					return 0u;
				}));
				uint MinAlignment = ComputeMinAlignment();
				if (InternalPadding > 0 && (InternalPadding + EndPadding) >= MinAlignment)
				{
					PotentialSaving = InternalPadding + EndPadding;
					PotentialSaving -= (PotentialSaving % MinAlignment);
				}
			}

			if (HasUnusedVTable())
				PotentialSaving += 8;

			PotentialSaving += (ulong)(Members.Sum(info =>
			{
				if (info.AlignWithPrevious)
					return 0u;
				if (info.Category == SymbolMemberInfo.MemberCategory.Member)
					return 0u;
				if (info.PotentialSaving.HasValue)
					return info.PotentialSaving;
				if (info.TypeName == Name)
					return 0u; // avoid infinite loops
				if (info.TypeInfo == null)
				{
					return 0u;
				}

				return (long)info.Count * (long)info.TypeInfo.ComputePotentialSaving(symbolAnalyzer);
			}));
			return PotentialSaving.Value;
		}

		public uint ComputeMinAlignment()
		{
			if (MinAlignment.HasValue)
			{
				return MinAlignment.Value;
			}
			
			if (EndPadding >= 8)
			{
				MinAlignment = 16;
				return MinAlignment.Value;
			}
			else if (EndPadding >= 4 || HasVtable)
			{
				MinAlignment = 8;
			}
			else if (EndPadding >= 2)
			{
				MinAlignment = 4;
			}
			else if (EndPadding > 0)
			{
				MinAlignment = 2;
			}
			else
			{
				MinAlignment = 1;
			}

			foreach (var member in Members)
			{
				if (member.MinAlignment > MinAlignment)
				{
					MinAlignment = member.MinAlignment;
				}
			}
			return MinAlignment.Value;

		}

		public ulong ComputeTotalMempoolUsage()
		{
			ulong TotalUsage = CurrentMemPool * NumInstances;
			if (DerivedClasses != null)
			{
				foreach (var derivedClass in DerivedClasses)
				{
					TotalUsage += derivedClass.ComputeTotalMempoolUsage();
				}
			}
			return TotalUsage;
		}

		public ulong ComputePotentialTotalSaving()
		{
			if (PotentialSaving.HasValue)
				return ComputePotentialTotalSaving(PotentialSaving.Value);
			return 0;
		}

		private ulong ComputePotentialTotalSaving(ulong Win)
		{
			ulong TotalWin = 0;
			if (LowerMemPool > 0 && PotentialSaving.HasValue && (Size - Win) <= LowerMemPool)
				TotalWin = (CurrentMemPool - LowerMemPool) * NumInstances;

			if (DerivedClasses != null)
			{
				foreach (var derivedClass in DerivedClasses)
				{
					TotalWin += derivedClass.ComputePotentialTotalSaving(Win);
				}
			}

			return TotalWin;
		}


		public ulong ComputeTotalMempoolWin()
		{
			return ComputeTotalMempoolWin(Size - LowerMemPool);
		}

		private ulong ComputeTotalMempoolWin(ulong Win)
		{
			ulong TotalWin = 0;
			if (LowerMemPool > 0 && (Size - Win) <= LowerMemPool)
				TotalWin = (CurrentMemPool - LowerMemPool) * NumInstances;

			if (DerivedClasses != null)
			{
				foreach (var derivedClass in DerivedClasses)
				{
					TotalWin += derivedClass.ComputeTotalMempoolWin(Win);
				}
			}

			return TotalWin;
		}

		public long ComputeTotalDelta()
		{
			long TotalUsage = ((long)NewSize - (long)Size) * (long)NumInstances;
			if (DerivedClasses != null)
			{
				foreach (var derivedClass in DerivedClasses)
				{
					TotalUsage += derivedClass.ComputeTotalDelta();
				}
			}
			return TotalUsage;
		}

		public long ComputeTotalMempoolDelta()
		{
			if (!NewMemPool.HasValue)
				return 0;
			long TotalUsage = ((long)NewMemPool - (long)CurrentMemPool) * (long)NumInstances;
			if (DerivedClasses != null)
			{
				foreach (var derivedClass in DerivedClasses)
				{
					TotalUsage += derivedClass.ComputeTotalMempoolDelta();
				}
			}
			return TotalUsage;
		}

		private bool _BaseClassUpdated = false;
		public void UpdateBaseClass(SymbolAnalyzer symbolAnalyzer)
        {
			if (_BaseClassUpdated)
				return;
			_BaseClassUpdated = true;
			bool _ChildClassUpdated = false;

			foreach (var member in Members)
            {
                if (member.Category == SymbolMemberInfo.MemberCategory.Base)
                {
					member.TypeInfo = symbolAnalyzer.FindSymbolInfo(member.TypeName);
                    if (member.TypeInfo != null)
                    {
                        if (member.TypeInfo.DerivedClasses == null)
							member.TypeInfo.DerivedClasses = new List<SymbolInfo>();
						member.TypeInfo.DerivedClasses.Add(this);
                    }
                }
				else
				{
					if (member.UpdateTypeInfo(symbolAnalyzer))
					{
						_ChildClassUpdated = true;
					}
				}
            }

			if (_ChildClassUpdated)
			{
				SortAndCalculate();
			}
        }

		public bool IsA(string name, SymbolAnalyzer symbolAnalyzer)
		{
			var SymbolInfo = symbolAnalyzer.FindSymbolInfo(name);
			if (SymbolInfo == null)
			{
				return false;
			}
			foreach (var member in Members)
			{
				if (member.Category == SymbolMemberInfo.MemberCategory.Base)
				{
					if (member.TypeInfo != null)
					{
						if (member.TypeInfo == SymbolInfo)
						{
							return true;
						}
						if (member.TypeInfo.IsA(name, symbolAnalyzer))
						{
							return true;
						}
					}
				}
			}
			return false;
		}

        public void CheckOverride()
        {
            foreach (var function in Functions)
            {
                if (function.Virtual && 
					function.IsOverloaded == false && 
					function.Category == SymbolFunctionInfo.FunctionCategory.Function && 
					DerivedClasses != null)
                {
                    foreach(var derivedClass in DerivedClasses)
                    {
                        if (derivedClass.IsOverloadingFunction(function))
                        {
                            function.IsOverloaded = true;
                            break;
                        }
                    }
                }
            }
        }

        public void CheckMasking()
        {
            foreach (var function in Functions)
            {
                if (function.Virtual == false && function.Category == SymbolFunctionInfo.FunctionCategory.Function && DerivedClasses != null)
                {
                    foreach (var derivedClass in DerivedClasses)
                    {
                        derivedClass.CheckMasking(function);
                    }
                }
            }
        }

        private void CheckMasking(SymbolFunctionInfo func)
        {
            foreach (var function in Functions)
            {
                if (function.Virtual == false && function.Name == func.Name)
                {
                    function.IsMasking = true;

                    if (DerivedClasses != null)
                    {
                        foreach (var derivedClass in DerivedClasses)
                        {
                            derivedClass.CheckMasking(func);
                        }
                    }
                }
            }
        }

        private bool IsOverloadingFunction(SymbolFunctionInfo func)
        {
            foreach (var function in Functions)
            {
                if (function.Name == func.Name)
                    return true;
            }
            if (DerivedClasses != null)
            {
                foreach (var derivedClass in DerivedClasses)
                {
                    if (derivedClass.IsOverloadingFunction(func))
                        return true;
                }
            }
            return false;
        }

        public void UpdateTotalCount(ulong count)
        {
            foreach (var member in Members)
            {
                if (member.Category == SymbolMemberInfo.MemberCategory.UDT || member.Category == SymbolMemberInfo.MemberCategory.Base)
                {
                    if (member.TypeInfo != null)
                    {
						count *= member.Count;
						member.TypeInfo.TotalCount += count;
						member.TypeInfo.UpdateTotalCount(count);
					}
                }
            }
        }

        public override string ToString()
        {
            var sw = new StringWriter();

            sw.WriteLine($"Symbol: {Name}");
            sw.WriteLine($"TypeName: {TypeName}");
            sw.WriteLine($"Size: {Size}");
            sw.WriteLine($"Padding: {Padding}");
            sw.WriteLine($"Total padding: {TotalPadding}");
            sw.WriteLine("Members:");
            sw.WriteLine("-------");

			const string PaddingMarker = "****Padding";

            foreach (var member in Members)
            {
                if (member.PaddingBefore > 0)
                {
                    var paddingOffset = member.Offset - member.PaddingBefore;
                    sw.WriteLine($"{PaddingMarker,-40} {paddingOffset,5} {member.PaddingBefore,5}");
                }
                sw.WriteLine($"{member.DisplayName,-40} {member.Offset,5} {member.Size,5}");
            }

            if (EndPadding > 0)
            {
                var endPaddingOffset = Size - EndPadding;
                sw.WriteLine($"{PaddingMarker,-40} {endPaddingOffset,5} {EndPadding,5}");
            }

            return sw.ToString();
        }
    }
}
