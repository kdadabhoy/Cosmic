#pragma once

#include "graphics/VertexBuffer.h"


namespace Cosmic
{
	class VertexBufferLayout;


	class VertexArray
	{
	public:
		VertexArray();
		~VertexArray();

		void addBuffer(const VertexBuffer& vb, const VertexBufferLayout& layout);

		void bind() const;
		void unBind() const;


	private:
		unsigned int m_RendererID;

	};
}
