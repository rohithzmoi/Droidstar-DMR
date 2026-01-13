import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Controls.Material
import org.dudetronics.droidstar

// Legacy screens live in the parent QRC directory (qrc:/DroidStar/).
// Import it so types like `MainTab`, `QsoTab`, etc are resolvable from this folder.
import ".."

import "theme"
import "components"

ApplicationWindow {
    id: window

    readonly property bool isMobile: (Qt.platform.os === "ios" || Qt.platform.os === "android")

    visible: true
    // On phones/tablets, always fill the display; on desktop keep a reasonable default.
    visibility: isMobile ? Window.FullScreen : Window.Windowed
    width: isMobile ? Screen.width : 360
    height: isMobile ? Screen.height : 640
    title: qsTr("DroidStar")

    // Material base theme
    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.primary: theme.surface
    color: theme.bg

    Theme { id: theme }

    // Keep FontAwesome usage for icons (existing resource).
    FontLoader {
        id: fontAwesome
        source: "fontawesome-webfont.ttf"
    }

    // Dialogs preserved
    MessageDialog {
        id: errorDialog
        title: "Error"
    }
    MessageDialog {
        id: updateDialog
        title: "Updating..."
        text: "Check log tab for details"
    }
    MessageDialog {
        id: vocoderDialog
        title: "No vocoder found"
        text: "No hardware or software vocoder found for this mode. You can still connect, but you will not RX or TX any audio. See the project website (url on the About tab) for info on loading a sw vocoder, or use a USB AMBE dongle (and an OTG adapter on Android devices)"
    }

    // QML DroidStar object is still used for signal/slot wiring.
    // (A context property named \"droidStar\" also exists from C++, but we keep this id for compatibility.)
    DroidStar {
        id: droidstar
    }

    footer: TabBar {
        id: bar
        currentIndex: swiper.currentIndex

        background: Rectangle {
            color: theme.surface2
            border.color: theme.divider
            border.width: 1
        }

        IconTabButton {
            iconFontFamily: fontAwesome.name
            iconText: "\uf015"
            labelText: qsTr("Main")
        }
        IconTabButton {
            iconFontFamily: fontAwesome.name
            iconText: "\uf013"
            labelText: qsTr("Settings")
        }
        IconTabButton {
            iconFontFamily: fontAwesome.name
            iconText: "\uf0ac"
            labelText: qsTr("QSO")
        }
        IconTabButton {
            iconFontFamily: fontAwesome.name
            iconText: "\uf15c"
            labelText: qsTr("Log")
        }
        IconTabButton {
            iconFontFamily: fontAwesome.name
            iconText: "\uf0c0"
            labelText: qsTr("Hosts")
        }
        IconTabButton {
            iconFontFamily: fontAwesome.name
            iconText: "\uf128"
            labelText: qsTr("About")
        }
    }

    SwipeView {
        id: swiper
        anchors.fill: window.contentItem
        currentIndex: bar.currentIndex

        // Existing screens are kept for now; migration will modernize them incrementally.
        MainTab { id: mainTab }
        SettingsTab { id: settingsTab }
        QsoTab {
            id: qsoTab
            mainTab: mainTab
        }
        LogTab { id: logTab }
        HostsTab { id: hostsTab }
        AboutTab { }
    }

    // Connections moved verbatim from legacy `main.qml` to preserve behavior.
    Connections {
        target: Qt.application
        onStateChanged: {
            if (Qt.application.state === Qt.ApplicationSuspended) {
                console.log("Application is suspended, starting background audio session");
                //startBackgroundAudio();  // Call the exposed C++ function directly
            } else if (Qt.application.state === Qt.ApplicationClosing) {
                console.log("Application is closing, resetting connection");
                droidstar.reset_connect_status();
            } else if (Qt.application.state === Qt.ApplicationActive) {
                console.log("Application is active, stopping background audio session");
                // stopBackgroundAudio();  // Call the exposed C++ function directly
            }
        }
    }

    Connections {
        target: droidstar
        Component.onCompleted: {
            mainTab.comboMode.loaded = true;
            droidstar.process_settings();
            settingsTab.comboVocoder.model = droidstar.get_vocoders();
            settingsTab.comboModem.model = droidstar.get_modems();
            settingsTab.comboPlayback.model = droidstar.get_playbacks();
            settingsTab.comboCapture.model = droidstar.get_captures();
            mainTab.data1.font.family = droidstar.get_monofont();
            mainTab.data2.font.family = droidstar.get_monofont();
            mainTab.data3.font.family = droidstar.get_monofont();
            mainTab.data4.font.family = droidstar.get_monofont();
            mainTab.data5.font.family = droidstar.get_monofont();
            mainTab.data6.font.family = droidstar.get_monofont();
        }
        function onSwtx_state(s){
            mainTab.swtxBox.checked = s;
            mainTab.swtxBox.enabled = !s;
        }
        function onSwrx_state(s){
            mainTab.swrxBox.checked = s;
            mainTab.swrxBox.enabled = !s;
        }
        function onMycall_changed(s){
            settingsTab.mycallEdit.text = s;
        }
        function onUrcall_changed(s){
            settingsTab.urcallEdit.text = s;
        }
        function onRptr1_changed(s){
            settingsTab.rptr1Edit.text = s;
        }
        function onRptr2_changed(s){
            settingsTab.rptr2Edit.text = s;
        }
        function onUpdate_devices(){
            settingsTab.comboVocoder.model = droidstar.get_vocoders();
            settingsTab.comboModem.model = droidstar.get_modems();
            settingsTab.comboPlayback.model = droidstar.get_playbacks();
            settingsTab.comboCapture.model = droidstar.get_captures();
        }

        function onMode_changed() {
            //console.log("onMode_changed ", mainTab.comboMode.find(droidstar.get_mode()), ":", droidstar.get_mode(), ":", droidstar.get_ref_host(), ":", droidstar.get_module());
            mainTab.label1.text = droidstar.get_label1();
            mainTab.label2.text = droidstar.get_label2();
            mainTab.label3.text = droidstar.get_label3();
            mainTab.label4.text = droidstar.get_label4();
            mainTab.label5.text = droidstar.get_label5();
            mainTab.label6.text = droidstar.get_label6();
            droidstar.set_modelchange(true);
            mainTab.comboHost.model = droidstar.get_hosts();
            droidstar.set_modelchange(false);
            mainTab.comboMode.currentIndex = mainTab.comboMode.find(droidstar.get_mode());
            if(droidstar.get_mode() === "REF"){
                //mainTab.comboMode.width = mainTab.width / 2;
                mainTab.comboHost.visible = true;
                mainTab.dtmflabel.visible = false;
                mainTab.editIAXDTMF.visible = false;
                mainTab.dtmfsendbutton.visible = false;
                mainTab.comboHost.currentIndex = mainTab.comboHost.find(droidstar.get_ref_host());
                mainTab.comboModule.visible = true;
                mainTab.comboSlot.visible = false;
                mainTab.comboCC.visible = false;
                mainTab.element3.visible = false;
                mainTab.dmrtgidEdit.visible = false;
                mainTab.comboM17CAN.visible = false;
                mainTab.privateBox.visible = false;
                mainTab.sliderMicGain.value = 0.0;
                mainTab.lastHeard.visible = false;
                mainTab.firstRowData.visible = false;
                mainTab.secondRowData.visible = false;
            }
            if(droidstar.get_mode() === "DCS"){
                //mainTab.comboMode.width = mainTab.width / 2;
                mainTab.comboHost.visible = true;
                mainTab.dtmflabel.visible = false;
                mainTab.editIAXDTMF.visible = false;
                mainTab.dtmfsendbutton.visible = false;
                mainTab.comboHost.currentIndex = mainTab.comboHost.find(droidstar.get_dcs_host());
                mainTab.comboModule.visible = true;
                mainTab.comboSlot.visible = false;
                mainTab.comboCC.visible = false;
                mainTab.element3.visible = false;
                mainTab.dmrtgidEdit.visible = false;
                mainTab.comboM17CAN.visible = false;
                mainTab.privateBox.visible = false;
                mainTab.sliderMicGain.value = 0.0;
                mainTab.recentTgLabel.visible = false;
                mainTab.recentTgidsComboBox.visible = false;
                mainTab.lastHeard.visible = false;
                mainTab.lastHeard.visible = false;
                mainTab.firstRowData.visible = false;
                mainTab.secondRowData.visible = false;

            }
            if(droidstar.get_mode() === "XRF"){
                //mainTab.comboMode.width = mainTab.width / 2;
                mainTab.comboHost.visible = true;
                mainTab.dtmflabel.visible = false;
                mainTab.editIAXDTMF.visible = false;
                mainTab.dtmfsendbutton.visible = false;
                mainTab.comboHost.currentIndex = mainTab.comboHost.find(droidstar.get_xrf_host());
                mainTab.comboModule.visible = true;
                mainTab.comboSlot.visible = false;
                mainTab.comboCC.visible = false;
                mainTab.element3.visible = false;
                mainTab.dmrtgidEdit.visible = false;
                mainTab.comboM17CAN.visible = false;
                mainTab.privateBox.visible = false;
                mainTab.sliderMicGain.value = 0.0;
                mainTab.recentTgLabel.visible = false;
                mainTab.recentTgidsComboBox.visible = false;
                mainTab.lastHeard.visible = false;
                mainTab.firstRowData.visible = false;
                mainTab.secondRowData.visible = false;
            }
            if(droidstar.get_mode() === "YSF"){
                //mainTab.comboMode.width = mainTab.width / 2;
                mainTab.comboHost.visible = true;
                mainTab.dtmflabel.visible = false;
                mainTab.editIAXDTMF.visible = false;
                mainTab.dtmfsendbutton.visible = false;
                mainTab.comboHost.currentIndex = mainTab.comboHost.find(droidstar.get_ysf_host());
                mainTab.comboModule.visible = false;
                mainTab.comboSlot.visible = false;
                mainTab.comboCC.visible = false;
                mainTab.element3.visible = false;
                mainTab.dmrtgidEdit.visible = false;
                mainTab.comboM17CAN.visible = false;
                mainTab.privateBox.visible = false;
                mainTab.sliderMicGain.value = 0.5;
                mainTab.recentTgLabel.visible = false;
                mainTab.recentTgidsComboBox.visible = false;
                mainTab.lastHeard.visible = false;
                mainTab.firstRowData.visible = false;
                mainTab.secondRowData.visible = false;
            }
            if(droidstar.get_mode() === "FCS"){
                //mainTab.comboMode.width = mainTab.width / 2;
                mainTab.comboHost.visible = true;
                mainTab.dtmflabel.visible = false;
                mainTab.editIAXDTMF.visible = false;
                mainTab.dtmfsendbutton.visible = false;
                mainTab.comboHost.currentIndex = mainTab.comboHost.find(droidstar.get_fcs_host());
                mainTab.comboModule.visible = false;
                mainTab.comboSlot.visible = false;
                mainTab.comboCC.visible = false;
                mainTab.element3.visible = false;
                mainTab.dmrtgidEdit.visible = false;
                mainTab.comboM17CAN.visible = false;
                mainTab.privateBox.visible = false;
                mainTab.sliderMicGain.value = 0.5;
                mainTab.recentTgLabel.visible = false;
                mainTab.recentTgidsComboBox.visible = false;
                mainTab.lastHeard.visible = false;
                mainTab.firstRowData.visible = false;
                mainTab.secondRowData.visible = false;
            }
            if(droidstar.get_mode() === "DMR"){
                //mainTab.comboMode.width = (mainTab.width / 5) - 5;
                mainTab.comboHost.visible = true;
                mainTab.dtmflabel.visible = false;
                mainTab.editIAXDTMF.visible = false;
                mainTab.dtmfsendbutton.visible = false;
                mainTab.comboHost.currentIndex = mainTab.comboHost.find(droidstar.get_dmr_host());
                mainTab.comboModule.visible = false;
                mainTab.comboSlot.visible = true;
                mainTab.comboCC.visible = true;
                mainTab.element3.text = "TGID";
                mainTab.element3.visible = true;
                mainTab.dmrtgidEdit.visible = true;
                mainTab.comboM17CAN.visible = false;
                mainTab.privateBox.visible = true;
                mainTab.sliderMicGain.value = 0.5;
                mainTab.recentTgLabel.visible = true;
                mainTab.recentTgidsComboBox.visible = true;
                mainTab.lastHeard.visible = true;
                mainTab.firstRowData.visible = true;
                mainTab.secondRowData.visible = true;
            }
            if(droidstar.get_mode() === "P25"){
                //mainTab.comboMode.width = mainTab.width / 2;
                mainTab.comboHost.visible = true;
                mainTab.dtmflabel.visible = false;
                mainTab.editIAXDTMF.visible = false;
                mainTab.dtmfsendbutton.visible = false;
                mainTab.comboHost.currentIndex = mainTab.comboHost.find(droidstar.get_p25_host());
                mainTab.comboModule.visible = false;
                mainTab.comboSlot.visible = false;
                mainTab.comboCC.visible = false;
                mainTab.element3.text = "TGID";
                mainTab.element3.visible = true;
                mainTab.dmrtgidEdit.visible = true;
                mainTab.comboM17CAN.visible = false;
                mainTab.privateBox.visible = false;
                mainTab.sliderMicGain.value = 0.5;
                mainTab.lastHeard.visible = false;
                mainTab.firstRowData.visible = false;
                mainTab.secondRowData.visible = false;
                mainTab.recentTgLabel.visible = true;
                mainTab.recentTgidsComboBox.visible = true;
            }
            if(droidstar.get_mode() === "NXDN"){
                //mainTab.comboMode.width = mainTab.width / 2;
                mainTab.comboHost.visible = true;
                mainTab.dtmflabel.visible = false;
                mainTab.editIAXDTMF.visible = false;
                mainTab.dtmfsendbutton.visible = false;
                mainTab.comboHost.currentIndex = mainTab.comboHost.find(droidstar.get_nxdn_host());
                mainTab.comboModule.visible = false;
                mainTab.comboSlot.visible = false;
                mainTab.comboCC.visible = false;
                mainTab.element3.visible = false;
                mainTab.dmrtgidEdit.visible = false;
                mainTab.comboM17CAN.visible = false;
                mainTab.privateBox.visible = false;
                mainTab.sliderMicGain.value = 0.5;
                mainTab.recentTgLabel.visible = false;
                mainTab.recentTgidsComboBox.visible = false;
                mainTab.lastHeard.visible = false;
                mainTab.firstRowData.visible = false;
                mainTab.secondRowData.visible = false;
            }
            if(droidstar.get_mode() === "M17"){
                //mainTab.comboMode.width = mainTab.width / 2;
                mainTab.comboHost.visible = true;
                mainTab.dtmflabel.visible = false;
                mainTab.editIAXDTMF.visible = false;
                mainTab.dtmfsendbutton.visible = false;
                mainTab.comboHost.currentIndex = mainTab.comboHost.find(droidstar.get_m17_host());
                mainTab.comboModule.currentIndex = mainTab.comboModule.find(droidstar.get_module());
                mainTab.comboModule.visible = true;
                mainTab.comboSlot.visible = false;
                mainTab.comboCC.visible = false;
                mainTab.element3.text = "CAN";
                mainTab.element3.visible = true;
                mainTab.dmrtgidEdit.visible = false;
                mainTab.comboM17CAN.visible = true;
                mainTab.privateBox.visible = false;
                mainTab.sliderMicGain.value = 0.5;
                mainTab.recentTgLabel.visible = false;
                mainTab.recentTgidsComboBox.visible = false;
                mainTab.lastHeard.visible = false;
                mainTab.firstRowData.visible = false;
                mainTab.secondRowData.visible = false;
            }
            if(droidstar.get_mode() === "IAX"){
                //mainTab.comboMode.width = mainTab.width / 2;
                mainTab.comboHost.visible = true;
                mainTab.dtmflabel.visible = true;
                mainTab.editIAXDTMF.visible = true;
                mainTab.dtmfsendbutton.visible = true;
                mainTab.comboHost.currentIndex = mainTab.comboHost.find(droidstar.get_iax_host());
                mainTab.comboModule.visible = false;
                mainTab.comboSlot.visible = false;
                mainTab.comboCC.visible = false;
                mainTab.element3.visible = false;
                mainTab.dmrtgidEdit.visible = false;
                mainTab.comboM17CAN.visible = false;
                mainTab.privateBox.visible = false;
                mainTab.sliderMicGain.value = 0.5;
                mainTab.recentTgLabel.visible = false;
                mainTab.recentTgidsComboBox.visible = false;
                mainTab.lastHeard.visible = false;
                mainTab.firstRowData.visible = false;
                mainTab.secondRowData.visible = false;
            }
        }
        function onUpdate_data() {
            mainTab.data1.text = droidstar.get_data1();
            mainTab.data2.text = droidstar.get_data2();
            mainTab.data3.text = droidstar.get_data3();
            mainTab.data4.text = droidstar.get_data4();
            mainTab.data5.text = droidstar.get_data5();
            mainTab.data6.text = droidstar.get_data6();
            //mainTab.ambestatus.text = droidstar.get_ambestatustxt();
            //mainTab.mmdvmstatus.text = droidstar.get_mmdvmstatustxt();
            settingsTab.ambestatus.text = droidstar.get_ambestatustxt();
            settingsTab.mmdvmstatus.text = droidstar.get_mmdvmstatustxt();
            mainTab.netstatus.text = droidstar.get_netstatustxt();
            ++mainTab.uitimer.rxcnt;
        }
        function onUpdate_settings() {
            //console.log("update_settings comboHost == ", mainTab.comboHost.find(droidstar.get_host()));
            //console.log("update_settings comboModule == ", mainTab.comboModule.find(droidstar.get_module()));
            settingsTab.ipv6.checked = droidstar.get_ipv6();
            settingsTab.xrf2ref.checked = droidstar.get_xrf2ref();
            settingsTab.toggleTX.checked = droidstar.get_toggletx();
            if(droidstar.get_mode() === "REF"){
                mainTab.comboHost.currentIndex = mainTab.comboHost.find(droidstar.get_ref_host());
            }
            if(droidstar.get_mode() === "DCS"){
                mainTab.comboHost.currentIndex = mainTab.comboHost.find(droidstar.get_dcs_host());
            }
            if(droidstar.get_mode() === "XRF"){
                mainTab.comboHost.currentIndex = mainTab.comboHost.find(droidstar.get_xrf_host());
            }
            if(droidstar.get_mode() === "YSF"){
                mainTab.comboHost.currentIndex = mainTab.comboHost.find(droidstar.get_ysf_host());
            }
            if(droidstar.get_mode() === "FCS"){
                mainTab.comboHost.currentIndex = mainTab.comboHost.find(droidstar.get_fcs_host());
            }
            if(droidstar.get_mode() === "DMR"){
                mainTab.comboHost.currentIndex = mainTab.comboHost.find(droidstar.get_dmr_host());
            }
            if(droidstar.get_mode() === "P25"){
                mainTab.comboHost.currentIndex = mainTab.comboHost.find(droidstar.get_p25_host());
            }
            if(droidstar.get_mode() === "NXDN"){
                mainTab.comboHost.currentIndex = mainTab.comboHost.find(droidstar.get_nxdn_host());
            }
            if(droidstar.get_mode() === "M17"){
                mainTab.comboHost.currentIndex = mainTab.comboHost.find(droidstar.get_m17_host());
            }
            if(droidstar.get_mode() === "IAX"){
                mainTab.comboHost.currentIndex = mainTab.comboHost.find(droidstar.get_iax_host());
            }
            mainTab.comboModule.currentIndex = mainTab.comboModule.find(droidstar.get_module());
            settingsTab.callsignEdit.text = droidstar.get_callsign();
            settingsTab.dmridEdit.text = droidstar.get_dmrid();
            settingsTab.comboEssid.currentIndex = settingsTab.comboEssid.find(droidstar.get_essid());
            settingsTab.bmpwEdit.text = droidstar.get_bm_password();
            settingsTab.tgifpwEdit.text = droidstar.get_tgif_password();
            settingsTab.latEdit.text = droidstar.get_latitude();
            settingsTab.lonEdit.text = droidstar.get_longitude();
            settingsTab.locEdit.text = droidstar.get_location();
            settingsTab.descEdit.text = droidstar.get_description();
            settingsTab.urlEdit.text = droidstar.get_url();
            settingsTab.swidEdit.text = droidstar.get_swid();
            settingsTab.pkgidEdit.text = droidstar.get_pkgid();
            settingsTab.dmroptsEdit.text = droidstar.get_dmr_options();
            mainTab.dmrtgidEdit.text = droidstar.get_dmrtgid();
            settingsTab.mycallEdit.text = droidstar.get_mycall();
            settingsTab.urcallEdit.text = droidstar.get_urcall();
            settingsTab.rptr1Edit.text = droidstar.get_rptr1();
            settingsTab.rptr2Edit.text = droidstar.get_rptr2();
            settingsTab.txtimerEdit.text = droidstar.get_txtimeout();

            settingsTab.modemRXFreqEdit.text = droidstar.get_modemRxFreq();
            settingsTab.modemTXFreqEdit.text = droidstar.get_modemTxFreq();
            settingsTab.modemRXOffsetEdit.text = droidstar.get_modemRxOffset();
            settingsTab.modemTXOffsetEdit.text = droidstar.get_modemTxOffset();
            settingsTab.modemRXDCOffsetEdit.text = droidstar.get_modemRxDCOffset();
            settingsTab.modemTXDCOffsetEdit.text = droidstar.get_modemTxDCOffset();
            settingsTab.modemRXLevelEdit.text = droidstar.get_modemRxLevel();
            settingsTab.modemTXLevelEdit.text = droidstar.get_modemTxLevel();
            settingsTab.modemRFLevelEdit.text = droidstar.get_modemRFLevel();
            settingsTab.modemTXDelayEdit.text = droidstar.get_modemTxDelay();
            settingsTab.modemCWIdTXLevelEdit.text = droidstar.get_modemCWIdTxLevel();
            settingsTab.modemDStarTXLevelEdit.text = droidstar.get_modemDstarTxLevel();
            settingsTab.modemDMRTXLevelEdit.text = droidstar.get_modemDMRTxLevel();
            settingsTab.modemYSFTXLevelEdit.text = droidstar.get_modemYSFTxLevel();
            settingsTab.modemP25TXLevelEdit.text = droidstar.get_modemP25TxLevel()
            settingsTab.modemNXDNTXLevelEdit.text = droidstar.get_modemNXDNTxLevel();
            settingsTab.modemBaudEdit.text = droidstar.get_modemBaud();

            hostsTab.hostsTextEdit.text = droidstar.get_local_hosts();
        }
        function onUpdate_log(s) {
            logTab.appendLog(s);
        }

        function onOpen_vocoder_dialog() {
            vocoderDialog.open();
        }

        function onConnect_status_changed(c) {
            if(c === 0){
                if(mainTab.buttonTX.tx){
                    mainTab.buttonTX.tx = false;
                    droidstar.tx_clicked(false);
                    mainTab.txtimer.running = false;
                    mainTab.btntxt.color = "black";
                    mainTab.btntxt.text = "TX";
                }
                mainTab.connectbutton.text = "Connect";
                mainTab.comboMode.enabled = true;
                mainTab.comboHost.enabled = true;
                mainTab.comboModule.enabled = true;
                mainTab.buttonTX.enabled = false;
                mainTab.btntxt.color = "steelblue";
                mainTab.data1.text = "";
                mainTab.data2.text = "";
                mainTab.data3.text = "";
                mainTab.data4.text = "";
                mainTab.data5.text = "";
                mainTab.data6.text = "";
                mainTab.netstatus.text = "Not connected";
                mainTab.sliderMicGain.enabled = true;
            }
            if(c === 1){
                mainTab.connectbutton.text = "Connecting";
                mainTab.comboMode.enabled = false;
                mainTab.comboHost.enabled = false;
                if(mainTab.comboMode.currentText != "REF"){
                    mainTab.comboModule.enabled = false;
                }
                mainTab.sliderMicGain.enabled = false;
            }
            if(c === 2){
                mainTab.connectbutton.text = "Disconnect";
                mainTab.sliderMicGain.enabled = true;
                mainTab.comboMode.enabled = false;
                mainTab.comboHost.enabled = false;
                //droidstar.fetchFirstNameFromDMR(); // Fetch first name API

                if(mainTab.comboMode.currentText != "REF"){
                    mainTab.comboModule.enabled = false;
                }
                if(mainTab.comboMode.currentText === "YSF"){
                    settingsTab.m171600.checked = true;
                }
                if(mainTab.comboMode.currentText === "FCS"){
                    settingsTab.m171600.checked = true;
                }
                if(mainTab.comboMode.currentText === "M17"){
                    if(settingsTab.mmdvmBox.checked){
                        mainTab.comboModule.enabled = true;
                        mainTab.comboHost.enabled = true;
                    }

                    settingsTab.m173200.checked = true;
                }

                mainTab.buttonTX.enabled = true;
                mainTab.btntxt.color = "black";
                mainTab.agcBox.checked = true;
                droidstar.set_debug(settingsTab.debugBox.checked);
            }
            if(c === 3){
            }
            if(c === 4){
                idcheckDialog.open();
                onConnect_status_changed(0);
            }
            if(c === 5){
                errorDialog.text = droidstar.get_error_text();
                if(errorDialog.text == ""){
                    errorDialog.text = "Banned!"
                }
                errorDialog.open();
                droidstar.onConnect_status_changed(0);
            }
        }
    }
}

