//------------------------------------------------------------------------------------------------
//! A complete saved loadout: metadata plus the recursive entry tree describing every item.
//!
//! This is the unit of persistence (one JSON file) and the unit of transmission (one serialized
//! string over RPC). It owns serialization to/from both strings and files via the engine's JSON
//! serialization contexts; the recursive walk over GRAD_LoadoutEntry happens automatically.
//!
//! Schema versioning: m_iSchemaVersion is written on save and checked on load. A loaded file with
//! a newer version than this build understands is rejected (forward-incompatible); older versions
//! may be migrated here as the schema evolves.
class GRAD_LoadoutData
{
	//! Current schema version this build writes and is the maximum it can read.
	static const int CURRENT_SCHEMA_VERSION = 1;

	//! Schema version of this instance (set on capture/load).
	int m_iSchemaVersion;

	//! User-facing loadout name (original, unsanitized — the filename is sanitized separately).
	string m_sName;

	//! Unix timestamp (UTC seconds) of when this loadout was captured.
	int m_iCreatedUnix;

	//! Optional faction key the loadout was captured from, for UI grouping/filtering. May be empty.
	string m_sFactionKey;

	//! Root of the entry tree. Its children are the character's top-level equipment.
	ref GRAD_LoadoutEntry m_Root;

	//------------------------------------------------------------------------------------------------
	void GRAD_LoadoutData()
	{
		m_iSchemaVersion = CURRENT_SCHEMA_VERSION;
		m_iCreatedUnix = GRAD_CommonUtils.GetUnixTime();
		m_Root = new GRAD_LoadoutEntry();
	}

	//------------------------------------------------------------------------------------------------
	//! Serialize to a JSON string (used for RPC transmission and as the file body).
	//! Returns empty string on failure.
	string ToJsonString()
	{
		SCR_JsonSaveContext ctx = new SCR_JsonSaveContext();
		WriteInto(ctx);
		return ctx.ExportToString();
	}

	//------------------------------------------------------------------------------------------------
	//! Save to a file at the given absolute profile path. Returns true on success.
	bool SaveToFile(string filePath)
	{
		SCR_JsonSaveContext ctx = new SCR_JsonSaveContext();
		WriteInto(ctx);

		bool ok = ctx.SaveToFile(filePath);
		if (!ok)
			GRAD_Log.Error(string.Format("SaveToFile failed: '%1'", filePath));

		return ok;
	}

	//------------------------------------------------------------------------------------------------
	//! Common write path shared by ToJsonString and SaveToFile.
	protected void WriteInto(notnull SCR_JsonSaveContext ctx)
	{
		ctx.WriteValue("schemaVersion", m_iSchemaVersion);
		ctx.WriteValue("name", m_sName);
		ctx.WriteValue("createdUnix", m_iCreatedUnix);
		ctx.WriteValue("factionKey", m_sFactionKey);
		ctx.WriteValue("root", m_Root);
	}

	//------------------------------------------------------------------------------------------------
	//! Build an instance from a JSON string. Returns null on parse failure or unsupported version.
	static GRAD_LoadoutData FromJsonString(string json)
	{
		if (GRAD_CommonUtils.IsBlank(json))
		{
			GRAD_Log.Warn("FromJsonString: empty input");
			return null;
		}

		SCR_JsonLoadContext ctx = new SCR_JsonLoadContext();
		if (!ctx.ImportFromString(json))
		{
			GRAD_Log.Error("FromJsonString: JSON parse failed");
			return null;
		}

		return ReadFrom(ctx);
	}

	//------------------------------------------------------------------------------------------------
	//! Build an instance from a file. Returns null on read/parse failure or unsupported version.
	static GRAD_LoadoutData LoadFromFile(string filePath)
	{
		SCR_JsonLoadContext ctx = new SCR_JsonLoadContext();
		if (!ctx.LoadFromFile(filePath))
		{
			GRAD_Log.Warn(string.Format("LoadFromFile: could not read '%1'", filePath));
			return null;
		}

		return ReadFrom(ctx);
	}

	//------------------------------------------------------------------------------------------------
	//! Common read path. Enforces the schema-version ceiling and ensures a non-null root.
	protected static GRAD_LoadoutData ReadFrom(notnull SCR_JsonLoadContext ctx)
	{
		GRAD_LoadoutData data = new GRAD_LoadoutData();

		ctx.ReadValueDefault("schemaVersion", data.m_iSchemaVersion, 0);
		ctx.ReadValueDefault("name", data.m_sName, string.Empty);
		ctx.ReadValueDefault("createdUnix", data.m_iCreatedUnix, 0);
		ctx.ReadValueDefault("factionKey", data.m_sFactionKey, string.Empty);

		if (data.m_iSchemaVersion > CURRENT_SCHEMA_VERSION)
		{
			GRAD_Log.Error(string.Format("ReadFrom: loadout schema v%1 is newer than supported v%2 — refusing to load",
				data.m_iSchemaVersion, CURRENT_SCHEMA_VERSION));
			return null;
		}

		data.m_Root = new GRAD_LoadoutEntry();
		ctx.ReadValue("root", data.m_Root);

		// Future migrations from older schema versions are applied here.
		// (none yet — v1 is the first format)

		return data;
	}
}
