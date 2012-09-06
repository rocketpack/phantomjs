/*
  This file is part of the PhantomJS project from Ofi Labs.

  Copyright (C) 2011 Ariya Hidayat <ariya.hidayat@gmail.com>

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "config.h"
#include "cookiejar.h"

#include <QDebug>
#include <QDateTime>
#include <QSettings>
#include <QTimer>

#define COOKIE_JAR_VERSION      1

// Operators needed for Cookie Serialization
QT_BEGIN_NAMESPACE
QDataStream &operator<<(QDataStream &stream, const QList<QNetworkCookie> &list)
{
    stream << COOKIE_JAR_VERSION;
    stream << quint32(list.size());
    for (int i = 0; i < list.size(); ++i)
        stream << list.at(i).toRawForm();
    return stream;
}

QDataStream &operator>>(QDataStream &stream, QList<QNetworkCookie> &list)
{
    list.clear();

    quint32 version;
    stream >> version;

    if (version != COOKIE_JAR_VERSION)
        return stream;

    quint32 count;
    stream >> count;
    for(quint32 i = 0; i < count; ++i)
    {
        QByteArray value;
        stream >> value;
        QList<QNetworkCookie> newCookies = QNetworkCookie::parseCookies(value);
        if (newCookies.count() == 0 && value.length() != 0) {
            qWarning() << "CookieJar: Unable to parse saved cookie:" << value;
        }
        for (int j = 0; j < newCookies.count(); ++j)
            list.append(newCookies.at(j));
        if (stream.atEnd())
            break;
    }
    return stream;
}
QT_END_NAMESPACE


// public:
CookieJar::CookieJar(QString cookiesFile)
    : QNetworkCookieJar()
    , m_cookieStorage(new QSettings(cookiesFile, QSettings::IniFormat, this))
    , m_enabled(true)
{
    // TODO Make a singleton of this
    QTimer::singleShot(0, this, SLOT(load()));
}

CookieJar::~CookieJar()
{
    // On destruction, before saving, clear all the session cookies
    purgeSessionCookies();
    save();
}

bool CookieJar::setCookiesFromUrl(const QList<QNetworkCookie> & cookieList, const QUrl &url)
{
    // Update cookies in memory
    if (isEnabled() && QNetworkCookieJar::setCookiesFromUrl(cookieList, url)) {
        // Update cookies in permanent storage, because at least 1 changed
        save();
        return true;
    }
    // No changes occurred
    return false;
}

QList<QNetworkCookie> CookieJar::cookiesForUrl(const QUrl &url) const
{
    if (isEnabled()) {
        return QNetworkCookieJar::cookiesForUrl(url);
    }
    // The CookieJar is disabled: don't return any cookie
    return QList<QNetworkCookie>();
}

void CookieJar::addCookie(const QNetworkCookie &cookie, const QString &url)
{
    if (isEnabled()) {
        // Save a single cookie
        setCookiesFromUrl(
                    QList<QNetworkCookie>() << cookie, //< unfortunately, "setCookiesFromUrl" requires a list
                    !url.isEmpty() ?
                        url :       //< use given URL
                        QUrl(       //< mock-up a URL
                            (cookie.isSecure() ? "https://" : "http://") +                              //< URL protocol
                            QString(cookie.domain().startsWith('.') ? "www" : "") + cookie.domain() +   //< URL domain
                            (cookie.path().isEmpty() ? "/" : cookie.path())));                          //< URL path
    }
}

void CookieJar::addCookieFromMap(const QVariantMap &cookie, const QString &url)
{
    QNetworkCookie newCookie;

    // The cookie must have "domain", "name" and "value"
    if (!cookie["domain"].isNull() && !cookie["domain"].toString().isEmpty() &&
        !cookie["name"].isNull() && !cookie["name"].toString().isEmpty() &&
        !cookie["value"].isNull()
    ) {
        newCookie.setDomain(cookie["domain"].toString());
        newCookie.setName(cookie["name"].toByteArray());
        newCookie.setValue(cookie["value"].toByteArray());

        newCookie.setPath((cookie["path"].isNull() || cookie["path"].toString().isEmpty()) ?
                              "/" :cookie["path"].toString());
        newCookie.setHttpOnly(cookie["httponly"].isNull() ? false : cookie["httponly"].toBool());
        newCookie.setSecure(cookie["secure"].isNull() ? false : cookie["secure"].toBool());

        if (!cookie["expires"].isNull()) {
            QString datetime = cookie["expires"].toString().replace(" GMT", "");
            QDateTime expires = QDateTime::fromString(datetime, "ddd, dd MMM yyyy hh:mm:ss");
            if (expires.isValid()) {
                newCookie.setExpirationDate(expires);
            }
        }

        addCookie(newCookie, url);
    }
}

void CookieJar::addCookies(const QList<QNetworkCookie> &cookiesList, const QString &url)
{
    for (int i = cookiesList.length() -1; i >=0; --i) {
        addCookie(cookiesList.at(i), url);
    }
}

void CookieJar::addCookiesFromMap(const QVariantList &cookiesList, const QString &url)
{
    for (int i = cookiesList.length() -1; i >= 0; --i) {
        addCookieFromMap(cookiesList.at(i).toMap(), url);
    }
}

QList<QNetworkCookie> CookieJar::cookies(const QString &url) const
{
    if (url.isEmpty()) {
        // No url provided: return all the cookies in this CookieJar
        return allCookies();
    } else {
        // Return ONLY the cookies that match this URL
        return cookiesForUrl(url);
    }
}

QVariantList CookieJar::cookiesToMap(const QString &url) const
{
    QVariantList result;
    QNetworkCookie c;
    QVariantMap cookie;

    QList<QNetworkCookie> cookiesList = cookies(url);
    for (int i = cookiesList.length() -1; i >= 0; --i) {
        c = cookiesList.at(i);

        cookie["domain"] = QVariant(c.domain());
        cookie["name"] = QVariant(QString(c.name()));
        cookie["value"] = QVariant(QString(c.value()));
        cookie["path"] = (c.path().isNull() || c.path().isEmpty()) ? QVariant("/") : QVariant(c.path());
        cookie["httponly"] = QVariant(c.isHttpOnly());
        cookie["secure"] = QVariant(c.isSecure());
        if (c.expirationDate().isValid()) {
            cookie["expires"] = QVariant(QString(c.expirationDate().toString("ddd, dd MMM yyyy hh:mm:ss")).append(" GMT"));
        }

        result.append(cookie);
    }

    return result;
}

QNetworkCookie CookieJar::cookie(const QString &name, const QString &url) const
{
    QList<QNetworkCookie> cookiesList = cookies(url);
    for (int i = cookiesList.length() -1; i >= 0; --i) {
        if (cookiesList.at(i).name() == name) {
            return cookiesList.at(i);
        }
    }
    return QNetworkCookie();
}

QVariantMap CookieJar::cookieToMap(const QString &name, const QString &url) const
{
    QVariantMap cookie;

    QVariantList cookiesList = cookiesToMap(url);
    for (int i = cookiesList.length() -1; i >= 0; --i) {
        cookie = cookiesList.at(i).toMap();
        if (cookie["name"].toString() == name) {
            return cookie;
        }
    }
    return QVariantMap();
}

void CookieJar::deleteCookie(const QString &name, const QString &url)
{
    if (isEnabled() && !name.isEmpty()) {
        QNetworkCookie cookie;

        // For all the cookies that are visible to this URL
        QList<QNetworkCookie> cookiesList = cookies(url);
        for (int i = cookiesList.length() -1; i >= 0; --i) {
            cookie = cookiesList.at(i);

            if (cookie.name() == name) {
                // If we found the right cookie, mark it expired so it gets purged
                cookie.setExpirationDate(QDateTime().addDays(-1));
                // Set a new list of cookies for this URL
                setCookiesFromUrl(cookiesList, url);
                return;
            }
        }
    }
}

void CookieJar::deleteCookies(const QString &url)
{
    if (isEnabled()) {
        if (url.isEmpty()) {
            // No URL provided: delete ALL the cookies in the CookieJar
            clearCookies();
        } else {
            setCookiesFromUrl(QList<QNetworkCookie>(), url);
        }
    }
}

void CookieJar::clearCookies()
{
    if (isEnabled()) {
        setAllCookies(QList<QNetworkCookie>());
    }
}

void CookieJar::enable()
{
    m_enabled = true;
}

void CookieJar::disable()
{
    m_enabled = false;
}

bool CookieJar::isEnabled() const
{
    return m_enabled;
}

// private:
void CookieJar::save()
{
    if (isEnabled()) {
        // Get rid of all the Cookies that have expired
        purgeExpiredCookies();

        // Store cookies
        m_cookieStorage->setValue(QLatin1String("cookies"), QVariant::fromValue<QList<QNetworkCookie> >(allCookies()));
    }
}

bool CookieJar::purgeExpiredCookies()
{
    QList<QNetworkCookie> cookies = allCookies();

    // If empty, there is nothing to purge
    if (cookies.isEmpty()) {
        return false;
    }

    // Check if any cookie has expired
    int prePurgeCookiesCount = cookies.count();
    QDateTime now = QDateTime::currentDateTime();
    for (int i = cookies.count() - 1; i >= 0; --i) {
        if (!cookies.at(i).isSessionCookie() && cookies.at(i).expirationDate() < now) {
            cookies.removeAt(i);
        }
    }

    // Returns "true" if at least 1 cookie expired and has been removed
    return prePurgeCookiesCount != cookies.count();
}

bool CookieJar::purgeSessionCookies()
{
    QList<QNetworkCookie> cookies = allCookies();

    // If empty, there is nothing to purge
    if (cookies.isEmpty()) {
        return false;
    }

    // Check if any cookie has expired
    int prePurgeCookiesCount = cookies.count();
    for (int i = cookies.count() - 1; i >= 0; --i) {
        if (cookies.at(i).isSessionCookie()) {
            cookies.removeAt(i);
        }
    }

    // Returns "true" if at least 1 session cookie was found and removed
    return prePurgeCookiesCount != cookies.count();
}

void CookieJar::load()
{
    if (isEnabled()) {
        // Register a "StreamOperator" for this Meta Type, so we can easily serialize/deserialize the cookies
        qRegisterMetaTypeStreamOperators<QList<QNetworkCookie> >("QList<QNetworkCookie>");

        // Load all the cookies
        setAllCookies(qvariant_cast<QList<QNetworkCookie> >(m_cookieStorage->value(QLatin1String("cookies"))));

        // If any cookie has expired since last execution, purge and save before going any further
        if (purgeExpiredCookies()) {
            save();
        }
    }
}


