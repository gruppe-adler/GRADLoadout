//------------------------------------------------------------------------------------------------
//! Tiny on-screen "Preloading arsenal…" indicator with a progress bar, shown while the catalog index
//! builds and hidden once it completes. Built entirely in script (no layout resource to register):
//! a bottom-left frame holding a label, a bar track, and a bar fill whose width tracks progress.
//!
//! Driven by GRAD_ArsenalService each frame via Update(progress). Call Destroy() when done / on
//! service teardown. Self-contained so it works during preload, before any menu exists.
class GRAD_PreloadIndicator
{
	protected Widget m_wRoot;
	protected TextWidget m_wLabel;
	protected Widget m_wBarTrack;
	protected Widget m_wBarFill;

	protected const int BAR_WIDTH = 300;		//!< px width of the full bar track
	protected const int PANEL_WIDTH = 320;
	protected bool m_bDone;

	//------------------------------------------------------------------------------------------------
	void GRAD_PreloadIndicator()
	{
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;

		// Root frame, bottom-left, ignores cursor so it never eats clicks.
		m_wRoot = ws.CreateWidget(WidgetType.FrameWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(Color.WHITE), 0);
		if (!m_wRoot)
			return;
		m_wRoot.SetName("GRAD_PreloadIndicator");
		FrameSlot.SetAnchorMin(m_wRoot, 0, 1);
		FrameSlot.SetAnchorMax(m_wRoot, 0, 1);
		FrameSlot.SetSizeToContent(m_wRoot, false);
		FrameSlot.SetSize(m_wRoot, PANEL_WIDTH, 48);
		FrameSlot.SetOffsets(m_wRoot, 24, -72, 0, 0);

		// Dark background panel.
		Widget bg = ws.CreateWidget(WidgetType.ImageWidgetTypeID, WidgetFlags.VISIBLE, Color.FromInt(Color.WHITE), 0, m_wRoot);
		ImageWidget bgImg = ImageWidget.Cast(bg);
		if (bgImg)
		{
			bgImg.SetColor(new Color(0.02, 0.02, 0.03, 0.85));
			FrameSlot.SetAnchorMin(bg, 0, 0);
			FrameSlot.SetAnchorMax(bg, 1, 1);
			FrameSlot.SetOffsets(bg, 0, 0, 0, 0);
		}

		// Label.
		Widget label = ws.CreateWidget(WidgetType.TextWidgetTypeID, WidgetFlags.VISIBLE, Color.FromInt(Color.WHITE), 0, m_wRoot);
		m_wLabel = TextWidget.Cast(label);
		if (m_wLabel)
		{
			m_wLabel.SetText("Preloading arsenal…");
			m_wLabel.SetColor(new Color(0.9, 0.9, 0.95, 1));
			FrameSlot.SetAnchorMin(label, 0, 0);
			FrameSlot.SetAnchorMax(label, 1, 0);
			FrameSlot.SetSize(label, 0, 20);
			FrameSlot.SetOffsets(label, 12, 6, 12, 0);
		}

		// Bar track.
		m_wBarTrack = ws.CreateWidget(WidgetType.ImageWidgetTypeID, WidgetFlags.VISIBLE, Color.FromInt(Color.WHITE), 0, m_wRoot);
		ImageWidget trackImg = ImageWidget.Cast(m_wBarTrack);
		if (trackImg)
		{
			trackImg.SetColor(new Color(0.15, 0.15, 0.18, 1));
			FrameSlot.SetAnchorMin(m_wBarTrack, 0, 1);
			FrameSlot.SetAnchorMax(m_wBarTrack, 0, 1);
			FrameSlot.SetSize(m_wBarTrack, BAR_WIDTH, 8);
			FrameSlot.SetOffsets(m_wBarTrack, 12, -16, 0, 0);
		}

		// Bar fill (width driven by progress).
		m_wBarFill = ws.CreateWidget(WidgetType.ImageWidgetTypeID, WidgetFlags.VISIBLE, Color.FromInt(Color.WHITE), 0, m_wRoot);
		ImageWidget fillImg = ImageWidget.Cast(m_wBarFill);
		if (fillImg)
		{
			fillImg.SetColor(new Color(0.35, 0.7, 1, 1));
			FrameSlot.SetAnchorMin(m_wBarFill, 0, 1);
			FrameSlot.SetAnchorMax(m_wBarFill, 0, 1);
			FrameSlot.SetSize(m_wBarFill, 0, 8);
			FrameSlot.SetOffsets(m_wBarFill, 12, -16, 0, 0);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Update the bar to `progress` (0..1). At >= 1 the indicator hides itself (once).
	void Update(float progress)
	{
		if (m_bDone)
			return;

		if (m_wBarFill)
			FrameSlot.SetSize(m_wBarFill, BAR_WIDTH * Math.Clamp(progress, 0, 1), 8);

		if (m_wLabel)
			m_wLabel.SetText(string.Format("Preloading arsenal… %1%%", Math.Round(progress * 100)));

		if (progress >= 1.0)
			Hide();
	}

	//------------------------------------------------------------------------------------------------
	protected void Hide()
	{
		m_bDone = true;
		if (m_wRoot)
			m_wRoot.SetVisible(false);
	}

	//------------------------------------------------------------------------------------------------
	void Destroy()
	{
		if (m_wRoot)
		{
			m_wRoot.RemoveFromHierarchy();
			m_wRoot = null;
		}
	}
}
