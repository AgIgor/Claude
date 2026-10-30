// Executa a lógica C++ do BCM a partir de um cenário em texto (stdin) e
// imprime o estado a cada passo (stdout). Usado por tools/check_parity.py
// para comparar bit a bit com a referência em Python.
//
// Entrada:
//   CFG chave=valor chave=valor ...
//   S <dt> <ign 0-2> <switch OFF|PARK|LOW|AUTO> <lux> <portas 0000> <bateria> <eventos L/U/F ou ->
// Saída (doubles como bits hex, para comparação exata):
//   O <t> <modo> <mode_timer> <locked> <dark> <low> <park> <puddle> <interior> <ind> <relock> <int_timer> <saver> <isaver>
//   L <t> <mensagem>
#define __USE_MINGW_ANSI_STDIO 1
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bcm_logic.h"

using namespace bcm;

static void hex(double v) {
    uint64_t bits;
    memcpy(&bits, &v, sizeof(bits));
    printf(" %016llx", static_cast<unsigned long long>(bits));
}

static void onLog(double t, const char* msg, void*) {
    printf("L");
    hex(t);
    printf(" %s\n", msg);
}

int main() {
    Config cfg;
    BCM bcm(&cfg);
    bcm.setLogger(onLog, nullptr);
    Inputs in;

    static char line[4096];
    while (fgets(line, sizeof(line), stdin)) {
        if (strncmp(line, "CFG", 3) == 0) {
            for (char* tok = strtok(line + 3, " \r\n"); tok; tok = strtok(nullptr, " \r\n")) {
                char* eq = strchr(tok, '=');
                if (!eq) continue;
                *eq = '\0';
                const ConfigField* f = findConfigField(tok);
                if (!f) {
                    fprintf(stderr, "campo desconhecido: %s\n", tok);
                    return 2;
                }
                setConfigValue(cfg, *f, strtod(eq + 1, nullptr));
            }
            bcm.reset();
            continue;
        }
        if (line[0] != 'S') continue;

        double dt, lux, batt;
        int ign;
        char sw[8], doors[8], ev[16];
        if (sscanf(line, "S %lf %d %7s %lf %7s %lf %15s", &dt, &ign, sw, &lux, doors, &batt, ev) != 7) {
            fprintf(stderr, "linha inválida: %s", line);
            return 2;
        }
        in.ignition = static_cast<Ignition>(ign);
        parseSwitch(sw, &in.light_switch);
        in.ambient_lux = lux;
        for (int i = 0; i < 4; ++i) in.doors[i] = doors[i] == '1';
        in.battery_v = batt;
        for (const char* p = ev; *p; ++p) {
            if (*p == 'L') bcm.pressLock();
            else if (*p == 'U') bcm.pressUnlock();
            else if (*p == 'F') bcm.flashHighBeam();
        }

        const Outputs& o = bcm.step(dt, in);
        printf("O");
        hex(bcm.t);
        printf(" %s", modeName(bcm.mode));
        hex(bcm.mode_timer);
        printf(" %d %d", bcm.locked, bcm.dark);
        hex(o.low_beam);
        hex(o.park_lamps);
        hex(o.puddle);
        hex(o.interior);
        printf(" %d", o.indicators);
        hex(bcm.relock_timer);
        hex(bcm.interior_timer);
        printf(" %d %d\n", bcm.saver_tripped, bcm.interior_saver_tripped);
    }
    return 0;
}
