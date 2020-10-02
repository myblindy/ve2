module;

#include <gl/glew.h>
#include <glfw/glfw3.h>
#include <memory>

using namespace std;

export module vertex_array;

template<typename T>
concept HasSetupGlArrayAttributeFunction = requires (T t) { T::setup_gl_array_attributes((GLuint)0); };

export template<typename TVertex> requires HasSetupGlArrayAttributeFunction<TVertex>
struct VertexArray
{
	GLuint vertex_buffer_object_name, vertex_array_object_name;

	template<size_t N>
	void update(const TVertex(&vertices)[N])
	{
		throw_on_invalid_permissions(N > buffer_capacity, true);

		buffer_size = N;
		if (buffer_capacity < buffer_size) throw exception();

		memcpy(data, vertices, sizeof(TVertex) * buffer_size);
	}

	template<typename iterator>
	void update(const iterator begin, const iterator end)
	{
		throw_on_invalid_permissions(end - begin > buffer_capacity, true);

		buffer_size = end - begin;
		if (buffer_capacity < buffer_size) throw exception();

		memcpy(data, &*begin, sizeof(TVertex) * buffer_size);
	}

	size_t size() { return buffer_size; }
	size_t capacity() { return buffer_capacity; }

	template<size_t N>
	static unique_ptr<VertexArray<TVertex>> create(const TVertex(&vertices)[N])
	{
		auto ret = new VertexArray<TVertex>();
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
		ret->can_grow = ret->can_update = true;
		ret->buffer_capacity = initial_size;

		glCreateBuffers(1, &ret->vertex_buffer_object_name);
		glNamedBufferStorage(ret->vertex_buffer_object_name, sizeof(TVertex) * initial_size, nullptr, 
			GL_MAP_WRITE_BIT | GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT | GL_DYNAMIC_STORAGE_BIT);

		ret->data = (TVertex *)glMapNamedBufferRange(ret->vertex_buffer_object_name, 0, sizeof(TVertex) * initial_size,
			GL_MAP_WRITE_BIT | GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);

		glCreateVertexArrays(1, &ret->vertex_array_object_name);
		glVertexArrayVertexBuffer(ret->vertex_array_object_name, 0, ret->vertex_buffer_object_name, 0, sizeof(TVertex));

		TVertex::setup_gl_array_attributes(ret->vertex_array_object_name);

		return unique_ptr<VertexArray<TVertex>>(ret);
	}

private:
	void throw_on_invalid_permissions(bool needs_to_grow, bool needs_to_update)
	{
		if (needs_to_grow && !can_grow) throw exception("Tried to grow a non-growable buffer");
		if (needs_to_update && !can_update) throw exception("Tried to update a non-updateable buffer");
	}

	VertexArray() = default;
	bool can_grow{}, can_update{};
	size_t buffer_size{}, buffer_capacity{};
	TVertex* data{};
};
