#pragma once
#include <Matter.h>
#include <app/clusters/mode-base-server/mode-base-server.h>
#include <app/clusters/operational-state-server/operational-state-server.h>
#include "SimVacuum.h"

namespace roocore_sim {
namespace clusters = chip::app::Clusters;
// Arduino's bundled accessories are friends of ArduinoMatter. A custom endpoint
// uses a small subclass to access the same protected lifecycle helpers.
class MatterLifecycle : public ArduinoMatter {
 public:
  using ArduinoMatter::_init;
  using ArduinoMatter::isStackStarted;
};
class MatterVacuum;

class ModeDelegate : public clusters::ModeBase::Delegate {
 public:
  ModeDelegate(MatterVacuum &owner, bool cleaning) : owner_(owner), cleaning_(cleaning) {}
  CHIP_ERROR Init() override { return CHIP_NO_ERROR; }
  CHIP_ERROR GetModeLabelByIndex(uint8_t, chip::MutableCharSpan &) override;
  CHIP_ERROR GetModeValueByIndex(uint8_t, uint8_t &) override;
  CHIP_ERROR GetModeTagsByIndex(uint8_t, chip::app::DataModel::List<clusters::detail::Structs::ModeTagStruct::Type> &) override;
  void HandleChangeToMode(uint8_t, clusters::ModeBase::Commands::ChangeToModeResponse::Type &) override;
  void synchronize(uint8_t mode);
 private:
  MatterVacuum &owner_;
  bool cleaning_;
};

class StateDelegate : public clusters::RvcOperationalState::Delegate {
 public:
  explicit StateDelegate(MatterVacuum &owner) : owner_(owner) {}
  chip::app::DataModel::Nullable<uint32_t> GetCountdownTime() override { return {}; }
  CHIP_ERROR GetOperationalStateAtIndex(size_t, clusters::OperationalState::GenericOperationalState &) override;
  CHIP_ERROR GetOperationalPhaseAtIndex(size_t, chip::MutableCharSpan &) override { return CHIP_ERROR_NOT_FOUND; }
  void HandlePauseStateCallback(clusters::OperationalState::GenericOperationalError &) override;
  void HandleResumeStateCallback(clusters::OperationalState::GenericOperationalError &) override;
  void HandleGoHomeCommandCallback(clusters::OperationalState::GenericOperationalError &) override;
 private:
  MatterVacuum &owner_;
};

class MatterVacuum : public MatterEndPoint {
 public:
  MatterVacuum() : run_(*this, false), clean_(*this, true), states_(*this) {}
  bool begin();
  void ready();
  void network(bool online);
  void tick();
  Result changeMode(uint8_t mode);
  void operate(uint8_t command, clusters::OperationalState::GenericOperationalError &error);
  bool attributeChangeCB(uint16_t, uint32_t, uint32_t, esp_matter_attr_val_t *) override { return true; }
 private:
  void sync(bool includeMode = true);
  static void initState(void *self, uint16_t endpoint);
  Vacuum model_;
  ModeDelegate run_, clean_;
  StateDelegate states_;
  clusters::RvcOperationalState::Instance *operational_ = nullptr;
  bool ready_ = false;
};
}  // namespace roocore_sim
