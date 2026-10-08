#pragma once

#include "barcode_label_printing.h"
#include "labelprint/medical_label_print.h"

namespace search {

// Text-only 50 x 30 mm label. Does not add a barcode or barcode number.
labelprint::LabelDocument build_microbiology_label(
    const BarcodeLabelPayload& payload,
    labelprint::MedicalLabelPrinterModel model = labelprint::MedicalLabelPrinterModel::XprinterXp360b);

labelprint::PrintJob render_microbiology_label(
    const labelprint::LabelDocument& doc, const labelprint::PrinterProfile& profile);

}  // namespace search
