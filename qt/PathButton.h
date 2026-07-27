// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QtCore/QStringList>

#include <QtWidgets/QToolButton>

class QMenu;

class PathButton : public QToolButton
{
    Q_OBJECT

public:
    enum Mode : uint8_t { DirectoryMode, FileMode };

    explicit PathButton(QWidget* parent = nullptr);
    ~PathButton() override = default;
    PathButton(PathButton&&) = delete;
    PathButton(PathButton const&) = delete;
    PathButton& operator=(PathButton&&) = delete;
    PathButton& operator=(PathButton const&) = delete;

    void setMode(Mode mode);
    void setTitle(QString const& title);
    void setNameFilter(QString const& name_filter);

    void setPath(QString const& path);
    void setRecentPaths(QStringList const& paths);

    [[nodiscard]] constexpr auto const& path() const noexcept
    {
        return path_;
    }

    // QWidget
    [[nodiscard]] QSize sizeHint() const override;

signals:
    void pathChanged(QString const& path);

protected:
    // QWidget
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onClicked() const;
    void onFileSelected(QString const& path);

private:
    void updateAppearance();
    void rebuildMenu();

    [[nodiscard]] bool isDirMode() const;
    [[nodiscard]] QString effectiveTitle() const;

    QStringList recent_paths_;
    QString name_filter_;
    QString path_;
    QString title_;
    QMenu* menu_ = nullptr;
    Mode mode_ = DirectoryMode;
};
