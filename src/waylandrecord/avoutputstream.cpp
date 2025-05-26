// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

/*
时间戳，DTS(decoding time stamp)，PTS(presention time stamp)，CTS（current time stamp）。
---
ffmepg中的时间戳，是以微秒为单位，关乎timebase变量，它是作为dts、pts的时间基准粒度，数值会很大。
---
其中函数av_rescale_q()是很多的，AV_ROUND_NEAR_INF是就近、中间从零，av_rescale_rnd它是计算a*b/c，
传入参数为八字节，为避免溢出，里面做了与INT_MAX的比较，分开计算。将以 "时钟基c" 表示的 数值a 转换成以 "时钟基b" 来表示。
---
FFmpeg中用AVPacket结构体来描述解码前或编码后的压缩包，用AVFrame结构体来描述解码后或编码前的信号帧。
对于视频来说，AVFrame就是视频的一帧图像。这帧图像什么时候显示给用户，就取决于它的PTS。
DTS是AVPacket里的一个成员，表示这个压缩包应该什么时候被解码。
如果视频里各帧的编码是按输入顺序（也就是显示顺序）依次进行的，那么解码和显示时间应该是一致的。
可事实上，在大多数编解码标准（如H.264或HEVC）中，编码顺序和输入顺序并不一致。 于是才会需要PTS和DTS这两种不同的时间戳。
*/

#include "avoutputstream.h"
#include "../utils/log.h"
#include <unistd.h>
#include <QTime>
#include <QDebug>
#include <QThread>

CAVOutputStream::CAVOutputStream(WaylandIntegration::WaylandIntegrationPrivate *context):
    m_context(context),
    m_pSysAudioSwrContext(nullptr),
    m_micAudioFifo(nullptr),
    m_sysAudioFifo(nullptr)
{
    Q_UNUSED(m_context)
    m_videoCodecID  = AV_CODEC_ID_NONE;
    m_micAudioCodecID = AV_CODEC_ID_NONE;
    m_sysAudioCodecID = AV_CODEC_ID_NONE;
    mMic_frame = nullptr;
    mSpeaker_frame = nullptr;
    m_bMix = false;
    m_out_buffer = nullptr;
    m_width = 320;
    m_height = 240;
    m_framerate = 25;
    m_video_bitrate = 500000;
    m_samplerate = 0;
    m_channels = 1;
    m_audio_bitrate = 32000;
    m_samplerate_card = 0;
    is_fifo_scardinit = 0;
    m_channels_card = 1;
    m_audio_bitrate_card = 32000;
    m_videoStream = nullptr;
    m_micAudioStream = nullptr;
    m_sysAudioStream = nullptr;
    m_videoFormatContext = nullptr;
    pCodecCtx = nullptr;
    m_pMicCodecContext = nullptr;
    m_pSysCodecContext = nullptr;
    pCodecCtx_amix = nullptr;
    pCodec = nullptr;
    pCodec_a = nullptr;
    pCodec_aCard = nullptr;
    pCodec_amix = nullptr;
    pFrameYUV = nullptr;
    m_pVideoSwsContext = nullptr;
    m_pMicAudioSwrContext = nullptr;
    m_nb_samples = 0;
    m_convertedMicSamples = nullptr;
    m_convertedSysSamples = nullptr;
    m_start_mix_time = 0;
    m_next_vid_time = 0;
    m_next_aud_time = 0;
    audio_amix_st = nullptr;
    m_nLastAudioPresentationTime = 0;
    m_nLastAudioCardPresentationTime = 0;
    m_nLastAudioMixPresentationTime = 0;
    m_mixCount = 0;
    m_videoType = videoType::MP4;
    m_left = 0;
    m_top = 0;
    m_right = 0;
    m_bottom = 0;
    m_path = nullptr;
    m_isWriteFrame = false;
    filter_graph = nullptr;
    buffersink_ctx = nullptr;
    buffersrc_ctx1 = nullptr;
    buffersrc_ctx2 = nullptr;
    tmpFifoFailed = 0;
    m_isOverWrite = false;

    m_gopsize = 0;
    m_channels_layout = 0;
    m_channels_card_layout = 0;
    avlibInterface::m_avcodec_register_all();
    avlibInterface::m_av_register_all();
}

CAVOutputStream::~CAVOutputStream(void)
{
    printf("Desctruction Onput!\n");
    if (m_micAudioFifo) {
        audioFifoFree(m_micAudioFifo);
        m_micAudioFifo = nullptr;
    }
    if (nullptr != m_sysAudioFifo) {
        audioFifoFree(m_sysAudioFifo);
        m_sysAudioFifo = nullptr;
    }
    if (nullptr != m_path) {
        delete[] m_path;
    }
}



//初始化视频编码器
void CAVOutputStream::SetVideoCodecProp(AVCodecID codec_id, int framerate, int bitrate, int gopsize, int width, int height)
{
    qCInfo(dsrApp) << "Setting video codec properties - codec_id:" << codec_id << "framerate:" << framerate << "bitrate:" << bitrate << "GOP size:" << gopsize << "resolution:" << width << "x" << height;
    m_videoCodecID  = codec_id;
    m_width = width;
    m_height = height;
    m_framerate = ((framerate == 0) ? 10 : framerate);
    m_video_bitrate = bitrate;
    m_gopsize = gopsize;
    if (framerate == 0) {
        qCWarning(dsrApp) << "Framerate was 0, adjusted to default value:" << m_framerate;
    }
    qCDebug(dsrApp) << "Video codec properties set successfully";
}

//初始化音频编码器
void CAVOutputStream::SetAudioCodecProp(AVCodecID codec_id, int samplerate, int channels, int layout, int bitrate)
{
    qCInfo(dsrApp) << "Setting microphone audio codec properties - codec_id:" << codec_id << "samplerate:" << samplerate << "channels:" << channels << "layout:" << layout << "bitrate:" << bitrate;
    m_micAudioCodecID = codec_id;
    m_samplerate = samplerate;
    m_channels = channels;
    m_channels_layout = layout;
    m_audio_bitrate = bitrate;
    qCDebug(dsrApp) << "Microphone audio codec properties set successfully";
}
void CAVOutputStream::SetAudioCardCodecProp(AVCodecID codec_id, int samplerate, int channels, int layout, int bitrate)
{
    qCInfo(dsrApp) << "Setting system audio codec properties - codec_id:" << codec_id << "samplerate:" << samplerate << "channels:" << channels << "layout:" << layout << "bitrate:" << bitrate;
    m_sysAudioCodecID = codec_id;
    m_samplerate_card = samplerate;
    m_channels_card = channels;
    m_channels_card_layout = layout;
    m_audio_bitrate_card = bitrate;
    qCDebug(dsrApp) << "System audio codec properties set successfully";
}
int CAVOutputStream::init_filters()
{
    qCInfo(dsrApp) << "Initializing audio filters for mixing";
    static const char *filter_descr = "[in0][in1]amix=inputs=2[out]";//"[in0][in1]amix=inputs=2[out]";amerge//"aresample=8000,aformat=sample_fmts=s16:channel_layouts=mono";
    char args1[512];
    char args2[512];
    int ret = 0;
    string formatStr = "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%";
    formatStr.append(PRIx64);
    AVFilterInOut *filter_outputs[2];
    const AVFilter *abuffersrc1  = avlibInterface::m_avfilter_get_by_name("abuffer");
    const AVFilter *abuffersrc2  = avlibInterface::m_avfilter_get_by_name("abuffer");
    const AVFilter *abuffersink = avlibInterface::m_avfilter_get_by_name("abuffersink");
    AVFilterInOut *outputs1 = avlibInterface::m_avfilter_inout_alloc();
    AVFilterInOut *outputs2 = avlibInterface::m_avfilter_inout_alloc();
    AVFilterInOut *inputs  = avlibInterface::m_avfilter_inout_alloc();
    static  enum AVSampleFormat out_sample_fmts[] = { pCodecCtx_amix->sample_fmt, AV_SAMPLE_FMT_NONE }; //{ AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE };
    static const int64_t out_channel_layouts[] = {static_cast<int64_t>(pCodecCtx_amix->channel_layout), -1};
    static const int out_sample_rates[] = { pCodecCtx_amix->sample_rate, -1 };
    const AVFilterLink *outlink;
    AVRational time_base_1 = m_pMicCodecContext->time_base;
    AVRational time_base_2 = m_pSysCodecContext->time_base;
    filter_graph = avlibInterface::m_avfilter_graph_alloc();
    if (!outputs1 || !inputs || !filter_graph) {
        qCCritical(dsrApp) << "Failed to allocate filter components for audio mixing";
        ret = AVERROR(ENOMEM);
        avlibInterface::m_avfilter_inout_free(&inputs);

        avlibInterface::m_avfilter_inout_free(&outputs1);
        avlibInterface::m_avfilter_inout_free(&outputs2);
        return 1;
    }
    AVCodecContext *dec_ctx1;
    AVCodecContext *dec_ctx2;
    /* buffer audio source: the decoded frames from the decoder will be inserted here. */
    dec_ctx1 = m_pMicCodecContext;
    if (!dec_ctx1->channel_layout)
        dec_ctx1->channel_layout = static_cast<uint64_t>(avlibInterface::m_av_get_default_channel_layout(dec_ctx1->channels));
    snprintf(args1,
             sizeof(args1),
             formatStr.c_str(),
             time_base_1.num,
             time_base_1.den,
             dec_ctx1->sample_rate,
             avlibInterface::m_av_get_sample_fmt_name(dec_ctx1->sample_fmt),
             dec_ctx1->channel_layout);
    qCDebug(dsrApp) << "Creating first audio buffer source filter with args:" << args1;
    ret = avlibInterface::m_avfilter_graph_create_filter(&buffersrc_ctx1, abuffersrc1, "in0",
                                                         args1, nullptr, filter_graph);

    if (ret < 0) {
        qCCritical(dsrApp) << "Cannot create first audio buffer source filter, error code:" << ret;
        //av_log(nullptr, AV_LOG_ERROR, "Cannot create audio buffer source\n");
        avlibInterface::m_avfilter_inout_free(&inputs);

        avlibInterface::m_avfilter_inout_free(&outputs1);
        avlibInterface::m_avfilter_inout_free(&outputs2);
        return 1;
    }
    /* buffer audio source: the decoded frames from the decoder will be inserted here. */
    dec_ctx2 = m_pSysCodecContext;
    snprintf(args2, sizeof(args2),
             formatStr.c_str(),
             time_base_2.num, time_base_2.den, dec_ctx2->sample_rate,
             avlibInterface::m_av_get_sample_fmt_name(dec_ctx2->sample_fmt), dec_ctx2->channel_layout);
    qCDebug(dsrApp) << "Creating second audio buffer source filter with args:" << args2;
    ret = avlibInterface::m_avfilter_graph_create_filter(&buffersrc_ctx2, abuffersrc2, "in1",
                                                         args2, nullptr, filter_graph);
    if (ret < 0) {
        qCCritical(dsrApp) << "Cannot create second audio buffer source filter, error code:" << ret;
        //av_log(nullptr, AV_LOG_ERROR, "Cannot create audio buffer source\n");

        avlibInterface::m_avfilter_inout_free(&inputs);

        avlibInterface::m_avfilter_inout_free(&outputs1);
        avlibInterface::m_avfilter_inout_free(&outputs2);
        return 1;
    }

    /* buffer audio sink: to terminate the filter chain. */

    ret = avlibInterface::m_avfilter_graph_create_filter(&buffersink_ctx, abuffersink, "out",

                                                         nullptr, nullptr, filter_graph);

    if (ret < 0) {
        qCCritical(dsrApp) << "Cannot create audio buffer sink, error code:" << ret;
        //av_log(nullptr, AV_LOG_ERROR, "Cannot create audio buffer sink\n");

        avlibInterface::m_avfilter_inout_free(&inputs);

        avlibInterface::m_avfilter_inout_free(&outputs1);
        avlibInterface::m_avfilter_inout_free(&outputs2);
        return 1;

    }

    //ret = av_opt_set_int_list(buffersink_ctx, "sample_fmts", out_sample_fmts, -1, AV_OPT_SEARCH_CHILDREN);
    ret = avlibInterface::m_av_opt_set_bin(buffersink_ctx, "sample_fmts", (const uint8_t *)(out_sample_fmts),
                                           avlibInterface::m_av_int_list_length_for_size(sizeof(*(out_sample_fmts)), out_sample_fmts, -1) * sizeof(*(out_sample_fmts)), AV_OPT_SEARCH_CHILDREN);


    if (ret < 0) {
        qCCritical(dsrApp) << "Cannot set output sample format, error code:" << ret;
        //av_log(nullptr, AV_LOG_ERROR, "Cannot set output sample format\n");

        avlibInterface::m_avfilter_inout_free(&inputs);

        avlibInterface::m_avfilter_inout_free(&outputs1);
        avlibInterface::m_avfilter_inout_free(&outputs2);
        return 1;

    }


    //ret = av_opt_set_int_list(buffersink_ctx, "channel_layouts", out_channel_layouts, -1, AV_OPT_SEARCH_CHILDREN);
    ret = avlibInterface::m_av_opt_set_bin(buffersink_ctx, "channel_layouts", (const uint8_t *)(out_channel_layouts),
                                           avlibInterface::m_av_int_list_length_for_size(sizeof(*(out_channel_layouts)), out_channel_layouts, -1) * sizeof(*(out_channel_layouts)), AV_OPT_SEARCH_CHILDREN);


    if (ret < 0) {
        qCCritical(dsrApp) << "Cannot set output channel layout, error code:" << ret;
        //av_log(nullptr, AV_LOG_ERROR, "Cannot set output channel layout\n");

        avlibInterface::m_avfilter_inout_free(&inputs);

        avlibInterface::m_avfilter_inout_free(&outputs1);
        avlibInterface::m_avfilter_inout_free(&outputs2);
        return 1;

    }

    //ret = av_opt_set_int_list(buffersink_ctx, "sample_rates", out_sample_rates, -1,AV_OPT_SEARCH_CHILDREN);
    ret = avlibInterface::m_av_opt_set_bin(buffersink_ctx, "sample_rates", (const uint8_t *)(out_sample_rates),
                                           avlibInterface::m_av_int_list_length_for_size(sizeof(*(out_sample_rates)), out_sample_rates, -1) * sizeof(*(out_sample_rates)), AV_OPT_SEARCH_CHILDREN);

    if (ret < 0) {
        qCCritical(dsrApp) << "Cannot set output sample rate, error code:" << ret;
        //av_log(nullptr, AV_LOG_ERROR, "Cannot set output sample rate\n");

        avlibInterface::m_avfilter_inout_free(&inputs);

        avlibInterface::m_avfilter_inout_free(&outputs1);
        avlibInterface::m_avfilter_inout_free(&outputs2);
        return 1;

    }

    /*

     * Set the endpoints for the filter graph. The filter_graph will

     * be linked to the graph described by filters_descr.

     */



    /*

     * The buffer source output must be connected to the input pad of

     * the first filter described by filters_descr; since the first

     * filter input label is not specified, it is set to "in" by

     * default.

     */

    outputs1->name       = avlibInterface::m_av_strdup("in0");
    outputs1->filter_ctx = buffersrc_ctx1;
    outputs1->pad_idx    = 0;
    outputs1->next       = outputs2;
    outputs2->name       = avlibInterface::m_av_strdup("in1");
    outputs2->filter_ctx = buffersrc_ctx2;
    outputs2->pad_idx    = 0;
    outputs2->next       = nullptr;

    /*

     * The buffer sink input must be connected to the output pad of

     * the last filter described by filters_descr; since the last

     * filter output label is not specified, it is set to "out" by

     * default.

     */
    inputs->name       = avlibInterface::m_av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = nullptr;


    filter_outputs[0] = outputs1;

    filter_outputs[1] = outputs2;


    qCDebug(dsrApp) << "Parsing audio filter graph with description:" << filter_descr;

    if ((ret = avlibInterface::m_avfilter_graph_parse_ptr(filter_graph, filter_descr,

                                                          &inputs, filter_outputs, nullptr)) < 0)//filter_outputs

    {
        qCCritical(dsrApp) << "Filter graph parse failed, error code:" << ret;
        //av_log(nullptr, AV_LOG_ERROR, "parse ptr fail, ret: %d\n", ret);

        avlibInterface::m_avfilter_inout_free(&inputs);

        avlibInterface::m_avfilter_inout_free(&outputs1);
        avlibInterface::m_avfilter_inout_free(&outputs2);
        return 1;

    }



    if ((ret = avlibInterface::m_avfilter_graph_config(filter_graph, nullptr)) < 0)

    {
        qCCritical(dsrApp) << "Filter graph config failed, error code:" << ret;
        //av_log(nullptr, AV_LOG_ERROR, "config graph fail, ret: %d\n", ret);

        avlibInterface::m_avfilter_inout_free(&inputs);

        avlibInterface::m_avfilter_inout_free(&outputs1);
        avlibInterface::m_avfilter_inout_free(&outputs2);
        return 1;
    }
    /* Print summary of the sink buffer

     * Note: args buffer is reused to store channel layout string */
    outlink = buffersink_ctx->inputs[0];

    avlibInterface::m_av_get_channel_layout_string(args1, sizeof(args1), -1, outlink->channel_layout);

    /*
    av_log(nullptr, AV_LOG_INFO, "Output: srate:%dHz fmt:%s chlayout:%s\n",
           outlink->sample_rate,
           static_cast<char *>(av_x_if_null(avlibInterface::m_av_get_sample_fmt_name(static_cast<AVSampleFormat>(outlink->format)), "?")),
           args1);
           */
    mMic_frame = avlibInterface::m_av_frame_alloc();
    mSpeaker_frame = avlibInterface::m_av_frame_alloc();
    avlibInterface::m_avfilter_inout_free(&inputs);
    avlibInterface::m_avfilter_inout_free(filter_outputs);

    qCInfo(dsrApp) << "Audio filters initialization completed successfully";
    return ret;

}
int CAVOutputStream::init_context_amix(int channel, uint64_t channel_layout, int sample_rate, int64_t bit_rate)
{
    qCInfo(dsrApp) << "Initializing audio mixing context with channel:" << channel << "layout:" << channel_layout << "sample_rate:" << sample_rate << "bit_rate:" << bit_rate;
    Q_UNUSED(channel)
    Q_UNUSED(channel_layout)
    Q_UNUSED(sample_rate)
    Q_UNUSED(bit_rate)
    pCodec_amix = avlibInterface::m_avcodec_find_encoder(m_sysAudioCodecID);
    if (!pCodec_amix) {
        qCCritical(dsrApp) << "Cannot find output audio encoder for system audio, codec ID:" << m_sysAudioCodecID;
        printf("Can not find output audio encoder! (没有找到合适的编码器！)\n");
        return false;
    }

    //pCodecCtx_amix = avlibInterface::m_avcodec_alloc_context3(pCodec_amix); //注释此行无效代码，同时解决内存泄露
    pCodecCtx_amix = avlibInterface::m_avcodec_alloc_context3(pCodec_a);
    pCodecCtx_amix->channels = m_channels;
    pCodecCtx_amix->channel_layout = static_cast<uint64_t>(m_channels_layout);
    if (pCodecCtx_amix->channel_layout == 0) {
        qCWarning(dsrApp) << "Channel layout is 0, setting to default stereo layout";
        pCodecCtx_amix->channel_layout = AV_CH_LAYOUT_STEREO;
        pCodecCtx_amix->channels = avlibInterface::m_av_get_channel_layout_nb_channels(pCodecCtx_amix->channel_layout);
    }

    pCodecCtx_amix->sample_rate = m_samplerate;
    pCodecCtx_amix->sample_fmt = pCodec_amix->sample_fmts[0];
    pCodecCtx_amix->bit_rate = m_audio_bitrate;
    pCodecCtx_amix->time_base.num = 1;
    pCodecCtx_amix->time_base.den = pCodecCtx_amix->sample_rate;

    if (m_sysAudioCodecID == AV_CODEC_ID_AAC) {
        qCDebug(dsrApp) << "Using AAC codec, enabling experimental compliance";
        /** Allow the use of the experimental AAC encoder */
        pCodecCtx_amix->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    }

    /* Some formats want stream headers to be separate. */
    if (m_videoFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
        pCodecCtx_amix->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avlibInterface::m_avcodec_open2(pCodecCtx_amix, pCodec_amix, nullptr) < 0) {
        qCCritical(dsrApp) << "Failed to open output audio encoder for mixing";
        printf("Failed to open ouput audio encoder! (编码器打开失败！)\n");
        return false;
    }
    audio_amix_st = avlibInterface::m_avformat_new_stream(m_videoFormatContext, pCodec_amix);
    //Add a new stream to output,should be called by the user before avformat_write_header() for muxing
    if (audio_amix_st) {
        //                audio_amix_st->index = 1;
        if (audio_amix_st == nullptr) {
            qCCritical(dsrApp) << "Failed to create mixing audio stream";
            return false;
        }
        audio_amix_st->time_base.num = 1;
        audio_amix_st->time_base.den = pCodecCtx_amix->sample_rate;//48000
        audio_amix_st->codec = pCodecCtx_amix;
        qCDebug(dsrApp) << "Audio mixing stream created successfully with sample rate:" << pCodecCtx_amix->sample_rate;
    }
    qCInfo(dsrApp) << "Audio mixing context initialization completed successfully";
    return true;
}
//创建编码器和混合器
bool CAVOutputStream::open(QString path)
{
    qCInfo(dsrApp) << "Opening output stream to path:" << path;
    QByteArray pathArry = path.toLocal8Bit();
    m_path = new char[strlen(pathArry.data()) + 1];
    strcpy(m_path, pathArry.data());

    avlibInterface::m_avformat_alloc_output_context2(&m_videoFormatContext, nullptr, nullptr, m_path);

    if (m_videoCodecID  != 0) {
        qCInfo(dsrApp) << "Initializing video encoder with codec ID:" << m_videoCodecID;
        printf("FLQQ,video encoder initialize\n\n");
        pCodec = avlibInterface::m_avcodec_find_encoder(m_videoCodecID);

        if (!pCodec) {
            qCCritical(dsrApp) << "Cannot find output video encoder for codec ID:" << m_videoCodecID;
            printf("Can not find output video encoder! (没有找到合适的编码器！)\n");
            return false;
        }

        pCodecCtx = avlibInterface::m_avcodec_alloc_context3(pCodec);
        pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
#ifdef VIDEO_RESCALE
        pCodecCtx->width = m_width / 2;
        pCodecCtx->height = m_height / 2;
        qCDebug(dsrApp) << "Video rescaling enabled, resolution set to:" << pCodecCtx->width << "x" << pCodecCtx->height;
#else
        pCodecCtx->width = m_width;
        pCodecCtx->height = m_height;
        qCDebug(dsrApp) << "Video resolution set to:" << pCodecCtx->width << "x" << pCodecCtx->height;
#endif
        pCodecCtx->time_base.num = 1;
        pCodecCtx->time_base.den = m_framerate;
        //pCodecCtx->bit_rate = m_video_bitrate;
        //added by flq
        //pCodecCtx->flags |= CODEC_FLAG_QSCALE; //VBR（可变率控制）
        //added end
        pCodecCtx->gop_size = m_gopsize;
        /* Some formats want stream headers to be separate. */
        if (m_videoFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
            pCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;


        AVDictionary *param = nullptr;

        //set H264 codec param
        if (m_videoCodecID  == AV_CODEC_ID_H264) {
            qCDebug(dsrApp) << "Configuring H264 codec parameters";
            pCodecCtx->qmin = 10;
            //low
            pCodecCtx->qmax = 51;
            pCodecCtx->max_b_frames = 0;

            // Set H264 preset and tune
            avlibInterface::m_av_dict_set(&param, "preset", "ultrafast", 0);
            //                av_dict_set(&param, "tune", "zerolatency", 0);

        }

        if (avlibInterface::m_avcodec_open2(pCodecCtx, pCodec, &param) < 0) {
            qCCritical(dsrApp) << "Failed to open output video encoder";
            printf("Failed to open output video encoder! (编码器打开失败！)\n");
            return false;
        }

        //Add a new stream to output,should be called by the user before avformat_write_header() for muxing
        m_videoStream = avlibInterface::m_avformat_new_stream(m_videoFormatContext, pCodec);
        if (m_videoStream == nullptr) {
            qCCritical(dsrApp) << "Failed to create video stream";
            return false;
        }
        m_videoStream->time_base.num = 1;
        m_videoStream->time_base.den = m_framerate;
        m_videoStream->codec = pCodecCtx;
        //Initialize the buffer to store YUV frames to be encoded.
        //
        pFrameYUV = avlibInterface::m_av_frame_alloc();
        m_out_buffer = static_cast<uint8_t *>(avlibInterface::m_av_malloc(static_cast<size_t>(avlibInterface::m_avpicture_get_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height))));
        avlibInterface::m_avpicture_fill((AVPicture *)pFrameYUV, m_out_buffer, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);
        qCInfo(dsrApp) << "Video encoder initialized successfully";
    }
    if (m_sysAudioCodecID && m_micAudioCodecID) {
        qCInfo(dsrApp) << "Both system and microphone audio codecs detected, enabling audio mixing";
        m_bMix = true;
        bool initSccess = init_context_amix(m_channels, 0, 0, m_audio_bitrate);
        if (!initSccess) {
            qCCritical(dsrApp) << "Failed to initialize audio mixing context";
            printf("Can not init_context_amix\n");
            return 1;
        }
    }
    ///音频
    if (m_micAudioCodecID != 0) {
        qCInfo(dsrApp) << "Initializing microphone audio encoder with codec ID:" << m_micAudioCodecID;
        printf("FLQQ,mic audio encoder initialize\n\n");
        //output audio encoder initialize
        pCodec_a = avlibInterface::m_avcodec_find_encoder(m_micAudioCodecID);
        if (!pCodec_a) {
            qCCritical(dsrApp) << "Cannot find output audio encoder for microphone, codec ID:" << m_micAudioCodecID;
            printf("Can not find output audio encoder! (没有找到合适的编码器！)\n");
            return false;
        }
        m_pMicCodecContext = avlibInterface::m_avcodec_alloc_context3(pCodec_a);
        m_pMicCodecContext->channels = m_channels;
        //        pCodecCtx_a->channel_layout = av_get_default_channel_layout(m_channels);
        m_pMicCodecContext->channel_layout = static_cast<uint64_t>(m_channels_layout);
        //      pCodecCtx_a->channels = av_get_channel_layout_nb_channels(pAudioStream->codec->channel_layout);
        if (m_pMicCodecContext->channel_layout == 0) {
            qCWarning(dsrApp) << "Microphone channel layout is 0, setting to default stereo layout";
            m_pMicCodecContext->channel_layout = AV_CH_LAYOUT_STEREO;
            m_pMicCodecContext->channels = avlibInterface::m_av_get_channel_layout_nb_channels(m_pMicCodecContext->channel_layout);
        }

        m_pMicCodecContext->sample_rate = m_samplerate;
        m_pMicCodecContext->sample_fmt = pCodec_a->sample_fmts[0];
        m_pMicCodecContext->bit_rate = m_audio_bitrate;
        m_pMicCodecContext->time_base.num = 1;
        m_pMicCodecContext->time_base.den = m_pMicCodecContext->sample_rate;

        if (m_micAudioCodecID == AV_CODEC_ID_AAC) {
            qCDebug(dsrApp) << "Using AAC codec for microphone, enabling experimental compliance";
            /** Allow the use of the experimental AAC encoder */
            m_pMicCodecContext->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
        }

        /* Some formats want stream headers to be separate. */
        if (m_videoFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
            m_pMicCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        if (avlibInterface::m_avcodec_open2(m_pMicCodecContext, pCodec_a, nullptr) < 0) {
            qCCritical(dsrApp) << "Failed to open output microphone audio encoder";
            printf("Failed to open ouput audio encoder! (编码器打开失败！)\n");
            return false;
        }

        //Add a new stream to output,should be called by the user before avformat_write_header() for muxing
        if (!m_bMix) {
            qCDebug(dsrApp) << "Creating separate microphone audio stream (no mixing)";
            m_micAudioStream = avlibInterface::m_avformat_new_stream(m_videoFormatContext, pCodec_a);
            if (nullptr == m_micAudioStream) {
                qCCritical(dsrApp) << "Failed to create microphone audio stream";
                return false;
            }
            m_micAudioStream->time_base.num = 1;
            m_micAudioStream->time_base.den = m_pMicCodecContext->sample_rate;//48000
            m_micAudioStream->codec = m_pMicCodecContext;
        } else {
            qCDebug(dsrApp) << "Using audio mixing, microphone stream will be mixed";
        }


        //Initialize the FIFO buffer to store audio samples to be encoded.

        //        m_fifo = av_audio_fifo_alloc(pCodecCtx_a->sample_fmt, pCodecCtx_a->channels, 30*pCodecCtx_a->frame_size);

        //Initialize the buffer to store converted samples to be encoded.
        m_convertedMicSamples = nullptr;
        /**
        * Allocate as many pointers as there are audio channels.
        * Each pointer will later point to the audio samples of the corresponding
        * channels (although it may be nullptr for interleaved formats).
        */
        if (!(m_convertedMicSamples = static_cast<uint8_t **>(calloc(static_cast<size_t>(m_pMicCodecContext->channels), sizeof(**m_convertedMicSamples))))) {
            printf("Could not allocate converted input sample pointers\n");
            return false;
        }
        //m_convertedMicSamples[0] = nullptr;
    }
    if (m_sysAudioCodecID != 0) {
        qCInfo(dsrApp) << "Initializing system audio encoder with codec ID:" << m_sysAudioCodecID;
        printf("FLQQ,system audio encoder initialize\n\n");
        //output audio encoder initialize
        pCodec_aCard = avlibInterface::m_avcodec_find_encoder(m_sysAudioCodecID);
        if (!pCodec_aCard) {
            qCCritical(dsrApp) << "Cannot find output audio encoder for system audio, codec ID:" << m_sysAudioCodecID;
            printf("Can not find output audio encoder! (没有找到合适的编码器！)\n");
            return false;
        }
        m_pSysCodecContext = avlibInterface::m_avcodec_alloc_context3(pCodec_aCard);
        m_pSysCodecContext->channels = m_channels_card;
        //        pCodecCtx_aCard->channel_layout = av_get_default_channel_layout(m_channels_card);
        m_pSysCodecContext->channel_layout = static_cast<uint64_t>(m_channels_card_layout);
        //                pCodecCtx_aCard->channels = av_get_channel_layout_nb_channels(pAudioStream->codec->channel_layout);
        if (m_pSysCodecContext->channel_layout == 0) {
            qCWarning(dsrApp) << "System audio channel layout is 0, setting to default stereo layout";
            m_pSysCodecContext->channel_layout = AV_CH_LAYOUT_STEREO;
            m_pSysCodecContext->channels = avlibInterface::m_av_get_channel_layout_nb_channels(m_pSysCodecContext->channel_layout);
        }
        m_pSysCodecContext->sample_rate = m_samplerate_card;
        m_pSysCodecContext->sample_fmt = pCodec_aCard->sample_fmts[0];
        m_pSysCodecContext->bit_rate = m_audio_bitrate_card;
        m_pSysCodecContext->time_base.num = 1;
        m_pSysCodecContext->time_base.den = m_pSysCodecContext->sample_rate;

        if (m_sysAudioCodecID == AV_CODEC_ID_AAC) {
            qCDebug(dsrApp) << "Using AAC codec for system audio, enabling experimental compliance";
            /** Allow the use of the experimental AAC encoder */
            m_pSysCodecContext->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
        }

        /* Some formats want stream headers to be separate. */
        if (m_videoFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
            m_pSysCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        if (avlibInterface::m_avcodec_open2(m_pSysCodecContext, pCodec_aCard, nullptr) < 0) {
            qCCritical(dsrApp) << "Failed to open output system audio encoder";
            printf("Failed to open ouput audio encoder! (编码器打开失败！)\n");
            return false;
        }

        //Add a new stream to output,should be called by the user before avformat_write_header() for muxing
        if (!m_bMix) {
            qCDebug(dsrApp) << "Creating separate system audio stream (no mixing)";
            m_sysAudioStream = avlibInterface::m_avformat_new_stream(m_videoFormatContext, pCodec_aCard);
            if (m_sysAudioStream == nullptr) {
                qCCritical(dsrApp) << "Failed to create system audio stream";
                return false;
            }
            m_sysAudioStream->time_base.num = 1;
            m_sysAudioStream->time_base.den = m_pSysCodecContext->sample_rate;//48000
            m_sysAudioStream->codec = m_pSysCodecContext;
        } else {
            qCDebug(dsrApp) << "Using audio mixing, system audio stream will be mixed";
        }
        m_convertedSysSamples = nullptr;
        if (!(m_convertedSysSamples = static_cast<uint8_t **>(calloc(static_cast<size_t>(m_pSysCodecContext->channels), sizeof(**m_convertedSysSamples))))) {
            qCCritical(dsrApp) << "Could not allocate converted system audio sample pointers";
            printf("Could not allocate converted input sample pointers\n");
            return false;
        }
        //m_convertedSysSamples[0] = nullptr;
        qCInfo(dsrApp) << "System audio encoder initialized successfully";
    }
    if (m_bMix) {
        qCInfo(dsrApp) << "Initializing audio mixing filters";
        if (init_filters() != 0) {
            qCCritical(dsrApp) << "Failed to initialize audio mixing filters";
            return false;
        }
        qCDebug(dsrApp) << "Audio mixing filters initialized successfully";
    }

    //Open output URL,set before avformat_write_header() for muxing 打开输出URL，在avformat_write_header()之前设置muxing
    if (avlibInterface::m_avio_open(&m_videoFormatContext->pb, m_path, AVIO_FLAG_READ_WRITE) < 0) {
        qCCritical(dsrApp) << "Failed to open output file:" << m_path;
        printf("Failed to open output file! (输出文件打开失败！)\n");
        return false;
    }
    //Show some Information
    avlibInterface::m_av_dump_format(m_videoFormatContext, 0, m_path, 1);

    //Write File Header 写文件头
    if (avlibInterface::m_avformat_write_header(m_videoFormatContext, nullptr) < 0) {
        qCCritical(dsrApp) << "Failed to write output file header";
        return false;
    }

    //m_vid_framecnt = 0;
    m_nb_samples = 0;
    m_nLastAudioPresentationTime = 0;
    m_nLastAudioMixPresentationTime = 0;
    m_mixCount = 0;
    m_next_vid_time = 0;
    m_next_aud_time = 0;
    //m_first_vid_time1 = m_first_vid_time2 = -1;
    //m_first_aud_time = -1;
    m_start_mix_time = -1;
    //m_isOverWrite = false;
    setIsWriteFrame(true);
    fflush(stdout);
    qCInfo(dsrApp) << "Output stream opened successfully, ready for recording";
    return true;
}

int CAVOutputStream::writeVideoFrame(WaylandIntegration::WaylandIntegrationPrivate::waylandFrame &frame)
{
    qCDebug(dsrApp) << "Writing video frame with dimensions:" << frame._width << "x" << frame._height << "timestamp:" << frame._time;
    if (nullptr == frame._frame || frame._width <= 0 || frame._height <= 0) {
        qCWarning(dsrApp) << "Invalid video frame parameters - frame:" << (void*)frame._frame << "width:" << frame._width << "height:" << frame._height;
        return -1;
    }
    AVFrame *pRgbFrame = avlibInterface::m_av_frame_alloc();
    pRgbFrame->width = frame._width;
    pRgbFrame->height = frame._height;
    pRgbFrame->format = AV_PIX_FMT_RGB32;
    if (0 == avlibInterface::m_av_frame_get_buffer(pRgbFrame, 32)) {
        pRgbFrame->width  = frame._width;
        pRgbFrame->height = frame._height;
        //pRgbFrame->format = AV_PIX_FMT_RGB32;
        pRgbFrame->crop_left   = static_cast<size_t>(m_left);
        pRgbFrame->crop_top    = static_cast<size_t>(m_top);
        pRgbFrame->crop_right  = static_cast<size_t>(m_right);
        pRgbFrame->crop_bottom = static_cast<size_t>(m_bottom);
        pRgbFrame->linesize[0] = frame._stride;
        pRgbFrame->data[0]     = frame._frame;
    } else {
        qCWarning(dsrApp) << "Failed to allocate RGB frame buffer";
    }
    if (nullptr == m_pVideoSwsContext) {
        qCDebug(dsrApp) << "Creating video scaling context";
        AVPixelFormat fmt = AV_PIX_FMT_RGBA;
        if (m_boardVendorType) {
            fmt = AV_PIX_FMT_RGB32;
            qCDebug(dsrApp) << "Using RGB32 format for board vendor type:" << m_boardVendorType;
        }
        m_pVideoSwsContext = avlibInterface::m_sws_getContext(m_width, m_height,
                                                              fmt,
                                                              pCodecCtx->width,
                                                              pCodecCtx->height,
                                                              AV_PIX_FMT_YUV420P,
                                                              SWS_BICUBIC,
                                                              nullptr,
                                                              nullptr,
                                                              nullptr);
        if (m_pVideoSwsContext) {
            qCDebug(dsrApp) << "Video scaling context created successfully";
        } else {
            qCCritical(dsrApp) << "Failed to create video scaling context";
        }
    }
    if (avlibInterface::m_av_frame_apply_cropping(pRgbFrame, AV_FRAME_CROP_UNALIGNED) < 0) {
        qCWarning(dsrApp) << "Failed to apply frame cropping";
        AVERROR(ERANGE);
        return 2;
    }
    avlibInterface::m_sws_scale(m_pVideoSwsContext, pRgbFrame->data, pRgbFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
    pFrameYUV->width  = pRgbFrame->width;//test
    pFrameYUV->height = pRgbFrame->height;
    pFrameYUV->format = AV_PIX_FMT_YUV420P;
    AVPacket packet;
    packet.data = nullptr;
    packet.size = 0;
    avlibInterface::m_av_init_packet(&packet);
    int enc_got_frame = 0;
    avlibInterface::m_avcodec_encode_video2(pCodecCtx, &packet, pFrameYUV, &enc_got_frame);
    pFrameYUV->pts++;
    if (1 == enc_got_frame) {
        packet.stream_index = m_videoStream->index;
        //packet.pts = static_cast<int64_t>(m_videoStream->time_base.den) * frame._time / AV_TIME_BASE;
        m_videoFrameCount++;
        if (m_videoFrameCount == 1) {
            m_fristVideoFramePts = frame._time;
        }
        packet.pts = static_cast<int64_t>(m_videoStream->time_base.den) * (frame._time - m_fristVideoFramePts) / AV_TIME_BASE;
        //qDebug() << "video packet.pts: " << packet.pts;
        packet.dts =  packet.pts;
        int ret = writeFrame(m_videoFormatContext, &packet);
        if (ret < 0) {
            //char tmpErrString[128] = {0};
            //printf("Could not write video frame, error: %s\n", av_make_error_string(tmpErrString, AV_ERROR_MAX_STRING_SIZE, ret));
            avlibInterface::m_av_packet_unref(&packet);
            return ret;
        }
    }
    avlibInterface::m_av_free_packet(&packet);
    avlibInterface::m_av_frame_free(&pRgbFrame);
    fflush(stdout);
    return 0;
}

//input_st -- 输入流的信息
//input_frame -- 输入音频帧的信息
//lTimeStamp -- 时间戳，时间单位为1/1000000
//
int CAVOutputStream::writeMicAudioFrame(AVStream *stream, AVFrame *inputFrame, int64_t lTimeStamp)
{
    if (nullptr == m_micAudioStream)
        return -1;

    const int frameSize = m_pMicCodecContext->frame_size;
    int ret;
    if (nullptr == m_pMicAudioSwrContext) {
        qCDebug(dsrApp) << "Initializing microphone audio resampler context";
        // Initialize the resampler to be able to convert audio sample formats
        m_pMicAudioSwrContext = avlibInterface::m_swr_alloc_set_opts(nullptr,
                                                                     avlibInterface::m_av_get_default_channel_layout(m_pMicCodecContext->channels),
                                                                     m_pMicCodecContext->sample_fmt,
                                                                     m_pMicCodecContext->sample_rate,
                                                                     avlibInterface::m_av_get_default_channel_layout(stream->codec->channels),
                                                                     stream->codec->sample_fmt,
                                                                     stream->codec->sample_rate,
                                                                     0,
                                                                     nullptr);
        /**
        * Perform a sanity check so that the number of converted samples is
        * not greater than the number of samples to be converted.
        * If the sample rates differ, this case has to be handled differently
        */
        assert(m_pMicCodecContext->sample_rate == stream->codec->sample_rate);
        avlibInterface::m_swr_init(m_pMicAudioSwrContext);
        if (nullptr == m_micAudioFifo) {
            qCDebug(dsrApp) << "Allocating microphone audio FIFO with capacity for" << (20 * inputFrame->nb_samples) << "samples";
            m_micAudioFifo = audioFifoAlloc(m_pMicCodecContext->sample_fmt, m_pMicCodecContext->channels, 20 * inputFrame->nb_samples);
        }
        is_fifo_scardinit++;
        qCDebug(dsrApp) << "Microphone audio resampler and FIFO initialized successfully";
    }

    /**
    * Allocate memory for the samples of all channels in one consecutive
    * block for convenience.
    */

    if ((ret = avlibInterface::m_av_samples_alloc(m_convertedMicSamples, nullptr, m_pMicCodecContext->channels, inputFrame->nb_samples, m_pMicCodecContext->sample_fmt, 0)) < 0) {
        qCCritical(dsrApp) << "Could not allocate converted microphone input samples, error code:" << ret;
        printf("Could not allocate converted input samples\n");
        avlibInterface::m_av_freep(&(*m_convertedMicSamples)[0]);
        free(*m_convertedMicSamples);
        freeSwrContext(m_pMicAudioSwrContext);
        return ret;
    }

    /**
    * Convert the input samples to the desired output sample format.
    * This requires a temporary storage provided by converted_input_samples.
    */
    /** Convert the samples using the resampler. */
    //extended_data -> m_convertedMicSamples
    if ((ret = avlibInterface::m_swr_convert(m_pMicAudioSwrContext, m_convertedMicSamples, inputFrame->nb_samples, const_cast<const uint8_t **>(inputFrame->extended_data), inputFrame->nb_samples)) < 0) {
        qCCritical(dsrApp) << "Could not convert microphone input samples, error code:" << ret;
        printf("Could not convert input samples\n");
        freeSwrContext(m_pMicAudioSwrContext);
        return ret;
    }
    freeSwrContext(m_pMicAudioSwrContext);
    AVRational rational;
    int audioSize = audioFifoSize(m_micAudioFifo);
    //因为Fifo里有之前未读完的数据，所以从Fifo队列里面取出的第一个音频包的时间戳等于当前时间减掉缓冲部分的时长
    int64_t timeshift = static_cast<int64_t>(audioSize * AV_TIME_BASE) / static_cast<int64_t>(stream->codec->sample_rate);
    qCDebug(dsrApp) << "Microphone audio FIFO size before adding:" << audioSize << "samples, timeshift:" << timeshift;
    /** Add the converted input samples to the FIFO buffer for later processing. */

    /**
    * Make the FIFO as large as it needs to be to hold both,
    * the old and the new samples.
    */
    if ((ret = audioFifoRealloc(m_micAudioFifo, audioFifoSize(m_micAudioFifo) + inputFrame->nb_samples)) < 0) {
        qCCritical(dsrApp) << "Could not reallocate microphone audio FIFO, error code:" << ret;
        printf("Could not reallocate FIFO\n");
        return ret;
    }

    /** Store the new samples in the FIFO buffer. */
    //write m_convertedMicSamples
    //static_cast、dynamic_cast、const_cast、reinterpret_cast
    if (audioWrite(m_micAudioFifo, reinterpret_cast<void **>(m_convertedMicSamples), inputFrame->nb_samples) < inputFrame->nb_samples) {
        qCCritical(dsrApp) << "Could not write microphone audio data to FIFO";
        printf("Could not write data to FIFO\n");
        return AVERROR_EXIT;
    }
    qCDebug(dsrApp) << "Successfully added" << inputFrame->nb_samples << "samples to microphone audio FIFO";
    int64_t timeinc = static_cast<int64_t>(m_pMicCodecContext->frame_size * AV_TIME_BASE / stream->codec->sample_rate);
    //当前帧的时间戳不能小于上一帧的值
    if (lTimeStamp - timeshift > m_nLastAudioPresentationTime) {
        m_nLastAudioPresentationTime = lTimeStamp - timeshift;
    }
    /**
    * Take one frame worth of audio samples from the FIFO buffer,
    * encode it and write it to the output file.
    */
    while (audioFifoSize(m_micAudioFifo) >= frameSize) {
        /** Temporary storage of the output samples of the frame written to the file. */
        AVFrame *outputFrame = avlibInterface::m_av_frame_alloc();
        if (!outputFrame) {
            qCCritical(dsrApp) << "Could not allocate microphone output frame";
            return AVERROR(ENOMEM);
        }
        /**
        * Use the maximum number of possible samples per frame.
        * If there is less than the maximum possible frame size in the FIFO
        * buffer use this number. Otherwise, use the maximum possible frame size
        */
        const int frame_size = FFMIN(audioFifoSize(m_micAudioFifo), m_pMicCodecContext->frame_size);
        /** Initialize temporary storage for one output frame. */
        /**
        * Set the frame's parame-ters, especially its size and format.
        * av_frame_get_buffer needs this to allocate memory for the
        * audio samples of the frame.
        * Default channel layouts based on the number of channels
        * are assumed for simplicity.
        */
        outputFrame->nb_samples = frame_size;
        outputFrame->channel_layout = m_pMicCodecContext->channel_layout;
        outputFrame->format = m_pMicCodecContext->sample_fmt;
        outputFrame->sample_rate = m_pMicCodecContext->sample_rate;
        outputFrame->pts = avlibInterface::m_av_rescale_q(m_nLastAudioPresentationTime, rational, m_micAudioStream->time_base);
        /**
        * Allocate the samples of the created frame. This call will make
        * sure that the audio frame can hold as many samples as specified.
        */
        if ((ret = avlibInterface::m_av_frame_get_buffer(outputFrame, 0)) < 0) {
            qCCritical(dsrApp) << "Could not allocate microphone output frame samples, error code:" << ret;
            printf("Could not allocate output frame samples\n");
            avlibInterface::m_av_frame_free(&outputFrame);
            return ret;
        }

        /**
        * Read as many samples from the FIFO buffer as required to fill the frame.
        * The samples are stored in the frame temporarily.
        */
        //read -> outputFrame->data
        if (audioRead(m_micAudioFifo, (void **)outputFrame->data, frame_size) < frame_size) {
            qCCritical(dsrApp) << "Could not read microphone audio data from FIFO";
            printf("Could not read data from FIFO\n");
            avlibInterface::m_av_frame_free(&outputFrame);
            return AVERROR_EXIT;
        }
        //int ret = 0;
        /** Encode one frame worth of audio samples. */
        /** Packet used for temporary storage. */
        AVPacket outputPacket;
        avlibInterface::m_av_init_packet(&outputPacket);
        outputPacket.data = nullptr;
        outputPacket.size = 0;
        int enc_got_frame_a = 0;
        /**
            * Encode the audio frame and store it in the temporary packet.
            * The output audio stream encoder is used to do this.
            */
        //outputFrame -> outputPacket
        if ((ret = avlibInterface::m_avcodec_encode_audio2(m_pMicCodecContext, &outputPacket, outputFrame, &enc_got_frame_a)) < 0) {
            qCCritical(dsrApp) << "Could not encode microphone audio frame, error code:" << ret;
            printf("Could not encode frame\n");
            avlibInterface::m_av_packet_unref(&outputPacket);
            return ret;
        }
        /** Write one audio frame from the temporary packet to the output file. */
        if (enc_got_frame_a) {
            outputPacket.stream_index = m_micAudioStream->index;
            printf("output_packet.stream_index1  audio_st =%d\n", outputPacket.stream_index);
            //outputPacket.pts = avlibInterface::m_av_rescale_q(m_nLastAudioPresentationTime, rational, m_micAudioStream->time_base);
            if (m_videoType == videoType::MKV) {
                //显示时间戳，应大于或等于解码时间戳
                outputPacket.pts = m_singleCount * m_pMicCodecContext->frame_size * 1000 / m_pMicCodecContext->sample_rate;
            } else {
                //显示时间戳，应大于或等于解码时间戳
                outputPacket.pts = m_singleCount * m_pMicCodecContext->frame_size;
            }
            outputPacket.dts = outputPacket.pts;
            //qDebug() << m_singleCount << " mic audio outputPacket.pts: " << outputPacket.pts;
            m_singleCount++;
            //write -> outputPacket
            if ((ret = writeFrame(m_videoFormatContext, &outputPacket)) < 0) {
                qCWarning(dsrApp) << "Could not write microphone audio frame, error code:" << ret;
                //char tmpErrString[128] = {0};
                //printf("Could not write audio frame, error: %s\n", av_make_error_string(tmpErrString, AV_ERROR_MAX_STRING_SIZE, ret));
                avlibInterface::m_av_packet_unref(&outputPacket);
                return ret;
            }
            qCDebug(dsrApp) << "Successfully encoded and wrote microphone audio frame, count:" << m_singleCount;
            avlibInterface::m_av_packet_unref(&outputPacket);
        }
        m_nb_samples += outputFrame->nb_samples;
        m_nLastAudioPresentationTime += timeinc;
        avlibInterface::m_av_frame_free(&outputFrame);
    }// -- while end
    return 0;
}

int CAVOutputStream::writeMicToMixAudioFrame(AVStream *stream, AVFrame *inputFrame, int64_t lTimeStamp)
{
    qCDebug(dsrApp) << "Writing microphone audio frame to mix buffer, timestamp:" << lTimeStamp;
    Q_UNUSED(lTimeStamp)
    int ret;
    if (nullptr == m_pMicAudioSwrContext) {
        qCDebug(dsrApp) << "Initializing microphone audio resampler for mixing";
        // Initialize the resampler to be able to convert audio sample formats
        m_pMicAudioSwrContext = avlibInterface::m_swr_alloc_set_opts(nullptr,
                                                                     avlibInterface::m_av_get_default_channel_layout(m_pMicCodecContext->channels),
                                                                     m_pMicCodecContext->sample_fmt,
                                                                     m_pMicCodecContext->sample_rate,
                                                                     avlibInterface::m_av_get_default_channel_layout(stream->codec->channels),
                                                                     stream->codec->sample_fmt,
                                                                     stream->codec->sample_rate,
                                                                     0,
                                                                     nullptr);
        assert(m_pMicCodecContext->sample_rate == stream->codec->sample_rate);
        avlibInterface::m_swr_init(m_pMicAudioSwrContext);
        if (nullptr == m_micAudioFifo) {
            qCDebug(dsrApp) << "Allocating microphone mix audio FIFO with capacity for" << (20 * inputFrame->nb_samples) << "samples";
            m_micAudioFifo = audioFifoAlloc(m_pMicCodecContext->sample_fmt, m_pMicCodecContext->channels, 20 * inputFrame->nb_samples);
        }
        is_fifo_scardinit++;
        qCDebug(dsrApp) << "Microphone mix audio resampler initialized successfully";
    }
    if ((ret = avlibInterface::m_av_samples_alloc(m_convertedMicSamples, nullptr, m_pMicCodecContext->channels, inputFrame->nb_samples, m_pMicCodecContext->sample_fmt, 0)) < 0) {
        qCCritical(dsrApp) << "Could not allocate converted microphone input samples for mixing, error code:" << ret;
        printf("Could not allocate converted input samples\n");
        avlibInterface::m_av_freep(&(*m_convertedMicSamples)[0]);
        free(*m_convertedMicSamples);
        freeSwrContext(m_pMicAudioSwrContext);
        return ret;
    }
    if ((ret = avlibInterface::m_swr_convert(m_pMicAudioSwrContext, m_convertedMicSamples, inputFrame->nb_samples, const_cast<const uint8_t **>(inputFrame->extended_data), inputFrame->nb_samples)) < 0) {
        qCCritical(dsrApp) << "Could not convert microphone input samples for mixing, error code:" << ret;
        printf("Could not convert input samples\n");
        freeSwrContext(m_pMicAudioSwrContext);
        return ret;
    }
    freeSwrContext(m_pMicAudioSwrContext);
    int fifoSpace = audioFifoSpace(m_micAudioFifo);
    if (fifoSpace < inputFrame->nb_samples) {
        fifoSpace = avlibInterface::m_av_audio_fifo_size(m_micAudioFifo);
        if (INT_MAX / 2 - fifoSpace < inputFrame->nb_samples) {
            qCWarning(dsrApp) << "Microphone audio buffer space asymmetry detected";
            qDebug() << "麦克风音频缓存空间不对称!";
            return AVERROR(EINVAL);
        }
        if ((ret = audioFifoRealloc(m_micAudioFifo, audioFifoSize(m_micAudioFifo) + inputFrame->nb_samples)) < 0) {
            qCCritical(dsrApp) << "Could not reallocate microphone mix FIFO, error code:" << ret;
            printf("Could not reallocate FIFO\n");
            return ret;
        }
        fifoSpace = audioFifoSpace(m_micAudioFifo);
        qCDebug(dsrApp) << "Reallocated microphone mix FIFO, new space:" << fifoSpace;
    }
    int checkTime = 0;
    while (fifoSpace < inputFrame->nb_samples
            && isWriteFrame()
            && checkTime <= 1000) {
        fifoSpace = audioFifoSpace(m_micAudioFifo);
        usleep(10 * 1000);
        checkTime ++;
        //printf("_fifo_spk full  m_fifo!\n");
    }
    if (checkTime > 0) {
        qCDebug(dsrApp) << "Waited" << checkTime << "cycles for microphone FIFO space";
    }

    if (fifoSpace >= inputFrame->nb_samples) {
        //write m_convertedMicSamples
        if (audioWrite(m_micAudioFifo, (void **)m_convertedMicSamples, inputFrame->nb_samples) < inputFrame->nb_samples) {
            qCCritical(dsrApp) << "Could not write microphone data to mix FIFO";
            printf("Could not write data to FIFO\n");
            return AVERROR_EXIT;
        }
        qCDebug(dsrApp) << "Successfully wrote" << inputFrame->nb_samples << "microphone samples to mix FIFO";
        printf("_fifo_spk write secceffull  m_fifo!\n");
    } else {
        qCWarning(dsrApp) << "Insufficient microphone FIFO space after waiting, dropping frame";
    }
    return 0;
}
int CAVOutputStream::write_filter_audio_frame(AVStream *&outst, AVCodecContext *&codecCtx_audio, AVFrame *&output_frame)
{
    qCDebug(dsrApp) << "Writing filtered audio frame to output stream";
    int ret = 0;
    /** Encode one frame worth of audio samples. */
    /** Packet used for temporary storage. */
    AVPacket output_packet;
    avlibInterface::m_av_init_packet(&output_packet);
    output_packet.data = nullptr;
    output_packet.size = 0;

    int enc_got_frame_a = 0;

    /**
     * Encode the audio frame and store it in the temporary packet.
     * The output audio stream encoder is used to do this.
     */
    if ((ret = avlibInterface::m_avcodec_encode_audio2(codecCtx_audio, &output_packet, output_frame, &enc_got_frame_a)) < 0) {
        qCCritical(dsrApp) << "Could not encode filtered audio frame, error code:" << ret;
        printf("Could not encode frame\n");
        avlibInterface::m_av_packet_unref(&output_packet);
        return ret;
    }




    /** Write one audio frame from the temporary packet to the output file. */
    if (enc_got_frame_a) {
        //output_packet.flags |= AV_PKT_FLAG_KEY;
        output_packet.stream_index = outst->index;
        printf("output_packet.stream_index2  audio_st =%d\n", output_packet.stream_index);
#if 0
        AVRational r_framerate1 = { input_st->codec->sample_rate, 1 };// { 44100, 1};
        //int64_t_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1));  //内部时间戳
        int64_t_t calc_pts = (double)m_nb_samples * (AV_TIME_BASE) * (1 / av_q2d(r_framerate1));

        output_packet.pts = av_rescale_q(calc_pts, time_base_q, audio_st->time_base);
        //output_packet.dts = output_packet.pts;
        //output_packet.duration = output_frame->nb_samples;
#else
        //         output_packet.pts = av_rescale_q(lastAudioPresentationTime, time_base_q, outst->time_base);

#endif


        if ((ret = writeFrame(m_videoFormatContext, &output_packet)) < 0) {
            qCWarning(dsrApp) << "Could not write filtered audio frame, error code:" << ret;
            //char tmpErrString[128] = {0};
            //printf("Could not write audio frame, error: %s\n", av_make_error_string(tmpErrString, AV_ERROR_MAX_STRING_SIZE, ret));
            avlibInterface::m_av_packet_unref(&output_packet);
            return ret;
        }
        qCDebug(dsrApp) << "Successfully wrote filtered audio frame to output";
        avlibInterface::m_av_packet_unref(&output_packet);
    }//if (enc_got_frame_a)


    //     m_nb_samples += output_frame->nb_samples;

    avlibInterface::m_av_frame_unref(output_frame);
    //     av_frame_free(&output_frame);
    return ret;
}

/**
 * @brief 音频缓冲区是否还有数据
 * @return
 */
bool CAVOutputStream::isNotAudioFifoEmty()
{
    bool flag  = false;
    if ((m_micAudioFifo != nullptr && audioFifoSize(m_micAudioFifo) >= m_pMicCodecContext->frame_size) ||
            (m_sysAudioFifo != nullptr && audioFifoSize(m_sysAudioFifo) >= m_pSysCodecContext->frame_size)) {
        flag = true;
    }
    //qDebug () << "isNotAudioFifoEmty() : " << flag << audioFifoSize(m_sysAudioFifo) << audioFifoSpace(m_sysAudioFifo) << " , " << audioFifoSize(m_micAudioFifo) << audioFifoSpace(m_micAudioFifo);
    qCDebug(dsrApp) << "Audio FIFO empty check:" << flag << "sys size:" << (m_sysAudioFifo ? audioFifoSize(m_sysAudioFifo) : 0) << "mic size:" << (m_micAudioFifo ? audioFifoSize(m_micAudioFifo) : 0);
    return flag;
}

void CAVOutputStream::writeMixAudio()
{
    qCDebug(dsrApp) << "Starting audio mixing process";
    if (is_fifo_scardinit < 2 || nullptr == m_sysAudioFifo || nullptr == m_micAudioFifo) {
        qCWarning(dsrApp) << "Audio mixing prerequisites not met - fifo_scardinit:" << is_fifo_scardinit << "sys FIFO:" << (void*)m_sysAudioFifo << "mic FIFO:" << (void*)m_micAudioFifo;
        return;
    }
    AVFrame *pFrame_sys = avlibInterface::m_av_frame_alloc();
    AVFrame *pFrame_mic = avlibInterface::m_av_frame_alloc();
    int sysFifosize = audioFifoSize(m_sysAudioFifo);
    int micFifoSize = audioFifoSize(m_micAudioFifo);
    int minMicFrameSize = m_pMicCodecContext->frame_size;
    int minSysFrameSize = m_pSysCodecContext->frame_size;
    qCDebug(dsrApp) << "Audio mixing - sys FIFO size:" << sysFifosize << "mic FIFO size:" << micFifoSize << "required sys frame:" << minSysFrameSize << "required mic frame:" << minMicFrameSize;
    if (micFifoSize >= minMicFrameSize && sysFifosize >= minSysFrameSize) {
        qCDebug(dsrApp) << "Sufficient audio data available for mixing, proceeding";
        int ret;
        tmpFifoFailed = 0;
        AVRational time_base_q;
        // 样本数量 1152
        pFrame_sys->nb_samples = minSysFrameSize;
        //通道布局
        pFrame_sys->channel_layout = m_pSysCodecContext->channel_layout;
        //样本格式
        pFrame_sys->format = m_pSysCodecContext->sample_fmt;
        //采样率
        pFrame_sys->sample_rate = m_pSysCodecContext->sample_rate;
        avlibInterface::m_av_frame_get_buffer(pFrame_sys, 0);
        pFrame_mic->nb_samples = minMicFrameSize;
        pFrame_mic->channel_layout = m_pMicCodecContext->channel_layout;
        pFrame_mic->format = m_pMicCodecContext->sample_fmt;
        pFrame_mic->sample_rate = m_pMicCodecContext->sample_rate;
        avlibInterface::m_av_frame_get_buffer(pFrame_mic, 0);
        //从fifo缓存区中将数据读取到pFrame_sys->datap
        audioRead(m_sysAudioFifo, reinterpret_cast<void **>(pFrame_sys->data), minSysFrameSize);
        //从fifo缓存区中将数据读取到pFrame_mic->datap
        audioRead(m_micAudioFifo, reinterpret_cast<void **>(pFrame_mic->data), minMicFrameSize);
        qCDebug(dsrApp) << "Read audio frames from FIFO - sys:" << minSysFrameSize << "samples, mic:" << minMicFrameSize << "samples";
        int nFifoSamples = pFrame_sys->nb_samples;
        if (m_start_mix_time == -1) {
            //printf("First Audio timestamp: %ld \n", av_gettime());
            m_start_mix_time = avlibInterface::m_av_gettime();
            qCInfo(dsrApp) << "Starting audio mixing timer at:" << m_start_mix_time;
        }
        int64_t lTimeStamp = avlibInterface::m_av_gettime() - m_start_mix_time;
        int64_t timeshift = (int64_t)nFifoSamples * AV_TIME_BASE / (int64_t)(audio_amix_st->codec->sample_rate);
        if (lTimeStamp - timeshift > m_nLastAudioMixPresentationTime) {
            m_nLastAudioMixPresentationTime = lTimeStamp - timeshift;
        }
        pFrame_sys->pts = avlibInterface::m_av_rescale_q(m_nLastAudioMixPresentationTime, time_base_q, audio_amix_st->time_base);
        pFrame_mic->pts = pFrame_sys->pts;
        int64_t timeinc = (int64_t)pCodecCtx_amix->frame_size * AV_TIME_BASE / (int64_t)(audio_amix_st->codec->sample_rate);
        m_nLastAudioMixPresentationTime += timeinc;
        qCDebug(dsrApp) << "Audio mixing timestamps - pts:" << pFrame_sys->pts << "timeshift:" << timeshift << "timeinc:" << timeinc;
        ret = avlibInterface::m_av_buffersrc_add_frame_flags(buffersrc_ctx1, pFrame_mic, 0);
        if (ret < 0) {
            qCCritical(dsrApp) << "Failed to add microphone frame to buffer source, error code:" << ret;
            printf("Mixer: failed to call av_buffersrc_add_frame (speaker)\n");
            return;
        }
        ret = avlibInterface::m_av_buffersrc_add_frame_flags(buffersrc_ctx2, pFrame_sys, 0);
        if (ret < 0) {
            printf("Mixer: failed to call av_buffersrc_add_frame (microphone)\n");
            return;
        }
        while (isWriteFrame() || isNotAudioFifoEmty()) {
            //两种音频混合后输出的帧
            AVFrame *pFrame_out = avlibInterface::m_av_frame_alloc();
            //AVERROR(EAGAIN) 返回这个表示还没转换完成既 不存在帧，则返回AVERROR（EAGAIN）
            //从缓冲源中获取一个带有过滤数据的帧并将其放入pFrame_out中
            ret = avlibInterface::m_av_buffersink_get_frame(buffersink_ctx, pFrame_out);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                //printf("%d %d \n", AVERROR(EAGAIN), AVERROR_EOF);
            }
            if (ret < 0) {
                printf("Mixer: failed to call av_buffersink_get_frame_flags ret : %d \n", ret);
                break;
            }
            fflush(stdout);
            if (pFrame_out->data[0] != nullptr) {
                AVPacket packet_out;
                avlibInterface::m_av_init_packet(&packet_out);
                packet_out.data = nullptr;
                packet_out.size = 0;
                int got_packet_ptr;
                ret = avlibInterface::m_avcodec_encode_audio2(pCodecCtx_amix, &packet_out, pFrame_out, &got_packet_ptr);
                if (ret < 0) {
                    printf("Mixer: failed to call avcodec_decode_audio4\n");
                    break;
                }
                if (got_packet_ptr) {
                    packet_out.stream_index = audio_amix_st->index;

                    if (m_videoType == videoType::MKV) {
                        //显示时间戳，应大于或等于解码时间戳
                        packet_out.pts = m_mixCount * pCodecCtx_amix->frame_size * 1000 / pFrame_out->sample_rate;
                    } else {
                        //显示时间戳，应大于或等于解码时间戳
                        packet_out.pts = m_mixCount * pCodecCtx_amix->frame_size;
                    }
                    qDebug() << m_mixCount << " mix audio packet_out.pts: " << packet_out.pts ;

                    packet_out.dts = packet_out.pts;
                    packet_out.duration = pCodecCtx_amix->frame_size;
                    packet_out.duration = avlibInterface::m_av_rescale_q_rnd(packet_out.duration,
                                                                             pCodecCtx_amix->time_base,
                                                                             audio_amix_st->time_base,
                                                                             (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));

                    m_mixCount++;
                    ret = writeFrame(m_videoFormatContext, &packet_out);
                    if (ret < 0) {
                        printf("Mixer: failed to call av_interleaved_write_frame\n");
                    }
                    printf("-------Mixer: write frame to file got_packet_ptr =%d\n", got_packet_ptr);
                }
                avlibInterface::m_av_free_packet(&packet_out);
            }
            avlibInterface::m_av_frame_free(&pFrame_out);
        }
    } else {
        tmpFifoFailed++;
        usleep(20 * 1000);
        if (tmpFifoFailed > 300) {
            usleep(30 * 1000);
            return;
        }
    }
    avlibInterface::m_av_frame_free(&pFrame_sys);
    avlibInterface::m_av_frame_free(&pFrame_mic);
}

int  CAVOutputStream::writeSysAudioFrame(AVStream *stream, AVFrame *inputFrame, int64_t lTimeStamp)
{
    if (nullptr == m_sysAudioStream)
        return -1;
    const int output_frame_size = m_pSysCodecContext->frame_size;
    int ret;
    //因为Fifo里有之前未读完的数据，所以从Fifo队列里面取出的第一个音频包的时间戳等于当前时间减掉缓冲部分的时长
    if (nullptr == m_pSysAudioSwrContext) {
        // Initialize the resampler to be able to convert audio sample formats
        m_pSysAudioSwrContext = avlibInterface::m_swr_alloc_set_opts(nullptr,
                                                                     avlibInterface::m_av_get_default_channel_layout(m_pSysCodecContext->channels),
                                                                     m_pSysCodecContext->sample_fmt,
                                                                     m_pSysCodecContext->sample_rate,
                                                                     avlibInterface::m_av_get_default_channel_layout(stream->codec->channels),
                                                                     stream->codec->sample_fmt,
                                                                     stream->codec->sample_rate,
                                                                     0,
                                                                     nullptr);

        /**
        * Perform a sanity check so that the number of converted samples is
        * not greater than the number of samples to be converted.
        * If the sample rates differ, this case has to be handled differently
        */
        assert(m_pSysCodecContext->sample_rate == stream->codec->sample_rate);
        avlibInterface::m_swr_init(m_pSysAudioSwrContext);
        if (nullptr == m_sysAudioFifo) {
            if (m_videoType == videoType::MKV) {
                m_sysAudioFifo = audioFifoAlloc(m_pSysCodecContext->sample_fmt, m_pSysCodecContext->channels,  inputFrame->nb_samples);
                m_initFifoSpace = audioFifoSpace(m_sysAudioFifo);
            } else {
                m_sysAudioFifo = audioFifoAlloc(m_pSysCodecContext->sample_fmt, m_pSysCodecContext->channels,  40 * inputFrame->nb_samples);
            }
            //qDebug() << "init m_sysAudioFifo audioFifoSize: " << audioFifoSize(m_sysAudioFifo);
            //qDebug() << "init m_sysAudioFifo audioFifoSpace: " << audioFifoSpace(m_sysAudioFifo);
        }
        is_fifo_scardinit ++;
    }
    /**
    * Allocate memory for the samples of all channels in one consecutive
    * block for convenience.为方便起见，在一个连续块中为所有通道的样本分配内存。
    */

    if ((ret = avlibInterface::m_av_samples_alloc(m_convertedSysSamples, nullptr, m_pSysCodecContext->channels, inputFrame->nb_samples, m_pSysCodecContext->sample_fmt, 0)) < 0) {
        avlibInterface::m_av_freep(&(*m_convertedSysSamples)[0]);
        free(*m_convertedSysSamples);
        freeSwrContext(m_pSysAudioSwrContext);
        return ret;
    }

    /**
    * Convert the input samples to the desired output sample format.将输入示例转换为所需的输出示例格式。
    * This requires a temporary storage provided by converted_input_samples.这需要converted_input_samples提供的临时存储。
    */
    /** Convert the samples using the resampler. */
    if ((ret = avlibInterface::m_swr_convert(m_pSysAudioSwrContext, m_convertedSysSamples, inputFrame->nb_samples, const_cast<const uint8_t **>(inputFrame->extended_data), inputFrame->nb_samples)) < 0) {
        freeSwrContext(m_pSysAudioSwrContext);
        return ret;
    }
    freeSwrContext(m_pSysAudioSwrContext);
    AVRational rational = {1, AV_TIME_BASE };
    int audioSize = audioFifoSize(m_sysAudioFifo);
    int64_t timeshift = (int64_t)audioSize * AV_TIME_BASE / (int64_t)(stream->codec->sample_rate);
    /** Add the converted input samples to the FIFO buffer for later processing. 将转换后的输入样本添加到FIFO缓冲器中以供后续处理。*/
    /**
    * Make the FIFO as large as it needs to be to hold both,
    * the old and the new samples.使FIFO尽可能大，因为它需要容纳旧的和新的样品。
    */
    //qDebug() << "m_videoType: " << m_videoType;
    if (m_videoType == videoType::MKV) {
        //对比某个时间段缓冲区大小是否比初始化缓冲区大小大，大：需要减小当前的缓冲区，小不做操作
        if (m_initFifoSpace < audioFifoSpace(m_sysAudioFifo)) {
            if ((ret = audioFifoRealloc(m_sysAudioFifo, m_initFifoSpace + inputFrame->nb_samples)) < 0) {
                printf("Could not reallocate FIFO\n");
                return ret;
            }
        }
        //对比某个时间段缓冲区是否有可用空间，没有直接丢帧处理
        if (audioFifoSpace(m_sysAudioFifo) < inputFrame->nb_samples) {
            return 0;
        }
    } else {
        if ((ret = audioFifoRealloc(m_sysAudioFifo, audioFifoSize(m_sysAudioFifo) + inputFrame->nb_samples)) < 0) {
            printf("Could not reallocate FIFO\n");
            return ret;
        }
    }
    /** Store the new samples in the FIFO buffer. 将新样品存储在FIFO缓冲区中。*/
    if (audioWrite(m_sysAudioFifo, reinterpret_cast<void **>(m_convertedSysSamples), inputFrame->nb_samples) < inputFrame->nb_samples) {
        printf("Could not write data to FIFO\n");
        return AVERROR_EXIT;
    }

    int64_t timeinc = static_cast<int64_t>(m_pSysCodecContext->frame_size * AV_TIME_BASE / stream->codec->sample_rate);
    //当前帧的时间戳不能小于上一帧的值
    if (lTimeStamp - timeshift > m_nLastAudioCardPresentationTime) {
        m_nLastAudioCardPresentationTime = lTimeStamp - timeshift;
    }
    /**
        * Take one frame worth of audio samples from the FIFO buffer,
        * encode it and write it to the output file.从FIFO缓冲区取一帧值的音频样本，将其编码并写入输出文件。
        */
    while (audioFifoSize(m_sysAudioFifo) >= output_frame_size) {
        /** Temporary storage of the output samples of the frame written to the file. */
        AVFrame *output_frame = avlibInterface::m_av_frame_alloc();
        if (!output_frame) {
            return AVERROR(ENOMEM);
        }
        /**
            * Use the maximum number of possible samples per frame.
            * If there is less than the maximum possible frame size in the FIFO
            * buffer use this number. Otherwise, use the maximum possible frame size
            */
        const int frame_size = FFMIN(audioFifoSize(m_sysAudioFifo), m_pSysCodecContext->frame_size);
        /** Initialize temporary storage for one output frame. */
        /**
            * Set the frame's parameters, especially its size and format.
            * av_frame_get_buffer needs this to allocate memory for the
            * audio samples of the frame.
            * Default channel layouts based on the number of channels
            * are assumed for simplicity.
            */
        output_frame->nb_samples = frame_size;
        output_frame->channel_layout = m_pSysCodecContext->channel_layout;
        output_frame->format = m_pSysCodecContext->sample_fmt;
        output_frame->sample_rate = m_pSysCodecContext->sample_rate;
        output_frame->pts = avlibInterface::m_av_rescale_q(m_nLastAudioCardPresentationTime, rational, m_sysAudioStream->time_base);
        /**
            * Allocate the samples of the created frame. This call will make
            * sure that the audio frame can hold as many samples as specified.
            */
        if ((ret = avlibInterface::m_av_frame_get_buffer(output_frame, 0)) < 0) {
            printf("Could not allocate output frame samples\n");
            avlibInterface::m_av_frame_free(&output_frame);
            return ret;
        }

        /**
            * Read as many samples from the FIFO buffer as required to fill the frame.
            * The samples are stored in the frame temporarily.
            */
        if (audioRead(m_sysAudioFifo, (void **)output_frame->data, frame_size) < frame_size) {
            printf("Could not read data from FIFO\n");
            return AVERROR_EXIT;
        }
        /** Encode one frame worth of audio samples. */
        /** Packet used for temporary storage. */
        AVPacket outputPacket;
        avlibInterface::m_av_init_packet(&outputPacket);
        outputPacket.data = nullptr;
        outputPacket.size = 0;
        int enc_got_frame_a = 0;
        /**
                * Encode the audio frame and store it in the temporary packet.
                * The output audio stream encoder is used to do this.
                */
        if ((ret = avlibInterface::m_avcodec_encode_audio2(m_pSysCodecContext, &outputPacket, output_frame, &enc_got_frame_a)) < 0) {
            printf("Could not encode frame\n");
            avlibInterface::m_av_packet_unref(&outputPacket);
            return ret;
        }
        /** Write one audio frame from the temporary packet to the output file. */
        if (enc_got_frame_a) {
            outputPacket.stream_index = m_sysAudioStream->index;
            //            outputPacket.pts = m_singleCount * m_pSysCodecContext->frame_size * 1000 / m_pSysCodecContext->sample_rate;
            if (m_videoType == videoType::MKV) {
                //显示时间戳，应大于或等于解码时间戳
                outputPacket.pts = m_singleCount * m_pSysCodecContext->frame_size * 1000 / m_pSysCodecContext->sample_rate;
            } else {
                //显示时间戳，应大于或等于解码时间戳
                outputPacket.pts = m_singleCount * m_pSysCodecContext->frame_size;
                outputPacket.dts = outputPacket.pts;
            }
            outputPacket.duration = m_pSysCodecContext->frame_size;
            //qDebug() << m_singleCount << " sys audio outputPacket.pts: " << outputPacket.pts;
            m_singleCount++;
            if ((ret = writeFrame(m_videoFormatContext, &outputPacket)) < 0) {
                //char tmpErrString[128] = {0};
                //printf("Could not write audio frame, error: %s\n", av_make_error_string(tmpErrString, AV_ERROR_MAX_STRING_SIZE, ret));
                avlibInterface::m_av_packet_unref(&outputPacket);
                return ret;
            }
            avlibInterface::m_av_packet_unref(&outputPacket);
        }//if (enc_got_frame_a)
        m_nb_samples += output_frame->nb_samples;
        m_nLastAudioCardPresentationTime += timeinc;
        avlibInterface::m_av_frame_free(&output_frame);
    }// -- while end
    return 0;
}

int CAVOutputStream::writeSysToMixAudioFrame(AVStream *stream, AVFrame *inputFrame, int64_t lTimeStamp)
{
    qCDebug(dsrApp) << "Writing system audio frame to mix buffer, timestamp:" << lTimeStamp;
    Q_UNUSED(lTimeStamp)
    int ret;
    if (nullptr == m_pSysAudioSwrContext) {
        qCDebug(dsrApp) << "Initializing system audio resampler for mixing";
        m_pSysAudioSwrContext = avlibInterface::m_swr_alloc_set_opts(nullptr,
                                                                     avlibInterface::m_av_get_default_channel_layout(m_pSysCodecContext->channels),
                                                                     m_pSysCodecContext->sample_fmt,
                                                                     m_pSysCodecContext->sample_rate,
                                                                     avlibInterface::m_av_get_default_channel_layout(stream->codec->channels),
                                                                     stream->codec->sample_fmt,
                                                                     stream->codec->sample_rate,
                                                                     0,
                                                                     nullptr);
        assert(m_pSysCodecContext->sample_rate == stream->codec->sample_rate);
        avlibInterface::m_swr_init(m_pSysAudioSwrContext);
        if (nullptr == m_sysAudioFifo) {
            qCDebug(dsrApp) << "Allocating system mix audio FIFO with capacity for" << (20 * inputFrame->nb_samples) << "samples";
            //根据采样格式，通道数，样本个数 划分系统音频fifo缓存空间的大小
            m_sysAudioFifo = audioFifoAlloc(m_pSysCodecContext->sample_fmt, m_pSysCodecContext->channels, 20 * inputFrame->nb_samples);
        }
        is_fifo_scardinit ++;
        qCDebug(dsrApp) << "System mix audio resampler initialized successfully";
    }

    //为nb_samples样本分配一个样本缓冲区，并相应地填充数据指针和行大小。已分配的样本缓冲区可以通过使用av_freep(&audio_data[0])来释放。已分配的数据将被初始化为静默。
    if ((ret = avlibInterface::m_av_samples_alloc(m_convertedSysSamples, nullptr, m_pSysCodecContext->channels, inputFrame->nb_samples, m_pSysCodecContext->sample_fmt, 0)) < 0) {
        qCCritical(dsrApp) << "Could not allocate converted system audio input samples for mixing, error code:" << ret;
        avlibInterface::m_av_freep(&(*m_convertedSysSamples)[0]);
        free(*m_convertedSysSamples);
        freeSwrContext(m_pSysAudioSwrContext);
        return ret;
    }
    /*
     * 转换音频。
     * In和in_count可以设置为0，以在最后清除最后几个样本。
     * 如果提供的输入大于输出空间，则输入将被缓冲。
     * 可以通过使用swr_get_out_samples()检索给定输入样本数量所需输出样本数量的上限来避免这种缓冲。只要有可能，转换将直接运行而不进行复制。
     */
    if ((ret = avlibInterface::m_swr_convert(m_pSysAudioSwrContext, m_convertedSysSamples, inputFrame->nb_samples, const_cast<const uint8_t **>(inputFrame->extended_data), inputFrame->nb_samples)) < 0) {
        qCCritical(dsrApp) << "Could not convert system audio input samples for mixing, error code:" << ret;
        freeSwrContext(m_pSysAudioSwrContext);
        return ret;
    }

    freeSwrContext(m_pSysAudioSwrContext);

    int fifoSpace = audioFifoSpace(m_sysAudioFifo);
    if (fifoSpace < inputFrame->nb_samples) {
        fifoSpace = avlibInterface::m_av_audio_fifo_size(m_sysAudioFifo);
        if (INT_MAX / 2 - fifoSpace < inputFrame->nb_samples) {
            qCWarning(dsrApp) << "System audio buffer space asymmetry detected";
            return AVERROR(EINVAL);
        }
        if ((ret = audioFifoRealloc(m_sysAudioFifo, audioFifoSize(m_sysAudioFifo) + inputFrame->nb_samples)) < 0) {
            qCCritical(dsrApp) << "Could not reallocate system mix FIFO, error code:" << ret;
            return ret;
        }
        fifoSpace = audioFifoSpace(m_sysAudioFifo);
        qCDebug(dsrApp) << "Reallocated system mix FIFO, new space:" << fifoSpace;
    }
    int checkTime = 0;
    while (fifoSpace < inputFrame->nb_samples
            && isWriteFrame()
            && checkTime < 1000) {
        fifoSpace = audioFifoSpace(m_sysAudioFifo);
        //Sleep usseconds微秒，或者直到信号到达没有被阻塞或忽略。
        usleep(10 * 1000);
        checkTime ++;
        //printf("_fifo_spk full m_fifo_scard!\n");
    }

    if (fifoSpace >= inputFrame->nb_samples) {
        //数据写入到m_sysAudioFifo,如果可用的空间小于传入的inputFrame->nb_samples，m_sysAudioFifo会自动重新分配空间，但此处做了限制的只有大于等于的情况才进来
        if (audioWrite(m_sysAudioFifo, reinterpret_cast<void **>(m_convertedSysSamples), inputFrame->nb_samples) < inputFrame->nb_samples) {
            qCCritical(dsrApp) << "Could not write system data to mix FIFO";
            printf("Could not write data to FIFO\n");
            return AVERROR_EXIT;
        }
        qCDebug(dsrApp) << "Successfully wrote" << inputFrame->nb_samples << "system samples to mix FIFO";
    } else {
        qCWarning(dsrApp) << "Insufficient system FIFO space after waiting, dropping frame";
    }
    return 0;
}

void  CAVOutputStream::close()
{
    qCInfo(dsrApp) << "Closing AV output stream";
    QThread::msleep(500);
    if (nullptr != m_videoFormatContext
            || nullptr != m_videoStream
            || nullptr != m_micAudioStream
            || nullptr != m_sysAudioStream
            || nullptr != audio_amix_st) {
        qCInfo(dsrApp) << "Writing file trailer";
        //qDebug() << Q_FUNC_INFO << "开始写文件尾";
        //Write file trailer
        writeTrailer(m_videoFormatContext);
        qCInfo(dsrApp) << Q_FUNC_INFO << "写文件尾完成";
    }

    if (m_videoStream) {
        qCDebug(dsrApp) << "Closing video codec";
        avlibInterface::m_avcodec_close(m_videoStream->codec);
        m_videoStream = nullptr;
    }
    if (m_micAudioStream) {
        qCDebug(dsrApp) << "Closing microphone audio codec";
        avlibInterface::m_avcodec_close(m_micAudioStream->codec);
        m_micAudioStream = nullptr;
    }
    //    if (audio_scard_st)
    //    {
    //        avcodec_close(audio_scard_st->codec);
    //        audio_scard_st = nullptr;
    //    }
    if (audio_amix_st) {
        qCDebug(dsrApp) << "Closing audio mix codec";
        avlibInterface::m_avcodec_close(audio_amix_st->codec);
        audio_amix_st = nullptr;
    }

    if (m_out_buffer) {
        qCDebug(dsrApp) << "Freeing output buffer";
        avlibInterface::m_av_free(m_out_buffer);
        m_out_buffer = nullptr;
    }
    if (m_convertedMicSamples) {
        qCDebug(dsrApp) << "Freeing converted microphone samples";
        avlibInterface::m_av_freep(&m_convertedMicSamples[0]);
        m_convertedMicSamples = nullptr;
    }
    if (m_convertedSysSamples) {
        qCDebug(dsrApp) << "Freeing converted system samples";
        avlibInterface::m_av_freep(&m_convertedSysSamples[0]);
        m_convertedSysSamples = nullptr;
    }
    is_fifo_scardinit = 0;
    if (m_videoFormatContext) {
        qCDebug(dsrApp) << "Closing video format context";
        avlibInterface::m_avio_close(m_videoFormatContext->pb);
    }
    avlibInterface::m_avformat_free_context(m_videoFormatContext);
    m_videoFormatContext = nullptr;
    m_videoCodecID  = AV_CODEC_ID_NONE;
    m_micAudioCodecID = AV_CODEC_ID_NONE;
    m_sysAudioCodecID = AV_CODEC_ID_NONE;
    if (m_bMix) {
        qCDebug(dsrApp) << "Freeing audio mixing resources";
        avlibInterface::m_av_frame_free(&mMic_frame);
        avlibInterface::m_av_frame_free(&mSpeaker_frame);
        avlibInterface::m_avfilter_graph_free(&filter_graph);
    }
    qCInfo(dsrApp) << "AV output stream closed successfully";
}
void CAVOutputStream::setIsOverWrite(bool isCOntinue)
{
    qCDebug(dsrApp) << "Setting overwrite flag to:" << isCOntinue;
    m_isOverWrite = isCOntinue;
}

int CAVOutputStream::audioRead(AVAudioFifo *af, void **data, int nb_samples)
{
    QMutexLocker locker(&m_audioReadWriteMutex);
    //qDebug() << "+++++++++++++++++++++++ 读音频帧 +++++++++++++++++++++++";
    return avlibInterface::m_av_audio_fifo_read(af, data, nb_samples);
}

int CAVOutputStream::audioWrite(AVAudioFifo *af, void **data, int nb_samples)
{
    QMutexLocker locker(&m_audioReadWriteMutex);
    //qDebug() << "----------------------- 写音频帧";
    return avlibInterface::m_av_audio_fifo_write(af, data, nb_samples);
}

int CAVOutputStream::writeFrame(AVFormatContext *s, AVPacket *pkt)
{
    QMutexLocker locker(&m_writeFrameMutex);
    //qDebug() << "+++++++++++++++++++++++ 写视频帧";
    return avlibInterface::m_av_interleaved_write_frame(s, pkt);
}

int CAVOutputStream::writeTrailer(AVFormatContext *s)
{
    //qDebug() << "+++++++++++++++++++++++ 写视频尾";
    qCDebug(dsrApp) << "Writing format trailer";
    QMutexLocker locker(&m_writeFrameMutex);
    return avlibInterface::m_av_write_trailer(s);
}

/**
 * @brief 获取当前af中可供写入的样本数（剩余空间）
 * @param af
 * @return
 */
int CAVOutputStream::audioFifoSpace(AVAudioFifo *af)
{
    //qDebug() << "+++++++++++++++++++++++ 1";
    QMutexLocker locker(&m_audioReadWriteMutex);
    return avlibInterface::m_av_audio_fifo_space(af);
}

/**
 * @brief 获取当前af中可供读取的样本数（已使用空间）
 * @param af
 * @return
 */
int CAVOutputStream::audioFifoSize(AVAudioFifo *af)
{
    //qDebug() << "+++++++++++++++++++++++ 2";
    QMutexLocker locker(&m_audioReadWriteMutex);
    return avlibInterface::m_av_audio_fifo_size(af);
}

int CAVOutputStream::audioFifoRealloc(AVAudioFifo *af, int nb_samples)
{
    //qDebug() << "+++++++++++++++++++++++ 3";
    qCDebug(dsrApp) << "Reallocating audio FIFO to" << nb_samples << "samples";
    QMutexLocker locker(&m_audioReadWriteMutex);
    return avlibInterface::m_av_audio_fifo_realloc(af, nb_samples);
}

AVAudioFifo *CAVOutputStream::audioFifoAlloc(AVSampleFormat sample_fmt, int channels, int nb_samples)
{
    //qDebug() << "+++++++++++++++++++++++ 4";
    qCDebug(dsrApp) << "Allocating audio FIFO with format:" << sample_fmt << "channels:" << channels << "samples:" << nb_samples;
    QMutexLocker locker(&m_audioReadWriteMutex);
    return avlibInterface::m_av_audio_fifo_alloc(sample_fmt, channels, nb_samples);
}

void CAVOutputStream::audioFifoFree(AVAudioFifo *af)
{
    //qDebug() << "+++++++++++++++++++++++ 5";
    qCDebug(dsrApp) << "Freeing audio FIFO";
    QMutexLocker locker(&m_audioReadWriteMutex);
    avlibInterface::m_av_audio_fifo_free(af);
}

bool CAVOutputStream::isWriteFrame()
{
    QMutexLocker locker(&m_isWriteFrameMutex);
    if (isNotAudioFifoEmty()) {
        return true;
    }
    return m_isWriteFrame;
}

void CAVOutputStream::setIsWriteFrame(bool isWriteFrame)
{
    qCDebug(dsrApp) << "Setting write frame flag to:" << isWriteFrame;
    QMutexLocker locker(&m_isWriteFrameMutex);
    m_isWriteFrame = isWriteFrame;
}

void CAVOutputStream::setBoardVendor(int boardVendorType)
{
    qCDebug(dsrApp) << "Setting board vendor type to:" << boardVendorType;
    m_boardVendorType = boardVendorType;
}

//释放swrContext
void CAVOutputStream::freeSwrContext(SwrContext *swrContext)
{
    Q_UNUSED(swrContext);
//    if (swrContext != nullptr) {
//        avlibInterface::m_swr_free(&swrContext);
//        swrContext = nullptr;
//    }
}

