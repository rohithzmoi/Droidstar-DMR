/*
    Copyright (C) 2026 Rohith Namboothiri

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef DMRAUTHPROBE_H
#define DMRAUTHPROBE_H

#include <QUdpSocket>
#include <QHostInfo>
#include <QTimer>
#include <QCryptographicHash>
#include <cstring>
#include <functional>
#include <memory>

// One Homebrew login (RPTL -> RPTACK+salt -> RPTK -> RPTACK) then RPTCL, all async on the caller's
// thread. done(socket, result) fires once with: ok | default (TGIF took "passw0rd") | rejected | unreachable.
// The socket deletes itself after done; deleting it earlier cancels the probe silently.
inline QUdpSocket *startDmrAuthProbe(QObject *owner, const QString &host, quint16 port, uint32_t id,
                                     const QString &pass, bool tgifDefaultFallback,
                                     std::function<void(QUdpSocket *, const QString &)> done,
                                     int retryMs = 2500)
{
    struct Probe { int stage = 0; int tries = 0; QString pass; bool triedDefault = false; bool finished = false; QHostAddress addr; };
    auto st = std::make_shared<Probe>();
    st->pass = pass;
    QUdpSocket *sock = new QUdpSocket(owner);
    QTimer *timer = new QTimer(sock);
    timer->setInterval(retryMs);

    auto send = [sock, st, port](const char *tag, int n, const QByteArray &tail) {
        QByteArray o(tag, n);
        o.append(tail);
        sock->writeDatagram(o, st->addr, port);
    };
    auto idBytes = [id]() {
        QByteArray b;
        b.append(char(id >> 24)); b.append(char(id >> 16)); b.append(char(id >> 8)); b.append(char(id));
        return b;
    };
    auto finish = [sock, st, send, idBytes, done](const QString &r) {
        if (st->finished) return;
        st->finished = true;
        if (st->stage == 1) send("RPTCL", 5, idBytes());
        sock->deleteLater();
        done(sock, r);
    };
    auto sendLogin = [st, send, idBytes, timer]() {
        st->stage = 0;
        send("RPTL", 4, idBytes());
        timer->start();
    };

    QObject::connect(timer, &QTimer::timeout, sock, [st, finish, sendLogin]() {
        if (++st->tries >= 3) finish(QStringLiteral("unreachable"));
        else sendLogin();
    });
    QObject::connect(sock, &QUdpSocket::readyRead, sock, [sock, st, tgifDefaultFallback, send, idBytes, timer, finish, sendLogin]() {
        while (!st->finished && sock->hasPendingDatagrams()) {
            QByteArray b(int(qMax<qint64>(0, sock->pendingDatagramSize())), Qt::Uninitialized);
            sock->readDatagram(b.data(), b.size());
            if (b.size() >= 6 && std::memcmp(b.constData() + 3, "NAK", 3) == 0) {
                // TGIF takes the default password when hotspot security is off, same as the live connect.
                if (tgifDefaultFallback && !st->triedDefault && st->pass != QLatin1String("passw0rd")) {
                    st->triedDefault = true;
                    st->pass = QStringLiteral("passw0rd");
                    st->tries = 0;
                    sendLogin();
                    continue;
                }
                finish(QStringLiteral("rejected"));
            } else if (b.size() >= 10 && std::memcmp(b.constData(), "RPTACK", 6) == 0) {
                if (st->stage == 0) {
                    const QByteArray in = b.mid(6, 4) + st->pass.toUtf8();
                    st->stage = 1;
                    send("RPTK", 4, idBytes() + QCryptographicHash::hash(in, QCryptographicHash::Sha256));
                    timer->start();
                } else {
                    finish(st->triedDefault ? QStringLiteral("default") : QStringLiteral("ok"));
                }
            }
        }
    });

    // IPv4 on purpose: this checks credentials, not the user's IPv6 path.
    QHostInfo::lookupHost(host, sock, [st, sock, finish, sendLogin](const QHostInfo &info) {
        for (const QHostAddress &a : info.addresses()) {
            if (a.protocol() == QAbstractSocket::IPv4Protocol) { st->addr = a; break; }
        }
        if (st->addr.isNull() || !sock->bind(QHostAddress(QHostAddress::AnyIPv4), 0)) {
            finish(QStringLiteral("unreachable"));
            return;
        }
        sendLogin();
    });
    return sock;
}

#endif
