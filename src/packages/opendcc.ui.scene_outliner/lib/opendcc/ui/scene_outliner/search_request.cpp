/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */
#include "opendcc/ui/scene_outliner/search_request.h"
#include <QFutureWatcher>
#include <QFutureInterface>
#include <QPointer>
#include <QThread>
#include <atomic>

OPENDCC_NAMESPACE_OPEN

class SceneOutlinerSearchState
{
public:
    enum Status
    {
        PENDING,
        COMPLETED,
        CANCELLED
    };
    explicit SceneOutlinerSearchState(SceneOutlinerSearchRequest* request)
        : request(request)
        , owner_thread(request->thread())
    {
        result.reportStarted();
    }

    const QPointer<SceneOutlinerSearchRequest> request;
    QThread* const owner_thread;
    std::atomic<Status> status { PENDING };
    std::atomic<bool> result_reported { false };
    using Result = std::shared_ptr<const SceneItemList>;
    QFutureInterface<Result> result;
};

SceneOutlinerSearchCompletion::SceneOutlinerSearchCompletion(std::shared_ptr<SceneOutlinerSearchState> state)
    : m_state(std::move(state))
{
}

bool SceneOutlinerSearchCompletion::is_cancelled() const
{
    return !m_state || m_state->status.load() == SceneOutlinerSearchState::CANCELLED;
}

void SceneOutlinerSearchCompletion::complete(SceneItemList matches) const
{
    const auto state = m_state;
    if (!state || state->status.load() != SceneOutlinerSearchState::PENDING)
        return;
    // The guarded request is only accessed on its owning thread.
    if (QThread::currentThread() == state->owner_thread && state->request && state->request->thread() == state->owner_thread)
        return state->request->complete(matches);
    if (!state->result_reported.exchange(true))
    {
        const auto result = std::make_shared<const SceneItemList>(std::move(matches));
        state->result.reportFinished(&result);
    }
}

SceneOutlinerSearchRequest::SceneOutlinerSearchRequest(QObject* parent)
    : QObject(parent)
    , m_state(std::make_shared<SceneOutlinerSearchState>(this))
{
    qRegisterMetaType<SceneItemList>();
    auto* watcher = new QFutureWatcher<SceneOutlinerSearchState::Result>(this);
    connect(watcher, &QFutureWatcher<SceneOutlinerSearchState::Result>::finished, this, [this, watcher]() {
        const auto matches = watcher->result();
        watcher->deleteLater();
        deliver(*matches);
    });
    watcher->setFuture(m_state->result.future());
}

SceneOutlinerSearchRequest::~SceneOutlinerSearchRequest()
{
    auto expected = SceneOutlinerSearchState::PENDING;
    m_state->status.compare_exchange_strong(expected, SceneOutlinerSearchState::CANCELLED);
}

SceneOutlinerSearchCompletion SceneOutlinerSearchRequest::completion() const
{
    return SceneOutlinerSearchCompletion(m_state);
}

bool SceneOutlinerSearchRequest::is_cancelled() const
{
    return completion().is_cancelled();
}

bool SceneOutlinerSearchRequest::is_finished() const
{
    return m_state->status.load() == SceneOutlinerSearchState::COMPLETED;
}

void SceneOutlinerSearchRequest::complete(const SceneItemList& matches)
{
    if (QThread::currentThread() == thread())
        deliver(matches);
    else
        completion().complete(matches);
}

void SceneOutlinerSearchRequest::deliver(const SceneItemList& matches)
{
    auto expected = SceneOutlinerSearchState::PENDING;
    if (m_state->status.compare_exchange_strong(expected, SceneOutlinerSearchState::COMPLETED))
        Q_EMIT finished(matches);
}

void SceneOutlinerSearchRequest::cancel()
{
    auto expected = SceneOutlinerSearchState::PENDING;
    if (m_state->status.compare_exchange_strong(expected, SceneOutlinerSearchState::CANCELLED))
        Q_EMIT cancelled();
}

OPENDCC_NAMESPACE_CLOSE
