module;

extern "C"
{
#include <gl/glew.h>
#include <glfw/glfw3.h>
}

#include <string>
#include <iostream>

export module shader_programs;

export enum class ShaderType { Vertex = GL_VERTEX_SHADER, Fragment = GL_FRAGMENT_SHADER };
export GLuint compile_shader_from_source(const std::string source, const ShaderType type)
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

		std::string info_log;
		info_log.reserve(size);
		glGetShaderInfoLog(shader, size, nullptr, info_log.data());

		std::cerr << "Shader compilation errors:\n" << info_log;
		return -1;
	}

	return shader;
}

template<class... Tail>
void attach_shaders(const GLuint shader_program, const GLuint head_shader, Tail... tail_shaders)
{
	glAttachShader(shader_program, head_shader);
	if constexpr (sizeof...(tail_shaders))
		attach_shaders(shader_program, tail_shaders...);
}

template<class... Tail>
void delete_shaders(const GLuint head_shader, Tail... tail_shaders)
{
	glDeleteShader(head_shader);
	if constexpr (sizeof...(tail_shaders))
		delete_shaders(tail_shaders...);
}

template<class Head, class... Tail>
using are_same = std::conjunction<std::is_same<Head, Tail>...>;

export template<class... Tail, class = std::enable_if_t<are_same<GLuint, Tail...>::value, void>>
GLuint link_shader_program_from_shader_objects(GLuint head_shader, Tail... tail_shaders)
{
	const auto shader_program = glCreateProgram();

	attach_shaders(shader_program, head_shader, tail_shaders...);

	glLinkProgram(shader_program);

	GLint success{};
	glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
	if (!success)
	{
		GLint size{};
		glGetProgramiv(shader_program, GL_INFO_LOG_LENGTH, &size);

		std::string info_log;
		info_log.reserve(size);
		glGetProgramInfoLog(shader_program, size, nullptr, info_log.data());

		return -1;
	}

	delete_shaders(head_shader, tail_shaders...);

	return shader_program;
}