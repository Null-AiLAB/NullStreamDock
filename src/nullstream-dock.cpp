/*
NullStreamDock
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include "nullstream-dock.h"

#include <QObject>
#include <QCursor>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLabel>
#include <QFrame>
#include <QFont>
#include <QString>
#include <QStringList>

#include <string>

namespace {

// --- single dock instance widgets ---
QWidget *g_dock = nullptr;
QListWidget *g_sceneList = nullptr;
QComboBox *g_coverCombo = nullptr;
QPushButton *g_coverBtn = nullptr;
QPushButton *g_streamBtn = nullptr;

// --- cover (蓋絵) state ---
std::string g_sceneBeforeCover;
bool g_coverOn = false;

const char *kOnAirSuffix = "   \u25CF ON AIR"; // "   ● ON AIR"

// ---- styling ----
const char *kStreamStartStyle = "QPushButton{background-color:#2f8f4e;color:#ffffff;font-weight:bold;"
				"border:none;border-radius:6px;padding:10px;}"
				"QPushButton:hover{background-color:#36a059;}"
				"QPushButton:pressed{background-color:#287a43;}";
const char *kStreamStopStyle = "QPushButton{background-color:#c0392b;color:#ffffff;font-weight:bold;"
			       "border:none;border-radius:6px;padding:10px;}"
			       "QPushButton:hover{background-color:#d0463a;}"
			       "QPushButton:pressed{background-color:#a5301f;}";
const char *kCoverIdleStyle = "QPushButton{background-color:#3a3f4b;color:#e7e9ee;border:1px solid #4a5160;"
			      "border-radius:6px;padding:8px;}"
			      "QPushButton:hover{background-color:#444b59;}";
const char *kCoverArmedStyle = "QPushButton{background-color:#d9822b;color:#ffffff;font-weight:bold;"
			       "border:none;border-radius:6px;padding:8px;}"
			       "QPushButton:hover{background-color:#e6913a;}";
const char *kSectionLabelStyle = "QLabel{color:#8b919e;font-size:10px;font-weight:bold;"
				 "letter-spacing:1px;text-transform:uppercase;}";

QStringList getSceneNames()
{
	QStringList list;
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		const char *n = obs_source_get_name(scenes.sources.array[i]);
		if (n)
			list << QString::fromUtf8(n);
	}
	obs_frontend_source_list_free(&scenes);
	return list;
}

std::string currentSceneName()
{
	std::string name;
	obs_source_t *cur = obs_frontend_get_current_scene();
	if (cur) {
		const char *n = obs_source_get_name(cur);
		if (n)
			name = n;
		obs_source_release(cur);
	}
	return name;
}

void switchToScene(const QString &qname)
{
	const std::string n = qname.toUtf8().constData();
	obs_source_t *src = obs_get_source_by_name(n.c_str());
	if (src) {
		obs_frontend_set_current_scene(src);
		obs_source_release(src);
	}
}

void refreshScenes()
{
	if (!g_sceneList || !g_coverCombo)
		return;

	const QStringList names = getSceneNames();
	const QString keepCover = g_coverCombo->currentText();
	const std::string cur = currentSceneName();
	const QString curQ = QString::fromUtf8(cur.c_str());

	g_sceneList->blockSignals(true);
	g_coverCombo->blockSignals(true);

	g_sceneList->clear();
	g_coverCombo->clear();

	for (const QString &name : names) {
		g_coverCombo->addItem(name);

		QListWidgetItem *item = new QListWidgetItem(name, g_sceneList);
		if (!curQ.isEmpty() && curQ == name) {
			QFont f = item->font();
			f.setBold(true);
			item->setFont(f);
			item->setText(name + QString::fromUtf8(kOnAirSuffix));
		}
	}

	const int idx = g_coverCombo->findText(keepCover);
	if (idx >= 0)
		g_coverCombo->setCurrentIndex(idx);

	g_sceneList->blockSignals(false);
	g_coverCombo->blockSignals(false);
}

void updateStreamButton()
{
	if (!g_streamBtn)
		return;
	const bool active = obs_frontend_streaming_active();
	if (active) {
		g_streamBtn->setText(QStringLiteral("\u25A0 配信を終了"));
		g_streamBtn->setStyleSheet(kStreamStopStyle);
	} else {
		g_streamBtn->setText(QStringLiteral("\u25CF 配信を開始"));
		g_streamBtn->setStyleSheet(kStreamStartStyle);
	}
}

// strip the "   ● ON AIR" marker that refreshScenes() may append
QString sceneNameFromItem(const QListWidgetItem *item)
{
	QString text = item->text();
	const QString suffix = QString::fromUtf8(kOnAirSuffix);
	if (text.endsWith(suffix))
		text.chop(suffix.length());
	return text;
}

void setCoverArmed(bool armed)
{
	g_coverOn = armed;
	if (!g_coverBtn)
		return;
	if (armed) {
		g_coverBtn->setText(QStringLiteral("蓋絵を解除"));
		g_coverBtn->setStyleSheet(kCoverArmedStyle);
	} else {
		g_coverBtn->setText(QStringLiteral("蓋絵を表示"));
		g_coverBtn->setStyleSheet(kCoverIdleStyle);
	}
}

QLabel *makeSectionLabel(const QString &text, QWidget *parent)
{
	QLabel *l = new QLabel(text, parent);
	l->setStyleSheet(kSectionLabelStyle);
	return l;
}

QWidget *buildDockWidget()
{
	QWidget *root = new QWidget();
	root->setMinimumWidth(220);
	QVBoxLayout *layout = new QVBoxLayout(root);
	layout->setContentsMargins(10, 10, 10, 10);
	layout->setSpacing(8);

	// --- stream control ---
	layout->addWidget(makeSectionLabel(QStringLiteral("配信"), root));
	g_streamBtn = new QPushButton(root);
	g_streamBtn->setMinimumHeight(40);
	g_streamBtn->setCursor(Qt::PointingHandCursor);
	layout->addWidget(g_streamBtn);

	// --- cover (蓋絵) ---
	layout->addSpacing(2);
	layout->addWidget(makeSectionLabel(QStringLiteral("蓋絵（緊急カバー）"), root));
	g_coverCombo = new QComboBox(root);
	layout->addWidget(g_coverCombo);
	g_coverBtn = new QPushButton(root);
	g_coverBtn->setMinimumHeight(34);
	g_coverBtn->setCursor(Qt::PointingHandCursor);
	layout->addWidget(g_coverBtn);

	// --- separator ---
	QFrame *line = new QFrame(root);
	line->setFrameShape(QFrame::HLine);
	line->setFrameShadow(QFrame::Sunken);
	line->setStyleSheet("color:#3a3f4b;");
	layout->addSpacing(4);
	layout->addWidget(line);

	// --- scenes ---
	layout->addWidget(makeSectionLabel(QStringLiteral("シーン"), root));
	g_sceneList = new QListWidget(root);
	g_sceneList->setStyleSheet("QListWidget{border:1px solid #3a3f4b;border-radius:6px;}"
				   "QListWidget::item{padding:6px 8px;}"
				   "QListWidget::item:selected{background-color:#2f6f63;color:#ffffff;}");
	layout->addWidget(g_sceneList, 1);

	// --- wiring ---
	QObject::connect(g_sceneList, &QListWidget::itemClicked, root, [](QListWidgetItem *item) {
		if (!item)
			return;
		switchToScene(sceneNameFromItem(item));
	});

	QObject::connect(g_streamBtn, &QPushButton::clicked, root, []() {
		if (obs_frontend_streaming_active())
			obs_frontend_streaming_stop();
		else
			obs_frontend_streaming_start();
		updateStreamButton();
	});

	QObject::connect(g_coverBtn, &QPushButton::clicked, root, []() {
		if (!g_coverOn) {
			const QString target = g_coverCombo->currentText();
			if (target.isEmpty())
				return;
			const std::string cur = currentSceneName();
			const QString curQ = QString::fromUtf8(cur.c_str());
			if (!cur.empty() && curQ != target)
				g_sceneBeforeCover = cur;
			else
				g_sceneBeforeCover.clear();
			switchToScene(target);
			setCoverArmed(true);
		} else {
			if (!g_sceneBeforeCover.empty())
				switchToScene(QString::fromUtf8(g_sceneBeforeCover.c_str()));
			setCoverArmed(false);
		}
	});

	// initial state
	refreshScenes();
	updateStreamButton();
	setCoverArmed(false);

	return root;
}

// Frontend events -> refresh the dock on the Qt UI thread.
void on_frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		if (g_dock)
			QMetaObject::invokeMethod(g_dock, []() { refreshScenes(); }, Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		if (g_dock)
			QMetaObject::invokeMethod(g_dock, []() { updateStreamButton(); }, Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		g_dock = nullptr; // stop scheduling work during shutdown
		break;
	default:
		break;
	}
}

} // namespace

extern "C" void nullstream_dock_load(void)
{
	obs_log(LOG_INFO, "[NullStreamDock] creating dock widget");
	g_dock = buildDockWidget();
	obs_frontend_add_dock_by_id("nullstream_dock", "NullStreamDock", g_dock);
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	obs_log(LOG_INFO, "[NullStreamDock] dock registered");
}

extern "C" void nullstream_dock_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	g_dock = nullptr;
	g_sceneList = nullptr;
	g_coverCombo = nullptr;
	g_coverBtn = nullptr;
	g_streamBtn = nullptr;
}
