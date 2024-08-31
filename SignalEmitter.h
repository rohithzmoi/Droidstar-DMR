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
