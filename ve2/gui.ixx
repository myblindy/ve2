module;
#include <string>
#include <memory>
#include <vector>
#include <optional>
#include <algorithm>
#include <functional>
#include <variant>
#include "sdf_font.h"

#include <CppCoreCheck\Warnings.h>
#pragma warning(push)
#pragma warning(disable : ALL_CPPCORECHECK_WARNINGS)
#include <glm/glm.hpp>
#include <gl/glew.h>
#include <glfw/glfw3.h>
#pragma warning(pop)

export module gui;

import shader_program;
import vertex_array;
import utilities;

using namespace std;
using namespace glm;

#pragma warning(push)
#pragma warning(disable : ALL_CPPCORECHECK_WARNINGS)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#pragma warning(pop)

namespace gui_priv
{
#pragma pack(push, 1)
	struct Vertex
	{
		vec2 position;
		vec2 uv;
		vec4 color;

		Vertex(const vec2& position, const vec2& uv, const vec4& color)
			:position(position), uv(uv), color(color)
		{
		}

		static void setup_gl_array_attributes(GLuint vertex_array_object_name)
		{
			glEnableVertexArrayAttrib(vertex_array_object_name, 0);
			glVertexArrayAttribFormat(vertex_array_object_name, 0, 2, GL_FLOAT, false, offsetof(Vertex, position));
			glVertexArrayAttribBinding(vertex_array_object_name, 0, 0);

			glEnableVertexArrayAttrib(vertex_array_object_name, 1);
			glVertexArrayAttribFormat(vertex_array_object_name, 1, 2, GL_FLOAT, false, offsetof(Vertex, uv));
			glVertexArrayAttribBinding(vertex_array_object_name, 1, 0);

			glEnableVertexArrayAttrib(vertex_array_object_name, 2);
			glVertexArrayAttribFormat(vertex_array_object_name, 2, 4, GL_FLOAT, false, offsetof(Vertex, color));
			glVertexArrayAttribBinding(vertex_array_object_name, 2, 0);
		}
	};
#pragma pack(pop)

	unique_ptr<VertexArray<Vertex>> vertex_array;
	unique_ptr<ShaderProgram> shader_program;
	unique_ptr<Font> font;
	vector<Vertex> vertex_cache;									// the vertex cache that stores the gui vertices as they are being build
	optional<uint64_t> last_vertex_cache_hash;						// the hash of the vertex cache that was previously uploaded, if any

	GLFWframebuffersizefun previous_framebuffer_size_callback;
	GLFWcursorposfun previous_cursor_pos_callback;
	GLFWmousebuttonfun previous_mouse_button_callback;

	GLFWwindow* window;
	GLFWcursor* cursor_move, * cursor_h, * cursor_v, * cursor_nesw, * cursor_nwse, * next_cursor{};

	vec2 framebuffer_size{}, previous_framebuffer_size{}, mouse_position{}, left_mouse_down_position;
	bool left_mouse;

	const vec4 color_label_text{ 1.f, 1.f, 1.f, 1.f };
	const vec4 color_button_face{ .8f, .8f, .8f, 1.f };
	const vec4 color_button_face_highlight{ 1.f, 1.f, 1.f, 1.f };
	const vec4 color_button_text{ 0.f, 0.f, 0.f, 1.f };
}

using namespace gui_priv;

enum class SelectionBoxStateSide { Up, Down, Left, Right, All };
export struct SelectionBoxState
{
	SelectionBoxStateSide side;
	vec2 last_mouse_position;
	box2* normalized_box;
	box2 full_pixel_box;
	optional<float> aspect_ratio;
	function<void()> changed;
};

struct
{
	optional<variant<SelectionBoxState*>> selected_object;
	bool left_mouse_handled{};
} gui_state;

// hashing functions
namespace std
{
	template<>
	struct hash<Vertex>
	{
		size_t operator()(Vertex const& vertex) const noexcept
		{
			const hash<float> hash_float;

			return (((((((239811 + hash_float(vertex.position.x) * 324871) + hash_float(vertex.position.y) * 324871) +
				hash_float(vertex.uv.x) * 324871) + hash_float(vertex.uv.y) * 324871) +
				hash_float(vertex.color.r) * 324871) + hash_float(vertex.color.g) * 324871) + hash_float(vertex.color.b) * 324871) + hash_float(vertex.color.a) * 324871;
		}
	};

	template<>
	struct hash<vector<Vertex>>
	{
		size_t operator()(vector<Vertex> const& vertices) const noexcept
		{
			const hash<Vertex> hash_vertex;
			size_t res = 239811;

			for (const auto& vertex : vertices)
				res = (res + hash_vertex(vertex) * 324871);

			return res;
		}
	};
}

void framebuffer_size_callback(GLFWwindow* window, int x, int y)
{
	framebuffer_size = { x, y };
	if (previous_framebuffer_size_callback) previous_framebuffer_size_callback(window, x, y);
}

void cursor_pos_callback(GLFWwindow* window, double x, double y)
{
	mouse_position = { x, y };

	// handle gui element movement
	if (gui_state.selected_object)
	{
		auto ppSelectionBoxState = get_if<SelectionBoxState*>(&*gui_state.selected_object);
		if (ppSelectionBoxState)
		{
			auto& state = **ppSelectionBoxState;
			const auto pixel_delta = mouse_position - state.last_mouse_position;

			switch (state.side)
			{
#define PROCESS_SIDE(_side, _move_func, _ar_func, _field) \
			case SelectionBoxStateSide::_side: \
				state.normalized_box->_move_func(pixel_delta._field / state.full_pixel_box.size()._field); \
				state.normalized_box->clamp(0, 0, 1, 1);\
				if(state.aspect_ratio)\
				{\
					state.normalized_box->_ar_func(*state.aspect_ratio);\
					state.normalized_box->clamp_slide(0, 0, 1, 1);\
				}\
				state.changed();\
				break

				PROCESS_SIDE(Up, move_top, force_aspect_ratio_right, y);
				PROCESS_SIDE(Down, move_bottom, force_aspect_ratio_right, y);
				PROCESS_SIDE(Left, move_left, force_aspect_ratio_bottom, x);
				PROCESS_SIDE(Right, move_right, force_aspect_ratio_bottom, x);

			case SelectionBoxStateSide::All:
				state.normalized_box->move(pixel_delta / state.full_pixel_box.size());
				state.normalized_box->clamp_slide(0, 0, 1, 1);
				state.changed();
				break;

#undef PROCESS_SIDE
			}

			state.last_mouse_position = mouse_position;
		}
	}

	if (previous_cursor_pos_callback) previous_cursor_pos_callback(window, x, y);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT)
	{
		const auto old_left_mouse = left_mouse;
		left_mouse = action == GLFW_PRESS;
		if (left_mouse != old_left_mouse && left_mouse)
			left_mouse_down_position = mouse_position;
		else
		{
			gui_state.selected_object.reset();
			gui_state.left_mouse_handled = false;
		}
	}

	if (previous_mouse_button_callback) previous_mouse_button_callback(window, button, action, mods);
}

GLFWcursor* load_cursor(const char* path, const vec2& hotspot)
{
	GLFWimage image{};
	int b;
	image.pixels = stbi_load((string("content\\") + path).c_str(), &image.width, &image.height, &b, 4);

	return glfwCreateCursor(&image, static_cast<int>(hotspot.x * image.width), static_cast<int>(hotspot.y * image.height));
}

export int gui_init(GLFWwindow* window, unique_ptr<Font> _font)
{
	gui_priv::window = window;

	vertex_array = VertexArray<Vertex>::create_growable();
	font = move(_font);

	shader_program = link_shader_program_from_shader_objects(
		{
			compile_shader_from_source("\
				#version 460 \n\
				uniform vec2 window_size; \n\
				\n\
				in vec2 position; \n\
				in vec2 uv; \n\
				in vec4 color; \n\
				\n\
				out vec2 fs_uv; \n\
				out vec4 fs_color; \n\
				void main() \n\
				{ \n\
					fs_uv = uv; \n\
					fs_color = color; \n\
					gl_Position = vec4(position.x / window_size.x * 2.0 - 1.0, position.y / window_size.y * 2.0 - 1.0, 0, 1); \n\
				}", ShaderType::Vertex),
			compile_shader_from_source("\
				#version 460 \n\
				uniform sampler2D tex; \n\
				\n\
				in vec2 fs_uv; \n\
				in vec4 fs_color; \n\
				\n\
				out vec4 out_color; \n\
				\n\
				void main() \n\
				{ \n\
					if(fs_uv.s >= 0) \n\
					{ \n\
						const vec3 outline_color = vec3(0, 0, 0); \n\
						const float smoothing = 1.0/16.0; \n\
						const float outlineWidth = 3.0 / 16.0; \n\
						const float outerEdgeCenter = 0.5 - outlineWidth; \n\
						\n\
						float distance = texture(tex, fs_uv).r; \n\
						//float alpha = smoothstep(outerEdgeCenter - smoothing, outerEdgeCenter + smoothing, distance); \n\
						//float border = smoothstep(0.5 - smoothing, 0.5 + smoothing, distance); \n\
						//out_color = vec4(mix(outline_color.rgb, fs_color.rgb, border), alpha); \n\
						float width = fwidth(distance); \n\
						float alpha = smoothstep(0.5-width, 0.5+width, distance); \n\
						out_color = vec4(fs_color.rgb, alpha); \n\
					} \n\
					else \n\
						out_color = fs_color; \n\
				}", ShaderType::Fragment)
		});
	glProgramUniform1i(shader_program->program_name, shader_program->uniform_locations["tex"], 0);

	// register a callback to keep the window size updated
	previous_framebuffer_size_callback = glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

	// register i/o callbacks
	previous_cursor_pos_callback = glfwSetCursorPosCallback(window, cursor_pos_callback);
	previous_mouse_button_callback = glfwSetMouseButtonCallback(window, mouse_button_callback);

	int x, y;
	glfwGetFramebufferSize(window, &x, &y);
	framebuffer_size = { x, y };

	// load cursors
	cursor_move = load_cursor("cursor_move.png", { .5f, .5f });
	cursor_v = load_cursor("cursor_resizenorthsouth.png", { .5f, .5f });
	cursor_h = load_cursor("cursor_resizeeastwest.png", { .5f, .5f });

	// cold cache some useful characters
	font->get_glyph_data(u8"0123456789");

	return 0;
}

export int gui_render()
{
	auto vertex_cache_hash = hash<vector<Vertex>>{}(vertex_cache);
	if (!last_vertex_cache_hash || *last_vertex_cache_hash != vertex_cache_hash)
	{
		// the vertices changed, upload to the vertex buffer
		vertex_array->update(vertex_cache.begin(), vertex_cache.end());
		last_vertex_cache_hash = vertex_cache_hash;
	}

	glUseProgram(shader_program->program_name);
	if (previous_framebuffer_size != framebuffer_size)
	{
		// the framebuffer size has changed, upload it to the program
		glUniform2f(shader_program->uniform_locations["window_size"], framebuffer_size.x, framebuffer_size.y);
		previous_framebuffer_size = framebuffer_size;
	}

	if (vertex_array->size())
	{
		glActiveTexture(GL_TEXTURE0);
		font->bind();
		glBindVertexArray(vertex_array->vertex_array_object_name);
		glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertex_array->size()));
	}

	vertex_cache.clear();

	glfwSetCursor(window, next_cursor);
	next_cursor = nullptr;

	return 0;
}

template<typename TState>
bool gui_select(TState& state)
{
	if (!gui_state.selected_object)
	{
		gui_state.selected_object = &state;
		return true;
	}
	else if (*get_if<TState*>(&*gui_state.selected_object) == &state)
		return true;
	return false;
}

box2 uv_no_texture{ -1, -1 };
void quad(const box2& box, const box2& uv, const vec4& color)
{
	vertex_cache.emplace_back(vec2(box.v0.x, box.v0.y), uv.v0, color);
	vertex_cache.emplace_back(vec2(box.v1.x, box.v1.y), uv.v1, color);
	vertex_cache.emplace_back(vec2(box.v1.x, box.v0.y), vec2(uv.v1.x, uv.v0.y), color);

	vertex_cache.emplace_back(vec2(box.v0.x, box.v1.y), vec2(uv.v0.x, uv.v1.y), color);
	vertex_cache.emplace_back(vec2(box.v1.x, box.v1.y), uv.v1, color);
	vertex_cache.emplace_back(vec2(box.v0.x, box.v0.y), uv.v0, color);
}

export int gui_slider(const box2& box, const double min, const double max, const double val, const function<void(const double)> clicked)
{
	// the outer rectangle
	quad(box, uv_no_texture, vec4(0, 1, 0, 1));

	// the thumb
	double percentage = (val - min) / (max - min);
	const vec2 box_size = box.size();
	const vec2 thumb_position = { (box_size.x - box_size.y) * percentage + box.v0.x, box.v0.y };
	const vec2 thumb_size = { box_size.y, box_size.y };
	const box2 thumb_box = box2::from_corner_size(thumb_position, thumb_size);
	quad(thumb_box, uv_no_texture, thumb_box.contains(mouse_position) ? color_button_face_highlight : color_button_face);

	// handle a click
	if (box.contains(mouse_position) && left_mouse && !gui_state.selected_object && !gui_state.left_mouse_handled)
	{
		clicked((mouse_position.x - box.v0.x) / (box.v1.x - box.v0.x));
		gui_state.left_mouse_handled = true;
	}

	return 0;
}

export int gui_label(const box2& box, const u8string& s, const float scale, const vec4& color)
{
	float x = box.v0.x;
	const auto glyphs = font->get_glyph_data(s);

	// find the max bearing
	auto max_bearing_y = max_element(glyphs.begin(), glyphs.end(), [](auto& x, auto& y) {return x.bearing_y < y.bearing_y; })->bearing_y;

	// calculate the bounding box
	box2 bbox;
	for (const auto& glyph : glyphs)
	{
		bbox.v1.x += static_cast<float>(glyph.advance * scale);
		if (bbox.v1.y < glyph.height * scale + static_cast<float>(max_bearing_y - glyph.bearing_y) * scale)
			bbox.v1.y = glyph.height * scale + static_cast<float>(max_bearing_y - glyph.bearing_y) * scale;
	}

	// add a quad for each character, aligned on the baseline based on the bearing
	vec2 offset = box.size() / 2.f - bbox.size() / 2.f;
	for (const auto& glyph : glyphs)
	{
		const float adv = static_cast<float>(glyph.advance);
		quad(box2::from_corner_size(offset + vec2{ x + glyph.left * scale, box.v0.y + static_cast<float>(max_bearing_y - glyph.bearing_y) * scale },
			{ glyph.width * scale, glyph.height * scale }), glyph.uv, color);
		x += adv * scale;
	}

	return 0;
}

export int gui_label(const box2& box, const u8string& s, const float scale = 1.f) { return gui_label(box, s, scale, color_label_text); }

export int gui_button(const box2& box, const u8string& s, const function<void()>& clicked, const float font_scale = 1.0f)
{
	const auto inside = box.contains(mouse_position);
	if (inside && left_mouse && !gui_state.selected_object && !gui_state.left_mouse_handled)
	{
		clicked();
		gui_state.left_mouse_handled = true;
	}

	// background
	quad(box, uv_no_texture, inside ? color_button_face_highlight : color_button_face);

	// text
	auto ret = gui_label(box.with_offset(-5), s, font_scale, color_button_text);
	if (!ret) return ret;

	return 0;
}

export int gui_selection_box(box2& normalized_box, const box2& full_pixel_box, const bool read_only, const vec4& base_color,
	const optional<float>& aspect_ratio, const function<void()>& changed, SelectionBoxState& state)
{
	const vec2 full_pixel_box_size = full_pixel_box.size();
	const box2 pixel_box = { normalized_box.v0 * full_pixel_box_size + full_pixel_box.v0, normalized_box.v1 * full_pixel_box_size + full_pixel_box.v0 };
	const vec2 pixel_box_size = pixel_box.size();
	constexpr int border = 2, touch_offset = 4, outline_width = 1;

#define PROCESS_SIDE_HELPER(_side)\
	do {\
		if(left_mouse && !gui_state.selected_object && gui_select(state))\
		{\
			auto pSelectionBoxState = get<SelectionBoxState *>(*gui_state.selected_object);\
			pSelectionBoxState->side = SelectionBoxStateSide::_side;\
			pSelectionBoxState->last_mouse_position = left_mouse_down_position;\
			pSelectionBoxState->normalized_box = &normalized_box;\
			pSelectionBoxState->full_pixel_box = full_pixel_box;\
			pSelectionBoxState->changed = changed;\
			pSelectionBoxState->aspect_ratio = aspect_ratio;\
		}\
	} while(0) 

#define PROCESS_SIDE(_box, _side)\
do {\
	const box2 box = _box;\
	const vec4 *color;\
	if (!read_only && box.with_offset(touch_offset).contains(mouse_position))\
	{\
		next_cursor = SelectionBoxStateSide::_side == SelectionBoxStateSide::Up || SelectionBoxStateSide::_side == SelectionBoxStateSide::Down ? cursor_v : cursor_h;\
		color = &color_button_face_highlight;\
		\
		PROCESS_SIDE_HELPER(_side);\
	}\
	else\
		color = &base_color;\
	quad(box, uv_no_texture, *color);\
} while(0)

#define PROCESS_SIDES(...) \
do{\
	box2 boxes[] = __VA_ARGS__;\
	for(const auto &box: boxes) quad(box.with_offset(outline_width), uv_no_texture, vec4(0, 0, 0, 1));\
	PROCESS_SIDE(boxes[0], Up);\
	PROCESS_SIDE(boxes[1], Down);\
	PROCESS_SIDE(boxes[2], Left);\
	PROCESS_SIDE(boxes[3], Right);\
} while(0)

	PROCESS_SIDES(
		{
			box2::from_corner_size(pixel_box.top_left(), { pixel_box_size.x, border }),
			box2::from_corner_size(pixel_box.bottom_left() - vec2{ 0, border }, { pixel_box_size.x, border }),
			box2::from_corner_size(pixel_box.top_left(), { border, pixel_box_size.y }),
			box2::from_corner_size(pixel_box.top_right() - vec2{ border, 0 }, { border, pixel_box_size.y })
		});

	if (!read_only && !next_cursor && pixel_box.contains(mouse_position))
	{
		next_cursor = cursor_move;
		PROCESS_SIDE_HELPER(All);
	}

#undef PROCESS_SIDES
#undef PROCESS_SIDE
#undef PROCESS_SIDE_HELPER

	return 0;
}