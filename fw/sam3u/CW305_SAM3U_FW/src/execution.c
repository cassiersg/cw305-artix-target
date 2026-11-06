/* execution.c */
#include "execution.h"
#include "naeusb_board_config.h"
#include "CETimer.h"
#include "srsw.h"

volatile execution_state  exstate;
volatile execution_config exconfig0, exconfig1;
volatile execution_result exresult0, exresult1;

srsw_reg exconfig_reg, exresult_reg;

void exec_init(){
    srsw_reg_init(&exconfig_reg, (void *) &exconfig0, (void *) &exconfig1);
    srsw_reg_init(&exresult_reg, (void *) &exresult0, (void *) &exresult1);
}

void BUSY_ISR(uint32_t ul_id, uint32_t ul_mask){
    if (FPGA_BUSY_ID == ul_id && (ul_mask & FPGA_BUSY_MASK)) {
        // FPGA is done
        gpio_set_pin_low(FPGA_RESET_GPIO); // put FPGA into reset
        if(exstate.state == WAIT_FOR_DONE){
            // Timer ISR completed and FPGA is done, get results
            exstate.state = GET_RESULTS;
            exec_get_results();

            exstate.state = ACTIVATE_TV;
            exec_activate_tv();
        } else {
            // Timer ISR did not complete, something went wrong
            gpio_set_pin_low(FPGA_TRIGGER_GPIO);
            stop_CE_timer();
        }
        // Disable port interrupt
       	pio_disable_interrupt(FPGA_BUSY_PIO, FPGA_BUSY_MASK);
        exstate.state = START;  // ready for next execution through timer.
    }
}

void exec_prepare_tv(){
    // grap exconfig for reading
    execution_config * exconfig = srsw_reg_reader_grab(&exconfig_reg);
    //exstate.next_settings=exconfig.settings;
    //if(exstate.next_settings.id == exstate.current_settings.id){ // configuration was not changed, continue
    if(exconfig->settings.id == exstate.current_settings.id){ // configuration was not changed, continue
        exstate.next_exec_cnt = exstate.current_exec_cnt+1;
    } else {
        exstate.next_settings=exconfig->settings;
        exstate.next_exec_cnt = 0;                               // configuration was changed, start at 0
        exstate.next_settings.done = false;
        if (exstate.next_settings.exec_max > 0){                 // if exec_max > 0 then we want to take measurements
            exstate.next_settings.measurement = true;
        }
    }
    exstate.next_input_len=_exec_compute_tv(exstate.next_exec_cnt, exstate.next_settings.exec_max, exconfig->exec_buf, exstate.next_input);
    // release exconfig
    srsw_reg_reader_release(&exconfig_reg);
}

void exec_activate_tv(){
    // activate next settings
    exstate.current_settings = exstate.next_settings;
    exstate.current_exec_cnt = exstate.next_exec_cnt;
    exstate.current_id = exstate.current_settings.id;

    // send new TV to FPGA
    unsigned int address = 0;
    FPGA_setlock(fpga_generic);
    /* Start Transaction */

    /* Do memory write */
    for(unsigned int i = 0; i < exstate.next_input_len; i++){
        xram[i+address] = exstate.next_input[i];
    }
    FPGA_setlock(fpga_unlocked);

    if (exstate.current_settings.measurement) {
        // enable trigger
        gpio_set_pin_high(FPGA_TRIGGER_GPIO);
    }

}

void exec_get_results(){
    if (exstate.current_settings.measurement){
        exstate.current_crc = 0;
        for(unsigned int i = 0; i <4; i++){
            exstate.current_crc = exstate.current_crc << 8;
            exstate.current_crc += (uint32_t) xram[i+RESULT_OFFSET];
        }
        // grab exresult for writing
        execution_result * exresult = srsw_reg_writer_grab(&exresult_reg);
        exresult->id = exstate.current_id;
        exresult->exec_cnt = exstate.current_exec_cnt +1; // internally we start with 0
        exresult->crc = exstate.current_crc;
        exresult->status = (uint32_t)(((uint32_t) exstate.state << 24) + exstate.current_settings.measurement + exstate.configured*2 + status_CE_timer()*4+ CORE_BUSY()*8 + TRIGGER_ON()*16) + exstate.current_settings.done*32;

        // release exresult
        srsw_reg_writer_release(&exresult_reg);
    }

    if (exstate.current_exec_cnt +1 == exstate.current_settings.exec_max){
        // done with measurements: remove trigger
        gpio_set_pin_low(FPGA_TRIGGER_GPIO);
        exstate.next_settings.measurement = false;
        exstate.next_settings.done = true;
    }

}

uint32_t _exec_compute_tv(uint32_t exec_cnt, uint32_t exec_max, volatile uint8_t *exec_buf, volatile uint8_t *input){
    uint32_t input_len=INPUT_SIZE;
    if(exec_cnt==0){ // Initialize plaintext (first 16 bytes) and key (next 16 bytes)  to first 32 byte from exec_buf
        for(int i = 0; i < input_len; i++){
            input[i]  = exec_buf[i];
        }
    } else {        // Use counter mode, i.e., increment plaintext
        uint32_t cin = 1;
        for(int i = input_len/2; i > 0; i--){
            if((input[i-1]==255) && cin){
                input[i-1]=0;
                cin=1;
            } else {
                input[i-1] = input[i-1] + cin;
                cin=0;
            }
        }
    }
    return(input_len);
}

