module;

#include <gl/glew.h>
#include <glfw/glfw3.h>
#include <memory>

using namespace std;

export module vertex_arrays;

export struct VertexArray
{
	GLuint vertex_buffer_object_name, vertex_array_object_name;
};

template<typename T>
concept HasSetupGlArrayAttributeFunction = requires (T t) { T::setup_gl_array_attributes((GLuint)0); };

export template<typename TVertex, size_t N> requires HasSetupGlArrayAttributeFunction<TVertex>
unique_ptr<VertexArray> create_vertex_array(const TVertex (&vertices)[N])
{
	auto ret = make_unique<VertexArray>();

	glCreateBuffers(1, &ret->vertex_buffer_object_name);
	glNamedBufferStorage(ret->vertex_buffer_object_name, sizeof(vertices), vertices, 0);

	glCreateVertexArrays(1, &ret->vertex_array_object_name);
	glVertexArrayVertexBuffer(ret->vertex_array_object_name, 0, ret->vertex_buffer_object_name, 0, sizeof(*vertices));

	TVertex::setup_gl_array_attributes(ret->vertex_array_object_name);

	return ret;
}