#include "MatterVacuum.h"
#include <esp_timer.h>
#include <app-common/zap-generated/cluster-objects.h>

namespace roocore_sim {
using namespace chip;
using namespace chip::app;
using namespace clusters;
static uint32_t nowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
static_assert(static_cast<uint8_t>(State::Stopped) == to_underlying(RvcOperationalState::OperationalStateEnum::kStopped));
static_assert(static_cast<uint8_t>(State::Running) == to_underlying(RvcOperationalState::OperationalStateEnum::kRunning));
static_assert(static_cast<uint8_t>(State::Paused) == to_underlying(RvcOperationalState::OperationalStateEnum::kPaused));
static_assert(static_cast<uint8_t>(State::SeekingCharger) == to_underlying(RvcOperationalState::OperationalStateEnum::kSeekingCharger));
static_assert(static_cast<uint8_t>(State::Docked) == to_underlying(RvcOperationalState::OperationalStateEnum::kDocked));

CHIP_ERROR ModeDelegate::GetModeLabelByIndex(uint8_t i, MutableCharSpan &label) {
  if (i >= (cleaning_ ? 1 : 2)) return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
  return CopyCharSpanToMutableCharSpan(CharSpan::fromCharString(cleaning_ ? "Vacuum" : (i ? "Cleaning" : "Idle")), label);
}
CHIP_ERROR ModeDelegate::GetModeValueByIndex(uint8_t i, uint8_t &value) {
  if (i >= (cleaning_ ? 1 : 2)) return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
  value = i;
  return CHIP_NO_ERROR;
}
CHIP_ERROR ModeDelegate::GetModeTagsByIndex(uint8_t i, DataModel::List<clusters::detail::Structs::ModeTagStruct::Type> &tags) {
  if (i >= (cleaning_ ? 1 : 2)) return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
  if (tags.size() < 1) return CHIP_ERROR_BUFFER_TOO_SMALL;
  tags[0] = {};
  tags[0].value = cleaning_ ? to_underlying(RvcCleanMode::ModeTag::kVacuum)
                           : to_underlying(i ? RvcRunMode::ModeTag::kCleaning : RvcRunMode::ModeTag::kIdle);
  tags.reduce_size(1);
  return CHIP_NO_ERROR;
}
void ModeDelegate::HandleChangeToMode(uint8_t mode, ModeBase::Commands::ChangeToModeResponse::Type &response) {
  const auto result = cleaning_ ? (mode == 0 ? Result::Ok : Result::UnsupportedMode) : owner_.changeMode(mode);
  response.status = to_underlying(result == Result::Ok ? ModeBase::StatusCode::kSuccess :
                                 result == Result::UnsupportedMode ? ModeBase::StatusCode::kUnsupportedMode :
                                 ModeBase::StatusCode::kGenericFailure);
}
void ModeDelegate::synchronize(uint8_t mode) {
  if (!GetInstance()) abort();
  if (GetInstance()->GetCurrentMode() != mode) {
    if (GetInstance()->UpdateCurrentMode(mode) != Protocols::InteractionModel::Status::Success) abort();
  }
}
CHIP_ERROR StateDelegate::GetOperationalStateAtIndex(size_t index, OperationalState::GenericOperationalState &state) {
  using S = RvcOperationalState::OperationalStateEnum;
  constexpr S supported[] = {S::kStopped, S::kRunning, S::kPaused, S::kError, S::kSeekingCharger, S::kDocked};
  if (index >= sizeof(supported) / sizeof(supported[0])) return CHIP_ERROR_NOT_FOUND;
  state = OperationalState::GenericOperationalState(static_cast<uint8_t>(supported[index]));
  return CHIP_NO_ERROR;
}
void StateDelegate::HandlePauseStateCallback(OperationalState::GenericOperationalError &error) {
  owner_.operate(RvcOperationalState::Commands::Pause::Id, error);
}
void StateDelegate::HandleResumeStateCallback(OperationalState::GenericOperationalError &error) {
  owner_.operate(RvcOperationalState::Commands::Resume::Id, error);
}
void StateDelegate::HandleGoHomeCommandCallback(OperationalState::GenericOperationalError &error) {
  owner_.operate(RvcOperationalState::Commands::GoHome::Id, error);
}

bool MatterVacuum::begin() {
  MatterLifecycle::_init();
  endpoint::robotic_vacuum_cleaner::config_t config;
  config.rvc_run_mode.delegate = &run_;
  auto *ep = endpoint::robotic_vacuum_cleaner::create(node::get(), &config, ENDPOINT_FLAG_NONE, this);
  if (!ep) return false;
  setEndPointId(endpoint::get_id(ep));
  cluster::rvc_clean_mode::config_t cleanConfig;
  cleanConfig.delegate = &clean_;
  if (!cluster::rvc_clean_mode::create(ep, &cleanConfig, CLUSTER_FLAG_SERVER)) return false;
  auto *op = cluster::get(getEndPointId(), RvcOperationalState::Id);
  if (!op) return false;
  if (!cluster::operational_state::event::create_operational_error(op)) return false;
  // The SDK endpoint creates attributes/events; the delegate handles these standard commands.
  for (auto commandId : {RvcOperationalState::Commands::Pause::Id, RvcOperationalState::Commands::Resume::Id,
                         RvcOperationalState::Commands::GoHome::Id}) {
    if (!command::create(op, commandId, COMMAND_FLAG_ACCEPTED, nullptr)) return false;
  }
  if (!command::create(op, RvcOperationalState::Commands::OperationalCommandResponse::Id, COMMAND_FLAG_GENERATED, nullptr)) return false;
  return cluster::set_delegate_and_init_callback(op, initState, this) == ESP_OK;
}
void MatterVacuum::initState(void *self, uint16_t endpointId) {
  auto &device = *static_cast<MatterVacuum *>(self);
  device.operational_ = new RvcOperationalState::Instance(&device.states_, endpointId);
  if (!device.operational_ || device.operational_->Init() != CHIP_NO_ERROR) abort();
}
void MatterVacuum::ready() {
  if (!operational_) abort();
  if (operational_->SetCurrentPhase(DataModel::NullNullable) != CHIP_NO_ERROR) abort();
  ready_ = true;
  // ModeBase may restore CurrentMode from flash. It must agree with a fresh, inactive simulation.
  sync();
}
void MatterVacuum::sync(bool includeMode) {
  if (!ready_) return;
  if (includeMode) run_.synchronize(model_.runMode());
  clean_.synchronize(0);
  if (operational_->SetOperationalState(static_cast<uint8_t>(model_.state())) != CHIP_NO_ERROR) abort();
}
void MatterVacuum::network(bool online) {
  model_.network(online);
  sync();
}
void MatterVacuum::tick() {
  const auto before = model_.state();
  if (ready_ && model_.tick(nowMs())) {
    sync();
    if (before == State::Running) {
      operational_->OnOperationCompletionDetected(to_underlying(OperationalState::ErrorStateEnum::kNoError));
    }
  }
}
Result MatterVacuum::changeMode(uint8_t mode) {
  if (!ready_) return Result::Offline;
  const auto result = model_.mode(mode, nowMs());
  // ModeBase updates CurrentMode after a successful callback.
  if (result == Result::Ok) sync(false);
  return result;
}
void MatterVacuum::operate(uint8_t commandId, OperationalState::GenericOperationalError &error) {
  Result result = Result::InvalidState;
  if (ready_) {
    if (commandId == RvcOperationalState::Commands::Pause::Id) result = model_.pause(nowMs());
    if (commandId == RvcOperationalState::Commands::Resume::Id) result = model_.resume(nowMs());
    if (commandId == RvcOperationalState::Commands::GoHome::Id) result = model_.goHome(nowMs());
  }
  error.Set(to_underlying(result == Result::Ok ? OperationalState::ErrorStateEnum::kNoError :
                                               OperationalState::ErrorStateEnum::kCommandInvalidInState));
  if (result == Result::Ok) sync();
}
}  // namespace roocore_sim
