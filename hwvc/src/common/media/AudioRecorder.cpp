/*
* Copyright (c) 2018-present, lmyooyo@gmail.com.
*
* This source code is licensed under the GPL license found in the
* LICENSE file in the root directory of this source tree.
*/

#include "../include/AudioRecorder.h"

void bufferDequeueCallback(SLBufferQueueItf slBufferQueueItf, void *context) {
    AudioRecorder *recorder = static_cast<AudioRecorder *>(context);
    recorder->bufferDequeue(slBufferQueueItf);
}

void AudioRecorder::bufferDequeue(SLBufferQueueItf slBufferQueueItf) {
    ObjectBox *cache = recycler->takeCache();
    if (cache) {
        (*slBufferQueueItf)->Enqueue(bufferQueueItf, cache->ptr, minBufferSize);
    }
    recycler->offer(cache);
}

AudioRecorder::AudioRecorder(unsigned int channels, unsigned int sampleHz, int format,
                             int minBufferSize) {
    this->channels = channels;
    this->sampleHz = sampleHz;
    this->format = format;
    this->minBufferSize = minBufferSize;
    LOGI("Create AudioRecorder, channels=%d, sampleHz=%d",
         this->channels,
         this->sampleHz);
    this->recycler = new RecyclerBlockQueue<ObjectBox>(16, [minBufferSize] {
        return new ObjectBox(new uint8_t[minBufferSize]);
    });
    int ret = this->createEngine();
    if (!ret) {
        LOGE("AudioRecorder start failed");
    }
}

AudioRecorder::~AudioRecorder() {
    LOGI("~AudioRecorder");
    stop();
}

int AudioRecorder::start() {
    SLresult result = (*recordItf)->SetRecordState(recordItf, SL_RECORDSTATE_RECORDING);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("Recorder SetRecordState start failed!");
        return 0;
    }
    bufferDequeue(bufferQueueItf);
    return 1;
}

void AudioRecorder::stop() {
    SLresult result = (*recordItf)->SetRecordState(recordItf, SL_RECORDSTATE_STOPPED);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("Recorder SetRecordState stop failed!");
    }
    if (recycler) {
        recycler->notify();
    }
    destroyEngine();
    if (recycler) {
        delete recycler;
        recycler = nullptr;
    }
}

size_t AudioRecorder::read(uint8_t *buffer) {
    ObjectBox *cache = recycler->take();
    if (!cache) {
        LOGE("Cache invalid");
        return 0;
    }
    memcpy(cache->ptr, buffer, minBufferSize);
    recycler->recycle(cache);
    return minBufferSize;
}

void AudioRecorder::AudioRecorder::flush() {

}

int AudioRecorder::createEngine() {
    SLresult result = slCreateEngine(&engineObject, 0, NULL, 0, NULL, NULL);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("slCreateEngine failed!");
        return 0;
    }
    result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("Engine Realize failed!");
        return 0;
    }
    result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineItf);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("Engine GetInterface failed!");
        return 0;
    }
    return createBufferQueueObject();
}

int AudioRecorder::createBufferQueueObject() {
    SLDataLocator_IODevice io = {SL_DATALOCATOR_IODEVICE,
                                 SL_IODEVICE_AUDIOINPUT,
                                 SL_DEFAULTDEVICEID_AUDIOINPUT,
                                 NULL};
    SLDataSource dataSource = {&io, NULL};

    SLDataLocator_AndroidSimpleBufferQueue buffer_queue = {
            SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
            2
    };
    SLDataFormat_PCM format = {SL_DATAFORMAT_PCM,
                               channels,
                               sampleHz * 1000,
                               this->format,
                               this->format,
                               getChannelMask(channels),
                               SL_BYTEORDER_LITTLEENDIAN};
    SLDataSink slDataSink = {
            &buffer_queue,
            &format
    };
    const SLInterfaceID ids[1] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
    const SLboolean req[1] = {SL_BOOLEAN_TRUE};
    SLresult result = (*engineItf)->CreateAudioRecorder(engineItf,
                                                      &recordObject,
                                                      &dataSource,
                                                      &slDataSink,
                                                      1,
                                                      ids,
                                                      req);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("CreateAudioRecorder failed! ret=%d", result);
        return 0;
    }
    result = (*recordObject)->Realize(recordObject, SL_BOOLEAN_FALSE);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("Recorder Realize failed!");
        return 0;
    }
    result = (*recordObject)->GetInterface(recordObject, SL_IID_RECORD, &recordItf);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("Recorder GetInterface failed!");
        return 0;
    }
    result = (*recordObject)->GetInterface(recordObject, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &bufferQueueItf);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("Recorder GetInterface buffer queue failed!");
        return 0;
    }
    result = (*bufferQueueItf)->RegisterCallback(bufferQueueItf,
                                                 bufferDequeueCallback,
                                                 this);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("Player RegisterCallback failed!");
        return 0;
    }
    return 1;
}

void AudioRecorder::destroyEngine() {
    if (nullptr != recordObject) {
        (*recordObject)->Destroy(recordObject);
        recordObject = nullptr;
        bufferQueueItf = nullptr;
        recordItf = nullptr;
    }
    if (nullptr != engineObject) {
        (*engineObject)->Destroy(engineObject);
        engineObject = nullptr;
        engineItf = nullptr;
    }
}