// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.AspNetCore.Http;

namespace HordeServer.Plugins
{
	/// <summary>
	/// Interface allowing plugins to modify API responses
	/// </summary>
	public interface IPluginResponseFilter
	{
		/// <summary>
		/// Apply any changes to the given response
		/// </summary>
		public void Apply(HttpContext context, object response);
	}
}
