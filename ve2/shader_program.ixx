module;

#include <gl/glew.h>
#include <glfw/glfw3.h>

#include <string>
#include <iostream>
#include <unordered_map>
#include <initializer_list>
#include <memory>

export module shader_program;

using namespace std;

export template<typename T>
struct UniformBufferObject
{
	GLuint object_name;
	T data{};

	static unique_ptr<UniformBufferObject<T>> create()
	{
		GLuint name{};
		glCreateBuffers(1, &name);
		auto p = new UniformBufferObject<T>(name);
		glNamedBufferData(p->object_name, sizeof(T), nullptr, GL_DYNAMIC_DRAW);

		return unique_ptr<UniformBufferObject<T>>(p);
	}

	static unique_ptr<UniformBufferObject<T>> create(const T& data)
	{
		GLuint name{};
		glCreateBuffers(1, &name);
		auto p = new UniformBufferObject<T>(name);
		p->data = data;
		glNamedBufferData(p->object_name, sizeof(T), &data, GL_DYNAMIC_DRAW);

		return unique_ptr<UniformBufferObject<T>>(p);
	}

	void bind(GLuint binding_point)
	{
		glBindBufferBase(GL_UNIFORM_BUFFER, binding_point, object_name);
	}

	void update()
	{
		glNamedBufferSubData(object_name, 0, sizeof(T), &data);
	}

private:
	UniformBufferObject(GLuint object_name) : object_name(object_name) {}
};

export struct ShaderProgram
{
	int program_name;
	unordered_map<string, int> uniform_locations;

	void uniform_block_binding(const GLuint block_index, const GLuint binding_point) { glUniformBlockBinding(program_name, block_index, binding_point); }

	void use() { glUseProgram(program_name); }
};

export enum class ShaderType { Vertex = GL_VERTEX_SHADER, Fragment = GL_FRAGMENT_SHADER };
export GLuint compile_shader_from_source(const string source, const ShaderType type)
{
	const auto shader = glCreateShader(static_cast<GLenum>(type));

	auto data = source.data();
	glShaderSource(shader, 1, &data, nullptr);

	glCompileShader(shader);

	GLint status{};
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status)
	{
		GLint size{};
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);

		string info_log;
		info_log.resize(size);
		glGetShaderInfoLog(shader, size, nullptr, info_log.data());

		cerr << "Shader compilation errors:\n" << info_log << endl;
		return -1;
	}

	return shader;
}

export unique_ptr<ShaderProgram> link_shader_program_from_shader_objects(initializer_list<GLuint> shader_names)
{
	for (auto shader_name : shader_names)
		if (shader_name == UINT32_MAX)
			return nullptr;

	const auto shader_program = glCreateProgram();

	for (auto shader_name : shader_names)
		glAttachShader(shader_program, shader_name);

	glLinkProgram(shader_program);

	GLint success{};
	glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
	if (!success)
	{
		GLint size{};
		glGetProgramiv(shader_program, GL_INFO_LOG_LENGTH, &size);

		string info_log;
		info_log.resize(size);
		glGetProgramInfoLog(shader_program, size, nullptr, info_log.data());

		cerr << "Program compilation errors:\n" << info_log << endl;
		return nullptr;
	}

	for (auto shader_name : shader_names)
		glDeleteShader(shader_name);

	auto result = make_unique<ShaderProgram>();
	result->program_name = shader_program;

	GLint uniforms_count{};
	glGetProgramiv(shader_program, GL_ACTIVE_UNIFORMS, &uniforms_count);

	for (int i = 0; i < uniforms_count; ++i)
	{
		char name[60]{};
		glGetActiveUniformName(shader_program, i, sizeof(name), nullptr, name);
		result->uniform_locations[name] = glGetUniformLocation(shader_program, name);
	}

	GLint blocks_count{};
	glGetProgramiv(shader_program, GL_ACTIVE_UNIFORM_BLOCKS, &blocks_count);

	for (int i = 0; i < blocks_count; ++i)
	{
		char name[60]{};
		glGetActiveUniformBlockName(shader_program, i, sizeof(name), NULL, name);
		result->uniform_locations[name] = glGetUniformBlockIndex(shader_program, name);
	}

	return result;
}