module;

#include <gl/glew.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

export module growable_texture_atlas;

using namespace std;
using namespace glm;

export enum class GrowableTextureAtlasInternalStorageType
{
	OneChannel,
};

constexpr GLenum internal_storage_for_growable_texture_atlas_internal_storage_type[] =
{
	GL_R8,
};

constexpr GLenum format_for_growable_texture_atlas_internal_storage_type[] =
{
	GL_RED,
};

constexpr GLenum pixel_type_for_growable_texture_atlas_internal_storage_type[] =
{
	GL_BYTE,
};

export enum class GrowableTextureAtlasFilter
{
	Nearest = GL_NEAREST,
	Linear = GL_LINEAR,
};

struct node
{
	node(const vec2& uv0, const vec2& uv1) : uv0(uv0), uv1(uv1) {}
	vec2 uv0, uv1;
};

export struct GrowableTextureAtlas
{
	GLuint texture_name;

	GrowableTextureAtlas(const ivec2& initial_size = { 512, 512 },
		const GrowableTextureAtlasInternalStorageType storage_type = GrowableTextureAtlasInternalStorageType::OneChannel,
		const GrowableTextureAtlasFilter min_filter = GrowableTextureAtlasFilter::Linear,
		const GrowableTextureAtlasFilter mag_filter = GrowableTextureAtlasFilter::Linear)
		: texture_size(initial_size)
	{
		glCreateTextures(GL_TEXTURE_2D, 1, &texture_name);
		glTextureStorage2D(texture_name, 1, internal_storage_for_growable_texture_atlas_internal_storage_type[(int)storage_type],
			initial_size.x, initial_size.y);
		texture_storage_format = format_for_growable_texture_atlas_internal_storage_type[(int)storage_type];
		texture_storage_pixel_type = pixel_type_for_growable_texture_atlas_internal_storage_type[(int)storage_type];

		glTextureParameteri(texture_name, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(texture_name, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTextureParameteri(texture_name, GL_TEXTURE_MAG_FILTER, (GLint)mag_filter);
		glTextureParameteri(texture_name, GL_TEXTURE_MIN_FILTER, (GLint)min_filter);
	}

	int add(const ivec2& size, const char* data)
	{
		if (current_position.x + size.x > texture_size.x)
		{
			// move to the next line
			current_position = { 0, current_position.y + current_max_height };
			current_max_height = 0;
		}

		if (current_position.y + size.y > texture_size.y)
		{
			// allocate a new page and move to it (NYI)
			throw exception();
		}

		// upload the data to texture memory at the correct position
		glTextureSubImage2D(texture_name, 0, current_position.x, current_position.y, size.x, size.y, texture_storage_format,
			texture_storage_pixel_type, data);

		nodes.emplace_back(
			vec2((float)current_position.x / texture_size.x, (float)current_position.y / texture_size.y),
			vec2((float)(current_position.x + size.x) / texture_size.x, (float)(current_position.y + size.y) / texture_size.y));

		// advance
		current_position.x += size.x;
		current_position.y += size.y;
		if (current_max_height < size.y) current_max_height = size.y;
	}

	vec2 get_uv(int index, int which) const { return which == 0 ? nodes[index].uv0 : nodes[index].uv1; }

private:
	GLenum texture_storage_format, texture_storage_pixel_type;
	ivec2 texture_size;

	vector<node> nodes;

	ivec2 current_position{};
	int current_max_height{};
};