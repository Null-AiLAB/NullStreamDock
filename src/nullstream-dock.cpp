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

// --- widgets we need to update later (single dock instance) ---
QListWidget *g_sceneList = nullptr;
QComboBox *g_coverCombo = nullptr;
QPushButton *g_coverBtn = nullptr;
QPushButton *g_streamBtn = nullptr;

// --- cover (蓋絵) state ---
std::string g_sceneBeforeCover;
bool g_coverOn = false;

const char *kOnAirSuffix = "   \u25CF ON AIR"; // "   ● ON AIR"

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
}

void updateStreamButton()
{
	if (!g_streamBtn)
		return;
	const bool active = obs_frontend_streaming_active();
	g_streamBtn->setText(active ? QStringLiteral("\u25A0 配信を終了") : QStringLiteral("\u25CF 配信を開始"));
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

QWidget *buildDockWidget()
{
	QWidget *root = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout(root);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(8);

	// --- stream control ---
	g_streamBtn = new QPushButton(QStringLiteral("\u25CF 配信を開始"), root);
	g_streamBtn->setMinimumHeight(38);
	layout->addWidget(g_streamBtn);

	// --- cover (蓋絵) ---
	layout->addWidget(new QLabel(QStringLiteral("蓋絵（緊急カバー）"), root));

	g_coverCombo = new QComboBox(root);
	layout->addWidget(g_coverCombo);

	g_coverBtn = new QPushButton(QStringLiteral("蓋絵を表示"), root);
	g_coverBtn->setMinimumHeight(32);
	layout->addWidget(g_coverBtn);

	// --- separator ---
	QFrame *line = new QFrame(root);
	line->setFrameShape(QFrame::HLine);
	line->setFrameShadow(QFrame::Sunken);
	layout->addWidget(line);

	// --- scenes ---
	QHBoxLayout *sceneHeader = new QHBoxLayout();
	sceneHeader->addWidget(new QLabel(QStringLiteral("シーン"), root));
	sceneHeader->addStretch();
	QPushButton *refreshBtn = new QPushButton(QStringLiteral("更新"), root);
	refreshBtn->setMaximumWidth(90);
	sceneHeader->addWidget(refreshBtn);
	layout->addLayout(sceneHeader);

	g_sceneList = new QListWidget(root);
	layout->addWidget(g_sceneList, 1);

	// --- wiring ---
	QObject::connect(refreshBtn, &QPushButton::clicked, root, []() {
		refreshScenes();
		updateStreamButton();
	});

	QObject::connect(g_sceneList, &QListWidget::itemClicked, root, [](QListWidgetItem *item) {
		if (!item)
			return;
		switchToScene(sceneNameFromItem(item));
		refreshScenes();
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
			g_coverOn = true;
			g_coverBtn->setText(QStringLiteral("蓋絵を解除"));
		} else {
			if (!g_sceneBeforeCover.empty())
				switchToScene(QString::fromUtf8(g_sceneBeforeCover.c_str()));
			g_coverOn = false;
			g_coverBtn->setText(QStringLiteral("蓋絵を表示"));
		}
		refreshScenes();
	});

	// initial fill (scene collection may not be ready yet at module load;
	// the 更新 button re-syncs once OBS has fully started)
	refreshScenes();
	updateStreamButton();

	return root;
}

} // namespace

extern "C" void nullstream_dock_load(void)
{
	obs_log(LOG_INFO, "[NullStreamDock] creating dock widget");
	QWidget *dock = buildDockWidget();
	obs_frontend_add_dock_by_id("nullstream_dock", "NullStreamDock", dock);
	obs_log(LOG_INFO, "[NullStreamDock] dock registered");
}

extern "C" void nullstream_dock_unload(void)
{
	// OBS owns the dock widget once registered; nothing to free here.
	g_sceneList = nullptr;
	g_coverCombo = nullptr;
	g_coverBtn = nullptr;
	g_streamBtn = nullptr;
}
