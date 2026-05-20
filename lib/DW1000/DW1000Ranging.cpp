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
 * Arduino global library (source file) working with the DW1000 library 
 * for the Decawave DW1000 UWB transceiver IC.
 *
 * @TODO
 * - remove or debugmode for Serial.print
 * - move strings to flash to reduce ram usage
 * - do not safe duplicate of pin settings
 * - maybe other object structure
 * - use enums instead of preprocessor constants
 */


#include "DW1000Ranging.h"
#include "DW1000Device.h"

DW1000RangingClass DW1000Ranging;


//other devices we are going to communicate with which are on our network:
DW1000Device DW1000RangingClass::_networkDevices[MAX_DEVICES];
byte         DW1000RangingClass::_currentAddress[8];
byte         DW1000RangingClass::_currentShortAddress[2];
byte         DW1000RangingClass::_lastSentToShortAddress[2];
volatile uint8_t DW1000RangingClass::_networkDevicesNumber = 0; // TODO short, 8bit?
int16_t      DW1000RangingClass::_lastDistantDevice    = 0; // TODO short, 8bit?
DW1000Mac    DW1000RangingClass::_globalMac;

//module type (anchor or tag)
int16_t      DW1000RangingClass::_type; // TODO enum??

// message flow state
volatile byte    DW1000RangingClass::_expectedMsgId;

// range filter
volatile boolean DW1000RangingClass::_useRangeFilter = false;
uint16_t DW1000RangingClass::_rangeFilterValue = 15;

// TAG scheduling and low-power state

// Periodo entre transmisiones de rango por parte del tag. Se divide en slots para TDMA.
uint32_t DW1000RangingClass::_tagTxPeriodMs = (uint32_t)DW1000_TAG_TX_PERIOD_S * 1000UL;
// Tiempo dedicado a transmision de data por tag 
uint16_t DW1000RangingClass::_tagTDMASlotMs = DW1000_TAG_TDMA_SLOT_MS;
// Cantidad total de tags en la red, incluyendo el propio tag. Se usa para calcular el slot de cada tag.
uint8_t  DW1000RangingClass::_tagTDMASlotCount = DW1000_TAG_TDMA_SLOT_COUNT;
uint8_t  DW1000RangingClass::_tagOwnSlotIndex = 0;
uint32_t DW1000RangingClass::_tagNextTxDueMs = 0;
uint32_t DW1000RangingClass::_tagKeepAwakeUntilMs = 0;
boolean  DW1000RangingClass::_tagPowerSaveActive = false;
uint32_t DW1000RangingClass::_tagPollAckDeadlineMs = 0;
bool     DW1000RangingClass::_tagBurstActive = false;
bool     DW1000RangingClass::_tagBurstBlinkPhase = false;
uint32_t DW1000RangingClass::_tagBurstBlinkDeadlineMs = 0;
uint8_t  DW1000RangingClass::_tagBurstRoundsRemaining = 0;
uint8_t  DW1000RangingClass::_tagBurstWarmupRemaining = 0;
uint32_t DW1000RangingClass::_tagBurstRoundDeadlineMs = 0;
uint32_t DW1000RangingClass::_baseReplyDelayUS = DW1000_ANCHOR_POLL_ACK_BASE_DELAY_US;
uint32_t DW1000RangingClass::_rangeReportBaseDelayUS = DW1000_ANCHOR_RANGE_REPORT_BASE_DELAY_US;
uint32_t DW1000RangingClass::_rangingInitSlotUS = DW1000_ANCHOR_RANGING_INIT_SLOT_US;
boolean  DW1000RangingClass::_anchorMasterEnabled = DW1000_ANCHOR_MASTER_ENABLED;
uint32_t DW1000RangingClass::_syncPeriodMs = DW1000_SYNC_PERIOD_MS;
uint32_t DW1000RangingClass::_anchorNextSyncMs = 0;
boolean  DW1000RangingClass::_tagSyncValid = false;
uint32_t DW1000RangingClass::_tagLastSyncRxMs = 0;
int32_t  DW1000RangingClass::_tagEpochOffsetMs = 0;

// message sent/received state
volatile boolean DW1000RangingClass::_sentAck     = false;
volatile boolean DW1000RangingClass::_receivedAck = false;
volatile uint32_t DW1000RangingClass::_rxEventCount = 0;
volatile uint32_t DW1000RangingClass::_txEventCount = 0;
uint32_t DW1000RangingClass::_lastRxEventMs = 0;
uint32_t DW1000RangingClass::_lastTxEventMs = 0;

// protocol error state
boolean          DW1000RangingClass::_protocolFailed = false;

// timestamps to remember
int32_t            DW1000RangingClass::timer           = 0;
int16_t            DW1000RangingClass::counterForBlink = 0; // TODO 8 bit?


// data buffer
byte          DW1000RangingClass::data[LEN_DATA];
// reset line to the chip
uint8_t   DW1000RangingClass::_RST;
uint8_t   DW1000RangingClass::_SS;
// watchdog and reset period
uint32_t  DW1000RangingClass::_lastActivity;
uint32_t  DW1000RangingClass::_resetPeriod;
// reply times (same on both sides for symm. ranging)
uint16_t  DW1000RangingClass::_replyDelayTimeUS;
//timer delay
uint16_t  DW1000RangingClass::_timerDelay;
// ranging counter (per second)
uint16_t  DW1000RangingClass::_successRangingCount = 0;
uint32_t  DW1000RangingClass::_rangingCountPeriod  = 0;

//Flag para saber si ha recibido fallo
bool DW1000RangingClass::_rangeFailed = false;
//Here our handlers
void (* DW1000RangingClass::_handleNewRange)(void) = 0;
void (* DW1000RangingClass::_handleBlinkDevice)(DW1000Device*) = 0;
void (* DW1000RangingClass::_handleNewDevice)(DW1000Device*) = 0;
void (* DW1000RangingClass::_handleInactiveDevice)(DW1000Device*) = 0;

static const char* debugMessageTypeName(int16_t messageType) {
	switch(messageType) {
		case POLL: return "POLL";
		case POLL_ACK: return "POLL_ACK";
		case RANGE: return "RANGE";
		case RANGE_REPORT: return "RANGE_REPORT";
		case RANGE_FAILED: return "RANGE_FAILED";
		case BLINK: return "BLINK";
		case RANGING_INIT: return "RANGING_INIT";
		case SYNC_FRAME: return "SYNC_FRAME";
		default: return "UNKNOWN";
	}
}

static void debugPrintRawData(byte* frame, uint16_t length) {
	for(uint16_t i = 0; i < length; i++) {
		if(frame[i] < 0x10) {
			Serial.print('0');
		}
		Serial.print(frame[i], HEX);
		if(i + 1 < length) {
			Serial.print(' ');
		}
	}
	Serial.println();
}

/* ###########################################################################
 * #### Init and end #######################################################
 * ######################################################################### */

void DW1000RangingClass::initCommunication(uint8_t myRST, uint8_t mySS, uint8_t myIRQ, 
	uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t ss) {
	// reset line to the chip
	_RST              = myRST;
	_SS               = mySS;
	_resetPeriod      = DEFAULT_RESET_PERIOD;
	// reply times (same on both sides for symm. ranging)
	_replyDelayTimeUS = DEFAULT_REPLY_DELAY_TIME;
	_baseReplyDelayUS = DW1000_ANCHOR_POLL_ACK_BASE_DELAY_US;    
	_rangeReportBaseDelayUS = DW1000_ANCHOR_RANGE_REPORT_BASE_DELAY_US;
	_rangingInitSlotUS = DW1000_ANCHOR_RANGING_INIT_SLOT_US;
	//we set our timer delay
	_timerDelay       = DEFAULT_TIMER_DELAY;
	if(_tagTxPeriodMs == 0) {
		_tagTxPeriodMs = 1000;
	}
	if(_tagTDMASlotMs == 0) {
		_tagTDMASlotMs = 1;
	}
	if(_tagTDMASlotCount == 0) {
		_tagTDMASlotCount = 1;
	}
	
	
	DW1000.begin(myIRQ, myRST, sck, miso, mosi, ss);
	DW1000.select(mySS);
}


void DW1000RangingClass::configureNetwork(uint16_t deviceAddress, uint16_t networkId, const byte mode[]) {
	// general configuration
	DW1000.newConfiguration();
	DW1000.setDefaults();
	DW1000.setDeviceAddress(deviceAddress);
	DW1000.setNetworkId(networkId);
	DW1000.enableMode(mode);
	DW1000.commitConfiguration();
}

void DW1000RangingClass::generalStart() {
	// attach callback for (successfully) sent and received messages
	DW1000.attachSentHandler(handleSent);
	DW1000.attachReceivedHandler(handleReceived);
	// anchor starts in receiving mode, awaiting a ranging poll message
	
	
	if(DEBUG) {
		// DEBUG monitoring
		Serial.println("DW1000-arduino");
		// initialize the driver
		
		
		Serial.println("configuration..");
		// DEBUG chip info and registers pretty printed
		char msg[90];
		DW1000.getPrintableDeviceIdentifier(msg);
		Serial.print("Device ID: ");
		Serial.println(msg);
		DW1000.getPrintableExtendedUniqueIdentifier(msg);
		Serial.print("Unique ID: ");
		Serial.print(msg);
		char string[6];
		sprintf(string, "%02X:%02X", _currentShortAddress[0], _currentShortAddress[1]);
		Serial.print(" short: ");
		Serial.println(string);
		
		DW1000.getPrintableNetworkIdAndShortAddress(msg);
		Serial.print("Network ID & Device Address: ");
		Serial.println(msg);
		DW1000.getPrintableDeviceMode(msg);
		Serial.print("Device mode: ");
		Serial.println(msg);
	}
	
	
	// anchor starts in receiving mode, awaiting a ranging poll message
	receiver();
	// for first time ranging frequency computation
	_rangingCountPeriod = millis();
}


void DW1000RangingClass::startAsAnchor(char address[], const byte mode[], const bool randomShortAddress) {
	//save the address
	DW1000.convertToByte(address, _currentAddress);
	//write the address on the DW1000 chip
	DW1000.setEUI(address);
	Serial.print("device address: ");
	Serial.println(address);
	if (randomShortAddress) {
		//we need to define a random short address:
		randomSeed(analogRead(0));
		_currentShortAddress[0] = random(0, 256);
		_currentShortAddress[1] = random(0, 256);
	}
	else {
		// we use first two bytes in addess for short address
		_currentShortAddress[0] = _currentAddress[LEN_EUI-1];
		_currentShortAddress[1] = _currentAddress[LEN_EUI-2];
	}
	
	//we configur the network for mac filtering
	//(device Address, network ID, frequency)
	DW1000Ranging.configureNetwork(_currentShortAddress[0]*256+_currentShortAddress[1], DW1000_NETWORK_ID, mode);

	//general start:
	generalStart();
	
	//defined type as anchor
	_type = ANCHOR;
	_anchorNextSyncMs = millis() + 100;
	
	Serial.println("### ANCHOR ###");
	
}

void DW1000RangingClass::startAsTag(char address[], const byte mode[], const bool randomShortAddress) {
	//save the address
	DW1000.convertToByte(address, _currentAddress);
	//write the address on the DW1000 chip
	DW1000.setEUI(address);
	Serial.print("device address: ");
	Serial.println(address);
	Serial.println(_currentAddress[0]);
	Serial.println(_currentAddress[1]);
	if (randomShortAddress) {
		//we need to define a random short address:
		randomSeed(analogRead(0));
		_currentShortAddress[0] = random(0, 256);
		_currentShortAddress[1] = random(0, 256);
	}
	else {
		// we use first two bytes in addess for short address
		_currentShortAddress[0] = _currentAddress[LEN_EUI-1];
		_currentShortAddress[1] = _currentAddress[LEN_EUI-2];
	}
	
	//we configur the network for mac filtering
	//(device Address, network ID, frequency)
	DW1000Ranging.configureNetwork(_currentShortAddress[0]*256+_currentShortAddress[1], DW1000_NETWORK_ID, mode);
	
	generalStart();
	//defined type as tag
	_type = TAG;

	randomSeed(micros());
	counterForBlink = 0; // not used for TAG burst logic
	timer = millis() + random(0, DEFAULT_TIMER_DELAY + 1);

	// ── Auto-timing: must run before slot-index and TX-deadline computation ──
	// so that _tagTDMASlotCount and _tagTxPeriodMs are already updated.
#if DW1000_AUTO_TIMING
	computeAutoTiming(DW1000_AUTO_TIMING_MAX_ANCHORS, DW1000_AUTO_TIMING_MAX_TAGS);
#endif

	uint16_t shortAddressValue = _currentShortAddress[0]*256+_currentShortAddress[1];
	_tagOwnSlotIndex = shortAddressValue % _tagTDMASlotCount;
	_tagNextTxDueMs = millis() + random(0, _tagTxPeriodMs + 1);
	_tagKeepAwakeUntilMs = millis() + DW1000_TAG_ACTIVE_GUARD_MS;
	_tagPowerSaveActive = false;
	_tagSyncValid = false;
	_tagLastSyncRxMs = 0;
	_tagEpochOffsetMs = 0;
	_tagPollAckDeadlineMs = 0;		// deadline for receiving the POLL_ACK after sending a POLL, used to detect missed responses and recover without waiting for a full tag Tx period
	_tagBurstActive = false;       	// indica si un burst está en curso
	_tagBurstBlinkPhase = false;   	// true mientras el TAG espera respuestas al BLINK
	_tagBurstBlinkDeadlineMs = 0;  	// cuándo termina la fase BLINK
	_tagBurstRoundsRemaining = 0; 	//rondas restantes (warmup + ranging)
	_tagBurstWarmupRemaining = 0; 	// POLLs de warmup restantes
	_tagBurstRoundDeadlineMs = 0;  	//  deadline calculado para cada ronda
	Serial.print("[DW1000 TAG] TDMA slot index: ");
	Serial.print(_tagOwnSlotIndex);
	Serial.print("/");
	Serial.println();
	
	Serial.println("### TAG ###");
}

boolean DW1000RangingClass::addNetworkDevices(DW1000Device* device, boolean shortAddress) {
	boolean   addDevice = true;
	//we test our network devices array to check
	//we don't already have it
	for(uint8_t i = 0; i < _networkDevicesNumber; i++) {
		if(_networkDevices[i].isAddressEqual(device) && !shortAddress) {
			//the device already exists
			addDevice = false;
			return false;
		}
		else if(_networkDevices[i].isShortAddressEqual(device) && shortAddress) {
			//the device already exists
			addDevice = false;
			return false;
		}
		
	}
	
	if(addDevice) {
		if(_networkDevicesNumber >= MAX_DEVICES) {
			return false;
		}
		device->setRange(0);
		device->noteActivity();
		memcpy(&_networkDevices[_networkDevicesNumber], device, sizeof(DW1000Device));
		_networkDevices[_networkDevicesNumber].noteActivity();
		_networkDevices[_networkDevicesNumber].setIndex(_networkDevicesNumber);
		_networkDevicesNumber++;
		return true;
	}
	
	return false;
}

boolean DW1000RangingClass::addNetworkDevices(DW1000Device* device) {
	boolean addDevice = true;
	//we test our network devices array to check
	//we don't already have it
	for(uint8_t i = 0; i < _networkDevicesNumber; i++) {
		if(_networkDevices[i].isAddressEqual(device) && _networkDevices[i].isShortAddressEqual(device)) {
			//the device already exists
			addDevice = false;
			return false;
		}
		
	}
	
	if(addDevice) {
		if(_networkDevicesNumber >= MAX_DEVICES) {
			return false;
		}
		device->noteActivity();
		memcpy(&_networkDevices[_networkDevicesNumber], device, sizeof(DW1000Device));
		_networkDevices[_networkDevicesNumber].noteActivity();
		_networkDevices[_networkDevicesNumber].setIndex(_networkDevicesNumber);
		_networkDevicesNumber++;
		return true;
	}
	
	return false;
}

void DW1000RangingClass::removeNetworkDevices(int16_t index) {
	//if we have just 1 element
	if(_networkDevicesNumber == 1) {
		_networkDevicesNumber = 0;
	}
	else if(index == _networkDevicesNumber-1) //if we delete the last element
	{
		_networkDevicesNumber--;
	}
	else {
		//we translate all the element wich are after the one we want to delete.
		for(int16_t i = index; i < _networkDevicesNumber-1; i++) { // TODO 8bit?
			memcpy(&_networkDevices[i], &_networkDevices[i+1], sizeof(DW1000Device));
			_networkDevices[i].setIndex(i);
		}
		_networkDevicesNumber--;
	}
}

/* ###########################################################################
 * #### Setters and Getters ##################################################
 * ######################################################################### */

//setters
void DW1000RangingClass::setReplyTime(uint16_t replyDelayTimeUs) { _replyDelayTimeUS = replyDelayTimeUs; }

void DW1000RangingClass::setResetPeriod(uint32_t resetPeriod) { _resetPeriod = resetPeriod; }


DW1000Device* DW1000RangingClass::searchDistantDevice(byte shortAddress[]) {
	//we compare the 2 bytes address with the others
	for(uint16_t i = 0; i < _networkDevicesNumber; i++) { // TODO 8bit?
		if(memcmp(shortAddress, _networkDevices[i].getByteShortAddress(), 2) == 0) {
			//we have found our device !
			return &_networkDevices[i];
		}
	}
	
	return nullptr;
}

DW1000Device* DW1000RangingClass::getDistantDevice() {
	//we get the device which correspond to the message which was sent (need to be filtered by MAC address)
	if (_lastDistantDevice >= 0 && _lastDistantDevice < _networkDevicesNumber) {
		return &_networkDevices[_lastDistantDevice];
	}
	return nullptr;
}


/* ###########################################################################
 * #### Public methods #######################################################
 * ######################################################################### */

void DW1000RangingClass::checkForReset() {
	uint32_t curMillis = millis();
	if(!_sentAck && !_receivedAck) {
		// check if inactive
		// if(curMillis-_lastActivity > _resetPeriod) {
		if(curMillis-_lastActivity > _resetPeriod) {
			resetInactive();
			// Serial.println("DW1000Ranging: reset due to inactivity");
		}
		return; // TODO cc
	}
}

void DW1000RangingClass::checkForInactiveDevices() {
	for(uint8_t i = 0; i < _networkDevicesNumber; i++) {
		if(_networkDevices[i].isInactive()) {
			if(_handleInactiveDevice != 0) {
				(*_handleInactiveDevice)(&_networkDevices[i]);
			}
			//we need to delete the device from the array:
			removeNetworkDevices(i);
			i--; // we need to check the new device which is at this index after deletion
		}
	}
}

// TODO check return type
int16_t DW1000RangingClass::detectMessageType(byte datas[]) {
	if(datas[0] == FC_1_BLINK) {
		return BLINK;
	}
	else if(datas[0] == FC_1 && datas[1] == FC_2) {
		//we have a long MAC frame message (ranging init)
		return datas[LONG_MAC_LEN];
	}
	else if(datas[0] == FC_1 && datas[1] == FC_2_SHORT) {
		//we have a short mac frame message (poll, range, range report, etc..)
		return datas[SHORT_MAC_LEN];
	}
	return -1; 
}

void DW1000RangingClass::loop() {
	//we check if needed to reset !
	checkForReset();
	// uint32_t time = millis(); // TODO other name - too close to "timer"

	// if(time-timer > _timerDelay) {
	// 	timer = time;
	// 	timerTick();
	// }
	timerTick();

	if(_sentAck) {
		_sentAck = false;
		// TODO cc
		int messageType = detectMessageType(data);
		if(messageType != POLL_ACK && messageType != POLL && messageType != RANGE)
			return;
		
		//A msg was sent. We launch the ranging protocole when a message was sent
		if(_type == ANCHOR) {
			if(messageType == POLL_ACK) {
				DW1000Device* myDistantDevice = searchDistantDevice(_lastSentToShortAddress);
				
				if (myDistantDevice) {
					DW1000.getTransmitTimestamp(myDistantDevice->timePollAckSent);
				}
			}
		}
		else if(_type == TAG) {
			if(messageType == POLL) {
				DW1000Time timePollSent;
				DW1000.getTransmitTimestamp(timePollSent);
				//if the last device we send the POLL is broadcast:
				if(_lastSentToShortAddress[0] == 0xFF && _lastSentToShortAddress[1] == 0xFF) {
					//we save the value for all the devices !
					for(uint16_t i = 0; i < _networkDevicesNumber; i++) {
						_networkDevices[i].timePollSent = timePollSent;
					}
				}
				else {
					//we search the device associated with the last send address
					DW1000Device* myDistantDevice = searchDistantDevice(_lastSentToShortAddress);
					//we save the value just for one device
					if (myDistantDevice) {
						myDistantDevice->timePollSent = timePollSent;
					}
				}
			}
			else if(messageType == RANGE) {
				DW1000Time timeRangeSent;
				DW1000.getTransmitTimestamp(timeRangeSent);
				//if the last device we send the POLL is broadcast:
				if(_lastSentToShortAddress[0] == 0xFF && _lastSentToShortAddress[1] == 0xFF) {
					//we save the value for all the devices !
					for(uint16_t i = 0; i < _networkDevicesNumber; i++) {
						_networkDevices[i].timeRangeSent = timeRangeSent;
					}
				}
				else {
					//we search the device associated with the last send address
					DW1000Device* myDistantDevice = searchDistantDevice(_lastSentToShortAddress);
					//we save the value just for one device
					if (myDistantDevice) {
						myDistantDevice->timeRangeSent = timeRangeSent;
					}
				}
				
			}
		}
		
	}
	
	//check for new received message
	if(_receivedAck) {
		// Serial.println("RX ACK");
		// Serial.println(micros());
		_receivedAck = false;
		//we read the datas from the modules:
		// get message and parse
		uint16_t dataLength = DW1000.getDataLength();
		if(dataLength > LEN_DATA) {
			dataLength = LEN_DATA;
		}
		memset(data, 0, LEN_DATA);
		DW1000.getData(data, dataLength);

		// Early address filter — discard frames not meant for us before any parsing.
		// Short MAC frames: destination at data[5..6], stored reversed (data[5]=addr[1], data[6]=addr[0]).
		// Long MAC frames:  destination 8-byte address at data[5..12], stored reversed.
		// Serial.print("Received frame: ");
		// debugPrintRawData(data, dataLength);
		if(data[0] == FC_1 && data[1] == FC_2_SHORT) {
			bool isBroadcast = (data[5] == 0xFF && data[6] == 0xFF);
			bool isForUs     = (data[5] == _currentShortAddress[1] && data[6] == _currentShortAddress[0]);
			if(!isBroadcast && !isForUs) {
				receiver();
				return;
			}
		} else if(data[0] == FC_1 && data[1] == FC_2) {
			// Long MAC (RANGING_INIT): anchors never receive valid long MAC frames.
			// TAGs check the full 8-byte destination address.
			if(_type == ANCHOR) {
				receiver();
				return;
			}
			bool isForUs = true;
			for(uint8_t i = 0; i < 8; i++) {
				if(data[5 + i] != _currentAddress[7 - i]) { isForUs = false; break; }
			}
			if(!isForUs) {
				receiver();
				return;
			}
		}

		int messageType = detectMessageType(data);
				// Serial.println(debugMessageTypeName(messageType));
		// we have just received a BLINK message from tag
		if (_type == ANCHOR) {
			if (messageType == BLINK){
				// Serial.println("Received BLINK");
				byte address[8];
				byte shortAddress[2];
				_globalMac.decodeBlinkFrame(data, address, shortAddress);
				//we crate a new device with th tag
				DW1000Device myTag(address, shortAddress);
				bool isNewTag = addNetworkDevices(&myTag);
				if(isNewTag) {
					if(_handleBlinkDevice != 0) {
						(*_handleBlinkDevice)(&myTag);
					}
				} else {
					// TAG already known — refresh its activity so it is not
					// removed while still actively blinking.
					DW1000Device* existing = searchDistantDevice(shortAddress);
					if(existing) {
						existing->noteActivity();
					}
				}
				// Reply with RANGING_INIT for both new and known tags.
				transmitRangingInit(&myTag);
				noteActivity();
				_expectedMsgId = POLL;
				// Serial.print("RI ");
			}
			else {
				byte address[2];
				_globalMac.decodeShortMACFrame(data, address);
				//we get the device which correspond to the message which was sent (need to be filtered by MAC address)
				DW1000Device* myDistantDevice = searchDistantDevice(address);

				if(myDistantDevice == nullptr) {
					// Device not registered (e.g. POLL/RANGE arrived before BLINK).
					// Ignore and re-arm receiver.
					receiver();
					return;
				} 

				if(messageType != _expectedMsgId) {
					Serial.print("U: ");
					Serial.print(debugMessageTypeName(messageType));
					Serial.print(", E: ");
					Serial.println(debugMessageTypeName(_expectedMsgId));
					// unexpected message, start over again (except if already POLL)
					_expectedMsgId = POLL;
					// _protocolFailed = true;
				}

				if(messageType == POLL) {
					//we receive a POLL which is a broacast message
					//we need to grab info about it
					// Serial.println(millis());
					int16_t numberDevices = 0;
					memcpy(&numberDevices, data+SHORT_MAC_LEN+1, 1);
					
					for(uint16_t i = 0; i < numberDevices; i++) {
						//we need to test if this value is for us:
						//we grab the mac address of each devices:
						byte shortAddress[2];
						memcpy(shortAddress, data+SHORT_MAC_LEN+2+i*4, 2);
						
						//we test if the short address is our address
						if(shortAddress[0] == _currentShortAddress[0] && shortAddress[1] == _currentShortAddress[1]) {
							//we grab the replytime wich is for us
							uint16_t replyTime;
							memcpy(&replyTime, data+SHORT_MAC_LEN+2+i*4+2, 2);
							//we configure our replyTime;
							_replyDelayTimeUS = replyTime;
							
							// on POLL we (re-)start, so no protocol failure
							_protocolFailed = false;
							
							DW1000.getReceiveTimestamp(myDistantDevice->timePollReceived);
							//we note activity for our device:
							myDistantDevice->noteActivity();
							//we indicate our next receive message for our ranging protocole
							_expectedMsgId = RANGE;
							transmitPollAck(myDistantDevice);
							noteActivity();
							// Serial.println("P");
							// receiver();							return;
						}				
					}
				}
				else if(messageType == RANGE) {
					//we receive a RANGE which is a broacast message
					//we need to grab info about it
					uint8_t numberDevices = 0;
					memcpy(&numberDevices, data+SHORT_MAC_LEN+1, 1);
					
					
					for(uint8_t i = 0; i < numberDevices; i++) {
						//we need to test if this value is for us:
						//we grab the mac address of each devices:
						byte shortAddress[2];
						memcpy(shortAddress, data+SHORT_MAC_LEN+2+i*17, 2);
						
						//we test if the short address is our address
						if(shortAddress[0] == _currentShortAddress[0] && shortAddress[1] == _currentShortAddress[1]) {
							//we grab the replytime wich is for us
							DW1000.getReceiveTimestamp(myDistantDevice->timeRangeReceived);
							noteActivity();
							_expectedMsgId = POLL;
							
							if(!_protocolFailed) {
								
								myDistantDevice->timePollSent.setTimestamp(data+SHORT_MAC_LEN+4+17*i);
								myDistantDevice->timePollAckReceived.setTimestamp(data+SHORT_MAC_LEN+9+17*i);
								myDistantDevice->timeRangeSent.setTimestamp(data+SHORT_MAC_LEN+14+17*i);
								
								// (re-)compute range as two-way ranging is done
								DW1000Time myTOF;
								computeRangeAsymmetric(myDistantDevice, &myTOF); // CHOSEN RANGING ALGORITHM
								
								float distance = myTOF.getAsMeters();
								// Serial.println(distance);
								// Reject clearly invalid ranges (negative or
								// unreasonably large values caused by clock
								// drift, multipath, or timestamp errors).
								if(distance < 0.0f || distance > MAX_VALID_RANGE_METERS) {
									Serial.println("IR: ");
									// Serial.println(distance);
									transmitRangeFailed(myDistantDevice);

									return;
								}
								
								if (_useRangeFilter) {
									//Skip first range
									if (myDistantDevice->getRange() != 0.0f) {
										distance = filterValue(distance, myDistantDevice->getRange(), _rangeFilterValue);
									}
								}
								
								myDistantDevice->setRXPower(DW1000.getReceivePower());
								myDistantDevice->setRange(distance);
								
								myDistantDevice->setFPPower(DW1000.getFirstPathPower());
								myDistantDevice->setQuality(DW1000.getReceiveQuality());
								
								//we send the range to TAG
								transmitRangeReport(myDistantDevice);
								// Serial.println("R");
								//we have finished our range computation. We send the corresponding handler
								_lastDistantDevice = myDistantDevice->getIndex();
								if(_handleNewRange != 0) {
									(*_handleNewRange)();
								}
								
							}
							else {
								transmitRangeFailed(myDistantDevice);
							}

							return;
						}
					}
				}
			}
		}
		else if(_type == TAG) {
			// Serial.println(debugMessageTypeName(messageType));	
			if(messageType == RANGING_INIT) {
				byte address[2];
				_globalMac.decodeLongMACFrame(data, address);
				//we crate a new device with the anchor
				DW1000Device myAnchor(address, true);
				// Serial.print("[DW1000 TAG] RANGING_INIT recibido de ");
				// Serial.print(address[0], HEX);
				if(addNetworkDevices(&myAnchor, true)) {
					if(_handleNewDevice != 0) {
						(*_handleNewDevice)(&myAnchor);
					}
				}
				noteActivity();
				// Serial.println("RANGING_INIT recibido, respondiendo con POLL_ACK");
				// If the master anchor embedded its epoch in this frame, apply sync.
				if(dataLength >= LONG_MAC_LEN + 5) {
					uint32_t masterEpochMs = 0;
					memcpy(&masterEpochMs, data + LONG_MAC_LEN + 1, sizeof(uint32_t));
					uint32_t localNowMs = millis();
					_tagEpochOffsetMs = (int32_t)masterEpochMs - (int32_t)localNowMs;
					_tagLastSyncRxMs = localNowMs;
					_tagSyncValid = true;
					for(uint8_t i = 0; i < _networkDevicesNumber; i++) {
						_networkDevices[i].noteActivity();
					}
					_tagKeepAwakeUntilMs = localNowMs + DW1000_TAG_ACTIVE_GUARD_MS;
					if(DEBUG) {
						Serial.print("[DW1000 TAG] SYNC en RANGING_INIT. Offset(ms): ");
						Serial.println(_tagEpochOffsetMs);
					}
				}
			}
			else 
			{
			//we have a short mac layer frame !
				byte address[2];
				_globalMac.decodeShortMACFrame(data, address);
				//we get the device which correspond to the message which was sent (need to be filtered by MAC address)
				DW1000Device* myDistantDevice = searchDistantDevice(address);
				
				if((_networkDevicesNumber == 0) || (myDistantDevice == nullptr)) {
					//we don't have the short address of the device in memory
					if (DEBUG) {
						Serial.println("Not found");
					}
					return;
				}
				else if(messageType == SYNC_FRAME) {
					uint32_t masterEpochMs = 0;
					//Serial.println("SYNC_FRAME recibido, actualizando reloj interno del TAG");
					memcpy(&masterEpochMs, data+SHORT_MAC_LEN+1, sizeof(uint32_t));
					uint32_t localNowMs = millis();
					_tagEpochOffsetMs = (int32_t)masterEpochMs - (int32_t)localNowMs;
					_tagLastSyncRxMs = localNowMs;
					_tagSyncValid = true;
					for(uint8_t i = 0; i < _networkDevicesNumber; i++) {
						_networkDevices[i].noteActivity();
					}
					_tagKeepAwakeUntilMs = localNowMs + DW1000_TAG_ACTIVE_GUARD_MS;
					if(DEBUG) {
						Serial.print("[DW1000 TAG] SYNC recibido. Offset(ms): ");
						Serial.println(_tagEpochOffsetMs);
					}
					return;
				}

				else if(messageType == POLL_ACK || messageType == RANGE_REPORT || messageType == RANGE_FAILED) {
					myDistantDevice->noteActivity();
				}

				if(messageType == RANGE_FAILED) {
					//not needed as we have a timer;
					// _expectedMsgId = POLL_ACK;
					// Serial.print("[DW1000 TAG] RANGE_FAILED recibido de ");
					// Serial.print(myDistantDevice->getByteShortAddress()[0], HEX);
					// Serial.print(":");
					// Serial.println(myDistantDevice->getByteShortAddress()[1], HEX);
					_rangeFailed = true;
					// small delay to avoid busy loop if anchor keeps sending RANGE_FAILED
					return;
				}
				// get message and parse
				if(messageType != _expectedMsgId) {
					// unexpected message, start over again'
					// Serial.print("[DW1000 TAG] Unexpected message. Expected ");
					// Serial.println(debugMessageTypeName(_expectedMsgId));
					// Serial.print("[DW1000 TAG] Unexpected message type: ");
					// Serial.println(debugMessageTypeName(messageType));
					// Serial.print("[DW1000 TAG] From device: ");
					// Serial.print(myDistantDevice->getByteShortAddress()[0], HEX);
					// Serial.print(":");
					// Serial.println(myDistantDevice->getByteShortAddress()[1], HEX);
					// _expectedMsgId = POLL_ACK;
					return;
				}
				else if(messageType == POLL_ACK) {
					DW1000.getReceiveTimestamp(myDistantDevice->timePollAckReceived);
					//we note activity for our device:
					myDistantDevice->noteActivity();
					
					// Wait until the last expected anchor has responded before sending
					// RANGE. If the last anchor's POLL_ACK is lost, the deadline check
					// in timerTick() will fire transmitRange() as a fallback.
					if(myDistantDevice->getIndex() == _networkDevicesNumber-1) {
						// All expected POLL_ACKs received — cancel the deadline and
						// transmit RANGE immediately.
						_tagPollAckDeadlineMs = 0;
						_expectedMsgId = RANGE_REPORT;
						//and transmit the next message (range) of the ranging protocole (in broadcast)
						transmitRange(nullptr);
					}
				}
				else if(messageType == RANGE_REPORT) {
					// Serial.print("[DW1000 TAG] RANGE_REPORT recibido de ");
					// Serial.print(myDistantDevice->getByteShortAddress()[0], HEX);
					// Serial.print(":");
					// Serial.println(myDistantDevice->getByteShortAddress()[1], HEX);
					float curRange;
					memcpy(&curRange, data+1+SHORT_MAC_LEN, 4);
					float curRXPower;
					memcpy(&curRXPower, data+5+SHORT_MAC_LEN, 4);
					myDistantDevice->noteActivity();
					
					if (_useRangeFilter) {
						//Skip first range
						if (myDistantDevice->getRange() != 0.0f) {
							curRange = filterValue(curRange, myDistantDevice->getRange(), _rangeFilterValue);
						}
					}

					//we have a new range to save !
					myDistantDevice->setRange(curRange);
					myDistantDevice->setRXPower(curRXPower);
					

					//We can call our handler !
					//we have finished our range computation. We send the corresponding handler
					_lastDistantDevice = myDistantDevice->getIndex();
					// Serial.print("[DW1000 TAG] Range to ");
					// Serial.print(myDistantDevice->getByteShortAddress()[0], HEX);
					// Serial.print(":");
					// Serial.print(myDistantDevice->getByteShortAddress()[1], HEX);
					// Serial.print(" = ");
					// Serial.println(curRange);

					if(_handleNewRange != 0) {
						(*_handleNewRange)();
					}
				}
			}
		}
	}
}

void DW1000RangingClass::useRangeFilter(boolean enabled) {
	_useRangeFilter = enabled;
}

void DW1000RangingClass::forceReceiverMode() {
	receiver();
	noteActivity();
}

void DW1000RangingClass::setRangeFilterValue(uint16_t newValue) {
	if (newValue < 2) {
		_rangeFilterValue = 2;
	}else{
		_rangeFilterValue = newValue;
	}
}


/* ###########################################################################
 * #### Private methods and Handlers for transmit & Receive reply ############
 * ######################################################################### */


void DW1000RangingClass::handleSent() {
	// status change on sent success
	_sentAck = true;
	_txEventCount++;
	_lastTxEventMs = millis();
	if(_type == TAG) {
		_tagKeepAwakeUntilMs = millis() + DW1000_TAG_ACTIVE_GUARD_MS;
	}
}

void DW1000RangingClass::handleReceived() {
	_rxEventCount++;
	_lastRxEventMs = millis();
	// status change on received success
	_receivedAck = true;
	if(_type == TAG) {
		_tagKeepAwakeUntilMs = millis() + DW1000_TAG_ACTIVE_GUARD_MS;
	}
}


void DW1000RangingClass::noteActivity() {
	// update activity timestamp, so that we do not reach "resetPeriod"
	_lastActivity = millis();
}

void DW1000RangingClass::resetInactive() {
	//if inactive
	if(_type == ANCHOR) {
		_expectedMsgId = POLL;
		receiver();
	}
	noteActivity();
}

void DW1000RangingClass::timerTick() {
	uint32_t now = millis();

	if(_type == TAG) {
		if(_tagSyncValid && (now - _tagLastSyncRxMs > DW1000_SYNC_TIMEOUT_MS)) {
			_tagSyncValid = false;
		}

		bool syncReady = true;
		#if DW1000_TAG_REQUIRE_SYNC
				syncReady = _tagSyncValid;
		#endif

		// ── End BLINK phase: start polling rounds if anchors are now known ──
		if(_tagBurstActive && _tagBurstBlinkPhase && now >= _tagBurstBlinkDeadlineMs) {
			
			_tagBurstBlinkPhase = false;
			if(_networkDevicesNumber > 0) {
				_tagBurstRoundsRemaining = DW1000_TAG_BURST_WARMUP_POLLS
				                         + DW1000_TAG_BURST_RANGING_ROUNDS;
				_tagBurstWarmupRemaining = DW1000_TAG_BURST_WARMUP_POLLS;
				// Serial.print("[DW1000 TAG] Iniciando burst (");
				// Serial.print(DW1000_TAG_BURST_WARMUP_POLLS);
				// Serial.print(" warmup + ");
				// Serial.print(DW1000_TAG_BURST_RANGING_ROUNDS);
				// Serial.println(" ranging)");
				startNextBurstRound();
			} else {
				// No anchor responded to the BLINK; abort burst and wait next cycle
				_tagBurstActive = false;
				armTagNextTransmission(now);
				_tagKeepAwakeUntilMs = now + DW1000_TAG_ACTIVE_GUARD_MS;
				// Serial.println("[DW1000 TAG] Sin anclas tras BLINK, abortando burst");
			}
		}

		// ── Advance burst round when deadline expires ──
		if(_tagBurstActive && !_tagBurstBlinkPhase && _tagBurstRoundsRemaining > 0 && now >= _tagBurstRoundDeadlineMs) {
			completeBurstRound();
		}

		// ── Start a new burst when the TX period elapses and TDMA slot is open ──
		// A BLINK broadcast is always sent first so anchors can (re-)register
		// the tag and stagger their RANGING_INIT replies before polling begins.
		if(!_tagBurstActive && syncReady && now >= _tagNextTxDueMs && isTagTransmissionWindow(now)) {
			exitTagPowerSave();
			_tagBurstActive = true;
			_tagBurstBlinkPhase = true;
			transmitBlink();
			_tagBurstBlinkDeadlineMs = now + DW1000_TAG_BURST_BLINK_WAIT_MS;
			_tagKeepAwakeUntilMs = _tagBurstBlinkDeadlineMs + DW1000_TAG_ACTIVE_GUARD_MS;
			// Serial.println("[DW1000 TAG] BLINK de reconocimiento");
			// armTagNextTransmission is called when burst completes or is aborted
		}

		// ── POLL_ACK deadline fallback ──
		if(_expectedMsgId == POLL_ACK &&
		   _tagPollAckDeadlineMs != 0 &&
		   now >= _tagPollAckDeadlineMs) {
			_tagPollAckDeadlineMs = 0;
			_expectedMsgId = RANGE_REPORT;
			transmitRange(nullptr);
			_tagKeepAwakeUntilMs = now + DW1000_TAG_ACTIVE_GUARD_MS;
			if(DEBUG) {
				Serial.println("[DW1000 TAG] POLL_ACK deadline: enviando RANGE sin todos los ACKs");
			}
		}

		checkForInactiveDevices();
		if(!syncReady) {
			exitTagPowerSave();
			return;
		}
		if(!_tagBurstActive && now >= _tagKeepAwakeUntilMs) {
			if (_rangeFailed) {
				// Si se ha recibido un RANGE_FAILED, esperamos un poco antes de continuar para evitar bucles ocupados si el anchor sigue enviando RANGE_FAILED
				enterTagPowerSave();
				vTaskDelay(2000 / portTICK_PERIOD_MS);
				_rangeFailed = false;
				return;
			}
			enterTagPowerSave();
		}
		return;
	}

	// Sync epoch is now embedded directly in RANGING_INIT when anchor master
	// is enabled; no separate periodic SYNC_FRAME broadcast is needed.

	// Legacy ANCHOR cadence
	if(counterForBlink == 0) {
		checkForInactiveDevices();
	}
	counterForBlink++;
	if(counterForBlink > 20) {
		counterForBlink = 0;
	}
}

bool DW1000RangingClass::isTagTransmissionWindow(uint32_t nowMs) {
	if(_tagTDMASlotCount == 0 || _tagTDMASlotMs == 0) {
		return true;
	}

	uint32_t cycleMs = (uint32_t)_tagTDMASlotCount * (uint32_t)_tagTDMASlotMs;

	if(cycleMs == 0) {
		return true;
	}

	int64_t alignedNowMs = (int64_t)nowMs;
	if(_tagSyncValid) {
		alignedNowMs += (int64_t)_tagEpochOffsetMs;
	}
	int64_t alignedOffset = alignedNowMs % (int64_t)cycleMs;
	if(alignedOffset < 0) {
		alignedOffset += cycleMs;
	}

	uint32_t slotStart = (uint32_t)_tagOwnSlotIndex * (uint32_t)_tagTDMASlotMs;
	uint32_t slotOffset = (uint32_t)alignedOffset;
	return slotOffset >= slotStart && slotOffset < (slotStart + _tagTDMASlotMs);
}

void DW1000RangingClass::armTagNextTransmission(uint32_t nowMs) {
	_tagNextTxDueMs = nowMs + _tagTxPeriodMs;
}

void DW1000RangingClass::startNextBurstRound() {
	uint32_t now = millis();
	_expectedMsgId = POLL_ACK;
	transmitPoll(nullptr); // broadcast POLL

	// ── Compute round duration ──
	// Each round: POLL → POLL_ACK window → RANGE → RANGE_REPORT window.
	// POLL_ACK and RANGE_REPORT can use different base delays.
	uint32_t nDev = _networkDevicesNumber > 0 ? _networkDevicesNumber : 1;
	uint32_t ackWindowMs = ((((2u * nDev) - 1u) * _baseReplyDelayUS) + 999u) / 1000u + 10u;
	uint32_t reportWindowMs = ((((2u * nDev) - 1u) * _rangeReportBaseDelayUS) + 999u) / 1000u + 10u;
	uint32_t roundMs = ackWindowMs + reportWindowMs + DW1000_TAG_BURST_ROUND_GAP_MS;
	_tagBurstRoundDeadlineMs = now + roundMs;
	_tagKeepAwakeUntilMs = _tagBurstRoundDeadlineMs + DW1000_TAG_ACTIVE_GUARD_MS;

	// Arm POLL_ACK deadline (fallback if some ACKs are lost)
	uint32_t lastAckDelayMs = ((((2u * nDev) - 1u) * _baseReplyDelayUS) + 999u) / 1000u + 15u;
	_tagPollAckDeadlineMs = now + lastAckDelayMs;

	uint8_t totalRounds = DW1000_TAG_BURST_WARMUP_POLLS + DW1000_TAG_BURST_RANGING_ROUNDS;
	uint8_t currentRound = totalRounds - _tagBurstRoundsRemaining + 1;
	// if(_tagBurstWarmupRemaining > 0) {
	// 	Serial.print("[DW1000 TAG] Warmup POLL #");
	// 	Serial.print(DW1000_TAG_BURST_WARMUP_POLLS - _tagBurstWarmupRemaining + 1);
	// 	Serial.print("/");
	// 	Serial.println(DW1000_TAG_BURST_WARMUP_POLLS);
	// } else {
	// 	Serial.print("[DW1000 TAG] Ranging round #");
	// 	Serial.print(currentRound - DW1000_TAG_BURST_WARMUP_POLLS);
	// 	Serial.print("/");
	// 	Serial.println(DW1000_TAG_BURST_RANGING_ROUNDS);
	// }
}

void DW1000RangingClass::completeBurstRound() {
	uint32_t now = millis();
	if(_tagBurstWarmupRemaining > 0) {
		_tagBurstWarmupRemaining--;
	}
	_tagBurstRoundsRemaining--;

	if(_tagBurstRoundsRemaining > 0) {
		startNextBurstRound();
	} else {
		_tagBurstActive = false;
		armTagNextTransmission(now);
		_tagKeepAwakeUntilMs = now + DW1000_TAG_ACTIVE_GUARD_MS;
		// Serial.println("[DW1000 TAG] Burst completo, entrando en reposo");
	}
}

void DW1000RangingClass::computeAutoTiming(uint8_t maxAnchors, uint8_t maxTags) {
	// ── Air-time model: 110 kbps, PRF 64 MHz, 4096-symbol preamble ──
	// SHR 4096 (preámbulo+SFD): ~4310 µs  |  PHR: ~48 µs  |  Payload: ~73 µs/byte
	// (SHR 2048 era ~2220 µs; se añaden 2048×1017.63 ns ≈ 2084 µs al pasar a 4096)
	static const uint32_t SHR_US      = 4310u;
	static const uint32_t PHR_US      =   48u;
	static const uint32_t US_PER_BYTE =   73u; // ceil(8 bits / 110 kbps * 1e6)

	// Estimate air-time for the relevant anchor reply frames.
	uint32_t pollAckAirTimeUs = SHR_US + PHR_US + 12u * US_PER_BYTE;      // 10B + 2B CRC
	uint32_t rangeReportAirTimeUs = SHR_US + PHR_US + 20u * US_PER_BYTE;  // 18B + 2B CRC
	uint32_t rangingInitAirTimeUs = SHR_US + PHR_US + 18u * US_PER_BYTE;  // 16B + 2B CRC

	// Compute separate timing floors and keep build-flag values as minimums.
	uint32_t pollAckDelayUs = ((pollAckAirTimeUs + 1500u + 499u) / 500u) * 500u;
	uint32_t rangeReportDelayUs = ((rangeReportAirTimeUs + 2000u + 499u) / 500u) * 500u;
	uint32_t rangingInitSlotUs = ((rangingInitAirTimeUs + 1500u + 499u) / 500u) * 500u;

	if(pollAckDelayUs < DW1000_ANCHOR_POLL_ACK_BASE_DELAY_US) {
		pollAckDelayUs = DW1000_ANCHOR_POLL_ACK_BASE_DELAY_US;
	}
	if(rangeReportDelayUs < DW1000_ANCHOR_RANGE_REPORT_BASE_DELAY_US) {
		rangeReportDelayUs = DW1000_ANCHOR_RANGE_REPORT_BASE_DELAY_US;
	}
	if(rangingInitSlotUs < DW1000_ANCHOR_RANGING_INIT_SLOT_US) {
		rangingInitSlotUs = DW1000_ANCHOR_RANGING_INIT_SLOT_US;
	}

	_baseReplyDelayUS = pollAckDelayUs;
	_rangeReportBaseDelayUS = rangeReportDelayUs;
	_rangingInitSlotUS = rangingInitSlotUs;

	// ── Round time: POLL → last RANGE_REPORT arrives ──
	uint32_t N      = (maxAnchors > 0u) ? (uint32_t)maxAnchors : 1u;
	uint32_t ackWindowMs = (((2u * N - 1u) * pollAckDelayUs) + 999u) / 1000u + 10u;
	uint32_t reportWindowMs = (((2u * N - 1u) * rangeReportDelayUs) + 999u) / 1000u + 10u;
	uint32_t roundMs = ackWindowMs + reportWindowMs + DW1000_TAG_BURST_ROUND_GAP_MS;

	// ── Burst duration ──
	// BLINK wait scales with the last expected RANGING_INIT slot plus guard.
	uint32_t blinkWaitMs  = (uint32_t)((maxAnchors > 0 ? (maxAnchors) : 1u) * _rangingInitSlotUS / 1000u + DW1000_TAG_BURST_BLINK_WAIT_MS);
	uint32_t totalRounds  = DW1000_TAG_BURST_WARMUP_POLLS + DW1000_TAG_BURST_RANGING_ROUNDS;
	uint32_t burstMs      = blinkWaitMs + totalRounds * roundMs;

	// ── TDMA slot: one burst + settling guard ──
	uint32_t slotMs = burstMs + DW1000_TAG_ACTIVE_GUARD_MS;
	_tagTDMASlotMs = (slotMs > 65535u) ? 65535u : (uint16_t)slotMs;
	_tagTDMASlotCount = maxTags;

	// ── TX period: slotCount * slotMs + extra guard ──
	_tagTxPeriodMs = (uint32_t)maxTags * _tagTDMASlotMs + (uint32_t)DW1000_TAG_ACTIVE_GUARD_MS;

	Serial.println("[DW1000 AUTO-TIMING] ============================");
	Serial.print("[DW1000 AUTO-TIMING]  maxAnchors="); Serial.print(maxAnchors);
	Serial.print("  maxTags=");    Serial.println(maxTags);
	Serial.print("[DW1000 AUTO-TIMING]  pollAckDelay=");     Serial.print(pollAckDelayUs);      Serial.println(" us");
	Serial.print("[DW1000 AUTO-TIMING]  rangeReportDelay="); Serial.print(rangeReportDelayUs);  Serial.println(" us");
	Serial.print("[DW1000 AUTO-TIMING]  rangingInitSlot=");  Serial.print(rangingInitSlotUs);   Serial.println(" us");
	Serial.print("[DW1000 AUTO-TIMING]  roundTime=");        Serial.print(roundMs);            Serial.println(" ms");
	Serial.print("[DW1000 AUTO-TIMING]  burstTime=");   Serial.print(burstMs);         Serial.println(" ms");
	Serial.print("[DW1000 AUTO-TIMING]  tdmaSlot=");    Serial.print(_tagTDMASlotMs);  Serial.println(" ms");
	Serial.print("[DW1000 AUTO-TIMING]  txPeriod=");    Serial.print(_tagTxPeriodMs);  Serial.println(" ms");
	Serial.println("[DW1000 AUTO-TIMING] ============================");
}

void DW1000RangingClass::enterTagPowerSave() {
	if(_type != TAG || _tagPowerSaveActive) {
		return;
	}

#if DW1000_TAG_POWER_SAVE_MODE == 2
	DW1000.deepSleep();
	_tagPowerSaveActive = true;
#elif DW1000_TAG_POWER_SAVE_MODE == 1
	DW1000.idle();
	_tagPowerSaveActive = true;
#endif
}

void DW1000RangingClass::exitTagPowerSave() {
	if(_type != TAG || !_tagPowerSaveActive) {
		return;
	}

#if DW1000_TAG_POWER_SAVE_MODE == 2
	DW1000.spiWakeup();
	DW1000.select(_SS);
#endif
	receiver();
	_tagPowerSaveActive = false;
}

void DW1000RangingClass::transmitSync() {
	transmitInit();
	byte shortBroadcast[2] = {0xFF, 0xFF};
	_globalMac.generateShortMACFrame(data, _currentShortAddress, shortBroadcast);
	data[SHORT_MAC_LEN] = SYNC_FRAME;

	uint32_t masterEpochMs = millis();
	memcpy(data+SHORT_MAC_LEN+1, &masterEpochMs, sizeof(uint32_t));

	copyShortAddress(_lastSentToShortAddress, shortBroadcast);
	// SYNC_FRAME: SHORT_MAC_LEN(9) + MsgType(1) + MasterEpoch(4) = 14 bytes
	transmit(data, SHORT_MAC_LEN + 5);
}


void DW1000RangingClass::copyShortAddress(byte address1[], byte address2[]) {
	*address1     = *address2;
	*(address1+1) = *(address2+1);
}

/* ###########################################################################
 * #### Methods for ranging protocole   ######################################
 * ######################################################################### */

void DW1000RangingClass::transmitInit() {
	DW1000.newTransmit();
	DW1000.setDefaults();
}


void DW1000RangingClass::transmit(byte datas[], uint16_t dataLength) {
	if(DEBUG) {
		int16_t messageType = detectMessageType(datas);
		Serial.print("[DW1000][TX ");
		Serial.print(dataLength);
		Serial.print("B][");
		Serial.print(debugMessageTypeName(messageType));
		Serial.print("] ");
		debugPrintRawData(datas, dataLength);
	}
	DW1000.setData(datas, dataLength);
	DW1000.startTransmit();
}


void DW1000RangingClass::transmit(byte datas[], uint16_t dataLength, DW1000Time time) {
	if(DEBUG) {
		int16_t messageType = detectMessageType(datas);
		Serial.print("[DW1000][TX delayed ");
		Serial.print(dataLength);
		Serial.print("B][");
		Serial.print(debugMessageTypeName(messageType));
		Serial.print("] ");
		debugPrintRawData(datas, dataLength);
	}
	DW1000.setDelay(time);
	DW1000.setData(datas, dataLength);
	DW1000.startTransmit();
}

void DW1000RangingClass::transmitBlink() {
	transmitInit();
	_globalMac.generateBlinkFrame(data, _currentAddress, _currentShortAddress);
	// Blink frame: FC(1) + SeqNum(1) + EUI(8) + ShortAddr(2) = 12 bytes
	transmit(data, 12);
}

void DW1000RangingClass::transmitRangingInit(DW1000Device* myDistantDevice) {
	// Non-blocking anti-collision: use noteActivity timestamp instead of
	// blocking delay() which stalls the main loop.
	// Serial.print("[DW1000 ANCHOR] Enviando RANGING_INIT a ");
	// Serial.println(myDistantDevice->getByteAddress()[0], HEX);
	transmitInit();
	//we generate the mac frame for a ranging init message
	_globalMac.generateLongMACFrame(data, _currentShortAddress, myDistantDevice->getByteAddress());
	//we define the function code
	data[LONG_MAC_LEN] = RANGING_INIT;

	// If this anchor is the master, embed the current epoch so the TAG can
	// synchronise its clock from the same frame (no separate SYNC_FRAME needed).
	uint16_t frameLen = LONG_MAC_LEN + 1;
	if(_anchorMasterEnabled) {
		uint32_t masterEpochMs = millis();
		memcpy(data + LONG_MAC_LEN + 1, &masterEpochMs, sizeof(uint32_t));
		frameLen = LONG_MAC_LEN + 5;
	}

	copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
	
	// Use a deterministic slot derived from the anchor short address.
	// Do NOT use _networkDevicesNumber here: in ANCHOR mode that value tracks
	// known TAGs, and with a single TAG all anchors would collapse to slot 0.
	uint32_t rangingInitSlotCount = (MAX_DEVICES > 0) ? (uint32_t)MAX_DEVICES : 1u;
	uint32_t antiCollisionDelayUs =
		(uint32_t)(((_currentShortAddress[1] * 256u + _currentShortAddress[0]) %
		rangingInitSlotCount) * _rangingInitSlotUS);
	// Serial.print("[DW1000 ANCHOR] Short address: ");
	// Serial.print(_currentShortAddress[0], HEX);
	// Serial.print(":");
	// Serial.println(_currentShortAddress[1], HEX);
	// Serial.print("[DW1000 ANCHOR] RANGING_INIT slot delay: ");
	// Serial.println(antiCollisionDelayUs);
	if(antiCollisionDelayUs > 0) {
		DW1000Time deltaTime = DW1000Time((uint64_t)antiCollisionDelayUs, DW1000Time::MICROSECONDS);
		transmit(data, frameLen, deltaTime);
	} else {
		transmit(data, frameLen);
	}
}

void DW1000RangingClass::transmitPoll(DW1000Device* myDistantDevice) {
	
	transmitInit();
	// POLL and RANGE messages are broadcast. The POLL payload lists all known anchors with individually assigned reply delays:
	if(myDistantDevice == nullptr) {		
		// Bounds check: POLL frame = SHORT_MAC_LEN + 2 + N*4.
		// Limit N so the frame fits in LEN_DATA.
		uint8_t maxDevicesInFrame = (LEN_DATA - SHORT_MAC_LEN - 2) / 4;
		uint8_t devicesToSend = _networkDevicesNumber;
		if(devicesToSend > maxDevicesInFrame) {
			devicesToSend = maxDevicesInFrame;
		}
		
		byte shortBroadcast[2] = {0xFF, 0xFF};
		_globalMac.generateShortMACFrame(data, _currentShortAddress, shortBroadcast);
		data[SHORT_MAC_LEN]   = POLL;
		//we enter the number of devices
		data[SHORT_MAC_LEN+1] = devicesToSend;
		// Each anchor checks if its own short address appears in the POLL payload and uses the slot's replyTime 
		// as its _replyDelayTimeUS. This staggers POLL_ACK transmissions by 2i * _baseReplyDelayUS per anchor. 
		// RANGE_REPORT replies use the same _replyDelayTimeUS, so reports are also staggered.
		for(uint8_t i = 0; i < devicesToSend; i++) {
			//each devices have a different reply delay time.
			// Guard against uint16_t overflow: cap at 65535.
			uint32_t replyTimeCalc = (uint32_t)(2u * i + 1u) * _baseReplyDelayUS;
			if(replyTimeCalc > 65535u) {
				replyTimeCalc = 65535u;
			}
			_networkDevices[i].setReplyTime((uint16_t)replyTimeCalc);
			//we write the short address of our device:
			memcpy(data+SHORT_MAC_LEN+2+4*i, _networkDevices[i].getByteShortAddress(), 2);
			
			//we add the replyTime
			uint16_t replyTime = _networkDevices[i].getReplyTime();
			memcpy(data+SHORT_MAC_LEN+2+2+4*i, &replyTime, 2);
			
		}
		
		copyShortAddress(_lastSentToShortAddress, shortBroadcast);
		
		// Actual frame length: SHORT_MAC_LEN + 2 + devicesToSend * 4
		uint16_t frameLen = SHORT_MAC_LEN + 2 + (uint16_t)devicesToSend * 4;
		transmit(data, frameLen);
	}
	else {	
		_globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());
		
		data[SHORT_MAC_LEN]   = POLL;
		data[SHORT_MAC_LEN+1] = 1;
		uint16_t replyTime = myDistantDevice->getReplyTime();
		//we write the short address of our device:
		memcpy(data+SHORT_MAC_LEN+2, myDistantDevice->getByteShortAddress(), 2);
		memcpy(data+SHORT_MAC_LEN+4, &replyTime, sizeof(uint16_t)); // todo is code correct?
		
		copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
		
		// Single-device POLL: SHORT_MAC_LEN + 2 + 4 = 15 bytes
		transmit(data, SHORT_MAC_LEN + 6);
	}
}


void DW1000RangingClass::transmitPollAck(DW1000Device* myDistantDevice) {
	transmitInit();
	_globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());
	data[SHORT_MAC_LEN] = POLL_ACK;
	// delay the same amount as ranging tag
	DW1000Time deltaTime = DW1000Time(_replyDelayTimeUS, DW1000Time::MICROSECONDS);
	copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
	// POLL_ACK: SHORT_MAC_LEN(9) + MsgType(1) = 10 bytes
	transmit(data, SHORT_MAC_LEN + 1, deltaTime);
	// Serial.println(millis());
}

void DW1000RangingClass::transmitRange(DW1000Device* myDistantDevice) {
	//transmit range need to accept broadcast for multiple anchor
	transmitInit();
	uint8_t responseSlotCount = DW1000_TAG_RESPONSE_SLOT_COUNT;
	if(responseSlotCount == 0) {
		responseSlotCount = 1;
	}
	uint8_t responseSlotIndex = _tagOwnSlotIndex % responseSlotCount;
	// uint32_t responseSlotOffsetUs = (uint32_t)responseSlotIndex * (uint32_t)DW1000_TAG_RESPONSE_SLOT_US;
	uint32_t responseSlotOffsetUs =0;
	
	
	if(myDistantDevice == nullptr) {
		// Bounds check: RANGE frame = SHORT_MAC_LEN + 2 + N*17.
		// Limit N so the frame fits in LEN_DATA.
		uint8_t maxDevicesInFrame = (LEN_DATA - SHORT_MAC_LEN - 2) / 17;
		uint8_t devicesToSend = _networkDevicesNumber;
		if(devicesToSend > maxDevicesInFrame) {
			devicesToSend = maxDevicesInFrame;
		}
		
		byte shortBroadcast[2] = {0xFF, 0xFF};
		_globalMac.generateShortMACFrame(data, _currentShortAddress, shortBroadcast);
		data[SHORT_MAC_LEN]   = RANGE;
		//we enter the number of devices
		data[SHORT_MAC_LEN+1] = devicesToSend;
		
		// delay sending the message and remember expected future sent timestamp
		DW1000Time deltaTime     = DW1000Time((uint64_t)_baseReplyDelayUS + responseSlotOffsetUs, DW1000Time::MICROSECONDS);
		DW1000Time timeRangeSent = DW1000.setDelay(deltaTime);
		
		for(uint8_t i = 0; i < devicesToSend; i++) {
			//we write the short address of our device:
			memcpy(data+SHORT_MAC_LEN+2+17*i, _networkDevices[i].getByteShortAddress(), 2);
			// memcpy(data+SHORT_MAC_LEN+2+2*i, _networkDevices[i].getByteShortAddress(), 2);
			
			//we get the device which correspond to the message which was sent (need to be filtered by MAC address)
			_networkDevices[i].timeRangeSent = timeRangeSent;
			_networkDevices[i].timePollSent.getTimestamp(data+SHORT_MAC_LEN+4+17*i);
			_networkDevices[i].timePollAckReceived.getTimestamp(data+SHORT_MAC_LEN+9+17*i);
			_networkDevices[i].timeRangeSent.getTimestamp(data+SHORT_MAC_LEN+14+17*i);
			
		}
		
		copyShortAddress(_lastSentToShortAddress, shortBroadcast);
		
		// Actual frame length: SHORT_MAC_LEN + 2 + devicesToSend * 17
		uint16_t frameLen = SHORT_MAC_LEN + 2 + (uint16_t)devicesToSend * 17;
		DW1000.setData(data, frameLen);
		DW1000.startTransmit();
	}
	else {
		_globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());
		data[SHORT_MAC_LEN] = RANGE;
		// delay sending the message and remember expected future sent timestamp
		DW1000Time deltaTime = DW1000Time((uint64_t)_replyDelayTimeUS + responseSlotOffsetUs, DW1000Time::MICROSECONDS);
		//we get the device which correspond to the message which was sent (need to be filtered by MAC address)
		myDistantDevice->timeRangeSent = DW1000.setDelay(deltaTime);
		myDistantDevice->timePollSent.getTimestamp(data+1+SHORT_MAC_LEN);
		myDistantDevice->timePollAckReceived.getTimestamp(data+6+SHORT_MAC_LEN);
		myDistantDevice->timeRangeSent.getTimestamp(data+11+SHORT_MAC_LEN);
		copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
		
		// Single-device RANGE: SHORT_MAC_LEN(9) + MsgType(1) + 3*Timestamp(5) = 25 bytes
		DW1000.setData(data, SHORT_MAC_LEN + 16);
		DW1000.startTransmit();
	}
}


void DW1000RangingClass::transmitRangeReport(DW1000Device* myDistantDevice) {
	transmitInit();
	_globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());
	data[SHORT_MAC_LEN] = RANGE_REPORT;
	// write final ranging result
	float curRange   = myDistantDevice->getRange();
	float curRXPower = myDistantDevice->getRXPower();
	//We add the Range and then the RXPower
	memcpy(data+1+SHORT_MAC_LEN, &curRange, 4);
	memcpy(data+5+SHORT_MAC_LEN, &curRXPower, 4);
	copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
	// RANGE_REPORT: keep the same slot ordinal assigned in POLL_ACK, but with its
	// own base delay so larger frames can have a slightly bigger gap.
	uint32_t slotOrdinal = 1u;
	if(_baseReplyDelayUS > 0u) {
		slotOrdinal = ((uint32_t)_replyDelayTimeUS + (_baseReplyDelayUS / 2u)) / _baseReplyDelayUS;
		if(slotOrdinal == 0u) {
			slotOrdinal = 1u;
		}
	}
	uint32_t rangeReportDelayUs = slotOrdinal * _rangeReportBaseDelayUS;
	transmit(data, SHORT_MAC_LEN + 9, DW1000Time(rangeReportDelayUs, DW1000Time::MICROSECONDS));
}

void DW1000RangingClass::transmitRangeFailed(DW1000Device* myDistantDevice) {
	transmitInit();
	_globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());
	data[SHORT_MAC_LEN] = RANGE_FAILED;
	
	copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
	// RANGE_FAILED: SHORT_MAC_LEN(9) + MsgType(1) = 10 bytes
	transmit(data, SHORT_MAC_LEN + 1);
}

void DW1000RangingClass::receiver() {
	DW1000.newReceive();
	DW1000.setDefaults();
	// so we don't need to restart the receiver manually
	DW1000.receivePermanently(true);
	DW1000.startReceive();
}


/* ###########################################################################
 * #### Methods for range computation and corrections  #######################
 * ######################################################################### */


void DW1000RangingClass::computeRangeAsymmetric(DW1000Device* myDistantDevice, DW1000Time* myTOF) {
	// asymmetric two-way ranging (more computation intense, less error prone)
	DW1000Time round1 = (myDistantDevice->timePollAckReceived-myDistantDevice->timePollSent).wrap();
	DW1000Time reply1 = (myDistantDevice->timePollAckSent-myDistantDevice->timePollReceived).wrap();
	DW1000Time round2 = (myDistantDevice->timeRangeReceived-myDistantDevice->timePollAckSent).wrap();
	DW1000Time reply2 = (myDistantDevice->timeRangeSent-myDistantDevice->timePollAckReceived).wrap();
	
	myTOF->setTimestamp((round1*round2-reply1*reply2)/(round1+round2+reply1+reply2));
	/*
	Serial.print("timePollAckReceived ");myDistantDevice->timePollAckReceived.print();
	Serial.print("timePollSent ");myDistantDevice->timePollSent.print();
	Serial.print("round1 "); Serial.println((long)round1.getTimestamp());
	
	Serial.print("timePollAckSent ");myDistantDevice->timePollAckSent.print();
	Serial.print("timePollReceived ");myDistantDevice->timePollReceived.print();
	Serial.print("reply1 "); Serial.println((long)reply1.getTimestamp());
	
	Serial.print("timeRangeReceived ");myDistantDevice->timeRangeReceived.print();
	Serial.print("timePollAckSent ");myDistantDevice->timePollAckSent.print();
	Serial.print("round2 "); Serial.println((long)round2.getTimestamp());
	
	Serial.print("timeRangeSent ");myDistantDevice->timeRangeSent.print();
	Serial.print("timePollAckReceived ");myDistantDevice->timePollAckReceived.print();
	Serial.print("reply2 "); Serial.println((long)reply2.getTimestamp());
	 */
}


/* FOR DEBUGGING*/
void DW1000RangingClass::visualizeDatas(byte datas[]) {
	char string[60];
	sprintf(string, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
					datas[0], datas[1], datas[2], datas[3], datas[4], datas[5], datas[6], datas[7], datas[8], datas[9], datas[10], datas[11], datas[12], datas[13], datas[14], datas[15]);
	Serial.println(string);
}



/* ###########################################################################
 * #### Utils  ###############################################################
 * ######################################################################### */

float DW1000RangingClass::filterValue(float value, float previousValue, uint16_t numberOfElements) {
	
	float k = 2.0f / ((float)numberOfElements + 1.0f);
	return (value * k) + previousValue * (1.0f - k);
}



