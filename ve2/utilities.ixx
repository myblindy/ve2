module;
#include <glm/glm.hpp>
#include <memory>
#include <string>

export module utilities;

using namespace std;
using namespace glm;

export unique_ptr<char[]> mem_new_dup(const char* data, const size_t length)
{
	auto mem = make_unique<char[]>(length);
	memcpy(mem.get(), data, length);
	return move(mem);
}

export u8string u8_to_string(int val)
{
	if (!val) return u8"0";

	u8string s;
	if (val < 0) { s += u8"-"; val = -val; }

	int n = val, mask = 1;
	while (n != 0) {
		n = n / 10;
		mask = mask * 10;
	}

	mask /= 10;

	do
	{
		s += (char8_t)('0' + (val / mask) % 10);
	} while ((mask /= 10) >= 1);

	return s;
}

export u8string u8_seconds_to_time_string(const double total_sec, const bool show_decimals = false)
{
	auto mins = static_cast<int>(total_sec / 60);
	auto secs = static_cast<int>(total_sec - 60 * mins);
	auto secs_string = secs < 10 ? u8"0" + u8_to_string(secs) : u8_to_string(secs);

	u8string res = u8_to_string(mins) + u8":" + secs_string;

	if (show_decimals)
	{
		auto decs = (total_sec - floor(total_sec)) * 1000;
		auto decs_string = decs < 10 ? u8"000" + u8_to_string(decs) : decs < 100 ? u8"00" + u8_to_string(decs) : decs < 1000 ? u8"0" + u8_to_string(decs) : u8_to_string(decs);
		res += u8".";
		res += decs_string;
	}

	return res;
}

namespace glm
{
	export struct box2
	{
		vec2 v0, v1;

		static box2 from_corner_size(const vec2& pos, const vec2& size)
		{
			return { pos, pos + size };
		}

		vec2 size() const { return v1 - v0; }
		vec2 topLeft() const { return v0; }
		vec2 topRight() const { return { v1.x, v0.y }; }
		vec2 bottomLeft() const { return { v0.x, v1.y }; }
		vec2 bottomRight() const { return v1; }

	};

	export bool is_vec2_inside_box2(const vec2& pos, const box2& box)
	{
		return pos.x >= min(box.v0.x, box.v1.x) && pos.x <= max(box.v0.x, box.v1.x)
			&& pos.y >= min(box.v0.y, box.v1.y) && pos.y <= max(box.v0.y, box.v1.y);
	}

	export struct ibox2
	{
		ivec2 v0, v1;
	};
}