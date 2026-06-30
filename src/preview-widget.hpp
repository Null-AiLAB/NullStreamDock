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

#pragma once

#include <QWidget>

struct obs_display;

// A Qt widget that renders the OBS main (horizontal) program output, modeled
// after OBS's own OBSQTDisplay. Uses a native window and an obs_display.
class PreviewWidget : public QWidget {
public:
	explicit PreviewWidget(QWidget *parent = nullptr);
	~PreviewWidget() override;

	QPaintEngine *paintEngine() const override;

protected:
	void showEvent(QShowEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private:
	void createDisplay();
	static void drawPreview(void *data, uint32_t cx, uint32_t cy);

	obs_display *display_ = nullptr;
	bool destroying_ = false;
};
