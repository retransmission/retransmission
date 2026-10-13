// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "FileList.h"

#include "GtkCompat.h"
#include "HigWorkarea.h" // GUI_PAD, GUI_PAD_BIG
#include "IconCache.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "Utils.h"

#include <libtransmission/file-utils.h>
#include <libtransmission/file.h>
#include <libtransmission/string-utils.h>
#include <libtransmission/utils.h>

#include <gtkmm/cellrendererpixbuf.h>
#include <gtkmm/cellrendererprogress.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/cellrenderertoggle.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/treemodel.h>
#include <gtkmm/treemodelcolumn.h>
#include <gtkmm/treerowreference.h>
#include <gtkmm/treeselection.h>
#include <gtkmm/treestore.h>
#include <gtkmm/treeview.h>

#include <giomm/icon.h>

#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/markup.h>
#include <glibmm/miscutils.h>
#include <glibmm/nodetree.h>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <ranges>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std::literals;

namespace
{

enum : uint16_t {
    /* these two fields could be any number at all so long as they're not
     * TR_PRI_LOW, TR_PRI_NORMAL, TR_PRI_HIGH, true, or false */
    NOT_SET = 1000,
    MIXED = 1001
};

class FileModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    FileModelColumns() noexcept
    {
        add(icon);
        add(label);
        add(label_esc);
        add(prog);
        add(prog_str);
        add(index);
        add(size);
        add(size_str);
        add(have);
        add(priority);
        add(enabled);
    }

    Gtk::TreeModelColumn<Glib::RefPtr<Gio::Icon>> icon;
    Gtk::TreeModelColumn<Glib::ustring> label;
    Gtk::TreeModelColumn<Glib::ustring> label_esc;
    Gtk::TreeModelColumn<int> prog;
    Gtk::TreeModelColumn<Glib::ustring> prog_str;
    Gtk::TreeModelColumn<unsigned int> index;
    Gtk::TreeModelColumn<uint64_t> size;
    Gtk::TreeModelColumn<Glib::ustring> size_str;
    Gtk::TreeModelColumn<uint64_t> have;
    Gtk::TreeModelColumn<int> priority;
    Gtk::TreeModelColumn<int> enabled;
};

FileModelColumns const file_cols;

} // namespace

class FileList::Impl : public std::enable_shared_from_this<FileList::Impl>
{
public:
    Impl(
        FileList& widget,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Glib::ustring const& view_name,
        Glib::RefPtr<Session> const& core,
        tr_torrent_id_t torrent_id);
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    ~Impl();

    void set_torrent(tr_torrent_id_t torrent_id);
    void reset_torrent();

private:
    struct SortState {
        int column;
        Gtk::SortType order;
    };

    struct RenameData {
        Gtk::TreeRowReference row;
        tr_torrent_id_t torrent_id;
        Glib::ustring path_string;
        Glib::ustring newname;
        std::optional<int> error = {};
    };

    void clearData();
    void refresh();
    void suspend_sorting();
    void on_editing_started(Gtk::CellEditable* editable, Glib::ustring const& path_string);
    void on_editing_finished();
    bool on_editing_finished_idle();

    bool getAndSelectEventPath(double view_x, double view_y, Gtk::TreeViewColumn*& col, Gtk::TreeModel::Path& path);

    [[nodiscard]] std::vector<tr_file_index_t> getActiveFilesForPath(Gtk::TreeModel::Path const& path) const;
    [[nodiscard]] std::vector<tr_file_index_t> getSelectedFilesAndDescendants() const;
    [[nodiscard]] std::vector<tr_file_index_t> getSubtree(Gtk::TreeModel::Path const& path) const;

    bool onViewButtonPressed(guint button, TrGdkModifierType state, double view_x, double view_y);
    bool onViewPathToggled(Gtk::TreeViewColumn* col, Gtk::TreeModel::Path const& path);
    void onRowActivated(Gtk::TreeModel::Path const& path, Gtk::TreeViewColumn* col);
    void cell_edited_callback(Glib::ustring const& path_string, Glib::ustring const& newname);
    void on_rename_done_idle(uint64_t rename_id, int error);
    void apply_rename_done(RenameData const& rename);

private:
    FileList& widget_;

    Glib::RefPtr<Session> const core_;
    // GtkWidget* top_ = nullptr; // == widget_
    Gtk::TreeView* view_ = nullptr;
    Glib::RefPtr<Gtk::TreeStore> store_;
    tr_torrent_id_t torrent_id_ = {};
    sigc::connection timeout_tag_;
    std::array<sigc::connection, 3> editing_tags_;
    sigc::connection editing_finished_tag_;
    std::optional<Gtk::TreeRowReference> editing_row_;
    std::optional<SortState> suspended_sort_;
    uint64_t next_rename_id_ = 0;
    std::unordered_map<uint64_t, RenameData> pending_renames_;
};

void FileList::Impl::clearData()
{
    torrent_id_ = -1;

    timeout_tag_.disconnect();
    editing_finished_tag_.disconnect();
    editing_row_.reset();
    suspended_sort_.reset();
    pending_renames_.clear();
}

FileList::Impl::~Impl()
{
    for (auto& tag : editing_tags_) {
        tag.disconnect();
    }

    clearData();
}

/***
****
***/

namespace
{

struct FileStats {
    int enabled;
    uint64_t have;
    int priority;
    uint64_t size;
};

struct RefreshData {
    int sort_column_id;
    bool resort_needed;
    tr_torrent* tor;
    Gtk::TreeModel::iterator editing_iter;
    std::optional<FileStats> editing_stats = {};
};

bool refreshFilesForeach(
    Glib::RefPtr<Gtk::TreeStore> const& store,
    Gtk::TreeModel::iterator const& iter,
    RefreshData& refresh_data)
{
    bool const is_file = iter->children().empty();

    auto const old_enabled = iter->get_value(file_cols.enabled);
    auto const old_have = iter->get_value(file_cols.have);
    auto const old_priority = iter->get_value(file_cols.priority);
    auto const old_progress = iter->get_value(file_cols.prog);
    auto const old_size = iter->get_value(file_cols.size);

    auto new_enabled = int{};
    auto new_have = decltype(old_have){};
    auto new_priority = int{};
    auto new_progress = int{};
    auto new_size = decltype(old_size){};

    if (is_file) {
        auto const index = iter->get_value(file_cols.index);
        auto const file = tr_torrentFile(refresh_data.tor, index);

        new_enabled = static_cast<int>(file.wanted);
        new_priority = int{ file.priority };
        new_have = file.have;
        new_size = file.length;
        new_progress = static_cast<int>(100 * file.progress);
    } else {
        new_enabled = NOT_SET;
        new_priority = NOT_SET;

        /* since gtk_tree_model_foreach() is depth-first, we can
         * get the `sub' info by walking the immediate children */

        for (auto const& child : iter->children()) {
            bool const is_editing = refresh_data.editing_stats &&
                TR_GTK_TREE_MODEL_CHILD_ITER(child) == refresh_data.editing_iter;
            auto const child_size = is_editing ? refresh_data.editing_stats->size : child[file_cols.size];
            auto const child_have = is_editing ? refresh_data.editing_stats->have : child[file_cols.have];
            auto const child_priority = is_editing ? refresh_data.editing_stats->priority : child[file_cols.priority];
            auto const child_enabled = is_editing ? refresh_data.editing_stats->enabled : child[file_cols.enabled];

            if (child_enabled != static_cast<int>(false) && (child_enabled != NOT_SET)) {
                new_size += child_size;
                new_have += child_have;
            }

            if (new_enabled == NOT_SET) {
                new_enabled = child_enabled;
            } else if (new_enabled != child_enabled) {
                new_enabled = MIXED;
            }

            if (new_priority == NOT_SET) {
                new_priority = child_priority;
            } else if (new_priority != child_priority) {
                new_priority = MIXED;
            }
        }

        new_progress = new_size != 0 ? static_cast<int>(100.0 * static_cast<double>(new_have) / static_cast<double>(new_size)) :
                                       1;
    }

    new_progress = std::clamp(new_progress, 0, 100);

    if (refresh_data.editing_iter && iter == refresh_data.editing_iter) {
        // GTK cancels editing on any change to this row; ancestors still need its current statistics.
        refresh_data.editing_stats = FileStats{
            .enabled = new_enabled,
            .have = new_have,
            .priority = new_priority,
            .size = new_size,
        };
        return false;
    }

    if (new_priority != old_priority || new_enabled != old_enabled) {
        /* Changing a value in the sort column can trigger a resort
         * which breaks this foreach () call. (See #3529)
         * As a workaround: if that's about to happen, temporarily disable
         * sorting until we finish walking the tree. */
        if (!refresh_data.resort_needed &&
            (((refresh_data.sort_column_id == file_cols.priority.index()) && (new_priority != old_priority)) ||
             ((refresh_data.sort_column_id == file_cols.enabled.index()) && (new_enabled != old_enabled)))) {
            refresh_data.resort_needed = true;

            store->set_sort_column(GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, TR_GTK_SORT_TYPE(ASCENDING));
        }
    }

    if (new_enabled != old_enabled) {
        (*iter)[file_cols.enabled] = new_enabled;
    }

    if (new_priority != old_priority) {
        (*iter)[file_cols.priority] = new_priority;
    }

    if (new_size != old_size) {
        (*iter)[file_cols.size] = new_size;
        (*iter)[file_cols.size_str] = tr_strlsize(new_size);
    }

    if (new_have != old_have) {
        (*iter)[file_cols.have] = new_have;
    }

    if (new_progress != old_progress) {
        (*iter)[file_cols.prog] = new_progress;
        (*iter)[file_cols.prog_str] = fmt::format("{:Ld}%", new_progress);
    }

    return false; /* keep walking */
}

void gtr_tree_model_foreach_postorder(Glib::RefPtr<Gtk::TreeModel> const& model, Gtk::TreeModel::SlotForeachIter const& func)
{
    auto items = std::stack<Gtk::TreeModel::iterator>();
    if (auto const root_child_it = model->children().begin(); root_child_it) {
        items.push(root_child_it);
    }

    while (!items.empty()) {
        while (items.top()) {
            if (auto const child_it = items.top()->children().begin(); child_it) {
                items.push(child_it);
            } else {
                func(items.top()++);
            }
        }

        items.pop();

        if (!items.empty()) {
            func(items.top()++);
        }
    }
}

} // namespace

void FileList::Impl::refresh()
{
    if (tr_torrent* tor = core_->find_torrent(torrent_id_); tor == nullptr) {
        widget_.clear();
    } else {
        if (editing_row_) {
            suspend_sorting();
        }

        Gtk::SortType order = TR_GTK_SORT_TYPE(ASCENDING);
        int sort_column_id = 0;
        store_->get_sort_column_id(sort_column_id, order);

        auto const editing_iter = editing_row_ && editing_row_->is_valid() && editing_row_->get_model() == store_ ?
            store_->get_iter(editing_row_->get_path()) :
            Gtk::TreeModel::iterator{};
        RefreshData refresh_data{
            .sort_column_id = sort_column_id,
            .resort_needed = false,
            .tor = tor,
            .editing_iter = editing_iter,
        };
        gtr_tree_model_foreach_postorder(store_, [this, &refresh_data](Gtk::TreeModel::iterator const& iter) {
            return refreshFilesForeach(store_, iter, refresh_data);
        });

        if (refresh_data.resort_needed) {
            store_->set_sort_column(sort_column_id, order);
        }
    }
}

void FileList::Impl::suspend_sorting()
{
    auto column = int{};
    auto order = TR_GTK_SORT_TYPE(ASCENDING);
    store_->get_sort_column_id(column, order);
    if (!suspended_sort_ || column != GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID) {
        suspended_sort_ = SortState{ column, order };
    }
    store_->set_sort_column(GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, order);
}

void FileList::Impl::on_editing_started(Gtk::CellEditable* /*editable*/, Glib::ustring const& path_string)
{
    editing_finished_tag_.disconnect();
    editing_row_.emplace(store_, Gtk::TreeModel::Path{ path_string });
    suspend_sorting();
}

void FileList::Impl::on_editing_finished()
{
    if (editing_row_ && !editing_finished_tag_.connected()) {
        // GTK must remove the editor before model updates or sorting can resume.
        editing_finished_tag_ = Glib::signal_idle().connect(sigc::mem_fun(*this, &Impl::on_editing_finished_idle));
    }
}

bool FileList::Impl::on_editing_finished_idle()
{
    editing_finished_tag_.disconnect();
    if (!editing_row_) {
        return false;
    }

    auto const store = store_;
    editing_row_.reset();
    suspend_sorting();

    auto completed = std::vector<std::pair<uint64_t, int>>{};
    for (auto const& [id, rename] : pending_renames_) {
        if (rename.error) {
            completed.emplace_back(id, *rename.error);
        }
    }
    for (auto const& [id, error] : completed) {
        on_rename_done_idle(id, error);
    }

    refresh();
    if (store_ == store && !editing_row_ && suspended_sort_) {
        auto const sort = *std::exchange(suspended_sort_, std::nullopt);
        store_->set_sort_column(sort.column, sort.order);
    }
    return false;
}

/***
****
***/

namespace
{

bool getSelectedFilesForeach(
    Gtk::TreeModel::iterator const& iter,
    Glib::RefPtr<Gtk::TreeSelection> const& sel,
    std::vector<tr_file_index_t>& indexBuf)
{
    if (bool const is_file = iter->children().empty(); is_file) {
        /* active means: if it's selected or any ancestor is selected */
        bool is_active = sel->is_selected(iter);

        if (!is_active) {
            for (auto walk = iter->parent(); !is_active && walk; walk = walk->parent()) {
                is_active = sel->is_selected(walk);
            }
        }

        if (is_active) {
            indexBuf.push_back(iter->get_value(file_cols.index));
        }
    }

    return false; /* keep walking */
}

} // namespace

std::vector<tr_file_index_t> FileList::Impl::getSelectedFilesAndDescendants() const
{
    auto const sel = view_->get_selection();
    std::vector<tr_file_index_t> indexBuf;
    store_->foreach_iter(
        [&sel, &indexBuf](Gtk::TreeModel::iterator const& iter) { return getSelectedFilesForeach(iter, sel, indexBuf); });
    return indexBuf;
}

namespace
{

bool getSubtreeForeach(
    Gtk::TreeModel::Path const& path,
    Gtk::TreeModel::iterator const& iter,
    Gtk::TreeModel::Path const& subtree_path,
    std::vector<tr_file_index_t>& indexBuf)
{
    if (bool const is_file = iter->children().empty(); is_file) {
        if (path == subtree_path || path.is_descendant(subtree_path)) {
            indexBuf.push_back(iter->get_value(file_cols.index));
        }
    }

    return false; /* keep walking */
}

} // namespace

std::vector<tr_file_index_t> FileList::Impl::getSubtree(Gtk::TreeModel::Path const& subtree_path) const
{
    std::vector<tr_file_index_t> indexBuf;
    store_->foreach ([&subtree_path, &indexBuf](Gtk::TreeModel::Path const& path, Gtk::TreeModel::iterator const& iter) {
        return getSubtreeForeach(path, iter, subtree_path, indexBuf);
    });
    return indexBuf;
}

/* if `path' is a selected row, all selected rows are returned.
 * otherwise, only the row indicated by `path' is returned.
 * this is for toggling all the selected rows' states in a batch.
 *
 * indexBuf should be large enough to hold tr_inf.fileCount files.
 */
std::vector<tr_file_index_t> FileList::Impl::getActiveFilesForPath(Gtk::TreeModel::Path const& path) const
{
    if (view_->get_selection()->is_selected(path)) {
        /* clicked in a selected row... use the current selection */
        return getSelectedFilesAndDescendants();
    }

    /* clicked OUTSIDE of the selected row... just use the clicked row */
    return getSubtree(path);
}

/***
****
***/

void FileList::clear()
{
    impl_->reset_torrent();
}

namespace
{

struct build_data {
    Gtk::Widget* w = nullptr;
    tr_torrent* tor = nullptr;
    Gtk::TreeStore::iterator iter;
    Glib::RefPtr<Gtk::TreeStore> store;
};

struct row_struct {
    uint64_t length = 0;
    Glib::ustring name;
    int index = 0;
};

using FileRowNode = Glib::NodeTree<row_struct>;

void buildTree(FileRowNode& node, build_data& build)
{
    auto const& child_data = node.data();
    bool const isLeaf = node.child_count() == 0;

    auto const mime_type = isLeaf ? tr_get_mime_type_for_filename(child_data.name.raw()) : DirectoryMimeType;
    auto const icon = gtr_get_mime_type_icon(mime_type);
    auto const file = isLeaf ? tr_torrentFile(build.tor, child_data.index) : tr_file_view{};
    int const priority = isLeaf ? file.priority : 0;
    bool const enabled = isLeaf ? file.wanted : true;
    auto name_esc = Glib::Markup::escape_text(child_data.name);

    auto const child_iter = build.store->prepend(build.iter->children());
    (*child_iter)[file_cols.index] = child_data.index;
    (*child_iter)[file_cols.label] = child_data.name;
    (*child_iter)[file_cols.label_esc] = name_esc;
    (*child_iter)[file_cols.size] = child_data.length;
    (*child_iter)[file_cols.size_str] = tr_strlsize(child_data.length);
    (*child_iter)[file_cols.icon] = icon;
    (*child_iter)[file_cols.priority] = priority;
    (*child_iter)[file_cols.enabled] = static_cast<int>(enabled);

    if (!isLeaf) {
        build_data b = build;
        b.iter = child_iter;
        node.foreach ([&b](auto& child_node) { buildTree(child_node, b); }, TR_GLIB_NODE_TREE_TRAVERSE_FLAGS(FileRowNode, ALL));
    }
}

} // namespace

void FileList::set_torrent(tr_torrent_id_t torrent_id)
{
    impl_->set_torrent(torrent_id);
}

namespace
{
struct PairHash {
    template<typename T1, typename T2>
    auto operator()(std::pair<T1, T2> const& pair) const
    {
        return std::hash<T1>{}(pair.first) ^ std::hash<T2>{}(pair.second);
    }
};
} // namespace

void FileList::Impl::set_torrent(tr_torrent_id_t torrent_id)
{
    if (torrent_id_ == torrent_id && store_ != nullptr && !store_->children().empty()) {
        return;
    }

    /* unset the old fields */
    clearData();

    /* instantiate the model */
    store_ = Gtk::TreeStore::create(file_cols);
    torrent_id_ = torrent_id;

    /* populate the model */
    if (torrent_id_ > 0) {
        if (auto* const tor = core_->find_torrent(torrent_id_); tor != nullptr) {
            // build a GNode tree of the files
            auto root = FileRowNode{};
            auto& root_data = root.data();
            root_data.name = tr_torrentName(tor);
            root_data.index = -1;
            root_data.length = 0;

            auto nodes = std::unordered_map<std::pair<FileRowNode* /*parent*/, std::string_view>, FileRowNode*, PairHash>{};

            for (tr_file_index_t i = 0, n_files = tr_torrentFileCount(tor); i < n_files; ++i) {
                auto* parent = &root;
                auto const file = tr_torrentFile(tor, i);

                auto path = std::string_view{ file.name };
                auto token = std::string_view{};
                while (tr_strv_sep(&path, &token, '/')) {
                    auto*& node = nodes[std::make_pair(parent, token)];

                    if (node == nullptr) {
                        auto const is_leaf = std::empty(path);

                        node = parent->prepend_data({});
                        auto& node_data = node->data();
                        node_data.name = std::string{ token };
                        node_data.index = is_leaf ? static_cast<int>(i) : -1;
                        node_data.length = is_leaf ? file.length : 0;
                    }

                    parent = node;
                }
            }

            // now, add them to the model
            struct build_data build;
            build.w = &widget_;
            build.tor = tor;
            build.store = store_;
            root.foreach (
                [&build](auto& child_node) { buildTree(child_node, build); },
                TR_GLIB_NODE_TREE_TRAVERSE_FLAGS(FileRowNode, ALL));
        }

        refresh();
        timeout_tag_ = Glib::signal_timeout().connect_seconds(
            [this]() { return refresh(), true; },
            SECONDARY_WINDOW_REFRESH_INTERVAL_SECONDS);
    }

    view_->set_model(store_);

    /* set default sort by label */
    store_->set_sort_column(file_cols.label, TR_GTK_SORT_TYPE(ASCENDING));

    view_->expand_row(Gtk::TreeModel::Path("0"), false);
    // view_->expand_all();
}

void FileList::Impl::reset_torrent()
{
    clearData();

    store_ = Gtk::TreeStore::create(file_cols);
    view_->set_model(store_);
}

/***
****
***/

namespace
{

void renderDownload(Gtk::CellRenderer* renderer, Gtk::TreeModel::const_iterator const& iter)
{
    auto* const toggle_renderer = dynamic_cast<Gtk::CellRendererToggle*>(renderer);
    g_assert(toggle_renderer != nullptr);
    if (toggle_renderer == nullptr) {
        return;
    }

    auto const enabled = iter->get_value(file_cols.enabled);
    toggle_renderer->property_inconsistent() = enabled == MIXED;
    toggle_renderer->property_active() = enabled == static_cast<int>(true);
}

void renderPriority(Gtk::CellRenderer* renderer, Gtk::TreeModel::const_iterator const& iter)
{
    auto* const text_renderer = dynamic_cast<Gtk::CellRendererText*>(renderer);
    g_assert(text_renderer != nullptr);
    if (text_renderer == nullptr) {
        return;
    }

    Glib::ustring text;

    switch (auto const priority = iter->get_value(file_cols.priority); priority) {
    case TR_PRI_HIGH:
        text = _("High");
        break;

    case TR_PRI_NORMAL:
        text = _("Normal");
        break;

    case TR_PRI_LOW:
        text = _("Low");
        break;

    default:
        text = _("Mixed");
        break;
    }

    text_renderer->property_text() = text;
}

/* build a filename from tr_torrentGetCurrentDir() + the model's FC_LABELs */
std::string build_filename(tr_torrent const* tor, Gtk::TreeModel::iterator const& iter)
{
    std::vector<std::string> tokens;
    for (auto child = iter; child; child = child->parent()) {
        tokens.push_back(child->get_value(file_cols.label));
    }

    tokens.emplace_back(tr_torrentGetCurrentDir(tor));
    std::ranges::reverse(tokens);
    return Glib::build_filename(tokens);
}

std::optional<std::string> get_filename_to_open(tr_torrent const* tor, Gtk::TreeModel::iterator const& iter)
{
    auto file = Gio::File::create_for_path(build_filename(tor, iter));

    // if the selected file is complete, use it
    if (iter->get_value(file_cols.prog) == 100 && file->query_exists()) {
        return file->get_path();
    }

    // use nearest existing ancestor instead
    for (;;) {
        file = file->get_parent();

        if (!file) {
            return {};
        }

        if (file->query_exists()) {
            return file->get_path();
        }
    }
}
} // namespace

void FileList::Impl::onRowActivated(Gtk::TreeModel::Path const& path, Gtk::TreeViewColumn* /*col*/)
{
    auto const* const tor = core_->find_torrent(torrent_id_);
    if (tor == nullptr) {
        return;
    }

    auto const iter = store_->get_iter(path);
    if (!iter) {
        return;
    }

    if (auto const filename = get_filename_to_open(tor, iter); filename) {
        gtr_open_file(*filename);
    }
}

bool FileList::Impl::onViewPathToggled(Gtk::TreeViewColumn* col, Gtk::TreeModel::Path const& path)
{
    if (col == nullptr || path.empty()) {
        return false;
    }

    bool handled = false;

    auto const cid = col->get_sort_column_id();
    auto* tor = core_->find_torrent(torrent_id_);

    if (tor != nullptr && (cid == file_cols.priority.index() || cid == file_cols.enabled.index())) {
        auto const indexBuf = getActiveFilesForPath(path);

        auto const iter = store_->get_iter(path);

        if (cid == file_cols.priority.index()) {
            auto const old_priority = iter->get_value(file_cols.priority);
            auto new_priority = TR_PRI_NORMAL;

            switch (old_priority) {
            case TR_PRI_NORMAL:
                new_priority = TR_PRI_HIGH;
                break;

            case TR_PRI_HIGH:
                new_priority = TR_PRI_LOW;
                break;

            default:
                new_priority = TR_PRI_NORMAL;
                break;
            }

            tr_torrentSetFilePriorities(tor, indexBuf, new_priority);
        } else {
            auto const enabled = iter->get_value(file_cols.enabled);
            tr_torrentSetFileDLs(tor, indexBuf, enabled == static_cast<int>(false));
        }

        refresh();
        handled = true;
    }

    return handled;
}

/**
 * @note 'col' and 'path' are assumed not to be nullptr.
 */
bool FileList::Impl::getAndSelectEventPath(double view_x, double view_y, Gtk::TreeViewColumn*& col, Gtk::TreeModel::Path& path)
{
    int cell_x = 0;
    int cell_y = 0;

    if (view_->get_path_at_pos(static_cast<int>(view_x), static_cast<int>(view_y), path, col, cell_x, cell_y)) {
        if (auto const sel = view_->get_selection(); !sel->is_selected(path)) {
            sel->unselect_all();
            sel->select(path);
        }

        return true;
    }

    return false;
}

bool FileList::Impl::onViewButtonPressed(guint button, TrGdkModifierType state, double view_x, double view_y)
{
    Gtk::TreeViewColumn* col = nullptr;
    Gtk::TreeModel::Path path;
    bool handled = false;

    if (button == GDK_BUTTON_PRIMARY &&
        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        (state & (TR_GDK_MODIFIED_TYPE(SHIFT_MASK) | TR_GDK_MODIFIED_TYPE(CONTROL_MASK))) == TrGdkModifierType{} &&
        getAndSelectEventPath(view_x, view_y, col, path)) {
        handled = onViewPathToggled(col, path);
    }

    return handled;
}

void FileList::Impl::on_rename_done_idle(uint64_t rename_id, int error)
{
    auto const iter = pending_renames_.find(rename_id);
    if (iter == std::end(pending_renames_)) {
        return;
    }

    auto& rename = iter->second;
    rename.error = error;
    if (editing_row_ &&
        (error != 0 ||
         (editing_row_->is_valid() && rename.row.is_valid() && editing_row_->get_model() == rename.row.get_model() &&
          editing_row_->get_path() == rename.row.get_path()))) {
        return;
    }

    auto const result = std::move(rename);
    pending_renames_.erase(iter);
    apply_rename_done(result);
}

void FileList::Impl::apply_rename_done(RenameData const& rename)
{
    auto const error = rename.error.value();
    auto const& newname = rename.newname;
    if (error == 0) {
        if (rename.torrent_id != torrent_id_ || rename.row.get_model() != store_ || !rename.row.is_valid()) {
            return;
        }
        if (auto const iter = store_->get_iter(rename.row.get_path()); iter) {
            bool const isLeaf = iter->children().empty();
            auto const mime_type = isLeaf ? tr_get_mime_type_for_filename(newname.raw()) : DirectoryMimeType;
            auto const icon = gtr_get_mime_type_icon(mime_type);

            (*iter)[file_cols.label] = newname;
            (*iter)[file_cols.icon] = icon;

            if (!iter->parent()) {
                core_->torrent_changed(rename.torrent_id);
            }
        }
    } else {
        auto w = std::make_shared<Gtk::MessageDialog>(
            gtr_widget_get_window(widget_),
            fmt::format(
                fmt::runtime(_("Couldn't rename '{old_path}' as '{path}': {error} ({error_code})")),
                fmt::arg("old_path", rename.path_string),
                fmt::arg("path", newname),
                fmt::arg("error", tr_strerror(error)),
                fmt::arg("error_code", error)),
            false,
            TR_GTK_MESSAGE_TYPE(ERROR),
            TR_GTK_BUTTONS_TYPE(CLOSE),
            true);
        w->set_secondary_text(_("Please correct the errors and try again."));
        w->signal_response().connect([w](int /*response*/) mutable { w.reset(); });
        w->show();
    }
}

void FileList::Impl::cell_edited_callback(Glib::ustring const& path_string, Glib::ustring const& newname)
{
    on_editing_finished();
    tr_torrent* const tor = core_->find_torrent(torrent_id_);

    if (tor == nullptr) {
        return;
    }

    auto iter = store_->get_iter(path_string);
    if (!iter) {
        return;
    }

    auto const rename_id = ++next_rename_id_;
    pending_renames_.try_emplace(
        rename_id,
        RenameData{
            .row = Gtk::TreeRowReference{ store_, store_->get_path(iter) },
            .torrent_id = torrent_id_,
            .path_string = path_string,
            .newname = newname,
        });

    // Edited-row labels can lag behind renames; use the torrent's current path.
    auto file_iter = iter;
    auto parent_count = 0U;
    while (!file_iter->children().empty()) {
        file_iter = file_iter->children().begin();
        ++parent_count;
    }
    auto oldpath = std::string{ tr_torrentFile(tor, file_iter->get_value(file_cols.index)).name };
    for (auto i = 0U; i < parent_count; ++i) {
        oldpath = tr_sys_path_dirname(oldpath);
    }

    /* do the renaming */
    tr_torrentRenamePath(
        tor,
        oldpath,
        newname.raw(),
        [self = weak_from_this(), rename_id](
            tr_torrent_id_t const /*tor_id*/,
            std::string_view const /*oldpath*/,
            std::string_view const /*newname*/,
            tr_error const& error) {
            Glib::signal_idle().connect([self, rename_id, error_code = error ? error.code() : 0]() {
                if (auto const impl = self.lock(); impl) {
                    impl->on_rename_done_idle(rename_id, error_code);
                }
                return false;
            });
        });
}

FileList::FileList(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::ustring const& view_name,
    Glib::RefPtr<Session> const& core,
    tr_torrent_id_t torrent_id)
    : Gtk::ScrolledWindow(cast_item)
    , impl_(std::make_shared<Impl>(*this, builder, view_name, core, torrent_id))
{
}

FileList::Impl::Impl(
    FileList& widget,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::ustring const& view_name,
    Glib::RefPtr<Session> const& core,
    tr_torrent_id_t torrent_id)
    : widget_(widget)
    , core_(core)
    , view_(gtr_get_widget<Gtk::TreeView>(builder, view_name))
{
    static constexpr auto FileAndSizeScale = 0.8;

    /* create the view */
    view_->signal_row_activated().connect(sigc::mem_fun(*this, &Impl::onRowActivated));
    setup_item_view_button_event_handling(
        *view_,
        [this](guint button, TrGdkModifierType state, double view_x, double view_y, bool /*context_menu_requested*/) {
            return onViewButtonPressed(button, state, view_x, view_y);
        },
        [this](double view_x, double view_y) { return on_item_view_button_released(*view_, view_x, view_y); });

    /* set up view */
    auto const sel = view_->get_selection();
    sel->set_mode(TR_GTK_SELECTION_MODE(MULTIPLE));
    view_->expand_all();
    view_->set_search_column(file_cols.label);

    {
        /* add file column */
        auto* col = Gtk::make_managed<Gtk::TreeViewColumn>();
        col->set_expand(true);
        col->set_title(_("Name"));
        col->set_resizable(true);
        auto* icon_rend = Gtk::make_managed<Gtk::CellRendererPixbuf>();
        col->pack_start(*icon_rend, false);
        col->add_attribute(icon_rend->property_gicon(), file_cols.icon);
#if GTKMM_CHECK_VERSION(4, 0, 0)
        icon_rend->property_icon_size() = Gtk::IconSize::NORMAL;
#else
        icon_rend->property_stock_size() = Gtk::ICON_SIZE_MENU;
#endif
        /* add text renderer */
        auto* text_rend = Gtk::make_managed<Gtk::CellRendererText>();
        text_rend->property_editable() = true;
        text_rend->property_ellipsize() = TR_PANGO_ELLIPSIZE_MODE(END);
        text_rend->property_scale() = FileAndSizeScale;
        editing_tags_ = {
            text_rend->signal_editing_started().connect(sigc::mem_fun(*this, &Impl::on_editing_started)),
            text_rend->signal_editing_canceled().connect(sigc::mem_fun(*this, &Impl::on_editing_finished)),
            text_rend->signal_edited().connect(sigc::mem_fun(*this, &Impl::cell_edited_callback)),
        };
        col->pack_start(*text_rend, true);
        col->set_spacing(GUI_PAD_SMALL);
        col->add_attribute(text_rend->property_text(), file_cols.label);
        col->set_sort_column(file_cols.label);
        view_->append_column(*col);
    }

    {
        /* add "size" column */
        auto* rend = Gtk::make_managed<Gtk::CellRendererText>();
        rend->property_alignment() = TR_PANGO_ALIGNMENT(RIGHT);
        rend->property_scale() = FileAndSizeScale;
        rend->property_xpad() = GUI_PAD;
        rend->property_xalign() = 1.0F;
        auto* col = Gtk::make_managed<Gtk::TreeViewColumn>(_("Size"), *rend);
        col->set_sizing(TR_GTK_TREE_VIEW_COLUMN_SIZING(GROW_ONLY));
        col->set_sort_column(file_cols.size);
        col->add_attribute(rend->property_text(), file_cols.size_str);
        view_->append_column(*col);
    }

    {
        /* add "progress" column */
        auto* rend = Gtk::make_managed<Gtk::CellRendererProgress>();
        auto* col = Gtk::make_managed<Gtk::TreeViewColumn>(_("Progress"), *rend);
        col->add_attribute(rend->property_text(), file_cols.prog_str);
        col->add_attribute(rend->property_value(), file_cols.prog);
        col->set_sizing(TR_GTK_TREE_VIEW_COLUMN_SIZING(FIXED));
        col->set_sort_column(file_cols.prog);
        view_->append_column(*col);
    }

    {
        /* add "enabled" column */
        auto* rend = Gtk::make_managed<Gtk::CellRendererToggle>();
        auto* col = Gtk::make_managed<Gtk::TreeViewColumn>(_("Download"), *rend);
        col->set_sizing(TR_GTK_TREE_VIEW_COLUMN_SIZING(FIXED));
        col->set_cell_data_func(*rend, sigc::ptr_fun(&renderDownload));
        col->set_sort_column(file_cols.enabled);
        view_->append_column(*col);
    }

    {
        /* add priority column */
        auto* rend = Gtk::make_managed<Gtk::CellRendererText>();
        rend->property_xalign() = 0.5F;
        auto* col = Gtk::make_managed<Gtk::TreeViewColumn>(_("Priority"), *rend);
        col->set_sizing(TR_GTK_TREE_VIEW_COLUMN_SIZING(FIXED));
        col->set_sort_column(file_cols.priority);
        col->set_cell_data_func(*rend, sigc::ptr_fun(&renderPriority));
        view_->append_column(*col);
    }

    /* add tooltip to tree */
    view_->set_tooltip_column(file_cols.label_esc.index());

    set_torrent(torrent_id);
}

FileList::~FileList() = default;
