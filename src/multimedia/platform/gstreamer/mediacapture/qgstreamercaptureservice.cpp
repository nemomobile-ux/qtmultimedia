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

#include "qgstreamercaptureservice_p.h"
#include "qgstreamercapturesession_p.h"
#include "qgstreamerrecordercontrol_p.h"
#include "qgstreamermediacontainercontrol_p.h"
#include "qgstreameraudioencode_p.h"
#include "qgstreamervideoencode_p.h"
#include "qgstreamerimageencode_p.h"
#include "qgstreamercameracontrol_p.h"
#include <private/qgstreamerbushelper_p.h>
#include "qgstreamercapturemetadatacontrol_p.h"

#if defined(USE_GSTREAMER_CAMERA)
#include "qgstreamerv4l2input_p.h"
#endif

#include "qgstreamerimagecapturecontrol_p.h"
#include <private/qgstreamervideoinputdevicecontrol_p.h>
#include <private/qgstreameraudioprobecontrol_p.h>

#include <private/qgstreamervideorenderer_p.h>
#include <private/qgstreamervideowindow_p.h>

QT_BEGIN_NAMESPACE

QGstreamerCaptureService::QGstreamerCaptureService(QMediaRecorder::CaptureMode mode)
    : QMediaPlatformCaptureInterface()
    , m_captureSession(0)
    , m_cameraControl(0)
#if defined(USE_GSTREAMER_CAMERA)
    , m_videoInput(0)
#endif
    , m_metaDataControl(0)
    , m_videoInputDevice(0)
    , m_videoOutput(0)
    , m_videoRenderer(0)
    , m_videoWindow(0)
    , m_imageCaptureControl(0)
    , m_audioProbeControl(0)
{
    if (mode == QMediaRecorder::AudioOnly) {
        m_captureSession = new QGstreamerCaptureSession(QGstreamerCaptureSession::Audio, this);
    }

#if defined(USE_GSTREAMER_CAMERA)
   else {
        m_captureSession = new QGstreamerCaptureSession(QGstreamerCaptureSession::AudioAndVideo, this);
        m_cameraControl = new QGstreamerCameraControl(m_captureSession);
        m_videoInput = new QGstreamerV4L2Input(this);
        m_captureSession->setVideoInput(m_videoInput);
        m_videoInputDevice = new QGstreamerVideoInputDeviceControl(this);

        connect(m_videoInputDevice, SIGNAL(selectedDeviceChanged(QString)),
                m_videoInput, SLOT(setDevice(QString)));

        if (m_videoInputDevice->deviceCount())
            m_videoInput->setDevice(m_videoInputDevice->deviceName(m_videoInputDevice->selectedDevice()));

        m_videoRenderer = new QGstreamerVideoRenderer(this);

        m_videoWindow = new QGstreamerVideoWindow(this);
        // If the GStreamer video sink is not available, don't provide the video window control since
        // it won't work anyway.
        if (!m_videoWindow->videoSink()) {
            delete m_videoWindow;
            m_videoWindow = 0;
        }

        m_imageCaptureControl = new QGstreamerImageCaptureControl(m_captureSession);
    }
#endif

    m_metaDataControl = new QGstreamerCaptureMetaDataControl(this);
    connect(m_metaDataControl, SIGNAL(metaDataChanged(QMap<QByteArray,QVariant>)),
            m_captureSession, SLOT(setMetaData(QMap<QByteArray,QVariant>)));
}

QGstreamerCaptureService::~QGstreamerCaptureService()
{
}

QObject *QGstreamerCaptureService::requestControl(const char *name)
{
    if (!m_captureSession)
        return 0;

    if (qstrcmp(name,QVideoDeviceSelectorControl_iid) == 0)
        return m_videoInputDevice;

    if (qstrcmp(name,QMediaRecorderControl_iid) == 0)
        return m_captureSession->recorderControl();

    if (qstrcmp(name,QAudioEncoderSettingsControl_iid) == 0)
        return m_captureSession->audioEncodeControl();

    if (qstrcmp(name,QVideoEncoderSettingsControl_iid) == 0)
        return m_captureSession->videoEncodeControl();

    if (qstrcmp(name,QImageEncoderControl_iid) == 0)
        return m_captureSession->imageEncodeControl();


    if (qstrcmp(name,QMediaContainerControl_iid) == 0)
        return m_captureSession->mediaContainerControl();

    if (qstrcmp(name,QCameraControl_iid) == 0)
        return m_cameraControl;

    if (qstrcmp(name,QMetaDataWriterControl_iid) == 0)
        return m_metaDataControl;

    if (qstrcmp(name, QCameraImageCaptureControl_iid) == 0)
        return m_imageCaptureControl;

    if (qstrcmp(name,QMediaAudioProbeControl_iid) == 0) {
        if (!m_audioProbeControl) {
            m_audioProbeControl = new QGstreamerAudioProbeControl(this);
            m_captureSession->addProbe(m_audioProbeControl);
        }
        m_audioProbeControl->ref.ref();
        return m_audioProbeControl;
    }

    if (!m_videoOutput) {
        if (qstrcmp(name, QVideoRendererControl_iid) == 0) {
            m_videoOutput = m_videoRenderer;
        } else if (qstrcmp(name, QVideoWindowControl_iid) == 0) {
            m_videoOutput = m_videoWindow;
        }

        if (m_videoOutput) {
            m_captureSession->setVideoPreview(m_videoOutput);
            return m_videoOutput;
        }
    }

    return 0;
}

void QGstreamerCaptureService::releaseControl(QObject *control)
{
    if (!control) {
        return;
    } else if (control == m_videoOutput) {
        m_videoOutput = 0;
        m_captureSession->setVideoPreview(0);
    } else if (control == m_audioProbeControl && !m_audioProbeControl->ref.deref()) {
        m_captureSession->removeProbe(m_audioProbeControl);
        delete m_audioProbeControl;
        m_audioProbeControl = 0;
    }
}

QT_END_NAMESPACE
