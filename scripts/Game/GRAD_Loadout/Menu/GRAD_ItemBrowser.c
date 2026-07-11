//------------------------------------------------------------------------------------------------
//! In-memory query/grouping layer over the catalog index, feeding the menu's left rail and item
//! list. Pure data — no widgets — so it is testable and reusable.
//!
//! Design: rather than hardcode arsenal item-type enum values (which vary by game version and are
//! not all exposed to the API index), the browser GROUPS records by whatever m_iArsenalType values
//! the catalog actually produced. Each distinct type becomes a category. Optional faction and
//! free-text filters narrow the records shown within the selected category.
class GRAD_ItemBrowser
{
	//! The records this browser draws from (the full catalog index).
	protected ref array<ref GRAD_ArsenalItemRecord> m_aAllRecords;

	//! Distinct arsenal-type values present, in ascending order — the category list.
	protected ref array<int> m_aCategoryTypes;

	//! Active filters.
	protected int m_iCategoryType = -1;		//!< -1 = all categories
	protected string m_sFactionKey;			//!< empty = all factions
	protected string m_sSearch;				//!< empty = no text filter

	//! Reusable sorter for alphabetical output.
	protected ref GRAD_ArsenalItemNameSorter m_Sorter;

	//------------------------------------------------------------------------------------------------
	void GRAD_ItemBrowser(notnull array<ref GRAD_ArsenalItemRecord> records)
	{
		m_aAllRecords = records;
		m_aCategoryTypes = {};
		m_Sorter = new GRAD_ArsenalItemNameSorter();
		RebuildCategories();
	}

	//------------------------------------------------------------------------------------------------
	//! Recompute the distinct category types from the current record set.
	void RebuildCategories()
	{
		m_aCategoryTypes.Clear();

		// Collect distinct types via a set-like presence map.
		map<int, bool> seen = new map<int, bool>();
		foreach (GRAD_ArsenalItemRecord rec : m_aAllRecords)
		{
			if (rec && !seen.Contains(rec.m_iArsenalType))
			{
				seen.Set(rec.m_iArsenalType, true);
				m_aCategoryTypes.Insert(rec.m_iArsenalType);
			}
		}

		m_aCategoryTypes.Sort();
	}

	//------------------------------------------------------------------------------------------------
	//! Distinct category type values (the left-rail entries). Caller maps each to a label.
	array<int> GetCategoryTypes()
	{
		return m_aCategoryTypes;
	}

	//------------------------------------------------------------------------------------------------
	int GetCategoryCount()
	{
		return m_aCategoryTypes.Count();
	}

	//------------------------------------------------------------------------------------------------
	// ---- filter setters -------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------

	//! Set the active category by arsenal-type value (-1 = all).
	void SetCategory(int arsenalType)
	{
		m_iCategoryType = arsenalType;
	}

	//! Set the active category by index into GetCategoryTypes() (-1 = all).
	void SetCategoryByIndex(int index)
	{
		if (index < 0 || index >= m_aCategoryTypes.Count())
			m_iCategoryType = -1;
		else
			m_iCategoryType = m_aCategoryTypes[index];
	}

	void SetFactionKey(string factionKey)
	{
		m_sFactionKey = factionKey;
	}

	void SetSearch(string search)
	{
		m_sSearch = search;
	}

	int GetActiveCategory()
	{
		return m_iCategoryType;
	}

	//------------------------------------------------------------------------------------------------
	// ---- query ----------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------

	//! Produce the filtered, alphabetically-sorted record list for the current filters.
	int GetFiltered(out notnull array<ref GRAD_ArsenalItemRecord> outRecords)
	{
		outRecords.Clear();

		foreach (GRAD_ArsenalItemRecord rec : m_aAllRecords)
		{
			if (!rec)
				continue;

			if (m_iCategoryType != -1 && rec.m_iArsenalType != m_iCategoryType)
				continue;

			if (!GRAD_CommonUtils.IsBlank(m_sFactionKey)
				&& !GRAD_CommonUtils.IsBlank(rec.m_sFactionKey)
				&& rec.m_sFactionKey != m_sFactionKey)
				continue;

			if (!GRAD_CommonUtils.IsBlank(m_sSearch) && !MatchesSearch(rec, m_sSearch))
				continue;

			outRecords.Insert(rec);
		}

		m_Sorter.Sort(outRecords);
		return outRecords.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! Produce the filtered records bucketed into groups by base name (variants of one item group
	//! together, e.g. "SVD Mag" -> its API/Tracer/LPS variants). Groups are sorted by label; items
	//! within each group by display name. A base name with a single record still forms a group (the
	//! renderer shows single-item groups as a plain row).
	int GetGrouped(out notnull array<ref GRAD_ItemGroup> outGroups)
	{
		outGroups.Clear();

		array<ref GRAD_ArsenalItemRecord> records = {};
		GetFiltered(records);

		// Bucket by base name, preserving first-seen group order via a key->index map.
		map<string, int> indexByKey = new map<string, int>();
		foreach (GRAD_ArsenalItemRecord rec : records)
		{
			if (!rec)
				continue;

			string key = rec.m_sBaseName;
			if (GRAD_CommonUtils.IsBlank(key))
				key = rec.m_sDisplayName;

			int gi;
			if (!indexByKey.Find(key, gi))
			{
				GRAD_ItemGroup group = new GRAD_ItemGroup();
				group.m_sLabel = key;
				outGroups.Insert(group);
				gi = outGroups.Count() - 1;
				indexByKey.Set(key, gi);
			}

			outGroups[gi].m_aItems.Insert(rec);
		}

		// Sort groups alphabetically by label (simple selection sort — group counts are small).
		for (int i = 0, n = outGroups.Count(); i < n - 1; i++)
		{
			int min = i;
			for (int j = i + 1; j < n; j++)
			{
				if (outGroups[j].m_sLabel < outGroups[min].m_sLabel)
					min = j;
			}
			if (min != i)
			{
				GRAD_ItemGroup tmp = outGroups[i];
				outGroups[i] = outGroups[min];
				outGroups[min] = tmp;
			}
		}

		// Sort items within each group by display name.
		foreach (GRAD_ItemGroup group : outGroups)
			m_Sorter.Sort(group.m_aItems);

		return outGroups.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! Case-insensitive substring match of the search term against the display name.
	protected bool MatchesSearch(notnull GRAD_ArsenalItemRecord rec, string search)
	{
		string name = rec.m_sDisplayName;
		name.ToLower();

		string needle = search;
		needle.ToLower();

		return name.Contains(needle);
	}
}

//------------------------------------------------------------------------------------------------
//! One sub-group in the item list: a base-name header and the records (variants) under it.
class GRAD_ItemGroup : Managed
{
	string m_sLabel;
	ref array<ref GRAD_ArsenalItemRecord> m_aItems = {};

	//------------------------------------------------------------------------------------------------
	int GetCount()
	{
		return m_aItems.Count();
	}
}
