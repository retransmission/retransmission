// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission-app/torrent-list.h>

#include "BaseDialog.h"
#include "ui_RelocateDialog.h"

class Prefs;
class Session;
class TorrentModel;

class RelocateDialog : public BaseDialog
{
    Q_OBJECT

public:
    RelocateDialog(
        Session& session,
        Prefs const& prefs,
        TorrentModel const& model,
        tr::app::TorrentList::Ids ids,
        QWidget* parent = nullptr);
    ~RelocateDialog() override = default;
    RelocateDialog(RelocateDialog&&) = delete;
    RelocateDialog(RelocateDialog const&) = delete;
    RelocateDialog& operator=(RelocateDialog&&) = delete;
    RelocateDialog& operator=(RelocateDialog const&) = delete;

private slots:
    void onSetLocation();
    void onMoveToggled(bool b) const;

private:
    [[nodiscard]] QString newLocation() const;

    Session& session_;
    tr::app::TorrentList::Ids const ids_;

    Ui::RelocateDialog ui_ = {};

    static bool move_flag;
};
