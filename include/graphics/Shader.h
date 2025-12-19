#ifndef SHADER_H
#define SHADER_H

#include <string>
#include <glad/glad.h>



struct ShaderProgramSource
{
    std::string VertexSource;
    std::string FragmentSource;
};



class Shader {
public:
    Shader(const std::string& vertexFilePath, const std::string& fragmentFilePath);
    ~Shader();


    // Wrappers for OpenGL 
    void bind() const;       // glUseProgram(rendererID)
    void unBind() const;     // glUseProgram(0)


private:
    unsigned int rendererID;
    std::string vertFilePath;
    std::string fragFilePath;

    std::string parseShader(const std::string& path);


    unsigned int compileShader(unsigned int type, const std::string& source);
    unsigned int createShader(const std::string& vertexShader, const std::string& fragmentShader);


};

#endif