// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "MpvHandle.h"

#include "PlayerLogging.h"

#include <mpv/client.h>

#include <clocale>
#include <deque>
#include <vector>

namespace linernotes::player {

namespace detail {
void MpvDeleter::operator()(mpv_handle *handle) const noexcept
{
    if (handle != nullptr) {
        mpv_terminate_destroy(handle);
    }
}
} // namespace detail

namespace {

// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access,cppcoreguidelines-pro-bounds-pointer-arithmetic,misc-no-recursion)
QVariant nodeToVariant(const mpv_node *node)
{
    if (node == nullptr) {
        return { };
    }

    switch (node->format) {
    case MPV_FORMAT_FLAG:
        return { static_cast<bool>(node->u.flag != 0) };
    case MPV_FORMAT_INT64:
        return { static_cast<qint64>(node->u.int64) };
    case MPV_FORMAT_DOUBLE:
        return { node->u.double_ };
    case MPV_FORMAT_STRING:
    case MPV_FORMAT_OSD_STRING:
        return (node->u.string != nullptr) ? QVariant(QString::fromUtf8(node->u.string))
                                           : QVariant(QString());
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList list;
        if (node->u.list != nullptr) {
            list.reserve(node->u.list->num);
            for (int i = 0; i < node->u.list->num; ++i) {
                list.append(nodeToVariant(&node->u.list->values[i]));
            }
        }
        return list;
    }
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap map;
        if (node->u.list != nullptr) {
            for (int i = 0; i < node->u.list->num; ++i) {
                const QString key
                    = (node->u.list->keys != nullptr && node->u.list->keys[i] != nullptr)
                    ? QString::fromUtf8(node->u.list->keys[i])
                    : QString();
                map.insert(key, nodeToVariant(&node->u.list->values[i]));
            }
        }
        return map;
    }
    case MPV_FORMAT_BYTE_ARRAY: {
        if (node->u.ba != nullptr && node->u.ba->data != nullptr) {
            return QByteArray(static_cast<const char *>(node->u.ba->data),
                static_cast<qsizetype>(node->u.ba->size));
        }
        return QByteArray();
    }
    case MPV_FORMAT_NONE:
    default:
        return { };
    }
}

class MpvNodeBuilder {
public:
    mpv_node create(const QVariant &variant) { return buildNode(variant); }

private:
    mpv_node buildNode(const QVariant &variant)
    {
        mpv_node node;
        node.format = MPV_FORMAT_NONE;
        node.u.int64 = 0;

        if (!variant.isValid() || variant.isNull()) {
            return node;
        }

        switch (variant.typeId()) {
        case QMetaType::Bool:
            node.format = MPV_FORMAT_FLAG;
            node.u.flag = variant.toBool() ? 1 : 0;
            break;
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
        case QMetaType::Long:
        case QMetaType::ULong:
        case QMetaType::Short:
        case QMetaType::UShort:
        case QMetaType::Char:
        case QMetaType::UChar:
        case QMetaType::SChar:
        case QMetaType::Char16:
        case QMetaType::Char32:
            node.format = MPV_FORMAT_INT64;
            node.u.int64 = variant.toLongLong();
            break;
        case QMetaType::Double:
        case QMetaType::Float:
            node.format = MPV_FORMAT_DOUBLE;
            node.u.double_ = variant.toDouble();
            break;
        case QMetaType::QString: {
            node.format = MPV_FORMAT_STRING;
            auto &ba = m_byteArrays.emplace_back(variant.toString().toUtf8());
            node.u.string = ba.data();
            break;
        }
        case QMetaType::QByteArray: {
            node.format = MPV_FORMAT_BYTE_ARRAY;
            auto &ba = m_byteArrays.emplace_back(variant.toByteArray());
            auto &mba = m_byteArrayLists.emplace_back();
            mba.data = ba.data();
            mba.size = static_cast<size_t>(ba.size());
            node.u.ba = &mba;
            break;
        }
        case QMetaType::QVariantList:
        case QMetaType::QStringList: {
            node.format = MPV_FORMAT_NODE_ARRAY;
            const QVariantList list = variant.toList();
            auto &nodeList = m_lists.emplace_back();
            nodeList.num = static_cast<int>(list.size());
            if (list.isEmpty()) {
                nodeList.values = nullptr;
                nodeList.keys = nullptr;
            } else {
                auto &nodes = m_nodeArrays.emplace_back(static_cast<size_t>(list.size()));
                for (qsizetype i = 0; i < list.size(); ++i) {
                    nodes.at(static_cast<size_t>(i)) = buildNode(list.at(i));
                }
                nodeList.values = nodes.data();
                nodeList.keys = nullptr;
            }
            node.u.list = &nodeList;
            break;
        }
        case QMetaType::QVariantMap: {
            node.format = MPV_FORMAT_NODE_MAP;
            const QVariantMap map = variant.toMap();
            auto &nodeList = m_lists.emplace_back();
            nodeList.num = static_cast<int>(map.size());
            if (map.isEmpty()) {
                nodeList.values = nullptr;
                nodeList.keys = nullptr;
            } else {
                auto &nodes = m_nodeArrays.emplace_back(static_cast<size_t>(map.size()));
                auto &keys = m_keyArrays.emplace_back(static_cast<size_t>(map.size()));
                size_t i = 0;
                for (auto it = map.cbegin(); it != map.cend(); ++it, ++i) {
                    auto &keyBa = m_byteArrays.emplace_back(it.key().toUtf8());
                    keys.at(i) = keyBa.data();
                    nodes.at(i) = buildNode(it.value());
                }
                nodeList.values = nodes.data();
                nodeList.keys = keys.data();
            }
            node.u.list = &nodeList;
            break;
        }
        default: {
            if (variant.canConvert<QString>()) {
                node.format = MPV_FORMAT_STRING;
                auto &ba = m_byteArrays.emplace_back(variant.toString().toUtf8());
                node.u.string = ba.data();
            } else {
                node.format = MPV_FORMAT_NONE;
            }
            break;
        }
        }

        return node;
    }

    std::deque<QByteArray> m_byteArrays;
    std::deque<mpv_byte_array> m_byteArrayLists;
    std::deque<mpv_node_list> m_lists;
    std::deque<std::vector<mpv_node>> m_nodeArrays;
    std::deque<std::vector<char *>> m_keyArrays;
};
// NOLINTEND(cppcoreguidelines-pro-type-union-access,cppcoreguidelines-pro-bounds-pointer-arithmetic,misc-no-recursion)

void handleLogMessage(const mpv_event *event)
{
    const auto *msg = static_cast<const mpv_event_log_message *>(event->data);
    if (msg == nullptr || msg->text == nullptr) {
        return;
    }
    QString text = QString::fromUtf8(msg->text);
    while (text.endsWith(u'\n') || text.endsWith(u'\r')) {
        text.chop(1);
    }
    const char *prefix = (msg->prefix != nullptr) ? msg->prefix : "";
    if (msg->log_level <= MPV_LOG_LEVEL_ERROR) {
        qCWarning(lcMpv, "[%s] %s", prefix, qUtf8Printable(text));
    } else if (msg->log_level == MPV_LOG_LEVEL_WARN) {
        qCWarning(lcMpv, "[%s] %s", prefix, qUtf8Printable(text));
    } else {
        qCDebug(lcMpv, "[%s] %s", prefix, qUtf8Printable(text));
    }
}

void handleEndFileEvent(MpvHandle *self, const mpv_event *event)
{
    const auto *data = static_cast<const mpv_event_end_file *>(event->data);
    MpvHandle::EndFileReason reason = MpvHandle::EndFileReason::Unknown;
    QString error;
    qint64 entryId = -1;

    if (data != nullptr) {
        entryId = data->playlist_entry_id;
        switch (data->reason) {
        case MPV_END_FILE_REASON_EOF:
            reason = MpvHandle::EndFileReason::Eof;
            break;
        case MPV_END_FILE_REASON_STOP:
            reason = MpvHandle::EndFileReason::Stop;
            break;
        case MPV_END_FILE_REASON_QUIT:
            reason = MpvHandle::EndFileReason::Quit;
            break;
        case MPV_END_FILE_REASON_ERROR:
            reason = MpvHandle::EndFileReason::Error;
            error = QString::fromUtf8(mpv_error_string(data->error));
            break;
        case MPV_END_FILE_REASON_REDIRECT:
            reason = MpvHandle::EndFileReason::Redirect;
            break;
        default:
            reason = MpvHandle::EndFileReason::Unknown;
            break;
        }
    }
    emit self->endFile(entryId, reason, error);
}

} // namespace

MpvHandle::OptionList MpvHandle::defaultOptions()
{
    return {
        { QStringLiteral("vid"), QStringLiteral("no") },
        { QStringLiteral("audio-display"), QStringLiteral("no") },
        { QStringLiteral("gapless-audio"), QStringLiteral("weak") },
        { QStringLiteral("replaygain"), QStringLiteral("track") },
        { QStringLiteral("idle"), QStringLiteral("yes") },
        { QStringLiteral("prefetch-playlist"), QStringLiteral("yes") },
        { QStringLiteral("config"), QStringLiteral("no") },
        { QStringLiteral("load-scripts"), QStringLiteral("no") },
        { QStringLiteral("ytdl"), QStringLiteral("no") },
        { QStringLiteral("input-default-bindings"), QStringLiteral("no") },
        { QStringLiteral("input-vo-keyboard"), QStringLiteral("no") },
        { QStringLiteral("osc"), QStringLiteral("no") },
        { QStringLiteral("terminal"), QStringLiteral("no") },
        { QStringLiteral("audio-client-name"), QStringLiteral("linernotes") },
    };
}

MpvHandle::MpvHandle(const OptionList &extraOptions, QObject *parent)
    : QObject(parent)
{
    // mpv_create requires LC_NUMERIC to be "C" for correct parsing of float numbers.
    std::setlocale(LC_NUMERIC, "C");

    mpv_handle *ctx = mpv_create();
    if (ctx == nullptr) {
        m_isValid = false;
        m_errorString = QStringLiteral("Failed to create mpv handle");
        qCWarning(lcPlayer) << m_errorString;
        return;
    }
    m_handle.reset(ctx);

    OptionList options = defaultOptions();
    for (const auto &extra : extraOptions) {
        bool replaced = false;
        for (auto &opt : options) {
            if (opt.first == extra.first) {
                opt.second = extra.second;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            options.append(extra);
        }
    }

    for (const auto &[key, value] : options) {
        const QByteArray keyBa = key.toUtf8();
        const QByteArray valBa = value.toUtf8();
        const int err = mpv_set_option_string(m_handle.get(), keyBa.constData(), valBa.constData());
        if (err < 0) {
            qCWarning(lcPlayer) << "Failed to set option" << key << "=" << value << ":"
                                << mpv_error_string(err);
        }
    }

    mpv_request_log_messages(m_handle.get(), "warn");
    mpv_set_wakeup_callback(m_handle.get(), &MpvHandle::onWakeup, this);

    const int initErr = mpv_initialize(m_handle.get());
    if (initErr < 0) {
        m_isValid = false;
        m_errorString = QString::fromUtf8(mpv_error_string(initErr));
        qCWarning(lcPlayer) << "mpv_initialize failed:" << m_errorString;
        mpv_set_wakeup_callback(m_handle.get(), nullptr, nullptr);
        m_handle.reset();
        return;
    }

    m_isValid = true;
    m_errorString.clear();
}

MpvHandle::~MpvHandle()
{
    if (m_handle) {
        mpv_set_wakeup_callback(m_handle.get(), nullptr, nullptr);
        m_handle.reset();
    }
}

bool MpvHandle::isValid() const
{
    return m_isValid;
}

QString MpvHandle::errorString() const
{
    return m_errorString;
}

bool MpvHandle::command(const QStringList &args, bool warnOnFailure)
{
    if (!isValid() || !m_handle || args.isEmpty()) {
        return false;
    }

    QList<QByteArray> utf8Args;
    utf8Args.reserve(args.size());
    std::vector<const char *> cArgs;
    cArgs.reserve(static_cast<size_t>(args.size()) + 1);

    for (const QString &arg : args) {
        utf8Args.append(arg.toUtf8());
        cArgs.push_back(utf8Args.last().constData());
    }
    cArgs.push_back(nullptr);

    const int err = mpv_command(m_handle.get(), cArgs.data());
    if (err < 0) {
        if (warnOnFailure) {
            qCWarning(lcPlayer) << "Command failed:" << args << "error:" << mpv_error_string(err);
        } else {
            qCDebug(lcPlayer) << "Command failed:" << args << "error:" << mpv_error_string(err);
        }
        return false;
    }
    return true;
}

bool MpvHandle::setProperty(const QString &name, const QVariant &value)
{
    if (!isValid() || !m_handle || name.isEmpty()) {
        return false;
    }

    const QByteArray nameUtf8 = name.toUtf8();
    int err = 0;
    if (value.metaType().id() == QMetaType::QString) {
        const QByteArray valUtf8 = value.toString().toUtf8();
        err = mpv_set_property_string(m_handle.get(), nameUtf8.constData(), valUtf8.constData());
    } else {
        MpvNodeBuilder builder;
        mpv_node node = builder.create(value);
        err = mpv_set_property(m_handle.get(), nameUtf8.constData(), MPV_FORMAT_NODE, &node);
    }

    if (err < 0) {
        qCWarning(lcPlayer) << "Failed to set property" << name << "to" << value << ":"
                            << mpv_error_string(err);
        return false;
    }
    return true;
}

QVariant MpvHandle::property(const QString &name) const
{
    if (!isValid() || !m_handle || name.isEmpty()) {
        return { };
    }

    mpv_node node;
    const QByteArray nameUtf8 = name.toUtf8();
    const int err = mpv_get_property(m_handle.get(), nameUtf8.constData(), MPV_FORMAT_NODE, &node);
    if (err < 0) {
        return { };
    }

    const QVariant result = nodeToVariant(&node);
    mpv_free_node_contents(&node);
    return result;
}

void MpvHandle::observeProperty(const QString &name)
{
    if (!isValid() || !m_handle || name.isEmpty()) {
        return;
    }

    if (m_propertyToUserdata.contains(name)) {
        return;
    }

    const quint64 id = m_nextObserveId++;
    m_observedProperties.insert(id, name);
    m_propertyToUserdata.insert(name, id);

    const QByteArray nameUtf8 = name.toUtf8();
    const int err = mpv_observe_property(m_handle.get(), id, nameUtf8.constData(), MPV_FORMAT_NODE);
    if (err < 0) {
        qCWarning(lcPlayer) << "Failed to observe property" << name << ":" << mpv_error_string(err);
        m_observedProperties.remove(id);
        m_propertyToUserdata.remove(name);
    }
}

void MpvHandle::onWakeup(void *ctx) noexcept
{
    auto *self = static_cast<MpvHandle *>(ctx);
    if (self != nullptr) {
        if (!self->m_drainPending.exchange(true, std::memory_order_acq_rel)) {
            QMetaObject::invokeMethod(self, &MpvHandle::drainEvents, Qt::QueuedConnection);
        }
    }
}

void MpvHandle::drainEvents()
{
    m_drainPending.store(false, std::memory_order_release);

    while (m_handle) {
        const mpv_event *event = mpv_wait_event(m_handle.get(), 0);
        if (event == nullptr || event->event_id == MPV_EVENT_NONE) {
            break;
        }

        switch (event->event_id) {
        case MPV_EVENT_LOG_MESSAGE:
            handleLogMessage(event);
            break;
        case MPV_EVENT_PROPERTY_CHANGE: {
            const auto it = m_observedProperties.constFind(event->reply_userdata);
            if (it != m_observedProperties.constEnd()) {
                const QString &propName = it.value();
                const auto *prop = static_cast<const mpv_event_property *>(event->data);
                QVariant value;
                if (prop != nullptr && prop->format == MPV_FORMAT_NODE && prop->data != nullptr) {
                    value = nodeToVariant(static_cast<const mpv_node *>(prop->data));
                }
                emit propertyChanged(propName, value);
            }
            break;
        }
        case MPV_EVENT_START_FILE: {
            const auto *data = static_cast<const mpv_event_start_file *>(event->data);
            const qint64 entryId = (data != nullptr) ? data->playlist_entry_id : -1;
            emit startFile(entryId);
            break;
        }
        case MPV_EVENT_FILE_LOADED:
            emit fileLoaded();
            break;
        case MPV_EVENT_AUDIO_RECONFIG:
            emit audioReconfigured();
            break;
        case MPV_EVENT_END_FILE:
            handleEndFileEvent(this, event);
            break;
        case MPV_EVENT_SHUTDOWN:
            emit shutdown();
            break;
        default:
            break;
        }
    }
}

} // namespace linernotes::player
