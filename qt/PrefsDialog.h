// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>

#include <QtCore/QTimer>

#include "BaseDialog.h"
#include "Prefs.h"
#include "Session.h"
#include "ui_PrefsDialog.h"

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

    // QWidget
    void setVisible(bool visible) override;

private:
    enum class PortTestStatus : uint8_t { Unknown = 0U, Checking, Open, Closed, Error };

    template<typename T>
    void set(tr_quark const key, T const& val)
    {
        prefs_.set(key, val);
        refreshPref(key);
    }

    void refreshPref(tr_quark key);
    void sessionUpdated();

    void onPortTest();
    void onPortTested(std::optional<bool> result, Session::PortTestIpProtocol ip_protocol);
    void portTestSetEnabled();
    void updatePortStatusLabel();
    [[nodiscard]] static QString getPortStatusText(PortTestStatus status);

    void onUpdateBlocklistClicked();
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
    std::array<PortTestStatus, Session::NUM_PORT_TEST_IP_PROTOCOL> port_test_status_ = {};

    std::multimap<tr_quark, std::function<void()>> updaters_;

    QTimer blocklist_info_timer_;
};
