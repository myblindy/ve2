module;

#include <vector>
#include <algorithm>
#include <optional>

export module keyframes;

import utilities;

using namespace std;
using namespace glm;

export struct KeyFrame
{
	double frame_time{};
	box2 box{};

	KeyFrame(double frame_time, const box2& box) :frame_time(frame_time), box(box) {}
};

const box2 default_box{ {}, {1, 1} };

export struct KeyFrames
{
	box2 at(double frame_time)
	{
		if (keyframes.empty()) return default_box;

		auto last_box = &default_box;
		double last_box_frame_time = 0;
		for (const auto& keyframe : keyframes)
		{
			if (keyframe.frame_time == frame_time)
				return keyframe.box;

			if (keyframe.frame_time > frame_time)
				if (last_box_frame_time == frame_time)
					return *last_box;
				else
					return mix(*last_box, keyframe.box, static_cast<float>((frame_time - last_box_frame_time) / (keyframe.frame_time - last_box_frame_time)));

			last_box = &keyframe.box;
			last_box_frame_time = keyframe.frame_time;
		}

		return *last_box;
	}

	optional<float> aspect_ratio()
	{
		if (keyframes.empty()) return {};
		const auto& first_box_size = keyframes[0].box.size();
		return first_box_size.x / first_box_size.y;
	}

	bool contains(double frame_time) { return find_if(keyframes.begin(), keyframes.end(), [&](const auto& kf) {return kf.frame_time == frame_time; }) != keyframes.end(); }

	bool add(double frame_time, const box2& box)
	{
		const auto next_it = find_if(keyframes.begin(), keyframes.end(), [&](const auto& kf) {return kf.frame_time >= frame_time; });

		if (next_it == keyframes.end())
		{
			// nothing past the frame index we want, but is the last frame index the one we need to add?
			if (!keyframes.empty() && keyframes.back().frame_time == frame_time)
			{
				keyframes.back().box = box;
				return false;
			}

			keyframes.emplace_back(frame_time, box);
		}
		else if (next_it->frame_time == frame_time)
			next_it->box = box;
		else
		{
			// insert before the found iterator
			keyframes.emplace(next_it, frame_time, box);
		}

		return true;
	}

	void remove(double frame_time)
	{
		keyframes.erase(remove_if(keyframes.begin(), keyframes.end(), [&](const auto& kf) { return kf.frame_time == frame_time; }));
	}

	void clear() { keyframes.clear(); }

private:
	vector<KeyFrame> keyframes;
};