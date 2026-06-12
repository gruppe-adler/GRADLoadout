//------------------------------------------------------------------------------------------------
//! Severity levels for GRAD_Loadout logging.
enum GRAD_ELogLevel
{
	DEBUG,		//!< Verbose, developer-only tracing.
	INFO,		//!< Normal operational messages.
	WARNING,	//!< Recoverable problems (e.g. a missing prefab was skipped).
	ERROR		//!< Failures that abort the current operation.
}

//------------------------------------------------------------------------------------------------
//! Thin logging wrapper with a consistent prefix and a runtime-adjustable verbosity threshold.
//!
//! All GRAD_Loadout code logs through this rather than calling Print() directly, so the prefix
//! and minimum level are controlled in one place. No third-party identifiers appear in output.
class GRAD_Log
{
	protected static const string PREFIX = "[GRAD_Loadout] ";

	//! Messages below this level are suppressed. Default INFO; lower to DEBUG while developing.
	protected static GRAD_ELogLevel s_eMinLevel = GRAD_ELogLevel.INFO;

	//------------------------------------------------------------------------------------------------
	//! Set the minimum level that will be printed.
	static void SetMinLevel(GRAD_ELogLevel level)
	{
		s_eMinLevel = level;
	}

	//------------------------------------------------------------------------------------------------
	static GRAD_ELogLevel GetMinLevel()
	{
		return s_eMinLevel;
	}

	//------------------------------------------------------------------------------------------------
	//! Core log entry point. Maps our level onto the engine's LogLevel so messages are coloured
	//! and filtered correctly in the Workbench console.
	static void Log(string message, GRAD_ELogLevel level = GRAD_ELogLevel.INFO)
	{
		if (level < s_eMinLevel)
			return;

		string line = PREFIX + message;

		switch (level)
		{
			case GRAD_ELogLevel.DEBUG:		Print(line, LogLevel.VERBOSE); break;
			case GRAD_ELogLevel.INFO:		Print(line, LogLevel.NORMAL); break;
			case GRAD_ELogLevel.WARNING:	Print(line, LogLevel.WARNING); break;
			case GRAD_ELogLevel.ERROR:		Print(line, LogLevel.ERROR); break;
		}
	}

	//------------------------------------------------------------------------------------------------
	static void Debug(string message)	{ Log(message, GRAD_ELogLevel.DEBUG); }
	static void Info(string message)	{ Log(message, GRAD_ELogLevel.INFO); }
	static void Warn(string message)	{ Log(message, GRAD_ELogLevel.WARNING); }
	static void Error(string message)	{ Log(message, GRAD_ELogLevel.ERROR); }
}
