// Copyright Epic Games, Inc. All Rights Reserved.

namespace HordeServer.Utilities
{
	/// <summary>
	/// Case insensitive set of strings
	/// </summary>
	public class CaseInsensitiveDictionary<TValue> : Dictionary<string, TValue>
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public CaseInsensitiveDictionary()
			: base(StringComparer.OrdinalIgnoreCase)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public CaseInsensitiveDictionary(IDictionary<string, TValue> items)
			: base(items, StringComparer.OrdinalIgnoreCase)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public CaseInsensitiveDictionary(IEnumerable<KeyValuePair<string, TValue>> items)
			: base(items, StringComparer.OrdinalIgnoreCase)
		{
		}
	}
}
