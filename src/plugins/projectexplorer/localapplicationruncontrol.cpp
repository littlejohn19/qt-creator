/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2010 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** No Commercial Usage
**
** This file contains pre-release code and may not be distributed.
** You may use this file in accordance with the terms and conditions
** contained in the Technology Preview License Agreement accompanying
** this package.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights.  These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
**
**************************************************************************/

#include "localapplicationruncontrol.h"
#include "applicationrunconfiguration.h"
#include "projectexplorerconstants.h"

#include <utils/qtcassert.h>
#include <utils/environment.h>

#include <QtGui/QLabel>
#include <QtCore/QDir>

namespace ProjectExplorer {
namespace Internal {

LocalApplicationRunControlFactory::LocalApplicationRunControlFactory()
{
}

LocalApplicationRunControlFactory::~LocalApplicationRunControlFactory()
{
}

bool LocalApplicationRunControlFactory::canRun(ProjectExplorer::RunConfiguration *runConfiguration, const QString &mode) const
{
    return (mode == ProjectExplorer::Constants::RUNMODE)
            && (qobject_cast<LocalApplicationRunConfiguration *>(runConfiguration) != 0);
}

QString LocalApplicationRunControlFactory::displayName() const
{
    return tr("Run");
}

RunControl *LocalApplicationRunControlFactory::create(ProjectExplorer::RunConfiguration *runConfiguration, const QString &mode)
{
    QTC_ASSERT(canRun(runConfiguration, mode), return 0);
    return new LocalApplicationRunControl(qobject_cast<LocalApplicationRunConfiguration *>(runConfiguration), mode);
}

QWidget *LocalApplicationRunControlFactory::createConfigurationWidget(RunConfiguration *runConfiguration)
{
    Q_UNUSED(runConfiguration)
    return new QLabel("TODO add Configuration widget");
}

// ApplicationRunControl

LocalApplicationRunControl::LocalApplicationRunControl(LocalApplicationRunConfiguration *rc, QString mode)
    : RunControl(rc, mode)
{
    Utils::Environment env = rc->environment();
    QString dir = rc->workingDirectory();
    m_applicationLauncher.setEnvironment(env);
    m_applicationLauncher.setWorkingDirectory(dir);

    m_executable = rc->executable();
    m_runMode = static_cast<ApplicationLauncher::Mode>(rc->runMode());
    m_commandLineArguments = rc->commandLineArguments();

    connect(&m_applicationLauncher, SIGNAL(appendMessage(QString,bool)),
            this, SLOT(slotAppendMessage(QString,bool)));
    connect(&m_applicationLauncher, SIGNAL(appendOutput(QString, bool)),
            this, SLOT(slotAddToOutputWindow(QString, bool)));
    connect(&m_applicationLauncher, SIGNAL(processExited(int)),
            this, SLOT(processExited(int)));
    connect(&m_applicationLauncher, SIGNAL(bringToForegroundRequested(qint64)),
            this, SLOT(bringApplicationToForeground(qint64)));
}

LocalApplicationRunControl::~LocalApplicationRunControl()
{
}

void LocalApplicationRunControl::start()
{
    m_applicationLauncher.start(m_runMode, m_executable, m_commandLineArguments);
    emit started();

    emit appendMessage(this, tr("Starting %1...").arg(QDir::toNativeSeparators(m_executable)), false);
}

LocalApplicationRunControl::StopResult LocalApplicationRunControl::stop()
{
    m_applicationLauncher.stop();
    return StoppedSynchronously;
}

bool LocalApplicationRunControl::isRunning() const
{
    return m_applicationLauncher.isRunning();
}

void LocalApplicationRunControl::slotAppendMessage(const QString &err,
                                                   bool isError)
{
    emit appendMessage(this, err, isError);
    emit finished();
}

void LocalApplicationRunControl::slotAddToOutputWindow(const QString &line,
                                                       bool isError)
{
    emit addToOutputWindow(this, line, isError, true);
}

void LocalApplicationRunControl::processExited(int exitCode)
{
    emit appendMessage(this, tr("%1 exited with code %2").arg(QDir::toNativeSeparators(m_executable)).arg(exitCode), false);
    emit finished();
}

} // namespace Internal
} // namespace ProjectExplorer
