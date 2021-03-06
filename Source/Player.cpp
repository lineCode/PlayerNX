#include "Player.hpp"

bool ffinit = false;
bool stop = false;
bool pause = false;
AVFormatContext* ctx_format;
AVCodecContext* ctx_codec;
AVCodec* codec;
AVFrame* frame;
AVFrame* rgbframe;
int stream_idx;
SwsContext* ctx_sws;
AVStream *vid_stream;
AVPacket* pkt;
int ret;
int iffw = 1;
int counter = 0;

void Player::playbackInit(string VideoPath)
{
    if(!ffinit)
    {
        frame = av_frame_alloc();
        rgbframe = av_frame_alloc();
        pkt = av_packet_alloc();
        av_register_all();
        avcodec_register_all();
        av_log_set_level(AV_LOG_QUIET);
        ret = avformat_open_input(&ctx_format, VideoPath.c_str(), NULL, NULL);
        if(ret != 0) Player::playbackThrowError("Error opening file. Is it a valid video?");
        if(avformat_find_stream_info(ctx_format, NULL) < 0) Player::playbackThrowError("Error finding stream info.");
        av_dump_format(ctx_format, 0, VideoPath.c_str(), false);
        for(int i = 0; i < ctx_format->nb_streams; i++) if(ctx_format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            stream_idx = i;
            vid_stream = ctx_format->streams[i];
            break;
        }
        if(!vid_stream) Player::playbackThrowError("Error getting video stream.");
        codec = avcodec_find_decoder(vid_stream->codecpar->codec_id);
        if(!codec) Player::playbackThrowError("Error finding a decoder (strange)");
        ctx_codec = avcodec_alloc_context3(codec);
        if(avcodec_parameters_to_context(ctx_codec, vid_stream->codecpar) < 0) Player::playbackThrowError("Error sending parameters to codec context.");
        if(avcodec_open2(ctx_codec, codec, NULL) < 0) Player::playbackThrowError("Error opening codec with context.");
        ffinit = true;
    }
}

bool Player::playbackLoop()
{
    if(ffinit)
    {
        while(true)
        {
            if(stop) break;
            if(pause)
            {
                hidScanInput();
                u64 k = hidKeysDown(CONTROLLER_P1_AUTO);
                if(k & KEY_X) Player::playbackPauseResume();
                else if((k & KEY_PLUS) || (k & KEY_MINUS)) Player::playbackStop();
                else if(k & KEY_Y)
                {
                    int newffd = 1;
                    if(iffw == 1) newffd = 2;
                    else if(iffw == 2) newffd = 4;
                    else if(iffw == 4) newffd = 8;
                    else newffd = 1;
                    Player::playbackSetFastForward(newffd);
                }
                Gfx::flush();
            }
            else if(av_read_frame(ctx_format, pkt) >= 0)
            {
                if(pkt->stream_index == stream_idx)
                {
                    ret = avcodec_send_packet(ctx_codec, pkt);
                    if(ret < 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                    while(ret >= 0)
                    {
                        ret = avcodec_receive_frame(ctx_codec, frame);
                        if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                        hidScanInput();
                        u64 k = hidKeysDown(CONTROLLER_P1_AUTO);
                        if(k & KEY_X) Player::playbackPauseResume();
                        else if((k & KEY_PLUS) || (k & KEY_MINUS)) Player::playbackStop();
                        else if(k & KEY_Y)
                        {
                            int newffd = 1;
                            if(iffw == 1) newffd = 2;
                            else if(iffw == 2) newffd = 4;
                            else if(iffw == 4) newffd = 8;
                            else if(iffw == 8) newffd = 1;
                            Player::playbackSetFastForward(newffd);
                        }
                        counter++;
                        if(counter >= iffw) counter = 0;
                        else continue;
                        ctx_sws = sws_getContext(frame->width, frame->height, AV_PIX_FMT_YUV420P, frame->width, frame->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, 0, 0, 0);
                        rgbframe->width = frame->width;
                        rgbframe->height = frame->height;
                        rgbframe->format = AV_PIX_FMT_RGB24;
                        av_image_alloc(rgbframe->data, rgbframe->linesize, rgbframe->width, rgbframe->height, rgbframe->format, 32);
                        sws_scale(ctx_sws, frame->data, frame->linesize, 0, frame->height, rgbframe->data, rgbframe->linesize);
                        u32 w = 1280;
                        u8 *fbuf = gfxGetFramebuffer(&w, NULL);
                        for(u32 i = 0; i < rgbframe->width; i++) for(u32 j = 0; j < rgbframe->height; j++)
                        {
                            u64 pos = (rgbframe->width * j + i) * 3;
                            u64 fpos = (j * w + i) * 4; 
                            fbuf[fpos] = rgbframe->data[0][pos];
                            fbuf[fpos + 1] = rgbframe->data[0][pos + 1];
                            fbuf[fpos + 2] = rgbframe->data[0][pos + 2];
                            fbuf[fpos + 3] = 0xff;
                        }
                        Gfx::flush();
                        av_freep(rgbframe->data);
                    }
                }
            }
            else stop = true;
        }
        if(stop) Player::playbackExit();
        return false;
    }
}

void Player::playbackPause()
{
    if(!pause) pause = true;
}

void Player::playbackResume()
{
    if(pause) pause = false;
}

void Player::playbackPauseResume()
{
    pause = !pause;
}

void Player::playbackStop()
{
    if(!stop) stop = true;
}

void Player::playbackExit()
{
    if(ffinit)
    {
        avformat_close_input(&ctx_format);
        av_packet_unref(pkt);
        av_frame_unref(rgbframe);
        av_frame_unref(frame);
        avcodec_free_context(&ctx_codec);
        avformat_free_context(ctx_format);
        if(stop) stop = false;
        if(pause) pause = false;
        iffw = 1;
        ffinit = false;
    }
}

void Player::playbackSetFastForward(int Power)
{
    iffw = Power;
}

void Player::playbackThrowError(string Error)
{
    Gfx::exit();
    Player::playbackExit();
    gfxInitDefault();
    consoleInit(NULL);
    cout << endl << endl << "PlayerNX was closed due to an error: " << endl << endl << endl << " - " << Error << endl << endl << " - Press A to exit PlayerNX.";
    while(true)
    {
        hidScanInput();
        int k = hidKeysDown(CONTROLLER_P1_AUTO);
        if(k & KEY_A) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gfxWaitForVsync();
    }
    gfxExit();
    romfsExit();
    exit(0);
}

vector<string> Player::getVideoFiles()
{
    vector<string> vids;
    struct dirent *de;
    DIR *dir = opendir("sdmc:/media");
    if(dir)
    {
        while(true)
        {
            de = readdir(dir);
            if(de == NULL) break;
            vids.push_back(string(de->d_name));
        }
    }
    closedir(dir);
    return vids;
}