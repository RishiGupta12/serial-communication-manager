package test2;

import com.embeddedunveiled.serial.SerialComManager;
import com.embeddedunveiled.serial.SerialComManager.BAUDRATE;
import com.embeddedunveiled.serial.SerialComManager.DATABITS;
import com.embeddedunveiled.serial.SerialComManager.FLOWCONTROL;
import com.embeddedunveiled.serial.SerialComManager.PARITY;
import com.embeddedunveiled.serial.SerialComManager.STOPBITS;

public class Test2 {
	public static void main(String[] args) {
		
		// get serial communication manager instance
		SerialComManager scm = new SerialComManager();
		
		String PORT = null;
		int osType = SerialComManager.getOSType();
		if(osType == SerialComManager.OS_LINUX) {
			PORT = "/dev/ttyUSB0";
		}else if(osType == SerialComManager.OS_WINDOWS) {
			PORT = "COM51";
		}else if(osType == SerialComManager.OS_MAC_OS_X) {
			PORT = "/dev/cu.usbserial-A70362A3";
		}else if(osType == SerialComManager.OS_SOLARIS) {
			PORT = null;
		}else{
		}
		
		try {
			// try opening serial port for read and write without exclusive ownership
			long handle = scm.openComPort(PORT, true, true, true);
			
			// configure data communication related parameters
			scm.configureComPortData(handle, DATABITS.DB_8, STOPBITS.SB_1, PARITY.P_NONE, BAUDRATE.B115200, 0);
			
			// configure line control related parameters
			scm.configureComPortControl(handle, FLOWCONTROL.NONE, 'x', 'x', false, false);
			
			// try to send data out of serial port
			if(scm.writeString(handle, "testing hello", 0) == true) {
				System.out.println("write success \n");
			}
		
			// try to read data from serial port
			String data = scm.readString(handle);
			System.out.println("data read is :" + data);

			// close serial port
			scm.closeComPort(handle);
			
		} catch (Exception e) {
			e.printStackTrace();
		}
	}
}