// ve2.cpp : Defines the entry point for the application.
//

import shader_program;
import vertex_array;
import gui;
import utilities;

#include "framework.h"
#include "sdf_font.h"

using namespace std;
using namespace glm;

#define CHECK_SUCCESS(cmd, errormsg) if(!(cmd)) { cerr << errormsg << "\n"; return -1; }
char av_error_buffer[2048];
#define CHECK_AV_SUCCESS(cmd) [[gsl::suppress(bounds.3)]] { const int __res = (cmd); if(__res < 0) { av_strerror(__res, av_error_buffer, sizeof(av_error_buffer)); cerr << av_error_buffer << "\n"; } }

GLFWwindow* window;
int window_width, window_height;
GLFWframebuffersizefun previous_framebuffer_size_callback;

AVFormatContext* format_context{};
AVFrame* input_frame;
AVPacket* input_packet;
AVStream* video_stream;
AVCodecContext* codec_decoder_context;
double frame_time_sec, next_frame_time_sec = 0;

constexpr int frames_queue_max_length = 10;
queue<AVFrame*> frames_queue;
mutex frames_queue_mutex;
condition_variable frames_queue_cv;

box2 video_pixel_box{};
box2 active_selection_box{ {0, 0}, {1, 1} };

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

AVFrame* av_deep_clone_frame(AVFrame* src)
{
	auto dst = av_frame_alloc();
	dst->format = src->format;
	dst->width = src->width;
	dst->height = src->height;
	dst->channels = src->channels;
	dst->channel_layout = src->channel_layout;
	dst->nb_samples = src->nb_samples;

	av_frame_get_buffer(dst, 32);
	av_frame_copy(dst, src);
	av_frame_copy_props(dst, src);

	return dst;
}

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

	// multi-threaded decoder, use 4 threads
	codec_decoder_context->thread_count = 4;
	codec_decoder_context->thread_type = FF_THREAD_FRAME;

	// open the codec
	avcodec_open2(codec_decoder_context, codec_decoder, nullptr);

	input_frame = av_frame_alloc();
	input_packet = av_packet_alloc();

	const auto frame_rate_rational = av_guess_frame_rate(format_context, video_stream, nullptr);
	frame_time_sec = static_cast<double>(frame_rate_rational.den) / frame_rate_rational.num;

	thread([&]()
		{
			while (av_get_next_frame([&](AVFrame* frame)
				{
					auto new_frame = av_deep_clone_frame(frame);

					// queue the frame
					unique_lock<mutex> lock(frames_queue_mutex);
					frames_queue_cv.wait(lock, [] { return frames_queue.size() < frames_queue_max_length; });
					frames_queue.push(new_frame);
				}) != AVERROR_EOF)
			{
			}
		}).detach();

		return 0;
}

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

unique_ptr<ShaderProgram> shader_program;
unique_ptr<VertexArray<Vertex>> video_vertex_array;
constexpr int yuv_planar_textures_count = 3;
GLuint yuv_planar_texture_names[yuv_planar_textures_count];

void update_aspect_ratio()
{
	const float ar_window = (float)window_width / window_height, ar_video = (float)video_stream->codecpar->width / video_stream->codecpar->height;
	const auto aspect_correction = scale(ar_window < ar_video ? vec3(1, ar_window / ar_video, 1) : vec3(ar_video / ar_window, 1, 1));
	glProgramUniformMatrix4fv(shader_program->program_name, shader_program->uniform_locations["transform_matrix"], 1, false, value_ptr(aspect_correction));

	const vec2 window_size{ window_width, window_height };
	video_pixel_box = box2{
		((vec4(-1, -1, 0, 1) * aspect_correction).xy() / 2.0f + .5f) * window_size,
		((vec4(1, 1, 0, 1) * aspect_correction).xy() / 2.0f + .5f) * window_size
	};
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	glViewport(0, 0, window_width = width, window_height = height);
	update_aspect_ratio();
	if (previous_framebuffer_size_callback) previous_framebuffer_size_callback(window, width, height);
}

void debug_message_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
	if (severity > GL_DEBUG_SEVERITY_NOTIFICATION)
		cerr << "GL ERROR " << message << " type " << type << " severity " << severity << " source " << source << "\n";
}

int gl_init()
{
	CHECK_SUCCESS(glfwInit(), "Could not initialize GLFW.");

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	window = glfwCreateWindow(800, 600, ApplicationName, nullptr, nullptr);
	glfwGetFramebufferSize(window, &window_width, &window_height);
	previous_framebuffer_size_callback = glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	CHECK_SUCCESS(window, "Could not create window.");
	glfwMakeContextCurrent(window);

	glewExperimental = GL_TRUE;
	CHECK_SUCCESS(!glewInit(), "Could not initialize GLEW.");

#ifdef _DEBUG
	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	glDebugMessageCallback(debug_message_callback, nullptr);
#endif

	// shaders
	string yuv_rgb_color_transform_matrix = codec_decoder_context->colorspace != AVCOL_SPC_BT709
		? "1, 1, 1, 0, -0.39465, 2.03211, 1.13983, -0.58060, 0"
		: "1, 1, 1, 0, -0.21482, 2.12798, 1.28033, -0.38059, 0";
	shader_program = link_shader_program_from_shader_objects(
		{
			compile_shader_from_source("\
				#version 460 \n\
				uniform mat4 transform_matrix; \n\
				in vec2 position, uv; \n\
				out vec2 fs_uv; \n\
				\n\
				void main() \n\
				{ \n\
					fs_uv = uv; \n\
					gl_Position = vec4(position, 0, 1) * transform_matrix; \n\
				}", ShaderType::Vertex),
			compile_shader_from_source(("\
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
					rgb = mat3(" + yuv_rgb_color_transform_matrix + ") * yuv; \n\
					color = vec4(rgb, 1); \n\
				}").c_str(), ShaderType::Fragment)
		});
	CHECK_SUCCESS(shader_program, "Could not link shader program.");

	// video vertices
	Vertex vertices[] = { { vec2(-1, -1), vec2(0, 0) }, { vec2(-1, 1), vec2(0, 1) }, { vec2(1, -1), vec2(1, 0) }, { vec2(1, 1), vec2(1, 1) } };
	video_vertex_array = VertexArray<Vertex>::create(vertices);

	// video textures (3 yuv planar textures)
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

	// aspect ratio transforms
	update_aspect_ratio();

	gui_init(window, make_unique<Font>(vector<const char*>
	{
		"content\\OpenSans-Regular.ttf",
			"content\\malgun.ttf",
	}, 64));

	// gl state init stuff
	glClearColor(0, 0, 0, 0);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glClipControl(GL_UPPER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	return 0;
}

bool had_underflow = false;

// The main render function, return true to swap buffers.
bool gl_render()
{
	const auto current_time_sec = glfwGetTime();
	if (current_time_sec < next_frame_time_sec) return false;

	// read the next video frame
	AVFrame* frame;
	{
		lock_guard<mutex> lock(frames_queue_mutex);
		if (frames_queue.empty())
		{
			had_underflow = true;
			return false;					// no data, buffer underflow
		}

		frame = frames_queue.front();
		frames_queue.pop();
	}

	// upload the data
	glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[0]);
	glTextureSubImage2D(yuv_planar_texture_names[0], 0, 0, 0, video_stream->codecpar->width, video_stream->codecpar->height, GL_RED, GL_UNSIGNED_BYTE, frame->data[0]);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[1]);
	glTextureSubImage2D(yuv_planar_texture_names[1], 0, 0, 0, video_stream->codecpar->width / 2, video_stream->codecpar->height / 2, GL_RED, GL_UNSIGNED_BYTE, frame->data[1]);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[2]);
	glTextureSubImage2D(yuv_planar_texture_names[2], 0, 0, 0, video_stream->codecpar->width / 2, video_stream->codecpar->height / 2, GL_RED, GL_UNSIGNED_BYTE, frame->data[2]);

	// notify the decoder thread that we consumed a frame
	frames_queue_cv.notify_all();

	// frame is processed
	next_frame_time_sec += frame_time_sec;

	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(shader_program->program_name);
	glBindVertexArray(video_vertex_array->vertex_array_object_name);
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, yuv_planar_texture_names[0]);
	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, yuv_planar_texture_names[1]);
	glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, yuv_planar_texture_names[2]);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	// render the position slider and its label
	gui_slider(box2::from_corner_size({}, { window_width - 100, 15.0 }), 0.0f,
		static_cast<double>(video_stream->duration), static_cast<double>(frame->best_effort_timestamp));

	const auto slider_label = u8_seconds_to_time_string(frame->best_effort_timestamp * av_q2d(video_stream->time_base)) + u8" / " +
		u8_seconds_to_time_string(video_stream->duration * av_q2d(video_stream->time_base));
	gui_label({ window_width - 100, 3 }, slider_label, 0.2f);

	// render the selection box
	gui_selection_box(active_selection_box, video_pixel_box, { 1, 1, 1, 1 });

	// render the gui to screen
	gui_render();

	av_frame_free(&frame);
	return true;
}

int main(int argc, const char* argv[])
{
	if (av_open(argv[1])) return -1;

	if (gl_init()) return -1;
	next_frame_time_sec = glfwGetTime() + frame_time_sec;

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		if (gl_render())
		{
			had_underflow = false;
			glfwSwapBuffers(window);
		}
	}
}
