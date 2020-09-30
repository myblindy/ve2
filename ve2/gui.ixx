module;
#include <string>
#include <memory>
#include <vector>
#include <optional>
#include <glm/glm.hpp>
#include <gl/glew.h>
#include <glfw/glfw3.h>

export module gui;

import shader_programs;
import vertex_arrays;

using namespace std;
using namespace glm;

namespace priv
{
	struct Vertex
	{
		ivec2 position;
		vec2 uv;
		vec4 color;

		Vertex(const ivec2& position, const vec2& uv, const vec4& color)
			:position(position), uv(uv), color(color)
		{
		}

		static void setup_gl_array_attributes(GLuint vertex_array_object_name)
		{
			glEnableVertexArrayAttrib(vertex_array_object_name, 0);
			glVertexArrayAttribFormat(vertex_array_object_name, 0, 2, GL_INT, false, offsetof(Vertex, position));
			glVertexArrayAttribBinding(vertex_array_object_name, 0, 0);

			glEnableVertexArrayAttrib(vertex_array_object_name, 1);
			glVertexArrayAttribFormat(vertex_array_object_name, 1, 2, GL_FLOAT, false, offsetof(Vertex, uv));
			glVertexArrayAttribBinding(vertex_array_object_name, 1, 0);

			glEnableVertexArrayAttrib(vertex_array_object_name, 2);
			glVertexArrayAttribFormat(vertex_array_object_name, 2, 4, GL_FLOAT, false, offsetof(Vertex, color));
			glVertexArrayAttribBinding(vertex_array_object_name, 2, 0);
		}
	};

	unique_ptr<VertexArray<Vertex>> vertex_array;
	unique_ptr<ShaderProgram> shader_program;
	vector<Vertex> vertex_cache;									// the vertex cache that stores the gui vertices as they are being build
	optional<uint64_t> last_vertex_cache_hash;						// the hash of the vertex cache that was previously uploaded, if any
	GLFWframebuffersizefun previous_framebuffer_size_callback;
	ivec2 framebuffer_size{}, previous_framebuffer_size{};
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
			const hash<int> hash_int;
			const hash<float> hash_float;

			return (((((((239811 + hash_int(vertex.position.x) * 324871) + hash_int(vertex.position.y) * 324871) +
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

export int gui_init(GLFWwindow* window)
{
	vertex_array = VertexArray<Vertex>::create_growable();

	shader_program = link_shader_program_from_shader_objects(
		{
			compile_shader_from_source("\
				#version 460 \n\
				uniform ivec2 window_size; \n\
				\n\
				in ivec2 position; \n\
				in vec2 uv; \n\
				in vec4 color; \n\
				\n\
				out vec2 fs_uv; \n\
				out vec4 fs_color; \n\
				void main() \n\
				{ \n\
					fs_uv = uv; \n\
					fs_color = color; \n\
					gl_Position = vec4(position / window_size, 0, 1); \n\
				}", ShaderType::Vertex),
			compile_shader_from_source("\
				#version 460 \n\
				in vec2 fs_uv; \n\
				in vec4 fs_color; \n\
				\n\
				out vec4 out_color; \n\
				\n\
				void main() \n\
				{ \n\
					out_color = fs_color; \n\
				}", ShaderType::Fragment)
		});

	// register a callback to keep the window size updated
	previous_framebuffer_size_callback = glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	glfwGetFramebufferSize(window, &framebuffer_size.x, &framebuffer_size.y);

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
		glUniform2iv(shader_program->uniform_locations["window_size"], 1, &framebuffer_size.x);
		previous_framebuffer_size = framebuffer_size;
	}

	if (vertex_array->size())
		glDrawArrays(GL_TRIANGLES, 0, vertex_array->size());

	vertex_cache.clear();

	return 0;
}

void quad(ivec2 position, ivec2 size, vec4 color)
{
	vertex_cache.emplace_back(ivec2(position.x, position.y), vec2(0, 0), color);
	vertex_cache.emplace_back(ivec2(position.x + 1, position.y), vec2(1, 0), color);
	vertex_cache.emplace_back(ivec2(position.x + 1, position.y + 1), vec2(1, 1), color);

	vertex_cache.emplace_back(ivec2(position.x + 1, position.y), vec2(1, 0), color);
	vertex_cache.emplace_back(ivec2(position.x + 1, position.y + 1), vec2(1, 1), color);
	vertex_cache.emplace_back(ivec2(position.x, position.y + 1), vec2(0, 1), color);
}

export int gui_slider(const ivec2& position, const ivec2& size, const double min, const double max, const double val)
{
	// the outer rectangle
	quad(position, size, vec4(1, 0, 0, 1));

	// the thumb
	double percentage = (val - min) / (max - min);
	quad(vec2((size.x - size.y) * percentage + position.x + size.y / 2, position.y), vec2(size.y, size.y), vec4(1, 1, 1, 1));

	return 0;
}