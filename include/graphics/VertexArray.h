#ifndef VERTEXARRAY_H
#define VERTEXARRAY_H


#include "graphics/VertexBuffer.h"

class VertexBufferLayout;



class VertexArray {
public:
	VertexArray();
	~VertexArray();

	void addBuffer(const VertexBuffer& vb, const VertexBufferLayout& layout);

	void bind() const;
	void unBind() const;


private:
	unsigned int m_RendererID;

};


#endif