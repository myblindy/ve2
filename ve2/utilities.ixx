module;
#include <memory>

export module utilities;

using namespace std;

export unique_ptr<char[]> mem_new_dup(const char* data, const size_t length)
{
	auto mem = make_unique<char[]>(length);
	memcpy(mem.get(), data, length);
	return move(mem);
}