// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // int64_t
#include <functional>
#include <initializer_list>
#include <map>
#include <string_view>

#include <sigslot/signal.hpp>

#include <QtCore/QPointer>

#include "BaseDialog.h"
#include "Prefs.h"
#include "Session.h"
#include "ui_PrefsDialog.h"

class QMessageBox;
class QString;

class PrefsDialog : public BaseDialog
{
    Q_OBJECT

public:
    PrefsDialog(Session& session, Prefs& prefs, QWidget* parent = nullptr);
    ~PrefsDialog() override;
    PrefsDialog& operator=(PrefsDialog&&) = delete;
    PrefsDialog& operator=(PrefsDialog const&) = delete;
    PrefsDialog(PrefsDialog&&) = delete;
    PrefsDialog(PrefsDialog const&) = delete;

private:
    template<typename T>
    void set(tr_quark const key, T const& val)
    {
        prefs_.set(key, val);
        refreshPref(key);
    }

    void refreshPref(tr_quark key);
    void sessionUpdated();

    void portTestSetEnabled();
    void updatePortStatusLabel();
    [[nodiscard]] static QString getPortStatusText(tr::app::Session::PortStatus status);

    void onUpdateBlocklistClicked();
    void onBlocklistUpdated(int64_t rule_count) const;
    void onBlocklistUpdateFailed(std::string_view errmsg) const;
    void updateBlocklistLabel();
    void updateLocality();

    template<typename T, typename W, typename Setter>
    void addUpdater(W* w, tr_quark key, Setter setter);
    template<typename T, typename W, typename Setter, typename Signal>
    void bind(W* w, tr_quark key, Setter setter, Signal signal);
    void bindEnabled(QWidgetList const& widgets, std::initializer_list<tr_quark> keys);

    template<typename T>
    void initCombo(QComboBox* w, tr_quark key);
    void initWidget(FreeSpaceLabel* w, tr_quark key);
    void initWidget(PathButton* w, tr_quark key);
    void initWidget(QCheckBox* w, tr_quark key);
    void initWidget(QDoubleSpinBox* w, tr_quark key);
    void initWidget(QLineEdit* w, tr_quark key);
    void initWidget(QPlainTextEdit* w, tr_quark key);
    void initWidget(QSpinBox* w, tr_quark key);
    void initWidget(QTimeEdit* w, tr_quark key);

    void initDownloadingTab();
    void initSeedingTab();
    void initSpeedTab();
    void initPrivacyTab();
    void initNetworkTab();
    void initDesktopTab();
    void initRemoteTab();

    Session& session_;
    Prefs& prefs_;

    Ui::PrefsDialog ui_ = {};

    bool is_local_fs_ = {};

    std::multimap<tr_quark, std::function<void()>> updaters_;

    // the box showing the latest blocklist update, while it is open
    QPointer<QMessageBox> blocklist_box_;

    sigslot::scoped_connection session_connection_;
    sigslot::scoped_connection stopped_connection_;
    sigslot::scoped_connection port_test_connection_;
    sigslot::scoped_connection blocklist_updated_connection_;
    sigslot::scoped_connection blocklist_update_failed_connection_;
};
