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

#include "clangcodemodelserver.h"

#include "clangfilesystemwatcher.h"
#include "codecompleter.h"
#include "diagnosticset.h"
#include "highlightingmarks.h"
#include "projectpartsdonotexistexception.h"
#include "skippedsourceranges.h"
#include "translationunitdoesnotexistexception.h"
#include "translationunitfilenotexitexception.h"
#include "translationunitisnullexception.h"
#include "translationunitparseerrorexception.h"
#include "translationunits.h"

#include <clangbackendipcdebugutils.h>
#include <cmbcodecompletedmessage.h>
#include <cmbcompletecodemessage.h>
#include <cmbregisterprojectsforeditormessage.h>
#include <cmbregistertranslationunitsforeditormessage.h>
#include <cmbunregisterprojectsforeditormessage.h>
#include <cmbunregistertranslationunitsforeditormessage.h>
#include <documentannotationschangedmessage.h>
#include <registerunsavedfilesforeditormessage.h>
#include <requestdocumentannotations.h>
#include <projectpartsdonotexistmessage.h>
#include <translationunitdoesnotexistmessage.h>
#include <unregisterunsavedfilesforeditormessage.h>
#include <updatetranslationunitsforeditormessage.h>
#include <updatevisibletranslationunitsmessage.h>

#include <QCoreApplication>
#include <QDebug>

namespace ClangBackEnd {

namespace {

int getIntervalFromEnviromentVariable()
{
    const QByteArray userIntervalAsByteArray = qgetenv("QTC_CLANG_DELAYED_REPARSE_TIMEOUT");

    bool isConversionOk = false;
    const int intervalAsInt = userIntervalAsByteArray.toInt(&isConversionOk);

    if (isConversionOk)
        return intervalAsInt;
    else
        return -1;
}

int delayedDocumentAnnotationsTimerInterval()
{
    static const int defaultInterval = 1500;
    static const int userDefinedInterval = getIntervalFromEnviromentVariable();
    static const int interval = userDefinedInterval >= 0 ? userDefinedInterval : defaultInterval;

    return interval;
}

} // anonymous

ClangCodeModelServer::ClangCodeModelServer()
    : translationUnits(projects, unsavedFiles)
    , updateDocumentAnnotationsTimeOutInMs(delayedDocumentAnnotationsTimerInterval())
{
    updateDocumentAnnotationsTimer.setSingleShot(true);

    QObject::connect(&updateDocumentAnnotationsTimer,
                     &QTimer::timeout,
                     [this]() {
        processJobsForDirtyAndVisibleDocuments();
    });

    QObject::connect(translationUnits.clangFileSystemWatcher(),
                     &ClangFileSystemWatcher::fileChanged,
                     [this](const Utf8String &filePath) {
        ClangCodeModelServer::startDocumentAnnotationsTimerIfFileIsNotATranslationUnit(filePath);
    });
}

void ClangCodeModelServer::end()
{
    QCoreApplication::exit();
}

void ClangCodeModelServer::registerTranslationUnitsForEditor(const ClangBackEnd::RegisterTranslationUnitForEditorMessage &message)
{
    TIME_SCOPE_DURATION("ClangCodeModelServer::registerTranslationUnitsForEditor");

    try {
        auto createdTranslationUnits = translationUnits.create(message.fileContainers());
        unsavedFiles.createOrUpdate(message.fileContainers());
        translationUnits.setUsedByCurrentEditor(message.currentEditorFilePath());
        translationUnits.setVisibleInEditors(message.visibleEditorFilePaths());

        processInitialJobsForDocuments(createdTranslationUnits);
    } catch (const ProjectPartDoNotExistException &exception) {
        client()->projectPartsDoNotExist(ProjectPartsDoNotExistMessage(exception.projectPartIds()));
    } catch (const std::exception &exception) {
        qWarning() << "Error in ClangCodeModelServer::registerTranslationUnitsForEditor:" << exception.what();
    }
}

void ClangCodeModelServer::updateTranslationUnitsForEditor(const UpdateTranslationUnitsForEditorMessage &message)
{
    TIME_SCOPE_DURATION("ClangCodeModelServer::updateTranslationUnitsForEditor");

    try {
        const auto newerFileContainers = translationUnits.newerFileContainers(message.fileContainers());
        if (newerFileContainers.size() > 0) {
            translationUnits.update(newerFileContainers);
            unsavedFiles.createOrUpdate(newerFileContainers);

            updateDocumentAnnotationsTimer.start(updateDocumentAnnotationsTimeOutInMs);
        }
    } catch (const ProjectPartDoNotExistException &exception) {
        client()->projectPartsDoNotExist(ProjectPartsDoNotExistMessage(exception.projectPartIds()));
    } catch (const TranslationUnitDoesNotExistException &exception) {
        client()->translationUnitDoesNotExist(TranslationUnitDoesNotExistMessage(exception.fileContainer()));
    } catch (const std::exception &exception) {
        qWarning() << "Error in ClangCodeModelServer::updateTranslationUnitsForEditor:" << exception.what();
    }
}

void ClangCodeModelServer::unregisterTranslationUnitsForEditor(const ClangBackEnd::UnregisterTranslationUnitsForEditorMessage &message)
{
    TIME_SCOPE_DURATION("ClangCodeModelServer::unregisterTranslationUnitsForEditor");

    try {
        translationUnits.remove(message.fileContainers());
        unsavedFiles.remove(message.fileContainers());
    } catch (const TranslationUnitDoesNotExistException &exception) {
        client()->translationUnitDoesNotExist(TranslationUnitDoesNotExistMessage(exception.fileContainer()));
    } catch (const ProjectPartDoNotExistException &exception) {
        client()->projectPartsDoNotExist(ProjectPartsDoNotExistMessage(exception.projectPartIds()));
    } catch (const std::exception &exception) {
        qWarning() << "Error in ClangCodeModelServer::unregisterTranslationUnitsForEditor:" << exception.what();
    }
}

void ClangCodeModelServer::registerProjectPartsForEditor(const RegisterProjectPartsForEditorMessage &message)
{
    TIME_SCOPE_DURATION("ClangCodeModelServer::registerProjectPartsForEditor");

    try {
        projects.createOrUpdate(message.projectContainers());
        translationUnits.setTranslationUnitsDirtyIfProjectPartChanged();

        processJobsForDirtyAndVisibleDocuments();
    } catch (const std::exception &exception) {
        qWarning() << "Error in ClangCodeModelServer::registerProjectPartsForEditor:" << exception.what();
    }
}

void ClangCodeModelServer::unregisterProjectPartsForEditor(const UnregisterProjectPartsForEditorMessage &message)
{
    TIME_SCOPE_DURATION("ClangCodeModelServer::unregisterProjectPartsForEditor");

    try {
        projects.remove(message.projectPartIds());
    } catch (const ProjectPartDoNotExistException &exception) {
        client()->projectPartsDoNotExist(ProjectPartsDoNotExistMessage(exception.projectPartIds()));
    } catch (const std::exception &exception) {
        qWarning() << "Error in ClangCodeModelServer::unregisterProjectPartsForEditor:" << exception.what();
    }
}

void ClangCodeModelServer::registerUnsavedFilesForEditor(const RegisterUnsavedFilesForEditorMessage &message)
{
    TIME_SCOPE_DURATION("ClangCodeModelServer::registerUnsavedFilesForEditor");

    try {
        unsavedFiles.createOrUpdate(message.fileContainers());
        translationUnits.updateTranslationUnitsWithChangedDependencies(message.fileContainers());

        updateDocumentAnnotationsTimer.start(updateDocumentAnnotationsTimeOutInMs);
    } catch (const ProjectPartDoNotExistException &exception) {
        client()->projectPartsDoNotExist(ProjectPartsDoNotExistMessage(exception.projectPartIds()));
    } catch (const std::exception &exception) {
        qWarning() << "Error in ClangCodeModelServer::registerUnsavedFilesForEditor:" << exception.what();
    }
}

void ClangCodeModelServer::unregisterUnsavedFilesForEditor(const UnregisterUnsavedFilesForEditorMessage &message)
{
    TIME_SCOPE_DURATION("ClangCodeModelServer::unregisterUnsavedFilesForEditor");

    try {
        unsavedFiles.remove(message.fileContainers());
        translationUnits.updateTranslationUnitsWithChangedDependencies(message.fileContainers());
    } catch (const TranslationUnitDoesNotExistException &exception) {
        client()->translationUnitDoesNotExist(TranslationUnitDoesNotExistMessage(exception.fileContainer()));
    } catch (const ProjectPartDoNotExistException &exception) {
        client()->projectPartsDoNotExist(ProjectPartsDoNotExistMessage(exception.projectPartIds()));
    } catch (const std::exception &exception) {
        qWarning() << "Error in ClangCodeModelServer::unregisterUnsavedFilesForEditor:" << exception.what();
    }
}

void ClangCodeModelServer::completeCode(const ClangBackEnd::CompleteCodeMessage &message)
{
    TIME_SCOPE_DURATION("ClangCodeModelServer::completeCode");

    try {
        auto translationUnit = translationUnits.translationUnit(message.filePath(),
                                                                message.projectPartId());

        JobRequest jobRequest = createJobRequest(translationUnit, JobRequest::Type::CompleteCode);
        jobRequest.line = message.line();
        jobRequest.column = message.column();
        jobRequest.ticketNumber = message.ticketNumber();

        jobs().add(jobRequest);
        jobs().process();
    } catch (const TranslationUnitDoesNotExistException &exception) {
        client()->translationUnitDoesNotExist(TranslationUnitDoesNotExistMessage(exception.fileContainer()));
    } catch (const ProjectPartDoNotExistException &exception) {
        client()->projectPartsDoNotExist(ProjectPartsDoNotExistMessage(exception.projectPartIds()));
    }  catch (const std::exception &exception) {
        qWarning() << "Error in ClangCodeModelServer::completeCode:" << exception.what();
    }
}

void ClangCodeModelServer::requestDocumentAnnotations(const RequestDocumentAnnotationsMessage &message)
{
    TIME_SCOPE_DURATION("ClangCodeModelServer::requestDocumentAnnotations");

    try {
        auto translationUnit = translationUnits.translationUnit(message.fileContainer().filePath(),
                                                                message.fileContainer().projectPartId());

        const JobRequest jobRequest = createJobRequest(translationUnit,
                                                       JobRequest::Type::RequestDocumentAnnotations);

        jobs().add(jobRequest);
        jobs().process();
    } catch (const TranslationUnitDoesNotExistException &exception) {
        client()->translationUnitDoesNotExist(TranslationUnitDoesNotExistMessage(exception.fileContainer()));
    } catch (const ProjectPartDoNotExistException &exception) {
        client()->projectPartsDoNotExist(ProjectPartsDoNotExistMessage(exception.projectPartIds()));
    }  catch (const std::exception &exception) {
        qWarning() << "Error in ClangCodeModelServer::requestDocumentAnnotations:" << exception.what();
    }
}

void ClangCodeModelServer::updateVisibleTranslationUnits(const UpdateVisibleTranslationUnitsMessage &message)
{
    TIME_SCOPE_DURATION("ClangCodeModelServer::updateVisibleTranslationUnits");

    try {
        translationUnits.setUsedByCurrentEditor(message.currentEditorFilePath());
        translationUnits.setVisibleInEditors(message.visibleEditorFilePaths());
        updateDocumentAnnotationsTimer.start(0);
    }  catch (const std::exception &exception) {
        qWarning() << "Error in ClangCodeModelServer::updateVisibleTranslationUnits:" << exception.what();
    }
}

const TranslationUnits &ClangCodeModelServer::translationUnitsForTestOnly() const
{
    return translationUnits;
}

void ClangCodeModelServer::startDocumentAnnotationsTimerIfFileIsNotATranslationUnit(const Utf8String &filePath)
{
    if (!translationUnits.hasTranslationUnitWithFilePath(filePath))
        updateDocumentAnnotationsTimer.start(0);
}

const Jobs &ClangCodeModelServer::jobsForTestOnly()
{
    return jobs();
}

bool ClangCodeModelServer::isTimerRunningForTestOnly() const
{
    return updateDocumentAnnotationsTimer.isActive();
}

void ClangCodeModelServer::addJobRequestsForDirtyAndVisibleDocuments()
{
    for (const auto &translationUnit : translationUnits.translationUnits()) {
        if (translationUnit.isNeedingReparse() && translationUnit.isVisibleInEditor()) {
            jobs().add(createJobRequest(translationUnit,
                                        JobRequest::Type::UpdateDocumentAnnotations));
        }
    }
}

void ClangCodeModelServer::processJobsForDirtyAndVisibleDocuments()
{
    addJobRequestsForDirtyAndVisibleDocuments();
    jobs().process();
}

void ClangCodeModelServer::processInitialJobsForDocuments(
        const std::vector<TranslationUnit> &translationUnits)
{
    for (const auto &translationUnit : translationUnits) {
        jobs().add(createJobRequest(translationUnit,
                                    JobRequest::Type::UpdateDocumentAnnotations));
        jobs().add(createJobRequest(translationUnit,
                                    JobRequest::Type::CreateInitialDocumentPreamble));
    }

    jobs().process();
}

JobRequest ClangCodeModelServer::createJobRequest(const TranslationUnit &translationUnit,
                                                  JobRequest::Type type) const
{
    JobRequest jobRequest;
    jobRequest.type = type;
    jobRequest.requirements = JobRequest::requirementsForType(type);
    jobRequest.filePath = translationUnit.filePath();
    jobRequest.projectPartId = translationUnit.projectPartId();
    jobRequest.unsavedFilesChangeTimePoint = unsavedFiles.lastChangeTimePoint();
    jobRequest.documentRevision = translationUnit.documentRevision();
    const ProjectPart &projectPart = projects.project(translationUnit.projectPartId());
    jobRequest.projectChangeTimePoint = projectPart.lastChangeTimePoint();

    return jobRequest;
}

void ClangCodeModelServer::setUpdateDocumentAnnotationsTimeOutInMsForTestsOnly(int value)
{
    updateDocumentAnnotationsTimeOutInMs = value;
}

Jobs &ClangCodeModelServer::jobs()
{
    if (!jobs_) {
        // Jobs needs a reference to the client, but the client is not known at
        // construction time of ClangCodeModelServer, so construct Jobs in a
        // lazy manner.
        jobs_.reset(new Jobs(translationUnits, unsavedFiles, projects, *client()));
    }

    return *jobs_.data();
}

} // namespace ClangBackEnd
