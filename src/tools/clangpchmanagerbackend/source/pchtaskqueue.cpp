/****************************************************************************
**
** Copyright (C) 2018 The Qt Company Ltd.
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

#include "pchtaskqueue.h"

#include <environment.h>
#include <pchcreatorinterface.h>
#include <precompiledheaderstorageinterface.h>
#include <progresscounter.h>
#include <sqlitetransaction.h>

#include <utils/algorithm.h>

namespace ClangBackEnd {

void PchTaskQueue::addPchTasks(PchTasks &&newPchTasks, PchTasks &destination)
{
    auto compare = [](const PchTask &first, const PchTask &second) {
        return first.projectPartIds < second.projectPartIds;
    };

    const std::size_t oldSize = destination.size();

    PchTasks mergedPchTasks;
    mergedPchTasks.reserve(destination.size() + newPchTasks.size());
    Utils::set_union(std::make_move_iterator(newPchTasks.begin()),
                     std::make_move_iterator(newPchTasks.end()),
                     std::make_move_iterator(destination.begin()),
                     std::make_move_iterator(destination.end()),
                     std::back_inserter(mergedPchTasks),
                     compare);

    destination = std::move(mergedPchTasks);

    m_progressCounter.addTotal(int(destination.size() - oldSize));
}

void PchTaskQueue::removePchTasksByProjectPartId(const ProjectPartIds &projectsPartIds,
                                                 PchTasks &destination)
{
    class CompareDifference
    {
    public:
        bool operator()(const PchTask &first, ProjectPartId second)
        {
            return first.projectPartId() < second;
        }

        bool operator()(ProjectPartId first, const PchTask &second)
        {
            return first < second.projectPartId();
        }
    };

    const std::size_t oldSize = destination.size();

    PchTasks notToBeRemovedProjectParts;
    notToBeRemovedProjectParts.reserve(destination.size());
    std::set_difference(std::make_move_iterator(destination.begin()),
                        std::make_move_iterator(destination.end()),
                        projectsPartIds.begin(),
                        projectsPartIds.end(),
                        std::back_inserter(notToBeRemovedProjectParts),
                        CompareDifference{});

    destination = std::move(notToBeRemovedProjectParts);

    m_progressCounter.removeTotal(int(oldSize - destination.size()));
}

void PchTaskQueue::addSystemPchTasks(PchTasks &&pchTasks)
{
    addPchTasks(std::move(pchTasks), m_systemPchTasks);
}

void PchTaskQueue::addProjectPchTasks(PchTasks &&pchTasks)
{
    addPchTasks(std::move(pchTasks), m_projectPchTasks);
}

void PchTaskQueue::removePchTasks(const ProjectPartIds &projectsPartIds)
{
    removePchTasksByProjectPartId(projectsPartIds, m_projectPchTasks);
}

void PchTaskQueue::processProjectPchTasks()
{
    uint systemRunningTaskCount = m_systemPchTaskScheduler.slotUsage().used;

    if (!systemRunningTaskCount) {
        uint freeTaskCount = m_projectPchTaskScheduler.slotUsage().free;

        auto newEnd = std::prev(m_projectPchTasks.end(),
                                std::min<int>(int(freeTaskCount), int(m_projectPchTasks.size())));
        m_projectPchTaskScheduler.addTasks(createProjectTasks(
            {std::make_move_iterator(newEnd), std::make_move_iterator(m_projectPchTasks.end())}));
        m_projectPchTasks.erase(newEnd, m_projectPchTasks.end());
    }
}

void PchTaskQueue::processSystemPchTasks()
{
    uint freeTaskCount = m_systemPchTaskScheduler.slotUsage().free;

    auto newEnd = std::prev(m_systemPchTasks.end(),
                            std::min<int>(int(freeTaskCount), int(m_systemPchTasks.size())));
    m_systemPchTaskScheduler.addTasks(createSystemTasks(
        {std::make_move_iterator(newEnd), std::make_move_iterator(m_systemPchTasks.end())}));
    m_systemPchTasks.erase(newEnd, m_systemPchTasks.end());
}

void PchTaskQueue::processEntries()
{
    processSystemPchTasks();
    processProjectPchTasks();
}

std::vector<PchTaskQueue::Task> PchTaskQueue::createProjectTasks(PchTasks &&pchTasks) const
{
    std::vector<Task> tasks;
    tasks.reserve(pchTasks.size());

    auto convert = [this](auto &&pchTask) {
        return [pchTask = std::move(pchTask), this](PchCreatorInterface &pchCreator) mutable {
            const auto projectPartId = pchTask.projectPartId();
            if (pchTask.includes.size()) {
                pchTask.systemPchPath = m_precompiledHeaderStorage.fetchSystemPrecompiledHeaderPath(
                    projectPartId);
                pchTask.preIncludeSearchPath = m_environment.preIncludeSearchPath();
                pchCreator.generatePch(std::move(pchTask));
                const auto &projectPartPch = pchCreator.projectPartPch();
                if (projectPartPch.pchPath.empty()) {
                    m_precompiledHeaderStorage.deleteProjectPrecompiledHeader(projectPartId);
                } else {
                    m_precompiledHeaderStorage.insertProjectPrecompiledHeader(
                        projectPartId, projectPartPch.pchPath, projectPartPch.lastModified);
                }
            } else {
                m_precompiledHeaderStorage.deleteProjectPrecompiledHeader(projectPartId);
            }
        };
    };

    std::transform(std::make_move_iterator(pchTasks.begin()),
                   std::make_move_iterator(pchTasks.end()),
                   std::back_inserter(tasks),
                   convert);

    return tasks;
}

std::vector<PchTaskQueue::Task> PchTaskQueue::createSystemTasks(PchTasks &&pchTasks) const
{
    std::vector<Task> tasks;
    tasks.reserve(pchTasks.size());

    auto convert = [this](auto &&pchTask) {
        return [pchTask = std::move(pchTask), this](PchCreatorInterface &pchCreator) mutable {
            const auto projectPartIds = pchTask.projectPartIds;
            if (pchTask.includes.size()) {
                pchTask.preIncludeSearchPath = m_environment.preIncludeSearchPath();
                pchCreator.generatePch(std::move(pchTask));
                const auto &projectPartPch = pchCreator.projectPartPch();
                if (projectPartPch.pchPath.empty()) {
                    m_precompiledHeaderStorage.deleteSystemPrecompiledHeaders(projectPartIds);
                } else {
                    m_precompiledHeaderStorage.insertSystemPrecompiledHeaders(
                        projectPartIds, projectPartPch.pchPath, projectPartPch.lastModified);
                }
            } else {
                m_precompiledHeaderStorage.deleteSystemPrecompiledHeaders(projectPartIds);
            }
        };
    };

    std::transform(std::make_move_iterator(pchTasks.begin()),
                   std::make_move_iterator(pchTasks.end()),
                   std::back_inserter(tasks),
                   convert);

    return tasks;
}

} // namespace ClangBackEnd
