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

	void ymfm_set_timer(uint32_t n, int32_t ciclos) override { ng_ym_temporizador(n, ciclos); }
	void ymfm_set_busy_end(uint32_t ciclos) override { ng_ym_ocupado_hasta(ciclos); }
	bool ymfm_is_busy() override { return ng_ym_ocupado() != 0; }
	void ymfm_update_irq(bool activa) override { ng_ym_irq(activa ? 1 : 0); }
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

void ym_reiniciar(void)
{
	chip.set_fidelity(ymfm::OPN_FIDELITY_MED);
	chip.reset();
}

uint8_t ym_leer(uint32_t offset) { return chip.read(offset & 3); }
void ym_escribir(uint32_t offset, uint8_t dato) { chip.write(offset & 3, dato); }
void ym_temporizador_vencido(uint32_t n) { interfaz.vencido(n); }

uint32_t ym_estado_tam(void)
{
	std::vector<uint8_t> b;
	ymfm::ymfm_saved_state st(b, true);
	chip.save_restore(st);
	return (uint32_t)b.size();
}

void ym_estado_guardar(uint8_t *destino)
{
	std::vector<uint8_t> b;
	ymfm::ymfm_saved_state st(b, true);
	chip.save_restore(st);
	std::copy(b.begin(), b.end(), destino);
}

void ym_estado_cargar(const uint8_t *datos, uint32_t n)
{
	std::vector<uint8_t> b(datos, datos + n);
	ymfm::ymfm_saved_state st(b, false);
	chip.save_restore(st);
}

void ym_generar(int32_t salida[3])
{
	ymfm::ym2610::output_data o;
	chip.generate(&o, 1);
	for (int i = 0; i < 3; i++)
		salida[i] = o.data[i];
}

}
