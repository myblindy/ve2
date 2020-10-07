module;
#include <string>
#include <memory>
#include <vector>
#include <optional>
#include <algorithm>
#include <glm/glm.hpp>
#include <gl/glew.h>
#include <glfw/glfw3.h>
#include "sdf_font.h"

export module gui;

import shader_program;
import vertex_array;
import utilities;

using namespace std;
using namespace glm;

namespace priv
{
#pragma pack(push, 1)
	struct Vertex
	{
		vec2 position;
		vec2 uv;
		vec4 color;

		Vertex(const vec2& position, const vec2& uv, const vec4& color)
			:position(position), uv(uv), color(color)
		{
		}

		static void setup_gl_array_attributes(GLuint vertex_array_object_name)
		{
			glEnableVertexArrayAttrib(vertex_array_object_name, 0);
			glVertexArrayAttribFormat(vertex_array_object_name, 0, 2, GL_FLOAT, false, offsetof(Vertex, position));
			glVertexArrayAttribBinding(vertex_array_object_name, 0, 0);

			glEnableVertexArrayAttrib(vertex_array_object_name, 1);
			glVertexArrayAttribFormat(vertex_array_object_name, 1, 2, GL_FLOAT, false, offsetof(Vertex, uv));
			glVertexArrayAttribBinding(vertex_array_object_name, 1, 0);

			glEnableVertexArrayAttrib(vertex_array_object_name, 2);
			glVertexArrayAttribFormat(vertex_array_object_name, 2, 4, GL_FLOAT, false, offsetof(Vertex, color));
			glVertexArrayAttribBinding(vertex_array_object_name, 2, 0);
		}
	};
#pragma pack(pop)

	unique_ptr<VertexArray<Vertex>> vertex_array;
	unique_ptr<ShaderProgram> shader_program;
	vector<Vertex> vertex_cache;									// the vertex cache that stores the gui vertices as they are being build
	optional<uint64_t> last_vertex_cache_hash;						// the hash of the vertex cache that was previously uploaded, if any
	GLFWframebuffersizefun previous_framebuffer_size_callback;
	vec2 framebuffer_size{}, previous_framebuffer_size{};
	shared_ptr<Font> font;
}

using namespace priv;

// hashing functions
namespace std
{
	template<>
	struct hash<Vertex>
	{
		size_t operator()(Vertex const& vertex) const noexcept
		{
			const hash<float> hash_float;

			return (((((((239811 + hash_float(vertex.position.x) * 324871) + hash_float(vertex.position.y) * 324871) +
				hash_float(vertex.uv.x) * 324871) + hash_float(vertex.uv.y) * 324871) +
				hash_float(vertex.color.r) * 324871) + hash_float(vertex.color.g) * 324871) + hash_float(vertex.color.b) * 324871) + hash_float(vertex.color.a) * 324871;
		}
	};

	template<>
	struct hash<vector<Vertex>>
	{
		size_t operator()(vector<Vertex> const& vertices) const noexcept
		{
			const hash<Vertex> hash_vertex;
			size_t res = 239811;

			for (const auto& vertex : vertices)
				res = (res + hash_vertex(vertex) * 324871);

			return res;
		}
	};
}

void framebuffer_size_callback(GLFWwindow* window, int x, int y)
{
	framebuffer_size = { x, y };
	if (previous_framebuffer_size_callback) previous_framebuffer_size_callback(window, x, y);
}

export int gui_init(GLFWwindow* window, shared_ptr<Font> _font)
{
	vertex_array = VertexArray<Vertex>::create_growable();
	font = _font;

	shader_program = link_shader_program_from_shader_objects(
		{
			compile_shader_from_source("\
				#version 460 \n\
				uniform vec2 window_size; \n\
				\n\
				in vec2 position; \n\
				in vec2 uv; \n\
				in vec4 color; \n\
				\n\
				out vec2 fs_uv; \n\
				out vec4 fs_color; \n\
				void main() \n\
				{ \n\
					fs_uv = uv; \n\
					fs_color = color; \n\
					gl_Position = vec4(position.x / window_size.x * 2.0 - 1.0, position.y / window_size.y * 2.0 - 1.0, 0, 1); \n\
				}", ShaderType::Vertex),
			compile_shader_from_source("\
				#version 460 \n\
				uniform sampler2D tex; \n\
				\n\
				in vec2 fs_uv; \n\
				in vec4 fs_color; \n\
				\n\
				out vec4 out_color; \n\
				\n\
				void main() \n\
				{ \n\
					if(fs_uv.s >= 0) \n\
					{ \n\
						const vec3 outline_color = vec3(0, 0, 0); \n\
						const float smoothing = 1.0/16.0; \n\
						const float outlineWidth = 3.0 / 16.0; \n\
						const float outerEdgeCenter = 0.5 - outlineWidth; \n\
						\n\
						float distance = texture(tex, fs_uv).r; \n\
						float alpha = smoothstep(outerEdgeCenter - smoothing, outerEdgeCenter + smoothing, distance); \n\
						float border = smoothstep(0.5 - smoothing, 0.5 + smoothing, distance); \n\
						out_color = vec4(mix(outline_color.rgb, fs_color.rgb, border), alpha); \n\
					} \n\
					else \n\
						out_color = fs_color; \n\
				}", ShaderType::Fragment)
		});
	glProgramUniform1i(shader_program->program_name, shader_program->uniform_locations["tex"], 0);

	// register a callback to keep the window size updated
	previous_framebuffer_size_callback = glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

	int x, y;
	glfwGetFramebufferSize(window, &x, &y);
	framebuffer_size = { x, y };

	// cold cache some useful characters
	font->get_glyph_data(u8"0123456789");

	return 0;
}

export int gui_render()
{
	auto vertex_cache_hash = hash<vector<Vertex>>{}(vertex_cache);
	if (!last_vertex_cache_hash || *last_vertex_cache_hash != vertex_cache_hash)
	{
		// the vertices changed, upload to the vertex buffer
		vertex_array->update(vertex_cache.begin(), vertex_cache.end());
		last_vertex_cache_hash = vertex_cache_hash;
	}

	glUseProgram(shader_program->program_name);
	if (previous_framebuffer_size != framebuffer_size)
	{
		// the framebuffer size has changed, upload it to the program
		glUniform2f(shader_program->uniform_locations["window_size"], framebuffer_size.x, framebuffer_size.y);
		previous_framebuffer_size = framebuffer_size;
	}

	if (vertex_array->size())
	{
		glActiveTexture(GL_TEXTURE0);
		font->bind();
		glBindVertexArray(vertex_array->vertex_array_object_name);
		glDrawArrays(GL_TRIANGLES, 0, vertex_array->size());
	}

	vertex_cache.clear();

	return 0;
}

box2 uv_no_texture{ -1, -1 };
void quad(const vec2& position, const vec2& size, const box2& uv, const vec4& color)
{
	vertex_cache.emplace_back(vec2(position.x, position.y), uv.v0, color);
	vertex_cache.emplace_back(vec2(position.x + size.x, position.y + size.y), uv.v1, color);
	vertex_cache.emplace_back(vec2(position.x + size.x, position.y), vec2(uv.v1.x, uv.v0.y), color);

	vertex_cache.emplace_back(vec2(position.x, position.y + size.y), vec2(uv.v0.x, uv.v1.y), color);
	vertex_cache.emplace_back(vec2(position.x + size.x, position.y + size.y), uv.v1, color);
	vertex_cache.emplace_back(vec2(position.x, position.y), uv.v0, color);
}

export int gui_slider(const vec2& position, const vec2& size, const double min, const double max, const double val)
{
	// the outer rectangle
	quad(position, size, uv_no_texture, vec4(0, 1, 0, 1));

	// the thumb
	double percentage = (val - min) / (max - min);
	quad(vec2((size.x - size.y) * percentage + position.x, position.y), vec2(size.y, size.y), uv_no_texture, vec4(1, 1, 1, 1));

	return 0;
}

export int gui_label(const vec2& position, const u8string& s, const float scale = 1.0f)
{
	float x = position.x;
	const auto glyphs = font->get_glyph_data(s);

	// find the max bearing
	auto max_bearing_y = max_element(glyphs.begin(), glyphs.end(), [](auto& x, auto& y) {return x.bearing_y < y.bearing_y; })->bearing_y;

	// add a quad for each character, aligned on the baseline based on the bearing
	for (const auto& glyph : glyphs)
	{
		const float adv = glyph.advance;
		quad({ x + glyph.left * scale, position.y + static_cast<float>(max_bearing_y - glyph.bearing_y) * scale },
			{ glyph.width * scale, glyph.height * scale }, glyph.uv, vec4(1, 1, 1, 1));
		x += adv * scale;
	}

	return 0;
}