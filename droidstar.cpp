/*
    Copyright (C) 2019-2021 Doug McLain
    Modification Copyright (C) 2024-2026 Rohith Namboothiri

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

#include "droidstar.h"
#include "dmr.h"
#include "httpmanager.h"
#include "TranscriptionEngine.h"
#include <QGuiApplication>
#include <QTimer>
#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#endif
#ifdef Q_OS_IOS
#include "micpermission.h"
#include "AudioSessionManager.h"
#include "ios_live_activity.h"
#endif
#include <QStandardPaths>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <cstring>
#include <stdio.h>
#include <fcntl.h>
#include <iostream>

static DroidStar *s_droidStarInstance = nullptr;

static void txEnhancerStateChangedCallback() {
    if (s_droidStarInstance) {
        QMetaObject::invokeMethod(s_droidStarInstance, "update_settings", Qt::QueuedConnection);
    }
}

#ifdef Q_OS_IOS
static void pttPressCallback() {
    if (s_droidStarInstance) {
        QMetaObject::invokeMethod(s_droidStarInstance, "press_tx", Qt::QueuedConnection);
    }
}

static void pttReleaseCallback() {
    if (s_droidStarInstance) {
        QMetaObject::invokeMethod(s_droidStarInstance, "release_tx", Qt::QueuedConnection);
    }
}
#endif

DroidStar::DroidStar(QObject *parent) :
    QObject(parent),
    m_audioEngine(new AudioEngine("defaultInputDevice", "defaultOutputDevice")),
    connect_status(Mode::DISCONNECTED),
    m_update_host_files(false),
    m_settings(nullptr),
    m_pc(0),
    m_dmrid(0),
    m_essid(0),
    m_dmr_srcid(0),
    m_dmr_destid(0),
    m_module('A'),
    m_port(0),
    m_txtimeout(300),
    m_toggletx(true),
    m_modethread(nullptr),
    m_mode(nullptr),
    m_iaxport(0),
    m_settings_processed(false),
    m_modelchange(false),
    m_outlevel(0),
    m_xrf2ref(false),
    m_ipv6(false),
    m_mdirect(false),
    m_tts(0),
    m_modemTxInvert(true),
    m_modemRxInvert(false),
    m_modemPTTInvert(false),
    m_reconnectTimer(new QTimer(this)),
    m_keepAliveTimer(new QTimer(this))
{
    // Connect the audio device list update signal to a slot
      connect(m_audioEngine, &AudioEngine::audioDeviceListChanged, this, &DroidStar::updateDeviceListInQML);

      // Discover initial audio devices
      QStringList playbackDevices = m_audioEngine->discover_audio_devices(1);  // 1 for playback devices
      QStringList captureDevices = m_audioEngine->discover_audio_devices(0);   // 0 for capture devices

    
    qRegisterMetaType<Mode::MODEINFO>("Mode::MODEINFO");
    m_settings_processed = false;
    m_modelchange = false;
    connect_status = Mode::DISCONNECTED;
    m_settings = new QSettings(QSettings::IniFormat, QSettings::UserScope, "dudetronics", "droidstar", this);
    qDebug() << "QSettings file:" << m_settings->fileName()
             << "exists:" << QFileInfo(m_settings->fileName()).exists();
    config_path = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
   
        connect(m_reconnectTimer, &QTimer::timeout, this, &DroidStar::attempt_reconnect);
        connect(m_keepAliveTimer, &QTimer::timeout, this, &DroidStar::send_keep_alive);

  


        // Start timers
        m_reconnectTimer->setInterval(5000);
        m_keepAliveTimer->setInterval(30000);

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_WIN)
    config_path += "/dudetronics";
#endif
#if defined(Q_OS_ANDROID)
    keepScreenOn();
    m_USBmonitor = &AndroidSerialPort::GetInstance();
    connect(m_USBmonitor, SIGNAL(devices_changed()), this, SLOT(discover_devices()));
#endif
    check_host_files();
    discover_devices();
    process_settings();

    m_hoselineNam = new QNetworkAccessManager(this);
    m_hoselineUiTimer = new QTimer(this);
    m_hoselineUiTimer->setInterval(800);
    connect(m_hoselineUiTimer, &QTimer::timeout, this, &DroidStar::onHoselineUiTick);

    m_hoselineReconnectTimer = new QTimer(this);
    m_hoselineReconnectTimer->setSingleShot(true);
    connect(m_hoselineReconnectTimer, &QTimer::timeout, this, [this]() {
        if (m_hoselineActive) {
            startHoselineSession();
        }
    });

    m_tuneReconnectTimer = new QTimer(this);
    m_tuneReconnectTimer->setSingleShot(true);
    connect(m_tuneReconnectTimer, &QTimer::timeout, this, [this]() {
        m_tuningReconnect = false;
        if (connect_status == Mode::DISCONNECTED) {
            process_connect();
        }
    });

    m_autoTxPunchStartTimer = new QTimer(this);
    m_autoTxPunchStartTimer->setSingleShot(true);
    connect(m_autoTxPunchStartTimer, &QTimer::timeout, this, [this]() {
        if (connect_status != Mode::CONNECTED_RW || m_protocol != "DMR" || !m_mode || m_dmr_destid == 0U || m_pc != 0) {
            return;
        }
        emit slot_changed(m_dmr_slot);
        emit cc_changed(m_dmr_cc);
        emit dmrpc_state_changed(m_pc);
        emit dmr_tgid_changed(m_dmr_destid);
        QMetaObject::invokeMethod(m_mode, "relink_talkgroup", Qt::QueuedConnection);
    });

    m_autoTxPunchStopTimer = new QTimer(this);
    m_autoTxPunchStopTimer->setSingleShot(true);
    connect(m_autoTxPunchStopTimer, &QTimer::timeout, this, [this]() {
        if (!m_autoTxPunchRunning) {
            return;
        }
        m_autoTxPunchRunning = false;
        if (connect_status == Mode::CONNECTED_RW) {
            click_tx(false);
        }
        if (m_mode) {
            m_mode->set_auto_tx_punch(false);
        }
    });

    initTalkgroupDirectory();
    loadTalkgroupCache();
    loadTgifTalkgroupCache();
    QTimer::singleShot(1200, this, &DroidStar::fetchBrandMeisterTalkgroups);
    QTimer::singleShot(1800, this, &DroidStar::fetchTgifTalkgroups);

    qDebug() << "CPU arch: " << QSysInfo::currentCpuArchitecture();
    qDebug() << "Build ABI: " << QSysInfo::buildAbi();
    qDebug() << "boot ID: " << QSysInfo::bootUniqueId();
    qDebug() << "Pretty name: " << QSysInfo::prettyProductName();
    qDebug() << "Type: " << QSysInfo::productType();
    qDebug() << "Version: " << QSysInfo::productVersion();
    qDebug() << "Kernel type: " << QSysInfo::kernelType();
    qDebug() << "Kernel version: " << QSysInfo::kernelVersion();
    qDebug() << "Software version: " << get_software_build();
    
    s_droidStarInstance = this;
    AudioEngine::setStateChangeCallback(txEnhancerStateChangedCallback);
#ifdef Q_OS_IOS
    // Register this instance for PTT callbacks from remote commands (headphones, Control
    setPTTCallbacks(pttPressCallback, pttReleaseCallback);
#endif
}

DroidStar::~DroidStar()
{
    stopHoselineSession();
    AudioEngine::setStateChangeCallback(nullptr);
    s_droidStarInstance = nullptr;
#ifdef Q_OS_IOS
    setPTTCallbacks(nullptr, nullptr);
    setAudioConnectionState(false, "", "");
#endif
    // Tear the Mode down on its own thread (stops timers/audio/UDP) before
    stop_mode_thread();
    // Host-file download threads are parented to this object; a QThread deleted
    const QList<QThread *> workers = findChildren<QThread *>(QString(), Qt::FindDirectChildrenOnly);
    for (QThread *worker : workers) {
        if (worker->isRunning()) {
            worker->quit();
            if (!worker->wait(1500)) {
                worker->setParent(nullptr);
            }
        }
    }
    delete m_reconnectTimer;
    delete m_keepAliveTimer;
    delete m_audioEngine;
}
#ifdef Q_OS_ANDROID
void DroidStar::keepScreenOn()
{
    char const * const action = "addFlags";
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([action](){
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        QJniObject window = activity.callObjectMethod("getWindow", "()Landroid/view/Window;");

        if (window.isValid()) {
            const int FLAG_KEEP_SCREEN_ON = 128;
            window.callMethod<void>("addFlags", "(I)V", FLAG_KEEP_SCREEN_ON);
        }
    }});

    QMicrophonePermission microphonePermission;
    if (qApp->checkPermission(microphonePermission) != Qt::PermissionStatus::Granted) {
        qApp->requestPermission(microphonePermission, this, &DroidStar::keepScreenOn);
    }
}

void DroidStar::reset_connect_status()
{
    if(connect_status == Mode::CONNECTED_RW){
        connect_status = Mode::CONNECTING;
        process_connect();
    }
}
#endif

void DroidStar::updateDeviceListInQML()
{
    // Update the playback and capture devices
    m_playbackDevices = m_audioEngine->discover_audio_devices(1);  // 1 for playback devices
    m_captureDevices = m_audioEngine->discover_audio_devices(0);   // 0 for capture devices

    // Emit signals to notify QML of the changes
    emit playbackDevicesChanged();
    emit captureDevicesChanged();
    //emit audioEngineChanged();
}


void DroidStar::discover_devices()
{
    m_playbacks.clear();
    m_captures.clear();
    m_vocoders.clear();
    m_modems.clear();
    m_playbacks.append("OS Default");
    m_captures.append("OS Default");
    m_vocoders.append("Software vocoder");
    m_modems.append("None");
    m_playbacks.append(m_audioEngine->discover_audio_devices(AUDIO_OUT));
    m_captures.append(m_audioEngine->discover_audio_devices(AUDIO_IN));
#if !defined(Q_OS_IOS)
    QMap<QString, QString> l = SerialAMBE::discover_devices();
    QMap<QString, QString>::const_iterator i = l.constBegin();

    while (i != l.constEnd()) {
        m_vocoders.append(i.value());
        m_modems.append(i.value());
        ++i;
    }
    emit update_devices();
#endif
}

void DroidStar::download_file(QString f, bool u)
{
    HttpManager *http = new HttpManager(f, u);
    QThread *httpThread = new QThread(this);
    http->moveToThread(httpThread);
    connect(httpThread, SIGNAL(started()), http, SLOT(process()));
    if(u){
        connect(http, SIGNAL(file_downloaded(QString)), this, SLOT(url_downloaded(QString)));
    }
    else{
        connect(http, SIGNAL(file_downloaded(QString)), this, SLOT(file_downloaded(QString)));
    }
    connect(httpThread, SIGNAL(finished()), http, SLOT(deleteLater()));
    connect(httpThread, &QThread::finished, httpThread, &QObject::deleteLater);
    httpThread->start();
}

void DroidStar::url_downloaded(QString url)
{
    emit update_log("Downloaded " + url);
}

void DroidStar::file_downloaded(QString filename)
{
    emit update_log("Updated " + filename);
    bool hostsChangedForCurrentMode = false;
    if(filename == "dplus.txt" && m_protocol == "REF"){
        process_dstar_hosts(m_protocol);
        hostsChangedForCurrentMode = true;
    }
    else if(filename == "dextra.txt" && m_protocol == "XRF"){
        process_dstar_hosts(m_protocol);
        hostsChangedForCurrentMode = true;
    }
    else if(filename == "dcs.txt" && m_protocol == "DCS"){
        process_dstar_hosts(m_protocol);
        hostsChangedForCurrentMode = true;
    }
    else if(filename == "YSFHosts.txt" && m_protocol == "YSF"){
        process_ysf_hosts();
        hostsChangedForCurrentMode = true;
    }
    else if(filename == "FCSHosts.txt" && m_protocol == "FCS"){
        process_fcs_rooms();
        hostsChangedForCurrentMode = true;
    }
    else if(filename == "P25Hosts.txt" && m_protocol == "P25"){
        process_p25_hosts();
        hostsChangedForCurrentMode = true;
    }
    else if(filename == "DMRHosts.txt" && m_protocol == "DMR"){
        process_dmr_hosts();
        hostsChangedForCurrentMode = true;
    }
    else if(filename == "NXDNHosts.txt" && m_protocol == "NXDN"){
        process_nxdn_hosts();
        hostsChangedForCurrentMode = true;
    }
    else if(filename == "M17Hosts-full.csv" && m_protocol == "M17"){
        process_m17_hosts();
        hostsChangedForCurrentMode = true;
    }
    else if(filename == "DMRIDs.dat"){
        process_dmr_ids();
    }
    else if(filename == "NXDN.csv"){
        process_nxdn_ids();
    }

    // Critical: when host files are downloaded/updated asynchronously, refresh QML
    if (hostsChangedForCurrentMode) {
        emit mode_changed();
    }
}

void DroidStar::dtmf_send_clicked(QString dtmf)
{
    QByteArray tx(dtmf.simplified().toUtf8(), dtmf.simplified().size());
    emit send_dtmf(tx);
}

void DroidStar::tts_changed(QString tts)
{
    if(tts == "Mic"){
        m_tts = 0;
    }
    else if(tts == "TTS1"){
        m_tts = 1;
    }
    else if(tts == "TTS2"){
        m_tts = 2;
    }
    else if(tts == "TTS3"){
        m_tts = 3;
    }
    else{
        m_tts = 0;
    }
    emit input_source_changed(m_tts, m_ttstxt);
}

void DroidStar::tts_text_changed(QString ttstxt)
{
    m_ttstxt = ttstxt;
    emit input_source_changed(m_tts, m_ttstxt);
}

void DroidStar::stop_mode_thread()
{
    if (m_autoTxPunchStartTimer) m_autoTxPunchStartTimer->stop();
    if (m_autoTxPunchStopTimer) m_autoTxPunchStopTimer->stop();
    m_autoTxPunchRunning = false;

    if (m_mode) {
        disconnect(m_mode, nullptr, this, nullptr);
        disconnect(this, nullptr, m_mode, nullptr);
        if (m_modethread && m_modethread->isRunning() && QThread::currentThread() != m_modethread) {
            QMetaObject::invokeMethod(m_mode, "deleteLater", Qt::BlockingQueuedConnection);
        } else {
            m_mode->deleteLater();
        }
        m_mode = nullptr;
    }
    if (m_modethread) {
        if (QThread::currentThread() != m_modethread) {
            m_modethread->quit();
            if (m_modethread->wait(1500)) {
                delete m_modethread;
            } else {
                // Deleting a QThread that is still running aborts the whole process.
                // Let it finish on its own and free itself afterwards.
                connect(m_modethread, &QThread::finished, m_modethread, &QObject::deleteLater);
            }
        } else {
            m_modethread->quit();
            connect(m_modethread, &QThread::finished, m_modethread, &QObject::deleteLater);
        }
        m_modethread = nullptr;
    }
}

void DroidStar::obtain_asl_wt_creds()
{
    qDebug() << "obtain_asl_wt_creds() called";
    if (m_asl_password.trimmed().isEmpty()) {
        m_errortxt = "ASL WT requires your AllStarLink portal password (set ASL Password in Settings)";
        emit update_log(m_errortxt);
        connect_status = Mode::DISCONNECTED;
        emit connect_status_changed(5);
        return;
    }
    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
    QUrl url("https://www.allstarlink.org/portal/login.php");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QByteArray postData;
    postData.append("user=" + QUrl::toPercentEncoding(m_callsign.toUtf8()));
    postData.append("&pass=" + QUrl::toPercentEncoding(m_asl_password.toUtf8()));

    connect(manager, &QNetworkAccessManager::finished, this, [=, this](QNetworkReply *reply) {
        manager->disconnect();
        if (reply->error() == QNetworkReply::NoError) {
            QUrl wtUrl(QString("https://www.allstarlink.org/portal/webtransceiver.php?node=%1").arg(m_refname));
            QNetworkRequest wtReq(wtUrl);

            connect(manager, &QNetworkAccessManager::finished, this, [=, this](QNetworkReply *wtReply) {
                bool token_found = false;
                manager->disconnect();
                if (wtReply->error() == QNetworkReply::NoError) {
                    QString html = wtReply->readAll();
                    QStringList l = html.split('\n');
                    for (int i = 0; i < l.size(); i++) {
                        if (l.at(i).contains("callingName")) {
                            QStringList ll = l.at(i).split('"');
                            if (ll.size() > 3) {
                                m_wt_callingname = ll.at(3);
                                m_wt_callingname_pass = m_asl_password;
                                qDebug() << "ASL authentication complete call token == " << m_wt_callingname;
                                token_found = true;
                                process_connect();
                                break;
                            }
                        }
                    }
                    if (!token_found) {
                        m_errortxt = "ASL WT call token not found (check Callsign & ASL Password in Settings)";
                        emit update_log(m_errortxt);
                        connect_status = Mode::DISCONNECTED;
                        emit connect_status_changed(5);
                    }
                } else {
                    qDebug() << "Error: " << wtReply->errorString();
                    m_errortxt = "ASL WT authentication failed";
                    emit update_log(m_errortxt);
                    connect_status = Mode::DISCONNECTED;
                    emit connect_status_changed(5);
                }
                wtReply->deleteLater();
                manager->deleteLater();
            });

            manager->get(wtReq);
        } else {
            qDebug() << "Error: " << reply->errorString();
            m_errortxt = "ASL WT login failed";
            emit update_log(m_errortxt);
            connect_status = Mode::DISCONNECTED;
            emit connect_status_changed(5);
            manager->deleteLater();
        }
        reply->deleteLater();
    });
    manager->post(request, postData);
}

void DroidStar::process_connect()
{
    qDebug() << "process_connect() called:"
             << "connect_status=" << connect_status
             << "protocol=" << m_protocol
             << "callsign=" << m_callsign
             << "dmrid=" << m_dmrid
             << "module=" << QChar(m_module)
             << "saved_dmrhost=" << m_saved_dmrhost;

    if(connect_status != Mode::DISCONNECTED){
        connect_status = Mode::DISCONNECTED;
        m_odtpActive = false;
        m_keepAliveTimer->stop();
        stop_mode_thread();
        m_outlevel = 0;
        m_data1.clear();
        m_data2.clear();
        m_data3.clear();
        m_data4.clear();
        m_data5.clear();
        m_data6.clear();
        m_netstatustxt.clear();
        m_ambestatustxt.clear();
        m_mmdvmstatustxt.clear();
        if (TranscriptionEngine *te = TranscriptionEngine::instance()) {
            te->onDisconnected();
        }
#ifdef Q_OS_IOS
        // Notify audio session manager of disconnection (clears Now Playing)
        setAudioConnectionState(false, "", "");
        ios_live_activity_end();
#endif
        emit update_data();
        emit connect_status_changed(0);
        emit update_log("Disconnected");
    }
    else{
        if (TranscriptionEngine *te = TranscriptionEngine::instance()) {
            te->setUserCallsign(m_callsign);
        }
#ifdef Q_OS_IOS
        MicPermission::check_permission();
        // Set up audio session EARLY so background mode is configured before audio starts.
        setupAVAudioSession();
#endif
        if(m_protocol == "REF"){
            m_refname = m_saved_refhost;
        }
        else if(m_protocol == "DCS"){
            m_refname = m_saved_dcshost;
        }
        else if(m_protocol == "XRF"){
            m_refname = m_saved_xrfhost;
        }
        else if(m_protocol == "YSF"){
            m_refname = m_saved_ysfhost;
        }
        else if(m_protocol == "FCS"){
            m_refname = m_saved_fcshost;
        }
        else if(m_protocol == "DMR"){
            if (m_hostmap.isEmpty()) {
                process_dmr_hosts();
            }
            if ((m_saved_dmrhost.isEmpty() || !m_hostmap.contains(m_saved_dmrhost)) && !m_hostsmodel.isEmpty()) {
                m_saved_dmrhost = m_hostsmodel.first();
            }
            m_refname = m_saved_dmrhost;
        }
        else if(m_protocol == "P25"){
            m_refname = m_saved_p25host;
        }
        else if(m_protocol == "NXDN"){
            m_refname = m_saved_nxdnhost;
        }
        else if(m_protocol == "M17"){
            m_refname = m_saved_m17host;
        }
        else if(m_protocol == "IAX"){
            if (m_hostmap.isEmpty()) {
                process_iax_hosts();
            }
            if ((m_saved_iaxhost.isEmpty() || !m_hostmap.contains(m_saved_iaxhost)) && !m_hostsmodel.isEmpty()) {
                m_saved_iaxhost = m_hostsmodel.first();
            }
            m_refname = m_saved_iaxhost;
            qDebug() << "m_refname:m_wt_callingname:m_asl_password == " << m_refname << ":" << m_wt_callingname << ":" << m_asl_password;
            if ((m_hostmap[m_refname].contains(".nodes.allstarlink.org") || m_hostmap[m_refname].contains(",allstar-public,allstar")) &&
                (m_wt_callingname.isEmpty() || (m_asl_password != m_wt_callingname_pass))) {
                obtain_asl_wt_creds();
                return;
            }
        }

        qDebug() << "process_connect() using refname=" << m_refname
                 << "hostmap_contains=" << m_hostmap.contains(m_refname)
                 << "hostsmodel_count=" << m_hostsmodel.size();

        m_keepAliveTimer->start();
        emit connect_status_changed(1);
        connect_status = Mode::CONNECTING;
        QStringList sl;

        m_host = m_hostmap[m_refname];
        sl = m_host.split(',');

        if( (m_protocol == "M17") && !m_mdirect && (m_ipv6) && (sl.size() > 2) && (sl.at(2) != "none") ){
            m_host = sl.at(2).simplified();
            m_port = sl.at(1).toInt();
        }
        else if(sl.size() > 1){
            m_host = sl.at(0).simplified();
            m_port = sl.at(1).toInt();
        }
        else if( (m_protocol == "M17") && m_mdirect ){
            qDebug() << "Going MMDVM_DIRECT";
        }
        else{
            m_errortxt = "Invalid host selection";
            emit update_log(m_errortxt);
            connect_status = Mode::DISCONNECTED;
            emit connect_status_changed(5);
            return;
        }

        int fallbackHbPort = 62031;
        bool useOdtp = false;
        if (m_protocol == "DMR") {
            const bool isTGIFServer = m_refname.startsWith("TGIF", Qt::CaseInsensitive) ||
                                      m_host.contains("tgif", Qt::CaseInsensitive);
            const bool isBMServer = !isTGIFServer &&
                                    (m_refname.startsWith("BM", Qt::CaseInsensitive) ||
                                     m_host.contains("brandmeister", Qt::CaseInsensitive) ||
                                     m_host.contains("repeater.net", Qt::CaseInsensitive) ||
                                     (m_port == 54006));
            useOdtp = (m_bm_odtp && isBMServer) || (m_port == 54006);
            fallbackHbPort = (m_port > 0 && m_port != 54006) ? m_port : 62031;
            if (useOdtp && (m_port == 62031 || m_port <= 0)) {
                m_port = 54006;
            }
        }
        m_odtpActive = useOdtp;

        QString vocoder = "";
        if( (m_vocoder != "Software vocoder") && (m_vocoder.contains(':')) ){
            QStringList vl = m_vocoder.split(':');
            vocoder = vl.at(1);
        }
        QString modem = "";
        if( (m_modem != "None") && (m_modem.contains(':')) ){
            QStringList ml = m_modem.split(':');
            modem = ml.at(1);
        }

        const bool txInvert = true;
        const bool rxInvert = false;
        const bool pttInvert = false;
        const bool useCOSAsLockout = 0;
        const uint32_t ysfTXHang = 4;
        const float pocsagTXLevel = 50;
        const float m17TXLevel = 50;
        const bool duplex = m_modemRxFreq.toUInt() != m_modemTxFreq.toUInt();
        const int rxfreq = m_modemRxFreq.toInt() + m_modemRxOffset.toInt();
        const int txfreq = m_modemTxFreq.toInt() + m_modemTxOffset.toInt();

        emit update_log("Connecting to " + m_host + ":" + QString::number(m_port) + (useOdtp ? " (ODTP)..." : "..."));

        uint16_t nxdnid = m_nxdnids.key(m_callsign);

        stop_mode_thread();
        m_mode = Mode::create_mode(m_protocol);
        if (!m_mode) {
            m_errortxt = "Unsupported mode: " + m_protocol;
            emit update_log(m_errortxt);
            connect_status = Mode::DISCONNECTED;
            emit connect_status_changed(5);
            return;
        }
        m_modethread = new QThread();
        m_mode->moveToThread(m_modethread);

        if(m_protocol == "IAX"){
            QString iaxuser = (sl.size() > 2) ? sl.at(2).simplified() : "";
            QString iaxpass = (sl.size() > 3) ? sl.mid(3).join(",").trimmed() : "";
            m_mode->set_iax_params(iaxuser, iaxpass, m_wt_callingname, m_refname, m_host, m_port);
            connect(this, SIGNAL(send_dtmf(QByteArray)), m_mode, SLOT(send_dtmf(QByteArray)));
        }

        m_mode->init(m_callsign, m_dmrid, nxdnid, m_module, m_refname, m_host, m_port, m_ipv6, vocoder, modem, m_capture, m_playback, m_mdirect);
        m_mode->set_modem_flags(rxInvert, txInvert, pttInvert, useCOSAsLockout, duplex);
        m_mode->set_modem_params(m_modemBaud.toUInt(), rxfreq, txfreq, m_modemTxDelay.toInt(), m_modemRxLevel.toFloat(), m_modemRFLevel.toFloat(), ysfTXHang, m_modemCWIdTxLevel.toFloat(), m_modemDstarTxLevel.toFloat(), m_modemDMRTxLevel.toFloat(), m_modemYSFTxLevel.toFloat(), m_modemP25TxLevel.toFloat(), m_modemNXDNTxLevel.toFloat(), pocsagTXLevel, m17TXLevel);

        connect(this, SIGNAL(module_changed(char)), m_mode, SLOT(module_changed(char)));
        connect(m_mode, SIGNAL(update(Mode::MODEINFO)), this, SLOT(update_data(Mode::MODEINFO)));
        connect(m_mode, SIGNAL(update_log(QString)), this, SLOT(updatelog(QString)));
        connect(m_mode, SIGNAL(update_output_level(unsigned short)), this, SLOT(update_output_level(unsigned short)));
        connect(m_modethread, SIGNAL(started()), m_mode, SLOT(begin_connect()));
        connect(this, SIGNAL(input_source_changed(int,QString)), m_mode, SLOT(input_src_changed(int,QString)));
        connect(this, SIGNAL(swrx_state_changed(int)), m_mode, SLOT(swrx_state_changed(int)));
        connect(this, SIGNAL(swtx_state_changed(int)), m_mode, SLOT(swtx_state_changed(int)));
        connect(this, SIGNAL(agc_state_changed(int)), m_mode, SLOT(agc_state_changed(int)));
        connect(this, SIGNAL(tx_clicked(bool)), m_mode, SLOT(toggle_tx(bool)));
        connect(this, SIGNAL(tx_pressed()), m_mode, SLOT(start_tx()));
        connect(this, SIGNAL(tx_released()), m_mode, SLOT(stop_tx()));
        connect(this, SIGNAL(in_audio_vol_changed(qreal)), m_mode, SLOT(in_audio_vol_changed(qreal)));
        connect(this, SIGNAL(out_audio_vol_changed(qreal)), m_mode, SLOT(out_audio_vol_changed(qreal)));
        connect(this, SIGNAL(mycall_changed(QString)), m_mode, SLOT(mycall_changed(QString)));
        connect(this, SIGNAL(urcall_changed(QString)), m_mode, SLOT(urcall_changed(QString)));
        connect(this, SIGNAL(rptr1_changed(QString)), m_mode, SLOT(rptr1_changed(QString)));
        connect(this, SIGNAL(rptr2_changed(QString)), m_mode, SLOT(rptr2_changed(QString)));
        connect(this, SIGNAL(usrtxt_changed(QString)), m_mode, SLOT(usrtxt_changed(QString)));
        connect(this, SIGNAL(debug_changed(bool)), m_mode, SLOT(debug_changed(bool)));
        emit module_changed(m_module);
        emit mycall_changed(m_mycall);
        emit urcall_changed(m_urcall);
        emit rptr1_changed(m_rptr1);
        emit rptr2_changed(m_rptr2);
        emit usrtxt_changed(m_dstarusertxt);
        emit in_audio_vol_changed(m_inVol);
        emit out_audio_vol_changed(m_outVol);
        emit agc_state_changed(m_agc ? 1 : 0);

        if(m_protocol == "DMR"){
            // Join from index 2 onward in case a custom host password contains a comma
            QString dmrpass = (sl.size() > 2) ? sl.mid(2).join(",").trimmed() : QString();

            const bool isTGIFHost = m_refname.startsWith("TGIF", Qt::CaseInsensitive) ||
                                    m_host.contains("tgif", Qt::CaseInsensitive);
            const bool isBMHost = !isTGIFHost &&
                                  (m_refname.startsWith("BM", Qt::CaseInsensitive) ||
                                   m_host.contains("brandmeister", Qt::CaseInsensitive) ||
                                   m_host.contains("repeater.net", Qt::CaseInsensitive) ||
                                   (m_port == 54006));
            if (isBMHost && !m_bm_password.isEmpty()) {
                dmrpass = m_bm_password;
            } else if (isTGIFHost) {
                if (!m_tgif_password.isEmpty()) {
                    dmrpass = m_tgif_password;
                } else if (dmrpass.isEmpty()) {
                    dmrpass = QStringLiteral("passw0rd");
                }
            }

            m_mode->set_dmr_params(m_essid, dmrpass, m_latitude, m_longitude, m_location, m_description, m_freq, m_url, m_swid, m_pkgid, m_dmropts);
            if (DMR *dmrMode = qobject_cast<DMR *>(m_mode)) {
                dmrMode->set_odtp_mode(useOdtp, fallbackHbPort);
                dmrMode->set_tgif_mode(isTGIFHost);
                connect(this, &DroidStar::send_dmr_sms,
                        dmrMode, &DMR::send_sms, Qt::QueuedConnection);
                connect(dmrMode, &DMR::sms_received,
                        this, &DroidStar::dmr_sms_received, Qt::QueuedConnection);
                connect(dmrMode, &DMR::sms_send_status,
                        this, &DroidStar::dmr_sms_send_status, Qt::QueuedConnection);
                connect(dmrMode, &DMR::sms_ack_received,
                        this, &DroidStar::dmr_sms_ack_received, Qt::QueuedConnection);
            }
            connect(this, SIGNAL(dmr_tgid_changed(int)), m_mode, SLOT(dmr_tgid_changed(int)));
            connect(this, SIGNAL(dmrpc_state_changed(int)), m_mode, SLOT(dmrpc_state_changed(int)));
            connect(this, SIGNAL(slot_changed(int)), m_mode, SLOT(slot_changed(int)));
            connect(this, SIGNAL(cc_changed(int)), m_mode, SLOT(cc_changed(int)));
            emit slot_changed(m_dmr_slot);
            emit cc_changed(m_dmr_cc);
            emit dmr_tgid_changed(m_dmr_destid);
            emit dmrpc_state_changed(m_pc);
        }

        if(m_protocol == "P25"){
            connect(this, SIGNAL(dmr_tgid_changed(int)), m_mode, SLOT(dmr_tgid_changed(int)));
            emit dmr_tgid_changed(m_dmr_destid);
        }

        if(m_protocol == "M17"){
            connect(this, SIGNAL(m17_rate_changed(int)), m_mode, SLOT(rate_changed(int)));
            connect(this, SIGNAL(m17_can_changed(int)), m_mode, SLOT(can_changed(int)));
            if(m_mdirect){
                connect(this, SIGNAL(dst_changed(QString)), m_mode, SLOT(dst_changed(QString)));
            }
        }

        m_modethread->start();

    }
/*
    qDebug() << "process_connect called m_callsign == " << m_callsign;
    qDebug() << "process_connect called m_dmrid == " << m_dmrid;
    qDebug() << "process_connect called m_bm_password == " << m_bm_password;
    qDebug() << "process_connect called m_tgif_password == " << m_tgif_password;
    qDebug() << "process_connect called m_dmropts == " << m_dmropts;
    qDebug() << "process_connect called m_refname == " << m_refname;
    qDebug() << "process_connect called m_host == " << m_host;
    qDebug() << "process_connect called m_module == " << m_module;
    qDebug() << "process_connect called m_protocol == " << m_protocol;
    qDebug() << "process_connect called m_port == " << m_port;
*/
}


void DroidStar::relink_dmr_talkgroup()
{
    if (!m_mode || m_protocol != "DMR") {
        return;
    }
    trigger_auto_tx_punch(50);
}

void DroidStar::trigger_auto_tx_punch(int delayMs)
{
    if (connect_status != Mode::CONNECTED_RW || m_protocol != "DMR" || !m_mode || m_dmr_destid == 0U || m_pc != 0) {
        return;
    }
    if (m_odtpActive) {
        // In BrandMeister ODTP mode, talkgroup subscriptions happen via REWIND_TYPE_SUBSCRIPTION
        // without needing to key up TX on the air.
        emit dmrpc_state_changed(m_pc);
        emit dmr_tgid_changed(m_dmr_destid);
        return;
    }
    if (m_autoTxPunchRunning || (m_autoTxPunchStopTimer && m_autoTxPunchStopTimer->isActive())) {
        return;
    }
    if (m_autoTxPunchStartTimer) {
        m_autoTxPunchStartTimer->start(std::max(10, delayMs));
    }
}

void DroidStar::cancel_tune_reconnect()
{
    m_tuningReconnect = false;
    if (m_tuneReconnectTimer) m_tuneReconnectTimer->stop();
    if (m_autoTxPunchStartTimer) m_autoTxPunchStartTimer->stop();
    if (m_autoTxPunchStopTimer) m_autoTxPunchStopTimer->stop();
    if (m_autoTxPunchRunning) {
        m_autoTxPunchRunning = false;
        if (m_mode) m_mode->set_auto_tx_punch(false);
        if (connect_status == Mode::CONNECTED_RW) {
            click_tx(false);
        }
    }
}

bool DroidStar::is_tuning_reconnect() const
{
    return m_tuningReconnect || (m_tuneReconnectTimer && m_tuneReconnectTimer->isActive());
}

bool DroidStar::is_auto_tx_punch_pending() const
{
    return m_autoTxPunchRunning ||
           (m_autoTxPunchStartTimer && m_autoTxPunchStartTimer->isActive()) ||
           (m_autoTxPunchStopTimer && m_autoTxPunchStopTimer->isActive());
}

bool DroidStar::is_tgif_host() const
{
    return m_saved_dmrhost.startsWith("TGIF", Qt::CaseInsensitive) ||
           m_refname.startsWith("TGIF", Qt::CaseInsensitive) ||
           m_host.contains("tgif", Qt::CaseInsensitive);
}

bool DroidStar::is_odtp_host(const QString &refname) const
{
    const QString r = refname.trimmed();
    if (r.isEmpty() || r.startsWith("TGIF", Qt::CaseInsensitive)) {
        return false;
    }
    const QString mapped = m_hostmap.value(r);
    const QStringList sl = mapped.split(',');
    const QString hostStr = (sl.size() > 0) ? sl.at(0).trimmed() : QString();
    const int portNum = (sl.size() > 1) ? sl.at(1).trimmed().toInt() : 0;

    if (portNum == 54006) {
        return true;
    }
    if (!m_bm_odtp) {
        return false;
    }
    return r.startsWith("BM", Qt::CaseInsensitive) ||
           hostStr.contains("brandmeister", Qt::CaseInsensitive) ||
           hostStr.contains("repeater.net", Qt::CaseInsensitive);
}

void DroidStar::tune_to_talkgroup(const QString &tgid, bool isPrivate)
{
    QString cleaned;
    for (const QChar &ch : tgid) {
        if (ch.isDigit()) cleaned.append(ch);
    }
    const uint32_t newTg = cleaned.toUInt();
    if (newTg == 0U) {
        return;
    }

    if (m_autoTxPunchStartTimer) m_autoTxPunchStartTimer->stop();
    if (m_autoTxPunchStopTimer) m_autoTxPunchStopTimer->stop();
    if (m_autoTxPunchRunning) {
        m_autoTxPunchRunning = false;
        if (m_mode) m_mode->set_auto_tx_punch(false);
        if (connect_status == Mode::CONNECTED_RW) {
            click_tx(false);
        }
    }

    if (m_protocol != "DMR") {
        process_mode_change("DMR");
    }
    if (m_hostmap.isEmpty()) {
        process_dmr_hosts();
    }
    if ((m_saved_dmrhost.isEmpty() || !m_hostmap.contains(m_saved_dmrhost)) && !m_hostsmodel.isEmpty()) {
        m_saved_dmrhost = m_hostsmodel.first();
    }

    m_pc = isPrivate ? 1 : 0;
    m_dmr_destid = newTg;
    emit dmrpc_state_changed(m_pc);
    emit dmr_tgid_changed(m_dmr_destid);
    save_settings();
    addRecentTGID(QString::number(newTg));

    if (connect_status == Mode::CONNECTED_RW && m_protocol == "DMR") {
        // Seamless in-session DMR talkgroup switch: - BrandMeister ODTP:
        m_tuningReconnect = false;
        if (m_tuneReconnectTimer) m_tuneReconnectTimer->stop();
        m_data1.clear();
        m_data2.clear();
        m_data3.clear();
        m_data4.clear();
        m_data5.clear();
        m_data6.clear();
        m_outlevel = 0;
#ifdef Q_OS_IOS
        clearAudioRXState();
        ios_live_activity_start_or_update("IDLE",
                                          m_protocol.toUtf8().constData(),
                                          m_refname.toUtf8().constData(),
                                          m_callsign.toUtf8().constData(),
                                          QString::number(newTg).toUtf8().constData());
#endif
        emit update_data();
        return;
    }

    if (connect_status == Mode::CONNECTING && m_protocol == "DMR") {
        // Already mid-handshake; DMR::setup_connection() will link the updated
        return;
    }

    if (connect_status != Mode::DISCONNECTED) {
        m_tuningReconnect = true;
        process_connect();
        if (m_tuneReconnectTimer) {
            m_tuneReconnectTimer->start(150);
        }
    } else {
        if (m_tuneReconnectTimer && m_tuneReconnectTimer->isActive()) {
            return;
        }
        m_tuningReconnect = false;
        process_connect();
    }
}

void DroidStar::attempt_reconnect()
{
    if(connect_status == Mode::DISCONNECTED) {
        qDebug() << "Attempting to reconnect...";
        process_connect();
    }
}

// Function to start the keep-alive mechanism
void DroidStar::start_keep_alive()
{
    if (!m_keepAliveTimer->isActive()) {
        m_keepAliveTimer->start(30000);  // 30,000 milliseconds = 30 seconds
        emit update_log("Keep-alive timer started");
    }
}

// Function to stop the keep-alive mechanism
void DroidStar::stop_keep_alive()
{
    if (m_keepAliveTimer->isActive()) {
        m_keepAliveTimer->stop();
        emit update_log("Keep-alive timer stopped");
    }
}

// Slot to send the keep-alive packet
void DroidStar::send_keep_alive()
{
    if (connect_status == Mode::CONNECTED_RW) {
        // Emit a keep-alive packet or ping command
        emit update_log("Sending keep-alive packet");
        // Call an appropriate function to send a small data packet or ping
        // This could be a simple command to the host/server
        if (m_mode) {
            //m_mode->send_keep_alive();
        }
    } else {
        // Stop the timer if not connected
        stop_keep_alive();
    }
}

void DroidStar::process_host_change(const QString &h)
{
    if(m_protocol == "REF"){
        m_saved_refhost = h.simplified();
    }
    if(m_protocol == "DCS"){
        m_saved_dcshost = h.simplified();
    }
    if(m_protocol == "XRF"){
        m_saved_xrfhost = h.simplified();
    }
    if(m_protocol == "YSF"){
        m_saved_ysfhost = h.simplified();
    }
    if(m_protocol == "FCS"){
        m_saved_fcshost = h.simplified();
    }
    if(m_protocol == "DMR"){
        m_saved_dmrhost = h.simplified();
    }
    if(m_protocol == "P25"){
        m_saved_p25host = h.simplified();
    }
    if(m_protocol == "NXDN"){
        m_saved_nxdnhost = h.simplified();
    }
    if(m_protocol == "M17"){
        m_saved_m17host = h.simplified();
    }
    if(m_protocol == "IAX"){
        m_saved_iaxhost = h.simplified();
    }
    save_settings();
}

void DroidStar::process_mode_change(const QString &m)
{
    m_protocol = m;
    if((m == "REF") || (m == "DCS") || (m == "XRF")){
        process_dstar_hosts(m);
        m_label1 = "MYCALL";
        m_label2 = "URCALL";
        m_label3 = "RPTR1";
        m_label4 = "RPTR2";
        m_label5 = "Stream ID";
        m_label6 = "User txt";
    }
    if(m == "YSF"){
        process_ysf_hosts();
        m_label1 = "Gateway";
        m_label2 = "Callsign";
        m_label3 = "Dest";
        m_label4 = "Type";
        m_label5 = "Path";
        m_label6 = "Frame#";
    }
    if(m == "FCS"){
        process_fcs_rooms();
        m_label1 = "Gateway";
        m_label2 = "Callsign";
        m_label3 = "Dest";
        m_label4 = "Type";
        m_label5 = "Path";
        m_label6 = "Frame#";
    }
    if(m == "DMR"){
        process_dmr_hosts();
        //process_dmr_ids();
        m_label1 = "Callsign";
        m_label2 = "SrcID";
        m_label3 = "DestID";
        m_label4 = "GWID";
        m_label5 = "Info";
        m_label6 = "";
    }
    if(m == "P25"){
        process_p25_hosts();
        m_label1 = "Callsign";
        m_label2 = "SrcID";
        m_label3 = "DestID";
        m_label4 = "GWID";
        m_label5 = "Seq#";
        m_label6 = "";
    }
    if(m == "NXDN"){
        process_nxdn_hosts();
        m_label1 = "Callsign";
        m_label2 = "SrcID";
        m_label3 = "DestID";
        m_label4 = "GWID";
        m_label5 = "Seq#";
        m_label6 = "";
    }
    if(m == "M17"){
        process_m17_hosts();
        m_label1 = "SrcID";
        m_label2 = "DstID";
        m_label3 = "Type";
        m_label4 = "Frame#";
        m_label5 = "StreamID";
        m_label6 = "";
    }
    if(m == "IAX"){
        process_iax_hosts();
        m_label1 = "";
        m_label2 = "";
        m_label3 = "";
        m_label4 = "";
        m_label5 = "";
        m_label6 = "";
    }
    // During startup, process_settings() calls process_mode_change() while other fields
    if (m_settings_processed) {
        save_settings();
    }
    emit mode_changed();
}

void DroidStar::save_settings()
{
    // Ensure a sane module gets persisted (avoid writing NUL which later
    if (m_module == 0) m_module = 'A';

    //m_settings->setValue("PLAYBACK", ui->comboPlayback->currentText());
    //m_settings->setValue("CAPTURE", ui->comboCapture->currentText());
    m_settings->setValue("IPV6", m_ipv6 ? "true" : "false");
    m_settings->setValue("MODE", m_protocol);
    m_settings->setValue("REFHOST", m_saved_refhost);
    m_settings->setValue("DCSHOST", m_saved_dcshost);
    m_settings->setValue("XRFHOST", m_saved_xrfhost);
    m_settings->setValue("YSFHOST", m_saved_ysfhost);
    m_settings->setValue("FCSHOST", m_saved_fcshost);
    m_settings->setValue("DMRHOST", m_saved_dmrhost);
    m_settings->setValue("P25HOST", m_saved_p25host);
    m_settings->setValue("NXDNHOST", m_saved_nxdnhost);
    m_settings->setValue("M17HOST", m_saved_m17host);
    m_settings->setValue("IAXHOST", m_saved_iaxhost);
    m_settings->setValue("MODULE", QString(m_module));
    m_settings->setValue("CALLSIGN", m_callsign);
    m_settings->setValue("DMRID", m_dmrid);
    m_settings->setValue("ESSID", m_essid);
    m_settings->setValue("BMPASSWORD", m_bm_password);
    m_settings->setValue("BMPASSWORD_B64", QString::fromLatin1(m_bm_password.toUtf8().toBase64()));
    m_settings->setValue("TGIFPASSWORD", m_tgif_password);
    m_settings->setValue("TGIFPASSWORD_B64", QString::fromLatin1(m_tgif_password.toUtf8().toBase64()));
    m_settings->setValue("ASLPASSWORD", m_asl_password);
    m_settings->setValue("ASLPASSWORD_B64", QString::fromLatin1(m_asl_password.toUtf8().toBase64()));
    m_settings->setValue("DMRTGID", m_dmr_destid);
    m_settings->setValue("DMRLAT", m_latitude);
    m_settings->setValue("DMRLONG", m_longitude);
    m_settings->setValue("DMRLOC", m_location);
    m_settings->setValue("DMRDESC", m_description);
    m_settings->setValue("DMRFREQ", m_freq);
    m_settings->setValue("DMRURL", m_url);
    m_settings->setValue("DMRSWID", m_swid);
    m_settings->setValue("DMRPKGID", m_pkgid);
    m_settings->setValue("DMROPTS", m_dmropts);
    m_settings->setValue("MYCALL", m_mycall);
    m_settings->setValue("URCALL", m_urcall);
    m_settings->setValue("RPTR1", m_rptr1);
    m_settings->setValue("RPTR2", m_rptr2);
    m_settings->setValue("TXTIMEOUT", m_txtimeout);
    m_settings->setValue("TXTOGGLE", m_toggletx ? "true" : "false");
    m_settings->setValue("BM_ODTP", m_bm_odtp ? "true" : "false");
    m_settings->setValue("XRF2REF", m_xrf2ref ? "true" : "false");
    m_settings->setValue("USRTXT", m_dstarusertxt);

    m_settings->setValue("ModemRxFreq", m_modemRxFreq);
    m_settings->setValue("ModemTxFreq", m_modemTxFreq);
    m_settings->setValue("ModemRxOffset", m_modemRxOffset);
    m_settings->setValue("ModemTxOffset", m_modemTxOffset);
    m_settings->setValue("ModemRxDCOffset", m_modemRxDCOffset);
    m_settings->setValue("ModemTxDCOffset", m_modemTxDCOffset);
    m_settings->setValue("ModemRxLevel", m_modemRxLevel);
    m_settings->setValue("ModemTxLevel", m_modemTxLevel);
    m_settings->setValue("ModemRFLevel", m_modemRFLevel);
    m_settings->setValue("ModemTxDelay", m_modemTxDelay);
    m_settings->setValue("ModemCWIdTxLevel", m_modemCWIdTxLevel);
    m_settings->setValue("ModemDstarTxLevel", m_modemDstarTxLevel);
    m_settings->setValue("ModemDMRTxLevel", m_modemDMRTxLevel);
    m_settings->setValue("ModemYSFTxLevel", m_modemYSFTxLevel);
    m_settings->setValue("ModemP25TxLevel", m_modemP25TxLevel);
    m_settings->setValue("ModemNXDNTxLevel", m_modemNXDNTxLevel);
    m_settings->setValue("ModemBaud", m_modemBaud);
    m_settings->setValue("ModemM17CAN", m_modemM17CAN);
    m_settings->setValue("ModemTxInvert", m_modemTxInvert ? "true" : "false");
    m_settings->setValue("ModemRxInvert", m_modemRxInvert ? "true" : "false");
    m_settings->setValue("ModemPTTInvert", m_modemPTTInvert ? "true" : "false");
    m_settings->setValue("AUDIO_NR_MODE", AudioEngine::savedUserNoiseMode());
    m_settings->setValue("AUDIO_HD_UPSCALE", AudioEngine::savedUserHdUpscale());
    m_settings->setValue("AUDIO_TX_ENHANCE", AudioEngine::savedUserTxEnhance());
    m_settings->setValue("AUDIO_VOICE_ISOLATION", AudioEngine::globalVoiceIsolation());
    m_settings->setValue("AUDIO_RX_CLEAN", AudioEngine::globalRxClean());
    m_settings->setValue("AUDIO_SPK_PREAMP_DB", AudioEngine::globalSpkPreampDb());

    // Force flush to disk so iOS reliably persists immediately.
    m_settings->sync();
}

void DroidStar::process_settings()
{
    // Ensure we read the latest values from disk (important on mobile sandboxes).
    m_settings->sync();

    // We are loading settings now; prevent write-back until fully loaded.
    m_settings_processed = false;

    m_ipv6 = (m_settings->value("IPV6").toString().simplified() == "true") ? true : false;
    {
        const QVariant rawLocalHosts = m_settings->value("LOCALHOSTS");
        if (rawLocalHosts.metaType().id() == QMetaType::QStringList) {
            m_localhosts = rawLocalHosts.toStringList().join(",");
        } else {
            m_localhosts = rawLocalHosts.toString();
        }
    }
    process_mode_change(m_settings->value("MODE").toString().simplified());
    m_saved_refhost = m_settings->value("REFHOST").toString().simplified();
    m_saved_dcshost =m_settings->value("DCSHOST").toString().simplified();
    m_saved_xrfhost = m_settings->value("XRFHOST").toString().simplified();
    m_saved_ysfhost = m_settings->value("YSFHOST").toString().simplified();
    m_saved_fcshost = m_settings->value("FCSHOST").toString().simplified();
    m_saved_dmrhost = m_settings->value("DMRHOST").toString().simplified();
    m_saved_p25host = m_settings->value("P25HOST").toString().simplified();
    m_saved_nxdnhost = m_settings->value("NXDNHOST").toString().simplified();
    m_saved_m17host = m_settings->value("M17HOST").toString().simplified();
    m_saved_iaxhost = m_settings->value("IAXHOST").toString().simplified();
    {
        const QString moduleStr = m_settings->value("MODULE", "A").toString();
        if (!moduleStr.isEmpty() && moduleStr.at(0).unicode() != 0) m_module = moduleStr.toStdString()[0];
        else m_module = 'A';
    }
    m_callsign = m_settings->value("CALLSIGN").toString().simplified();
    m_dmrid = m_settings->value("DMRID").toString().simplified().toUInt();
    m_essid = m_settings->value("ESSID").toString().simplified().toUInt();
    {
        auto readPassword = [this](const char *rawKey, const char *b64Key) -> QString {
            if (m_settings->contains(b64Key)) {
                const QByteArray b64 = m_settings->value(b64Key).toString().trimmed().toLatin1();
                if (!b64.isEmpty()) {
                    return QString::fromUtf8(QByteArray::fromBase64(b64));
                }
            }
            const QVariant rawVal = m_settings->value(rawKey);
            if (rawVal.metaType().id() == QMetaType::QStringList) {
                // QSettings::IniFormat splits unquoted values containing commas into a QStringList
                return rawVal.toStringList().join(",");
            }
            return rawVal.toString().trimmed();
        };
        m_bm_password = readPassword("BMPASSWORD", "BMPASSWORD_B64");
        m_tgif_password = readPassword("TGIFPASSWORD", "TGIFPASSWORD_B64");
        m_asl_password = readPassword("ASLPASSWORD", "ASLPASSWORD_B64");
    }
    m_latitude = m_settings->value("DMRLAT", "0").toString().simplified();
    m_longitude = m_settings->value("DMRLONG", "0").toString().simplified();
    m_location = m_settings->value("DMRLOC").toString().simplified();
    m_description = m_settings->value("DMRDESC", "").toString().simplified();
    m_freq = m_settings->value("DMRFREQ", "438800000").toString().simplified();
    m_url = m_settings->value("DMRURL", "www.qrz.com").toString().simplified();
    m_swid = m_settings->value("DMRSWID", "20200922").toString().simplified();
    m_pkgid = m_settings->value("DMRPKGID", "MMDVM_MMDVM_HS_Hat").toString().simplified();
    m_dmropts = m_settings->value("DMROPTS").toString().simplified();
    m_dmr_destid = m_settings->value("DMRTGID", "4000").toString().simplified().toUInt();
    m_mycall = m_settings->value("MYCALL").toString().simplified();
    m_urcall = m_settings->value("URCALL", "CQCQCQ").toString().simplified();
    m_rptr1 = m_settings->value("RPTR1").toString().simplified();
    m_rptr2 = m_settings->value("RPTR2").toString().simplified();
    m_txtimeout = m_settings->value("TXTIMEOUT", "300").toString().simplified().toUInt();
    // IMPORTANT: On a fresh install (no settings yet), default to TX toggle mode enabled.
    m_toggletx = (m_settings->value("TXTOGGLE", "true").toString().simplified() == "true") ? true : false;
    m_bm_odtp = (m_settings->value("BM_ODTP", "true").toString().simplified() == "true") ? true : false;
    m_dstarusertxt = m_settings->value("USRTXT").toString().simplified();
    m_xrf2ref = (m_settings->value("XRF2REF").toString().simplified() == "true") ? true : false;
    m_localhosts = m_settings->value("LOCALHOSTS").toString();

    // Treat empty-but-present values as missing (QSettings defaults apply only to missing
    if (m_latitude.isEmpty()) m_latitude = "0";
    if (m_longitude.isEmpty()) m_longitude = "0";
    if (m_freq.isEmpty() || m_freq.toUInt() == 0) m_freq = "438800000";
    if (m_url.isEmpty()) m_url = "www.qrz.com";
    if (m_swid.isEmpty()) m_swid = "20200922";
    if (m_pkgid.isEmpty()) m_pkgid = "MMDVM_MMDVM_HS_Hat";

    m_modemRxFreq = m_settings->value("ModemRxFreq", "438800000").toString().simplified();
    m_modemTxFreq = m_settings->value("ModemTxFreq", "438800000").toString().simplified();
    m_modemRxOffset = m_settings->value("ModemRxOffset", "0").toString().simplified();
    m_modemTxOffset = m_settings->value("ModemTxOffset", "0").toString().simplified();
    m_modemRxDCOffset = m_settings->value("ModemRxDCOffset", "0").toString().simplified();
    m_modemTxDCOffset = m_settings->value("ModemTxDCOffset", "0").toString().simplified();
    m_modemRxLevel = m_settings->value("ModemRxLevel", "50").toString().simplified();
    m_modemTxLevel = m_settings->value("ModemTxLevel", "50").toString().simplified();
    m_modemRFLevel = m_settings->value("ModemRFLevel", "100").toString().simplified();
    m_modemTxDelay = m_settings->value("ModemTxDelay", "100").toString().simplified();
    m_modemCWIdTxLevel = m_settings->value("ModemCWIdTxLevel", "50").toString().simplified();
    m_modemDstarTxLevel = m_settings->value("ModemDstarTxLevel", "50").toString().simplified();
    m_modemDMRTxLevel = m_settings->value("ModemDMRTxLevel", "50").toString().simplified();
    m_modemYSFTxLevel = m_settings->value("ModemYSFTxLevel", "50").toString().simplified();
    m_modemP25TxLevel = m_settings->value("ModemP25TxLevel", "50").toString().simplified();
    m_modemNXDNTxLevel = m_settings->value("ModemNXDNTxLevel", "50").toString().simplified();
    m_modemBaud = m_settings->value("ModemBaud", "115200").toString().simplified();
    m_modemM17CAN = m_settings->value("ModemM17CAN", "0").toString().simplified();
    m_modemTxInvert = (m_settings->value("ModemTxInvert", "true").toString().simplified() == "true") ? true : false;
    m_modemRxInvert = (m_settings->value("ModemRxInvert", "false").toString().simplified() == "true") ? true : false;
    m_modemPTTInvert = (m_settings->value("ModemPTTInvert", "false").toString().simplified() == "true") ? true : false;

    // Load user's saved persisted TX audio config (defaults on first run
    const int savedNr = std::clamp(m_settings->value("AUDIO_NR_MODE", 2).toInt(), 0, 2);
    const bool savedHd = m_settings->value("AUDIO_HD_UPSCALE", true).toBool();
    const bool savedTx = m_settings->value("AUDIO_TX_ENHANCE", false).toBool();
    const bool savedVi = m_settings->value("AUDIO_VOICE_ISOLATION", false).toBool();
    const bool savedRxClean = m_settings->value("AUDIO_RX_CLEAN", false).toBool();
    const float savedPreampDb = std::clamp(m_settings->value("AUDIO_SPK_PREAMP_DB", 6.0f).toFloat(), 0.0f, 24.0f);
    AudioEngine::loadSavedUserTxSettings(savedNr, savedHd, savedTx, savedVi);
    AudioEngine::setGlobalRxClean(savedRxClean);
    AudioEngine::setGlobalSpkPreampDb(savedPreampDb);
    m_settings->setValue("AUDIO_NR_MODE", savedNr);
    m_settings->setValue("AUDIO_HD_UPSCALE", savedHd);
    m_settings->setValue("AUDIO_TX_ENHANCE", savedTx);
    m_settings->setValue("AUDIO_VOICE_ISOLATION", savedVi);
    m_settings->setValue("AUDIO_RX_CLEAN", savedRxClean);
    m_settings->setValue("AUDIO_SPK_PREAMP_DB", savedPreampDb);

    qDebug() << "process_settings loaded:"
             << "CALLSIGN=" << m_callsign
             << "DMRID=" << m_dmrid
             << "MODE=" << m_protocol
             << "DMRHOST=" << m_saved_dmrhost;
    m_settings_processed = true;
    emit update_settings();
}

void DroidStar::update_custom_hosts(QString h)
{
    m_settings->setValue("LOCALHOSTS", h);
    m_settings->sync();
    m_localhosts = h;

    // Immediately reload the host list for the active mode so custom
    if (m_protocol == "REF" || m_protocol == "DCS" || m_protocol == "XRF") {
        process_dstar_hosts(m_protocol);
    } else if (m_protocol == "YSF") {
        process_ysf_hosts();
    } else if (m_protocol == "FCS") {
        process_fcs_rooms();
    } else if (m_protocol == "DMR") {
        process_dmr_hosts();
    } else if (m_protocol == "P25") {
        process_p25_hosts();
    } else if (m_protocol == "NXDN") {
        process_nxdn_hosts();
    } else if (m_protocol == "M17") {
        process_m17_hosts();
    } else if (m_protocol == "IAX") {
        process_iax_hosts();
    }
    emit mode_changed();
}

void DroidStar::process_dstar_hosts(QString m)
{
    m_hostmap.clear();
    m_hostsmodel.clear();
    QString filename, port;
    if(m == "REF"){
        filename = "dplus.txt";
        port = "20001";
    }
    else if(m == "DCS"){
        filename = "dcs.txt";
        port = "30051";
    }
    else if(m == "XRF"){
        filename = "dextra.txt";
        port = "30001";
    }

    QFileInfo check_file(config_path + "/" + filename);

    if(check_file.exists() && check_file.isFile()){
        QFile f(config_path + "/" + filename);
        if(f.open(QIODevice::ReadOnly)){
            while(!f.atEnd()){
                QString l = f.readLine().simplified();
                if(l.isEmpty() || l.at(0) == '#'){
                    continue;
                }
                QStringList ll = l.split(' ', Qt::SkipEmptyParts);
                if(ll.size() > 1){
                    m_hostmap[ll.at(0).simplified()] = ll.at(1).simplified() + "," + port;
                }
            }

            m_customhosts = m_localhosts.split('\n');
            for (const auto& i : std::as_const(m_customhosts)){
                QStringList line = i.simplified().split(' ');

                if(line.size() >= 4 && line.at(0) == m){
                    m_hostmap[line.at(1).simplified()] = line.at(2).simplified() + "," + line.at(3).simplified();
                }
            }

            QMap<QString, QString>::const_iterator i = m_hostmap.constBegin();
            while (i != m_hostmap.constEnd()) {
                m_hostsmodel.append(i.key());
                ++i;
            }
        }
        f.close();
    }
    else{
        download_file("/" + filename);
    }
}

void DroidStar::process_ysf_hosts()
{
    m_hostmap.clear();
    m_hostsmodel.clear();
    QFileInfo check_file(config_path + "/YSFHosts.txt");
    if(check_file.exists() && check_file.isFile()){
        QFile f(config_path + "/YSFHosts.txt");
        if(f.open(QIODevice::ReadOnly)){
            while(!f.atEnd()){
                QString l = f.readLine();
                if(l.isEmpty() || l.at(0) == '#'){
                    continue;
                }
                QStringList ll = l.split(';');
                if(ll.size() > 4){
                    m_hostmap[ll.at(1).simplified()] = ll.at(3) + "," + ll.at(4);
                }
            }

            m_customhosts = m_localhosts.split('\n');
            for (const auto& i : std::as_const(m_customhosts)){
                QStringList line = i.simplified().split(' ');

                if(line.size() >= 4 && line.at(0) == "YSF"){
                    m_hostmap[line.at(1).simplified()] = line.at(2).simplified() + "," + line.at(3).simplified();
                }
            }

            QMap<QString, QString>::const_iterator i = m_hostmap.constBegin();
            while (i != m_hostmap.constEnd()) {
                m_hostsmodel.append(i.key());
                ++i;
            }
        }
        f.close();
    }
    else{
        download_file("/YSFHosts.txt");
    }
}

void DroidStar::process_fcs_rooms()
{
    m_hostmap.clear();
    m_hostsmodel.clear();
    QFileInfo check_file(config_path + "/FCSHosts.txt");
    if(check_file.exists() && check_file.isFile()){
        QFile f(config_path + "/FCSHosts.txt");
        if(f.open(QIODevice::ReadOnly)){
            while(!f.atEnd()){
                QString l = f.readLine();
                if(l.isEmpty() || l.at(0) == '#'){
                    continue;
                }
                QStringList ll = l.split(';');
                if(ll.size() > 4){
                    if(ll.at(1).simplified() != "nn"){
                        m_hostmap[ll.at(0).simplified() + " - " + ll.at(1).simplified()] = ll.at(2).left(6).toLower() + ".xreflector.net,62500";
                    }
                }
            }

            m_customhosts = m_localhosts.split('\n');
            for (const auto& i : std::as_const(m_customhosts)){
                QStringList line = i.simplified().split(' ');

                if(line.size() >= 4 && line.at(0) == "FCS"){
                    m_hostmap[line.at(1).simplified()] = line.at(2).simplified() + "," + line.at(3).simplified();
                }
            }

            QMap<QString, QString>::const_iterator i = m_hostmap.constBegin();
            while (i != m_hostmap.constEnd()) {
                m_hostsmodel.append(i.key());
                ++i;
            }
        }
        f.close();
    }
    else{
        download_file("/FCSHosts.txt");
    }
}

void DroidStar::process_dmr_hosts()
{
    m_hostmap.clear();
    m_hostsmodel.clear();
    QFileInfo check_file(config_path + "/DMRHosts.txt");
    if(check_file.exists() && check_file.isFile()){
        QFile f(config_path + "/DMRHosts.txt");
        if(f.open(QIODevice::ReadOnly)){
            while(!f.atEnd()){
                QString l = f.readLine();
                if(l.isEmpty() || l.at(0) == '#'){
                    continue;
                }
                QStringList ll = l.simplified().split(' ');
                if(ll.size() > 4){
                    if( (ll.at(0).simplified() != "DMRGateway")
                     && (ll.at(0).simplified() != "DMR2YSF")
                     && (ll.at(0).simplified() != "DMR2NXDN"))
                    {
                        m_hostmap[ll.at(0).simplified()] = ll.at(2) + "," + ll.at(4) + "," + ll.at(3);
                    }
                }
            }

            m_customhosts = m_localhosts.split('\n');
            for (const auto& i : std::as_const(m_customhosts)){
                QStringList line = i.simplified().split(' ');

                if(line.size() >= 5 && line.at(0) == "DMR"){
                    m_hostmap[line.at(1).simplified()] = line.at(2).simplified() + "," + line.at(3).simplified() + "," + line.mid(4).join(" ").trimmed();
                }
            }

            QMap<QString, QString>::const_iterator i = m_hostmap.constBegin();
            while (i != m_hostmap.constEnd()) {
                m_hostsmodel.append(i.key());
                ++i;
            }
        }
        f.close();
    }
    else{
        download_file("/DMRHosts.txt");
    }
}

void DroidStar::process_p25_hosts()
{
    m_hostmap.clear();
    m_hostsmodel.clear();
    QFileInfo check_file(config_path + "/P25Hosts.txt");
    if(check_file.exists() && check_file.isFile()){
        QFile f(config_path + "/P25Hosts.txt");
        if(f.open(QIODevice::ReadOnly)){
            while(!f.atEnd()){
                QString l = f.readLine();
                if(l.isEmpty() || l.at(0) == '#'){
                    continue;
                }
                QStringList ll = l.simplified().split(' ');
                if(ll.size() > 2){
                    m_hostmap[ll.at(0).simplified()] = ll.at(1) + "," + ll.at(2);
                }
            }

            m_customhosts = m_localhosts.split('\n');
            for (const auto& i : std::as_const(m_customhosts)){
                QStringList line = i.simplified().split(' ');

                if(line.size() >= 4 && line.at(0) == "P25"){
                    m_hostmap[line.at(1).simplified()] = line.at(2).simplified() + "," + line.at(3).simplified();
                }
            }

            QMap<QString, QString>::const_iterator i = m_hostmap.constBegin();
            while (i != m_hostmap.constEnd()) {
                m_hostsmodel.append(i.key());
                ++i;
            }
            QMap<int, QString> m;
            for (auto s : m_hostsmodel) m[s.toInt()] = s;
            m_hostsmodel = QStringList(m.values());
        }
        f.close();
    }
    else{
        download_file("/P25Hosts.txt");
    }
}

void DroidStar::process_nxdn_hosts()
{
    m_hostmap.clear();
    m_hostsmodel.clear();
    QFileInfo check_file(config_path + "/NXDNHosts.txt");
    if(check_file.exists() && check_file.isFile()){
        QFile f(config_path + "/NXDNHosts.txt");
        if(f.open(QIODevice::ReadOnly)){
            while(!f.atEnd()){
                QString l = f.readLine();
                if(l.isEmpty() || l.at(0) == '#'){
                    continue;
                }
                QStringList ll = l.simplified().split(' ');
                if(ll.size() > 2){
                    m_hostmap[ll.at(0).simplified()] = ll.at(1) + "," + ll.at(2);
                }
            }

            m_customhosts = m_localhosts.split('\n');
            for (const auto& i : std::as_const(m_customhosts)){
                QStringList line = i.simplified().split(' ');

                if(line.size() >= 4 && line.at(0) == "NXDN"){
                    m_hostmap[line.at(1).simplified()] = line.at(2).simplified() + "," + line.at(3).simplified();
                }
            }

            QMap<QString, QString>::const_iterator i = m_hostmap.constBegin();
            while (i != m_hostmap.constEnd()) {
                m_hostsmodel.append(i.key());
                ++i;
            }
            QMap<int, QString> m;
            for (auto s : m_hostsmodel) m[s.toInt()] = s;
            m_hostsmodel = QStringList(m.values());
        }
        f.close();
    }
    else{
        download_file("/NXDNHosts.txt");
    }
}

void DroidStar::process_m17_hosts()
{
    m_hostmap.clear();
    m_hostsmodel.clear();

    QFileInfo check_file(config_path + "/M17Hosts-full.csv");
    if(check_file.exists() && check_file.isFile()){
        QFile f(config_path + "/M17Hosts-full.csv");
        if(f.open(QIODevice::ReadOnly)){
            while(!f.atEnd()){
                QString l = f.readLine().simplified();
                if(l.isEmpty() || l.at(0) == '#'){
                    continue;
                }
                if (l.contains(',')) {
                    QStringList ll = l.split(',');
                    if(ll.size() > 4){
                        m_hostmap[ll.at(0).simplified()] = ll.at(2).simplified() + "," + ll.at(4).simplified() + "," + ll.at(3).simplified();
                    }
                } else {
                    QStringList ll = l.split(' ', Qt::SkipEmptyParts);
                    if (ll.size() > 2) {
                        m_hostmap[ll.at(0).simplified()] = ll.at(1).simplified() + "," + ll.at(2).simplified() + ",none";
                    }
                }
            }

            m_customhosts = m_localhosts.split('\n');
            for (const auto& i : std::as_const(m_customhosts)){
                QStringList line = i.simplified().split(' ');

                if(line.size() >= 4 && line.at(0) == "M17"){
                    m_hostmap[line.at(1).simplified()] = line.at(2).simplified() + "," + line.at(3).simplified();
                }
            }
            if(m_mdirect){
                m_hostmap["ALL"] = "ALL";
                m_hostmap["UNLINK"] = "UNLINK";
                m_hostmap["ECHO"] = "ECHO";
                m_hostmap["INFO"] = "INFO";
            }
            QMap<QString, QString>::const_iterator i = m_hostmap.constBegin();
            while (i != m_hostmap.constEnd()) {
                m_hostsmodel.append(i.key());
                ++i;
            }
        }
        f.close();
    }
    else{
        download_file("/M17Hosts-full.csv");
    }
}

void DroidStar::process_iax_hosts()
{
    m_hostmap.clear();
    m_hostsmodel.clear();
    m_customhosts = m_localhosts.split('\n');
    for (const auto& i : std::as_const(m_customhosts)){
        const QString trimmed = i.simplified();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) {
            continue;
        }
        QStringList line = trimmed.split(' ');
        if (line.size() >= 3 && line.at(0).compare("IAX", Qt::CaseInsensitive) == 0) {
            const QString node = line.at(1).simplified();
            if (line.at(2).simplified().compare("wt", Qt::CaseInsensitive) == 0) {
                const QString port = (line.size() == 3) ? "4569" : line.at(3).simplified();
                m_hostmap[node] = node + ".nodes.allstarlink.org," + port + ",allstar-public,allstar";
            } else if (line.size() >= 4) {
                QString host = line.at(2).simplified();
                QString port = line.at(3).simplified();
                int userIdx = 4;

                // Also support users entering `IAX <node> <host>:<port> <username> <password>`
                if (host.count(':') == 1) {
                    const int colonIdx = host.indexOf(':');
                    bool portInHostOk = false;
                    const int parsedPort = host.mid(colonIdx + 1).toInt(&portInHostOk);
                    bool nextIsPort = false;
                    const int explicitPort = port.toInt(&nextIsPort);
                    if (portInHostOk && parsedPort > 0 && parsedPort <= 65535) {
                        host = host.left(colonIdx).simplified();
                        if (!nextIsPort || explicitPort <= 0 || explicitPort > 65535) {
                            port = QString::number(parsedPort);
                            userIdx = 3;
                        }
                    }
                }

                const QString user = (line.size() > userIdx) ? line.at(userIdx).simplified() : "";
                const QString pass = (line.size() > userIdx + 1) ? line.mid(userIdx + 1).join(" ").trimmed() : "";
                m_hostmap[node] = host + "," + port + "," + user + "," + pass;
            }
        }
    }

    QMap<QString, QString>::const_iterator i = m_hostmap.constBegin();
    while (i != m_hostmap.constEnd()) {
        m_hostsmodel.append(i.key());
        ++i;
    }
}

void DroidStar::process_dmr_ids()
{
    QFileInfo check_file(config_path + "/DMRIDs.dat");
    if(check_file.exists() && check_file.isFile()){
        QFile f(config_path + "/DMRIDs.dat");
        if(f.open(QIODevice::ReadOnly)){
            m_dmrids.clear();
            m_callsignToDmrId.clear();
            m_callsignToDmrId.reserve(260000);
            while(!f.atEnd()){
                const QByteArray raw = f.readLine();
                if (raw.isEmpty() || raw.at(0) == '#') {
                    continue;
                }
                const char *p = raw.constData();
                while (*p == ' ' || *p == '\t') ++p;
                char *endp = nullptr;
                const unsigned long id = std::strtoul(p, &endp, 10);
                if (!id || endp == p) continue;
                p = endp;
                while (*p == ' ' || *p == '\t') ++p;
                const char *csStart = p;
                while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') ++p;
                if (p > csStart) {
                    const uint32_t dmrId = static_cast<uint32_t>(id);
                    const QString cs = QString::fromLatin1(csStart, static_cast<qsizetype>(p - csStart));
                    m_dmrids.insert(dmrId, cs);
                    const QString upperCs = cs.toUpper();
                    if (!m_callsignToDmrId.contains(upperCs)) {
                        m_callsignToDmrId.insert(upperCs, dmrId);
                    }
                }
            }
            f.close();
        }
    }
    else{
        download_file("/DMRIDs.dat");
    }
}

void DroidStar::update_dmr_ids()
{
    // Non-destructive update: keep existing DMRIDs.dat intact on disk until the new
    download_file("/DMRIDs.dat");
    update_nxdn_ids();
}

void DroidStar::process_nxdn_ids()
{
    QFileInfo check_file(config_path + "/NXDN.csv");
    if(check_file.exists() && check_file.isFile()){
        QFile f(config_path + "/NXDN.csv");
        if(f.open(QIODevice::ReadOnly)){
            m_nxdnids.clear();
            while(!f.atEnd()){
                QString lids = f.readLine();
                if(lids.isEmpty() || lids.at(0) == '#'){
                    continue;
                }
                QStringList llids = lids.simplified().split(',');

                if(llids.size() > 1){
                    m_nxdnids[llids.at(0).toUInt()] = llids.at(1);
                }
            }
        }
        f.close();
    }
    else{
        download_file("/NXDN.csv");
    }
}

void DroidStar::update_nxdn_ids()
{
    // Non-destructive update: keep existing NXDN.csv intact on disk until the new
    download_file("/NXDN.csv");
}

void DroidStar::update_host_files()
{
    m_update_host_files = true;
    check_host_files();
}

void DroidStar::check_host_files()
{
    if(!QDir(config_path).exists()){
        QDir().mkdir(config_path);
    }

    QFileInfo check_file(config_path + "/dplus.txt");
    if( (!check_file.exists() && !(check_file.isFile())) || m_update_host_files ){
        download_file("/dplus.txt");
    }

    check_file.setFile(config_path + "/dextra.txt");
    if( (!check_file.exists() && !check_file.isFile() ) || m_update_host_files  ){
        download_file("/dextra.txt");
    }

    check_file.setFile(config_path + "/dcs.txt");
    if( (!check_file.exists() && !check_file.isFile()) || m_update_host_files ){
        download_file( "/dcs.txt");
    }

    check_file.setFile(config_path + "/YSFHosts.txt");
    if( (!check_file.exists() && !check_file.isFile()) || m_update_host_files ){
        download_file("/YSFHosts.txt");
    }

    check_file.setFile(config_path + "/FCSHosts.txt");
    if( (!check_file.exists() && !check_file.isFile()) || m_update_host_files ){
        download_file("/FCSHosts.txt");
    }

    check_file.setFile(config_path + "/DMRHosts.txt");
    if( (!check_file.exists() && !check_file.isFile()) || m_update_host_files ){
        download_file("/DMRHosts.txt");
    }

    check_file.setFile(config_path + "/P25Hosts.txt");
    if( (!check_file.exists() && !check_file.isFile()) || m_update_host_files ){
        download_file("/P25Hosts.txt");
    }

    check_file.setFile(config_path + "/NXDNHosts.txt");
    if((!check_file.exists() && !check_file.isFile()) || m_update_host_files ){
        download_file("/NXDNHosts.txt");
    }

    check_file.setFile(config_path + "/M17Hosts-full.csv");
    if( (!check_file.exists() && !check_file.isFile()) || m_update_host_files ){
        download_file("/M17Hosts-full.csv");
    }

    check_file.setFile(config_path + "/DMRIDs.dat");
    if(!check_file.exists() && !check_file.isFile()){
        download_file("/DMRIDs.dat");
    }
    else {
        process_dmr_ids();
    }

    check_file.setFile(config_path + "/NXDN.csv");
    if(!check_file.exists() && !check_file.isFile()){
        download_file("/NXDN.csv");
    }
    else{
        process_nxdn_ids();
    }
    m_update_host_files = false;
    //process_mode_change(ui->modeCombo->currentText().simplified());
/*
#if defined(Q_OS_ANDROID)
    QString vocname = "/vocoder_plugin." + QSysInfo::productType() + "." + QSysInfo::currentCpuArchitecture();
#else
    QString vocname = "/vocoder_plugin." + QSysInfo::kernelType() + "." + QSysInfo::currentCpuArchitecture();
#endif
    QString newvoc = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + vocname;
    QString voc = config_path + vocname;
    check_file.setFile(newvoc);
    qDebug() << "newvoc == " << newvoc;
    qDebug() << "voc == " << voc;
    if(check_file.exists() && check_file.isFile()){
        qDebug() << newvoc << " found";
        if(QFile::exists(voc)){
            qDebug() << voc << " found";
            if(QFile::remove(voc)){
                qDebug() << voc << " deleted";
            }
            else{
                qDebug() << voc << " not deleted";
            }
        }
        if(QFile::copy(newvoc, voc)){
            qDebug() << newvoc << " copied";
        }
        else{
            qDebug() << "Could not copy " << newvoc;
        }
    }
    else{
        qDebug() << newvoc << " not found";
    }
*/
}

void DroidStar::update_data(Mode::MODEINFO info)
{
    if (connect_status == Mode::DISCONNECTED) {
        return;
    }
    // Queued updates from a Mode that was already torn down (tune/reconnect) can still arrive
    if (!m_mode || (sender() && sender() != m_mode)) {
        return;
    }

    // Helpful status tracing for debugging connect/TX issues
    if (connect_status == Mode::CONNECTING) {
        qDebug() << "update_data: CONNECTING, mode_status=" << info.status;
    }
    // If the Mode reports DISCONNECTED while we are trying to connect, treat it as
    if ((connect_status != Mode::DISCONNECTED) && (info.status == Mode::DISCONNECTED || info.status == Mode::CLOSED || info.status == Mode::TIMEOUT)) {
        const bool wasConnecting = (connect_status == Mode::CONNECTING);
        qDebug() << "Connection ended or failed (Mode status=" << info.status << ")";
        m_errortxt = (info.status == Mode::TIMEOUT) ? "Connection timed out" : (wasConnecting ? "Connection failed" : "Disconnected by server");
        connect_status = Mode::DISCONNECTED;
        m_odtpActive = false;
        m_keepAliveTimer->stop();
        stop_mode_thread();
        m_outlevel = 0;
        m_data1.clear();
        m_data2.clear();
        m_data3.clear();
        m_data4.clear();
        m_data5.clear();
        m_data6.clear();
        m_netstatustxt.clear();
        m_ambestatustxt.clear();
        m_mmdvmstatustxt.clear();
        if (TranscriptionEngine *te = TranscriptionEngine::instance()) {
            te->onDisconnected();
        }
#ifdef Q_OS_IOS
        setAudioConnectionState(false, "", "");
        ios_live_activity_end();
#endif
        emit update_data();
        emit update_log(m_errortxt);
        emit connect_status_changed(wasConnecting ? 5 : 0);
        return;
    }

    if( (connect_status == Mode::CONNECTING) && ( info.status == Mode::CONNECTED_RW)){
        connect_status = Mode::CONNECTED_RW;
        if (m_protocol == "DMR") {
            if (DMR *dmrMode = qobject_cast<DMR *>(m_mode)) {
                m_odtpActive = dmrMode->is_odtp();
                m_port = info.port;
            }
        }
        emit connect_status_changed(2);
        emit in_audio_vol_changed(m_inVol);
        emit out_audio_vol_changed(m_outVol);
        emit agc_state_changed(m_agc ? 1 : 0);
        emit swtx_state(!m_mode->get_hwtx());
        emit swrx_state(!m_mode->get_hwrx());
        emit rptr2_changed(m_refname + " " + m_module);
        if(m_mycall.isEmpty()) set_mycall(m_callsign);
        if(m_urcall.isEmpty()) set_urcall("CQCQCQ");
        if(m_rptr1.isEmpty()) set_rptr1(m_callsign + " " + m_module);
        emit update_log("Connected to " + m_protocol + (m_odtpActive ? " (ODTP) " : " ") + m_refname + " " + m_host + ":" + QString::number(m_port));
#ifdef Q_OS_IOS
        // Notify audio session manager of connection (enables Now Playing & keep-alive)
        setAudioConnectionState(true, m_refname.toUtf8().constData(), m_protocol.toUtf8().constData());
        const QString initTg = m_dmr_destid ? QString::number(m_dmr_destid) : QString();
        ios_live_activity_start_or_update("IDLE",
                                          m_protocol.toUtf8().constData(),
                                          m_refname.toUtf8().constData(),
                                          m_callsign.toUtf8().constData(),
                                          initTg.toUtf8().constData());
#endif

        if(info.sw_vocoder_loaded){
            emit update_log("Vocoder plugin loaded");
        }
        else{
            emit update_log("Vocoder plugin not loaded");
            emit open_vocoder_dialog();
        }
    }

    m_netstatustxt = "Connected ping cnt: " + QString::number(info.count);
    m_ambestatustxt = "AMBE: " + (info.ambeprodid.isEmpty() ? "No device" : info.ambeprodid);
    m_mmdvmstatustxt = "MMDVM: ";

    if(info.mmdvm.isEmpty()){
        m_mmdvmstatustxt += "No device";
    }

    QStringList verlist = info.ambeverstr.split('.');
    if(verlist.size() > 7){
        m_ambestatustxt += " " + verlist.at(0) + " " + verlist.at(5) + " " + verlist.at(6);
    }

    verlist = info.mmdvm.split(' ');
    if(verlist.size() > 3){
        m_mmdvmstatustxt += verlist.at(0) + " " + verlist.at(1);
    }

    if(info.stream_state == Mode::STREAM_IDLE ||
       info.stream_state == Mode::TRANSMITTING ||
       info.stream_state == Mode::TRANSMITTING_MODEM){
        m_data1.clear();
        m_data2.clear();
        m_data3.clear();
        m_data4.clear();
        m_data5.clear();
        m_data6.clear();
    }
    else if (m_protocol == "REF" || m_protocol == "XRF" || m_protocol == "DCS"){
        m_data1 = info.src;
        m_data2 = info.dst;
        m_data3 = info.gw;
        m_data4 = info.gw2;
        m_data5 = QString::number(info.streamid, 16) + " " + QString("%1").arg(info.frame_number, 2, 16, QChar('0'));
        m_data6 = info.usertxt;
    }
    else if (m_protocol == "YSF" || m_protocol == "FCS"){
        m_data1 = info.gw;
        m_data2 = info.src;
        m_data3 = info.dst;

        if(info.type == 0){
            m_data4 = "V/D mode 1";
        }
        else if(info.type == 1){
            m_data4 = "Data Full Rate";
        }
        else if(info.type == 2){
            m_data4 = "V/D mode 2";
        }
        else if(info.type == 3){
            m_data4 = "Voice Full Rate";
        }
        else{
            m_data4 = "";
        }
        if(info.type >= 0){
            m_data5 = info.path  ? "Internet" : "Local";
            m_data6 = QString::number(info.frame_number) + "/" + QString::number(info.frame_total);
        }
        else{
            m_data5 = m_data6 = "";
        }
    }
    else if(m_protocol == "DMR"){
        if (info.srcid == 0U && info.dstid > 0U && m_odtpActive) {
            // If joining an ongoing ODTP call mid-stream before the next SuperHeader/LC
            const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
            auto hlIt = m_hoselineCalls.constFind(static_cast<int>(info.dstid));
            if (hlIt != m_hoselineCalls.constEnd() && hlIt->stopSec == 0 && (nowSec - hlIt->startSec) < 210 && hlIt->sourceId > 0U) {
                info.srcid = hlIt->sourceId;
                if (info.src.isEmpty() && !hlIt->sourceCall.isEmpty()) {
                    info.src = hlIt->sourceCall;
                }
            }
        }
        m_data1 = m_dmrids.value(info.srcid);
        if (m_data1.isEmpty() && !info.src.isEmpty()) {
            m_data1 = info.src;
        }
        if (m_data1.isEmpty() && info.srcid) {
            if (info.srcid == 9990 || info.srcid == 31000) m_data1 = "Parrot";
            else if (info.srcid == 4000) m_data1 = "Unlink";
            else if (info.srcid == 5000) m_data1 = "Status";
            else m_data1 = QString::number(info.srcid);
        }
        m_data2 = info.srcid ? QString::number(info.srcid) : "";
        m_data3 = info.dstid ? QString::number(info.dstid) : "";
        m_data4 = info.gwid ? QString::number(info.gwid) : "";
        const uint8_t slotNum = (info.slot & 0x03) ? (info.slot & 0x03) : 2;
        QString s = "Slot" + QString::number(slotNum);
        QString flco = (info.slot & 0x40) ? "Private" : "Group";

        if(info.frame_number){
            QString n = s + " " + flco + " " + QString("%1").arg(info.frame_number, 2, 16, QChar('0'));
            m_data5 = n;
        }
    }
    else if(m_protocol == "P25"){
        m_data1 = m_dmrids.value(info.srcid);
        m_data2 = info.srcid ? QString::number(info.srcid) : "";
        m_data3 = info.dstid ? QString::number(info.dstid) : "";
        m_data4 = info.srcid ? QString::number(info.srcid) : "";
        if(info.frame_number){
            QString n = QString("%1").arg(info.frame_number, 2, 16, QChar('0'));
            m_data5 = n;
        }
    }
    else if(m_protocol == "NXDN"){
        if(info.srcid){
            m_data1 = m_nxdnids.value(static_cast<uint16_t>(info.srcid));
            m_data2 = QString::number(info.srcid);
        }
        m_data3 = QString::number(info.dstid);

        if(info.frame_number){
            QString n = QString("%1").arg(info.frame_number, 4, 16, QChar('0'));
            m_data5 = n;
        }
    }
    else if(m_protocol == "M17"){
        m_data1 = info.src;
        m_data2 = info.dst + " " + info.module;
        m_data3 = info.type ? "3200 Voice" : "1600 V/D";
        if(info.frame_number){
            QString n = QString("%1").arg(info.frame_number, 4, 16, QChar('0'));
            m_data4 = n;
        }
        m_data5 = QString::number(info.streamid, 16);
    }
    else if(m_protocol == "IAX"){

    }
    QString t = QDateTime::fromMSecsSinceEpoch(info.ts).toString("yyyy.MM.dd hh:mm:ss.zzz");
    if((m_protocol == "DMR") || (m_protocol == "P25") || (m_protocol == "NXDN")){
        if(info.stream_state == Mode::STREAM_NEW){
            emit update_log(t + " " + m_protocol + " RX started id: " + " srcid: " + QString::number(info.srcid) + " dstid: " + QString::number(info.dstid));
        }
        if(info.stream_state == Mode::STREAM_END){
            emit update_log(t + " " + m_protocol + " RX ended id: " + " srcid: " + QString::number(info.srcid) + " dstid: " + QString::number(info.dstid));
        }
        if(info.stream_state == Mode::STREAM_LOST){
            emit update_log(t + " " + m_protocol + " RX lost id: " + " srcid: " + QString::number(info.srcid) + " dstid: " + QString::number(info.dstid));
        }
    }
    else{
        if(info.stream_state == Mode::STREAM_NEW){
            emit update_log(t + " " + m_protocol + " RX started id: " + QString::number(info.streamid, 16) + " src: " + info.src + " dst: " + info.gw2);
        }
        if(info.stream_state == Mode::STREAM_END){
            emit update_log(t + " " + m_protocol + " RX ended id: " + QString::number(info.streamid, 16) + " src: " + info.src + " dst: " + info.gw2);
        }
        if(info.stream_state == Mode::STREAM_LOST){
            emit update_log(t + " " + m_protocol + " RX lost id: " + QString::number(info.streamid, 16) + " src: " + info.src + " dst: " + info.gw2);
        }
    }
    
    const QString resolvedRxCall = (m_protocol == "YSF" || m_protocol == "FCS")
                                   ? (!m_data2.isEmpty() ? m_data2.trimmed() : m_data1.trimmed())
                                   : m_data1.trimmed();
    if (TranscriptionEngine *te = TranscriptionEngine::instance()) {
        te->onStreamStateChanged(info.streamid, info.srcid, resolvedRxCall, info.stream_state);
    }

#ifdef Q_OS_IOS
    const QString &activeRxCall = resolvedRxCall;
    const QString activeTg = !m_data3.isEmpty() ? m_data3.trimmed()
                             : (m_dmr_destid ? QString::number(m_dmr_destid) : QString());

    if (info.stream_state == Mode::STREAM_IDLE || info.stream_state == Mode::STREAM_END || info.stream_state == Mode::STREAM_LOST) {
        clearAudioRXState();
        if (connect_status == Mode::CONNECTED_RW) {
            ios_live_activity_start_or_update("IDLE",
                                              m_protocol.toUtf8().constData(),
                                              m_refname.toUtf8().constData(),
                                              m_callsign.toUtf8().constData(),
                                              activeTg.toUtf8().constData());
        }
    } else if (info.stream_state == Mode::TRANSMITTING || info.stream_state == Mode::TRANSMITTING_MODEM) {
        ios_live_activity_start_or_update("TX",
                                          m_callsign.toUtf8().constData(),
                                          (m_protocol + " • Transmitting").toUtf8().constData(),
                                          m_refname.toUtf8().constData(),
                                          activeTg.toUtf8().constData());
    } else if (!activeRxCall.isEmpty()) {
        setAudioRXState(activeRxCall.toUtf8().constData(), "", "");
        if (info.stream_state == Mode::STREAM_NEW) {
            ios_live_activity_start_or_update("RX",
                                              activeRxCall.toUtf8().constData(),
                                              (m_protocol == "DMR" && !m_data2.isEmpty()) ? ("ID " + m_data2).toUtf8().constData() : "",
                                              m_refname.toUtf8().constData(),
                                              activeTg.toUtf8().constData());
        }
    }
#endif
    
    emit update_data();
}

void DroidStar::updatelog(QString s)
{
    emit update_log(s);
}

QString DroidStar::lookupCallsignForDmrId(uint32_t dmrId) const
{
    return m_dmrids.value(dmrId).trimmed();
}

uint32_t DroidStar::lookupDmrIdForCallsign(const QString &callsign) const
{
    QString wanted = callsign.trimmed().toUpper();
    if (wanted.isEmpty())
        return 0U;
    const int dash = wanted.indexOf(QStringLiteral(" - "));
    if (dash > 0)
        wanted.truncate(dash);
    const int space = wanted.indexOf(QLatin1Char(' '));
    if (space > 0)
        wanted.truncate(space);
    auto fastIt = m_callsignToDmrId.constFind(wanted);
    if (fastIt != m_callsignToDmrId.constEnd())
        return fastIt.value();
    for (auto it = m_dmrids.constBegin(); it != m_dmrids.constEnd(); ++it) {
        QString candidate = it.value().trimmed().toUpper();
        const int cSpace = candidate.indexOf(QLatin1Char(' '));
        if (cSpace > 0)
            candidate.truncate(cSpace);
        if (candidate == wanted)
            return it.key();
    }
    return 0U;
}

QVariantList DroidStar::searchDmrContacts(const QString &query, int limit) const
{
    QVariantList out;
    const QString q = query.trimmed().toUpper();
    if (q.isEmpty() || limit <= 0)
        return out;

    const int maxOut = std::clamp(limit, 1, 25);
    QSet<uint32_t> seen;

    auto addResult = [&](uint32_t id, const QString &cs) {
        if (id == 0U || seen.contains(id))
            return;
        seen.insert(id);
        out.append(QVariantMap{
            { QStringLiteral("id"), id },
            { QStringLiteral("callsign"), cs.isEmpty() ? QString::number(id) : cs },
            { QStringLiteral("isGroup"), false }
        });
    };

    // 1. Exact ID match
    bool isDigits = false;
    const uint32_t exactId = q.toUInt(&isDigits);
    if (isDigits && exactId > 0U) {
        addResult(exactId, m_dmrids.value(exactId));
    }

    // 2. O(1) exact callsign match
    auto exactCsIt = m_callsignToDmrId.constFind(q);
    if (exactCsIt != m_callsignToDmrId.constEnd()) {
        addResult(exactCsIt.value(), q);
    }

    if (out.size() >= maxOut || q.size() < 2)
        return out;

    // 3. Fast prefix match on numeric IDs (using QMap::lowerBound) or callsigns
    if (isDigits && q.size() >= 3) {
        uint32_t lo = exactId;
        uint32_t hi = exactId + 1U;
        for (int scale = 0; scale < 5 && lo < 1000000U; ++scale) {
            lo *= 10U;
            hi *= 10U;
            auto it = m_dmrids.lowerBound(lo);
            while (it != m_dmrids.constEnd() && it.key() < hi && out.size() < maxOut) {
                addResult(it.key(), it.value());
                ++it;
            }
            if (out.size() >= maxOut)
                break;
        }
    } else if (!isDigits && q.size() >= 2) {
        for (auto it = m_callsignToDmrId.constBegin(); it != m_callsignToDmrId.constEnd(); ++it) {
            if (it.key().startsWith(q)) {
                addResult(it.value(), it.key());
                if (out.size() >= maxOut)
                    break;
            }
        }
    }
    return out;
}

bool DroidStar::isDmrConnected() const
{
    return m_protocol == QLatin1String("DMR") &&
           connect_status == Mode::CONNECTED_RW &&
           m_mode != nullptr;
}

void DroidStar::requestDmrSms(int msgId, uint32_t dstId, const QString &text,
                              bool isGroup, int format)
{
    if (!isDmrConnected()) {
        emit dmr_sms_send_status(msgId, 3, QStringLiteral("DMR is not connected"));
        return;
    }
    emit send_dmr_sms(msgId, dstId, text, isGroup, format);
}

void DroidStar::set_input_volume(qreal v)
{
    m_inVol = std::clamp(v, 0.0, 1.0);
    emit in_audio_vol_changed(m_inVol);
}

void DroidStar::set_output_volume(qreal v)
{
    m_outVol = std::clamp(v, 0.0, 2.0);
    emit out_audio_vol_changed(m_outVol);
}

void DroidStar::press_tx()
{
    if (m_autoTxPunchStartTimer) m_autoTxPunchStartTimer->stop();
    if (m_autoTxPunchStopTimer) m_autoTxPunchStopTimer->stop();
    if (m_autoTxPunchRunning) {
        m_autoTxPunchRunning = false;
        if (m_mode) m_mode->set_auto_tx_punch(false);
    }
#ifdef Q_OS_IOS
    setAudioTXState(true);
    const QString tg = m_dmr_destid ? QString::number(m_dmr_destid) : m_data3;
    ios_live_activity_start_or_update("TX",
                                      m_callsign.toUtf8().constData(),
                                      (m_protocol + " • Transmitting").toUtf8().constData(),
                                      m_refname.toUtf8().constData(),
                                      tg.toUtf8().constData());
#endif
    emit tx_pressed();
}

void DroidStar::release_tx()
{
#ifdef Q_OS_IOS
    setAudioTXState(false);
    if (connect_status == Mode::CONNECTED_RW) {
        const QString tg = m_dmr_destid ? QString::number(m_dmr_destid) : m_data3;
        ios_live_activity_start_or_update("IDLE",
                                          m_protocol.toUtf8().constData(),
                                          m_refname.toUtf8().constData(),
                                          m_callsign.toUtf8().constData(),
                                          tg.toUtf8().constData());
    }
#endif
    emit tx_released();
}

void DroidStar::click_tx(bool tx)
{
#ifdef Q_OS_IOS
    setAudioTXState(tx);
    if (connect_status == Mode::CONNECTED_RW) {
        const QString tg = m_dmr_destid ? QString::number(m_dmr_destid) : m_data3;
        if (tx) {
            ios_live_activity_start_or_update("TX",
                                              m_callsign.toUtf8().constData(),
                                              (m_protocol + " • Transmitting").toUtf8().constData(),
                                              m_refname.toUtf8().constData(),
                                              tg.toUtf8().constData());
        } else {
            ios_live_activity_start_or_update("IDLE",
                                              m_protocol.toUtf8().constData(),
                                              m_refname.toUtf8().constData(),
                                              m_callsign.toUtf8().constData(),
                                              tg.toUtf8().constData());
        }
    }
#endif
    emit tx_clicked(tx);
}

void DroidStar::addRecentTGID(const QString& tgid) {
    const QString cleaned = tgid.simplified();
    if (cleaned.isEmpty() || cleaned.toInt() <= 0) {
        return;
    }

    QStringList tgids = loadRecentTGIDs();
    tgids.removeAll(cleaned);
    tgids.prepend(cleaned);
    while (tgids.size() > 50) {
        tgids.removeLast();
    }

    QSettings settings;
    settings.beginGroup("RecentTGIDs");
    settings.setValue("tgids", tgids);
    settings.endGroup();

    if (m_settings) {
        m_settings->setValue("RECENT_TGIDS", tgids);
        m_settings->sync();
    }
    emit recentTgidsUpdated();
}

void DroidStar::removeRecentTGID(const QString& tgid) {
    const QString cleaned = tgid.simplified();
    if (cleaned.isEmpty()) {
        return;
    }

    QStringList tgids = loadRecentTGIDs();
    if (tgids.removeAll(cleaned) > 0) {
        QSettings settings;
        settings.beginGroup("RecentTGIDs");
        settings.setValue("tgids", tgids);
        settings.endGroup();

        if (m_settings) {
            m_settings->setValue("RECENT_TGIDS", tgids);
            m_settings->sync();
        }
        emit recentTgidsUpdated();
    }
}

void DroidStar::updateNowPlayingRX(const QString& callsign, const QString& name, const QString& country)
{
#ifdef Q_OS_IOS
    setAudioRXState(callsign.toUtf8().constData(), 
                    name.toUtf8().constData(), 
                    country.toUtf8().constData());
    if (connect_status == Mode::CONNECTED_RW && !callsign.trimmed().isEmpty()) {
        const QString tg = !m_data3.isEmpty() ? m_data3.trimmed()
                           : (m_dmr_destid ? QString::number(m_dmr_destid) : QString());
        ios_live_activity_start_or_update("RX",
                                          callsign.trimmed().toUtf8().constData(),
                                          name.toUtf8().constData(),
                                          country.toUtf8().constData(),
                                          tg.toUtf8().constData());
    }
#else
    Q_UNUSED(callsign);
    Q_UNUSED(name);
    Q_UNUSED(country);
#endif
}

QStringList DroidStar::loadRecentTGIDs() const {
    QStringList result;
    if (m_settings && m_settings->contains("RECENT_TGIDS")) {
        result = m_settings->value("RECENT_TGIDS").toStringList();
    }
    if (result.isEmpty()) {
        QSettings settings;
        settings.beginGroup("RecentTGIDs");
        result = settings.value("tgids").toStringList();
        settings.endGroup();
    }
    QStringList cleaned;
    for (const QString &item : std::as_const(result)) {
        const QString s = item.simplified();
        if (!s.isEmpty() && s.toInt() > 0 && !cleaned.contains(s)) {
            cleaned.append(s);
        }
    }
    return cleaned;
}

void DroidStar::clearRecentTGIDs() {
    QSettings settings;
    settings.beginGroup("RecentTGIDs");
    settings.remove("tgids");
    settings.endGroup();
    if (m_settings) {
        m_settings->remove("RECENT_TGIDS");
        m_settings->sync();
    }
    emit recentTgidsUpdated();
}

QStringList DroidStar::loadFavouriteTGIDs() const {
    QStringList result;
    if (m_settings && m_settings->contains("FAVOURITE_TGIDS")) {
        result = m_settings->value("FAVOURITE_TGIDS").toStringList();
    }
    if (result.isEmpty()) {
        QSettings settings;
        settings.beginGroup("RecentTGIDs");
        result = settings.value("favourites").toStringList();
        settings.endGroup();
    }
    QStringList cleaned;
    for (const QString &item : std::as_const(result)) {
        const QString s = item.simplified();
        if (!s.isEmpty() && s.toInt() > 0 && !cleaned.contains(s)) {
            cleaned.append(s);
        }
    }
    return cleaned;
}

void DroidStar::addFavouriteTGID(const QString& tgid) {
    const QString cleaned = tgid.simplified();
    if (cleaned.isEmpty() || cleaned.toInt() <= 0) {
        return;
    }
    QStringList favs = loadFavouriteTGIDs();
    if (!favs.contains(cleaned)) {
        favs.prepend(cleaned);
        QSettings settings;
        settings.beginGroup("RecentTGIDs");
        settings.setValue("favourites", favs);
        settings.endGroup();
        if (m_settings) {
            m_settings->setValue("FAVOURITE_TGIDS", favs);
            m_settings->sync();
        }
        emit favouriteTgidsUpdated();
    }
}

void DroidStar::removeFavouriteTGID(const QString& tgid) {
    const QString cleaned = tgid.simplified();
    if (cleaned.isEmpty()) {
        return;
    }
    QStringList favs = loadFavouriteTGIDs();
    if (favs.removeAll(cleaned) > 0) {
        QSettings settings;
        settings.beginGroup("RecentTGIDs");
        settings.setValue("favourites", favs);
        settings.endGroup();
        if (m_settings) {
            m_settings->setValue("FAVOURITE_TGIDS", favs);
            m_settings->sync();
        }
        emit favouriteTgidsUpdated();
    }
}

void DroidStar::toggleFavouriteTGID(const QString& tgid) {
    const QString cleaned = tgid.simplified();
    if (cleaned.isEmpty() || cleaned.toInt() <= 0) {
        return;
    }
    if (isFavouriteTGID(cleaned)) {
        removeFavouriteTGID(cleaned);
    } else {
        addFavouriteTGID(cleaned);
    }
}

bool DroidStar::isFavouriteTGID(const QString& tgid) const {
    const QString cleaned = tgid.simplified();
    return !cleaned.isEmpty() && loadFavouriteTGIDs().contains(cleaned);
}

void DroidStar::initTalkgroupDirectory()
{
    static const struct { int id; const char *name; } kBuiltInTgs[] = {
        { 1, "Local" },
        { 2, "Cluster" },
        { 8, "Regional" },
        { 9, "Local / Reflector" },
        { 91, "World-wide" },
        { 92, "Europe" },
        { 93, "North America" },
        { 94, "Asia & Middle East" },
        { 95, "Australia & New Zealand" },
        { 98, "Radio Test" },
        { 202, "Greece" },
        { 204, "Netherlands" },
        { 206, "Belgium" },
        { 208, "France" },
        { 214, "Spain" },
        { 216, "Hungary" },
        { 218, "Bosnia & Herzegovina" },
        { 219, "Croatia" },
        { 220, "Serbia" },
        { 222, "Italy" },
        { 226, "Romania" },
        { 228, "Switzerland" },
        { 230, "Czech Republic" },
        { 231, "Slovakia" },
        { 232, "Austria" },
        { 235, "United Kingdom" },
        { 238, "Denmark" },
        { 240, "Sweden" },
        { 242, "Norway" },
        { 244, "Finland" },
        { 250, "Russia" },
        { 255, "Ukraine" },
        { 260, "Poland" },
        { 262, "Germany" },
        { 268, "Portugal" },
        { 272, "Ireland" },
        { 284, "Bulgaria" },
        { 286, "Turkey" },
        { 302, "Canada" },
        { 310, "TAC 310" },
        { 311, "TAC 311" },
        { 312, "TAC 312" },
        { 3100, "USA Nationwide" },
        { 3101, "Alabama" },
        { 3102, "Alaska" },
        { 3104, "Arizona" },
        { 3105, "Arkansas" },
        { 3106, "California" },
        { 3108, "Colorado" },
        { 3109, "Connecticut" },
        { 3112, "Florida" },
        { 3113, "Georgia" },
        { 3115, "Hawaii" },
        { 3117, "Illinois" },
        { 3118, "Indiana" },
        { 3120, "Kansas" },
        { 3121, "Kentucky" },
        { 3122, "Louisiana" },
        { 3124, "Maryland" },
        { 3125, "Massachusetts" },
        { 3126, "Michigan" },
        { 3127, "Minnesota" },
        { 3129, "Missouri" },
        { 3132, "Nevada" },
        { 3134, "New Jersey" },
        { 3136, "New York" },
        { 3137, "North Carolina" },
        { 3139, "Ohio" },
        { 3140, "Oklahoma" },
        { 3141, "Oregon" },
        { 3142, "Pennsylvania" },
        { 3145, "South Carolina" },
        { 3147, "Tennessee" },
        { 3148, "Texas" },
        { 3149, "Utah" },
        { 3151, "Virginia" },
        { 3153, "Washington" },
        { 3155, "Wisconsin" },
        { 334, "Mexico" },
        { 404, "India" },
        { 4040, "India National" },
        { 4041, "India North" },
        { 4042, "India South" },
        { 4043, "India East" },
        { 4044, "India West" },
        { 424, "UAE" },
        { 425, "Israel" },
        { 440, "Japan" },
        { 450, "South Korea" },
        { 460, "China" },
        { 502, "Malaysia" },
        { 505, "Australia" },
        { 510, "Indonesia" },
        { 515, "Philippines" },
        { 520, "Thailand" },
        { 525, "Singapore" },
        { 530, "New Zealand" },
        { 655, "South Africa" },
        { 716, "Peru" },
        { 722, "Argentina" },
        { 724, "Brazil" },
        { 730, "Chile" },
        { 732, "Colombia" },
        { 4000, "Unlink / Disconnect" },
        { 9990, "Parrot Echo" }
    };
    for (const auto &entry : kBuiltInTgs) {
        m_tgNames.insert(entry.id, QString::fromUtf8(entry.name));
    }

    static const struct { int id; const char *name; } kBuiltInTgifTgs[] = {
        { 9, "Local" },
        { 101, "Tac-101" },
        { 102, "Tac-102" },
        { 103, "Tac-103" },
        { 104, "Tac-104" },
        { 110, "North America" },
        { 111, "Europe" },
        { 112, "Asia Pacific" },
        { 113, "World Wide English" },
        { 114, "World Wide" },
        { 116, "United Kingdom" },
        { 777, "TGIF Scan" },
        { 4000, "Unlink / Disconnect" },
        { 9990, "Parrot Echo" },
        { 31000, "TGIF Parrot Echo" },
        { 31665, "TGIF The Mothership" }
    };
    for (const auto &entry : kBuiltInTgifTgs) {
        m_tgifTgNames.insert(entry.id, QString::fromUtf8(entry.name));
    }
}

void DroidStar::parseTgifTalkgroupsCsv(const QByteArray &raw)
{
    const QList<QByteArray> lines = raw.split('\n');
    for (const QByteArray &lineRaw : lines) {
        const QString line = QString::fromUtf8(lineRaw).trimmed();
        if (line.isEmpty() || line.startsWith(QStringLiteral("TG Number"), Qt::CaseInsensitive)) {
            continue;
        }
        const int commaIdx = line.indexOf(QLatin1Char(','));
        if (commaIdx <= 0) {
            continue;
        }
        const int id = line.left(commaIdx).trimmed().toInt();
        const QString name = line.mid(commaIdx + 1).trimmed();
        if (id > 0 && !name.isEmpty()) {
            m_tgifTgNames.insert(id, name);
        }
    }
}

void DroidStar::loadTgifTalkgroupCache()
{
    const QString cacheFile = config_path + "/tgif_talkgroups.csv";
    QFile f(cacheFile);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QByteArray raw = f.readAll();
    f.close();
    parseTgifTalkgroupsCsv(raw);
}

void DroidStar::fetchTgifTalkgroups()
{
    if (!m_hoselineNam) {
        return;
    }
    QNetworkRequest req(QUrl(QStringLiteral("https://api.tgif.network/dmr/talkgroups/csv")));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DroidStar/2026"));
    req.setTransferTimeout(15000);
    QNetworkReply *reply = m_hoselineNam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            return;
        }
        const QByteArray raw = reply->readAll();
        const int prevCount = m_tgifTgNames.size();
        parseTgifTalkgroupsCsv(raw);
        if (m_tgifTgNames.size() > prevCount && m_tgifTgNames.size() > 50) {
            QDir().mkpath(config_path);
            QFile f(config_path + "/tgif_talkgroups.csv");
            if (f.open(QIODevice::WriteOnly)) {
                f.write(raw);
                f.close();
            }
            emit hoselineUpdated();
        }
    });
}

void DroidStar::loadTalkgroupCache()
{
    const QString cacheFile = config_path + "/bm_talkgroups.json";
    QFile f(cacheFile);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const int id = it.key().toInt();
        const QString name = it.value().toString().trimmed();
        if (id > 0 && !name.isEmpty()) {
            m_tgNames.insert(id, name);
        }
    }
}

void DroidStar::fetchBrandMeisterTalkgroups()
{
    if (!m_hoselineNam) {
        return;
    }
    QNetworkRequest req(QUrl(QStringLiteral("https://api.brandmeister.network/v2/talkgroup")));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DroidStar/2026"));
    req.setTransferTimeout(15000);
    QNetworkReply *reply = m_hoselineNam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            return;
        }
        const QByteArray raw = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (!doc.isObject()) {
            return;
        }
        const QJsonObject obj = doc.object();
        int count = 0;
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            const int id = it.key().toInt();
            const QString name = it.value().toString().trimmed();
            if (id > 0 && !name.isEmpty()) {
                m_tgNames.insert(id, name);
                ++count;
            }
        }
        if (count > 50) {
            QDir().mkpath(config_path);
            QFile f(config_path + "/bm_talkgroups.json");
            if (f.open(QIODevice::WriteOnly)) {
                f.write(raw);
                f.close();
            }
            emit hoselineUpdated();
        }
    });
}

QString DroidStar::getTalkgroupName(const QString& tgid) const
{
    const int id = tgid.simplified().toInt();
    if (id <= 0) {
        return QString();
    }
    if (is_tgif_host()) {
        auto tgifIt = m_tgifTgNames.constFind(id);
        if (tgifIt != m_tgifTgNames.constEnd()) {
            return tgifIt.value();
        }
    }
    auto it = m_tgNames.constFind(id);
    if (it != m_tgNames.constEnd()) {
        return it.value();
    }
    auto tgifFallback = m_tgifTgNames.constFind(id);
    if (tgifFallback != m_tgifTgNames.constEnd()) {
        return tgifFallback.value();
    }
    auto liveIt = m_hoselineCalls.constFind(id);
    if (liveIt != m_hoselineCalls.constEnd() && !liveIt->tgName.isEmpty()) {
        return liveIt->tgName;
    }
    return QString();
}

QVariantList DroidStar::searchTalkgroups(const QString& query, int limit) const
{
    const QString q = query.simplified().toLower();
    const QStringList favs = loadFavouriteTGIDs();
    const QStringList recents = loadRecentTGIDs();
    const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
    const QHash<int, QString> &primaryDir = is_tgif_host() ? m_tgifTgNames : m_tgNames;
    const QHash<int, QString> &secondaryDir = is_tgif_host() ? m_tgNames : m_tgifTgNames;

    auto makeEntry = [&](int id, const QString &name) -> QVariantMap {
        const QString idStr = QString::number(id);
        QVariantMap m;
        m["tgid"] = idStr;
        m["name"] = !name.isEmpty() ? name : getTalkgroupName(idStr);
        m["isFavourite"] = favs.contains(idStr);
        m["isRecent"] = recents.contains(idStr);
        auto callIt = m_hoselineCalls.constFind(id);
        if (callIt != m_hoselineCalls.constEnd() && !is_tgif_host()) {
            const bool live = (callIt->stopSec == 0 && (nowSec - callIt->startSec) < 240);
            m["isLive"] = live;
            m["sourceCall"] = callIt->sourceCall;
            m["sourceName"] = callIt->sourceName;
        } else {
            m["isLive"] = false;
            m["sourceCall"] = QString();
            m["sourceName"] = QString();
        }
        return m;
    };

    QVariantList results;
    QSet<int> added;

    auto matches = [&](int id, const QString &name) -> bool {
        if (q.isEmpty()) return true;
        if (QString::number(id).contains(q)) return true;
        return name.toLower().contains(q);
    };

    // 1. Favourites first
    for (const QString &f : favs) {
        const int id = f.toInt();
        if (id <= 0 || added.contains(id)) continue;
        const QString name = getTalkgroupName(f);
        if (matches(id, name)) {
            results.append(makeEntry(id, name));
            added.insert(id);
            if (results.size() >= limit) return results;
        }
    }

    // 2. Recent TGs second
    for (const QString &r : recents) {
        const int id = r.toInt();
        if (id <= 0 || added.contains(id)) continue;
        const QString name = getTalkgroupName(r);
        if (matches(id, name)) {
            results.append(makeEntry(id, name));
            added.insert(id);
            if (results.size() >= limit) return results;
        }
    }

    // 3. If user typed a search query, match active network directory
    if (!q.isEmpty()) {
        const int exactId = q.toInt();
        if (exactId > 0 && !added.contains(exactId)) {
            results.append(makeEntry(exactId, getTalkgroupName(QString::number(exactId))));
            added.insert(exactId);
        }

        QList<int> allIds = primaryDir.keys();
        std::sort(allIds.begin(), allIds.end());
        for (int id : allIds) {
            if (added.contains(id)) continue;
            const QString &name = primaryDir[id];
            if (matches(id, name)) {
                results.append(makeEntry(id, name));
                added.insert(id);
                if (results.size() >= limit) break;
            }
        }

        if (results.size() < limit) {
            QList<int> secIds = secondaryDir.keys();
            std::sort(secIds.begin(), secIds.end());
            for (int id : secIds) {
                if (added.contains(id)) continue;
                const QString &name = secondaryDir[id];
                if (matches(id, name)) {
                    results.append(makeEntry(id, name));
                    added.insert(id);
                    if (results.size() >= limit) break;
                }
            }
        }
    }

    return results;
}

void DroidStar::setHoselineActive(bool active)
{
    if (m_hoselineActive == active) {
        if (active && !m_hoselineConnected && !m_hoselinePollReply) {
            startHoselineSession();
        }
        return;
    }
    m_hoselineActive = active;
    if (active) {
        m_hoselineFailCount = 0;
        m_hoselineUiTimer->start();
        startHoselineSession();
    } else {
        stopHoselineSession();
        m_hoselineUiTimer->stop();
    }
    emit hoselineUpdated();
}

void DroidStar::refreshHoseline()
{
    m_hoselineFailCount = 0;
    stopHoselineSession();
    if (m_hoselineActive) {
        startHoselineSession();
    } else {
        setHoselineActive(true);
    }
}

void DroidStar::stopHoselineSession()
{
    if (m_hoselineReconnectTimer) {
        m_hoselineReconnectTimer->stop();
    }
    if (m_hoselinePollReply) {
        m_hoselinePollReply->disconnect(this);
        m_hoselinePollReply->abort();
        m_hoselinePollReply->deleteLater();
        m_hoselinePollReply = nullptr;
    }
    if (m_hoselinePostReply) {
        m_hoselinePostReply->disconnect(this);
        m_hoselinePostReply->abort();
        m_hoselinePostReply->deleteLater();
        m_hoselinePostReply = nullptr;
    }
    m_hoselinePostQueue.clear();
    m_hoselineSid.clear();
    m_hoselineConnected = false;
    m_hoselineJoined = false;
}

int DroidStar::hoselineRetryDelayMs(int baseMs)
{
    // Exponential backoff (base, 2x, 4x ... capped at 60 s) so an offline device
    const int shift = std::min(m_hoselineFailCount, 5);
    ++m_hoselineFailCount;
    return std::min(60000, baseMs << shift);
}

void DroidStar::startHoselineSession()
{
    if (!m_hoselineActive || !m_hoselineNam) {
        return;
    }
    stopHoselineSession();

    QUrl url(QStringLiteral("https://api.brandmeister.network/lh/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("version"), QStringLiteral("2"));
    query.addQueryItem(QStringLiteral("EIO"), QStringLiteral("4"));
    query.addQueryItem(QStringLiteral("transport"), QStringLiteral("polling"));
    query.addQueryItem(QStringLiteral("t"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DroidStar/2026"));
    req.setTransferTimeout(15000);

    m_hoselinePollReply = m_hoselineNam->get(req);
    QNetworkReply *reply = m_hoselinePollReply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_hoselinePollReply == reply) {
            m_hoselinePollReply = nullptr;
        }
        reply->deleteLater();
        if (!m_hoselineActive) {
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            m_hoselineConnected = false;
            emit hoselineUpdated();
            m_hoselineReconnectTimer->start(hoselineRetryDelayMs(3000));
            return;
        }
        const QString body = QString::fromUtf8(reply->readAll());
        const QStringList packets = body.split(QChar(0x1E), Qt::SkipEmptyParts);
        for (const QString &pkt : packets) {
            handleHoselinePacket(pkt);
        }
        if (!m_hoselineSid.isEmpty()) {
            m_hoselineFailCount = 0;
            startNextHoselinePoll();
        } else {
            m_hoselineReconnectTimer->start(hoselineRetryDelayMs(3000));
        }
    });
}

void DroidStar::startNextHoselinePoll()
{
    if (!m_hoselineActive || m_hoselineSid.isEmpty() || m_hoselinePollReply) {
        return;
    }

    QUrl url(QStringLiteral("https://api.brandmeister.network/lh/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("version"), QStringLiteral("2"));
    query.addQueryItem(QStringLiteral("EIO"), QStringLiteral("4"));
    query.addQueryItem(QStringLiteral("transport"), QStringLiteral("polling"));
    query.addQueryItem(QStringLiteral("sid"), m_hoselineSid);
    query.addQueryItem(QStringLiteral("t"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DroidStar/2026"));
    req.setTransferTimeout(32000);

    m_hoselinePollReply = m_hoselineNam->get(req);
    QNetworkReply *reply = m_hoselinePollReply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_hoselinePollReply == reply) {
            m_hoselinePollReply = nullptr;
        }
        reply->deleteLater();
        if (!m_hoselineActive) {
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            m_hoselineConnected = false;
            emit hoselineUpdated();
            m_hoselineReconnectTimer->start(hoselineRetryDelayMs(2500));
            return;
        }
        m_hoselineFailCount = 0;
        const QString body = QString::fromUtf8(reply->readAll());
        const QStringList packets = body.split(QChar(0x1E), Qt::SkipEmptyParts);
        for (const QString &pkt : packets) {
            handleHoselinePacket(pkt);
        }
        if (m_hoselineActive && !m_hoselineSid.isEmpty()) {
            startNextHoselinePoll();
        }
    });
}

void DroidStar::queueHoselinePost(const QString &payload)
{
    if (!m_hoselineActive || m_hoselineSid.isEmpty()) {
        return;
    }
    m_hoselinePostQueue.append(payload);
    flushHoselinePostQueue();
}

void DroidStar::flushHoselinePostQueue()
{
    if (!m_hoselineActive || m_hoselineSid.isEmpty() || m_hoselinePostReply || m_hoselinePostQueue.isEmpty()) {
        return;
    }

    const QString batch = m_hoselinePostQueue.join(QChar(0x1E));
    m_hoselinePostQueue.clear();

    QUrl url(QStringLiteral("https://api.brandmeister.network/lh/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("version"), QStringLiteral("2"));
    query.addQueryItem(QStringLiteral("EIO"), QStringLiteral("4"));
    query.addQueryItem(QStringLiteral("transport"), QStringLiteral("polling"));
    query.addQueryItem(QStringLiteral("sid"), m_hoselineSid);
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/plain;charset=UTF-8"));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DroidStar/2026"));
    req.setTransferTimeout(12000);

    m_hoselinePostReply = m_hoselineNam->post(req, batch.toUtf8());
    QNetworkReply *reply = m_hoselinePostReply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_hoselinePostReply == reply) {
            m_hoselinePostReply = nullptr;
        }
        reply->deleteLater();
        if (!m_hoselineActive) {
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            m_hoselineConnected = false;
            emit hoselineUpdated();
            m_hoselineReconnectTimer->start(hoselineRetryDelayMs(2500));
            return;
        }
        flushHoselinePostQueue();
    });
}

void DroidStar::handleHoselinePacket(const QString &packet)
{
    if (packet.isEmpty()) {
        return;
    }
    const QChar type = packet.at(0);
    if (type == QLatin1Char('0')) {
        // Engine.IO open packet: 0{"sid":"..."}
        const QJsonDocument doc = QJsonDocument::fromJson(packet.mid(1).toUtf8());
        if (doc.isObject()) {
            m_hoselineSid = doc.object().value(QStringLiteral("sid")).toString();
            if (!m_hoselineSid.isEmpty()) {
                // Connect Socket.IO default namespace
                queueHoselinePost(QStringLiteral("40"));
            }
        }
    } else if (type == QLatin1Char('2')) {
        // Engine.IO ping -> respond with pong "3"
        queueHoselinePost(QStringLiteral("3"));
    } else if (type == QLatin1Char('1')) {
        // Engine.IO close
        m_hoselineSid.clear();
        m_hoselineConnected = false;
        emit hoselineUpdated();
        m_hoselineReconnectTimer->start(2000);
    } else if (packet.startsWith(QStringLiteral("40"))) {
        // Socket.IO connect ACK
        m_hoselineConnected = true;
        if (!m_hoselineJoined) {
            m_hoselineJoined = true;
            queueHoselinePost(QStringLiteral("42[\"join\",\"everything\"]"));
            queueHoselinePost(QStringLiteral("42[\"sendCurrentSessions\"]"));
        }
        m_hoselineDirty = true;
        emit hoselineUpdated();
    } else if (packet.startsWith(QStringLiteral("41"))) {
        // Socket.IO disconnect
        m_hoselineSid.clear();
        m_hoselineConnected = false;
        emit hoselineUpdated();
        m_hoselineReconnectTimer->start(2000);
    } else if (packet.startsWith(QStringLiteral("42"))) {
        // Socket.IO event: 42["mqtt",{"topic":"LH","payload":"{...}"}]
        const QJsonDocument doc = QJsonDocument::fromJson(packet.mid(2).toUtf8());
        if (!doc.isArray()) {
            return;
        }
        const QJsonArray arr = doc.array();
        if (arr.size() >= 2 && arr.at(0).toString() == QStringLiteral("mqtt") && arr.at(1).isObject()) {
            const QJsonObject evt = arr.at(1).toObject();
            const QString topic = evt.value(QStringLiteral("topic")).toString();
            const QString payloadStr = evt.value(QStringLiteral("payload")).toString();
            handleHoselineMqttPayload(topic, payloadStr);
        }
    }
}

void DroidStar::handleHoselineMqttPayload(const QString &topic, const QString &payloadStr)
{
    if (topic != QStringLiteral("LH") && topic != QStringLiteral("LH-Startup")) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(payloadStr.toUtf8());
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject n = doc.object();
    const int sessionType = n.value(QStringLiteral("SessionType")).toInt();
    const QString eventType = n.value(QStringLiteral("Event")).toString();
    const int dstId = n.value(QStringLiteral("DestinationID")).toInt();
    const int flagSet = n.value(QStringLiteral("FlagSet")).toInt();
    const qint64 startSec = static_cast<qint64>(n.value(QStringLiteral("Start")).toDouble());
    const qint64 stopSec = static_cast<qint64>(n.value(QStringLiteral("Stop")).toDouble());
    const qint64 nowSec = QDateTime::currentSecsSinceEpoch();

    // Match BrandMeister Hoseline filtering: Group Voice Call (SessionType == 7), TG
    if (sessionType != 7 || dstId <= 90 || dstId > 999999 || dstId == 4000) {
        return;
    }
    if ((flagSet & 1) != 0 || (flagSet & 2) != 0) {
        return;
    }
    if (eventType != QStringLiteral("Session-Start") &&
        eventType != QStringLiteral("Session-Update") &&
        eventType != QStringLiteral("Session-Stop")) {
        return;
    }
    if (stopSec > 0 && (nowSec - stopSec) > 180) {
        return;
    }

    const QString sessionId = n.value(QStringLiteral("SessionID")).toString();
    auto existingIt = m_hoselineCalls.find(dstId);
    if (existingIt != m_hoselineCalls.end()) {
        // Don't overwrite a currently active session on this TG with an
        if (existingIt->stopSec == 0 && stopSec > 0 && existingIt->sessionId != sessionId &&
            (nowSec - existingIt->startSec) < 120) {
            return;
        }
    }

    HoselineCall call;
    call.tgid = dstId;
    call.sessionId = sessionId;
    call.sourceId = static_cast<uint32_t>(n.value(QStringLiteral("SourceID")).toInt());
    call.sourceCall = n.value(QStringLiteral("SourceCall")).toString().trimmed();
    if (call.sourceCall.isEmpty() && call.sourceId > 0) {
        call.sourceCall = m_dmrids.value(call.sourceId, QString::number(call.sourceId));
    }
    call.sourceName = n.value(QStringLiteral("SourceName")).toString().trimmed();
    call.talkerAlias = n.value(QStringLiteral("TalkerAlias")).toString().trimmed();
    call.master = n.value(QStringLiteral("Master")).toString().trimmed();
    call.linkCall = n.value(QStringLiteral("LinkCall")).toString().trimmed();
    call.startSec = (startSec > 0) ? startSec : nowSec;
    call.stopSec = (eventType == QStringLiteral("Session-Stop") && stopSec == 0) ? nowSec : stopSec;
    call.lastUpdatedMs = QDateTime::currentMSecsSinceEpoch();

    QString dstName = n.value(QStringLiteral("DestinationName")).toString().trimmed();
    if (!dstName.isEmpty() && !m_tgNames.contains(dstId)) {
        m_tgNames.insert(dstId, dstName);
    }
    call.tgName = m_tgNames.value(dstId, dstName);

    m_hoselineCalls.insert(dstId, call);
    m_hoselineDirty = true;
}

void DroidStar::onHoselineUiTick()
{
    if (!m_hoselineActive) {
        return;
    }
    // While the app is backgrounded (iOS keeps us alive for audio) nobody can see
    const Qt::ApplicationState appStateNow = QGuiApplication::applicationState();
    if (appStateNow == Qt::ApplicationSuspended || appStateNow == Qt::ApplicationHidden) {
        if (++m_hoselineBgTicks < 5) {
            return;
        }
    }
    m_hoselineBgTicks = 0;
    const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
    bool removedAny = false;
    bool hasLive = false;

    for (auto it = m_hoselineCalls.begin(); it != m_hoselineCalls.end(); ) {
        if (it->stopSec > 0 && (nowSec - it->stopSec) > 180) {
            it = m_hoselineCalls.erase(it);
            removedAny = true;
        } else if (it->stopSec == 0 && (nowSec - it->startSec) > 210) {
            // Auto-expire stuck sessions without a stop packet after 3.5 minutes
            it->stopSec = nowSec;
            ++it;
            removedAny = true;
        } else {
            if (it->stopSec == 0) {
                hasLive = true;
            }
            ++it;
        }
    }

    if (m_hoselineDirty || removedAny || hasLive) {
        m_hoselineDirty = false;
        emit hoselineUpdated();
    }
}

QVariantList DroidStar::getHoselineCalls() const
{
    const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
    const QStringList favList = loadFavouriteTGIDs();
    QSet<int> favSet;
    for (const QString &f : favList) {
        const int id = f.toInt();
        if (id > 0) favSet.insert(id);
    }

    QList<HoselineCall> list = m_hoselineCalls.values();
    std::sort(list.begin(), list.end(), [&](const HoselineCall &a, const HoselineCall &b) {
        const bool aLive = (a.stopSec == 0 && (nowSec - a.startSec) < 210);
        const bool bLive = (b.stopSec == 0 && (nowSec - b.startSec) < 210);
        if (aLive != bLive) return aLive > bLive;
        const bool aFav = favSet.contains(a.tgid);
        const bool bFav = favSet.contains(b.tgid);
        if (aFav != bFav) return aFav > bFav;
        if (aLive && bLive) return a.tgid < b.tgid;
        const qint64 aTime = a.stopSec > 0 ? a.stopSec : a.startSec;
        const qint64 bTime = b.stopSec > 0 ? b.stopSec : b.startSec;
        return aTime > bTime;
    });

    QVariantList out;
    out.reserve(list.size());
    for (const HoselineCall &c : list) {
        const bool isLive = (c.stopSec == 0 && (nowSec - c.startSec) < 210);
        const qint64 dur = isLive ? std::max<qint64>(0, nowSec - c.startSec)
                                  : std::max<qint64>(0, c.stopSec - c.startSec);
        const qint64 ago = (!isLive && c.stopSec > 0) ? std::max<qint64>(0, nowSec - c.stopSec) : 0;

        QVariantMap m;
        m["tgid"] = QString::number(c.tgid);
        m["tgidNum"] = c.tgid;
        m["tgName"] = !c.tgName.isEmpty() ? c.tgName : m_tgNames.value(c.tgid);
        m["sourceId"] = static_cast<int>(c.sourceId);
        m["sourceCall"] = c.sourceCall;
        m["sourceName"] = c.sourceName;
        m["talkerAlias"] = c.talkerAlias;
        m["master"] = c.master;
        m["linkCall"] = c.linkCall;
        m["isLive"] = isLive;
        m["isFavourite"] = favSet.contains(c.tgid);
        m["durationSec"] = static_cast<int>(dur);
        m["agoSec"] = static_cast<int>(ago);
        out.append(m);
    }
    return out;
}

QVariantList DroidStar::getActiveFavouriteCalls() const
{
    const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
    const QStringList favList = loadFavouriteTGIDs();

    struct FavItem {
        int tgid { 0 };
        QString tgidStr;
        QString tgName;
        bool isLive { false };
        bool hasRecent { false };
        QString sourceCall;
        QString sourceName;
        QString talkerAlias;
        QString master;
        int durationSec { 0 };
        int agoSec { 0 };
        int originalOrder { 0 };
    };

    QVector<FavItem> items;
    items.reserve(favList.size());
    for (int i = 0; i < favList.size(); ++i) {
        const QString &f = favList.at(i);
        const int id = f.toInt();
        if (id <= 0) continue;

        FavItem item;
        item.tgid = id;
        item.tgidStr = QString::number(id);
        item.tgName = m_tgNames.value(id);
        item.originalOrder = i;

        auto it = m_hoselineCalls.constFind(id);
        if (it != m_hoselineCalls.constEnd()) {
            item.isLive = (it->stopSec == 0 && (nowSec - it->startSec) < 210);
            item.hasRecent = !item.isLive;
            item.sourceCall = it->sourceCall;
            item.sourceName = it->sourceName;
            item.talkerAlias = it->talkerAlias;
            item.master = it->master;
            if (item.tgName.isEmpty()) item.tgName = it->tgName;
            item.durationSec = static_cast<int>(item.isLive
                ? std::max<qint64>(0, nowSec - it->startSec)
                : std::max<qint64>(0, it->stopSec - it->startSec));
            item.agoSec = static_cast<int>((!item.isLive && it->stopSec > 0)
                ? std::max<qint64>(0, nowSec - it->stopSec) : 0);
        }
        items.push_back(item);
    }

    std::sort(items.begin(), items.end(), [](const FavItem &a, const FavItem &b) {
        if (a.isLive != b.isLive) return a.isLive > b.isLive;
        if (a.hasRecent != b.hasRecent) return a.hasRecent > b.hasRecent;
        return a.originalOrder < b.originalOrder;
    });

    QVariantList out;
    out.reserve(items.size());
    for (const FavItem &item : items) {
        QVariantMap m;
        m["tgid"] = item.tgidStr;
        m["tgidNum"] = item.tgid;
        m["tgName"] = item.tgName;
        m["isLive"] = item.isLive;
        m["hasRecent"] = item.hasRecent;
        m["sourceCall"] = item.sourceCall;
        m["sourceName"] = item.sourceName;
        m["talkerAlias"] = item.talkerAlias;
        m["master"] = item.master;
        m["durationSec"] = item.durationSec;
        m["agoSec"] = item.agoSec;
        m["isFavourite"] = true;
        out.append(m);
    }
    return out;
}

QVariantMap DroidStar::getHoselineStatus() const
{
    const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
    const QStringList favList = loadFavouriteTGIDs();
    QSet<int> favSet;
    for (const QString &f : favList) {
        const int id = f.toInt();
        if (id > 0) favSet.insert(id);
    }

    int liveCount = 0;
    int activeFavCount = 0;
    for (auto it = m_hoselineCalls.constBegin(); it != m_hoselineCalls.constEnd(); ++it) {
        if (it->stopSec == 0 && (nowSec - it->startSec) < 210) {
            ++liveCount;
            if (favSet.contains(it->tgid)) {
                ++activeFavCount;
            }
        }
    }

    QVariantMap m;
    m["active"] = m_hoselineActive;
    m["connected"] = m_hoselineConnected;
    m["liveCount"] = liveCount;
    m["totalCount"] = m_hoselineCalls.size();
    m["activeFavCount"] = activeFavCount;
    m["favCount"] = favSet.size();
    return m;
}

void DroidStar::on_network_state_changed(QNetworkInformation::Reachability reachability) {
    if (reachability == QNetworkInformation::Reachability::Online) {
        qDebug() << "Network is online. Checking connection status...";
        if (connect_status == Mode::DISCONNECTED) {
            process_connect();
        }
    } else {
        qDebug() << "Network is offline. Stopping keep-alive messages.";
        m_keepAliveTimer->stop();
    }
}


// Function to handle entering the background
void DroidStar::handle_background_state() {
    qDebug() << "App has entered background.";
    if (connect_status == Mode::CONNECTED_RW) {
        m_keepAliveTimer->stop();
#ifdef Q_OS_IOS
        // Critical: set up audio session for background playback and start a background task
        // so iOS doesn't suspend us while audio is playing.
        setupBackgroundAudio();
        renewBackgroundTask();
#endif
    }
}


/*void DroidStar::setPlaybackDevice(const QString &deviceName) {
    if (m_audioEngine) {
        m_audioEngine->setOutputDevice(deviceName);
    } else {
        qDebug() << "AudioEngine not initialized!";
    }
}

void DroidStar::setCaptureDevice(const QString &deviceName) {
    if (m_audioEngine) {
        m_audioEngine->setInputDevice(deviceName);
    } else {
        qDebug() << "AudioEngine not initialized!";
    }
}*/

QStringList DroidStar::get_playbacks() {
    QStringList friendlyPlaybacks;
    friendlyPlaybacks << "OS Default";
    const QStringList playbacks = m_audioEngine->discover_audio_devices(1);
    for (const QString &device : playbacks) {
        const QString name = m_audioEngine->getFriendlyName(device);
        if (!name.isEmpty() && !friendlyPlaybacks.contains(name)) {
            friendlyPlaybacks << name;
        }
    }
    return friendlyPlaybacks;
}

QStringList DroidStar::get_captures() {
    QStringList friendlyCaptures;
    friendlyCaptures << "OS Default";
    const QStringList captures = m_audioEngine->discover_audio_devices(0);
    for (const QString &device : captures) {
        const QString name = m_audioEngine->getFriendlyName(device);
        if (!name.isEmpty() && !friendlyCaptures.contains(name)) {
            friendlyCaptures << name;
        }
    }
    return friendlyCaptures;
}

void DroidStar::setPlaybackDevice(const QString &deviceName) {
    QString internalDeviceName = m_audioEngine->mapFriendlyNameToDevice(deviceName);
    m_playback = internalDeviceName;
    m_audioEngine->setOutputDevice(internalDeviceName);
}

void DroidStar::setCaptureDevice(const QString &deviceName) {
    QString internalDeviceName = m_audioEngine->mapFriendlyNameToDevice(deviceName);
    m_capture = internalDeviceName;
    m_audioEngine->setInputDevice(internalDeviceName);
}


// Function to handle returning to the foreground
void DroidStar::handle_foreground_state() {
    qDebug() << "App has returned to foreground.";
    if (connect_status == Mode::CONNECTED_RW) {
        m_keepAliveTimer->start();
    }
}

void DroidStar::setup_state_change_listeners() {
#ifdef Q_OS_IOS
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [=](Qt::ApplicationState state){
        if (state == Qt::ApplicationInactive || state == Qt::ApplicationSuspended) {
            handle_background_state();
        } else if (state == Qt::ApplicationActive) {
            handle_foreground_state();
        }
    });
#endif
}

QString DroidStar::get_software_build()
{
#ifdef Q_OS_IOS
    char shortVer[64] = {0};
    char buildVer[64] = {0};
    getIOSBundleVersion(shortVer, sizeof(shortVer), buildVer, sizeof(buildVer));
    const QString sVer = QString::fromUtf8(shortVer).trimmed();
    const QString bVer = QString::fromUtf8(buildVer).trimmed();
    if (!bVer.isEmpty()) {
        if (sVer.isEmpty() || bVer == sVer || bVer.startsWith(sVer + ".")) {
            return bVer;
        }
        return sVer + "." + bVer;
    }
    if (!sVer.isEmpty()) {
        return sVer;
    }
#endif
    const QString appVer = QCoreApplication::applicationVersion().trimmed();
    if (!appVer.isEmpty()) {
        return appVer;
    }
#ifdef VERSION_NUMBER
    return QString(VERSION_NUMBER).trimmed();
#else
    return QString();
#endif
}

QVariantMap DroidStar::get_safe_area()
{
    double top = 0, left = 0, bottom = 0, right = 0;
    bool valid = false;
#ifdef Q_OS_IOS
    valid = getSafeAreaInsets(&top, &left, &bottom, &right);
#endif
    QVariantMap m;
    m["valid"] = valid;
    m["top"] = top;
    m["left"] = left;
    m["bottom"] = bottom;
    m["right"] = right;
    return m;
}
