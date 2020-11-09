// ve2.cpp : Defines the entry point for the application.
//

import shader_program;
import vertex_array;
import gui;
import utilities;
import keyframes;
import video;

#include "framework.h"
#include "sdf_font.h"

using namespace std;
using namespace glm;

#define CHECK_SUCCESS(cmd, errormsg) if(!(cmd)) { cerr << errormsg << "\n"; return -1; }

GLFWwindow* window;
int window_width, window_height;
GLFWframebuffersizefun previous_framebuffer_size_callback;
GLFWkeyfun previous_key_callback;

unique_ptr<Video> video;
int64_t last_frame_timestamp{};
constexpr double frame_time_sec_paused{ 1.0 / 30.0 };
double frame_time_sec, next_frame_time_sec = 0, next_frame_time_sec_remaining_paused{};

KeyFrames keyframes;

// gui layout constants
constexpr float gui_left_button_width = 30.f, gui_slider_height = 15.f, gui_slider_margins_x = 5.f, gui_time_position_width = 100.f,
gui_play_bar_height = gui_left_button_width;
constexpr float gui_font_scale = 0.2f;

// gui boxes
box2 active_selection_box{ {0, 0}, {1, 1} };
bool active_selection_box_is_keyframe = false;

#pragma pack(push, 1)
struct Vertex
{
	vec2 position, uv;

	static void setup_gl_array_attributes(GLuint vertex_array_object_name) noexcept
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
	const auto frame_size = video->frame_size();

	// split the screen horizontally
	const auto height = (window_height - gui_play_bar_height) / 2;
	full_video_buffer_object->data.pixel_bounds_box = box2::from_corner_size({ 0, gui_play_bar_height }, { window_width, height });
	full_video_buffer_object->data.window_and_video_pixel_size = { window_width, window_height, frame_size.x, frame_size.y };
	full_video_buffer_object->update();
	preview_video_buffer_object->data.pixel_bounds_box = box2::from_corner_size({ 0, height + gui_play_bar_height }, { window_width, height });
	preview_video_buffer_object->data.window_and_video_pixel_size = { window_width, window_height, frame_size.x, frame_size.y };
	preview_video_buffer_object->update();
}

void toggle_play(double current_time_sec)
{
	video->play(!video->playing());

	if (video->playing())
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
		video->seek_pts(video->start_pts());

	// RIGHT skips one frame while paused
	else if (key == GLFW_KEY_RIGHT && action != GLFW_RELEASE && !video->playing())
		video->set_force_display();									// this shows the next frame in queue, if any

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
	string yuv_rgb_color_transform_matrix = video->colorspace_is_bt709()
		? "1, 1, 1, 0, -0.21482, 2.12798, 1.28033, -0.38059, 0"
		: "1, 1, 1, 0, -0.39465, 2.03211, 1.13983, -0.58060, 0";
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
	const auto frame_size = video->frame_size();
	for (auto yuv_planar_texture_name = yuv_planar_texture_names; yuv_planar_texture_name < yuv_planar_texture_names + yuv_planar_textures_count; ++yuv_planar_texture_name, ++yuv_planar_texture_name_index)
	{
		glTextureParameteri(*yuv_planar_texture_name, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(*yuv_planar_texture_name, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(*yuv_planar_texture_name, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(*yuv_planar_texture_name, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTextureStorage2D(*yuv_planar_texture_name, 1, GL_R8,
			frame_size.x / (yuv_planar_texture_name_index ? 2 : 1), frame_size.y / (yuv_planar_texture_name_index ? 2 : 1));
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
		static_cast<double>(video->duration_pts()), static_cast<double>(last_frame_timestamp),
		[&](double new_value) { video->seek_pts(static_cast<int64_t>(new_value * video->duration_pts())); });

	const auto slider_label = u8_seconds_to_time_string(last_frame_timestamp * video->time_base()) + u8" / " +
		u8_seconds_to_time_string(video->duration_sec());
	gui_label(box2::from_corner_size({ window_width - gui_time_position_width, 0 }, { gui_time_position_width, gui_play_bar_height }), slider_label, gui_font_scale);

	// left buttons
	gui_button(box2::from_corner_size({}, { gui_left_button_width, gui_play_bar_height }), video->playing() ? u8"⬛" : u8"▶",
		[&] { toggle_play(current_time_sec); }, gui_font_scale);

	// render the selection box -- need to figure out the aspect corrected position of the main video player
	static SelectionBoxState selection_box_state{};
	gui_selection_box(active_selection_box, get_aspect_corrected_video_pixel_bounds_box(), video->playing(),
		active_selection_box_is_keyframe ? vec4(1, 0, 1, 1) : vec4(1, 1, 1, 1), keyframes.is_first(last_frame_timestamp) ? optional<float>() : keyframes.aspect_ratio(),
		[]
		{
			keyframes.add(last_frame_timestamp * video->time_base(), active_selection_box);
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

	if (video->playing() || video->force_display())
	{
		const auto underflow = !video->consume_frame([&](int64_t pts, int64_t frame_duration_pts, array<span<uint8_t>, 3> planes)
			{
				const auto frame_size = video->frame_size();

				// upload the data
				glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(planes[0].size_bytes()));
				glTextureSubImage2D(yuv_planar_texture_names[0], 0, 0, 0, frame_size.x, frame_size.y, GL_RED, GL_UNSIGNED_BYTE, planes[0].data());
				glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(planes[1].size_bytes()));
				glTextureSubImage2D(yuv_planar_texture_names[1], 0, 0, 0, frame_size.x / 2, frame_size.y / 2, GL_RED, GL_UNSIGNED_BYTE, planes[1].data());
				glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(planes[2].size_bytes()));
				glTextureSubImage2D(yuv_planar_texture_names[2], 0, 0, 0, frame_size.x / 2, frame_size.y / 2, GL_RED, GL_UNSIGNED_BYTE, planes[2].data());

				const auto ts = pts * video->time_base();
				active_selection_box = keyframes.at(ts);
				active_selection_box_is_keyframe = keyframes.contains(ts);

				// frame is processed
				last_frame_timestamp = pts;
				next_frame_time_sec += frame_duration_pts * video->time_base();
				video->clear_force_display();
			});

		if (underflow)
			return false;
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

	video = make_unique<Video>(argv[1]);

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
