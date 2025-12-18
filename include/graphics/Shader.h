#ifndef SHADER_H
#define SHADER_H

#include <string>
#include <glad/glad.h>



class Shader {
public:
    Shader();
    ~Shader();


private:
    unsigned int ID;
    std::string readFile(const std::string& path);
};

#endif