/*
 * Copyright (c) 2015 by Thomas Trojer <thomas@trojer.net> and Leopold Sayous <leosayous@gmail.com>
 * Decawave DW1000 library for arduino.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @file DW1000Ranging.h
 * Arduino global library (header file) working with the DW1000 library
 * for the Decawave DW1000 UWB transceiver IC.
 *
 * @TODO
 * - remove or debugmode for Serial.print
 * - move strings to flash to reduce ram usage
 * - do not safe duplicate of pin settings
 * - maybe other object structure
 * - use enums instead of preprocessor constants
 */

#ifndef _DW1000Ranging_H_INCLUDED
#define _DW1000Ranging_H_INCLUDED

#include "DW1000.h"
#include "DW1000Time.h"
#include "DW1000Device.h" 
#include "DW1000Mac.h"

// messages used in the ranging protocol
#define POLL 0
#define POLL_ACK 1 //Response
#define RANGE 2
#define RANGE_REPORT 3
#define RANGE_FAILED 255
#define BLINK 4
#define RANGING_INIT 5
#define SYNC_FRAME 6

#define LEN_DATA 120

//Max devices we put in the networkDevices array ! Each DW1000Device is 74 Bytes in SRAM memory for now.
#ifndef MAX_DEVICES
#define MAX_DEVICES 16
#endif

// Shared UWB network id (PAN id) used by TAGs and anchors.
#ifndef DW1000_NETWORK_ID
#define DW1000_NETWORK_ID 0xDECA
#endif

// Extra random delay applied on TAG side to reduce synchronized transmissions.
#ifndef DW1000_TAG_TIMER_JITTER_MS
#define DW1000_TAG_TIMER_JITTER_MS 25
#endif

// Seconds between TAG transmission cycles (POLL/RANGE broadcast sequence).
#ifndef DW1000_TAG_TX_PERIOD_S
#define DW1000_TAG_TX_PERIOD_S 2
#endif

// TDMA slot duration and number of slots in one TDMA cycle.
#ifndef DW1000_TAG_TDMA_SLOT_MS
#define DW1000_TAG_TDMA_SLOT_MS 120
#endif

#ifndef DW1000_TAG_TDMA_SLOT_COUNT
#define DW1000_TAG_TDMA_SLOT_COUNT 8
#endif

// Extra slotting for TAG response phase (RANGE after POLL_ACK), in microseconds.
// Effective delayed TX = base + slotIndex * slotWidth.
#ifndef DW1000_TAG_RESPONSE_SLOT_US
#define DW1000_TAG_RESPONSE_SLOT_US 500
#endif

#ifndef DW1000_TAG_RESPONSE_SLOT_COUNT
#define DW1000_TAG_RESPONSE_SLOT_COUNT DW1000_TAG_TDMA_SLOT_COUNT
#endif

// Extra awake guard time after a transmission/reception to finish protocol exchanges.
#ifndef DW1000_TAG_ACTIVE_GUARD_MS
#define DW1000_TAG_ACTIVE_GUARD_MS 120
#endif

// Number of warm-up broadcast POLLs at the start of each burst.
// These run full TWR cycles to stabilise oscillator/PLL and refresh
// anchor timestamps before the "real" ranging rounds.
#ifndef DW1000_TAG_BURST_WARMUP_POLLS
#define DW1000_TAG_BURST_WARMUP_POLLS 1
#endif

// Number of actual ranging rounds per burst (main measurement cycles).
#ifndef DW1000_TAG_BURST_RANGING_ROUNDS
#define DW1000_TAG_BURST_RANGING_ROUNDS 4
#endif

// Inter-round gap (ms) within a burst.  Allows late RANGE_REPORTs to
// arrive and separates consecutive POLLs to avoid self-collision.
#ifndef DW1000_TAG_BURST_ROUND_GAP_MS
#define DW1000_TAG_BURST_ROUND_GAP_MS 50
#endif

// Wait time (ms) after the BLINK at the start of each burst before sending
// the first POLL.  Anchors stagger RANGING_INIT by ~2 ms each; give enough
// time for all to reply before the TAG begins polling.
#ifndef DW1000_TAG_BURST_BLINK_WAIT_MS
#define DW1000_TAG_BURST_BLINK_WAIT_MS 50
#endif

// Auto-timing mode: when set to 1 the library derives the reply-delay,
// TDMA slot and TX period automatically from the configured maximum
// number of anchors and tags at startAsTag() time.
// Individual -D overrides remain active when auto-timing is disabled.
#ifndef DW1000_AUTO_TIMING
#define DW1000_AUTO_TIMING 0
#endif

// Input parameters for the auto-timing calculation.
// Set these to the worst-case (maximum) counts you expect to deploy.
#ifndef DW1000_AUTO_TIMING_MAX_ANCHORS
#define DW1000_AUTO_TIMING_MAX_ANCHORS 5
#endif

#ifndef DW1000_AUTO_TIMING_MAX_TAGS
#define DW1000_AUTO_TIMING_MAX_TAGS 8
#endif

// TAG power-save mode while waiting next slot/period:
// 0 = disabled, 1 = idle, 2 = deepSleep/spiWakeup.
#ifndef DW1000_TAG_POWER_SAVE_MODE
#define DW1000_TAG_POWER_SAVE_MODE 1
#endif

#ifndef DW1000_SYNC_PERIOD_MS
#define DW1000_SYNC_PERIOD_MS 1000
#endif

#ifndef DW1000_SYNC_TIMEOUT_MS
#define DW1000_SYNC_TIMEOUT_MS 5000
#endif

#ifndef DW1000_TAG_REQUIRE_SYNC
#define DW1000_TAG_REQUIRE_SYNC 1
#endif

//Default Pin for module:
#define DEFAULT_RST_PIN 4
#define DEFAULT_SPI_SS_PIN 10

//Default value
//in ms  –  Must exceed the longest possible ranging cycle at the
//          configured data rate.  At 110 kbps a single POLL→RANGE_REPORT
//          exchange with 5 anchors can easily take 300+ ms.
#define DEFAULT_RESET_PERIOD 1000
//in us  –  Separate anchor response timings.
// POLL_ACK uses odd slots: (2*i+1) * DW1000_ANCHOR_POLL_ACK_BASE_DELAY_US.
// Effective gap between consecutive anchors = 2 * base.
#ifndef DW1000_ANCHOR_POLL_ACK_BASE_DELAY_US
#define DW1000_ANCHOR_POLL_ACK_BASE_DELAY_US 5000
#endif

// RANGE_REPORT is slightly larger than POLL_ACK, so it can use a slightly
// bigger base delay while preserving the same slot ordinal (1, 3, 5, ...).
#ifndef DW1000_ANCHOR_RANGE_REPORT_BASE_DELAY_US
#define DW1000_ANCHOR_RANGE_REPORT_BASE_DELAY_US 6000
#endif

// RANGING_INIT is sent after BLINK discovery. Anchors are staggered with a
// simple linear slot (i * this value), so this should be >= the frame airtime
// plus a small processing margin.
#ifndef DW1000_ANCHOR_RANGING_INIT_SLOT_US
#define DW1000_ANCHOR_RANGING_INIT_SLOT_US 5000
#endif

// Backward-compatible fallback used where a single base delay is needed.
#ifndef DEFAULT_REPLY_DELAY_TIME
#define DEFAULT_REPLY_DELAY_TIME DW1000_ANCHOR_POLL_ACK_BASE_DELAY_US
#endif
//sketch type (anchor or tag)
#define TAG 0
#define ANCHOR 1

//default timer delay  –  Must cover the full round-trip at 110 kbps.
//  Base 150 ms + per-device overhead added at runtime in transmitPoll/Range.
#define DEFAULT_TIMER_DELAY 1

// Maximum physically plausible range (metres).  Measurements beyond this
// threshold are discarded as invalid (multipath, clock errors, etc.).
// DW1000 theoretical max is ~290 m indoors / ~60 m NLOS; 300 m provides margin.
#define MAX_VALID_RANGE_METERS 300.0f

//debug mode
#ifndef DEBUG
#define DEBUG false
#endif


class DW1000RangingClass {
public:
	//variables
	// data buffer
	static byte data[LEN_DATA];
	
	//initialisation
	static void    initCommunication(uint8_t myRST = DEFAULT_RST_PIN, uint8_t mySS = DEFAULT_SPI_SS_PIN, uint8_t myIRQ = 2);
    void initCommunication(uint8_t myRST, uint8_t mySS, uint8_t myIRQ, uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t ss);
    static void configureNetwork(uint16_t deviceAddress, uint16_t networkId, const byte mode[]);
    static void    generalStart();
	static void    startAsAnchor(char address[], const byte mode[], const bool randomShortAddress = true);
	static void    startAsTag(char address[], const byte mode[], const bool randomShortAddress = true);
	static boolean addNetworkDevices(DW1000Device* device, boolean shortAddress);
	static boolean addNetworkDevices(DW1000Device* device);
	static void    removeNetworkDevices(int16_t index);
	
	//setters
	static void setReplyTime(uint16_t replyDelayTimeUs);
	static void setResetPeriod(uint32_t resetPeriod);
	
	//getters
	static byte* getCurrentAddress() { return _currentAddress; };
	
	static byte* getCurrentShortAddress() { return _currentShortAddress; };
	
	static uint8_t getNetworkDevicesNumber() { return _networkDevicesNumber; };

	static bool isTagBurstActive() { return _tagBurstActive; };
	
	//ranging functions
	static int16_t detectMessageType(byte datas[]); // TODO check return type
	static void loop();
	static void useRangeFilter(boolean enabled);
	// Re-arm receiver mode explicitly (useful after runtime reconfiguration).
	static void forceReceiverMode();
	// Diagnostics helpers to inspect link health after resets.
	static uint32_t getRxEventCount() { return _rxEventCount; };
	static uint32_t getTxEventCount() { return _txEventCount; };
	static uint32_t getLastRxEventMs() { return _lastRxEventMs; };
	static uint32_t getLastTxEventMs() { return _lastTxEventMs; };
	// Used for the smoothing algorithm (Exponential Moving Average). newValue must be >= 2. Default 15.
	static void setRangeFilterValue(uint16_t newValue);
	
	//Handlers:
	static void attachNewRange(void (* handleNewRange)(void)) { _handleNewRange = handleNewRange; };
	
	static void attachBlinkDevice(void (* handleBlinkDevice)(DW1000Device*)) { _handleBlinkDevice = handleBlinkDevice; };
	
	static void attachNewDevice(void (* handleNewDevice)(DW1000Device*)) { _handleNewDevice = handleNewDevice; };
	
	static void attachInactiveDevice(void (* handleInactiveDevice)(DW1000Device*)) { _handleInactiveDevice = handleInactiveDevice; };
	
	
	
	static DW1000Device* getDistantDevice();
	static DW1000Device* searchDistantDevice(byte shortAddress[]);
	
	//FOR DEBUGGING
	static void visualizeDatas(byte datas[]);


private:
	//other devices in the network
	static DW1000Device _networkDevices[MAX_DEVICES];
	static volatile uint8_t _networkDevicesNumber;
	static int16_t      _lastDistantDevice;
	static byte         _currentAddress[8];
	static byte         _currentShortAddress[2];
	static byte         _lastSentToShortAddress[2];
	static DW1000Mac    _globalMac;
	static int32_t      timer;
	static int16_t      counterForBlink;
	
	//Handlers:
	static void (* _handleNewRange)(void);
	static void (* _handleBlinkDevice)(DW1000Device*);
	static void (* _handleNewDevice)(DW1000Device*);
	static void (* _handleInactiveDevice)(DW1000Device*);
	
	//sketch type (tag or anchor)
	static int16_t          _type; //0 for tag and 1 for anchor
	// TODO check type, maybe enum?
	// message flow state
	static volatile byte    _expectedMsgId;
	// message sent/received state
	static volatile boolean _sentAck;
	static volatile boolean _receivedAck;
	// Diagnostics counters and last event timestamps.
	static volatile uint32_t _rxEventCount;
	static volatile uint32_t _txEventCount;
	static uint32_t _lastRxEventMs;
	static uint32_t _lastTxEventMs;
	// protocol error state
	static boolean          _protocolFailed;
	// reset line to the chip
	static uint8_t     _RST;
	static uint8_t     _SS;
	// watchdog and reset period
	static uint32_t    _lastActivity;
	static uint32_t    _resetPeriod;
	// reply times (same on both sides for symm. ranging)
	static uint16_t     _replyDelayTimeUS;
	//timer Tick delay
	static uint16_t     _timerDelay;
	// ranging counter (per second)
	static uint16_t     _successRangingCount;
	static uint32_t    _rangingCountPeriod;
	//ranging filter
	static volatile boolean _useRangeFilter;
	static uint16_t         _rangeFilterValue;
	// TAG scheduling and low-power
	static uint32_t _tagTxPeriodMs;
	static uint16_t _tagTDMASlotMs;
	static uint8_t  _tagTDMASlotCount;
	static uint8_t  _tagOwnSlotIndex;
	static uint32_t _tagNextTxDueMs;
	static uint32_t _tagKeepAwakeUntilMs;
	static boolean  _tagPowerSaveActive;
	// Deadline by which RANGE must be sent even if not all POLL_ACKs have arrived.
	// Set when a broadcast POLL is sent; cleared when RANGE is actually transmitted.
	// 0 = no deadline active.
	static uint32_t _tagPollAckDeadlineMs;
	// Burst state
	static bool     _tagBurstActive;
	static bool     _tagBurstBlinkPhase;       // true while waiting for RANGING_INITs after burst BLINK
	static uint32_t _tagBurstBlinkDeadlineMs;  // when BLINK phase ends
	static uint8_t  _tagBurstRoundsRemaining;
	static uint8_t  _tagBurstWarmupRemaining;
	static uint32_t _tagBurstRoundDeadlineMs;
	static uint32_t _baseReplyDelayUS;         // POLL_ACK base delay (µs)
	static uint32_t _rangeReportBaseDelayUS;   // RANGE_REPORT base delay (µs)
	static uint32_t _rangingInitSlotUS;        // RANGING_INIT slot spacing (µs)
	static boolean  _anchorMasterEnabled;
	static uint32_t _syncPeriodMs;
	static uint32_t _anchorNextSyncMs;
	static boolean  _tagSyncValid;
	static uint32_t _tagLastSyncRxMs;
	static int32_t  _tagEpochOffsetMs;
	//_bias correction
	static char  _bias_RSL[17]; // TODO remove or use
	//17*2=34 bytes in SRAM
	static int16_t _bias_PRF_16[17]; // TODO remove or use
	//17 bytes in SRAM
	static char  _bias_PRF_64[17]; // TODO remove or use
	static bool  _rangeFailed; // Flag to indicate if a RANGE_FAILED message was received in the current cycle.
	
	//methods
	static void handleSent();
	static void handleReceived();
	static void noteActivity();
	static void resetInactive();
	
	//global functions:
	static void checkForReset();
	static void checkForInactiveDevices();
	static void copyShortAddress(byte address1[], byte address2[]);
	static bool isTagTransmissionWindow(uint32_t nowMs);
	static void armTagNextTransmission(uint32_t nowMs);
	static void enterTagPowerSave();
	static void exitTagPowerSave();
	static void startNextBurstRound();
	static void completeBurstRound();
	static void computeAutoTiming(uint8_t maxAnchors, uint8_t maxTags);
	static void transmitSync();
	
	//for ranging protocole (ANCHOR)
	static void transmitInit();
	static void transmit(byte datas[], uint16_t dataLength);
	static void transmit(byte datas[], uint16_t dataLength, DW1000Time time);
	static void transmitBlink();
	static void transmitRangingInit(DW1000Device* myDistantDevice);
	static void transmitPollAck(DW1000Device* myDistantDevice);
	static void transmitRangeReport(DW1000Device* myDistantDevice);
	static void transmitRangeFailed(DW1000Device* myDistantDevice);
	static void receiver();
	
	//for ranging protocole (TAG)
	static void transmitPoll(DW1000Device* myDistantDevice);
	static void transmitRange(DW1000Device* myDistantDevice);
	
	//methods for range computation
	static void computeRangeAsymmetric(DW1000Device* myDistantDevice, DW1000Time* myTOF);
	
	static void timerTick();
	
	//Utils
	static float filterValue(float value, float previousValue, uint16_t numberOfElements);
};

extern DW1000RangingClass DW1000Ranging;

#endif
