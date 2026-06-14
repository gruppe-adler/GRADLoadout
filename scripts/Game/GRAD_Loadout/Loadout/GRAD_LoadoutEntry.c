//------------------------------------------------------------------------------------------------
//! One node in a loadout tree: a single entity (item, container, weapon, attachment) plus the
//! slot-addressing data needed to re-create it under its parent, and its own child entries.
//!
//! The structure is recursive: a vest entry carries magazine entries as children; a rifle entry
//! carries optic/muzzle entries as children. The root entry represents the character itself (no
//! prefab spawned for it — its children are the top-level equipment).
//!
//! Addressing scheme (see docs/DECISIONS.md D3): each entry records the index of the slot it
//! occupies within its parent storage, plus the parent storage's class name. Index drives
//! placement; the class name is a sanity tag used to detect a mismatched layout and to pick the
//! right storage when a parent has several.
//!
//! Serialization: the engine's container serialization (SCR_Json*Context) reflects over the
//! public members of this class automatically, recursing into nested objects and arrays of
//! `ref` objects. We therefore expose plain public members and do NOT hand-roll per-node
//! Write/Read calls — the top-level context.WriteValue("root", entry) walks the whole tree.
//! Field names are original to this project.
//!
//! VERIFY IN WORKBENCH (P2): confirm SCR_JsonSaveContext.WriteValue auto-serializes these public
//! members and the array<ref GRAD_LoadoutEntry> recursively without per-class hooks. If member
//! auto-reflection turns out to require [BaseContainerProps()]/attributes, add them here.
class GRAD_LoadoutEntry
{
	//! ResourceName of the prefab this entry spawns. Empty only for the synthetic root entry.
	string m_sPrefab;

	//! Index of the slot within the parent storage that this entry occupies. -1 means "any free
	//! slot" (used for free-insertion storages where exact index is not meaningful).
	int m_iSlotIndex;

	//! Class name of the parent storage component (e.g. "SCR_WeaponAttachmentsStorageComponent").
	//! A sanity/disambiguation tag, not a hard requirement during apply.
	string m_sStorageClass;

	//! Stack quantity for stackable items (ammo, etc.). 1 for non-stackables.
	int m_iQuantity;

	//! Child entries nested inside this entity's own storages.
	ref array<ref GRAD_LoadoutEntry> m_aChildren;

	//------------------------------------------------------------------------------------------------
	//! Zero-argument constructor. REQUIRED by the engine container serializer (SCR_Json*Context):
	//! it instantiates each node via a no-arg constructor before reading members. A single
	//! constructor with all-default args does NOT satisfy this — it must be a genuine zero-arg
	//! overload. Without it, deserialization fails with "needs to have a constructor with zero
	//! arguments when using serialization" and the loadout comes back empty.
	void GRAD_LoadoutEntry()
	{
		m_sPrefab = string.Empty;
		m_iSlotIndex = -1;
		m_sStorageClass = string.Empty;
		m_iQuantity = 1;
		m_aChildren = new array<ref GRAD_LoadoutEntry>();
	}

	//------------------------------------------------------------------------------------------------
	//! Convenience factory for constructing a populated entry in code (capture/click paths).
	static GRAD_LoadoutEntry Create(string prefab, int slotIndex = -1, string storageClass = string.Empty, int quantity = 1)
	{
		GRAD_LoadoutEntry e = new GRAD_LoadoutEntry();
		e.m_sPrefab = prefab;
		e.m_iSlotIndex = slotIndex;
		e.m_sStorageClass = storageClass;
		e.m_iQuantity = quantity;
		return e;
	}

	//------------------------------------------------------------------------------------------------
	void AddChild(GRAD_LoadoutEntry child)
	{
		if (!child)
			return;

		if (!m_aChildren)
			m_aChildren = new array<ref GRAD_LoadoutEntry>();

		m_aChildren.Insert(child);
	}

	//------------------------------------------------------------------------------------------------
	int GetChildCount()
	{
		if (!m_aChildren)
			return 0;

		return m_aChildren.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! Total number of nodes in this subtree including self. Used for diagnostics and as a hint for
	//! RPC payload sizing (see P3 chunking decision, DECISIONS.md D4).
	int CountSubtree()
	{
		int total = 1;
		if (m_aChildren)
		{
			foreach (GRAD_LoadoutEntry child : m_aChildren)
			{
				if (child)
					total += child.CountSubtree();
			}
		}
		return total;
	}
}
