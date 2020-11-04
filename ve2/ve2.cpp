// ve2.cpp : Defines the entry point for the application.
//

import shader_program;
import vertex_array;
import gui;
import utilities;
import keyframes;

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
GLFWkeyfun previous_key_callback;

AVFormatContext* format_context{};
AVFrame* input_frame;
AVPacket* input_packet;
AVStream* video_stream;
AVCodecContext* codec_decoder_context;
int64_t last_frame_timestamp{};
constexpr double frame_time_sec_paused{ 1.0 / 30.0 };
double frame_time_sec, next_frame_time_sec = 0, next_frame_time_sec_remaining_paused{};

constexpr int frames_queue_max_length = 10;
optional<double> seek_timestamp_sec{};					// if set, triggers the frame read thread to clear the frame cache, seek to this position and restart the decoding
queue<AVFrame*> frames_queue;
mutex frames_queue_mutex;
condition_variable frames_queue_cv;
bool playing = true, seek_needs_display = false;
KeyFrames keyframes;

// gui layout constants
constexpr float gui_left_button_width = 30.f, gui_slider_height = 15.f, gui_slider_margins_x = 5.f, gui_time_position_width = 100.f,
gui_play_bar_height = gui_left_button_width;
constexpr float gui_font_scale = 0.2f;

// gui boxes
box2 active_selection_box{ {0, 0}, {1, 1} };
bool active_selection_box_is_keyframe = false;

int av_get_next_frame(const int64_t skip_pts, function<void(AVFrame* frame)> process_frame)
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

				// skip frames as needed for seeking
				if (input_frame->pts >= skip_pts)
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

	thread([&]
		{
			while (true)
			{
				optional<double> _seek_timestamp_sec;
				int64_t ts_pts = INT64_MIN;
				{
					lock_guard<mutex> lg(frames_queue_mutex);
					_seek_timestamp_sec = seek_timestamp_sec;
				}
				seek_timestamp_sec.reset();

				// seek if needed
				if (_seek_timestamp_sec)
				{
					// convert seconds to pts
					ts_pts = static_cast<int64_t>(*_seek_timestamp_sec / av_q2d(video_stream->time_base));

					// seek before the time stamp 
					avformat_seek_file(format_context, video_stream->index, INT64_MIN, ts_pts, ts_pts, AVSEEK_FLAG_BACKWARD);

					// flush the context
					avcodec_flush_buffers(codec_decoder_context);
				}

				while (av_get_next_frame(ts_pts, [&](AVFrame* frame)
					{
						auto new_frame = av_deep_clone_frame(frame);

						// queue the frame
						unique_lock<mutex> lock(frames_queue_mutex);
						frames_queue_cv.wait(lock, [] { return seek_timestamp_sec || frames_queue.size() < frames_queue_max_length; });

						// seek instead if required
						if (seek_timestamp_sec)
						{
							av_frame_unref(new_frame);
							return;
						}

						frames_queue.push(new_frame);
					}) != AVERROR_EOF && !seek_timestamp_sec)
				{
				}
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

struct VideoBufferObject
{
	box2 normalized_crop_box = { {}, {1, 1} };
	box2 pixel_bounds_box;
	vec4 window_and_video_pixel_size;
};
#pragma pack(pop)

unique_ptr<ShaderProgram> shader_program;
unique_ptr<VertexArray<Vertex>> video_vertex_array;
constexpr int yuv_planar_textures_count = 3;
GLuint yuv_planar_texture_names[yuv_planar_textures_count];
constexpr GLuint video_program_binding_point = 1;
unique_ptr<UniformBufferObject<VideoBufferObject>> full_video_buffer_object, preview_video_buffer_object;

void update_screen_layout()
{
	// split the screen horizontally
	const auto height = (window_height - gui_play_bar_height) / 2;
	full_video_buffer_object->data.pixel_bounds_box = box2::from_corner_size({ 0, gui_play_bar_height }, { window_width, height });
	full_video_buffer_object->data.window_and_video_pixel_size = { window_width, window_height, video_stream->codecpar->width, video_stream->codecpar->height };
	full_video_buffer_object->update();
	preview_video_buffer_object->data.pixel_bounds_box = box2::from_corner_size({ 0, height + gui_play_bar_height }, { window_width, height });
	preview_video_buffer_object->data.window_and_video_pixel_size = { window_width, window_height, video_stream->codecpar->width, video_stream->codecpar->height };
	preview_video_buffer_object->update();
}

// needs to be under a frames_queue_mutex lock
void clear_frames_queue()
{
	// clear the queue and free up the allocated frames
	while (!frames_queue.empty())
	{
		av_frame_unref(frames_queue.front());
		frames_queue.pop();
	}

	// notify the decoder thread that we need more frames to replace what we just cleared
	frames_queue_cv.notify_all();
}

void seek_pts(int64_t pts)
{
	seek_timestamp_sec = pts * av_q2d(video_stream->time_base);
	{
		lock_guard<mutex> lock(frames_queue_mutex);
		clear_frames_queue();
	}
	seek_needs_display = true;
}

void toggle_play(double current_time_sec)
{
	if (playing = !playing)
		next_frame_time_sec = next_frame_time_sec_remaining_paused + current_time_sec; // now playing
	else
	{
		next_frame_time_sec_remaining_paused = next_frame_time_sec - current_time_sec; // now paused
		next_frame_time_sec = current_time_sec + frame_time_sec_paused;
	}
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	glViewport(0, 0, window_width = width, window_height = height);
	update_screen_layout();
	if (previous_framebuffer_size_callback) previous_framebuffer_size_callback(window, width, height);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	// HOME seeks at the beginning
	if (key == GLFW_KEY_HOME && action == GLFW_PRESS)
		seek_pts(video_stream->start_time);

	// RIGHT skips one frame while paused
	else if (key == GLFW_KEY_RIGHT && action != GLFW_RELEASE && !playing)
		seek_needs_display = true;		// this shows the next frame in queue, if any

	// SPACE toggles pause
	else if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
		toggle_play(glfwGetTime());
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
	previous_key_callback = glfwSetKeyCallback(window, key_callback);
	CHECK_SUCCESS(window, "Could not create window.");
	glfwMakeContextCurrent(window);

	glewExperimental = GL_TRUE;
	CHECK_SUCCESS(!glewInit(), "Could not initialize GLEW.");

#ifdef _DEBUG
	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	glDebugMessageCallback(debug_message_callback, nullptr);
#endif

	full_video_buffer_object = UniformBufferObject<VideoBufferObject>::create();
	preview_video_buffer_object = UniformBufferObject<VideoBufferObject>::create();

	// shaders
	string yuv_rgb_color_transform_matrix = codec_decoder_context->colorspace != AVCOL_SPC_BT709
		? "1, 1, 1, 0, -0.39465, 2.03211, 1.13983, -0.58060, 0"
		: "1, 1, 1, 0, -0.21482, 2.12798, 1.28033, -0.38059, 0";
	shader_program = link_shader_program_from_shader_objects(
		{
			compile_shader_from_source("\
				#version 460 core \n\
				layout(std140) uniform video_buffer_object { \n\
					vec4 normalized_crop_box; // normalized box stored as (x0, y0, x1, y1) to represent the crop view \n\
					vec4 pixel_bounds_box;    // pixel box stored as (x0, y0, x1, y1) to represent the full bounds available \n\
					vec4 window_and_video_pixel_size; \n\
				}; \n\
				\n\
				in vec2 position, uv; \n\
				out vec2 fs_uv; \n\
				\n\
				void main() \n\
				{ \n\
					fs_uv = vec2(mix(normalized_crop_box.x, normalized_crop_box.z, uv.x), mix(normalized_crop_box.y, normalized_crop_box.w, uv.y)); \n\
					\n\
					const float ar_bounds = (pixel_bounds_box.z - pixel_bounds_box.x) / (pixel_bounds_box.w - pixel_bounds_box.y), \n\
						ar_crop = (normalized_crop_box.z - normalized_crop_box.x) / (normalized_crop_box.w - normalized_crop_box.y), \n\
						ar_video = (window_and_video_pixel_size.z / window_and_video_pixel_size.w) * ar_crop; \n\
					const vec2 aspect_correction = ar_bounds < ar_video ? vec2(1, ar_bounds / ar_video) : vec2(ar_video / ar_bounds, 1); \n\
					const vec4 aspect_corrected_pixel_box = vec4( \n\
						pixel_bounds_box.xy * aspect_correction, \n\
						pixel_bounds_box.zw * aspect_correction); \n\
					const vec2 offset = vec2( \n\
						(pixel_bounds_box.z - pixel_bounds_box.x) / 2 - (aspect_corrected_pixel_box.z - aspect_corrected_pixel_box.x) / 2 + pixel_bounds_box.x - aspect_corrected_pixel_box.x,\n\
						(pixel_bounds_box.w - pixel_bounds_box.y) / 2 - (aspect_corrected_pixel_box.w - aspect_corrected_pixel_box.y) / 2 + pixel_bounds_box.y - aspect_corrected_pixel_box.y);\n\
					const vec2 aspect_corrected_normalized_position = vec2( \n\
						mix(aspect_corrected_pixel_box.x, aspect_corrected_pixel_box.z, (position.x + 1) / 2) + offset.x, \n\
						mix(aspect_corrected_pixel_box.y, aspect_corrected_pixel_box.w, (position.y + 1) / 2) + offset.y) \n\
						/ window_and_video_pixel_size.xy * 2 - 1; \n\
					gl_Position = vec4(aspect_corrected_normalized_position, 0, 1); \n\
				}", ShaderType::Vertex),
			compile_shader_from_source(("\
				#version 460 core \n\
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
	int yuv_planar_texture_name_index = 0;
	for (auto yuv_planar_texture_name = yuv_planar_texture_names; yuv_planar_texture_name < yuv_planar_texture_names + yuv_planar_textures_count; ++yuv_planar_texture_name, ++yuv_planar_texture_name_index)
	{
		glTextureParameteri(*yuv_planar_texture_name, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(*yuv_planar_texture_name, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(*yuv_planar_texture_name, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(*yuv_planar_texture_name, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTextureStorage2D(*yuv_planar_texture_name, 1, GL_R8,
			video_stream->codecpar->width / (yuv_planar_texture_name_index ? 2 : 1), video_stream->codecpar->height / (yuv_planar_texture_name_index ? 2 : 1));
	}

	glProgramUniform1i(shader_program->program_name, shader_program->uniform_locations["y_texture"], 0);
	glProgramUniform1i(shader_program->program_name, shader_program->uniform_locations["u_texture"], 1);
	glProgramUniform1i(shader_program->program_name, shader_program->uniform_locations["v_texture"], 2);
	shader_program->uniform_block_binding(shader_program->uniform_locations["video_buffer_object"], video_program_binding_point);

	// aspect ratio transforms
	update_screen_layout();

	gui_init(window, make_unique<Font>(vector<const char*>{ "content\\OpenSans-Regular.ttf", "content\\NotoSansKR-Regular.otf", "content\\Symbola605.ttf" }, 64));

	// gl state init stuff
	glClearColor(0, 0, 0, 0);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glClipControl(GL_UPPER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	return 0;
}

box2 get_aspect_corrected_video_pixel_bounds_box()
{
	const float ar_bounds = (full_video_buffer_object->data.pixel_bounds_box.v1.x - full_video_buffer_object->data.pixel_bounds_box.v0.x) / (full_video_buffer_object->data.pixel_bounds_box.v1.y - full_video_buffer_object->data.pixel_bounds_box.v0.y),
		ar_video = (full_video_buffer_object->data.window_and_video_pixel_size.z / full_video_buffer_object->data.window_and_video_pixel_size.w);
	const vec2 aspect_correction = ar_bounds < ar_video ? vec2(1, ar_bounds / ar_video) : vec2(ar_video / ar_bounds, 1);
	const box2 aspect_corrected_pixel_box = {
		full_video_buffer_object->data.pixel_bounds_box.top_left() * aspect_correction,
		full_video_buffer_object->data.pixel_bounds_box.bottom_right() * aspect_correction
	};
	const vec2 offset = vec2(
		(full_video_buffer_object->data.pixel_bounds_box.z() - full_video_buffer_object->data.pixel_bounds_box.x()) / 2 - (aspect_corrected_pixel_box.z() - aspect_corrected_pixel_box.x()) / 2 + aspect_corrected_pixel_box.x(),
		(full_video_buffer_object->data.pixel_bounds_box.w() - full_video_buffer_object->data.pixel_bounds_box.y()) / 2 - (aspect_corrected_pixel_box.w() - aspect_corrected_pixel_box.y()) / 2 + aspect_corrected_pixel_box.y());
	return box2::from_corner_size(offset, aspect_corrected_pixel_box.size());
}

void gui_process(const double current_time_sec)
{
	// render the position slider and its label
	gui_slider(
		box2::from_corner_size({ gui_left_button_width + gui_slider_margins_x, gui_play_bar_height / 2.f - gui_slider_height / 2.f }, { window_width - gui_time_position_width - gui_slider_margins_x - gui_left_button_width, gui_slider_height }), 0.0f,
		static_cast<double>(video_stream->duration), static_cast<double>(last_frame_timestamp),
		[&](double new_value) { seek_pts(static_cast<int64_t>(new_value * video_stream->duration)); });

	const auto slider_label = u8_seconds_to_time_string(last_frame_timestamp * av_q2d(video_stream->time_base)) + u8" / " +
		u8_seconds_to_time_string(video_stream->duration * av_q2d(video_stream->time_base));
	gui_label(box2::from_corner_size({ window_width - gui_time_position_width, 0 }, { gui_time_position_width, gui_play_bar_height }), slider_label, gui_font_scale);

	// left buttons
	gui_button(box2::from_corner_size({}, { gui_left_button_width, gui_play_bar_height }), playing ? u8"⬛" : u8"▶",
		[&] { toggle_play(current_time_sec); }, gui_font_scale);

	// render the selection box -- need to figure out the aspect corrected position of the main video player
	static SelectionBoxState selection_box_state{};
	gui_selection_box(active_selection_box, get_aspect_corrected_video_pixel_bounds_box(), playing,
		active_selection_box_is_keyframe ? vec4(1, 0, 1, 1) : vec4(1, 1, 1, 1), keyframes.aspect_ratio(),
		[]
		{
			keyframes.add(last_frame_timestamp * av_q2d(video_stream->time_base), active_selection_box);
			active_selection_box_is_keyframe = true;
		}, selection_box_state);

	// render the gui to screen
	gui_render();
}

bool had_underflow = false;

// The main render function, return true to swap buffers.
bool gl_render()
{
	const auto current_time_sec = glfwGetTime();
	if (current_time_sec < next_frame_time_sec) return false;

	if (playing || seek_needs_display)
	{
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

			const auto ts = frame->best_effort_timestamp * av_q2d(video_stream->time_base);
			active_selection_box = keyframes.at(ts);
			active_selection_box_is_keyframe = keyframes.contains(ts);
		}

		// notify the decoder thread that we consumed a frame
		frames_queue_cv.notify_all();

		// upload the data
		glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[0]);
		glTextureSubImage2D(yuv_planar_texture_names[0], 0, 0, 0, video_stream->codecpar->width, video_stream->codecpar->height, GL_RED, GL_UNSIGNED_BYTE, frame->data[0]);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[1]);
		glTextureSubImage2D(yuv_planar_texture_names[1], 0, 0, 0, video_stream->codecpar->width / 2, video_stream->codecpar->height / 2, GL_RED, GL_UNSIGNED_BYTE, frame->data[1]);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[2]);
		glTextureSubImage2D(yuv_planar_texture_names[2], 0, 0, 0, video_stream->codecpar->width / 2, video_stream->codecpar->height / 2, GL_RED, GL_UNSIGNED_BYTE, frame->data[2]);

		// frame is processed
		last_frame_timestamp = frame->best_effort_timestamp;
		next_frame_time_sec += frame->pkt_duration * av_q2d(video_stream->time_base);
		seek_needs_display = false;

		av_frame_free(&frame);
	}

	glClear(GL_COLOR_BUFFER_BIT);

	// set up draw call
	shader_program->use();
	video_vertex_array->bind();
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, yuv_planar_texture_names[0]);
	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, yuv_planar_texture_names[1]);
	glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, yuv_planar_texture_names[2]);

	// and draw the video 
	full_video_buffer_object->bind(video_program_binding_point);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	// and the preview
	preview_video_buffer_object->data.normalized_crop_box = active_selection_box;
	preview_video_buffer_object->update();
	preview_video_buffer_object->bind(video_program_binding_point);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	// process and draw the gui
	gui_process(current_time_sec);

	return true;
}

int main(int argc, const char* argv[])
{
	keyframes.add(0, { {.2f, .3f}, {.5f, .4f} });
	keyframes.add(10, { {.3f, .5f}, {.6f, .6f} });

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
