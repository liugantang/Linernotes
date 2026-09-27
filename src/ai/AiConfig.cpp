// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QUuid>

#include <ai/AiConfig.h>
#include <ai/AiEnumNames.h>
#include <ai/AiLogging.h>

#include <utility>

namespace linernotes::ai {

namespace {

// NOLINTNEXTLINE(bugprone-throwing-static-initialization) - QString constructor is non-noexcept in
// Qt6
inline const core::SettingKey<QString> kServicesKey { u"ai/services", QString() };
// NOLINTNEXTLINE(bugprone-throwing-static-initialization) - QString constructor is non-noexcept in
// Qt6
inline const core::SettingKey<QString> kDefaultServiceKey { u"ai/defaultService", QString() };
// NOLINTNEXTLINE(bugprone-throwing-static-initialization) - QString constructor is non-noexcept in
// Qt6
inline const core::SettingKey<QString> kRoutesKey { u"ai/routes", QString() };

QJsonObject serviceToJson(const ServiceProfile &profile)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), profile.id);
    obj.insert(QStringLiteral("name"), profile.name);
    obj.insert(QStringLiteral("baseUrl"), profile.baseUrl.toString());
    obj.insert(QStringLiteral("defaultModel"), profile.defaultModel);
    obj.insert(QStringLiteral("timeoutMs"), profile.timeoutMs);
    if (profile.capabilities.has_value()) {
        QJsonObject capsObj;
        capsObj.insert(QStringLiteral("jsonSchema"), profile.capabilities->jsonSchema);
        capsObj.insert(QStringLiteral("tools"), profile.capabilities->tools);
        capsObj.insert(QStringLiteral("jsonObject"), profile.capabilities->jsonObject);
        obj.insert(QStringLiteral("capabilities"), capsObj);
    }
    return obj;
}

std::optional<ServiceProfile> serviceFromJson(const QJsonObject &obj)
{
    if (!obj.contains(QStringLiteral("id")) || !obj.contains(QStringLiteral("name"))) {
        return std::nullopt;
    }
    const QString id = obj.value(QStringLiteral("id")).toString();
    const QString name = obj.value(QStringLiteral("name")).toString();
    const QUrl baseUrl = QUrl(obj.value(QStringLiteral("baseUrl")).toString());
    const QString defaultModel = obj.value(QStringLiteral("defaultModel")).toString();
    const int timeoutMs = obj.contains(QStringLiteral("timeoutMs"))
        ? obj.value(QStringLiteral("timeoutMs")).toInt(60000)
        : 60000;

    std::optional<Capabilities> caps;
    if (obj.contains(QStringLiteral("capabilities"))
        && obj.value(QStringLiteral("capabilities")).isObject()) {
        const QJsonObject capsObj = obj.value(QStringLiteral("capabilities")).toObject();
        caps = Capabilities {
            .jsonSchema = capsObj.value(QStringLiteral("jsonSchema")).toBool(false),
            .tools = capsObj.value(QStringLiteral("tools")).toBool(false),
            .jsonObject = capsObj.value(QStringLiteral("jsonObject")).toBool(false),
        };
    }

    return ServiceProfile {
        .id = id,
        .name = name,
        .baseUrl = baseUrl,
        .defaultModel = defaultModel,
        .timeoutMs = timeoutMs,
        .capabilities = caps,
    };
}

QString servicesToJsonString(const QList<ServiceProfile> &list)
{
    QJsonArray array;
    for (const auto &profile : list) {
        array.append(serviceToJson(profile));
    }
    const QJsonDocument doc(array);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

QList<ServiceProfile> servicesFromJsonString(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return { };
    }
    QJsonParseError parseError { };
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcAi) << "Failed to parse ai/services JSON:" << parseError.errorString();
        return { };
    }
    if (!doc.isArray()) {
        qCWarning(lcAi) << "ai/services is not a JSON array";
        return { };
    }
    const QJsonArray array = doc.array();
    QList<ServiceProfile> result;
    result.reserve(array.size());
    for (const auto &val : array) {
        if (val.isObject()) {
            const auto profileOpt = serviceFromJson(val.toObject());
            if (profileOpt.has_value()) {
                result.append(*profileOpt);
            }
        }
    }
    return result;
}

QJsonObject routeToJson(const PurposeRoute &route)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("service"), route.serviceId);
    obj.insert(QStringLiteral("model"), route.model);
    return obj;
}

PurposeRoute routeFromJson(const QJsonObject &obj)
{
    return PurposeRoute {
        .serviceId = obj.value(QStringLiteral("service")).toString(),
        .model = obj.value(QStringLiteral("model")).toString(),
    };
}

QString routesToJsonString(const QHash<Purpose, PurposeRoute> &routes)
{
    QJsonObject obj;
    for (auto it = routes.cbegin(); it != routes.cend(); ++it) {
        obj.insert(purposeName(it.key()), routeToJson(it.value()));
    }
    const QJsonDocument doc(obj);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

QHash<Purpose, PurposeRoute> routesFromJsonString(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return { };
    }
    QJsonParseError parseError { };
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcAi) << "Failed to parse ai/routes JSON:" << parseError.errorString();
        return { };
    }
    if (!doc.isObject()) {
        qCWarning(lcAi) << "ai/routes is not a JSON object";
        return { };
    }
    const QJsonObject obj = doc.object();
    QHash<Purpose, PurposeRoute> routes;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const auto pOpt = purposeFromName(it.key());
        if (pOpt.has_value() && it.value().isObject()) {
            routes.insert(*pOpt, routeFromJson(it.value().toObject()));
        }
    }
    return routes;
}

} // namespace

AiConfig::AiConfig(core::Settings &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    load();
}

void AiConfig::load()
{
    m_services = servicesFromJsonString(m_settings.value(kServicesKey));
    m_defaultServiceId = m_settings.value(kDefaultServiceKey);
    m_routes = routesFromJsonString(m_settings.value(kRoutesKey));
}

void AiConfig::persist()
{
    m_settings.setValue(kServicesKey, servicesToJsonString(m_services));
    m_settings.setValue(kDefaultServiceKey, m_defaultServiceId);
    m_settings.setValue(kRoutesKey, routesToJsonString(m_routes));
}

QList<ServiceProfile> AiConfig::services() const
{
    return m_services;
}

std::optional<ServiceProfile> AiConfig::service(const QString &id) const
{
    for (const auto &p : std::as_const(m_services)) {
        if (p.id == id) {
            return p;
        }
    }
    return std::nullopt;
}

QString AiConfig::saveService(ServiceProfile profile)
{
    if (profile.id.trimmed().isEmpty()) {
        profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    const bool wasEmpty = m_services.isEmpty();
    bool replaced = false;
    for (int i = 0; i < m_services.size(); ++i) {
        if (m_services.at(i).id == profile.id) {
            m_services.replace(i, profile);
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        m_services.append(profile);
    }

    if (wasEmpty || m_defaultServiceId.isEmpty() || !service(m_defaultServiceId).has_value()) {
        m_defaultServiceId = profile.id;
    }

    persist();
    emit changed();
    return profile.id;
}

void AiConfig::removeService(const QString &id)
{
    int removeIndex = -1;
    for (int i = 0; i < m_services.size(); ++i) {
        if (m_services.at(i).id == id) {
            removeIndex = i;
            break;
        }
    }
    if (removeIndex == -1) {
        return;
    }
    m_services.removeAt(removeIndex);

    if (m_defaultServiceId == id) {
        m_defaultServiceId = m_services.isEmpty() ? QString() : m_services.at(0).id;
    }

    for (auto it = m_routes.begin(); it != m_routes.end(); ++it) {
        if (it.value().serviceId == id) {
            it.value().serviceId.clear();
            it.value().model.clear();
        }
    }

    persist();
    emit changed();
}

void AiConfig::setCapabilities(const QString &id, const Capabilities &caps)
{
    bool found = false;
    for (auto &profile : m_services) {
        if (profile.id == id) {
            profile.capabilities = caps;
            found = true;
            break;
        }
    }
    if (!found) {
        return;
    }
    persist();
    emit changed();
}

QString AiConfig::defaultServiceId() const
{
    return m_defaultServiceId;
}

void AiConfig::setDefaultServiceId(const QString &id)
{
    if (id.isEmpty() || m_defaultServiceId == id || !service(id).has_value()) {
        return;
    }
    m_defaultServiceId = id;
    persist();
    emit changed();
}

PurposeRoute AiConfig::route(Purpose purpose) const
{
    return m_routes.value(purpose);
}

void AiConfig::setRoute(Purpose purpose, const PurposeRoute &route)
{
    m_routes.insert(purpose, route);
    persist();
    emit changed();
}

std::optional<ResolvedService> AiConfig::resolve(Purpose purpose) const
{
    if (m_services.isEmpty()) {
        return std::nullopt;
    }

    const PurposeRoute r = route(purpose);
    std::optional<ServiceProfile> chosenProfile;

    if (!r.serviceId.isEmpty()) {
        chosenProfile = service(r.serviceId);
    }

    if (!chosenProfile.has_value()) {
        if (!m_defaultServiceId.isEmpty()) {
            chosenProfile = service(m_defaultServiceId);
        }
        if (!chosenProfile.has_value()) {
            chosenProfile = m_services.at(0);
        }
    }

    if (!chosenProfile.has_value()) {
        return std::nullopt;
    }

    const ServiceProfile &profile = *chosenProfile;

    QString modelName;
    if (!r.model.trimmed().isEmpty()) {
        modelName = r.model.trimmed();
    } else {
        modelName = profile.defaultModel.trimmed();
    }

    if (modelName.isEmpty()) {
        return std::nullopt;
    }

    return ResolvedService {
        .profile = profile,
        .model = modelName,
    };
}

} // namespace linernotes::ai
