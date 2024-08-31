#include "SignalEmitter.h"

SignalEmitter::SignalEmitter(QObject *parent) : QObject(parent) {
    qDebug() << "SignalEmitter: Initialized with parent" << parent;
}
