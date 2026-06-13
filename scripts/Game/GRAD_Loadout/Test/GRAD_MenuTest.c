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
}
