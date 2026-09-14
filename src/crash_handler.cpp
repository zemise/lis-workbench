#include "crash_handler.h"

// Even MiniDumpNormal can capture credentials and patient data. Do not write
// application memory dumps by default. OS WER policy is managed separately.
void install_crash_handler() {}
