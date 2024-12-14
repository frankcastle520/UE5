// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Agents.Leases;

namespace HordeCommon.Rpc.Messages
{
	/// <summary>
	/// Additional methods for RpcLease
	/// </summary>
	partial class RpcLease
	{
		/// <summary>
		/// Typed acesssor for the <see cref="RpcLease.IdString"/> field.
		/// </summary>
		public LeaseId Id
		{
			get => LeaseId.Parse(IdString);
			set => IdString = value.ToString();
		}
	}
}
