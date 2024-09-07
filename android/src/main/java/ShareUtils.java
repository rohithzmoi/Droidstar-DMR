/*
    Original Copyright (C) 2019-2021 Doug McLain
    Modification Copyright (C) 2024 Rohith Namboothiri

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

package com.dmr.droidstardmr;

import android.content.Intent;
import android.net.Uri;
import android.content.Context;
import androidx.core.content.FileProvider;

import java.io.File;

public class ShareUtils {

    public static void shareFile(Context context, String filePath) {
        File file = new File(filePath);
        if (file.exists()) {
            // Use the hardcoded authority string
          Uri fileUri = FileProvider.getUriForFile(context, "com.dmr.droidstardmr.fileprovider", file);

            // Create an intent to share the file
            Intent shareIntent = new Intent(Intent.ACTION_SEND);
            shareIntent.setType("text/csv"); // Adjust MIME type as needed
            shareIntent.putExtra(Intent.EXTRA_STREAM, fileUri);
            shareIntent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);

            // Start the share intent
            context.startActivity(Intent.createChooser(shareIntent, "Share File"));
        }
    }
}
