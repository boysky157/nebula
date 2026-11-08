/*
 * Nebula AI - AI assistant plugin for Stellarium (fork "Nebula")
 * License: GPL-2.0-or-later, same as Stellarium.
 */
#ifndef NEBULAAI_HPP
#define NEBULAAI_HPP

#include "StelModule.hpp"
#include "StelPluginInterface.hpp"
#include <QObject>
#include <QWidget>
#include <QJsonArray>
#include <QTimer>
#include <functional>

class QNetworkAccessManager;
class QTextEdit;
class QLineEdit;
class NebulaChatWindow;

class NebulaAI : public StelModule
{
	Q_OBJECT
public:
	NebulaAI();
	~NebulaAI() override;

	void init() override;
	void deinit() override;
	void draw(StelCore*) override {}
	double getCallOrder(StelModuleActionName actionName) const override;

public slots:
	void toggleWindow();
	void ask(const QString& text);
	void identifyPhoto(const QString& path);
	void startTour();

signals:
	void reply(const QString& text);

private:
	void post(const QJsonArray& messages, const QString& system,
	          std::function<void(const QString&)> done);
	QString context() const;
	void runActions(const QJsonArray& actions);
	bool gotoObject(const QString& name);
	void playNext();

	QNetworkAccessManager* nam = nullptr;
	NebulaChatWindow* win = nullptr;
	QJsonArray history;
	QJsonArray tourStops;
	int tourIdx = 0;
	QTimer tourTimer;
};

class NebulaChatWindow : public QWidget
{
	Q_OBJECT
public:
	explicit NebulaChatWindow(NebulaAI* module);
	void append(const QString& who, const QString& text);

private:
	QTextEdit* log;
	QLineEdit* input;
};

//! Qt plug-in interface for NebulaAI
class NebulaAIStelPluginInterface : public QObject, public StelPluginInterface
{
	Q_OBJECT
	Q_PLUGIN_METADATA(IID StelPluginInterface_iid)
	Q_INTERFACES(StelPluginInterface)
public:
	StelModule* getStelModule() const override;
	StelPluginInfo getPluginInfo() const override;
	QObjectList getExtensionList() const override { return QObjectList(); }
};

#endif /* NEBULAAI_HPP */
