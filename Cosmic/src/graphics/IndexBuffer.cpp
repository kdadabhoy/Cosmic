#include "graphics/IndexBuffer.h"

#include "graphics/Renderer.h"





namespace Cosmic {
    IndexBuffer::IndexBuffer(const unsigned int* data, unsigned int count)
        : m_Count(count)
    {
        sizeof(unsigned int) == sizeof(GLuint);

        glGenBuffers(1, &m_RendererID);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(unsigned int), data, GL_STATIC_DRAW);
    }






    IndexBuffer::~IndexBuffer()
    {
        glDeleteBuffers(1, &m_RendererID);
    }





    void IndexBuffer::bind() const
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
    }




    void IndexBuffer::unBind() const
    {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

}