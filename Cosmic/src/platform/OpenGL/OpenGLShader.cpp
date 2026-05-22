// Resolve Windows/GLAD macro collision
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

#ifdef APIENTRY
    #undef APIENTRY
#endif

#include "platform/opengl/OpenGLShader.h"
#include <vector>
#include <glm/gtc/type_ptr.hpp>
#include "core/Log.h"

#include <filesystem>


namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * ShaderTypeFromString
	 * * INTERNAL HELPER: Converts a string identifier (e.g., "vertex") into
	 * the corresponding OpenGL shader type constant.
	 */
	static GLenum ShaderTypeFromString(const std::string& type)
	{
		if (type == "vertex") return GL_VERTEX_SHADER;
		if (type == "fragment" || type == "pixel") return GL_FRAGMENT_SHADER;

		CS_CORE_ERROR("Unknown shader type '{0}'!", type);
		return 0;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OpenGLShader Constructor
	 * * Orchestrates the shader build pipeline:
	 * 1. Reads the raw text file from disk.
	 * 2. Pre-processes the source to separate different shader stages.
	 * 3. Compiles and links the source into a usable GPU program.
	 */
	OpenGLShader::OpenGLShader(const std::string& filepath)
	{
		std::string source = ReadFile(filepath);
		auto shaderSources = PreProcess(source);

		// Fix: Extract and store the filename in m_Name before compiling
		std::filesystem::path path = filepath;
		m_Name = path.stem().string();

		Compile(shaderSources);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OpenGLShader Destructor
	 * * Cleans up the GPU program resource once the Shader instance is destroyed.
	 */
	OpenGLShader::~OpenGLShader()
	{
		glDeleteProgram(m_RendererID);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * ReadFile
	 * * Utility function to load GLSL source code from a file into a string.
	 * It reads in binary mode to ensure file size calculations are accurate.
	 */
	std::string OpenGLShader::ReadFile(const std::string& filepath)
	{
		std::string result;
		std::ifstream in(filepath, std::ios::in | std::ios::binary);
		if (in)
		{
			in.seekg(0, std::ios::end);
			result.resize(in.tellg());
			in.seekg(0, std::ios::beg);
			in.read(&result[0], result.size());
			in.close();
		}
		else
		{
			CS_CORE_ERROR("Could not open file '{0}'", filepath);
		}
		return result;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * PreProcess
	 * * ASSET WORKFLOW: Parses a single GLSL file and splits it into multiple
	 * sources based on the "#type" directive. This allows Cosmic to keep
	 * vertex and fragment logic in one cohesive asset.
	 */
	std::unordered_map<GLenum, std::string> OpenGLShader::PreProcess(const std::string& source)
	{
		std::unordered_map<GLenum, std::string> shaderSources;

		const char* typeToken = "#type";
		size_t typeTokenLength = strlen(typeToken);
		size_t pos = source.find(typeToken, 0);

		while (pos != std::string::npos)
		{
			size_t eol = source.find_first_of("\r\n", pos);
			size_t begin = pos + typeTokenLength + 1;
			std::string type = source.substr(begin, eol - begin);

			size_t nextLinePos = source.find_first_not_of("\r\n", eol);
			pos = source.find(typeToken, nextLinePos);

			std::string rawSource = (pos == std::string::npos) ?
				source.substr(nextLinePos) : source.substr(nextLinePos, pos - nextLinePos);

			// --- SAFE INJECTION GATEWAY ---
			std::string enginePreamble = "";

			// Only inject declarations if the shader source hasn't manually declared them yet
			if (rawSource.find("u_ViewProjection") == std::string::npos)
				enginePreamble += "uniform mat4 u_ViewProjection;\n";

			if (rawSource.find("u_Time") == std::string::npos && rawSource.find("iTime") == std::string::npos)
				enginePreamble += "uniform float u_Time;\n#define iTime u_Time\n";

			if (rawSource.find("u_ViewportSize") == std::string::npos && rawSource.find("iResolution") == std::string::npos)
				enginePreamble += "uniform vec2 u_ViewportSize;\n#define iResolution vec3(u_ViewportSize, 1.0)\n";

			// Find the line ending of '#version' to safely append downstream strings
			size_t versionPos = rawSource.find("#version");
			if (versionPos != std::string::npos)
			{
				size_t versionEol = rawSource.find_first_of("\r\n", versionPos);

				// Advance past whichever line ending style (\r\n or \n) is actively being read
				while (rawSource[versionEol] == '\r' || rawSource[versionEol] == '\n')
				{
					versionEol++;
				}
				rawSource.insert(versionEol, enginePreamble);
			}

			shaderSources[ShaderTypeFromString(type)] = rawSource;
		}

		return shaderSources;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Compile
	 * * THE BUILD CORE: Handles the compilation of individual shader stages,
	 * links them into a final program, and detaches/deletes individual shaders
	 * post-link to save GPU memory. Includes comprehensive error logging
	 * for GLSL compilation and linking failures.
	 * Now automatically queries hardware texture limits and initializes
	 * batching arrays (`u_Textures`) if present.
	 */
	void OpenGLShader::Compile(const std::unordered_map<GLenum, std::string>& shaderSources)
	{
		GLuint program = glCreateProgram();
		std::vector<GLuint> shaderIDs;

		for (auto& kv : shaderSources)
		{
			GLenum type = kv.first;
			const std::string& source = kv.second;

			GLuint shader = glCreateShader(type);

			const GLchar* sourceCStr = source.c_str();
			glShaderSource(shader, 1, &sourceCStr, 0);

			glCompileShader(shader);

			GLint isCompiled = 0;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
			if (isCompiled == GL_FALSE)
			{
				GLint maxLength = 0;
				glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

				std::vector<GLchar> infoLog(maxLength);
				glGetShaderInfoLog(shader, maxLength, &maxLength, &infoLog[0]);

				glDeleteShader(shader);

				CS_CORE_ERROR("Shader compilation failure!");
				CS_CORE_ERROR("{0}", infoLog.data());
				break;
			}

			glAttachShader(program, shader);
			shaderIDs.push_back(shader);
		}

		glLinkProgram(program);

		GLint isLinked = 0;
		glGetProgramiv(program, GL_LINK_STATUS, (int*)&isLinked);
		if (isLinked == GL_FALSE)
		{
			GLint maxLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

			std::vector<GLchar> infoLog(maxLength);
			glGetProgramInfoLog(program, maxLength, &maxLength, &infoLog[0]);

			glDeleteProgram(program);
			for (auto id : shaderIDs) glDeleteShader(id);

			CS_CORE_ERROR("Shader link failure!");
			CS_CORE_ERROR("{0}", infoLog.data());
			return;
		}

		m_RendererID = program;

		// =========================================================================
		// ENGINE CORE AUTOMATION: Dynamic Sampler Array Initialization
		// =========================================================================
		GLint texturesArrayLocation = glGetUniformLocation(m_RendererID, "u_Textures");
		if (texturesArrayLocation != -1)
		{
			glUseProgram(m_RendererID);

			// Ask the active physical GPU driver for its maximum supported texture units
			GLint maxHardwareSlots = 0;
			glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxHardwareSlots);

			// Cap it to a maximum of 32 to safely align with Renderer2D's fixed-size tracking arrays
			int32_t maxEngineSlots = std::min(maxHardwareSlots, 32);

			// Create and populate a dynamic array matching the slot sequence [0, 1, 2, ... 31]
			std::vector<int32_t> samplers(maxEngineSlots);
			for (int32_t i = 0; i < maxEngineSlots; i++)
			{
				samplers[i] = i;
			}

			// Upload the indices seamlessly straight to the driver allocation registry
			glUniform1iv(texturesArrayLocation, maxEngineSlots, samplers.data());

			// Build tracking works perfectly now!
			CS_CORE_INFO("Shader [{0}]: Automatically mapped 'u_Textures' to {1} hardware slots.", m_Name, maxEngineSlots);
		}
		// =========================================================================

		// Cleanup: Individual shader stages are no longer needed once linked
		for (auto id : shaderIDs)
		{
			glDetachShader(program, id);
			glDeleteShader(id);
		}
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Bind
	 * * Makes this shader program active in the GPU pipeline.
	 */
	void OpenGLShader::Bind() const
	{
		glUseProgram(m_RendererID);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Unbind
	 * * Deactivates the current shader program.
	 */
	void OpenGLShader::Unbind() const
	{
		glUseProgram(0);
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Uniform Setters (Implementation of virtual interface)
	/////////////////////////////////////////////////////////////////////////////////

	void OpenGLShader::SetInt(const std::string& name, int value)
	{
		UploadUniformInt(name, value);
	}

	void OpenGLShader::SetIntArray(const std::string& name, int* values, uint32_t count)
	{
		UploadUniformIntArray(name, values, count);
	}

	void OpenGLShader::SetFloat3(const std::string& name, const glm::vec3& value)
	{
		UploadUniformFloat3(name, value);
	}

	void OpenGLShader::SetFloat4(const std::string& name, const glm::vec4& value)
	{
		UploadUniformFloat4(name, value);
	}

	void OpenGLShader::SetMat3(const std::string& name, const glm::mat3& value)
	{
		UploadUniformMat3(name, value);
	}

	void OpenGLShader::SetMat4(const std::string& name, const glm::mat4& value)
	{
		UploadUniformMat4(name, value);
	}

	void OpenGLShader::SetFloat(const std::string& name, float value)
	{
		UploadUniformFloat(name, value);
	}

	/////////////////////////////////////////////////////////////////////////////////
	// OpenGL-specific Uniform Uploaders: Safe Location Cache Gateway
	/////////////////////////////////////////////////////////////////////////////////

	GLint OpenGLShader::GetUniformLocation(const std::string& name)
	{
		// Check if we already looked up this location to avoid expensive driver calls
		if (m_UniformLocationCache.find(name) != m_UniformLocationCache.end())
			return m_UniformLocationCache[name];

		GLint location = glGetUniformLocation(m_RendererID, name.c_str());

		// Cache it even if it's -1 so we don't query the driver again
		m_UniformLocationCache[name] = location;
		return location;
	}

	void OpenGLShader::UploadUniformInt(const std::string& name, int value)
	{
		GLint location = GetUniformLocation(name);
		if (location != -1) glUniform1i(location, value);
	}

	void OpenGLShader::UploadUniformIntArray(const std::string& name, int* values, uint32_t count)
	{
		GLint location = GetUniformLocation(name);
		if (location != -1) glUniform1iv(location, count, values);
	}

	void OpenGLShader::UploadUniformFloat(const std::string& name, float value)
	{
		GLint location = GetUniformLocation(name);
		if (location != -1) glUniform1f(location, value);
	}

	void OpenGLShader::UploadUniformFloat3(const std::string& name, const glm::vec3& values)
	{
		GLint location = GetUniformLocation(name);
		if (location != -1) glUniform3f(location, values.x, values.y, values.z);
	}

	void OpenGLShader::UploadUniformFloat4(const std::string& name, const glm::vec4& values)
	{
		GLint location = GetUniformLocation(name);
		if (location != -1) glUniform4f(location, values.x, values.y, values.z, values.w);
	}

	void OpenGLShader::UploadUniformMat4(const std::string& name, const glm::mat4& matrix)
	{
		GLint location = GetUniformLocation(name);
		if (location != -1) glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
	}

	/////////////////////////////////////////////////////////////////////////////////
	void OpenGLShader::SetFloat2(const std::string& name, const glm::vec2& value)
	{
		UploadUniformFloat2(name, value);
	}

	void OpenGLShader::UploadUniformFloat2(const std::string& name, const glm::vec2& values)
	{
		GLint location = GetUniformLocation(name);
		if (location != -1) glUniform2f(location, values.x, values.y);
	}

	void OpenGLShader::UploadUniformMat3(const std::string& name, const glm::mat3& matrix)
	{
		GLint location = GetUniformLocation(name);
		if (location != -1)
			glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
	}



}