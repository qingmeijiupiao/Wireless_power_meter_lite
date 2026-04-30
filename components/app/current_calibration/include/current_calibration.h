#ifndef CURRENT_CALIBRATION_H
#define CURRENT_CALIBRATION_H
#include "HXC_NVS.h"
#include "CurrentCalib.h"

namespace CurrentCalib {

constexpr params_t DEFAULT = {
    .current_base_K = 1114,
    .points = {},
    .temperature_K = 0,
};

extern HXC::NVS_DATA<params_t> params_data;

}
#endif