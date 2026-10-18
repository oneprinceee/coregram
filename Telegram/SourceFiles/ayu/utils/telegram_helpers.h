#pragma once

#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "ayu/data/messages_storage.h"

using ID = long long;

inline ID getDialogIdFromPeer(not_null<PeerData*> peer) {
	ID peerId = peer->id.value & PeerId::kChatTypeMask;
	if (peer->isChannel() || peer->isChat()) {
		peerId = -peerId;
	}
	return peerId;
}

inline bool isMessageSavable(const not_null<HistoryItem*> item) {
	if (const auto possiblyBot = item->history()->peer->asUser()) {
		return !possiblyBot->isBot();
	}
	return true;
}

inline void processMessageDelete(not_null<HistoryItem*> item) {
	if (!isMessageSavable(item)) {
		item->destroy();
	} else {
		if (item->ttlDestroyAt() > 0) {
			item->applyTTL(0);
		}
		item->setDeleted();
		AyuMessages::addDeletedMessage(item);
	}
}
