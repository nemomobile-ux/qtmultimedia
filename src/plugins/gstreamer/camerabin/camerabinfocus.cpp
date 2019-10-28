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

#include "camerabinfocus.h"
#include "camerabinsession.h"

#include <gst/interfaces/photography.h>

#include <QDebug>
#include <QtCore/qcoreevent.h>
#include <QtCore/qmetaobject.h>

#include <private/qgstutils_p.h>

#if !GST_CHECK_VERSION(1,0,0)
typedef GstFocusMode GstPhotographyFocusMode;
#endif

//#define CAMERABIN_DEBUG 1

QT_BEGIN_NAMESPACE

CameraBinFocus::CameraBinFocus(CameraBinSession *session)
    :QCameraFocusControl(session),
#if GST_CHECK_VERSION(1,0,0)
     QGstreamerBufferProbe(ProbeBuffers),
#endif
     m_session(session),
     m_cameraStatus(QCamera::UnloadedStatus),
     m_focusMode(QCameraFocus::AutoFocus),
     m_focusPointMode(QCameraFocus::FocusPointAuto),
     m_focusStatus(QCamera::Unlocked),
     m_focusZoneStatus(QCameraFocusZone::Selected),
     m_focusPoint(0.5, 0.5),
     m_focusRectSize(0.3, 0.3)
{
    gst_photography_set_focus_mode(m_session->photography(), GST_PHOTOGRAPHY_FOCUS_MODE_AUTO);

    connect(m_session, SIGNAL(statusChanged(QCamera::Status)),
            this, SLOT(_q_handleCameraStatusChange(QCamera::Status)));
}

CameraBinFocus::~CameraBinFocus()
{
}

QCameraFocus::FocusModes CameraBinFocus::focusMode() const
{
    return m_focusMode;
}

void CameraBinFocus::setFocusMode(QCameraFocus::FocusModes mode)
{
    GstPhotographyFocusMode photographyMode;

    switch (mode) {
    case QCameraFocus::AutoFocus:
        photographyMode = GST_PHOTOGRAPHY_FOCUS_MODE_AUTO;
        break;
    case QCameraFocus::HyperfocalFocus:
        photographyMode = GST_PHOTOGRAPHY_FOCUS_MODE_HYPERFOCAL;
        break;
    case QCameraFocus::InfinityFocus:
        photographyMode = GST_PHOTOGRAPHY_FOCUS_MODE_INFINITY;
        break;
    case QCameraFocus::ContinuousFocus:
        photographyMode = GST_PHOTOGRAPHY_FOCUS_MODE_CONTINUOUS_NORMAL;
        break;
    case QCameraFocus::MacroFocus:
        photographyMode = GST_PHOTOGRAPHY_FOCUS_MODE_MACRO;
        break;
    default:
        if (mode & QCameraFocus::AutoFocus) {
            photographyMode = GST_PHOTOGRAPHY_FOCUS_MODE_AUTO;
            break;
        } else {
            return;
        }
    }

    if (gst_photography_set_focus_mode(m_session->photography(), photographyMode))
        m_focusMode = mode;
}

bool CameraBinFocus::isFocusModeSupported(QCameraFocus::FocusModes mode) const
{
    switch (mode) {
    case QCameraFocus::AutoFocus:
    case QCameraFocus::HyperfocalFocus:
    case QCameraFocus::InfinityFocus:
    case QCameraFocus::ContinuousFocus:
    case QCameraFocus::MacroFocus:
        return true;
    default:
        return mode & QCameraFocus::AutoFocus;
    }
}

QCameraFocus::FocusPointMode CameraBinFocus::focusPointMode() const
{
    return m_focusPointMode;
}

void CameraBinFocus::setFocusPointMode(QCameraFocus::FocusPointMode mode)
{
    GstElement *source = m_session->cameraSource();

    if (m_focusPointMode == mode || !source)
        return;

    if (!isFocusPointModeSupported(mode))
        return;

#if GST_CHECK_VERSION(1,0,0)
    if (m_focusPointMode == QCameraFocus::FocusPointFaceDetection) {
        g_object_set (G_OBJECT(source), "detect-faces", FALSE, NULL);

        if (GstPad *pad = gst_element_get_static_pad(source, "vfsrc")) {
            removeProbeFromPad(pad);
            gst_object_unref(GST_OBJECT(pad));
        }

        m_faceResetTimer.stop();
        m_faceFocusRects.clear();

        QMutexLocker locker(&m_mutex);
        m_faces.clear();
    } else if (mode == QCameraFocus::FocusPointFaceDetection) {
        GstPad *pad = gst_element_get_static_pad(source, "vfsrc");
        if (!pad)
            return;

        addProbeToPad(pad);
        g_object_set (G_OBJECT(source), "detect-faces", TRUE, NULL);
        gst_object_unref(GST_OBJECT(pad));
    }
#endif

    m_focusPointMode = mode;
    updateRegionOfInterest();

    emit focusPointModeChanged(m_focusPointMode);
    emit focusZonesChanged();
}

bool CameraBinFocus::isFocusPointModeSupported(QCameraFocus::FocusPointMode mode) const
{
    switch (mode) {
    case QCameraFocus::FocusPointAuto:
    case QCameraFocus::FocusPointCustom:
        return true;
#if GST_CHECK_VERSION(1,0,0)
    case QCameraFocus::FocusPointFaceDetection:
        if (GstElement *source = m_session->cameraSource())
            return g_object_class_find_property(G_OBJECT_GET_CLASS(source), "detect-faces");
        return false;
#endif
    default:
        return false;
    }
}

QPointF CameraBinFocus::customFocusPoint() const
{
    return m_focusPoint;
}

void CameraBinFocus::setCustomFocusPoint(const QPointF &point)
{
    if (m_focusPoint != point) {
        m_focusPoint = point;

        // Bound the focus point so the focus rect remains entirely within the unit square.
        m_focusPoint.setX(qBound(m_focusRectSize.width() / 2, m_focusPoint.x(), 1 - m_focusRectSize.width() / 2));
        m_focusPoint.setY(qBound(m_focusRectSize.height() / 2, m_focusPoint.y(), 1 - m_focusRectSize.height() / 2));

        if (m_focusPointMode == QCameraFocus::FocusPointCustom) {
            updateRegionOfInterest();
            emit focusZonesChanged();
        }

        emit customFocusPointChanged(m_focusPoint);
    }
}

static QRectF rectFromCenterPoint(QPointF point, QSizeF size) {
    QRectF rect = QRectF(QPointF(0, 0), size);
    rect.moveCenter(point);
    return rect;
}

QCameraFocusZoneList CameraBinFocus::focusZones() const
{
    QCameraFocusZoneList zones;

    switch (m_focusPointMode) {
    case QCameraFocus::FocusPointAuto:
    case QCameraFocus::FocusPointCenter:
        zones.append(QCameraFocusZone(
            rectFromCenterPoint(QPointF(0.5, 0.5), m_focusRectSize), m_focusZoneStatus));
        break;
    case QCameraFocus::FocusPointCustom:
        zones.append(QCameraFocusZone(
            rectFromCenterPoint(m_focusPoint, m_focusRectSize), m_focusZoneStatus));
        break;
#if GST_CHECK_VERSION(1,0,0)
    case QCameraFocus::FocusPointFaceDetection:
        for (const QRect &face : qAsConst(m_faceFocusRects)) {
            const QRectF normalizedRect(
                        face.x() / qreal(m_viewfinderResolution.width()),
                        face.y() / qreal(m_viewfinderResolution.height()),
                        face.width() / qreal(m_viewfinderResolution.width()),
                        face.height() / qreal(m_viewfinderResolution.height()));
            zones.append(QCameraFocusZone(normalizedRect, m_focusZoneStatus));
        }
        break;
#endif
    }
    return zones;
}

void CameraBinFocus::handleFocusMessage(GstMessage *gm)
{
    //it's a sync message, so it's called from non main thread
    const GstStructure *structure = gst_message_get_structure(gm);
    if (gst_structure_has_name(structure, GST_PHOTOGRAPHY_AUTOFOCUS_DONE)) {
        gint status = GST_PHOTOGRAPHY_FOCUS_STATUS_NONE;
        gst_structure_get_int (structure, "status", &status);
        QCamera::LockStatus focusStatus = m_focusStatus;
        QCamera::LockChangeReason reason = QCamera::UserRequest;

        switch (status) {
            case GST_PHOTOGRAPHY_FOCUS_STATUS_FAIL:
                focusStatus = QCamera::Unlocked;
                reason = QCamera::LockFailed;
                break;
            case GST_PHOTOGRAPHY_FOCUS_STATUS_SUCCESS:
                focusStatus = QCamera::Locked;
                break;
            case GST_PHOTOGRAPHY_FOCUS_STATUS_NONE:
                break;
            case GST_PHOTOGRAPHY_FOCUS_STATUS_RUNNING:
                focusStatus = QCamera::Searching;
                break;
            default:
                break;
        }

        static int signalIndex = metaObject()->indexOfSlot(
                    "_q_setFocusStatus(QCamera::LockStatus,QCamera::LockChangeReason)");
        metaObject()->method(signalIndex).invoke(this,
                                                 Qt::QueuedConnection,
                                                 Q_ARG(QCamera::LockStatus,focusStatus),
                                                 Q_ARG(QCamera::LockChangeReason,reason));
    }
}

void CameraBinFocus::_q_setFocusStatus(QCamera::LockStatus status, QCamera::LockChangeReason reason)
{
#ifdef CAMERABIN_DEBUG
    qDebug() << Q_FUNC_INFO << "Current:"
                << m_focusStatus
                << "New:"
                << status << reason;
#endif

    if (m_focusStatus != status) {
        m_focusStatus = status;

        QCameraFocusZone::FocusZoneStatus zonesStatus =
                m_focusStatus == QCamera::Locked ?
                    QCameraFocusZone::Focused : QCameraFocusZone::Selected;

        if (m_focusZoneStatus != zonesStatus) {
            m_focusZoneStatus = zonesStatus;
            emit focusZonesChanged();
        }

#if GST_CHECK_VERSION(1,0,0)
        if (m_focusPointMode == QCameraFocus::FocusPointFaceDetection
                && m_focusStatus == QCamera::Unlocked) {
            _q_updateFaces();
        }
#endif

        emit _q_focusStatusChanged(m_focusStatus, reason);
    }
}

void CameraBinFocus::_q_handleCameraStatusChange(QCamera::Status status)
{
    m_cameraStatus = status;
    if (status == QCamera::ActiveStatus) {
        if (GstPad *pad = gst_element_get_static_pad(m_session->cameraSource(), "vfsrc")) {
            if (GstCaps *caps = qt_gst_pad_get_current_caps(pad)) {
                if (GstStructure *structure = gst_caps_get_structure(caps, 0)) {
                    int width = 0;
                    int height = 0;
                    gst_structure_get_int(structure, "width", &width);
                    gst_structure_get_int(structure, "height", &height);
                    setViewfinderResolution(QSize(width, height));
                }
                gst_caps_unref(caps);
            }
            gst_object_unref(GST_OBJECT(pad));
        }

        updateRegionOfInterest();
    } else {
        _q_setFocusStatus(QCamera::Unlocked, QCamera::LockLost);
    }
}

void CameraBinFocus::_q_startFocusing()
{
    _q_setFocusStatus(QCamera::Searching, QCamera::UserRequest);
    gst_photography_set_autofocus(m_session->photography(), TRUE);
}

void CameraBinFocus::_q_stopFocusing()
{
    gst_photography_set_autofocus(m_session->photography(), FALSE);
    _q_setFocusStatus(QCamera::Unlocked, QCamera::UserRequest);
}

void CameraBinFocus::setViewfinderResolution(const QSize &resolution)
{
    if (resolution != m_viewfinderResolution) {
        m_viewfinderResolution = resolution;
        if (!resolution.isEmpty())
            m_focusRectSize.setWidth(m_focusRectSize.height() * resolution.height() / resolution.width());
    }
}

static void appendRegion(GValue *regions, int priority, const QRect &rectangle)
{
    GstStructure *region = gst_structure_new(
                "region",
                "region-x"        , G_TYPE_UINT , rectangle.x(),
                "region-y"        , G_TYPE_UINT,  rectangle.y(),
                "region-w"        , G_TYPE_UINT , rectangle.width(),
                "region-h"        , G_TYPE_UINT,  rectangle.height(),
                "region-priority" , G_TYPE_UINT,  priority,
                NULL);

    GValue regionValue = G_VALUE_INIT;
    g_value_init(&regionValue, GST_TYPE_STRUCTURE);
    gst_value_set_structure(&regionValue, region);
    gst_structure_free(region);

    gst_value_list_append_value(regions, &regionValue);
    g_value_unset(&regionValue);
}

void CameraBinFocus::sendRegionOfInterestEvent(const QVector<QRect> &rectangles)
{
    if (m_cameraStatus != QCamera::ActiveStatus)
        return;

    GstElement * const cameraSource = m_session->cameraSource();
    if (!cameraSource)
        return;

    GValue regions = G_VALUE_INIT;
    g_value_init(&regions, GST_TYPE_LIST);

    if (rectangles.isEmpty()) {
        appendRegion(&regions, 0, QRect(0, 0, 0, 0));
    } else {
        // Add padding around small face rectangles so the auto focus has a reasonable amount
        // of image to work with.
        const int minimumDimension = qMin(
                    m_viewfinderResolution.width(), m_viewfinderResolution.height()) * 0.3;
        const QRect viewfinderRectangle(QPoint(0, 0), m_viewfinderResolution);

        for (const QRect &rectangle : rectangles) {
            QRect paddedRectangle(
                        0,
                        0,
                        qMax(rectangle.width(), minimumDimension),
                        qMax(rectangle.height(), minimumDimension));
            paddedRectangle.moveCenter(rectangle.center());

            appendRegion(&regions, 1, viewfinderRectangle.intersected(paddedRectangle));
        }
    }

    GstStructure *regionsOfInterest = gst_structure_new(
                "regions-of-interest",
                "frame-width"     , G_TYPE_UINT , m_viewfinderResolution.width(),
                "frame-height"    , G_TYPE_UINT,  m_viewfinderResolution.height(),
                NULL);
    gst_structure_set_value(regionsOfInterest, "regions", &regions);
    g_value_unset(&regions);

    GstEvent *event = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, regionsOfInterest);
    gst_element_send_event(cameraSource, event);
}

void CameraBinFocus::updateRegionOfInterest()
{
    QVector<QRect> rectangles = QVector<QRect>();

    switch (m_focusPointMode) {
    case QCameraFocus::FocusPointAuto:
    case QCameraFocus::FocusPointCenter:
        // Reset RoI by an empty list
        break;
    case QCameraFocus::FocusPointCustom:
        // GStreamer RoI uses denormalized rectangle
        rectangles.append(QRect(
            m_focusPoint.x() * m_viewfinderResolution.width(),
            m_focusPoint.y() * m_viewfinderResolution.height(),
            m_focusRectSize.width() * m_viewfinderResolution.width(),
            m_focusRectSize.height() * m_viewfinderResolution.height()));
        break;
    case QCameraFocus::FocusPointFaceDetection:
        rectangles = m_faceFocusRects;
        break;
    }

    sendRegionOfInterestEvent(rectangles);
}

#if GST_CHECK_VERSION(1,0,0)

void CameraBinFocus::_q_updateFaces()
{
    if (m_focusPointMode != QCameraFocus::FocusPointFaceDetection
            || m_focusStatus != QCamera::Unlocked) {
        return;
    }

    QVector<QRect> faces;

    {
        QMutexLocker locker(&m_mutex);
        faces = m_faces;
    }

    if (!faces.isEmpty()) {
        m_faceResetTimer.stop();
        m_faceFocusRects = faces;
        updateRegionOfInterest();
        emit focusZonesChanged();
    } else {
        m_faceResetTimer.start(500, this);
    }
}

void CameraBinFocus::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_faceResetTimer.timerId()) {
        m_faceResetTimer.stop();

        if (m_focusStatus == QCamera::Unlocked) {
            m_faceFocusRects.clear();
            updateRegionOfInterest();
            emit focusZonesChanged();
        }
    } else {
        QCameraFocusControl::timerEvent(event);
    }
}

bool CameraBinFocus::probeBuffer(GstBuffer *buffer)
{
    QVector<QRect> faces;

#if GST_CHECK_VERSION(1,1,3)
    gpointer state = NULL;
    const GstMetaInfo *info = GST_VIDEO_REGION_OF_INTEREST_META_INFO;

    while (GstMeta *meta = gst_buffer_iterate_meta(buffer, &state)) {
        if (meta->info->api != info->api)
            continue;

        GstVideoRegionOfInterestMeta *region = reinterpret_cast<GstVideoRegionOfInterestMeta *>(meta);

        faces.append(QRect(region->x, region->y, region->w, region->h));
    }
#else
    Q_UNUSED(buffer);
#endif

    QMutexLocker locker(&m_mutex);

    if (m_faces != faces) {
        m_faces = faces;

        static const int signalIndex = metaObject()->indexOfSlot("_q_updateFaces()");
        metaObject()->method(signalIndex).invoke(this, Qt::QueuedConnection);
    }

    return true;
}

#endif

QT_END_NAMESPACE
