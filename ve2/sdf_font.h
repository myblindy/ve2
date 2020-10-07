#pragma once

#include "mapbox/glyph_foundry.hpp"
#include <glm/glm.hpp>
#include <gl/glew.h>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

import growable_texture_atlas;
import utilities;

struct FontGlyph
{
	int atlas_index;
	double advance;
	int width, height, left, top;
	double bearing_y;
	glm::box2 uv;
};

struct Font
{
	Font(const std::vector<const char*> &font_face_paths, int render_size = 512, int sdf_size = 32);
	std::vector<FontGlyph> get_glyph_data(const std::u8string& s);
	void bind() { glBindTexture(GL_TEXTURE_2D, atlas->texture_name); }

private:
	int sdf_size;

	std::vector<FT_Face> ft_faces;

	std::unordered_map<char32_t, FontGlyph> character_font_data;
	std::unique_ptr<GrowableTextureAtlas> atlas;
};