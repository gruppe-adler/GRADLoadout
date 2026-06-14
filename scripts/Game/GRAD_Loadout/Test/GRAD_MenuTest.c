//------------------------------------------------------------------------------------------------
//! Console helpers for driving the arsenal menu by hand during development.
//!
//! From the Workbench script console (in PlayMode, with a controlled character):
//!   GRAD_MenuTest.Open();        // open the arsenal targeting your own character
//!   GRAD_MenuTest.SpawnService(); // spawn the arsenal service if no world has placed one yet
class GRAD_MenuTest
{
	//------------------------------------------------------------------------------------------------
	//! Open the arsenal for the local controlled character.
	static void Open()
	{
		IEntity me = SCR_PlayerController.GetLocalControlledEntity();
		if (!me)
		{
			GRAD_Log.Error("MenuTest.Open: no local controlled entity");
			return;
		}

		// Make sure the catalog service exists, else the item browser will be empty.
		if (!GRAD_ArsenalService.GetInstance())
		{
			GRAD_Log.Warn("MenuTest.Open: no GRAD_ArsenalService in world — spawning one");
			SpawnService();
		}

		GRAD_ArsenalMenuContext context = new GRAD_ArsenalMenuContext();
		context.AddTarget(me);
		GRAD_ArsenalMenu menu = GRAD_ArsenalMenu.Open(context);

		if (menu)
			GRAD_Log.Info("MenuTest.Open: arsenal opened");
		else
			GRAD_Log.Error("MenuTest.Open: OpenMenu returned null (menu preset registered?)");
	}

	//------------------------------------------------------------------------------------------------
	//! Spawn a GRAD_ArsenalService entity at the world origin (local), if a world hasn't placed one.
	static void SpawnService()
	{
		if (GRAD_ArsenalService.GetInstance())
		{
			GRAD_Log.Info("MenuTest.SpawnService: service already exists");
			return;
		}

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.MatrixIdentity4(params.Transform);

		GetGame().SpawnEntity(GRAD_ArsenalService, world, params);
		GRAD_Log.Info("MenuTest.SpawnService: spawned arsenal service");
	}

	//------------------------------------------------------------------------------------------------
	//! Find any ChimeraCharacter in the world (player or AI). Used by the debug auto-open to give
	//! the preview a real character to clone when nothing is under local control (GM editor).
	static IEntity FindAnyCharacter()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return null;

		GRAD_FirstCharacterCollector collector = new GRAD_FirstCharacterCollector();
		// Large AABB around origin covering the playable area.
		world.QueryEntitiesByAABB("-100000 -10000 -100000", "100000 10000 100000", collector.OnEntity);
		return collector.m_Found;
	}
}

//------------------------------------------------------------------------------------------------
//! Collects the first ChimeraCharacter encountered during a world entity query.
class GRAD_FirstCharacterCollector
{
	IEntity m_Found;

	//! QueryEntitiesCallback: return false to stop the query once a character is found.
	bool OnEntity(IEntity e)
	{
		if (ChimeraCharacter.Cast(e))
		{
			m_Found = e;
			return false; // stop iterating
		}
		return true; // keep looking
	}
}
