#include "graphics/VertexArray.h"

#include "graphics/VertexBufferLayout.h"
#include "graphics/Renderer.h"




namespace Cosmic {
	VertexArray::VertexArray()
	{
		glGenVertexArrays(1, &m_RendererID);
	}







	VertexArray::~VertexArray()
	{
		glDeleteVertexArrays(1, &m_RendererID);
	}








	void VertexArray::addBuffer(const VertexBuffer& vb, const VertexBufferLayout& layout)
	{
		this->bind();
		vb.bind();

		const auto& elements = layout.getElements();

		unsigned int offset = 0;
		for (unsigned int i = 0; i < elements.size(); i++) {
			const auto& element = elements[i];
			glEnableVertexAttribArray(i);
			glVertexAttribPointer(i, element.count, element.type, element.normalized, layout.getStride(), (const void*)offset);
			offset += element.count * VertexBufferElement::getSizeOfType(element.type);
		}


		return;
	}








	void VertexArray::bind() const
	{
		glBindVertexArray(m_RendererID);
		return;
	}









	void VertexArray::unBind() const
	{
		glBindVertexArray(0);
		return;
	}


}