//------------------------------------------------------------------------------------------------
//! User action that opens the arsenal for the using character. Placed on the arsenal box prefab.
//!
//! Opening is a purely local UI action: it builds a context targeting the user's own character and
//! opens GRAD_ArsenalMenu. All edits happen on the local preview; applying the result to the real
//! character goes through the server RPC on confirm (P3).
class GRAD_OpenArsenalAction : SCR_ScriptedUserAction
{
	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (!pUserEntity)
			return;

		GRAD_ArsenalMenuContext context = new GRAD_ArsenalMenuContext();
		context.AddTarget(pUserEntity);
		GRAD_ArsenalMenu.Open(context);
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		// Only show to a local player character (the menu is a local UI).
		return user == SCR_PlayerController.GetLocalControlledEntity();
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		outName = "#GRAD_OPEN_ARSENAL";
		return true;
	}
}
