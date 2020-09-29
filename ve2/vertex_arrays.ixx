module;

#include <gl/glew.h>
#include <glfw/glfw3.h>
#include <memory>

using namespace std;

export module vertex_arrays;

template<typename T>
concept HasSetupGlArrayAttributeFunction = requires (T t) { T::setup_gl_array_attributes((GLuint)0); };

export template<typename TVertex> requires HasSetupGlArrayAttributeFunction<TVertex>
struct VertexArray
{
	GLuint vertex_buffer_object_name, vertex_array_object_name;

	template<size_t N>
	void update_data(const TVertex(&vertices)[N])
	{
		glMapNamedBufferRange(vertex_buffer_object_name, 0, sizeof(TVertex) * N, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
	}

	size_t size() { return buffer_size; }
	size_t capacity() { return buffer_capacity; }

	template<size_t N>
	static unique_ptr<VertexArray<TVertex>> create(const TVertex(&vertices)[N])
	{
		auto ret = new VertexArray<TVertex>();
		ret->growable = false;
		ret->buffer_size = ret->buffer_capacity = N;

		glCreateBuffers(1, &ret->vertex_buffer_object_name);
		glNamedBufferStorage(ret->vertex_buffer_object_name, sizeof(vertices), vertices, 0);

		glCreateVertexArrays(1, &ret->vertex_array_object_name);
		glVertexArrayVertexBuffer(ret->vertex_array_object_name, 0, ret->vertex_buffer_object_name, 0, sizeof(*vertices));

		TVertex::setup_gl_array_attributes(ret->vertex_array_object_name);

		return unique_ptr<VertexArray<TVertex>>(ret);
	}

	static unique_ptr<VertexArray<TVertex>> create_growable(size_t initial_size = 128)
	{
		auto ret = new VertexArray<TVertex>();
		ret->growable = true;
		ret->buffer_capacity = initial_size;

		glCreateBuffers(1, &ret->vertex_buffer_object_name);
		glNamedBufferStorage(ret->vertex_buffer_object_name, sizeof(TVertex) * initial_size, nullptr, GL_STREAM_DRAW);

		glCreateVertexArrays(1, &ret->vertex_array_object_name);
		glVertexArrayVertexBuffer(ret->vertex_array_object_name, 0, ret->vertex_buffer_object_name, 0, sizeof(TVertex));

		TVertex::setup_gl_array_attributes(ret->vertex_array_object_name);

		return unique_ptr<VertexArray<TVertex>>(ret);
	}

private:
	VertexArray() = default;
	bool growable{};
	size_t buffer_size{}, buffer_capacity{};
};
