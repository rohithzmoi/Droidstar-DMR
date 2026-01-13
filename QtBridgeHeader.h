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
