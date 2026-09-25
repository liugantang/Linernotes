// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

struct mpv_handle;

namespace linernotes::player {

namespace detail {
struct MpvDeleter {
    void operator()(mpv_handle *handle) const noexcept;
};
} // namespace detail

/// 对 libmpv 的底层 Qt 封装。
///
/// 线程安全性：所有公共方法只允许在对象所属线程调用（非线程安全，禁止跨线程直接调用）。
class MpvHandle : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MpvHandle)

public:
    using OptionList = QList<std::pair<QString, QString>>;

    /// 默认初始化参数。
    static OptionList defaultOptions();

    /// 创建并初始化 mpv 实例：先应用 defaultOptions()，再应用 extraOptions（同名项后者覆盖前者）。
    /// 初始化失败时 isValid() 为 false，errorString() 给出原因；此时其他方法安全地返回失败/空值。
    explicit MpvHandle(const OptionList &extraOptions = { }, QObject *parent = nullptr);
    ~MpvHandle() override;

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] QString errorString() const;

    /// 同步执行命令，例如 {"loadfile", path, "replace"}。失败返回 false 并以 qCWarning(lcPlayer)
    /// 记录 mpv_error_string。
    bool command(const QStringList &args);

    /// 通过 MPV_FORMAT_NODE 读写属性。支持 QVariant 类型：bool、int/qint64、double、QString、
    /// QVariantList、QVariantMap（递归）。读取失败返回无效 QVariant。
    bool setProperty(const QString &name, const QVariant &value);
    [[nodiscard]] QVariant property(const QString &name) const;

    /// 观察属性（MPV_FORMAT_NODE）；值变化时发 propertyChanged。重复观察同名属性不重复注册。
    /// 属性不可用（如未加载文件时的 duration）时，value 为无效 QVariant。
    void observeProperty(const QString &name);

    enum class EndFileReason : std::uint8_t { Eof, Stop, Quit, Error, Redirect, Unknown };
    Q_ENUM(EndFileReason)

signals:
    void propertyChanged(const QString &name, const QVariant &value);
    void startFile(qint64 playlistEntryId);
    void fileLoaded();
    /// error 为 mpv_error_string（仅 reason==Error 时非空）
    void endFile(qint64 playlistEntryId, linernotes::player::MpvHandle::EndFileReason reason,
        const QString &error);
    void shutdown();

private:
    static void onWakeup(void *ctx) noexcept;
    void drainEvents();

    std::unique_ptr<mpv_handle, detail::MpvDeleter> m_handle;
    bool m_isValid = false;
    QString m_errorString;
    std::atomic<bool> m_drainPending { false };
    QHash<quint64, QString> m_observedProperties;
    QHash<QString, quint64> m_propertyToUserdata;
    quint64 m_nextObserveId = 1;
};

} // namespace linernotes::player
