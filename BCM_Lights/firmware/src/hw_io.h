// Acesso ao hardware da bancada: GPIOs de entrada, LDR e saídas PWM.
#pragma once

#include "app.h"
#include "bcm_logic.h"

namespace hw {

struct Buttons {
    bool lock = false;    // borda de descida (pressionado) neste ciclo
    bool unlock = false;
    bool flash = false;
};

void begin();
// Lê e filtra as entradas. Chamar a cada ciclo do BCM com o período em ms.
void readInputs(uint32_t period_ms, app::PhysInputs& phys, Buttons& buttons);
void writeOutputs(const bcm::Outputs& out);
void setStatusLed(bool on);

}  // namespace hw
