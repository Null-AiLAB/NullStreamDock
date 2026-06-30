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

#include "preview-widget.hpp"

#include <obs.h>

#include <QWindow>
#include <QResizeEvent>
#include <QShowEvent>

namespace {

// Letterbox a base canvas into a window, preserving aspect ratio.
// (Mirrors GetScaleAndCenterPos from OBS display-helpers.)
inline void getScaleAndCenter(int baseCX, int baseCY, int winCX, int winCY, int &x, int &y, float &scale)
{
	if (baseCX <= 0 || baseCY <= 0 || winCX <= 0 || winCY <= 0) {
		x = 0;
		y = 0;
		scale = 1.0f;
		return;
	}

	const double winAspect = double(winCX) / double(winCY);
	const double baseAspect = double(baseCX) / double(baseCY);
	int newCX, newCY;

	if (winAspect > baseAspect) {
		scale = float(winCY) / float(baseCY);
		newCX = int(double(winCY) * baseAspect);
		newCY = winCY;
	} else {
		scale = float(winCX) / float(baseCX);
		newCX = winCX;
		newCY = int(double(winCX) / baseAspect);
	}

	x = winCX / 2 - newCX / 2;
	y = winCY / 2 - newCY / 2;
}

} // namespace

PreviewWidget::PreviewWidget(QWidget *parent) : QWidget(parent)
{
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);
}

PreviewWidget::~PreviewWidget()
{
	destroying_ = true;
	if (display_) {
		obs_display_remove_draw_callback(display_, drawPreview, this);
		obs_display_destroy(display_);
		display_ = nullptr;
	}
}

QPaintEngine *PreviewWidget::paintEngine() const
{
	return nullptr;
}

void PreviewWidget::createDisplay()
{
	if (display_ || destroying_)
		return;
	if (!windowHandle() || !windowHandle()->isExposed())
		return;

	const qreal dpr = devicePixelRatioF();

	gs_init_data info = {};
	info.cx = uint32_t(width() * dpr);
	info.cy = uint32_t(height() * dpr);
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;
	info.window.hwnd = reinterpret_cast<void *>(windowHandle()->winId());

	display_ = obs_display_create(&info, 0x000000);
	if (display_)
		obs_display_add_draw_callback(display_, drawPreview, this);
}

void PreviewWidget::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	createDisplay();
}

void PreviewWidget::paintEvent(QPaintEvent *)
{
	createDisplay();
}

void PreviewWidget::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	createDisplay();

	if (display_) {
		const qreal dpr = devicePixelRatioF();
		obs_display_resize(display_, uint32_t(width() * dpr), uint32_t(height() * dpr));
	}
}

void PreviewWidget::drawPreview(void *data, uint32_t, uint32_t)
{
	auto *self = static_cast<PreviewWidget *>(data);
	if (!self || !self->display_)
		return;

	obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return;

	uint32_t dw = 0, dh = 0;
	obs_display_size(self->display_, &dw, &dh);
	if (dw == 0 || dh == 0)
		return;

	int x, y;
	float scale;
	getScaleAndCenter(int(ovi.base_width), int(ovi.base_height), int(dw), int(dh), x, y, scale);

	const int previewCX = int(scale * float(ovi.base_width));
	const int previewCY = int(scale * float(ovi.base_height));

	gs_viewport_push();
	gs_projection_push();

	gs_ortho(0.0f, float(ovi.base_width), 0.0f, float(ovi.base_height), -100.0f, 100.0f);
	gs_set_viewport(x, y, previewCX, previewCY);

	obs_render_main_texture_src_color_only();

	gs_load_vertexbuffer(nullptr);

	gs_projection_pop();
	gs_viewport_pop();
}
