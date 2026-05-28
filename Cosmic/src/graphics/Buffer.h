#pragma once

// Buffer.h
// Last Modified 5/14/2026

/**
 * General Description:
 * 
 * Buffer.h defines the memory transport layer of the Cosmic Engine. It provides
 * abstract interfaces for managing GPU-side memory blocks, specifically for
 * geometry (VertexBuffer) and topology (IndexBuffer).
 * * The system is designed around the concept of "Hardware Abstraction," meaning
 * the high-level renderer interacts only with these base classes, while the
 * specific API (OpenGL/DirectX) is determined at runtime.
 * 
 * 
 * Architecture Components:
 * 
 * 1. ShaderDataType: An enum and utility suite that maps high-level types
 * (Float3, Mat4) to their respective byte sizes and component counts.
 * 
 * 2. BufferLayout: The "Interpreter" for raw memory. It defines the schema of
 * a vertex (e.g., Position is at offset 0, Color is at offset 12).
 * 
 * 3. VertexBuffer: Stores the raw attribute data for vertices. Supports
 * static allocation or dynamic streaming (required for Batch Rendering).
 * 
 * 4. IndexBuffer: Stores the order in which vertices should be drawn,
 * optimizing GPU cache and reducing redundant vertex processing.
 */

#include <string>
#include <vector>
#include <memory>

namespace Cosmic
{
	/**
	 * ShaderDataType
	 * Enum representing the data types recognized by the Graphics API shaders.
	 */
	enum class ShaderDataType
	{
		None = 0, Float, Float2, Float3, Float4, Mat3, Mat4, Int, Int2, Int3, Int4, Bool
	};

	/**
	 * ShaderDataTypeSize
	 * Returns the size in bytes of a given ShaderDataType.
	 */
	static uint32_t ShaderDataTypeSize(ShaderDataType type)
	{
		switch (type)
		{
		case ShaderDataType::Float:    return 4;
		case ShaderDataType::Float2:   return 4 * 2;
		case ShaderDataType::Float3:   return 4 * 3;
		case ShaderDataType::Float4:   return 4 * 4;
		case ShaderDataType::Mat3:     return 4 * 3 * 3;
		case ShaderDataType::Mat4:     return 4 * 4 * 4;
		case ShaderDataType::Int:      return 4;
		case ShaderDataType::Int2:     return 4 * 2;
		case ShaderDataType::Int3:     return 4 * 3;
		case ShaderDataType::Int4:     return 4 * 4;
		case ShaderDataType::Bool:     return 1;
		}
		return 0;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * BufferElement
	 * Represents a single attribute within a vertex (e.g., "a_Position").
	 * Automatically calculates size and component count based on the provided type.
	 */
	struct BufferElement
	{
		std::string Name;
		ShaderDataType Type;
		uint32_t Size;
		size_t Offset;
		bool Normalized;
		bool Instanced; // Dedicated hardware divisor flag

		// Updated constructor to support explicit instancing parameters
		BufferElement(ShaderDataType type, const std::string& name, bool normalized = false, bool instanced = false)
			: Name(name), Type(type), Size(ShaderDataTypeSize(type)), Offset(0), Normalized(normalized), Instanced(instanced)
		{
		}

		uint32_t GetComponentCount() const
		{
			switch (Type)
			{
			case ShaderDataType::Float:   return 1;
			case ShaderDataType::Float2:  return 2;
			case ShaderDataType::Float3:  return 3;
			case ShaderDataType::Float4:  return 4;
			case ShaderDataType::Mat3:    return 3 * 3;
			case ShaderDataType::Mat4:    return 4 * 4;
			case ShaderDataType::Int:     return 1;
			case ShaderDataType::Int2:    return 2;
			case ShaderDataType::Int3:    return 3;
			case ShaderDataType::Int4:    return 4;
			case ShaderDataType::Bool:    return 1;
			}
			return 0;
		}
	};

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * BufferLayout
	 * Describes the full structure of a VertexBuffer.
	 * Calculates the 'Stride' (total size of a vertex) and the relative 'Offsets'
	 * of each element so the GPU knows how to parse the raw byte stream.
	 */
	class BufferLayout
	{
	public:
		BufferLayout() {}
		BufferLayout(const std::initializer_list<BufferElement>& elements)
			: m_Elements(elements)
		{
			CalculateOffsetsAndStride();
		}

		inline uint32_t GetStride() const { return m_Stride; }
		inline const std::vector<BufferElement>& GetElements() const { return m_Elements; }

		// Standard iterators for range-based for loops
		std::vector<BufferElement>::iterator begin() { return m_Elements.begin(); }
		std::vector<BufferElement>::iterator end() { return m_Elements.end(); }
		std::vector<BufferElement>::const_iterator begin() const { return m_Elements.begin(); }
		std::vector<BufferElement>::const_iterator end() const { return m_Elements.end(); }

	private:
		/**
		 * CalculateOffsetsAndStride
		 * Iterates through elements to determine where each one starts in memory.
		 */
		void CalculateOffsetsAndStride()
		{
			uint32_t offset = 0;
			m_Stride = 0;
			for (auto& element : m_Elements)
			{
				element.Offset = offset;
				offset += element.Size;
				m_Stride += element.Size;
			}
		}

	private:
		std::vector<BufferElement> m_Elements;
		uint32_t m_Stride = 0;
	};

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * VertexBuffer (Interface)
	 * Abstract blueprint for vertex data storage.
	 * * Use 'Create' methods to instantiate platform-specific buffers.
	 * 'SetData' is vital for high-performance Batch Rendering.
	 */
	class VertexBuffer
	{
	public:
		virtual ~VertexBuffer() {}

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		/**
		 * SetData
		 * Updates the buffer content on the GPU. Essential for dynamic geometry.
		 */
		virtual void SetData(const void* data, uint32_t size) = 0;

		virtual const BufferLayout& GetLayout() const = 0;
		virtual void SetLayout(const BufferLayout& layout) = 0;

		// Factory methods for API-independent instantiation
		static std::shared_ptr<VertexBuffer> Create(uint32_t size);
		static std::shared_ptr<VertexBuffer> Create(float* vertices, uint32_t size);
	};

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * IndexBuffer (Interface)
	 * Abstract blueprint for vertex index data.
	 * * Directs the GPU to draw triangles using specific indices, allowing
	 * for vertex reuse and reduced memory overhead.
	 */
	class IndexBuffer
	{
	public:
		virtual ~IndexBuffer() {}

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		virtual uint32_t GetCount() const = 0;

		static std::shared_ptr<IndexBuffer> Create(uint32_t* indices, uint32_t count);
	};

}