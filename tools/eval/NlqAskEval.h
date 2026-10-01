// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include "EvalHarness.h"

#include <QElapsedTimer>
#include <QObject>

#include <nlq/Interpreter.h>

#include <memory>

namespace linernotes::eval {

class NlqAskEval : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(NlqAskEval)

public:
    explicit NlqAskEval(EvalConfig config, QObject *parent = nullptr);
    ~NlqAskEval() override;

    bool init();
    void start();

private:
    void onInterpreterFinished();

    EvalHarness m_harness;
    std::unique_ptr<nlq::Interpreter> m_interpreter;
    QElapsedTimer m_timer;
};

} // namespace linernotes::eval
