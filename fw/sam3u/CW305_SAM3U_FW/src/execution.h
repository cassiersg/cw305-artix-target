/* execution.h */
#ifndef _EXECUTION_
#define _EXECUTION_

#include <stdbool.h>
#include <stdint.h>
#include <asf.h>
#include "naeusb.h"
#include "naeusb_board_config.h"
#include "srsw.h"


// size in bytes, structure of contents depends on the agorithm used
#define EXEC_BUF_SIZE 32
#define INPUT_SIZE 32   // Maximum input size
#define RESULT_OFFSET 32 // address offset of where the result is in xram (shared ram with FPGA)

typedef enum {
    START,
    WAIT_FOR_BUSY,
    PREPARE_TV,
    WAIT_FOR_DONE,
    GET_RESULTS,
    ACTIVATE_TV
} e_state;



typedef struct {
    uint32_t id;
    uint32_t exec_max;
    uint32_t period;
    uint32_t output_size;
    bool     measurement;
    bool     done;
} execution_settings;

typedef struct {
    execution_settings settings;
    uint8_t exec_buf[EXEC_BUF_SIZE];
} execution_config;

typedef struct {
    uint32_t current_id;
    uint32_t current_exec_cnt;
    uint32_t current_crc;
    uint32_t next_exec_cnt;
    uint8_t  next_input[INPUT_SIZE];
    uint32_t next_input_len;
    execution_settings current_settings;
    execution_settings next_settings;
    e_state  state;
    //bool     CE_on;
    bool     configured;
    //bool     reset_exec;
} execution_state;

typedef struct {
    uint32_t id;
    uint32_t exec_cnt;
    uint32_t crc;
    uint32_t status;  // including error code
} execution_result;

extern volatile execution_state  exstate;
extern srsw_reg exconfig_reg;
extern srsw_reg exresult_reg;

void BUSY_ISR(uint32_t ul_id, uint32_t ul_mask);
void exec_init(void);
void exec_prepare_tv(void);
void exec_activate_tv(void);
void exec_get_results(void);

uint32_t _exec_compute_tv(uint32_t exec_cnt, uint32_t exec_max, volatile uint8_t *exec_buf, volatile uint8_t *input);

#define  TRIGGER_ON()         gpio_pin_is_high(FPGA_TRIGGER_GPIO)
#define  CORE_BUSY()          gpio_pin_is_high(FPGA_BUSY_GPIO)



#endif /* _EXECUTION_ */
