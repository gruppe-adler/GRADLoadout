//------------------------------------------------------------------------------------------------
//! Server-side permission gate for loadout operations.
//!
//! A player may operate on a target character if ANY of:
//!   - they control that character themselves (ownership), OR
//!   - they hold an admin / session-admin role (PlayerManager.HasPlayerRole), OR
//!   - they have full (non-limited) Game Master authority over the session.
//!
//! All checks run on the server/authority. The client is never trusted to assert its own
//! permission — these functions are only meaningful when called from authoritative code.
class GRAD_LoadoutPermissions
{
	//------------------------------------------------------------------------------------------------
	//! May the player identified by requesterPlayerId apply/read a loadout on targetEntity?
	static bool CanOperate(int requesterPlayerId, IEntity targetEntity)
	{
		if (requesterPlayerId <= 0 || !targetEntity)
			return false;

		// 1) Ownership: the requester controls the target.
		if (OwnsEntity(requesterPlayerId, targetEntity))
			return true;

		// 2) Admin / session admin.
		if (IsAdmin(requesterPlayerId))
			return true;

		// 3) Full Game Master authority.
		if (IsGameMaster(requesterPlayerId))
			return true;

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! True if requesterPlayerId currently controls targetEntity.
	static bool OwnsEntity(int requesterPlayerId, IEntity targetEntity)
	{
		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return false;

		int ownerId = pm.GetPlayerIdFromControlledEntity(targetEntity);
		return ownerId > 0 && ownerId == requesterPlayerId;
	}

	//------------------------------------------------------------------------------------------------
	//! True if the player holds an administrator or session-administrator role.
	//!
	//! NOTE: the exact EPlayerRole flag names are confirmed at compile time. If the engine renames
	//! these, update here — they are the only two role constants this mod depends on.
	static bool IsAdmin(int playerId)
	{
		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return false;

		return pm.HasPlayerRole(playerId, EPlayerRole.ADMINISTRATOR)
			|| pm.HasPlayerRole(playerId, EPlayerRole.SESSION_ADMINISTRATOR);
	}

	//------------------------------------------------------------------------------------------------
	//! True if the player has a full (non-limited) Game Master editor instance, i.e. real GM powers
	//! rather than a restricted/limited editor mode.
	static bool IsGameMaster(int playerId)
	{
		BaseGameMode gameMode = GetGame().GetGameMode();
		if (!gameMode)
			return false;

		SCR_EditorManagerCore core = SCR_EditorManagerCore.Cast(gameMode.FindComponent(SCR_EditorManagerCore));
		if (!core)
			return false;

		SCR_EditorManagerEntity editorManager = core.GetEditorManager(playerId);
		if (!editorManager)
			return false;

		// A limited editor instance does not grant full GM powers.
		return !editorManager.IsLimited();
	}
}
