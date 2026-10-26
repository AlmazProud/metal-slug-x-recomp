// C -> ymfm bridge for the YM2610 (see ym2610.h). The only C++ file of the runtime: ymfm is
// written in C++ and is used unmodified.
#include "ym2610.h"

#include "ymfm_opn.h"

#include <algorithm>
#include <vector>

namespace {

class Interface : public ymfm::ymfm_interface
{
public:
	void expired(uint32_t n) { m_engine->engine_timer_expired(n); }

	void ymfm_set_timer(uint32_t n, int32_t cycles) override { ng_ym_timer(n, cycles); }
	void ymfm_set_busy_end(uint32_t cycles) override { ng_ym_busy_until(cycles); }
	bool ymfm_is_busy() override { return ng_ym_busy() != 0; }
	void ymfm_update_irq(bool active) override { ng_ym_irq(active ? 1 : 0); }
	uint8_t ymfm_external_read(ymfm::access_class type, uint32_t address) override
	{
		// On the Neo Geo, ADPCM-A and ADPCM-B read the same V ROM when the cartridge has no
		// separate ADPCM-B ROMs (like mslugx).
		if (type == ymfm::ACCESS_ADPCM_A || type == ymfm::ACCESS_ADPCM_B)
			return ng_ym_rom(address);
		return 0;
	}
};

Interface iface;
ymfm::ym2610 chip(iface);

}

extern "C" {

void ym_reset(void)
{
	chip.set_fidelity(ymfm::OPN_FIDELITY_MED);
	chip.reset();
}

void ym_ssg_clock(uint32_t count) { chip.ssg_clock(count); }

uint8_t ym_read(uint32_t offset) { return chip.read(offset & 3); }
void ym_write(uint32_t offset, uint8_t data) { chip.write(offset & 3, data); }
void ym_timer_expired(uint32_t n) { iface.expired(n); }

uint32_t ym_state_size(void)
{
	std::vector<uint8_t> b;
	ymfm::ymfm_saved_state st(b, true);
	chip.save_restore(st);
	return (uint32_t)b.size();
}

void ym_state_save(uint8_t *dest)
{
	std::vector<uint8_t> b;
	ymfm::ymfm_saved_state st(b, true);
	chip.save_restore(st);
	std::copy(b.begin(), b.end(), dest);
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
