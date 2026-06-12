//------------------------------------------------------------------------------------------------
//! Extends the engine menu-preset enum with our arsenal menu. Registered to a layout + this menu
//! class via Configs/UI/GRAD_ArsenalMenu.conf (a MenuPreset entry).
modded enum ChimeraMenuPreset
{
	GRAD_ArsenalMenu
}

//------------------------------------------------------------------------------------------------
//! Full-screen virtual arsenal menu (MVP).
//!
//! UI concept (docs/DECISIONS.md D5): a left category rail drives a single content panel; the
//! preview character sits center-left. This MVP wires:
//!   - the preview character (local-only clone of the target) via ItemPreviewManagerEntity (D1),
//!   - the category rail -> item browser for the focused category,
//!   - OK (serialize preview -> apply RPC on the real target) / Cancel (discard preview).
//!
//! All edits happen on the LOCAL preview character only. Nothing touches the networked target
//! until OK, which sends the apply RPC through GRAD_LoadoutManagerComponent (P3).
//!
//! Opened with a GRAD_ArsenalMenuContext (set just before OpenMenu) describing the target(s).
class GRAD_ArsenalMenu : ChimeraMenuBase
{
	// Widget names expected in the layout (UI/Layouts/Arsenal/GRAD_ArsenalMenu.layout).
	protected const string WIDGET_PREVIEW			= "PreviewCharacter";
	protected const string WIDGET_CATEGORY_LIST		= "CategoryList";
	protected const string WIDGET_ITEM_LIST			= "ItemList";
	protected const string WIDGET_BTN_OK			= "ButtonOK";
	protected const string WIDGET_BTN_CANCEL		= "ButtonCancel";
	protected const string WIDGET_TITLE				= "Title";

	// Runtime context (target entities, permission hint) set before the menu opens.
	protected static ref GRAD_ArsenalMenuContext s_PendingContext;

	protected ref GRAD_ArsenalMenuContext m_Context;

	// Preview character + its render plumbing.
	protected IEntity m_PreviewCharacter;
	protected ItemPreviewWidget m_wPreview;
	protected ItemPreviewManagerEntity m_PreviewManager;
	protected ref SCR_InventoryCharacterWidgetHelper m_PreviewCameraHelper;

	// Entities created on the preview character by the last apply (for cleanup).
	protected ref array<IEntity> m_aPreviewCreated = {};

	// The category currently in focus (drives the item browser).
	protected int m_iSelectedCategory = -1;

	//------------------------------------------------------------------------------------------------
	//! Stash the context that the next OpenMenu(GRAD_ArsenalMenu) call should pick up.
	static void SetPendingContext(GRAD_ArsenalMenuContext context)
	{
		s_PendingContext = context;
	}

	//------------------------------------------------------------------------------------------------
	//! Convenience: open the arsenal for the given context.
	static GRAD_ArsenalMenu Open(GRAD_ArsenalMenuContext context)
	{
		SetPendingContext(context);

		MenuManager mm = GetGame().GetMenuManager();
		if (!mm)
			return null;

		return GRAD_ArsenalMenu.Cast(mm.OpenMenu(ChimeraMenuPreset.GRAD_ArsenalMenu));
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuOpen()
	{
		super.OnMenuOpen();

		m_Context = s_PendingContext;
		s_PendingContext = null;

		if (!m_Context || !m_Context.HasTargets())
		{
			GRAD_Log.Error("ArsenalMenu: opened without a valid context/target; closing");
			Close();
			return;
		}

		Widget root = GetRootWidget();
		if (!root)
		{
			GRAD_Log.Error("ArsenalMenu: no root widget");
			Close();
			return;
		}

		BindButtons(root);
		SetupPreview(root);
		SetupCategoryRail(root);

		GRAD_Log.Info("ArsenalMenu: opened");
	}

	//------------------------------------------------------------------------------------------------
	protected void BindButtons(notnull Widget root)
	{
		SCR_InputButtonComponent okBtn = SCR_InputButtonComponent.GetInputButtonComponent(WIDGET_BTN_OK, root);
		if (okBtn)
			okBtn.m_OnActivated.Insert(OnConfirm);

		SCR_InputButtonComponent cancelBtn = SCR_InputButtonComponent.GetInputButtonComponent(WIDGET_BTN_CANCEL, root);
		if (cancelBtn)
			cancelBtn.m_OnActivated.Insert(OnCancel);
	}

	//------------------------------------------------------------------------------------------------
	//! Spawn a local-only preview clone of the primary target and bind it to the preview widget.
	protected void SetupPreview(notnull Widget root)
	{
		m_wPreview = ItemPreviewWidget.Cast(root.FindAnyWidget(WIDGET_PREVIEW));
		if (!m_wPreview)
		{
			GRAD_Log.Warn("ArsenalMenu: preview widget not found in layout");
			return;
		}

		ChimeraWorld world = GetGame().GetWorld();
		if (world)
			m_PreviewManager = world.GetItemPreviewManager();

		// Clone the primary target's prefab as a local, non-replicated preview character.
		IEntity primary = m_Context.GetPrimaryTarget();
		ResourceName charPrefab = GRAD_InventoryLib.GetPrefabResourceName(primary);
		if (charPrefab != ResourceName.Empty)
			m_PreviewCharacter = GRAD_InventoryLib.SpawnLocal(charPrefab);

		if (m_PreviewCharacter && m_PreviewManager)
		{
			m_PreviewManager.SetPreviewItem(m_wPreview, m_PreviewCharacter);

			// Mirror the target's current loadout onto the preview so the player edits from their
			// real starting kit.
			GRAD_LoadoutData current = GRAD_LoadoutCapture.Capture(primary, "PreviewBase", true);
			if (current)
				GRAD_LoadoutApply.Apply(m_PreviewCharacter, current, true, true, m_aPreviewCreated);
		}

		// Mouse orbit + wheel zoom on the preview.
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (m_wPreview && workspace)
			m_PreviewCameraHelper = new SCR_InventoryCharacterWidgetHelper(m_wPreview, workspace);
	}

	//------------------------------------------------------------------------------------------------
	//! Populate the left category rail. Category -> item browser wiring is fleshed out as the
	//! browser component lands; the MVP establishes the rail and selection handler.
	protected void SetupCategoryRail(notnull Widget root)
	{
		// Category labels map to arsenal item-type groupings. Kept here as the single source the
		// rail builds from; the browser filters the catalog index by the selected category.
		// (Item-browser population is built out in P6.)
		SelectCategory(0);
	}

	//------------------------------------------------------------------------------------------------
	protected void SelectCategory(int categoryIndex)
	{
		m_iSelectedCategory = categoryIndex;
		// P6: filter GRAD_ArsenalService.GetInstance().GetCatalogIndex() by this category and fill
		// the item list widget.
	}

	//------------------------------------------------------------------------------------------------
	//! OK: serialize the preview character's loadout and apply it to the real target(s) via RPC.
	protected void OnConfirm()
	{
		if (!m_PreviewCharacter || !m_Context)
		{
			Close();
			return;
		}

		GRAD_LoadoutData result = GRAD_LoadoutCapture.Capture(m_PreviewCharacter, "ArsenalResult", true);
		if (!result)
		{
			GRAD_Log.Error("ArsenalMenu: failed to capture preview loadout on confirm");
			Close();
			return;
		}

		GRAD_LoadoutManagerComponent mgr = GRAD_LoadoutManagerComponent.GetLocal();
		if (!mgr)
		{
			GRAD_Log.Error("ArsenalMenu: no local loadout manager to apply through");
			Close();
			return;
		}

		// Apply to every target (multi-target GM apply supported).
		array<IEntity> targets = m_Context.GetTargets();
		foreach (IEntity target : targets)
		{
			RplId rplId = GRAD_LoadoutManagerComponent.GetEntityRplId(target);
			if (rplId.IsValid())
				mgr.ApplyLoadout(rplId, result);
			else
				GRAD_Log.Warn(string.Format("ArsenalMenu: target %1 has no RplId; skipped", GRAD_InventoryLib.GetEntityShortName(target)));
		}

		// Remember name for the "load previous" quick action.
		GRAD_ArsenalService service = GRAD_ArsenalService.GetInstance();
		if (service)
			service.SetLastUsedLoadoutName(result.m_sName);

		Close();
	}

	//------------------------------------------------------------------------------------------------
	//! Cancel: discard the preview, change nothing on the real target.
	protected void OnCancel()
	{
		Close();
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuClose()
	{
		// Tear down the preview character + helpers. The networked target is never touched here.
		if (m_PreviewCameraHelper)
		{
			m_PreviewCameraHelper.Destroy();
			m_PreviewCameraHelper = null;
		}

		GRAD_LoadoutApply.CleanupCreated(m_aPreviewCreated);

		if (m_PreviewCharacter)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(m_PreviewCharacter);
			m_PreviewCharacter = null;
		}

		super.OnMenuClose();
		GRAD_Log.Info("ArsenalMenu: closed");
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuUpdate(float tDelta)
	{
		super.OnMenuUpdate(tDelta);

		// Drive the preview orbit/zoom camera.
		if (m_PreviewCameraHelper)
		{
			PreviewRenderAttributes attribs;
			m_PreviewCameraHelper.Update(tDelta, attribs);
		}
	}
}

//------------------------------------------------------------------------------------------------
//! Describes what the arsenal menu operates on: one or more target characters. For players this
//! is their own character; for GMs it may be several selected units (multi-target apply).
class GRAD_ArsenalMenuContext
{
	protected ref array<IEntity> m_aTargets = {};

	//------------------------------------------------------------------------------------------------
	void AddTarget(IEntity target)
	{
		if (target)
			m_aTargets.Insert(target);
	}

	//------------------------------------------------------------------------------------------------
	array<IEntity> GetTargets()
	{
		return m_aTargets;
	}

	//------------------------------------------------------------------------------------------------
	IEntity GetPrimaryTarget()
	{
		if (m_aTargets.IsEmpty())
			return null;

		return m_aTargets[0];
	}

	//------------------------------------------------------------------------------------------------
	bool HasTargets()
	{
		return !m_aTargets.IsEmpty();
	}
}
