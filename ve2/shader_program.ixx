module;

#include <gl/glew.h>
#include <glfw/glfw3.h>

#include <string>
#include <iostream>
#include <unordered_map>
#include <initializer_list>

export module shader_program;

using namespace std;

export struct ShaderProgram
{
	int program_name;
	unordered_map<string, int> uniform_locations;
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

	return result;
}