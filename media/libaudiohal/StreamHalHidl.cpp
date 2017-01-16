/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <time.h>

#define LOG_TAG "StreamHalHidl"
//#define LOG_NDEBUG 0

#include <android/hardware/audio/2.0/IStreamOutCallback.h>
#include <utils/Log.h>

#include "DeviceHalHidl.h"
#include "EffectHalHidl.h"
#include "StreamHalHidl.h"

using ::android::hardware::audio::common::V2_0::AudioChannelMask;
using ::android::hardware::audio::common::V2_0::AudioFormat;
using ::android::hardware::audio::V2_0::AudioDrain;
using ::android::hardware::audio::V2_0::IStreamOutCallback;
using ::android::hardware::audio::V2_0::MessageQueueFlagBits;
using ::android::hardware::audio::V2_0::MmapBufferInfo;
using ::android::hardware::audio::V2_0::MmapPosition;
using ::android::hardware::audio::V2_0::ParameterValue;
using ::android::hardware::audio::V2_0::Result;
using ::android::hardware::audio::V2_0::ThreadPriority;
using ::android::hardware::audio::V2_0::TimeSpec;
using ::android::hardware::MQDescriptorSync;
using ::android::hardware::Return;
using ::android::hardware::Void;

namespace android {

StreamHalHidl::StreamHalHidl(IStream *stream)
        : ConversionHelperHidl("Stream"),
          mHalThreadPriority(static_cast<int>(ThreadPriority::NORMAL)),
          mStream(stream) {
}

StreamHalHidl::~StreamHalHidl() {
    mStream = nullptr;
}

status_t StreamHalHidl::getSampleRate(uint32_t *rate) {
    if (!mStream) return NO_INIT;
    return processReturn("getSampleRate", mStream->getSampleRate(), rate);
}

status_t StreamHalHidl::getBufferSize(size_t *size) {
    if (!mStream) return NO_INIT;
    return processReturn("getBufferSize", mStream->getBufferSize(), size);
}

status_t StreamHalHidl::getChannelMask(audio_channel_mask_t *mask) {
    if (!mStream) return NO_INIT;
    return processReturn("getChannelMask", mStream->getChannelMask(), mask);
}

status_t StreamHalHidl::getFormat(audio_format_t *format) {
    if (!mStream) return NO_INIT;
    return processReturn("getFormat", mStream->getFormat(), format);
}

status_t StreamHalHidl::getAudioProperties(
        uint32_t *sampleRate, audio_channel_mask_t *mask, audio_format_t *format) {
    if (!mStream) return NO_INIT;
    Return<void> ret = mStream->getAudioProperties(
            [&](uint32_t sr, AudioChannelMask m, AudioFormat f) {
                *sampleRate = sr;
                *mask = static_cast<audio_channel_mask_t>(m);
                *format = static_cast<audio_format_t>(f);
            });
    return processReturn("getAudioProperties", ret);
}

status_t StreamHalHidl::setParameters(const String8& kvPairs) {
    if (!mStream) return NO_INIT;
    hidl_vec<ParameterValue> hidlParams;
    status_t status = parametersFromHal(kvPairs, &hidlParams);
    if (status != OK) return status;
    return processReturn("setParameters", mStream->setParameters(hidlParams));
}

status_t StreamHalHidl::getParameters(const String8& keys, String8 *values) {
    values->clear();
    if (!mStream) return NO_INIT;
    hidl_vec<hidl_string> hidlKeys;
    status_t status = keysFromHal(keys, &hidlKeys);
    if (status != OK) return status;
    Result retval;
    Return<void> ret = mStream->getParameters(
            hidlKeys,
            [&](Result r, const hidl_vec<ParameterValue>& parameters) {
                retval = r;
                if (retval == Result::OK) {
                    parametersToHal(parameters, values);
                }
            });
    return processReturn("getParameters", ret, retval);
}

status_t StreamHalHidl::addEffect(sp<EffectHalInterface> effect) {
    if (!mStream) return NO_INIT;
    return processReturn("addEffect", mStream->addEffect(
                    static_cast<EffectHalHidl*>(effect.get())->effectId()));
}

status_t StreamHalHidl::removeEffect(sp<EffectHalInterface> effect) {
    if (!mStream) return NO_INIT;
    return processReturn("removeEffect", mStream->removeEffect(
                    static_cast<EffectHalHidl*>(effect.get())->effectId()));
}

status_t StreamHalHidl::standby() {
    if (!mStream) return NO_INIT;
    return processReturn("standby", mStream->standby());
}

status_t StreamHalHidl::dump(int fd) {
    if (!mStream) return NO_INIT;
    native_handle_t* hidlHandle = native_handle_create(1, 0);
    hidlHandle->data[0] = fd;
    Return<void> ret = mStream->debugDump(hidlHandle);
    native_handle_delete(hidlHandle);
    return processReturn("dump", ret);
}

status_t StreamHalHidl::start() {
    if (!mStream) return NO_INIT;
    return processReturn("start", mStream->start());
}

status_t StreamHalHidl::stop() {
    if (!mStream) return NO_INIT;
    return processReturn("stop", mStream->stop());
}

status_t StreamHalHidl::createMmapBuffer(int32_t minSizeFrames,
                                  struct audio_mmap_buffer_info *info) {
    Result retval;
    Return<void> ret = mStream->createMmapBuffer(
            minSizeFrames,
            [&](Result r, const MmapBufferInfo& hidlInfo) {
                retval = r;
                if (retval == Result::OK) {
                    const native_handle *handle = hidlInfo.sharedMemory.handle();
                    if (handle->numFds > 0) {
                        info->shared_memory_fd = dup(handle->data[0]);
                        info->buffer_size_frames = hidlInfo.bufferSizeFrames;
                        info->burst_size_frames = hidlInfo.burstSizeFrames;
                        // info->shared_memory_address is not needed in HIDL context
                        info->shared_memory_address = NULL;
                    } else {
                        retval = Result::NOT_INITIALIZED;
                    }
                }
            });
    return processReturn("createMmapBuffer", ret, retval);
}

status_t StreamHalHidl::getMmapPosition(struct audio_mmap_position *position) {
    Result retval;
    Return<void> ret = mStream->getMmapPosition(
            [&](Result r, const MmapPosition& hidlPosition) {
                retval = r;
                if (retval == Result::OK) {
                    position->time_nanoseconds = hidlPosition.timeNanoseconds;
                    position->position_frames = hidlPosition.positionFrames;
                }
            });
    return processReturn("getMmapPosition", ret, retval);
}

status_t StreamHalHidl::setHalThreadPriority(int priority) {
    mHalThreadPriority = priority;
    return OK;
}

namespace {

/* Notes on callback ownership.

This is how (Hw)Binder ownership model looks like. The server implementation
is owned by Binder framework (via sp<>). Proxies are owned by clients.
When the last proxy disappears, Binder framework releases the server impl.

Thus, it is not needed to keep any references to StreamOutCallback (this is
the server impl) -- it will live as long as HAL server holds a strong ref to
IStreamOutCallback proxy. We clear that reference by calling 'clearCallback'
from the destructor of StreamOutHalHidl.

The callback only keeps a weak reference to the stream. The stream is owned
by AudioFlinger.

*/

struct StreamOutCallback : public IStreamOutCallback {
    StreamOutCallback(const wp<StreamOutHalHidl>& stream) : mStream(stream) {}

    // IStreamOutCallback implementation
    Return<void> onWriteReady()  override {
        sp<StreamOutHalHidl> stream = mStream.promote();
        if (stream != 0) {
            stream->onWriteReady();
        }
        return Void();
    }

    Return<void> onDrainReady()  override {
        sp<StreamOutHalHidl> stream = mStream.promote();
        if (stream != 0) {
            stream->onDrainReady();
        }
        return Void();
    }

    Return<void> onError()  override {
        sp<StreamOutHalHidl> stream = mStream.promote();
        if (stream != 0) {
            stream->onError();
        }
        return Void();
    }

  private:
    wp<StreamOutHalHidl> mStream;
};

}  // namespace

StreamOutHalHidl::StreamOutHalHidl(const sp<IStreamOut>& stream)
        : StreamHalHidl(stream.get()), mStream(stream), mEfGroup(nullptr),
          mGetPresentationPositionNotSupported(false), mPPosFromWrite{ 0, OK, 0, { 0, 0 } } {
}

StreamOutHalHidl::~StreamOutHalHidl() {
    if (mStream != 0) {
        if (mCallback.unsafe_get()) {
            processReturn("clearCallback", mStream->clearCallback());
        }
        processReturn("close", mStream->close());
    }
    mCallback.clear();
    if (mEfGroup) {
        EventFlag::deleteEventFlag(&mEfGroup);
    }
}

status_t StreamOutHalHidl::getFrameSize(size_t *size) {
    if (mStream == 0) return NO_INIT;
    return processReturn("getFrameSize", mStream->getFrameSize(), size);
}

status_t StreamOutHalHidl::getLatency(uint32_t *latency) {
    if (mStream == 0) return NO_INIT;
    return processReturn("getLatency", mStream->getLatency(), latency);
}

status_t StreamOutHalHidl::setVolume(float left, float right) {
    if (mStream == 0) return NO_INIT;
    return processReturn("setVolume", mStream->setVolume(left, right));
}

status_t StreamOutHalHidl::write(const void *buffer, size_t bytes, size_t *written) {
    if (mStream == 0) return NO_INIT;
    *written = 0;

    if (bytes == 0 && !mDataMQ) {
        // Can't determine the size for the MQ buffer. Wait for a non-empty write request.
        ALOGW_IF(mCallback.unsafe_get(), "First call to async write with 0 bytes");
        return OK;
    }

    status_t status;
    if (!mDataMQ && (status = prepareForWriting(bytes)) != OK) {
        return status;
    }

    const size_t availBytes = mDataMQ->availableToWrite();
    if (bytes > availBytes) { bytes = availBytes; }
    if (!mDataMQ->write(static_cast<const uint8_t*>(buffer), bytes)) {
        ALOGW("data message queue write failed");
    }
    mEfGroup->wake(static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY));

    // TODO: Remove manual event flag handling once blocking MQ is implemented. b/33815422
    uint32_t efState = 0;
retry:
    status_t ret = mEfGroup->wait(
            static_cast<uint32_t>(MessageQueueFlagBits::NOT_FULL), &efState, NS_PER_SEC);
    if (efState & static_cast<uint32_t>(MessageQueueFlagBits::NOT_FULL)) {
        WriteStatus writeStatus =
                { Result::NOT_INITIALIZED, 0, Result::NOT_INITIALIZED, 0, { 0, 0 } };
        mStatusMQ->read(&writeStatus);
        if (writeStatus.writeRetval == Result::OK) {
            status = OK;
            *written = writeStatus.written;
            mPPosFromWrite.status = processReturn(
                    "get_presentation_position", writeStatus.presentationPositionRetval);
            if (mPPosFromWrite.status == OK) {
                mPPosFromWrite.frames = writeStatus.frames;
                mPPosFromWrite.ts.tv_sec = writeStatus.timeStamp.tvSec;
                mPPosFromWrite.ts.tv_nsec = writeStatus.timeStamp.tvNSec;
            }
            mPPosFromWrite.obtained = getCurrentTimeMs();
        } else {
            status = processReturn("write", writeStatus.writeRetval);
        }
        return status;
    }
    if (ret == -EAGAIN) {
        // This normally retries no more than once.
        goto retry;
    }
    return ret;
}

uint64_t StreamOutHalHidl::getCurrentTimeMs() {
    struct timespec timeNow;
    clock_gettime(CLOCK_MONOTONIC, &timeNow);
    return timeNow.tv_sec * 1000000 + timeNow.tv_nsec / 1000;
}

status_t StreamOutHalHidl::prepareForWriting(size_t bufferSize) {
    std::unique_ptr<DataMQ> tempDataMQ;
    std::unique_ptr<StatusMQ> tempStatusMQ;
    Result retval;
    Return<void> ret = mStream->prepareForWriting(
            1, bufferSize, ThreadPriority(mHalThreadPriority),
            [&](Result r,
                    const MQDescriptorSync<uint8_t>& dataMQ,
                    const MQDescriptorSync<WriteStatus>& statusMQ) {
                retval = r;
                if (retval == Result::OK) {
                    tempDataMQ.reset(new DataMQ(dataMQ));
                    tempStatusMQ.reset(new StatusMQ(statusMQ));
                    if (tempDataMQ->isValid() && tempDataMQ->getEventFlagWord()) {
                        EventFlag::createEventFlag(tempDataMQ->getEventFlagWord(), &mEfGroup);
                    }
                }
            });
    if (!ret.isOk() || retval != Result::OK) {
        return processReturn("prepareForWriting", ret, retval);
    }
    if (!tempDataMQ || !tempDataMQ->isValid() || !tempStatusMQ || !tempStatusMQ->isValid()
        || !mEfGroup) {
        ALOGE_IF(!tempDataMQ, "Failed to obtain data message queue for writing");
        ALOGE_IF(tempDataMQ && !tempDataMQ->isValid(), "Data message queue for writing is invalid");
        ALOGE_IF(!tempStatusMQ, "Failed to obtain status message queue for writing");
        ALOGE_IF(tempStatusMQ && !tempStatusMQ->isValid(),
                "Status message queue for writing is invalid");
        ALOGE_IF(!mEfGroup, "Event flag creation for writing failed");
        return NO_INIT;
    }
    mDataMQ = std::move(tempDataMQ);
    mStatusMQ = std::move(tempStatusMQ);
    return OK;
}

status_t StreamOutHalHidl::getRenderPosition(uint32_t *dspFrames) {
    if (mStream == 0) return NO_INIT;
    Result retval;
    Return<void> ret = mStream->getRenderPosition(
            [&](Result r, uint32_t d) {
                retval = r;
                if (retval == Result::OK) {
                    *dspFrames = d;
                }
            });
    return processReturn("getRenderPosition", ret, retval);
}

status_t StreamOutHalHidl::getNextWriteTimestamp(int64_t *timestamp) {
    if (mStream == 0) return NO_INIT;
    Result retval;
    Return<void> ret = mStream->getNextWriteTimestamp(
            [&](Result r, int64_t t) {
                retval = r;
                if (retval == Result::OK) {
                    *timestamp = t;
                }
            });
    return processReturn("getRenderPosition", ret, retval);
}

status_t StreamOutHalHidl::setCallback(wp<StreamOutHalInterfaceCallback> callback) {
    if (mStream == 0) return NO_INIT;
    status_t status = processReturn(
            "setCallback", mStream->setCallback(new StreamOutCallback(this)));
    if (status == OK) {
        mCallback = callback;
    }
    return status;
}

status_t StreamOutHalHidl::supportsPauseAndResume(bool *supportsPause, bool *supportsResume) {
    if (mStream == 0) return NO_INIT;
    Return<void> ret = mStream->supportsPauseAndResume(
            [&](bool p, bool r) {
                *supportsPause = p;
                *supportsResume = r;
            });
    return processReturn("supportsPauseAndResume", ret);
}

status_t StreamOutHalHidl::pause() {
    if (mStream == 0) return NO_INIT;
    return processReturn("pause", mStream->pause());
}

status_t StreamOutHalHidl::resume() {
    if (mStream == 0) return NO_INIT;
    return processReturn("pause", mStream->resume());
}

status_t StreamOutHalHidl::supportsDrain(bool *supportsDrain) {
    if (mStream == 0) return NO_INIT;
    return processReturn("supportsDrain", mStream->supportsDrain(), supportsDrain);
}

status_t StreamOutHalHidl::drain(bool earlyNotify) {
    if (mStream == 0) return NO_INIT;
    return processReturn(
            "drain", mStream->drain(earlyNotify ? AudioDrain::EARLY_NOTIFY : AudioDrain::ALL));
}

status_t StreamOutHalHidl::flush() {
    if (mStream == 0) return NO_INIT;
    return processReturn("pause", mStream->flush());
}

status_t StreamOutHalHidl::getPresentationPosition(uint64_t *frames, struct timespec *timestamp) {
    if (mStream == 0) return NO_INIT;
    if (mGetPresentationPositionNotSupported) return INVALID_OPERATION;
    if (getCurrentTimeMs() - mPPosFromWrite.obtained <= 1000) {
        // No more than 1 ms passed since the last write, use cached result to avoid binder calls.
        if (mPPosFromWrite.status == OK) {
            *frames = mPPosFromWrite.frames;
            timestamp->tv_sec = mPPosFromWrite.ts.tv_sec;
            timestamp->tv_nsec = mPPosFromWrite.ts.tv_nsec;
        }
        return mPPosFromWrite.status;
    }

    Result retval;
    Return<void> ret = mStream->getPresentationPosition(
            [&](Result r, uint64_t hidlFrames, const TimeSpec& hidlTimeStamp) {
                retval = r;
                if (retval == Result::OK) {
                    *frames = hidlFrames;
                    timestamp->tv_sec = hidlTimeStamp.tvSec;
                    timestamp->tv_nsec = hidlTimeStamp.tvNSec;
                }
            });
    if (ret.isOk() && retval == Result::NOT_SUPPORTED) {
        mGetPresentationPositionNotSupported = true;
    }
    return processReturn("getPresentationPosition", ret, retval);
}

void StreamOutHalHidl::onWriteReady() {
    sp<StreamOutHalInterfaceCallback> callback = mCallback.promote();
    if (callback == 0) return;
    ALOGV("asyncCallback onWriteReady");
    callback->onWriteReady();
}

void StreamOutHalHidl::onDrainReady() {
    sp<StreamOutHalInterfaceCallback> callback = mCallback.promote();
    if (callback == 0) return;
    ALOGV("asyncCallback onDrainReady");
    callback->onDrainReady();
}

void StreamOutHalHidl::onError() {
    sp<StreamOutHalInterfaceCallback> callback = mCallback.promote();
    if (callback == 0) return;
    ALOGV("asyncCallback onError");
    callback->onError();
}


StreamInHalHidl::StreamInHalHidl(const sp<IStreamIn>& stream)
        : StreamHalHidl(stream.get()), mStream(stream), mEfGroup(nullptr) {
}

StreamInHalHidl::~StreamInHalHidl() {
    if (mStream != 0) {
        processReturn("close", mStream->close());
    }
    if (mEfGroup) {
        EventFlag::deleteEventFlag(&mEfGroup);
    }
}

status_t StreamInHalHidl::getFrameSize(size_t *size) {
    if (mStream == 0) return NO_INIT;
    return processReturn("getFrameSize", mStream->getFrameSize(), size);
}

status_t StreamInHalHidl::setGain(float gain) {
    if (mStream == 0) return NO_INIT;
    return processReturn("setGain", mStream->setGain(gain));
}

status_t StreamInHalHidl::read(void *buffer, size_t bytes, size_t *read) {
    if (mStream == 0) return NO_INIT;
    *read = 0;

    if (bytes == 0 && !mDataMQ) {
        // Can't determine the size for the MQ buffer. Wait for a non-empty read request.
        return OK;
    }

    status_t status;
    if (!mDataMQ) {
        if ((status = prepareForReading(bytes)) != OK) return status;
        // Trigger the first read.
        mEfGroup->wake(static_cast<uint32_t>(MessageQueueFlagBits::NOT_FULL));
    }

    // TODO: Remove manual event flag handling once blocking MQ is implemented. b/33815422
    uint32_t efState = 0;
retry:
    status_t ret = mEfGroup->wait(
            static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY), &efState, NS_PER_SEC);
    if (efState & static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY)) {
        ReadStatus readStatus = { Result::NOT_INITIALIZED, 0 };
        const size_t availToRead = mDataMQ->availableToRead();
        if (bytes > availToRead) { bytes = availToRead; }
        mDataMQ->read(static_cast<uint8_t*>(buffer), bytes);
        mStatusMQ->read(&readStatus);
        mEfGroup->wake(static_cast<uint32_t>(MessageQueueFlagBits::NOT_FULL));
        if (readStatus.retval == Result::OK) {
            ALOGW_IF(availToRead != readStatus.read,
                    "HAL read report inconsistent: mq = %d, status = %d",
                    (int32_t)availToRead, (int32_t)readStatus.read);
            *read = readStatus.read;
        } else {
            status = processReturn("read", readStatus.retval);
        }
        return status;
    }
    if (ret == -EAGAIN) {
        // This normally retries no more than once.
        goto retry;
    }
    return ret;
}

status_t StreamInHalHidl::prepareForReading(size_t bufferSize) {
    std::unique_ptr<DataMQ> tempDataMQ;
    std::unique_ptr<StatusMQ> tempStatusMQ;
    Result retval;
    Return<void> ret = mStream->prepareForReading(
            1, bufferSize, ThreadPriority(mHalThreadPriority),
            [&](Result r,
                    const MQDescriptorSync<uint8_t>& dataMQ,
                    const MQDescriptorSync<ReadStatus>& statusMQ) {
                retval = r;
                if (retval == Result::OK) {
                    tempDataMQ.reset(new DataMQ(dataMQ));
                    tempStatusMQ.reset(new StatusMQ(statusMQ));
                    if (tempDataMQ->isValid() && tempDataMQ->getEventFlagWord()) {
                        EventFlag::createEventFlag(tempDataMQ->getEventFlagWord(), &mEfGroup);
                    }
                }
            });
    if (!ret.isOk() || retval != Result::OK) {
        return processReturn("prepareForReading", ret, retval);
    }
    if (!tempDataMQ || !tempDataMQ->isValid() || !tempStatusMQ || !tempStatusMQ->isValid()
        || !mEfGroup) {
        ALOGE_IF(!tempDataMQ, "Failed to obtain data message queue for reading");
        ALOGE_IF(tempDataMQ && !tempDataMQ->isValid(), "Data message queue for reading is invalid");
        ALOGE_IF(!tempStatusMQ, "Failed to obtain status message queue for reading");
        ALOGE_IF(tempStatusMQ && !tempStatusMQ->isValid(),
                "Status message queue for reading is invalid");
        ALOGE_IF(!mEfGroup, "Event flag creation for reading failed");
        return NO_INIT;
    }
    mDataMQ = std::move(tempDataMQ);
    mStatusMQ = std::move(tempStatusMQ);
    return OK;
}

status_t StreamInHalHidl::getInputFramesLost(uint32_t *framesLost) {
    if (mStream == 0) return NO_INIT;
    return processReturn("getInputFramesLost", mStream->getInputFramesLost(), framesLost);
}

status_t StreamInHalHidl::getCapturePosition(int64_t *frames, int64_t *time) {
    if (mStream == 0) return NO_INIT;
    Result retval;
    Return<void> ret = mStream->getCapturePosition(
            [&](Result r, uint64_t hidlFrames, uint64_t hidlTime) {
                retval = r;
                if (retval == Result::OK) {
                    *frames = hidlFrames;
                    *time = hidlTime;
                }
            });
    return processReturn("getCapturePosition", ret, retval);
}

} // namespace android