/*
 * Nebula AI - AI assistant plugin for Stellarium (fork "Nebula")
 * License: GPL-2.0-or-later, same as Stellarium.
 *
 * Needs an API key in the environment: export ANTHROPIC_API_KEY=...
 * Optional: NEBULA_MODEL, NEBULA_API_URL
 */
#include "NebulaAI.hpp"

#include "StelApp.hpp"
#include "StelCore.hpp"
#include "StelModuleMgr.hpp"
#include "StelObjectMgr.hpp"
#include "StelObject.hpp"
#include "StelMovementMgr.hpp"
#include "StelUtils.hpp"
#include "StelMainView.hpp"

#include <QtWidgets>
#include <QtNetwork>
#include <QtMultimedia>
#include <QTextToSpeech>
#include <QTemporaryFile>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>

static QString cfg(const char* env, const char* key, const QString& def = QString())
{
	const QString e = qEnvironmentVariable(env);
	if (!e.isEmpty()) return e;
	const QString v = QSettings("Nebula", "NebulaAI").value(key).toString();
	return v.isEmpty() ? def : v;
}

// ---------------------------------------------------------------- plugin glue
StelModule* NebulaAIStelPluginInterface::getStelModule() const
{
	return new NebulaAI();
}

StelPluginInfo NebulaAIStelPluginInterface::getPluginInfo() const
{
	StelPluginInfo info;
	info.id = "NebulaAI";
	info.displayedName = "Nebula AI";
	info.authors = "Nebula";
	info.contact = "-";
	info.description = "AI assistant: ask the sky, guided tours, object explanations, photo identification.";
	info.version = "0.1.0";
	info.license = "GNU GPLv2 or later";
	return info;
}

// ------------------------------------------------------------------ chat window
NebulaChatWindow::NebulaChatWindow(NebulaAI* m)
	: QWidget(&StelMainView::getInstance(), Qt::Dialog)
{
	setWindowTitle("Nebula AI");
	resize(400, 480);
	auto* lay = new QVBoxLayout(this);
	log = new QTextEdit;
	log->setReadOnly(true);
	lay->addWidget(log);
	input = new QLineEdit;
	input->setPlaceholderText("Ask the sky, e.g. show me Saturn tonight");
	auto* send = new QPushButton("Send");
	auto* tour = new QPushButton("Tour");
	auto* photo = new QPushButton("Photo");
    auto* voiceBtn = new QPushButton("Voice");
	auto* row = new QHBoxLayout;
	row->addWidget(input, 1);
	row->addWidget(send);
	row->addWidget(tour);
	row->addWidget(voiceBtn);
    row->addWidget(photo);
	auto* cfgBtn = new QPushButton("Settings");
	row->addWidget(cfgBtn);
	connect(cfgBtn, &QPushButton::clicked, this, [this]() {
		QSettings st("Nebula", "NebulaAI");
		QDialog d(this);
		d.setWindowTitle("Nebula settings");
		auto* f = new QFormLayout(&d);
		auto* style = new QComboBox;
		style->addItems({"openai", "anthropic"});
		style->setCurrentText(st.value("style", "openai").toString());
		auto* url = new QLineEdit(st.value("url", "https://api.apikeys.in/v1/chat/completions").toString());
		auto* key = new QLineEdit(st.value("key").toString());
		key->setEchoMode(QLineEdit::Password);
		auto* model = new QLineEdit(st.value("model", "anthropic/claude-sonnet-5-5").toString());
		f->addRow("API style", style);
		f->addRow("URL", url);
		f->addRow("API key", key);
		f->addRow("Model", model);
        auto* getKey = new QPushButton("Get a Yantra AI key (apikeys.in)");
        f->addRow(getKey);
        connect(getKey, &QPushButton::clicked, &d, []() { QDesktopServices::openUrl(QUrl("https://apikeys.in")); });
		auto* speakBox = new QCheckBox("Speak replies aloud");
        speakBox->setChecked(st.value("speak", true).toBool());
        auto* ttsModel = new QLineEdit(st.value("tts_model", "tts-1-hd").toString());
        auto* ttsVoice = new QLineEdit(st.value("tts_voice", "nova").toString());
        auto* sttModel = new QLineEdit(st.value("stt_model", "whisper-1").toString());
        f->addRow(speakBox);
        f->addRow("Voice model", ttsModel);
        f->addRow("Voice", ttsVoice);
        f->addRow("Speech-to-text model", sttModel);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
		f->addRow(bb);
		connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
		connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
		if (d.exec() == QDialog::Accepted) {
			st.setValue("style", style->currentText());
			st.setValue("url", url->text().trimmed());
			st.setValue("key", key->text().trimmed());
			st.setValue("model", model->text().trimmed());
            st.setValue("speak", speakBox->isChecked());
            st.setValue("tts_model", ttsModel->text().trimmed());
            st.setValue("tts_voice", ttsVoice->text().trimmed());
            st.setValue("stt_model", sttModel->text().trimmed());
			append("Nebula", "Settings saved.");
		}
	});
	lay->addLayout(row);

	auto submit = [this, m]() {
		const QString t = input->text().trimmed();
		if (t.isEmpty()) return;
		input->clear();
		append("You", t);
		m->ask(t);
	};
	connect(send, &QPushButton::clicked, this, submit);
	connect(input, &QLineEdit::returnPressed, this, submit);
	connect(tour, &QPushButton::clicked, m, &NebulaAI::startTour);
    connect(voiceBtn, &QPushButton::clicked, m, &NebulaAI::toggleVoice);
	connect(photo, &QPushButton::clicked, this, [this, m]() {
		const QString f = QFileDialog::getOpenFileName(this, "Choose a sky photo", QString(),
		                                               "Images (*.jpg *.jpeg *.png *.webp *.gif)");
		if (!f.isEmpty()) {
			append("You", "[photo] " + QFileInfo(f).fileName());
			m->identifyPhoto(f);
		}
	});
}

void NebulaChatWindow::append(const QString& who, const QString& text)
{
	log->append("<b>" + who.toHtmlEscaped() + ":</b> " + text.toHtmlEscaped().replace("\n", "<br>"));
}

// ----------------------------------------------------------------------- module
NebulaAI::NebulaAI() {}
NebulaAI::~NebulaAI() {}

double NebulaAI::getCallOrder(StelModuleActionName actionName) const
{
	Q_UNUSED(actionName)
	return 0;
}

void NebulaAI::init()
{
	nam = new QNetworkAccessManager(this);
	win = new NebulaChatWindow(this); qDebug() << "NebulaAI: init done";
	connect(this, &NebulaAI::reply, win, [this](const QString& t) { win->append("Nebula", t); });
	if (cfg("NEBULA_API_KEY", "key").isEmpty() && cfg("ANTHROPIC_API_KEY", "key").isEmpty())
        emit reply("Welcome to Nebula! Click Settings and paste your Yantra AI key (get one at apikeys.in) to start.");
    voice = new NebulaVoice(this);
    connect(voice, &NebulaVoice::transcribed, this, [this](const QString& t) {
        emit reply("You said: " + t);
        ask(t);
    });
    connect(voice, &NebulaVoice::status, this, [this](const QString& s) { emit reply(s); });
    tourTimer.setSingleShot(true);
	connect(&tourTimer, &QTimer::timeout, this, &NebulaAI::playNext);
	addAction("actionShow_NebulaAI", "Nebula AI", "Show Nebula AI chat", this, "toggleWindow()", "Ctrl+Shift+N");
	QTimer::singleShot(2500, this, &NebulaAI::toggleWindow);
}

void NebulaAI::deinit()
{
	tourTimer.stop();
	delete win;
	win = nullptr;
}

void NebulaAI::toggleWindow()
{
	if (win) { win->setVisible(!win->isVisible()); win->raise(); win->activateWindow(); qDebug() << "NebulaAI: window toggled"; }
}

QString NebulaAI::context() const
{
	StelCore* core = StelApp::getInstance().getCore();
	QString c = "Sky time (UTC): " + StelUtils::julianDayToISO8601String(core->getJD()) +
	            ". Observer location: " + core->getCurrentLocation().name + ".";
	StelObjectMgr* om = GETSTELMODULE(StelObjectMgr);
	const QList<StelObjectP> sel = om->getSelectedObject();
	if (!sel.isEmpty())
		c += " Selected object: " + sel[0]->getEnglishName() + ".";
	return c;
}

void NebulaAI::post(const QJsonArray& messages, const QString& system,
                    std::function<void(const QString&)> done)
{
	const bool oa = cfg("NEBULA_API_STYLE", "style", "openai") == "openai";
	const QByteArray key = cfg(oa ? "NEBULA_API_KEY" : "ANTHROPIC_API_KEY", "key").toUtf8();
	if (key.isEmpty()) {
		done(oa ? "No API key yet. Click Settings and paste your Yantra AI key (get one at apikeys.in)."
		        : "No API key found. Run: export ANTHROPIC_API_KEY=... then restart Nebula from that terminal.");
		return;
	}
	const QString url = cfg("NEBULA_API_URL", "url",
	    oa ? QString("https://api.apikeys.in/v1/chat/completions") : QString("https://api.anthropic.com/v1/messages"));
	if (url.isEmpty()) {
		done("Set NEBULA_API_URL to your gateway's chat completions URL.");
		return;
	}
	QNetworkRequest req{QUrl(url)};
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	const QString model = cfg("NEBULA_MODEL", "model", oa ? "anthropic/claude-sonnet-5-5" : "claude-sonnet-4-6");
	QJsonObject body;
	if (oa) {
		req.setRawHeader("Authorization", "Bearer " + key);
		QJsonArray ms{QJsonObject{{"role", "system"}, {"content", system}}};
		for (const QJsonValue& v : messages) {
			QJsonObject m = v.toObject();
			if (m["content"].isArray()) {
				QJsonArray parts;
				for (const QJsonValue& p : m["content"].toArray()) {
					const QJsonObject blk = p.toObject();
					if (blk["type"].toString() == "image") {
						const QJsonObject src = blk["source"].toObject();
						parts.append(QJsonObject{{"type", "image_url"},
						    {"image_url", QJsonObject{{"url", "data:" + src["media_type"].toString()
						        + ";base64," + src["data"].toString()}}}});
					} else {
						parts.append(blk);
					}
				}
				m["content"] = parts;
			}
			ms.append(m);
		}
		body = QJsonObject{{"model", model}, {"max_tokens", 1200}, {"messages", ms}};
	} else {
		req.setRawHeader("x-api-key", key);
		req.setRawHeader("anthropic-version", "2023-06-01");
		body = QJsonObject{{"model", model}, {"max_tokens", 1200}, {"system", system}, {"messages", messages}};
	}
	QNetworkReply* r = nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
	connect(r, &QNetworkReply::finished, this, [r, done, oa]() {
		const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
		r->deleteLater();
		if (o.contains("error")) {
			const QJsonValue e = o["error"];
			done("API error: " + (e.isObject() ? e.toObject()["message"].toString() : e.toString()));
			return;
		}
		if (oa) {
			const QJsonArray c = o["choices"].toArray();
			done(c.isEmpty() ? QString("No answer received.")
			                 : c[0].toObject()["message"].toObject()["content"].toString());
			return;
		}
		const QJsonArray c = o["content"].toArray();
		done(c.isEmpty() ? QString("No answer received.") : c[0].toObject()["text"].toString());
	});
}

static QJsonObject extractJson(const QString& s)
{
	const int a = s.indexOf('{'), b = s.lastIndexOf('}');
	if (a < 0 || b <= a) return QJsonObject();
	return QJsonDocument::fromJson(s.mid(a, b - a + 1).toUtf8()).object();
}

bool NebulaAI::gotoObject(const QString& name)
{
	StelObjectMgr* om = GETSTELMODULE(StelObjectMgr);
	StelMovementMgr* mv = GETSTELMODULE(StelMovementMgr);
	if (!om->findAndSelectI18n(name) && !om->findAndSelect(name)) return false;
	const QList<StelObjectP> sel = om->getSelectedObject();
	if (sel.isEmpty()) return false;
	mv->moveToObject(sel[0], 2.f);
	mv->setFlagTracking(true);
	return true;
}

void NebulaAI::runActions(const QJsonArray& actions)
{
	StelCore* core = StelApp::getInstance().getCore();
	StelMovementMgr* mv = GETSTELMODULE(StelMovementMgr);
	for (const QJsonValue& v : actions) {
		const QJsonObject a = v.toObject();
		const QString t = a["type"].toString();
		if (t == "goto") {
			if (!gotoObject(a["name"].toString()))
				emit reply("Couldn't find \"" + a["name"].toString() + "\" in the sky catalog.");
		} else if (t == "time") {
			const QString iso = a["iso"].toString();
			if (iso == "now") {
				core->setJD(StelUtils::getJDFromSystem());
			} else {
				const QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
				if (dt.isValid()) core->setJD(StelUtils::qDateTimeToJd(dt));
			}
		} else if (t == "zoom") {
			mv->zoomTo(qBound(0.5, a["fov"].toDouble(60.0), 180.0), 1.5f);
		}
	}
}

void NebulaAI::ask(const QString& text)
{
	const QString system =
	    "You are Nebula, the assistant inside a planetarium app. " + context() +
	    " Reply ONLY with JSON: {\"say\": string (under 70 words), \"actions\": array}. Actions: "
	    "{\"type\":\"goto\",\"name\":\"<object name, English>\"}, "
	    "{\"type\":\"time\",\"iso\":\"<ISO 8601 UTC like 2026-12-01T20:00:00Z, or now>\"}, "
	    "{\"type\":\"zoom\",\"fov\":<degrees 1-180>}. Use an empty actions array when no view change is needed.";
	history.append(QJsonObject{{"role", "user"}, {"content", text}});
	while (history.size() > 10) history.removeFirst();
	post(history, system, [this](const QString& out) {
		const QJsonObject o = extractJson(out);
		const QString say = o.contains("say") ? o["say"].toString() : out;
		history.append(QJsonObject{{"role", "assistant"}, {"content", out}});
		emit reply(say);
        speak(say);
		runActions(o["actions"].toArray());
	});
}

void NebulaAI::identifyPhoto(const QString& path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		emit reply("Couldn't read that file.");
		return;
	}
	const QString ext = QFileInfo(path).suffix().toLower();
	const QString mime = ext == "png" ? "image/png" : ext == "webp" ? "image/webp"
	                     : ext == "gif" ? "image/gif" : "image/jpeg";
	QJsonObject img{{"type", "image"},
	                {"source", QJsonObject{{"type", "base64"}, {"media_type", mime},
	                                       {"data", QString::fromLatin1(f.readAll().toBase64())}}}};
	QJsonObject txt{{"type", "text"},
	                {"text", "Identify the astronomical objects, constellations or phenomena in this photo. "
	                         "Be concise and say if you are unsure."}};
	QJsonArray msgs{QJsonObject{{"role", "user"}, {"content", QJsonArray{img, txt}}}};
	post(msgs, "You are an expert astronomer helping identify what appears in a sky photo.",
	     [this](const QString& out) { emit reply(out); });
}

void NebulaAI::toggleVoice()
{
    if (!voice) return;
    if (voice->recording())
        voice->stopRecording();
    else
        voice->startRecording();
}

void NebulaAI::speak(const QString& text)
{
    if (voice && QSettings("Nebula", "NebulaAI").value("speak", true).toBool())
        voice->speak(text);
}

void NebulaAI::startTour()
{
	emit reply("Planning a tour...");
	const QString system =
	    "You plan planetarium tours. " + context() +
	    " Reply ONLY with JSON: {\"stops\":[{\"name\":\"<object name, English>\",\"narration\":\"<2 vivid sentences>\"}]} "
	    "with 5 stops of naked-eye objects that suit this sky time and location.";
	QJsonArray msgs{QJsonObject{{"role", "user"}, {"content", "Plan the tour."}}};
	post(msgs, system, [this](const QString& out) {
		tourStops = extractJson(out)["stops"].toArray();
		tourIdx = 0;
		if (tourStops.isEmpty()) emit reply("Couldn't plan a tour: " + out);
		else playNext();
	});
}

void NebulaAI::playNext()
{
	if (tourIdx >= tourStops.size()) {
		emit reply("Tour finished.");
		return;
	}
	const QJsonObject s = tourStops[tourIdx++].toObject();
	gotoObject(s["name"].toString());
	emit reply(s["name"].toString() + ": " + s["narration"].toString());
    speak(s["narration"].toString());
	tourTimer.start(10000);
}

// ---------------------------------------------------------------- voice
static QString voiceBase()
{
    const QString u = cfg("NEBULA_API_URL", "url", "https://api.apikeys.in/v1/chat/completions");
    const int i = u.indexOf("/chat/completions");
    return i > 0 ? u.left(i) : u;
}

NebulaVoice::NebulaVoice(QObject* parent) : QObject(parent)
{
    net = new QNetworkAccessManager(this);
}

void NebulaVoice::startRecording()
{
#ifdef Q_OS_MACOS
    emit status("Voice input is not available on macOS yet.");
    return;
#endif
    if (rec) return;
    if (player) player->stop();
    if (tts) tts->stop();
    const QAudioDevice dev = QMediaDevices::defaultAudioInput();
    if (dev.isNull()) {
        emit status("No microphone found.");
        return;
    }
    QAudioFormat fmt = dev.preferredFormat();
    fmt.setSampleFormat(QAudioFormat::Int16);
    if (!dev.isFormatSupported(fmt)) {
        emit status("This microphone format is not supported.");
        return;
    }
    rate = fmt.sampleRate();
    channels = fmt.channelCount();
    delete src;
    delete buf;
    buf = new QBuffer(this);
    buf->open(QIODevice::ReadWrite);
    src = new QAudioSource(dev, fmt, this);
    src->start(buf);
    rec = true;
    emit status("Listening... click Voice again when you are done.");
}

void NebulaVoice::stopRecording()
{
    if (!rec) return;
    rec = false;
    src->stop();
    const QByteArray pcm = buf->data();
    if (pcm.size() < rate * channels / 2) {
        emit status("That was too short, please try again.");
        return;
    }
    const QString key = cfg("NEBULA_API_KEY", "key");
    if (key.isEmpty()) {
        emit status("Add your API key in Settings first.");
        return;
    }
    auto le32 = [](quint32 v) {
        QByteArray b;
        for (int i = 0; i < 4; ++i) b.append(char((v >> (8 * i)) & 0xFF));
        return b;
    };
    auto le16 = [](quint16 v) {
        QByteArray b;
        b.append(char(v & 0xFF));
        b.append(char((v >> 8) & 0xFF));
        return b;
    };
    QByteArray wav;
    wav += "RIFF";
    wav += le32(36 + pcm.size());
    wav += "WAVEfmt ";
    wav += le32(16);
    wav += le16(1);
    wav += le16(channels);
    wav += le32(rate);
    wav += le32(rate * channels * 2);
    wav += le16(channels * 2);
    wav += le16(16);
    wav += "data";
    wav += le32(pcm.size());
    wav += pcm;

    emit status("Transcribing...");
    auto* mp = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart model;
    model.setHeader(QNetworkRequest::ContentDispositionHeader, "form-data; name=\"model\"");
    model.setBody(cfg("NEBULA_STT_MODEL", "stt_model", "whisper-1").toUtf8());
    QHttpPart file;
    file.setHeader(QNetworkRequest::ContentTypeHeader, "audio/wav");
    file.setHeader(QNetworkRequest::ContentDispositionHeader, "form-data; name=\"file\"; filename=\"speech.wav\"");
    file.setBody(wav);
    mp->append(model);
    mp->append(file);
    QNetworkRequest req{QUrl(voiceBase() + "/audio/transcriptions")};
    req.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    QNetworkReply* r = net->post(req, mp);
    mp->setParent(r);
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        const QByteArray body = r->readAll();
        const QString err = r->errorString();
        const bool failed = r->error() != QNetworkReply::NoError;
        r->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        if (o.contains("error")) {
            const QJsonValue e = o["error"];
            emit status("Speech-to-text error: " + (e.isObject() ? e.toObject()["message"].toString() : e.toString()));
            return;
        }
        if (failed && o.isEmpty()) {
            emit status("Speech-to-text failed: " + err);
            return;
        }
        const QString t = o["text"].toString().trimmed();
        if (t.isEmpty()) {
            emit status("I did not catch that, please try again.");
            return;
        }
        emit transcribed(t);
    });
}

void NebulaVoice::speak(const QString& text)
{
    if (text.trimmed().isEmpty()) return;
    auto fallback = [this, text]() {
        if (!tts) tts = new QTextToSpeech(this);
        tts->say(text);
    };
    const QString key = cfg("NEBULA_API_KEY", "key");
    if (key.isEmpty()) {
        fallback();
        return;
    }
    QNetworkRequest req{QUrl(voiceBase() + "/audio/speech")};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    const QJsonObject body{{"model", cfg("NEBULA_TTS_MODEL", "tts_model", "tts-1-hd")},
                           {"input", text.left(3500)},
                           {"voice", cfg("NEBULA_TTS_VOICE", "tts_voice", "nova")},
                           {"response_format", "mp3"}};
    QNetworkReply* r = net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [this, r, fallback]() {
        const QByteArray audio = r->readAll();
        const bool ok = r->error() == QNetworkReply::NoError && audio.size() > 1000;
        const QString err = r->errorString();
        r->deleteLater();
        if (!ok) {
            emit status("Cloud voice unavailable (" + err + "), using the system voice.");
            fallback();
            return;
        }
        delete clip;
        clip = new QTemporaryFile(QDir::tempPath() + "/nebula-voice-XXXXXX.mp3", this);
        if (!clip->open()) {
            fallback();
            return;
        }
        clip->write(audio);
        clip->flush();
        if (!player) {
            player = new QMediaPlayer(this);
            player->setAudioOutput(new QAudioOutput(this));
        }
        player->stop();
        player->setSource(QUrl::fromLocalFile(clip->fileName()));
        player->play();
    });
}
