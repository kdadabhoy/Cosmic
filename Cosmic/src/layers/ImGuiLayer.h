#pragma once

#include "core/Layer.h"
#include "events/ApplicationEvent.h"
#include "events/KeyEvent.h"
#include "events/MouseEvent.h"


namespace Cosmic
{
	class ImGuiLayer : public Layer
	{
	public:
		ImGuiLayer();
		~ImGuiLayer();

		void OnAttach()			   override;
		void OnDetach()            override;
		void OnEvent(Event& event) override;

		// These are needed to bridge the Application loop to ImGui
		void Begin();
		void End();

	private:
		bool OnMouseButtonPressed(MouseButtonPressedEvent& e);
		// Add other event handlers (Key, MouseMove, etc.) as needed
	};

}