module;

#include <glm/glm.hpp>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <memory>
#include <functional>
#include <string>
#include <span>
#include <thread>
#include <optional>
#include <mutex>
#include <queue>
#include <array>

export module video;

using namespace std;
using namespace glm;

#define CHECK_SUCCESS(cmd, errormsg) if(!(cmd)) { throw exception(errormsg); }
char av_error_buffer[2048];
#define CHECK_AV_SUCCESS(cmd) [[gsl::suppress(bounds.3)]] { const int __res = (cmd); if(__res < 0) { av_strerror(__res, av_error_buffer, sizeof(av_error_buffer)); throw exception(av_error_buffer); } }

constexpr int frames_queue_max_length = 10;

struct VideoImpl
{
	AVFormatContext* format_context{};
	AVStream* video_stream{};
	AVCodecContext* codec_decoder_context{};

	AVFrame* input_frame{};
	AVPacket* input_packet{};

	queue<AVFrame*> frames_queue;
	mutex frames_queue_mutex;
	condition_variable frames_queue_cv;
	bool playing = false, seek_needs_display = false;

	optional<double> seek_timestamp_sec{};					// if set, triggers the frame read thread to clear the frame cache, seek to this position and restart the decoding
};

int av_get_next_frame(const VideoImpl* video_impl, const int64_t skip_pts, function<void(AVFrame* frame)> process_frame)
{
	while (av_read_frame(video_impl->format_context, video_impl->input_packet) >= 0)
	{
		if (video_impl->input_packet->stream_index == video_impl->video_stream->index)
		{
			// get the frame
			CHECK_AV_SUCCESS(avcodec_send_packet(video_impl->codec_decoder_context, video_impl->input_packet));

			int res{};
			while (1)
			{
				res = avcodec_receive_frame(video_impl->codec_decoder_context, video_impl->input_frame);
				if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) break;
				CHECK_AV_SUCCESS(res);

				// skip frames as needed for seeking
				if (video_impl->input_frame->pts >= skip_pts)
					process_frame(video_impl->input_frame);

				av_frame_unref(video_impl->input_frame);

				return 0;
			}
		}

		av_packet_unref(video_impl->input_packet);
	}

	return AVERROR_EOF;
}

AVFrame* av_deep_clone_frame(AVFrame* src)
{
	auto dst = av_frame_alloc();
	dst->format = src->format;
	dst->width = src->width;
	dst->height = src->height;
	dst->channels = src->channels;
	dst->channel_layout = src->channel_layout;
	dst->nb_samples = src->nb_samples;

	av_frame_get_buffer(dst, 32);
	av_frame_copy(dst, src);
	av_frame_copy_props(dst, src);

	return dst;
}

export class Video
{
	unique_ptr<VideoImpl> video_impl = make_unique<VideoImpl>();

public:
	Video(const char* url)
	{
		// read the file header
		CHECK_AV_SUCCESS(avformat_open_input(&video_impl->format_context, url, nullptr, nullptr));

		// get the stream information
		CHECK_AV_SUCCESS(avformat_find_stream_info(video_impl->format_context, nullptr));

		// find the first video stream
		for (const auto current_video_stream : span<AVStream*>(video_impl->format_context->streams, video_impl->format_context->nb_streams))
			if (current_video_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
			{
				video_impl->video_stream = current_video_stream;
				break;
			}
		CHECK_SUCCESS(video_impl->video_stream, "Could not find a video stream.");

		// dump information about it
		av_dump_format(video_impl->format_context, video_impl->video_stream->index, url, false);

		// find the decoder
		AVCodec const* codec_decoder = avcodec_find_decoder(video_impl->video_stream->codecpar->codec_id);
		CHECK_SUCCESS(codec_decoder, "Could not find decoder codec.");

		// copy the decoder codec context locally, since we must not use the the global version
		video_impl->codec_decoder_context = avcodec_alloc_context3(codec_decoder);
		CHECK_AV_SUCCESS(avcodec_parameters_to_context(video_impl->codec_decoder_context, video_impl->video_stream->codecpar));

		// multi-threaded decoder, use 4 threads
		video_impl->codec_decoder_context->thread_count = 4;
		video_impl->codec_decoder_context->thread_type = FF_THREAD_FRAME;

		// open the codec
		avcodec_open2(video_impl->codec_decoder_context, codec_decoder, nullptr);

		video_impl->input_frame = av_frame_alloc();
		video_impl->input_packet = av_packet_alloc();

		thread([&]
			{
				while (true)
				{
					optional<double> _seek_timestamp_sec;
					int64_t ts_pts = INT64_MIN;
					{
						lock_guard<mutex> lg(video_impl->frames_queue_mutex);
						_seek_timestamp_sec = video_impl->seek_timestamp_sec;
					}
					video_impl->seek_timestamp_sec.reset();

					// seek if needed
					if (_seek_timestamp_sec)
					{
						// convert seconds to pts
						ts_pts = static_cast<int64_t>(*_seek_timestamp_sec / av_q2d(video_impl->video_stream->time_base));

						// seek before the time stamp 
						avformat_seek_file(video_impl->format_context, video_impl->video_stream->index, INT64_MIN, ts_pts, ts_pts, AVSEEK_FLAG_BACKWARD);

						// flush the context
						avcodec_flush_buffers(video_impl->codec_decoder_context);
					}

					while (av_get_next_frame(video_impl.get(), ts_pts, [&](AVFrame* frame)
						{
							auto new_frame = av_deep_clone_frame(frame);

							// queue the frame
							unique_lock<mutex> lock(video_impl->frames_queue_mutex);
							video_impl->frames_queue_cv.wait(lock, [&] { return video_impl->seek_timestamp_sec || video_impl->frames_queue.size() < frames_queue_max_length; });

							// seek instead if required
							if (video_impl->seek_timestamp_sec)
							{
								av_frame_unref(new_frame);
								return;
							}

							video_impl->frames_queue.push(new_frame);
						}) != AVERROR_EOF && !video_impl->seek_timestamp_sec)
					{
					}
				}
			}).detach();
	}

	bool playing() { return video_impl->playing; }
	void play(bool enabled) { video_impl->playing = enabled; }

	void seek_pts(int64_t pts)
	{
		video_impl->seek_timestamp_sec = pts * av_q2d(video_impl->video_stream->time_base);
		{
			lock_guard<mutex> lock(video_impl->frames_queue_mutex);
			clear_frames_queue();
		}
		video_impl->seek_needs_display = true;
	}

	// needs to be under a frames_queue_mutex lock
	void clear_frames_queue()
	{
		// clear the queue and free up the allocated frames
		while (!video_impl->frames_queue.empty())
		{
			av_frame_unref(video_impl->frames_queue.front());
			video_impl->frames_queue.pop();
		}

		// notify the decoder thread that we need more frames to replace what we just cleared
		video_impl->frames_queue_cv.notify_all();
	}

	bool consume_frame(function<void(int64_t, span<uint8_t>[3])> process)
	{
		AVFrame* frame;
		{
			lock_guard<mutex> lock(video_impl->frames_queue_mutex);
			if (video_impl->frames_queue.empty())
				return false;					// no data, buffer underflow

			frame = video_impl->frames_queue.front();

			span<uint8_t> planes[] =
			{
				{ frame->data[0], frame->data[0] + frame->linesize[0] },
				{ frame->data[1], frame->data[1] + frame->linesize[1] },
				{ frame->data[2], frame->data[2] + frame->linesize[2] }
			};
			process(frame->best_effort_timestamp, planes);

			video_impl->seek_needs_display = false;
			av_frame_free(&frame);
			video_impl->frames_queue.pop();
		}

		// notify the decoder thread that we consumed a frame
		video_impl->frames_queue_cv.notify_all();

		return true;
	}

	ivec2 frame_size() { return { video_impl->video_stream->codecpar->width, video_impl->video_stream->codecpar->height }; }

	double time_base() { return av_q2d(video_impl->video_stream->time_base); }
	int64_t start_pts() { return video_impl->video_stream->start_time; }
	int64_t duration_pts() { return video_impl->video_stream->duration; }
	double duration_sec() { return duration_pts() * time_base(); }

	bool force_display() { return video_impl->seek_needs_display; }
	void set_force_display() { video_impl->seek_needs_display = true; }

	bool colorspace_is_bt709() { return video_impl->codec_decoder_context->colorspace == AVCOL_SPC_BT709; }
};