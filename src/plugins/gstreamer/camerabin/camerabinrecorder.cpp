/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "camerabinrecorder.h"
#include "camerabincontrol.h"
#include "camerabinresourcepolicy.h"
#include "camerabinaudioencoder.h"
#include "camerabinvideoencoder.h"
#include "camerabincontainer.h"
#include <QtCore/QDebug>
#include <QtGui/qguiapplication.h>


QT_BEGIN_NAMESPACE

CameraBinRecorder::CameraBinRecorder(CameraBinSession *session)
    :QMediaRecorderControl(session),
     m_session(session),
     m_state(QMediaRecorder::StoppedState),
     m_status(QMediaRecorder::UnloadedStatus),
     m_busHelper(session->bus()),
     m_awaitingAudioBuffer(false),
     m_audioBufferProbe(this)
{
    connect(m_session, SIGNAL(statusChanged(QCamera::Status)), SLOT(updateStatus()));
    connect(m_session, SIGNAL(pendingStateChanged(QCamera::State)), SLOT(updateStatus()));
    connect(m_session, SIGNAL(busyChanged(bool)), SLOT(updateStatus()));

    connect(m_session, SIGNAL(durationChanged(qint64)), SIGNAL(durationChanged(qint64)));
    connect(m_session, SIGNAL(mutedChanged(bool)), this, SIGNAL(mutedChanged(bool)));
    connect(m_session->cameraControl()->resourcePolicy(), SIGNAL(canCaptureChanged()),
            this, SLOT(updateStatus()));

    m_busHelper->installMessageFilter(this);

    if (qApp) {
        connect(qApp, SIGNAL(applicationStateChanged(Qt::ApplicationState)),
                this, SLOT(handleApplicationStateChanged()));
    }
}

CameraBinRecorder::~CameraBinRecorder()
{
    if (m_busHelper)
        m_busHelper->removeMessageFilter(this);
}

QUrl CameraBinRecorder::outputLocation() const
{
    return m_session->outputLocation();
}

bool CameraBinRecorder::setOutputLocation(const QUrl &sink)
{
    m_session->setOutputLocation(sink);
    return true;
}

QMediaRecorder::State CameraBinRecorder::state() const
{
    return m_state;
}

QMediaRecorder::Status CameraBinRecorder::status() const
{
    return m_status;
}

void CameraBinRecorder::updateStatus()
{
    QCamera::Status sessionStatus = m_session->status();

    QMediaRecorder::State oldState = m_state;
    QMediaRecorder::Status oldStatus = m_status;

    if (sessionStatus == QCamera::ActiveStatus &&
            m_session->captureMode().testFlag(QCamera::CaptureVideo)) {

        if (!m_session->cameraControl()->resourcePolicy()->canCapture()) {
            m_status = QMediaRecorder::UnavailableStatus;
            m_state = QMediaRecorder::StoppedState;
            m_session->stopVideoRecording();
        } else  if (m_state == QMediaRecorder::RecordingState) {
            m_status = QMediaRecorder::RecordingStatus;
        } else {
            m_status = m_session->isBusy() ?
                        QMediaRecorder::FinalizingStatus :
                        QMediaRecorder::LoadedStatus;
        }
    } else {
        if (m_state == QMediaRecorder::RecordingState) {
            m_state = QMediaRecorder::StoppedState;
            m_session->stopVideoRecording();
        }
        m_status = m_session->pendingState() == QCamera::ActiveState
                    && m_session->captureMode().testFlag(QCamera::CaptureVideo)
                ? QMediaRecorder::LoadingStatus
                : QMediaRecorder::UnloadedStatus;
    }

    if (m_state != oldState)
        emit stateChanged(m_state);

    if (m_status != oldStatus)
        emit statusChanged(m_status);
}

qint64 CameraBinRecorder::duration() const
{
    return m_session->duration();
}


void CameraBinRecorder::applySettings()
{
#if QT_CONFIG(gstreamer_encodingprofiles)
    CameraBinContainer *containerControl = m_session->mediaContainerControl();
    CameraBinAudioEncoder *audioEncoderControl = m_session->audioEncodeControl();
    CameraBinVideoEncoder *videoEncoderControl = m_session->videoEncodeControl();

    containerControl->resetActualContainerFormat();
    audioEncoderControl->resetActualSettings();
    videoEncoderControl->resetActualSettings();

    //encodebin doesn't like the encoding profile with ANY caps,
    //if container and codecs are not specified,
    //try to find a commonly used supported combination
    if (containerControl->containerFormat().isEmpty() &&
            audioEncoderControl->audioSettings().codec().isEmpty() &&
            videoEncoderControl->videoSettings().codec().isEmpty()) {

        QList<QStringList> candidates;

        // By order of preference

        // .mp4 (h264, AAC)
        candidates.append(QStringList() << "video/quicktime, variant=(string)iso" << "video/x-h264" << "audio/mpeg, mpegversion=(int)4");

        // .mp4 (h264, AC3)
        candidates.append(QStringList() << "video/quicktime, variant=(string)iso" << "video/x-h264" << "audio/x-ac3");

        // .mp4 (h264, MP3)
        candidates.append(QStringList() << "video/quicktime, variant=(string)iso" << "video/x-h264" << "audio/mpeg, mpegversion=(int)1, layer=(int)3");

        // .mkv (h264, AAC)
        candidates.append(QStringList() << "video/x-matroska" << "video/x-h264" << "audio/mpeg, mpegversion=(int)4");

        // .mkv (h264, AC3)
        candidates.append(QStringList() << "video/x-matroska" << "video/x-h264" << "audio/x-ac3");

        // .mkv (h264, MP3)
        candidates.append(QStringList() << "video/x-matroska" << "video/x-h264" << "audio/mpeg, mpegversion=(int)1, layer=(int)3");

        // .mov (h264, AAC)
        candidates.append(QStringList() << "video/quicktime" << "video/x-h264" << "audio/mpeg, mpegversion=(int)4");

        // .mov (h264, MP3)
        candidates.append(QStringList() << "video/quicktime" << "video/x-h264" << "audio/mpeg, mpegversion=(int)1, layer=(int)3");

        // .webm (VP8, Vorbis)
        candidates.append(QStringList() << "video/webm" << "video/x-vp8" << "audio/x-vorbis");

        // .ogg (Theora, Vorbis)
        candidates.append(QStringList() << "application/ogg" << "video/x-theora" << "audio/x-vorbis");

        // .avi (DivX, MP3)
        candidates.append(QStringList() << "video/x-msvideo" << "video/x-divx" << "audio/mpeg, mpegversion=(int)1, layer=(int)3");

        for (const QStringList &candidate : qAsConst(candidates)) {
            if (containerControl->supportedContainers().contains(candidate[0]) &&
                    videoEncoderControl->supportedVideoCodecs().contains(candidate[1]) &&
                    audioEncoderControl->supportedAudioCodecs().contains(candidate[2])) {
                containerControl->setActualContainerFormat(candidate[0]);

                QVideoEncoderSettings videoSettings = videoEncoderControl->videoSettings();
                videoSettings.setCodec(candidate[1]);
                videoEncoderControl->setActualVideoSettings(videoSettings);

                QAudioEncoderSettings audioSettings = audioEncoderControl->audioSettings();
                audioSettings.setCodec(candidate[2]);
                audioEncoderControl->setActualAudioSettings(audioSettings);

                break;
            }
        }
    }
#endif
}

#if QT_CONFIG(gstreamer_encodingprofiles)

GstEncodingContainerProfile *CameraBinRecorder::videoProfile()
{
    GstEncodingContainerProfile *containerProfile = m_session->mediaContainerControl()->createProfile();

    if (containerProfile) {
        GstEncodingProfile *audioProfile = m_session->audioEncodeControl()->createProfile();
        GstEncodingProfile *videoProfile = m_session->videoEncodeControl()->createProfile();

        if (audioProfile) {
            if (!gst_encoding_container_profile_add_profile(containerProfile, audioProfile))
                gst_encoding_profile_unref(audioProfile);
        }
        if (videoProfile) {
            if (!gst_encoding_container_profile_add_profile(containerProfile, videoProfile))
                gst_encoding_profile_unref(videoProfile);
        }
    }

    return containerProfile;
}

#endif

void CameraBinRecorder::setState(QMediaRecorder::State state)
{
    if (m_state == state)
        return;

    QMediaRecorder::State oldState = m_state;
    QMediaRecorder::Status oldStatus = m_status;

    switch (state) {
    case QMediaRecorder::StoppedState:
        m_state = state;
        m_status = QMediaRecorder::FinalizingStatus;
        m_session->stopVideoRecording();
        stopAwaitForAudioBuffer();
        break;
    case QMediaRecorder::PausedState:
        emit error(QMediaRecorder::ResourceError, tr("QMediaRecorder::pause() is not supported by camerabin2."));
        break;
    case QMediaRecorder::RecordingState:

        if (m_session->status() != QCamera::ActiveStatus) {
            emit error(QMediaRecorder::ResourceError, tr("Service has not been started"));
        } else if (!m_session->cameraControl()->resourcePolicy()->canCapture()) {
            emit error(QMediaRecorder::ResourceError, tr("Recording permissions are not available"));
        } else {
            m_session->recordVideo();
            m_state = state;
            m_status = QMediaRecorder::RecordingStatus;
            emit actualLocationChanged(m_session->outputLocation());
            awaitForAudioBuffer();
        }
    }

    if (m_state != oldState)
        emit stateChanged(m_state);

    if (m_status != oldStatus)
        emit statusChanged(m_status);
}

bool CameraBinRecorder::isMuted() const
{
    return m_session->isMuted();
}

qreal CameraBinRecorder::volume() const
{
    return 1.0;
}

void CameraBinRecorder::setMuted(bool muted)
{
    m_session->setMuted(muted);
}

void CameraBinRecorder::setVolume(qreal volume)
{
    if (!qFuzzyCompare(volume, qreal(1.0)))
        qWarning() << "Media service doesn't support recorder audio gain.";
}

// This prefix is known, as camerabinsession doesn't set its own audio source,
// the name of `autoaudiosrc` created by `camerabin` is fixed, and the way
// `gstautodetect` creates this prefix is known.
#define AUDIO_SRC_PREFIX "audiosrc-actual-src-"

bool CameraBinRecorder::processBusMessage(const QGstreamerMessage &message)
{
    if (m_state == QMediaRecorder::StoppedState)
        return false;

    GstMessage* gm = message.rawMessage();

    if (!gm)
        return false;

    if (GST_MESSAGE_TYPE(gm) == GST_MESSAGE_ERROR) {
        GError *err;
        gchar *debug;
        gst_message_parse_error (gm, &err, &debug);

        // Catch errors from the audio source element. Camera-related or
        // camerabin's error will be caught in camerabinsession.cpp.
        if (err && err->message && strncmp(GST_OBJECT_NAME(GST_MESSAGE_SRC(gm)),
                AUDIO_SRC_PREFIX, sizeof(AUDIO_SRC_PREFIX) - 1) == 0) {
            qWarning() << "Error in audio recording:" << err->message;
            emit error(QMediaRecorder::ResourceError, tr("Error in audio recording."));
            setState(QMediaRecorder::StoppedState);
            // Reload the pipeline to make sure everything is working correctly.
            m_session->cameraControl()->reloadLater();
        }

        if (err)
            g_error_free (err);

        if (debug)
            g_free (debug);
    }

    return false;
}

void CameraBinRecorder::handleApplicationStateChanged() {
    // If we're recording, stop the recording if a) the application is not
    // active (app state can jumps from Active to Suspended) and b) we've seen
    // at least 1 audio buffer (i.e. we're not waiting for the trust prompt).

    // Later, when the app become active again, don't restart the recording
    // as it will confuse the user.

    Qt::ApplicationState appState = qApp
                                    ? qApp->applicationState()
                                    : Qt::ApplicationActive;

    if (appState != Qt::ApplicationActive
            && m_state != QMediaRecorder::StoppedState
            && !(m_awaitingAudioBuffer.load()))
        setState(QMediaRecorder::StoppedState);
}

// In order to know that an audio buffer has arrived, we have to attach a pad
// probe to the audio element's srcpad inside the CameraBin. Unfortunately,
// there's no clean way to do this other than looking up the element inside the
// CameraBin itself.

// Caller owns the reference of the returned pad.
GstPad * CameraBinRecorder::findAudioSrcPad() {
    GstBin * camera_bin = GST_BIN(m_session->cameraBin());
    if (!camera_bin)
        return nullptr;

    GstElement * audioSrc = gst_bin_get_by_name(camera_bin, "audiosrc");
    if (!audioSrc)
        return nullptr;

    GstPad * audioSrcPad = gst_element_get_static_pad(audioSrc, "src");
    gst_object_unref(audioSrc);
    return audioSrcPad;
}

void CameraBinRecorder::awaitForAudioBuffer() {
    GstPad * audioSrcPad = findAudioSrcPad();
    if (!audioSrcPad)
        // Probably because audio is not recorded.
        return;

    m_awaitingAudioBuffer.store(true);
    m_audioBufferProbe.addProbeToPad(audioSrcPad);
    gst_object_unref(audioSrcPad);
}

// This function can be called from both the probe and the setState() function.
void CameraBinRecorder::stopAwaitForAudioBuffer() {
    GstPad * audioSrcPad = findAudioSrcPad();
    if (!audioSrcPad)
        // Probably because audio is not recorded.
        return;

    m_audioBufferProbe.removeProbeFromPad(audioSrcPad);
    m_awaitingAudioBuffer.store(false);
    gst_object_unref(audioSrcPad);
}

bool CameraBinRecorder::AudioBufferProbe::probeBuffer(GstBuffer * buffer) {
    Q_UNUSED(buffer);
    recorder->stopAwaitForAudioBuffer();

    // If we receive a buffer while the app is inactive, stop the recording.
    // The permission is granted, but the user switched away before we knew it.
    // handleApplicationStateChanged() will re-read the app state and act
    // accordingly.
    // TODO: this is kind of racy. We might receive a buffer before Unity8
    // returns focus to the app.
    QMetaObject::invokeMethod(recorder, "handleApplicationStateChanged",
                                Qt::QueuedConnection);

    return true;
}

QT_END_NAMESPACE

