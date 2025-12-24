#ifndef SHADER_H
#define SHADER_H

#include <string>
#include <glad/glad.h>
#include <unordered_map>
#include <glm/glm.hpp> // Add this for mat4

namespace Cosmic {
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


        // Uniforms
        void setUniform4f(const std::string& name, float v0, float v1, float v2, float v3);
        void setUniformMat4f(const std::string& name, const glm::mat4& matrix);

    private:
        unsigned int rendererID;
        std::string vertFilePath;
        std::string fragFilePath;


        // Reading Shader Files
        std::string parseShader(const std::string& path);


        // Shader Creation
        unsigned int compileShader(unsigned int type, const std::string& source);
        unsigned int createShader(const std::string& vertexShader, const std::string& fragmentShader);

        // Uniforms
        std::unordered_map<std::string, int> uniformLocationCache;
        int getUniformLocation(const std::string& name);


    };

}

#endif