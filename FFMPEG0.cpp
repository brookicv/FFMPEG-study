extern "C"
{
	# include <libavcodec\avcodec.h>
	# include <libavformat\avformat.h>
	# include <libswscale\swscale.h>
}
# include <iostream>
# include <stdio.h>

using namespace std;


extern "C"
{
	void saveFrame(AVFrame* pFrame, int width, int height, int iFrame)
	{
		FILE *pFile;
		char szFilename[32];
		int  y;

		// Open file
		sprintf(szFilename, "frame%d.ppm", iFrame);
		pFile = fopen(szFilename, "wb");
		if (pFile == NULL)
			return;

		// Write header
		fprintf(pFile, "P6\n%d %d\n255\n", width, height);

		// Write pixel data
		for (y = 0; y < height; y++)
			fwrite(pFrame->data[0] + y*pFrame->linesize[0], 1, width * 3, pFile);

		// Close file
		fclose(pFile);
	}
}

int main(int argv, char* argc[])
{
	av_register_all(); // 注册支持的文件格式及对应的codec

	char* filenName = "E:\\CCTV-Clip\\0912-2.mov";

	// 打开audio文件
	AVFormatContext* pFormatCtx = nullptr;
	// 读取文件头，将格式相关信息存放在AVFormatContext结构体中
	if (avformat_open_input(&pFormatCtx, filenName, nullptr, nullptr) != 0)
		return -1; // 打开失败

	// 检测文件的流信息
	if (avformat_find_stream_info(pFormatCtx, nullptr) < 0)
		return -1; // 没有检测到流信息 stream infomation

	// 在控制台输出文件信息
	av_dump_format(pFormatCtx, 0, filenName, 0);

	//查找视频流 video stream
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
		return -1; // 没有找到视频流video stream

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
	AVFrame* pFrameRGB = nullptr;

	pFrame = av_frame_alloc();
	pFrameRGB = av_frame_alloc();

	// 使用的缓冲区的大小
	int numBytes = 0;
	uint8_t* buffer = nullptr;

	numBytes = avpicture_get_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);
	buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));

	avpicture_fill((AVPicture*)pFrameRGB, buffer, AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);

	struct SwsContext* sws_ctx = nullptr;
	sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);

	AVPacket packet;
	int i = 0;
	while (av_read_frame(pFormatCtx, &packet) >= 0)
	{
		if (packet.stream_index == videoStream)
		{
			int frameFinished = 0;
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
			if (frameFinished)
			{
				sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data, pFrame->linesize, 0,
					pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);

				if (++i <= 5)
				{
					saveFrame(pFrameRGB, pCodecCtx->width, pCodecCtx->height, i);
				}
			}
		}
		
	}

	av_free_packet(&packet);

	av_free(buffer);
	av_frame_free(&pFrameRGB);

	av_frame_free(&pFrame);

	avcodec_close(pCodecCtx);
	avcodec_close(pCodecCtxOrg);

	avformat_close_input(&pFormatCtx);
	getchar();
	return 0;
}
