/*
 * Copyright (c) 2012, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#define USE_ERROR
#define USE_TRACE

extern "C" {
#include "DirectAudio.h"
}

#include "PLATFORM_API_HaikuOS_Utils.h"

#include <MediaDefs.h>
#include <MediaNode.h>
#include <MediaRoster.h>
#include <MediaRecorder.h>
#include <SoundPlayer.h>
#include <TimeSource.h>

#include <vector>

#if USE_DAUDIO == TRUE

AudioDeviceCache cache;

extern "C" {

INT32 DAUDIO_GetDirectAudioDeviceCount() {
    return cache.DeviceCount();
}

INT32 DAUDIO_GetDirectAudioDeviceDescription(INT32 mixerIndex,
                                             DirectAudioDeviceDescription* description) {
    live_node_info info;
    if (cache.GetDevice(mixerIndex, &info) != B_OK)
        return FALSE;

    strlcpy(description->name, info.name, DAUDIO_STRING_LENGTH);
    // Mac OS X port sets this to -1; unsure exactly what it means
    description->maxSimulLines = -1;

    // We don't have any info to fill out the other fields
    return TRUE;
}

static const int maxIOs = 64;

static void EnumerateInputs(media_node node, std::vector<media_input>* inputs,
        std::vector<media_format>* formats) {
    BMediaRoster* roster = BMediaRoster::Roster();

    media_input inputArray[maxIOs];
    int32 inputCount;

    status_t rr = roster->GetFreeInputsFor(node, inputArray, maxIOs, &inputCount);
    for (int i = 0; i < inputCount; i++) {
        if (inputs != NULL)
            inputs->push_back(inputArray[i]);
        if (formats != NULL)
            formats->push_back(inputArray[i].format);
    }
}

static void EnumerateOutputs(media_node node, std::vector<media_output>* outputs,
        std::vector<media_format>* formats) {
    BMediaRoster* roster = BMediaRoster::Roster();

    media_output outputArray[maxIOs];
    int32 outputCount;

    roster->GetFreeOutputsFor(node, outputArray, maxIOs, &outputCount);
    for (int i = 0; i < outputCount; i++) {
        if (outputs != NULL)
            outputs->push_back(outputArray[i]);
        if (formats != NULL)
            formats->push_back(outputArray[i].format);
    }
}

static int AudioFormatToBits(uint32 format) {
    switch (format) {
        case media_raw_audio_format::B_AUDIO_FLOAT:
            // TODO
        case media_raw_audio_format::B_AUDIO_INT:
            return 32;
        case media_raw_audio_format::B_AUDIO_SHORT:
            return 16;
        case media_raw_audio_format::B_AUDIO_UCHAR:
        case media_raw_audio_format::B_AUDIO_CHAR:
            return 8;
        default:
            return 0;
   }
}

static uint32 BitsToAudioFormat(int bits) {
    switch (bits) {
        case 32:
            return media_raw_audio_format::B_AUDIO_INT;
        case 16:
            return media_raw_audio_format::B_AUDIO_SHORT;
        case 8:
            return media_raw_audio_format::B_AUDIO_CHAR;
        default:
            return 0;
    }
}

static int bitDepths[] = { 8, 16, 24, 32 };
static float sampleRates[] = { 11025, 22050, 44100, 48000, 96000, 192000 };
static int channelCounts[] = { 2 };

void DAUDIO_GetFormats(INT32 mixerIndex, INT32 deviceID, int isSource, void* creator) {
    live_node_info info;

    if (cache.GetDevice(mixerIndex, &info) != B_OK)
        return;

    std::vector<media_format> formats;

    if (isSource == TRUE) {
        // We're looking for output formats, so we want to get the
        // node's inputs
        EnumerateInputs(info.node, NULL, &formats);
    } else {
        // We're looking for input formats, so we want to get the
        // node's outputs
        EnumerateOutputs(info.node, NULL, &formats);
    }

    for (size_t i = 0; i < formats.size(); i++) {
        media_multi_audio_format& format = formats[i].u.raw_audio;

        if (format == format.wildcard) {
            // If it's a wildcard format add some common formats
            for (size_t i = 0; i < sizeof(bitDepths) / sizeof(bitDepths[0]); i++) {
                for (size_t j = 0; j < sizeof(channelCounts) / sizeof(channelCounts[0]); j++) {
                    for (size_t k = 0; k < sizeof(sampleRates) / sizeof(sampleRates[0]); k++) {
                        DAUDIO_AddAudioFormat(creator, bitDepths[i], -1, channelCounts[j], sampleRates[k],
                            DAUDIO_PCM, TRUE, TRUE);
                    }
                }
            }
        } else {
            int bitCount = AudioFormatToBits(format.format);
            DAUDIO_AddAudioFormat(creator,
                bitCount, // bits per sample
                -1, // auto frame size
                format.channel_count, // channel count
                format.frame_rate, // sample rate
                DAUDIO_PCM, // pcm encoding
                format.format == media_raw_audio_format::B_AUDIO_UCHAR // is signed
                    ? FALSE : TRUE,
                format.byte_order == B_MEDIA_BIG_ENDIAN // is big endian
                    ? TRUE : FALSE);
        }
    }
}


typedef struct {
    BSoundPlayer* sound_player;
    BMediaRecorder* media_recorder;
    RingBuffer buffer;
} HaikuPCMInfo;


static void PlayBuffer(void* cookie, void* buffer, size_t size,
        const media_raw_audio_format& format) {
    if (size <= 0)
        return;

    HaikuPCMInfo* info = (HaikuPCMInfo*)cookie;

    // assume that the format is the one we requested for now

    // try to read size bytes from the buffer
    size_t read = info->buffer.Read(buffer, size);

    if (read < size) {
        // buffer underrun
        memset((UBYTE*)buffer + read, 0, size - read);
        fprintf(stderr, "Buffer underrun occured (%llu/%llu)...\n",
            (long long unsigned)read, (long long unsigned)read);
    }
}


static void PlayNotifier(void* cookie,
        BSoundPlayer::sound_player_notification what, ...) {
}


static void RecordBuffer(void* cookie, bigtime_t timestamp, void* buffer,
        size_t size, const media_format& format) {
    if (size <= 0)
        return;

    HaikuPCMInfo* info = (HaikuPCMInfo*)cookie;
    info->buffer.Write(buffer, size, false);
}


void* DAUDIO_Open(INT32 mixerIndex, INT32 deviceID, int isSource,
                  int encoding, float sampleRate, int sampleSizeInBits,
                  int frameSize, int channels,
                  int isSigned, int isBigEndian, int bufferSizeInBytes) {

    if (channels <= 0) {
        ERROR1("ERROR: Invalid number of channels=%d!\n", channels);
        return NULL;
    }

    live_node_info nodeInfo;
    if (cache.GetDevice(mixerIndex, &nodeInfo) != B_OK)
        return NULL;

    std::vector<media_input> inputs;
    std::vector<media_output> outputs;
    std::vector<media_format> formats;

    static const int maxIOs = 64;
    if (isSource == TRUE) {
        // We're looking for output formats, so we want to get the
        // node's inputs
        EnumerateInputs(nodeInfo.node, &inputs, &formats);
    } else {
        // We're looking for input formats, so we want to get the
        // node's outputs
        EnumerateOutputs(nodeInfo.node, &outputs, &formats);
    }

    // find the matching media_input/media_output
    int foundIndex = -1;
    for (size_t i = 0; i < formats.size(); i++) {
        media_multi_audio_format& format = formats[i].u.raw_audio;
        int bits = AudioFormatToBits(format.format);

        if (format.frame_rate == sampleRate && bits == sampleSizeInBits
                && (int)format.channel_count == channels
                && (format.byte_order == B_MEDIA_BIG_ENDIAN) == (isBigEndian == TRUE)
                && (format.format == media_raw_audio_format::B_AUDIO_UCHAR)
                    == (isSigned == FALSE)) {
            foundIndex = i;
            break;
        } else if (format == format.wildcard) {
            // We need to set up our requested format
            format.frame_rate = sampleRate;
            format.channel_count = channels;
            format.format = BitsToAudioFormat(sampleSizeInBits);
            format.byte_order = isBigEndian == TRUE ? B_MEDIA_BIG_ENDIAN : B_MEDIA_LITTLE_ENDIAN;
            format.buffer_size = bufferSizeInBytes;
            foundIndex = i;
            break;
        }
    }

    if (foundIndex == -1) {
        ERROR0("DAUDIO_Open: ERROR: format doesn't match format of any input/output!\n");
        return NULL;
    }

    media_format& format = formats[foundIndex];
    media_multi_audio_format& audioFormat = format.u.raw_audio;

    HaikuPCMInfo* info = new HaikuPCMInfo;
    bool allocated = info->buffer.Allocate(bufferSizeInBytes,
        isSource == TRUE ? audioFormat.buffer_size : 0);

    if (!allocated) {
        delete info;
        return NULL;
    }

    if (isSource == TRUE) {
        media_input& input = inputs[foundIndex];

        BSoundPlayer* player = new BSoundPlayer(input.node, &audioFormat,
            "jsoundSoundPlayer", &input, PlayBuffer, PlayNotifier, info);

        if (player->InitCheck() != B_OK) {
            ERROR0("DAUDIO_Open: Failed to initialise BSoundPlayer");
            delete info;
            delete player;
            return NULL;
        }

        info->sound_player = player;
        return (void*)info;
    } else {
        media_output& output = outputs[foundIndex];

        BMediaRecorder* recorder = new BMediaRecorder("jsoundMediaRecorder",   
            B_MEDIA_RAW_AUDIO);

        if (recorder->InitCheck() != B_OK
                || recorder->SetHooks(RecordBuffer, NULL, info) != B_OK
                || recorder->Connect(output.node, &output, &format) != B_OK) {
            ERROR0("DAUDIO_Open: Failed to initialise BMediaRecorder");
            delete info;
            delete recorder;
            return NULL;
        }

        info->media_recorder = recorder;
        return (void*)info;
    }

    return NULL;
}


int DAUDIO_Start(void* id, int isSource) {
    HaikuPCMInfo* info = (HaikuPCMInfo*)id;
    status_t result = B_ERROR;

    if (isSource == TRUE) {
        result = info->sound_player->Start();
    } else {
        result = info->media_recorder->Start();
    }

    return result == B_OK ? TRUE : FALSE;
}

int DAUDIO_Stop(void* id, int isSource) {
    HaikuPCMInfo* info = (HaikuPCMInfo*)id;

    if (isSource == TRUE) {
        info->sound_player->Stop();
    } else {
        info->media_recorder->Stop();
    }

    return TRUE;
}

void DAUDIO_Close(void* id, int isSource) {
    HaikuPCMInfo* info = (HaikuPCMInfo*)id;

    DAUDIO_Stop(id, isSource);
    if (isSource == TRUE) {
        delete info->sound_player;
    } else {
        info->media_recorder->Disconnect();
        delete info->media_recorder;
    }

    delete info;
}

int DAUDIO_Write(void* id, char* data, int byteSize) {
    HaikuPCMInfo* info = (HaikuPCMInfo*)id;
    TRACE1(">>DAUDIO_Write: %d bytes to write\n", byteSize);

    int result = info->buffer.Write(data, byteSize, true);

    TRACE1("<<DAUDIO_Write: %d bytes written\n", result);
    return result;
}

int DAUDIO_Read(void* id, char* data, int byteSize) {
    HaikuPCMInfo* info = (HaikuPCMInfo*)id;
    TRACE1(">>DAUDIO_Read: %d bytes to read\n", byteSize);

    int result = info->buffer.Read(data, byteSize);

    TRACE1("<<DAUDIO_Read: %d bytes has been read\n", result);
    return result;
}

int DAUDIO_GetBufferSize(void* id, int isSource) {
    HaikuPCMInfo* info = (HaikuPCMInfo*)id;

    int bufferSizeInBytes = info->buffer.GetBufferSize();

    TRACE1("DAUDIO_GetBufferSize returns %d\n", bufferSizeInBytes);
    return bufferSizeInBytes;
}

int DAUDIO_StillDraining(void* id, int isSource) {
    HaikuPCMInfo* info = (HaikuPCMInfo*)id;

    int draining = info->buffer.GetValidByteCount() > 0 ? TRUE : FALSE;

    TRACE1("DAUDIO_StillDraining returns %d\n", draining);
    return draining;
}

int DAUDIO_Flush(void* id, int isSource) {
    HaikuPCMInfo* info = (HaikuPCMInfo*)id;
    TRACE0("DAUDIO_Flush\n");

    info->buffer.Flush();

    return TRUE;
}

int DAUDIO_GetAvailable(void* id, int isSource) {
    HaikuPCMInfo* info = (HaikuPCMInfo*)id;

    int bytesInBuffer = info->buffer.GetValidByteCount();
    if (isSource) {
        return info->buffer.GetBufferSize() - bytesInBuffer;
    } else {
        return bytesInBuffer;
    }
}

INT64 DAUDIO_GetBytePosition(void* id, int isSource, INT64 javaBytePos) {
    HaikuPCMInfo* info = (HaikuPCMInfo*)id;
    INT64 position;

    if (isSource) {
        position = javaBytePos - info->buffer.GetValidByteCount();
    } else {
        position = javaBytePos + info->buffer.GetValidByteCount();
    }

    TRACE2("DAUDIO_GetBytePosition returns %lld (javaBytePos = %lld)\n", (long long)position, (long long)javaBytePos);
    return position;
}

void DAUDIO_SetBytePosition(void* id, int isSource, INT64 javaBytePos) {
    // unneeded
}

int DAUDIO_RequiresServicing(void* id, int isSource) {
    return FALSE;
}

void DAUDIO_Service(void* id, int isSource) {
    // unused
}

}

#endif // USE_DAUDIO
