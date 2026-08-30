#include "music_decode.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>

#include <stdio.h>
#include <stdlib.h>

typedef struct DecodeBuffer {
    int16_t *samples;
    size_t frames;
    size_t capacity;
    int sample_rate;
    int channels;
    SwrContext *resampler;
} DecodeBuffer;

static void format_error(char *output, size_t capacity, const char *operation, int code)
{
    char detail[AV_ERROR_MAX_STRING_SIZE] = "";
    av_strerror(code, detail, sizeof detail);
    snprintf(output, capacity, "%s: %s", operation, detail);
}

static int reserve_frames(DecodeBuffer *output, size_t additional, char *error, size_t error_capacity)
{
    if (additional <= output->capacity - output->frames) return 1;
    const size_t maximum_frames = music_decode_frame_limit(output->sample_rate);
    if (additional > maximum_frames - output->frames) {
        snprintf(error, error_capacity, "decoded music exceeds the ten-minute safety limit");
        return 0;
    }
    size_t capacity = output->capacity ? output->capacity : (size_t)output->sample_rate * 30u;
    while (capacity - output->frames < additional) {
        const size_t larger = capacity * 2u;
        capacity = larger > maximum_frames ? maximum_frames : larger;
    }
    int16_t *samples = realloc(output->samples, capacity * (size_t)output->channels * sizeof *samples);
    if (!samples) {
        snprintf(error, error_capacity, "out of memory while decoding music");
        return 0;
    }
    output->samples = samples;
    output->capacity = capacity;
    return 1;
}

static int convert_frame(DecodeBuffer *output, const AVFrame *frame, int input_rate, char *error, size_t error_capacity)
{
    const int wanted = (int)av_rescale_rnd(swr_get_delay(output->resampler, input_rate) + frame->nb_samples,
                                           output->sample_rate, input_rate, AV_ROUND_UP);
    if (wanted < 0 || !reserve_frames(output, (size_t)wanted, error, error_capacity)) return 0;
    uint8_t *destination = (uint8_t *)(output->samples + output->frames * (size_t)output->channels);
    const int converted = swr_convert(output->resampler, &destination, wanted,
                                      (const uint8_t *const *)frame->extended_data, frame->nb_samples);
    if (converted < 0) {
        format_error(error, error_capacity, "could not resample music", converted);
        return 0;
    }
    output->frames += (size_t)converted;
    return 1;
}

static int receive_frames(AVCodecContext *codec, AVFrame *frame, DecodeBuffer *output, char *error,
                          size_t error_capacity)
{
    for (;;) {
        const int received = avcodec_receive_frame(codec, frame);
        if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) return 1;
        if (received < 0) {
            format_error(error, error_capacity, "could not decode music frame", received);
            return 0;
        }
        const int converted = convert_frame(output, frame, codec->sample_rate, error, error_capacity);
        av_frame_unref(frame);
        if (!converted) return 0;
    }
}

static int flush_resampler(DecodeBuffer *output, int input_rate, char *error, size_t error_capacity)
{
    for (;;) {
        const int delayed = (int)av_rescale_rnd(swr_get_delay(output->resampler, input_rate), output->sample_rate,
                                                input_rate, AV_ROUND_UP);
        if (delayed <= 0) return 1;
        if (!reserve_frames(output, (size_t)delayed, error, error_capacity)) return 0;
        uint8_t *destination = (uint8_t *)(output->samples + output->frames * (size_t)output->channels);
        const int converted = swr_convert(output->resampler, &destination, delayed, NULL, 0);
        if (converted < 0) {
            format_error(error, error_capacity, "could not flush music resampler", converted);
            return 0;
        }
        if (converted == 0) return 1;
        output->frames += (size_t)converted;
    }
}

int music_decode_file(const char *path, int sample_rate, int channels, int16_t **samples, size_t *frames, char *error,
                      size_t error_capacity)
{
    if (!path || !samples || !frames || !error || error_capacity == 0 || sample_rate <= 0 || channels <= 0) return 0;
    *samples = NULL;
    *frames = 0;
    error[0] = 0;

    AVFormatContext *format = NULL;
    AVCodecContext *codec = NULL;
    AVPacket *packet = NULL;
    AVFrame *frame = NULL;
    DecodeBuffer output = {.sample_rate = sample_rate, .channels = channels};
    AVChannelLayout destination_layout = {0};
    int result = avformat_open_input(&format, path, NULL, NULL);
    if (result < 0) {
        format_error(error, error_capacity, "could not open music file", result);
        goto done;
    }
    result = avformat_find_stream_info(format, NULL);
    if (result < 0) {
        format_error(error, error_capacity, "could not inspect music stream", result);
        goto done;
    }

    const AVCodec *decoder = NULL;
    const int stream_index = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (stream_index < 0 || !decoder) {
        format_error(error, error_capacity, "could not find a supported music stream", stream_index);
        goto done;
    }
    codec = avcodec_alloc_context3(decoder);
    if (!codec) {
        snprintf(error, error_capacity, "out of memory while creating the music decoder");
        goto done;
    }
    result = avcodec_parameters_to_context(codec, format->streams[stream_index]->codecpar);
    if (result < 0 || (result = avcodec_open2(codec, decoder, NULL)) < 0) {
        format_error(error, error_capacity, "could not initialize the music decoder", result);
        goto done;
    }

    av_channel_layout_default(&destination_layout, channels);
    result = swr_alloc_set_opts2(&output.resampler, &destination_layout, AV_SAMPLE_FMT_S16, sample_rate,
                                 &codec->ch_layout, codec->sample_fmt, codec->sample_rate, 0, NULL);
    if (result < 0 || !output.resampler || (result = swr_init(output.resampler)) < 0) {
        format_error(error, error_capacity, "could not initialize the music resampler", result);
        goto done;
    }
    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (!packet || !frame) {
        snprintf(error, error_capacity, "out of memory while decoding music");
        goto done;
    }

    while ((result = av_read_frame(format, packet)) >= 0) {
        if (packet->stream_index == stream_index) {
            result = avcodec_send_packet(codec, packet);
            if (result < 0 || !receive_frames(codec, frame, &output, error, error_capacity)) {
                if (!error[0]) format_error(error, error_capacity, "could not submit music packet", result);
                av_packet_unref(packet);
                goto done;
            }
        }
        av_packet_unref(packet);
    }
    if (result != AVERROR_EOF) {
        format_error(error, error_capacity, "could not read music stream", result);
        goto done;
    }
    result = avcodec_send_packet(codec, NULL);
    if ((result < 0 && result != AVERROR_EOF) || !receive_frames(codec, frame, &output, error, error_capacity) ||
        !flush_resampler(&output, codec->sample_rate, error, error_capacity)) {
        if (!error[0]) format_error(error, error_capacity, "could not finish music decoding", result);
        goto done;
    }
    if (output.frames == 0) {
        snprintf(error, error_capacity, "music decoder returned no audio for %s", path);
        goto done;
    }

    *samples = output.samples;
    *frames = output.frames;
    output.samples = NULL;

done:
    free(output.samples);
    swr_free(&output.resampler);
    av_channel_layout_uninit(&destination_layout);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec);
    avformat_close_input(&format);
    return *samples != NULL;
}
