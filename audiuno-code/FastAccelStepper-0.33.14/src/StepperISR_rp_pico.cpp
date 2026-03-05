#include "StepperISR.h"

#if defined(SUPPORT_RP_PICO)
#include <FreeRTOS.h>
#include <task.h>

#include "hardware/irq.h"
#include "pico_pio.h"

// Here are the global variables to interface with the interrupts
StepperQueue fas_queue[NUM_QUEUES];
static stepper_pio_program* program;

// Forward declarations for per-PIO IRQ trampoline functions
static void pio0_fifo_irq_handler();
static void pio1_fifo_irq_handler();
#if NUM_PIOS > 2
static void pio2_fifo_irq_handler();
#endif

static const irq_handler_t pio_fifo_irq_handlers[] = {
    pio0_fifo_irq_handler,
    pio1_fifo_irq_handler,
#if NUM_PIOS > 2
    pio2_fifo_irq_handler,
#endif
};

bool StepperQueue::init(FastAccelStepperEngine* engine, uint8_t queue_num,
                        uint8_t step_pin) {
  (void)queue_num;  // silence compiler for unused parameter
  _step_pin = step_pin;
  _isActive = false;
  dirPin = PIN_UNDEFINED;
  pos_offset = 0;
  max_speed_in_ticks = 80;  // This equals 200kHz @ 16MHz
  bool ok = claim_pio_sm(engine);
  if (ok) {
    setupSM();
    connect();
  }
  return ok;
}

void StepperQueue::attachDirPinToStatemachine() {
  if ((dirPin != PIN_UNDEFINED) && ((dirPin & PIN_EXTERNAL_FLAG) == 0)) {
    // according to (some) documentation state machine should not be enabled
    pio_sm_set_set_pins(pio, sm, dirPin, 1);  // Direction pin via set
    pio_sm_set_consecutive_pindirs(pio, sm, dirPin, 1, true);
    pio_gpio_init(pio, dirPin);
  }
}

void StepperQueue::setDirPinState(bool high) {
  if ((dirPin != PIN_UNDEFINED) && ((dirPin & PIN_EXTERNAL_FLAG) == 0)) {
    // Serial.print("Set dir pin ");
    // Serial.print(dirPin);
    // Serial.print(" to ");
    // Serial.println(high ? "HIGH" : "LOW");
    pio_sm_exec(pio, sm, pio_encode_set(pio_pins, high ? 1 : 0));
  }
}

bool StepperQueue::claim_pio_sm(FastAccelStepperEngine* engine) {
  // We have two or three PIO modules. If we need one sm from a pio,
  // the whole PIO need to be claimed due to the size of our pio code.
  // Let's check first, if there is any PIO claimed.
  // If yes, check if we can claim a sm from that PIO.
  for (uint8_t i = 0; i < engine->claimed_pios; i++) {
    // pio has been claimed, so our program is valid
    int claimed_sm = pio_claim_unused_sm(engine->pio[i], false);
    if (claimed_sm >= 0) {
      // successfully claimed
      pio = engine->pio[i];
      sm = claimed_sm;
      // Serial.print("claim pio=");
      // Serial.print(i);
      // Serial.print(" sm=");
      // Serial.println(sm);
      return true;
    }
  }
  // claim a new pio
  pio_program_t pio_program;
  pio_program.instructions = program->code;
  pio_program.length = program->pc;
  pio_program.origin = 0;
  pio_program.pio_version = 0;
#if defined(PICO_RP_2350)
  pio_program.used_gpio_ranges = 0;
#endif
  uint offset;
  uint8_t pio_index = engine->claimed_pios;
  if (pio_index == NUM_PIOS) {
    return false;
  }
  bool rc = pio_claim_free_sm_and_add_program_for_gpio_range(
      &pio_program, &pio, &sm, &offset, _step_pin, 1, true);
  if (!rc) {
    // try again. for whatever reason this may fail on first attempt
    // Serial.println("retry claim");
    rc = pio_claim_free_sm_and_add_program_for_gpio_range(
        &pio_program, &pio, &sm, &offset, _step_pin, 1, true);
  }
  // Serial.print("claim new pio=");
  // Serial.print(engine->claimed_pios);
  // Serial.print(" sm=");
  // Serial.print(sm);
  // Serial.print(" result=");
  // Serial.println(rc);
  if (rc) {
    engine->pio[pio_index] = pio;
    engine->claimed_pios = pio_index + 1;
    // Set up FIFO interrupt handler for the newly claimed PIO block
    uint pio_idx = pio_get_index(pio);
    irq_set_exclusive_handler(PIO_IRQ_NUM(pio, 0),
                              pio_fifo_irq_handlers[pio_idx]);
    irq_set_enabled(PIO_IRQ_NUM(pio, 0), true);
  }
  return rc;
}

void StepperQueue::setupSM() {
  // Serial.print("setupSM pio=");
  // uint pio_idx = pio_get_index(pio);
  // Serial.print(pio_idx);
  // Serial.print(" sm=");
  // Serial.println(sm);
  
  pio_sm_config c = pio_get_default_sm_config();
  // Map the state machine's OUT pin group to one pin, namely the `pin`
  // parameter to this function.
  sm_config_set_jmp_pin(&c, _step_pin);      // Step pin read back
  sm_config_set_out_pins(&c, _step_pin, 1);  // Step pin via out
  sm_config_set_wrap(&c, program->wrap_target, program->wrap_at);

  // Load our configuration, and jump to the start of the program
  uint offset = 0;
  pio_sm_init(pio, sm, offset, &c);
  // Set the pin direction to output at the PIO
  pio_sm_set_consecutive_pindirs(pio, sm, _step_pin, 1, true);
  pio_sm_set_enabled(pio, sm, true);  // sm is running, otherwise loop() stops
  pio_sm_clear_fifos(pio, sm);
  pio_sm_restart(pio, sm);
}

void StepperQueue::connect() {
  pio_gpio_init(pio, _step_pin);
  if ((dirPin != PIN_UNDEFINED) && ((dirPin & PIN_EXTERNAL_FLAG) == 0)) {
    pio_gpio_init(pio, dirPin);
  }
}

void StepperQueue::disconnect() {
  // not sure, if disconnecting works. As long as connect() does not reenable sm
  // then the connect() will not work. so keep the sm running for now
  // pio_sm_set_enabled(pio, sm, false);
  gpio_init(_step_pin);
  if ((dirPin != PIN_UNDEFINED) && ((dirPin & PIN_EXTERNAL_FLAG) == 0)) {
    gpio_init(dirPin);
  }
}

bool StepperQueue::isReadyForCommands() { return true; }

static bool push_command(StepperQueue* q) {
  uint8_t rp = q->read_idx;
  if (rp == q->next_write_idx) {
    // no command in queue
    return false;
  }
  if (pio_sm_is_tx_fifo_full(q->pio, q->sm)) {
    // Serial.println("TX FIFO full, cannot push command");
    return false;
  }
  // Process command
  struct queue_entry* e_curr = &q->entry[rp & QUEUE_LEN_MASK];
  uint8_t steps = e_curr->steps;
  uint16_t ticks = e_curr->ticks;
  bool dirHigh = e_curr->dirPinState == 1;
  bool countUp = e_curr->countUp == 1;
  // char out[200];
  // sprintf(out, "push_command %d: dirHigh: %d, countUp: %d, steps: %d, ticks:
  // %d",
  //         rp, dirHigh, countUp, steps, ticks);
  // Serial.println(out);
  uint32_t loops = pio_calc_loops(steps, ticks, &q->adjust_80MHz);
  uint32_t entry = pio_make_fifo_entry(dirHigh, countUp, steps, loops);
  pio_sm_put(q->pio, q->sm, entry);
  // Serial.println((entry & 512) != 0 ? "HIGH":"LOW");
  rp++;
  q->read_idx = rp;
  return true;
}

void StepperQueue::startQueue() {
  // queue is already running with ISR enabled, so nothing to do here
  if (_isActive) {
     return;
  }
  // These commands would clear isr and consequently the sm state's position is
  // lost
  //  pio_sm_set_enabled(pio, sm, true); // sm is running, otherwise loop()
  //  stops pio_sm_clear_fifos(pio, sm); pio_sm_restart(pio, sm);

  // Disable FIFO interrupt during initial fill to prevent race with ISR
  pio_set_irq0_source_enabled(pio,
      pio_get_tx_fifo_not_full_interrupt_source(sm), false);
  _isActive = false;
  while (push_command(this)) {
  };
  _isActive = true;
  // Enable TX FIFO not full interrupt - ISR will keep FIFO fed from now on
  pio_set_irq0_source_enabled(pio,
      pio_get_tx_fifo_not_full_interrupt_source(sm), true);
}
void StepperQueue::forceStop() {
  // Disable FIFO interrupt before stopping to prevent ISR from pushing commands
  _isActive = false;
  pio_set_irq0_source_enabled(pio,
      pio_get_tx_fifo_not_full_interrupt_source(sm), false);
  pio_sm_set_enabled(pio, sm, false);
  pio_sm_clear_fifos(pio, sm);
  pio_sm_restart(pio, sm);
  pio_sm_set_enabled(pio, sm, true);
  // init pc to 0 (perhaps not needed)
  pio_sm_exec(pio, sm, pio_encode_jmp(0));
  // ensure step is zero
  pio_sm_exec(pio, sm, pio_encode_mov(pio_pins, pio_null));
  setDirPinState(queue_end.dir);

  // and empty the buffer
  read_idx = next_write_idx;

  // and clear the offset
  pos_offset = 0;
}
bool StepperQueue::isRunning() {
  if (!pio_sm_is_tx_fifo_empty(pio, sm)) {
    return true;
  }
  // Still the sm can process a command
  uint8_t pc = pio_sm_get_pc(pio, sm);
  // if pc > 0, then sm is not waiting for fifo entry
  return (pc != 0);
}
int32_t StepperQueue::getCurrentStepCount() {
  bool running = isRunning();
  uint32_t pos;
  if (!running) {
    // Empty queue
    for (uint8_t i = 0; i <= 4; i++) {
      if (pio_sm_is_rx_fifo_empty(pio, sm)) {
        break;
      }
      pio_sm_get(pio, sm);
    }
    // kick off loop to probe position
    uint32_t entry = pio_make_fifo_entry(
        queue_end.dir, 0, 0, LOOPS_FOR_1US);  // no steps and 1us cycle
    pio_sm_put(pio, sm, entry);

    // wait for pc reaching 0 again to ensure isRunning() returns false
    while (isRunning()) {
      if (!pio_sm_is_tx_fifo_empty(pio, sm)) {
        break;  // apparently the stepper is now getting commands
      }
    }
  }
  // use last value
  for (uint8_t i = 0; i <= 4; i++) {
    pos = pio_sm_get(pio, sm);
    if (pio_sm_is_rx_fifo_empty(pio, sm)) {
      break;
    }
  }
  return (int32_t)pos;
}

//*************************************************************************************************

bool StepperQueue::isValidStepPin(uint8_t step_pin) {
  // for now we do only support lower 32 gpios
  return (step_pin < 32);
}

//*************************************************************************************************
//
// PIO FIFO Interrupt Handler
//
// The PIO hardware provides a "TX FIFO not full" interrupt source per state
// machine.  This is a level-triggered interrupt: it stays asserted as long as
// the TX FIFO has at least one free slot (level < 4).
//
// There is no configurable FIFO watermark/threshold in the PIO hardware, so
// the ISR checks the actual FIFO level via pio_sm_get_tx_fifo_level() to
// distinguish two conditions:
//
//   a) FIFO just emptied  – level == 0
//      All entries have been consumed by the state machine.
//
//   b) FIFO low           – level <= 2  (1 or 2 entries still present)
//      The FIFO is running low and should be refilled soon.
//
// After detecting the condition the handler pushes as many commands as
// possible from the software queue into the FIFO.  When the software queue is
// empty, the interrupt source for that SM is disabled to prevent the
// level-triggered interrupt from firing continuously. 
//
//*************************************************************************************************

static void pio_fifo_irq_handler(PIO pio) {
  uint32_t ints = pio->ints0;

  for (uint8_t i = 0; i < NUM_QUEUES; i++) {
    StepperQueue* q = &fas_queue[i];
    if (!q->_isActive) continue;
    if (q->_step_pin == PIN_UNDEFINED) {
      continue;  // skip uninitialized queues
    } 
    if (q->pio != pio) continue;

    // Check if this SM's TX FIFO not full interrupt is pending
    if (!(ints & (1u << (pis_sm0_tx_fifo_not_full + q->sm)))) continue;

    // Determine current FIFO fill level
    // uint level = pio_sm_get_tx_fifo_level(pio, q->sm);

    // if (level == 0) {
      // Case a): FIFO just emptied – all entries consumed by the SM
    // }

    // if (level <= 2) {
      // Case b): FIFO low – only 1 or 2 entries remain
    // }

    // Push as many commands as possible to refill the FIFO
    while (push_command(q)) {
    }

    // If the software queue is empty, disable the interrupt source for this
    // SM to prevent the level-triggered interrupt from firing continuously.
    if (q->read_idx == q->next_write_idx) {
      q->_isActive = false;
      pio_set_irq0_source_enabled(
          pio, pio_get_tx_fifo_not_full_interrupt_source(q->sm), false);
    }
  }
}

static void pio0_fifo_irq_handler() { pio_fifo_irq_handler(pio0); }
static void pio1_fifo_irq_handler() { pio_fifo_irq_handler(pio1); }
#if NUM_PIOS > 2
static void pio2_fifo_irq_handler() { pio_fifo_irq_handler(pio2); }
#endif

//*************************************************************************************************
void StepperTask(void* parameter) {
  FastAccelStepperEngine* engine = (FastAccelStepperEngine*)parameter;
  while (true) {
    engine->manageSteppers();
    const TickType_t delay_time =
        (engine->_delay_ms + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS;
    vTaskDelay(delay_time);
  }
}

void fas_init_engine(FastAccelStepperEngine* engine) {
  for (uint8_t i = 0; i < NUM_QUEUES; i++) {
    fas_queue[i]._step_pin = PIN_UNDEFINED;
    fas_queue[i]._isActive = true;
  }
#define STACK_SIZE 3000
#define PRIORITY (configMAX_PRIORITIES - 1)
  engine->_delay_ms = DELAY_MS_BASE;
  xTaskCreate(StepperTask, "StepperTask", STACK_SIZE, engine, PRIORITY, NULL);

  program = stepper_make_program();
}
#endif
