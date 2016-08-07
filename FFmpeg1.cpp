
# include <iostream>
# include <stdio.h>

# include <SDL.h>
# include <SDL_thread.h>

extern "C"
{
	# include <libavcodec\avcodec.h>
	# include <libavformat\avformat.h>
	# include <libswscale\swscale.h>
}

using namespace std;

int main(int argv,char* argc[])
{
	//1.注册支持的文件格式及对应的codec
	av_register_all(); 

	char* filenName = "F:\\test.rmvb";

	

	// 2.打开video文件
	AVFormatContext* pFormatCtx = nullptr;
	// 读取文件头，将格式相关信息存放在AVFormatContext结构体中
	if (avformat_open_input(&pFormatCtx, filenName, nullptr, nullptr) != 0)
		return -1; // 打开失败

	// 检测文件的流信息
	if (avformat_find_stream_info(pFormatCtx, nullptr) < 0)
		return -1; // 没有检测到流信息 stream infomation

	// 在控制台输出文件信息
	av_dump_format(pFormatCtx, 0, filenName, 0);

	//查找第一个视频流 video stream
	int videoStream = -1;
	for (int i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videoStream = i;
			break;
		}
	}

	if (videoStream == -1)
		return -1; // 没有查找到视频流video stream

	AVCodecContext* pCodecCtxOrg = nullptr;
	AVCodecContext* pCodecCtx = nullptr;

	AVCodec* pCodec = nullptr;

	pCodecCtxOrg = pFormatCtx->streams[videoStream]->codec; // codec context

	// 找到video stream的 decoder
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

	// open codec
	if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0)
		return -1; // Could open codec

	AVFrame* pFrame = nullptr;
	AVFrame* pFrameYUV = nullptr;

	pFrame = av_frame_alloc();
	pFrameYUV = av_frame_alloc();

	// 使用的缓冲区的大小
	int numBytes = 0;
	uint8_t* buffer = nullptr;

	numBytes = avpicture_get_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);
	buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));

	avpicture_fill((AVPicture*)pFrameYUV, buffer, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);

	struct SwsContext* sws_ctx = nullptr;
	sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);

	///////////////////////////////////////////////////////////////////////////
	//
	// SDL2.0
	//
	//////////////////////////////////////////////////////////////////////////
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
	SDL_Window* window = SDL_CreateWindow("FFmpeg Decode", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		pCodecCtx->width, pCodecCtx->height, SDL_WINDOW_OPENGL | SDL_WINDOW_MAXIMIZED);
	SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, 0);
	SDL_Texture* bmp = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING,
		pCodecCtx->width, pCodecCtx->height);
	SDL_Rect rect;
	rect.x = 0;
	rect.y = 0;
	rect.w = pCodecCtx->width;
	rect.h = pCodecCtx->height;

	SDL_Event event;

	AVPacket packet;
	while (av_read_frame(pFormatCtx, &packet) >= 0)
	{
		if (packet.stream_index == videoStream)
		{
			int frameFinished = 0;
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
			if (frameFinished)
			{
				sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data, pFrame->linesize, 0,
					pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);

				SDL_UpdateTexture(bmp, &rect, pFrameYUV->data[0], pFrameYUV->linesize[0]);
				SDL_RenderClear(renderer);
				SDL_RenderCopy(renderer, bmp, &rect, &rect);
				SDL_RenderPresent(renderer);

			}
		}
		av_free_packet(&packet);

		SDL_PollEvent(&event);
		switch (event.type)
		{
		case SDL_QUIT:
			SDL_Quit();
			av_free(buffer);
			av_frame_free(&pFrame);
			av_frame_free(&pFrameYUV);

			avcodec_close(pCodecCtx);
			avcodec_close(pCodecCtxOrg);

			avformat_close_input(&pFormatCtx);
			return 0;
		}
	}
	

	av_free(buffer);
	av_frame_free(&pFrame);
	av_frame_free(&pFrameYUV);

	avcodec_close(pCodecCtx);
	avcodec_close(pCodecCtxOrg);

	avformat_close_input(&pFormatCtx);

	getchar();
	return 0;
}
