// Copyright Epic Games, Inc. All Rights Reserved.

using System.Reflection;
using System.Security.Claims;
using System.Threading.Channels;
using EpicGames.Horde.Agents;
using EpicGames.Horde.Agents.Pools;
using Grpc.Core;
using HordeCommon.Rpc;
using HordeCommon.Rpc.Messages;
using HordeServer.Agents;
using HordeServer.Server;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Http.Features;

namespace HordeServer.Tests.Server
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

	public class ServerCallContextStub : ServerCallContext
	{
		// Copied from ServerCallContextExtensions.cs in Grpc.Core
		const string HttpContextKey = "__HttpContext";

		protected override string MethodCore { get; } = null!;
		protected override string HostCore { get; } = null!;
		protected override string PeerCore { get; } = null!;
		protected override DateTime DeadlineCore { get; } = DateTime.Now.AddHours(24);
		protected override Metadata RequestHeadersCore { get; } = null!;
		protected override CancellationToken CancellationTokenCore => _cancellationToken;
		protected override Metadata ResponseTrailersCore { get; } = null!;
		protected override Status StatusCore { get; set; }
		protected override WriteOptions? WriteOptionsCore { get; set; } = null!;
		protected override AuthContext AuthContextCore { get; } = null!;

		private CancellationToken _cancellationToken;

		public static ServerCallContext ForAdminWithAgentSessionId(string agentSessionId)
		{
			return new ServerCallContextStub(new ClaimsPrincipal(new ClaimsIdentity(new List<Claim>
			{
				HordeClaims.AdminClaim.ToClaim(),
				new Claim(HordeClaimTypes.AgentSessionId, agentSessionId),
			}, "TestAuthType")));
		}

		public static ServerCallContext ForAdmin()
		{
			return new ServerCallContextStub(new ClaimsPrincipal(new ClaimsIdentity(new List<Claim>
			{
				HordeClaims.AdminClaim.ToClaim()
			}, "TestAuthType")));
		}

		public ServerCallContextStub(Claim roleClaimType)
		{
			// The GetHttpContext extension falls back to getting the HttpContext from UserState
			// We can piggyback on that behavior during tests
			UserState[HttpContextKey] = new HttpContextStub(roleClaimType);
		}

		public ServerCallContextStub(ClaimsPrincipal user)
		{
			// The GetHttpContext extension falls back to getting the HttpContext from UserState
			// We can piggyback on that behavior during tests
			UserState[HttpContextKey] = new HttpContextStub(user);
		}

		public void SetCancellationToken(CancellationToken cancellationToken)
		{
			_cancellationToken = cancellationToken;
		}

		protected override Task WriteResponseHeadersAsyncCore(Metadata responseHeaders)
		{
			throw new NotImplementedException();
		}

		protected override ContextPropagationToken CreatePropagationTokenCore(ContextPropagationOptions? options)
		{
			throw new NotImplementedException();
		}
	}

	[TestClass]
	public class RpcServiceTest : BuildTestSetup
	{
		private readonly ServerCallContext _adminContext = new ServerCallContextStub(HordeClaims.AdminClaim.ToClaim());

		class RpcServiceInvoker : CallInvoker
		{
			private readonly RpcService _rpcService;
			private readonly ServerCallContext _serverCallContext;

			public RpcServiceInvoker(RpcService rpcService, ServerCallContext serverCallContext)
			{
				_rpcService = rpcService;
				_serverCallContext = serverCallContext;
			}

			public override TResponse BlockingUnaryCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string? host, CallOptions options, TRequest request)
			{
				throw new NotImplementedException("Blocking calls are not supported! Method " + method.FullName);
			}

			public override AsyncUnaryCall<TResponse> AsyncUnaryCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string? host, CallOptions options, TRequest request)
			{
				MethodInfo methodInfo = GetMethod(method.Name);
				Task<TResponse> res = (methodInfo.Invoke(_rpcService, new object[] { request, _serverCallContext }) as Task<TResponse>)!;
				return new AsyncUnaryCall<TResponse>(res, null!, null!, null!, null!, null!);
			}

			public override AsyncServerStreamingCall<TResponse> AsyncServerStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string? host, CallOptions options,
				TRequest request)
			{
				Console.WriteLine($"RpcServiceInvoker.AsyncServerStreamingCall(method={method.FullName} request={request})");
				throw new NotImplementedException();
			}

			public override AsyncClientStreamingCall<TRequest, TResponse> AsyncClientStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string? host, CallOptions options)
			{
				Console.WriteLine($"RpcServiceInvoker.AsyncClientStreamingCall(method={method.FullName})");
				throw new NotImplementedException();
			}

			public override AsyncDuplexStreamingCall<TRequest, TResponse> AsyncDuplexStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string? host, CallOptions options)
			{
				Console.WriteLine($"RpcServiceInvoker.AsyncDuplexStreamingCall(method={method.FullName})");

				GrpcDuplexStreamHandler<TRequest> requestStream = new GrpcDuplexStreamHandler<TRequest>(_serverCallContext);
				GrpcDuplexStreamHandler<TResponse> responseStream = new GrpcDuplexStreamHandler<TResponse>(_serverCallContext);

				MethodInfo methodInfo = GetMethod(method.Name);
				Task methodTask = (methodInfo.Invoke(_rpcService, new object[] { requestStream, responseStream, _serverCallContext }) as Task)!;
				methodTask.ContinueWith(t => { Console.Error.WriteLine($"Uncaught exception in {method.Name}: {t}"); }, CancellationToken.None, TaskContinuationOptions.OnlyOnFaulted, TaskScheduler.Default);

				return new AsyncDuplexStreamingCall<TRequest, TResponse>(requestStream, responseStream, null!, null!, null!, null!);
			}

			private MethodInfo GetMethod(string methodName)
			{
				MethodInfo? method = _rpcService.GetType().GetMethod(methodName);
				if (method == null)
				{
					throw new ArgumentException($"Method {methodName} not found in RpcService");
				}

				return method;
			}
		}

		/// <summary>
		/// Combines and cross-writes streams for a duplex streaming call in gRPC
		/// </summary>
		/// <typeparam name="T"></typeparam>
		class GrpcDuplexStreamHandler<T> : IServerStreamWriter<T>, IClientStreamWriter<T>, IAsyncStreamReader<T> where T : class
		{
			private readonly ServerCallContext _serverCallContext;
			private readonly Channel<T> _channel;

			public WriteOptions? WriteOptions { get; set; }

			public GrpcDuplexStreamHandler(ServerCallContext serverCallContext)
			{
				_channel = System.Threading.Channels.Channel.CreateUnbounded<T>();

				_serverCallContext = serverCallContext;
			}

			public void Complete()
			{
				_channel.Writer.Complete();
			}

			public IAsyncEnumerable<T> ReadAllAsync()
			{
				return _channel.Reader.ReadAllAsync();
			}

			public async Task<T?> ReadNextAsync()
			{
				if (await _channel.Reader.WaitToReadAsync())
				{
					_channel.Reader.TryRead(out T? message);
					return message;
				}
				else
				{
					return null;
				}
			}

			public Task WriteAsync(T message)
			{
				if (_serverCallContext.CancellationToken.IsCancellationRequested)
				{
					return Task.FromCanceled(_serverCallContext.CancellationToken);
				}

				if (!_channel.Writer.TryWrite(message))
				{
					throw new InvalidOperationException("Unable to write message.");
				}

				return Task.CompletedTask;
			}

			public Task CompleteAsync()
			{
				Complete();
				return Task.CompletedTask;
			}

			public async Task<bool> MoveNext(CancellationToken cancellationToken)
			{
				_serverCallContext.CancellationToken.ThrowIfCancellationRequested();

				if (await _channel.Reader.WaitToReadAsync(cancellationToken))
				{
					if (_channel.Reader.TryRead(out T? message))
					{
						Current = message;
						return true;
					}
				}

				Current = null!;
				return false;
			}

			public T Current { get; private set; } = null!;
		}

		public RpcServiceTest()
		{
			UpdateConfig(x => x.Plugins.GetComputeConfig().Pools.Clear());
		}

		[TestMethod]
		public async Task CreateSessionTestAsync()
		{
			RpcCreateSessionRequest req = new RpcCreateSessionRequest();
			await Assert.ThrowsExceptionAsync<StructuredRpcException>(() => RpcService.CreateSession(req, _adminContext));

			req.Id = new AgentId("MyName").ToString();
			await Assert.ThrowsExceptionAsync<StructuredRpcException>(() => RpcService.CreateSession(req, _adminContext));

			req.Capabilities = new RpcAgentCapabilities();
			RpcCreateSessionResponse res = await RpcService.CreateSession(req, _adminContext);

			Assert.AreEqual("MYNAME", res.AgentId);
			// TODO: Check Token, ExpiryTime, SessionId 
		}

		[TestMethod]
		public async Task AgentJoinsPoolThroughPropertiesAsync()
		{
			RpcCreateSessionRequest req = new() { Id = new AgentId("bogusAgentName").ToString(), Capabilities = new RpcAgentCapabilities() };
			req.Capabilities.Properties.Add($"{KnownPropertyNames.RequestedPools}=fooPool,barPool");
			RpcCreateSessionResponse res = await RpcService.CreateSession(req, _adminContext);

			IAgent agent = (await AgentService.GetAgentAsync(new AgentId(res.AgentId)))!;
			CollectionAssert.AreEquivalent(new List<PoolId> { new("fooPool"), new("barPool") }, agent.Pools.ToList());

			// Connect a second time, when the agent has already been created
			req = new() { Id = new AgentId("bogusAgentName").ToString(), Capabilities = new RpcAgentCapabilities() };
			req.Capabilities.Properties.Add($"{KnownPropertyNames.RequestedPools}=bazPool");
			res = await RpcService.CreateSession(req, _adminContext);

			agent = (await AgentService.GetAgentAsync(new AgentId(res.AgentId)))!;
			CollectionAssert.AreEquivalent(new List<PoolId> { new("bazPool") }, agent.Pools.ToList());
		}

		[TestMethod]
		public async Task PropertiesFromAgentCapabilitiesAsync()
		{
			RpcCreateSessionRequest req = new() { Id = new AgentId("bogusAgentName").ToString(), Capabilities = new RpcAgentCapabilities() };
			req.Capabilities.Properties.Add("fooKey=barValue");
			RpcCreateSessionResponse res = await RpcService.CreateSession(req, _adminContext);
			IAgent agent = (await AgentService.GetAgentAsync(new AgentId(res.AgentId)))!;
			Assert.IsTrue(agent.Properties.Contains("fooKey=barValue"));
		}

#pragma warning disable CA1041
#pragma warning disable CS0612
		[TestMethod]
		public async Task PropertiesFromDeviceCapabilitiesAsync()
		{
			RpcCreateSessionRequest req = new() { Id = new AgentId("bogusAgentName").ToString(), Capabilities = new RpcAgentCapabilities() };
			req.Capabilities.Devices.Add(new RpcDeviceCapabilities { Handle = "someHandle", Properties = { "foo=bar" } });
			RpcCreateSessionResponse res = await RpcService.CreateSession(req, _adminContext);
			IAgent agent = (await AgentService.GetAgentAsync(new AgentId(res.AgentId)))!;
			Assert.IsTrue(agent.Properties.Contains("foo=bar"));
		}

		[TestMethod]
		public async Task KnownPropertiesAreSetAsResourcesAsync()
		{
			RpcCreateSessionRequest req = new() { Id = new AgentId("bogusAgentName").ToString(), Capabilities = new RpcAgentCapabilities() };
			req.Capabilities.Devices.Add(new RpcDeviceCapabilities { Handle = "someHandle", Properties = { $"{KnownResourceNames.LogicalCores}=10" } });
			RpcCreateSessionResponse res = await RpcService.CreateSession(req, _adminContext);
			IAgent agent = (await AgentService.GetAgentAsync(new AgentId(res.AgentId)))!;
			Assert.AreEqual(10, agent.Resources[KnownResourceNames.LogicalCores]);
		}
#pragma warning restore CS0612
#pragma warning restore CA1041

		[TestMethod]
		public async Task UpdateSessionTestAsync()
		{
			RpcCreateSessionRequest createReq = new RpcCreateSessionRequest
			{
				Id = new AgentId("UpdateSessionTest1").ToString(),
				Capabilities = new RpcAgentCapabilities()
			};
			RpcCreateSessionResponse createRes = await RpcService.CreateSession(createReq, _adminContext);
			string agentId = createRes.AgentId;
			string sessionId = createRes.SessionId;

			TestAsyncStreamReader<RpcUpdateSessionRequest> requestStream =
				new TestAsyncStreamReader<RpcUpdateSessionRequest>(_adminContext);
			TestServerStreamWriter<RpcUpdateSessionResponse> responseStream =
				new TestServerStreamWriter<RpcUpdateSessionResponse>(_adminContext);
			Task call = RpcService.UpdateSession(requestStream, responseStream, _adminContext);

			requestStream.AddMessage(new RpcUpdateSessionRequest { AgentId = "does-not-exist", SessionId = sessionId });
			StructuredRpcException re = await Assert.ThrowsExceptionAsync<StructuredRpcException>(() => call);
			Assert.AreEqual(StatusCode.NotFound, re.StatusCode);
			Assert.IsTrue(re.Message.Contains("Invalid agent name", StringComparison.OrdinalIgnoreCase));
		}

		[TestMethod]
		public async Task FinishBatchTestAsync()
		{
			RpcCreateSessionRequest createReq = new RpcCreateSessionRequest
			{
				Id = new AgentId("UpdateSessionTest1").ToString(),
				Capabilities = new RpcAgentCapabilities()
			};
			RpcCreateSessionResponse createRes = await RpcService.CreateSession(createReq, _adminContext);
			string agentId = createRes.AgentId;
			string sessionId = createRes.SessionId;

			TestAsyncStreamReader<RpcUpdateSessionRequest> requestStream =
				new TestAsyncStreamReader<RpcUpdateSessionRequest>(_adminContext);
			TestServerStreamWriter<RpcUpdateSessionResponse> responseStream =
				new TestServerStreamWriter<RpcUpdateSessionResponse>(_adminContext);
			Task call = RpcService.UpdateSession(requestStream, responseStream, _adminContext);

			requestStream.AddMessage(new RpcUpdateSessionRequest { AgentId = "does-not-exist", SessionId = sessionId });
			StructuredRpcException re = await Assert.ThrowsExceptionAsync<StructuredRpcException>(() => call);
			Assert.AreEqual(StatusCode.NotFound, re.StatusCode);
			Assert.IsTrue(re.Message.Contains("Invalid agent name", StringComparison.OrdinalIgnoreCase));
		}

		/*
		[TestMethod]
		public async Task UploadSoftwareAsync()
		{
			TestSetup TestSetup = await GetTestSetup();
			
			MemoryStream OutputStream = new MemoryStream();
			using (ZipArchive ZipFile = new ZipArchive(OutputStream, ZipArchiveMode.Create, false))
			{
				string TempFilename = Path.GetTempFileName();
				File.WriteAllText(TempFilename, "{\"Horde\": {\"Version\": \"myVersion\"}}");
				ZipFile.CreateEntryFromFile(TempFilename, "appsettings.json");
			}

			ByteString Data = ByteString.CopyFrom(OutputStream.ToArray());
			UploadSoftwareRequest Req = new UploadSoftwareRequest { Channel = "boguschannel", Data = Data };

			UploadSoftwareResponse Res1 = await TestSetup.RpcService.UploadSoftware(Req, AdminContext);
			Assert.AreEqual("r1", Res1.Version);
			
			UploadSoftwareResponse Res2 = await TestSetup.RpcService.UploadSoftware(Req, AdminContext);
			Assert.AreEqual("r2", Res2.Version);
		}
		*/
	}
}