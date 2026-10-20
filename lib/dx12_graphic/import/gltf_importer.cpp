#include "gltf_importer.h"
#include "third_party/json/json.hpp"
#include "cstdmf/log.h"
#include "core/project_paths.h"

#include <DirectXMath.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <unordered_map>

namespace csyren::render::assets
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr uint32_t GLB_MAGIC = 0x46546C67u;   // "glTF"
		constexpr uint32_t CHUNK_JSON = 0x4E4F534Au;  // "JSON"
		constexpr uint32_t CHUNK_BIN = 0x004E4942u;   // "BIN\0"

		constexpr int COMPONENT_BYTE = 5120, COMPONENT_UBYTE = 5121, COMPONENT_SHORT = 5122,
			COMPONENT_USHORT = 5123, COMPONENT_UINT = 5125, COMPONENT_FLOAT = 5126;

		struct Context
		{
			Json doc;
			std::vector<std::vector<uint8_t>> buffers;
			std::filesystem::path directory;   // physical directory of the model file
			std::string assetDirectory;        // logical directory (for texture references)
			std::string error;
		};

		struct AccessorView
		{
			const uint8_t* data = nullptr;
			size_t count = 0;
			int componentType = COMPONENT_FLOAT;
			int components = 1;
			size_t stride = 0;
			bool normalized = false;
		};

		bool readFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& out)
		{
			std::ifstream file(path, std::ios::binary);
			if (!file) return false;
			out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
			return !file.bad();
		}

		bool decodeBase64(std::string_view text, std::vector<uint8_t>& out)
		{
			auto value = [](char c) -> int {
				if (c >= 'A' && c <= 'Z') return c - 'A';
				if (c >= 'a' && c <= 'z') return c - 'a' + 26;
				if (c >= '0' && c <= '9') return c - '0' + 52;
				if (c == '+') return 62;
				if (c == '/') return 63;
				return -1;
			};
			out.clear(); out.reserve(text.size() * 3 / 4);
			uint32_t accumulator = 0; int bits = 0;
			for (char c : text)
			{
				if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
				const int v = value(c);
				if (v < 0) return false;
				accumulator = (accumulator << 6) | uint32_t(v); bits += 6;
				if (bits >= 8) { bits -= 8; out.push_back(uint8_t((accumulator >> bits) & 0xFF)); }
			}
			return true;
		}

		std::string percentDecode(const std::string& text)
		{
			std::string result; result.reserve(text.size());
			for (size_t i = 0; i < text.size(); ++i)
			{
				if (text[i] == '%' && i + 2 < text.size())
				{
					const auto hex = text.substr(i + 1, 2);
					char* end = nullptr;
					const long code = std::strtol(hex.c_str(), &end, 16);
					if (end && *end == '\0') { result.push_back(char(code)); i += 2; continue; }
				}
				result.push_back(text[i]);
			}
			return result;
		}

		bool parseGlb(const std::vector<uint8_t>& file, Json& doc, std::vector<uint8_t>& bin, std::string& error)
		{
			if (file.size() < 20) { error = "glb header too short"; return false; }
			uint32_t magic{}, version{}, length{};
			std::memcpy(&magic, file.data(), 4); std::memcpy(&version, file.data() + 4, 4); std::memcpy(&length, file.data() + 8, 4);
			if (magic != GLB_MAGIC) { error = "not a glb file"; return false; }
			if (version != 2) { error = "unsupported glb version " + std::to_string(version); return false; }
			size_t offset = 12;
			bool haveJson = false;
			while (offset + 8 <= file.size() && offset + 8 <= length)
			{
				uint32_t chunkLength{}, chunkType{};
				std::memcpy(&chunkLength, file.data() + offset, 4); std::memcpy(&chunkType, file.data() + offset + 4, 4);
				offset += 8;
				if (offset + chunkLength > file.size()) { error = "glb chunk exceeds file size"; return false; }
				if (chunkType == CHUNK_JSON)
				{
					doc = Json::parse(file.begin() + offset, file.begin() + offset + chunkLength, nullptr, false);
					if (doc.is_discarded()) { error = "glb JSON chunk is not valid JSON"; return false; }
					haveJson = true;
				}
				else if (chunkType == CHUNK_BIN) bin.assign(file.begin() + offset, file.begin() + offset + chunkLength);
				offset += chunkLength;
				offset = (offset + 3) & ~size_t(3);
			}
			if (!haveJson) error = "glb has no JSON chunk";
			return haveJson;
		}

		bool loadBuffers(Context& ctx, std::vector<uint8_t>&& bin)
		{
			if (!ctx.doc.contains("buffers")) return true;
			size_t index = 0;
			for (const auto& buffer : ctx.doc["buffers"])
			{
				std::vector<uint8_t> data;
				if (buffer.contains("uri"))
				{
					const std::string uri = buffer["uri"].get<std::string>();
					if (uri.rfind("data:", 0) == 0)
					{
						const auto comma = uri.find(',');
						if (comma == std::string::npos || !decodeBase64(std::string_view(uri).substr(comma + 1), data))
						{ ctx.error = "buffer " + std::to_string(index) + ": invalid data URI"; return false; }
					}
					else if (!readFileBytes(ctx.directory / core::pathFromUtf8(percentDecode(uri)), data))
					{ ctx.error = "buffer " + std::to_string(index) + ": cannot read '" + uri + "'"; return false; }
				}
				else if (index == 0 && !bin.empty()) data = std::move(bin);
				else { ctx.error = "buffer " + std::to_string(index) + " has no data"; return false; }
				const size_t expected = buffer.value("byteLength", size_t(0));
				if (data.size() < expected) { ctx.error = "buffer " + std::to_string(index) + " is shorter than byteLength"; return false; }
				ctx.buffers.push_back(std::move(data));
				++index;
			}
			return true;
		}

		int componentCount(const std::string& type)
		{
			if (type == "SCALAR") return 1;
			if (type == "VEC2") return 2;
			if (type == "VEC3") return 3;
			if (type == "VEC4") return 4;
			if (type == "MAT2") return 4;
			if (type == "MAT3") return 9;
			if (type == "MAT4") return 16;
			return 0;
		}
		size_t componentSize(int componentType)
		{
			switch (componentType)
			{
			case COMPONENT_BYTE: case COMPONENT_UBYTE: return 1;
			case COMPONENT_SHORT: case COMPONENT_USHORT: return 2;
			case COMPONENT_UINT: case COMPONENT_FLOAT: return 4;
			default: return 0;
			}
		}

		bool accessor(Context& ctx, int index, AccessorView& out)
		{
			const auto& accessors = ctx.doc["accessors"];
			if (index < 0 || index >= int(accessors.size())) { ctx.error = "accessor index out of range"; return false; }
			const auto& acc = accessors[index];
			if (acc.contains("sparse")) { ctx.error = "sparse accessors are not supported"; return false; }
			out.componentType = acc.value("componentType", COMPONENT_FLOAT);
			out.components = componentCount(acc.value("type", std::string("SCALAR")));
			out.count = acc.value("count", size_t(0));
			out.normalized = acc.value("normalized", false);
			const size_t cs = componentSize(out.componentType);
			if (!cs || !out.components) { ctx.error = "accessor " + std::to_string(index) + ": unsupported type"; return false; }
			const size_t elementSize = cs * out.components;
			if (!acc.contains("bufferView")) { ctx.error = "accessor " + std::to_string(index) + " without bufferView"; return false; }
			const int viewIndex = acc["bufferView"].get<int>();
			const auto& views = ctx.doc["bufferViews"];
			if (viewIndex < 0 || viewIndex >= int(views.size())) { ctx.error = "bufferView index out of range"; return false; }
			const auto& view = views[viewIndex];
			const int bufferIndex = view.value("buffer", -1);
			if (bufferIndex < 0 || bufferIndex >= int(ctx.buffers.size())) { ctx.error = "buffer index out of range"; return false; }
			const auto& buffer = ctx.buffers[bufferIndex];
			const size_t offset = view.value("byteOffset", size_t(0)) + acc.value("byteOffset", size_t(0));
			out.stride = view.value("byteStride", size_t(0));
			if (out.stride == 0) out.stride = elementSize;
			if (out.count == 0) { out.data = buffer.data() + offset; return true; }
			const size_t end = offset + (out.count - 1) * out.stride + elementSize;
			if (end > buffer.size()) { ctx.error = "accessor " + std::to_string(index) + " exceeds its buffer"; return false; }
			out.data = buffer.data() + offset;
			return true;
		}

		float readComponent(const uint8_t* p, int componentType, bool normalized)
		{
			switch (componentType)
			{
			case COMPONENT_FLOAT: { float v; std::memcpy(&v, p, 4); return v; }
			case COMPONENT_BYTE: { int8_t v; std::memcpy(&v, p, 1); return normalized ? (std::max)(float(v) / 127.0f, -1.0f) : float(v); }
			case COMPONENT_UBYTE: { uint8_t v = *p; return normalized ? float(v) / 255.0f : float(v); }
			case COMPONENT_SHORT: { int16_t v; std::memcpy(&v, p, 2); return normalized ? (std::max)(float(v) / 32767.0f, -1.0f) : float(v); }
			case COMPONENT_USHORT: { uint16_t v; std::memcpy(&v, p, 2); return normalized ? float(v) / 65535.0f : float(v); }
			case COMPONENT_UINT: { uint32_t v; std::memcpy(&v, p, 4); return float(v); }
			default: return 0.0f;
			}
		}

		// Reads every element as `wanted` floats (missing components padded with pad).
		void readFloats(const AccessorView& view, int wanted, std::vector<float>& out, float pad = 0.0f)
		{
			const size_t cs = componentSize(view.componentType);
			out.resize(view.count * wanted);
			for (size_t i = 0; i < view.count; ++i)
			{
				const uint8_t* element = view.data + i * view.stride;
				for (int c = 0; c < wanted; ++c)
					out[i * wanted + c] = c < view.components ? readComponent(element + c * cs, view.componentType, view.normalized) : pad;
			}
		}

		void readIndices(const AccessorView& view, std::vector<uint32_t>& out)
		{
			out.resize(view.count);
			for (size_t i = 0; i < view.count; ++i)
			{
				const uint8_t* element = view.data + i * view.stride;
				switch (view.componentType)
				{
				case COMPONENT_UBYTE: out[i] = *element; break;
				case COMPONENT_USHORT: { uint16_t v; std::memcpy(&v, element, 2); out[i] = v; break; }
				default: { uint32_t v; std::memcpy(&v, element, 4); out[i] = v; break; }
				}
			}
		}

		// glTF is right-handed (+Y up); the engine is left-handed. Mirror Z.
		math::Vector3 toEngine(const math::Vector3& v) { return { v.x, v.y, -v.z }; }
		math::Quaternion toEngine(const math::Quaternion& q) { return { -q.x, -q.y, q.z, q.w }; }

		void generateNormals(ModelPrimitive& primitive)
		{
			primitive.normals.assign(primitive.positions.size(), math::Vector3::zero);
			for (size_t i = 0; i + 2 < primitive.indices.size(); i += 3)
			{
				const auto i0 = primitive.indices[i], i1 = primitive.indices[i + 1], i2 = primitive.indices[i + 2];
				if (i0 >= primitive.positions.size() || i1 >= primitive.positions.size() || i2 >= primitive.positions.size()) continue;
				const auto& p0 = primitive.positions[i0];
				const auto normal = math::Vector3::cross(primitive.positions[i1] - p0, primitive.positions[i2] - p0);
				primitive.normals[i0] += normal; primitive.normals[i1] += normal; primitive.normals[i2] += normal;
			}
			for (auto& n : primitive.normals)
			{
				if (n.sqrMagnitude() > 1e-12f) n.normalize(); else n = math::Vector3::up;
			}
		}

		bool importMaterials(Context& ctx, ModelData& out)
		{
			if (!ctx.doc.contains("materials")) return true;
			auto textureUri = [&](int textureIndex) -> std::string {
				if (!ctx.doc.contains("textures") || textureIndex < 0 || textureIndex >= int(ctx.doc["textures"].size())) return {};
				const auto& texture = ctx.doc["textures"][textureIndex];
				const int source = texture.value("source", -1);
				if (!ctx.doc.contains("images") || source < 0 || source >= int(ctx.doc["images"].size())) return {};
				const auto& image = ctx.doc["images"][source];
				if (!image.contains("uri")) { log::warning("glTF '{}': embedded images are not supported", out.source); return {}; }
				const std::string uri = percentDecode(image["uri"].get<std::string>());
				if (uri.rfind("data:", 0) == 0) { log::warning("glTF '{}': data URI images are not supported", out.source); return {}; }
				std::string path = ctx.assetDirectory.empty() ? uri : ctx.assetDirectory + "/" + uri;
				std::replace(path.begin(), path.end(), '\\', '/');
				return path;
			};
			size_t index = 0;
			for (const auto& material : ctx.doc["materials"])
			{
				ModelMaterial result;
				result.name = material.value("name", "material" + std::to_string(index));
				result.doubleSided = material.value("doubleSided", false);
				if (material.contains("pbrMetallicRoughness"))
				{
					const auto& pbr = material["pbrMetallicRoughness"];
					if (pbr.contains("baseColorFactor") && pbr["baseColorFactor"].is_array() && pbr["baseColorFactor"].size() == 4)
					{
						const auto& f = pbr["baseColorFactor"];
						result.baseColor = math::Vector4(f[0].get<float>(), f[1].get<float>(), f[2].get<float>(), f[3].get<float>());
					}
					if (pbr.contains("baseColorTexture")) result.baseColorTexture = textureUri(pbr["baseColorTexture"].value("index", -1));
					result.metallic = pbr.value("metallicFactor", 1.0f);
					result.roughness = pbr.value("roughnessFactor", 1.0f);
				}
				if (material.contains("emissiveFactor") && material["emissiveFactor"].is_array() && material["emissiveFactor"].size() == 3)
				{
					const auto& f = material["emissiveFactor"];
					result.emissive = math::Vector3(f[0].get<float>(), f[1].get<float>(), f[2].get<float>());
				}
				out.materials.push_back(std::move(result));
				++index;
			}
			return true;
		}

		bool importMeshes(Context& ctx, ModelData& out)
		{
			if (!ctx.doc.contains("meshes")) return true;
			size_t meshIndex = 0;
			for (const auto& mesh : ctx.doc["meshes"])
			{
				ModelMesh result;
				result.name = mesh.value("name", "mesh" + std::to_string(meshIndex));
				if (mesh.contains("primitives")) for (const auto& primitive : mesh["primitives"])
				{
					const int mode = primitive.value("mode", 4);
					if (mode != 4) { log::warning("glTF '{}': mesh '{}' primitive mode {} skipped (only triangles)", out.source, result.name, mode); continue; }
					if (!primitive.contains("attributes") || !primitive["attributes"].contains("POSITION")) continue;
					const auto& attributes = primitive["attributes"];
					ModelPrimitive data;
					data.material = primitive.value("material", -1);

					AccessorView view;
					std::vector<float> floats;
					if (!accessor(ctx, attributes["POSITION"].get<int>(), view)) return false;
					readFloats(view, 3, floats);
					data.positions.resize(view.count);
					for (size_t i = 0; i < view.count; ++i) data.positions[i] = toEngine(math::Vector3(floats[i * 3], floats[i * 3 + 1], floats[i * 3 + 2]));

					if (attributes.contains("NORMAL") && accessor(ctx, attributes["NORMAL"].get<int>(), view) && view.count == data.positions.size())
					{
						readFloats(view, 3, floats);
						data.normals.resize(view.count);
						for (size_t i = 0; i < view.count; ++i) data.normals[i] = toEngine(math::Vector3(floats[i * 3], floats[i * 3 + 1], floats[i * 3 + 2])).normalized();
					}
					ctx.error.clear();
					if (attributes.contains("TEXCOORD_0") && accessor(ctx, attributes["TEXCOORD_0"].get<int>(), view) && view.count == data.positions.size())
					{
						readFloats(view, 2, floats);
						data.uvs.resize(view.count);
						for (size_t i = 0; i < view.count; ++i) data.uvs[i] = math::Vector2(floats[i * 2], floats[i * 2 + 1]);
					}
					ctx.error.clear();
					if (attributes.contains("COLOR_0") && accessor(ctx, attributes["COLOR_0"].get<int>(), view) && view.count == data.positions.size())
					{
						readFloats(view, 4, floats, 1.0f);
						data.colors.resize(view.count);
						for (size_t i = 0; i < view.count; ++i) data.colors[i] = math::Vector4(floats[i * 4], floats[i * 4 + 1], floats[i * 4 + 2], floats[i * 4 + 3]);
					}
					ctx.error.clear();
					if (data.uvs.size() != data.positions.size()) data.uvs.assign(data.positions.size(), math::Vector2(0.0f, 0.0f));

					if (primitive.contains("indices"))
					{
						if (!accessor(ctx, primitive["indices"].get<int>(), view)) return false;
						readIndices(view, data.indices);
					}
					else
					{
						data.indices.resize(data.positions.size());
						for (size_t i = 0; i < data.indices.size(); ++i) data.indices[i] = uint32_t(i);
					}
					// Mirroring flips the winding; swap to keep front faces consistent.
					for (size_t i = 0; i + 2 < data.indices.size(); i += 3) std::swap(data.indices[i + 1], data.indices[i + 2]);
					for (auto index : data.indices)
						if (index >= data.positions.size()) { ctx.error = "mesh '" + result.name + "' has out-of-range indices"; return false; }
					if (data.normals.size() != data.positions.size()) generateNormals(data);
					result.primitives.push_back(std::move(data));
				}
				out.meshes.push_back(std::move(result));
				++meshIndex;
			}
			return true;
		}

		bool importNodes(Context& ctx, ModelData& out)
		{
			if (!ctx.doc.contains("nodes")) return true;
			const auto& nodes = ctx.doc["nodes"];
			out.nodes.resize(nodes.size());
			for (size_t i = 0; i < nodes.size(); ++i)
			{
				const auto& node = nodes[i];
				auto& result = out.nodes[i];
				result.name = node.value("name", "node" + std::to_string(i));
				result.mesh = node.value("mesh", -1);
				if (result.mesh >= int(out.meshes.size())) result.mesh = -1;
				if (node.contains("matrix") && node["matrix"].is_array() && node["matrix"].size() == 16)
				{
					// glTF stores column-major; loading it row-major yields the row-vector matrix DirectX uses.
					DirectX::XMFLOAT4X4 matrix;
					float* target = &matrix._11;
					for (size_t k = 0; k < 16; ++k) target[k] = node["matrix"][k].get<float>();
					DirectX::XMVECTOR scale, rotation, translation;
					if (DirectX::XMMatrixDecompose(&scale, &rotation, &translation, DirectX::XMLoadFloat4x4(&matrix)))
					{
						result.scale = math::Vector3(scale);
						result.rotation = toEngine(math::Quaternion(rotation));
						result.translation = toEngine(math::Vector3(translation));
					}
				}
				else
				{
					if (node.contains("translation")) { const auto& t = node["translation"]; result.translation = toEngine(math::Vector3(t[0].get<float>(), t[1].get<float>(), t[2].get<float>())); }
					if (node.contains("rotation")) { const auto& r = node["rotation"]; result.rotation = toEngine(math::Quaternion(r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>())); }
					if (node.contains("scale")) { const auto& s = node["scale"]; result.scale = math::Vector3(s[0].get<float>(), s[1].get<float>(), s[2].get<float>()); }
				}
				if (node.contains("children")) for (const auto& child : node["children"])
				{
					const int childIndex = child.get<int>();
					if (childIndex < 0 || childIndex >= int(nodes.size()) || childIndex == int(i)) continue;
					result.children.push_back(childIndex);
				}
			}
			for (size_t i = 0; i < out.nodes.size(); ++i)
				for (int child : out.nodes[i].children) out.nodes[child].parent = int(i);
			if (ctx.doc.contains("scenes") && !ctx.doc["scenes"].empty())
			{
				const int sceneIndex = ctx.doc.value("scene", 0);
				const auto& scene = ctx.doc["scenes"][std::clamp(sceneIndex, 0, int(ctx.doc["scenes"].size()) - 1)];
				if (scene.contains("nodes")) for (const auto& root : scene["nodes"])
				{
					const int index = root.get<int>();
					if (index >= 0 && index < int(out.nodes.size())) out.roots.push_back(index);
				}
			}
			if (out.roots.empty())
				for (size_t i = 0; i < out.nodes.size(); ++i) if (out.nodes[i].parent < 0) out.roots.push_back(int(i));
			return true;
		}

		bool importAnimations(Context& ctx, ModelData& out)
		{
			if (!ctx.doc.contains("animations")) return true;
			size_t animationIndex = 0;
			for (const auto& animation : ctx.doc["animations"])
			{
				ModelAnimation result;
				result.name = animation.value("name", "animation" + std::to_string(animationIndex));
				if (!animation.contains("samplers") || !animation.contains("channels")) { out.animations.push_back(std::move(result)); ++animationIndex; continue; }
				const auto& samplers = animation["samplers"];
				for (const auto& channel : animation["channels"])
				{
					const int samplerIndex = channel.value("sampler", -1);
					if (samplerIndex < 0 || samplerIndex >= int(samplers.size()) || !channel.contains("target")) continue;
					const auto& target = channel["target"];
					const int node = target.value("node", -1);
					const std::string path = target.value("path", std::string());
					if (node < 0 || node >= int(out.nodes.size())) continue;
					AnimationChannel data;
					data.node = node; data.nodeName = out.nodes[node].name;
					if (path == "translation") data.path = AnimationPath::Translation;
					else if (path == "rotation") data.path = AnimationPath::Rotation;
					else if (path == "scale") data.path = AnimationPath::Scale;
					else continue; // weights (morph targets) are not supported
					const auto& sampler = samplers[samplerIndex];
					const std::string interpolation = sampler.value("interpolation", std::string("LINEAR"));
					const bool cubic = interpolation == "CUBICSPLINE";
					data.interpolation = interpolation == "STEP" ? AnimationInterpolation::Step : AnimationInterpolation::Linear;
					if (cubic) log::warning("glTF '{}': animation '{}' uses CUBICSPLINE, sampled as LINEAR", out.source, result.name);
					AccessorView input, output;
					if (!accessor(ctx, sampler.value("input", -1), input) || !accessor(ctx, sampler.value("output", -1), output)) return false;
					std::vector<float> times, values;
					readFloats(input, 1, times);
					const int components = data.path == AnimationPath::Rotation ? 4 : 3;
					readFloats(output, components, values, data.path == AnimationPath::Rotation ? 1.0f : 0.0f);
					const size_t keys = times.size();
					const size_t perKey = cubic ? 3 : 1;
					if (values.size() < keys * perKey * components) { ctx.error = "animation '" + result.name + "': output accessor too short"; return false; }
					data.times = times;
					data.values.resize(keys);
					for (size_t k = 0; k < keys; ++k)
					{
						const float* v = values.data() + (k * perKey + (cubic ? 1 : 0)) * components;
						if (data.path == AnimationPath::Rotation)
						{
							const auto q = toEngine(math::Quaternion(v[0], v[1], v[2], v[3])).normalized();
							data.values[k] = math::Vector4(q.x, q.y, q.z, q.w);
						}
						else if (data.path == AnimationPath::Translation)
						{
							const auto t = toEngine(math::Vector3(v[0], v[1], v[2]));
							data.values[k] = math::Vector4(t.x, t.y, t.z, 0.0f);
						}
						else data.values[k] = math::Vector4(v[0], v[1], v[2], 0.0f);
					}
					if (!times.empty()) result.duration = (std::max)(result.duration, times.back());
					result.channels.push_back(std::move(data));
				}
				out.animations.push_back(std::move(result));
				++animationIndex;
			}
			return true;
		}
	}

	bool GltfImporter::import(const std::filesystem::path& physicalPath, const std::string& assetPath, ModelData& out, std::string& error)
	{
		Context ctx;
		ctx.directory = physicalPath.parent_path();
		const auto slash = assetPath.find_last_of("/\\");
		ctx.assetDirectory = slash == std::string::npos ? std::string() : assetPath.substr(0, slash);

		std::vector<uint8_t> file;
		if (!readFileBytes(physicalPath, file)) { error = "cannot read file"; return false; }
		std::vector<uint8_t> bin;
		if (supports(".glb") && file.size() >= 4 && std::memcmp(file.data(), "glTF", 4) == 0)
		{
			if (!parseGlb(file, ctx.doc, bin, error)) return false;
		}
		else
		{
			ctx.doc = Json::parse(file.begin(), file.end(), nullptr, false);
			if (ctx.doc.is_discarded() || !ctx.doc.is_object()) { error = "not valid glTF JSON"; return false; }
		}
		if (ctx.doc.contains("asset") && ctx.doc["asset"].contains("version"))
		{
			const auto version = ctx.doc["asset"]["version"].get<std::string>();
			if (version.rfind("2", 0) != 0) { error = "unsupported glTF version " + version; return false; }
		}
		try
		{
			if (!loadBuffers(ctx, std::move(bin)) || !importMaterials(ctx, out) || !importMeshes(ctx, out) ||
				!importNodes(ctx, out) || !importAnimations(ctx, out))
			{
				error = ctx.error; return false;
			}
		}
		catch (const std::exception& exception)
		{
			error = std::string("malformed glTF: ") + exception.what();
			return false;
		}
		return true;
	}
}
