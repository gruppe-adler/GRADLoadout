//------------------------------------------------------------------------------------------------
//! Small grab-bag of stateless helpers used across GRAD_Loadout.
//!
//! Intentionally minimal: only helpers the arsenal actually consumes live here. Anything
//! inventory-specific belongs in GRAD_InventoryLib instead.
class GRAD_CommonUtils
{
	//------------------------------------------------------------------------------------------------
	//! Current Unix time in seconds (UTC). Used to stamp saved loadout files.
	static int GetUnixTime()
	{
		return System.GetUnixTime();
	}

	//------------------------------------------------------------------------------------------------
	//! True if the string is null or contains only whitespace.
	static bool IsBlank(string s)
	{
		if (!s)
			return true;

		return s.Trim().IsEmpty();
	}

	//! Characters permitted in a sanitized filename token (whitelist).
	protected static const string SAFE_CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";

	//------------------------------------------------------------------------------------------------
	//! Reduce an arbitrary user-supplied name to a filesystem-safe token.
	//!
	//! Keeps ASCII letters, digits, '-' and '_'; every disallowed character is dropped. Result is
	//! lower-cased and length-capped. Used when turning a loadout display name into a filename.
	//! The original display name is stored inside the file, so collapsing here only affects the
	//! filename, never what the user sees.
	static string Sanitize(string name, int maxLength = 64)
	{
		if (IsBlank(name))
			return "unnamed";

		// Filter() with useCharactersAsBlacklist = false keeps only whitelisted characters.
		string result = SCR_StringHelper.Filter(name.Trim(), SAFE_CHARS, false);
		result.ToLower();

		if (result.Length() > maxLength)
			result = result.Substring(0, maxLength);

		if (IsBlank(result))
			return "unnamed";

		return result;
	}
}
