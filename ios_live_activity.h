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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

bool ios_live_activity_is_available(void);
void ios_live_activity_start_or_update(const char *mode,
                                       const char *callsign,
                                       const char *handle,
                                       const char *country,
                                       const char *tgid);
void ios_live_activity_end(void);
void ios_live_activity_end_all(void);

#ifdef __cplusplus
}
#endif

