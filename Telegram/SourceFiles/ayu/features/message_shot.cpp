#include "ayu/features/message_shot.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>
#include <QtGui/QPainter>

#include "history/history.h"
#include "history/history_inner_widget.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_list_widget.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "dialogs/ui/dialogs_video_userpic.h"
#include "window/window_session_controller.h"
#include "mainwindow.h"
#include "window/themes/window_theme.h"
#include "ui/chat/chat_theme.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/painter.h"
#include "ui/toast/toast.h"
#include "lang/lang_instance.h"
#include "styles/style_chat.h"

namespace MessageShot {
namespace {

constexpr auto kPadding = 16;

class ShotElementDelegate final : public HistoryView::DefaultElementDelegate {
public:
	ShotElementDelegate(
		QWidget *parent,
		const Ui::ChatStyle *st,
		Fn<void()> update,
		not_null<History*> history)
	: _parent(parent)
	, _pathGradient(HistoryView::MakePathShiftGradient(st, update))
	, _history(history) {
	}

	bool elementAnimationsPaused() override {
		return true;
	}

	auto elementPathShiftGradient() -> not_null<Ui::PathShiftGradient*> override {
		return _pathGradient.get();
	}

	HistoryView::Context elementContext() override {
		return HistoryView::Context::AdminLog;
	}

	bool elementHideReply(not_null<const HistoryView::Element*> view) override {
		return false;
	}

	HistoryView::ElementChatMode elementChatMode() override {
		return HistoryView::ElementChatMode::Wide;
	}

private:
	QWidget *_parent = nullptr;
	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;
	not_null<History*> _history;
};

QImage RemoveEmptySpaceAround(const QImage &original) {
	if (original.isNull()) {
		return {};
	}

	int minX = original.width();
	int minY = original.height();
	int maxX = 0;
	int maxY = 0;

	for (int x = 0; x < original.width(); ++x) {
		for (int y = 0; y < original.height(); ++y) {
			if (qAlpha(original.pixel(x, y)) != 0) {
				minX = std::min(minX, x);
				minY = std::min(minY, y);
				maxX = std::max(maxX, x);
				maxY = std::max(maxY, y);
			}
		}
	}

	if (minX > maxX || minY > maxY) {
		return {};
	}

	const QRect bounds(minX, minY, maxX - minX + 1, maxY - minY + 1);
	return original.copy(bounds);
}

QImage AddPaddingAndBackground(const QImage &original, QColor bgColor) {
	if (original.isNull()) {
		return {};
	}

	const auto ratio = style::DevicePixelRatio();
	const auto pad = kPadding * ratio;

	QImage padded(
		original.width() + 2 * pad,
		original.height() + 2 * pad,
		QImage::Format_ARGB32_Premultiplied);
	padded.setDevicePixelRatio(ratio);
	padded.fill(bgColor);

	QPainter p(&padded);
	p.drawImage(kPadding, kPadding, original);
	p.end();

	return padded;
}

} // namespace

void CaptureAndCopy(
		not_null<Window::SessionController*> controller,
		const std::vector<not_null<HistoryItem*>> &messagesList,
		Fn<void()> callback) {
	auto validMessages = messagesList;
	validMessages.erase(
		std::remove_if(
			validMessages.begin(),
			validMessages.end(),
			[=](const auto &message) {
				return !message || !controller->session().data().message(message->fullId());
			}),
		validMessages.end());

	if (validMessages.empty()) {
		return;
	}

	auto delegate = std::make_shared<ShotElementDelegate>(
		static_cast<QWidget*>(controller->widget().get()),
		controller->chatStyle().get(),
		[] {},
		validMessages.front()->history());

	auto createdViews = std::make_shared<std::unordered_map<not_null<HistoryItem*>, std::shared_ptr<HistoryView::Element>>>();
	createdViews->reserve(validMessages.size());
	for (const auto &message : validMessages) {
		createdViews->emplace(message, message->createView(delegate.get()));
	}

	auto getView = [createdViews](not_null<HistoryItem*> msg) {
		return createdViews->at(msg).get();
	};

	if (validMessages.size() > 1) {
		auto currentMsg = validMessages[0].get();
		for (size_t i = 1; i != validMessages.size(); ++i) {
			const auto nextMsg = validMessages[i].get();
			if (getView(nextMsg)->isHidden()) {
				getView(nextMsg)->setDisplayDate(false);
			} else {
				const auto viewDate = getView(currentMsg)->dateTime();
				const auto nextDate = getView(nextMsg)->dateTime();
				getView(nextMsg)->setDisplayDate(nextDate.date() != viewDate.date());
				auto attached = getView(nextMsg)->computeIsAttachToPrevious(getView(currentMsg));
				getView(nextMsg)->setAttachToPrevious(attached, getView(currentMsg));
				getView(currentMsg)->setAttachToNext(attached, getView(nextMsg));
				currentMsg = nextMsg;
			}
		}
		getView(validMessages.back())->setAttachToNext(false);
	} else {
		getView(validMessages[0])->setAttachToPrevious(false);
		getView(validMessages[0])->setAttachToNext(false);
	}

	int width = st::msgMaxWidth + (st::msgMargin.left() + st::msgMargin.right());
	int height = 0;

	for (size_t i = 0; i < validMessages.size(); ++i) {
		const auto view = getView(validMessages[i]);
		view->itemDataChanged();
		height += view->resizeGetHeight(width);
	}

	const auto ratio = style::DevicePixelRatio();
	const int pixelWidth = width * ratio;
	const int pixelHeight = height * ratio;

	QImage image(pixelWidth, pixelHeight, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);

	const auto viewport = QRect(0, 0, width, height);
	base::flat_map<not_null<PeerData*>, Ui::PeerUserpicView> userpics;

	{
		Painter p(&image);
		int y = 0;
		for (size_t i = 0; i < validMessages.size(); ++i) {
			const auto &message = validMessages[i];
			const auto view = getView(message);
			const auto displayUserpic = view->displayFromPhoto() || message->isPost();
			const auto rect = QRect(0, 0, width, view->height());

			auto context = controller->defaultChatTheme()->preparePaintContext(
				controller->chatStyle().get(),
				viewport,
				rect,
				rect,
				true);

			p.translate(0, y);
			view->draw(p, context);
			p.translate(0, -y);

			if (displayUserpic) {
				const auto picX = st::msgMargin.left();
				const auto picY = y + view->height() - st::msgPhotoSize;

				if (const auto from = message->displayFrom()) {
					Dialogs::Ui::PaintUserpic(
						p,
						from,
						nullptr,
						userpics[from],
						picX,
						picY,
						width,
						st::msgPhotoSize,
						context.paused);
				} else if (const auto info = message->displayHiddenSenderInfo()) {
					if (info->customUserpic.empty()) {
						info->emptyUserpic.paintCircle(
							p,
							picX,
							picY,
							width,
							st::msgPhotoSize);
					}
				}
			}

			y += view->height();
		}
	}

	auto cropped = RemoveEmptySpaceAround(image);
	if (!cropped.isNull()) {
		const auto isNight = Window::Theme::IsNightMode();
		const auto bgColor = isNight ? QColor(24, 25, 29) : QColor(240, 242, 245);
		auto finalImage = AddPaddingAndBackground(cropped, bgColor);

		QGuiApplication::clipboard()->setImage(finalImage);

		const auto isRu = Lang::GetInstance().id().startsWith(u"ru"_q);
		controller->showToast(isRu
			? u"Снимок экрана скопирован в буфер обмена"_q
			: u"Screenshot copied to clipboard"_q);
	}

	if (callback) {
		callback();
	}
}

void CaptureFromList(
		not_null<HistoryView::ListWidget*> widget,
		Fn<void()> clearSelected) {
	const auto ids = widget->getSelectedIds();
	if (ids.empty()) {
		return;
	}

	const auto session = &widget->session();
	const auto controller = widget->session().tryResolveWindow();
	if (!controller) {
		return;
	}

	std::vector<not_null<HistoryItem*>> messages;
	messages.reserve(ids.size());
	for (const auto id : ids) {
		if (const auto item = session->data().message(id)) {
			messages.push_back(item);
		}
	}

	CaptureAndCopy(controller, messages, std::move(clearSelected));
}

void CaptureFromInner(
		not_null<HistoryInner*> widget,
		Fn<void()> clearSelected) {
	const auto items = widget->getSelectedItems();
	if (items.empty()) {
		return;
	}

	const auto session = &widget->session();
	const auto controller = widget->session().tryResolveWindow();
	if (!controller) {
		return;
	}

	std::vector<not_null<HistoryItem*>> messages;
	messages.reserve(items.size());
	for (const auto id : items) {
		if (const auto item = session->data().message(id)) {
			messages.push_back(item);
		}
	}

	CaptureAndCopy(controller, messages, std::move(clearSelected));
}

} // namespace MessageShot
