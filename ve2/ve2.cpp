// ve2.cpp : Defines the entry point for the application.
//

import shader_programs;
#include "framework.h"

using namespace std;

#define CHECK_SUCCESS(cmd, errormsg) if(!(cmd)) { cerr << errormsg << "\n"; return -1; }
char av_error_buffer[2048];
#define CHECK_AV_SUCCESS(cmd) [[gsl::suppress(bounds.3)]] { const int __res = (cmd); if(__res < 0) { av_strerror(__res, av_error_buffer, sizeof(av_error_buffer)); cerr << av_error_buffer << "\n"; } }

GLFWwindow *window;

AVFormatContext *format_context{};
AVFrame *input_frame;
AVPacket *input_packet;
AVStream *video_stream;
AVCodecContext *codec_decoder_context;
double frame_time_sec, last_frame_time_sec = 0;

int av_open(const char *url)
{
	// read the file header
	CHECK_AV_SUCCESS(avformat_open_input(&format_context, url, nullptr, nullptr));

	// get the stream information
	CHECK_AV_SUCCESS(avformat_find_stream_info(format_context, nullptr));

	// find the first video stream
	for (const auto current_video_stream : span<AVStream *>(format_context->streams, format_context->nb_streams))
		if (current_video_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			video_stream = current_video_stream;
			break;
		}
	CHECK_SUCCESS(video_stream, "Could not find a video stream.");

	// dump information about it
	av_dump_format(format_context, video_stream->index, url, false);

	// find the decoder
	AVCodec const *codec_decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);
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

int av_get_next_frame(function<void(AVFrame *)> process_frame)
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

GLuint shader_program;

int gl_init()
{
	CHECK_SUCCESS(glfwInit(), "Could not initialize GLFW.");

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	window = glfwCreateWindow(800, 600, ApplicationName, nullptr, nullptr);
	CHECK_SUCCESS(window, "Could not create window.");
	glfwMakeContextCurrent(window);

	glewExperimental = GL_TRUE;
	CHECK_SUCCESS(!glewInit(), "Could not initialize GLEW.");

	shader_program = link_shader_program_from_shader_objects(
		compile_shader_from_source("\
			#version 460 \n\
			const vec2 vertices[] = { \
				vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1) \
			}; \
			\
			void main() \
			{ \
				gl_Position = vec4(vertices[gl_VertexID], 0, 1);\
			}", ShaderType::Vertex),
		compile_shader_from_source("\
			#version 460 \n\
			out vec4 color; \
			\
			void main() \
			{ \
				color = vec4(1, 1, 1, 1); \
			}", ShaderType::Fragment));
	CHECK_SUCCESS(shader_program != UINT32_MAX, "Could not link shader program.");

	glClearColor(0, 0, 0, 0);

	return 0;
}

// The main render function, return true to swap buffers.
bool gl_render()
{
	const auto current_time_sec = glfwGetTime();
	if (current_time_sec - last_frame_time_sec < frame_time_sec) return false;
	last_frame_time_sec = current_time_sec;

	// read the next video frame
	av_get_next_frame([&](AVFrame *frame)
		{

		});

	glClear(GL_COLOR_BUFFER_BIT);

	return true;
}

int main(const char *args, int argc)
{
	if (av_open("C:\\Users\\mmitea\\Downloads\\Yuju Sleeping.mp4")) return -1;

	if (gl_init()) return -1;

	while (!glfwWindowShouldClose(window))
	{
		if (gl_render())
			glfwSwapBuffers(window);

		glfwPollEvents();
	}
}
