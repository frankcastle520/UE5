// Copyright Epic Games, Inc. All Rights Reserved.

using System.Security.Claims;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Http.Features;

namespace HordeServer.Tests
{
	using ISession = Microsoft.AspNetCore.Http.ISession;

	sealed class HttpContextStub : HttpContext
	{
		public override ConnectionInfo Connection { get; } = null!;
		public override IFeatureCollection Features { get; } = null!;
		public override IDictionary<object, object?> Items { get; set; } = null!;
		public override HttpRequest Request { get; } = null!;
		public override CancellationToken RequestAborted { get; set; }
		public override IServiceProvider RequestServices { get; set; } = null!;
		public override HttpResponse Response { get; } = null!;
		public override ISession Session { get; set; } = null!;
		public override string TraceIdentifier { get; set; } = null!;
		public override ClaimsPrincipal User { get; set; }
		public override WebSocketManager WebSockets { get; } = null!;

		public HttpContextStub(Claim roleClaimType)
		{
			User = new ClaimsPrincipal(new ClaimsIdentity(new List<Claim>
			{
				roleClaimType
			}, "TestAuthType"));
		}

		public HttpContextStub(ClaimsPrincipal user)
		{
			User = user;
		}

		public override void Abort()
		{
			throw new NotImplementedException();
		}
	}
}
