#include "graphics/Renderer.h"

#include <glad/glad.h>

#include <iostream>

namespace Cosmic {

    Renderer::Renderer() {}




    Renderer::~Renderer()
    {
        // Shader cleanup
    }






    void Renderer::clear() const
    {
        glClear(GL_COLOR_BUFFER_BIT);
    }








    void Renderer::draw(const VertexArray& va, const IndexBuffer& ib, const Shader& shader) const
    {
        shader.bind();
        va.bind();
        ib.bind();
        glDrawElements(GL_TRIANGLES, ib.getCount(), GL_UNSIGNED_INT, nullptr);
    }


}