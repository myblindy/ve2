// ve2.cpp : Defines the entry point for the application.
//

import shader_programs;
#include "framework.h"

using namespace std;
using namespace glm;

#define CHECK_SUCCESS(cmd, errormsg) if(!(cmd)) { cerr << errormsg << "\n"; return -1; }
char av_error_buffer[2048];
#define CHECK_AV_SUCCESS(cmd) [[gsl::suppress(bounds.3)]] { const int __res = (cmd); if(__res < 0) { av_strerror(__res, av_error_buffer, sizeof(av_error_buffer)); cerr << av_error_buffer << "\n"; } }

GLFWwindow* window;
int window_width, window_height;

AVFormatContext* format_context{};
AVFrame* input_frame;
AVPacket* input_packet;
AVStream* video_stream;
AVCodecContext* codec_decoder_context;
double frame_time_sec, last_frame_time_sec = 0;

int av_open(const char* url)
{
	// read the file header
	CHECK_AV_SUCCESS(avformat_open_input(&format_context, url, nullptr, nullptr));

	// get the stream information
	CHECK_AV_SUCCESS(avformat_find_stream_info(format_context, nullptr));

	// find the first video stream
	for (const auto current_video_stream : span<AVStream*>(format_context->streams, format_context->nb_streams))
		if (current_video_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			video_stream = current_video_stream;
			break;
		}
	CHECK_SUCCESS(video_stream, "Could not find a video stream.");

	// dump information about it
	av_dump_format(format_context, video_stream->index, url, false);

	// find the decoder
	AVCodec const* codec_decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);
	CHECK_SUCCESS(codec_decoder, "Could not find decoder codec.");

	// copy the decoder codec context locally, since we must not use the the global version
	codec_decoder_context = avcodec_alloc_context3(codec_decoder);
	CHECK_AV_SUCCESS(avcodec_parameters_to_context(codec_decoder_context, video_stream->codecpar));

	// open the codec
	avcodec_open2(codec_decoder_context, codec_decoder, nullptr);

	input_frame = av_frame_alloc();
	input_packet = av_packet_alloc();

	const auto frame_rate_rational = av_guess_frame_rate(format_context, video_stream, nullptr);
	frame_time_sec = static_cast<double>(frame_rate_rational.den) / frame_rate_rational.num;

	return 0;
}

int av_get_next_frame(function<void(AVFrame*)> process_frame)
{
	while (av_read_frame(format_context, input_packet) >= 0)
	{
		if (input_packet->stream_index == video_stream->index)
		{
			// get the frame
			CHECK_AV_SUCCESS(avcodec_send_packet(codec_decoder_context, input_packet));

			int res{};
			while (1)
			{
				res = avcodec_receive_frame(codec_decoder_context, input_frame);
				if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) break;
				CHECK_AV_SUCCESS(res);

				process_frame(input_frame);

				av_frame_unref(input_frame);

				return 0;
			}
		}

		av_packet_unref(input_packet);
	}

	return AVERROR_EOF;
}

unique_ptr<ShaderProgram> shader_program;
constexpr int yuv_planar_textures_count = 3;
GLuint vertex_buffer_object_name, vertex_array_object_name, yuv_planar_texture_names[yuv_planar_textures_count];

#pragma pack(push, 1)
struct Vertex
{
	vec2 position, uv;

	static void setup_gl_array_attributes(GLuint vertex_array_object_name)
	{
		glEnableVertexArrayAttrib(vertex_array_object_name, 0);
		glVertexArrayAttribFormat(vertex_array_object_name, 0, 2, GL_FLOAT, false, offsetof(Vertex, position));
		glVertexArrayAttribBinding(vertex_array_object_name, 0, 0);

		glEnableVertexArrayAttrib(vertex_array_object_name, 1);
		glVertexArrayAttribFormat(vertex_array_object_name, 1, 2, GL_FLOAT, false, offsetof(Vertex, uv));
		glVertexArrayAttribBinding(vertex_array_object_name, 1, 0);
	}
};
#pragma pack(pop)

void window_size_callback(GLFWwindow* window, int width, int height)
{
	window_width = width;
	window_height = height;
}

int gl_init()
{
	CHECK_SUCCESS(glfwInit(), "Could not initialize GLFW.");

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	window = glfwCreateWindow(800, 600, ApplicationName, nullptr, nullptr);
	glfwGetWindowSize(window, &window_width, &window_height);
	glfwSetWindowSizeCallback(window, window_size_callback);
	CHECK_SUCCESS(window, "Could not create window.");
	glfwMakeContextCurrent(window);

	glewExperimental = GL_TRUE;
	CHECK_SUCCESS(!glewInit(), "Could not initialize GLEW.");

	// shaders
	shader_program = link_shader_program_from_shader_objects(
		compile_shader_from_source("\
			#version 460 \n\
			uniform mat4 transform_matrix; \n\
			in vec2 position, uv; \n\
			out vec2 fs_uv; \n\
			\n\
			void main() \n\
			{ \n\
				fs_uv = uv; \n\
				gl_Position = vec4(position, 0, 1) /** transform_matrix*/; \n\
			}", ShaderType::Vertex),
		compile_shader_from_source("\
			#version 460 \n\
			uniform sampler2D y_texture, u_texture, v_texture; \n\
			in vec2 fs_uv; \n\
			out vec4 color; \n\
			\n\
			void main() \n\
			{ \n\
				vec3 yuv, rgb; \n\
				yuv.x = texture2D(y_texture, fs_uv).r; \n\
				yuv.y = texture2D(u_texture, fs_uv).r - 0.5; \n\
				yuv.z = texture2D(v_texture, fs_uv).r - 0.5; \n\
				rgb = mat3(1, 1, 1, 0, -0.21482, 2.12798, 1.28033, -0.38059, 0) * yuv; \n\
				color = vec4(rgb, 1); \n\
			}", ShaderType::Fragment));
	CHECK_SUCCESS(shader_program, "Could not link shader program.");

	// vertices
	Vertex vertices[] = { { vec2(-1, -1), vec2(0, 1) }, { vec2(-1, 1), vec2(0, 0) }, { vec2(1, -1), vec2(1, 1) }, { vec2(1, 1), vec2(1, 0) } };

	glCreateBuffers(1, &vertex_buffer_object_name);
	glNamedBufferStorage(vertex_buffer_object_name, sizeof(vertices), vertices, 0);

	glCreateVertexArrays(1, &vertex_array_object_name);
	glVertexArrayVertexBuffer(vertex_array_object_name, 0, vertex_buffer_object_name, 0, sizeof(*vertices));

	Vertex::setup_gl_array_attributes(vertex_array_object_name);

	// textures
	glCreateTextures(GL_TEXTURE_2D, yuv_planar_textures_count, yuv_planar_texture_names);
	for (int i = 0; i < yuv_planar_textures_count; ++i)
	{
		glTextureParameteri(yuv_planar_texture_names[i], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(yuv_planar_texture_names[i], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(yuv_planar_texture_names[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(yuv_planar_texture_names[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTextureStorage2D(yuv_planar_texture_names[i], 1, GL_R8, video_stream->codecpar->width / (i ? 2 : 1), video_stream->codecpar->height / (i ? 2 : 1));
	}

	glProgramUniform1i(shader_program->program_name, shader_program->uniform_locations["y_texture"], 0);
	glProgramUniform1i(shader_program->program_name, shader_program->uniform_locations["u_texture"], 1);
	glProgramUniform1i(shader_program->program_name, shader_program->uniform_locations["v_texture"], 2);
	glProgramUniformMatrix4fv(shader_program->program_name, shader_program->uniform_locations["transform_matrix"], 1, false, value_ptr(mat4()));

	// gl state init stuff
	glClearColor(0, 0, 0, 0);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);

	return 0;
}

// The main render function, return true to swap buffers.
bool gl_render()
{
	const auto current_time_sec = glfwGetTime();
	if (current_time_sec - last_frame_time_sec < frame_time_sec) return false;
	last_frame_time_sec = current_time_sec;

	// read the next video frame
	av_get_next_frame([&](AVFrame* frame)
		{
			for (int i = 0; i < yuv_planar_textures_count; ++i)
				glTextureSubImage2D(yuv_planar_texture_names[i], 0, 0, 0, video_stream->codecpar->width / (i ? 2 : 1), video_stream->codecpar->height / (i ? 2 : 1), GL_RED, GL_UNSIGNED_BYTE, frame->data[i]);
		});

	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(shader_program->program_name);
	glBindVertexArray(vertex_array_object_name);
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, yuv_planar_texture_names[0]);
	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, yuv_planar_texture_names[1]);
	glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, yuv_planar_texture_names[2]);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	return true;
}

int main(const char* args, int argc)
{
	if (av_open("C:\\Users\\Marius\\Downloads\\Yuju Sleeping.mp4")) return -1;

	if (gl_init()) return -1;

	while (!glfwWindowShouldClose(window))
	{
		if (gl_render())
			glfwSwapBuffers(window);

		glfwPollEvents();
	}
}
