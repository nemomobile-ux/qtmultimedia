/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/
**
** This file is part of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QCAMERAIMAGEPROCESSINGCONTROL_H
#define QCAMERAIMAGEPROCESSINGCONTROL_H

#include <qmediacontrol.h>
#include <qmediaobject.h>

#include <qcamera.h>
#include <qmediaenumdebug.h>

QT_BEGIN_HEADER

QT_BEGIN_NAMESPACE

QT_MODULE(Multimedia)

// Required for QDoc workaround
class QString;

class Q_MULTIMEDIA_EXPORT QCameraImageProcessingControl : public QMediaControl
{
    Q_OBJECT
    Q_ENUMS(ProcessingParameter)

public:
    ~QCameraImageProcessingControl();

    enum ProcessingParameter {
        WhiteBalancePreset,
        ColorTemperature,
        Contrast,
        Saturation,
        Brightness,
        Sharpening,
        Denoising,
        ContrastAdjustment,
        SaturationAdjustment,
        BrightnessAdjustment,
        SharpeningAdjustment,
        DenoisingAdjustment,
        ExtendedParameter = 1000
    };

    virtual bool isParameterSupported(ProcessingParameter) const = 0;
    virtual bool isParameterValueSupported(ProcessingParameter parameter, const QVariant &value) const = 0;
    virtual QVariant parameter(ProcessingParameter parameter) const = 0;
    virtual void setParameter(ProcessingParameter parameter, const QVariant &value) = 0;

protected:
    QCameraImageProcessingControl(QObject* parent = 0);
};

#define QCameraImageProcessingControl_iid "org.qt-project.qt.cameraimageprocessingcontrol/5.0"
Q_MEDIA_DECLARE_CONTROL(QCameraImageProcessingControl, QCameraImageProcessingControl_iid)

QT_END_NAMESPACE

Q_DECLARE_METATYPE(QCameraImageProcessingControl::ProcessingParameter)

Q_MEDIA_ENUM_DEBUG(QCameraImageProcessingControl, ProcessingParameter)

QT_END_HEADER

#endif

