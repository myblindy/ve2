module;
#include <memory>
#include <vector>
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
		vec2 position, uv;
		vec4 color;

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

	unique_ptr<VertexArray<Vertex>> vertex_array;
	vector<Vertex> vertex_cache;
	GLFWframebuffersizefun previous_framebuffer_size_callback;
	ivec2 framebuffer_size;
}

using namespace priv;

void framebuffer_size_callback(GLFWwindow* window, int x, int y)
{
	framebuffer_size = { x, y };
	if (previous_framebuffer_size_callback) previous_framebuffer_size_callback(window, x, y);
}

export int gui_init(GLFWwindow* window)
{
	vertex_array = VertexArray<Vertex>::create_growable();
	previous_framebuffer_size_callback = glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	glfwGetFramebufferSize(window, &framebuffer_size.x, &framebuffer_size.y);

	return 0;
}

void quad(ivec2 position, ivec2 size, vec4 color)
{
}

export int gui_slider(ivec2 position, ivec2 size, double min, double max, double val)
{
	// the outer rectangle
	quad(position, size, vec4(1, 0, 0, 1));

	// the thumb
	double percentage = (val - min) / (max - min);
	quad(vec2((size.x - size.y) * percentage + position.x + size.y / 2, position.y), vec2(size.y, size.y), vec4(1, 1, 1, 1));

	return 0;
}