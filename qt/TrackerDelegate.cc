// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "TrackerDelegate.h"

#include <algorithm>
#include <ctime> // time()
#include <iterator> // std::data(), std::size()
#include <span>

#include <QtGui/QAbstractTextDocumentLayout>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>
#include <QtGui/QTextDocument>

#include <QtWidgets/QApplication>

#include <libtransmission/types.h>
#include <libtransmission/web-utils.h>

#include <libtransmission-app/favicon-cache.h>
#include <libtransmission-app/formatters.h>

#include "Torrent.h"
#include "TrackerModel.h"
#include "TrFormat.h" // fmt::formatter<QString>
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
auto constexpr StatusMarkup = tr::app::TrackerStatusMarkup{
    .success_begin = R"(<span style="color:#008B00">)",
    .success_end = "</span>",
    .timeout_begin = R"(<span style="color:#224466">)",
    .timeout_end = "</span>",
    .error_begin = R"(<span style="color:red">)",
    .error_end = "</span>",
};

[[nodiscard]] tr_tracker_view toTrackerView(TrackerStat const& st)
{
    // Copies as much of `str` as fits, keeping room for the '\0'.
    auto const copy = [](QString const& str, std::span<char> const buf) {
        *fmt::format_to_n(std::data(buf), std::size(buf) - 1U, "{}", str).out = '\0';
    };

    auto view = tr_tracker_view{};
    copy(st.last_announce_result, view.lastAnnounceResult);
    copy(st.last_scrape_result, view.lastScrapeResult);
    view.lastAnnounceStartTime = st.last_announce_start_time;
    view.lastAnnounceTime = st.last_announce_time;
    view.nextAnnounceTime = st.next_announce_time;
    view.lastScrapeStartTime = st.last_scrape_start_time;
    view.lastScrapeTime = st.last_scrape_time;
    view.nextScrapeTime = st.next_scrape_time;
    view.lastAnnouncePeerCount = static_cast<size_t>(std::max(st.last_announce_peer_count, 0));
    view.leecherCount = st.leecher_count;
    view.seederCount = st.seeder_count;
    view.announceState = static_cast<tr_tracker_state>(st.announce_state);
    view.scrapeState = static_cast<tr_tracker_state>(st.scrape_state);
    view.hasAnnounced = st.has_announced;
    view.hasScraped = st.has_scraped;
    view.lastAnnounceSucceeded = st.last_announce_succeeded;
    view.lastAnnounceTimedOut = st.last_announce_timed_out;
    view.lastScrapeSucceeded = st.last_scrape_succeeded;
    return view;
}
} // namespace

QString TrackerDelegate::getText(TrackerInfo const& inf) const
{
    QString str;

    // hostname
    str += inf.st.is_backup ? QStringLiteral("<i>") : QStringLiteral("<b>");
    auto const announce_url = inf.st.announce.toStdString();
    if (auto const parsed = tr_urlParse(announce_url); parsed) {
        str += QStringLiteral("%1:%2").arg(Utils::qstringFromUtf8(parsed->host), QString::number(parsed->port));
    }
    str += inf.st.is_backup ? QStringLiteral("</i>") : QStringLiteral("</b>");

    // announce & scrape info
    if (!inf.st.is_backup) {
        for (auto const& line : tr::app::tracker_status_lines(toTrackerView(inf.st), time(nullptr), show_more_, StatusMarkup)) {
            str += QStringLiteral("<br/>\n");
            str += QString::fromStdString(line);
        }
    }

    return str;
}
