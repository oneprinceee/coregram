#pragma once

#include "history/view/history_view_list_widget.h"
#include <vector>

class HistoryInner;

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace MessageShot {

void CaptureAndCopy(
	not_null<Window::SessionController*> controller,
	const std::vector<not_null<HistoryItem*>> &messages,
	Fn<void()> callback = nullptr);

void CaptureFromList(
	not_null<HistoryView::ListWidget*> widget,
	Fn<void()> clearSelected = nullptr);

void CaptureFromInner(
	not_null<HistoryInner*> widget,
	Fn<void()> clearSelected = nullptr);

} // namespace MessageShot
