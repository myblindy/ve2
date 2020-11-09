module;

#include <gl/glew.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

export module growable_texture_atlas;

import utilities;

using namespace std;
using namespace glm;

struct details_t
{
	GLenum internal_storage, format, pixel_type;
};

export enum class GrowableTextureAtlasInternalStorageType
{
	OneChannel,
};

constexpr details_t details_for_growable_texture_atlas_internal_storage_type[] =
{
	{ GL_R8, GL_RED, GL_UNSIGNED_BYTE},
};

export enum class GrowableTextureAtlasFilter
{
	Nearest = GL_NEAREST,
	Linear = GL_LINEAR,
};

struct node
{
	box2 uv_box;
};

struct GrowableTextureAtlasImpl
{
	GLenum texture_storage_format, texture_storage_pixel_type;
	ivec2 texture_size;

	vector<node> nodes;

	ivec2 current_position{ 0, 0 };
	int current_max_height{};
};

export struct GrowableTextureAtlas
{
	GLuint texture_name;

	GrowableTextureAtlas(const ivec2& initial_size = { 512, 512 },
		const GrowableTextureAtlasInternalStorageType storage_type = GrowableTextureAtlasInternalStorageType::OneChannel,
		const GrowableTextureAtlasFilter min_filter = GrowableTextureAtlasFilter::Linear,
		const GrowableTextureAtlasFilter mag_filter = GrowableTextureAtlasFilter::Linear)
	{
		impl->texture_size = initial_size;

		glCreateTextures(GL_TEXTURE_2D, 1, &texture_name);
		glTextureStorage2D(texture_name, 1, details_for_growable_texture_atlas_internal_storage_type[(int)storage_type].internal_storage,
			initial_size.x, initial_size.y);
		impl->texture_storage_format = details_for_growable_texture_atlas_internal_storage_type[(int)storage_type].format;
		impl->texture_storage_pixel_type = details_for_growable_texture_atlas_internal_storage_type[(int)storage_type].pixel_type;

		glTextureParameteri(texture_name, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(texture_name, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTextureParameteri(texture_name, GL_TEXTURE_MAG_FILTER, (GLint)mag_filter);
		glTextureParameteri(texture_name, GL_TEXTURE_MIN_FILTER, (GLint)min_filter);
	}

	size_t add(const ivec2& size, const char* data)
	{
		if (impl->current_position.x + size.x > impl->texture_size.x)
		{
			// move to the next line
			impl->current_position = { 0, impl->current_position.y + impl->current_max_height };
			impl->current_max_height = 0;
		}

		if (impl->current_position.y + size.y > impl->texture_size.y)
		{
			// allocate a new page and move to it (NYI)
			throw exception();
		}

		// upload the data to texture memory at the correct position
		if (data)
		{
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
			glTextureSubImage2D(texture_name, 0, impl->current_position.x, impl->current_position.y, size.x, size.y,
				impl->texture_storage_format, impl->texture_storage_pixel_type, data);
		}

		impl->nodes.emplace_back(box2
			{
				vec2((float)impl->current_position.x / impl->texture_size.x, (float)impl->current_position.y / impl->texture_size.y),
				vec2((float)(impl->current_position.x + size.x) / impl->texture_size.x, (float)(impl->current_position.y + size.y) / impl->texture_size.y)
			});

		// advance
		impl->current_position.x += size.x;
		if (impl->current_max_height < size.y) impl->current_max_height = size.y;

		return impl->nodes.size() - 1;
	}

	const box2& get_uv(size_t index) const { return impl->nodes[index].uv_box; }

private:
	unique_ptr<GrowableTextureAtlasImpl> impl{ make_unique<GrowableTextureAtlasImpl>() };
};