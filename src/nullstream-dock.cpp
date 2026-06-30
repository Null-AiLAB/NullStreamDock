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
#include <util/platform.h>
#include "nullstream-dock.h"
#include "preview-widget.hpp"

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
#include <QSplitter>
#include <QFont>
#include <QString>
#include <QStringList>
#include <QInputDialog>
#include <QLineEdit>

#include <string>
#include <vector>

namespace {

// --- single dock instance widgets ---
QWidget *g_dock = nullptr;
QListWidget *g_sceneList = nullptr;
QComboBox *g_coverCombo = nullptr;
QPushButton *g_coverBtn = nullptr;
QPushButton *g_streamBtn = nullptr;
QListWidget *g_presetList = nullptr;

// --- cover (蓋絵) state ---
std::string g_sceneBeforeCover;
bool g_coverOn = false;

// --- presets ---
struct Preset {
	std::string name;
	std::string scene;
	std::string cover;
};
std::vector<Preset> g_presets;

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
const char *kSmallBtnStyle = "QPushButton{background-color:#3a3f4b;color:#e7e9ee;border:1px solid #4a5160;"
			     "border-radius:5px;padding:5px;}"
			     "QPushButton:hover{background-color:#444b59;}";
const char *kListStyle = "QListWidget{border:1px solid #3a3f4b;border-radius:6px;}"
			 "QListWidget::item{padding:6px 8px;}"
			 "QListWidget::item:selected{background-color:#2f6f63;color:#ffffff;}";

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

// ---------------- presets ----------------

void savePresets()
{
	obs_data_t *root = obs_data_create();
	obs_data_array_t *arr = obs_data_array_create();
	for (const Preset &p : g_presets) {
		obs_data_t *o = obs_data_create();
		obs_data_set_string(o, "name", p.name.c_str());
		obs_data_set_string(o, "scene", p.scene.c_str());
		obs_data_set_string(o, "cover", p.cover.c_str());
		obs_data_array_push_back(arr, o);
		obs_data_release(o);
	}
	obs_data_set_array(root, "presets", arr);

	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
	char *path = obs_module_config_path("presets.json");
	if (path) {
		obs_data_save_json(root, path);
		bfree(path);
	}

	obs_data_array_release(arr);
	obs_data_release(root);
}

void loadPresets()
{
	g_presets.clear();
	char *path = obs_module_config_path("presets.json");
	if (!path)
		return;
	obs_data_t *root = obs_data_create_from_json_file(path);
	bfree(path);
	if (!root)
		return;

	obs_data_array_t *arr = obs_data_get_array(root, "presets");
	if (arr) {
		const size_t n = obs_data_array_count(arr);
		for (size_t i = 0; i < n; i++) {
			obs_data_t *o = obs_data_array_item(arr, i);
			Preset p;
			p.name = obs_data_get_string(o, "name");
			p.scene = obs_data_get_string(o, "scene");
			p.cover = obs_data_get_string(o, "cover");
			if (!p.name.empty())
				g_presets.push_back(p);
			obs_data_release(o);
		}
		obs_data_array_release(arr);
	}
	obs_data_release(root);
}

void refreshPresetList()
{
	if (!g_presetList)
		return;
	g_presetList->clear();
	for (const Preset &p : g_presets)
		g_presetList->addItem(QString::fromUtf8(p.name.c_str()));
}

void applyPreset(int row)
{
	if (row < 0 || row >= static_cast<int>(g_presets.size()))
		return;
	const Preset &p = g_presets[static_cast<size_t>(row)];
	if (!p.scene.empty())
		switchToScene(QString::fromUtf8(p.scene.c_str()));
	if (!p.cover.empty() && g_coverCombo) {
		const int idx = g_coverCombo->findText(QString::fromUtf8(p.cover.c_str()));
		if (idx >= 0)
			g_coverCombo->setCurrentIndex(idx);
	}
}

void saveCurrentAsPreset()
{
	bool ok = false;
	const QString name = QInputDialog::getText(g_dock, QStringLiteral("プリセット保存"),
						   QStringLiteral("プリセット名:"), QLineEdit::Normal, QString(), &ok);
	if (!ok || name.trimmed().isEmpty())
		return;

	Preset p;
	p.name = name.trimmed().toUtf8().constData();
	p.scene = currentSceneName();
	p.cover = g_coverCombo ? g_coverCombo->currentText().toUtf8().constData() : "";
	g_presets.push_back(p);
	savePresets();
	refreshPresetList();
}

void deleteSelectedPreset()
{
	if (!g_presetList)
		return;
	const int row = g_presetList->currentRow();
	if (row < 0 || row >= static_cast<int>(g_presets.size()))
		return;
	g_presets.erase(g_presets.begin() + row);
	savePresets();
	refreshPresetList();
}

// ---------------- UI ----------------

QLabel *makeSectionLabel(const QString &text, QWidget *parent)
{
	QLabel *l = new QLabel(text, parent);
	l->setStyleSheet(kSectionLabelStyle);
	return l;
}

// Wrap content in a titled "card" panel.
QWidget *makePanel(const QString &title, QWidget *content)
{
	QFrame *frame = new QFrame();
	frame->setObjectName("nsdPanel");
	frame->setStyleSheet("QFrame#nsdPanel{background:#262a33;border-radius:8px;}");
	QVBoxLayout *l = new QVBoxLayout(frame);
	l->setContentsMargins(8, 6, 8, 8);
	l->setSpacing(6);
	l->addWidget(makeSectionLabel(title, frame));
	l->addWidget(content, 1);
	return frame;
}

// A "coming soon" placeholder for modules not built yet.
QWidget *makePlaceholder(const QString &note)
{
	QLabel *l = new QLabel(note);
	l->setAlignment(Qt::AlignCenter);
	l->setWordWrap(true);
	l->setMinimumHeight(70);
	l->setStyleSheet("QLabel{color:#6b7280;border:1px dashed #3a3f4b;"
			 "border-radius:6px;padding:10px;}");
	return l;
}

// --- top row panels ---

QWidget *makeVerticalPanel()
{
	return makePanel(QStringLiteral("縦配信画面（切替可）"),
			 makePlaceholder(QStringLiteral("縦プレビュー\n（これから・合成方法は相談中）")));
}

QWidget *makeMainPreviewPanel()
{
	PreviewWidget *preview = new PreviewWidget();
	preview->setMinimumSize(160, 90);
	return makePanel(QStringLiteral("メイン配信画面"), preview);
}

QWidget *makeCommentsPanel()
{
	return makePanel(QStringLiteral("統合コメント欄"),
			 makePlaceholder(QStringLiteral("統合コメント\n（これから）")));
}

// --- bottom row panels ---

QWidget *makeControlPanel()
{
	QWidget *w = new QWidget();
	QVBoxLayout *l = new QVBoxLayout(w);
	l->setContentsMargins(0, 0, 0, 0);
	l->setSpacing(8);

	// stream start/stop
	g_streamBtn = new QPushButton(w);
	g_streamBtn->setMinimumHeight(40);
	g_streamBtn->setCursor(Qt::PointingHandCursor);
	l->addWidget(g_streamBtn);
	QObject::connect(g_streamBtn, &QPushButton::clicked, g_streamBtn, []() {
		if (obs_frontend_streaming_active())
			obs_frontend_streaming_stop();
		else
			obs_frontend_streaming_start();
		updateStreamButton();
	});

	// presets
	l->addWidget(makeSectionLabel(QStringLiteral("プリセット"), w));
	g_presetList = new QListWidget(w);
	g_presetList->setStyleSheet(kListStyle);
	g_presetList->setMaximumHeight(110);
	l->addWidget(g_presetList);

	QHBoxLayout *presetBtns = new QHBoxLayout();
	QPushButton *savePresetBtn = new QPushButton(QStringLiteral("＋ 現在を保存"), w);
	QPushButton *applyPresetBtn = new QPushButton(QStringLiteral("適用"), w);
	QPushButton *delPresetBtn = new QPushButton(QStringLiteral("削除"), w);
	for (QPushButton *b : {savePresetBtn, applyPresetBtn, delPresetBtn}) {
		b->setStyleSheet(kSmallBtnStyle);
		b->setCursor(Qt::PointingHandCursor);
	}
	presetBtns->addWidget(savePresetBtn);
	presetBtns->addWidget(applyPresetBtn);
	presetBtns->addWidget(delPresetBtn);
	l->addLayout(presetBtns);
	QObject::connect(savePresetBtn, &QPushButton::clicked, savePresetBtn, []() { saveCurrentAsPreset(); });
	QObject::connect(applyPresetBtn, &QPushButton::clicked, applyPresetBtn, []() {
		if (g_presetList)
			applyPreset(g_presetList->currentRow());
	});
	QObject::connect(delPresetBtn, &QPushButton::clicked, delPresetBtn, []() { deleteSelectedPreset(); });
	QObject::connect(g_presetList, &QListWidget::itemDoubleClicked, g_presetList, [](QListWidgetItem *) {
		if (g_presetList)
			applyPreset(g_presetList->currentRow());
	});

	// cover (蓋絵)
	l->addWidget(makeSectionLabel(QStringLiteral("蓋絵（緊急カバー）"), w));
	g_coverCombo = new QComboBox(w);
	l->addWidget(g_coverCombo);
	g_coverBtn = new QPushButton(w);
	g_coverBtn->setMinimumHeight(32);
	g_coverBtn->setCursor(Qt::PointingHandCursor);
	l->addWidget(g_coverBtn);
	QObject::connect(g_coverBtn, &QPushButton::clicked, g_coverBtn, []() {
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

	l->addStretch(1);
	return makePanel(QStringLiteral("配信コントロール"), w);
}

QWidget *makeScenesPanel()
{
	QWidget *w = new QWidget();
	QVBoxLayout *l = new QVBoxLayout(w);
	l->setContentsMargins(0, 0, 0, 0);
	l->setSpacing(6);

	l->addWidget(makeSectionLabel(QStringLiteral("シーン"), w));
	g_sceneList = new QListWidget(w);
	g_sceneList->setStyleSheet(kListStyle);
	l->addWidget(g_sceneList, 1);
	QObject::connect(g_sceneList, &QListWidget::itemClicked, g_sceneList, [](QListWidgetItem *item) {
		if (!item)
			return;
		switchToScene(sceneNameFromItem(item));
	});

	l->addWidget(makeSectionLabel(QStringLiteral("ソース"), w));
	l->addWidget(makePlaceholder(QStringLiteral("ソース欄（次に作ります）")));

	return makePanel(QStringLiteral("シーン & ソース"), w);
}

QWidget *makeMixerPanel()
{
	return makePanel(QStringLiteral("音量ミキサー"), makePlaceholder(QStringLiteral("音量ミキサー\n（これから）")));
}

QWidget *buildDockWidget()
{
	QWidget *root = new QWidget();
	root->setMinimumSize(360, 280);
	QVBoxLayout *rootLayout = new QVBoxLayout(root);
	rootLayout->setContentsMargins(6, 6, 6, 6);

	QSplitter *outer = new QSplitter(Qt::Vertical, root);
	QSplitter *topRow = new QSplitter(Qt::Horizontal, outer);
	QSplitter *botRow = new QSplitter(Qt::Horizontal, outer);

	// top row: 縦 | メイン | コメント
	topRow->addWidget(makeVerticalPanel());
	topRow->addWidget(makeMainPreviewPanel());
	topRow->addWidget(makeCommentsPanel());
	topRow->setStretchFactor(0, 2);
	topRow->setStretchFactor(1, 5);
	topRow->setStretchFactor(2, 3);

	// bottom row: コントロール | シーン&ソース | ミキサー
	botRow->addWidget(makeControlPanel());
	botRow->addWidget(makeScenesPanel());
	botRow->addWidget(makeMixerPanel());
	botRow->setStretchFactor(0, 2);
	botRow->setStretchFactor(1, 3);
	botRow->setStretchFactor(2, 4);

	outer->addWidget(topRow);
	outer->addWidget(botRow);
	outer->setStretchFactor(0, 5);
	outer->setStretchFactor(1, 4);

	rootLayout->addWidget(outer);

	// initial state
	loadPresets();
	refreshPresetList();
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
	g_presetList = nullptr;
}
