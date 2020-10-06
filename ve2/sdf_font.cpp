#define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
#include <locale>
#include <codecvt>
#include "sdf_font.h"
#include "mapbox/glyph_foundry_impl.hpp"

using namespace std;
using namespace glm;

// statically init the library
FT_Library ft_library;
struct ft_library_init_t
{
	ft_library_init_t() { FT_Init_FreeType(&ft_library); }
} ft_library_init;

Font::Font(const char* font_face_path, int render_size, int sdf_size) : atlas(make_unique<GrowableTextureAtlas>()), sdf_size(sdf_size)
{
	FT_New_Face(ft_library, font_face_path, 0, &ft_face);
	FT_Set_Char_Size(ft_face, 0, (FT_F26Dot6)(render_size * (1 << 6)), 0, 0);
}

vector<FontGlyph> Font::get_glyph_data(const u8string& s)
{
	std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> ucs4conv;

	auto first = reinterpret_cast<const char*>(s.data());
	auto last = first + s.size();
	auto u32s = ucs4conv.from_bytes(first, last);

	vector<FontGlyph> glyphs;

	for (const auto c32 : u32s)
	{
		const auto font_data_iterator = character_font_data.find(c32);
		FontGlyph font_datum;
		if (font_data_iterator == character_font_data.end())
		{
			// render the character
			sdf_glyph_foundry::glyph_info glyph;
			glyph.glyph_index = FT_Get_Char_Index(ft_face, c32);

			const int buffer_frame = 3;
			sdf_glyph_foundry::RenderSDF(glyph, buffer_frame, 0.25, ft_face);

			// upload the character to the texture
			const auto atlas_index = atlas->add({ glyph.width + buffer_frame * 2, glyph.height + buffer_frame * 2 }, glyph.bitmap.data());
			font_datum =
			{
				atlas_index,
				glyph.advance,
				{ atlas->get_uv(atlas_index, 0), atlas->get_uv(atlas_index, 1) }
			};

			// store the datum
			character_font_data.insert(make_pair(c32, font_datum));
		}
		else
			font_datum = font_data_iterator->second;

		glyphs.push_back(font_datum);
	}

	return glyphs;
}
