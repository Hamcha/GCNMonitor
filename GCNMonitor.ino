#define GC0_PIN 2
#define DEBUG_PIN 3

void setup() {
	// Shut down status LED until a controller is detected
	digitalWrite(13, LOW);
	pinMode(13, OUTPUT);

	// Setup Serial IO
	Serial.begin(4800);

	// Setup GC pin (one for now)
	pinMode(GC0_PIN, INPUT);
	digitalWrite(GC0_PIN, HIGH);

	pinMode(DEBUG_PIN, OUTPUT);
	digitalWrite(DEBUG_PIN, LOW);
}

unsigned char gcvalue[12] = { 0 };

void loop() {
	memset(gcvalue, 0, 3);
	noInterrupts();
	int success = readGC();
	interrupts();
	if (success > 0) {
		// Poll signal from console: 0100 0000 0000 0011 0000 0010 = 0x400302
		for (int i = 3; i < 12; ++i) {
			Serial.write(gcvalue[i]);
		}
		Serial.println();
		Serial.flush();
	}
}

int readGC() {
	unsigned char* gcbytes = gcvalue;
	int ret = 0;
	asm volatile (
		"ldi  r25, lo8(0)\n"    // 1   - Reset counter r25 (Bit count)
		"ld   r23, Z\n"         // 2   - Load first byte into r23 (Current byte)
		"ldi  %[ret], lo8(1)\n" // 1   - Set default return value to SUCCESS (1)

".L%=_next_bit:\n"
		// Wait for low line (Bit start)
		"ldi  r24, lo8(64)\n"   // 1   - Set counter r23 (Wait timeout)
".L%=_wait_low:\n"
		"sbis 0x9, 2\n"         // 1/2 - Check for a low line on I/O 2
		"rjmp .L%=_read_bit\n"  // 1 | - Go read the bit if so!
		"subi r24, lo8(1)\n"    // 1   - Otherwise decrement the timeout counter
		"brne .L%=_wait_low\n"  // 1/2 - Retry if the timeout hasn't expired
		"ldi  %[ret], lo8(0)\n" // 1   - Load error return value into [ret]
		"rjmp .L%=_return\n"    // 1   - Jump to end of function

".L%=_read_bit:\n"

		"nop\nnop\nnop\nnop\n"  // Manual padding
		"nop\nnop\nnop\nnop\n"  // Move to around the half part of the bit
		"nop\nnop\nnop\nnop\n"  // 29 cycles, Around 2us
		"nop\nnop\nnop\nnop\n"
		"nop\nnop\nnop\nnop\n"
		"nop\nnop\nnop\nnop\n"
		"nop\nnop\nnop\nnop\n"
		"nop\n"

		// Set and reset debug signal
		"sbi 0x9, 3\n"
		"nop\n"
		"cbi 0x9, 3\n"

		// Add current bit value to the current byte
		"lsl  r23\n"            // 1   - Shift left (advance current bit)
		"sbic 0x9, 2\n"         // 1/2 - Check for high line on I/O 2
		"sbr  r23, lo8(1)\n"    // 1 | - If so, set current bit to 1
		"st   Z, r23\n"         // 2   - Store current byte into memory

								// Check for end of byte or message
		"subi r25, lo8(-1)\n"   // 1   - Increment bit counter
		"cpi  r25, lo8(90)\n"   // 1   - Check if we have enough bits
		"breq .L%=_return\n"    // 1/2 - Exit with success if so
		"mov  r24, r25\n"       // 1   - Copy counter for masking
		"andi r24, lo8(7)\n"    // 1   - Mask last 3 bits (% 8)
		"brne .L%=_wait_end\n"  // 1/2 - Not a full byte, go wait for next bit
		"adiw r30, 1\n"         // 2   - Add one to the byte pointer
		"ld   r23, Z\n"         // 2   - Load new byte into r23 (Current byte)

".L%=_wait_end:\n"
								// Wait for high line (Bit end)
		"ldi  r24, lo8(64)\n"   // 1   - Set counter r24 (Wait timeout)
".L%=_wait_high:\n"
		"sbic 0x9, 2\n"         // 1/2 - Check for a high line on I/O 2
		"rjmp .L%=_next_bit\n"  // 1 | - Go read next bit if so!
		"subi r24, lo8(1)\n"    // 1   - Otherwise decrement the timeout counter
		"brne .L%=_wait_high\n" // 1/2 - Retry if the timeout hasn't expired
		"ldi  %[ret], lo8(0)\n" // 1   - Load error return value into [ret]

".L%=_return:\n"
		: [ret] "=r" (ret), "+z" (gcbytes)
		:: "r25", "r24", "r23"
	);

	return ret;
}
