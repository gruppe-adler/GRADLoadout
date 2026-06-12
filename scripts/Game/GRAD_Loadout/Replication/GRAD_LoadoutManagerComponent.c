//------------------------------------------------------------------------------------------------
//! Server-authoritative loadout RPC host. Attached to the SCR_PlayerController prefab (composition
//! rather than `modded class` — see docs/DECISIONS.md D6).
//!
//! Two flows, both gated server-side by GRAD_LoadoutPermissions:
//!
//!   APPLY:   client -> RpcAsk_Apply(targetRplId, json) -> server validates, deserializes, applies.
//!   REQUEST: client -> RpcAsk_Request(targetRplId, requestId) -> server validates, captures,
//!            -> RpcDo_Response(requestId, json) back to the requesting owner.
//!
//! The request side uses a request-id + timeout + callback so the UI can show "loading…" and
//! recover from a dropped/lost request. Stale requests are swept on a timer.
//!
//! RPC payload size (D4): a serialized loadout can be large. We log the length and, if it exceeds
//! GRAD_RPC_SOFT_LIMIT, warn — chunked transfer is the planned mitigation if real loadouts breach
//! the engine's hard cap (to be measured in MP testing). The send path is factored so chunking can
//! be added without touching callers.
class GRAD_LoadoutManagerComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
class GRAD_LoadoutManagerComponent : ScriptComponent
{
	//! Soft warning threshold for serialized payload length (characters). Not the engine hard cap;
	//! a heuristic to flag loadouts that may need chunking. Refine after MP measurement (D4).
	protected static const int GRAD_RPC_SOFT_LIMIT = 8000;

	//! Seconds before an outstanding loadout request is considered dead and its callback failed.
	protected static const float GRAD_REQUEST_TIMEOUT_S = 6.0;

	//! Owning player controller (cached on init).
	protected PlayerController m_PlayerController;

	//! Monotonic request-id source for this controller's outgoing requests.
	protected int m_iNextRequestId = 1;

	//! Outstanding client-side requests awaiting a server response, keyed by request id.
	protected ref map<int, ref GRAD_PendingLoadoutRequest> m_mPendingRequests;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		m_PlayerController = PlayerController.Cast(owner);
		m_mPendingRequests = new map<int, ref GRAD_PendingLoadoutRequest>();
	}

	//------------------------------------------------------------------------------------------------
	//! Convenience accessor: the loadout manager component on a given player's controller.
	static GRAD_LoadoutManagerComponent GetForPlayer(int playerId)
	{
		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return null;

		PlayerController pc = pm.GetPlayerController(playerId);
		if (!pc)
			return null;

		return GRAD_LoadoutManagerComponent.Cast(pc.FindComponent(GRAD_LoadoutManagerComponent));
	}

	//------------------------------------------------------------------------------------------------
	//! The local player's own loadout manager component (client side).
	static GRAD_LoadoutManagerComponent GetLocal()
	{
		return GetForPlayer(SCR_PlayerController.GetLocalPlayerId());
	}

	//------------------------------------------------------------------------------------------------
	// ---- APPLY ----------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------

	//! Client entry point: request the server apply `data` to the character identified by targetRplId.
	void ApplyLoadout(RplId targetRplId, GRAD_LoadoutData data)
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

		WarnIfOversized("ApplyLoadout", json);
		Rpc(RpcAsk_Apply, targetRplId, json);
	}

	//------------------------------------------------------------------------------------------------
	//! SERVER: validate the requester's permission, then deserialize and apply.
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void RpcAsk_Apply(RplId targetRplId, string json)
	{
		int requesterId = GetOwnerPlayerId();
		IEntity target = ResolveEntity(targetRplId);
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
	//! `callback` is invoked with the result (success+data or failure) or on timeout.
	//! Returns the request id (or 0 if the request could not be issued).
	int RequestLoadout(RplId targetRplId, GRAD_LoadoutRequestCallback callback)
	{
		int requestId = m_iNextRequestId++;

		GRAD_PendingLoadoutRequest pending = new GRAD_PendingLoadoutRequest();
		pending.m_iRequestId = requestId;
		pending.m_Callback = callback;
		pending.m_fStartTime = GetWorldTimeS();
		m_mPendingRequests.Set(requestId, pending);

		Rpc(RpcAsk_Request, targetRplId, requestId);

		// Ensure the sweep loop is running so this request can time out if no answer comes.
		ScheduleTimeoutSweep();
		return requestId;
	}

	//------------------------------------------------------------------------------------------------
	//! SERVER: validate, capture the target's current loadout, and send it back to the owner.
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void RpcAsk_Request(RplId targetRplId, int requestId)
	{
		int requesterId = GetOwnerPlayerId();
		IEntity target = ResolveEntity(targetRplId);
		if (!target || !GRAD_LoadoutPermissions.CanOperate(requesterId, target))
		{
			GRAD_Log.Warn(string.Format("RpcAsk_Request: player %1 DENIED/!target for request %2", requesterId, requestId));
			// Send an explicit empty response so the client can fail fast rather than wait for timeout.
			Rpc(RpcDo_Response, requestId, string.Empty);
			return;
		}

		GRAD_LoadoutData data = GRAD_LoadoutCapture.Capture(target, "Requested", true);
		string json = string.Empty;
		if (data)
			json = data.ToJsonString();

		WarnIfOversized("RpcAsk_Request", json);
		Rpc(RpcDo_Response, requestId, json);
	}

	//------------------------------------------------------------------------------------------------
	//! OWNER (client): receive a requested loadout and fulfil the matching pending request.
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	void RpcDo_Response(int requestId, string json)
	{
		GRAD_PendingLoadoutRequest pending;
		if (!m_mPendingRequests.Find(requestId, pending) || !pending)
		{
			GRAD_Log.Debug(string.Format("RpcDo_Response: no pending request %1 (stale/late)", requestId));
			return;
		}

		m_mPendingRequests.Remove(requestId);

		if (GRAD_CommonUtils.IsBlank(json))
		{
			InvokeCallback(pending.m_Callback, false, null);
			return;
		}

		GRAD_LoadoutData data = GRAD_LoadoutData.FromJsonString(json);
		InvokeCallback(pending.m_Callback, data != null, data);
	}

	//------------------------------------------------------------------------------------------------
	// ---- timeout handling -----------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------

	//! (Re)arm the periodic sweep that fails out timed-out requests. Cheap no-op if already armed.
	protected void ScheduleTimeoutSweep()
	{
		GetGame().GetCallqueue().Remove(SweepTimeouts);
		GetGame().GetCallqueue().CallLater(SweepTimeouts, 1000, false);
	}

	//------------------------------------------------------------------------------------------------
	//! Fail any pending request older than the timeout; reschedule while requests remain.
	protected void SweepTimeouts()
	{
		if (!m_mPendingRequests || m_mPendingRequests.IsEmpty())
			return;

		float now = GetWorldTimeS();
		array<int> expired = {};

		foreach (int id, GRAD_PendingLoadoutRequest pending : m_mPendingRequests)
		{
			if (pending && (now - pending.m_fStartTime) >= GRAD_REQUEST_TIMEOUT_S)
				expired.Insert(id);
		}

		foreach (int id : expired)
		{
			GRAD_PendingLoadoutRequest pending;
			if (m_mPendingRequests.Find(id, pending))
			{
				m_mPendingRequests.Remove(id);
				GRAD_Log.Warn(string.Format("Loadout request %1 timed out", id));
				InvokeCallback(pending.m_Callback, false, null);
			}
		}

		if (!m_mPendingRequests.IsEmpty())
			ScheduleTimeoutSweep();
	}

	//------------------------------------------------------------------------------------------------
	// ---- helpers --------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------

	//! Player id of this controller's owner (server-side identity of the requester).
	protected int GetOwnerPlayerId()
	{
		if (!m_PlayerController)
			return 0;

		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return 0;

		IEntity controlled = m_PlayerController.GetControlledEntity();
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
			if (pm.GetPlayerController(pid) == m_PlayerController)
				return pid;
		}

		return 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Resolve a replicated entity from its RplId. The RplId we transmit is the entity's
	//! RplComponent id (entity.GetRplComponent().Id()); FindItem returns that component, whose owner
	//! is the entity.
	protected IEntity ResolveEntity(RplId rplId)
	{
		RplComponent rpl = RplComponent.Cast(Replication.FindItem(rplId));
		if (!rpl)
			return null;

		return rpl.GetEntity();
	}

	//------------------------------------------------------------------------------------------------
	//! RplId to transmit for a target entity (its RplComponent id). Returns an invalid id if the
	//! entity is not replicated. Use this on the client before calling ApplyLoadout/RequestLoadout.
	static RplId GetEntityRplId(IEntity entity)
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
	protected void WarnIfOversized(string context, string json)
	{
		int len = json.Length();
		if (len > GRAD_RPC_SOFT_LIMIT)
		{
			GRAD_Log.Warn(string.Format("%1: payload %2 chars exceeds soft limit %3 — chunking may be required (see D4)",
				context, len, GRAD_RPC_SOFT_LIMIT));
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void InvokeCallback(GRAD_LoadoutRequestCallback callback, bool success, GRAD_LoadoutData data)
	{
		if (callback)
			callback.OnLoadoutResponse(success, data);
	}

	//------------------------------------------------------------------------------------------------
	//! Wall-clock seconds, used for request ageing. Unix time is adequate at our 6-second timeout
	//! granularity and avoids depending on world-time plumbing.
	protected float GetWorldTimeS()
	{
		return GRAD_CommonUtils.GetUnixTime();
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
//! Callback interface for an asynchronous loadout request. The UI implements this to receive the
//! captured loadout (or a failure) once the server answers, or on timeout.
class GRAD_LoadoutRequestCallback
{
	//! \param success true if a loadout was returned; false on denial/timeout/parse failure
	//! \param data    the captured loadout when success is true; null otherwise
	void OnLoadoutResponse(bool success, GRAD_LoadoutData data);
}
