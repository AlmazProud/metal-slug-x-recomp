// Puente C -> ymfm para el YM2610 (ver ym2610.h). Único archivo C++ del runtime: ymfm está
// escrito en C++ y se usa sin modificar.
#include "ym2610.h"

#include "ymfm_opn.h"

#include <algorithm>
#include <vector>

namespace {

class Interfaz : public ymfm::ymfm_interface
{
public:
	void vencido(uint32_t n) { m_engine->engine_timer_expired(n); }

	void ymfm_set_timer(uint32_t n, int32_t cycles) override { ng_ym_timer(n, cycles); }
	void ymfm_set_busy_end(uint32_t cycles) override { ng_ym_busy_until(cycles); }
	bool ymfm_is_busy() override { return ng_ym_busy() != 0; }
	void ymfm_update_irq(bool active) override { ng_ym_irq(active ? 1 : 0); }
	uint8_t ymfm_external_read(ymfm::access_class tipo, uint32_t direccion) override
	{
		// En la Neo Geo, ADPCM-A y ADPCM-B leen la misma ROM V cuando el cartucho no trae ROMs
		// separadas para ADPCM-B (como mslugx).
		if (tipo == ymfm::ACCESS_ADPCM_A || tipo == ymfm::ACCESS_ADPCM_B)
			return ng_ym_rom(direccion);
		return 0;
	}
};

Interfaz interfaz;
ymfm::ym2610 chip(interfaz);

}

extern "C" {

void ym_reset(void)
{
	chip.set_fidelity(ymfm::OPN_FIDELITY_MED);
	chip.reset();
}

uint8_t ym_read(uint32_t offset) { return chip.read(offset & 3); }
void ym_write(uint32_t offset, uint8_t dato) { chip.write(offset & 3, dato); }
void ym_timer_expired(uint32_t n) { interfaz.vencido(n); }

uint32_t ym_state_size(void)
{
	std::vector<uint8_t> b;
	ymfm::ymfm_saved_state st(b, true);
	chip.save_restore(st);
	return (uint32_t)b.size();
}

void ym_state_save(uint8_t *destino)
{
	std::vector<uint8_t> b;
	ymfm::ymfm_saved_state st(b, true);
	chip.save_restore(st);
	std::copy(b.begin(), b.end(), destino);
}

void ym_state_load(const uint8_t *data, uint32_t n)
{
	std::vector<uint8_t> b(data, data + n);
	ymfm::ymfm_saved_state st(b, false);
	chip.save_restore(st);
}

void ym_generate(int32_t output[3])
{
	ymfm::ym2610::output_data o;
	chip.generate(&o, 1);
	for (int i = 0; i < 3; i++)
		output[i] = o.data[i];
}

}
