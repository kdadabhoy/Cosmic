// graphics/Buffer.h — BufferLayout offset/stride math. No GL context required
// (BufferLayout is pure CPU-side description; only VertexBuffer::Create touches GL).

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

#include <doctest.h>

#include "graphics/Buffer.h"

using namespace Cosmic;

TEST_CASE("BufferLayout computes offsets and stride for a mixed layout")
{
    BufferLayout layout = {
        { ShaderDataType::Float3, "a_Position" },   // 12 bytes @ 0
        { ShaderDataType::Float4, "a_Color"    },   // 16 bytes @ 12
        { ShaderDataType::Float2, "a_TexCoord" },   //  8 bytes @ 28
        { ShaderDataType::Int,    "a_EntityID" },   //  4 bytes @ 36
    };

    const auto& elements = layout.GetElements();
    REQUIRE(elements.size() == 4);

    CHECK(elements[0].Offset == 0);
    CHECK(elements[1].Offset == 12);
    CHECK(elements[2].Offset == 28);
    CHECK(elements[3].Offset == 36);
    CHECK(layout.GetStride() == 40);
}

TEST_CASE("BufferElement sizes and component counts match ShaderDataType")
{
    CHECK(BufferElement(ShaderDataType::Float3, "v").Size == 12);
    CHECK(BufferElement(ShaderDataType::Mat4,   "m").Size == 64);
    CHECK(BufferElement(ShaderDataType::Bool,   "b").Size == 1);

    CHECK(BufferElement(ShaderDataType::Float3, "v").GetComponentCount() == 3);
    CHECK(BufferElement(ShaderDataType::Mat4,   "m").GetComponentCount() == 16);
    CHECK(BufferElement(ShaderDataType::Int4,   "i").GetComponentCount() == 4);
}

TEST_CASE("Instanced flag is carried through the layout")
{
    BufferLayout layout = {
        { ShaderDataType::Float3, "a_InstancePos", false, true },
        { ShaderDataType::Float4, "a_Color" },
    };

    CHECK(layout.GetElements()[0].Instanced == true);
    CHECK(layout.GetElements()[1].Instanced == false);
}
