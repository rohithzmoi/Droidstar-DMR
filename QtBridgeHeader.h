#ifndef QTBRIDGEHEADER_H
#define QTBRIDGEHEADER_H

#ifdef __OBJC__
// Include the main Qt header that will include all necessary QtCore classes
// This assumes Qt is properly set up in your include paths
#include <QtCore>

// Forward declarations for Qt classes we use
class QString;
class QObject;

#endif // __OBJC__

#endif // QTBRIDGEHEADER_H
