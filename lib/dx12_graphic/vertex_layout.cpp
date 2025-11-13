#include "pch.h"
#include "vertex_layout.h"

namespace
{
	inline uint32_t getFormatSize(DXGI_FORMAT format)
	{
		switch (format)
		{
		case DXGI_FORMAT_R32G32B32A32_FLOAT: return 16;
		case DXGI_FORMAT_R32G32B32_FLOAT:    return 12;
		case DXGI_FORMAT_R32G32_FLOAT:       return 8;
		case DXGI_FORMAT_R32_FLOAT:          return 4;
		case DXGI_FORMAT_R8G8B8A8_UNORM:     return 4;
		case DXGI_FORMAT_R8G8B8A8_UINT:      return 4;
		case DXGI_FORMAT_R16_UINT:           return 2;
		case DXGI_FORMAT_R32_UINT:           return 4;
			// ... other format
		default: throw std::logic_error("Format size not implemented.");
		}
	}

	class LayoutStringPool
	{
	public:
		static constexpr size_t MaxDataSize = 1 * 1024;     // (1 KB)
		static constexpr size_t MaxEntries = 512;

		LayoutStringPool() : _dataPos(0), _entryCount(0) {}

		std::string_view add(std::string_view s)
		{
			//slow but this is not a frequent call.
			for (size_t i = 0; i < _entryCount; ++i)
			{
				if (_entries[i] == s)
					return _entries[i];
			}

			const size_t len = s.size();
			if (_dataPos + len + 1 > MaxDataSize)
				throw std::runtime_error("LayoutStringPool: out of memory");

			if (_entryCount >= MaxEntries)
				throw std::runtime_error("LayoutStringPool: too many entries");

			char* dst = &_data[_dataPos];
			std::memcpy(dst, s.data(), len);
			dst[len] = '\0';

			std::string_view view(dst, len);
			_entries[_entryCount++] = view;
			_dataPos += len + 1;

			return view;
		}

		template<size_t N>
		std::string_view add(const char(&s)[N])
		{
			return add(std::string_view(s, N - 1));
		}

		std::string_view add(const char* s) { return add(std::string_view(s)); }
		std::string_view add(const std::string& s) { return add(std::string_view(s)); }


	private:
		std::array<char, MaxDataSize> _data;
		std::array<std::string_view, MaxEntries> _entries;
		size_t _dataPos;
		size_t _entryCount;
	};

	static LayoutStringPool s_layoutStringPool{};
}

namespace csyren::render
{
	std::string_view Semantic::POSITION     = s_layoutStringPool.add("POSITION");
	std::string_view Semantic::NORMAL       = s_layoutStringPool.add("NORMAL");
	std::string_view Semantic::TANGENT      = s_layoutStringPool.add("TANGENT");
	std::string_view Semantic::BITANGENT    = s_layoutStringPool.add("BITANGENT");
	std::string_view Semantic::COLOR        = s_layoutStringPool.add("COLOR");
	std::string_view Semantic::TEXCOORD     = s_layoutStringPool.add("TEXCOORD");
	std::string_view Semantic::BLENDINDICES = s_layoutStringPool.add("BLENDINDICES");
	std::string_view Semantic::BLENDWEIGHT  = s_layoutStringPool.add("BLENDWEIGHT");

	std::string_view makeIndexedSemantic(std::string_view base, uint32_t index)
	{
		char buffer[32];
		int len = std::snprintf(buffer, sizeof(buffer), "%.*s%u",
			static_cast<int>(base.size()), base.data(), index);
		return s_layoutStringPool.add(std::string_view(buffer, len));
	}
	VertexLayout::VertexLayout(std::vector<VertexAttribute>&& attrs) :
		_attrs(std::move(attrs)),
		_stride(0u),
		_hash(0u)
	{
		uint32_t offset = 0;
		_d3dLayout.reserve(_attrs.size());

		for (const auto& attr : _attrs)
		{
			_d3dLayout.push_back({ attr.semanticName.data(),attr.semanticIndex,attr.format,0,offset,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0 });
			offset += getFormatSize(attr.format);
		}
		_stride = offset;
		_hash = computeHash();
	}
	size_t VertexLayout::computeHash() const noexcept
	{
		size_t seed = _attrs.size();
		for (const auto& attr : _attrs)
		{
			size_t nameHash = std::hash<std::string_view>{}(attr.semanticName);
			size_t formatHash = static_cast<size_t>(attr.format);
			seed ^= nameHash + 0x9e3779b9 + (seed << 6) + (seed >> 2);
			seed ^= formatHash + 0x9e3779b9 + (seed << 6) + (seed >> 2);
		}
		return seed;
	}
}
