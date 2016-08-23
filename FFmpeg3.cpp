extern "C" {

#include <libavcodec\avcodec.h>
#include <libavformat\avformat.h>
#include <libswscale\swscale.h>
#include <libswresample\swresample.h>

}

#include <SDL.h>
#include <SDL_thread.h>

#include <iostream>
#include <cassert>

using namespace std;

////////////////////////////////////////////
// 
// 常量定义
//
////////////////////////////////////////////
#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

#define VIDEO_PICTURE_QUEUE_SIZE 1	

AVFrame wanted_frame;

//////////////////////////////////////////
//
// 结构体声明
//
/////////////////////////////////////////

// Packet queue
typedef struct PacketQueue{
	AVPacketList *first_pkt;
	AVPacketList *last_pkt;
	int nb_packets;
	int size;
	SDL_mutex *mutex;
	SDL_cond *cond;
}PacketQueue;

typedef struct VideoPicture {
	SDL_Texture* bmp;
	int width;
	int height;
	int allocated;
}VideoPicture;

typedef struct VideoState{

	AVFormatContext *pFormatCtx;
	int videoStream;
	int audioStream;

	/* audio 相关的字段 */
	AVStream *audio_st;
	AVCodecContext *audio_ctx;
	PacketQueue audioq;
	uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
	unsigned int audio_buf_size;
	unsigned int audio_buf_index;
	AVFrame audio_frame;
	AVPacket audio_pkt;
	uint8_t *audio_pkt_data;
	int audio_pkt_size;

	/* video 相关的字段 */
	AVStream *video_st;
	AVCodecContext *video_ctx;
	PacketQueue videoq;
	struct SwsContext *sws_ctx;
	VideoPicture pictq[VIDEO_PICTURE_QUEUE_SIZE];
	int pictq_size;
	int pictq_rindex;
	int pictq_windex;
	SDL_mutex *pictq_mutex;
	SDL_cond *pictq_cond;

	/* 线程相关*/
	SDL_Thread *parse_tid;
	SDL_Thread *video_tid;

	char  filename[1024];
	int quit;
}VideoState;

SDL_Window *window;
SDL_Renderer *renderer;

VideoState *global_video_state;

void packet_queue_init(PacketQueue *q)
{
	q->first_pkt = nullptr;
	q->last_pkt = nullptr;
	q->nb_packets = 0;
	q->size = 0;
	
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
}

int packet_queue_get(PacketQueue *q, AVPacket *pkt)
{
	AVPacketList *pktl;
	pktl = (AVPacketList*)av_malloc(sizeof(AVPacketList));
	if (!pktl)
		return -1;

	if (av_packet_ref(&pktl->pkt, pkt) < 0)
		return -1;
	pktl->next = nullptr;

	SDL_LockMutex(q->mutex);

	if (!q->last_pkt)
		q->first_pkt = pktl;
	else
		q->last_pkt->next = pktl;
	q->last_pkt = pktl;

	q->nb_packets++;
	q->size += pktl->pkt.size;

	SDL_CondSignal(q->cond);
	
	SDL_UnlockMutex(q->mutex);

	return 0;
}

static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
	AVPacketList *pktl;
	int ret;

	SDL_LockMutex(q->mutex);

	while (true)
	{
		if (global_video_state->quit)
		{
			ret = -1;
			break;
		}

		pktl = q->first_pkt; // 取出队首节点
		if (pktl)
		{
			q->first_pkt = pktl->next;
			if (!q->first_pkt)
				q->last_pkt = nullptr;

			q->nb_packets--;
			q->size -= pktl->pkt.size;
			
			if (av_packet_ref(pkt, &pktl->pkt) < 0) // 复制AVpacket中的data
			{
				ret = -1;
				break;
			}

			av_free(pktl);
			ret = 1;
			break;
		}
		else if (!block)
		{
			ret = 0;
			break;
		}
		else 
		{
			SDL_CondWait(q->cond, q->mutex);
		}
	}

	SDL_UnlockMutex(q->mutex);
	return ret;
}

int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size)
{
	int len1;
	int data_size = 0;
	AVPacket *pkt = &is->audio_pkt;

	SwrContext *swr_ctx = nullptr;

	while (true)
	{
		while (is->audio_pkt_size > 0)
		{
			int got_frame = 0;
			len1 = avcodec_decode_audio4(is->audio_ctx, &is->audio_frame, &got_frame, pkt);
			if (len1 < 0)
			{
				is->audio_pkt_size = 0;
				break;
			}
			data_size = 0;
			if (got_frame)
			{
				data_size = av_samples_get_buffer_size(nullptr, is->audio_ctx->channels,
					is->audio_frame.nb_samples, is->audio_ctx->sample_fmt);
				assert(data_size <= buf_size);

				if (is->audio_frame.channels > 0 && is->audio_frame.channel_layout == 0)
					is->audio_frame.channel_layout = av_get_default_channel_layout(is->audio_frame.channels);
				else if (is->audio_frame.channels == 0 && is->audio_frame.channel_layout > 0)
					is->audio_frame.channels = av_get_channel_layout_nb_channels(is->audio_frame.channel_layout);

				swr_ctx = swr_alloc_set_opts(nullptr, wanted_frame.channel_layout, (AVSampleFormat)wanted_frame.format,
					wanted_frame.sample_rate, is->audio_frame.channel_layout, (AVSampleFormat)is->audio_frame.format, is->audio_frame.sample_rate, 0, nullptr);
				if (!swr_ctx || swr_init(swr_ctx) < 0)
				{
					cout << "swr_init failed" << endl;
					break;
				}

				int dst_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, is->audio_frame.sample_rate) + is->audio_frame.nb_samples,
					wanted_frame.sample_rate, wanted_frame.format, AVRounding(1));
				int len2 = swr_convert(swr_ctx, (uint8_t**)&(is->audio_buf), dst_nb_samples, (const uint8_t**)is->audio_frame.data, is->audio_frame.nb_samples);
				if (len2 < 0)
				{
					cout << "swr_convert failed" << endl;
					break;
				}

				return wanted_frame.channels * len2 * av_get_bytes_per_sample((AVSampleFormat)wanted_frame.format);
			}

			is->audio_pkt_data += len1;
			is->audio_pkt_size -= len1;
			if (data_size <= 0)
				continue;		
		}
		if (pkt->data)
			av_packet_unref(pkt);
		if (is->quit)
			return -1;

		// next packet
		if (packet_queue_get(&is->audioq, pkt, 1) < 0)
			return -1;
		is->audio_pkt_data = pkt->data;
		is->audio_pkt_size = pkt->size;
	}
}
int main(int argv, char* argc[])
{
	return 0;
}