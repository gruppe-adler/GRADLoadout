//------------------------------------------------------------------------------------------------
//! Server-authoritative loadout RPC host, added to EVERY player controller via
//! `modded class SCR_PlayerController` (see docs/DECISIONS.md D6 — revised).
//!
//! Why modded class, not a component on a controller prefab: worlds use different controller
//! prefabs (the game default is DefaultPlayerController.et; campaign/MP variants differ). A
//! prefab override only reaches the one prefab it overrides, so the RPC host would be missing in
//! worlds using a different controller. Modding the class puts the host on every controller in
//! every world automatically. (AI units have no player controller, but that's fine: the GM's own
//! controller hosts the RPC and the target AI character is acted on server-side.)
//!
//! Two flows, both gated server-side by GRAD_LoadoutPermissions:
//!   APPLY:   client -> RpcAsk_Apply(targetRplId, json) -> server validates, deserializes, applies.
//!   REQUEST: client -> RpcAsk_Request(targetRplId, requestId) -> server validates, captures,
//!            -> RpcDo_Response(requestId, json) back to the requesting owner.
//!
//! The request side uses a request-id + timeout + callback so the UI can show "loading…" and
//! recover from a dropped request. Stale requests are swept on a timer.
//!
//! RPC payload size (D4): a serialized loadout can be large. We log the length and warn past
//! GRAD_RPC_SOFT_LIMIT; chunked transfer is the planned mitigation if real loadouts breach the
//! engine cap (to be measured in MP). The send path is factored so chunking can be added later.
modded class SCR_PlayerController
{
	//! Soft warning threshold for serialized payload length (characters).
	protected static const int GRAD_RPC_SOFT_LIMIT = 8000;

	//! Seconds before an outstanding loadout request is considered dead.
	protected static const float GRAD_REQUEST_TIMEOUT_S = 6.0;

	//! DEBUG ONLY (P5 manual UI test): auto-open the arsenal shortly after the local controller
	//! inits, so the menu can be seen without a console/keybind/box. REMOVE before release.
	protected static const bool GRAD_DEBUG_AUTO_OPEN = false;

	//! Monotonic request-id source for this controller's outgoing requests.
	protected int m_iGradNextRequestId = 1;

	//! Outstanding client-side requests awaiting a server response, keyed by request id.
	protected ref map<int, ref GRAD_PendingLoadoutRequest> m_mGradPendingRequests;

	//------------------------------------------------------------------------------------------------
	//! Lazily create the pending-requests map (called before any use).
	protected void GradEnsureInit()
	{
		if (!m_mGradPendingRequests)
			m_mGradPendingRequests = new map<int, ref GRAD_PendingLoadoutRequest>();
	}

	//------------------------------------------------------------------------------------------------
	//! Constructor runs on every controller instantiation (guaranteed). For the DEBUG auto-open we
	//! start a repeating poll here that opens the arsenal once the LOCAL controller is established.
	void SCR_PlayerController(IEntitySource src, IEntity parent)
	{
		if (GRAD_DEBUG_AUTO_OPEN)
			GetGame().GetCallqueue().CallLater(GradDebugAutoOpenTick, 3000, true);

		// Warm the arsenal catalog ahead of first use so the item browser is instant when the GM
		// opens it. Spawning the service starts the amortized index build (492 records over frames);
		// doing it now (on world entry) means it is finished long before the editor/arsenal is opened.
		GetGame().GetCallqueue().CallLater(GradPreloadArsenalTick, 2000, true);
	}

	//------------------------------------------------------------------------------------------------
	//! One-shot: once this is the LOCAL controller, spawn the arsenal service (which kicks off the
	//! catalog build) and stop ticking. Runs early so the catalog is ready before the GM opens the
	//! editor/arsenal. No-op on dedicated servers / non-local controllers.
	protected void GradPreloadArsenalTick()
	{
		if (GradGetLocal() != this)
			return; // not yet (or not) the local controller — keep waiting

		GetGame().GetCallqueue().Remove(GradPreloadArsenalTick);

		if (!GRAD_ArsenalService.GetInstance())
		{
			GRAD_MenuTest.SpawnService();
			GRAD_Log.Info("Arsenal preload: service spawned, catalog build started");
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The local player's own controller (client side), as a GRAD loadout host.
	static SCR_PlayerController GradGetLocal()
	{
		return SCR_PlayerController.Cast(GetGame().GetPlayerController());
	}

	//------------------------------------------------------------------------------------------------
	//! The controller of a given player.
	static SCR_PlayerController GradGetForPlayer(int playerId)
	{
		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return null;

		return SCR_PlayerController.Cast(pm.GetPlayerController(playerId));
	}

	//------------------------------------------------------------------------------------------------
	// ---- APPLY ----------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------

	//! Client entry point: ask the server to apply `data` to the character identified by targetRplId.
	void GradApplyLoadout(RplId targetRplId, GRAD_LoadoutData data)
	{
		if (!data)
		{
			GRAD_Log.Error("ApplyLoadout: null data");
			return;
		}

		string json = data.ToJsonString();
		if (GRAD_CommonUtils.IsBlank(json))
		{
			GRAD_Log.Error("ApplyLoadout: serialization failed");
			return;
		}

		GradWarnIfOversized("ApplyLoadout", json);
		Rpc(GradRpcAsk_Apply, targetRplId, json);
	}

	//------------------------------------------------------------------------------------------------
	//! SERVER: validate the requester's permission, then deserialize and apply.
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void GradRpcAsk_Apply(RplId targetRplId, string json)
	{
		int requesterId = GradGetOwnerPlayerId();
		IEntity target = GradResolveEntity(targetRplId);
		if (!target)
		{
			GRAD_Log.Warn(string.Format("RpcAsk_Apply: target RplId %1 did not resolve", targetRplId));
			return;
		}

		if (!GRAD_LoadoutPermissions.CanOperate(requesterId, target))
		{
			GRAD_Log.Warn(string.Format("RpcAsk_Apply: player %1 DENIED apply on %2",
				requesterId, GRAD_InventoryLib.GetEntityShortName(target)));
			return;
		}

		GRAD_LoadoutData data = GRAD_LoadoutData.FromJsonString(json);
		if (!data)
		{
			GRAD_Log.Error("RpcAsk_Apply: could not deserialize loadout");
			return;
		}

		array<IEntity> created = {};
		bool ok = GRAD_LoadoutApply.Apply(target, data, false, true, created);
		GRAD_Log.Info(string.Format("RpcAsk_Apply: player %1 applied to %2 (ok=%3, %4 items)",
			requesterId, GRAD_InventoryLib.GetEntityShortName(target), ok, created.Count()));
	}

	//------------------------------------------------------------------------------------------------
	// ---- REQUEST --------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------

	//! Client entry point: ask the server for the current loadout of the character at targetRplId.
	int GradRequestLoadout(RplId targetRplId, GRAD_LoadoutRequestCallback callback)
	{
		GradEnsureInit();
		int requestId = m_iGradNextRequestId++;

		GRAD_PendingLoadoutRequest pending = new GRAD_PendingLoadoutRequest();
		pending.m_iRequestId = requestId;
		pending.m_Callback = callback;
		pending.m_fStartTime = GradWorldTimeS();
		m_mGradPendingRequests.Set(requestId, pending);

		Rpc(GradRpcAsk_Request, targetRplId, requestId);

		GradScheduleTimeoutSweep();
		return requestId;
	}

	//------------------------------------------------------------------------------------------------
	//! SERVER: validate, capture the target's current loadout, send it back to the owner.
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void GradRpcAsk_Request(RplId targetRplId, int requestId)
	{
		int requesterId = GradGetOwnerPlayerId();
		IEntity target = GradResolveEntity(targetRplId);
		if (!target || !GRAD_LoadoutPermissions.CanOperate(requesterId, target))
		{
			GRAD_Log.Warn(string.Format("RpcAsk_Request: player %1 DENIED/!target for request %2", requesterId, requestId));
			Rpc(GradRpcDo_Response, requestId, string.Empty);
			return;
		}

		GRAD_LoadoutData data = GRAD_LoadoutCapture.Capture(target, "Requested", true);
		string json = string.Empty;
		if (data)
			json = data.ToJsonString();

		GradWarnIfOversized("RpcAsk_Request", json);
		Rpc(GradRpcDo_Response, requestId, json);
	}

	//------------------------------------------------------------------------------------------------
	//! OWNER (client): receive a requested loadout and fulfil the matching pending request.
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	void GradRpcDo_Response(int requestId, string json)
	{
		GradEnsureInit();
		GRAD_PendingLoadoutRequest pending;
		if (!m_mGradPendingRequests.Find(requestId, pending) || !pending)
		{
			GRAD_Log.Debug(string.Format("RpcDo_Response: no pending request %1 (stale/late)", requestId));
			return;
		}

		m_mGradPendingRequests.Remove(requestId);

		if (GRAD_CommonUtils.IsBlank(json))
		{
			GradInvokeCallback(pending.m_Callback, false, null);
			return;
		}

		GRAD_LoadoutData data = GRAD_LoadoutData.FromJsonString(json);
		GradInvokeCallback(pending.m_Callback, data != null, data);
	}

	//------------------------------------------------------------------------------------------------
	// ---- timeout handling -----------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------

	protected void GradScheduleTimeoutSweep()
	{
		GetGame().GetCallqueue().Remove(GradSweepTimeouts);
		GetGame().GetCallqueue().CallLater(GradSweepTimeouts, 1000, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void GradSweepTimeouts()
	{
		if (!m_mGradPendingRequests || m_mGradPendingRequests.IsEmpty())
			return;

		float now = GradWorldTimeS();
		array<int> expired = {};

		foreach (int id, GRAD_PendingLoadoutRequest pending : m_mGradPendingRequests)
		{
			if (pending && (now - pending.m_fStartTime) >= GRAD_REQUEST_TIMEOUT_S)
				expired.Insert(id);
		}

		foreach (int id : expired)
		{
			GRAD_PendingLoadoutRequest pending;
			if (m_mGradPendingRequests.Find(id, pending))
			{
				m_mGradPendingRequests.Remove(id);
				GRAD_Log.Warn(string.Format("Loadout request %1 timed out", id));
				GradInvokeCallback(pending.m_Callback, false, null);
			}
		}

		if (!m_mGradPendingRequests.IsEmpty())
			GradScheduleTimeoutSweep();
	}

	//------------------------------------------------------------------------------------------------
	// ---- helpers --------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------

	//! Player id owning this controller (server-side identity of the requester).
	protected int GradGetOwnerPlayerId()
	{
		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return 0;

		IEntity controlled = GetControlledEntity();
		if (controlled)
		{
			int id = pm.GetPlayerIdFromControlledEntity(controlled);
			if (id > 0)
				return id;
		}

		// Fallback: match this controller against the player list.
		array<int> players = {};
		pm.GetPlayers(players);
		foreach (int pid : players)
		{
			if (pm.GetPlayerController(pid) == this)
				return pid;
		}

		return 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Resolve a replicated entity from the RplId of its RplComponent.
	protected IEntity GradResolveEntity(RplId rplId)
	{
		RplComponent rpl = RplComponent.Cast(Replication.FindItem(rplId));
		if (!rpl)
			return null;

		return rpl.GetEntity();
	}

	//------------------------------------------------------------------------------------------------
	//! RplId to transmit for a target entity (its RplComponent id). Invalid if not replicated.
	static RplId GradGetEntityRplId(IEntity entity)
	{
		if (!entity)
			return RplId.Invalid();

		BaseGameEntity gameEntity = BaseGameEntity.Cast(entity);
		if (!gameEntity)
			return RplId.Invalid();

		RplComponent rpl = gameEntity.GetRplComponent();
		if (!rpl)
			return RplId.Invalid();

		return rpl.Id();
	}

	//------------------------------------------------------------------------------------------------
	protected void GradWarnIfOversized(string context, string json)
	{
		int len = json.Length();
		if (len > GRAD_RPC_SOFT_LIMIT)
			GRAD_Log.Warn(string.Format("%1: payload %2 chars exceeds soft limit %3 — chunking may be required (see D4)",
				context, len, GRAD_RPC_SOFT_LIMIT));
	}

	//------------------------------------------------------------------------------------------------
	protected void GradInvokeCallback(GRAD_LoadoutRequestCallback callback, bool success, GRAD_LoadoutData data)
	{
		if (callback)
			callback.OnLoadoutResponse(success, data);
	}

	//------------------------------------------------------------------------------------------------
	protected float GradWorldTimeS()
	{
		return GRAD_CommonUtils.GetUnixTime();
	}

	//------------------------------------------------------------------------------------------------
	//! DEBUG: poll (repeating) until this IS the local controller, then open the arsenal once and
	//! cancel. Repeats because the constructor runs before the controller is registered as local.
	protected void GradDebugAutoOpenTick()
	{
		// Wait until the local player is established and THIS controller is the local one.
		int localId = SCR_PlayerController.GetLocalPlayerId();
		if (localId <= 0)
			return; // local player not ready yet; keep polling

		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm || pm.GetPlayerController(localId) != this)
			return; // not the local controller; keep polling

		GetGame().GetCallqueue().Remove(GradDebugAutoOpenTick);

		MenuManager mm = GetGame().GetMenuManager();
		if (mm && mm.FindMenuByPreset(ChimeraMenuPreset.GRAD_ArsenalMenu))
			return;

		GRAD_Log.Info("DEBUG_AUTO_OPEN: opening arsenal (UI render test)");

		if (!GRAD_ArsenalService.GetInstance())
			GRAD_MenuTest.SpawnService();

		GRAD_ArsenalMenuContext context = new GRAD_ArsenalMenuContext();

		// Prefer the local controlled character; otherwise clone ANY character in the world (player
		// or AI) so the preview shows a real unit even from the GM free camera.
		IEntity target = SCR_PlayerController.GetLocalControlledEntity();
		if (!target)
			target = GRAD_MenuTest.FindAnyCharacter();

		if (target)
			context.AddTarget(target);

		GRAD_ArsenalMenu.Open(context);
	}
}

//------------------------------------------------------------------------------------------------
//! Bookkeeping for one outstanding client-side loadout request.
class GRAD_PendingLoadoutRequest
{
	int m_iRequestId;
	float m_fStartTime;
	ref GRAD_LoadoutRequestCallback m_Callback;
}

//------------------------------------------------------------------------------------------------
//! Callback interface for an asynchronous loadout request.
class GRAD_LoadoutRequestCallback
{
	void OnLoadoutResponse(bool success, GRAD_LoadoutData data);
}
