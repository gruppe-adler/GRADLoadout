//------------------------------------------------------------------------------------------------
//! P3 manual test notes + a tiny callback implementation for exercising the request flow.
//!
//! These are driven by hand in the Workbench MP peer tool (host + client), not automated:
//!
//!   1. Host + 1 client. Each player controls their own character.
//!   2. CLIENT applies a loadout to its OWN character:
//!        GRAD_LoadoutManagerComponent.GetLocal().ApplyLoadout(
//!            GRAD_LoadoutManagerComponent.GetEntityRplId(myChar), data);
//!      EXPECT: applied (ownership grants permission). ✔
//!   3. CLIENT applies to a FOREIGN character (the host's):
//!      EXPECT: server logs "DENIED apply", nothing changes. ✘ (correctly blocked)
//!   4. Make the client a Game Master, repeat (3):
//!      EXPECT: applied (GM authority). ✔
//!   5. Request flow: call RequestLoadout with a GRAD_TestLoadoutCallback and confirm the callback
//!      fires with success+data for a permitted target, and with failure for a denied target or
//!      after the ~6s timeout if the server never answers.
//!
//! Payload-size note (D4): log the serialized length printed by WarnIfOversized for a heavy
//! loadout (full kit + many magazines) to find whether chunking is needed against the engine's
//! RPC string cap. Record the finding in docs/DECISIONS.md D4.
class GRAD_TestLoadoutCallback : GRAD_LoadoutRequestCallback
{
	override void OnLoadoutResponse(bool success, GRAD_LoadoutData data)
	{
		if (!success || !data)
		{
			GRAD_Log.Warn("GRAD_TestLoadoutCallback: request FAILED (denied/timeout/parse)");
			return;
		}

		GRAD_Log.Info(string.Format("GRAD_TestLoadoutCallback: received '%1' with %2 nodes",
			data.m_sName, data.m_Root.CountSubtree() - 1));
	}
}
