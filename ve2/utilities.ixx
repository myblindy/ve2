module;
#include <glm/glm.hpp>
#include <memory>

export module utilities;

using namespace std;
using namespace glm;

export unique_ptr<char[]> mem_new_dup(const char* data, const size_t length)
{
	auto mem = make_unique<char[]>(length);
	memcpy(mem.get(), data, length);
	return move(mem);
}

namespace glm
{
	export struct box2
	{
		vec2 v0, v1;
	};

	export struct ibox2
	{
		ivec2 v0, v1;
	};
}