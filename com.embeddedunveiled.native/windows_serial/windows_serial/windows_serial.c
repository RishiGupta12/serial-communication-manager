/************************************************************************************************************
* Author : Rishi Gupta
* Email  : gupt21@gmail.com
*
* This file is part of 'serial communication manager' library.
*
* The 'serial communication manager' is free software: you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by the Free Software
* Foundation, either version 3 of the License, or (at your option) any later version.
*
* The 'serial communication manager' is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
* PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with serial communication manager. If not, see <http://www.gnu.org/licenses/>.
*
*************************************************************************************************************/

/**************************************************************************************************************
* Native code to communicate with UART-style port in Windows OS
*
* More info can be found here :-
* Data types for Windows API  : http://msdn.microsoft.com/en-us/library/windows/desktop/aa383751(v=vs.85).aspx
* System Error Codes          : http://msdn.microsoft.com/en-us/library/windows/desktop/ms681382(v=vs.85).aspx
* Communications Reference    : http://msdn.microsoft.com/en-us/library/aa363195.aspx
* Serial Communications       : http://msdn.microsoft.com/en-us/library/ff802693.aspx
* Locking and synchronisation : http://msdn.microsoft.com/en-us/library/windows/desktop/ms686927(v=vs.85).aspx
* Thread programming          : http://msdn.microsoft.com/en-us/library/windows/desktop/ms682453(v=vs.85).aspx
* Examples at msdn            : http://msdn.microsoft.com/en-us/library/windows/desktop/bb540534(v=vs.85).aspx
* Synchronization Functions   : http://msdn.microsoft.com/en-us/library/windows/desktop/ms686360(v=vs.85).aspx
*
* - When printing error number (using fprintf()), number returned by Windows OS is printed as it is by this library.
* - windows header files are in include directory of MinGW tool chain.
* - toolchain integration and concepts :
* http://help.eclipse.org/kepler/index.jsp?topic=%2Forg.eclipse.cdt.doc.user%2Fconcepts%2Fcdt_c_before_you_begin.htm&cp=9_0
* - sample code by microsoft at code.msdn.microsoft.com
* https://code.msdn.microsoft.com/windowsdesktop/Serial-Port-Sample-e8accf30/sourcecode?fileId=67164&pathId=1394200469
* - get details about java on your computer https://www.java.com/en/download/help/win_controlpanel.xml
* - With Visual C++, STRICT type checking is defined by default.
***************************************************************************************************************/

/* stdafx.h must come as first include file if you are using precompiled headers and Microsoft compiler. */
#include "stdafx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <process.h>
#include <tchar.h>

#include <jni.h>
#include "windows_serial_lib.h"

/* Common interface with java layer for supported OS types. */
#include "../../com_embeddedunveiled_serial_SerialComJNINativeInterface.h"

/* function prototypes */
extern void LOGE(JNIEnv *env);
extern int serial_delay(unsigned usecs);
extern unsigned WINAPI event_data_looper(LPVOID lpParam);
int setupLooperThread(JNIEnv *env, jobject obj, jlong handle, jobject looper_obj_ref, int data_enabled, int event_enabled);

#define DEBUG 1

#undef  UART_NATIVE_LIB_VERSION
#define UART_NATIVE_LIB_VERSION "1.0.0"

/* This is the maximum number of threads and hence data listeners instance we support. */
#define MAX_NUM_THREADS 1024

/* Reference to JVM shared among all the threads within a process. */
JavaVM *jvm;

/* When creating data looper threads, we pass some data to thread. A index in this array, holds pointer to
* the structure which is passed as parameter to a thread. Every time a data looper thread is created, we
* save the location of parameters passed to it and update the index to be used next time. */
int dtp_index = 0;
struct looper_thread_params handle_looper_info[MAX_NUM_THREADS] = { 0 };

/* The threads of a single process can use a critical section object for mutual-exclusion synchronization.
 * Used to protect global data from concurrent access. */
CRITICAL_SECTION csmutex;

/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    initNativeLib
* Signature: ()I
*
* This function save reference to JVM which will be used across native library, threads etc.
*/
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_initNativeLib(JNIEnv *env, jobject obj) {
	int ret = 0;

	ret = (*env)->GetJavaVM(env, &jvm);
	if(ret < 0) {
		if(DEBUG) fprintf(stderr, "%s \n", "NATIVE initNativeLib() could not get JVM.");
		if(DEBUG) fflush(stderr);
		return -240;
	}

	/* Initialise critical section (does not return any value). TODO DELETE WHEN UNLOADING LIBRARY*/
	InitializeCriticalSection(&csmutex);

	return 0;
}

/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    getNativeLibraryVersion
* Signature: ()Ljava/lang/String;
*
* This might return null which is handled by java layer.
*/
JNIEXPORT jstring JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_getNativeLibraryVersion(JNIEnv *env, jobject obj) {
	jstring version = (*env)->NewStringUTF(env, UART_NATIVE_LIB_VERSION);
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}
	return version;
}

/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    getSerialPortNames
* Signature: ()[Ljava/lang/String;
*
* This returns serial style ports known to system at this moment. This information is gleaned by reading windows registry for serial ports.
* Use registry editor to see available serial ports; HKEY_LOCAL_MACHINE->HARDWARE->DEVICEMAP->SERIALCOMM
*/
JNIEXPORT jobjectArray JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_getSerialPortNames(JNIEnv *env, jobject obj) {
	LONG result = 0;
	HKEY hKey;
	TCHAR achKey[255];                    /* buffer for subkey name      */
	DWORD cbName;                         /* size of name string         */
	TCHAR achClass[MAX_PATH] = TEXT("");  /* buffer for class name       */
	DWORD cchClassName = MAX_PATH;        /* size of class string        */
	DWORD cSubKeys = 0;                   /* number of subkeys           */
	DWORD cbMaxSubKey;                    /* longest subkey size         */
	DWORD cchMaxClass;                    /* longest class string        */
	DWORD cValues;                        /* number of values for key    */
	DWORD cchMaxValue;                    /* longest value name          */
	DWORD cbMaxValueData;                 /* longest value data          */
	DWORD cbSecurityDescriptor;           /* size of security descriptor */
	FILETIME ftLastWriteTime;             /* last write time             */
	DWORD i, retCode;
	TCHAR nameBuffer[1024];
	DWORD cchValueName = 1024;
	TCHAR valueBuffer[1024];
	DWORD cchValueData = 1024;

	jobjectArray ports_found = NULL;
	jclass stringClass = (*env)->FindClass(env, "java/lang/String");

	/* Try to open the registry key for serial communication devices. */
	result = RegOpenKeyEx(HKEY_LOCAL_MACHINE,     /* pre-defined key                                                                    */
		TEXT("HARDWARE\\DEVICEMAP\\SERIALCOMM"),  /* name of the registry subkey to be opened                                           */
		0,                                        /* option to apply when opening the key                                               */
		KEY_READ | KEY_WOW64_64KEY,               /* access rights to the key to be opened, user might run 32 bit JRE on 64 bit machine */
		&hKey);                                   /* variable that receives a handle to the opened key                                  */

	if(result != ERROR_SUCCESS) {
		if(result == ERROR_FILE_NOT_FOUND) {
			if(DEBUG) fprintf(stderr, "%s \n", "NATIVE getSerialPortNames() failed to open registry key with ERROR_FILE_NOT_FOUND !");
			if(DEBUG) fflush(stderr);
		}else if(result == ERROR_ACCESS_DENIED) {
			if(DEBUG) fprintf(stderr, "%s \n", "NATIVE getSerialPortNames() failed to open registry key with ERROR_ACCESS_DENIED !");
			if(DEBUG) fflush(stderr);
		}else {
			if(DEBUG) fprintf(stderr, "%s %ld \n", "NATIVE getSerialPortNames() failed to open registry key with error number ", result);
			if(DEBUG) fflush(stderr);
		}
		return NULL; /* Unable to get port's name or no port exist into system etc scenarios. */
	}

	/* Count number of values for this key. */
	retCode = RegQueryInfoKey(  hKey,                    /* key handle                    */
								achClass,                /* buffer for class name         */
								&cchClassName,           /* size of class string          */
								NULL,                    /* reserved                      */
								&cSubKeys,               /* number of subkeys             */
								&cbMaxSubKey,            /* longest subkey size           */
								&cchMaxClass,            /* longest class string          */
								&cValues,                /* number of values for this key */
								&cchMaxValue,            /* longest value name            */
								&cbMaxValueData,         /* longest value data            */
								&cbSecurityDescriptor,   /* security descriptor           */
								&ftLastWriteTime);       /* last write time               */

	/* For each entry try to get names and return array constructed out of these names. */
	if(cValues > 0) {
		ports_found = (*env)->NewObjectArray(env, cValues, stringClass, NULL);
			for (i = 0; i < cValues; i++) {
				nameBuffer[0] = '\0';
				valueBuffer[0] = '\0';
				cchValueName = 1024;
				cchValueData = 1024;
				result = RegEnumValue(hKey,  /* handle to an open registry key                                                                       */
					i,                       /* index of the value to be retrieved                                                                   */
					(LPBYTE) nameBuffer,     /* pointer to a buffer that receives the name of the value as a null-terminated string                  */
					&cchValueName,           /* pointer to a variable that specifies the size of the buffer pointed to by the lpValueName parameter  */
					NULL,                    /* reserved                                                                                             */
					NULL,                    /* pointer to a variable that receives a code indicating the type of data stored in the specified value */
					(LPBYTE) valueBuffer,    /* pointer to a buffer that receives the value data for this registry entry                             */
					&cchValueData);          /* pointer to a variable that specifies the size of the buffer pointed to by the lpData parameter       */
				if(result == ERROR_SUCCESS) {
					valueBuffer[cchValueData / 2] = '\0';
					(*env)->SetObjectArrayElement(env, ports_found, i, (*env)->NewString(env, valueBuffer, (cchValueData / 2)));
				}else if(result == ERROR_MORE_DATA) {
					if(DEBUG) fprintf(stderr, "%s \n", "NATIVE getSerialPortNames() failed to read registry value with ERROR_MORE_DATA !");
					if(DEBUG) fflush(stderr);
					break;
				}else if(result == ERROR_NO_MORE_ITEMS) {
					break;
				}else {
					if(DEBUG) fprintf(stderr, "%s%ld \n", "NATIVE getSerialPortNames() failed to read registry value with error number ", result);
					if(DEBUG) fflush(stderr);
					ports_found = NULL;
					break;
				}
			}
	}

	/* NULL is returned in case no port found or an exception occurs. */
	RegCloseKey(hKey);
	return ports_found;
}

/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    openComPort
* Signature: (Ljava/lang/String;ZZZ)J
*
* Communications ports cannot be shared in the same manner as text files are shared in Windows.
*/
JNIEXPORT jlong JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_openComPort(JNIEnv *env, jobject obj, jstring portName, jboolean enableRead, jboolean enableWrite, jboolean exclusiveOwner) {
	jint ret = 0;
	DWORD errorVal;
	jint negative = -1;
	DWORD dwerror = 0;
	COMSTAT comstat;
	HANDLE hComm = INVALID_HANDLE_VALUE;
	jint OPEN_MODE = 0;
	jint SHARING = 0;
	DCB dcb = { 0 };                      /* Device control block for RS-232 serial devices */
	COMMTIMEOUTS lpCommTimeouts;
	wchar_t portFullName[512] = { 0 };

	const jchar* port = (*env)->GetStringChars(env, portName, JNI_FALSE);
	if(port == NULL) {
		if(DEBUG) fprintf(stderr, "%s \n", "NATIVE openComPort() failed to create port name string from JNI environment.");
		if(DEBUG) fflush(stderr);
		return -240;
	}

	/* To specify a COM port number greater than 9, use the following syntax : "\\.\COMXX". */
	swprintf_s(portFullName, 512/2, TEXT("\\\\.\\%s"), port);

	/* Access style; read, write or both */
	if((enableRead == JNI_TRUE) && (enableWrite == JNI_TRUE)) {
		OPEN_MODE = GENERIC_READ | GENERIC_WRITE;
	}else if(enableRead == JNI_TRUE) {
		OPEN_MODE = GENERIC_READ;
	}else if(enableWrite == JNI_TRUE) {
		OPEN_MODE = GENERIC_WRITE;
	}

	/* Exclusive ownership claim; '0' means no sharing. As per this link sharing has to be 0 means exclusive access.
	 * msdn.microsoft.com/en-us/library/windows/desktop/aa363858%28v=vs.85%29.aspx */
	SHARING = 0;

	/* The CreateFile function opens a communications port. */
	hComm = CreateFile(portFullName,
						OPEN_MODE,                                     /* Access style; read, write or both */
						SHARING,                                       /* Exclusive owner or shared */
						NULL,                                          /* Security */
						OPEN_EXISTING,                                 /* Open existing port */
						FILE_FLAG_OVERLAPPED,                          /* Overlapping operations permitted */
						NULL);					                       /* hTemplateFile */

	(*env)->ReleaseStringChars(env, portName, port);

	if(hComm == INVALID_HANDLE_VALUE) {
		errorVal = GetLastError();
		if(errorVal == ERROR_SHARING_VIOLATION) {
			return (negative * EBUSY);
		}else if(errorVal == ERROR_ACCESS_DENIED) {
			return (negative * EACCES);
		}else if((errorVal == ERROR_FILE_NOT_FOUND) || (errorVal == ERROR_PATH_NOT_FOUND)) {
			return (negative * ENXIO);
		}else if(errorVal == ERROR_INVALID_NAME) {
			return (negative * EINVAL);
		}else {
			if(DEBUG) fprintf(stderr, "%s %ld\n", "NATIVE CreateFile() in openComPort() failed with error number : ", errorVal);
			if(DEBUG) fflush(stderr);
			return -240;
		}
	}

	/* Clear the device's communication error flag if set previously due to any reason. */
	ClearCommError(hComm, &dwerror, &comstat);

	/* Make sure that the device we are going to operate on, is a valid serial port. */
	SecureZeroMemory(&dcb, sizeof(DCB));
	dcb.DCBlength = sizeof(DCB);
	ret = GetCommState(hComm, &dcb);
	if(ret == 0) {
		if(DEBUG) fprintf(stderr, "%s \n", "NATIVE GetCommState() in openComPort() failed.");
		if(DEBUG) fflush(stderr);
		CloseHandle(hComm);
		return (negative * EINVAL);
	}

	/* Set port to 9600 8N1 setting, no flow control. Bring the port in sane state. */
	dcb.BaudRate = CBR_9600;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;
	dcb.fBinary = TRUE;                    /* Windows does not support non-binary mode transfers, so this member must be TRUE. */
	dcb.fOutxCtsFlow = FALSE;
	dcb.fOutxDsrFlow = FALSE;
	dcb.fDtrControl = DTR_CONTROL_DISABLE;
	dcb.fDsrSensitivity = FALSE;
	dcb.fTXContinueOnXoff = TRUE;
	dcb.fOutX = FALSE;
	dcb.fInX = FALSE;
	dcb.fErrorChar = FALSE;
	dcb.fRtsControl = RTS_CONTROL_DISABLE;
	dcb.fAbortOnError = FALSE;
	dcb.XonLim = 1;
	dcb.XoffLim = 1;
	dcb.XonChar = 0x11;      /* Default value */
	dcb.XoffChar = 0X13;     /* Default value */
	dcb.fNull = FALSE;       /* Do not discard when null bytes are received. */
	ret = SetCommState(hComm, &dcb);
	if(ret == 0) {
		errorVal = GetLastError();
		if(errorVal == ERROR_INVALID_PARAMETER) {
			if(DEBUG)fprintf(stderr, "%s %ld\n", "NATIVE SetCommState() in openComPort() failed with error number : ", errorVal);
			if(DEBUG) fflush(stderr);
			CloseHandle(hComm);
			return (negative * EINVAL);
		}
		fprintf(stderr, "%s %ld\n", "NATIVE SetCommState() in openComPort() failed with error number : ", errorVal);
		fflush(stderr);
		CloseHandle(hComm);
		return -240;
	}

	/* In Windows, timeout related settings needs to be cleared as they might have values from previous applications. */
	lpCommTimeouts.ReadIntervalTimeout = 1;
	lpCommTimeouts.ReadTotalTimeoutConstant = 1;
	lpCommTimeouts.ReadTotalTimeoutMultiplier = 1;
	lpCommTimeouts.WriteTotalTimeoutConstant = 1;
	lpCommTimeouts.WriteTotalTimeoutMultiplier = 1;
	ret = SetCommTimeouts(hComm, &lpCommTimeouts);
	if(ret == 0) {
		errorVal = GetLastError();
		if(DEBUG) fprintf(stderr, "%s%ld\n", "NATIVE SetCommTimeouts() in openComPort() failed with error number : ", errorVal);
		if(DEBUG) fprintf(stderr, "%s \n", "PLEASE RETRY OPENING SERIAL PORT");
		if(DEBUG) fflush(stderr);
		CloseHandle(hComm);
		return -240;
	}

	/* Abort outstanding I/O operations, clear port's I/O buffer (flush old garbage values). */
	PurgeComm(hComm, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);

	return (jlong)hComm;
}

/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    closeComPort
* Signature: (J)I
*
* Exclusive ownership is cleared automatically.
*/
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_closeComPort(JNIEnv *env, jobject obj, jlong handle) {
	jint ret = -1;
	jint negative = -1;
	HANDLE hComm = (HANDLE)handle;
	DWORD errorVal;

	/* Flush remaining data in IO buffers. */
	ret = FlushFileBuffers(hComm);
	if(ret == 0) {
		errorVal = GetLastError();
		if(DEBUG) fprintf(stderr, "%s%ld\n", "NATIVE FlushFileBuffers() in closeComPort() failed to flush data with error number : ", errorVal);
		if(DEBUG) fflush(stderr);
	}

	/* Close the port. */
	ret = CloseHandle(hComm);
	if(ret == 0) {
		errorVal = GetLastError();
		if(DEBUG) fprintf(stderr, "%s %ld\n", "NATIVE CloseHandle() in closeComPort() failed with error number : ", errorVal);
		if(DEBUG) fflush(stderr);
		return -240;
	}

	return 0;
}

/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    readBytes
* Signature: (JI)[B
*
*/
JNIEXPORT jbyteArray JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_readBytes(JNIEnv *env, jobject obj, jlong handle, jint count) {
	jint ret = 0;
	jint negative = -1;
	HANDLE hComm = (HANDLE)handle;
	DWORD errorVal;
	jbyte data_buf[1024];
	DWORD num_of_bytes_read;
	OVERLAPPED overlapped;
	jbyteArray data_read;

	/* Only hEvent member need to be initialled and others can be left 0. */
	memset(&overlapped, 0, sizeof(overlapped));
	overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if(overlapped.hEvent == NULL) {
		if(DEBUG) fprintf(stderr, "%s\n", "NATIVE CreateEvent() in readBytes() failed creating overlapped event handle !");
		if(DEBUG) fflush(stderr);
		return NULL;
	}

	ret = ReadFile(hComm, data_buf, 1024, &num_of_bytes_read, &overlapped);
	if(ret == 0) {
		errorVal = GetLastError();
		if(errorVal == ERROR_IO_PENDING) {
			if(WaitForSingleObject(overlapped.hEvent, 10) == WAIT_OBJECT_0) {
				ret = GetOverlappedResult(hComm, &overlapped, &num_of_bytes_read, FALSE);
				if(ret == 0) {
					errorVal = GetLastError();
					if(DEBUG) fprintf(stderr, "%s %ld\n", "NATIVE GetOverlappedResult() in readBytes() failed with error number : ", errorVal);
					if(DEBUG) fflush(stderr);
					return NULL;
				}
			}
		}else if((errorVal == ERROR_INVALID_USER_BUFFER) || (errorVal == ERROR_NOT_ENOUGH_MEMORY)) {
			if(DEBUG) fprintf(stderr, "%s\n", "NATIVE CreateEvent() in ReadFile() failed with error ETOOMANYOP !");
			if(DEBUG) fflush(stderr);
			return NULL;
		}else if((errorVal == ERROR_NOT_ENOUGH_QUOTA) || (errorVal == ERROR_INSUFFICIENT_BUFFER)) {
			if(DEBUG) fprintf(stderr, "%s\n", "NATIVE CreateEvent() in ReadFile() failed with error ENOMEM !");
			if(DEBUG) fflush(stderr);
			return NULL;
		}else if(errorVal == ERROR_OPERATION_ABORTED) {
			if(DEBUG) fprintf(stderr, "%s\n", "NATIVE CreateEvent() in ReadFile() failed with error ECANCELED !");
			if(DEBUG) fflush(stderr);
			return NULL;
		}else {
			if(DEBUG) fprintf(stderr, "%s %ld\n", "NATIVE ReadFile() in readBytes() failed with error number : ", errorVal);
			if(DEBUG) fflush(stderr);
			return NULL;
		}
	}

	data_read = (*env)->NewByteArray(env, num_of_bytes_read);
	(*env)->SetByteArrayRegion(env, data_read, 0, num_of_bytes_read, data_buf);
	CloseHandle(overlapped.hEvent);

	return data_read;
}

/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    writeBytes
* Signature: (J[BI)I
*
* Note that write method return success does not mean data has been sent to receiver. Therefore we flush data after writing using 'TCSBRK' ioctl.
* Delay is in micro-seconds.
*/
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_writeBytes(JNIEnv *env, jobject obj, jlong handle, jbyteArray buffer, jint delay) {
	int ret = 0;
	BOOL result = FALSE;
	jint negative = -1;
	DWORD errorVal = 0;
	HANDLE hComm = (HANDLE)handle;
	jbyte* data_buf = (*env)->GetByteArrayElements(env, buffer, JNI_FALSE);
	DWORD byte_count = (*env)->GetArrayLength(env, buffer);
	DWORD num_of_bytes_written = 0;
	OVERLAPPED ovWrite = { 0 };

	/* Only hEvent member need to be initialled and others can be left 0. */
	ovWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if(ovWrite.hEvent == NULL) {
		if(DEBUG) fprintf(stderr, "%s\n", "NATIVE CreateEvent() in writeBytes() failed creating overlapped event handle !");
		if(DEBUG) fflush(stderr);
		return -240;
	}

	result = WriteFile(hComm, data_buf, byte_count, &num_of_bytes_written, &ovWrite);
	if(result == FALSE) {
		errorVal = GetLastError();
		if(errorVal == ERROR_IO_PENDING) {
			if(WaitForSingleObject(ovWrite.hEvent, INFINITE) == WAIT_OBJECT_0) {
				ret = GetOverlappedResult(hComm, &ovWrite, &num_of_bytes_written, TRUE);
				if(ret == 0) {
					errorVal = GetLastError();
					if(DEBUG) fprintf(stderr, "%s %ld\n", "NATIVE GetOverlappedResult() in writeBytes() failed with error number : ", errorVal);
					if(DEBUG) fflush(stderr);
					return -240;
				}
			}
		}else if((errorVal == ERROR_INVALID_USER_BUFFER) || (errorVal == ERROR_NOT_ENOUGH_MEMORY)) {
			return (negative * ETOOMANYOP);
		}else if(errorVal == ERROR_NOT_ENOUGH_QUOTA) {
			return (negative * ENOMEM);
		}else if(errorVal == ERROR_OPERATION_ABORTED) {
			return (negative * ECANCELED);
		}else {
			if(DEBUG) fprintf(stderr, "%s %ld\n", "NATIVE WriteFile() in writeBytes() failed with error number : ", errorVal);
			if(DEBUG) fflush(stderr);
			return -240;
		}
	}

	(*env)->ReleaseByteArrayElements(env, buffer, data_buf, 0);
	CloseHandle(ovWrite.hEvent);
	return 0;
}

/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    configureComPortData
* Signature: (JIIIII)I
*/
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_configureComPortData(JNIEnv *env, jobject obj, jlong handle, jint dataBits, jint stopBits, jint parity, jint baudRateTranslated, jint custBaudTranslated) {
	jint ret = 0;
	DWORD errorVal = 0;
	jint negative = -1;
	DWORD baud = -1;
	HANDLE hComm = (HANDLE)handle;
	DCB dcb = { 0 };

	SecureZeroMemory(&dcb, sizeof(DCB));
	dcb.DCBlength = sizeof(DCB);
	ret = GetCommState(hComm, &dcb);
	if(ret == 0) {
		errorVal = GetLastError();
		if(DEBUG) fprintf(stderr, "%s %ld\n", "NATIVE SetCommState() in configureComPortData() failed with error number : ", errorVal);
		if(DEBUG) fflush(stderr);
		return -240;
	}

	if(baudRateTranslated == 251) {
		baud = custBaudTranslated;
	}else {
		/* Baudrate support depends upon operating system, driver and chipset used. */
		switch (baudRateTranslated) {
			case 0: baud = 0;
				break;
			case 50: baud = 50;
				break;
			case 75: baud = 75;
				break;
			case 110: baud = CBR_110;
				break;
			case 134: baud = 134;
				break;
			case 150: baud = 150;
				break;
			case 200: baud = 200;
				break;
			case 300: baud = CBR_300;
				break;
			case 600: baud = CBR_600;
				break;
			case 1200: baud = CBR_1200;
				break;
			case 1800: baud = 1800;
				break;
			case 2400: baud = CBR_2400;
				break;
			case 4800: baud = CBR_4800;
				break;
			case 9600: baud = CBR_9600;
				break;
			case 14400: baud = CBR_14400;
				break;
			case 19200: baud = CBR_19200;
				break;
			case 28800: baud = 28800;
				break;
			case 38400: baud = CBR_38400;
				break;
			case 56000: baud = 56000;
				break;
			case 57600: baud = 57600;
				break;
			case 115200: baud = 115200;
				break;
			case 128000: baud = 128000;
				break;
			case 153600: baud = 153600;
				break;
			case 230400: baud = 230400;
				break;
			case 256000: baud = 256000;
				break;
			case 460800: baud = 460800;
				break;
			case 500000: baud = 500000;
				break;
			case 576000: baud = 576000;
				break;
			case 921600: baud = 921600;
				break;
			case 1000000: baud = 1000000;
				break;
			case 1152000: baud = 1152000;
				break;
			case 1500000: baud = 1500000;
				break;
			case 2000000: baud = 2000000;
				break;
			case 2500000: baud = 2500000;
				break;
			case 3000000: baud = 3000000;
				break;
			case 3500000: baud = 3500000;
				break;
			case 4000000: baud = 4000000;
				break;
			default: baud = -1;
				break;
		}
	}
	/* Set baud rate after appropriate manipulation has been done. */
	dcb.BaudRate = baud;

	/* Set data bits. This Specifies the bits per data byte transmitted and received. */
	dcb.ByteSize = (BYTE)dataBits;

	/* Set stop bits. */
	if(stopBits == 1) {
		dcb.StopBits = ONESTOPBIT;
	}else if(stopBits == 4) {
		dcb.StopBits = ONE5STOPBITS;
	}else if(stopBits == 2) {
		dcb.StopBits = TWOSTOPBITS;
	}

	/* Set parity */
	dcb.fParity = TRUE;
	if(parity == 1) {
		dcb.fParity = FALSE;
		dcb.Parity = NOPARITY;
	}else if (parity == 2) {
		dcb.Parity = ODDPARITY;
	}else if (parity == 3) {
		dcb.Parity = EVENPARITY;
	}else if (parity == 4) {
		dcb.Parity = MARKPARITY;
	}else if (parity == 5) {
		dcb.Parity = SPACEPARITY;
	}

	ret = SetCommState(hComm, &dcb);
	if(ret == 0) {
		errorVal = GetLastError();
		if(DEBUG) fprintf(stderr, "%s %ld\n", "NATIVE SetCommState() in configureComPortData() failed with error number : ", errorVal);
		if(DEBUG) fflush(stderr);
		if(errorVal == ERROR_INVALID_PARAMETER) {
			return (negative * EINVAL);
		}
		return -240;
	}

	/* Flush old garbage values in IO port buffer for this port. */
	PurgeComm(hComm, PURGE_RXCLEAR | PURGE_TXCLEAR);

	return 0;
}

/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    configureComPortControl
* Signature: (JICCZZ)I
*
*/
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_configureComPortControl(JNIEnv *env, jobject obj, jlong handle, jint flowctrl, jchar xon, jchar xoff, jboolean ParFraError, jboolean overFlowErr) {
	jint ret = 0;
	DWORD errorVal;
	jint negative = -1;
	DWORD baud = -1;
	HANDLE hComm = (HANDLE)handle;
	DCB dcb = { 0 };

	FillMemory(&dcb, sizeof(dcb), 0);
	dcb.DCBlength = sizeof(DCB);
	ret = GetCommState(hComm, &dcb);
	if(ret == 0) {
		errorVal = GetLastError();
		if(DEBUG) fprintf(stderr, "%s %ld\n", "NATIVE GetCommState() in configureComPortControl() failed with error number : ", errorVal);
		if(DEBUG) fflush(stderr);
		return -240;
	}

	/* Set flow control. */
	if(flowctrl == 1) {                          /* No flow control. */
		dcb.fOutX = FALSE;
		dcb.fInX = FALSE;
		dcb.fOutxCtsFlow = FALSE;
		dcb.fOutxDsrFlow = FALSE;
		dcb.fDsrSensitivity = FALSE;
		dcb.fOutxCtsFlow = FALSE;
		dcb.fOutxDsrFlow = FALSE;
		dcb.fDtrControl = DTR_CONTROL_DISABLE;
		dcb.fRtsControl = RTS_CONTROL_DISABLE;
	}else if (flowctrl == 2) {                   /* Hardware flow control. */
		dcb.fOutX = FALSE;
		dcb.fInX = FALSE;
		dcb.fOutxCtsFlow = TRUE;
		dcb.fOutxDsrFlow = TRUE;
		dcb.fDsrSensitivity = TRUE;
		dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
		dcb.fDtrControl = DTR_CONTROL_HANDSHAKE;
	}else if (flowctrl == 3) {                  /* Software flow control. */
		dcb.fOutX = TRUE;
		dcb.fInX = TRUE;
		dcb.fOutxCtsFlow = FALSE;
		dcb.fOutxDsrFlow = FALSE;
		dcb.fDsrSensitivity = FALSE;
		dcb.fDtrControl = DTR_CONTROL_DISABLE;
		dcb.fRtsControl = RTS_CONTROL_DISABLE;
		dcb.XonChar = (char) xon;
		dcb.XoffChar = (char) xoff;
		dcb.XonLim = 2048;
		dcb.XoffLim = 512;
	}

	/* Set parity and frame error. */


	/* Set buffer overrun error. */

	return 0;
}

/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    setRTS
* Signature: (JZ)I
*/
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_setRTS(JNIEnv *env, jobject obj, jlong handle, jboolean enabled) {
	jint ret = 0;
	DWORD errorVal;
	HANDLE hComm = (HANDLE)handle;
	DWORD RTSVAL;

	if(enabled == JNI_TRUE){
		RTSVAL = SETRTS;
	}else {
		RTSVAL = CLRRTS;
	}

	ret = EscapeCommFunction(hComm, RTSVAL);
	if(ret == 0) {
		errorVal = GetLastError();
		if(DEBUG) fprintf(stderr, "%s %ld\n", "NATIVE EscapeCommFunction() in setRTS() failed with error number : ", errorVal);
		if(DEBUG) fflush(stderr);
		return -240;
	}

	return 0;
}

/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    setDTR
* Signature: (JZ)I
*/
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_setDTR(JNIEnv *env, jobject obj, jlong handle, jboolean enabled) {
	jint ret = 0;
	DWORD errorVal;
	HANDLE hComm = (HANDLE)handle;
	DWORD DTRVAL;

	if(enabled == JNI_TRUE){
		DTRVAL = SETDTR;
	}else {
		DTRVAL = CLRDTR;
	}

	ret = EscapeCommFunction(hComm, DTRVAL);
	if(ret == 0) {
		errorVal = GetLastError();
		if(DEBUG) fprintf(stderr, "%s %ld\n", "NATIVE EscapeCommFunction() in setDTR() failed with error number : ", errorVal);
		if(DEBUG) fflush(stderr);
		return -240;
	}

	return 0;
}

/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    getCurrentConfigurationW
* Signature: (J)[Ljava/lang/String;
*
* We return the bit mask as it is with out interpretation so that application can manipulate easily using mathematics.
*/
JNIEXPORT jobjectArray JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_getCurrentConfigurationW(JNIEnv *env, jobject obj, jlong handle) {
	jint ret = 0;
	DWORD errorVal;
	jint negative = -1;
	HANDLE hComm = (HANDLE)handle;
	DCB dcb = { 0 };
	char tmp[100] = { 0 };  /* 100 is selected randomly. */
	char tmp1[100] = { 0 };
	char *tmp1Ptr = tmp1;

	FillMemory(&dcb, sizeof(dcb), 0);
	dcb.DCBlength = sizeof(DCB);
	ret = GetCommState(hComm, &dcb);
	if(ret == 0) {
		errorVal = GetLastError();
		if(DEBUG) fprintf(stderr, "%s %ld\n", "NATIVE GetCommState() in getCurrentConfiguration() failed with error number : ", errorVal);
		if(DEBUG) fflush(stderr);
		return NULL;
	}

	jclass strClass = (*env)->FindClass(env, "java/lang/String");
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	jobjectArray current_config = (*env)->NewObjectArray(env, 28, strClass, NULL);
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcblength = "DCBlength : ";
	memset(tmp, 0, sizeof(tmp));
	memset(tmp1, 0, sizeof(tmp1));
	strcpy_s(tmp, sizeof(tmp), dcblength);
	sprintf_s(tmp1, sizeof(tmp1), "%lu", dcb.DCBlength);
	strcat_s(tmp, sizeof(tmp), tmp1Ptr);
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 0, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbbaud = "BaudRate : ";
	memset(tmp, 0, sizeof(tmp));
	memset(tmp1, 0, sizeof(tmp1));
	strcpy_s(tmp, sizeof(tmp), dcbbaud);
	sprintf_s(tmp1, sizeof(tmp1), "%lu", dcb.BaudRate);
	strcat_s(tmp, sizeof(tmp), tmp1Ptr);
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 1, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbbin = "fBinary : ";
	memset(tmp, 0, sizeof(tmp));
	strcpy_s(tmp, sizeof(tmp), dcbbin);
	if(dcb.fBinary == TRUE) {
		strcat_s(tmp, sizeof(tmp), "TRUE");
	}else {
		strcat_s(tmp, sizeof(tmp), "FALSE");
	}
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 2, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbpar = "fParity : ";
	memset(tmp, 0, sizeof(tmp));
	strcpy_s(tmp, sizeof(tmp), dcbpar);
	if(dcb.fParity == TRUE) {
		strcat_s(tmp, sizeof(tmp), "TRUE");
	}else {
		strcat_s(tmp, sizeof(tmp), "FALSE");
	}
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 3, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbocts = "fOutxCtsFlow : ";
	memset(tmp, 0, sizeof(tmp));
	strcpy_s(tmp, sizeof(tmp), dcbocts);
	if(dcb.fOutxCtsFlow == TRUE) {
		strcat_s(tmp, sizeof(tmp), "TRUE");
	}else {
		strcat_s(tmp, sizeof(tmp), "FALSE");
	}
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 4, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbodsr = "fOutxDsrFlow : ";
	memset(tmp, 0, sizeof(tmp));
	strcpy_s(tmp, sizeof(tmp), dcbodsr);
	if(dcb.fOutxDsrFlow == TRUE) {
		strcat_s(tmp, sizeof(tmp), "TRUE");
	}else {
		strcat_s(tmp, sizeof(tmp), "FALSE");
	}
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 5, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbdtrc = "fDtrControl : ";
	memset(tmp, 0, sizeof(tmp));
	strcpy_s(tmp, sizeof(tmp), dcbdtrc);
	if (dcb.fDtrControl == DTR_CONTROL_DISABLE) {
		strcat_s(tmp, sizeof(tmp), "DTR_CONTROL_DISABLE");
	}
	else if(dcb.fDtrControl == DTR_CONTROL_ENABLE) {
		strcat_s(tmp, sizeof(tmp), "DTR_CONTROL_ENABLE");
	}
	else if(dcb.fDtrControl == DTR_CONTROL_HANDSHAKE) {
		strcat_s(tmp, sizeof(tmp), "DTR_CONTROL_HANDSHAKE");
	}
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 6, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbdsrs = "fDsrSensitivity : ";
	memset(tmp, 0, sizeof(tmp));
	strcpy_s(tmp, sizeof(tmp), dcbdsrs);
	if(dcb.fDsrSensitivity == TRUE) {
		strcat_s(tmp, sizeof(tmp), "TRUE");
	}else {
		strcat_s(tmp, sizeof(tmp), "FALSE");
	}
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 7, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbtxcox = "fTXContinueOnXoff : ";
	memset(tmp, 0, sizeof(tmp));
	strcpy_s(tmp, sizeof(tmp), dcbtxcox);
	if(dcb.fTXContinueOnXoff == TRUE) {
		strcat_s(tmp, sizeof(tmp), "TRUE");
	}else {
		strcat_s(tmp, sizeof(tmp), "FALSE");
	}
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 8, (*env)->NewStringUTF(env, tmp));
	if ((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbfox = "fOutX : ";
	memset(tmp, 0, sizeof(tmp));
	strcpy_s(tmp, sizeof(tmp), dcbfox);
	if(dcb.fOutX == TRUE) {
		strcat_s(tmp, sizeof(tmp), "TRUE");
	}else {
		strcat_s(tmp, sizeof(tmp), "FALSE");
	}
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 9, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbfix = "fInX : ";
	memset(tmp, 0, sizeof(tmp));
	strcpy_s(tmp, sizeof(tmp), dcbfix);
	if (dcb.fInX == TRUE) {
		strcat_s(tmp, sizeof(tmp), "TRUE");
	}else {
		strcat_s(tmp, sizeof(tmp), "FALSE");
	}
	(*env)->SetObjectArrayElement(env, current_config, 10, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbec = "fErrorChar : ";
	memset(tmp, 0, sizeof(tmp));
	strcpy_s(tmp, sizeof(tmp), dcbec);
	if(dcb.fErrorChar == TRUE) {
		strcat_s(tmp, sizeof(tmp), "TRUE");
	}else {
		strcat_s(tmp, sizeof(tmp), "FALSE");
	}
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 11, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbfn = "fNull : ";
	memset(tmp, 0, sizeof(tmp));
	strcpy_s(tmp, sizeof(tmp), dcbfn);
	if(dcb.fNull == TRUE) {
		strcat_s(tmp, sizeof(tmp), "TRUE");
	}else {
		strcat_s(tmp, sizeof(tmp), "FALSE");
	}
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 12, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbrtsc = "fRtsControl : ";
	memset(tmp, 0, sizeof(tmp));
	strcpy_s(tmp, sizeof(tmp), dcbrtsc);
	if(dcb.fRtsControl == DTR_CONTROL_DISABLE) {
		strcat_s(tmp, sizeof(tmp), "RTS_CONTROL_DISABLE");
	}
	else if (dcb.fRtsControl == DTR_CONTROL_ENABLE) {
		strcat_s(tmp, sizeof(tmp), "RTS_CONTROL_ENABLE");
	}
	else if (dcb.fRtsControl == DTR_CONTROL_HANDSHAKE) {
		strcat_s(tmp, sizeof(tmp), "RTS_CONTROL_HANDSHAKE");
	}
	else if (dcb.fRtsControl == RTS_CONTROL_TOGGLE) {
		strcat_s(tmp, sizeof(tmp), "RTS_CONTROL_TOGGLE");
	}
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 13, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbabo = "fAbortOnError : ";
	memset(tmp, 0, sizeof(tmp));
	strcpy_s(tmp, sizeof(tmp), dcbabo);
	if(dcb.fAbortOnError == TRUE) {
		strcat_s(tmp, sizeof(tmp), "TRUE");
	}else {
		strcat_s(tmp, sizeof(tmp), "FALSE");
	}
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 14, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbfdu = "fDummy2 : NA";
	memset(tmp, 0, sizeof(tmp));
	strcpy_s(tmp, sizeof(tmp), dcbfdu);
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 15, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbwrs = "wReserved : NA";
	memset(tmp, 0, sizeof(tmp));
	strcpy_s(tmp, sizeof(tmp), dcbwrs);
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 16, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbxom = "XonLim : ";
	memset(tmp, 0, sizeof(tmp));
	memset(tmp1, 0, sizeof(tmp1));
	strcpy_s(tmp, sizeof(tmp), dcbxom);
	sprintf_s(tmp1, sizeof(tmp1), "%lu", dcb.XonLim);
	strcat_s(tmp, sizeof(tmp), tmp1Ptr);
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 17, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbxof = "XoffLim : ";
	memset(tmp, 0, sizeof(tmp));
	memset(tmp1, 0, sizeof(tmp1));
	strcpy_s(tmp, sizeof(tmp), dcbxof);
	sprintf_s(tmp1, sizeof(tmp1), "%lu", dcb.XoffLim);
	strcat_s(tmp, sizeof(tmp), tmp1Ptr);
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 18, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbbs = "ByteSize : ";
	memset(tmp, 0, sizeof(tmp));
	memset(tmp1, 0, sizeof(tmp1));
	strcpy_s(tmp, sizeof(tmp), dcbbs);
	sprintf_s(tmp1, sizeof(tmp1), "%lu", dcb.ByteSize);
	strcat_s(tmp, sizeof(tmp), tmp1Ptr);
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 19, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbpr = "Parity : ";
	memset(tmp, 0, sizeof(tmp));
	memset(tmp1, 0, sizeof(tmp1));
	strcpy_s(tmp, sizeof(tmp), dcbpr);
	sprintf_s(tmp1, sizeof(tmp1), "%lu", dcb.Parity);
	strcat_s(tmp, sizeof(tmp), tmp1Ptr);
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 20, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbsb = "StopBits : ";
	memset(tmp, 0, sizeof(tmp));
	memset(tmp1, 0, sizeof(tmp1));
	strcpy_s(tmp, sizeof(tmp), dcbsb);
	sprintf_s(tmp1, sizeof(tmp1), "%lu", dcb.StopBits);
	strcat_s(tmp, sizeof(tmp), tmp1Ptr);
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 21, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbxoc = "XonChar : ";
	memset(tmp, 0, sizeof(tmp));
	memset(tmp1, 0, sizeof(tmp1));
	strcpy_s(tmp, sizeof(tmp), dcbxoc);
	sprintf_s(tmp1, sizeof(tmp1), "%lc", dcb.XonChar);
	strcat_s(tmp, sizeof(tmp), tmp1Ptr);
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 22, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbxofc = "XoffChar : ";
	memset(tmp, 0, sizeof(tmp));
	memset(tmp1, 0, sizeof(tmp1));
	strcpy_s(tmp, sizeof(tmp), dcbxofc);
	sprintf_s(tmp1, sizeof(tmp1), "%lc", dcb.XoffChar);
	strcat_s(tmp, sizeof(tmp), tmp1Ptr);
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 23, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcberh = "ErrorChar : ";
	memset(tmp, 0, sizeof(tmp));
	memset(tmp1, 0, sizeof(tmp1));
	strcpy_s(tmp, sizeof(tmp), dcberh);
	sprintf_s(tmp1, sizeof(tmp1), "%lc", dcb.ErrorChar);
	strcat_s(tmp, sizeof(tmp), tmp1Ptr);
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 24, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbeofch = "EofChar : ";
	memset(tmp, 0, sizeof(tmp));
	memset(tmp1, 0, sizeof(tmp1));
	strcpy_s(tmp, sizeof(tmp), dcbeofch);
	sprintf_s(tmp1, sizeof(tmp1), "%lc", (char)dcb.EofChar);
	strcat_s(tmp, sizeof(tmp), tmp1Ptr);
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 25, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbevar = "EvtChar : ";
	memset(tmp, 0, sizeof(tmp));
	memset(tmp1, 0, sizeof(tmp1));
	strcpy_s(tmp, sizeof(tmp), dcbevar);
	sprintf_s(tmp1, sizeof(tmp1), "%lc", dcb.EvtChar);
	strcat_s(tmp, sizeof(tmp), tmp1Ptr);
	strcat_s(tmp, sizeof(tmp), "\n");
	(*env)->SetObjectArrayElement(env, current_config, 26, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	char* dcbwrsa = "wReserved1 : NA";
	memset(tmp, 0, sizeof(tmp));
	strcpy_s(tmp, sizeof(tmp), dcbwrsa);
	(*env)->SetObjectArrayElement(env, current_config, 27, (*env)->NewStringUTF(env, tmp));
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}

	return current_config;
}

/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    getByteCount
* Signature: (J)[I
*
* Return array's sequence is error number, number of input bytes, number of output bytes in tty buffers.
*/
JNIEXPORT jintArray JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_getByteCount(JNIEnv *env, jobject obj, jlong handle) {
	int ret = 0;
	int errorVal = 0;
	HANDLE hComm = (HANDLE)handle;
	DWORD errors;
	COMSTAT comstat;
	jint val[3] = { 0, 0, 0 };
	jintArray values = (*env)->NewIntArray(env, 3);

	ret = ClearCommError(hComm, &errors, &comstat);
	if(ret == 0) {
		errorVal = GetLastError();
		if(DEBUG) fprintf(stderr, "%s %d\n", "NATIVE ClearCommError() in getByteCount() failed with error number : ", errorVal);
		if(DEBUG) fflush(stderr);
		val[0] = -240;
		(*env)->SetIntArrayRegion(env, values, 0, 3, val);
		return values;
	}

	val[0] = 0;
	val[1] = (jint)comstat.cbInQue;
	val[2] = (jint)comstat.cbOutQue;
	(*env)->SetIntArrayRegion(env, values, 0, 3, val);
	return values;
}

/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    clearPortIOBuffers
* Signature: (JZZ)I
*
*  This will discard all pending data in given buffers. Received data therefore can not be read by application or/and data to be transmitted
*  in output buffer will get discarded i.e. not transmitted.
*/
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_clearPortIOBuffers(JNIEnv *env, jobject obj, jlong handle, jboolean rxPortbuf, jboolean txPortbuf) {
	jint ret = -1;
	jint negative = -1;
	DWORD errorVal = -1;
	jint PORTIOBUFFER = -1;
	HANDLE hComm = (HANDLE)handle;

	if((rxPortbuf == JNI_TRUE) && (txPortbuf == JNI_TRUE)) {
		PORTIOBUFFER = PURGE_RXCLEAR | PURGE_TXCLEAR;
	}else if(rxPortbuf == JNI_TRUE) {
		PORTIOBUFFER = PURGE_RXCLEAR;
	}else if(txPortbuf == JNI_TRUE) {
		PORTIOBUFFER = PURGE_TXCLEAR;
	}else {
	}

TODO FIND WHT ERROR PURGECOM MAY RETURN AND PASS MEANINGFUL ERROR N INSTEAD OF 240
	ret = PurgeComm(hComm, PORTIOBUFFER);
	if(ret == 0) {
		errorVal = GetLastError();
		if(DEBUG) fprintf(stderr, "%s %ld\n", "NATIVE PurgeComm() in clearPortIOBuffers() failed with error number : ", errorVal);
		if(DEBUG) fflush(stderr);
		return -240;
	}

	return 0;
}

/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    getLinesStatus
* Signature: (J)[I
*
* The status of modem/control lines is returned as array of integers where '1' means line is asserted and '0' means de-asserted.
* The sequence of lines matches in both java layer and native layer.
* Last three values, DTR, RTS, LOOP are set to 0, as windows does not have any API to read there status.
*/
JNIEXPORT jintArray JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_getLinesStatus(JNIEnv *env, jobject obj, jlong handle) {
	jint ret = -1;
	DWORD errorVal = -1;
	jint status[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	HANDLE hComm = (HANDLE)handle;
	DWORD modem_stat;
	jintArray current_status = (*env)->NewIntArray(env, 8);

	ret = GetCommModemStatus(hComm, &modem_stat);
	if(ret == 0) {
		errorVal = GetLastError();
		if(DEBUG) fprintf(stderr, "%s %ld\n", "NATIVE GetCommModemStatus() in getLinesStatus() failed with error number : ", errorVal);
		if(DEBUG) fflush(stderr);
		status[0] = -240;
		(*env)->SetIntArrayRegion(env, current_status, 0, 8, status);
		return current_status;
	}

	status[0] = 0;
	status[1] = (modem_stat & MS_CTS_ON)  ? 1 : 0;
	status[2] = (modem_stat & MS_DSR_ON)  ? 1 : 0;
	status[3] = (modem_stat & MS_RLSD_ON) ? 1 : 0;
	status[4] = (modem_stat & MS_RING_ON) ? 1 : 0;
	status[5] = 0;
	status[6] = 0;
	status[7] = 0;

	(*env)->SetIntArrayRegion(env, current_status, 0, 8, status);
	return current_status;
}

/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    sendBreak
* Signature: (JI)I
*
* The duration is in milliseconds.
*/
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_sendBreak(JNIEnv *env, jobject obj, jlong handle, jint duration) {
	jint ret = -1;
	DWORD errorVal = -1;
	HANDLE hComm = (HANDLE)handle;

	ret = SetCommBreak(hComm);
	if(ret == 0) {
		errorVal = GetLastError();
		if(DEBUG) fprintf(stderr, "%s%ld\n", "NATIVE SetCommBreak() in sendBreak() failed with error number : ", errorVal);
		if(DEBUG) fflush(stderr);
		return -240;
	}

	Sleep(duration);

	ret = ClearCommBreak(hComm);
	if(ret == 0) {
		errorVal = GetLastError();
		if(DEBUG) fprintf(stderr, "%s%ld\n", "NATIVE ClearCommBreak() in sendBreak() failed with error number : ", errorVal);
		if(DEBUG) fflush(stderr);
		return -240;
	}

	return 0;
}

/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    getInterruptCount
* Signature: (J)I
*
* Not supported on windows. Return 0 for all indexes.
*/
JNIEXPORT jintArray JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_getInterruptCount(JNIEnv *env, jobject obj, jlong handle) {
	jint count_info[12] = { 0 };
	jintArray interrupt_info = (*env)->NewIntArray(env, sizeof(count_info));
	(*env)->SetIntArrayRegion(env, interrupt_info, 0, sizeof(count_info), count_info);
	return interrupt_info;
}

/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    setMinDataLength
* Signature: (JI)I
*
* This function changes the behaviour of when data listener is called based on the value of numOfBytes variable.
* The listener will be called only when this many bytes will be available to read from file descriptor.
* Not supported for Windows OS. Return -1 notifying application.
*/
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_setMinDataLength(JNIEnv *env, jobject obj, jlong handle, jint numOfBytes) {
	return -1;
}

/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    setUpDataLooperThread
* Signature: (JLcom/embeddedunveiled/serial/SerialComLooper;)I
* Both setUpDataLooperThread() and setUpEventLooperThread() call same function setupLooperThread().
*/
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_setUpDataLooperThread(JNIEnv *env, jobject obj, jlong handle, jobject looper) {
	/* Check whether thread for this handle already exist or not. If it exist just update event
	* mask to wait for otherwise create thread. */
	int x = 0;
	int ret = 0;
	int thread_exist = 0;
	struct looper_thread_params *ptr;
	ptr = handle_looper_info;
	HANDLE hComm = (HANDLE)handle;
	DWORD event_mask;
	DWORD updated_mask = 0;
	DWORD error_type = 0;
	COMSTAT com_stat;
	DWORD errorVal;

	for (x = 0; x < MAX_NUM_THREADS; x++) {
		if(ptr->hComm == hComm) {
			thread_exist = 1;
			break;
		}
		ptr++;
	}

	if(thread_exist == 1) {
		/* Thread exist so just update event to listen to. */
		ret = GetCommMask(hComm, &event_mask);
		updated_mask = event_mask | EV_RXCHAR;
		ret = SetCommMask(hComm, updated_mask);
		if(ret == 0) {
			errorVal = GetLastError();
			if(DEBUG) fprintf(stderr, "%s%ld\n", "NATIVE setUpDataLooperThread() failed in SetCommMask() with error number : ", errorVal);
			if(DEBUG) fprintf(stderr, "%s \n", "Try again !");
			if(DEBUG) fflush(stderr);
			ClearCommError(hComm, &error_type, &com_stat);
			return -240;
		}
	}else {
		/* Not found in our records, so we create the thread. */
		return setupLooperThread(env, obj, handle, looper, 1, 0);
	}

	return -1;
}
/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    setUpEventLooperThread
* Signature: (JLcom/embeddedunveiled/serial/SerialComLooper;)I

* Both data and event creation function call same function setupLooperThread().
*/
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_setUpEventLooperThread(JNIEnv *env, jobject obj, jlong handle, jobject looper) {
	/* Check whether thread for this handle already exist or not.
	* If it exist just update event mask to wait for otherwise create thread. */
	int x = 0;
	int ret = 0;
	int thread_exist = 0;
	struct looper_thread_params *ptr;
	ptr = handle_looper_info;
	HANDLE hComm = (HANDLE)handle;
	DWORD event_mask;
	DWORD updated_mask = 0;
	DWORD error_type = 0;
	COMSTAT com_stat;
	DWORD errorVal;

	for(x = 0; x < MAX_NUM_THREADS; x++) {
		if(ptr->hComm == hComm) {
			thread_exist = 1;
			break;
		}
		ptr++;
	}

	if(thread_exist == 1) {
		/* Thread exist so just update event to listen to. */
		ret = GetCommMask(hComm, &event_mask);
		updated_mask = event_mask | EV_BREAK | EV_CTS | EV_DSR | EV_ERR | EV_RING | EV_RLSD | EV_RXFLAG;
		ret = SetCommMask(hComm, updated_mask);
		if(ret == 0) {
			errorVal = GetLastError();
			if(DEBUG) fprintf(stderr, "%s %ld\n", "NATIVE setUpEventLooperThread() failed in SetCommMask() with error number : ", errorVal);
			if(DEBUG) fprintf(stderr, "%s \n", "Try again !");
			if(DEBUG) fflush(stderr);
			ClearCommError(hComm, &error_type, &com_stat);
			return -240;
		}
	}else {
		/* Not found in our records, so we create the thread. */
		return setupLooperThread(env, obj, handle, looper, 0, 1);
	}

	return -1;
}

int setupLooperThread(JNIEnv *env, jobject obj, jlong handle, jobject looper_obj_ref, int data_enabled, int event_enabled) {
	HANDLE hComm = (HANDLE)handle;
	HANDLE thread_handle;
	struct looper_thread_params params;
	unsigned thread_id;
	DWORD errorVal = 0;

	/* we make sure that thread creation and data passing is atomic. */
	EnterCriticalSection(&csmutex);

	jobject looper_ref = (*env)->NewGlobalRef(env, looper_obj_ref);
	if(looper_ref == NULL) {
		if(DEBUG) fprintf(stderr, "%s \n", "NATIVE setupLooperThread() failed to create global reference for looper object !");
		if(DEBUG) fflush(stderr);
		return -240;
	}

	/* Set the values that will be passed to data thread. */
	params.jvm = jvm;
	params.hComm = hComm;
	params.looper = looper_ref;
	params.data_enabled = data_enabled;
	params.event_enabled = event_enabled;
	params.thread_exit = 0;
	params.csmutex = &csmutex;           /* Same mutex is shared across all the threads. */

	params.wait_event_handles[0] = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(params.wait_event_handles[0] == NULL) {
		if(DEBUG) fprintf(stderr, "%s%d\n", "NATIVE setupLooperThread() failed to create wait event with error number : -", GetLastError());
		if(DEBUG) fprintf(stderr, "%s \n", "PLEASE TRY AGAIN !");
		if(DEBUG) fflush(stderr);
		return -240;
	}
	params.wait_event_handles[1] = 0;

	/* We have prepared data to be passed to thread, so create reference and pass it. */
	handle_looper_info[dtp_index] = params;
	void *arg = &handle_looper_info[dtp_index];

	/* Managed thread creation. The _beginthreadex initializes Certain CRT (C Run-Time) internals that ensures that other C functions will
	   work exactly as expected. */
	thread_handle = (HANDLE) _beginthreadex(NULL,   /* default security attributes */
					0,                              /* use default stack size      */
					&event_data_looper,             /* thread function name        */
					arg,                            /* argument to thread function */
					0,                              /* start thread immediately    */
					&thread_id);                    /* thread identifier           */
	if(thread_handle == 0) {
		if(DEBUG) fprintf(stderr, "%s%d\n", "NATIVE setupLooperThread() failed to create looper thread with error number : -", errno);
		if(DEBUG) fprintf(stderr, "%s \n", "PLEASE TRY AGAIN !");
		if(DEBUG) fflush(stderr);
		return -240;
	}

	/* Save the thread handle which will be used when listener is unregistered. */
	((struct looper_thread_params*) arg)->thread_handle = thread_handle;

	/* update address where data parameters for next thread will be stored. */
	dtp_index++;

	LeaveCriticalSection(&csmutex);
	return 0;
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    destroyDataLooperThread
 * Signature: (J)I
 * 
 * If a process attempts to change the device handle's event mask by using the SetCommMask function while an overlapped  WaitCommEvent operation is 
 * in progress, WaitCommEvent returns immediately. When WaitCommEvent returns, thread can check if it asked to exit. This is equivalent to 'evfd' 
 * concept used in Linux.
 */
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_destroyDataLooperThread(JNIEnv *env, jobject obj, jlong handle) {
	int ret = 0;
	int x = 0;
	DWORD a = 0;
	DWORD b = a & EV_CTS;
	HANDLE hComm = (HANDLE)handle;
	DWORD error_type = 0;
	COMSTAT com_stat;
	DWORD event_mask = 0;
	DWORD errorVal;
	struct looper_thread_params *ptr;

	/* handle_looper_info is global array holding all the information. */
	ptr = handle_looper_info;

	ret = GetCommMask(hComm, &event_mask);
	if(ret == 0) {
		errorVal = GetLastError();
		if(DEBUG) fprintf(stderr, "%s %ld\n", "NATIVE GetCommMask() failed in destroyDataLooperThread() with error number : ", errorVal);
		if(DEBUG) fflush(stderr);
		ClearCommError(hComm, &error_type, &com_stat);
		return 0; /* For unrecoverable errors we would like to exit and try again. */
	}

	if((b & event_mask) == 1) {
		/* Event listener exist, so just tell thread to wait for control events events only. */
		event_mask = 0;
		event_mask = event_mask | EV_BREAK | EV_CTS | EV_DSR | EV_ERR | EV_RING | EV_RLSD | EV_RXFLAG;
		ret = SetCommMask(hComm, event_mask);
		if(ret == 0) {
			errorVal = GetLastError();
			if(DEBUG) fprintf(stderr, "%s %ld\n", "NATIVE destroyDataLooperThread() failed in SetCommMask() with error number : ", errorVal);
			if(DEBUG) fprintf(stderr, "%s \n", "Try again !");
			if(DEBUG) fflush(stderr);
			ClearCommError(hComm, &error_type, &com_stat);
			return -240;
		}
	}else {
		/* Destroy thread as event listener does not exist and user wish to unregister data listener also. */
		for (x = 0; x < MAX_NUM_THREADS; x++) {
			if(ptr->hComm == hComm) {
				ptr->thread_exit = 1;
				break;
			}
			ptr++;
		}
	}

	/* This causes thread to come out of waiting state. */
	ret = SetEvent(ptr->wait_event_handles[0]);
	if(ret == 0) {
		errorVal = GetLastError();
		if(DEBUG) fprintf(stderr, "%s %ld\n", "NATIVE destroyDataLooperThread() failed in SetEvent() with error number : ", errorVal);
		if(DEBUG) fflush(stderr);
	}

	return 0;
}
/*
* Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
* Method:    destroyEventLooperThread
* Signature: (J)I
*/
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_destroyEventLooperThread(JNIEnv *env, jobject obj, jlong handle) {
	int ret = 0;
	int x = 0;
	DWORD a = 0;
	DWORD b = a & EV_RXCHAR;
	HANDLE hComm = (HANDLE)handle;
	DWORD error_type = 0;
	COMSTAT com_stat;
	DWORD event_mask = 0;
	ret = GetCommMask(hComm, &event_mask);
	struct looper_thread_params *ptr;
	ptr = handle_looper_info;
	DWORD errorVal = 0;

	if((b & event_mask) == 1) {
		/* Data listener exist, so just tell thread to wait for data events only. */
		event_mask = 0;
		event_mask = event_mask | EV_RXCHAR;
		ret = SetCommMask(hComm, event_mask);
		if(ret == 0) {
			errorVal = GetLastError();
			if(DEBUG) fprintf(stderr, "%s %ld\n", "NATIVE destroyEventLooperThread() failed in SetCommMask() with error number : ", errorVal);
			if(DEBUG) fprintf(stderr, "%s \n", "Try again !");
			if(DEBUG) fflush(stderr);
			ClearCommError(hComm, &error_type, &com_stat);
			return -240;
		}
	}else {
		/* Destroy thread as data listener does not exist and user wish to unregister event listener also. */
		for (x = 0; x < MAX_NUM_THREADS; x++) {
			if(ptr->hComm == hComm) {
				ptr->thread_exit = 1;
				break;
			}
			ptr++;
		}

		/* This causes thread to come out of waiting state. */
		ret = SetEvent(ptr->wait_event_handles[0]);
		if(ret == 0) {
			errorVal = GetLastError();
			if (DEBUG) fprintf(stderr, "%s %ld\n", "NATIVE destroyEventLooperThread() failed in SetEvent() with error number : ", errorVal);
			if (DEBUG) fflush(stderr);
		}
	}

	return 0;
}