/*
    Copyright (C) 2025 Rohith Namboothiri

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

#ifndef SIGNALEMITTER_H
#define SIGNALEMITTER_H

#include <QObject>
#include <QDebug>

class SignalEmitter : public QObject {
    Q_OBJECT
public:
    explicit SignalEmitter(QObject *parent = nullptr);
signals:
    void firstNameChanged(const QString &firstName);
};

#endif // SIGNALEMITTER_H
