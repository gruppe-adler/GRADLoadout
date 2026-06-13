//------------------------------------------------------------------------------------------------
//! User action that re-applies the player's most recently used saved loadout directly, without
//! opening the menu. A quick "kit me out like last time" button on the arsenal box.
//!
//! The last-used name is session state held by the arsenal service. If there is no last-used
//! loadout, or it no longer exists on disk, the action is hidden.
class GRAD_LoadPreviousAction : SCR_ScriptedUserAction
{
	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (!pUserEntity)
			return;

		string lastName = GetLastName();
		if (GRAD_CommonUtils.IsBlank(lastName))
			return;

		GRAD_LoadoutData data = GRAD_LoadoutStore.Load(lastName);
		if (!data)
		{
			GRAD_Log.Warn(string.Format("LoadPrevious: '%1' could not be loaded", lastName));
			return;
		}

		SCR_PlayerController mgr = SCR_PlayerController.GradGetLocal();
		if (!mgr)
			return;

		RplId rplId = SCR_PlayerController.GradGetEntityRplId(pUserEntity);
		if (rplId.IsValid())
			mgr.GradApplyLoadout(rplId, data);
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		if (user != SCR_PlayerController.GetLocalControlledEntity())
			return false;

		// Only show when there is a last-used loadout that still exists on disk.
		string lastName = GetLastName();
		return !GRAD_CommonUtils.IsBlank(lastName) && GRAD_LoadoutStore.Exists(lastName);
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		outName = "#GRAD_LOAD_PREVIOUS_LOADOUT";
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected string GetLastName()
	{
		GRAD_ArsenalService service = GRAD_ArsenalService.GetInstance();
		if (!service)
			return string.Empty;

		return service.GetLastUsedLoadoutName();
	}
}
