#ifndef RENDERER_H
#define RENDERER_H

#include "graphics/VertexArray.h"
#include "graphics/IndexBuffer.h"
#include "graphics/Shader.h"

#include <glm/glm.hpp>
#include <iostream>




namespace Cosmic {
    class Renderer {
    public:
        Renderer();
        ~Renderer();
        void clear() const;
        void draw(const VertexArray& va, const IndexBuffer& ib, const Shader& shader) const;


    private:

    };



}
#endif







/*

Documentation:

	Purpose:
        - From the render loop, we want to be able to say "draw a square"
            or something along those lines... we don't want to say
            opengl command x10 to do this...
                - In other words we want to abstract the drawing
                    from the render loop into the Renderer class
                    so that we can just call functions and the drawing
                    will happen for us :)
       
       - The Renderer will call on Shader.h, Camera.h, and any other 
            class needed to accomplish the drawing

       - The Renderer need's a Mesh class, so that the Renderer
            Class can focus on drawing, instead of storing data
       

    A word on glm:
        - glm is essentially a matrix + other stuff library that makes
            life a lot easier...




	Renderer();
		- Creates




*/