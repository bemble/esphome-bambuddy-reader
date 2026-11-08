#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/pn532/pn532.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/button/button.h"

#include "bambu_colors.h"
#include "bambu_densities.h"

#include <span>

namespace esphome {
namespace bambu_nfc {

class BambuNfc;

class BambuNfcResetButton : public button::Button, public Parented<BambuNfc> {
 protected:
  void press_action() override;
};

class BambuSuccessTrigger : public Trigger<> {};
class BambuErrorTrigger : public Trigger<> {};

class BambuNfc : public pn532::PN532,
                 public spi::SPIDevice<spi::BIT_ORDER_LSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                       spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_1MHZ> {
 public:
  void setup() override;
  void dump_config() override;
  void loop() override;
  void clear_sensors();

  void set_filament_type_sensor(text_sensor::TextSensor *s) { filament_type_sensor_ = s; }
  void set_filament_color_sensor(text_sensor::TextSensor *s) { filament_color_sensor_ = s; }
  void set_filament_color_name_sensor(text_sensor::TextSensor *s) { filament_color_name_sensor_ = s; }
  void set_filament_subtype_sensor(text_sensor::TextSensor *s) { filament_subtype_sensor_ = s; }
  void set_tray_uid_sensor(text_sensor::TextSensor *s) { tray_uid_sensor_ = s; }
  void set_tray_info_idx_sensor(text_sensor::TextSensor *s) { tray_info_idx_sensor_ = s; }
  void set_variant_id_sensor(text_sensor::TextSensor *s) { variant_id_sensor_ = s; }
  void set_card_uid_sensor(text_sensor::TextSensor *s) { card_uid_sensor_ = s; }
  void set_production_date_sensor(text_sensor::TextSensor *s) { production_date_sensor_ = s; }
  void set_last_scan_date_sensor(text_sensor::TextSensor *s) { last_scan_date_sensor_ = s; }

  void set_min_temp_sensor(sensor::Sensor *s) { min_temp_sensor_ = s; }
  void set_max_temp_sensor(sensor::Sensor *s) { max_temp_sensor_ = s; }
  void set_bed_temp_sensor(sensor::Sensor *s) { bed_temp_sensor_ = s; }
  void set_spool_weight_sensor(sensor::Sensor *s) { spool_weight_sensor_ = s; }
  void set_filament_diameter_sensor(sensor::Sensor *s) { filament_diameter_sensor_ = s; }
  void set_drying_temp_sensor(sensor::Sensor *s) { drying_temp_sensor_ = s; }
  void set_drying_time_sensor(sensor::Sensor *s) { drying_time_sensor_ = s; }
  void set_nozzle_diameter_sensor(sensor::Sensor *s) { nozzle_diameter_sensor_ = s; }
  void set_spool_width_sensor(sensor::Sensor *s) { spool_width_sensor_ = s; }
  void set_filament_length_sensor(sensor::Sensor *s) { filament_length_sensor_ = s; }
  void set_material_density_sensor(sensor::Sensor *s) { material_density_sensor_ = s; }

  void register_bambu_success_trigger(BambuSuccessTrigger *t) { success_triggers_.push_back(t); }
  void register_bambu_error_trigger(BambuErrorTrigger *t) { error_triggers_.push_back(t); }

 protected:
  bool is_read_ready() override;
  bool write_data(std::span<const uint8_t> data) override;
  bool read_data(pn532::PN532Frame &data, size_t len) override;
  bool read_response(uint8_t command, pn532::PN532Frame &data) override;

  bool read_bambu_data_(nfc::NfcTagUid &uid);
  bool derive_bambu_keys_(const uint8_t *uid, size_t uid_len, uint8_t *keys_out);

  text_sensor::TextSensor *filament_type_sensor_{nullptr};
  text_sensor::TextSensor *filament_color_sensor_{nullptr};
  text_sensor::TextSensor *filament_color_name_sensor_{nullptr};
  text_sensor::TextSensor *filament_subtype_sensor_{nullptr};
  text_sensor::TextSensor *tray_uid_sensor_{nullptr};
  text_sensor::TextSensor *tray_info_idx_sensor_{nullptr};
  text_sensor::TextSensor *variant_id_sensor_{nullptr};
  text_sensor::TextSensor *card_uid_sensor_{nullptr};
  text_sensor::TextSensor *production_date_sensor_{nullptr};
  text_sensor::TextSensor *last_scan_date_sensor_{nullptr};

  sensor::Sensor *min_temp_sensor_{nullptr};
  sensor::Sensor *max_temp_sensor_{nullptr};
  sensor::Sensor *bed_temp_sensor_{nullptr};
  sensor::Sensor *spool_weight_sensor_{nullptr};
  sensor::Sensor *filament_diameter_sensor_{nullptr};
  sensor::Sensor *drying_temp_sensor_{nullptr};
  sensor::Sensor *drying_time_sensor_{nullptr};
  sensor::Sensor *nozzle_diameter_sensor_{nullptr};
  sensor::Sensor *spool_width_sensor_{nullptr};
  sensor::Sensor *filament_length_sensor_{nullptr};
  sensor::Sensor *material_density_sensor_{nullptr};

  std::vector<BambuSuccessTrigger *> success_triggers_;
  std::vector<BambuErrorTrigger *> error_triggers_;
};

}  // namespace bambu_nfc
}  // namespace esphome
