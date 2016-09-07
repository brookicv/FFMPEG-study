extern "C" {

#include <libavcodec\avcodec.h>
#include <libavformat\avformat.h>
#include <libswscale\swscale.h>
#include <libswresample\swresample.h>

}

#include <SDL.h>
#include <SDL_thread.h>

#include <iostream>
#include <queue>
#include <cassert>

using namespace std;

bool quit = false;

typedef struct PacketQueue
{
	queue<AVPacket> queue;

	int nb_packets;
	int size;

	SDL_mutex *mutex;
	SDL_cond *cond;

	PacketQueue()
	{
		nb_packets = 0;
		size = 0;

		mutex = SDL_CreateMutex();
		cond = SDL_CreateCond();
	}

	bool enQueue(const AVPacket *packet)
	{
		AVPacket pkt;
		if (av_packet_ref(&pkt, packet) < 0)
			return false;

		SDL_LockMutex(mutex);
		queue.push(pkt);

		size += pkt.size;
		nb_packets++;

		SDL_CondSignal(cond);
		SDL_UnlockMutex(mutex);

		return true;
	}

	bool deQueue(AVPacket *packet, bool block)
	{
		bool ret = false;

		SDL_LockMutex(mutex);
		while (true)
		{
			if (quit)
			{
				ret = false;
				break;
			}

			if (!queue.empty())
			{
				if (av_packet_ref(packet, &queue.front()) < 0)
				{
					ret = false;
					break;
				}
				queue.pop();
				nb_packets--;
				size -= packet->size;

				ret = true;
				break;
			}
			else if (!block)
			{
				ret = false;
				break;
			}
			else
			{
				SDL_CondWait(cond, mutex);
			}
		}
		SDL_UnlockMutex(mutex);
		return ret;
	}
}PacketQueue;

PacketQueue audioq;

// 从Packet中解码，返回解码的数据长度
int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size)
{
	AVFrame *frame = av_frame_alloc();
	int data_size = 0;
	AVPacket pkt;

	SwrContext *swr_ctx = nullptr;

	if (quit)
		return -1;
	if (!audioq.deQueue(&pkt, true))
		return -1;

	int ret = avcodec_send_packet(aCodecCtx, &pkt);
	if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
		return -1;

	ret = avcodec_receive_frame(aCodecCtx, frame);
	if (ret < 0 && ret != AVERROR_EOF)
		return -1;

	int index = av_get_channel_layout_channel_index(av_get_default_channel_layout(4), AV_CH_FRONT_CENTER);

	// 设置通道数或channel_layout
	if (frame->channels > 0 && frame->channel_layout == 0)
		frame->channel_layout = av_get_default_channel_layout(frame->channels);
	else if (frame->channels == 0 && frame->channel_layout > 0)
		frame->channels = av_get_channel_layout_nb_channels(frame->channel_layout);

	AVSampleFormat dst_format = AV_SAMPLE_FMT_S16;//av_get_packed_sample_fmt((AVSampleFormat)frame->format);
	Uint64 dst_layout = av_get_default_channel_layout(frame->channels);
	// 设置转换参数
	swr_ctx = swr_alloc_set_opts(nullptr, dst_layout, dst_format, frame->sample_rate,
		frame->channel_layout, (AVSampleFormat)frame->format, frame->sample_rate, 0, nullptr);
	if (!swr_ctx || swr_init(swr_ctx) < 0)
		return -1;

	// 计算转换后的sample个数 a * b / c
	int dst_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, frame->sample_rate) + frame->nb_samples, frame->sample_rate, frame->sample_rate, AVRounding(1));
	// 转换，返回值为转换后的sample个数
	int nb = swr_convert(swr_ctx, &audio_buf, dst_nb_samples, (const uint8_t**)frame->data, frame->nb_samples);
	data_size = frame->channels * nb * av_get_bytes_per_sample(dst_format);

	av_frame_free(&frame);
	swr_free(&swr_ctx);
	return data_size;
}

static const int MAX_AUDIO_FRAME_SIZE = 192000;
static const int SDL_AUDIO_BUFFER_SIZE = 1024;

void audio_callback(void* userdata, Uint8 *stream, int len)
{
	AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;

	int len1, audio_size;

	static uint8_t audio_buff[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
	static unsigned int audio_buf_size = 0;
	static unsigned int audio_buf_index = 0;

	SDL_memset(stream, 0, len);

	while (len > 0)// 想设备发送长度为len的数据
	{
		if (audio_buf_index >= audio_buf_size) // 缓冲区中无数据
		{
			// 从packet中解码数据
			audio_size = audio_decode_frame(aCodecCtx, audio_buff, sizeof(audio_buff));
			if (audio_size < 0) // 没有解码到数据或出错，填充0
			{
				audio_buf_size = 0;
				memset(audio_buff, 0, audio_buf_size);
			}
			else
				audio_buf_size = audio_size;

			audio_buf_index = 0;
		}
		len1 = audio_buf_size - audio_buf_index; // 缓冲区中剩下的数据长度
		if (len1 > len) // 向设备发送的数据长度为len
			len1 = len;

		SDL_MixAudio(stream, audio_buff + audio_buf_index, len, SDL_MIX_MAXVOLUME);

		len -= len1;
		stream += len1;
		audio_buf_index += len1;
	}
}

int main(int argv, char* argc[])
{
	av_register_all();

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
	char* filename = "E:\\Wildlife.wmv";

	AVFormatContext *pFormatCtx = nullptr;
	if (avformat_open_input(&pFormatCtx, filename, nullptr, nullptr) != 0)
		return -1;

	if (avformat_find_stream_info(pFormatCtx, nullptr) < 0)
		return -1;

	av_dump_format(pFormatCtx, 0, filename, 0);

	int audioStream = -1;
	for (int i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			audioStream = i;
			break;
		}
	}

	if (audioStream == -1)
		return -1;

	AVCodecContext* pCodecCtxOrg = nullptr;
	AVCodecContext* pCodecCtx = nullptr;

	AVCodec* pCodec = nullptr;

	pCodecCtxOrg = pFormatCtx->streams[audioStream]->codec; // codec context

	// 找到audio stream的 decoder
	pCodec = avcodec_find_decoder(pCodecCtxOrg->codec_id);

	if (!pCodec)
	{
		cout << "Unsupported codec!" << endl;
		return -1;
	}

	// 不直接使用从AVFormatContext得到的CodecContext，要复制一个
	pCodecCtx = avcodec_alloc_context3(pCodec);
	if (avcodec_copy_context(pCodecCtx, pCodecCtxOrg) != 0)
	{
		cout << "Could not copy codec context!" << endl;
		return -1;
	}


	// Set audio settings from codec info
	SDL_AudioSpec wanted_spec, spec;
	wanted_spec.freq = pCodecCtx->sample_rate;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.channels = pCodecCtx->channels;
	wanted_spec.silence = 0;
	wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
	wanted_spec.callback = audio_callback;
	wanted_spec.userdata = pCodecCtx;

	if (SDL_OpenAudio(&wanted_spec, &spec) < 0)
	{
		cout << "Open audio failed:" << SDL_GetError() << endl;
		getchar();
		return -1;
	}

	avcodec_open2(pCodecCtx, pCodec, nullptr);

	SDL_PauseAudio(0);

	AVPacket packet;
	while (av_read_frame(pFormatCtx, &packet) >= 0)
	{
		if (packet.stream_index == audioStream)
			audioq.enQueue(&packet);
		else
			av_packet_unref(&packet);
	}

	avformat_close_input(&pFormatCtx);
	getchar();
	return 0;
}