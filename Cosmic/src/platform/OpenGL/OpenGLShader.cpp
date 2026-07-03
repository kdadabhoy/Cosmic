// windows.h defines APIENTRY as __stdcall; undef it so glad can redefine it cleanly.
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef APIENTRY
#undef APIENTRY
#endif

#include "platform/opengl/OpenGLShader.h"
#include "platform/opengl/OpenGLContext.h"
#include <vector>
#include <glm/gtc/type_ptr.hpp>
#include "core/Log.h"
#include <filesystem>
#include <sstream>
#include <algorithm>

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
        if (type == "compute") return GL_COMPUTE_SHADER;   // S4.7 GPU compute stage

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
        // Skip the GL delete if the context is already gone (abort/teardown order) —
        // see OpenGLContext::HasCurrentContext(). The driver reclaims the program with
        // the context, so this leaks nothing.
        if (OpenGLContext::HasCurrentContext())
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

    std::unordered_map<GLenum, std::string> OpenGLShader::PreProcess(const std::string& source)
    {
        std::unordered_map<GLenum, std::string> shaderSources;

        // Shared reusable 2D Batch-compatible Vertex Shader boilerplate
        std::string autoVertexShader =
            "#version 450 core\n"
            "layout(location = 0) in vec3 a_Position;\n"
            "layout(location = 1) in vec4 a_Color;\n"
            "layout(location = 2) in vec2 a_TexCoord;\n"
            "layout(location = 3) in float a_TexIndex;\n"
            "layout(location = 4) in float a_TilingFactor;\n\n"
            "uniform mat4 u_ViewProjection;\n\n"
            "out vec4 v_Color;\n"
            "out vec2 v_TexCoord;\n\n"
            "void main()\n"
            "{\n"
            "    v_Color = a_Color;\n"
            "    v_TexCoord = a_TexCoord;\n"
            "    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);\n"
            "}\n";

        const char* typeToken = "#type";
        size_t pos = source.find(typeToken, 0);

        // =================================================================
        // FALLBACK 1: PURE SHADERTOY / RAW FILES (No #type tags anywhere)
        // =================================================================
        if (pos == std::string::npos)
        {
            if (source.find("mainImage") != std::string::npos || source.find("iTime") != std::string::npos)
            {
                std::string autoFragmentPreamble = "\n";
                autoFragmentPreamble += "uniform mat4 u_ViewProjection;\n";
                autoFragmentPreamble += "uniform float u_Time;\n";
                autoFragmentPreamble += "uniform vec2 u_ViewportSize;\n";
                autoFragmentPreamble += "in vec4 v_Color;\n";
                autoFragmentPreamble += "in vec2 v_TexCoord;\n";
                autoFragmentPreamble += "layout(location = 0) out vec4 color;\n";
                autoFragmentPreamble += "#define iTime u_Time\n";
                autoFragmentPreamble += "#define iResolution vec3(u_ViewportSize, 1.0)\n";

                std::string autoFragmentPostamble = R"(void main(){    vec2 shadertoyFragCoord = v_TexCoord * u_ViewportSize;    vec4 shadertoyFragColor;    mainImage(shadertoyFragColor, shadertoyFragCoord);    color = shadertoyFragColor * v_Color;})";

                shaderSources[GL_VERTEX_SHADER] = autoVertexShader;
                shaderSources[GL_FRAGMENT_SHADER] = "#version 450 core\n" + autoFragmentPreamble + source + autoFragmentPostamble;
                return shaderSources;
            }

            CS_CORE_ERROR("Shader Preprocessor Critical Failure: File contains no '#type' configurations and lacks Shadertoy compatibility signatures.");
            return shaderSources;
        }

        // =================================================================
        // STANDARD: MULTI-STAGE NATIVE PARSING ENGINE (#type validation)
        // =================================================================
        size_t typeTokenLength = strlen(typeToken);
        while (pos != std::string::npos)
        {
            size_t eol = source.find_first_of("\r\n", pos);
            size_t begin = pos + typeTokenLength + 1;

            // Sanity Check: Ensure we didn't hit the end of the file or a malformed token line
            if (eol == std::string::npos || eol <= begin)
            {
                CS_CORE_ERROR("Shader Preprocessor Syntax Error: Malformed '#type' identifier line.");
                return shaderSources;
            }

            std::string typeStr = source.substr(begin, eol - begin);

            typeStr.erase(std::remove_if(typeStr.begin(), typeStr.end(), ::isspace), typeStr.end());
            GLenum shaderType = ShaderTypeFromString(typeStr);

            size_t nextLinePos = source.find_first_not_of("\r\n", eol);
            pos = source.find(typeToken, nextLinePos);

            std::string rawSource = (pos == std::string::npos) ?
                source.substr(nextLinePos) : source.substr(nextLinePos, pos - nextLinePos);

            // --- ROCK-SOLID COMMENT INSULATION ---
            std::string cleanSearchSource = rawSource;
            size_t blockStart = cleanSearchSource.find("/*");
            while (blockStart != std::string::npos)
            {
                size_t blockEnd = cleanSearchSource.find("*/", blockStart);
                if (blockEnd != std::string::npos) cleanSearchSource.erase(blockStart, (blockEnd + 2) - blockStart);
                else break;
                blockStart = cleanSearchSource.find("/*");
            }
            size_t lineStart = cleanSearchSource.find("//");
            while (lineStart != std::string::npos)
            {
                size_t lineEnd = cleanSearchSource.find_first_of("\r\n", lineStart);
                if (lineEnd != std::string::npos)
                {
                    cleanSearchSource.erase(lineStart, lineEnd - lineStart);
                }
                else
                {
                    cleanSearchSource.erase(lineStart); // Cleanly erase everything left to the end
                    break;
                }
                lineStart = cleanSearchSource.find("//");
            }
            // -----------------------------------------------------------------

            std::string enginePreamble = "\n";

            struct EngineUniform
            {
                std::string exactUniformName;
                std::vector<std::string> compatibilityKeys;
                std::string glslDeclaration;
            };

            std::vector<EngineUniform> globalUniformRegistry = {
                { "u_ViewProjection", { "u_ViewProjection" }, "uniform mat4 u_ViewProjection;\n" },
                { "u_Time", { "u_Time", "iTime", "TIME", "_Time" }, "uniform float u_Time;\n" },
                { "u_ViewportSize", { "u_ViewportSize", "iResolution", "BUFFER_SIZE", "_ScreenParams" }, "uniform vec2 u_ViewportSize;\n" }
            };

            for (const auto& uniform : globalUniformRegistry)
            {
                bool usesFeature = false;
                if (cleanSearchSource.find(uniform.exactUniformName) != std::string::npos)
                {
                    usesFeature = true;
                }
                else
                {
                    for (const auto& key : uniform.compatibilityKeys)
                    {
                        if (cleanSearchSource.find(key) != std::string::npos)
                        {
                            usesFeature = true;
                            break;
                        }
                    }
                }

                bool alreadyDeclared = false;
                size_t namePos = cleanSearchSource.find(uniform.exactUniformName);
                while (namePos != std::string::npos)
                {
                    // Scan backwards line-by-line from the matched token position to verify explicit 'uniform' keyword context
                    size_t currentScan = namePos;
                    while (currentScan > 0)
                    {
                        currentScan--;
                        if (cleanSearchSource[currentScan] == '\n' || cleanSearchSource[currentScan] == '\r')
                        {
                            currentScan++; // Step ahead onto the actual line payload start 
                            break;
                        }
                    }

                    std::string lineContext = cleanSearchSource.substr(currentScan, namePos - currentScan);
                    if (lineContext.find("uniform") != std::string::npos)
                    {
                        alreadyDeclared = true;
                        break;
                    }
                    namePos = cleanSearchSource.find(uniform.exactUniformName, namePos + 1);
                }

                if (usesFeature && !alreadyDeclared)
                {
                    if (uniform.exactUniformName == "u_ViewProjection" && shaderType != GL_VERTEX_SHADER)
                    {
                        continue;
                    }
                    enginePreamble += uniform.glslDeclaration;
                }
            }

            if (shaderType == GL_FRAGMENT_SHADER)
            {
                if (cleanSearchSource.find("v_TexCoord") == std::string::npos)
                {
                    enginePreamble += "in vec2 v_TexCoord;\n";
                }
                if (cleanSearchSource.find("v_Color") == std::string::npos)
                {
                    enginePreamble += "in vec4 v_Color;\n";
                }
                if (cleanSearchSource.find("out vec4 color") == std::string::npos)
                {
                    enginePreamble += "layout(location = 0) out vec4 color;\n";
                }
            }

            bool isShadertoy = (cleanSearchSource.find("iTime") != std::string::npos || cleanSearchSource.find("mainImage") != std::string::npos);
            std::string shadertoyWrapper = "";

            if (isShadertoy)
            {
                enginePreamble += "#define iTime u_Time\n";
                enginePreamble += "#define iResolution vec3(u_ViewportSize, 1.0)\n";

                if (shaderType == GL_FRAGMENT_SHADER && cleanSearchSource.find("mainImage") != std::string::npos && cleanSearchSource.find("void main") == std::string::npos)
                {
                    shadertoyWrapper = "\nvoid main(){\n\tvec2 shadertoyFragCoord = v_TexCoord * u_ViewportSize;\n\tvec4 shadertoyFragColor;\n\tmainImage(shadertoyFragColor, shadertoyFragCoord);\n\tcolor = shadertoyFragColor * v_Color;\n}\n";
                }
            }

            // =================================================================
            // STEP 4: Reconstruction Assembly
            // =================================================================
			size_t versionPos = rawSource.find("#version");
			if (versionPos != std::string::npos)
			{
				size_t versionEol = rawSource.find_first_of("\r\n", versionPos);

				// CRITICAL SAFETY GUARD: Handle files ending abruptly without newline markers
				if (versionEol == std::string::npos)
					versionEol = rawSource.size();

				while (versionEol < rawSource.size() && (rawSource[versionEol] == '\r' || rawSource[versionEol] == '\n'))
				{
					versionEol++;
				}

				if (versionEol < rawSource.size())
				{
					std::string versionLine = rawSource.substr(0, versionEol);
					std::string remainingSource = rawSource.substr(versionEol);

					rawSource = versionLine + "\n" + enginePreamble + "\n" + remainingSource + shadertoyWrapper;
				}
				else
				{
					rawSource = rawSource + "\n" + enginePreamble + shadertoyWrapper;
				}
			}
			else
			{
				rawSource = "#version 450 core\n" + enginePreamble + "\n" + rawSource + shadertoyWrapper;
			}

			shaderSources[shaderType] = rawSource;
        }

        // =================================================================
        // FALLBACK 2: NATIVE FILE DECLARED FRAGMENT BUT MISSED VERTEX SECTION
        // =================================================================
        if (shaderSources.find(GL_VERTEX_SHADER) == shaderSources.end() && shaderSources.find(GL_FRAGMENT_SHADER) != shaderSources.end())
        {
            shaderSources[GL_VERTEX_SHADER] = autoVertexShader;
        }
        else if (shaderSources.find(GL_FRAGMENT_SHADER) == shaderSources.end() && shaderSources.find(GL_VERTEX_SHADER) != shaderSources.end())
        {
            CS_CORE_ERROR("Shader Preprocessor Diagnostic Failure: Generated a valid Vertex pipeline block, but missing target Fragment block stage.");
        }

        return shaderSources;
    }

    /////////////////////////////////////////////////////////////////////////////////

    // Helper to dump shader source on failure instead of spamming standard initialization logs
    void OpenGLShader::DumpPreprocessedShader(const std::unordered_map<GLenum, std::string>& shaderSources)
    {
        CS_CORE_ERROR("--------------------- PREPROCESSED SHADER DUMP BEGIN ---------------------");
        for (const auto& [stage, sourceText] : shaderSources)
        {
            std::string stageName = (stage == GL_VERTEX_SHADER) ? "VERTEX SHADER"
                : (stage == GL_COMPUTE_SHADER) ? "COMPUTE SHADER" : "FRAGMENT SHADER";
            CS_CORE_ERROR("==================== STAGE: {0} ====================", stageName);

            std::stringstream ss(sourceText);
            std::string line;
            int lineCounter = 1;
            while (std::getline(ss, line))
            {
                char linePrefix[32];
                sprintf(linePrefix, "[Line %03d]: ", lineCounter++);
                CS_CORE_ERROR("{0}{1}", linePrefix, line);
            }
        }
        CS_CORE_ERROR("--------------------- PREPROCESSED SHADER DUMP END -----------------------");
    }

    /////////////////////////////////////////////////////////////////////////////////

    /**
     * Compile
     * * THE BUILD CORE: Handles the compilation of individual shader stages,
     * links them into a final program, and detaches/deletes individual shaders
     * post-link to save GPU memory. Includes comprehensive error logging
     * for GLSL compilation and linking failures.
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

                CS_CORE_ERROR("Shader compilation failure in stage: {0}",
                    type == GL_VERTEX_SHADER ? "VERTEX" : (type == GL_COMPUTE_SHADER ? "COMPUTE" : "FRAGMENT"));
                CS_CORE_ERROR("{0}", infoLog.data());

                // Trigger the source code dump to pinpoint line number issues
                DumpPreprocessedShader(shaderSources);

                // Clean up resources allocated up to this point
                glDeleteShader(shader);
                for (auto id : shaderIDs) glDeleteShader(id);
                glDeleteProgram(program);
                return; // Exit completely; do not attempt to link a broken shader pipeline
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

            CS_CORE_ERROR("Shader link failure!");
            CS_CORE_ERROR("{0}", infoLog.data());

            // Trigger dump on link validation failure
            DumpPreprocessedShader(shaderSources);

            glDeleteProgram(program);
            for (auto id : shaderIDs) glDeleteShader(id);
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

    void OpenGLShader::Bind() const
    {
        glUseProgram(m_RendererID);
    }

    /////////////////////////////////////////////////////////////////////////////////

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

    void OpenGLShader::SetFloat(const std::string& name, float value)
    {
        UploadUniformFloat(name, value);
    }

    void OpenGLShader::SetFloat2(const std::string& name, const glm::vec2& value)
    {
        UploadUniformFloat2(name, value);
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

    /////////////////////////////////////////////////////////////////////////////////
    // OpenGL-specific Uniform Uploaders: Safe Location Cache Gateway
    /////////////////////////////////////////////////////////////////////////////////

    GLint OpenGLShader::GetUniformLocation(const std::string& name)
    {
        if (m_UniformLocationCache.find(name) != m_UniformLocationCache.end())
            return m_UniformLocationCache[name];

        GLint location = glGetUniformLocation(m_RendererID, name.c_str());

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

    void OpenGLShader::UploadUniformFloat2(const std::string& name, const glm::vec2& values)
    {
        GLint location = GetUniformLocation(name);
        if (location != -1) glUniform2f(location, values.x, values.y);
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

    void OpenGLShader::UploadUniformMat3(const std::string& name, const glm::mat3& matrix)
    {
        GLint location = GetUniformLocation(name);
        if (location != -1) glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
    }

    void OpenGLShader::UploadUniformMat4(const std::string& name, const glm::mat4& matrix)
    {
        GLint location = GetUniformLocation(name);
        if (location != -1) glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
    }
}