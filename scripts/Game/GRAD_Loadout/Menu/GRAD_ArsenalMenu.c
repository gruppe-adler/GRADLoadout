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

	// Item browser (query/grouping over the catalog index) + the container widgets it fills.
	protected ref GRAD_ItemBrowser m_Browser;
	protected VerticalLayoutWidget m_wCategoryList;
	protected VerticalLayoutWidget m_wItemList;

	// Row button layout (vanilla text button + our SCR_InputButtonComponent).
	protected const ResourceName ROW_LAYOUT = "{4BE35AEBB44455F0}UI/Layouts/GRAD_ListButtonRow.layout";

	// Live row handlers, kept alive for the menu's lifetime so their invokers stay valid.
	protected ref array<ref GRAD_ArsenalRowHandler> m_aRowHandlers = {};

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

		// A context is required, but a targetless context is allowed: the browser UI still renders;
		// only the live preview and OK-apply need a target. (Targetless is mainly a debug/render
		// path — normal entry points always supply at least one target.)
		if (!m_Context)
			m_Context = new GRAD_ArsenalMenuContext();

		if (!m_Context.HasTargets())
			GRAD_Log.Warn("ArsenalMenu: opened with no target; preview + apply disabled");

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
			// real starting kit. force=false: the clone already carries the prefab's locked cosmetic
			// body/clothing nodes (which cannot be re-inserted once removed). Clearing those with
			// force=true left the clone naked and the engine despawned it. So we keep the locked
			// nodes and only mirror the editable items on top.
			GRAD_LoadoutData current = GRAD_LoadoutCapture.Capture(primary, "PreviewBase", true);
			if (current)
				GRAD_LoadoutApply.Apply(m_PreviewCharacter, current, true, false, m_aPreviewCreated);

			// Pin the clone alive. A full gameplay character spawned with no controller/agent is
			// reaped by the character/AI lifetime system after a short while (the "timeout" despawn).
			// Deactivating the clone's entity events stops the controller ticking toward that cleanup;
			// the preview manager keeps rendering it from its hierarchy, and inventory storage edits
			// operate on the storage components directly so they still work. We re-activate briefly
			// around each click-apply (see ApplyToPreview) then deactivate again.
			PinPreviewAlive();
		}

		// Mouse orbit + wheel zoom on the preview.
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (m_wPreview && workspace)
			m_PreviewCameraHelper = new SCR_InventoryCharacterWidgetHelper(m_wPreview, workspace);
	}

	//------------------------------------------------------------------------------------------------
	//! Deactivate the preview clone's entity events so the character lifetime system stops ticking it
	//! toward cleanup. Safe to call repeatedly.
	protected void PinPreviewAlive()
	{
		if (m_PreviewCharacter)
			m_PreviewCharacter.Deactivate();
	}

	//------------------------------------------------------------------------------------------------
	//! Apply a single-item loadout to the preview clone, re-activating it for the duration of the
	//! mutation (some inventory operations expect an active entity) and re-pinning it afterwards.
	protected void ApplyToPreview(notnull GRAD_LoadoutData data)
	{
		if (!m_PreviewCharacter)
			return;

		m_PreviewCharacter.Activate();

		array<IEntity> created = {};
		GRAD_LoadoutApply.Apply(m_PreviewCharacter, data, true, false, created);
		foreach (IEntity e : created)
			m_aPreviewCreated.Insert(e);

		// Refresh the render in case the hierarchy auto-update missed the local mutation, then re-pin.
		if (m_PreviewManager && m_wPreview)
			m_PreviewManager.SetPreviewItem(m_wPreview, m_PreviewCharacter, null, true);

		PinPreviewAlive();
	}

	//------------------------------------------------------------------------------------------------
	//! Build the item browser from the catalog index and populate the left category rail. Each
	//! category is a button that, when clicked, fills the item list with that category's items.
	protected void SetupCategoryRail(notnull Widget root)
	{
		m_wCategoryList = VerticalLayoutWidget.Cast(root.FindAnyWidget(WIDGET_CATEGORY_LIST));
		m_wItemList = VerticalLayoutWidget.Cast(root.FindAnyWidget(WIDGET_ITEM_LIST));

		// Source the records from the singleton service's (amortized) catalog index. The service is
		// normally placed in the world, but ensure one exists so the browser works from any entry
		// point (GM right-click, arsenal box, etc.).
		if (!GRAD_ArsenalService.GetInstance())
			GRAD_MenuTest.SpawnService();

		GRAD_ArsenalService service = GRAD_ArsenalService.GetInstance();
		if (!service || !service.GetCatalogIndex())
		{
			GRAD_Log.Warn("ArsenalMenu: no catalog index available; item browser empty");
			return;
		}

		GRAD_CatalogIndex index = service.GetCatalogIndex();

		// The index builds amortized over frames, so it is often not finished when the menu opens.
		// Populate from whatever is ready now, AND subscribe to OnComplete to repopulate when the
		// full index lands. If the build hasn't started yet, kick it off.
		if (!index.IsComplete() && !index.IsBuilding())
			index.BeginBuild();

		if (!index.IsComplete())
			index.GetOnComplete().Insert(OnCatalogReady);

		RebuildBrowser(index);
	}

	//------------------------------------------------------------------------------------------------
	//! Repopulate the rail/list once the catalog index finishes building.
	protected void OnCatalogReady()
	{
		GRAD_ArsenalService service = GRAD_ArsenalService.GetInstance();
		if (!service || !service.GetCatalogIndex())
			return;

		service.GetCatalogIndex().GetOnComplete().Remove(OnCatalogReady);
		RebuildBrowser(service.GetCatalogIndex());
	}

	//------------------------------------------------------------------------------------------------
	//! (Re)build the item browser from the current index contents and refill the rail.
	protected void RebuildBrowser(notnull GRAD_CatalogIndex index)
	{
		m_Browser = new GRAD_ItemBrowser(index.GetRecords());

		// Optionally scope to the target's faction so the rail shows relevant items first.
		ChimeraCharacter primaryChar = ChimeraCharacter.Cast(m_Context.GetPrimaryTarget());
		if (primaryChar)
		{
			SCR_ChimeraCharacter scrChar = SCR_ChimeraCharacter.Cast(primaryChar);
			if (scrChar)
				m_Browser.SetFactionKey(scrChar.GetFactionKey());
		}

		GRAD_Log.Info(string.Format("ArsenalMenu: browser has %1 records, %2 categories",
			index.GetRecordCount(), m_Browser.GetCategoryCount()));

		PopulateCategories();

		if (m_Browser.GetCategoryCount() > 0)
			SelectCategoryByIndex(0);
	}

	//------------------------------------------------------------------------------------------------
	//! Create one row button per category in the rail.
	protected void PopulateCategories()
	{
		if (!m_wCategoryList || !m_Browser)
			return;

		ClearChildren(m_wCategoryList);

		array<int> types = m_Browser.GetCategoryTypes();
		for (int i = 0, count = types.Count(); i < count; i++)
		{
			int categoryType = types[i];
			Widget row = CreateRow(m_wCategoryList, GRAD_ArsenalCategoryLabels.LabelFor(categoryType));
			if (!row)
				continue;

			GRAD_ArsenalRowHandler handler = new GRAD_ArsenalRowHandler(this, row);
			handler.m_iCategoryIndex = i;
			handler.m_bIsCategory = true;
			m_aRowHandlers.Insert(handler);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Select a category by rail index and refill the item list.
	void SelectCategoryByIndex(int categoryIndex)
	{
		m_iSelectedCategory = categoryIndex;
		if (m_Browser)
			m_Browser.SetCategoryByIndex(categoryIndex);

		PopulateItems();
	}

	//------------------------------------------------------------------------------------------------
	//! Fill the item list with the current category's filtered records.
	protected void PopulateItems()
	{
		if (!m_wItemList || !m_Browser)
			return;

		ClearChildren(m_wItemList);

		array<ref GRAD_ArsenalItemRecord> records = {};
		m_Browser.GetFiltered(records);

		foreach (GRAD_ArsenalItemRecord rec : records)
		{
			if (!rec)
				continue;

			Widget row = CreateRow(m_wItemList, rec.m_sDisplayName);
			if (!row)
				continue;

			GRAD_ArsenalRowHandler handler = new GRAD_ArsenalRowHandler(this, row);
			handler.m_Record = rec;
			handler.m_bIsCategory = false;
			m_aRowHandlers.Insert(handler);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Called by a row handler when an ITEM row is clicked: add that item to the preview character.
	void OnItemRowClicked(GRAD_ArsenalItemRecord record)
	{
		if (!record || !m_PreviewCharacter)
			return;

		// Add the chosen item to the preview locally. The engine auto-refreshes the preview render.
		GRAD_LoadoutEntry entry = new GRAD_LoadoutEntry(record.m_sPrefab, -1, string.Empty, 1);
		GRAD_LoadoutData single = new GRAD_LoadoutData();
		single.m_Root.AddChild(entry);

		ApplyToPreview(single);
	}

	//------------------------------------------------------------------------------------------------
	//! Create a labelled row button under parent from the row layout. Returns the row widget.
	protected Widget CreateRow(notnull Widget parent, string label)
	{
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return null;

		Widget row = workspace.CreateWidgets(ROW_LAYOUT, parent);
		if (!row)
			return null;

		SCR_ButtonTextComponent text = SCR_ButtonTextComponent.FindButtonTextComponent(row);
		if (text)
			text.SetText(label);

		return row;
	}

	//------------------------------------------------------------------------------------------------
	//! Remove all children of a layout widget (and drop the row handlers tied to them).
	protected void ClearChildren(notnull Widget parent)
	{
		Widget child = parent.GetChildren();
		while (child)
		{
			Widget next = child.GetSibling();
			child.RemoveFromHierarchy();
			child = next;
		}
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

		SCR_PlayerController mgr = SCR_PlayerController.GradGetLocal();
		if (!mgr)
		{
			GRAD_Log.Error("ArsenalMenu: no local controller to apply through");
			Close();
			return;
		}

		// Apply to every target (multi-target GM apply supported).
		array<IEntity> targets = m_Context.GetTargets();
		foreach (IEntity target : targets)
		{
			RplId rplId = SCR_PlayerController.GradGetEntityRplId(target);
			if (rplId.IsValid())
				mgr.GradApplyLoadout(rplId, result);
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

		// Stop listening for a catalog build that may outlive this menu.
		GRAD_ArsenalService service = GRAD_ArsenalService.GetInstance();
		if (service && service.GetCatalogIndex())
			service.GetCatalogIndex().GetOnComplete().Remove(OnCatalogReady);

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

//------------------------------------------------------------------------------------------------
//! One clickable rail/list row. Bridges a row button's activation to the menu, carrying the
//! per-row context (which category, or which item) the menu needs to react.
class GRAD_ArsenalRowHandler
{
	protected GRAD_ArsenalMenu m_Menu;

	int m_iCategoryIndex = -1;			//!< meaningful when m_bIsCategory
	ref GRAD_ArsenalItemRecord m_Record;	//!< meaningful when !m_bIsCategory
	bool m_bIsCategory;

	//------------------------------------------------------------------------------------------------
	void GRAD_ArsenalRowHandler(GRAD_ArsenalMenu menu, notnull Widget rowWidget)
	{
		m_Menu = menu;

		SCR_InputButtonComponent button = SCR_InputButtonComponent.FindComponent(rowWidget);
		if (button)
			button.m_OnActivated.Insert(OnActivated);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnActivated()
	{
		if (!m_Menu)
			return;

		if (m_bIsCategory)
			m_Menu.SelectCategoryByIndex(m_iCategoryIndex);
		else
			m_Menu.OnItemRowClicked(m_Record);
	}
}

//------------------------------------------------------------------------------------------------
//! Maps an arsenal item-type value (a SCR_EArsenalItemType BITFLAG) to a display label for the
//! category rail.
//!
//! SCR_EArsenalItemType is a power-of-two bitflag enum. The bit -> meaning mapping below matches
//! the live engine enum (RIFLE = 1<<1 .. VEHICLE = 1<<22); the case values are the decimal bit
//! values (1<<1 = 2, 1<<2 = 4, ...). These names are easily edited; for full localization, replace
//! the returned strings with `#`-prefixed stringtable keys (see docs/WORKBENCH_TASKS.md).
class GRAD_ArsenalCategoryLabels
{
	//------------------------------------------------------------------------------------------------
	static string LabelFor(int arsenalType)
	{
		switch (arsenalType)
		{
			case 2:       return "Rifles";				// RIFLE = 1<<1
			case 4:       return "Pistols";				// PISTOL = 1<<2
			case 8:       return "Grenades";			// LETHAL_THROWABLE = 1<<3
			case 16:      return "Launchers";			// ROCKET_LAUNCHER = 1<<4
			case 32:      return "Machine Guns";		// MACHINE_GUN = 1<<5
			case 64:      return "Medical";				// HEAL = 1<<6
			case 128:     return "Backpacks";			// BACKPACK = 1<<7
			case 256:     return "Sniper Rifles";		// SNIPER_RIFLE = 1<<8
			case 512:     return "Smokes & Flares";		// NON_LETHAL_THROWABLE = 1<<9
			case 1024:    return "Headgear";			// HEADWEAR = 1<<10
			case 2048:    return "Jackets";				// TORSO = 1<<11
			case 4096:    return "Vests & Belts";		// VEST_AND_WAIST = 1<<12
			case 8192:    return "Trousers";			// LEGS = 1<<13
			case 16384:   return "Footwear";			// FOOTWEAR = 1<<14
			case 32768:   return "Radio Backpacks";		// RADIO_BACKPACK = 1<<15
			case 65536:   return "Equipment";			// EQUIPMENT = 1<<16
			case 131072:  return "Weapon Attachments";	// WEAPON_ATTACHMENT = 1<<17
			case 262144:  return "Explosives";			// EXPLOSIVES = 1<<18
			case 524288:  return "Gloves";				// HANDWEAR = 1<<19
			case 1048576: return "Mortars";				// MORTARS = 1<<20
			case 2097152: return "Helicopters";			// HELICOPTER = 1<<21
			case 4194304: return "Vehicles";			// VEHICLE = 1<<22
		}

		return string.Format("Type %1", arsenalType);
	}
}
