// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "TrackerDelegate.h"

#include <QtGui/QAbstractTextDocumentLayout>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>
#include <QtGui/QTextDocument>

#include <QtWidgets/QApplication>

#include <libtransmission/web-utils.h>

#include <libtransmission-app/favicon-cache.h>

#include "Formatter.h"
#include "Torrent.h"
#include "TrackerModel.h"
#include "TrFormat.h"
#include "Utils.h"

using namespace tr::app;

/***
****
***/

namespace
{
auto constexpr Spacing = 6;

auto constexpr Margin = QSize{ 10, 10 };

class ItemLayout
{
public:
    QRect icon_rect;
    QRect text_rect;

    ItemLayout(QString const& text, bool suppress_colors, Qt::LayoutDirection direction, QPoint const& top_left, int width);

    [[nodiscard]] QSize size() const
    {
        return (icon_rect | text_rect).size();
    }

    [[nodiscard]] QAbstractTextDocumentLayout* textLayout() const
    {
        return text_document_.documentLayout();
    }

private:
    QTextDocument text_document_;
};

ItemLayout::ItemLayout(
    QString const& text,
    bool suppress_colors,
    Qt::LayoutDirection direction,
    QPoint const& top_left,
    int width)
{
    auto const icon_size = QSize{ FaviconCache<QPixmap>::Width, FaviconCache<QPixmap>::Height };

    QRect base_rect{ top_left, QSize{ width, 0 } };

    icon_rect = QStyle::alignedRect(direction, Qt::AlignLeft | Qt::AlignTop, icon_size, base_rect);
    Utils::narrowRect(base_rect, icon_size.width() + Spacing, 0, direction);

    text_document_.setDocumentMargin(0);
    text_document_.setTextWidth(base_rect.width());

    QTextOption text_option;
    text_option.setTextDirection(direction);

    if (suppress_colors) {
        text_option.setFlags(QTextOption::SuppressColors);
    }

    text_document_.setDefaultTextOption(text_option);
    text_document_.setHtml(text);

    text_rect = base_rect;
    text_rect.setSize(text_document_.size().toSize());
}

} // namespace

/***
****
***/

QSize TrackerDelegate::sizeHint(QStyleOptionViewItem const& option, TrackerInfo const& info) const
{
    ItemLayout const layout{ getText(info),
                             true,
                             option.direction,
                             QPoint{ 0, 0 },
                             option.rect.width() - (Margin.width() * 2) };
    return layout.size() + Margin * 2;
}

QSize TrackerDelegate::sizeHint(QStyleOptionViewItem const& option, QModelIndex const& index) const
{
    auto const tracker_info = index.data(TrackerModel::TrackerRole).value<TrackerInfo>();
    return sizeHint(option, tracker_info);
}

void TrackerDelegate::paint(QPainter* painter, QStyleOptionViewItem const& option, QModelIndex const& index) const
{
    auto const tracker_info = index.data(TrackerModel::TrackerRole).value<TrackerInfo>();
    painter->save();
    painter->setClipRect(option.rect);
    drawBackground(painter, option, index);
    drawTracker(painter, option, tracker_info);
    drawFocus(painter, option, option.rect);
    painter->restore();
}

void TrackerDelegate::drawTracker(QPainter* painter, QStyleOptionViewItem const& option, TrackerInfo const& inf) const
{
    bool const is_item_selected((option.state & QStyle::State_Selected) != 0);
    bool const is_item_enabled((option.state & QStyle::State_Enabled) != 0);
    bool const is_item_active((option.state & QStyle::State_Active) != 0);

    QIcon const tracker_icon(inf.st.getFavicon());

    QRect const content_rect(option.rect.adjusted(Margin.width(), Margin.height(), -Margin.width(), -Margin.height()));
    ItemLayout const layout(getText(inf), is_item_selected, option.direction, content_rect.topLeft(), content_rect.width());

    painter->save();

    if (is_item_selected) {
        QPalette::ColorGroup cg = is_item_enabled ? QPalette::Normal : QPalette::Disabled;

        if (cg == QPalette::Normal && !is_item_active) {
            cg = QPalette::Inactive;
        }

        painter->fillRect(option.rect, option.palette.brush(cg, QPalette::Highlight));
    }

    tracker_icon
        .paint(painter, layout.icon_rect, Qt::AlignCenter, is_item_selected ? QIcon::Selected : QIcon::Normal, QIcon::On);

    QAbstractTextDocumentLayout::PaintContext paint_context;
    paint_context.clip = layout.text_rect.translated(-layout.text_rect.topLeft());
    paint_context.palette.setColor(
        QPalette::Text,
        option.palette.color(is_item_selected ? QPalette::HighlightedText : QPalette::Text));
    painter->translate(layout.text_rect.topLeft());
    layout.textLayout()->draw(painter, paint_context);

    painter->restore();
}

void TrackerDelegate::setShowMore(bool b)
{
    show_more_ = b;
}

namespace
{
QString timeToRoundedString(int seconds)
{
    if (seconds > 60) {
        seconds -= seconds % 60;
    }

    return Formatter::timeToString(seconds);
}
} // namespace

QString TrackerDelegate::getText(TrackerInfo const& inf) const
{
    QString str;
    auto const err_markup_begin = QStringLiteral("<span style=\"color:red\">");
    auto const err_markup_end = QStringLiteral("</span>");
    auto const timeout_markup_begin = QStringLiteral("<span style=\"color:#224466\">");
    auto const timeout_markup_end = QStringLiteral("</span>");
    auto const success_markup_begin = QStringLiteral("<span style=\"color:#008B00\">");
    auto const success_markup_end = QStringLiteral("</span>");

    auto const now = time(nullptr);
    auto const time_until = [&now](auto t) {
        return timeToRoundedString(static_cast<int>(t - now));
    };
    auto const time_since = [&now](auto t) {
        return timeToRoundedString(static_cast<int>(now - t));
    };

    // hostname
    str += inf.st.is_backup ? QStringLiteral("<i>") : QStringLiteral("<b>");
    auto const announce_url = inf.st.announce.toStdString();
    if (auto const parsed = tr_urlParse(announce_url); parsed) {
        str += QStringLiteral("%1:%2").arg(Utils::qstringFromUtf8(parsed->host)).arg(parsed->port);
    }
    str += inf.st.is_backup ? QStringLiteral("</i>") : QStringLiteral("</b>");

    // announce & scrape info
    if (!inf.st.is_backup) {
        if (inf.st.has_announced && inf.st.announce_state != TR_TRACKER_INACTIVE) {
            auto const tstr = time_since(inf.st.last_announce_time);
            str += QStringLiteral("<br/>\n");

            if (inf.st.last_announce_succeeded) {
                //: {markup_begin} and {markup_end} are replaced with HTML markup, {time_span} is duration
                str += TR_FORMAT_N(
                    "Got a list of{markup_begin} {peer_count:L} peer(s){markup_end} {time_span} ago",
                    inf.st.last_announce_peer_count,
                    fmt::arg("markup_begin", success_markup_begin),
                    fmt::arg("peer_count", inf.st.last_announce_peer_count),
                    fmt::arg("markup_end", success_markup_end),
                    fmt::arg("time_span", tstr));
            } else if (inf.st.last_announce_timed_out) {
                //: {markup_begin} and {markup_end} are replaced with HTML markup, {time_span} is duration
                str += TR_FORMAT(
                    "Peer list request {markup_begin}timed out{markup_end} {time_span} ago; will retry",
                    fmt::arg("markup_begin", timeout_markup_begin),
                    fmt::arg("markup_end", timeout_markup_end),
                    fmt::arg("time_span", tstr));
            } else {
                //: {markup_begin} and {markup_end} are replaced with HTML markup, {error} is error message,
                //: {time_span} is duration
                str += TR_FORMAT(
                    "Got an error {markup_begin}\"{error}\"{markup_end} {time_span} ago",
                    fmt::arg("markup_begin", err_markup_begin),
                    fmt::arg("error", inf.st.last_announce_result),
                    fmt::arg("markup_end", err_markup_end),
                    fmt::arg("time_span", tstr));
            }
        }

        switch (inf.st.announce_state) {
        case TR_TRACKER_INACTIVE:
            str += QStringLiteral("<br/>\n");
            str += tr("No updates scheduled");
            break;

        case TR_TRACKER_WAITING:
            str += QStringLiteral("<br/>\n");
            //: {time_span} is duration
            str += TR_FORMAT(
                "Asking for more peers in {time_span}",
                fmt::arg("time_span", time_until(inf.st.next_announce_time)));
            break;

        case TR_TRACKER_QUEUED:
            str += QStringLiteral("<br/>\n");
            str += tr("Queued to ask for more peers");
            break;

        case TR_TRACKER_ACTIVE:
            str += QStringLiteral("<br/>\n");
            //: {time_span} is duration
            str += TR_FORMAT(
                "Asking for more peers now… <small>{time_span}</small>",
                fmt::arg("time_span", time_since(inf.st.last_announce_start_time)));
            break;

        default:
            break;
        }

        if (!show_more_) {
            return str;
        }

        if (inf.st.has_scraped) {
            str += QStringLiteral("<br/>\n");
            auto const tstr = time_since(inf.st.last_scrape_time);

            if (!inf.st.last_scrape_succeeded) {
                //: {markup_begin} and {markup_end} are replaced with HTML markup, {error} is error message,
                //: {time_span} is duration
                str += TR_FORMAT(
                    "Got a scrape error {markup_begin}\"{error}\"{markup_end} {time_span} ago",
                    fmt::arg("markup_begin", err_markup_begin),
                    fmt::arg("error", inf.st.last_scrape_result),
                    fmt::arg("markup_end", err_markup_end),
                    fmt::arg("time_span", tstr));
            } else if (inf.st.seeder_count >= 0 && inf.st.leecher_count >= 0) {
                //: First part of phrase "Tracker had ... seeder(s) and ... leecher(s) ... ago",
                //: {markup_begin} and {markup_end} are replaced with HTML markup
                str += TR_FORMAT_N(
                    "Tracker had{markup_begin} {seeder_count:L} seeder(s){markup_end}",
                    inf.st.seeder_count,
                    fmt::arg("markup_begin", success_markup_begin),
                    fmt::arg("seeder_count", inf.st.seeder_count),
                    fmt::arg("markup_end", success_markup_end));
                //: Second part of phrase "Tracker had ... seeder(s) and ... leecher(s) ... ago",
                //: {markup_begin} and {markup_end} are replaced with HTML markup, {time_span} is duration;
                //: notice that leading space (before "and") is included here
                str += TR_FORMAT_N(
                    " and{markup_begin} {leecher_count:L} leecher(s){markup_end} {time_span} ago",
                    inf.st.leecher_count,
                    fmt::arg("markup_begin", success_markup_begin),
                    fmt::arg("leecher_count", inf.st.leecher_count),
                    fmt::arg("markup_end", success_markup_end),
                    fmt::arg("time_span", tstr));
            } else {
                //: {markup_begin} and {markup_end} are replaced with HTML markup, {time_span} is duration
                str += TR_FORMAT(
                    "Tracker had {markup_begin}no information{markup_end} on peer counts {time_span} ago",
                    fmt::arg("markup_begin", success_markup_begin),
                    fmt::arg("markup_end", success_markup_end),
                    fmt::arg("time_span", tstr));
            }
        }

        switch (inf.st.scrape_state) {
        case TR_TRACKER_WAITING:
            str += QStringLiteral("<br/>\n");
            //: {time_span} is duration
            str += TR_FORMAT(
                "Asking for peer counts in {time_span}",
                fmt::arg("time_span", time_until(inf.st.next_scrape_time)));
            break;

        case TR_TRACKER_QUEUED:
            str += QStringLiteral("<br/>\n");
            str += tr("Queued to ask for peer counts");
            break;

        case TR_TRACKER_ACTIVE:
            str += QStringLiteral("<br/>\n");
            //: {time_span} is duration
            str += TR_FORMAT(
                "Asking for peer counts now… <small>{time_span}</small>",
                fmt::arg("time_span", time_since(inf.st.last_scrape_start_time)));
            break;

        default: // TR_TRACKER_INACTIVE
            break;
        }
    }

    return str;
}
