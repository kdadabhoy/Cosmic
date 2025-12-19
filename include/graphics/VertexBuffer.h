#ifndef VERTEXBUFFER_H
#define VERTEXBUFFER_H



class VertexBuffer {

public:
	VertexBuffer(const void* data, unsigned int size);
	~VertexBuffer();



	// OpenGL Wrappers
	void bind() const;   // glBindBuffer()
	void unBind() const; // glBindBuffer( , 0)


private:
	unsigned int rendererID;
};


#endif