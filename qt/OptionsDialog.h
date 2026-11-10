// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <memory>
#include <variant>
#include <vector>

#include <sigslot/signal.hpp>

#include <QtCore/QDir>
#include <QtCore/QString>
#include <QtCore/QTimer>

#include <libtransmission-app/add-data.h>
#include <libtransmission-app/torrent.h> // TorrentFile

#include "BaseDialog.h"
#include "Typedefs.h" // file_indices_t
#include "ui_OptionsDialog.h"

class Prefs;
class Session;

class OptionsDialog : public BaseDialog
{
    Q_OBJECT

public:
    // `addme` and `preset`, the choices the dialog starts with, are what `torrent_add_prompt` fired with
    OptionsDialog(
        Session& session,
        Prefs const& prefs,
        tr::app::AddData addme,
        tr::app::AddOptions const& preset,
        QWidget* parent = nullptr);
    OptionsDialog& operator=(OptionsDialog&&) = delete;
    OptionsDialog& operator=(OptionsDialog const&) = delete;
    OptionsDialog(OptionsDialog&&) = delete;
    OptionsDialog(OptionsDialog const&) = delete;
    ~OptionsDialog() override;

private slots:
    void onAccepted();
    void onPriorityChanged(file_indices_t const& file_indices, int priority);
    void onWantedChanged(file_indices_t const& file_indices, bool is_wanted);

    void onSourceChanged();
    void onDestinationChanged();

private:
    void onSessionUpdated();
    void reload();
    void updateWidgetsLocality();
    void clearSource();

    tr::app::AddData add_;
    std::vector<tr::app::TorrentFile> files_;

    QDir local_destination_;
    QTimer edit_timer_;
    Session& session_;
    Ui::OptionsDialog ui_ = {};
    QString submitted_source_; // the source edit's text when it last named a source
    std::shared_ptr<std::monostate const> resolving_; // the current source's `resolve_add()` token
    bool is_local_fs_ = {};

    sigslot::scoped_connection session_connection_;
};
