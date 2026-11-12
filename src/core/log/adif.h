// One logged QSO as an ADIF record, for the programs gtty reports QSOs to.
#pragma once
#include <string>

#include "core/log/logbook.h"

namespace gtty {

enum class AdifStyle {
  Wsjtx,   // lower-case field names, <EOR>: the WSJT-X "Logged ADIF" UDP message
  Fldigi,  // upper-case field names, <eor>: what fldigi hands to fllog in log.add_record
};

// The fields of `q` followed by the end-of-record tag; no header. Empty fields are left out.
std::string adifRecord(const Qso& q, const std::string& myCall, const std::string& myGrid, AdifStyle style);

}  // namespace gtty
