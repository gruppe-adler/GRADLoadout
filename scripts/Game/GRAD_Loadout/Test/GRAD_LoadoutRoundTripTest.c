//------------------------------------------------------------------------------------------------
//! P2 acceptance harness: capture -> JSON -> apply round-trip on a single character.
//!
//! Developer probe, not shipped behaviour. Run from the Workbench script console against a spawned
//! vanilla character entity:
//!
//!   GRAD_LoadoutRoundTripTest.Run(myCharacter);
//!
//! Steps:
//!   1. Capture the character's current loadout (full capture).
//!   2. Serialize to a JSON string and log it.
//!   3. Deserialize back and confirm the tree survives the round-trip (node counts match).
//!   4. Apply the deserialized loadout back onto the SAME character (replicated path).
//!   5. Re-capture and compare node counts before/after as a sanity check.
//!
//! Milestone: the character ends up equipped equivalently to how it started. Exact-equality of
//! entity identity is not expected (items are re-spawned); structural equivalence (same prefabs in
//! the same slots) is.
class GRAD_LoadoutRoundTripTest
{
	//------------------------------------------------------------------------------------------------
	static void Run(IEntity character)
	{
		GRAD_Log.SetMinLevel(GRAD_ELogLevel.DEBUG);
		GRAD_Log.Info("==== GRAD_LoadoutRoundTripTest begin ====");

		if (!character)
		{
			GRAD_Log.Error("RoundTrip: character is null");
			return;
		}

		// 1) Capture.
		GRAD_LoadoutData captured = GRAD_LoadoutCapture.Capture(character, "RoundTripTest", true);
		if (!captured)
		{
			GRAD_Log.Error("RoundTrip: capture returned null");
			return;
		}
		int capturedNodes = captured.m_Root.CountSubtree();
		GRAD_Log.Info(string.Format("RoundTrip: captured %1 nodes", capturedNodes));

		// 2) Serialize.
		string json = captured.ToJsonString();
		if (GRAD_CommonUtils.IsBlank(json))
		{
			GRAD_Log.Error("RoundTrip: serialization produced empty string");
			return;
		}
		GRAD_Log.Info(string.Format("RoundTrip: serialized length = %1 chars", json.Length()));
		GRAD_Log.Debug("RoundTrip JSON:");
		GRAD_Log.Debug(json);

		// 3) Deserialize and compare structure.
		GRAD_LoadoutData reloaded = GRAD_LoadoutData.FromJsonString(json);
		if (!reloaded)
		{
			GRAD_Log.Error("RoundTrip: deserialization returned null");
			return;
		}
		int reloadedNodes = reloaded.m_Root.CountSubtree();
		if (reloadedNodes != capturedNodes)
			GRAD_Log.Error(string.Format("RoundTrip: NODE COUNT MISMATCH after JSON round-trip (%1 -> %2)", capturedNodes, reloadedNodes));
		else
			GRAD_Log.Info(string.Format("RoundTrip: JSON round-trip preserved %1 nodes OK", reloadedNodes));

		if (reloaded.m_iSchemaVersion != GRAD_LoadoutData.CURRENT_SCHEMA_VERSION)
			GRAD_Log.Error(string.Format("RoundTrip: schema version mismatch (%1)", reloaded.m_iSchemaVersion));

		// 4) Apply back onto the same character (replicated path; force clears everything first).
		array<IEntity> created = {};
		bool applied = GRAD_LoadoutApply.Apply(character, reloaded, false, true, created);
		if (!applied)
		{
			GRAD_Log.Error("RoundTrip: apply failed");
			return;
		}
		GRAD_Log.Info(string.Format("RoundTrip: apply created %1 entities", created.Count()));

		// 5) Re-capture and compare node counts.
		GRAD_LoadoutData after = GRAD_LoadoutCapture.Capture(character, "RoundTripTest_After", true);
		if (after)
		{
			int afterNodes = after.m_Root.CountSubtree();
			if (afterNodes == capturedNodes)
				GRAD_Log.Info(string.Format("RoundTrip: re-equip MATCHES original node count (%1) ✓", afterNodes));
			else
				GRAD_Log.Warn(string.Format("RoundTrip: re-equip node count %1 differs from original %2 (inspect slots)", afterNodes, capturedNodes));
		}

		GRAD_Log.Info("==== GRAD_LoadoutRoundTripTest end ====");
	}
}
