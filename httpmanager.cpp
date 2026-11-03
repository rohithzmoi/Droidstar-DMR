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

#include "httpmanager.h"
#include <QSaveFile>
#include <zlib.h>

static QByteArray decompressGzipIfNeeded(const QByteArray &input)
{
	if (input.size() < 2 ||
	    static_cast<uint8_t>(input.at(0)) != 0x1fU ||
	    static_cast<uint8_t>(input.at(1)) != 0x8bU) {
		return input;
	}

	z_stream strm;
	memset(&strm, 0, sizeof(strm));
	strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.constData()));
	strm.avail_in = static_cast<uInt>(input.size());

	if (inflateInit2(&strm, 16 + MAX_WBITS) != Z_OK) {
		return QByteArray();
	}

	QByteArray out;
	out.reserve(input.size() * 3);
	char chunk[32768];
	int ret = Z_OK;

	do {
		strm.next_out = reinterpret_cast<Bytef *>(chunk);
		strm.avail_out = sizeof(chunk);
		ret = inflate(&strm, Z_NO_FLUSH);
		if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
			inflateEnd(&strm);
			return QByteArray();
		}
		const int have = static_cast<int>(sizeof(chunk)) - static_cast<int>(strm.avail_out);
		if (have > 0) {
			out.append(chunk, have);
		}
	} while (ret != Z_STREAM_END && strm.avail_in > 0);

	inflateEnd(&strm);
	return (ret == Z_STREAM_END) ? out : QByteArray();
}

static bool isValidHostOrIdPayload(const QByteArray &payload)
{
	if (payload.size() < 64) {
		return false;
	}
	const QByteArray head = payload.left(256).trimmed().toLower();
	if (head.startsWith("<!doctype") ||
	    head.startsWith("<html") ||
	    head.startsWith("<head") ||
	    head.startsWith("<body")) {
		return false;
	}
	return true;
}

HttpManager::HttpManager(QString f, bool u) : QObject(nullptr)
{
	m_qnam = new QNetworkAccessManager(this);
	QObject::connect(m_qnam, SIGNAL(finished(QNetworkReply*)), this, SLOT(http_finished(QNetworkReply*)));
	m_config_path = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
	m_url = u;
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_WIN)
	m_config_path += "/dudetronics";
#endif
	m_filename = f;
	m_urlIndex = 0;

	if (m_url) {
		m_candidateUrls.append(m_filename);
	} else {
		if (!m_filename.startsWith('/')) {
			m_filename.prepend('/');
		}
		// Tier 1: Primary VU3LVO Cloudflare Edge Mirror (latest)
		m_candidateUrls.append(QStringLiteral("https://ds.vu3lvo.com") + m_filename);
		// Tier 2: Legacy Dudetronics Mirror (fallback 1)
		m_candidateUrls.append(QStringLiteral("http://www.dudetronics.com/ar-dns") + m_filename);
		// Tier 3: VU3LVO Cloudflare Edge Mirror (previous known-good backup snapshot)
		m_candidateUrls.append(QStringLiteral("https://ds.vu3lvo.com/backup") + m_filename);
	}
}

void HttpManager::process()
{
	QMetaObject::invokeMethod(this, "doRequest");
}

void HttpManager::doRequest()
{
	if (m_urlIndex < 0 || m_urlIndex >= m_candidateUrls.size()) {
		QThread::currentThread()->quit();
		return;
	}

	QNetworkRequest req(QUrl(m_candidateUrls.at(m_urlIndex)));
	req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DroidStar/2026"));
	req.setTransferTimeout(15000);
	m_qnam->get(req);
}

void HttpManager::http_finished(QNetworkReply *reply)
{
	if (!reply) {
		QThread::currentThread()->quit();
		return;
	}

	const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	const bool hasNetError = (reply->error() != QNetworkReply::NoError) ||
	                         (statusCode != 0 && (statusCode < 200 || statusCode >= 300));

	QByteArray payload;
	if (!hasNetError) {
		payload = decompressGzipIfNeeded(reply->readAll());
	}

	const bool validPayload = !hasNetError && (m_url ? !payload.isEmpty() : isValidHostOrIdPayload(payload));

	if (!validPayload) {
		qDebug() << "HttpManager: source failed (" << m_candidateUrls.value(m_urlIndex)
		         << "status=" << statusCode << "err=" << reply->errorString()
		         << "bytes=" << payload.size() << ")";
		reply->deleteLater();
		++m_urlIndex;
		if (m_urlIndex < m_candidateUrls.size()) {
			qDebug() << "HttpManager: falling back to" << m_candidateUrls.at(m_urlIndex);
			doRequest();
			return;
		}
		QThread::currentThread()->quit();
		return;
	}

	if (m_url) {
		QStringList l = m_filename.split('/');
		m_filename = "/" + l.at(l.size() - 1);
	}
	QStringList l = m_filename.split('_');
	if ((l.at(0) != "/vocoder") && (l.size() > 1)) {
		reply->deleteLater();
		QThread::currentThread()->quit();
		return;
	}

	QDir().mkpath(m_config_path);
	const QString targetPath = m_config_path + m_filename;
	QSaveFile hosts_file(targetPath);
	if (hosts_file.open(QIODevice::WriteOnly)) {
		hosts_file.write(payload);
		if (hosts_file.commit()) {
			QFileInfo fileInfo(targetPath);
			emit file_downloaded(fileInfo.fileName());
		}
	}
	reply->deleteLater();
	QThread::currentThread()->quit();
}
