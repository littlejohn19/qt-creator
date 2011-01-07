/****************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the Qt Creator.
**
** $QT_BEGIN_LICENSE:LGPL$
** No Commercial Usage
** This file contains pre-release code and may not be distributed.
** You may use this file in accordance with the terms and conditions
** contained in the Technology Preview License Agreement accompanying
** this package.
**
** GNU Lesser General Public License Usage
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
** $QT_END_LICENSE$
**
****************************************************************************/

#include "maemoruncontrol.h"

#include "maemodeploystep.h"
#include "maemoglobal.h"
#include "maemorunconfiguration.h"
#include "maemosshrunner.h"

#include <projectexplorer/projectexplorerconstants.h>
#include <utils/qtcassert.h>

#include <QtGui/QMessageBox>

using namespace Core;

namespace Qt4ProjectManager {
namespace Internal {

using ProjectExplorer::RunConfiguration;

MaemoRunControl::MaemoRunControl(RunConfiguration *rc)
    : RunControl(rc, ProjectExplorer::Constants::RUNMODE)
    , m_runner(new MaemoSshRunner(this, qobject_cast<MaemoRunConfiguration *>(rc), false))
    , m_running(false)
{
}

MaemoRunControl::~MaemoRunControl()
{
    stop();
}

void MaemoRunControl::start()
{
    m_running = true;
    emit started();
    disconnect(m_runner, 0, this, 0);
    connect(m_runner, SIGNAL(error(QString)), SLOT(handleSshError(QString)));
    connect(m_runner, SIGNAL(readyForExecution()), SLOT(startExecution()));
    connect(m_runner, SIGNAL(remoteErrorOutput(QByteArray)),
        SLOT(handleRemoteErrorOutput(QByteArray)));
    connect(m_runner, SIGNAL(remoteOutput(QByteArray)),
        SLOT(handleRemoteOutput(QByteArray)));
    connect(m_runner, SIGNAL(remoteProcessStarted()),
        SLOT(handleRemoteProcessStarted()));
    connect(m_runner, SIGNAL(remoteProcessFinished(qint64)),
        SLOT(handleRemoteProcessFinished(qint64)));
    connect(m_runner, SIGNAL(reportProgress(QString)),
        SLOT(handleProgressReport(QString)));
    connect(m_runner, SIGNAL(mountDebugOutput(QString)),
        SLOT(handleMountDebugOutput(QString)));
    m_runner->start();
}

ProjectExplorer::RunControl::StopResult MaemoRunControl::stop()
{
    m_runner->stop();
    return StoppedSynchronously;
}

void MaemoRunControl::handleSshError(const QString &error)
{
    handleError(error);
    setFinished();
}

void MaemoRunControl::startExecution()
{
    emit appendMessage(this, tr("Starting remote process ..."), false);
    m_runner->startExecution(QString::fromLocal8Bit("%1 %2 %3 %4")
        .arg(MaemoGlobal::remoteCommandPrefix(m_runner->remoteExecutable()))
        .arg(MaemoGlobal::remoteEnvironment(m_runner->userEnvChanges()))
        .arg(m_runner->remoteExecutable())
        .arg(m_runner->arguments()).toUtf8());
}

void MaemoRunControl::handleRemoteProcessFinished(qint64 exitCode)
{
    if (exitCode != MaemoSshRunner::InvalidExitCode) {
        emit appendMessage(this,
            tr("Finished running remote process. Exit code was %1.")
            .arg(exitCode), false);
    }
    setFinished();
}

void MaemoRunControl::handleRemoteOutput(const QByteArray &output)
{
    emit addToOutputWindow(this, QString::fromUtf8(output), false, true);
}

void MaemoRunControl::handleRemoteErrorOutput(const QByteArray &output)
{
    emit addToOutputWindow(this, QString::fromUtf8(output), true, true);
}

void MaemoRunControl::handleProgressReport(const QString &progressString)
{
    emit appendMessage(this, progressString, false);
}

void MaemoRunControl::handleMountDebugOutput(const QString &output)
{
    emit addToOutputWindow(this, output, true, true);
}

bool MaemoRunControl::isRunning() const
{
    return m_running;
}

void MaemoRunControl::handleError(const QString &errString)
{
    stop();
    emit appendMessage(this, errString, true);
    QMessageBox::critical(0, tr("Remote Execution Failure"), errString);
}

void MaemoRunControl::setFinished()
{
    disconnect(m_runner, 0, this, 0);
    m_running = false;
    emit finished();
}

} // namespace Internal
} // namespace Qt4ProjectManager
