#include "pco/credentialstore.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

using namespace visualis;

namespace {

constexpr char kAccount[] = "personal-access-token";

QString message(OSStatus status)
{
    CFStringRef text = SecCopyErrorMessageString(status, nullptr);
    const QString s = text ? QString::fromCFString(text) : QStringLiteral("error %1").arg(status);
    if (text)
        CFRelease(text);
    return s;
}

// A base query for this entry; the caller owns the result.
CFMutableDictionaryRef query(const QString &service)
{
    CFMutableDictionaryRef q = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                         &kCFTypeDictionaryValueCallBacks);
    CFStringRef svc = service.toCFString();
    CFStringRef account = QString::fromLatin1(kAccount).toCFString();
    CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(q, kSecAttrService, svc);
    CFDictionarySetValue(q, kSecAttrAccount, account);
    CFRelease(svc);
    CFRelease(account);
    return q;
}

} // namespace

bool CredentialStore::isSupported()
{
    return true;
}

CredentialStore::Result CredentialStore::save(const PcoCredentials &credentials) const
{
    const QByteArray data = QJsonDocument(QJsonObject{{"appId", credentials.appId.trimmed()},
                                                      {"secret", credentials.secret.trimmed()}}).toJson(QJsonDocument::Compact);
    CFMutableDictionaryRef attributes = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                                  &kCFTypeDictionaryValueCallBacks);
    CFDataRef value = CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(data.constData()), data.size());
    CFStringRef label = QStringLiteral("Visualis: Planning Center").toCFString();
    CFDictionarySetValue(attributes, kSecValueData, value);
    CFDictionarySetValue(attributes, kSecAttrLabel, label);
    // Readable only on this Mac and only while it's unlocked; never synced to iCloud.
    CFDictionarySetValue(attributes, kSecAttrAccessible, kSecAttrAccessibleWhenUnlockedThisDeviceOnly);

    // Update in place rather than delete and add, so a failure never loses the working token.
    CFMutableDictionaryRef q = query(m_service);
    OSStatus status = SecItemUpdate(q, attributes);
    if (status == errSecItemNotFound) {
        CFDictionaryApplyFunction(attributes, [](const void *key, const void *v, void *target) {
            CFDictionarySetValue(static_cast<CFMutableDictionaryRef>(target), key, v);
        }, q);
        status = SecItemAdd(q, nullptr);
    }
    CFRelease(label);
    CFRelease(value);
    CFRelease(attributes);
    CFRelease(q);
    if (status != errSecSuccess)
        return {false, QStringLiteral("Couldn't save to the Keychain: %1").arg(message(status))};
    return {};
}

std::optional<PcoCredentials> CredentialStore::load(QString *error) const
{
    CFMutableDictionaryRef q = query(m_service);
    CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(q, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching(q, &result);
    CFRelease(q);
    if (status == errSecItemNotFound)
        return std::nullopt;
    if (status != errSecSuccess || !result) {
        if (error)
            *error = QStringLiteral("Couldn't read the Keychain: %1").arg(message(status));
        return std::nullopt;
    }
    const QByteArray data = QByteArray::fromCFData(static_cast<CFDataRef>(result));
    CFRelease(result);
    const QJsonObject o = QJsonDocument::fromJson(data).object();
    PcoCredentials c{o.value("appId").toString(), o.value("secret").toString()};
    if (!c.isValid()) {
        if (error)
            *error = QStringLiteral("The saved Planning Center token is damaged; sign in again.");
        return std::nullopt;
    }
    return c;
}

CredentialStore::Result CredentialStore::remove() const
{
    CFMutableDictionaryRef q = query(m_service);
    const OSStatus status = SecItemDelete(q);
    CFRelease(q);
    if (status != errSecSuccess && status != errSecItemNotFound)
        return {false, QStringLiteral("Couldn't remove it from the Keychain: %1").arg(message(status))};
    return {};
}
