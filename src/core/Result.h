// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>
#include <QtAssert>

#include <optional>
#include <utility>
#include <variant>

namespace linernotes::core {

struct Error {
    /// 机器可读错误码。格式约定为 "<领域>.<原因>"，小写点分；
    /// 每个模块在自己的 Errors.h（命名空间 <模块>::errc）集中定义常量，调用方只用常量。
    QString code;
    QString message; // 人类可读的简述
    QString detail; // 上下文：文件路径、SQL 错误文本、迁移编号等

    QString toString() const
    {
        if (detail.isEmpty()) {
            return QStringLiteral("%1: %2").arg(code, message);
        }
        return QStringLiteral("%1: %2 (%3)").arg(code, message, detail);
    }

    bool operator==(const Error &other) const = default;
};

template <typename T> class [[nodiscard]] Result {
public:
    Result(T value)
        : m_data(std::move(value))
    {
    }

    Result(Error error)
        : m_data(std::move(error))
    {
    }

    [[nodiscard]] bool ok() const { return std::holds_alternative<T>(m_data); }

    explicit operator bool() const { return ok(); }

    [[nodiscard]] const T &value() const &
    {
        Q_ASSERT_X(ok(), "Result::value", "Called value() on an error Result");
        return std::get<T>(m_data);
    }

    [[nodiscard]] T &value() &
    {
        Q_ASSERT_X(ok(), "Result::value", "Called value() on an error Result");
        return std::get<T>(m_data);
    }

    [[nodiscard]] T &&value() &&
    {
        Q_ASSERT_X(ok(), "Result::value", "Called value() on an error Result");
        return std::get<T>(std::move(m_data));
    }

    [[nodiscard]] const Error &error() const
    {
        Q_ASSERT_X(!ok(), "Result::error", "Called error() on an ok Result");
        return std::get<Error>(m_data);
    }

private:
    std::variant<T, Error> m_data;
};

template <> class [[nodiscard]] Result<void> {
public:
    Result()
        : m_error(std::nullopt)
    {
    }

    Result(Error error)
        : m_error(std::move(error))
    {
    }

    [[nodiscard]] bool ok() const { return !m_error.has_value(); }

    explicit operator bool() const { return ok(); }

    [[nodiscard]] const Error &error() const
    {
        Q_ASSERT_X(!ok(), "Result<void>::error", "Called error() on an ok Result");
        if (m_error.has_value()) {
            return *m_error;
        }
        static const Error s_emptyError { };
        return s_emptyError;
    }

private:
    std::optional<Error> m_error;
};

} // namespace linernotes::core
