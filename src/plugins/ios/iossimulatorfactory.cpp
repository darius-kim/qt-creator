/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "iossimulatorfactory.h"
#include <QLatin1String>
#include "iosconstants.h"
#include "iossimulator.h"
#include "utils/icon.h"
#include "utils/qtcassert.h"

#include <QIcon>

namespace Ios {
namespace Internal {

IosSimulatorFactory::IosSimulatorFactory()
    : ProjectExplorer::IDeviceFactory(Constants::IOS_SIMULATOR_TYPE)
{
    setObjectName(QLatin1String("IosSimulatorFactory"));
}

QString IosSimulatorFactory::displayName() const
{
    return tr("iOS Simulator");
}

QIcon IosSimulatorFactory::icon() const
{
    using namespace Utils;
    static const QIcon icon =
            Icon::combinedIcon({Icon({{":/ios/images/iosdevicesmall.png",
                                       Theme::PanelTextColorDark}}, Icon::Tint),
                                Icon({{":/ios/images/iosdevice.png",
                                       Theme::IconsBaseColor}})});
    return icon;
}

bool IosSimulatorFactory::canCreate() const
{
    return false;
}

ProjectExplorer::IDevice::Ptr IosSimulatorFactory::create() const
{
    return ProjectExplorer::IDevice::Ptr();
}

ProjectExplorer::IDevice::Ptr IosSimulatorFactory::restore(const QVariantMap &map) const
{
    QTC_ASSERT(canRestore(map), return ProjectExplorer::IDevice::Ptr());
    const ProjectExplorer::IDevice::Ptr device = ProjectExplorer::IDevice::Ptr(new IosSimulator());
    device->fromMap(map);
    return device;
}

} // namespace Internal
} // namespace Ios
