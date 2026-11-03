//
//  DroidStarLiveActivityExtensionBundle.swift
//  DroidStarLiveActivityExtension
//
//  Created by Rohith Namboothiri on 03/10/26.
//

import WidgetKit
import SwiftUI

@main
struct DroidStarLiveActivityExtensionBundle: WidgetBundle {
    var body: some Widget {
        if #available(iOS 16.1, *) {
            DroidStarLiveActivityExtensionLiveActivity()
        }
    }
}
