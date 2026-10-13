// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "FileList.h"
#include "GtkCompat.h"
#include "Prefs.h"
#include "Session.h"
#include "Utils.h"

#include "../libtransmission/test-fixtures.h"

#include <libtransmission-app/app.h>

#include <gtkmm/application.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/entry.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/treemodel.h>
#include <gtkmm/treestore.h>
#include <gtkmm/treeview.h>
#include <gtkmm/window.h>

#include <glibmm/main.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace std::literals;

namespace
{

enum : int { LabelColumn = 1, ProgressColumn = 3, SizeColumn = 6, HaveColumn = 8, PriorityColumn = 9, WantedColumn = 10 };

template<typename T>
T row_value(Gtk::TreeModel::iterator const& iter, int column)
{
    auto value = T{};
    iter->get_value(column, value);
    return value;
}

void iterate()
{
    auto const context = Glib::MainContext::get_default();
    while (context->pending()) {
        context->iteration(false);
    }
}

bool wait_for(std::function<bool()> const& condition)
{
    return tr::test::waitFor(
        [&condition]() {
            iterate();
            return condition();
        },
        5s);
}

class FileListTest : public tr::test::SessionTest
{
protected:
    static void SetUpTestSuite()
    {
        tr::test::SessionTest::SetUpTestSuite();
        tr::app::init();
    }

    void SetUp() override
    {
        if (!IF_GTKMM4(gtk_init_check(), gtk_init_check(nullptr, nullptr))) {
            GTEST_SKIP() << "Filename editing tests require a GTK display.";
        }

        if (!application_) {
            application_ = Gtk::Application::create("org.retransmission.FileListTest", TR_GIO_APPLICATION_FLAGS(NON_UNIQUE));
            ASSERT_TRUE(application_->register_application());
        }

        tr::test::SessionTest::SetUp();
        gtr_pref_init(sandboxDir());
        core_ = Session::create(session_);

        auto files = tr_variant::Vector{};
        for (
            auto const& path : std::array{ std::vector<std::string>{ "active.bin" },
                                           std::vector<std::string>{ "sibling.bin" },
                                           std::vector<std::string>{ "nested", "other.bin" } }) {
            auto names = tr_variant::Vector{};
            for (auto const& name : path) {
                names.emplace_back(name);
            }

            auto file = tr_variant::Map{ 2U };
            file.try_emplace(TR_KEY_length, FileSize);
            file.try_emplace(TR_KEY_path, std::move(names));
            files.emplace_back(std::move(file));
        }

        auto const hash = tr_sha1::digest(std::string(TrBlockSize, '\0'));
        auto pieces = std::string{};
        for (auto i = uint64_t{}; i < 3 * FileSize / TrBlockSize; ++i) {
            pieces.append(std::begin(hash), std::end(hash));
        }

        auto info = tr_variant::Map{ 4U };
        info.try_emplace(TR_KEY_files, std::move(files));
        info.try_emplace(TR_KEY_name, "root"s);
        info.try_emplace(TR_KEY_piece_length, TrBlockSize);
        info.try_emplace(TR_KEY_pieces, std::move(pieces));
        auto metainfo = tr_variant::Map{ 1U };
        metainfo.try_emplace(TR_KEY_info, std::move(info));

        auto builder = tr_torrent_builder{ session_ };
        builder.set_paused(true);
        ASSERT_TRUE(builder.set_metainfo(tr_variant_serde::benc().to_string(tr_variant{ std::move(metainfo) })));
        tor_ = createTorrentAndWaitForVerifyDone(&builder);
        ASSERT_NE(nullptr, tor_);

        builder_ = Gtk::Builder::create_from_string(R"(
            <interface>
                <object class="GtkScrolledWindow" id="files_view_scroll"/>
                <object class="GtkTreeView" id="files_view"/>
            </interface>
        )");
        file_list_ = gtr_get_widget_derived<FileList>(builder_, "files_view_scroll", "files_view", core_, tr_torrentId(tor_));
        view_ = gtr_get_widget<Gtk::TreeView>(builder_, "files_view");
        IF_GTKMM4(file_list_->set_child(*view_), file_list_->add(*view_));

        auto* const column = view_->get_column(0);
        for (auto* renderer : column->get_cells()) {
            if (auto* text = dynamic_cast<Gtk::CellRendererText*>(renderer); text != nullptr) {
                text_ = text;
            }
        }
        ASSERT_NE(nullptr, text_);

        tags_ = {
            text_->signal_editing_started().connect(
                [this](Gtk::CellEditable* editable, Glib::ustring const&) { entry_ = dynamic_cast<Gtk::Entry*>(editable); }),
            text_->signal_editing_canceled().connect([this]() {
                entry_ = nullptr;
                ++canceled_;
            }),
            text_->signal_edited().connect([this](Glib::ustring const&, Glib::ustring const&) {
                entry_ = nullptr;
                ++edited_;
            }),
            view_->get_model()->signal_rows_reordered().connect(
                [this](auto const&, auto const&, auto const&) { ++reordered_; }),
        };

        window_ = std::make_unique<Gtk::Window>();
        application_->add_window(*window_);
        window_->set_default_size(640, 320);
        IF_GTKMM4(window_->set_child(*file_list_), window_->add(*file_list_));
        gtr_widget_set_visible(*view_, true);
        gtr_widget_set_visible(*file_list_, true);
        window_->present();
        ASSERT_TRUE(wait_for([this]() { return view_->get_mapped() && view_->get_column(0)->get_width() > 0; }));
    }

    void TearDown() override
    {
        if (application_) {
            for (auto* window : Gtk::Window::list_toplevels()) {
                if (auto* dialog = dynamic_cast<Gtk::MessageDialog*>(window); dialog != nullptr) {
                    dialog->response(TR_GTK_RESPONSE_TYPE(CLOSE));
                }
            }
        }

        destroy_view();
        if (core_) {
            core_->close();
        }
        tr::test::SessionTest::TearDown();
        iterate();
        core_.reset();
    }

    void destroy_view()
    {
        for (auto& tag : tags_) {
            tag.disconnect();
        }
        window_.reset();
        builder_.reset();
        file_list_ = nullptr;
        view_ = nullptr;
        text_ = nullptr;
        entry_ = nullptr;
    }

    Gtk::TreeModel::iterator find_row(Glib::ustring const& name) const
    {
        auto found = Gtk::TreeModel::iterator{};
        view_->get_model()->foreach_iter([&](Gtk::TreeModel::iterator const& iter) {
            if (row_value<Glib::ustring>(iter, LabelColumn) == name) {
                found = iter;
                return true;
            }
            return false;
        });
        return found;
    }

    bool start_edit(Glib::ustring const& name)
    {
        auto const iter = find_row(name);
        if (!iter) {
            ADD_FAILURE() << "Missing filename row: " << name;
            return false;
        }

        auto const path = view_->get_model()->get_path(iter);
        view_->expand_to_path(path);
        view_->set_cursor(path, *view_->get_column(0), *text_, true);
        auto const started = wait_for([this]() { return entry_ != nullptr && text_->property_editing(); });
        if (!started) {
            ADD_FAILURE() << "GTK did not create a filename editor; editing: " << static_cast<bool>(text_->property_editing())
                          << ", column width: " << view_->get_column(0)->get_width();
        }
        return started;
    }

    bool editor_has_focus() const
    {
        auto* const focus = window_->get_focus();
        return entry_ != nullptr && focus != nullptr && (focus == entry_ || focus->is_ancestor(*entry_));
    }

    void cancel_edit()
    {
        auto* const entry = entry_;
        entry->property_editing_canceled() = true;
        entry->editing_done();
        entry->remove_widget();
        iterate();
    }

    void commit_edit()
    {
        g_signal_emit_by_name(entry_->gobj(), "activate");
    }

    void add_data(std::initializer_list<std::pair<tr_file_index_t, size_t>> files)
    {
        for (auto const& [index, size] : files) {
            auto const filename = tr_pathbuf{ tr_sessionGetDownloadDir(session_), '/', tr_torrentFile(tor_, index).name };
            auto const data = std::string(size, '\0');
            createFileWithContents(filename.sv(), std::data(data), std::size(data));
        }
        blockingTorrentVerify(tor_);
    }

    void flush_rename()
    {
        blockingRunInSessionThread([]() {});
        iterate();
    }

    Gtk::MessageDialog* error_dialog() const
    {
        for (auto* window : Gtk::Window::list_toplevels()) {
            if (auto* dialog = dynamic_cast<Gtk::MessageDialog*>(window); dialog != nullptr && dialog->get_visible()) {
                return dialog;
            }
        }
        return nullptr;
    }

    static constexpr uint64_t FileSize = 64 * 1024 * 1024;
    inline static Glib::RefPtr<Gtk::Application> application_;
    Glib::RefPtr<Session> core_;
    tr_torrent* tor_ = nullptr;
    Glib::RefPtr<Gtk::Builder> builder_;
    std::unique_ptr<Gtk::Window> window_;
    FileList* file_list_ = nullptr;
    Gtk::TreeView* view_ = nullptr;
    Gtk::CellRendererText* text_ = nullptr;
    Gtk::Entry* entry_ = nullptr;
    std::array<sigc::connection, 4> tags_;
    unsigned int canceled_ = 0;
    unsigned int edited_ = 0;
    unsigned int reordered_ = 0;
};

TEST_F(FileListTest, keepsUntouchedEditWhileOtherRowsRefresh)
{
    ASSERT_TRUE(start_edit("active.bin"));
    auto* const entry = entry_;
    auto const name = entry_->get_text();

    add_data({ { 0, TrBlockSize }, { 1, 2 * TrBlockSize }, { 2, TrBlockSize } });
    ASSERT_EQ(0, static_cast<int>(100 * tr_torrentFile(tor_, 0).progress));
    ASSERT_TRUE(wait_for([this]() { return row_value<uint64_t>(find_row("sibling.bin"), HaveColumn) == 2 * TrBlockSize; }));

    ASSERT_TRUE(text_->property_editing());
    ASSERT_EQ(entry, entry_);
    EXPECT_TRUE(editor_has_focus());
    EXPECT_EQ(name, entry_->get_text());
    EXPECT_EQ(0U, canceled_);
    EXPECT_EQ(0U, row_value<uint64_t>(find_row("active.bin"), HaveColumn));
    EXPECT_EQ(4 * TrBlockSize, row_value<uint64_t>(find_row("root"), HaveColumn));
    EXPECT_EQ(TrBlockSize, row_value<uint64_t>(find_row("nested"), HaveColumn));

    cancel_edit();
    EXPECT_EQ(1U, canceled_);
    EXPECT_EQ(0U, edited_);
    EXPECT_EQ(TrBlockSize, row_value<uint64_t>(find_row("active.bin"), HaveColumn));
}

TEST_F(FileListTest, keepsDirectoryDraftAndCurrentAncestorTotals)
{
    ASSERT_TRUE(start_edit("nested"));
    entry_->set_text("directory-draft");
    entry_->select_region(1, 5);
    auto* const entry = entry_;
    auto const position = entry_->get_position();
    int selection_begin = 0;
    int selection_end = 0;
    ASSERT_TRUE(entry_->get_selection_bounds(selection_begin, selection_end));

    add_data({ { 1, 1024 * 1024 }, { 2, 2 * 1024 * 1024 } });
    ASSERT_TRUE(wait_for([this]() { return row_value<uint64_t>(find_row("other.bin"), HaveColumn) == 2 * 1024 * 1024; }));

    ASSERT_TRUE(text_->property_editing());
    ASSERT_EQ(entry, entry_);
    EXPECT_TRUE(editor_has_focus());
    EXPECT_EQ("directory-draft", entry_->get_text());
    EXPECT_EQ(position, entry_->get_position());
    int begin = 0;
    int end = 0;
    ASSERT_TRUE(entry_->get_selection_bounds(begin, end));
    EXPECT_EQ(selection_begin, begin);
    EXPECT_EQ(selection_end, end);
    EXPECT_EQ(0U, canceled_);
    EXPECT_EQ(0U, row_value<uint64_t>(find_row("nested"), HaveColumn));
    EXPECT_EQ(3 * 1024 * 1024, row_value<uint64_t>(find_row("root"), HaveColumn));

    commit_edit();
    ASSERT_TRUE(wait_for([this]() { return bool{ find_row("directory-draft") }; }));
    EXPECT_EQ(1U, edited_);
    EXPECT_EQ(0U, canceled_);
    EXPECT_EQ(2 * 1024 * 1024, row_value<uint64_t>(find_row("directory-draft"), HaveColumn));
    EXPECT_EQ("root/directory-draft/other.bin"sv, std::string_view{ tr_torrentFile(tor_, 2).name });
}

class FileListSortingTest
    : public FileListTest
    , public ::testing::WithParamInterface<int>
{
};

TEST_P(FileListSortingTest, keepsEditAndLiveRowsWithoutAutomaticReordering)
{
    auto const model = view_->get_model();
    auto const store = gtr_ptr_dynamic_cast<Gtk::TreeStore>(model);
    ASSERT_TRUE(store);
    store->set_sort_column(GetParam(), TR_GTK_SORT_TYPE(ASCENDING));
    ASSERT_TRUE(start_edit("active.bin"));
    auto const path = model->get_path(find_row("active.bin"));
    reordered_ = 0;

    add_data({ { 0, 1024 * 1024 }, { 1, 2 * 1024 * 1024 }, { 2, 1024 * 1024 } });
    auto const high = std::array<tr_file_index_t, 1>{ 1 };
    auto const unwanted = std::array<tr_file_index_t, 1>{ 2 };
    tr_torrentSetFilePriorities(tor_, high, TR_PRI_HIGH);
    tr_torrentSetFileDLs(tor_, unwanted, false);
    ASSERT_TRUE(wait_for([this]() {
        return row_value<uint64_t>(find_row("sibling.bin"), HaveColumn) == 2 * 1024 * 1024 &&
            row_value<int>(find_row("sibling.bin"), PriorityColumn) == TR_PRI_HIGH &&
            row_value<int>(find_row("other.bin"), WantedColumn) == 0;
    }));

    ASSERT_TRUE(text_->property_editing());
    EXPECT_TRUE(editor_has_focus());
    EXPECT_EQ(0U, canceled_);
    EXPECT_EQ(0U, reordered_);
    EXPECT_EQ(path, model->get_path(find_row("active.bin")));
    EXPECT_EQ(0U, row_value<uint64_t>(find_row("active.bin"), HaveColumn));
    EXPECT_EQ(3 * 1024 * 1024, row_value<uint64_t>(find_row("root"), HaveColumn));
    EXPECT_EQ(0U, row_value<uint64_t>(find_row("nested"), SizeColumn));

    cancel_edit();
    int column = 0;
    Gtk::SortType order = TR_GTK_SORT_TYPE(DESCENDING);
    ASSERT_TRUE(store->get_sort_column_id(column, order));
    EXPECT_EQ(GetParam(), column);
    EXPECT_EQ(TR_GTK_SORT_TYPE(ASCENDING), order);
    EXPECT_EQ(1024 * 1024, row_value<uint64_t>(find_row("active.bin"), HaveColumn));
}

INSTANTIATE_TEST_SUITE_P(
    SortColumns,
    FileListSortingTest,
    ::testing::Values(ProgressColumn, SizeColumn, PriorityColumn, WantedColumn));

TEST_F(FileListTest, retainsAnExplicitSortChoiceWhenEditingEnds)
{
    auto const store = gtr_ptr_dynamic_cast<Gtk::TreeStore>(view_->get_model());
    ASSERT_TRUE(start_edit("active.bin"));
    store->set_sort_column(PriorityColumn, TR_GTK_SORT_TYPE(DESCENDING));
    iterate();

    EXPECT_FALSE(text_->property_editing());
    int column = 0;
    Gtk::SortType order = TR_GTK_SORT_TYPE(ASCENDING);
    ASSERT_TRUE(store->get_sort_column_id(column, order));
    EXPECT_EQ(PriorityColumn, column);
    EXPECT_EQ(TR_GTK_SORT_TYPE(DESCENDING), order);
}

TEST_F(FileListTest, renamesTheCorrectRowAfterRestoringProgressSorting)
{
    auto const model = view_->get_model();
    auto const store = gtr_ptr_dynamic_cast<Gtk::TreeStore>(model);
    store->set_sort_column(ProgressColumn, TR_GTK_SORT_TYPE(ASCENDING));
    ASSERT_TRUE(start_edit("active.bin"));
    auto const row = find_row("active.bin");
    auto const path = model->get_path(row);
    add_data({ { 0, 4 * 1024 * 1024 }, { 1, 2 * 1024 * 1024 } });
    ASSERT_TRUE(wait_for([this]() { return row_value<uint64_t>(find_row("sibling.bin"), HaveColumn) == 2 * 1024 * 1024; }));
    ASSERT_TRUE(text_->property_editing());

    entry_->set_text("zulu.bin");
    commit_edit();
    ASSERT_TRUE(wait_for([&row]() { return row_value<Glib::ustring>(row, LabelColumn) == "zulu.bin"; }));

    EXPECT_NE(path, model->get_path(row));
    EXPECT_TRUE(bool{ find_row("nested") });
    EXPECT_TRUE(bool{ find_row("sibling.bin") });
    EXPECT_EQ("root/zulu.bin"sv, std::string_view{ tr_torrentFile(tor_, 0).name });
    EXPECT_EQ(0U, canceled_);
    EXPECT_EQ(1U, edited_);
}

TEST_F(FileListTest, defersRenameCompletionDuringAnotherEditOfTheSameRow)
{
    ASSERT_TRUE(start_edit("active.bin"));
    entry_->set_text("zulu.bin");
    commit_edit();
    ASSERT_TRUE(start_edit("active.bin"));
    auto* const entry = entry_;
    flush_rename();
    ASSERT_TRUE(wait_for([this]() { return std::string_view{ tr_torrentFile(tor_, 0).name } == "root/zulu.bin"sv; }));
    flush_rename();

    ASSERT_TRUE(text_->property_editing());
    ASSERT_EQ(entry, entry_);
    EXPECT_TRUE(editor_has_focus());
    EXPECT_EQ(0U, canceled_);
    EXPECT_TRUE(bool{ find_row("active.bin") });
    EXPECT_FALSE(bool{ find_row("zulu.bin") });

    cancel_edit();
    EXPECT_TRUE(bool{ find_row("zulu.bin") });
    EXPECT_TRUE(bool{ find_row("sibling.bin") });
}

TEST_F(FileListTest, commitsAnotherFileRenameWhileTheEarlierResultIsDeferred)
{
    ASSERT_TRUE(start_edit("active.bin"));
    entry_->set_text("first.bin");
    commit_edit();
    ASSERT_TRUE(start_edit("active.bin"));
    flush_rename();
    ASSERT_TRUE(wait_for([this]() { return std::string_view{ tr_torrentFile(tor_, 0).name } == "root/first.bin"sv; }));
    flush_rename();
    ASSERT_TRUE(text_->property_editing());

    entry_->set_text("second.bin");
    commit_edit();
    ASSERT_TRUE(wait_for([this]() { return bool{ find_row("second.bin") }; }));
    EXPECT_EQ("root/second.bin"sv, std::string_view{ tr_torrentFile(tor_, 0).name });
    EXPECT_EQ(nullptr, error_dialog());
    EXPECT_EQ(0U, canceled_);
    EXPECT_EQ(2U, edited_);
}

TEST_F(FileListTest, commitsAnotherDirectoryRenameWhileTheEarlierResultIsDeferred)
{
    ASSERT_TRUE(start_edit("nested"));
    entry_->set_text("first-directory");
    commit_edit();
    ASSERT_TRUE(start_edit("nested"));
    flush_rename();
    ASSERT_TRUE(
        wait_for([this]() { return std::string_view{ tr_torrentFile(tor_, 2).name } == "root/first-directory/other.bin"sv; }));
    flush_rename();
    ASSERT_TRUE(text_->property_editing());

    entry_->set_text("second-directory");
    commit_edit();
    ASSERT_TRUE(wait_for([this]() { return bool{ find_row("second-directory") }; }));
    EXPECT_EQ("root/second-directory/other.bin"sv, std::string_view{ tr_torrentFile(tor_, 2).name });
    EXPECT_EQ(nullptr, error_dialog());
    EXPECT_EQ(0U, canceled_);
    EXPECT_EQ(2U, edited_);
}

TEST_F(FileListTest, reportsEarlierRenameFailureAfterTheActiveEditEnds)
{
    ASSERT_TRUE(start_edit("active.bin"));
    entry_->set_text("invalid/name");
    commit_edit();
    ASSERT_TRUE(start_edit("active.bin"));
    flush_rename();

    ASSERT_TRUE(text_->property_editing());
    EXPECT_TRUE(editor_has_focus());
    EXPECT_EQ(nullptr, error_dialog());
    EXPECT_EQ(0U, canceled_);

    cancel_edit();
    ASSERT_TRUE(wait_for([this]() { return error_dialog() != nullptr; }));
    EXPECT_EQ("root/active.bin"sv, std::string_view{ tr_torrentFile(tor_, 0).name });
    error_dialog()->response(TR_GTK_RESPONSE_TYPE(CLOSE));
}

TEST_F(FileListTest, doesNotRestoreStaleEditingStateAfterClear)
{
    ASSERT_TRUE(start_edit("active.bin"));
    auto* const entry = entry_;
    entry->property_editing_canceled() = true;
    entry->editing_done();
    entry->remove_widget();
    file_list_->clear();
    file_list_->set_torrent(tr_torrentId(tor_));
    ASSERT_TRUE(start_edit("active.bin"));
    iterate();

    ASSERT_TRUE(text_->property_editing());
    EXPECT_TRUE(editor_has_focus());
    cancel_edit();
}

TEST_F(FileListTest, safelyDestroysTheViewWithARenamePending)
{
    ASSERT_TRUE(start_edit("active.bin"));
    entry_->set_text("zulu.bin");
    commit_edit();
    destroy_view();
    flush_rename();
}

} // namespace
