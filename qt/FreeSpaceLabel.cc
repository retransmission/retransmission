// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <cstdint>

#include <QDir>
#include <QLabel>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <libtransmission/converters.h>
#include <libtransmission/quark.h>
#include <libtransmission/types.h>
#include <libtransmission/variant.h>

#include "Formatter.h"
#include "FreeSpaceLabel.h"
#include "Session.h"
#include "VariantHelpers.h"

using ::trqt::variant_helpers::dictFind;

namespace
{

int const IntervalMSec = 15000;

} // namespace

FreeSpaceLabel::FreeSpaceLabel(QWidget* parent)
    : QLabel{ parent }
    , timer_{ this }
{
    timer_.setSingleShot(true);
    timer_.setInterval(IntervalMSec);

    connect(&timer_, &QTimer::timeout, this, &FreeSpaceLabel::onTimer);
}

void FreeSpaceLabel::setSession(Session& session)
{
    if (session_ == &session) {
        return;
    }

    session_ = &session;
    onTimer();
}

void FreeSpaceLabel::setPath(QString const& path)
{
    if (path_ != path) {
        setText(tr("<i>Calculating Free Space…</i>"));
        path_ = path;
        onTimer();
    }
}

void FreeSpaceLabel::onTimer()
{
    timer_.stop();

    if (session_ == nullptr || path_.isEmpty()) {
        return;
    }

    auto params = tr_variant::Map{ 1U };
    params.insert_or_assign(TR_KEY_path, tr::serializer::to_variant(path_));

    auto q = tr::app::RpcQueue::make();

    q->add([this, params = std::move(params)](RpcClient::ResponseFunc done) mutable {
        session_->exec(TR_KEY_free_space, std::move(params), std::move(done));
    });

    q->add([self = QPointer<FreeSpaceLabel>{ this }](RpcResponse const& r) {
        // the label may have been destroyed while the request was in flight
        if (self == nullptr) {
            return;
        }

        // update the label
        if (auto const bytes = dictFind<int64_t>(r.args.get(), TR_KEY_size_bytes); bytes && *bytes > 1) {
            self->setText(tr("%1 free").arg(Formatter::storage_to_string(*bytes)));
        } else {
            self->setText(QString{});
        }

        // update the tooltip
        auto const path = dictFind<QString>(r.args.get(), TR_KEY_path);
        self->setToolTip(QDir::toNativeSeparators(path.value_or(QString{})));

        self->timer_.start();
    });

    q->run();
}
