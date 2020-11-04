module;

#include <memory>

export module video;

using namespace std;

class VideoImpl
{

};

export class Video
{
	unique_ptr<VideoImpl> video_impl;
};