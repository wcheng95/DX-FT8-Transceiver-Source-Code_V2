// host_mocks.c
// Mocks for STM32 HAL, peripheral inits, and radio/DSP hooks


#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include "main.h"
#include "host_mocks.h"
#include "decode_ft8.h"
#include "autoseq_engine.h"
#include "ADIF.h"
#include "json_parser.h"

// From Process_DSP.h
#define ft8_msg_samples 91
// From Display.h
#define MESSAGE_SIZE 40

// default test json file name
// override it with ./main <test_file>
const char* test_data_file = "test_data.json";

// Forward declaration
void advance_mock_tick(uint32_t ms);
void init_mock_timing(void);
static void handle_beacon_changes(void);
static void handle_touch_events(void);

// TX_ON_EVEN now loaded from JSON config
int Tune_On; // 0 = Receive, 1 = Xmit Tune Signal
int Beacon_On = 0;
int Xmit_Mode;
int xmit_flag = 0, ft8_xmit_counter, ft8_xmit_flag, ft8_xmit_delay;
int DSP_Flag = 0;
uint16_t buff_offset;
int ft8_flag, FT_8_counter, ft8_marker;

// Display.c
int FT_8_TouchIndex = 0;

uint16_t cursor;
char rtc_date_string[9];
char rtc_time_string[9];
extern int decode_flag;

int QSO_Xmit_Touch;
int FT8_Touch_Flag;

// FT8 timing variables from main.c 
extern uint32_t current_time, start_time, ft8_time;
extern int target_slot;

int frame_counter = 0;

void I2S2_RX_ProcessBuffer(uint16_t offset) {
    
    // Initialize timing on first call
    init_mock_timing();
    
    // Simulate frame processing (matches real SDR_Audio.c:146-150)
    if (++frame_counter == 4) {
        // process_FT8_FFT() would be called here in real code
        // model the cost of process_FT8_FFT()
        advance_mock_tick(29);
		FT_8_counter++;
        frame_counter = 0;
    }

    if (frame_counter == 0 && FT_8_counter % 10 == 0) {
        if (xmit_flag) {
            printf("t");
        } else {
            printf("r");
        }
    }
    
    fflush(stdout);
    
    // model the cost of I2S2_RX_ProcessBuffer()
    advance_mock_tick(7);
    
    if (frame_counter == 0 && FT_8_counter == ft8_msg_samples) {
        decode_flag = 1;
    }

	// I2S2_RX_ProcessBuffer() is called every 40ms
    usleep(400); // 0.4ms so 100X faster
    
    // Handle beacon changes based on timing
    handle_beacon_changes();
    
    // Handle touch events based on timing
    handle_touch_events();
}

void Process_Touch(void) {}

// constants.c
uint8_t tones[79];

// DS3231.c
void display_RealTime(int x, int y) {}
void display_Real_Date(int x, int y) {}
void make_Real_Time(void) {};
void make_Real_Date(void) {};
char log_rtc_time_string[13] = "RTC_TIME";
char log_rtc_date_string[13] = "RTC_DATE";

// Config will be loaded from JSON file
static char config_my_callsign[14] = "N6HAN";  // Default values
static char config_my_grid[7] = "CM87";
static char config_dx_callsign[14] = "AG6AQ";
static char config_dx_grid[7] = "CM97";

// gen_ft8.c
char Station_Call[10];
char Locator[5];
char Target_Call[10];	// seven character call sign (e.g. 3DA0XYZ) + optional /P + null terminator
char Target_Locator[5]; // four character locator  + null terminator
int Target_RSL;
int Station_RSL;
int CQ_Mode_Index = 0;
int Free_Index = 0;
char Free_Text1[MESSAGE_SIZE];
char Free_Text2[MESSAGE_SIZE];
char Comment[MESSAGE_SIZE];


// button.c
int Arm_Tune = 0;
int BandIndex = 2; // 20M
int Skip_Tx1 = 0;
const FreqStruct sBand_Data[NumBands] =
	{
		{// 40,
		 7074, "7.074"},
		{// 30,
		 10136, "10.136"},
		{// 20,
		 14074, "14.075"},
		{// 17,
		 18100, "18.101"},
		{// 15,
		 21074, "21.075"},
		{// 12,
		 24915, "24.916"},
		{// 10,
		 28074, "28.075"}};
void receive_sequence(void) {}

// log_file.c
void Write_Log_Data(const char *entry) {
	printf("ADIF log: %s\n", entry);
}

void Write_RxTxLog_Data(const char *entry) {
	printf("RxTx log: %s\n", entry);
}

// traffic_manager.c
void ft8_receive_sequence(void) {}

void terminate_QSO() {};

void set_FT8_Tone(uint8_t ft8_tone) {
    // model the cost
    // when frame_counter == 0, the RX path already takes 7+29=36ms,
    // this captures the bug when TX and process_FT8_FFT are not interleaved
    // to cause TX to take more than 15s to finish
    advance_mock_tick(5);
}

// decode_ft8.c
Decode new_decoded[25];

void process_selected_Station(int stations_decoded, int TouchIndex)
{
	if (stations_decoded > 0 && TouchIndex <= stations_decoded)
	{
		strcpy(Target_Call, new_decoded[TouchIndex].call_from);
		strcpy(Target_Locator, new_decoded[TouchIndex].target_locator);
		// Target_RSL = new_decoded[TouchIndex].snr;
		target_slot = new_decoded[TouchIndex].slot ^ 1;
		target_freq = new_decoded[TouchIndex].freq_hz;

		// if (QSO_Fix == 1)
		// 	set_QSO_Xmit_Freq(target_freq);

		// Auto_QSO_State = 1;
		// RSL_sent = 0;
		// RR73_sent = 0;
	}

	FT8_Touch_Flag = 0;
}

// Initialize FT8 timing for mock
static bool timing_initialized = false;
void init_mock_timing(void) {
    if (!timing_initialized) {
        start_time = HAL_GetTick();
        timing_initialized = true;
    }
}

// -----------------------------------------------------------------------------
// Test Data Management
static TestData test_data = {0};
static bool test_data_loaded = false;

// Helper function to handle beacon changes
static void handle_beacon_changes(void) {
    if (!test_data_loaded) {
        return;
    }
    int period_index = ft8_time / 30000;
    if (period_index < test_data.period_count)
    {
        TestPeriod *current_period = &test_data.periods[period_index];

        if (current_period->has_beacon_change)
        {
            uint32_t period_time_ms = ft8_time % 30000;   // Time within current 30s period
            float period_time_s = period_time_ms / 1000.0f; // Convert to seconds

            BeaconChange *bc = &current_period->beacon_change;

            // Apply beacon change if time has passed the offset
            if (period_time_s >= bc->time_offset && Beacon_On != bc->beacon_on)
            {
                printf("Setting Beacon_On to: %d\n", bc->beacon_on);
                Beacon_On = bc->beacon_on;
            }
        }
    }
}

// Helper function to handle touch events
static void handle_touch_events(void) {
    static int last_touch_slot = -1;
    
    if (!test_data_loaded) {
        return;
    }
    int period_index = ft8_time / 30000;


    if (period_index < test_data.period_count)
    {
        TestPeriod *current_period = &test_data.periods[period_index];

        if (current_period->has_touch_event && last_touch_slot != period_index)
        {
            uint32_t period_time_ms = ft8_time % 30000;   // Time within current 30s period
            float period_time_s = period_time_ms / 1000.0f; // Convert to seconds

            TouchEvent *te = &current_period->touch_event;

            // Apply touch event if time has passed the offset and we have messages
            if (period_time_s >= te->time_offset && current_period->message_count > 0)
            {
                int msg_index = te->message_index;
                if (msg_index >= 0 && msg_index < current_period->message_count)
                {
                    // Set the touch index and flag
                    FT_8_TouchIndex = msg_index;
                    FT8_Touch_Flag = 1;
                    last_touch_slot = period_index; // Prevent repeated triggers
                    printf("\nSimulating touch on message %d at time %.1fs...\n", msg_index, period_time_s);
                }
            }
        }
    }
}

// Convert TestMessage to Decode structure
static void convert_test_message_to_decode(const TestMessage* src, Decode* dst) {
    memset(dst, 0, sizeof(Decode));
    strncpy(dst->call_to, src->call_to, 13);
    strncpy(dst->call_from, src->call_from, 13);
    strncpy(dst->locator, src->locator, 6);
    strncpy(dst->target_locator, src->target_locator, 6);
    dst->snr = src->snr;
    dst->received_snr = src->received_snr;
    dst->sequence = (Sequence)src->sequence;
    dst->slot = src->slot;
    dst->freq_hz = src->freq_hz;
    dst->sync_score = src->sync_score;
}

// Needed by autoseq_engine
static char queued_msg[MAX_MSG_LEN];
void queue_custom_text(const char *tx_msg) {
	strncpy(queued_msg, tx_msg, sizeof(queued_msg));
}

// SiLabs.c
void output_enable(enum si5351_clock clk, uint8_t enable) {}

// Enhanced SysTick & tick counter for FT8 timing
static uint32_t mock_tick = 0;

// For modeling the cost of timing-critical functions
// Also simuates the behavior of BSP_AUDIO_IN_* callback
// If crossing the 40ms boundary, set DSP_Flag
// Use prime number to simulate jitters
void advance_mock_tick(uint32_t ms) {
    if (mock_tick % 40 + ms >= 40) {
        DSP_Flag = 1;
    }
    mock_tick += ms;
}

uint32_t HAL_GetTick(void) {
    advance_mock_tick(1);
    return mock_tick;
}

void HAL_Delay(uint32_t ms) {
    mock_tick += ms;
}

// Initialize test data and configuration
static void init_test_data(void) {
    if (!test_data_loaded) {
        // Try to load from JSON file using C++ parser
        if (load_test_data_json(test_data_file, &test_data)) {
            // Use loaded config
            strncpy(config_my_callsign, test_data.config.my_callsign, sizeof(config_my_callsign)-1);
            strncpy(config_my_grid, test_data.config.my_grid, sizeof(config_my_grid)-1);
            strncpy(config_dx_callsign, test_data.config.dx_callsign, sizeof(config_dx_callsign)-1);
            strncpy(config_dx_grid, test_data.config.dx_grid, sizeof(config_dx_grid)-1);
			target_slot = !test_data.config.tx_on_even;
        }
        
        // Set global variables
        strncpy(Station_Call, config_my_callsign, sizeof(Station_Call)-1);
        strncpy(Locator, config_my_grid, sizeof(Locator)-1);
        
        
        test_data_loaded = true;

		// init autoseq again
	    autoseq_init();
    }
}

// Replace the real ft8_decode() to feed our test data
int ft8_decode(void) {
    printf("d\n");
	int period_index = ft8_time / 30000;
    
    // Initialize test data on first call
    init_test_data();
    
    // If no test data loaded, return 0 (no messages)
    if (test_data.period_count == 0) {
        return 0;
    }
    
    // Handle TX_ON_EVEN logic
    bool tx_on_even = test_data.config.tx_on_even;
    
    if (slot_state != tx_on_even) {
        // printf("ERROR!! FT8_DECODE() CALLED IN WRONG SLOT!!\n");
        return 0;
    }
    
    
    // Check if period_index is within bounds (O(1) lookup)
    if (period_index >= test_data.period_count) {
        return -1;
    }
    
    // Direct array access - O(1) instead of O(N) search
    TestPeriod* current_period = &test_data.periods[period_index];
    
    // If no messages, return 0
    if (current_period->message_count == 0) {
        return 0;
    }
    
    // Copy messages to new_decoded array
    int num_decoded = 0;
    for (int i = 0; i < current_period->message_count && i < 25; i++) {
        convert_test_message_to_decode(&current_period->messages[i], &new_decoded[i]);
        // Set slot to current decode slot
        new_decoded[i].slot = slot_state;
        num_decoded++;
    }
    
    // Simulate decoding cost
    advance_mock_tick(307);
    return num_decoded;
}

// -----------------------------------------------------------------------------
// Stub for transmit trigger: dump queued messages to stdout
void setup_to_transmit_on_next_DSP_Flag(void) {
    printf("\n[=== TRANSMIT START ===]\n");
	printf("Transmitting: %s\n", queued_msg);
	ft8_xmit_counter = 0;
	ft8_xmit_delay = 0;  // Start transmission immediately for testing
	xmit_flag = 1;
}

// LCD mocks
sFONT Font16 = {
  NULL,
  11, /* Width */
  16, /* Height */
};

void BSP_LCD_DisplayStringAt(uint16_t Xpos, uint16_t Ypos, const uint8_t *Text, Text_AlignModeTypdef Mode)
{
    // Trying to clear the message or display empty string
    if (!Text || Text[0] == '\0' || Text[0] == ' ') {
        return;
    }
    printf("LCD: ");
    if (Xpos != 0) { // TX side
        printf("                     |");
    }
    printf("%s\n", Text);
}

void BSP_LCD_SetBackColor(uint32_t Color) {}
void BSP_LCD_SetFont(sFONT *fonts) {}
void BSP_LCD_SetTextColor(uint32_t Color) {}

// -----------------------------------------------------------------------------
// Optional: stub or disable any audio / codec / Si5351 dependencies
void Audio_Init(void) {}
void Codec_Start(void) {}
// etc.

// -----------------------------------------------------------------------------
// Hook the main loop's timing: we advance mock_tick there too
// If main() uses HAL_Delay or tick, we're covered.


// End of host_mocks.c
