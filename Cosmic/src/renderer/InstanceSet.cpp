// InstanceSet.cpp — S12.3-lite per-instance transform pool. See InstanceSet.h.

#include "renderer/InstanceSet.h"
#include "renderer/BindingPoints.h"
#include "core/Log.h"

#include <algorithm>
#include <vector>

namespace Cosmic
{
	namespace
	{
		// Mirrors the std430 `InstanceData { mat4 Model; vec4 Tint; }` in
		// PBRInstanced.glsl / ShadowDepthInstanced.glsl. std430 packs a mat4 as
		// 4 vec4 (64 B) followed by a vec4 (16 B) = 80 B, naturally.
		struct InstanceGpu
		{
			glm::mat4 Model;
			glm::vec4 Tint;
		};
		static_assert(sizeof(InstanceGpu) == 80,
		              "InstanceGpu must match the std430 InstanceData (80 bytes).");
	}

	Ref<InstanceSet> InstanceSet::Create(uint32_t capacity)
	{
		if (capacity == 0)
		{
			CS_CORE_ERROR("InstanceSet::Create: capacity must be > 0.");
			return nullptr;
		}

		auto set = std::make_shared<InstanceSet>();
		set->m_Capacity = capacity;
		set->m_Count    = 0;
		set->m_Buffer   = StorageBuffer::Create(capacity * sizeof(InstanceGpu), Bindings::InstancesSsbo);
		if (!set->m_Buffer)
		{
			CS_CORE_ERROR("InstanceSet::Create: storage buffer allocation failed.");
			return nullptr;
		}
		return set;
	}

	void InstanceSet::SetInstances(const glm::mat4* transforms, const glm::vec4* tints, uint32_t count)
	{
		if (!m_Buffer || !transforms)
		{
			m_Count = 0;
			return;
		}

		m_Count = std::min(count, m_Capacity);
		if (m_Count == 0)
			return;

		std::vector<InstanceGpu> packed(m_Count);
		for (uint32_t i = 0; i < m_Count; ++i)
		{
			packed[i].Model = transforms[i];
			packed[i].Tint  = tints ? tints[i] : glm::vec4(1.0f);
		}

		m_Buffer->SetData(packed.data(), m_Count * sizeof(InstanceGpu));
	}

	void InstanceSet::Bind() const
	{
		if (m_Buffer)
			m_Buffer->Bind();
	}
}
