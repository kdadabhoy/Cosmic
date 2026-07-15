// Light2DRenderer.cpp — Phase 27 X5. See Light2DRenderer.h.

#include "renderer/Light2DRenderer.h"
#include "renderer/RenderCommand.h"
#include "graphics/FrameBuffer.h"
#include "graphics/Shader.h"

namespace Cosmic
{
	namespace
	{
		struct State
		{
			Ref<Shader>      Light;   // Light2D.glsl (radial additive quad)
			Ref<Shader>      Blit;    // BlitCopy.glsl (multiplicative composite)
			Ref<FrameBuffer> Fbo;     // half-res RGBA16F light buffer (color only)
			uint32_t         W = 0, H = 0;
			bool             Tried = false;
		};

		State& S() { static State s; return s; }

		bool Ensure(uint32_t halfW, uint32_t halfH)
		{
			State& s = S();
			if (!s.Tried)
			{
				s.Tried = true;
				s.Light = Shader::Create("assets/shaders/Light2D.glsl");
				s.Blit  = Shader::Create("assets/shaders/BlitCopy.glsl");
			}
			if (!s.Light || !s.Blit)
				return false;

			if (!s.Fbo)
			{
				// Deviation from the plan's R11G11B10F: RGBA16F is already a
				// conformance-clean, LINEAR-filtered HDR format; the extra alpha
				// channel at half-res is negligible and it needs no new GL format.
				FramebufferSpecification spec;
				spec.Width  = halfW;
				spec.Height = halfH;
				spec.Attachments = { FramebufferTextureFormat::RGBA16F };
				s.Fbo = FrameBuffer::Create(spec);
				s.W = halfW; s.H = halfH;
			}
			else if (s.W != halfW || s.H != halfH)
			{
				s.Fbo->Resize(halfW, halfH);
				s.W = halfW; s.H = halfH;
			}
			return s.Fbo != nullptr;
		}
	}

	void Light2DRenderer::Composite(const std::vector<Light>& lights, const glm::vec3& ambient,
	                                const glm::mat4& viewProjection, uint32_t targetW, uint32_t targetH)
	{
		if (targetW == 0 || targetH == 0)
			return;
		const uint32_t halfW = (targetW + 1) / 2;
		const uint32_t halfH = (targetH + 1) / 2;
		if (!Ensure(halfW, halfH))
			return;   // headless / shader load failure — leave the scene untouched
		State& s = S();

		const uint32_t prevFbo = RenderCommand::GetBoundFramebuffer();

		// 1) Accumulate the lights into the half-res buffer, cleared to ambient.
		s.Fbo->Bind();
		RenderCommand::SetViewport(0, 0, halfW, halfH);
		RenderCommand::SetClearColor({ ambient.r, ambient.g, ambient.b, 1.0f });
		RenderCommand::Clear();
		RenderCommand::SetDepthTest(false);
		RenderCommand::SetDepthWrite(false);
		RenderCommand::SetBlendMode(RendererAPI::BlendMode::Additive);

		s.Light->Bind();
		s.Light->SetMat4("u_ViewProjection", viewProjection);
		for (const Light& l : lights)
		{
			s.Light->SetFloat2("u_Center", l.Center);
			s.Light->SetFloat("u_Radius", l.Radius);
			s.Light->SetFloat3("u_Color", l.Color);
			s.Light->SetFloat("u_Intensity", l.Intensity);
			s.Light->SetFloat("u_Falloff", l.Falloff);
			RenderCommand::DrawArrays(RenderCommand::PrimitiveTopology::Triangles, 0, 6);
		}

		// 2) Multiply the (bilinearly upsampled) light buffer over the scene target.
		RenderCommand::BindFramebufferHandle(prevFbo);
		RenderCommand::SetViewport(0, 0, targetW, targetH);
		RenderCommand::SetBlendMode(RendererAPI::BlendMode::Multiply);
		s.Blit->Bind();
		RenderCommand::BindTextureSlot(0, s.Fbo->GetColorAttachmentRendererID(0));
		s.Blit->SetInt("u_Source", 0);
		RenderCommand::DrawArrays(RenderCommand::PrimitiveTopology::Triangles, 0, 3);

		// 3) Restore the engine defaults the sprite pass leaves on exit (depth
		//    test + write on, straight-alpha blend).
		RenderCommand::SetBlendMode(RendererAPI::BlendMode::Alpha);
		RenderCommand::SetDepthTest(true);
		RenderCommand::SetDepthWrite(true);
	}

	void Light2DRenderer::Shutdown()
	{
		State& s = S();
		s.Light.reset();
		s.Blit.reset();
		s.Fbo.reset();
		s.W = s.H = 0;
		s.Tried = false;
	}
}
