
#include "acia.h"

//------------------------------------------------------------------------
// sc6551
//------------------------------------------------------------------------

// sc6551 private functions 

DWORD WINAPI sc6551_input_thread(LPVOID);
//void sc6551_init();
//void sc6551_close();

// Comunications hooks
void com_open();
void com_close();
int  com_write(char*,int);
int  com_read(char*,int);

// Stat Reg is protected by a critical section
// to prevent race conditions
unsigned char StatReg = 0;

// Status register bits.  
// b0-B2  parity, frame, overrun error
// b3 Rcv Data register full if tru
// b4 Snd Data register empty if true
// b5 DCD Data carrier detect
// b6 DSR Data set ready
// b7 IRQ Interrupt generated
// Only RxF, TxE, and IRQ are used here
#define StatRxF  0x08
#define StatTxE  0x10
#define StatIRQ  0x80

unsigned char CmdReg  = 0;
// Command register bits.
// b0 DTR 1 = enable receive and interupts
// b1 Receiver IRQ enable.
// b3 b2  Transmit IRQ control
// B4 Echo mode. 0 = normal 1 = Echo (B2&B3 zero)
// B7 B6 B4 parity control
// Only DTR is used here
#define CmdDTR   0x01

// CtrReg is not really used, it is just echoed back to CoCo
unsigned char CtlReg  = 0;

// Data register is loaded by input thread.
unsigned char RcvChr = 0;

HANDLE hEventAciaRead;       // Event handle for read thread
HANDLE hThread;
CRITICAL_SECTION CritSect;

//------------------------------------------------------------------------
//  Initiallize sc6551 
//------------------------------------------------------------------------

void sc6551_init()
{
    DWORD id;
//	if (sc6551_initialized == 0) {
        // Make sure any previous instance is closed first
        sc6551_close();  
        // Open communications link
        com_open(); 
        // Create a critical section for RxF bit
        InitializeCriticalSectionAndSpinCount(&CritSect,512);
        // Create input thread
		hThread=CreateThread(NULL,0,sc6551_input_thread,NULL,0,&id);
        // Create event for input thread
        hEventAciaRead = CreateEvent (NULL,FALSE,FALSE,NULL);
        sc6551_initialized = 1;
//	}
}

//------------------------------------------------------------------------
//  Close sc6551 
//------------------------------------------------------------------------
void sc6551_close()
{
    if (hThread) {
        TerminateThread(hThread,1);
        WaitForSingleObject(hThread,2000);
    }
    hThread = NULL;
	com_close();
    DeleteCriticalSection(&CritSect);
	sc6551_initialized = 0;
}

//------------------------------------------------------------------------
// Input Thread.
//------------------------------------------------------------------------

    char inbuf[128];
    int  bufcnt=0;
    int  readcnt=0;

// Reads hang. They are done in a seperate thread.
DWORD WINAPI sc6551_input_thread(LPVOID param) 
{
    DWORD rc;
    DWORD ms=100;

    while(TRUE) {
        rc = WaitForSingleObject(hEventAciaRead,ms);
		if (CmdReg & CmdDTR) {
            if ( readcnt >= bufcnt ) {
                bufcnt = com_read(inbuf,120);
                readcnt = 0;
                StatReg = StatReg | StatRxF;
                ms = 2;
            } else if (rc == WAIT_TIMEOUT) {
                if ( readcnt < bufcnt ) StatReg = StatReg | StatRxF;
                StatReg = StatReg | StatIRQ;
                AssertInt(IRQ,0);
                ms = 100;
            }
		}
	}
}

//------------------------------------------------------------------------
// Port I/O
//------------------------------------------------------------------------

unsigned char sc6551_read(unsigned char port)
{
	unsigned char data;
	switch (port) {
		case 0x68:
            data = inbuf[readcnt];
            readcnt++;
            if (readcnt >= bufcnt) StatReg = StatReg &~ StatRxF;
            SetEvent(hEventAciaRead);       // data was read
			break;
		case 0x69:
            data = StatReg;
    		StatReg = StatReg & ~StatIRQ;   // Stat read clears IRQ flag
			break;
		case 0x6A:
			data = CmdReg;
			break;
		case 0x6B:
			data = CtlReg;
			break;
    }
	return data;
}

void sc6551_write(unsigned char data,unsigned short port)
{
	switch (port) {
		case 0x68:
			com_write(&data,1);
			StatReg = StatReg | StatTxE;  // mark out buffer empty
			break;
		case 0x69:
			StatReg = 0;
			break;
		case 0x6A:
			CmdReg = data;
            // If DTR set enable sc6551
		    if (CmdReg & CmdDTR) {
	            if (sc6551_initialized == 0) sc6551_init();
			    StatReg = StatTxE;          
                SetEvent(hEventAciaRead); // tell input worker
			} else {
            // Else disable sc6551
                sc6551_close();
				StatReg = 0;
			}
			break;
		case 0x6B:
			CtlReg = data;  // Not used, just returned on read
			break;
    }
}

//----------------------------------------------------------------
// Dispatch I/0 to communication type used.
// Hooks allow sc6551 to do communications to selected media
//----------------------------------------------------------------

// Open com
void com_open() {
	switch (AciaComType) {
	case 0: // Legacy Console
        console_open();
		break;
	}
}

void com_close() {
	switch (AciaComType) {
	case 0: // Console
        console_close();
		break;
	}
}

int com_write(char * buf,int len) { // returns bytes written
	switch (AciaComType) {
	case 0: // Legacy Console
        return console_write(buf,len);
        break;
    }
    return 0;
}

int com_read(char * buf,int len) {  // returns bytes read
	switch (AciaComType) {
	case 0: // Legacy Console
        return console_read(buf,len);
    }
    return 0;
}

