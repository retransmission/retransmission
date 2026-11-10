// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "OptionsDialog.h"

#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include <QtCore/QFileInfo>
#include <QtCore/QSignalBlocker>
#include <QtCore/QString>

#include <QtWidgets/QComboBox>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>

#include <libtransmission/quark.h>
#include <libtransmission/torrent-metainfo.h>
#include <libtransmission/types.h>

#include "FileTreeModel.h"
#include "FreeSpaceLabel.h"
#include "Prefs.h"
#include "Session.h"
#include "Utils.h"

/***
****
***/

OptionsDialog::OptionsDialog(
    Session& session,
    Prefs const& prefs,
    tr::app::AddData addme,
    tr::app::AddOptions const& preset,
    QWidget* parent)
    : BaseDialog{ parent }
    , add_{ std::move(addme) }
    , session_{ session }
    , is_local_fs_{ session_.is_local_filesystem() }
{
    ui_.setupUi(this);

    setWindowTitle(tr("Torrent Options"));

    edit_timer_.setInterval(2000);
    edit_timer_.setSingleShot(true);
    connect(&edit_timer_, &QTimer::timeout, this, &OptionsDialog::onDestinationChanged);

    if (add_.type == tr::app::AddData::Type::File) {
        ui_.sourceStack->setCurrentWidget(ui_.sourceButton);
        ui_.sourceButton->setMode(PathButton::FileMode);
        ui_.sourceButton->setTitle(tr("Open a Torrent"));
        ui_.sourceButton->setNameFilter(tr("Torrent Files (*.torrent);;All Files (*.*)"));
        ui_.sourceButton->setPath(Utils::qstringFromUtf8(add_.source));
        connect(ui_.sourceButton, &PathButton::pathChanged, this, &OptionsDialog::onSourceChanged);
    } else {
        ui_.sourceStack->setCurrentWidget(ui_.sourceEdit);
        submitted_source_ = Utils::qstringFromUtf8(add_.readable_name());
        ui_.sourceEdit->setText(submitted_source_);
        ui_.sourceEdit->selectAll();
        connect(ui_.sourceEdit, &QLineEdit::editingFinished, this, &OptionsDialog::onSourceChanged);
    }

    ui_.sourceStack->setFixedHeight(ui_.sourceStack->currentWidget()->sizeHint().height());
    ui_.sourceLabel->setBuddy(ui_.sourceStack->currentWidget());

    auto const font_metrics = QFontMetrics{ font() };
    int const width = font_metrics.size(0, QStringLiteral("This is a pretty long torrent filename indeed.torrent")).width();
    ui_.sourceStack->setMinimumWidth(width);

    auto const download_dir = Utils::removeTrailingDirSeparator(Utils::qstringFromUtf8(preset.download_dir));
    ui_.freeSpaceLabel->setSession(session_);
    ui_.freeSpaceLabel->setPath(download_dir);

    auto const recent_download_paths = prefs.get<QStringList>(TR_KEY_recent_download_paths);

    ui_.destinationButton->setMode(PathButton::DirectoryMode);
    ui_.destinationButton->setTitle(tr("Select Destination Folder"));
    ui_.destinationButton->setPath(download_dir);
    ui_.destinationButton->setRecentPaths(recent_download_paths);

    ui_.destinationCombo->addItems(recent_download_paths);
    ui_.destinationCombo->setCurrentText(download_dir);

    if (is_local_fs_) {
        local_destination_.setPath(download_dir);
    }

    connect(ui_.destinationButton, &PathButton::pathChanged, this, &OptionsDialog::onDestinationChanged);
    connect(ui_.destinationCombo, &QComboBox::editTextChanged, &edit_timer_, qOverload<>(&QTimer::start));
    connect(ui_.destinationCombo->lineEdit(), &QLineEdit::editingFinished, this, &OptionsDialog::onDestinationChanged);
    connect(ui_.destinationCombo, qOverload<int>(&QComboBox::activated), this, &OptionsDialog::onDestinationChanged);

    ui_.filesView->setEditable(false);
    ui_.priorityCombo->addItem(tr("High"), TR_PRI_HIGH);
    ui_.priorityCombo->addItem(tr("Normal"), TR_PRI_NORMAL);
    ui_.priorityCombo->addItem(tr("Low"), TR_PRI_LOW);
    ui_.priorityCombo->setCurrentIndex(ui_.priorityCombo->findData(preset.bandwidth_priority.value_or(TR_PRI_NORMAL)));

    ui_.startCheck->setChecked(!preset.paused.value_or(false));
    ui_.trashCheck->setChecked(add_.trash_source);

    // a resolve that lands after the user is done with the dialog must not report a duplicate
    connect(this, &QDialog::rejected, this, [this] { resolving_.reset(); });
    connect(ui_.dialogButtons, &QDialogButtonBox::rejected, this, [this] {
        resolving_.reset();
        deleteLater();
    });
    connect(ui_.dialogButtons, &QDialogButtonBox::accepted, this, &OptionsDialog::onAccepted);

    connect(ui_.filesView, &FileTreeView::priorityChanged, this, &OptionsDialog::onPriorityChanged);
    connect(ui_.filesView, &FileTreeView::wantedChanged, this, &OptionsDialog::onWantedChanged);

    session_connection_ = session_.session_updated.connect_scoped(&OptionsDialog::onSessionUpdated, this);

    updateWidgetsLocality();
    reload();
}

OptionsDialog::~OptionsDialog()
{
    clearInfo();
}

/***
****
***/

void OptionsDialog::clearInfo()
{
    metainfo_.reset();
    files_.clear();
}

void OptionsDialog::reload()
{
    clearInfo();

    ui_.filesView->clear();
    priorities_.clear();
    wanted_.clear();

    metainfo_ = add_.parsed();

    bool const have_files_to_show = metainfo_ && !std::empty(*metainfo_);

    ui_.filesView->setVisible(have_files_to_show);
    layout()->setSizeConstraint(have_files_to_show ? QLayout::SetDefaultConstraint : QLayout::SetFixedSize);

    if (metainfo_) {
        auto const n_files = metainfo_->file_count();
        priorities_.assign(n_files, TR_PRI_NORMAL);
        wanted_.assign(n_files, true);

        for (tr_file_index_t i = 0; i < n_files; ++i) {
            auto f = tr::app::TorrentFile{};
            f.index = i;
            f.priority = static_cast<tr_priority_t>(priorities_[i]);
            f.wanted = wanted_[i];
            f.size = metainfo_->file_size(i);
            f.have = 0;
            f.name = metainfo_->file_subpath(i);
            files_.push_back(std::move(f));
        }
    }

    ui_.filesView->update(files_);
    ui_.filesView->hideColumn(FileTreeModel::COL_PROGRESS);
}

void OptionsDialog::updateWidgetsLocality()
{
    ui_.destinationStack->setCurrentWidget(is_local_fs_ ? static_cast<QWidget*>(ui_.destinationButton) : ui_.destinationCombo);
    ui_.destinationStack->setFixedHeight(ui_.destinationStack->currentWidget()->sizeHint().height());
    ui_.destinationLabel->setBuddy(ui_.destinationStack->currentWidget());
}

void OptionsDialog::onSessionUpdated()
{
    auto const is_local_fs = session_.is_local_filesystem();

    if (is_local_fs_ != is_local_fs) {
        is_local_fs_ = is_local_fs;
        updateWidgetsLocality();
    }
}

void OptionsDialog::onPriorityChanged(file_indices_t const& file_indices, int priority)
{
    for (int const i : file_indices) {
        priorities_[i] = priority;
    }
}

void OptionsDialog::onWantedChanged(file_indices_t const& file_indices, bool is_wanted)
{
    for (int const i : file_indices) {
        wanted_[i] = is_wanted;
    }
}

void OptionsDialog::onAccepted()
{
    auto options = tr::app::AddOptions{};

    options.download_dir = (ui_.destinationStack->currentWidget() == ui_.destinationButton ?
                                local_destination_.absolutePath() :
                                ui_.destinationCombo->currentText())
                               .toStdString();
    options.paused = !ui_.startCheck->isChecked();
    options.bandwidth_priority = static_cast<tr_priority_t>(ui_.priorityCombo->currentData().toInt());

    for (int i = 0, n = static_cast<int>(std::size(wanted_)); i < n; ++i) {
        if (!wanted_[i]) {
            options.files_unwanted.push_back(i);
        }

        if (priorities_[i] == TR_PRI_LOW) {
            options.priority_low.push_back(i);
        } else if (priorities_[i] == TR_PRI_HIGH) {
            options.priority_high.push_back(i);
        }
    }

    add_.trash_source = ui_.trashCheck->isChecked();
    resolving_.reset();
    session_.add_torrent(add_, std::move(options));

    deleteLater();
}

void OptionsDialog::onSourceChanged()
{
    auto add = tr::app::AddData{};

    if (ui_.sourceStack->currentWidget() == ui_.sourceButton) {
        add = tr::app::AddData{ ui_.sourceButton->path().toStdString() };
    } else {
        auto const text = ui_.sourceEdit->text();
        if (text == submitted_source_) {
            return;
        }

        submitted_source_ = text;
        add = tr::app::AddData{ text.toStdString() };
    }

    add_ = add;
    reload();

    resolving_.reset();
    if (add.type == tr::app::AddData::Type::None) {
        return;
    }

    // runs only while `resolving_` is this source's token, so `this` is alive
    resolving_ = std::make_shared<std::monostate const>();
    session_.resolve_add(std::move(add), resolving_, [this](std::optional<tr::app::AddData> resolved) {
        if (resolved) {
            add_ = std::move(*resolved);
        } else {
            // the session already has it; the duplicate report tells the user
            add_ = {};
            clearSource();
        }

        reload();
    });
}

void OptionsDialog::clearSource()
{
    if (ui_.sourceStack->currentWidget() == ui_.sourceButton) {
        auto const blocker = QSignalBlocker{ ui_.sourceButton };
        ui_.sourceButton->setPath({});
    } else {
        ui_.sourceEdit->clear();
        submitted_source_.clear();
    }
}

void OptionsDialog::onDestinationChanged()
{
    if (ui_.destinationStack->currentWidget() == ui_.destinationButton) {
        local_destination_.setPath(ui_.destinationButton->path());
        ui_.freeSpaceLabel->setPath(local_destination_.absolutePath());
    } else {
        ui_.freeSpaceLabel->setPath(ui_.destinationCombo->currentText());
    }
}
