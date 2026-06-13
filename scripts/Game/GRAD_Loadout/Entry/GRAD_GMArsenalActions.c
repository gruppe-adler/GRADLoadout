//------------------------------------------------------------------------------------------------
//! Game Master editor context actions for the arsenal:
//!   - GRAD_GMOpenArsenalAction  : open the arsenal targeting all selected characters (multi-apply)
//!   - GRAD_GMCopyLoadoutAction   : capture the hovered/selected character's loadout to the clipboard
//!   - GRAD_GMPasteLoadoutAction  : apply the clipboard loadout to all selected characters
//!
//! These run in the editor; the actual apply still goes through the server RPC + permission gate
//! (a GM passes the permission check). The clipboard is the in-memory loadout held by the
//! singleton arsenal service.

//------------------------------------------------------------------------------------------------
//! Shared helpers for the GM actions.
class GRAD_GMArsenalActionUtils
{
	//------------------------------------------------------------------------------------------------
	//! Collect the IEntity of every selected editable entity that is a character.
	static int CollectCharacters(notnull set<SCR_EditableEntityComponent> selected, out notnull array<IEntity> outChars)
	{
		outChars.Clear();
		foreach (SCR_EditableEntityComponent editable : selected)
		{
			if (!editable)
				continue;

			IEntity entity = editable.GetOwner();
			if (ChimeraCharacter.Cast(entity))
				outChars.Insert(entity);
		}
		return outChars.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! Entity of a single editable (hovered) entity if it is a character, else null.
	static IEntity CharacterOf(SCR_EditableEntityComponent editable)
	{
		if (!editable)
			return null;

		IEntity entity = editable.GetOwner();
		if (ChimeraCharacter.Cast(entity))
			return entity;

		return null;
	}
}

//------------------------------------------------------------------------------------------------
//! GM: open the arsenal targeting all selected characters. Confirm applies the same loadout to all.
class GRAD_GMOpenArsenalAction : SCR_BaseContextAction
{
	//------------------------------------------------------------------------------------------------
	override bool CanBeShown(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
	{
		array<IEntity> chars = {};
		return GRAD_GMArsenalActionUtils.CollectCharacters(selectedEntities, chars) > 0
			|| GRAD_GMArsenalActionUtils.CharacterOf(hoveredEntity) != null;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformed(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
	{
		return CanBeShown(hoveredEntity, selectedEntities, cursorWorldPosition, flags);
	}

	//------------------------------------------------------------------------------------------------
	override void Perform(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags, int param = -1)
	{
		GRAD_ArsenalMenuContext context = new GRAD_ArsenalMenuContext();

		array<IEntity> chars = {};
		GRAD_GMArsenalActionUtils.CollectCharacters(selectedEntities, chars);
		foreach (IEntity c : chars)
			context.AddTarget(c);

		// Fall back to the hovered character if nothing was selected.
		if (!context.HasTargets())
		{
			IEntity hovered = GRAD_GMArsenalActionUtils.CharacterOf(hoveredEntity);
			if (hovered)
				context.AddTarget(hovered);
		}

		if (context.HasTargets())
			GRAD_ArsenalMenu.Open(context);
	}
}

//------------------------------------------------------------------------------------------------
//! GM: copy the hovered (or first selected) character's current loadout to the clipboard.
class GRAD_GMCopyLoadoutAction : SCR_BaseContextAction
{
	//------------------------------------------------------------------------------------------------
	override bool CanBeShown(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
	{
		return ResolveSource(hoveredEntity, selectedEntities) != null;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformed(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
	{
		return ResolveSource(hoveredEntity, selectedEntities) != null;
	}

	//------------------------------------------------------------------------------------------------
	override void Perform(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags, int param = -1)
	{
		IEntity source = ResolveSource(hoveredEntity, selectedEntities);
		if (!source)
			return;

		GRAD_ArsenalService service = GRAD_ArsenalService.GetInstance();
		if (!service)
			return;

		GRAD_LoadoutData data = GRAD_LoadoutCapture.Capture(source, "Clipboard", true);
		if (data)
		{
			service.SetClipboard(data);
			GRAD_Log.Info("GM: loadout copied to clipboard");
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Hovered character, else the first selected character.
	protected IEntity ResolveSource(SCR_EditableEntityComponent hoveredEntity, set<SCR_EditableEntityComponent> selectedEntities)
	{
		IEntity hovered = GRAD_GMArsenalActionUtils.CharacterOf(hoveredEntity);
		if (hovered)
			return hovered;

		array<IEntity> chars = {};
		GRAD_GMArsenalActionUtils.CollectCharacters(selectedEntities, chars);
		if (!chars.IsEmpty())
			return chars[0];

		return null;
	}
}

//------------------------------------------------------------------------------------------------
//! GM: apply the clipboard loadout to every selected character (via the server RPC).
class GRAD_GMPasteLoadoutAction : SCR_BaseContextAction
{
	//------------------------------------------------------------------------------------------------
	override bool CanBeShown(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
	{
		GRAD_ArsenalService service = GRAD_ArsenalService.GetInstance();
		if (!service || !service.HasClipboard())
			return false;

		array<IEntity> chars = {};
		return GRAD_GMArsenalActionUtils.CollectCharacters(selectedEntities, chars) > 0
			|| GRAD_GMArsenalActionUtils.CharacterOf(hoveredEntity) != null;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformed(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
	{
		return CanBeShown(hoveredEntity, selectedEntities, cursorWorldPosition, flags);
	}

	//------------------------------------------------------------------------------------------------
	override void Perform(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags, int param = -1)
	{
		GRAD_ArsenalService service = GRAD_ArsenalService.GetInstance();
		if (!service || !service.HasClipboard())
			return;

		GRAD_LoadoutData data = service.GetClipboard();
		SCR_PlayerController mgr = SCR_PlayerController.GradGetLocal();
		if (!mgr)
			return;

		array<IEntity> targets = {};
		GRAD_GMArsenalActionUtils.CollectCharacters(selectedEntities, targets);
		if (targets.IsEmpty())
		{
			IEntity hovered = GRAD_GMArsenalActionUtils.CharacterOf(hoveredEntity);
			if (hovered)
				targets.Insert(hovered);
		}

		int applied = 0;
		foreach (IEntity target : targets)
		{
			RplId rplId = SCR_PlayerController.GradGetEntityRplId(target);
			if (rplId.IsValid())
			{
				mgr.GradApplyLoadout(rplId, data);
				applied++;
			}
		}

		GRAD_Log.Info(string.Format("GM: pasted clipboard loadout to %1 character(s)", applied));
	}
}
