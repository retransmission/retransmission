#include "Percents.h"

#include "libtransmission/utils.h"

#include <string>

std::string Percents::to_string() const
{
    return tr_strpercent(raw_value_ / 100.);
}
