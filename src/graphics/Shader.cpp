#include "graphics/Shader.h"


#include <iostream>
#include <fstream>
#include <sstream>





Shader::Shader(const std::string& vertexFilePath, const std::string& fragmentFilePath)
	: vertFilePath(vertexFilePath), fragFilePath(fragmentFilePath), rendererID(0)
{
	ShaderProgramSource source;
	source.VertexSource = parseShader(vertFilePath);
	source.FragmentSource = parseShader(fragmentFilePath);
	// create shader
	rendererID = createShader(source.VertexSource, source.FragmentSource);
}








Shader::~Shader()
{
	// Shader cleanup
	glDeleteProgram(rendererID);
}









std::string Shader::parseShader(const std::string& filePath)
{
	std::ifstream stream(filePath);

	if (!stream.is_open()) {
		std::cerr << "Error: Could not open shader file: " << filePath << std::endl;
		return "";
	}

    std::string line;
	std::stringstream ss;

	while (getline(stream, line)) {
        ss << line << '\n';
	}

    return ss.str();
}










unsigned int Shader::compileShader(unsigned int type, const std::string& source)
{
	unsigned int id = glCreateShader(type);
	const char* src = source.c_str();
	glShaderSource(id, 1, &src, nullptr);
	glCompileShader(id);



	int result;
	glGetShaderiv(id, GL_COMPILE_STATUS, &result);

	if (result == GL_FALSE) {
		int length;
		glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);
		char* message = (char*)malloc(length * sizeof(char));
		glGetShaderInfoLog(id, length, &length, message);
		printf("Failed to compile %s shader!\n", (type == GL_VERTEX_SHADER) ? "vertex" : "fragment");
		std::cout << message << std::endl;
		glDeleteShader(id);
		return 0;
	}



	return id;
}











unsigned int Shader::createShader(const std::string& vertexShader, const std::string& fragmentShader)
{
	unsigned int program = glCreateProgram();
	unsigned int vs = compileShader(GL_VERTEX_SHADER, vertexShader);
	unsigned int fs = compileShader(GL_FRAGMENT_SHADER, fragmentShader);

	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	glValidateProgram(program);

	glDeleteShader(vs);
	glDeleteShader(fs);

	return program;
}









void Shader::bind() const
{
	glUseProgram(rendererID);
	return;
}









void Shader::unBind() const
{
	glUseProgram(0);
	return;
}