// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

// uint8_t: underlying type of the gtkmm3-only Change enum below
#include "GtkCompat.h"

#include <glibmm/objectbase.h>
#include <glibmm/refptr.h>

#include <cstdint> // IWYU pragma: keep

#if GTKMM_CHECK_VERSION(4, 0, 0)
#include <gtkmm/filter.h>
#else
#include <glibmm/object.h>
#endif

template<typename T>
class FilterBase : public IF_GTKMM4(Gtk::Filter, Glib::Object)
{
#if !GTKMM_CHECK_VERSION(4, 0, 0)
public:
    enum class Change : uint8_t {
        DIFFERENT,
        LESS_STRICT,
        MORE_STRICT,
    };
#endif

public:
    FilterBase() = default;
    FilterBase(FilterBase&&) = delete;
    FilterBase(FilterBase const&) = delete;
    FilterBase& operator=(FilterBase&&) = delete;
    FilterBase& operator=(FilterBase const&) = delete;
    ~FilterBase() override = default;

    virtual bool match(T const& item) const = 0;

    virtual bool matches_all() const;
    virtual bool matches_none() const;

#if !GTKMM_CHECK_VERSION(4, 0, 0)
    sigc::signal<void(Change)>& signal_changed();
#endif

protected:
#if GTKMM_CHECK_VERSION(4, 0, 0)
    // Gtk::Filter
    bool match_vfunc(Glib::RefPtr<Glib::ObjectBase> const& object) override;
    Match get_strictness_vfunc() override;
#else
    void changed(Change change);
#endif

private:
#if !GTKMM_CHECK_VERSION(4, 0, 0)
    sigc::signal<void(Change)> signal_changed_;
#endif
};
