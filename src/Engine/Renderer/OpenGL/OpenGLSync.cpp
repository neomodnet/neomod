// Copyright (c) 2025, WH, All rights reserved.
#include "OpenGLSync.h"
#if defined(MCENGINE_FEATURE_OPENGL) || defined(MCENGINE_FEATURE_GLES32)
#include "OpenGLHeaders.h"

#include "ConVar.h"
#include "Engine.h"
#include "Logging.h"
#include "MakeDelegateWrapper.h"


void OpenGLSync::onFramecountNumChanged(float newValue) {
    int newInt = static_cast<int>(newValue);
    if(newInt > 0) {
        this->setMaxFramesInFlight(newInt);
    } else {
        cv::r_sync_max_frames.setValue(this->iMaxFramesInFlight, false);
    }
}

OpenGLSync::OpenGLSync() {
    this->iSyncFrameCount = 0;
    this->bEnabled = this->bAvailable = false;
    this->iMaxFramesInFlight = cv::r_sync_max_frames.getVal<int>();

#ifdef MCENGINE_PLATFORM_WASM
    this->bAvailable = false;
#else
    this->bAvailable = !!glClientWaitSync && !!glFenceSync && !!glGetSynciv;
#endif
    this->bEnabled = cv::r_sync_enabled.getBool() && this->bAvailable;

    cv::r_sync_max_frames.setCallback(SA::MakeDelegate<&OpenGLSync::onFramecountNumChanged>(this));
    cv::r_sync_enabled.setCallback(SA::MakeDelegate<&OpenGLSync::onSyncBehaviorChanged>(this));
}

OpenGLSync::~OpenGLSync() {
    if(this->bAvailable) {
        while(!this->frameSyncQueue.empty()) {
            this->deleteSyncObject(this->frameSyncQueue.front().syncObject);
            this->frameSyncQueue.pop_front();
        }
    }
}

void OpenGLSync::begin() {
    if(this->bEnabled) {
        // this will block and wait if we have too many frames in flight already
        this->manageFrameSyncQueue();
    }
}

void OpenGLSync::end() {
    if(this->bEnabled) {
        // create a new fence sync, to be signaled when current rendering commands complete on the GPU
        GLsync syncObj = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

        if(syncObj) {
            // add it to the queue
            FrameSyncPoint syncPoint;
            syncPoint.syncObject = syncObj;
            syncPoint.frameNumber = this->iSyncFrameCount++;

            this->frameSyncQueue.push_back(syncPoint);

            if(cv::r_sync_debug.getBool())
                debugLog("Created sync object for frame {:d} (frames in flight: {:d}/{:d})", syncPoint.frameNumber,
                         this->frameSyncQueue.size(), this->iMaxFramesInFlight);
        }
    }
}

void OpenGLSync::manageFrameSyncQueue(bool forceWait) {
    if(this->frameSyncQueue.empty()) return;

    const bool debug = cv::r_sync_debug.getBool();

    // check front of queue to see if it's complete
    bool needToWait = forceWait || (this->frameSyncQueue.size() >= this->iMaxFramesInFlight);

    // first, try a non-blocking check on the oldest sync object
    FrameSyncPoint &oldestSync = this->frameSyncQueue.front();
    GLint signaled = 0;

    GLsync sync = oldestSync.syncObject;
    glGetSynciv(sync, GL_SYNC_STATUS, sizeof(GLint), nullptr, &signaled);

    // if oldest frame is done, we can remove it (and any other completed frames)
    if(signaled == GL_SIGNALED) {
        logIf(debug, "Frame {:d} already completed (no wait needed)", oldestSync.frameNumber);

        this->deleteSyncObject(oldestSync.syncObject);
        this->frameSyncQueue.pop_front();

        // check if more frames are done
        while(!this->frameSyncQueue.empty()) {
            FrameSyncPoint &nextSync = this->frameSyncQueue.front();
            signaled = 0;

            sync = nextSync.syncObject;
            glGetSynciv(sync, GL_SYNC_STATUS, sizeof(GLint), nullptr, &signaled);

            if(signaled == GL_SIGNALED) {
                logIf(debug, "Frame {:d} also completed", nextSync.frameNumber);

                this->deleteSyncObject(nextSync.syncObject);
                this->frameSyncQueue.pop_front();
            } else {
                break;
            }
        }
    }
    // if we need to wait and the frame isn't done yet, do a blocking wait with timeout
    else if(needToWait) {
        logIf(debug, "Waiting for frame {:d} to complete (frames in flight: {:d}/{:d})", oldestSync.frameNumber,
              this->frameSyncQueue.size(), this->iMaxFramesInFlight);

        SYNC_RESULT result = this->waitForSyncObject(oldestSync.syncObject, cv::r_sync_timeout.getInt());

        if(debug) {
            switch(result) {
                case SYNC_OBJECT_NOT_READY:
                    debugLog("Frame {:d} sync object was not ready", oldestSync.frameNumber);
                    break;
                case SYNC_ALREADY_SIGNALED:
                    debugLog("Frame {:d} sync object was already signaled", oldestSync.frameNumber);
                    break;
                case SYNC_GPU_COMPLETED:
                    debugLog("Frame {:d} sync object was just completed", oldestSync.frameNumber);
                    break;
                case SYNC_TIMEOUT_EXPIRED:
                    debugLog("Frame {:d} sync object timed out after {:d} microseconds", oldestSync.frameNumber,
                             cv::r_sync_timeout.getInt());
                    break;
                case SYNC_WAIT_FAILED:
                    debugLog("Frame {:d} sync wait failed", oldestSync.frameNumber);
                    break;
            }
        }

        // always remove the sync object we waited for, even if it timed out (don't get stuck if the GPU falls far
        // behind)
        this->deleteSyncObject(oldestSync.syncObject);
        this->frameSyncQueue.pop_front();

        // check if other frames are done too
        if(result == SYNC_ALREADY_SIGNALED || result == SYNC_GPU_COMPLETED) {
            while(!this->frameSyncQueue.empty()) {
                FrameSyncPoint &nextSync = this->frameSyncQueue.front();
                signaled = 0;

                sync = nextSync.syncObject;
                glGetSynciv(sync, GL_SYNC_STATUS, sizeof(GLint), nullptr, &signaled);

                if(signaled == GL_SIGNALED) {
                    logIf(debug, "Frame {:d} also completed", nextSync.frameNumber);

                    this->deleteSyncObject(nextSync.syncObject);
                    this->frameSyncQueue.pop_front();
                } else {
                    break;
                }
            }
        }
    }
}

// wait for a sync object with timeout
OpenGLSync::SYNC_RESULT OpenGLSync::waitForSyncObject(GLsync syncObject, uint64_t timeoutNs) {
    if(!syncObject) return SYNC_OBJECT_NOT_READY;

    GLsync sync = syncObject;

    // do a non-blocking check if already signaled
    GLint signaled = 0;
    glGetSynciv(sync, GL_SYNC_STATUS, sizeof(GLint), nullptr, &signaled);

    if(signaled == GL_SIGNALED) return SYNC_ALREADY_SIGNALED;

    // wait with timeout
    GLenum result = glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, timeoutNs);

    switch(result) {
        case GL_ALREADY_SIGNALED:
            return SYNC_ALREADY_SIGNALED;
        case GL_TIMEOUT_EXPIRED:
            return SYNC_TIMEOUT_EXPIRED;
        case GL_WAIT_FAILED:
            return SYNC_WAIT_FAILED;
        case GL_CONDITION_SATISFIED:
            return SYNC_GPU_COMPLETED;
        default:
            return SYNC_WAIT_FAILED;
    }
}

void OpenGLSync::deleteSyncObject(GLsync syncObject) {
    if(syncObject) {
        glDeleteSync(syncObject);
    }
}

void OpenGLSync::setMaxFramesInFlight(int maxFrames) {
    this->iMaxFramesInFlight = maxFrames;

    // may need to wait on some frames if the limit is being reduced
    if(this->bEnabled && this->frameSyncQueue.size() > this->iMaxFramesInFlight) {
        logIfCV(r_sync_debug, "Max frames reduced to {:d}, waiting for excess frames", this->iMaxFramesInFlight);
        // wait
        this->manageFrameSyncQueue(true);
    }
}

#endif
