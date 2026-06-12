//------------------------------------------------------------------------------------------------
//! Local persistence for named loadouts as JSON files under the game profile directory.
//!
//! Files live in `$profile:GRAD_Loadout/loadouts/<sanitized-name>.json`. The user-facing name is
//! stored inside the file (GRAD_LoadoutData.m_sName); the filename is a sanitized derivative, so
//! two display names that sanitize to the same token are disambiguated by overwrite semantics the
//! caller controls (list shows display names, not filenames).
//!
//! All operations are local/client-side: this is the player's personal loadout library. Applying
//! a loaded loadout to a networked character still goes through the server RPC (P3).
class GRAD_LoadoutStore
{
	protected static const string ROOT_DIR		= "$profile:GRAD_Loadout/";
	protected static const string LOADOUT_DIR	= "$profile:GRAD_Loadout/loadouts/";
	protected static const string EXT			= ".json";

	//------------------------------------------------------------------------------------------------
	//! Ensure the loadout directory exists. Safe to call repeatedly.
	protected static void EnsureDir()
	{
		FileIO.MakeDirectory(ROOT_DIR);
		FileIO.MakeDirectory(LOADOUT_DIR);
	}

	//------------------------------------------------------------------------------------------------
	//! Absolute profile path for a loadout of the given display name.
	protected static string PathFor(string displayName)
	{
		return LOADOUT_DIR + GRAD_CommonUtils.Sanitize(displayName) + EXT;
	}

	//------------------------------------------------------------------------------------------------
	//! Save a loadout under its own name. Overwrites any existing file with the same sanitized name.
	//! Returns true on success.
	static bool Save(notnull GRAD_LoadoutData data)
	{
		if (GRAD_CommonUtils.IsBlank(data.m_sName))
		{
			GRAD_Log.Error("LoadoutStore.Save: loadout has no name");
			return false;
		}

		EnsureDir();
		string path = PathFor(data.m_sName);
		bool ok = data.SaveToFile(path);
		if (ok)
			GRAD_Log.Info(string.Format("LoadoutStore: saved '%1' -> %2", data.m_sName, path));

		return ok;
	}

	//------------------------------------------------------------------------------------------------
	//! Load a loadout by display name. Returns null if missing or unreadable.
	static GRAD_LoadoutData Load(string displayName)
	{
		string path = PathFor(displayName);
		if (!FileIO.FileExists(path))
		{
			GRAD_Log.Warn(string.Format("LoadoutStore.Load: '%1' not found", displayName));
			return null;
		}

		return GRAD_LoadoutData.LoadFromFile(path);
	}

	//------------------------------------------------------------------------------------------------
	//! Delete a loadout by display name. Returns true if a file was removed.
	static bool Delete(string displayName)
	{
		string path = PathFor(displayName);
		if (!FileIO.FileExists(path))
			return false;

		bool ok = FileIO.DeleteFile(path);
		if (ok)
			GRAD_Log.Info(string.Format("LoadoutStore: deleted '%1'", displayName));

		return ok;
	}

	//------------------------------------------------------------------------------------------------
	//! True if a loadout with this display name exists on disk.
	static bool Exists(string displayName)
	{
		return FileIO.FileExists(PathFor(displayName));
	}

	//------------------------------------------------------------------------------------------------
	//! List all saved loadouts as their display names (read from each file's metadata, so the names
	//! shown match what the user typed, not the sanitized filenames). Sorted alphabetically.
	static int ListNames(out notnull array<string> outNames)
	{
		outNames.Clear();
		EnsureDir();

		array<ref SCR_FileInfo> files = SCR_FileIOHelper.GetDirectoryContent(LOADOUT_DIR, EXT);
		if (!files)
			return 0;

		foreach (SCR_FileInfo info : files)
		{
			if (!info)
				continue;

			GRAD_LoadoutData data = GRAD_LoadoutData.LoadFromFile(info.m_sFilePath);
			if (data && !GRAD_CommonUtils.IsBlank(data.m_sName))
				outNames.Insert(data.m_sName);
		}

		outNames.Sort();
		return outNames.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! Load full metadata (name + timestamp + faction) for every saved loadout, for the loadout
	//! manager list. Sorted by name.
	static int ListAll(out notnull array<ref GRAD_LoadoutData> outLoadouts)
	{
		outLoadouts.Clear();
		EnsureDir();

		array<ref SCR_FileInfo> files = SCR_FileIOHelper.GetDirectoryContent(LOADOUT_DIR, EXT);
		if (!files)
			return 0;

		foreach (SCR_FileInfo info : files)
		{
			if (!info)
				continue;

			GRAD_LoadoutData data = GRAD_LoadoutData.LoadFromFile(info.m_sFilePath);
			if (data)
				outLoadouts.Insert(data);
		}

		return outLoadouts.Count();
	}
}
