module;

#include <vector>
#include <memory>

export module composition;

using namespace std;

struct Part
{
	int64_t from_pts, to_pts;
};

struct CompositionImpl
{
	int64_t duration_pts;

	vector<Part> parts;
};

export struct Composition
{
	Composition(const int64_t duration_pts)
	{
		impl->duration_pts = duration_pts;
		impl->parts.push_back({ 0, duration_pts });
	}

	void split(const int64_t pts)
	{
		for (auto part_it = impl->parts.begin(); part_it != impl->parts.end(); ++part_it)
			if (part_it->from_pts <= pts && part_it->to_pts >= pts)
			{
				impl->parts.insert(part_it + 1, { pts, part_it->to_pts });
				part_it->to_pts = pts;
			}
	}

	auto begin() const noexcept { return impl->parts.cbegin(); }
	auto end() const noexcept { return impl->parts.cend(); }

	auto erase(const vector<Part>::const_iterator it) { return impl->parts.erase(it); }

	int64_t operator[](int64_t pts) const
	{
		int64_t real_pts{}, full_pts{};
		for (const auto& part : impl->parts)
			if (full_pts + part.to_pts - part.from_pts > pts)
				return real_pts + pts - part.from_pts;
			else
			{
				real_pts += part.to_pts - part.from_pts;
				full_pts = part.to_pts;
			}

		throw exception("composition lookup out of range");
	}

private:
	const unique_ptr<CompositionImpl> impl{ make_unique<CompositionImpl>() };
};