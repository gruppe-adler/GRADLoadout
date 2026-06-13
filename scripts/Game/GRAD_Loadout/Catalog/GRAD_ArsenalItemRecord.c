//------------------------------------------------------------------------------------------------
//! One indexed arsenal item: the flattened, UI-ready data the menu needs about a single
//! catalog-eligible item. Built once (amortized) at world load and held by the catalog index.
//!
//! Keeping this as a plain script object (not a catalog lookup) means the menu can sort, filter,
//! and display thousands of items without re-querying the catalog every frame.
class GRAD_ArsenalItemRecord
{
	//! Prefab to spawn for this item.
	ResourceName m_sPrefab;

	//! Localized display name (resolved from the catalog UI info).
	string m_sDisplayName;

	//! Arsenal item type (weapon / vest / headgear / magazine / ...), used to decide which slots
	//! this item is valid for. Stored as int to stay decoupled from the engine enum at the record
	//! level; the indexer fills it from SCR_ArsenalItem.GetItemType().
	int m_iArsenalType;

	//! UI info object from the catalog, for icon resolution in the browser. May be null.
	SCR_UIInfo m_UiInfo;

	//! Faction key this record was sourced from (empty for faction-agnostic items).
	string m_sFactionKey;

	//------------------------------------------------------------------------------------------------
	void GRAD_ArsenalItemRecord(ResourceName prefab, string displayName, int arsenalType, SCR_UIInfo uiInfo, string factionKey)
	{
		m_sPrefab = prefab;
		m_sDisplayName = displayName;
		m_iArsenalType = arsenalType;
		m_UiInfo = uiInfo;
		m_sFactionKey = factionKey;
	}
}

//------------------------------------------------------------------------------------------------
//! Alphabetical (by display name) sorter for arsenal records, used by the item browser.
class GRAD_ArsenalItemNameSorter : GRAD_Sorter<GRAD_ArsenalItemRecord>
{
	override int Compare(GRAD_ArsenalItemRecord a, GRAD_ArsenalItemRecord b)
	{
		if (!a || !b)
			return 0;

		string an = a.m_sDisplayName;
		string bn = b.m_sDisplayName;
		if (an < bn)
			return -1;
		if (an > bn)
			return 1;
		return 0;
	}
}
