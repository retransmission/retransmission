// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <set>

#include <QtCore/QObject>
#include <QtCore/QTimer>

class QGridLayout;

class ColumnResizer : public QObject
{
    Q_OBJECT

public:
    explicit ColumnResizer(QObject* parent = nullptr);
    ~ColumnResizer() override = default;
    ColumnResizer(ColumnResizer&&) = delete;
    ColumnResizer(ColumnResizer const&) = delete;
    ColumnResizer& operator=(ColumnResizer&&) = delete;
    ColumnResizer& operator=(ColumnResizer const&) = delete;

    void addLayout(QGridLayout* layout);

    // QObject
    bool eventFilter(QObject* object, QEvent* event) override;

public slots:
    void update() const;

private:
    void scheduleUpdate();

    QTimer timer_;
    std::set<QGridLayout*> layouts_;
};
